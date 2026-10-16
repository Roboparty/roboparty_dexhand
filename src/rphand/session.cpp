#include "session.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>

namespace rphand {
namespace {
using namespace std::chrono_literals;
void interval() { std::this_thread::sleep_for(5ms); }
}
Session::Session() {
    state_.axes.resize(6);
    for (auto& a : state_.axes) { a.home_function = 1; a.velocity = 20000; a.current = 400; }
}
Session::~Session() { close(); }
bool Session::open_transport(const std::string& interface, int node) {
    close();
    rx_failed_ = false;
    {
        std::lock_guard<std::mutex> lock(receive_mutex_);
        feedback_ = {}; snapshot_ = {}; samples_ = {}; track_home_ = false;
    }
    if (!transport_.open(interface, node)) return false;
    set_send_callback([this](unsigned id, const unsigned char* data, unsigned size, int extended) {
        return transport_.send(id, data, size, extended);
    });
    rx_done_ = false;
    try { receiver_ = std::thread([this] { receive_loop(); }); }
    catch (...) { close(); throw; }
    return true;
}
void Session::receive_loop() {
    while (!rx_done_) {
        canfd_frame frame{};
        const int result = transport_.receive(frame);
        if (result < 0) {
            std::lock_guard<std::mutex> lock(receive_mutex_);
            rx_failed_ = true;
            received_.notify_all();
            break;
        }
        if (result) set_canfd_data_decode(frame.can_id, frame.data, frame.len);
    }
}
Snapshot Session::snapshot(bool connected) const {
    std::lock_guard<std::mutex> lock(receive_mutex_);
    auto result = snapshot_;
    const auto now = Clock::now();
    result.state.feedback_valid = connected && !closed_ && !rx_failed_
        && result.sequence[0] && result.sequence[1]
        && now - result.last[0] < 100ms && now - result.last[1] < 100ms;
    return result;
}
bool Session::pause(std::chrono::milliseconds duration, const Cancel& cancelled) {
    std::unique_lock<std::mutex> lock(receive_mutex_);
    received_.wait_for(lock, duration, cancelled);
    return !cancelled();
}
void Session::wait_for_status(std::uint64_t sequence, Clock::time_point deadline, const Cancel& cancelled) {
    std::unique_lock<std::mutex> lock(receive_mutex_);
    received_.wait_until(lock, deadline, [&] {
        return cancelled() || rx_failed_ || snapshot_.sequence[1] > sequence;
    });
}
void Session::notify_waiters() {
    // Synchronize external cancellation with the condition-variable predicate.
    std::lock_guard<std::mutex> lock(receive_mutex_);
    received_.notify_all();
}
void Session::track_home(bool enabled) {
    std::lock_guard<std::mutex> lock(receive_mutex_);
    if (enabled) snapshot_.observed_home.fill(false);
    track_home_ = enabled;
}
void Session::clear_feedback_samples() {
    std::lock_guard<std::mutex> lock(receive_mutex_);
    for (auto& samples : samples_) samples.clear();
}
std::array<FeedbackRate, 2> Session::feedback_rates() const {
    std::lock_guard<std::mutex> lock(receive_mutex_);
    std::array<FeedbackRate, 2> rates{};
    for (int group = 0; group < 2; ++group) {
        const auto& times = samples_[group];
        auto& rate = rates[group];
        rate.count = times.size();
        if (times.empty()) continue;
        rate.age_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - times.back()).count();
        if (times.size() > 1)
            rate.hz = (times.size() - 1) / std::chrono::duration<double>(times.back() - times.front()).count();
        for (std::size_t i = 1; i < times.size(); ++i) rate.gap_ok &= times[i] - times[i-1] < 100ms;
    }
    return rates;
}
SdoResult Session::feedback_period(unsigned index, bool write, const Cancel& cancelled) {
    return transact_sdo(index, 0x14, write, write ? 200 : 0, 500, cancelled, true);
}
int Session::configure_feedback() {
    std::lock_guard<std::recursive_mutex> lock(command_mutex_);
    return emit(Command::Config);
}
int Session::recover_stop(int control) {
    std::lock_guard<std::recursive_mutex> lock(command_mutex_);
    return send(0x500, stop_recovery_payload(control));
}
void Session::set_send_callback(Send callback) {
    std::lock_guard<std::mutex> lock(receive_mutex_);
    callback_ = std::move(callback);
}
void Session::close() {
    { std::lock_guard<std::mutex> lock(receive_mutex_); closed_ = true; ready_ = false; }
    rx_done_ = true;
    received_.notify_all();
    if (receiver_.joinable()) receiver_.join();
    std::lock_guard<std::recursive_mutex> lock(command_mutex_);
    transport_.close();
}
int Session::send(unsigned base, const Bytes& bytes) {
    Send callback;
    { std::lock_guard<std::mutex> lock(receive_mutex_); callback = callback_; }
    if (closed_) return 2;
    try { return callback && callback(base + node_, bytes.data(), bytes.size(), 0) ? 0 : 6; }
    catch (...) { return 6; }
}
int Session::emit(Command command, unsigned char frame) {
    return send(0x500, command_payload(command, state_, frame));
}
int Session::read_sdo(unsigned index, unsigned sub, int* value, int timeout_ms) {
    if (!value) return 1;
    const auto result = transact_sdo(index, sub, false, 0, timeout_ms, {}, false);
    if (!result.code) *value = static_cast<int>(result.value);
    return result.code;
}
SdoResult Session::transact_sdo(unsigned index, unsigned sub, bool write, int value,
                                int timeout_ms, const Cancel& cancelled, bool configuration) {
    std::lock_guard<std::recursive_mutex> serial(command_mutex_);
    {
        std::lock_guard<std::mutex> lock(receive_mutex_);
        pending_index_ = index; pending_sub_ = sub; pending_ = true; answered_ = false;
        pending_write_ = write; pending_configuration_ = configuration; sdo_ = {};
    }
    const auto stopped = [&] { return cancelled && cancelled(); };
    int rc = stopped() ? 2 : send(0x600, sdo_payload(write ? SdoCommand::Write : SdoCommand::Read, index, sub, value));
    std::unique_lock<std::mutex> lock(receive_mutex_);
    if (!rc) {
        if (!received_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [&] {
            return answered_ || closed_ || stopped() || (configuration && rx_failed_);
        })) rc = 7;
        else if (closed_ || stopped()) rc = 2;
        else if (configuration && rx_failed_) rc = 6;
        else rc = sdo_.code;
    }
    pending_ = false;
    auto result = sdo_;
    result.code = rc;
    return result;
}
int Session::initial(int mode, int node) {
    std::lock_guard<std::recursive_mutex> serial(command_mutex_);
    if (mode != Canfd || node < 1 || node > 127) return 1;
    if (!closed_) return 1;
    node_ = node;
    {
        std::lock_guard<std::mutex> lock(receive_mutex_);
        feedback_ = {}; snapshot_ = {}; samples_ = {}; track_home_ = false;
        closed_ = false; ready_ = false;
    }
    state_ = CommandState{}; state_.axes.resize(6);
    for (auto& a : state_.axes) { a.home_function = 1; a.velocity = 20000; a.current = 400; }
    velocity_dirty_ = current_dirty_ = true; restore_pending_ = allow_no_home_ = false;
    int model = 0;
    int rc = read_sdo(0x200e, 0x1d, &model, 50);
    if (!rc && (model < 3 || model > 5)) rc = 3;
    if (rc) { closed_ = true; return rc; }
    // These metadata reads preserve the device startup sequence; only model detection is required.
    int metadata = 0;
    read_sdo(0x2030, 5, &metadata, 50);
    send(0x600, sdo_payload(SdoCommand::Read, 0x200e, 0x1e, 0));
    for (unsigned i = 0; i < 6 && !closed_; ++i) read_sdo(0x2030 + 0x40*i, 6, &metadata);
    rc = emit(Command::Config);
    {
        std::lock_guard<std::mutex> lock(receive_mutex_);
        if (closed_) rc = 2;
        ready_ = rc == 0;
        if (rc) closed_ = true;
    }
    return rc;
}
int Session::get_hand_type(int* value) const {
    if (!value) return 1;
    if (!ready_) return 2;
    *value = rphand; return 0;
}
int Session::get_dof(int* total, int* active) const {
    if (!total || !active) return 1;
    if (!ready_) return 2;
    *total = 11; *active = 6; return 0;
}
int Session::set_canfd_data_decode(unsigned id, const unsigned char* data, int size) {
    if (!data || size < 0 || size > 64) return 1;
    std::lock_guard<std::mutex> lock(receive_mutex_);
    if (closed_) return 2;
    if (id == 0x580u + node_) {
        SdoReply reply;
        if (!decode_sdo_reply(data, size, reply)) return 3;
        const bool matches = pending_ && !answered_ && reply.index == pending_index_ && reply.subindex == pending_sub_;
        // Startup/parameter registers are 16-bit; configuration retains full readback.
        if (reply.command == 0x43 && reply.value > 0xffff && !(matches && pending_configuration_)) return 3;
        if (matches) {
            const bool accepted = reply.command == 0x80 || (pending_write_ ? reply.command == 0x60
                : reply.command == 0x4b || reply.command == 0x43 || (!pending_configuration_ && reply.command == 0x4f));
            if (!accepted) return 0;
            sdo_ = {reply.command == 0x80 ? 7 : 0, reply.value, true, reply.command == 0x80};
            answered_ = true; received_.notify_all();
        }
        return 0;
    }
    if (id != 0x480u + node_ || size < 2) return 3;
    const unsigned type = data[0] & 0x7f;
    if (decode_motor_feedback(Bytes(data, data + size), kinematics_, feedback_) != DecodeResult::Ok) return 3;
    const int group = type == 0x50 ? 0 : 1;
    for (int i = 0; i < 6; ++i) {
        const auto& axis = feedback_[i];
        if (group == 0) {
            snapshot_.state.positions[i] = axis.position;
            snapshot_.state.velocities[i] = axis.position_velocity;
            snapshot_.state.currents[i] = axis.current;
        } else {
            const unsigned status = axis.status, running = axis.running_status();
            snapshot_.state.enabled[i] = status & 1;
            snapshot_.state.homed[i] = status & 0x10;
            snapshot_.state.torque_reached[i] = status & 8;
            snapshot_.state.fault_codes[i] = axis.alarm;
            snapshot_.stopped[i] = (status & 4) && running != 1 && running != 7;
            snapshot_.emergency_stopped[i] = running == 6;
            if (track_home_ && running == 7) snapshot_.observed_home[i] = true;
        }
    }
    const auto now = Clock::now();
    snapshot_.last[group] = now;
    ++snapshot_.sequence[group];
    samples_[group].push_back(now);
    if (samples_[group].size() > 256) samples_[group].pop_front();
    received_.notify_all();
    return 0;
}
int Session::get_feedback(int id, int* value, int FeedbackAxis::*field) const {
    if (!value || id < 1 || id > 6) return 1;
    std::lock_guard<std::mutex> lock(receive_mutex_);
    if (closed_) return 2;
    *value = feedback_[id-1].*field; return 0;
}
int Session::get_now_position(int id, int* v) const { return get_feedback(id, v, &FeedbackAxis::position); }
int Session::get_now_position_velocity(int id, int* v) const { return get_feedback(id, v, &FeedbackAxis::position_velocity); }
int Session::get_now_current(int id, int* v) const { return get_feedback(id, v, &FeedbackAxis::current); }
int Session::set_move_no_home(int enable) {
    std::lock_guard<std::recursive_mutex> lock(command_mutex_);
    if (enable != 0 && enable != 1) return 1;
    allow_no_home_ = enable; return 0;
}
int Session::control(int id, int mode, int command) {
    if (!valid_axis(id)) return 1;
    for (int i = 0; i < 6; ++i) if (!id || i == id-1) {
        state_.axes[i].mode = mode; state_.axes[i].control = command;
    }
    return emit(Command::Control);
}
int Session::set_enable(int id, int enable) {
    std::lock_guard<std::recursive_mutex> lock(command_mutex_);
    if (!ready_) return 2;
    int rc = control(id, 0, 0); if (rc) return rc;
    interval(); return control(id, 0, enable > 0 ? 1 : 2);
}
int Session::set_control_mode(int id, int mode) {
    std::lock_guard<std::recursive_mutex> lock(command_mutex_);
    if (!valid_axis(id) || mode != Position) return 1;
    if (!ready_) return 2;
    for (int i = 0; i < 6; ++i) if (!id || i == id-1) state_.axes[i].mode = mode;
    return emit(Command::Control);
}
int Session::stop_motors(int id) {
    std::lock_guard<std::recursive_mutex> lock(command_mutex_);
    if (!ready_) return 2;
    if (!valid_axis(id)) return 1;
    int rc = control(id, 0, 7);
    velocity_dirty_ = current_dirty_ = true;
    if (rc) return rc;
    std::this_thread::sleep_for(100ms);
    return control(id, 0, 8);
}
int Session::home_motors(int id) {
    std::lock_guard<std::recursive_mutex> lock(command_mutex_);
    if (!ready_) return 2;
    int rc = control(id, 5, 0); if (rc) return rc;
    interval(); set_target_position(id, 0);
    rc = control(id, 5, 4); if (rc) return rc;
    for (int i = 0; i < 6; ++i) if (!id || i == id-1) { state_.axes[i].mode = 0; state_.axes[i].control = 0; }
    velocity_dirty_ = current_dirty_ = restore_pending_ = true;
    return 0;
}
int Session::set_axis(int id, int value, int Axis::*field, bool* dirty) {
    std::lock_guard<std::recursive_mutex> lock(command_mutex_);
    if (!valid_axis(id)) return 1;
    if (!ready_) return 2;
    for (int i = 0; i < 6; ++i) if (!id || i == id-1) {
        if (dirty && state_.axes[i].*field != value) *dirty = true;
        state_.axes[i].*field = value;
    }
    return 0;
}
int Session::set_position_velocity(int id, int v) { return set_axis(id, v, &Axis::velocity, &velocity_dirty_); }
int Session::set_max_current(int id, int v) { return set_axis(id, v, &Axis::current, &current_dirty_); }
int Session::set_target_position(int id, int v) { return set_axis(id, std::clamp(v, 0, 10000), &Axis::position); }
int Session::get_axis(int id, int* value, int Axis::*field) const {
    if (!value || id < 1 || id > 6) return 1;
    std::lock_guard<std::recursive_mutex> lock(command_mutex_);
    if (!ready_) return 2;
    *value = state_.axes[id-1].*field; return 0;
}
int Session::get_position_velocity(int id, int* v) const { return get_axis(id, v, &Axis::velocity); }
int Session::get_max_current(int id, int* v) const { return get_axis(id, v, &Axis::current); }
int Session::set_safe_current_enable(int enable) {
    std::lock_guard<std::recursive_mutex> lock(command_mutex_);
    if (!ready_) return 2;
    if (enable != 0 && enable != 1) return 1;
    for (unsigned i = 0; i < 6; ++i) {
        int rc = send(0x600, sdo_payload(SdoCommand::Write, 0x2017 + 0x40*i, 1, enable));
        if (rc) return rc;
    }
    return 0;
}
int Session::get_safe_current_enable(int* value) {
    std::lock_guard<std::recursive_mutex> lock(command_mutex_);
    if (!ready_) return 2;
    return read_sdo(0x2017, 1, value);
}
int Session::apply_motion_params() {
    std::lock_guard<std::recursive_mutex> lock(command_mutex_);
    if (!ready_) return 2;
    if (restore_pending_) {
        int rc = control(0, 0, 0); if (rc) return rc;
        interval(); restore_pending_ = false;
    }
    if (velocity_dirty_) {
        int rc = emit(Command::Move, 3); if (rc) return rc;
        velocity_dirty_ = false; interval();
    }
    if (current_dirty_) {
        int rc = emit(Command::Move, 4); if (rc) return rc;
        current_dirty_ = false; interval();
    }
    return 0;
}
int Session::move_motors(int id) {
    std::lock_guard<std::recursive_mutex> lock(command_mutex_);
    if (!valid_axis(id)) return 1;
    if (!ready_) return 2;
    if (std::any_of(state_.axes.begin(), state_.axes.end(), [](const Axis& a) { return a.mode != 0; })) {
        int rc = control(0, 0, 0); if (rc) return rc;
        interval();
    }
    bool revoke = false, homed = true;
    {
        std::lock_guard<std::mutex> receive(receive_mutex_);
        for (int i = 0; i < 6; ++i) {
            revoke |= feedback_[i].running_status() == 6;
            if ((!id || i == id-1) && feedback_[i].seen && !(feedback_[i].status & 0x10)) homed = false;
        }
    }
    if (revoke) { int rc = control(0, 0, 8); if (rc) return rc; interval(); }
    if (!allow_no_home_ && !homed) return 11;
    int rc = apply_motion_params();
    return rc ? rc : emit(Command::Move, 2);
}
}

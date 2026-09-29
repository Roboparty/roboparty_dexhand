#include <dh116s/hand.hpp>
#include "socketcan_transport.hpp"
#include "feedback_format.hpp"
#include <LHandProLib/LHandProLib.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace dh116s {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
std::mutex owners_mutex, log_mutex;
std::set<std::pair<std::string, int>> owners;
template<class T> bool all(const T& values) {
    return std::all_of(values.begin(), values.end(), [](auto value) { return bool(value); });
}
bool fail(const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cerr << "[dh116s] " << message << '\n';
    return false;
}
struct Snapshot {
    HandState state;
    std::array<bool, 6> stopped{}, homing{}, observed_home{}, emergency_stopped{};
    std::array<Clock::time_point, 2> last{};
    std::array<std::uint64_t, 2> sequence{};
};
}

struct Hand::Impl {
    detail::SocketCan transport;
    std::unique_ptr<lhplib::LHandProLib> vendor;
    std::function<bool(unsigned, const unsigned char*, unsigned, int)> callback;
    std::atomic<bool> connected{false}, closing{false}, rx_done{false}, rx_failed{false};
    std::atomic<std::uint64_t> generation{0};
    std::thread worker, receiver;
    std::mutex lifecycle_mutex, queue_mutex;
    std::condition_variable queue_cv, feedback_cv;
    struct Job {
        std::function<bool(std::uint64_t)> call;
        std::promise<bool> result;
        std::uint64_t token;
        bool urgent;
    };
    std::deque<std::shared_ptr<Job>> queue;
    bool worker_exit = false, ordinary_busy = false;
    unsigned urgent_count = 0;
    mutable std::mutex feedback_mutex;
    Snapshot feedback;
    std::array<std::deque<Clock::time_point>, 2> samples;
    bool track_home = false;
    struct Sdo {
        bool pending = false, received = false, write = false, abort = false;
        unsigned index = 0;
        std::uint32_t value = 0;
    } sdo;
    std::string interface;
    int node = 0;
    bool owns_device = false;

    ~Impl() { disconnect(); }
    bool cancelled(std::uint64_t token) const { return closing || generation != token; }
    bool pause(std::uint64_t token, std::chrono::milliseconds duration) {
        std::unique_lock<std::mutex> lock(feedback_mutex);
        feedback_cv.wait_for(lock, duration, [&] { return cancelled(token); });
        return !cancelled(token);
    }
    Snapshot snapshot(bool initializing = false) const {
        std::lock_guard<std::mutex> lock(feedback_mutex);
        auto result = feedback;
        const auto now = Clock::now();
        result.state.feedback_valid = (connected || initializing) && !rx_failed && result.sequence[0] && result.sequence[1]
            && now - result.last[0] < 100ms && now - result.last[1] < 100ms;
        return result;
    }
    bool check(int rc, const char* operation) {
        return rc == 0 || fail(interface + " node=" + std::to_string(node) + " " + operation
                               + " vendor_rc=" + std::to_string(rc));
    }
    template<class F> bool command(const char* name, F call) {
        const auto before = transport.errors();
        const bool ok = check(call(), name);
        return ok && before == transport.errors();
    }
    void run() {
        for (;;) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [&] { return worker_exit || !queue.empty(); });
                if (worker_exit && queue.empty()) break;
                job = queue.front();
                queue.pop_front();
            }
            bool ok = false;
            try {
                if (!cancelled(job->token)) ok = job->call(job->token);
                else fail("operation cancelled before execution");
            } catch (const std::exception& e) {
                fail(std::string("operation failed: ") + e.what());
            } catch (...) { fail("unexpected vendor failure"); }
            {
                std::lock_guard<std::mutex> lock(queue_mutex);
                if (job->urgent) --urgent_count;
                else ordinary_busy = false;
            }
            job->result.set_value(ok);
        }
        cleanup();
    }
    bool submit(std::function<bool(std::uint64_t)> call, bool urgent = false) {
        auto job = std::make_shared<Job>();
        auto result = job->result.get_future();
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (!connected || closing || worker_exit) return fail("NotConnected/closing");
            if (!urgent && (ordinary_busy || urgent_count)) return fail("Busy: another operation is active");
            job->call = std::move(call);
            job->urgent = urgent;
            if (urgent) {
                job->token = ++generation;
                ++urgent_count;
                queue.push_front(job);
            } else {
                job->token = generation;
                ordinary_busy = true;
                queue.push_back(job);
            }
        }
        feedback_cv.notify_all();
        queue_cv.notify_one();
        return result.get();
    }
    void cleanup_attempt() {
        connected = false;
        // Stop decoding before close() can destroy its caches. The transport and
        // callback remain alive until all vendor background activity has ended.
        rx_done = true;
        if (receiver.joinable()) receiver.join();
        if (vendor) {
            vendor->close();
            vendor.reset();
        }
        callback = {};
        transport.close();
    }
    void cleanup() {
        cleanup_attempt();
        if (owns_device) {
            std::lock_guard<std::mutex> lock(owners_mutex);
            owners.erase({interface, node});
            owns_device = false;
        }
    }
    void disconnect(std::uint64_t expected_generation = 0) {
        std::lock_guard<std::mutex> lifecycle(lifecycle_mutex);
        if (expected_generation && generation != expected_generation) return;
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            closing = true;
            connected = false;
            ++generation;
            worker_exit = true;
        }
        feedback_cv.notify_all();
        queue_cv.notify_all();
        if (worker.joinable()) worker.join();
    }
    void receive_loop() {
        while (!rx_done) {
            canfd_frame frame{};
            const int result = transport.receive(frame);
            if (result < 0) { rx_failed = true; feedback_cv.notify_all(); break; }
            if (!result) continue;
            const unsigned id = frame.can_id;
            if (id == 0x580u + node && frame.len == 8) {
                bool managed = false;
                {
                    std::lock_guard<std::mutex> lock(feedback_mutex);
                    const unsigned index = unsigned(frame.data[1]) | (unsigned(frame.data[2]) << 8);
                    if (sdo.pending && index == sdo.index && frame.data[3] == 0x14) {
                        const unsigned cmd = frame.data[0];
                        if (cmd == 0x80 || (sdo.write ? cmd == 0x60 : (cmd == 0x4B || cmd == 0x43))) {
                            sdo.abort = cmd == 0x80;
                            sdo.value = std::uint32_t(frame.data[4]) | (std::uint32_t(frame.data[5]) << 8)
                                | (std::uint32_t(frame.data[6]) << 16) | (std::uint32_t(frame.data[7]) << 24);
                            if (cmd == 0x4B) sdo.value &= 0xffff;
                            sdo.received = true;
                            managed = true;
                        }
                    }
                }
                feedback_cv.notify_all();
                // Raw configuration transactions do not share the vendor SDO cache.
                if (managed || frame.data[0] == 0x60) continue;
            }
            if (id != 0x480u + node && id != 0x580u + node) continue;
            int group = -1;
            if (id == 0x480u + node && frame.len >= 2) {
                const unsigned type = frame.data[0] & 0x7f;
                if (type == 0x50 || type == 0x5a) {
                    if (!detail::valid_feedback(type, frame.data[1], frame.len)) continue;
                    group = type == 0x50 ? 0 : 1;
                }
            }
            if (vendor->set_canfd_data_decode(id, frame.data, frame.len) != 0 || group < 0) continue;
            HandState state;
            if (group == 0) {
                bool ok = true;
                for (int j = 0; j < 6; ++j) {
                    ok &= vendor->get_now_position(j + 1, &state.positions[j]) == 0;
                    ok &= vendor->get_now_position_velocity(j + 1, &state.velocities[j]) == 0;
                    ok &= vendor->get_now_current(j + 1, &state.currents[j]) == 0;
                }
                if (!ok) continue;
            }
            {
                std::lock_guard<std::mutex> lock(feedback_mutex);
                if (group == 0) {
                    feedback.state.positions = state.positions;
                    feedback.state.velocities = state.velocities;
                    feedback.state.currents = state.currents;
                } else {
                    for (int j = 0; j < 6; ++j) {
                        const unsigned status = frame.data[2 + 2*j], running = status >> 5;
                        feedback.state.enabled[j] = status & 1;
                        feedback.state.homed[j] = status & 0x10;
                        feedback.state.torque_reached[j] = status & 8;
                        feedback.state.fault_codes[j] = frame.data[3 + 2*j] & 0xf;
                        feedback.stopped[j] = (status & 4) && running != 1 && running != 7;
                        feedback.homing[j] = running == 7;
                        feedback.emergency_stopped[j] = running == 6;
                        if (track_home && running == 7) feedback.observed_home[j] = true;
                    }
                }
                const auto now = Clock::now();
                feedback.last[group] = now;
                ++feedback.sequence[group];
                samples[group].push_back(now);
                if (samples[group].size() > 256) samples[group].pop_front();
            }
            feedback_cv.notify_all();
        }
    }
    bool transaction(unsigned index, bool write, std::uint64_t token) {
        {
            std::lock_guard<std::mutex> lock(feedback_mutex);
            sdo = {};
            sdo.pending = true;
            sdo.index = index;
            sdo.write = write;
        }
        const unsigned char request[8] = {static_cast<unsigned char>(write ? 0x2B : 0x40),
            static_cast<unsigned char>(index), static_cast<unsigned char>(index >> 8),
            0x14, static_cast<unsigned char>(write ? 200 : 0), 0, 0, 0};
        const bool sent = !cancelled(token) && transport.send(0x600 + node, request, 8, 0);
        std::unique_lock<std::mutex> lock(feedback_mutex);
        if (sent) feedback_cv.wait_for(lock, 500ms, [&] { return sdo.received || cancelled(token) || rx_failed; });
        sdo.pending = false;
        const bool ok = sent && !cancelled(token) && !rx_failed && sdo.received && !sdo.abort
            && (write || sdo.value == 200);
        if (!ok) {
            std::lock_guard<std::mutex> log_lock(log_mutex);
            std::cerr << "[dh116s] SDO " << (write ? "write" : "read") << " index=0x"
                      << std::hex << index << " response=0x" << sdo.value << std::dec
                      << " received=" << sdo.received << " abort=" << sdo.abort << '\n';
        }
        return ok;
    }
    bool configure_feedback(std::uint64_t token) {
        vendor->stop_monitor();
        // Drain replies from the vendor monitor before owning the SDO channel.
        if (!pause(token, 100ms)) return false;
        for (unsigned index : {0x201du, 0x205du, 0x209du, 0x20ddu, 0x211du, 0x215du}) {
            if (!transaction(index, true, token) || !transaction(index, false, token)) return false;
        }
        const unsigned char config[] = {0, 4, 0x50, 1, 0x5a, 1};
        if (!transport.send(0x500 + node, config, sizeof(config), 0)) return false;
        {
            std::lock_guard<std::mutex> lock(feedback_mutex);
            for (auto& group : samples) group.clear();
        }
        if (!pause(token, 1200ms) || rx_failed) return false;
        std::lock_guard<std::mutex> lock(feedback_mutex);
        for (int group = 0; group < 2; ++group) {
            const auto& times = samples[group];
            if (times.size() < 40 || Clock::now() - times.back() >= 100ms) {
                const auto age = times.empty() ? -1 : std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - times.back()).count();
                return fail(interface + " node=" + std::to_string(node)
                    + (group == 0 ? " 0x50" : " 0x5A") + " feedback check failed: samples="
                    + std::to_string(times.size()) + " age_ms=" + std::to_string(age));
            }
            const double hz = (times.size() - 1) / std::chrono::duration<double>(times.back() - times.front()).count();
            bool gap_ok = true;
            for (std::size_t i = 1; i < times.size(); ++i) gap_ok &= times[i] - times[i-1] < 100ms;
            {
                std::lock_guard<std::mutex> log_lock(log_mutex);
                std::cerr << "[dh116s] " << interface << " node=" << node
                          << (group == 0 ? " 0x50=" : " 0x5A=") << hz << " Hz\n";
            }
            if (hz < 40 || hz > 60 || !gap_ok) return fail("feedback frequency outside 40..60 Hz or gap >=100ms");
        }
        return true;
    }
    bool open_device(std::uint64_t token) {
        constexpr int retries = 30;
        bool initialized = false;
        for (int attempt = 1; attempt <= retries && !cancelled(token); ++attempt) {
            {
                std::lock_guard<std::mutex> lock(feedback_mutex);
                feedback = {};
                samples = {};
                sdo = {};
                track_home = false;
            }
            rx_failed = false;
            rx_done = false;
            if (transport.open(interface, node)) {
                vendor = std::make_unique<lhplib::LHandProLib>();
                callback = [this](unsigned id, const unsigned char* data, unsigned size, int ext) {
                    return transport.send(id, data, size, ext);
                };
                vendor->set_send_canfd_callback_ex(&callback);
                receiver = std::thread([this] { receive_loop(); });
                int type = -1, total = 0, active = 0;
                initialized = command("initial", [&] { return vendor->initial(lhplib::LCN_CANFD, node); })
                    && check(vendor->get_hand_type(&type), "get_hand_type")
                    && check(vendor->get_dof(&total, &active), "get_dof")
                    && type == lhplib::LAC_DOF_6_S && active == 6 && !rx_failed;
                if (initialized) break;
                fail("initialization/model check failed: type=" + std::to_string(type) + " active=" + std::to_string(active));
            }
            cleanup_attempt();
            fail("connect initialization attempt=" + std::to_string(attempt));
            if (attempt < retries) pause(token, 100ms);
        }
        if (initialized) {
            for (int attempt = 1; attempt <= retries && !cancelled(token); ++attempt) {
                if (configure_feedback(token)) {
                    if (!recover_stop(token)) break;
                    connected = true;
                    if (!cancelled(token)) return true;
                    connected = false;
                    break;
                }
                fail("connect feedback configuration attempt=" + std::to_string(attempt));
                // CANopen SDO has no transaction ID. Drain late responses before
                // retrying the same index; readback and measured rate are required too.
                if (attempt < retries) pause(token, 500ms);
            }
        }
        cleanup();
        return fail("connect failed/cancelled; local resources released");
    }
    bool connect(const std::string& name, int id) {
        if (name.empty() || id < 1 || id > 127) return fail("invalid connect arguments");
        std::future<bool> result;
        std::uint64_t connect_token = 0;
        {
            std::lock_guard<std::mutex> lifecycle(lifecycle_mutex);
            std::lock_guard<std::mutex> lock(queue_mutex);
            if (worker.joinable()) return fail("already connected/connecting; disconnect before reconnecting");
            {
                std::lock_guard<std::mutex> owner_lock(owners_mutex);
                if (!owners.emplace(name, id).second) return fail("device already owned by this process");
                owns_device = true;
            }
            interface = name;
            node = id;
            closing = false;
            worker_exit = false;
            ordinary_busy = true;
            auto job = std::make_shared<Job>();
            job->token = ++generation;
            connect_token = job->token;
            job->urgent = false;
            job->call = [this](auto token) { return open_device(token); };
            result = job->result.get_future();
            queue.push_back(job);
            try { worker = std::thread([this] { run(); }); }
            catch (...) {
                queue.clear();
                ordinary_busy = false;
                cleanup();
                return fail("cannot start control worker");
            }
        }
        queue_cv.notify_one();
        const bool ok = result.get();
        if (!ok) disconnect(connect_token);
        return ok;
    }
    bool healthy(const Snapshot& value) {
        if (!value.state.feedback_valid) return fail("feedback missing/stale (100ms) or receive failed");
        for (int fault : value.state.fault_codes) if (fault) return fail("device fault; inspect get_state().fault_codes");
        return true;
    }
    // Waits run on the control worker, but priority submissions invalidate token
    // and wake this condition variable immediately, before their own job runs.
    bool confirm(std::uint64_t token, std::uint64_t baseline,
                 const std::function<bool(const Snapshot&)>& predicate, bool initializing = false) {
        const auto deadline = Clock::now() + 500ms;
        while (!cancelled(token) && Clock::now() < deadline) {
            auto value = snapshot(initializing);
            if (value.state.feedback_valid && value.sequence[1] > baseline && predicate(value)) return true;
            if (rx_failed) return false;
            std::unique_lock<std::mutex> lock(feedback_mutex);
            feedback_cv.wait_until(lock, deadline, [&] {
                return cancelled(token) || rx_failed || feedback.sequence[1] > value.sequence[1];
            });
        }
        return false;
    }
    bool recover_stop(std::uint64_t token) {
        auto any = [](const auto& values) {
            return std::any_of(values.begin(), values.end(), [](auto value) { return bool(value); });
        };
        auto value = snapshot(true);
        if (cancelled(token) || !healthy(value)) return false;
        if (!any(value.emergency_stopped)) return true;

        // Releasing a latched stop must not resume an enabled axis or old target.
        bool disabled = !any(value.state.enabled) && all(value.stopped);
        for (int attempt = 1; !disabled && attempt <= 30 && !cancelled(token); ++attempt) {
            value = snapshot(true);
            if (!healthy(value)) return false;
            const auto baseline = value.sequence[1];
            const bool sent = command("disable before stop recovery", [&] { return vendor->set_enable(0, 0); });
            disabled = sent && confirm(token, baseline, [&](const Snapshot& s) {
                return !any(s.state.enabled) && all(s.stopped);
            }, true);
        }
        if (!disabled) return fail("connect: disable before stop recovery not confirmed/cancelled");

        // The vendor public API exposes stop, but not revoke. Protocol 0x01
        // requires a control edge: idle (0), then revoke emergency stop (8).
        std::array<unsigned char, 14> frame{0x01, 6};
        for (int j = 0; j < 6; ++j) frame[2 + 2*j] = 0x20;
        for (int attempt = 1; attempt <= 30 && !cancelled(token); ++attempt) {
            value = snapshot(true);
            if (!healthy(value) || any(value.state.enabled) || !all(value.stopped))
                return fail("connect: stop recovery requires disabled, stationary axes");
            for (int j = 0; j < 6; ++j) frame[3 + 2*j] = 0;
            const bool idle_sent = transport.send(0x500 + node, frame.data(), frame.size(), 0);
            if (!pause(token, 20ms)) return false;
            value = snapshot(true);
            if (!healthy(value) || any(value.state.enabled) || !all(value.stopped)) return false;
            const auto baseline = value.sequence[1];
            for (int j = 0; j < 6; ++j) frame[3 + 2*j] = 8;
            const bool sent = idle_sent && !cancelled(token)
                && transport.send(0x500 + node, frame.data(), frame.size(), 0);
            if (sent && confirm(token, baseline, [&](const Snapshot& s) {
                return !any(s.emergency_stopped) && !any(s.state.enabled)
                    && all(s.stopped) && !any(s.state.fault_codes);
            }, true)) return true;
            fail(interface + " node=" + std::to_string(node)
                + " stop recovery attempt=" + std::to_string(attempt));
        }
        return fail("connect: stop recovery not confirmed/cancelled");
    }
    // mode: 0=enable, 1=disable, 2=stop.
    bool action(int mode, std::uint64_t token) {
        auto matches = [mode](const Snapshot& value) {
            if (mode == 2) return all(value.stopped);
            return std::all_of(value.state.enabled.begin(), value.state.enabled.end(),
                               [mode](bool enabled) { return enabled == (mode == 0); });
        };
        auto value = snapshot();
        if (mode == 0 && !healthy(value)) return false;
        if (value.state.feedback_valid && matches(value)) return true;
        if (mode == 0) {
            if (!all(value.stopped)) return fail("enable requires stationary axes before changing mode");
            if (!command("set_control_mode", [&] { return vendor->set_control_mode(0, lhplib::LCM_POSITION); })) return false;
        }
        for (int attempt = 1; attempt <= 30 && !cancelled(token); ++attempt) {
            const auto baseline = snapshot().sequence[1];
            const bool sent = mode == 2
                ? command("stop_motors", [&] { return vendor->stop_motors(0); })
                : command("set_enable", [&] { return vendor->set_enable(0, mode == 0 ? 1 : 0); });
            if (sent && confirm(token, baseline, matches)) return true;
            if (!sent && !pause(token, 100ms)) break;
        }
        return fail(std::string(mode == 0 ? "enable" : mode == 1 ? "disable" : "stop") + " not confirmed/cancelled");
    }
    bool home(std::uint64_t token) {
        {
            std::lock_guard<std::mutex> lock(feedback_mutex);
            feedback.observed_home.fill(false);
            track_home = true;
        }
        struct TrackingGuard {
            Impl* self;
            ~TrackingGuard() { std::lock_guard<std::mutex> lock(self->feedback_mutex); self->track_home = false; }
        } guard{this};
        if (cancelled(token)) return false;
        // A send failure can still be partial: keep observing before retrying.
        command("home_motors", [&] { return vendor->home_motors(0); });
        int attempts = 1;
        const auto deadline = Clock::now() + 60s;
        auto retry_at = Clock::now() + 1s;
        while (!cancelled(token) && Clock::now() < deadline) {
            const auto value = snapshot();
            if (!healthy(value) || !all(value.state.enabled)) return false;
            if (all(value.observed_home) && all(value.state.homed) && all(value.stopped)) {
                return !cancelled(token) && command("restore position mode", [&] { return vendor->set_control_mode(0, lhplib::LCM_POSITION); });
            }
            if (Clock::now() >= retry_at && !all(value.observed_home)) {
                if (cancelled(token)) return false;
                if (all(snapshot().observed_home)) continue;
                if (attempts >= 30) return fail("home start not observed for all axes after 30 whole-hand attempts");
                ++attempts;
                command("home_motors retry", [&] { return vendor->home_motors(0); });
                retry_at = Clock::now() + 1s;
            }
            pause(token, 10ms);
        }
        return fail("home timed out/cancelled; physical stop is not implied");
    }
    bool parameter(int kind, int value, std::uint64_t token) {
        for (int attempt = 0; attempt < 30 && !cancelled(token); ++attempt) {
            bool ok = command("set parameter", [&] {
                if (kind == 0) return vendor->set_position_velocity(0, value);
                if (kind == 1) return vendor->set_max_current(0, value);
                return vendor->set_safe_current_enable(value);
            });
            if (kind < 2) ok = command("apply_motion_params", [&] { return vendor->apply_motion_params(); }) && ok;
            for (int id = 1; id <= (kind == 2 ? 1 : 6); ++id) {
                int cached = -1;
                const int rc = kind == 0 ? vendor->get_position_velocity(id, &cached)
                    : kind == 1 ? vendor->get_max_current(id, &cached) : vendor->get_safe_current_enable(&cached);
                ok = check(rc, "get parameter cache") && cached == value && ok;
            }
            if (ok && !cancelled(token)) return true;
            if (attempt < 29) pause(token, 20ms);
        }
        return fail("parameter cache confirmation failed/cancelled");
    }
};

Hand::Hand() : impl_(std::make_unique<Impl>()) {}
Hand::~Hand() = default;
bool Hand::connect(const std::string& name, int id) { return impl_->connect(name, id); }
void Hand::disconnect() { impl_->disconnect(); }
bool Hand::is_connected() const { return impl_->connected && !impl_->closing; }
HandState Hand::get_state() const { return impl_->snapshot().state; }
bool Hand::enable() { return impl_->submit([this](auto token) { return impl_->action(0, token); }); }
bool Hand::disable() { return impl_->submit([this](auto token) { return impl_->action(1, token); }, true); }
bool Hand::stop() { return impl_->submit([this](auto token) { return impl_->action(2, token); }, true); }
bool Hand::home() { return impl_->submit([this](auto token) { return impl_->home(token); }); }
bool Hand::set_positions(const std::array<int, 6>& positions) {
    for (int value : positions) if (value < 0 || value > 10000) return fail("position outside 0..10000");
    return impl_->submit([this, positions](auto token) {
        for (int j = 0; j < 6; ++j) {
            if (impl_->cancelled(token) || !impl_->command("set_target_position", [&] {
                return impl_->vendor->set_target_position(j + 1, positions[j]);
            })) return false;
        }
        return !impl_->cancelled(token) && impl_->command("move_motors", [&] { return impl_->vendor->move_motors(0); });
    });
}
bool Hand::set_position(int id, int position) {
    if (id < 1 || id > 6 || position < 0 || position > 10000) return fail("invalid axis/position");
    return impl_->submit([this, id, position](auto token) {
        return impl_->command("set_target_position", [&] { return impl_->vendor->set_target_position(id, position); })
            && !impl_->cancelled(token) && impl_->command("move_motors", [&] { return impl_->vendor->move_motors(id); });
    });
}
bool Hand::set_speed(int speed) {
    if (speed <= 0) return fail("speed must be positive (stroke units/second)");
    return impl_->submit([this, speed](auto token) { return impl_->parameter(0, speed, token); });
}
bool Hand::set_current_limit(int current) {
    if (current < 0 || current > 3000) return fail("current limit outside 0..3000");
    return impl_->submit([this, current](auto token) { return impl_->parameter(1, current, token); });
}
bool Hand::set_torque_reached_mode(bool stop_on_reached) {
    return impl_->submit([this, stop_on_reached](auto token) { return impl_->parameter(2, stop_on_reached, token); });
}
}  // namespace dh116s

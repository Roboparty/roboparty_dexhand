#include "rphand/session.hpp"
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

using rphand::Bytes;
using rphand::Session;

inline void check(bool condition, const std::string& label) {
    if (!condition) throw std::runtime_error(label);
}
using Frame = std::tuple<unsigned, Bytes, int>;
struct Device {
    Session hand;
    Session::Send callback;
    std::vector<Frame> frames;
    int node = 7, model = 3, safe = 1, fail_at = -1;
    bool silence = false, abort = false;
    std::function<void(Bytes&)> alter_reply;
    Device() {
        callback = [this](unsigned id, const unsigned char* data, unsigned size, int flags) {
            frames.emplace_back(id, Bytes(data, data + size), flags);
            if (fail_at == int(frames.size())) return false;
            if (id == 0x600u + node && size == 8) {
                const unsigned index = data[1] | (unsigned(data[2]) << 8);
                if (data[0] == 0x2b && index == 0x2017) safe = data[4];
                if (data[0] == 0x40 && !silence) {
                    int value = index == 0x200e && data[3] == 0x1d ? model :
                        index == 0x2017 ? safe : 1;
                    Bytes reply{static_cast<unsigned char>(abort ? 0x80 : 0x4b), data[1], data[2], data[3],
                                static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8), 0, 0};
                    if (alter_reply) alter_reply(reply);
                    hand.set_canfd_data_decode(0x580 + node, reply.data(), reply.size());
                }
            }
            return true;
        };
        bind();
    }
    ~Device() { hand.close(); }
    void bind() { hand.set_send_callback(callback); }
    void reply(const Bytes& bytes) {
        hand.set_canfd_data_decode(0x580 + node, bytes.data(), bytes.size());
    }
    void feedback(const Bytes& bytes) {
        check(hand.set_canfd_data_decode(0x480 + node, bytes.data(), bytes.size()) == 0, "decode feedback");
    }
    int initial() { return hand.initial(Session::Canfd, node); }
    void clear() { frames.clear(); fail_at = -1; }
};


using namespace std::chrono_literals;

static void protocol_frames() {
    rphand::CommandState state;
    state.axes.resize(6);
    for (auto& axis : state.axes) {
        axis.home_function = 1; axis.control = 1;
        axis.position = 1234; axis.velocity = 20000; axis.current = 400;
    }
    using rphand::Command;
    using rphand::SdoCommand;
    check(rphand::command_payload(Command::Config, state, 2) ==
        Bytes{0, 4, 0x50, 1, 0x5a, 1}, "CAN FD feedback configuration bytes");
    check(rphand::command_payload(Command::Control, state, 2) ==
        Bytes{1, 6, 0x20, 1, 0x20, 1, 0x20, 1, 0x20, 1, 0x20, 1, 0x20, 1}, "six-axis control bytes");
    check(rphand::command_payload(Command::Move, state, 2) ==
        Bytes{2, 6, 0xd2, 4, 0xd2, 4, 0xd2, 4, 0xd2, 4, 0xd2, 4, 0xd2, 4}, "position bytes");
    check(rphand::command_payload(Command::Move, state, 3) ==
        Bytes{3, 6, 0xd0, 7, 0xd0, 7, 0xd0, 7, 0xd0, 7, 0xd0, 7, 0xd0, 7}, "velocity scaling bytes");
    check(rphand::command_payload(Command::Move, state, 4) ==
        Bytes{4, 6, 0x90, 1, 0x90, 1, 0x90, 1, 0x90, 1, 0x90, 1, 0x90, 1}, "current bytes");
    check(rphand::sdo_payload(SdoCommand::Read, 0x200e, 0x1d, 0) ==
        Bytes{0x40, 0x0e, 0x20, 0x1d, 0, 0, 0, 0}, "SDO read bytes");
    check(rphand::sdo_payload(SdoCommand::Write, 0x2017, 1, 1) ==
        Bytes{0x2b, 0x17, 0x20, 1, 1, 0, 0, 0}, "SDO write bytes");
    check(rphand::stop_recovery_payload(0) ==
        Bytes{1, 6, 0x20, 0, 0x20, 0, 0x20, 0, 0x20, 0, 0x20, 0, 0x20, 0}, "stop recovery idle bytes");
    check(rphand::stop_recovery_payload(8) ==
        Bytes{1, 6, 0x20, 8, 0x20, 8, 0x20, 8, 0x20, 8, 0x20, 8, 0x20, 8}, "stop recovery revoke bytes");
}

static void independent() {
    Device d;
    check(d.hand.initial(2, 7) == 1 && d.frames.empty(), "reject other transports");
    check(d.hand.initial(1, 0) == 1 && d.frames.empty(), "reject node zero");
    check(d.initial() == 0, "synchronous startup replies do not deadlock");
    check(d.frames.size() == 10, "model/direction/firmware/six serial/config sequence");
    int v = -1, active = 0;
    check(d.hand.get_hand_type(&v) == 0 && v == 2, "detected rphand");
    check(d.hand.get_dof(&v, &active) == 0 && v == 11 && active == 6, "six axes");
    check(d.hand.set_target_position(7, 1) == 1, "axis bounds");
    check(d.hand.get_now_position(0, &v) == 1 && d.hand.get_now_current(1, nullptr) == 1, "getter bounds");
    d.clear(); d.fail_at = 1;
    check(d.hand.apply_motion_params() == 6 && d.frames.size() == 1, "failed velocity stops current send");
    d.clear(); check(d.hand.apply_motion_params() == 0 && d.frames.size() == 2, "dirty parameter retry");
    check(std::get<1>(d.frames[0])[0] == 3 && std::get<1>(d.frames[1])[0] == 4, "velocity then current");
    d.clear(); check(d.hand.apply_motion_params() == 0 && d.frames.empty(), "clean parameters not resent");
    check(d.hand.home_motors(0) == 0, "home");
    d.clear(); d.fail_at = 1;
    check(d.hand.move_motors(0) == 6 && d.frames.size() == 1, "failed home restore blocks motion");
    d.clear(); check(d.hand.move_motors(0) == 0 && d.frames.size() == 4, "restore retry and full params before move");
    Bytes feedback(64); feedback[0] = 0x50; feedback[1] = 8;
    feedback[2] = 0xd2; feedback[3] = 4; feedback[4] = 0xff; feedback[5] = 0xff;
    feedback[6] = 0xfe; feedback[7] = 0xff;
    check(d.hand.set_canfd_data_decode(0x487, feedback.data(), 64) == 0, "full capacity feedback");
    check(d.hand.get_now_position(1, &v) == 0 && v == 1234, "position decode");
    check(d.hand.get_now_position_velocity(1, &v) == 0 && v == -10, "signed velocity decode");
    check(d.hand.get_now_current(1, &v) == 0 && v == -2, "signed current decode");
    check(d.hand.set_canfd_data_decode(0x487, nullptr, 64) == 1, "null feedback");
    d.clear(); check(d.hand.move_motors(1) == 11 && d.frames.empty(), "observed unhomed axis blocked");
    check(d.hand.set_move_no_home(1) == 0 && d.hand.move_motors(1) == 0, "SDK explicit no-home policy");
    d.clear(); check(d.hand.set_safe_current_enable(0) == 0 && d.frames.size() == 6, "six current-limit registers");
    check(d.hand.get_safe_current_enable(&v) == 0 && v == 0, "SDO readback");
    d.abort = true; check(d.hand.get_safe_current_enable(&v) == 7, "SDO abort propagates"); d.abort = false;
    for (unsigned char command : {0x43, 0x4f}) {
        d.alter_reply = [command](Bytes& p) { p[0] = command; };
        check(d.hand.get_safe_current_enable(&v) == 0 && v == 0, "expedited SDO reply variants");
    }
    d.alter_reply = {};
    // Wait for the callback to prove the read is pending before closing it.
    std::promise<void> sent;
    d.hand.set_send_callback([&](unsigned, const unsigned char*, unsigned, int) { sent.set_value(); return true; });
    auto waiting = std::async(std::launch::async, [&] { int x; return d.hand.get_safe_current_enable(&x); });
    sent.get_future().wait(); d.hand.close();
    check(waiting.get() == 2, "close cancels pending read");
    check(d.hand.move_motors(0) == 2, "closed cannot move");
    d.silence = false; d.bind(); check(d.initial() == 0, "reinitialize resets state");
    std::promise<Bytes> requested;
    d.hand.set_send_callback([&](unsigned, const unsigned char* p, unsigned n, int) {
        requested.set_value(Bytes(p, p+n)); return true;
    });
    auto async_read = std::async(std::launch::async, [&] {
        int x = -1; const int rc = d.hand.get_safe_current_enable(&x); return std::make_pair(rc, x);
    });
    auto response = requested.get_future().get(); response[0] = 0x4b; response[4] = 1;
    check(d.hand.set_canfd_data_decode(0x588, response.data(), 8) == 3, "SDO other node rejected");
    check(d.hand.set_canfd_data_decode(0x587, response.data(), 5) == 3, "short SDO rejected");
    check(d.hand.set_canfd_data_decode(0x587, response.data(), 8) == 0, "asynchronous SDO receive");
    const auto result = async_read.get();
    check(result.first == 0 && result.second == 1, "asynchronous SDO read result");
    check(d.hand.set_canfd_data_decode(0x587, response.data(), 8) == 0, "late duplicate has no pending request");
    d.bind();
    std::atomic<bool> done{false};
    auto receiver = std::async(std::launch::async, [&] {
        for (int i = 0; i < 2000; ++i) d.hand.set_canfd_data_decode(0x487, feedback.data(), 64);
        done = true;
    });
    while (!done) { d.hand.get_now_position(1, &v); d.hand.set_position_velocity(0, 1234); }
    receiver.get(); check(true, "concurrent feedback and setters");
    for (int model : {0, 1, 2, 6, 7, 9, 65535}) {
        Device other; other.model = model;
        check(other.initial() == 3 && other.frames.size() == 1, "reject unsupported detected model");
        check(other.hand.set_enable(0, 1) == 2, "failed initialization cannot enable");
    }
    for (int model : {3, 4, 5}) { Device valid; valid.model = model; check(valid.initial() == 0, "rphand model codes"); }
    Device no_reply; no_reply.silence = true;
    check(no_reply.initial() == 7 && no_reply.frames.size() == 1, "model detection required");
    Device startup;
    std::promise<void> configuring, resume;
    auto resume_future = resume.get_future();
    startup.hand.set_send_callback([&](unsigned id, const unsigned char* p, unsigned n, int flags) {
        if (id == 0x507 && p[0] == 0) { configuring.set_value(); resume_future.wait(); }
        return startup.callback(id, p, n, flags);
    });
    auto initializing = std::async(std::launch::async, [&] { return startup.initial(); });
    configuring.get_future().wait();
    auto closing = std::async(std::launch::async, [&] { startup.hand.close(); });
    Bytes ack{0x60, 0, 0, 0, 0, 0, 0, 0};
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (startup.hand.set_canfd_data_decode(0x587, ack.data(), 8) != 2 &&
           std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    resume.set_value(); closing.get();
    check(initializing.get() == 2 && startup.hand.get_hand_type(&v) == 2, "close during final startup send remains closed");
}

static void snapshots() {
    Device d;
    check(d.initial() == 0, "initialize simulated session");
    check(!d.hand.snapshot().state.feedback_valid, "no feedback is invalid");
    Bytes positions(64);
    positions[0] = 0xd0; positions[1] = 8;
    positions[2] = 0xd2; positions[3] = 4;
    positions[4] = 0xff; positions[5] = 0xff;
    positions[6] = 0xfe; positions[7] = 0xff;
    d.feedback(positions);
    auto s = d.hand.snapshot();
    check(!s.state.feedback_valid && s.sequence[0] == 1 && s.sequence[1] == 0, "both groups required");
    check(s.state.positions[0] == 1234 && s.state.velocities[0] == -10 && s.state.currents[0] == -2,
          "snapshot uses decoded position values");

    Bytes status(64);
    status[0] = 0xda; status[1] = 31;
    for (int i = 0; i < 6; ++i) status[2 + 2*i] = 0x15;
    d.feedback(status);
    s = d.hand.snapshot();
    check(s.state.feedback_valid && s.state.enabled[0] && s.state.homed[0] && s.stopped[0], "status snapshot");
    check(!d.hand.snapshot(false).state.feedback_valid, "public disconnected state remains invalid");
    // Position frames must not replace the separately sampled public status group.
    positions[50] = 0xd6;
    d.feedback(positions);
    s = d.hand.snapshot();
    check(s.state.enabled[0] && !s.emergency_stopped[0], "position status does not overwrite status snapshot");
    check(s.sequence[0] == 2 && s.sequence[1] == 1, "independent group sequences");

    const auto sequence = s.sequence[1];
    auto waiting = std::async(std::launch::async, [&] {
        d.hand.wait_for_status(sequence, rphand::Clock::now() + 1s, [] { return false; });
    });
    d.hand.track_home(true);
    status[2] = 0xe1;
    d.feedback(status);
    waiting.get();
    check(d.hand.snapshot().observed_home[0], "track current homing start");
    d.hand.track_home(true);
    check(!d.hand.snapshot().observed_home[0], "new home tracking clears old observations");
    d.hand.track_home(false);
    d.feedback(status);
    check(!d.hand.snapshot().observed_home[0], "disabled tracking ignores homing status");

    const auto before = d.hand.snapshot().sequence;
    check(d.hand.set_canfd_data_decode(0x488, positions.data(), positions.size()) == 3, "reject other node");
    check(d.hand.set_canfd_data_decode(0x487, positions.data(), 48) == 3, "reject truncated feedback");
    check(d.hand.snapshot().sequence == before, "rejected frames cannot refresh feedback");
    const auto rates = d.hand.feedback_rates();
    check(rates[0].count == 2 && rates[1].count == 3, "sample counters follow each group");
    d.hand.clear_feedback_samples();
    check(d.hand.feedback_rates()[0].count == 0 && d.hand.snapshot().sequence == before,
          "frequency measurement reset preserves feedback");
    std::this_thread::sleep_for(110ms);
    d.feedback(positions);
    check(!d.hand.snapshot().state.feedback_valid, "one stale group invalidates fresh position feedback");
    d.feedback(status);
    check(d.hand.snapshot().state.feedback_valid, "fresh groups restore validity");
    d.hand.close();
    check(!d.hand.snapshot().state.feedback_valid, "close invalidates cached feedback");
}

static void sdo_transactions() {
    Device d;
    check(d.initial() == 0, "initialize simulated session");
    d.hand.set_send_callback([&](unsigned id, const unsigned char* p, unsigned size, int) {
        check(id == 0x607 && size == 8, "SDO request destination and size");
        if (p[0] == 0x2b) {
            check(p[3] == 0x14 && p[4] == 200 && p[5] == 0, "feedback period write bytes");
            d.reply({0x60, p[1], p[2], p[3], 0, 0, 0, 0});
        } else d.reply({0x43, p[1], p[2], p[3], 200, 0, 1, 0});
        return true;
    });
    const auto never = [] { return false; };
    auto result = d.hand.feedback_period(0x201d, true, never);
    check(result.code == 0 && result.received && !result.aborted, "inline write acknowledgement");
    result = d.hand.feedback_period(0x201d, false, never);
    check(result.code == 0 && result.value == 65736, "configuration preserves full 32-bit readback");

    d.hand.set_send_callback([&](unsigned, const unsigned char* p, unsigned, int) {
        d.reply({0x80, p[1], p[2], p[3], 0, 0, 2, 6});
        return true;
    });
    result = d.hand.feedback_period(0x201d, false, never);
    check(result.code == 7 && result.aborted && result.received && result.value == 0x06020000,
          "device abort remains distinct in transaction details");

    std::promise<void> requested;
    d.hand.set_send_callback([&](unsigned, const unsigned char* p, unsigned, int) {
        if (p[3] == 0x14) requested.set_value();
        else d.reply({0x4b, p[1], p[2], p[3], 1, 0, 0, 0});
        return true;
    });
    auto first = std::async(std::launch::async, [&] { return d.hand.feedback_period(0x201d, false, never); });
    requested.get_future().wait();
    auto second = std::async(std::launch::async, [&] {
        int value = 0;
        const int rc = d.hand.get_safe_current_enable(&value);
        return rc == 0 && value == 1;
    });
    d.reply({0x4b, 0x1d, 0x20, 0x15, 200, 0, 0, 0});
    d.reply({0x4b, 0x5d, 0x20, 0x14, 200, 0, 0, 0});
    d.reply({0x60, 0x1d, 0x20, 0x14, 0, 0, 0, 0});
    d.reply({0x4f, 0x1d, 0x20, 0x14, 200, 0, 0, 0});
    check(first.wait_for(20ms) == std::future_status::timeout, "wrong index/subindex/kind cannot complete read");
    d.reply({0x4b, 0x1d, 0x20, 0x14, 200, 0, 0, 0});
    result = first.get();
    check(result.code == 0 && result.value == 200 && second.get(), "configuration and parameter reads serialize");

    std::promise<void> pending;
    std::atomic<bool> cancelled{false};
    d.hand.set_send_callback([&](unsigned, const unsigned char*, unsigned, int) { pending.set_value(); return true; });
    auto cancelling = std::async(std::launch::async, [&] {
        return d.hand.feedback_period(0x201d, false, [&] { return cancelled.load(); });
    });
    pending.get_future().wait();
    cancelled = true;
    d.hand.notify_waiters();
    check(cancelling.get().code == 2, "external cancellation wakes SDO wait");

    unsigned sends = 0;
    d.hand.set_send_callback([&](unsigned, const unsigned char*, unsigned, int) { ++sends; return false; });
    check(d.hand.feedback_period(0x201d, true, never).code == 6 && sends == 1, "send failure does not retry in Session");
    d.hand.set_send_callback([&](unsigned, const unsigned char*, unsigned, int) { ++sends; return true; });
    result = d.hand.feedback_period(0x201d, true, never);
    check(result.code == 7 && !result.received && sends == 2, "timeout does not retry in Session");
}

int main() {
    try {
        protocol_frames();
        independent();
        snapshots();
        sdo_transactions();
        std::cout << "Protocol, session state and transactions passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

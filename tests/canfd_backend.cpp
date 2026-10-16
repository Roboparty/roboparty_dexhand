#include "session_device.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

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
    check(d.hand.set_canfd_data_decode(0x488, feedback.data(), 64) != 0, "other node ignored");
    check(d.hand.set_canfd_data_decode(0x487, feedback.data(), 48) != 0, "truncated feedback ignored");
    check(d.hand.set_canfd_data_decode(0x487, nullptr, 64) == 1, "null feedback");
    d.clear(); check(d.hand.move_motors(1) == 11 && d.frames.empty(), "observed unhomed axis blocked");
    check(d.hand.set_move_no_home(1) == 0 && d.hand.move_motors(1) == 0, "SDK explicit no-home policy");
    d.clear(); check(d.hand.set_safe_current_enable(0) == 0 && d.frames.size() == 6, "six current-limit registers");
    check(d.hand.get_safe_current_enable(&v) == 0 && v == 0, "SDO readback");
    d.abort = true; check(d.hand.get_safe_current_enable(&v) == 7, "SDO abort propagates"); d.abort = false;
    d.alter_reply = [](Bytes& p) { ++p[3]; };
    check(d.hand.get_safe_current_enable(&v) == 7, "mismatched subindex times out");
    d.alter_reply = [](Bytes& p) { ++p[1]; };
    check(d.hand.get_safe_current_enable(&v) == 7, "mismatched index times out");
    d.alter_reply = {};
    for (unsigned char command : {0x43, 0x4f}) {
        d.alter_reply = [command](Bytes& p) { p[0] = command; };
        check(d.hand.get_safe_current_enable(&v) == 0 && v == 0, "expedited SDO reply variants");
    }
    d.alter_reply = {};
    d.silence = true;
    check(d.hand.get_safe_current_enable(&v) == 7, "missing response times out");
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

int main() {
    try {
        protocol_frames();
        independent();
        std::cout << "CANFD backend checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

#include "session_device.hpp"
#include <atomic>
#include <future>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;

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
        snapshots();
        sdo_transactions();
        std::cout << "Session snapshot, SDO and cancellation checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

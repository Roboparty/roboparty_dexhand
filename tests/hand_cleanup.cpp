#include <rphand/hand.hpp>
#include "rphand/transport.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
namespace {
using Clock = std::chrono::steady_clock;
struct Device {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<rphand::detail::CanFrame> replies;
    Clock::time_point next_feedback;
    unsigned node = 1, stops = 0, disables = 0;
    unsigned char status = 0x15;
    bool open = false, respond = true, fail_stop = false;

    void scenario(unsigned char next_status, bool acknowledge, bool fail = false) {
        std::lock_guard<std::mutex> lock(mutex);
        status = next_status;
        respond = acknowledge;
        fail_stop = fail;
        stops = disables = 0;
        replies.clear();
        next_feedback = Clock::now();
        changed.notify_all();
    }
    void feedback() {
        rphand::detail::CanFrame position{}, state{};
        position.can_id = state.can_id = 0x480 + node;
        position.len = 64;
        position.data[0] = 0x50;
        position.data[1] = 8;
        state.len = 16;
        state.data[0] = 0x5a;
        state.data[1] = 6;
        for (int axis = 0; axis < 6; ++axis) {
            position.data[50 + axis] = status;
            state.data[2 + 2 * axis] = status;
        }
        replies.push_back(position);
        replies.push_back(state);
    }
} device;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void counts(unsigned stops, unsigned disables) {
    std::lock_guard<std::mutex> lock(device.mutex);
    check(device.stops == stops, "unexpected number of stop attempts");
    check(device.disables == disables, "unexpected number of disable attempts");
}
void settle() {
    // Allow the receiver to consume the new scenario's two feedback groups.
    std::this_thread::sleep_for(60ms);
}
}

// This target supplies a simulated transport factory; no physical I/O is linked.
namespace rphand::detail {
class MockTransport final : public CanTransport {
public:
    ~MockTransport() override { close(); }
    bool open(const ConnectionOptions&) override;
    void close() override;
    bool send(unsigned, const unsigned char*, unsigned, int) override;
    int receive(CanFrame&) override;
};
std::unique_ptr<CanTransport> make_transport(Backend) { return std::make_unique<MockTransport>(); }
bool MockTransport::open(const ConnectionOptions& options) {
    const unsigned node = options.node_id;
    std::lock_guard<std::mutex> lock(device.mutex);
    device.node = node;
    device.open = true;
    device.next_feedback = Clock::now();
    errors_ = 0;
    return true;
}
void MockTransport::close() {
    std::lock_guard<std::mutex> lock(device.mutex);
    device.open = false;
    device.replies.clear();
    device.changed.notify_all();
}
bool MockTransport::send(unsigned id, const unsigned char* data, unsigned size, int) {
    std::lock_guard<std::mutex> lock(device.mutex);
    if (!device.open) return false;
    if (id == 0x600 + device.node && size == 8) {
        rphand::detail::CanFrame reply{};
        reply.can_id = 0x580 + device.node;
        reply.len = 8;
        std::copy(data, data + 4, reply.data);
        reply.data[0] = data[0] == 0x40 ? 0x4b : 0x60;
        const unsigned index = data[1] | (unsigned(data[2]) << 8);
        reply.data[4] = index == 0x200e ? 3 : data[3] == 0x14 ? 200 : 1;
        device.replies.push_back(reply);
    } else if (id == 0x500 + device.node && size >= 14 && data[0] == 1) {
        if (data[3] == 7) {
            ++device.stops;
            if (device.fail_stop) { ++errors_; return false; }
            if (device.respond) device.status = 0x15;
        } else if (data[3] == 2) {
            ++device.disables;
            if (device.respond) device.status = 0x14;
        }
    }
    device.changed.notify_all();
    return true;
}
int MockTransport::receive(rphand::detail::CanFrame& frame) {
    std::unique_lock<std::mutex> lock(device.mutex);
    const auto deadline = Clock::now() + 25ms;
    while (device.open) {
        if (Clock::now() >= device.next_feedback) {
            device.feedback();
            // Emulate the hardware clock, not the host scheduler's wake-up time.
            device.next_feedback += 20ms;
        }
        if (!device.replies.empty()) {
            frame = device.replies.front();
            device.replies.pop_front();
            return 1;
        }
        if (Clock::now() >= deadline) return 0;
        device.changed.wait_until(lock, std::min(deadline, device.next_feedback));
    }
    return 0;
}
}

int main() {
    try {
        rphand::Hand hand;
        check(hand.connect("simulated", 1), "simulated connection failed");
        check(hand.stop(), "stationary feedback should satisfy stop");
        counts(0, 0);

        device.scenario(0x31, false);
        settle();
        check(!hand.prepare_disconnect(), "unconfirmed cleanup must fail");
        counts(1, 1);
        check(hand.is_connected(), "cleanup preparation must not disconnect");

        device.scenario(0x31, true, true);
        settle();
        check(!hand.prepare_disconnect(), "failed stop must fail combined cleanup");
        counts(1, 1);
        check(!hand.get_state().enabled[0], "disable must still execute after stop failure");

        device.scenario(0x31, true, true);
        settle();
        check(!hand.stop(), "failed ordinary stop must report failure");
        counts(30, 0);

        device.scenario(0x31, true);
        settle();
        check(hand.prepare_disconnect(), "acknowledged cleanup should succeed");
        counts(1, 1);
        check(hand.is_connected(), "successful cleanup must leave disconnect to caller");

        device.scenario(0x14, true);
        settle();
        check(hand.prepare_disconnect(), "already stopped and disabled should succeed");
        counts(0, 0);
        hand.disconnect();
        check(!hand.prepare_disconnect(), "offline cleanup cannot confirm state");
        std::cout << "Hand cleanup and retry checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

#include <rphand/hand.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <csignal>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr double thumb_seconds = 2, wave_seconds = 6;
constexpr double round_seconds = 2 * thumb_seconds + wave_seconds;
constexpr double pi = 3.14159265358979323846;
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool> interrupted{false};
void signal_handler(int) { interrupted.store(true, std::memory_order_relaxed); }

struct Options {
    std::string side = "left";
    int amplitude = 6000;
};
const char* help =
    "Usage: rphand_wave_demo [--side left|right] [--amplitude 6000]\n"
    "Left: can0/node 1; right: can3/node 1. Targets: 50 Hz.\n"
    "Enables and homes the hand, then repeats until Ctrl+C:\n"
    "thumb 1 full stroke (2 s), thumb 2 full stroke (2 s), four-finger wave (6 s).\n"
    "Stages advance by time without waiting for arrival.\n";

bool parse(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (i + 1 >= argc) return false;
        const std::string value = argv[++i];
        if (arg == "--side") {
            if (value != "left" && value != "right") return false;
            options.side = value;
        } else if (arg == "--amplitude") {
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), options.amplitude);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
                || options.amplitude < 1 || options.amplitude > 10000) return false;
        } else return false;
    }
    return true;
}

std::array<int, 6> target(double seconds, int amplitude) {
    const double t = std::fmod(seconds, round_seconds);
    std::array<int, 6> values{};
    if (t < 2 * thumb_seconds) {
        const int axis = t < thumb_seconds ? 0 : 1;
        const double phase = 2 * pi * (t - axis * thumb_seconds) / thumb_seconds;
        values[axis] = static_cast<int>(std::lround(5000 * (1 - std::cos(phase))));
    } else {
        const double local = t - 2 * thumb_seconds;
        const double edge = std::clamp(std::min(local, wave_seconds - local), 0.0, 1.0);
        const double envelope = (1 - std::cos(pi * edge)) / 2;
        for (int j = 0; j < 4; ++j)
            values[j + 2] = static_cast<int>(std::lround(amplitude * envelope
                * (1 - std::cos(2 * pi * local + j * pi / 2)) / 2));
    }
    return values;
}
bool healthy(const rphand::HandState& state) {
    return state.feedback_valid
        && std::all_of(state.enabled.begin(), state.enabled.end(), [](bool v) { return v; })
        && std::all_of(state.homed.begin(), state.homed.end(), [](bool v) { return v; })
        && std::all_of(state.fault_codes.begin(), state.fault_codes.end(), [](int v) { return v == 0; });
}
template<class F> void perform(const char* name, F call) {
    std::cout << name << "..." << std::endl;
    if (!call()) throw std::runtime_error(std::string(name) + " failed");
}
bool close_hand(rphand::Hand& hand) {
    bool ok = true;
    try { if (hand.is_connected()) ok = hand.disable(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; ok = false; }
    hand.disconnect();
    return ok;
}

void run(rphand::Hand& hand, const Options& options) {
    // Keep signal handling responsive while synchronous initialization is running.
    auto initialization = std::async(std::launch::async, [&] {
        auto step = [&](const char* name, auto call) {
            if (interrupted) throw std::runtime_error("Initialization interrupted");
            perform(name, call);
        };
        step("Connect", [&] { return hand.connect(options.side == "left" ? "can0" : "can3", 1); });
        step("Disable", [&] { return hand.disable(); });
        step("Set speed", [&] { return hand.set_speed(16000); });
        step("Set current limit", [&] { return hand.set_current_limit(400); });
        step("Set torque mode", [&] { return hand.set_torque_reached_mode(false); });
        step("Enable", [&] { return hand.enable(); });
        step("Home", [&] { return hand.home(); });
    });
    while (initialization.wait_for(20ms) != std::future_status::ready) {
        if (interrupted) { close_hand(hand); break; }
    }
    initialization.get();
    if (interrupted) return;
    std::cout << "Repeating thumb 1 -> thumb 2 -> four-finger wave; Ctrl+C to exit.\n";
    const auto start = Clock::now();
    auto next = start;
    while (!interrupted) {
        if (!healthy(hand.get_state())) throw std::runtime_error("Invalid/disabled/unhomed/fault feedback");
        const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
        if (!hand.set_positions(target(seconds, options.amplitude))) throw std::runtime_error("Position send failed");
        next = std::max(next + 20ms, Clock::now());
        std::this_thread::sleep_until(next);
    }
}
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--help") { std::cout << help; return 0; }
    Options options;
    if (!parse(argc, argv, options)) { std::cerr << help; return 2; }
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    rphand::Hand hand;
    int result = 0;
    try { run(hand, options); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; result = 1; }
    std::cout << "Disable -> disconnect" << std::endl;
    if (!close_hand(hand)) { std::cerr << "Disable unconfirmed.\n"; result = 1; }
    return interrupted ? 130 : result;
}

#include <rphand/hand.hpp>
#include "rphand/session.hpp"

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

namespace rphand {
namespace {
using namespace std::chrono_literals;
std::mutex owners_mutex, log_mutex;
std::set<std::pair<std::string, int>> owners;
template<class T> bool all(const T& values) {
    return std::all_of(values.begin(), values.end(), [](auto value) { return bool(value); });
}
bool fail(const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::cerr << "[rphand] " << message << '\n';
    return false;
}
}

struct Hand::Impl {
    Session session;
    std::atomic<bool> connected{false}, closing{false};
    std::atomic<std::uint64_t> generation{0};
    std::thread worker;
    std::mutex lifecycle_mutex, queue_mutex;
    std::condition_variable queue_cv;
    struct Job {
        std::function<bool(std::uint64_t)> call;
        std::promise<bool> result;
        std::uint64_t token;
        bool urgent;
    };
    std::deque<std::shared_ptr<Job>> queue;
    bool worker_exit = false, ordinary_busy = false;
    unsigned urgent_count = 0;
    std::string interface;
    int node = 0;
    bool owns_device = false;

    ~Impl() { disconnect(); }
    bool cancelled(std::uint64_t token) const { return closing || generation != token; }
    bool pause(std::uint64_t token, std::chrono::milliseconds duration) {
        return session.pause(duration, [&] { return cancelled(token); });
    }
    Snapshot snapshot(bool initializing = false) const {
        return session.snapshot(connected || initializing);
    }
    bool check(int rc, const char* operation) {
        return rc == 0 || fail(interface + " node=" + std::to_string(node) + " " + operation
                               + " backend_rc=" + std::to_string(rc));
    }
    template<class F> bool command(const char* name, F call) {
        const auto before = session.send_errors();
        const bool ok = check(call(), name);
        return ok && before == session.send_errors();
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
            } catch (...) { fail("unexpected backend failure"); }
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
        session.notify_waiters();
        queue_cv.notify_one();
        return result.get();
    }
    void cleanup_attempt() {
        connected = false;
        session.close();
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
        session.notify_waiters();
        queue_cv.notify_all();
        if (worker.joinable()) worker.join();
    }
    bool transaction(unsigned index, bool write, std::uint64_t token) {
        const auto response = session.feedback_period(index, write, [&] { return cancelled(token); });
        const bool ok = response.code == 0 && !cancelled(token) && !session.receive_failed()
            && response.received && !response.aborted && (write || response.value == 200);
        if (!ok) {
            std::lock_guard<std::mutex> log_lock(log_mutex);
            std::cerr << "[rphand] SDO " << (write ? "write" : "read") << " index=0x"
                      << std::hex << index << " response=0x" << response.value << std::dec
                      << " received=" << response.received << " abort=" << response.aborted << '\n';
        }
        return ok;
    }
    bool configure_feedback(std::uint64_t token) {
        // Allow late startup replies to drain before configuring feedback.
        if (!pause(token, 100ms)) return false;
        for (unsigned index : {0x201du, 0x205du, 0x209du, 0x20ddu, 0x211du, 0x215du}) {
            if (!transaction(index, true, token) || !transaction(index, false, token)) return false;
        }
        if (session.configure_feedback() != 0) return false;
        session.clear_feedback_samples();
        if (!pause(token, 1200ms) || session.receive_failed()) return false;
        const auto rates = session.feedback_rates();
        for (int group = 0; group < 2; ++group) {
            const auto& rate = rates[group];
            if (rate.count < 40 || rate.age_ms >= 100) {
                return fail(interface + " node=" + std::to_string(node)
                    + (group == 0 ? " 0x50" : " 0x5A") + " feedback check failed: samples="
                    + std::to_string(rate.count) + " age_ms=" + std::to_string(rate.age_ms));
            }
            {
                std::lock_guard<std::mutex> log_lock(log_mutex);
                std::cerr << "[rphand] " << interface << " node=" << node
                          << (group == 0 ? " 0x50=" : " 0x5A=") << rate.hz << " Hz\n";
            }
            if (rate.hz < 40 || rate.hz > 60 || !rate.gap_ok)
                return fail("feedback frequency outside 40..60 Hz or gap >=100ms");
        }
        return true;
    }
    bool configure_move_no_home(std::uint64_t token) {
        // The backend API has no getter for this flag; success is SDK-confirmed only.
        for (int attempt = 1; attempt <= 30 && !cancelled(token) && !session.receive_failed(); ++attempt) {
            if (command("set_move_no_home(1)", [&] { return session.set_move_no_home(1); })) {
                return !cancelled(token) && !session.receive_failed();
            }
            fail("set_move_no_home(1) attempt=" + std::to_string(attempt));
            if (attempt < 30 && !session.receive_failed() && !pause(token, 100ms)) break;
        }
        return fail("set_move_no_home(1) failed/cancelled");
    }
    bool open_device(std::uint64_t token) {
        constexpr int retries = 30;
        bool initialized = false;
        for (int attempt = 1; attempt <= retries && !cancelled(token); ++attempt) {
            if (session.open_transport(interface, node)) {
                int type = -1, total = 0, active = 0;
                initialized = command("initial", [&] { return session.initial(Session::Canfd, node); })
                    && check(session.get_hand_type(&type), "get_hand_type")
                    && check(session.get_dof(&total, &active), "get_dof")
                    && type == Session::rphand && active == 6 && !session.receive_failed();
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
                    if (!configure_move_no_home(token)) break;
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
            if (session.receive_failed()) return false;
            session.wait_for_status(value.sequence[1], deadline, [&] { return cancelled(token); });
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
            const bool sent = command("disable before stop recovery", [&] { return session.set_enable(0, 0); });
            disabled = sent && confirm(token, baseline, [&](const Snapshot& s) {
                return !any(s.state.enabled) && all(s.stopped);
            }, true);
        }
        if (!disabled) return fail("connect: disable before stop recovery not confirmed/cancelled");

        // Recovery requires an idle edge before revoking emergency stop.
        for (int attempt = 1; attempt <= 30 && !cancelled(token); ++attempt) {
            value = snapshot(true);
            if (!healthy(value) || any(value.state.enabled) || !all(value.stopped))
                return fail("connect: stop recovery requires disabled, stationary axes");
            const bool idle_sent = session.recover_stop(0) == 0;
            if (!pause(token, 20ms)) return false;
            value = snapshot(true);
            if (!healthy(value) || any(value.state.enabled) || !all(value.stopped)) return false;
            const auto baseline = value.sequence[1];
            const bool sent = idle_sent && !cancelled(token)
                && session.recover_stop(8) == 0;
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
    bool action(int mode, std::uint64_t token, int attempts = 30) {
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
            if (!command("set_control_mode", [&] { return session.set_control_mode(0, Session::Position); })) return false;
        }
        for (int attempt = 1; attempt <= attempts && !cancelled(token); ++attempt) {
            const auto baseline = snapshot().sequence[1];
            const bool sent = mode == 2
                ? command("stop_motors", [&] { return session.stop_motors(0); })
                : command("set_enable", [&] { return session.set_enable(0, mode == 0 ? 1 : 0); });
            if (sent && confirm(token, baseline, matches)) return true;
            if (!sent && !pause(token, 100ms)) break;
        }
        return fail(std::string(mode == 0 ? "enable" : mode == 1 ? "disable" : "stop") + " not confirmed/cancelled");
    }
    bool home(std::uint64_t token) {
        session.track_home(true);
        struct TrackingGuard {
            Impl* self;
            ~TrackingGuard() { self->session.track_home(false); }
        } guard{this};
        if (cancelled(token)) return false;
        // A send failure can still be partial: keep observing before retrying.
        command("home_motors", [&] { return session.home_motors(0); });
        int attempts = 1;
        const auto deadline = Clock::now() + 60s;
        auto retry_at = Clock::now() + 1s;
        while (!cancelled(token) && Clock::now() < deadline) {
            const auto value = snapshot();
            if (!healthy(value) || !all(value.state.enabled)) return false;
            if (all(value.observed_home) && all(value.state.homed) && all(value.stopped)) {
                bool sent_all = true;
                for (int attempt = 0; attempt < 30; ++attempt) {
                    const auto current = snapshot();
                    if (cancelled(token) || !healthy(current) || !all(current.state.enabled)
                        || !all(current.state.homed)) return false;
                    const bool sent = command("restore position mode", [&] {
                        return session.set_control_mode(0, Session::Position);
                    });
                    sent_all = sent && sent_all;
                    if (attempt < 29 && !pause(token, 20ms)) return false;
                }
                // Repeated sends do not constitute device-side mode readback.
                return !cancelled(token) && sent_all;
            }
            if (Clock::now() >= retry_at && !all(value.observed_home)) {
                if (cancelled(token)) return false;
                if (all(snapshot().observed_home)) continue;
                if (attempts >= 30) return fail("home start not observed for all axes after 30 whole-hand attempts");
                ++attempts;
                command("home_motors retry", [&] { return session.home_motors(0); });
                retry_at = Clock::now() + 1s;
            }
            pause(token, 10ms);
        }
        return fail("home timed out/cancelled; physical stop is not implied");
    }
    bool parameter(int kind, int value, std::uint64_t token) {
        for (int attempt = 0; attempt < 30 && !cancelled(token); ++attempt) {
            bool ok = command("set parameter", [&] {
                if (kind == 0) return session.set_position_velocity(0, value);
                if (kind == 1) return session.set_max_current(0, value);
                return session.set_safe_current_enable(value);
            });
            if (kind < 2) ok = command("apply_motion_params", [&] { return session.apply_motion_params(); }) && ok;
            for (int id = 1; id <= (kind == 2 ? 1 : 6); ++id) {
                int cached = -1;
                const int rc = kind == 0 ? session.get_position_velocity(id, &cached)
                    : kind == 1 ? session.get_max_current(id, &cached) : session.get_safe_current_enable(&cached);
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
bool Hand::prepare_disconnect() {
    return impl_->submit([this](auto token) {
        const bool stopped = impl_->action(2, token, 1);
        const bool disabled = impl_->action(1, token, 1);
        return stopped && disabled;
    }, true);
}
bool Hand::home() { return impl_->submit([this](auto token) { return impl_->home(token); }); }
bool Hand::set_positions(const std::array<int, 6>& positions) {
    for (int value : positions) if (value < 0 || value > 10000) return fail("position outside 0..10000");
    return impl_->submit([this, positions](auto token) {
        for (int j = 0; j < 6; ++j) {
            if (impl_->cancelled(token) || !impl_->command("set_target_position", [&] {
                return impl_->session.set_target_position(j + 1, positions[j]);
            })) return false;
        }
        return !impl_->cancelled(token) && impl_->command("move_motors", [&] { return impl_->session.move_motors(0); });
    });
}
bool Hand::set_position(int id, int position) {
    if (id < 1 || id > 6 || position < 0 || position > 10000) return fail("invalid axis/position");
    return impl_->submit([this, id, position](auto token) {
        return impl_->command("set_target_position", [&] { return impl_->session.set_target_position(id, position); })
            && !impl_->cancelled(token) && impl_->command("move_motors", [&] { return impl_->session.move_motors(id); });
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
}  // namespace rphand

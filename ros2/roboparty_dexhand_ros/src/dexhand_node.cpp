#include <rphand/hand.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/int32_multi_array.hpp>
#include <std_srvs/srv/trigger.hpp>
#include <roboparty_dexhand_ros/msg/dex_hand_state.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
using State = roboparty_dexhand_ros::msg::DexHandState;
using Trigger = std_srvs::srv::Trigger;

class DexHandNode : public rclcpp::Node {
public:
    DexHandNode() : Node("dexhand_node") {
        rcl_interfaces::msg::ParameterDescriptor immutable;
        immutable.read_only = true;
        interface_ = declare_parameter("interface", "can0", immutable);
        node_id_ = declare_parameter("node_id", 1, immutable);
        speed_ = declare_parameter("speed", 8000, immutable);
        current_ = declare_parameter("current_limit", 400, immutable);
        torque_stop_ = declare_parameter("torque_reached_stop", true, immutable);
        const int rate = declare_parameter("command_rate_hz", 200, immutable);
        if (rate != 50 && rate != 100 && rate != 150 && rate != 200)
            throw std::invalid_argument("command_rate_hz must be 50, 100, 150 or 200");
        command_period_ = std::chrono::nanoseconds(1000000000LL / rate);
        if (interface_.empty() || node_id_ < 1 || node_id_ > 127 || speed_ < 1 ||
            speed_ > 20000 || current_ < 0 || current_ > 3000)
            throw std::invalid_argument("Invalid interface/node_id/speed/current_limit");

        auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().durability_volatile();
        publisher_ = create_publisher<State>("state", qos);
        subscription_ = create_subscription<std_msgs::msg::Int32MultiArray>(
            "cmd", qos, [this](std_msgs::msg::Int32MultiArray::ConstSharedPtr msg) {
                if (msg->data.size() != 6 || std::any_of(msg->data.begin(), msg->data.end(),
                        [](auto v) { return v < 0 || v > 10000; })) {
                    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000,
                        "Rejected cmd: expected six values in 0..10000");
                    return;
                }
                std::lock_guard<std::mutex> lock(mutex_);
                if (!accept_targets_ || reset_busy_ || exiting_) return;
                Target values;
                std::copy(msg->data.begin(), msg->data.end(), values.begin());
                target_ = values;
            });
        reset_service_ = create_service<Trigger>("reset", [this](
                Trigger::Request::SharedPtr, Trigger::Response::SharedPtr response) {
            bool busy;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                busy = reset_busy_ || exiting_;
                if (!busy) {
                    reset_busy_ = true;
                    accept_targets_ = false;
                    target_.reset();
                    pending_reset_ = true;
                    stop_requested_ = true;
                }
            }
            if (busy) {
                response->message = "Busy";
            } else {
                response->success = true;
                response->message = "Restart accepted; initialization result will be logged";
                cv_.notify_all();
            }
        });
        RCLCPP_INFO(get_logger(), "%s node=%d: waiting for reset", interface_.c_str(), node_id_);
        manager_ = std::thread([this] { manage(); });
    }

    ~DexHandNode() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            exiting_ = true;
            stop_requested_ = true;
            target_.reset();
        }
        cv_.notify_all();
        if (manager_.joinable()) manager_.join();
    }

private:
    using Target = std::array<int, 6>;
    using Clock = std::chrono::steady_clock;

    bool exiting() {
        std::lock_guard<std::mutex> lock(mutex_);
        return exiting_ || stop_requested_ || !rclcpp::ok(get_node_base_interface()->get_context());
    }

    template<class F> bool attempt(const char* operation, F call) {
        try {
            if (call()) return true;
            RCLCPP_WARN(get_logger(), "%s not confirmed", operation);
        } catch (const std::exception& e) {
            RCLCPP_ERROR(get_logger(), "%s: %s", operation, e.what());
        } catch (...) {
            RCLCPP_ERROR(get_logger(), "%s: unexpected SDK exception", operation);
        }
        return false;
    }

    void cleanup() {
        if (!hand_) return;
        const auto started = Clock::now();
        if (hand_->is_connected()) {
            attempt("Single-attempt cleanup", [this] { return hand_->prepare_disconnect(); });
        }
        // All SDK calls have returned; disconnect joins its internal workers.
        hand_->disconnect();
        hand_.reset();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);
        RCLCPP_INFO(get_logger(), "SDK session released in %lld ms", static_cast<long long>(elapsed.count()));
    }

    Trigger::Response reset() {
        Trigger::Response response;
        const char* step = "create session";
        try {
            if (exiting()) throw std::runtime_error("Node shutting down");
            hand_ = std::make_unique<rphand::Hand>();
            auto perform = [&](const char* name, auto call) {
                step = name;
                if (exiting()) throw std::runtime_error("Node shutting down");
                if (!call()) throw std::runtime_error("SDK returned false");
            };
            perform("connect", [this] { return hand_->connect(interface_, node_id_); });
            perform("disable", [this] { return hand_->disable(); });
            perform("set speed", [this] { return hand_->set_speed(speed_); });
            perform("set current limit", [this] { return hand_->set_current_limit(current_); });
            perform("set torque mode", [this] { return hand_->set_torque_reached_mode(torque_stop_); });
            perform("enable", [this] { return hand_->enable(); });
            perform("home", [this] { return hand_->home(); });
            if (exiting()) throw std::runtime_error("Node shutting down");
            response.success = true;
            response.message = "Reset complete; waiting for a new target";
        } catch (const std::exception& e) {
            response.message = std::string("Reset failed at ") + step + ": " + e.what();
        } catch (...) {
            response.message = std::string("Reset failed at ") + step + ": unexpected SDK exception";
        }
        if (!response.success) cleanup();
        return response;
    }

    void manage() {
        std::thread session([this] { run(false); });
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return exiting_ || pending_reset_; });
            }
            // The old session owns its SDK until cleanup and join have completed.
            if (session.joinable()) session.join();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (exiting_) break;
                pending_reset_ = false;
                stop_requested_ = false;
            }
            try {
                session = std::thread([this] { run(true); });
            } catch (const std::exception& e) {
                RCLCPP_ERROR(get_logger(), "Could not start session: %s", e.what());
                std::lock_guard<std::mutex> lock(mutex_);
                reset_busy_ = false;
            }
        }
    }

    static Clock::time_point advance(Clock::time_point deadline, Clock::duration period) {
        const auto now = Clock::now();
        return deadline <= now ? deadline + period * ((now - deadline) / period + 1) : deadline;
    }

    void run(bool initialize) {
        if (initialize) {
            auto result = reset();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                reset_busy_ = false;
                accept_targets_ = result.success && !exiting_ && !stop_requested_;
            }
            if (result.success) RCLCPP_INFO(get_logger(), "%s", result.message.c_str());
            else RCLCPP_ERROR(get_logger(), "%s", result.message.c_str());
        }
        auto next_command = Clock::now();
        auto next_state = next_command;
        for (;;) {
            std::optional<Target> target;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_until(lock, std::min(next_command, next_state),
                    [this] { return exiting_ || stop_requested_; });
                if (exiting_ || stop_requested_ || !rclcpp::ok(get_node_base_interface()->get_context())) break;
                if (Clock::now() >= next_command) target = target_;
            }
            if (Clock::now() >= next_command) {
                try {
                    if (target && (!hand_ || !hand_->set_positions(*target)))
                        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000,
                            "Position send failed; reset may be required");
                } catch (const std::exception& e) {
                    RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 2000, "Position send: %s", e.what());
                } catch (...) {
                    RCLCPP_ERROR_THROTTLE(get_logger(), *get_clock(), 2000, "Unexpected position send exception");
                }
                next_command = advance(next_command, command_period_);
            }
            if (Clock::now() >= next_state && !exiting()) {
                try { publish_state(); }
                catch (const std::exception& e) {
                    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000, "Feedback publish: %s", e.what());
                }
                next_state = advance(next_state, 20ms);
            }
        }
        cleanup();
    }

    void publish_state() {
        const auto s = hand_ ? hand_->get_state() : rphand::HandState{};
        State msg;
        msg.header.stamp = now();
        msg.feedback_valid = s.feedback_valid;
        std::copy(s.positions.begin(), s.positions.end(), msg.positions.begin());
        std::copy(s.velocities.begin(), s.velocities.end(), msg.velocities.begin());
        std::copy(s.currents.begin(), s.currents.end(), msg.currents.begin());
        std::copy(s.enabled.begin(), s.enabled.end(), msg.enabled.begin());
        std::copy(s.homed.begin(), s.homed.end(), msg.homed.begin());
        std::copy(s.torque_reached.begin(), s.torque_reached.end(), msg.torque_reached.begin());
        std::copy(s.fault_codes.begin(), s.fault_codes.end(), msg.fault_codes.begin());
        publisher_->publish(msg);
    }

    std::unique_ptr<rphand::Hand> hand_;
    std::string interface_;
    int node_id_, speed_, current_;
    std::chrono::nanoseconds command_period_{5000000};
    bool torque_stop_, reset_busy_ = false, exiting_ = false;
    bool pending_reset_ = false, stop_requested_ = false, accept_targets_ = false;
    std::optional<Target> target_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread manager_;
    rclcpp::Publisher<State>::SharedPtr publisher_;
    rclcpp::Subscription<std_msgs::msg::Int32MultiArray>::SharedPtr subscription_;
    rclcpp::Service<Trigger>::SharedPtr reset_service_;
};

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    int status = 0;
    try { rclcpp::spin(std::make_shared<DexHandNode>()); }
    catch (const std::exception& e) {
        RCLCPP_FATAL(rclcpp::get_logger("dexhand_node"), "%s", e.what());
        status = 1;
    }
    rclcpp::shutdown();
    return status;
}

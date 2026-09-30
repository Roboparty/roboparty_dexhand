#include <dh116s/hand.hpp>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/int32_multi_array.hpp>
#include <std_srvs/srv/trigger.hpp>
#include <roboparty_dexhand_ros/msg/dex_hand_state.hpp>

#include <algorithm>
#include <chrono>
#include <future>
#include <mutex>
#include <optional>
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
            speed_ > 20000 || current_ < 0 || current_ > 3000) {
            throw std::invalid_argument("Invalid interface/node_id/speed/current_limit");
        }
        auto qos = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().durability_volatile();
        publisher_ = create_publisher<State>("state", qos);
        subscription_ = create_subscription<std_msgs::msg::Int32MultiArray>(
            "cmd", qos, [this](std_msgs::msg::Int32MultiArray::ConstSharedPtr msg) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (phase_ != State::READY) return;
                if (msg->data.size() != 6 || std::any_of(msg->data.begin(), msg->data.end(),
                        [](auto v) { return v < 0 || v > 10000; })) {
                    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 2000, "Rejected cmd: expected six values in 0..10000");
                    return;
                }
                std::array<int, 6> values;
                std::copy(msg->data.begin(), msg->data.end(), values.begin());
                target_ = values;
            });
        reset_service_ = create_service<Trigger>("reset", [this](
                const Trigger::Request::SharedPtr, Trigger::Response::SharedPtr response) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (resetting_ || phase_ == State::STOPPING) {
                response->message = "Busy";
                return;
            }
            target_.reset();
            resetting_ = true;
            step_ = 0;
            phase_ = State::CONNECTING;
            response->success = true;
            response->message = "Accepted: reconnect, configure, enable and home";
        });
        stop_service_ = create_service<Trigger>("stop", [this](
                const Trigger::Request::SharedPtr, Trigger::Response::SharedPtr response) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (phase_ != State::STOPPING &&
                !(phase_ == State::STOPPED && hand_.get_state().feedback_valid)) request_stop(false);
            response->success = true;
            response->message = "Accepted; inspect state for completion";
        });
        timer_ = create_wall_timer(20ms, [this] { publish_state(); });
        worker_ = std::thread([this] { run(); });
        RCLCPP_INFO(get_logger(), "%s node=%d: waiting for reset", interface_.c_str(), node_id_);
    }

    ~DexHandNode() override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            exiting_ = true;
            if (phase_ != State::UNINITIALIZED) request_stop(false);
        }
        if (worker_.joinable()) worker_.join();
        hand_.disconnect();
    }

private:
    static bool ready(const dh116s::HandState& s) {
        return s.feedback_valid &&
            std::all_of(s.enabled.begin(), s.enabled.end(), [](bool v) { return v; }) &&
            std::all_of(s.homed.begin(), s.homed.end(), [](bool v) { return v; }) &&
            std::all_of(s.fault_codes.begin(), s.fault_codes.end(), [](int v) { return v == 0; });
    }
    static bool done(std::future<bool>& f) {
        return f.valid() && f.wait_for(0ms) == std::future_status::ready;
    }
    bool result(std::future<bool>& f) {
        try { return f.get(); }
        catch (const std::exception& e) { RCLCPP_ERROR(get_logger(), "%s", e.what()); }
        catch (...) { RCLCPP_ERROR(get_logger(), "Unexpected SDK exception"); }
        return false;
    }
    void request_stop(bool error) {
        target_.reset();
        resetting_ = false;
        phase_ = State::STOPPING;
        stop_error_ = error;
    }
    void fail(const char* operation) {
        RCLCPP_ERROR(get_logger(), "%s failed (reset step %d); attempting stop", operation, step_);
        request_stop(true);
    }
    bool reset_step(int step) {
        switch (step) {
        case 0: return !hand_.is_connected() || hand_.disable();
        case 1: hand_.disconnect(); return hand_.connect(interface_, node_id_);
        case 2: return hand_.disable();
        case 3: return hand_.set_speed(speed_);
        case 4: return hand_.set_current_limit(current_);
        case 5: return hand_.set_torque_reached_mode(torque_stop_);
        case 6: return hand_.enable();
        case 7: return hand_.home();
        default: return false;
        }
    }
    void tick() {
        // Only this coordinator starts SDK work. ROS callbacks change intent under
        // the same lock; a stop can preempt a blocking SDK home via its urgent queue.
        if (phase_ == State::STOPPING) {
            if (done(active_)) result(active_);
            // Reconnect owns the SDK lifecycle; wait for it before stopping the
            // new connection. It cannot proceed to enable after cancellation.
            const bool reconnecting = active_.valid() && active_reset_ && step_ == 1;
            if (!stopping_.valid() && !reconnecting && (hand_.is_connected() || !active_.valid())) {
                stop_final_ = !active_.valid();
                stopping_ = std::async(std::launch::async, [this] { return hand_.stop(); });
            }
            if (!active_.valid() && done(stopping_)) {
                const bool ok = result(stopping_);
                // Confirm once more after the interrupted caller has returned,
                // including the case where its async dispatch was still starting.
                if (!stop_final_) return;
                phase_ = ok && !stop_error_ ? State::STOPPED : State::ERROR;
                if (!ok) RCLCPP_ERROR(get_logger(), "Physical stop could not be confirmed");
            }
            return;
        }
        if (done(active_)) {
            const bool ok = result(active_);
            if (!ok) { fail("SDK operation"); return; }
            // A reset can arrive while a position send is completing.
            if (resetting_ && active_reset_) ++step_;
        }
        if (active_.valid()) return;
        if (resetting_) {
            if (step_ == 8) {
                if (!ready(hand_.get_state())) { fail("Post-home feedback"); return; }
                resetting_ = false;
                phase_ = State::READY;
                RCLCPP_INFO(get_logger(), "READY: waiting for a new cmd");
                return;
            }
            phase_ = step_ < 3 ? State::CONNECTING : step_ < 6 ? State::CONFIGURING :
                step_ == 6 ? State::ENABLING : State::HOMING;
            active_reset_ = true;
            active_ = std::async(std::launch::async, [this, step = step_] { return reset_step(step); });
        } else if (phase_ == State::READY) {
            if (!ready(hand_.get_state())) { fail("Feedback health"); return; }
            if (target_) {
                active_reset_ = false;
                active_ = std::async(std::launch::async, [this, target = *target_] {
                    return hand_.set_positions(target);
                });
            }
        } else if (phase_ == State::STOPPED && !hand_.get_state().feedback_valid) {
            phase_ = State::ERROR;
            RCLCPP_ERROR(get_logger(), "Stopped feedback expired");
        }
    }
    void run() {
        auto next = std::chrono::steady_clock::now();
        for (;;) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (exiting_ && phase_ != State::STOPPING) break;
                tick();
            }
            next = std::max(next + command_period_, std::chrono::steady_clock::now());
            std::this_thread::sleep_until(next);
        }
    }
    void publish_state() {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto s = hand_.get_state();
        State msg;
        msg.header.stamp = now();
        msg.phase = phase_;
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
    dh116s::Hand hand_;
    std::string interface_;
    int node_id_, speed_, current_, step_ = 0;
    std::chrono::nanoseconds command_period_{5000000};
    bool torque_stop_, resetting_ = false, active_reset_ = false;
    bool exiting_ = false, stop_error_ = false, stop_final_ = false;
    uint8_t phase_ = State::UNINITIALIZED;
    std::optional<std::array<int, 6>> target_;
    std::mutex mutex_;
    std::future<bool> active_, stopping_;
    std::thread worker_;
    rclcpp::Publisher<State>::SharedPtr publisher_;
    rclcpp::Subscription<std_msgs::msg::Int32MultiArray>::SharedPtr subscription_;
    rclcpp::Service<Trigger>::SharedPtr reset_service_, stop_service_;
    rclcpp::TimerBase::SharedPtr timer_;
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

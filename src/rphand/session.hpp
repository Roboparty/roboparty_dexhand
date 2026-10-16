#pragma once
#include "protocol.hpp"
#include "socketcan.hpp"
#include <rphand/hand.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace rphand {
using Clock = std::chrono::steady_clock;
struct Snapshot {
    HandState state;
    std::array<bool, 6> stopped{}, observed_home{}, emergency_stopped{};
    std::array<Clock::time_point, 2> last{};
    std::array<std::uint64_t, 2> sequence{};
};
struct FeedbackRate {
    std::size_t count = 0;
    long long age_ms = -1;
    double hz = 0;
    bool gap_ok = true;
};
struct SdoResult {
    int code = 0;
    std::uint32_t value = 0;
    bool received = false, aborted = false;
};

// Owns communication and feedback. Action retries remain in Hand.
class Session {
public:
    static constexpr int Canfd = 1, rphand = 2, Position = 0;
    using Send = std::function<bool(unsigned, const unsigned char*, unsigned, int)>;
    using Cancel = std::function<bool()>;
    Session();
    ~Session();
    bool open_transport(const std::string& interface, int node);
    bool receive_failed() const { return rx_failed_; }
    unsigned long send_errors() const { return transport_.errors(); }
    Snapshot snapshot(bool connected = true) const;
    bool pause(std::chrono::milliseconds duration, const Cancel& cancelled);
    void wait_for_status(std::uint64_t sequence, Clock::time_point deadline, const Cancel& cancelled);
    void notify_waiters();
    void track_home(bool enabled);
    void clear_feedback_samples();
    std::array<FeedbackRate, 2> feedback_rates() const;
    SdoResult feedback_period(unsigned index, bool write, const Cancel& cancelled);
    int configure_feedback();
    int recover_stop(int control);
    void set_send_callback(Send callback);
    int initial(int mode, int node);
    void close();
    int get_hand_type(int* value) const;
    int get_dof(int* total, int* active) const;
    int set_canfd_data_decode(unsigned id, const unsigned char* data, int size);
    int get_now_position(int id, int* value) const;
    int get_now_position_velocity(int id, int* value) const;
    int get_now_current(int id, int* value) const;
    int set_move_no_home(int enable);
    int set_enable(int id, int enable);
    int set_control_mode(int id, int mode);
    int stop_motors(int id);
    int home_motors(int id);
    int set_position_velocity(int id, int value);
    int set_max_current(int id, int value);
    int set_target_position(int id, int value);
    int get_position_velocity(int id, int* value) const;
    int get_max_current(int id, int* value) const;
    int set_safe_current_enable(int enable);
    int get_safe_current_enable(int* value);
    int apply_motion_params();
    int move_motors(int id);
private:
    void receive_loop();
    SdoResult transact_sdo(unsigned index, unsigned sub, bool write, int value,
                           int timeout_ms, const Cancel& cancelled, bool configuration);
    static bool valid_axis(int id) { return id >= 0 && id <= 6; }
    int send(unsigned base, const Bytes& bytes);
    int emit(Command command, unsigned char frame = 2);
    int read_sdo(unsigned index, unsigned sub, int* value, int timeout_ms = 200);
    int control(int id, int mode, int command);
    int set_axis(int id, int value, int Axis::*field, bool* dirty = nullptr);
    int get_axis(int id, int* value, int Axis::*field) const;
    int get_feedback(int id, int* value, int FeedbackAxis::*field) const;
    // Commands serialize independently of receive state: callbacks may reply inline.
    mutable std::recursive_mutex command_mutex_;
    mutable std::mutex receive_mutex_;
    std::condition_variable received_;
    Send callback_;
    detail::SocketCan transport_;
    std::thread receiver_;
    std::atomic<bool> rx_done_{true}, rx_failed_{false};
    CommandState state_;
    Kinematics kinematics_{rphand};
    std::array<FeedbackAxis, 30> feedback_{};
    Snapshot snapshot_;
    std::array<std::deque<Clock::time_point>, 2> samples_;
    bool track_home_ = false;
    std::atomic<int> node_{1};
    std::atomic<bool> closed_{true}, ready_{false};
    bool velocity_dirty_ = true, current_dirty_ = true, restore_pending_ = false;
    bool allow_no_home_ = false;
    bool pending_ = false, answered_ = false;
    unsigned pending_index_ = 0, pending_sub_ = 0;
    bool pending_write_ = false, pending_configuration_ = false;
    SdoResult sdo_;
};
}

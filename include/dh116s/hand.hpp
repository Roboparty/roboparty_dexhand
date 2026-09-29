#pragma once

#include <array>
#include <memory>
#include <string>

namespace dh116s {

// Order: thumb abduction, thumb flexion, index, middle, ring, little.
struct HandState {
    std::array<int, 6> positions{};
    std::array<int, 6> velocities{};
    std::array<int, 6> currents{};
    std::array<bool, 6> enabled{};
    std::array<bool, 6> homed{};
    std::array<bool, 6> torque_reached{};
    std::array<int, 6> fault_codes{};
    bool feedback_valid = false;
};

class Hand {
public:
    Hand();
    ~Hand();
    Hand(const Hand&) = delete;
    Hand& operator=(const Hand&) = delete;
    Hand(Hand&&) = delete;
    Hand& operator=(Hand&&) = delete;

    bool connect(const std::string& can_name, int node_id, int retry_count = 30);
    void disconnect();
    bool is_connected() const;
    bool enable();
    bool disable();
    bool stop();
    bool home();
    bool set_positions(const std::array<int, 6>& positions);
    bool set_position(int id, int position);
    bool set_speed(int speed);
    bool set_current_limit(int current);
    bool set_torque_reached_mode(bool stop_on_reached);
    HandState get_state() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace dh116s

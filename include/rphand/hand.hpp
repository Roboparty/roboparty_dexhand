#pragma once

#include <rphand/types.hpp>
#include <memory>
#include <string>

namespace rphand {

class Hand {
public:
    Hand();
    ~Hand();
    Hand(const Hand&) = delete;
    Hand& operator=(const Hand&) = delete;
    Hand(Hand&&) = delete;
    Hand& operator=(Hand&&) = delete;

    // Write feedback periods (up to 3 attempts per axis), send configuration 10 times at 5ms
    // intervals, then wait up to 500ms for both groups. No motor control.
    bool connect(const std::string& can_name, int node_id);
    bool connect(const ConnectionOptions& options);
    void disconnect();
    bool is_connected() const;
    bool enable();
    bool disable();
    bool stop();
    // Best-effort session cleanup: one stop and one disable attempt, no retry loop.
    bool prepare_disconnect();
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

}  // namespace rphand

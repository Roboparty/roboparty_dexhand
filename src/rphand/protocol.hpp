#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rphand {
using Bytes = std::vector<std::uint8_t>;
enum class Command { Config, Control, Move };
enum class SdoCommand { Read, Write };

// Protocol values and per-axis command state.
struct Axis {
    std::uint8_t mode = 0, home_mode = 0, home_function = 0, control = 0;
    int position = 0, velocity = 0, current = 0;
};
struct CommandState {
    std::vector<Axis> axes;
};

void pack_control(Bytes&, const std::vector<Axis>&);
void pack_move(Bytes&, const std::vector<Axis>&, std::uint8_t frame_type);
Bytes command_payload(Command, const CommandState&, std::uint8_t frame_type);
Bytes sdo_payload(SdoCommand, std::uint16_t index, std::uint8_t subindex, std::uint16_t value);
Bytes stop_recovery_payload(int control);
struct SdoReply {
    unsigned command = 0, index = 0, subindex = 0;
    std::uint32_t value = 0;
};
bool decode_sdo_reply(const unsigned char*, int size, SdoReply&);
bool valid_feedback(unsigned type, unsigned slots, unsigned length);

struct Limits { float min_angle, max_angle; int min_position, max_position; };
class Kinematics {
public:
    explicit Kinematics(int hand_type, bool conversion = true, int generic_axes = 10);
    int active_axes() const { return active_; }
    const Limits& limits(int zero_based_axis) const;
    float finger_to_linkage(int axis, float angle) const;
    float linkage_to_finger(int axis, float angle) const;
    float angle_from_position(int axis, int position) const;
private:
    bool converts(int axis) const;
    int type_, active_;
    bool conversion_;
    std::array<Limits, 30> limits_{};
};
struct FeedbackAxis {
    int raw_position=0, position=0, position_velocity=0, current=0;
    float angle=0, angular_velocity=0;
    std::uint8_t status=0, alarm=0;
    bool seen=false;
    int running_status() const { return status >> 5; }
};
enum class DecodeResult { Ok, Malformed, SensorPayloadNotHandled };
DecodeResult decode_motor_feedback(const Bytes&, const Kinematics&, std::array<FeedbackAxis,30>&);
}

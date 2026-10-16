#include "protocol.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rphand {
namespace {
void u16(Bytes& b, int v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(static_cast<unsigned>(v) >> 8));
}
// Finger-linkage calibration coefficients for the four fingers.
constexpr float formula[4][3] = {
    {-0.0051f, 1.4017f, 0.4650f}, {-0.0043f, 1.3244f, 0.2225f},
    {-0.0048f, 1.3605f, 0.3793f}, {-0.0056f, 1.4499f, 0.4579f}
};
}

void pack_control(Bytes& out, const std::vector<Axis>& axes) {
    for (const auto& a : axes) {
        out.push_back((a.mode & 7) | ((a.home_mode & 3) << 3) | ((a.home_function & 3) << 5));
        out.push_back(a.control);
    }
}
void pack_move(Bytes& out, const std::vector<Axis>& axes, std::uint8_t frame) {
    for (const auto& a : axes) {
        if (frame == 2 || frame == 5 || frame == 7 || frame == 11) u16(out, a.position);
        if (frame == 3 || frame == 5 || frame == 9 || frame == 11) u16(out, a.velocity / 10);
        if (frame == 4 || frame == 7 || frame == 9 || frame == 11) u16(out, a.current);
    }
}
Bytes command_payload(Command command, const CommandState& s, std::uint8_t frame) {
    if (s.axes.size() > 30) throw std::invalid_argument("more than 30 axes");
    Bytes out;
    if (command == Command::Config) {
        out = {0, 4, 0x50, 1, 0x5a, 1};
    } else {
        auto kind = command == Command::Control ? 1 : frame;
        out = {static_cast<std::uint8_t>(kind), static_cast<std::uint8_t>(s.axes.size())};
        if (command == Command::Control) pack_control(out, s.axes);
        if (command == Command::Move) pack_move(out, s.axes, frame);
    }
    return out;
}
Bytes sdo_payload(SdoCommand type, std::uint16_t index, std::uint8_t subindex, std::uint16_t value) {
    Bytes out;
    out.push_back(type == SdoCommand::Read ? 0x40 : 0x2b);
    u16(out,index); out.push_back(subindex);
    if (type == SdoCommand::Write) u16(out,value);
    out.resize(8,0);
    return out;
}

Bytes stop_recovery_payload(int control) {
    CommandState state;
    state.axes.resize(6);
    for (auto& axis : state.axes) {
        axis.home_function = 1;
        axis.control = static_cast<std::uint8_t>(control);
    }
    return command_payload(Command::Control, state, 2);
}
bool decode_sdo_reply(const unsigned char* data, int size, SdoReply& reply) {
    if (!data || size != 8) return false;
    const unsigned command = data[0];
    if (command != 0x60 && command != 0x4b && command != 0x43 && command != 0x4f && command != 0x80)
        return false;
    reply = {command, unsigned(data[1]) | (unsigned(data[2]) << 8), data[3],
        std::uint32_t(data[4]) | (std::uint32_t(data[5]) << 8)
        | (std::uint32_t(data[6]) << 16) | (std::uint32_t(data[7]) << 24)};
    if (command == 0x4b) reply.value &= 0xffff;
    if (command == 0x4f) reply.value &= 0xff;
    return true;
}
bool valid_feedback(unsigned type, unsigned slots, unsigned length) {
    if (slots < 6 || length > 64) return false;
    type &= 0x7f;
    // Position status bytes follow eight fixed motor slots, even for six axes.
    if (type == 0x50) return slots <= 8 && length >= 50 + slots;
    if (type == 0x5a) return slots <= 31 && length >= 2 + 2 * slots;
    return false;
}

Kinematics::Kinematics(int type, bool conversion, int generic_axes) : type_(type), conversion_(conversion) {
    if (type < 0 || type > 4 || generic_axes < 1 || generic_axes > 30) throw std::invalid_argument("hand type/axis count");
    for (auto& l : limits_) l = {0,0,0,10000};
    if (type <= 2) {
        active_=6;
        const std::array<float,6> max = type == 2 ? std::array<float,6>{91,59,72,72,72,74} : std::array<float,6>{60,30,80,80,80,80};
        for (int i=0;i<6;++i) limits_[i].max_angle=max[i];
    } else if (type==3) {
        active_=16; limits_[0]={0,110,0,10000};
        for (int i=1;i<16;i+=3) {
            limits_[i]={-20,20,-5000,5000};
            limits_[i+1]={0,90,0,10000}; limits_[i+2]={0,70,0,10000};
        }
    } else {
        active_=generic_axes;
        for (auto& l:limits_) l={0,90,0,10000};
    }
}
const Limits& Kinematics::limits(int axis) const {
    if (axis<0 || axis>=active_) throw std::out_of_range("zero-based axis");
    return limits_[axis];
}
bool Kinematics::converts(int axis) const { return conversion_ && type_==2 && axis>=2 && axis<=5; }
float Kinematics::finger_to_linkage(int axis, float angle) const {
    limits(axis);
    if (!converts(axis)) return angle;
    const auto& f=formula[axis-2];
    const float a=f[0], b=f[1], c=f[2];
    if (std::fabs(a)<1e-6f) return std::fabs(b)<=1e-6f ? 0 : (angle-c)/b;
    const float discriminant=b*b-(c-angle)*a*4.0f;
    if (discriminant<0) return 0;
    const float root=std::sqrt(discriminant), denominator=a+a;
    return std::min((root-b)/denominator,(-b-root)/denominator);
}
float Kinematics::linkage_to_finger(int axis, float angle) const {
    limits(axis);
    if (!converts(axis)) return angle;
    const auto& f=formula[axis-2];
    return f[2]+f[0]*angle*angle+f[1]*angle;
}
float Kinematics::angle_from_position(int axis, int pos) const {
    const auto& l=limits(axis);
    const float low=finger_to_linkage(axis,l.min_angle);
    const float range=finger_to_linkage(axis,l.max_angle)-low;
    float angle=low;
    if (l.max_position>l.min_position) angle=static_cast<float>(pos-l.min_position)/static_cast<float>(l.max_position-l.min_position)*range+low;
    return std::clamp(linkage_to_finger(axis,angle),l.min_angle,l.max_angle);
}

DecodeResult decode_motor_feedback(const Bytes& p,const Kinematics& kin,std::array<FeedbackAxis,30>& state) {
    struct Config { int type,max,start,stride; bool position,velocity,current,sensor,status; };
    // Frame capacity determines where the trailing status bytes begin.
    constexpr Config configs[]={
        {0x50,8,0,6,1,1,1,0,0},{0x51,8,8,6,1,1,1,0,0},{0x52,8,16,6,1,1,1,0,0},
        {0x53,12,0,4,1,1,0,0,0},{0x54,12,12,4,1,1,0,0,0},
        {0x55,12,0,4,1,0,1,0,0},{0x56,12,12,4,1,0,1,0,0},{0x5a,30,0,2,0,0,0,0,1},
        {0x70,5,0,12,0,0,0,1,0},{0x71,1,5,36,0,0,0,1,0},
        {0x72,2,0,30,0,0,0,1,0},{0x73,2,2,30,0,0,0,1,0},{0x74,2,4,30,0,0,0,1,0},
        {0xd0,8,0,6,1,1,1,0,0},{0xd1,8,8,6,1,1,1,0,0},{0xda,30,0,2,0,0,0,0,1},
        {0xf0,5,0,12,0,0,0,1,0},{0xf1,1,5,36,0,0,0,1,0},
        {0xf2,2,0,30,0,0,0,1,0},{0xf3,2,2,30,0,0,0,1,0},{0xf4,2,4,30,0,0,0,1,0}
    };
    if (p.size()<3 || !valid_feedback(p[0], p[1], p.size())) return DecodeResult::Malformed;
    const Config* c=nullptr;
    for(const auto& x:configs)if(x.type==p[0]){c=&x;break;}
    if(!c)return DecodeResult::Malformed;
    if(c->sensor)return DecodeResult::SensorPayloadNotHandled;
    const int count=std::min({int(p[1]),kin.active_axes(),c->max});
    const std::size_t needed=2+(c->status ? count*c->stride : c->max*c->stride+count);
    if(p.size()<needed)return DecodeResult::Malformed;
    auto read16=[&](std::size_t i){return unsigned(p[i]) | (unsigned(p[i+1])<<8);};
    auto signed16=[](unsigned x){return x<32768 ? int(x) : int(x)-65536;};
    for(int i=0;i<count;++i) {
        const int axis=c->start+i;
        if(axis>=kin.active_axes())continue;
        auto& s=state[axis];const std::size_t off=2+i*c->stride;
        if(c->position)s.raw_position=read16(off);
        if(c->velocity) {
            s.position_velocity=signed16(static_cast<unsigned>(signed16(read16(off+2))*10)&0xffff);
            const auto& l=kin.limits(axis);
            s.angular_velocity=(float(s.position_velocity)*(l.max_angle-l.min_angle))/10000.0f;
        }
        if(c->current)s.current=signed16(read16(off+(c->velocity?4:2)));
        s.status=p[c->status ? off : 2+c->max*c->stride+i];
        if(c->status)s.alarm=p[off+1]&15;
        s.seen=true;
    }
    for(int i=0;i<kin.active_axes();++i) {
        auto& s=state[i];s.position=s.raw_position;
        if(s.position>20000)s.position=0;
        if(s.position>10000 && s.position<20000)s.position=10000;
        s.angle=kin.angle_from_position(i,s.position);
    }
    return DecodeResult::Ok;
}
}

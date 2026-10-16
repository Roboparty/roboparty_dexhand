#pragma once
#include "rphand/session.hpp"
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using rphand::Bytes;
using rphand::Session;

inline void check(bool condition, const std::string& label) {
    if (!condition) throw std::runtime_error(label);
}
using Frame = std::tuple<unsigned, Bytes, int>;
struct Device {
    Session hand;
    Session::Send callback;
    std::vector<Frame> frames;
    int node = 7, model = 3, safe = 1, fail_at = -1;
    bool silence = false, abort = false;
    std::function<void(Bytes&)> alter_reply;
    Device() {
        callback = [this](unsigned id, const unsigned char* data, unsigned size, int flags) {
            frames.emplace_back(id, Bytes(data, data + size), flags);
            if (fail_at == int(frames.size())) return false;
            if (id == 0x600u + node && size == 8) {
                const unsigned index = data[1] | (unsigned(data[2]) << 8);
                if (data[0] == 0x2b && index == 0x2017) safe = data[4];
                if (data[0] == 0x40 && !silence) {
                    int value = index == 0x200e && data[3] == 0x1d ? model :
                        index == 0x2017 ? safe : 1;
                    Bytes reply{static_cast<unsigned char>(abort ? 0x80 : 0x4b), data[1], data[2], data[3],
                                static_cast<unsigned char>(value), static_cast<unsigned char>(value >> 8), 0, 0};
                    if (alter_reply) alter_reply(reply);
                    hand.set_canfd_data_decode(0x580 + node, reply.data(), reply.size());
                }
            }
            return true;
        };
        bind();
    }
    ~Device() { hand.close(); }
    void bind() { hand.set_send_callback(callback); }
    void reply(const Bytes& bytes) {
        hand.set_canfd_data_decode(0x580 + node, bytes.data(), bytes.size());
    }
    void feedback(const Bytes& bytes) {
        check(hand.set_canfd_data_decode(0x480 + node, bytes.data(), bytes.size()) == 0, "decode feedback");
    }
    int initial() { return hand.initial(Session::Canfd, node); }
    void clear() { frames.clear(); fail_at = -1; }
};

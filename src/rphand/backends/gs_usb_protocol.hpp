#pragma once
#include "../transport.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

// Wire definitions interoperable with the Linux gs_usb driver. No OS types.
namespace rphand::detail::gs {
constexpr unsigned fd_feature = 1u << 8, extended_timing = 1u << 10;
constexpr unsigned rx_echo = 0xffffffffu, tx_slots = 10;
constexpr unsigned lengths[] = {0,1,2,3,4,5,6,7,8,12,16,20,24,32,48,64};
inline unsigned get32(const unsigned char* p) {
    return unsigned(p[0]) | (unsigned(p[1]) << 8) |
        (unsigned(p[2]) << 16) | (unsigned(p[3]) << 24);
}
inline void put32(unsigned char* p, unsigned value) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<unsigned char>(value >> (8 * i));
}
using Packet = std::array<unsigned char, 76>;
inline bool encode(Packet& packet, unsigned echo, unsigned channel,
                   unsigned id, const unsigned char* data, unsigned size, bool extended) {
    if (size > 64 || (!data && size) || echo >= tx_slots || channel > 255 ||
        id > (extended ? 0x1fffffffu : 0x7ffu)) return false;
    packet = {};
    put32(packet.data(), echo);
    put32(packet.data() + 4, id | (extended ? 0x80000000u : 0));
    unsigned dlc = 0;
    while (lengths[dlc] < size) ++dlc;
    packet[8] = static_cast<unsigned char>(dlc);
    packet[9] = static_cast<unsigned char>(channel);
    packet[10] = 6; // FD + bit-rate switching; timestamp mode is not enabled.
    if (size) std::memcpy(packet.data() + 12, data, size);
    return true;
}
enum class Kind { Frame, Echo, Ignore, Status, Failure, Incomplete };
inline Kind decode(const unsigned char* p, unsigned size, unsigned channel,
                   CanFrame& frame, unsigned& echo, const char** reason = nullptr) {
    if (reason) *reason = "";
    auto fail = [&](const char* message) {
        if (reason) *reason = message;
        return Kind::Failure;
    };
    if (!size) return Kind::Ignore; // USB zero-length completion, not a CAN frame.
    if (size < 12) return fail("short gs_usb header");
    if (p[9] != channel) return Kind::Ignore;
    const bool fd = (p[10] & 2) != 0;
    if (p[8] > 15) return fail("invalid DLC");
    if (p[10] & 1) return fail("adapter RX overflow");
    echo = get32(p);
    if (echo != rx_echo) return echo < tx_slots ? Kind::Echo : fail("invalid TX echo ID");
    const auto id = get32(p + 4);
    if (id & 0x20000000u) {
        // CAN error records also carry normal controller-state notifications.
        // Never pass these records to the hand's feedback decoder.
        if (fd || p[8] != 8 || size < 20 || (id & 0xc0000000u))
            return fail("malformed CAN error record");
        const unsigned classes = id & 0x1fffffffu;
        if (classes & 0x40) return fail("CAN bus-off");
        if (classes & 0x100) return fail("CAN controller restarted unexpectedly");
        const bool active = (classes == 0x4 || classes == 0x204) && p[13] == 0x40;
        const bool zero_counters = classes == 0x200 && p[18] == 0 && p[19] == 0;
        if ((active || zero_counters) && p[18] < 96 && p[19] < 96) {
            if (reason) *reason = active ? "CAN controller error-active notification" : "CAN zero error counters";
            return Kind::Status;
        }
        if (classes & 0x20) return fail("CAN transmit not acknowledged (ACK error)");
        if (classes & 0x4) return fail("CAN controller warning/passive/overflow");
        return fail("CAN error record");
    }
    if (id & 0xc0000000u) return Kind::Ignore; // Not a standard data frame.
    if (id > 0x7ff) return fail("invalid standard CAN ID");
    const unsigned length = fd ? lengths[p[8]] : std::min(unsigned(p[8]), 8u);
    // Timestamp mode is disabled. Firmware may send only the used data bytes,
    // and TX completion may contain just the 12-byte header.
    if (size < 12 + length) return fail("truncated CAN payload");
    frame = {};
    frame.can_id = id;
    frame.fd = fd;
    frame.brs = fd && (p[10] & 4);
    frame.len = static_cast<unsigned char>(length);
    std::memcpy(frame.data, p + 12, frame.len);
    return Kind::Frame;
}

// USB reads are chunks of a stream, not necessarily individual gs_usb records.
// Full records are 76 bytes for FD and 20 for classic CAN. A short USB transfer
// may also end with a compact record (or a header-only TX echo).
class ReceiveBuffer {
public:
    static constexpr unsigned max_chunk = 1024;
    bool append(const unsigned char* data, unsigned size, bool transfer_end) {
        if (size > max_chunk || end_ - begin_ + size > bytes_.size()) return false;
        const unsigned remaining = end_ - begin_;
        std::memmove(bytes_.data(), bytes_.data() + begin_, remaining);
        if (size) std::memcpy(bytes_.data() + remaining, data, size);
        begin_ = 0; end_ = remaining + size; transfer_end_ = transfer_end;
        return true;
    }
    Kind next(CanFrame& frame, unsigned& echo, const char** reason) {
        *reason = "";
        last_begin_ = begin_; last_size_ = end_ - begin_;
        if (!last_size_) return Kind::Incomplete;
        auto fail = [&](const char* message) {
            *reason = message;
            return Kind::Failure;
        };
        if (last_size_ < 12)
            return transfer_end_ ? fail("short gs_usb header at USB transfer boundary") : Kind::Incomplete;
        const auto* p = bytes_.data() + begin_;
        if (p[8] > 15) return fail("invalid DLC");
        unsigned size = (p[10] & 2) ? 76 : 20;
        if (last_size_ < size) {
            if (!transfer_end_) return Kind::Incomplete;
            const unsigned compact = 12 + ((p[10] & 2) ? lengths[p[8]] : std::min(unsigned(p[8]), 8u));
            if (last_size_ != compact && !(last_size_ == 12 && get32(p) < tx_slots))
                return fail("truncated gs_usb record at USB transfer boundary");
            size = last_size_;
        }
        last_size_ = size;
        const auto kind = decode(p, size, 0, frame, echo, reason);
        if (kind != Kind::Failure) begin_ += size;
        return kind;
    }
    const unsigned char* record_data() const { return bytes_.data() + last_begin_; }
    unsigned record_size() const { return last_size_; }
private:
    std::array<unsigned char, max_chunk + 76> bytes_{};
    unsigned begin_ = 0, end_ = 0, last_begin_ = 0, last_size_ = 0;
    bool transfer_end_ = false;
};

struct Limits {
    unsigned t1_min, t1_max, t2_min, t2_max, sjw_max, brp_min, brp_max, brp_inc;
};
inline Limits limits(const unsigned char* p) {
    return {get32(p), get32(p+4), get32(p+8), get32(p+12),
            get32(p+16), get32(p+20), get32(p+24), get32(p+28)};
}
// Require an exact bitrate. Choose the closest sample point to 80%, preferring
// more time quanta when equal. The device's advertised increment is respected.
inline bool timing(unsigned clock, unsigned bitrate, const Limits& l,
                   std::array<unsigned char, 20>& result) {
    if (!clock || !bitrate || !l.brp_min || !l.brp_inc || !l.sjw_max ||
        !l.t1_min || !l.t2_min || l.brp_min > l.brp_max ||
        l.t1_min > l.t1_max || l.t2_min > l.t2_max ||
        l.brp_max > 65536 || l.brp_inc > 65536 || l.t1_max > 65536 || l.t2_max > 65536) return false;
    double best = std::numeric_limits<double>::infinity();
    bool found = false;
    const unsigned first_brp = ((l.brp_min + l.brp_inc - 1) / l.brp_inc) * l.brp_inc;
    for (unsigned brp = first_brp; brp <= l.brp_max; brp += l.brp_inc) {
        const auto divisor = std::uint64_t(bitrate) * brp;
        if (clock % divisor) continue;
        const auto total = clock / divisor;
        if (total < 3 || total > std::uint64_t(1) + l.t1_max + l.t2_max) continue;
        for (unsigned t2 = l.t2_min; t2 <= l.t2_max && t2 + 1 < total; ++t2) {
            const unsigned t1 = static_cast<unsigned>(total - 1 - t2);
            if (t1 < l.t1_min || t1 > l.t1_max) continue;
            const double error = std::abs(double(1 + t1) / double(total) - 0.8);
            if (error >= best) continue;
            best = error; found = true;
            put32(result.data(), 0); // prop_seg; combined segment in phase_seg1.
            put32(result.data()+4, t1);
            put32(result.data()+8, t2);
            put32(result.data()+12, std::min({t1, t2, l.sjw_max}));
            put32(result.data()+16, brp);
        }
        if (l.brp_inc > l.brp_max - brp) break;
    }
    return found;
}
}

#include <rphand/hand.hpp>
#include "rphand/protocol.hpp"
#include <iostream>
#include <type_traits>
#include "rphand/backends/gs_usb_protocol.hpp"
#include <stdexcept>
#include <vector>

static_assert(std::is_same_v<decltype(static_cast<bool (rphand::Hand::*)(const std::string&, int)>(&rphand::Hand::connect)),
    bool (rphand::Hand::*)(const std::string&, int)>);

static int public_api_checks() {
    rphand::Hand hand;
    int failures = 0;
    auto expect = [&](bool condition, const char* name) {
        if (!condition) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
    };
    rphand::ConnectionOptions options;
    expect(options.bitrate == 1000000 && options.data_bitrate == 5000000, "product CAN FD defaults");
    expect(options.node_id == 1 && options.channel == 0 && options.interface == "can0", "connection defaults");
    options.backend = rphand::Backend::GsUsbWinUsb;
    expect(!hand.connect(options), "missing USB serial rejected");
    options.serial = "F000";
    options.bitrate = 0;
    expect(!hand.connect(options), "invalid arbitration rate rejected");
    options.bitrate = 1000000;
    options.data_bitrate = -1;
    expect(!hand.connect(options), "invalid data rate rejected");
    options.data_bitrate = 5000000;
    options.channel = 1;
    expect(!hand.connect(options), "unsupported USB channel rejected");
    expect(rphand::valid_feedback(0x50, 8, 64), "captured 8-slot position frame");
    expect(rphand::valid_feedback(0x5a, 31, 64), "captured 31-slot status frame");
    expect(rphand::valid_feedback(0x50, 6, 56), "six-axis position frame with fixed status offset");
    expect(rphand::valid_feedback(0x5a, 6, 16), "six-slot status frame");
    expect(!rphand::valid_feedback(0x50, 6, 48), "missing trailing position status bytes");
    expect(!rphand::valid_feedback(0x50, 8, 48), "truncated position frame");
    expect(!rphand::valid_feedback(0x5a, 31, 16), "truncated status frame");
    expect(!rphand::valid_feedback(0x50, 5, 64), "missing required axes");
    expect(!hand.is_connected() && !hand.get_state().feedback_valid, "initial state invalid");
    expect(!hand.connect("", 1), "empty interface rejected");
    expect(!hand.connect("can0", 0), "invalid node rejected");
    expect(!hand.set_position(0, 0), "broadcast not public");
    expect(!hand.set_positions({0, 0, 0, 0, 0, 10001}), "position range");
    expect(!hand.set_speed(0) && !hand.set_current_limit(3001), "parameter ranges");
    expect(!hand.enable() && !hand.home() && !hand.stop() && !hand.disable(), "offline operations fail");
    expect(!hand.prepare_disconnect(), "offline cleanup cannot confirm device state");
    hand.disconnect();
    hand.disconnect();
    // A deliberately overlong name cannot refer to a Linux network interface.
    for (int attempt = 0; attempt < 2; ++attempt) {
        expect(!hand.connect("rphand_nonexistent_interface", 1), "failed connect releases worker/owner");
        expect(!hand.is_connected() && !hand.get_state().feedback_valid, "failed connection state");
        hand.disconnect();
    }
    std::cout << "Basic smoke failures: " << failures << '\n';
    return failures ? 1 : 0;
}


using namespace rphand::detail;
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void receive_stream_checks() {
    gs::Packet feedback{}, echo_packet{};
    unsigned char payload[64]{};
    for (unsigned i = 0; i < 64; ++i) payload[i] = static_cast<unsigned char>(i);
    gs::encode(feedback, 0, 0, 0x481, payload, 64, false);
    gs::put32(feedback.data(), gs::rx_echo);
    gs::encode(echo_packet, 2, 0, 0x601, payload, 8, false);
    std::vector<unsigned char> stream;
    for (const auto& record : {feedback, echo_packet, feedback})
        stream.insert(stream.end(), record.begin(), record.end());
    // Exercise every split position, including multiple records per read and
    // the old 128-byte read length. No frame or echo may be lost/duplicated.
    for (unsigned chunk = 1; chunk <= stream.size(); ++chunk) {
        gs::ReceiveBuffer buffer;
        unsigned records = 0, echo = 99;
        CanFrame frame{};
        const char* reason = "";
        for (unsigned offset = 0; offset < stream.size();) {
            const unsigned count = std::min(chunk, unsigned(stream.size()) - offset);
            check(buffer.append(stream.data()+offset, count, offset+count == stream.size()), "append stream chunk");
            offset += count;
            for (;;) {
                const auto kind = buffer.next(frame, echo, &reason);
                if (kind == gs::Kind::Incomplete) break;
                check(records < 3, "no duplicate records");
                if (records == 1) check(kind == gs::Kind::Echo && echo == 2, "coalesced echo preserved");
                else check(kind == gs::Kind::Frame && frame.can_id == 0x481 && frame.len == 64 &&
                           std::memcmp(frame.data, payload, 64) == 0, "split feedback reassembled intact");
                ++records;
            }
        }
        check(records == 3, "all records delivered");
    }
    gs::ReceiveBuffer buffer;
    CanFrame frame{}; unsigned echo = 99; const char* reason = "";
    // Exact user log: 64 payload bytes followed by 64 bytes of an FD TX echo.
    // Supply the missing preceding header and following padding to reproduce a
    // read that begins mid-record, without inventing a new frame at its start.
    const std::string raw =
        "5008000000005400000000001f00000000003100000000002f00000000002100"
        "0000000044000000000021000000000044000000000000000000000000001717"
        "000000000106000008000600400e201d00000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000";
    std::vector<unsigned char> captured;
    for (unsigned i = 0; i < raw.size(); i += 2)
        captured.push_back(static_cast<unsigned char>(std::stoul(raw.substr(i, 2), nullptr, 16)));
    check(captured.size() == 128, "captured read length");
    check(buffer.append(feedback.data(), 12, false), "preceding header");
    check(buffer.next(frame, echo, &reason) == gs::Kind::Incomplete, "preserve split header");
    check(buffer.append(captured.data(), 128, false), "captured read");
    check(buffer.next(frame, echo, &reason) == gs::Kind::Frame && frame.data[0] == 0x50 &&
          std::memcmp(frame.data, captured.data(), 64) == 0, "captured feedback payload stays aligned");
    check(buffer.next(frame, echo, &reason) == gs::Kind::Incomplete, "retain trailing partial echo");
    unsigned char padding[12]{};
    check(buffer.append(padding, 12, true), "echo padding");
    check(buffer.next(frame, echo, &reason) == gs::Kind::Echo && echo == 0, "captured echo reassembled");
    check(buffer.next(frame, echo, &reason) == gs::Kind::Incomplete, "capture fully drained");

    // A short USB transfer preserves compact replies and header-only echoes.
    gs::encode(feedback, 0, 0, 0x581, payload, 8, false);
    gs::put32(feedback.data(), gs::rx_echo);
    for (unsigned flags : {0u, 6u}) {
        feedback[10] = static_cast<unsigned char>(flags);
        check(buffer.append(feedback.data(), 20, true), "compact reply");
        check(buffer.next(frame, echo, &reason) == gs::Kind::Frame && frame.len == 8, "classic/compact FD reply");
        check(buffer.next(frame, echo, &reason) == gs::Kind::Incomplete, "compact reply drained");
    }
    check(buffer.append(echo_packet.data(), 12, true), "compact echo");
    check(buffer.next(frame, echo, &reason) == gs::Kind::Echo && echo == 2, "header-only echo retained");
    check(buffer.append(nullptr, 0, true), "USB ZLP");
    check(buffer.next(frame, echo, &reason) == gs::Kind::Incomplete, "ZLP creates no record");
    check(buffer.append(feedback.data(), 17, true), "truncated short transfer");
    check(buffer.next(frame, echo, &reason) == gs::Kind::Failure, "do not hide a truncated record");
    buffer = {};
    gs::put32(echo_packet.data(), 0x850);
    check(buffer.append(echo_packet.data(), 76, true), "invalid echo");
    check(buffer.next(frame, echo, &reason) == gs::Kind::Failure && std::string(reason) == "invalid TX echo ID",
          "genuinely invalid echo still fails without scanning for another header");
}
int main() {
    if (public_api_checks() != 0) return 1;
    try {
        receive_stream_checks();
        gs::Packet packet{};
        unsigned char data[64]{};
        for (unsigned i = 0; i < 64; ++i) data[i] = static_cast<unsigned char>(i + 1);
        for (unsigned size = 0; size <= 64; ++size) {
            check(gs::encode(packet, 3, 0, 0x501, data, size, false), "encode FD lengths");
            check(packet[0] == 3 && packet[4] == 1 && packet[5] == 5 && packet[10] == 6, "header");
            const unsigned padded = gs::lengths[packet[8]];
            check(padded >= size && (!packet[8] || gs::lengths[packet[8]-1] < size), "minimal DLC");
            for (unsigned i = size; i < 64; ++i) check(packet[12+i] == 0, "zero padding");
            CanFrame frame; unsigned echo = 99;
            check(gs::decode(packet.data(), 76, 0, frame, echo) == gs::Kind::Echo && echo == 3, "echo is not feedback");
            check(gs::decode(packet.data(), 12, 0, frame, echo) == gs::Kind::Echo, "header-only echo");
            gs::put32(packet.data(), gs::rx_echo);
            check(gs::decode(packet.data(), 76, 0, frame, echo) == gs::Kind::Frame, "FD decode");
            check(frame.len == padded && frame.can_id == 0x501 && frame.fd && frame.brs, "frame properties");
            check(gs::decode(packet.data(), 12+padded, 0, frame, echo) == gs::Kind::Frame, "compact RX packet");
            if (padded)
                check(gs::decode(packet.data(), 11+padded, 0, frame, echo) == gs::Kind::Failure, "truncated compact RX");
            for (unsigned i = 0; i < size; ++i) check(frame.data[i] == data[i], "payload");
        }
        check(!gs::encode(packet, 0, 0, 0x800, data, 8, false), "standard ID bounds");
        check(!gs::encode(packet, 0, 0, 1, data, 65, false), "payload bounds");
        check(!gs::encode(packet, 10, 0, 1, data, 8, false), "echo bounds");
        check(!gs::encode(packet, 0, 0, 1, nullptr, 8, false), "null payload");
        gs::encode(packet, 0, 0, 0x481, data, 64, false);
        gs::put32(packet.data(), gs::rx_echo);
        CanFrame frame; unsigned echo = 0;
        check(gs::decode(packet.data(), 75, 0, frame, echo) == gs::Kind::Failure, "truncated FD");
        check(gs::decode(packet.data(), 11, 0, frame, echo) == gs::Kind::Failure, "truncated header");
        check(gs::decode(packet.data(), 76, 1, frame, echo) == gs::Kind::Ignore, "other channel");
        packet[8] = 16;
        check(gs::decode(packet.data(), 76, 0, frame, echo) == gs::Kind::Failure, "invalid DLC");
        packet[8] = 8; packet[10] = 0;
        check(gs::decode(packet.data(), 20, 0, frame, echo) == gs::Kind::Frame && frame.len == 8 && !frame.fd, "classic reply");
        packet[10] = 1;
        check(gs::decode(packet.data(), 20, 0, frame, echo) == gs::Kind::Failure, "overflow is fatal");
        packet[10] = 0; gs::put32(packet.data()+4, 0x20000040);
        check(gs::decode(packet.data(), 20, 0, frame, echo) == gs::Kind::Failure, "bus-off invalidates session");
        gs::put32(packet.data()+4, 0x80000481);
        check(gs::decode(packet.data(), 20, 0, frame, echo) == gs::Kind::Ignore, "extended frame filtered");
        gs::put32(packet.data()+4, 0x40000481);
        check(gs::decode(packet.data(), 20, 0, frame, echo) == gs::Kind::Ignore, "RTR filtered");

        const char* reason = "";
        check(gs::decode(nullptr, 0, 0, frame, echo, &reason) == gs::Kind::Ignore, "USB ZLP ignored");
        check(gs::decode(packet.data(), 5, 0, frame, echo, &reason) == gs::Kind::Failure &&
              std::string(reason) == "short gs_usb header", "precise truncation diagnostic");
        packet = {};
        gs::put32(packet.data(), gs::rx_echo);
        gs::put32(packet.data()+4, 0x20000204);
        packet[8] = 8;
        packet[13] = 0x40;
        frame.can_id = 0x481;
        check(gs::decode(packet.data(), 20, 0, frame, echo, &reason) == gs::Kind::Status,
              "error-active controller notification is not a fatal CAN error");
        check(frame.can_id == 0x481, "controller status must not become hand feedback");
        check(gs::decode(packet.data(), 19, 0, frame, echo, &reason) == gs::Kind::Failure,
              "truncated status is still fatal");
        packet[10] = 1;
        check(gs::decode(packet.data(), 20, 0, frame, echo, &reason) == gs::Kind::Failure &&
              std::string(reason) == "adapter RX overflow", "overflow cannot hide behind normal status");
        packet[10] = 0;
        for (unsigned status : {0u, 1u, 2u, 4u, 8u, 16u, 32u, 0x44u}) {
            packet[13] = static_cast<unsigned char>(status);
            check(gs::decode(packet.data(), 20, 0, frame, echo) == gs::Kind::Failure,
                  "unknown/warning/passive/overflow controller status remains fatal");
        }
        packet[13] = 0x40;
        packet[18] = 96;
        check(gs::decode(packet.data(), 20, 0, frame, echo) == gs::Kind::Failure, "warning counters remain fatal");
        packet[18] = 0;
        for (unsigned classes : {0x40u, 0x244u, 0x20u, 0x224u, 0x100u, 0x304u, 0x8u, 0u}) {
            gs::put32(packet.data()+4, 0x20000000u | classes);
            check(gs::decode(packet.data(), 20, 0, frame, echo) == gs::Kind::Failure,
                  "bus-off/ACK/restart/protocol/unknown error remains fatal");
        }
        gs::put32(packet.data()+4, 0x20000200);
        packet[13] = 0;
        check(gs::decode(packet.data(), 20, 0, frame, echo) == gs::Kind::Status, "zero counters are informational");
        packet[19] = 1;
        check(gs::decode(packet.data(), 20, 0, frame, echo) == gs::Kind::Failure, "nonzero counter-only report remains conservative");

        // Constants captured by read-only queries from the actual A8FA:8598.
        gs::Limits nominal{2,256,2,128,128,2,512,2};
        gs::Limits fast{1,32,1,16,16,1,32,2};
        for (auto limits : {nominal, fast}) {
            for (unsigned rate : {500000u,1000000u,2000000u,5000000u}) {
                std::array<unsigned char,20> t{};
                check(gs::timing(80000000,rate,limits,t), "representable timing");
                auto t1=gs::get32(t.data()+4), t2=gs::get32(t.data()+8), brp=gs::get32(t.data()+16);
                check(std::uint64_t(rate)*brp*(1+t1+t2)==80000000, "exact bitrate");
                check(t1>=limits.t1_min && t1<=limits.t1_max && t2>=limits.t2_min && t2<=limits.t2_max, "segment limits");
                check(brp>=limits.brp_min && brp<=limits.brp_max && brp%limits.brp_inc==0, "prescaler increment");
                check(gs::get32(t.data()+12)<=std::min({limits.sjw_max,t1,t2}), "SJW limits");
            }
        }
        std::array<unsigned char,20> t{};
        check(!gs::timing(80000000,1234567,nominal,t), "inexact bitrate rejected");
        nominal.brp_inc=0;
        check(!gs::timing(80000000,1000000,nominal,t), "invalid device limits rejected");
        std::cout << "gs_usb framing and timing checks passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}

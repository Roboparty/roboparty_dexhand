#include "rphand/backends/gs_usb_drain.hpp"
#include "rphand/backends/gs_usb_protocol.hpp"
#include <chrono>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace rphand::detail;
using namespace std::chrono_literals;
namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
struct Endpoint {
    struct Read {
        gs::DrainRead state;
        std::chrono::milliseconds delay;
        std::vector<unsigned char> data;
    };
    std::chrono::steady_clock::time_point time{};
    std::deque<Read> pending;
    unsigned consumed = 0;
    gs::DrainRead read(unsigned& bytes) {
        if (pending.empty()) {
            time += 20ms;
            return gs::DrainRead::Timeout;
        }
        auto result = pending.front();
        pending.pop_front();
        time += result.delay;
        bytes = static_cast<unsigned>(result.data.size());
        consumed += bytes;
        return result.state;
    }
    gs::DrainResult drain() {
        return gs::drain_stopped_rx([&](unsigned& bytes) { return read(bytes); }, [&] { return time; });
    }
};
}
int main() {
    try {
        Endpoint empty;
        auto result = empty.drain();
        check(result.status == gs::DrainStatus::Quiet && result.reads == 2 && !result.bytes,
              "empty endpoint must establish a 40 ms quiet interval");

        // Interrupt an FD record at the 64-byte USB boundary. Its remaining
        // twelve bytes reproduce the user's 0x16161616 invalid echo exactly.
        gs::Packet packet{};
        unsigned char payload[64]{};
        payload[0] = 0x50; payload[1] = 6;
        for (unsigned i = 50; i < 56; ++i) payload[i] = 0x16;
        gs::encode(packet, 0, 0, 0x481, payload, 64, false);
        gs::put32(packet.data(), gs::rx_echo);
        gs::ReceiveBuffer parser;
        CanFrame frame{}; unsigned echo = 0; const char* reason = "";
        parser.append(packet.data()+64, 12, true);
        check(parser.next(frame, echo, &reason) == gs::Kind::Failure && echo == 0x16161616,
              "undrained old frame tail must reproduce the reported failure");

        Endpoint stale;
        stale.pending.push_back({gs::DrainRead::Data, 1ms, {packet.begin()+64, packet.end()}});
        stale.pending.push_back({gs::DrainRead::Timeout, 20ms, {}});
        // A single timeout is not sufficient: another old transfer arrives late.
        stale.pending.push_back({gs::DrainRead::Data, 5ms, {packet.begin(), packet.begin()+64}});
        stale.pending.push_back({gs::DrainRead::Data, 1ms, {packet.begin()+64, packet.end()}});
        result = stale.drain();
        check(result.status == gs::DrainStatus::Quiet && result.bytes == 88 && result.reads == 6
              && stale.pending.empty() && stale.consumed == 88,
              "drain must consume both stale transfers, including delayed bytes");
        // Only newly received bytes enter the parser after CAN START.
        parser = {};
        parser.append(packet.data(), 64, false);
        check(parser.next(frame, echo, &reason) == gs::Kind::Incomplete, "retain new partial frame");
        parser.append(packet.data()+64, 12, true);
        check(parser.next(frame, echo, &reason) == gs::Kind::Frame && frame.can_id == 0x481,
              "new session feedback stays aligned after draining the old endpoint");

        Endpoint disconnected;
        disconnected.pending.push_back({gs::DrainRead::Failure, 1ms, {}});
        check(disconnected.drain().status == gs::DrainStatus::ReadFailure,
              "USB removal/read failure must not be treated as a quiet endpoint");

        auto time = std::chrono::steady_clock::time_point{};
        result = gs::drain_stopped_rx([&](unsigned& bytes) {
            time += 1ms; bytes = 64; return gs::DrainRead::Data;
        }, [&] { return time; });
        check(result.status == gs::DrainStatus::Limit && result.reads == 500,
              "continuous data must hit the deadline");

        result = gs::drain_stopped_rx([](unsigned& bytes) {
            bytes = 1024; return gs::DrainRead::Data;
        }, [&] { return time; });
        check(result.status == gs::DrainStatus::Limit && result.bytes == 65536 && result.reads == 64,
              "fast data must hit the byte budget");

        result = gs::drain_stopped_rx([](unsigned& bytes) {
            bytes = 0; return gs::DrainRead::Data;
        }, [&] { return time; });
        check(result.status == gs::DrainStatus::Limit && result.reads == 1024,
              "zero-length completions must not spin forever or count as quiet");

        result = gs::drain_stopped_rx([&](unsigned&) {
            time += 500ms; return gs::DrainRead::Timeout;
        }, [&] { return time; });
        check(result.status == gs::DrainStatus::Limit,
              "a read exceeding the overall deadline must fail closed");
        std::cout << "Stopped USB drain and reconnect checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

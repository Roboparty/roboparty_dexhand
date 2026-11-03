#pragma once
#include <chrono>

namespace rphand::detail::gs {
enum class DrainRead { Data, Timeout, Failure };
enum class DrainStatus { Quiet, ReadFailure, Limit };
struct DrainResult {
    DrainStatus status;
    unsigned bytes = 0, reads = 0;
};

// Only use after a successful CAN RESET and before START (or during shutdown).
// Read must have a bounded timeout. Injecting the clock/read lets the tests
// emulate device-side leftovers and delayed packets without physical hardware.
template<class Read, class Now>
DrainResult drain_stopped_rx(Read read, Now now) {
    using namespace std::chrono_literals;
    const auto deadline = now() + 500ms;
    auto quiet_since = now();
    DrainResult result{DrainStatus::Limit};
    while (result.reads < 1024 && result.bytes < 64 * 1024 && now() < deadline) {
        unsigned bytes = 0;
        const auto state = read(bytes);
        ++result.reads;
        if (state == DrainRead::Failure) {
            result.status = DrainStatus::ReadFailure;
            return result;
        }
        const auto time = now();
        if (state == DrainRead::Data) {
            if (bytes >= 64 * 1024 - result.bytes) {
                result.bytes = 64 * 1024;
                return result;
            }
            result.bytes += bytes;
            // Even zero-length completions count as activity, not proof that
            // the stopped endpoint is quiet. The read limit bounds ZLP storms.
            quiet_since = time;
        }
        if (time >= deadline) return result;
        if (state == DrainRead::Timeout && time - quiet_since >= 40ms) {
            result.status = DrainStatus::Quiet;
            return result;
        }
    }
    return result;
}
}

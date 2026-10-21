#pragma once
#include <rphand/types.hpp>
#include <atomic>
#include <cstdint>
#include <memory>

namespace rphand::detail {
struct CanFrame {
    std::uint32_t can_id = 0;
    unsigned char len = 0;
    unsigned char data[64]{};
    bool fd = true, brs = true, extended = false;
};

// One sender and one receiver may run concurrently. close() follows their exit.
// receive() must return within a bounded interval, including after USB removal.
class CanTransport {
public:
    virtual ~CanTransport() = default;
    virtual bool open(const ConnectionOptions&) = 0;
    virtual void close() = 0;
    virtual bool send(unsigned id, const unsigned char*, unsigned size, int extended) = 0;
    // 1: frame, 0: timeout/ignored frame, -1: transport failure.
    virtual int receive(CanFrame&) = 0;
    unsigned long errors() const { return errors_.load(); }
protected:
    std::atomic<unsigned long> errors_{0};
};
std::unique_ptr<CanTransport> make_transport(Backend backend);
}

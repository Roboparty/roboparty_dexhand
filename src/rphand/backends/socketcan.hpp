#pragma once

#include "../transport.hpp"
#include <string>

namespace rphand::detail {
class SocketCan : public CanTransport {
public:
    ~SocketCan() override;
    bool open(const ConnectionOptions& options) override;
    void close() override;
    bool send(unsigned id, const unsigned char* data, unsigned size, int extended) override;
    // 1 = frame, 0 = poll timeout/interruption, -1 = transport failure.
    int receive(CanFrame& frame) override;
private:
    int fd_ = -1;
};
}  // namespace rphand::detail

#pragma once

#include <atomic>
#include <linux/can.h>
#include <string>

namespace rphand::detail {
class SocketCan {
public:
    ~SocketCan();
    bool open(const std::string& name, unsigned node);
    void close();
    bool send(unsigned id, const unsigned char* data, unsigned size, int extended);
    // 1 = frame, 0 = poll timeout/interruption, -1 = transport failure.
    int receive(canfd_frame& frame);
    unsigned long errors() const { return errors_.load(); }
private:
    int fd_ = -1;
    std::atomic<unsigned long> errors_{0};
};
}  // namespace rphand::detail

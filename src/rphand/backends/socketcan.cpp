#include "socketcan.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace rphand::detail {
SocketCan::~SocketCan() { close(); }
void SocketCan::close() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}
bool SocketCan::open(const ConnectionOptions& options) {
    const auto& name = options.interface;
    const unsigned node = options.node_id;
    close();
    const unsigned index = if_nametoindex(name.c_str());
    if (!index) {
        std::cerr << "[rphand] CAN interface not found: " << name << '\n';
        return false;
    }
    fd_ = socket(PF_CAN, SOCK_RAW | SOCK_CLOEXEC, CAN_RAW);
    if (fd_ < 0) return false;
    ifreq info{};
    std::strncpy(info.ifr_name, name.c_str(), IFNAMSIZ - 1);
    int fd_enabled = 1;
    can_filter filters[2] = {
        {0x480u + node, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG},
        {0x580u + node, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG}};
    sockaddr_can address{};
    address.can_family = AF_CAN;
    address.can_ifindex = static_cast<int>(index);
    timeval timeout{0, 100000};
    const bool ok = ioctl(fd_, SIOCGIFMTU, &info) == 0 && info.ifr_mtu == CANFD_MTU
        && ioctl(fd_, SIOCGIFFLAGS, &info) == 0 && (info.ifr_flags & IFF_UP)
        && setsockopt(fd_, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &fd_enabled, sizeof(fd_enabled)) == 0
        && setsockopt(fd_, SOL_CAN_RAW, CAN_RAW_FILTER, filters, sizeof(filters)) == 0
        && setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0
        && bind(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    if (!ok) {
        std::cerr << "[rphand] CAN open failed (interface must be UP with FD MTU): "
                  << name << ": " << std::strerror(errno) << '\n';
        close();
    }
    return ok;
}
bool SocketCan::send(unsigned id, const unsigned char* data, unsigned size, int extended) {
    if (fd_ < 0 || size > 64 || (!data && size) || id > (extended ? CAN_EFF_MASK : CAN_SFF_MASK)) {
        ++errors_;
        return false;
    }
    canfd_frame frame{};
    frame.can_id = id | (extended ? CAN_EFF_FLAG : 0);
    unsigned length = size;
    for (unsigned legal : {12u, 16u, 20u, 24u, 32u, 48u, 64u}) {
        if (size > 8 && legal >= size) { length = legal; break; }
    }
    frame.len = static_cast<__u8>(length);
    frame.flags = CANFD_BRS;
    if (size) std::memcpy(frame.data, data, size);
    const bool ok = write(fd_, &frame, CANFD_MTU) == CANFD_MTU;
    if (!ok) {
        ++errors_;
        std::cerr << "[rphand] CAN write failed: " << std::strerror(errno) << '\n';
    }
    return ok;
}
int SocketCan::receive(CanFrame& output) {
    canfd_frame frame{};
    pollfd pfd{fd_, POLLIN, 0};
    const int rc = poll(&pfd, 1, 20);
    if (rc < 0 && errno == EINTR) return 0;
    if (rc < 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) return -1;
    if (!rc) return 0;
    const auto count = read(fd_, &frame, sizeof(frame));
    if (count < 0 && errno == EINTR) return 0;
    if (count != CAN_MTU && count != CANFD_MTU) return -1;
    if (frame.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_ERR_FLAG)) return 0;
    if (frame.len > (count == CAN_MTU ? 8 : 64)) return 0;
    output = {};
    output.can_id = frame.can_id;
    output.len = frame.len;
    output.fd = count == CANFD_MTU;
    output.brs = output.fd && (frame.flags & CANFD_BRS);
    std::memcpy(output.data, frame.data, frame.len);
    return 1;
}
}  // namespace rphand::detail

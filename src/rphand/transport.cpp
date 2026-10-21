#include "transport.hpp"
#ifdef _WIN32
#include "backends/gs_usb_winusb.hpp"
#else
#include "backends/socketcan.hpp"
#endif

namespace rphand::detail {
std::unique_ptr<CanTransport> make_transport(Backend backend) {
#ifdef _WIN32
    if (backend == Backend::GsUsbWinUsb) return std::make_unique<GsUsbWinUsb>();
#else
    if (backend == Backend::SocketCan) return std::make_unique<SocketCan>();
#endif
    return {};
}
}

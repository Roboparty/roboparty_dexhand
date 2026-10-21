#include "rphand/backends/gs_usb_winusb.hpp"
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: rphand_usb_probe SERIAL (read-only; does not start CAN)\n";
        return 2;
    }
    return rphand::detail::GsUsbWinUsb::inspect(argv[1]) ? 0 : 1;
}

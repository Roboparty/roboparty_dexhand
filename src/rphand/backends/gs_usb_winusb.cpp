#include "gs_usb_winusb.hpp"
#include "gs_usb_protocol.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <setupapi.h>
#include <winusb.h>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <vector>

namespace rphand::detail {
namespace {
bool failure(const char* operation) {
    std::cerr << "[rphand] WinUSB " << operation << " failed, Windows error="
              << GetLastError() << '\n';
    return false;
}
bool reject(const char* reason) {
    std::cerr << "[rphand] gs_usb: " << reason << '\n';
    return false;
}
void packet_log(const char* reason, const unsigned char* bytes, unsigned size) {
    std::ostringstream line;
    line << "[rphand] gs_usb: " << reason << " bytes=" << size;
    if (size >= 12) {
        line << " echo=0x" << std::hex << gs::get32(bytes)
             << " can_id=0x" << gs::get32(bytes + 4) << std::dec
             << " dlc=" << unsigned(bytes[8]) << " channel=" << unsigned(bytes[9])
             << " flags=0x" << std::hex << unsigned(bytes[10]) << std::dec;
    }
    line << " raw=" << std::hex << std::setfill('0');
    for (unsigned i = 0; i < size; ++i) line << std::setw(2) << unsigned(bytes[i]);
    std::cerr << line.str() << '\n';
}
// The HHS interface GUID and the generic WinUSB interface GUID exposed by
// this device. Scope this first backend to A8FA:8598, channel zero.
const GUID interfaces[] = {
    {0x13eb360b,0xbc1e,0x46cb,{0xac,0x8b,0xef,0x3d,0xa4,0x7b,0x40,0x62}},
    {0xdee824ef,0x729b,0x4a0e,{0x9c,0x14,0xb7,0x11,0x7d,0x33,0xa8,0x17}}
};
std::wstring device_path(const std::string& serial) {
    // This adapter's serial is ASCII hexadecimal; don't accept a partial path.
    if (serial.empty() || serial.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return {};
    std::wstring needle = L"usb#vid_a8fa&pid_8598#";
    needle.append(serial.begin(), serial.end()); needle += L"#";
    for (auto& ch : needle) ch = static_cast<wchar_t>(std::towlower(ch));
    for (const auto& guid : interfaces) {
        HDEVINFO set = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) continue;
        std::wstring found;
        for (DWORD index = 0;; ++index) {
            SP_DEVICE_INTERFACE_DATA item{}; item.cbSize = sizeof(item);
            if (!SetupDiEnumDeviceInterfaces(set, nullptr, &guid, index, &item)) break;
            DWORD bytes = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &item, nullptr, 0, &bytes, nullptr);
            if (bytes < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
            std::vector<unsigned char> storage(bytes);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
            detail->cbSize = sizeof(*detail);
            if (!SetupDiGetDeviceInterfaceDetailW(set, &item, detail, bytes, nullptr, nullptr)) continue;
            std::wstring path = detail->DevicePath, lower = path;
            for (auto& ch : lower) ch = static_cast<wchar_t>(std::towlower(ch));
            if (lower.find(needle) != std::wstring::npos) { found = path; break; }
        }
        SetupDiDestroyDeviceInfoList(set);
        if (!found.empty()) return found;
    }
    return {};
}
}

struct GsUsbWinUsb::Impl {
    HANDLE file = INVALID_HANDLE_VALUE;
    WINUSB_INTERFACE_HANDLE usb = nullptr;
    UCHAR input = 0, output = 0;
    unsigned input_packet_size = 0;
    gs::ReceiveBuffer receive_buffer;
    bool started = false;
    unsigned node = 1;
    std::array<unsigned char, 12> config{};
    std::array<unsigned char, 72> capabilities{};
    std::mutex mutex;
    std::condition_variable changed;
    std::array<bool, gs::tx_slots> pending{};
    bool failed = false;
    unsigned status_messages = 0;
    bool send_failure_reported = false;

    ~Impl() { close(); }
    bool control(bool read, UCHAR request, void* data, USHORT size) {
        WINUSB_SETUP_PACKET setup{};
        setup.RequestType = read ? 0xc1 : 0x41;
        setup.Request = request;
        // Device-wide format/config requests use Value=1; others select channel 0.
        setup.Value = request == 0 || request == 5 ? 1 : 0;
        setup.Length = size; // Index=interface=0.
        ULONG actual = 0;
        if (!WinUsb_ControlTransfer(usb, setup, static_cast<PUCHAR>(data), size, &actual, nullptr))
            return failure("control transfer");
        return actual == size || reject("short control transfer");
    }
    void close() {
        if (usb) {
            if (started) {
                std::array<unsigned char, 8> reset{};
                control(false, 2, reset.data(), static_cast<USHORT>(reset.size()));
            }
            WinUsb_Free(usb); usb = nullptr;
        }
        if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); file = INVALID_HANDLE_VALUE; }
        started = false; input = output = 0; input_packet_size = 0;
        receive_buffer = {};
        std::lock_guard<std::mutex> lock(mutex);
        failed = true; pending.fill(false); changed.notify_all();
    }
    bool access(const std::string& serial) {
        const auto path = device_path(serial);
        if (path.empty()) return reject("A8FA:8598 with this serial not found (WinUSB driver required)");
        // Exclusive file handle: a second process must not reset our CAN channel.
        file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (file == INVALID_HANDLE_VALUE) return failure("open device");
        if (!WinUsb_Initialize(file, &usb)) return failure("initialize");
        ULONG timeout = 1000;
        if (!WinUsb_SetPipePolicy(usb, 0, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout))
            return failure("control timeout");
        USB_DEVICE_DESCRIPTOR descriptor{}; ULONG actual = 0;
        if (!WinUsb_GetDescriptor(usb, USB_DEVICE_DESCRIPTOR_TYPE, 0, 0,
                reinterpret_cast<PUCHAR>(&descriptor), sizeof(descriptor), &actual))
            return failure("device descriptor");
        if (actual != sizeof(descriptor) || descriptor.idVendor != 0xa8fa || descriptor.idProduct != 0x8598)
            return reject("unexpected USB device");
        USB_INTERFACE_DESCRIPTOR iface{};
        if (!WinUsb_QueryInterfaceSettings(usb, 0, &iface)) return failure("interface descriptor");
        if (iface.bInterfaceNumber != 0) return reject("only USB interface zero is supported");
        for (UCHAR i = 0; i < iface.bNumEndpoints; ++i) {
            WINUSB_PIPE_INFORMATION pipe{};
            if (!WinUsb_QueryPipe(usb, 0, i, &pipe)) return failure("endpoint query");
            // gs_usb uses the first bulk IN/OUT pair; 0x82 on this device is not CAN RX.
            if (pipe.PipeType == UsbdPipeTypeBulk) {
                if ((pipe.PipeId & 0x80) && !input) {
                    input = pipe.PipeId;
                    input_packet_size = pipe.MaximumPacketSize;
                }
                if (!(pipe.PipeId & 0x80) && !output) output = pipe.PipeId;
            }
        }
        if (!input || !output) return reject("CAN bulk endpoints missing");
        if (!input_packet_size || input_packet_size > gs::ReceiveBuffer::max_chunk)
            return reject("unsupported USB maximum packet size");
        if (!control(true, 5, config.data(), 12) ||
            !control(true, 4, capabilities.data(), 40)) return false;
        const auto features = gs::get32(capabilities.data());
        if (config[3] != 0) return reject("this backend currently supports single-channel devices");
        if (!(features & gs::fd_feature) || !(features & gs::extended_timing))
            return reject("CAN FD and extended bit timing are required");
        if (features & (1u << 9)) return reject("USB packet quirk is not supported");
        return control(true, 11, capabilities.data(), 72);
    }
    bool broken() {
        std::lock_guard<std::mutex> lock(mutex);
        failed = true; changed.notify_all();
        return false;
    }
};

GsUsbWinUsb::GsUsbWinUsb() : impl_(std::make_unique<Impl>()) {}
GsUsbWinUsb::~GsUsbWinUsb() { close(); }
void GsUsbWinUsb::close() { impl_->close(); }
bool GsUsbWinUsb::inspect(const std::string& serial) {
    Impl device;
    if (!device.access(serial)) return false;
    std::cout << "gs_usb CAN FD: serial=" << serial
              << " channels=" << unsigned(device.config[3]) + 1
              << " firmware=" << gs::get32(device.config.data()+4)
              << " hardware=" << gs::get32(device.config.data()+8)
              << " clock=" << gs::get32(device.capabilities.data()+4)
              << " features=0x" << std::hex << gs::get32(device.capabilities.data()) << std::dec
              << " rx_packet_size=" << device.input_packet_size
              << " (read-only; CAN not started)\n";
    return true;
}
bool GsUsbWinUsb::open(const ConnectionOptions& options) {
    close();
    if (options.channel != 0 || options.bitrate <= 0 || options.data_bitrate <= 0 ||
        options.node_id < 1 || options.node_id > 127) return reject("invalid connection options");
    auto& d = *impl_;
    auto initialize = [&] {
        if (!d.access(options.serial)) return false;
        std::array<unsigned char, 20> nominal{}, data{};
        const auto clock = gs::get32(d.capabilities.data()+4);
        if (!gs::timing(clock, options.bitrate, gs::limits(d.capabilities.data()+8), nominal) ||
            !gs::timing(clock, options.data_bitrate, gs::limits(d.capabilities.data()+40), data))
            return reject("requested bitrates cannot be represented by device timing limits");
        ULONG timeout = 20;
        if (!WinUsb_SetPipePolicy(d.usb, d.input, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout))
            return failure("receive timeout");
        // Read exactly one USB packet at a time. Keep its boundary so a short
        // transfer can delimit compact firmware replies; assemble full records
        // across full USB packets and retain all records in a coalesced packet.
        UCHAR disabled = FALSE;
        if (!WinUsb_SetPipePolicy(d.usb, d.input, IGNORE_SHORT_PACKETS, sizeof(disabled), &disabled) ||
            !WinUsb_SetPipePolicy(d.usb, d.input, ALLOW_PARTIAL_READS, sizeof(disabled), &disabled))
            return failure("receive framing policy");
        timeout = 100;
        if (!WinUsb_SetPipePolicy(d.usb, d.output, PIPE_TRANSFER_TIMEOUT, sizeof(timeout), &timeout))
            return failure("send timeout");
        std::array<unsigned char, 4> format{};
        gs::put32(format.data(), 0x0000beef);
        std::array<unsigned char, 8> mode{};
        if (!d.control(false, 0, format.data(), 4) || !d.control(false, 2, mode.data(), 8))
            return false;
        // Discard host-side leftovers only while CAN is stopped, before START.
        if (!WinUsb_FlushPipe(d.usb, d.input)) return failure("flush receive cache");
        d.receive_buffer = {};
        if (!d.control(false, 1, nominal.data(), 20) || !d.control(false, 10, data.data(), 20)) return false;
        gs::put32(mode.data(), 1);
        gs::put32(mode.data()+4, gs::fd_feature);
        // Attempt a reset on cleanup even if the START request times out.
        d.started = true;
        if (!d.control(false, 2, mode.data(), 8)) return false;
        d.node = options.node_id;
        {
            std::lock_guard<std::mutex> lock(d.mutex);
            d.failed = false; d.pending.fill(false);
            d.status_messages = 0; d.send_failure_reported = false;
        }
        errors_ = 0;
        return true;
    };
    if (initialize()) return true;
    close(); return false;
}
bool GsUsbWinUsb::send(unsigned id, const unsigned char* data, unsigned size, int extended) {
    auto& d = *impl_;
    gs::Packet packet{};
    unsigned slot = 0;
    {
        std::unique_lock<std::mutex> lock(d.mutex);
        const bool available = d.changed.wait_for(lock, std::chrono::milliseconds(100), [&] {
            return d.failed || std::find(d.pending.begin(), d.pending.end(), false) != d.pending.end();
        });
        if (!available || d.failed || !d.started) {
            ++errors_;
            if (!d.send_failure_reported) {
                reject(!available ? "TX echo slots exhausted (100 ms timeout)" :
                       d.failed ? "send rejected: transport already failed; see first receive/write error" :
                                  "send rejected: channel not started");
                d.send_failure_reported = true;
            }
            return false;
        }
        slot = static_cast<unsigned>(std::find(d.pending.begin(), d.pending.end(), false) - d.pending.begin());
        if (!gs::encode(packet, slot, 0, id, data, size, extended != 0)) { ++errors_; return false; }
        d.pending[slot] = true;
    }
    ULONG sent = 0;
    if (!WinUsb_WritePipe(d.usb, d.output, packet.data(), static_cast<ULONG>(packet.size()), &sent, nullptr)) {
        ++errors_; failure("write"); return d.broken();
    }
    if (sent != packet.size()) { ++errors_; reject("short USB write"); return d.broken(); }
    return true; // CAN completion arrives as an echo, never as device feedback.
}
int GsUsbWinUsb::receive(CanFrame& frame) {
    auto& d = *impl_;
    { std::lock_guard<std::mutex> lock(d.mutex); if (d.failed) return -1; }
    unsigned echo = 0;
    const char* reason = "";
    auto kind = d.receive_buffer.next(frame, echo, &reason);
    if (kind == gs::Kind::Incomplete) {
        std::array<unsigned char, gs::ReceiveBuffer::max_chunk> packet{};
        ULONG received = 0;
        if (!WinUsb_ReadPipe(d.usb, d.input, packet.data(), d.input_packet_size, &received, nullptr)) {
            if (GetLastError() == ERROR_SEM_TIMEOUT) return 0;
            failure("read"); d.broken(); return -1;
        }
        if (!d.receive_buffer.append(packet.data(), received, received < d.input_packet_size)) {
            reject("USB receive assembly capacity exceeded"); d.broken(); return -1;
        }
        kind = d.receive_buffer.next(frame, echo, &reason);
    }
    switch (kind) {
    case gs::Kind::Failure:
        packet_log(reason, d.receive_buffer.record_data(), d.receive_buffer.record_size()); d.broken(); return -1;
    case gs::Kind::Status:
        if (d.status_messages++ < 4) packet_log(reason, d.receive_buffer.record_data(), d.receive_buffer.record_size());
        return 0;
    case gs::Kind::Echo: {
        std::lock_guard<std::mutex> lock(d.mutex);
        if (!d.pending[echo]) {
            packet_log("unexpected TX echo (no pending transmission)", d.receive_buffer.record_data(), d.receive_buffer.record_size());
            d.failed = true; d.changed.notify_all(); return -1;
        }
        d.pending[echo] = false; d.changed.notify_all(); return 0;
    }
    case gs::Kind::Incomplete:
    case gs::Kind::Ignore: return 0;
    case gs::Kind::Frame:
        return frame.can_id == 0x480 + d.node || frame.can_id == 0x580 + d.node ? 1 : 0;
    }
    return -1;
}
}

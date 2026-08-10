#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace roboparty::dexhand::detail {

struct CanFdFrame {
  std::uint32_t id{0};
  bool extended{false};
  bool brs{false};
  std::uint8_t len{0};
  std::array<std::uint8_t, 64> data{};
};

class CanFdTransport {
 public:
  using ReceiveCallback = std::function<void(const CanFdFrame&)>;
  virtual ~CanFdTransport() = default;
  virtual bool open(const std::string& interface,
                    const std::vector<std::uint32_t>& standard_ids) = 0;
  virtual bool transmit(const CanFdFrame& frame) noexcept = 0;
  virtual void set_receive_callback(ReceiveCallback callback) = 0;
  virtual void clear_receive_callback() noexcept = 0;
  virtual void close() noexcept = 0;
};

}  // namespace roboparty::dexhand::detail

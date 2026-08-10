#pragma once

#include "protocol/callback_gate.hpp"
#include "protocol/canfd_transport.hpp"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/socket.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

namespace roboparty::dexhand::detail {

class SocketOps {
 public:
  virtual ~SocketOps() = default;
  virtual int socket(int domain, int type, int protocol) noexcept = 0;
  virtual int set_option(int fd, int level, int name, const void* value,
                         socklen_t size) noexcept = 0;
  virtual int interface_index(int fd, const std::string& name) noexcept = 0;
  virtual int bind(int fd, const sockaddr_can& address) noexcept = 0;
  virtual int poll_readable(int fd, int timeout_ms) noexcept = 0;
  virtual ssize_t read(int fd, canfd_frame& frame) noexcept = 0;
  virtual ssize_t write(int fd, const canfd_frame& frame) noexcept = 0;
  virtual int close(int fd) noexcept = 0;
  virtual int last_error() const noexcept = 0;
};

class LinuxSocketOps final : public SocketOps {
 public:
  int socket(int domain, int type, int protocol) noexcept override;
  int set_option(int fd, int level, int name, const void* value,
                 socklen_t size) noexcept override;
  int interface_index(int fd, const std::string& name) noexcept override;
  int bind(int fd, const sockaddr_can& address) noexcept override;
  int poll_readable(int fd, int timeout_ms) noexcept override;
  ssize_t read(int fd, canfd_frame& frame) noexcept override;
  ssize_t write(int fd, const canfd_frame& frame) noexcept override;
  int close(int fd) noexcept override;
  int last_error() const noexcept override;
};

class SocketCanFdTransport final : public CanFdTransport {
 public:
  explicit SocketCanFdTransport(
      std::shared_ptr<SocketOps> ops = std::make_shared<LinuxSocketOps>());
  ~SocketCanFdTransport() override;
  bool open(const std::string& interface,
            const std::vector<std::uint32_t>& standard_ids) override;
  bool transmit(const CanFdFrame& frame) noexcept override;
  void set_receive_callback(ReceiveCallback callback) override;
  void clear_receive_callback() noexcept override;
  void close() noexcept override;

 private:
  void clear_receive_callback_locked_() noexcept;
  void close_locked_() noexcept;
  void receive_loop_() noexcept;
  void dispatch_(const canfd_frame& frame) noexcept;

  std::shared_ptr<SocketOps> ops_;
  int fd_{-1};
  std::atomic<bool> running_{false};
  std::thread receive_thread_;
  std::mutex lifecycle_mutex_;
  std::mutex transmit_mutex_;
  std::mutex callback_mutex_;
  ReceiveCallback receive_callback_;
  CallbackGate receive_gate_;
};

}  // namespace roboparty::dexhand::detail

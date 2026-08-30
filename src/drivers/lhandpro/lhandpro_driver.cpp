// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include "drivers/lhandpro/lhandpro_driver.hpp"

#include "logging.hpp"
#include "protocol/callback_gate.hpp"
#include "protocol/socket_canfd_transport.hpp"

#include <linux/can.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace roboparty::dexhand::detail {

struct TxContext {
  CallbackGate gate;
  CanFdTransport* transport{nullptr};
};

struct SlotToken {};

class SdoAckTracker final {
 public:
  enum class Result { Acknowledged, Aborted, TimedOut, Cancelled };

  struct Ticket {
    std::uint64_t generation{0};
    bool valid{false};
  };

  struct Observation {
    bool recognized{false};
    bool matched{false};
    Ticket ticket{};
    Result result{Result::Cancelled};
  };

  void start_session() noexcept {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      accepting_ = true;
      state_ = State::Idle;
      captured_ = false;
      ++generation_;
    } catch (...) {
    }
  }

  void shutdown() noexcept {
    try {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = false;
        captured_ = false;
        if (state_ == State::Pending) state_ = State::Cancelled;
      }
      cv_.notify_all();
    } catch (...) {
    }
  }

  Ticket arm(unsigned int index, unsigned char subindex) noexcept {
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!accepting_ || state_ == State::Pending) return {};
      ++generation_;
      expected_index_ = index;
      expected_subindex_ = subindex;
      state_ = State::Pending;
      captured_ = false;
      return {generation_, true};
    } catch (...) {
      return {};
    }
  }

  Observation observe(const CanFdFrame& frame,
                      std::uint32_t response_id) noexcept {
    Observation observation;
    if (frame.extended || frame.id != response_id || frame.len != 8U) {
      return observation;
    }

    const auto command = frame.data[0];
    constexpr std::array<std::uint8_t, 8> kSaveCompatibilityProbe{
        0x00U, 0x10U, 0x10U, 0x00U, 0x20U, 0x00U, 0x00U, 0x00U};
    const bool save_compatibility_probe = std::equal(
        kSaveCompatibilityProbe.begin(), kSaveCompatibilityProbe.end(),
        frame.data.begin());
    if (!save_compatibility_probe && command != 0x60U && command != 0x80U) {
      return observation;
    }
    const auto index = static_cast<unsigned int>(frame.data[1]) |
                       (static_cast<unsigned int>(frame.data[2]) << 8U);
    const auto subindex = frame.data[3];

    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (save_compatibility_probe) {
        if (accepting_ && state_ == State::Pending &&
            expected_index_ == 0x1010U && expected_subindex_ == 0x01U) {
          observation.recognized = true;
        }
        return observation;
      }
      const bool matches = accepting_ && state_ == State::Pending &&
                           !captured_ && index == expected_index_ &&
                           subindex == expected_subindex_;
      if (command == 0x80U && !matches) return observation;

      observation.recognized = true;
      if (!matches) return observation;
      captured_ = true;
      observation.matched = true;
      observation.ticket = {generation_, true};
      observation.result = command == 0x60U ? Result::Acknowledged
                                             : Result::Aborted;
      return observation;
    } catch (...) {
      return observation;
    }
  }

  void complete(const Observation& observation) noexcept {
    if (!observation.matched || !observation.ticket.valid) return;
    try {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::Pending ||
            observation.ticket.generation != generation_ || !captured_) {
          return;
        }
        state_ = observation.result == Result::Acknowledged
                     ? State::Acknowledged
                     : State::Aborted;
        captured_ = false;
      }
      cv_.notify_all();
    } catch (...) {
    }
  }

  void discard(Ticket ticket) noexcept {
    if (!ticket.valid) return;
    try {
      std::lock_guard<std::mutex> lock(mutex_);
      if (ticket.generation != generation_) return;
      state_ = State::Idle;
      captured_ = false;
    } catch (...) {
    }
  }

  Result wait(Ticket ticket, std::chrono::milliseconds timeout) noexcept {
    if (!ticket.valid) return Result::Cancelled;
    try {
      std::unique_lock<std::mutex> lock(mutex_);
      const auto complete = [this, ticket] {
        return ticket.generation != generation_ ||
               state_ != State::Pending;
      };
      if (!cv_.wait_for(lock, timeout, complete)) {
        if (ticket.generation == generation_ && state_ == State::Pending) {
          state_ = State::TimedOut;
          captured_ = false;
        }
        return Result::TimedOut;
      }
      if (ticket.generation != generation_) return Result::Cancelled;
      switch (state_) {
        case State::Acknowledged:
          return Result::Acknowledged;
        case State::Aborted:
          return Result::Aborted;
        case State::TimedOut:
          return Result::TimedOut;
        case State::Idle:
        case State::Pending:
        case State::Cancelled:
          return Result::Cancelled;
      }
    } catch (...) {
    }
    return Result::Cancelled;
  }

 private:
  enum class State {
    Idle,
    Pending,
    Acknowledged,
    Aborted,
    TimedOut,
    Cancelled
  };

  std::mutex mutex_;
  std::condition_variable cv_;
  bool accepting_{false};
  bool captured_{false};
  std::uint64_t generation_{0};
  unsigned int expected_index_{0};
  unsigned char expected_subindex_{0};
  State state_{State::Idle};
};

}  // namespace roboparty::dexhand::detail

namespace {

using roboparty::dexhand::detail::CanFdFrame;
using roboparty::dexhand::detail::CanFdTransport;
using roboparty::dexhand::detail::CapiLHandProSdk;
using roboparty::dexhand::detail::DriverState;
using roboparty::dexhand::detail::ExpectedDof;
using roboparty::dexhand::detail::LHandProModel;
using roboparty::dexhand::detail::SlotToken;
using roboparty::dexhand::detail::SocketCanFdTransport;
using roboparty::dexhand::detail::TxContext;

constexpr int kSdkSuccess = 0;
constexpr int kSdkException = -1;
constexpr int kValidationFailure = -2;
constexpr int kTransactionCancelled = -3;
constexpr int kSdoAckTimeout = -4;
constexpr int kSdoAbort = -5;
constexpr int kCanFdMode = 1;
constexpr int kVendorModel6DofS = 1;
constexpr int kVendorModel16Dof = 2;
constexpr int kRealtimeFeedbackConfigAttempts = 3;
constexpr auto kRealtimeFeedbackTargetPeriod = std::chrono::milliseconds(20);
constexpr auto kSdoAckTimeoutPeriod = std::chrono::milliseconds(100);
constexpr unsigned int kSaveSdoIndex = 0x1010U;
constexpr unsigned char kSaveSdoSubindex = 0x01U;
constexpr std::array<std::uint8_t, 6> kRealtimeFeedbackEveryBasePeriod{
    0x00U, 0x04U, 0x50U, 0x01U, 0x5AU, 0x01U};

std::mutex process_callback_mutex;
std::weak_ptr<TxContext> active_tx_context;
std::weak_ptr<SlotToken> active_slot_owner;

void log_boundary_error(const char* message) noexcept {
  roboparty::dexhand::detail::with_dexhand_logger(
      [message](spdlog::logger& logger) { logger.error("{}", message); });
}

bool claim_process_slot(const std::shared_ptr<SlotToken>& owner) noexcept {
  if (!owner) return false;
  try {
    std::lock_guard<std::mutex> lock(process_callback_mutex);
    if (!active_slot_owner.expired()) return false;
    active_slot_owner = owner;
    return true;
  } catch (...) {
    log_boundary_error("LHandPro process slot claim failed");
    return false;
  }
}

void release_process_slot(const std::shared_ptr<SlotToken>& owner) noexcept {
  if (!owner) return;
  try {
    std::lock_guard<std::mutex> lock(process_callback_mutex);
    if (active_slot_owner.lock() == owner) active_slot_owner.reset();
  } catch (...) {
    log_boundary_error("LHandPro process slot release failed");
  }
}

bool publish_context(const std::shared_ptr<TxContext>& context) noexcept {
  if (!context || !context->transport) return false;
  try {
    std::lock_guard<std::mutex> lock(process_callback_mutex);
    if (!active_tx_context.expired()) return false;
    context->gate.open();
    active_tx_context = context;
    return true;
  } catch (...) {
    log_boundary_error("LHandPro transmit context publication failed");
    return false;
  }
}

void unpublish_context(const std::shared_ptr<TxContext>& context) noexcept {
  if (!context) return;
  try {
    {
      std::lock_guard<std::mutex> lock(process_callback_mutex);
      if (active_tx_context.lock() == context) active_tx_context.reset();
    }
    context->gate.close_and_wait();
  } catch (...) {
    log_boundary_error("LHandPro transmit context shutdown failed");
  }
}

bool transmit_bridge(unsigned int id, const unsigned char* data,
                     unsigned int size, int is_extended) noexcept {
  try {
    std::shared_ptr<TxContext> context;
    {
      std::lock_guard<std::mutex> lock(process_callback_mutex);
      context = active_tx_context.lock();
    }
    if (!context || !context->transport || (!data && size > 0) || size > 64) {
      return false;
    }

    auto lease = context->gate.try_enter();
    if (!lease) return false;

    CanFdFrame frame;
    frame.extended = is_extended != 0;
    if ((frame.extended && id > CAN_EFF_MASK) ||
        (!frame.extended && id > CAN_SFF_MASK)) {
      return false;
    }
    frame.id = id;
    frame.len = static_cast<std::uint8_t>(size);
    frame.brs = size > 8;
    if (size > 0) std::copy_n(data, size, frame.data.begin());
    return context->transport->transmit(frame);
  } catch (const std::exception& error) {
    roboparty::dexhand::detail::with_dexhand_logger(
        [&error](spdlog::logger& logger) {
          logger.error("LHandPro transmit callback exception: {}",
                       error.what());
        });
  } catch (...) {
    log_boundary_error("LHandPro transmit callback exception");
  }
  return false;
}

}  // namespace

LHandProDriver::LHandProDriver(std::string can_interface,
                               LHandProModel model, int canfd_node_id)
    : LHandProDriver(std::move(can_interface), model, canfd_node_id,
                     std::make_unique<CapiLHandProSdk>(),
                     std::make_unique<SocketCanFdTransport>()) {}

LHandProDriver::LHandProDriver(
    std::string can_interface, LHandProModel model, int canfd_node_id,
    std::unique_ptr<roboparty::dexhand::detail::LHandProSdk> sdk,
    std::unique_ptr<CanFdTransport> transport)
    : model_(model),
      sdk_(std::move(sdk)),
      transport_(std::move(transport)),
      state_(DriverState::Created),
      sdo_ack_tracker_(
          std::make_unique<roboparty::dexhand::detail::SdoAckTracker>()) {
  if (can_interface.empty()) {
    throw std::invalid_argument("CAN interface must not be empty");
  }
  if (canfd_node_id < 1 || canfd_node_id > 127) {
    throw std::invalid_argument("CAN-FD node ID must be in [1, 127]");
  }
  if (model != LHandProModel::Dof6S && model != LHandProModel::Dof16) {
    throw std::invalid_argument("Unsupported LHandPro model");
  }
  if (!sdk_ || !transport_) {
    throw std::invalid_argument("SDK and transport must not be null");
  }

  can_interface_ = std::move(can_interface);
  canfd_node_id_ = canfd_node_id;
  comm_type_ = HandCommType::CANFD;
}

LHandProDriver::~LHandProDriver() {
  try {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    (void)cleanup_locked_();
  } catch (...) {
    log_boundary_error("LHandPro driver destruction cleanup failed");
  }
}

bool LHandProDriver::sdk_ok_(int code, const char* operation) const noexcept {
  if (code == kSdkSuccess) return true;
  try {
    logger_->error("LHandPro {} failed: SDK error={}", operation, code);
  } catch (...) {
  }
  return false;
}

void LHandProDriver::record_fault_(const char* operation, int code,
                                   FaultSource source) noexcept {
  try {
    std::lock_guard<std::mutex> health_lock(health_mutex_);
    if (!first_fault_) first_fault_ = FaultRecord{operation, code, source};
  } catch (...) {
  }

  auto state = state_.load(std::memory_order_acquire);
  while ((state == DriverState::Initializing || state == DriverState::Ready) &&
         !state_.compare_exchange_weak(state, DriverState::Faulted,
                                       std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
  }
}

void LHandProDriver::record_cleanup_fault_(const char* operation,
                                           int code) noexcept {
  try {
    std::lock_guard<std::mutex> health_lock(health_mutex_);
    const FaultRecord fault{operation, code, FaultSource::Cleanup};
    if (!first_fault_) {
      first_fault_ = fault;
      return;
    }
    if (cleanup_fault_count_ < cleanup_faults_.size()) {
      cleanup_faults_[cleanup_fault_count_++] = fault;
    }
  } catch (...) {
  }
}

bool LHandProDriver::begin_rx_callback_() noexcept {
  bool pending_counted = false;
  bool active_counted = false;
  try {
    if (rx_entry_attempt_hook_for_test_) rx_entry_attempt_hook_for_test_();
    {
      std::lock_guard<std::mutex> entry_lock(rx_entry_registration_mutex_);
      pending_rx_callbacks_.fetch_add(1, std::memory_order_acq_rel);
      pending_counted = true;
    }
    if (rx_registered_hook_for_test_) rx_registered_hook_for_test_();

    std::lock_guard<std::mutex> admission_lock(rx_init_admission_mutex_);
    pending_rx_callbacks_.fetch_sub(1, std::memory_order_acq_rel);
    pending_counted = false;
    active_rx_callbacks_.fetch_add(1, std::memory_order_acq_rel);
    active_counted = true;
  } catch (...) {
    if (pending_counted) {
      pending_rx_callbacks_.fetch_sub(1, std::memory_order_acq_rel);
    }
    if (active_counted) {
      active_rx_callbacks_.fetch_sub(1, std::memory_order_acq_rel);
    }
    record_fault_("decode_canfd", kSdkException, FaultSource::Async);
    rx_init_admission_cv_.notify_all();
    return false;
  }
  rx_init_admission_cv_.notify_all();
  return true;
}

void LHandProDriver::finish_rx_callback_(bool failed, int code) noexcept {
  bool decrement_needed = true;
  try {
    std::lock_guard<std::mutex> admission_lock(rx_init_admission_mutex_);
    if (failed) record_fault_("decode_canfd", code, FaultSource::Async);
    active_rx_callbacks_.fetch_sub(1, std::memory_order_acq_rel);
    decrement_needed = false;
  } catch (...) {
    record_fault_("decode_canfd", failed ? code : kSdkException,
                  FaultSource::Async);
    if (decrement_needed) {
      active_rx_callbacks_.fetch_sub(1, std::memory_order_acq_rel);
    }
  }
  rx_init_admission_cv_.notify_all();
}

bool LHandProDriver::initialization_healthy_() const noexcept {
  try {
    std::lock_guard<std::mutex> health_lock(health_mutex_);
    return !first_fault_ &&
           state_.load(std::memory_order_acquire) == DriverState::Initializing;
  } catch (...) {
    return false;
  }
}

const char* LHandProDriver::fault_source_name_(FaultSource source) noexcept {
  switch (source) {
    case FaultSource::Sync:
      return "sync";
    case FaultSource::Async:
      return "async";
    case FaultSource::Cleanup:
      return "cleanup";
  }
  return "unknown";
}

std::string LHandProDriver::fault_message_(const FaultRecord& fault) {
  return std::string("LHandPro ") + fault.operation + " failed: code=" +
         std::to_string(fault.code) +
         ", source=" + fault_source_name_(fault.source);
}

std::string LHandProDriver::fault_report_() const {
  std::lock_guard<std::mutex> health_lock(health_mutex_);
  if (!first_fault_) return {};

  std::string report = fault_message_(*first_fault_);
  for (std::size_t index = 0; index < cleanup_fault_count_; ++index) {
    report += "; attached cleanup fault: ";
    report += fault_message_(cleanup_faults_[index]);
  }
  return report;
}

[[noreturn]] void LHandProDriver::throw_sticky_fault_() const {
  std::string message = fault_report_();
  if (message.empty()) message = "LHandPro driver faulted without a root cause";
  throw std::runtime_error(message);
}

void LHandProDriver::check_health() const {
  const std::string message = fault_report_();
  if (!message.empty()) throw std::runtime_error(message);
  if (state_.load(std::memory_order_acquire) == DriverState::Faulted) {
    throw std::runtime_error("LHandPro driver faulted without a root cause");
  }
}

void LHandProDriver::validate_call_state_(bool allow_faulted,
                                          const char* operation) const {
  const auto state = state_.load(std::memory_order_acquire);
  if (state == DriverState::Ready ||
      (state == DriverState::Faulted && allow_faulted)) {
    return;
  }
  if (state == DriverState::Faulted) throw_sticky_fault_();
  throw std::logic_error(std::string("LHandPro ") + operation +
                         " requires Ready state");
}

void LHandProDriver::validate_provisioning_call_(
    const char* operation, std::uint64_t generation) const {
  if (session_generation_.load(std::memory_order_acquire) != generation) {
    throw std::logic_error(std::string("LHandPro ") + operation +
                           " provisioning session changed");
  }
  validate_call_state_(false, operation);
  if (model_ != LHandProModel::Dof6S ||
      session_purpose_.load(std::memory_order_acquire) !=
          SessionPurpose::Provisioning) {
    throw std::logic_error(std::string("LHandPro ") + operation +
                           " requires a Dof6S provisioning session");
  }
}

bool LHandProDriver::provisioning_epoch_active_(
    std::uint64_t generation) const noexcept {
  return state_.load(std::memory_order_acquire) == DriverState::Ready &&
         session_purpose_.load(std::memory_order_acquire) ==
             SessionPurpose::Provisioning &&
         session_generation_.load(std::memory_order_acquire) == generation;
}

void LHandProDriver::throw_if_sdk_failed_(int code, const char* operation) {
  if (code == kSdkSuccess) return;
  try {
    logger_->error("LHandPro {} failed: SDK error={}", operation, code);
  } catch (...) {
  }

  const FaultRecord failure{operation, code, FaultSource::Sync};
  record_fault_(operation, code, FaultSource::Sync);
  throw std::runtime_error(fault_message_(failure));
}

int LHandProDriver::expected_vendor_model_() const noexcept {
  return model_ == LHandProModel::Dof6S ? kVendorModel6DofS
                                       : kVendorModel16Dof;
}

ExpectedDof LHandProDriver::expected_dof_() const noexcept {
  return model_ == LHandProModel::Dof6S ? ExpectedDof{11, 6}
                                       : ExpectedDof{21, 16};
}

bool LHandProDriver::init_hand(bool enable_motors, bool home_motors,
                               float home_wait_time) {
  return init_session_(SessionPurpose::Motion, enable_motors, home_motors,
                       home_wait_time);
}

bool LHandProDriver::init_for_provisioning() {
  if (model_ != LHandProModel::Dof6S) return false;
  return init_session_(SessionPurpose::Provisioning, false, false, 0.0F);
}

bool LHandProDriver::init_session_(SessionPurpose purpose, bool enable_motors,
                                   bool home_motors,
                                   float home_wait_time) {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  const auto current = state_.load(std::memory_order_acquire);
  if (current == DriverState::Ready) {
    const bool same_purpose =
        session_purpose_.load(std::memory_order_acquire) == purpose;
    const bool same_args = session_enable_motors_ == enable_motors &&
                           session_home_motors_ == home_motors;
    if (same_purpose && same_args) return true;
    // An already-initialized driver keeps its session: re-running init with
    // different arguments would silently report success while ignoring the
    // new enable/home values. Fail explicitly instead; callers must
    // deinit_hand() first (repeating an identical call stays idempotent).
    try {
      logger_->error(
          "init_hand on an initialized driver with different arguments is "
          "rejected; deinit_hand() before re-initializing");
    } catch (...) {
    }
    return false;
  }
  if (current != DriverState::Created) return false;
  if (!std::isfinite(home_wait_time) || home_wait_time < 0.0F) {
    try {
      logger_->error("home_wait_time must be finite and nonnegative");
    } catch (...) {
    }
    return false;
  }

  session_generation_.fetch_add(1, std::memory_order_acq_rel);
  sdo_ack_tracker_->start_session();
  session_purpose_.store(purpose, std::memory_order_release);
  session_enable_motors_ = enable_motors;
  session_home_motors_ = home_motors;
  safety_cleanup_required_ = purpose == SessionPurpose::Motion;

  {
    std::lock_guard<std::mutex> health_lock(health_mutex_);
    first_fault_.reset();
    cleanup_faults_.fill(FaultRecord{});
    cleanup_fault_count_ = 0;
    state_.store(DriverState::Initializing, std::memory_order_release);
  }
  initial_ex_attempted_ = false;
  safety_cleanup_attempted_ = false;
  auto fail = [this]() noexcept {
    (void)cleanup_locked_();
    return false;
  };
  const auto run_init_sdk_call =
      [this](auto&& command, const char* operation) noexcept {
        int code = kSdkException;
        try {
          code = command();
        } catch (...) {
        }
        if (sdk_ok_(code, operation)) return true;
        record_fault_(operation, code, FaultSource::Sync);
        return false;
      };

  try {
    tx_context_ = std::make_shared<TxContext>();
    tx_context_->transport = transport_.get();
    slot_token_ = std::make_shared<SlotToken>();
    if (!claim_process_slot(slot_token_)) {
      record_fault_("claim_process_slot", kSdkException, FaultSource::Sync);
      return fail();
    }
    slot_claimed_ = true;
    if (!publish_context(tx_context_)) {
      record_fault_("publish_context", kSdkException, FaultSource::Sync);
      return fail();
    }

    if (!sdk_->create()) {
      sdk_ok_(kSdkException, "create");
      record_fault_("create", kSdkException, FaultSource::Sync);
      return fail();
    }
    sdk_created_ = true;

    if (!run_init_sdk_call(
            [this] { return sdk_->set_hand_type(expected_vendor_model_()); },
            "set_hand_type")) {
      return fail();
    }
    int reported_model = -1;
    if (!run_init_sdk_call(
            [this, &reported_model] {
              return sdk_->get_hand_type(reported_model);
            },
            "get_hand_type")) {
      return fail();
    }
    if (reported_model != expected_vendor_model_()) {
      try {
        logger_->error("LHandPro model readback mismatch: expected={}, got={}",
                       expected_vendor_model_(), reported_model);
      } catch (...) {
      }
      record_fault_("get_hand_type", kValidationFailure, FaultSource::Sync);
      return fail();
    }

    const std::vector<std::uint32_t> response_ids{
        static_cast<std::uint32_t>(0x500 + canfd_node_id_),
        static_cast<std::uint32_t>(0x480 + canfd_node_id_),
        static_cast<std::uint32_t>(0x580 + canfd_node_id_),
        static_cast<std::uint32_t>(0x180 + canfd_node_id_)};
    if (!transport_->open(can_interface_, response_ids)) {
      record_fault_("transport.open", kSdkException, FaultSource::Sync);
      return fail();
    }
    transport_open_ = true;

    transport_->set_receive_callback([this](const CanFdFrame& frame) noexcept {
      if (!begin_rx_callback_()) return;
      const auto sdo_observation = sdo_ack_tracker_->observe(
          frame, static_cast<std::uint32_t>(0x580 + canfd_node_id_));
      if (sdo_observation.recognized) {
        try {
          // The vendor decoder still receives write ACKs to clear any pending
          // internal request, but its return value is not authoritative.
          (void)sdk_->decode_canfd(frame.id, frame.data.data(),
                                   static_cast<int>(frame.len));
        } catch (...) {
        }
        finish_rx_callback_(false, kSdkSuccess);
        sdo_ack_tracker_->complete(sdo_observation);
        return;
      }
      try {
        if (state_.load(std::memory_order_acquire) == DriverState::Created) {
          finish_rx_callback_(false, kSdkSuccess);
          sdo_ack_tracker_->complete(sdo_observation);
          return;
        }
        const int code = sdk_->decode_canfd(frame.id, frame.data.data(),
                                            static_cast<int>(frame.len));
        const bool failed = !sdk_ok_(code, "decode_canfd");
        finish_rx_callback_(failed, code);
      } catch (...) {
        sdk_ok_(kSdkException, "decode_canfd");
        finish_rx_callback_(true, kSdkException);
      }
      sdo_ack_tracker_->complete(sdo_observation);
    });

    sdk_->set_send_canfd_callback(&transmit_bridge);
    tx_callback_installed_ = true;

    communication_started_ = true;
    initial_ex_attempted_ = true;
    if (!run_init_sdk_call(
            [this] { return sdk_->initial_ex(kCanFdMode, canfd_node_id_); },
            "initial_ex")) {
      return fail();
    }
    bool healthy_before_feedback_config = false;
    {
      std::lock_guard<std::mutex> admission_lock(
          rx_init_admission_mutex_);
      healthy_before_feedback_config = initialization_healthy_();
    }
    if (!healthy_before_feedback_config) return fail();

    CanFdFrame feedback_config;
    feedback_config.id =
        static_cast<std::uint32_t>(0x500 + canfd_node_id_);
    feedback_config.len =
        static_cast<std::uint8_t>(kRealtimeFeedbackEveryBasePeriod.size());
    std::copy(kRealtimeFeedbackEveryBasePeriod.begin(),
              kRealtimeFeedbackEveryBasePeriod.end(),
              feedback_config.data.begin());
    // The firmware has no acknowledgement for this idempotent command and may
    // ignore a frame near initial_ex. Span three complete target periods.
    for (int attempt = 0; attempt < kRealtimeFeedbackConfigAttempts;
         ++attempt) {
      if (!transport_->transmit(feedback_config)) {
        record_fault_("configure_realtime_feedback", kSdkException,
                      FaultSource::Sync);
        return fail();
      }
      std::this_thread::sleep_for(kRealtimeFeedbackTargetPeriod);

      bool healthy_after_feedback_config = false;
      {
        std::lock_guard<std::mutex> admission_lock(
            rx_init_admission_mutex_);
        healthy_after_feedback_config = initialization_healthy_();
      }
      if (!healthy_after_feedback_config) return fail();
    }

    sdk_->start_monitor();
    monitor_started_ = true;

    reported_model = -1;
    if (!run_init_sdk_call(
            [this, &reported_model] {
              return sdk_->get_hand_type(reported_model);
            },
            "get_hand_type")) {
      return fail();
    }
    if (reported_model != expected_vendor_model_()) {
      try {
        logger_->error("LHandPro model readback mismatch: expected={}, got={}",
                       expected_vendor_model_(), reported_model);
      } catch (...) {
      }
      record_fault_("get_hand_type", kValidationFailure, FaultSource::Sync);
      return fail();
    }

    int total = 0;
    int active = 0;
    const auto expected_dof = expected_dof_();
    if (!run_init_sdk_call(
            [this, &total, &active] { return sdk_->get_dof(total, active); },
            "get_dof")) {
      return fail();
    }
    if (total != expected_dof.total || active != expected_dof.active) {
      try {
        logger_->error(
            "LHandPro DOF validation failed: expected total={}, active={}; "
            "got total={}, active={}",
            expected_dof.total, expected_dof.active, total, active);
      } catch (...) {
      }
      record_fault_("get_dof", kValidationFailure, FaultSource::Sync);
      return fail();
    }
    {
      std::lock_guard<std::mutex> dof_lock(dof_mutex_);
      dof_total_ = total;
      dof_active_ = active;
    }

    const auto run_admitted_init_command =
        [this, &run_init_sdk_call](auto&& command, const char* operation) {
          std::unique_lock<std::mutex> admission_lock(
              rx_init_admission_mutex_);
          rx_init_admission_cv_.wait(admission_lock, [this] {
            return pending_rx_callbacks_.load(std::memory_order_acquire) == 0 &&
                   active_rx_callbacks_.load(std::memory_order_acquire) == 0;
          });
          if (!initialization_healthy_()) return false;
          // Bundled SDK risk calls do not synchronously wait for RX callback
          // completion; future integrations must preserve that contract.
          return run_init_sdk_call(std::forward<decltype(command)>(command),
                                   operation);
        };

    if (purpose == SessionPurpose::Motion) {
      if (enable_motors) {
        if (!run_admitted_init_command(
                [this] { return sdk_->set_enable(0, true); }, "set_enable")) {
          return fail();
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
      if (home_motors) {
        if (!run_admitted_init_command(
                [this] { return sdk_->home_motors(0); }, "home_motors")) {
          return fail();
        }
        std::this_thread::sleep_for(
            std::chrono::duration<float>(home_wait_time));
      }
      if (!run_admitted_init_command(
              [this] { return sdk_->set_move_no_home(1); },
              "set_move_no_home")) {
        return fail();
      }
    }

    bool became_ready = false;
    {
      if (ready_transition_hook_for_test_) ready_transition_hook_for_test_();
      std::lock_guard<std::mutex> entry_lock(rx_entry_registration_mutex_);
      std::unique_lock<std::mutex> admission_lock(
          rx_init_admission_mutex_);
      rx_init_admission_cv_.wait(admission_lock, [this] {
        return pending_rx_callbacks_.load(std::memory_order_acquire) == 0 &&
               active_rx_callbacks_.load(std::memory_order_acquire) == 0;
      });
      if (final_commit_hook_for_test_) final_commit_hook_for_test_();
      if (initialization_healthy_()) {
        auto expected = DriverState::Initializing;
        became_ready = state_.compare_exchange_strong(
            expected, DriverState::Ready, std::memory_order_acq_rel,
            std::memory_order_acquire);
      }
    }
    if (!became_ready) return fail();
    return true;
  } catch (const std::exception& error) {
    record_fault_("initialization", kSdkException, FaultSource::Sync);
    try {
      logger_->error("LHandPro initialization exception: {}", error.what());
    } catch (...) {
    }
    return fail();
  } catch (...) {
    record_fault_("initialization", kSdkException, FaultSource::Sync);
    return fail();
  }
}

void LHandProDriver::deinit_hand() {
  std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
  if (state_.load(std::memory_order_acquire) == DriverState::Created &&
      !sdk_created_ && !transport_open_ && !tx_callback_installed_ &&
      !communication_started_ && !monitor_started_ && !slot_claimed_ &&
      !initial_ex_attempted_ && !safety_cleanup_attempted_ && !tx_context_ &&
      !slot_token_) {
    return;
  }
  const auto result = cleanup_locked_();
  if (result.failed) {
    throw std::runtime_error(fault_message_(result.first_failure));
  }
}

LHandProDriver::CleanupResult LHandProDriver::cleanup_locked_() noexcept {
  CleanupResult result;
  state_.store(DriverState::Stopping, std::memory_order_release);
  sdo_ack_tracker_->shutdown();

  {
    std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
    if (safety_cleanup_required_ && initial_ex_attempted_ &&
        !safety_cleanup_attempted_ && sdk_created_) {
      safety_cleanup_attempted_ = true;
      const auto attempt_safety_call =
          [this, &result](auto&& command, const char* operation) noexcept {
            int code = kSdkException;
            try {
              code = command();
            } catch (...) {
            }
            if (sdk_ok_(code, operation)) return;

            const FaultRecord failure{operation, code, FaultSource::Cleanup};
            if (!result.failed) {
              result.failed = true;
              result.first_failure = failure;
            }
            record_cleanup_fault_(operation, code);
          };
      attempt_safety_call([this] { return sdk_->stop_motors(0); },
                          "stop_motors");
      attempt_safety_call([this] { return sdk_->set_enable(0, false); },
                          "set_enable");
      attempt_safety_call([this] { return sdk_->set_move_no_home(0); },
                          "set_move_no_home");
    }
    if (monitor_started_) {
      sdk_->stop_monitor();
      monitor_started_ = false;
    }
  }

  if (transport_open_) transport_->clear_receive_callback();

  {
    std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
    if (communication_started_) {
      sdk_->close();
      communication_started_ = false;
    }
    if (tx_callback_installed_) {
      sdk_->set_send_canfd_callback(nullptr);
      tx_callback_installed_ = false;
    }
  }

  unpublish_context(tx_context_);

  if (transport_open_) {
    transport_->close();
    transport_open_ = false;
  }

  {
    std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
    if (sdk_created_) {
      sdk_->destroy();
      sdk_created_ = false;
    }
  }

  {
    std::lock_guard<std::mutex> dof_lock(dof_mutex_);
    dof_total_ = 0;
    dof_active_ = 0;
  }
  tx_context_.reset();
  if (slot_claimed_) release_process_slot(slot_token_);
  slot_claimed_ = false;
  slot_token_.reset();
  initial_ex_attempted_ = false;
  safety_cleanup_attempted_ = false;
  safety_cleanup_required_ = false;
  session_purpose_.store(SessionPurpose::Motion, std::memory_order_release);
  state_.store(DriverState::Created, std::memory_order_release);
  return result;
}

int LHandProDriver::set_sdo_drive_param_verified_(
    unsigned int index, unsigned char subindex, unsigned int value) noexcept {
  const auto ticket = sdo_ack_tracker_->arm(index, subindex);
  if (!ticket.valid) return kTransactionCancelled;

  const int code = sdk_->set_sdo_drive_param(index, subindex, value);
  if (code != kSdkSuccess) {
    sdo_ack_tracker_->discard(ticket);
    return code;
  }

  switch (sdo_ack_tracker_->wait(ticket, kSdoAckTimeoutPeriod)) {
    case roboparty::dexhand::detail::SdoAckTracker::Result::Acknowledged:
      return kSdkSuccess;
    case roboparty::dexhand::detail::SdoAckTracker::Result::Aborted:
      return kSdoAbort;
    case roboparty::dexhand::detail::SdoAckTracker::Result::TimedOut:
      return kSdoAckTimeout;
    case roboparty::dexhand::detail::SdoAckTracker::Result::Cancelled:
      return kTransactionCancelled;
  }
  return kTransactionCancelled;
}

int LHandProDriver::save_sdo_drive_param_verified_() noexcept {
  const auto ticket =
      sdo_ack_tracker_->arm(kSaveSdoIndex, kSaveSdoSubindex);
  if (!ticket.valid) return kTransactionCancelled;

  const int code = sdk_->save_sdo_drive_param();
  if (code != kSdkSuccess) {
    sdo_ack_tracker_->discard(ticket);
    return code;
  }

  switch (sdo_ack_tracker_->wait(ticket, kSdoAckTimeoutPeriod)) {
    case roboparty::dexhand::detail::SdoAckTracker::Result::Acknowledged:
      return kSdkSuccess;
    case roboparty::dexhand::detail::SdoAckTracker::Result::Aborted:
      return kSdoAbort;
    case roboparty::dexhand::detail::SdoAckTracker::Result::TimedOut:
      return kSdoAckTimeout;
    case roboparty::dexhand::detail::SdoAckTracker::Result::Cancelled:
      return kTransactionCancelled;
  }
  return kTransactionCancelled;
}

roboparty::dexhand::detail::FeedbackPeriodReport
LHandProDriver::show_feedback_period() {
  constexpr const char* operation = "show_feedback_period";
  const auto generation =
      session_generation_.load(std::memory_order_acquire);
  validate_provisioning_call_(operation, generation);
  if (provisioning_pre_lock_hook_for_test_) {
    provisioning_pre_lock_hook_for_test_();
  }
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_provisioning_call_(operation, generation);
  auto report = roboparty::dexhand::detail::LHandProFeedbackPeriod(*sdk_).show();
  validate_provisioning_call_(operation, generation);
  check_health();
  return report;
}

roboparty::dexhand::detail::FeedbackPeriodReport
LHandProDriver::apply_feedback_period_20ms() {
  constexpr const char* operation = "apply_feedback_period_20ms";
  const auto generation =
      session_generation_.load(std::memory_order_acquire);
  validate_provisioning_call_(operation, generation);
  if (provisioning_pre_lock_hook_for_test_) {
    provisioning_pre_lock_hook_for_test_();
  }
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_provisioning_call_(operation, generation);
  auto report = roboparty::dexhand::detail::LHandProFeedbackPeriod(
                    *sdk_, [this, generation] {
                      return provisioning_epoch_active_(generation);
                    },
                    [this](unsigned int index, unsigned char subindex,
                           unsigned int value) {
                      return set_sdo_drive_param_verified_(index, subindex,
                                                           value);
                    },
                    [this] { return save_sdo_drive_param_verified_(); })
                    .apply_20ms();
  validate_provisioning_call_(operation, generation);
  check_health();
  return report;
}

void LHandProDriver::move_motors(int id) {
  validate_call_state_(false, "move_motors");
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(false, "move_motors");
  throw_if_sdk_failed_(sdk_->move_motors(id), "move_motors");
}

void LHandProDriver::stop_motors(int id) {
  validate_call_state_(true, "stop_motors");
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(true, "stop_motors");
  throw_if_sdk_failed_(sdk_->stop_motors(id), "stop_motors");
}

void LHandProDriver::set_target_position(int id, int value) {
  validate_call_state_(false, "set_target_position");
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(false, "set_target_position");
  throw_if_sdk_failed_(sdk_->set_target_position(id, value),
                       "set_target_position");
}

void LHandProDriver::set_target_angle(int id, float value) {
  validate_call_state_(false, "set_target_angle");
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(false, "set_target_angle");
  throw_if_sdk_failed_(sdk_->set_target_angle(id, value),
                       "set_target_angle");
}

void LHandProDriver::set_position_velocity(int id, int value) {
  validate_call_state_(false, "set_position_velocity");
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(false, "set_position_velocity");
  throw_if_sdk_failed_(sdk_->set_position_velocity(id, value),
                       "set_position_velocity");
}

void LHandProDriver::set_max_current(int id, int value) {
  validate_call_state_(false, "set_max_current");
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(false, "set_max_current");
  throw_if_sdk_failed_(sdk_->set_max_current(id, value), "set_max_current");
}

void LHandProDriver::set_enable(int id, bool enable) {
  const bool allow_faulted = !enable;
  validate_call_state_(allow_faulted, "set_enable");
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(allow_faulted, "set_enable");
  throw_if_sdk_failed_(sdk_->set_enable(id, enable), "set_enable");
}

void LHandProDriver::home_motors(int id) {
  validate_call_state_(false, "home_motors");
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(false, "home_motors");
  throw_if_sdk_failed_(sdk_->home_motors(id), "home_motors");
}

void LHandProDriver::set_move_no_home(int enable) {
  const bool allow_faulted = enable == 0;
  validate_call_state_(allow_faulted, "set_move_no_home");
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(allow_faulted, "set_move_no_home");
  throw_if_sdk_failed_(sdk_->set_move_no_home(enable), "set_move_no_home");
}

void LHandProDriver::clear_alarm(int id) {
  validate_call_state_(false, "clear_alarm");
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(false, "clear_alarm");
  throw_if_sdk_failed_(sdk_->clear_alarm(id), "clear_alarm");
}

int LHandProDriver::get_now_position(int id) {
  validate_call_state_(false, "get_now_position");
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(false, "get_now_position");
  int value = 0;
  throw_if_sdk_failed_(sdk_->get_now_position(id, value),
                       "get_now_position");
  return value;
}

float LHandProDriver::get_now_angle(int id) {
  validate_call_state_(false, "get_now_angle");
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(false, "get_now_angle");
  float value = 0.0F;
  throw_if_sdk_failed_(sdk_->get_now_angle(id, value), "get_now_angle");
  return value;
}

int LHandProDriver::get_now_status(int id) {
  validate_call_state_(false, "get_now_status");
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(false, "get_now_status");
  int value = 0;
  throw_if_sdk_failed_(sdk_->get_now_status(id, value), "get_now_status");
  return value;
}

int LHandProDriver::get_now_current(int id) {
  validate_call_state_(false, "get_now_current");
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(false, "get_now_current");
  int value = 0;
  throw_if_sdk_failed_(sdk_->get_now_current(id, value),
                       "get_now_current");
  return value;
}

int LHandProDriver::get_now_alarm(int id) {
  validate_call_state_(false, "get_now_alarm");
  std::lock_guard<std::mutex> sdk_lock(sdk_call_mutex_);
  validate_call_state_(false, "get_now_alarm");
  int value = 0;
  throw_if_sdk_failed_(sdk_->get_now_alarm(id, value), "get_now_alarm");
  return value;
}

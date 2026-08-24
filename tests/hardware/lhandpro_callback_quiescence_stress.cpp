// SPDX-License-Identifier: GPL-3.0
// Copyright (C) 2026 Roboparty

#include <LHandProLib/LHandProLib.h>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace {

using namespace std::chrono_literals;

constexpr std::size_t kCallbackGenerationCount = 1024;
constexpr auto kCallbackWait = 2s;
constexpr auto kBlockedStopObservation = 50ms;
constexpr auto kStopGrace = 20ms;
constexpr auto kCloseGrace = 10ms;
constexpr auto kDestroyGrace = 50ms;
constexpr std::size_t kQueriesPerIteration = 8;
constexpr std::size_t kDecodeCodeCount = 13;
constexpr std::size_t kDecodeIdCount = 5;
constexpr std::size_t kPhaseCount = 6;
constexpr std::array<unsigned int, 4> kTrackedDecodeIds{
    0x181, 0x481, 0x501, 0x581};

enum class Phase : unsigned char {
  Initializing,
  Running,
  Stopping,
  Stopped,
  Closed,
  Destroyed,
};

struct CallbackCounters {
  std::atomic<std::uint64_t> entered{0};
  std::atomic<std::uint64_t> exited{0};
  std::atomic<std::uint64_t> inflight{0};
  std::atomic<std::uint64_t> max_inflight{0};
  std::atomic<std::uint64_t> late_after_stop{0};
  std::atomic<std::uint64_t> late_after_close{0};
  std::atomic<std::uint64_t> late_after_destroy{0};
  std::atomic<std::uint64_t> stale_generation{0};
  std::atomic<std::uint64_t> transmit_failures{0};
  std::atomic<std::uint64_t> decode_calls{0};
  std::atomic<std::uint64_t> decode_failures{0};
  std::array<std::atomic<std::uint64_t>, kDecodeCodeCount>
      decode_error_codes{};
  std::array<std::atomic<std::uint64_t>, kDecodeIdCount>
      decode_failure_ids{};
  std::array<std::atomic<std::uint64_t>, kPhaseCount>
      decode_failure_phases{};
  std::atomic<std::uint64_t> inflight_at_stop_return{0};
};

struct DecodeFirstFailure {
  bool recorded{false};
  int code{0};
  unsigned int id{0};
  std::size_t size{0};
  Phase phase{Phase::Initializing};
  std::array<unsigned char, 8> data{};
};

void update_max(std::atomic<std::uint64_t>& maximum,
                std::uint64_t candidate) noexcept {
  auto observed = maximum.load(std::memory_order_relaxed);
  while (candidate > observed &&
         !maximum.compare_exchange_weak(observed, candidate,
                                        std::memory_order_relaxed)) {
  }
}

struct CallbackExit {
  CallbackCounters& counters;

  ~CallbackExit() {
    counters.exited.fetch_add(1, std::memory_order_release);
    counters.inflight.fetch_sub(1, std::memory_order_acq_rel);
  }
};

class CallbackBlocker {
 public:
  void arm() {
    std::lock_guard<std::mutex> lock(mutex_);
    armed_ = true;
    entered_ = false;
    released_ = false;
    finished_ = false;
  }

  void enter_if_armed() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!armed_) return;
    armed_ = false;
    entered_ = true;
    entered_cv_.notify_all();
    release_cv_.wait(lock, [this] { return released_; });
    finished_ = true;
    finished_cv_.notify_all();
  }

  bool wait_entered(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return entered_cv_.wait_for(lock, timeout, [this] { return entered_; });
  }

  void release() {
    std::lock_guard<std::mutex> lock(mutex_);
    released_ = true;
    release_cv_.notify_all();
  }

  bool wait_finished(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return finished_cv_.wait_for(lock, timeout, [this] { return finished_; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable entered_cv_;
  std::condition_variable release_cv_;
  std::condition_variable finished_cv_;
  bool armed_{false};
  bool entered_{false};
  bool released_{false};
  bool finished_{false};
};

struct RuntimeContext {
  int socket_fd{-1};
  std::atomic<lhandprolib_handle> handle{nullptr};
  std::atomic<Phase> phase{Phase::Initializing};
  std::atomic<std::size_t> generation{0};
  std::atomic<bool> receive_running{false};
  std::atomic<bool> decode_enabled{false};
  CallbackCounters counters;
  std::mutex decode_diagnostics_mutex;
  DecodeFirstFailure first_decode_failure;
};

std::atomic<RuntimeContext*> active_context{nullptr};
std::atomic<bool> stop_requested{false};

std::size_t decode_id_index(unsigned int id) noexcept {
  for (std::size_t index = 0; index < kTrackedDecodeIds.size(); ++index) {
    if (kTrackedDecodeIds[index] == id) return index;
  }
  return kTrackedDecodeIds.size();
}

void record_decode_result(RuntimeContext& context, Phase phase,
                          unsigned int id, const unsigned char* data,
                          std::size_t size, int result) noexcept {
  context.counters.decode_calls.fetch_add(1, std::memory_order_relaxed);
  if (result == C_LER_NONE) return;

  context.counters.decode_failures.fetch_add(1, std::memory_order_relaxed);
  const auto code_index =
      result >= 0 && static_cast<std::size_t>(result) < kDecodeCodeCount - 1
          ? static_cast<std::size_t>(result)
          : kDecodeCodeCount - 1;
  context.counters.decode_error_codes[code_index].fetch_add(
      1, std::memory_order_relaxed);
  context.counters.decode_failure_ids[decode_id_index(id)].fetch_add(
      1, std::memory_order_relaxed);
  const auto phase_index = static_cast<std::size_t>(phase);
  if (phase_index < kPhaseCount) {
    context.counters.decode_failure_phases[phase_index].fetch_add(
        1, std::memory_order_relaxed);
  }

  std::lock_guard<std::mutex> lock(context.decode_diagnostics_mutex);
  if (context.first_decode_failure.recorded) return;
  context.first_decode_failure.recorded = true;
  context.first_decode_failure.code = result;
  context.first_decode_failure.id = id;
  context.first_decode_failure.size = size;
  context.first_decode_failure.phase = phase;
  const auto captured =
      size < context.first_decode_failure.data.size()
          ? size
          : context.first_decode_failure.data.size();
  if (data != nullptr) {
    std::copy_n(data, captured, context.first_decode_failure.data.begin());
  }
}

bool callback_body(std::size_t callback_generation, unsigned int id,
                   const unsigned char* data, unsigned int size,
                   int extended) noexcept {
  auto* context = active_context.load(std::memory_order_acquire);
  if (context == nullptr) return false;

  context->counters.entered.fetch_add(1, std::memory_order_acq_rel);
  const auto inflight =
      context->counters.inflight.fetch_add(1, std::memory_order_acq_rel) + 1;
  update_max(context->counters.max_inflight, inflight);
  CallbackExit exit{context->counters};

  if (callback_generation !=
      context->generation.load(std::memory_order_acquire)) {
    context->counters.stale_generation.fetch_add(1,
                                                  std::memory_order_relaxed);
  }

  switch (context->phase.load(std::memory_order_acquire)) {
    case Phase::Stopped:
      context->counters.late_after_stop.fetch_add(1,
                                                  std::memory_order_relaxed);
      break;
    case Phase::Closed:
      context->counters.late_after_close.fetch_add(1,
                                                   std::memory_order_relaxed);
      break;
    case Phase::Destroyed:
      context->counters.late_after_destroy.fetch_add(
          1, std::memory_order_relaxed);
      break;
    case Phase::Initializing:
    case Phase::Running:
    case Phase::Stopping:
      break;
  }

  if (context->socket_fd < 0 || data == nullptr || size > CANFD_MAX_DLEN ||
      (extended == 0 && id > CAN_SFF_MASK) ||
      (extended != 0 && id > CAN_EFF_MASK)) {
    context->counters.transmit_failures.fetch_add(1,
                                                  std::memory_order_relaxed);
    return false;
  }

  canfd_frame frame{};
  frame.can_id = id;
  if (extended != 0) frame.can_id |= CAN_EFF_FLAG;
  frame.len = static_cast<__u8>(size);
  std::memcpy(frame.data, data, size);
  const auto written = ::write(context->socket_fd, &frame, CANFD_MTU);
  if (written != CANFD_MTU) {
    context->counters.transmit_failures.fetch_add(1,
                                                  std::memory_order_relaxed);
    return false;
  }
  return true;
}

template <std::size_t Generation>
bool transmit_callback(unsigned int id, const unsigned char* data,
                       unsigned int size, int extended) noexcept {
  return callback_body(Generation, id, data, size, extended);
}

template <std::size_t... Generations>
constexpr auto make_callbacks(std::index_sequence<Generations...>) {
  return std::array<CANFDSendDataCallbackWrapper, sizeof...(Generations)>{
      &transmit_callback<Generations>...};
}

constexpr auto callbacks =
    make_callbacks(std::make_index_sequence<kCallbackGenerationCount>{});

bool exercise_simulated_stop(bool join_before_return) {
  CallbackCounters counters;
  CallbackBlocker blocker;
  blocker.arm();

  std::thread callback([&] {
    counters.entered.fetch_add(1, std::memory_order_acq_rel);
    counters.inflight.fetch_add(1, std::memory_order_acq_rel);
    CallbackExit exit{counters};
    blocker.enter_if_armed();
  });
  if (!blocker.wait_entered(kCallbackWait)) {
    blocker.release();
    callback.join();
    return false;
  }

  std::atomic<bool> stop_returned{false};
  std::thread stopper([&] {
    if (join_before_return) callback.join();
    stop_returned.store(true, std::memory_order_release);
  });
  std::this_thread::sleep_for(kBlockedStopObservation);
  const bool returned_while_blocked =
      stop_returned.load(std::memory_order_acquire);
  blocker.release();
  if (!blocker.wait_finished(kCallbackWait)) {
    if (!join_before_return) callback.join();
    stopper.join();
    return false;
  }
  if (!join_before_return) callback.join();
  stopper.join();

  return !returned_while_blocked &&
         counters.entered.load(std::memory_order_acquire) == 1 &&
         counters.exited.load(std::memory_order_acquire) == 1 &&
         counters.inflight.load(std::memory_order_acquire) == 0;
}

bool exercise_immediate_monitor_capture() {
  CallbackBlocker blocker;
  blocker.arm();
  std::thread callback;
  std::thread starter([&] {
    callback = std::thread([&] { blocker.enter_if_armed(); });
  });
  starter.join();
  const bool entered = blocker.wait_entered(kCallbackWait);
  blocker.release();
  const bool finished = blocker.wait_finished(kCallbackWait);
  callback.join();
  return entered && finished;
}

bool exercise_traffic_then_stable_stop() {
  CallbackCounters counters;
  for (std::size_t query = 0; query < kQueriesPerIteration; ++query) {
    counters.entered.fetch_add(1, std::memory_order_acq_rel);
    counters.inflight.fetch_add(1, std::memory_order_acq_rel);
    CallbackExit exit{counters};
  }
  const auto before_stop = counters.entered.load(std::memory_order_acquire);
  std::this_thread::sleep_for(kStopGrace);
  return before_stop == kQueriesPerIteration &&
         counters.entered.load(std::memory_order_acquire) == before_stop &&
         counters.exited.load(std::memory_order_acquire) == before_stop &&
         counters.inflight.load(std::memory_order_acquire) == 0;
}

bool exercise_decode_diagnostics() {
  RuntimeContext context;
  const std::array<unsigned char, 8> payload{
      0x43, 0x1d, 0x20, 0x11, 0x01, 0x00, 0x00, 0x00};
  record_decode_result(context, Phase::Running, 0x581, payload.data(),
                       payload.size(), C_LER_COMM_DATA_FORMAT);
  return context.counters.decode_calls.load(std::memory_order_acquire) == 1 &&
         context.counters.decode_failures.load(std::memory_order_acquire) ==
             1 &&
         context.counters.decode_error_codes[8].load(
             std::memory_order_acquire) == 1 &&
         context.counters.decode_failure_ids[3].load(
             std::memory_order_acquire) == 1 &&
         context.counters.decode_failure_phases[1].load(
             std::memory_order_acquire) == 1 &&
         context.first_decode_failure.recorded &&
         context.first_decode_failure.code == C_LER_COMM_DATA_FORMAT &&
         context.first_decode_failure.id == 0x581 &&
         context.first_decode_failure.size == payload.size() &&
         context.first_decode_failure.phase == Phase::Running &&
         context.first_decode_failure.data == payload;
}

int run_self_test() {
  if (exercise_simulated_stop(false)) {
    std::cerr << "SELF_TEST FAIL bad=accepted\n";
    return 1;
  }
  if (!exercise_simulated_stop(true)) {
    std::cerr << "SELF_TEST FAIL good=rejected\n";
    return 1;
  }
  if (!exercise_immediate_monitor_capture()) {
    std::cerr << "SELF_TEST FAIL immediate_monitor_callback=missed\n";
    return 1;
  }
  if (!exercise_traffic_then_stable_stop()) {
    std::cerr << "SELF_TEST FAIL traffic_stability=rejected\n";
    return 1;
  }
  if (!exercise_decode_diagnostics()) {
    std::cerr << "SELF_TEST FAIL decode_diagnostics=rejected\n";
    return 1;
  }
  std::cout
      << "SELF_TEST PASS bad=rejected good=accepted immediate=captured "
         "traffic_stability=accepted decode_diagnostics=accepted\n";
  return 0;
}

struct Options {
  std::string interface{"can0"};
  int node_id{1};
  int model{C_LAC_DOF_6_S};
  int duration_seconds{60};
};

int parse_positive(std::string_view value, const char* name) {
  std::size_t parsed = 0;
  int result = 0;
  try {
    result = std::stoi(std::string(value), &parsed, 10);
  } catch (const std::exception&) {
    throw std::invalid_argument(std::string("invalid ") + name);
  }
  if (parsed != value.size() || result <= 0) {
    throw std::invalid_argument(std::string("invalid ") + name);
  }
  return result;
}

Options parse_options(int argc, char** argv) {
  Options options;
  bool saw_interface = false;
  bool saw_node_id = false;
  bool saw_model = false;
  bool saw_duration = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing value for " +
                                  std::string(argument));
    }
    const std::string_view value(argv[++index]);
    if (argument == "--interface") {
      if (saw_interface) throw std::invalid_argument("duplicate interface");
      saw_interface = true;
      options.interface = value;
    } else if (argument == "--node-id") {
      if (saw_node_id) throw std::invalid_argument("duplicate node id");
      saw_node_id = true;
      options.node_id = parse_positive(value, "node id");
    } else if (argument == "--model") {
      if (saw_model) throw std::invalid_argument("duplicate model");
      saw_model = true;
      options.model = parse_positive(value, "model");
    } else if (argument == "--duration-seconds") {
      if (saw_duration) throw std::invalid_argument("duplicate duration");
      saw_duration = true;
      options.duration_seconds = parse_positive(value, "duration");
    } else {
      throw std::invalid_argument("unknown option " + std::string(argument));
    }
  }
  if (!saw_interface || !saw_node_id || !saw_model || !saw_duration ||
      options.interface.empty() || options.node_id > 127 ||
      options.model != C_LAC_DOF_6_S) {
    throw std::invalid_argument("unsupported interface, node id, or model");
  }
  return options;
}

int open_canfd_socket(const Options& options) {
  const int descriptor = ::socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK, CAN_RAW);
  if (descriptor < 0) throw std::runtime_error("socket failed");

  const int enabled = 1;
  if (::setsockopt(descriptor, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enabled,
                   sizeof(enabled)) < 0) {
    ::close(descriptor);
    throw std::runtime_error("CAN_RAW_FD_FRAMES failed");
  }

  const std::array<can_filter, 4> filters{{
      {static_cast<canid_t>(0x500 + options.node_id),
       CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG},
      {static_cast<canid_t>(0x480 + options.node_id),
       CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG},
      {static_cast<canid_t>(0x580 + options.node_id),
       CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG},
      {static_cast<canid_t>(0x180 + options.node_id),
       CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG},
  }};
  if (::setsockopt(descriptor, SOL_CAN_RAW, CAN_RAW_FILTER, filters.data(),
                   static_cast<socklen_t>(sizeof(filters))) < 0) {
    ::close(descriptor);
    throw std::runtime_error("CAN_RAW_FILTER failed");
  }

  const unsigned int interface_index =
      ::if_nametoindex(options.interface.c_str());
  if (interface_index == 0) {
    ::close(descriptor);
    throw std::runtime_error("interface lookup failed");
  }
  sockaddr_can address{};
  address.can_family = AF_CAN;
  address.can_ifindex = static_cast<int>(interface_index);
  if (::bind(descriptor, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) < 0) {
    ::close(descriptor);
    throw std::runtime_error("bind failed");
  }
  return descriptor;
}

void receive_loop(RuntimeContext& context) noexcept {
  pollfd descriptor{context.socket_fd, POLLIN, 0};
  while (context.receive_running.load(std::memory_order_acquire)) {
    const int poll_result = ::poll(&descriptor, 1, 20);
    if (poll_result <= 0 || (descriptor.revents & POLLIN) == 0) continue;
    canfd_frame frame{};
    if (::read(context.socket_fd, &frame, CANFD_MTU) != CANFD_MTU) continue;
    if (!context.decode_enabled.load(std::memory_order_acquire)) continue;
    auto handle = context.handle.load(std::memory_order_acquire);
    if (handle == nullptr) continue;
    const auto id = frame.can_id &
                    ((frame.can_id & CAN_EFF_FLAG) != 0 ? CAN_EFF_MASK
                                                        : CAN_SFF_MASK);
    const auto phase = context.phase.load(std::memory_order_acquire);
    const int result =
        lhandprolib_set_canfd_data_decode(handle, id, frame.data, frame.len);
    record_decode_result(context, phase, id, frame.data, frame.len, result);
  }
}

const char* phase_name(Phase phase) noexcept {
  switch (phase) {
    case Phase::Initializing:
      return "initializing";
    case Phase::Running:
      return "running";
    case Phase::Stopping:
      return "stopping";
    case Phase::Stopped:
      return "stopped";
    case Phase::Closed:
      return "closed";
    case Phase::Destroyed:
      return "destroyed";
  }
  return "unknown";
}

std::string first_decode_data(const DecodeFirstFailure& failure) {
  static constexpr char kHex[] = "0123456789abcdef";
  const auto captured = failure.size < failure.data.size()
                            ? failure.size
                            : failure.data.size();
  std::string result;
  result.reserve(captured * 2);
  for (std::size_t index = 0; index < captured; ++index) {
    result.push_back(kHex[failure.data[index] >> 4]);
    result.push_back(kHex[failure.data[index] & 0x0f]);
  }
  return result;
}

bool stable_entries(CallbackCounters& counters,
                    std::chrono::milliseconds grace) {
  const auto before = counters.entered.load(std::memory_order_acquire);
  std::this_thread::sleep_for(grace);
  return counters.entered.load(std::memory_order_acquire) == before;
}

struct RunTotals {
  std::uint64_t iterations{0};
  std::uint64_t created{0};
  std::uint64_t initialized{0};
  std::uint64_t closed{0};
  std::uint64_t destroyed{0};
  std::uint64_t queries{0};
  std::uint64_t query_failures{0};
  std::uint64_t failures{0};
};

bool run_iteration(RuntimeContext& context, const Options& options,
                   RunTotals& totals, std::size_t generation) {
  bool monitor_started = false;
  bool receiver_started = false;
  std::thread receiver;
  auto handle = lhandprolib_create();
  if (handle == nullptr) {
    ++totals.failures;
    return false;
  }
  ++totals.created;
  context.generation.store(generation, std::memory_order_release);
  context.handle.store(handle, std::memory_order_release);
  context.phase.store(Phase::Initializing, std::memory_order_release);
  active_context.store(&context, std::memory_order_release);

  auto cleanup = [&] {
    if (monitor_started) {
      context.phase.store(Phase::Stopping, std::memory_order_release);
      lhandprolib_stop_monitor(handle);
      monitor_started = false;
      context.phase.store(Phase::Stopped, std::memory_order_release);
    }
    context.decode_enabled.store(false, std::memory_order_release);
    context.receive_running.store(false, std::memory_order_release);
    if (receiver_started && receiver.joinable()) receiver.join();
    lhandprolib_close(handle);
    ++totals.closed;
    context.phase.store(Phase::Closed, std::memory_order_release);
    lhandprolib_destroy(handle);
    ++totals.destroyed;
    context.handle.store(nullptr, std::memory_order_release);
    context.phase.store(Phase::Destroyed, std::memory_order_release);
    std::this_thread::sleep_for(kDestroyGrace);
  };

  if (lhandprolib_set_hand_type(handle, options.model) != C_LER_NONE) {
    ++totals.failures;
    cleanup();
    return false;
  }
  lhandprolib_set_send_canfd_callback(handle, callbacks[generation]);
  context.receive_running.store(true, std::memory_order_release);
  context.decode_enabled.store(true, std::memory_order_release);
  try {
    receiver = std::thread(receive_loop, std::ref(context));
    receiver_started = true;
  } catch (...) {
    ++totals.failures;
    cleanup();
    return false;
  }
  if (lhandprolib_initial_ex(handle, C_LCN_CANFD, options.node_id) !=
      C_LER_NONE) {
    ++totals.failures;
    cleanup();
    return false;
  }
  ++totals.initialized;

  context.phase.store(Phase::Running, std::memory_order_release);
  lhandprolib_start_monitor(handle);
  monitor_started = true;

  bool iteration_ok = true;
  for (std::size_t query = 0; query < kQueriesPerIteration; ++query) {
    const auto entered_before =
        context.counters.entered.load(std::memory_order_acquire);
    int reported_node_id = -1;
    const int query_result =
        lhandprolib_get_can_node_id(handle, &reported_node_id);
    ++totals.queries;
    const bool query_ok =
        query_result == C_LER_NONE && reported_node_id == options.node_id &&
        context.counters.entered.load(std::memory_order_acquire) >
            entered_before &&
        context.counters.inflight.load(std::memory_order_acquire) == 0;
    if (!query_ok) {
      ++totals.query_failures;
      iteration_ok = false;
      break;
    }
  }

  context.phase.store(Phase::Stopping, std::memory_order_release);
  lhandprolib_stop_monitor(handle);
  monitor_started = false;
  context.phase.store(Phase::Stopped, std::memory_order_release);

  if (context.counters.inflight.load(std::memory_order_acquire) != 0) {
    context.counters.inflight_at_stop_return.fetch_add(
        1, std::memory_order_relaxed);
    iteration_ok = false;
  }
  iteration_ok = stable_entries(context.counters, kStopGrace) && iteration_ok;

  context.decode_enabled.store(false, std::memory_order_release);
  context.receive_running.store(false, std::memory_order_release);
  if (receiver.joinable()) receiver.join();
  receiver_started = false;

  lhandprolib_close(handle);
  ++totals.closed;
  context.phase.store(Phase::Closed, std::memory_order_release);
  iteration_ok = stable_entries(context.counters, kCloseGrace) && iteration_ok;

  lhandprolib_destroy(handle);
  ++totals.destroyed;
  context.handle.store(nullptr, std::memory_order_release);
  context.phase.store(Phase::Destroyed, std::memory_order_release);
  iteration_ok = stable_entries(context.counters, kDestroyGrace) && iteration_ok;

  if (!iteration_ok) ++totals.failures;
  return iteration_ok;
}

int run_physical(const Options& options) {
  RuntimeContext context;
  RunTotals totals;
  try {
    context.socket_fd = open_canfd_socket(options);
  } catch (const std::exception& error) {
    std::cerr << "result=FAIL error=" << error.what() << '\n';
    return 1;
  }

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(options.duration_seconds);
  std::size_t generation = 0;
  while (!stop_requested.load(std::memory_order_acquire) &&
         std::chrono::steady_clock::now() < deadline &&
         generation < kCallbackGenerationCount) {
    ++totals.iterations;
    if (!run_iteration(context, options, totals, generation)) break;
    ++generation;
  }
  context.phase.store(Phase::Destroyed, std::memory_order_release);
  std::this_thread::sleep_for(kDestroyGrace);
  active_context.store(nullptr, std::memory_order_release);
  ::close(context.socket_fd);
  context.socket_fd = -1;

  const auto entered = context.counters.entered.load(std::memory_order_acquire);
  const auto exited = context.counters.exited.load(std::memory_order_acquire);
  const auto inflight =
      context.counters.inflight.load(std::memory_order_acquire);
  const auto inflight_at_stop = context.counters.inflight_at_stop_return.load(
      std::memory_order_acquire);
  const auto late_stop =
      context.counters.late_after_stop.load(std::memory_order_acquire);
  const auto late_close =
      context.counters.late_after_close.load(std::memory_order_acquire);
  const auto late_destroy =
      context.counters.late_after_destroy.load(std::memory_order_acquire);
  const auto stale =
      context.counters.stale_generation.load(std::memory_order_acquire);
  const auto transmit_failures =
      context.counters.transmit_failures.load(std::memory_order_acquire);
  const auto decode_failures =
      context.counters.decode_failures.load(std::memory_order_acquire);
  const auto decode_calls =
      context.counters.decode_calls.load(std::memory_order_acquire);

  const bool passed = totals.iterations > 0 && totals.failures == 0 &&
                      totals.created == totals.initialized &&
                      totals.created == totals.closed &&
                      totals.created == totals.destroyed && entered == exited &&
                      totals.queries > 0 && totals.query_failures == 0 &&
                      inflight == 0 && inflight_at_stop == 0 && late_stop == 0 &&
                      late_close == 0 && late_destroy == 0 && stale == 0 &&
                      transmit_failures == 0 && decode_failures == 0 &&
                      !stop_requested.load(std::memory_order_acquire);

  std::cout << "result=" << (passed ? "PASS" : "FAIL") << '\n'
            << "duration_seconds=" << options.duration_seconds << '\n'
            << "iterations=" << totals.iterations << '\n'
            << "created=" << totals.created << '\n'
            << "initialized=" << totals.initialized << '\n'
            << "closed=" << totals.closed << '\n'
            << "destroyed=" << totals.destroyed << '\n'
            << "queries=" << totals.queries << '\n'
            << "query_failures=" << totals.query_failures << '\n'
            << "entered=" << entered << '\n'
            << "exited=" << exited << '\n'
            << "inflight=" << inflight << '\n'
            << "max_inflight="
            << context.counters.max_inflight.load(std::memory_order_acquire)
            << '\n'
            << "inflight_at_stop_return=" << inflight_at_stop << '\n'
            << "late_after_stop=" << late_stop << '\n'
            << "late_after_close=" << late_close << '\n'
            << "late_after_destroy=" << late_destroy << '\n'
            << "stale_generation=" << stale << '\n'
            << "transmit_failures=" << transmit_failures << '\n'
            << "decode_calls=" << decode_calls << '\n'
            << "decode_successes=" << decode_calls - decode_failures << '\n'
            << "decode_failures=" << decode_failures << '\n'
            << "failures=" << totals.failures << '\n';
  for (std::size_t code = 1; code < kDecodeCodeCount - 1; ++code) {
    std::cout << "decode_error_code_" << code << '='
              << context.counters.decode_error_codes[code].load(
                     std::memory_order_acquire)
              << '\n';
  }
  std::cout << "decode_error_code_other="
            << context.counters.decode_error_codes.back().load(
                   std::memory_order_acquire)
            << '\n';
  for (std::size_t index = 0; index < kTrackedDecodeIds.size(); ++index) {
    static constexpr std::array<const char*, 4> kIdLabels{
        "0x181", "0x481", "0x501", "0x581"};
    std::cout << "decode_failure_id_" << kIdLabels[index] << '='
              << context.counters.decode_failure_ids[index].load(
                     std::memory_order_acquire)
              << '\n';
  }
  std::cout << "decode_failure_id_other="
            << context.counters.decode_failure_ids.back().load(
                   std::memory_order_acquire)
            << '\n';
  static constexpr std::array<const char*, kPhaseCount> kPhaseLabels{
      "initializing", "running", "stopping", "stopped", "closed",
      "destroyed"};
  for (std::size_t index = 0; index < kPhaseLabels.size(); ++index) {
    std::cout << "decode_failure_phase_" << kPhaseLabels[index] << '='
              << context.counters.decode_failure_phases[index].load(
                     std::memory_order_acquire)
              << '\n';
  }
  if (context.first_decode_failure.recorded) {
    std::cout << "first_decode_failure_code="
              << context.first_decode_failure.code << '\n'
              << "first_decode_failure_id=" << context.first_decode_failure.id
              << '\n'
              << "first_decode_failure_size="
              << context.first_decode_failure.size << '\n'
              << "first_decode_failure_phase="
              << phase_name(context.first_decode_failure.phase) << '\n'
              << "first_decode_failure_data="
              << first_decode_data(context.first_decode_failure) << '\n';
  } else {
    std::cout << "first_decode_failure=none\n";
  }
  return passed ? 0 : 1;
}

void print_usage(const char* program) {
  std::cerr << "usage: " << program
            << " --self-test | --interface can0 --node-id 1 --model 1 "
               "--duration-seconds 60\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
    return run_self_test();
  }
  if (argc != 9) {
    print_usage(argv[0]);
    return 2;
  }
  try {
    return run_physical(parse_options(argc, argv));
  } catch (const std::exception& error) {
    std::cerr << "result=FAIL error=" << error.what() << '\n';
    print_usage(argv[0]);
    return 2;
  }
}

#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace roboparty::dexhand::detail {

class CallbackGate {
 private:
  struct State {
    std::mutex mutex;
    std::condition_variable drained;
    bool accepting{false};
    std::size_t active{0};
  };

 public:
  class Lease {
   public:
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    Lease(Lease&& other) noexcept : state_(std::move(other.state_)) {}
    Lease& operator=(Lease&& other) noexcept {
      if (this != &other) {
        release();
        state_ = std::move(other.state_);
      }
      return *this;
    }
    ~Lease() { release(); }

   private:
    friend class CallbackGate;
    explicit Lease(std::shared_ptr<State> state) : state_(std::move(state)) {}
    void release() noexcept {
      if (!state_) return;
      std::lock_guard<std::mutex> lock(state_->mutex);
      --state_->active;
      if (state_->active == 0) state_->drained.notify_all();
      state_.reset();
    }
    std::shared_ptr<State> state_;
  };

  CallbackGate() : state_(std::make_shared<State>()) {}
  CallbackGate(const CallbackGate&) = delete;
  CallbackGate& operator=(const CallbackGate&) = delete;
  ~CallbackGate() { close_and_wait(); }

  void open() noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->active == 0) state_->accepting = true;
  }

  std::optional<Lease> try_enter() noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!state_->accepting) return std::nullopt;
    ++state_->active;
    return Lease(state_);
  }

  void close_and_wait() noexcept {
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->accepting = false;
    state_->drained.wait(lock, [this] { return state_->active == 0; });
  }

 private:
  std::shared_ptr<State> state_;
};

}  // namespace roboparty::dexhand::detail

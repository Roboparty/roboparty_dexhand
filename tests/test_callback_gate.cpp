#include "protocol/callback_gate.hpp"
#include "test_support.hpp"

#include <atomic>
#include <chrono>
#include <future>

using roboparty::dexhand::detail::CallbackGate;

int main() {
  CallbackGate gate;
  CHECK(!gate.try_enter().has_value());
  gate.open();

  auto lease = gate.try_enter();
  CHECK(lease.has_value());
  auto stopped = std::async(std::launch::async, [&gate] {
    gate.close_and_wait();
    return true;
  });
  CHECK(stopped.wait_for(std::chrono::milliseconds(50)) ==
        std::future_status::timeout);
  lease.reset();
  CHECK(stopped.wait_for(std::chrono::seconds(2)) ==
        std::future_status::ready);
  CHECK(!gate.try_enter().has_value());

  gate.open();
  CHECK(gate.try_enter().has_value());
  gate.close_and_wait();
  return 0;
}

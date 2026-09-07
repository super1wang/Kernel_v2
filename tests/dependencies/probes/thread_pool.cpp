#include "probe_common.hpp"
#include <BS_thread_pool.hpp>
#include <atomic>
#include <stdexcept>
int main() {
  BS::thread_pool<> pool(2); std::atomic<int> count{0};
  auto a = pool.submit_task([&] { ++count; return 7; });
  auto b = pool.submit_task([]() -> int { throw std::runtime_error("expected task error"); });
  PROBE_CHECK(a.get() == 7);
  bool error = false; try { (void)b.get(); } catch (const std::runtime_error&) { error = true; }
  pool.wait(); PROBE_CHECK(error && count == 1); std::cout << "thread pool future/error/drain smoke\n";
}

#pragma once
#include <cstddef>
namespace native_test::allocation {
struct Counts {
  std::size_t cpp=0,crt=0,frees=0,allocated_bytes=0,released_bytes=0;
  std::size_t asan_allocations=0,asan_frees=0;
  std::size_t live_bytes_before=0,live_bytes_after=0,peak_live_bytes=0;
  std::size_t live_blocks_before=0,live_blocks_after=0;
};
void initialize() noexcept;
void start() noexcept;
Counts stop() noexcept;
bool crt_available() noexcept;
bool asan_available() noexcept;
int registration_failure_probe() noexcept;
}

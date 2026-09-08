#pragma once
#include <cstddef>
#include <cstdint>
namespace host_fault {
struct AllocationObservation {
  std::size_t calls, failed_ordinal, failed_size;
  std::int64_t live_blocks;
};
// 仅测试EXE的本线程单次故障；抛异常前解除，异常回执不再受同次故障影响。
void begin_allocations(std::size_t fail_ordinal = 0) noexcept;
AllocationObservation end_allocations() noexcept;
std::int64_t live_blocks() noexcept;
enum class RandomMode { Real, Failure, Zero };
void random_mode(RandomMode) noexcept;
unsigned random_calls() noexcept;
bool random_arguments_valid() noexcept;
void internal_controls();
}

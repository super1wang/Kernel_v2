#pragma once
#include <cstddef>
namespace native_test::allocation {
struct Counts { std::size_t cpp=0,crt=0; };
void initialize() noexcept;
void start() noexcept;
Counts stop() noexcept;
bool crt_available() noexcept;
}

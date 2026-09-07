#include "probe_common.hpp"
#include <version>
#include <cstdint>
static_assert(__cplusplus >= 202002L);
static_assert(sizeof(void*) == 8);
int main() {
  PROBE_CHECK(_MSC_VER == 1944); PROBE_CHECK(_MSC_FULL_VER == 194435228);
  std::cout << "MSVC_FULL_VER=" << _MSC_FULL_VER << " __cplusplus=" << __cplusplus << " pointer_bits=" << sizeof(void*) * 8;
#ifdef _DEBUG
  std::cout << " CRT=/MDd\n";
#else
  std::cout << " CRT=/MD\n";
#endif
}

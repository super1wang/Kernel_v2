#include "probe_common.hpp"
#include <fmt/format.h>
int main() { PROBE_CHECK(fmt::format("value={}", 7) == "value=7"); std::cout << "fmt " << FMT_VERSION << "\n"; }

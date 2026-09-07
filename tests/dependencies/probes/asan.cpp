#include "probe_common.hpp"
#include <memory>
int main(int argc, char**) {
  auto memory = std::make_unique<int[]>(4); volatile int index = argc > 1 ? 4 : 3;
  memory[index] = 7; PROBE_CHECK(memory[3] == 7); return 0;
}

#include "probe_common.hpp"
#include <immer/vector.hpp>
#include <immer/vector_transient.hpp>
#include <future>
int main() {
  const immer::vector<int> old{1,2,3};
  auto reader = std::async(std::launch::async, [old] { return old[0] + old[1] + old[2]; });
  auto transient = old.transient(); transient.push_back(4); transient.set(0, 10);
  const auto fresh = transient.persistent();
  PROBE_CHECK(old.size() == 3 && old[0] == 1 && fresh.size() == 4 && fresh[0] == 10 && reader.get() == 6);
  std::cout << "immer immutable snapshot/transient freeze smoke\n";
}

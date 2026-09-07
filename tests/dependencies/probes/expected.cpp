#include "probe_common.hpp"
#include <tl/expected.hpp>
#include <memory>
#include <stdexcept>
int main() {
  tl::expected<std::unique_ptr<int>, std::string> value{std::make_unique<int>(7)};
  auto moved = std::move(value); PROBE_CHECK(**moved == 7);
  tl::expected<void, std::string> empty; PROBE_CHECK(empty.has_value());
  tl::expected<int, std::string> error = tl::make_unexpected(std::string("failure"));
  auto propagated = error.and_then([](int n) -> tl::expected<int, std::string> { return n + 1; });
  PROBE_CHECK(!propagated && propagated.error() == "failure");
  bool thrown = false; try { (void)error.value(); } catch (const tl::bad_expected_access<std::string>& e) { thrown = e.error() == "failure"; }
  PROBE_CHECK(thrown); std::cout << "expected move-only/void/error/exception smoke\n";
}

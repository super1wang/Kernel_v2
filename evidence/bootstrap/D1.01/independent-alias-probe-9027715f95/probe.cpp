#include <ock/foundation/foundation.hpp>
#include <type_traits>
#include <cstdio>
using namespace ock::foundation;
static_assert(std::is_copy_constructible_v<ErrorInfo>);
static_assert(std::is_copy_assignable_v<ErrorInfo>);
int main() {
  auto original = ErrorInfo::create("original");
  auto replacement = ErrorInfo::create("changed");
  auto mutable_alias = std::make_shared<ErrorInfo>(**original);
  Error published{ErrorCode{}, mutable_alias};
  if (published.info()->text() != "original") return 2;
  *mutable_alias = **replacement;
  std::printf("published details after alias assignment: %.*s\n", int(published.info()->text().size()), published.info()->text().data());
  return published.info()->text() == "original" ? 0 : 1;
}

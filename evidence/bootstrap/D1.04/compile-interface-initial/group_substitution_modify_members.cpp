#include <array>
#include "packages/runtime/policy/policy.hpp"
using namespace ock::contracts;using namespace ock::runtime::policy;
void rejected(const GroupSnapshot& group) { group.members()[0].targets.clear(); }

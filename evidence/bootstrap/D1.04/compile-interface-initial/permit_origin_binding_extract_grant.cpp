#include <array>
#include "packages/runtime/policy/policy.hpp"
using namespace ock::contracts;using namespace ock::runtime::policy;
void rejected(const VerifiedCaller& caller) { caller.grant(); }

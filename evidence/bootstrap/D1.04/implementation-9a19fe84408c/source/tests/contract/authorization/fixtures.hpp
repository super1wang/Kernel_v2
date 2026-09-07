#pragma once
#include "tests/compile/contracts/test_support.hpp"
#include "packages/runtime/policy/policy.hpp"
namespace policy_test {
using namespace ock::runtime::policy;
inline void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}

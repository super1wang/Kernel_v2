#include <array>
#include "packages/runtime/policy/policy.hpp"
using namespace ock::contracts;using namespace ock::runtime::policy;
struct ReadOnlyDigest final : TrustedGroupDigestPort {
  Result<ContractDigest> fingerprint(const GroupSnapshot& group) override {
    const OperationSelector& envelope = group.envelope();
    const auto anchor = group.anchor_target();
    const auto members = group.members();
    (void)envelope; (void)anchor; (void)members;
    return ContractDigest{};
  }
};


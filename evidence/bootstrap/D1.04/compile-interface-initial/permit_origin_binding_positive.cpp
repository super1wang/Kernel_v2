#include <array>
#include "packages/runtime/policy/policy.hpp"
using namespace ock::contracts;using namespace ock::runtime::policy;
void accepted(SessionAuthority& session, const VerifiedCaller& caller,
              const ActionRequest& request) {
  auto prepared = session.prepare(caller, request);
  if (prepared) {
    auto binding = (*prepared)->current_expected_binding();
    auto permit = (*prepared)->issue();
    if (binding && permit) { (void)(*prepared)->consume(**permit, *binding); }
  }
}


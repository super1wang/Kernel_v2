#include <array>
#include "packages/runtime/policy/policy.hpp"
using namespace ock::contracts;using namespace ock::runtime::policy;
void accepted(SessionAuthority& session, const CallerDescription& request) {
  auto caller = session.verify(request);
  auto authority = session.callers();
  auto grant = authority->authenticate(request);
  if (caller) { (void)(*caller)->view(); (void)(*caller)->authority(); }
  if (grant) { (void)authority->validate(**grant); }
}


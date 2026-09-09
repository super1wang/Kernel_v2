#define NOMINMAX
#include <Windows.h>
#include <bcrypt.h>
#include "managed_control.hpp"
namespace ock::runtime::executions::detail {
contracts::Result<contracts::ExecutionRef> new_execution_identity() noexcept {
  contracts::ExecutionRef result;
  if(BCryptGenRandom(nullptr,result.execution_id.bytes.data(),
      static_cast<ULONG>(result.execution_id.bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0||result.execution_id.empty())
    return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::Rejected));
  return result;
}
}

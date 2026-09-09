#pragma once
#include "execution_table.hpp"
#include <ock/runtime/native_types.hpp>
namespace ock::runtime::executions::detail {
// 读取采用现有策略层的当前授权；等待者没有执行取消权。
class ExecutionQueries final {
public:
  struct WaitReply {ExecutionTable::WaitState state;policy::AuthorizedSummary observed;};
  template<contracts::ContractResult R> struct ResultReply {
    std::shared_ptr<const contracts::InvokeReply<R>> value;
    std::shared_ptr<const policy::ResponseAuthorization> response;
  };
  ExecutionQueries(std::shared_ptr<ExecutionTable> table,std::shared_ptr<policy::SessionAuthority> session,
      std::shared_ptr<invocation::TrustedThreadPort> thread)
      :table_(std::move(table)),session_(std::move(session)),thread_(std::move(thread)) {
    foundation::invariant(bool(table_)&&bool(session_)&&bool(thread_));
  }
  template<contracts::ContractResult R>
  contracts::Result<ResultReply<R>> result(const policy::VerifiedCaller& caller,contracts::ExecutionRef ref) const {
    auto allowed=session_->observations()->get(caller,ref,policy::AccessUse::ReadResult);
    if(!allowed)return contracts::make_unexpected(allowed.error());
    auto value=table_->result<R>(ref);if(!value)return contracts::make_unexpected(value.error());
    // 取得 pin 后重新检查当前政策；传输适配仍须消费 response 的发送许可。
    allowed=session_->observations()->get(caller,ref,policy::AccessUse::ReadResult);
    if(!allowed)return contracts::make_unexpected(allowed.error());
    return ResultReply<R>{*value,allowed->response};
  }
  contracts::Result<WaitReply> wait(const policy::VerifiedCaller& caller,contracts::ExecutionRef ref,
      std::chrono::steady_clock::time_point deadline,std::stop_token stop={}) const {
    auto role=thread_->current();
    if(!role)return contracts::make_unexpected(role.error());
    if(role->role!=invocation::ThreadRole::Application)
      return contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::ThreadRejected));
    auto allowed=session_->observations()->get(caller,ref,policy::AccessUse::Wait);
    if(!allowed)return contracts::make_unexpected(allowed.error());
    auto waited=table_->wait_terminal(ref,std::min(deadline,allowed->response->deadline()),stop);
    if(!waited)return contracts::make_unexpected(waited.error());
    // 等待期间可能撤权；即使已完成也不能返回先前缓存的敏感摘要。
    allowed=session_->observations()->get(caller,ref,policy::AccessUse::Wait);
    if(!allowed)return contracts::make_unexpected(allowed.error());
    return WaitReply{*waited,std::move(*allowed)};
  }
private:
  std::shared_ptr<ExecutionTable> table_;
  std::shared_ptr<policy::SessionAuthority> session_;
  std::shared_ptr<invocation::TrustedThreadPort> thread_;
};
}

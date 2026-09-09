#pragma once
#include "invocation_access.hpp"

namespace ock::runtime::executions::detail {
// 由可信 typed 注册适配器提供；必须计入可变缓冲区，不能仅返回 sizeof(T)。
template <contracts::AsyncInput A, contracts::ContractResult R>
struct InvocationStoragePolicy {
  std::size_t input_limit,reply_limit;
  contracts::Result<std::size_t> (*input_bytes)(const A&);
  contracts::Result<std::size_t> (*reply_bytes)(const contracts::InvokeReply<R>&);
};

// Execution 表将持有此记录。创建时拥有参数、预留调用槽和最小结果存储；
// run_once 只能由赢得 Scheduler start claim 的工作调用，完成后读取不再变动。
template <contracts::AsyncInput A, contracts::ContractResult R>
  requires (std::same_as<R,void> || contracts::AsyncInput<R>)
class InvocationRecord final : public std::enable_shared_from_this<InvocationRecord<A,R>> {
public:
  using Policy=InvocationStoragePolicy<A,R>;
  static contracts::Result<std::shared_ptr<InvocationRecord>> create(
      const invocation::NativeBound<A,R>& bound,A args,
      invocation::InvokeOptions options,Policy policy) {
    if(!policy.input_limit || !policy.reply_limit || !policy.input_bytes || !policy.reply_bytes)
      return contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::InvalidBinding));
    try {
      auto record=std::shared_ptr<InvocationRecord>(new InvocationRecord(
          InvocationAccess::retain(bound),std::move(args),options,policy));
      auto lease=InvocationAccess::reserve(record->bound_,*record->input_,options);
      if(!lease) return contracts::make_unexpected(lease.error());
      record->call_=std::move(*lease);
      auto bytes=policy.input_bytes(*record->input_);
      if(!bytes) return contracts::make_unexpected(bytes.error());
      if(*bytes>policy.input_limit)
        return contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::BudgetExceeded));
      record->input_bytes_=*bytes;
      return record;
    } catch(...) {
      return contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::BudgetExceeded));
    }
  }
  bool run_once(std::stop_token stop,std::span<const contracts::ResourceLease* const> resources={}) {
    auto keep_alive=this->shared_from_this();
    if(claimed_.exchange(true,std::memory_order_acq_rel)) return false;
    const auto request=[&]{combined_.request_stop();};
    std::stop_callback original(options_.stop,request);
    std::stop_callback managed(stop,request);
    auto options=options_;options.stop=combined_.get_token();
    auto reply=InvocationAccess::run_reserved(bound_,*call_,*input_,options,resources);
    store(std::move(reply));
    return true;
  }
  // 仅用于 Scheduler 已确认未开始的退役/拒绝；不能代替 start/cancel 权威仲裁。
  // 一次性 claim 只防御重复交付，取消请求本身不得调用此方法。
  bool reject_before_start(contracts::Error reason) {
    auto keep_alive=this->shared_from_this();
    if(claimed_.exchange(true,std::memory_order_acq_rel)) return false;
    store(contracts::Rejected{contracts::Error{reason.code()}});
    return true;
  }
  const contracts::InvokeReply<R>* reply() const noexcept {
    return completed_.load(std::memory_order_acquire) ? &*reply_ : nullptr;
  }
  std::size_t input_bytes() const noexcept {return input_bytes_;}
  std::size_t reserved_reply_bytes() const noexcept {return policy_.reply_limit;}
  std::size_t reply_bytes() const noexcept {
    return completed_.load(std::memory_order_acquire) ? reply_bytes_ : 0;
  }
  auto material() const noexcept {return InvocationAccess::material(bound_);}
private:
  InvocationRecord(invocation::NativeBound<A,R> bound,A args,
      invocation::InvokeOptions options,Policy policy)
      :bound_(std::move(bound)),input_(std::move(args)),options_(options),policy_(policy) {}
  void store(contracts::InvokeReply<R> reply) {
    // 测量失败或超额时保留固定错误回执；不让结果容量失败吞掉可靠完成。
    try {
      auto bytes=policy_.reply_bytes(reply);
      if(!bytes || *bytes>policy_.reply_limit) {
        reply=contracts::Rejected{invocation::invocation_error(invocation::InvocationErrc::BudgetExceeded)};
      } else reply_bytes_=*bytes;
    } catch(...) {
      reply=contracts::Rejected{invocation::invocation_error(invocation::InvocationErrc::BudgetExceeded)};
    }
    reply_.emplace(std::move(reply));
    input_.reset(); // 参数析构与调用槽释放均先于可靠完成发布。
    call_.reset();
    completed_.store(true,std::memory_order_release);
  }
  invocation::NativeBound<A,R> bound_;
  std::optional<A> input_;
  invocation::InvokeOptions options_;
  Policy policy_;
  std::unique_ptr<invocation::detail::CallLease> call_;
  std::optional<contracts::InvokeReply<R>> reply_;
  std::size_t input_bytes_=0,reply_bytes_=0;
  std::stop_source combined_; // stop-state 分配也在接受前完成。
  std::atomic<bool> claimed_{false},completed_{false};
};
}

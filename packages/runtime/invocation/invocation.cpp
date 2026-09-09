#include "validation.hpp"
#include "invocation.hpp"
#include <algorithm>
#include <mutex>

namespace ock::runtime::invocation {
namespace {
template <class T> bool owned(const std::shared_ptr<T>& p) {
  return p && p.use_count() > 0;
}
Result<void> reject(InvocationErrc e) {
  return make_unexpected(invocation_error(e));
}
}
Result<NativeEntry> NativeAccess::inspect(const registry::Catalog& catalog,
    const OperationKey& key, ContractDigest digest, Shape shape,
    CppTypeToken args, CppTypeToken result) {
  auto handle = catalog.find(key);
  if (!handle) return make_unexpected(handle.error());
  auto slot = foundation::resolve_slot(*handle,catalog.identity(),catalog.generation(),catalog.size());
  if (!slot) return make_unexpected(slot.error());
  auto definition = catalog.describe(*slot);
  if (!definition) return make_unexpected(definition.error());
  if (!owned(*definition)) return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
  const auto& d = **definition;
  if (d.description().key!=key || d.description().contract_digest!=digest ||
      d.shape()!=shape || d.args_type()!=args || d.result_type()!=result)
    return make_unexpected(invocation_error(InvocationErrc::ContractMismatch));
  return NativeEntry{*handle,*definition,d.shape(),d.description().execution,
                     catalog.hot_[*slot].resources, catalog.hot_[*slot].submission_storage,
                     catalog.hot_[*slot].submission_storage_type,d.asynchronous_read()};
}
Result<void> NativeAccess::check(const registry::Catalog& catalog, const NativeEntry& entry,
    CppTypeToken args, CppTypeToken result) noexcept {
  auto slot=foundation::resolve_slot(entry.handle,catalog.identity(),catalog.generation(),catalog.size());
  if (!slot) return make_unexpected(slot.error());
  if (*slot>=catalog.cold_.size() || catalog.cold_[*slot].get()!=entry.definition.get() ||
      !entry.definition || entry.definition->args_type()!=args ||
      entry.definition->result_type()!=result)
    return reject(InvocationErrc::InvalidBinding);
  if (entry.shape==Shape::Read && !catalog.hot_[*slot].native && !catalog.hot_[*slot].async_factory)
    return reject(InvocationErrc::InvalidBinding);
  return {};
}
Result<std::shared_ptr<registry::detail::AsyncDispatchPort>> NativeAccess::prepare_async(
    const registry::Catalog& catalog,const NativeEntry& entry,std::shared_ptr<PortLifetime> source) {
  auto slot=foundation::resolve_slot(entry.handle,catalog.identity(),catalog.generation(),catalog.size());
  if(!slot||!entry.asynchronous_read||catalog.cold_[*slot].get()!=entry.definition.get()||!catalog.hot_[*slot].async_factory)
    return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
  return catalog.hot_[*slot].async_factory(catalog.hot_[*slot],std::move(source));
}
void NativeAccess::dispatch(const registry::Catalog& catalog, const NativeEntry& entry,
    const void* args, WorkContext& work, void* result) {
  // 此私有入口仅由本次已重验的 NativeBound 管线调用。
  auto slot=foundation::resolve_slot(entry.handle,catalog.identity(),catalog.generation(),catalog.size());
  foundation::invariant(bool(slot) && args && result);
  const auto& hot=catalog.hot_[*slot];
  foundation::invariant(hot.native && catalog.cold_[*slot].get()==entry.definition.get());
  hot.native(hot,args,work,result);
}
namespace detail {
struct EngineState {
  std::shared_ptr<const registry::Catalog> catalog;
  std::shared_ptr<policy::SessionAuthority> session;
  std::shared_ptr<TrustedThreadPort> thread;
  NativeBudget budget;
  std::atomic<std::size_t> bindings{0};
  mutable std::mutex mutex;
  std::vector<std::optional<InvocationRecord>> observations;
  std::size_t first=0,count=0;
  std::uint64_t sequence=0,dropped=0;
  bool dropped_saturated=false,sequence_exhausted=false;
};
BoundState::~BoundState() { if(counted) --engine->bindings; }
std::optional<std::size_t> BoundState::acquire() noexcept {
  for(std::size_t i=0;i<budget.concurrent_calls_per_binding;++i) {
    bool expected=false;
    if(slots[i].occupied.compare_exchange_strong(expected,true,std::memory_order_acquire))
      return i;
  }
  return {};
}
void BoundState::observe(InvocationRecordKind kind,
                          std::optional<foundation::ErrorCode> error) const noexcept {
  auto& s=*engine;
  std::lock_guard lock(s.mutex);
  auto drop=[&] {
    if(s.dropped<s.budget.observation_counter_limit) ++s.dropped;
    if(s.dropped==s.budget.observation_counter_limit)s.dropped_saturated=true;
  };
  if(s.sequence>=s.budget.observation_counter_limit) {
    s.sequence_exhausted=true;
    drop();
    return;
  }
  InvocationRecord record{++s.sequence,trace,kind,error};
  if(s.count==s.observations.size()) {
    s.observations[s.first]=record;
    s.first=(s.first+1)%s.observations.size();
    drop();
  } else {
    s.observations[(s.first+s.count)%s.observations.size()]=record;
    ++s.count;
  }
}
bool same_targets(std::span<const foundation::ObjectId> fixed,
                  std::span<const foundation::ObjectId> actual) noexcept {
  if(fixed.empty() || fixed.size()!=actual.size()) return false;
  for(std::size_t i=0;i<actual.size();++i) {
    if(actual[i].empty() || std::find(fixed.begin(),fixed.end(),actual[i])==fixed.end()) return false;
    for(std::size_t j=0;j<i;++j) if(actual[i]==actual[j]) return false;
  }
  return true;
}
Result<void> check_thread(const BoundState& state) noexcept {
  auto thread=state.thread->current();
  if(!thread) return make_unexpected(thread.error());
  const auto& execution=state.entry->execution;
  if(thread->role<ThreadRole::Application || thread->role>ThreadRole::Database ||
     !thread->inline_allowed || thread->affinity!=execution.thread_affinity ||
     !execution.inline_safe)
    return reject(InvocationErrc::ThreadRejected);
  if(execution.requires_async_dispatch || execution.requires_external_wait)
    return reject(InvocationErrc::SubmitRequired);
  return {};
}
Result<void> check_managed_thread(const BoundState& state) noexcept {
  auto thread=state.thread->current();
  if(!thread) return make_unexpected(thread.error());
  const auto& execution=state.entry->execution;
  if(thread->role!=ThreadRole::Worker || thread->affinity!=execution.thread_affinity)
    return reject(InvocationErrc::ThreadRejected);
  if(execution.requires_external_wait&&!state.entry->asynchronous_read)
    return reject(InvocationErrc::ProviderUnavailable);
  return {};
}
} // namespace detail

Result<void> detail::validate_budget(NativeBudget budget) {
  if(!budget.bindings||
     !budget.targets_per_binding||!budget.resources_per_binding||
     !budget.concurrent_calls_per_binding||!budget.observation_capacity||
     !budget.work_units||!budget.observation_counter_limit)
    return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
  const auto max=(std::numeric_limits<std::size_t>::max)();
  std::size_t total=sizeof(detail::EngineState);
  auto add=[&](std::size_t count,std::size_t bytes) {
    if(count>max/bytes)return false;
    const auto value=count*bytes;
    if(value>max-total)return false;
    total+=value;return true;
  };
  const auto slots=foundation::checked_mul(budget.bindings,budget.concurrent_calls_per_binding);
  if(!slots || budget.targets_per_binding>max/sizeof(foundation::ObjectId) ||
     budget.resources_per_binding>max/sizeof(registry::ResourceRef) ||
     !add(budget.observation_capacity,sizeof(std::optional<InvocationRecord>)) ||
     !add(budget.bindings,sizeof(detail::BoundState)) ||
     !add(*slots,sizeof(detail::CallSlot)) ||
     !add(*slots,budget.targets_per_binding*sizeof(foundation::ObjectId)) ||
     !add(budget.bindings,budget.targets_per_binding*sizeof(foundation::ObjectId)) ||
     !add(budget.bindings,budget.resources_per_binding*sizeof(registry::ResourceRef)))
    return make_unexpected(invocation_error(InvocationErrc::BudgetExceeded));
  return {};
}
Result<std::shared_ptr<NativeEngine>> NativeEngine::create(
    std::shared_ptr<const registry::Catalog> catalog,
    std::shared_ptr<policy::SessionAuthority> session,
    std::shared_ptr<TrustedThreadPort> thread, NativeBudget budget) {
  if(!owned(catalog)||!owned(session)||!owned(thread))
    return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
  auto valid = detail::validate_budget(budget);
  if(!valid) return make_unexpected(valid.error());
  try {
    auto state=std::make_shared<detail::EngineState>();
    state->catalog=std::move(catalog);
    state->session=std::move(session);
    state->thread=std::move(thread);
    state->budget=budget;
    state->observations.resize(budget.observation_capacity);
    return std::shared_ptr<NativeEngine>(new NativeEngine(std::move(state)));
  } catch(const std::bad_alloc&) {
    return make_unexpected(invocation_error(InvocationErrc::BudgetExceeded));
  } catch(const std::length_error&) {
    return make_unexpected(invocation_error(InvocationErrc::BudgetExceeded));
  }
}
std::shared_ptr<const registry::Catalog> NativeEngine::catalog_owner() const noexcept {
  return state_->catalog;
}
Result<std::shared_ptr<detail::BoundState>> NativeEngine::bind_erased(
    const OperationKey& key, ContractDigest digest, Shape shape,
    std::shared_ptr<const policy::VerifiedCaller> caller,
    std::span<const foundation::ObjectId> targets, Name trace,
    CppTypeToken args, CppTypeToken result) {
  if(!owned(caller)||targets.size()>state_->budget.targets_per_binding ||
     !detail::same_targets(targets,targets))
    return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
  auto authority=caller->authority();
  auto expected=state_->session->callers();
  if(!owned(authority)||!owned(expected)||authority.get()!=expected.get())
    return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
  auto entry=NativeAccess::inspect(*state_->catalog,key,digest,shape,args,result);
  if(!entry) return make_unexpected(entry.error());
  if(entry->resources.size()>state_->budget.resources_per_binding)
    return make_unexpected(invocation_error(InvocationErrc::BudgetExceeded));
  auto bound=std::make_shared<detail::BoundState>(trace);
  bound->engine=state_;
  auto count=state_->bindings.load();
  while(count<state_->budget.bindings &&
        !state_->bindings.compare_exchange_weak(count,count+1)) {}
  if(count>=state_->budget.bindings)
    return make_unexpected(invocation_error(InvocationErrc::BudgetExceeded));
  bound->counted=true; // 自此异常/拒绝均由 BoundState 析构归还占用。
  bound->catalog=state_->catalog;
  bound->session=state_->session;
  bound->caller=caller;
  bound->thread=state_->thread;
  bound->budget=state_->budget;
  bound->targets.assign(targets.begin(),targets.end());
  bound->entry.emplace(std::move(*entry));
  bound->slots=std::make_unique<detail::CallSlot[]>(bound->budget.concurrent_calls_per_binding);
  for(std::size_t i=0;i<bound->budget.concurrent_calls_per_binding;++i)
    bound->slots[i].targets.resize(bound->budget.targets_per_binding);
  auto facts=KnownFacts::create(std::span<const Fact>{},{0,0,0});
  if(!facts) return make_unexpected(facts.error());
  bound->empty_facts=std::move(*facts);
  if(shape==Shape::Read) {
    auto resolver=state_->session->targets();
    if(!owned(resolver)) return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
    std::vector<std::shared_ptr<const TargetView>> originals;
    originals.reserve(targets.size());
    for(auto target: targets) {
      auto resolved=resolver->resolve(caller->view(),target);
      if(!resolved) return make_unexpected(resolved.error());
      if(!owned(*resolved)) return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
      originals.push_back(std::move(*resolved));
    }
    auto authorization=state_->session->prepare_inline(*caller,bound->entry->definition,
        originals,bound->budget.concurrent_calls_per_binding);
    if(!authorization) return make_unexpected(authorization.error());
    bound->authorization=std::move(*authorization);
  }
  return bound;
}
InvocationSnapshot NativeEngine::snapshot(std::span<InvocationRecord> output) const noexcept {
  auto& s=*state_;
  std::lock_guard lock(s.mutex);
  InvocationSnapshot result{0,s.dropped,s.dropped_saturated,s.sequence_exhausted};
  auto count=(std::min)(output.size(),s.count);
  for(std::size_t i=0;i<count;++i)
    output[i]=*s.observations[(s.first+s.count-count+i)%s.observations.size()];
  result.written=count;
  return result;
}
} // namespace ock::runtime::invocation

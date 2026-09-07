#include "invocation.hpp"
#include <algorithm>
#include <mutex>

namespace ock::runtime::invocation {
namespace {
template <class T> bool owned(const std::shared_ptr<T> &value) {
  return value && value.use_count() > 0;
}
Result<void> reject(InvocationErrc code) {
  return make_unexpected(invocation_error(code));
}
}

Result<NativeEntry> NativeAccess::inspect(
    const registry::Catalog &catalog, const OperationKey &key,
    ContractDigest digest, Shape shape, CppTypeToken args, CppTypeToken result) {
  auto handle = catalog.find(key);
  if (!handle)
    return make_unexpected(handle.error());
  auto slot = foundation::resolve_slot(*handle, catalog.identity(),
                                       catalog.generation(), catalog.size());
  if (!slot)
    return make_unexpected(slot.error());
  auto definition = catalog.describe(*slot);
  if (!definition || !*definition)
    return make_unexpected(definition ? error(ContractsErrc::StaleBinding)
                                      : definition.error());
  const auto &d = **definition;
  if (d.description().key != key || d.description().contract_digest != digest ||
      d.shape() != shape || d.args_type() != args || d.result_type() != result)
    return make_unexpected(invocation_error(InvocationErrc::ContractMismatch));
  const auto &hot = catalog.hot_[*slot];
  return NativeEntry{*slot, *definition, d.shape(), d.description().execution,
                     hot.resource_count, hot.native != nullptr};
}

Result<void> NativeAccess::invoke(const registry::Catalog &catalog,
                                  const NativeEntry &entry, const void *args,
                                  WorkContext &work, void *result) {
  if (!args || !result || entry.slot >= catalog.hot_.size() ||
      entry.slot >= catalog.cold_.size() ||
      catalog.cold_[entry.slot].get() != entry.definition.get())
    return reject(InvocationErrc::InvalidBinding);
  const auto &hot = catalog.hot_[entry.slot];
  if (!entry.executable || !hot.native)
    return reject(InvocationErrc::ProviderUnavailable);
  hot.native(hot, args, work, result);
  return {};
}

Result<void> detail::NativeInvocation::call(const registry::Catalog &catalog,
                                            const NativeEntry &entry,
                                            const void *args, WorkContext &work,
                                            void *result) {
  return NativeAccess::invoke(catalog, entry, args, work, result);
}

struct NativeEngine::State {
  std::shared_ptr<const registry::Catalog> catalog;
  std::shared_ptr<policy::SessionAuthority> session;
  std::shared_ptr<TrustedThreadPort> thread;
  NativeBudget budget;
  std::atomic<std::size_t> bindings{0};
  mutable std::mutex observation_mutex;
  std::vector<std::optional<InvocationRecord>> observations;
  std::size_t first = 0, count = 0;
  std::uint64_t sequence = 0, dropped = 0;
  bool dropped_saturated = false, sequence_exhausted = false;
};

void NativeEngine::record_observation(void *opaque, InvocationRecordKind kind,
                                      foundation::ErrorCode error, Name trace) noexcept {
  auto &state = *static_cast<NativeEngine::State *>(opaque);
  std::lock_guard lock(state.observation_mutex);
  if (state.sequence >= state.budget.observation_counter_limit) {
    state.sequence_exhausted = true;
    if (state.dropped < state.budget.observation_counter_limit) ++state.dropped;
    else state.dropped_saturated = true;
    return;
  }
  InvocationRecord record{trace, ++state.sequence, kind, error};
  if (state.count == state.observations.size()) {
    state.observations[state.first] = record;
    state.first = (state.first + 1) % state.observations.size();
    if (state.dropped < state.budget.observation_counter_limit) ++state.dropped;
    else state.dropped_saturated = true;
    return;
  }
  state.observations[(state.first + state.count) % state.observations.size()] = record;
  ++state.count;
}

Result<std::shared_ptr<NativeEngine>> NativeEngine::create(
    std::shared_ptr<const registry::Catalog> catalog,
    std::shared_ptr<policy::SessionAuthority> session,
    std::shared_ptr<TrustedThreadPort> thread, NativeBudget budget) {
  if (!owned(catalog) || !owned(session) || !owned(thread) || !budget.bindings ||
      !budget.targets_per_binding || !budget.concurrent_calls_per_binding ||
      !budget.work_units || !budget.observation_capacity ||
      !budget.observation_counter_limit)
    return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
  try {
    auto state = std::make_shared<State>();
    state->catalog = std::move(catalog);
    state->session = std::move(session);
    state->thread = std::move(thread);
    state->budget = budget;
    state->observations.resize(budget.observation_capacity);
    return std::shared_ptr<NativeEngine>(new NativeEngine(std::move(state)));
  } catch (const std::bad_alloc &) {
    return make_unexpected(invocation_error(InvocationErrc::BudgetExceeded));
  }
}

Result<std::shared_ptr<detail::BoundState>> NativeEngine::bind_erased(
    const OperationKey &key, ContractDigest digest, Shape shape,
    std::shared_ptr<const policy::VerifiedCaller> caller,
    std::span<const foundation::ObjectId> targets, const void *projection,
    Name trace, CppTypeToken args, CppTypeToken result) {
  if (!state_ || !owned(caller) || !projection || targets.empty() ||
      targets.size() > state_->budget.targets_per_binding)
    return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
  auto session_authority = state_->session->callers();
  auto caller_authority = caller->authority();
  if (!owned(session_authority) || !owned(caller_authority) ||
      session_authority.get() != caller_authority.get())
    return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
  auto entry = NativeAccess::inspect(*state_->catalog, key, digest, shape, args, result);
  if (!entry) return make_unexpected(entry.error());
  auto current = state_->bindings.load();
  while (current < state_->budget.bindings &&
         !state_->bindings.compare_exchange_weak(current, current + 1)) {}
  if (current >= state_->budget.bindings)
    return make_unexpected(invocation_error(InvocationErrc::BudgetExceeded));
  try {
    auto bound = std::make_shared<detail::BoundState>(trace);
    bound->catalog = state_->catalog;
    bound->session = state_->session;
    bound->caller = std::move(caller);
    bound->thread = state_->thread;
    bound->entry.emplace(std::move(*entry));
    bound->targets.assign(targets.begin(), targets.end());
    bound->projection = projection;
    auto empty = KnownFacts::create(std::span<const Fact>{}, {0, 0, 0});
    if (!empty) {
      --state_->bindings;
      return make_unexpected(empty.error());
    }
    bound->empty_facts = std::move(*empty);
    bound->budget = state_->budget;
    bound->observer = state_.get();
    bound->record = NativeEngine::record_observation;
    return bound;
  } catch (const std::bad_alloc &) {
    --state_->bindings;
    return make_unexpected(invocation_error(InvocationErrc::BudgetExceeded));
  }
}

namespace detail {
Result<ErasedInvocation> invoke_bound(BoundState &bound, const void *args,
                                      CppTypeToken args_type,
                                      CppTypeToken result_type, void *result,
                                      const InvokeOptions &options) noexcept {
  try {
    const auto finish = [&](ErasedKind kind, Error error) {
      if (bound.record) {
        const auto record_kind = kind == ErasedKind::Completed
            ? InvocationRecordKind::ReadCompleted
            : kind == ErasedKind::Failed ? InvocationRecordKind::FailedBeforeApply
                                         : InvocationRecordKind::Rejected;
        bound.record(bound.observer, record_kind, error.code(), bound.trace);
      }
      return ErasedInvocation{kind, error};
    };
    if (!args || !result || !bound.entry ||
        bound.entry->definition->args_type() != args_type ||
        bound.entry->definition->result_type() != result_type)
      return finish(ErasedKind::Rejected, invocation_error(InvocationErrc::InvalidBinding));
    if (bound.entry->shape != Shape::Read)
      return finish(ErasedKind::Rejected, invocation_error(InvocationErrc::ProviderUnavailable));
    if (bound.entry->resource_count)
      return finish(ErasedKind::Rejected, invocation_error(InvocationErrc::ResourceUnavailable));
    if (!options.work_limit || options.work_limit > bound.budget.work_units ||
        options.stop.stop_requested())
      return finish(ErasedKind::Rejected, invocation_error(InvocationErrc::BudgetExceeded));
    auto thread = bound.thread->current();
    if (!thread || !thread->inline_allowed || thread->role != ThreadRole::Application ||
        thread->affinity != bound.entry->execution.thread_affinity ||
        !bound.entry->execution.inline_safe ||
        bound.entry->execution.requires_async_dispatch || bound.entry->execution.requires_external_wait)
      return finish(ErasedKind::Rejected, invocation_error(InvocationErrc::ThreadRejected));
    auto active = bound.active.load();
    while (active < bound.budget.concurrent_calls_per_binding &&
           !bound.active.compare_exchange_weak(active, active + 1)) {}
    if (active >= bound.budget.concurrent_calls_per_binding)
      return finish(ErasedKind::Rejected, invocation_error(InvocationErrc::Busy));
    struct Release { std::atomic<std::size_t> &value; ~Release() { --value; } } release{bound.active};
    auto admitted = bound.session->authorize_inline(
        *bound.caller, *bound.entry->definition, bound.targets, options.deadline);
    if (!admitted) return finish(ErasedKind::Rejected, admitted.error());
    auto budget = foundation::CheckedCount<std::uint64_t>::create(0, options.work_limit);
    if (!budget)
      return finish(ErasedKind::Rejected, budget.error());
    WorkContext work(options.stop, *admitted, *budget, bound.trace,
                     BorrowedResourceViews{std::span<const ResourceLease *const>{}});
    auto called = NativeInvocation::call(*bound.catalog, *bound.entry, args, work, result);
    if (!called) return finish(ErasedKind::Failed, called.error());
    return finish(ErasedKind::Completed, {});
  } catch (const std::bad_alloc &) {
    return ErasedInvocation{ErasedKind::Failed, invocation_error(InvocationErrc::BudgetExceeded)};
  } catch (...) {
    return ErasedInvocation{ErasedKind::Failed, invocation_error(InvocationErrc::HandlerException)};
  }
}
} // namespace detail

InvocationSnapshot NativeEngine::snapshot(std::span<InvocationRecord> output) const noexcept {
  std::lock_guard lock(state_->observation_mutex);
  InvocationSnapshot result{0, state_->dropped, state_->dropped_saturated,
                            state_->sequence_exhausted};
  auto copy = (std::min)(output.size(), state_->count);
  for (std::size_t i = 0; i < copy; ++i) {
    const auto &record = state_->observations[(state_->first + state_->count - copy + i) %
                                              state_->observations.size()];
    if (record) output[i] = *record;
  }
  result.written = copy;
  return result;
}
} // namespace ock::runtime::invocation

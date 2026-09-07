#pragma once
// D1.02：描述DTO不授予能力；受信接收端必须核对配置authority。
#include <chrono>
#include <ock/contracts/identity.hpp>
#include <stop_token>
namespace ock::contracts {
struct CallerDescription {
  PrincipalRef principal;
  std::optional<PrincipalRef> delegated_by;
  std::vector<Name> tags;
};
struct PermitBinding {
  PrincipalRef principal;
  OperationKey operation;
  ContractDigest group;
  foundation::ObjectId target;
  std::uint64_t permission_generation, lifecycle_generation;
  std::chrono::steady_clock::time_point deadline;
  bool operator==(const PermitBinding &) const = default;
};
class CallerGrant : public PortLifetime {
public:
  virtual const CallerDescription &description() const noexcept = 0;

protected:
  CallerGrant() = default;
};
class CallerAuthorityPort : public PortLifetime {
public:
  virtual Result<std::shared_ptr<const CallerGrant>>
  authenticate(const CallerDescription &) = 0;
  virtual Result<void> validate(const CallerGrant &) const = 0;
};
class CallerView final {
public:
  static Result<CallerView>
  check(std::shared_ptr<CallerAuthorityPort> authority,
        std::shared_ptr<const CallerGrant> grant) {
    if (!authority || !grant)
      return make_unexpected(error(ContractsErrc::InvalidAuthority));
    auto valid = authority->validate(*grant);
    if (!valid)
      return make_unexpected(valid.error());
    return CallerView{std::move(authority), std::move(grant)};
  }
  Result<void> revalidate() const { return authority_->validate(*grant_); }
  bool belongs_to(const CallerAuthorityPort &a) const noexcept {
    return authority_.get() == &a;
  }
  const CallerDescription &description() const noexcept {
    return grant_->description();
  }

private:
  CallerView(std::shared_ptr<CallerAuthorityPort> a,
             std::shared_ptr<const CallerGrant> g)
      : authority_(std::move(a)), grant_(std::move(g)) {}
  std::shared_ptr<CallerAuthorityPort> authority_;
  std::shared_ptr<const CallerGrant> grant_;
};
inline Result<void> validate_caller(const CallerAuthorityPort &expected,
                                    const CallerView &view) {
  if (!view.belongs_to(expected))
    return reject(ContractsErrc::InvalidAuthority);
  return view.revalidate();
}
class ActionPermit : public PortLifetime {
public:
  virtual const PermitBinding &binding() const noexcept = 0;

protected:
  ActionPermit() = default;
};
class PermitAuthorityPort : public PortLifetime {
public:
  virtual Result<std::shared_ptr<const ActionPermit>>
  issue(const CallerGrant &, const PermitBinding &) = 0;
  virtual Result<void> consume(const ActionPermit &, const PermitBinding &) = 0;
};
class ActivityLease : public PortLifetime {};
class ResourceLease : public PortLifetime {};
class TargetView : public PortLifetime {
public:
  virtual foundation::ObjectId target() const noexcept = 0;

protected:
  TargetView() = default;
};
class TargetAuthorityPort : public PortLifetime {
public:
  virtual Result<std::shared_ptr<const TargetView>>
  resolve(const CallerView &, foundation::ObjectId requested) = 0;
  virtual Result<void> validate(const TargetView &, const CallerView &,
                                foundation::ObjectId expected) const = 0;
};
namespace detail {
// 仅检查接收者已配置的authority及当前绑定，不从permit反取expected。
inline Result<void> validate_context_target(const CallerAuthorityPort &callers,
                                            const TargetAuthorityPort &targets,
                                            const CallerView &caller,
                                            const TargetView &target,
                                            const ActionPermit &permit,
                                            const PermitBinding &expected) {
  auto valid = validate_caller(callers, caller);
  if (!valid)
    return valid;
  if (permit.binding() != expected ||
      expected.principal != caller.description().principal ||
      expected.target.empty() || expected.permission_generation == 0 ||
      expected.lifecycle_generation == 0 ||
      expected.deadline <= std::chrono::steady_clock::now())
    return reject(ContractsErrc::InvalidGrant);
  return targets.validate(target, caller, expected.target);
}
} // namespace detail
enum class Shape { Read, StateEdit, ExternalEffect, Lifecycle };
enum class AtomicMode { Incompatible, PureCompute, CandidateRead, StateEdit };
struct ExecutionRequirements {
  bool inline_safe = false, requires_async_dispatch = false,
       requires_external_wait = false;
  Name executor;
  Name thread_affinity;
};
struct BorrowedResourceViews {
  std::span<const ResourceLease *const> values;
  explicit BorrowedResourceViews(std::span<const ResourceLease *const> input)
      : values(input) {}
};
class WorkContext final {
public:
  WorkContext(std::stop_token stop,
              std::chrono::steady_clock::time_point deadline,
              foundation::CheckedCount<std::uint64_t> budget, Name trace,
              const std::vector<std::shared_ptr<ResourceLease>> &resources)
      : stop_(stop), deadline_(deadline), budget_(budget), trace_(trace),
        owned_resources_(std::in_place) {
    owned_resources_->owners = resources;
    for (const auto &r : owned_resources_->owners) {
      foundation::invariant(bool(r));
      owned_resources_->views.push_back(r.get());
    }
    resource_views_ = owned_resources_->views;
  }
  WorkContext(std::stop_token stop,
              std::chrono::steady_clock::time_point deadline,
              foundation::CheckedCount<std::uint64_t> budget, Name trace,
              BorrowedResourceViews resources)
      : stop_(stop), deadline_(deadline), budget_(budget), trace_(trace),
        resource_views_(resources.values) {
    for (const auto *resource : resource_views_)
      foundation::invariant(resource != nullptr);
  }
  WorkContext(const WorkContext &) = delete;
  WorkContext &operator=(const WorkContext &) = delete;
  bool stop_requested() const noexcept { return stop_.stop_requested(); }
  std::chrono::steady_clock::time_point deadline() const noexcept {
    return deadline_;
  }
  Result<void> charge(std::uint64_t units) { return budget_.try_add(units); }
  const Name &trace_name() const noexcept { return trace_; }
  std::span<const ResourceLease *const> granted_resources() const noexcept {
    return resource_views_;
  }

private:
  std::stop_token stop_;
  std::chrono::steady_clock::time_point deadline_;
  foundation::CheckedCount<std::uint64_t> budget_;
  Name trace_;
  struct OwnedResources {
    std::vector<std::shared_ptr<ResourceLease>> owners;
    std::vector<const ResourceLease *> views;
  };
  std::optional<OwnedResources> owned_resources_;
  std::span<const ResourceLease *const> resource_views_;
};
template <class Reader> class ReadServices final {
public:
  explicit ReadServices(std::shared_ptr<const Reader> reader)
      : reader_(std::move(reader)) {
    foundation::invariant(bool(reader_));
  }
  ReadServices(const ReadServices &) = delete;
  ReadServices &operator=(const ReadServices &) = delete;
  const Reader &reader() const noexcept { return *reader_; }

private:
  std::shared_ptr<const Reader> reader_;
};
template <class P> class EditView final {
public:
  EditView(typename P::EditPort &port, AtomicDomainRef domain)
      : port_(port), domain_(std::move(domain)) {}
  EditView(const EditView &) = delete;
  EditView &operator=(const EditView &) = delete;
  typename P::EditPort &edit() noexcept { return port_; }
  const AtomicDomainRef &domain() const noexcept { return domain_; }

private:
  typename P::EditPort &port_;
  AtomicDomainRef domain_;
};
class EffectRecordPort : public PortLifetime {
public:
  virtual Result<void> record_attempt(EffectId) = 0;
};
class EffectContext final {
public:
  static Result<std::unique_ptr<EffectContext>>
  check(std::shared_ptr<const CallerAuthorityPort> expected_caller_authority,
        std::shared_ptr<const TargetAuthorityPort> expected_target_authority,
        CallerView caller, std::shared_ptr<const TargetView> target,
        std::shared_ptr<const ActionPermit> permit,
        const PermitBinding &expected_binding, WorkContext &work,
        EffectRecordPort &records) {
    if (!expected_caller_authority || !expected_target_authority)
      return make_unexpected(error(ContractsErrc::InvalidAuthority));
    if (!target || !permit)
      return make_unexpected(error(ContractsErrc::InvalidGrant));
    auto valid = detail::validate_context_target(
        *expected_caller_authority, *expected_target_authority, caller, *target,
        *permit, expected_binding);
    if (!valid)
      return make_unexpected(valid.error());
    return std::unique_ptr<EffectContext>(new EffectContext(
        std::move(expected_caller_authority),
        std::move(expected_target_authority), std::move(caller),
        std::move(target), std::move(permit), work, records));
  }
  Result<void> revalidate(const CallerAuthorityPort &expected_caller_authority,
                          const TargetAuthorityPort &expected_target_authority,
                          const PermitBinding &current_expected_binding) const {
    if (caller_authority_.get() != &expected_caller_authority ||
        target_authority_.get() != &expected_target_authority)
      return reject(ContractsErrc::InvalidAuthority);
    return detail::validate_context_target(
        expected_caller_authority, expected_target_authority, caller_, *target_,
        *permit_, current_expected_binding);
  }
  EffectContext(const EffectContext &) = delete;
  EffectContext &operator=(const EffectContext &) = delete;
  const TargetView &target() const noexcept { return *target_; }
  const PermitBinding &permit_binding() const noexcept {
    return permit_->binding();
  }
  bool stop_requested() const noexcept { return work_.stop_requested(); }
  Result<void> charge(std::uint64_t units) { return work_.charge(units); }
  EffectRecordPort &records() noexcept { return records_; }

private:
  EffectContext(std::shared_ptr<const CallerAuthorityPort> callers,
                std::shared_ptr<const TargetAuthorityPort> targets,
                CallerView caller, std::shared_ptr<const TargetView> target,
                std::shared_ptr<const ActionPermit> permit, WorkContext &work,
                EffectRecordPort &records)
      : caller_authority_(std::move(callers)),
        target_authority_(std::move(targets)), caller_(std::move(caller)),
        target_(std::move(target)), permit_(std::move(permit)), work_(work),
        records_(records) {}
  std::shared_ptr<const CallerAuthorityPort> caller_authority_;
  std::shared_ptr<const TargetAuthorityPort> target_authority_;
  CallerView caller_;
  std::shared_ptr<const TargetView> target_;
  std::shared_ptr<const ActionPermit> permit_;
  WorkContext &work_;
  EffectRecordPort &records_;
};
class TransitionPort : public PortLifetime {
public:
  virtual Result<Name> transition(const Name &desired) = 0;
};
class TransitionView final {
public:
  static Result<std::unique_ptr<TransitionView>>
  check(std::shared_ptr<const CallerAuthorityPort> expected_caller_authority,
        std::shared_ptr<const TargetAuthorityPort> expected_target_authority,
        CallerView caller, std::shared_ptr<const TargetView> target,
        Name before, std::uint64_t generation,
        std::shared_ptr<const ActionPermit> permit,
        const PermitBinding &expected_binding, TransitionPort &transition) {
    if (!expected_caller_authority || !expected_target_authority)
      return make_unexpected(error(ContractsErrc::InvalidAuthority));
    if (!target || !permit)
      return make_unexpected(error(ContractsErrc::InvalidGrant));
    auto valid = detail::validate_context_target(
        *expected_caller_authority, *expected_target_authority, caller, *target,
        *permit, expected_binding);
    if (!valid)
      return make_unexpected(valid.error());
    if (!generation || generation != expected_binding.lifecycle_generation)
      return make_unexpected(error(ContractsErrc::InvalidGrant));
    return std::unique_ptr<TransitionView>(new TransitionView(
        std::move(expected_caller_authority),
        std::move(expected_target_authority), std::move(caller),
        std::move(target), before, generation, std::move(permit), transition));
  }
  Result<void> revalidate(const CallerAuthorityPort &expected_caller_authority,
                          const TargetAuthorityPort &expected_target_authority,
                          const PermitBinding &current_expected_binding,
                          const Name &current_before) const {
    if (caller_authority_.get() != &expected_caller_authority ||
        target_authority_.get() != &expected_target_authority)
      return reject(ContractsErrc::InvalidAuthority);
    auto valid = detail::validate_context_target(
        expected_caller_authority, expected_target_authority, caller_, *target_,
        *permit_, current_expected_binding);
    if (!valid)
      return valid;
    if (before_ != current_before ||
        generation_ != current_expected_binding.lifecycle_generation)
      return reject(ContractsErrc::InvalidGrant);
    return {};
  }
  TransitionView(const TransitionView &) = delete;
  TransitionView &operator=(const TransitionView &) = delete;
  const TargetView &target() const noexcept { return *target_; }
  const Name &before() const noexcept { return before_; }
  std::uint64_t generation() const noexcept { return generation_; }
  const PermitBinding &permit_binding() const noexcept {
    return permit_->binding();
  }
  TransitionPort &transition() noexcept { return port_; }

private:
  TransitionView(std::shared_ptr<const CallerAuthorityPort> callers,
                 std::shared_ptr<const TargetAuthorityPort> targets,
                 CallerView caller, std::shared_ptr<const TargetView> target,
                 Name before, std::uint64_t generation,
                 std::shared_ptr<const ActionPermit> permit,
                 TransitionPort &port)
      : caller_authority_(std::move(callers)),
        target_authority_(std::move(targets)), caller_(std::move(caller)),
        target_(std::move(target)), before_(before), generation_(generation),
        permit_(std::move(permit)), port_(port) {}
  std::shared_ptr<const CallerAuthorityPort> caller_authority_;
  std::shared_ptr<const TargetAuthorityPort> target_authority_;
  CallerView caller_;
  std::shared_ptr<const TargetView> target_;
  Name before_;
  std::uint64_t generation_;
  std::shared_ptr<const ActionPermit> permit_;
  TransitionPort &port_;
};
} // namespace ock::contracts

#pragma once
// D1.02：只冻结从实际签名推导的描述；Handler留注册作者与后续Registry持有。
#include <ock/contracts/context.hpp>
#include <ock/contracts/identity.hpp>
#include <ock/contracts/observation.hpp>
#include <ock/contracts/outcome.hpp>
#include <ock/contracts/ports.hpp>
namespace ock::contracts {
struct DefinitionInput {
  OperationKey key;
  ContractDigest contract_digest;
  ExecutionRequirements execution;
  AtomicMode atomic_mode;
  std::vector<Name> required_permissions;
  std::string docs;
  std::size_t max_docs_bytes = 4096;
};
template <class P> struct ProviderContract;
template <class A, class R> class OperationDefinition;
class DefinitionSnapshot final {
public:
  DefinitionSnapshot(const DefinitionSnapshot &) = delete;
  DefinitionSnapshot &operator=(const DefinitionSnapshot &) = delete;
  DefinitionSnapshot &operator=(DefinitionSnapshot &&) = delete;
  const DefinitionInput &description() const noexcept { return input_; }
  Shape shape() const noexcept { return shape_; }
  CppTypeToken args_type() const noexcept { return args_; }
  CppTypeToken result_type() const noexcept { return result_; }
  const TypeIdentity &args_contract() const noexcept { return args_contract_; }
  const TypeIdentity &result_contract() const noexcept {
    return result_contract_;
  }
  CppTypeToken context_type() const noexcept { return context_; }
  bool asynchronous_read() const noexcept {return asynchronous_read_;}
  const std::optional<AtomicProviderKey> &provider() const noexcept {
    return provider_;
  }
  const std::optional<CppTypeToken> &provider_type() const noexcept {
    return provider_type_;
  }
  const std::optional<CppTypeToken> &candidate_type() const noexcept {
    return candidate_type_;
  }

private:
  template <class A, class R> friend class OperationDefinition;
  DefinitionSnapshot(const DefinitionInput &input, Shape shape, CppTypeToken a,
                     CppTypeToken r, TypeIdentity ac, TypeIdentity rc,
                     CppTypeToken context,
                     std::optional<AtomicProviderKey> provider = {},
                     std::optional<CppTypeToken> provider_type = {},
                     std::optional<CppTypeToken> candidate_type = {},bool asynchronous_read=false)
      : input_(input), shape_(shape), args_(a), result_(r),
        args_contract_(std::move(ac)), result_contract_(std::move(rc)),
        context_(context), provider_(provider), provider_type_(provider_type),
        candidate_type_(candidate_type),asynchronous_read_(asynchronous_read) {}
  DefinitionInput input_;
  Shape shape_;
  CppTypeToken args_, result_;
  TypeIdentity args_contract_, result_contract_;
  CppTypeToken context_;
  std::optional<AtomicProviderKey> provider_;
  std::optional<CppTypeToken> provider_type_, candidate_type_;
  bool asynchronous_read_;
};
template <class A, class R> class OperationDefinition final {
public:
  const std::shared_ptr<const DefinitionSnapshot> &snapshot() const noexcept {
    return snapshot_;
  }
  template <class Reader>
  static Result<OperationDefinition>
  read(Result<R> (*fn)(const A &, WorkContext &, ReadServices<Reader> &),
       const DefinitionInput &in)
    requires(ContractValue<A> && ContractResult<R>)
  {
    if (!fn || in.atomic_mode != AtomicMode::Incompatible)
      return make_unexpected(error(ContractsErrc::InvalidContract));
    return create(in, Shape::Read, CppTypeToken::of<Reader>());
  }
  static Result<OperationDefinition>
  compute(Result<R> (*fn)(const A &, WorkContext &), const DefinitionInput &in)
    requires(ContractValue<A> && ContractResult<R>)
  {
    if (!fn || in.atomic_mode != AtomicMode::PureCompute)
      return make_unexpected(error(ContractsErrc::InvalidContract));
    return create(in, Shape::Read, CppTypeToken::of<void>());
  }
  template<class Reader>
  static Result<OperationDefinition> read_async(
      Result<void> (*fn)(std::shared_ptr<AsyncReadCall<A,R,Reader>>),const DefinitionInput& in)
    requires (AsyncInput<A> && ContractResult<R> && (std::same_as<R,void> || AsyncInput<R>)) {
    if(!fn||in.atomic_mode!=AtomicMode::Incompatible||in.execution.inline_safe||
       !in.execution.requires_external_wait)
      return make_unexpected(error(ContractsErrc::InvalidContract));
    return create(in,Shape::Read,CppTypeToken::of<Reader>(),{},{},{},true);
  }
  template <class P>
  static Result<OperationDefinition>
  edit(Result<R> (*fn)(const A &, EditView<P> &, WorkContext &),
       const DefinitionInput &in)
    requires(ContractValue<A> && ContractResult<R>)
  {
    if (!fn || in.atomic_mode != AtomicMode::StateEdit)
      return make_unexpected(error(ContractsErrc::InvalidContract));
    return create(in, Shape::StateEdit, CppTypeToken::of<P>(),
                  ProviderContract<P>::key(), CppTypeToken::of<P>());
  }
  static Result<OperationDefinition>
  effect(EffectReport<R> (*fn)(const A &, EffectContext &),
         const DefinitionInput &in)
    requires(ContractValue<A> && ContractResult<R>)
  {
    if (!fn || in.atomic_mode != AtomicMode::Incompatible)
      return make_unexpected(error(ContractsErrc::InvalidContract));
    return create(in, Shape::ExternalEffect, CppTypeToken::of<EffectContext>());
  }
  static Result<OperationDefinition>
  lifecycle(TransitionReport<R> (*fn)(const A &, TransitionView &),
            const DefinitionInput &in)
    requires(ContractValue<A> && ContractResult<R>)
  {
    if (!fn || in.atomic_mode != AtomicMode::Incompatible)
      return make_unexpected(error(ContractsErrc::InvalidContract));
    return create(in, Shape::Lifecycle, CppTypeToken::of<TransitionView>());
  }
  template <class P>
  static Result<OperationDefinition>
  candidate(const OperationDefinition &read,
            Result<R> (*fn)(const A &, const typename P::CandidateReadPort &,
                            WorkContext &),
            AtomicProviderKey expected)
    requires(ContractValue<A> && ContractResult<R>)
  {
    const auto &s = *read.snapshot_;
    if (!fn || s.shape() != Shape::Read ||
        s.description().atomic_mode != AtomicMode::Incompatible ||
        expected != ProviderContract<P>::key() ||
        s.description().execution.requires_async_dispatch ||
        s.description().execution.requires_external_wait)
      return make_unexpected(error(ContractsErrc::InvalidContract));
    auto in = s.description();
    in.atomic_mode = AtomicMode::CandidateRead;
    return create(in, Shape::Read, s.context_type(), expected,
                  CppTypeToken::of<P>(),
                  CppTypeToken::of<typename P::CandidateReadPort>());
  }

private:
  explicit OperationDefinition(std::shared_ptr<const DefinitionSnapshot> s)
      : snapshot_(std::move(s)) {}
  static Result<OperationDefinition>
  create(const DefinitionInput &in, Shape shape, CppTypeToken context,
         std::optional<AtomicProviderKey> provider = {},
         std::optional<CppTypeToken> provider_type = {},
         std::optional<CppTypeToken> candidate_type = {},bool asynchronous_read=false) {
    if (in.docs.size() > in.max_docs_bytes ||
        in.required_permissions.size() > 96 ||
        !foundation::detail::valid_utf8(in.docs))
      return make_unexpected(error(ContractsErrc::BudgetExceeded));
    for (std::size_t i = 0; i < in.required_permissions.size(); ++i)
      for (std::size_t j = 0; j < i; ++j)
        if (in.required_permissions[i] == in.required_permissions[j])
          return make_unexpected(error(ContractsErrc::InvalidContract));
    return OperationDefinition{
        std::shared_ptr<const DefinitionSnapshot>(new DefinitionSnapshot(
            in, shape, CppTypeToken::of<A>(), CppTypeToken::of<R>(),
            TypeContract<A>::identity(), TypeContract<R>::identity(), context,
            provider, provider_type, candidate_type,asynchronous_read))};
  }
  std::shared_ptr<const DefinitionSnapshot> snapshot_;
};
template <ContractValue A, ContractResult R, class Reader>
auto make_read_definition(Result<R> (*fn)(const A &, WorkContext &,
                                          ReadServices<Reader> &),
                          const DefinitionInput &in) {
  return OperationDefinition<A, R>::template read<Reader>(fn, in);
}
template <ContractValue A, ContractResult R>
auto make_compute_definition(Result<R> (*fn)(const A &, WorkContext &),
                             const DefinitionInput &in) {
  return OperationDefinition<A, R>::compute(fn, in);
}
template <ContractValue A, ContractResult R, class P>
auto make_state_edit_definition(Result<R> (*fn)(const A &, EditView<P> &,
                                                WorkContext &),
                                const DefinitionInput &in) {
  return OperationDefinition<A, R>::template edit<P>(fn, in);
}
template <ContractValue A, ContractResult R>
auto make_effect_definition(EffectReport<R> (*fn)(const A &, EffectContext &),
                            const DefinitionInput &in) {
  return OperationDefinition<A, R>::effect(fn, in);
}
template <ContractValue A, ContractResult R>
auto make_lifecycle_definition(TransitionReport<R> (*fn)(const A &,
                                                         TransitionView &),
                               const DefinitionInput &in) {
  return OperationDefinition<A, R>::lifecycle(fn, in);
}
template <class P, ContractValue A, ContractResult R>
auto with_candidate_read(const OperationDefinition<A, R> &read,
                         Result<R> (*fn)(const A &,
                                         const typename P::CandidateReadPort &,
                                         WorkContext &),
                         AtomicProviderKey provider) {
  return OperationDefinition<A, R>::template candidate<P>(read, fn, provider);
}
template <class A, class R>
Result<void> validate_atomic_admission(const OperationDefinition<A, R> &def,
                                       const AtomicDomainRef &domain,
                                       CppTypeToken provider) {
  const auto &d = *def.snapshot();
  const auto &in = d.description();
  if (!valid_domain(domain) || in.execution.requires_async_dispatch ||
      in.execution.requires_external_wait)
    return reject(ContractsErrc::InvalidContract);
  if (in.atomic_mode == AtomicMode::PureCompute && d.shape() == Shape::Read)
    return {};
  if ((in.atomic_mode != AtomicMode::CandidateRead &&
       in.atomic_mode != AtomicMode::StateEdit) ||
      !d.provider() || !d.provider_type() || *d.provider() != domain.provider ||
      *d.provider_type() != provider)
    return reject(ContractsErrc::InvalidContract);
  if (in.atomic_mode == AtomicMode::CandidateRead && !d.candidate_type())
    return reject(ContractsErrc::InvalidContract);
  return {};
}
struct OperationHandleTag {};
using OperationHandle = foundation::BoundHandle<OperationHandleTag>;
class BindingPort : public PortLifetime {
public:
  virtual foundation::RegistryId identity() const noexcept = 0;
  virtual foundation::RegistryGeneration generation() const noexcept = 0;
  virtual std::size_t size() const noexcept = 0;
  virtual Result<OperationHandle> find(const OperationKey &) const = 0;
  virtual Result<std::shared_ptr<const DefinitionSnapshot>>
  describe(std::uint32_t) const = 0;
};
template <ContractValue A, ContractResult R> class BoundOperation final {
public:
  static Result<BoundOperation> bind(std::shared_ptr<const BindingPort> port,
                                     const OperationKey &key,
                                     ContractDigest digest, Shape shape) {
    if (!port)
      return make_unexpected(error(ContractsErrc::StaleBinding));
    auto handle = port->find(key);
    if (!handle)
      return make_unexpected(handle.error());
    auto slot = foundation::resolve_slot(*handle, port->identity(),
                                         port->generation(), port->size());
    if (!slot)
      return make_unexpected(slot.error());
    auto def = port->describe(*slot);
    if (!def)
      return make_unexpected(def.error());
    if (!*def)
      return make_unexpected(error(ContractsErrc::StaleBinding));
    auto match = matches(**def, key, digest, shape);
    if (!match)
      return make_unexpected(match.error());
    return BoundOperation{std::move(port), *handle, *def};
  }
  const OperationKey &key() const noexcept {
    return definition_->description().key;
  }
  Result<void> revalidate() const {
    auto slot = foundation::resolve_slot(handle_, port_->identity(),
                                         port_->generation(), port_->size());
    if (!slot)
      return make_unexpected(slot.error());
    auto def = port_->describe(*slot);
    if (!def)
      return make_unexpected(def.error());
    if (!*def || def->get() != definition_.get())
      return reject(ContractsErrc::StaleBinding);
    return matches(**def, key(), definition_->description().contract_digest,
                   definition_->shape());
  }

private:
  static Result<void> matches(const DefinitionSnapshot &d,
                              const OperationKey &key, ContractDigest digest,
                              Shape shape) {
    if (d.args_type() != CppTypeToken::of<A>() ||
        d.result_type() != CppTypeToken::of<R>() ||
        d.args_contract() != TypeContract<A>::identity() ||
        d.result_contract() != TypeContract<R>::identity())
      return reject(ContractsErrc::TypeMismatch);
    if (d.shape() != shape)
      return reject(ContractsErrc::ShapeMismatch);
    if (d.description().key != key || d.description().contract_digest != digest)
      return reject(ContractsErrc::InvalidContract);
    return {};
  }
  BoundOperation(std::shared_ptr<const BindingPort> p, OperationHandle h,
                 std::shared_ptr<const DefinitionSnapshot> d)
      : port_(std::move(p)), handle_(h), definition_(std::move(d)) {}
  std::shared_ptr<const BindingPort> port_;
  OperationHandle handle_;
  std::shared_ptr<const DefinitionSnapshot> definition_;
};
template <ContractValue A, ContractResult R>
auto bind(std::shared_ptr<const BindingPort> port, const OperationKey &key,
          ContractDigest digest, Shape shape) {
  return BoundOperation<A, R>::bind(std::move(port), key, digest, shape);
}
template <ContractValue A, ContractResult R>
Result<void> validate_inline_args(const BoundOperation<A, R> &bound,
                                  const A &args) {
  auto v = bound.revalidate();
  if (!v)
    return v;
  return TypeContract<A>::validate(args);
}
template <AsyncInput A, ContractResult R>
Result<std::unique_ptr<const A>>
prepare_submit_args(const BoundOperation<A, R> &bound, A &&args) {
  auto v = validate_inline_args(bound, args);
  if (!v)
    return make_unexpected(v.error());
  return std::unique_ptr<const A>(new A(std::move(args)));
}
} // namespace ock::contracts

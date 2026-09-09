#pragma once
// NativeSubset 唯一注册器声明。
#include <functional>
#include <utility>
#include <ock/contracts/operation.hpp>
namespace ock::runtime::invocation {
class NativeAccess;
}
namespace ock::runtime::registry {
using namespace contracts;
using foundation::RegistryGeneration;
using foundation::RegistryId;
enum class RegistryErrc : std::uint32_t {
  InvalidManifest = 1,
  DuplicateModule,
  MissingDependency,
  VersionMismatch,
  DependencyCycle,
  UndeclaredOperation,
  MissingOperation,
  DuplicateOperation,
  InvalidDefinition,
  MissingService,
  ServiceTypeMismatch,
  MissingProvider,
  ProviderMismatch,
  MissingExecutor,
  CapabilityMismatch,
  MissingResource,
  MissingConfiguration,
  UnsupportedSchema,
  CallbackException,
  BudgetExceeded,
  Frozen,
  InvalidIdentity
};
inline constexpr foundation::ErrorDomain registry_domain{"ock.registry"};
inline Error registry_error(RegistryErrc c) noexcept {
  return Error{foundation::ErrorCode::make<registry_domain>(
      static_cast<std::uint32_t>(c))};
}
struct RegistrationError {
  RegistryErrc code;
  std::optional<Name> module;
  std::optional<OperationKey> operation;
};
struct BatchBudget {
  std::size_t modules = 128, operations = 4096, declarations = 16384,
              text_bytes = 1048576, diagnostics = 128;
};
struct ModuleDependency {
  Name name;
  OperationVersion version;
  bool operator==(const ModuleDependency &) const = default;
};
struct ServiceRef {
  Name module, name;
  bool operator==(const ServiceRef &) const = default;
};
struct ProviderRef {
  Name module;
  AtomicProviderKey key;
  bool operator==(const ProviderRef &) const = default;
};
struct ResourceRef {
  Name module, name;
  bool operator==(const ResourceRef &) const = default;
};
struct ExecutorRef {
  Name module, name;
  bool operator==(const ExecutorRef &) const = default;
};
struct ServiceRequirement {
  ServiceRef ref;
  CppTypeToken type;
};
struct ProviderRequirement {
  ProviderRef ref;
  CppTypeToken type;
};
struct ModuleManifest {
  Name name;
  OperationVersion version;
  std::vector<ModuleDependency> dependencies;
  std::vector<OperationKey> operations;
  std::vector<Name> services;
  std::vector<AtomicProviderKey> providers;
  std::vector<Name> resources;
  std::vector<TypeIdentity> required_configuration;
  std::vector<ServiceRequirement> required_services;
  std::vector<ProviderRequirement> required_providers;
  std::vector<ResourceRef> required_resources;
  std::vector<ExecutorRef> required_executors;
  std::vector<Name> executors;
};
class RegistrationBatch;
class ServiceBinding final {
  friend class RegistrationBatch;
  Name name_;
  CppTypeToken type_;
  std::shared_ptr<void> owner_;
  ServiceBinding(Name n, CppTypeToken t, std::shared_ptr<void> o)
      : name_(std::move(n)), type_(t), owner_(std::move(o)) {}

public:
  template <class T>
  static Result<ServiceBinding> make(Name n, std::shared_ptr<T> p)
    requires(std::is_object_v<T> && !std::is_const_v<T>)
  {
    if (!p || p.use_count() == 0)
      return make_unexpected(registry_error(RegistryErrc::MissingService));
    return ServiceBinding(std::move(n), CppTypeToken::of<T>(), std::move(p));
  }
};
class ProviderBinding final {
  friend class RegistrationBatch;
  AtomicProviderKey key_;
  CppTypeToken type_;
  std::shared_ptr<void> owner_;
  ProviderBinding(AtomicProviderKey k, CppTypeToken t, std::shared_ptr<void> o)
      : key_(std::move(k)), type_(t), owner_(std::move(o)) {}

public:
  template <class P>
  static Result<ProviderBinding>
  make(std::shared_ptr<AtomicProviderPort<P>> p) {
    if (!p || p.use_count() == 0)
      return make_unexpected(registry_error(RegistryErrc::MissingProvider));
    return ProviderBinding(ProviderContract<P>::key(), CppTypeToken::of<P>(),
                           std::move(p));
  }
};
template <class T> struct ConfigurationSnapshot;
class ConfigurationBinding final {
  friend class RegistrationBatch;
  TypeIdentity identity_;
  std::shared_ptr<const void> owner_;
  ConfigurationBinding(TypeIdentity i, std::shared_ptr<const void> o)
      : identity_(std::move(i)), owner_(std::move(o)) {}

public:
  template <ContractValue T>
  static Result<ConfigurationBinding> make(const T &value)
    requires requires {
      { ConfigurationSnapshot<T>::freeze(value) } -> std::same_as<Result<T>>;
    }
  {
    auto frozen = ConfigurationSnapshot<T>::freeze(value);
    if (!frozen)
      return make_unexpected(frozen.error());
    auto valid = TypeContract<T>::validate(*frozen);
    if (!valid)
      return make_unexpected(valid.error());
    return ConfigurationBinding(TypeContract<T>::identity(),
                                std::make_shared<const T>(std::move(*frozen)));
  }
};
struct ResourceBinding {
  Name name;
  std::shared_ptr<ResourceLease> owner;
};
struct ExecutorBinding {
  Name name, affinity;
  bool async_dispatch, external_wait;
  std::shared_ptr<ExecutorPort> owner;
};
class Registrar;
struct ModuleInput {
  ModuleManifest manifest;
  std::vector<ServiceBinding> services;
  std::vector<ProviderBinding> providers;
  std::vector<ResourceBinding> resources;
  std::vector<ConfigurationBinding> configurations;
  std::vector<ExecutorBinding> executors;
  std::function<void(Registrar &)> register_operations;
};
struct OperationOptions {
  std::optional<ServiceRef> read_service;
  std::optional<ProviderRef> provider;
  ExecutorRef executor;
  std::vector<ResourceRef> resources;
  bool requires_dynamic_schema = false;
};
// 由可信注册者声明拥有型提交的完整存储额度，包含动态缓冲区。
// 客户端提交不能替换计量函数或上限；未声明者仅保留原 Invoke 能力。
template <AsyncInput A, ContractResult R>
  requires (std::same_as<R,void> || AsyncInput<R>)
struct SubmissionStorage {
  std::size_t input_limit, reply_limit;
  Result<std::size_t> (*input_bytes)(const A&);
  Result<std::size_t> (*reply_bytes)(const InvokeReply<R>&);
};
// 热条目没有冷描述指针。处理器及其真实类型见证只可被注册器保存。
namespace detail {
// 仅受管理入口回传当前准入期限；公开 scope 不提供修改期限的权限。
class InvocationScopePort : public ExecutionScopePort {
public:
  virtual void admitted(std::chrono::steady_clock::time_point) noexcept=0;
};
inline thread_local bool execution_callback_active=false;
class ExecutionCallbackFrame final {
public:
  ExecutionCallbackFrame() noexcept:previous_(std::exchange(execution_callback_active,true)) {}
  ~ExecutionCallbackFrame() {execution_callback_active=previous_;}
  ExecutionCallbackFrame(const ExecutionCallbackFrame&)=delete;
  ExecutionCallbackFrame& operator=(const ExecutionCallbackFrame&)=delete;
private:
  bool previous_;
};
// 私有 owning 适配，不暴露 Registry dispatch；实际 A/R 来自冻结签名。
template<AsyncInput A,ContractResult R>
  requires (std::same_as<R,void> || AsyncInput<R>)
class AsyncSource : public PortLifetime {
public:
  virtual const A& input() const noexcept=0;
  virtual WorkContext& work() noexcept=0;
  virtual Result<void> complete(Result<R>) noexcept=0;
};
class AsyncDispatchPort : public PortLifetime {
public:
  virtual Result<void> start()=0;
};
struct HotEntry final {
  using NativeThunk = void (*)(const HotEntry &, const void *, WorkContext &,
                               void *);
  using AsyncFactory=Result<std::shared_ptr<AsyncDispatchPort>> (*)(const HotEntry&,std::shared_ptr<PortLifetime>);
  Shape shape;
  std::shared_ptr<const void> handler;
  CppTypeToken handler_type;
  std::vector<std::shared_ptr<const void>> owners;
  bool inline_safe, async_dispatch, external_wait;
  NativeThunk native = nullptr;
  std::shared_ptr<const void> reader_owner;
  CppTypeToken reader_type = CppTypeToken::of<void>();
  // 保留注册时的可信声明，受管理调用不得从客户端重建资源身份。
  std::vector<ResourceRef> resources;
  std::shared_ptr<const void> submission_storage;
  CppTypeToken submission_storage_type = CppTypeToken::of<void>();
  AsyncFactory async_factory=nullptr;
};

template<AsyncInput A,ContractResult R,class Reader>
  requires (std::same_as<R,void> || AsyncInput<R>)
Result<std::shared_ptr<AsyncDispatchPort>> read_async(const HotEntry& entry,std::shared_ptr<PortLifetime> source) {
  using Function=Result<void> (*)(std::shared_ptr<AsyncReadCall<A,R,Reader>>);
  class Call final : public AsyncReadCall<A,R,Reader> {
  public:
    Call(std::shared_ptr<AsyncSource<A,R>> source,std::shared_ptr<const Reader> reader)
        :source_(std::move(source)),reader_(std::move(reader)) {}
    ~Call() override {
      ExecutionCallbackFrame frame;
      reader_.reset();source_.reset();
    }
    const A& input() const noexcept override {return source_->input();}
    WorkContext& work() noexcept override {return source_->work();}
    const Reader& reader() const noexcept override {return *reader_;}
    Result<void> complete(Result<R> result) noexcept override {return source_->complete(std::move(result));}
  private:
    std::shared_ptr<AsyncSource<A,R>> source_; // Reader 析构后才释放真实 callback owner。
    std::shared_ptr<const Reader> reader_;
  };
  class Dispatch final : public AsyncDispatchPort {
  public:
    Dispatch(std::shared_ptr<const Function> function,std::shared_ptr<AsyncReadCall<A,R,Reader>> call)
        :function_(std::move(function)),call_(std::move(call)) {}
    Result<void> start() override {
      ExecutionCallbackFrame frame;
      if(!call_)return make_unexpected(error(ContractsErrc::DuplicateCompletion));
      return (*function_)(std::move(call_));
    }
  private:
    std::shared_ptr<const Function> function_;
    std::shared_ptr<AsyncReadCall<A,R,Reader>> call_;
  };
  auto typed=std::dynamic_pointer_cast<AsyncSource<A,R>>(source);
  if(!typed||entry.handler_type!=CppTypeToken::of<Function>()||entry.reader_type!=CppTypeToken::of<Reader>())
    return make_unexpected(error(ContractsErrc::TypeMismatch));
  auto call=std::make_shared<Call>(std::move(typed),std::static_pointer_cast<const Reader>(entry.reader_owner));
  return std::shared_ptr<AsyncDispatchPort>(std::make_shared<Dispatch>(
      std::static_pointer_cast<const Function>(entry.handler),std::move(call)));
}

template <ContractValue A, ContractResult R>
void compute_native(const HotEntry &entry, const void *args, WorkContext &work,
                    void *result) {
  using Function = Result<R> (*)(const A &, WorkContext &);
  auto fn = std::static_pointer_cast<const Function>(entry.handler);
  auto slot = static_cast<std::optional<Result<R>> *>(result);
  slot->emplace((*fn)(*static_cast<const A *>(args), work));
}

template <ContractValue A, ContractResult R, class Reader>
void read_native(const HotEntry &entry, const void *args, WorkContext &work,
                 void *result) {
  using Function = Result<R> (*)(const A &, WorkContext &, ReadServices<Reader> &);
  auto fn = std::static_pointer_cast<const Function>(entry.handler);
  auto reader = std::static_pointer_cast<const Reader>(entry.reader_owner);
  ReadServices<Reader> services(std::move(reader));
  auto slot = static_cast<std::optional<Result<R>> *>(result);
  slot->emplace((*fn)(*static_cast<const A *>(args), work, services));
}

template <ContractValue A, ContractResult R, class Reader, class Provider>
void candidate_read_native(const HotEntry &entry, const void *args,
                           WorkContext &work, void *result) {
  using Read = Result<R> (*)(const A &, WorkContext &, ReadServices<Reader> &);
  using Candidate = Result<R> (*)(const A &, const typename Provider::CandidateReadPort &,
                                  WorkContext &);
  using Function = std::pair<Read, Candidate>;
  auto fn = std::static_pointer_cast<const Function>(entry.handler);
  auto reader = std::static_pointer_cast<const Reader>(entry.reader_owner);
  ReadServices<Reader> services(std::move(reader));
  auto slot = static_cast<std::optional<Result<R>> *>(result);
  slot->emplace(fn->first(*static_cast<const A *>(args), work, services));
}
} // namespace detail
class Catalog final : public BindingPort {
  friend class RegistrationBatch;
  friend class invocation::NativeAccess;
  Catalog(RegistryId id, std::vector<detail::HotEntry> h,
          std::vector<std::shared_ptr<const DefinitionSnapshot>> c,
          std::vector<std::shared_ptr<const void>> owners, std::vector<Name> module_order);
  RegistryId id_;
  RegistryGeneration generation_{};
  std::vector<detail::HotEntry> hot_;
  std::vector<std::shared_ptr<const DefinitionSnapshot>> cold_;
  std::vector<std::shared_ptr<const void>> owners_;
  std::vector<Name> module_order_;

public:
  Catalog(const Catalog &) = delete;
  Catalog &operator=(const Catalog &) = delete;
  RegistryId identity() const noexcept override { return id_; }
  RegistryGeneration generation() const noexcept override {
    return generation_;
  }
  std::size_t size() const noexcept override { return hot_.size(); }
  std::span<const Name> module_order() const noexcept { return module_order_; }
  Result<OperationHandle> find(const OperationKey &) const override;
  Result<std::shared_ptr<const DefinitionSnapshot>>
  describe(std::uint32_t) const override;
};
class RegistrationBatch final {
  friend class Registrar;
  enum class State { Collecting, Validating, Published, Failed };
  State state_ = State::Collecting;
  bool failed_ = false, truncated_ = false, publishing_ = false;
  BatchBudget budget_;
  std::vector<ModuleInput> modules_;
  std::vector<RegistrationError> errors_;
  std::vector<detail::HotEntry> hot_;
  std::vector<std::shared_ptr<const DefinitionSnapshot>> cold_;
  std::size_t texts_ = 0, declarations_ = 0, operations_ = 0;
  explicit RegistrationBatch(BatchBudget b) : budget_(b) {}
  Result<void> fail(RegistryErrc, const Name * = nullptr,
                    const OperationKey * = nullptr) noexcept;
  Result<void> insert(std::size_t, std::shared_ptr<const DefinitionSnapshot>,
                      const OperationOptions &, std::shared_ptr<const void>,
                      CppTypeToken, detail::HotEntry::NativeThunk,
                      std::shared_ptr<const void>, CppTypeToken,detail::HotEntry::AsyncFactory);
  Result<void> preflight(std::size_t, const DefinitionInput &,
                         const OperationOptions &);
  Result<std::shared_ptr<void>> service(std::size_t, const ServiceRef &,
                                        CppTypeToken);
  bool validate_module(std::size_t);
  std::optional<std::size_t> module_index(const Name &) const;
  bool allowed(std::size_t, const Name &) const;
  void release_candidates() noexcept;

public:
  RegistrationBatch(const RegistrationBatch &) = delete;
  RegistrationBatch &operator=(const RegistrationBatch &) = delete;
  static Result<std::unique_ptr<RegistrationBatch>> create(BatchBudget);
  Result<void> add(const ModuleInput &);
  Result<std::shared_ptr<const Catalog>> publish();
  std::span<const RegistrationError> errors() const noexcept { return errors_; }
  bool failed() const noexcept { return failed_; }
  bool diagnostics_truncated() const noexcept { return truncated_; }
};
class Registrar final {
  friend class RegistrationBatch;
  RegistrationBatch &batch_;
  std::size_t module_;
  Registrar(RegistrationBatch &b, std::size_t m) : batch_(b), module_(m) {}
  template <class F, class Factory>
  Result<void> accept(F fn, Factory factory, const DefinitionInput &input,
                      const OperationOptions &o,
                      detail::HotEntry::NativeThunk native,
                      std::shared_ptr<const void> storage = {},
                      CppTypeToken storage_type = CppTypeToken::of<void>(),
                      detail::HotEntry::AsyncFactory async_factory=nullptr) {
    try {
      if (batch_.state_ != RegistrationBatch::State::Validating ||
          batch_.failed_)
        return batch_.fail(RegistryErrc::Frozen);
      auto budget = batch_.preflight(module_, input, o);
      if (!budget)
        return budget;
      auto d = factory();
      if (!d)
        return batch_.fail(RegistryErrc::InvalidDefinition,
                           &batch_.modules_[module_].manifest.name);
      return batch_.insert(module_, d->snapshot(), o,
                           std::make_shared<const F>(fn),
                           CppTypeToken::of<F>(), native, std::move(storage), storage_type,async_factory);
    } catch (...) {
      batch_.fail(RegistryErrc::CallbackException);
      throw;
    }
  }

public:
  Registrar(const Registrar &) = delete;
  Registrar &operator=(const Registrar &) = delete;
  template<AsyncInput A,ContractResult R,class Reader>
    requires (std::same_as<R,void> || AsyncInput<R>)
  Result<void> read_async(Result<void> (*f)(std::shared_ptr<AsyncReadCall<A,R,Reader>>),
      const DefinitionInput& d,const OperationOptions& o,SubmissionStorage<A,R> storage) {
    if(!storage.input_limit||!storage.reply_limit||!storage.input_bytes||!storage.reply_bytes)
      return batch_.fail(RegistryErrc::InvalidDefinition);
    return accept(f,[&]{return OperationDefinition<A,R>::template read_async<Reader>(f,d);},d,o,nullptr,
        std::make_shared<const SubmissionStorage<A,R>>(storage),CppTypeToken::of<SubmissionStorage<A,R>>(),
        &detail::read_async<A,R,Reader>);
  }
  template <AsyncInput A, ContractResult R, class Reader>
    requires (std::same_as<R,void> || AsyncInput<R>)
  Result<void> read(Result<R> (*f)(const A&, WorkContext&, ReadServices<Reader>&),
                    const DefinitionInput& d, const OperationOptions& o,
                    SubmissionStorage<A,R> storage) {
    if (!storage.input_limit || !storage.reply_limit || !storage.input_bytes || !storage.reply_bytes)
      return batch_.fail(RegistryErrc::InvalidDefinition);
    return accept(f, [&] { return make_read_definition(f,d); }, d, o,
                  &detail::read_native<A,R,Reader>,
                  std::make_shared<const SubmissionStorage<A,R>>(storage),
                  CppTypeToken::of<SubmissionStorage<A,R>>());
  }
  template <AsyncInput A, ContractResult R>
    requires (std::same_as<R,void> || AsyncInput<R>)
  Result<void> compute(Result<R> (*f)(const A&, WorkContext&),
                       const DefinitionInput& d, const OperationOptions& o,
                       SubmissionStorage<A,R> storage) {
    if (!storage.input_limit || !storage.reply_limit || !storage.input_bytes || !storage.reply_bytes)
      return batch_.fail(RegistryErrc::InvalidDefinition);
    return accept(f, [&] { return make_compute_definition(f,d); }, d, o,
                  &detail::compute_native<A,R>,
                  std::make_shared<const SubmissionStorage<A,R>>(storage),
                  CppTypeToken::of<SubmissionStorage<A,R>>());
  }
  template <ContractValue A, ContractResult R, class Reader>
  Result<void> read(Result<R> (*f)(const A &, WorkContext &,
                                   ReadServices<Reader> &),
                    const DefinitionInput &d, const OperationOptions &o) {
    return accept(f, [&] { return make_read_definition(f, d); }, d, o,
                  &detail::read_native<A, R, Reader>);
  }
  template <ContractValue A, ContractResult R>
  Result<void> compute(Result<R> (*f)(const A &, WorkContext &),
                       const DefinitionInput &d, const OperationOptions &o) {
    return accept(f, [&] { return make_compute_definition(f, d); }, d, o,
                  &detail::compute_native<A, R>);
  }
  template <ContractValue A, ContractResult R, class P>
  Result<void> state_edit(Result<R> (*f)(const A &, EditView<P> &,
                                         WorkContext &),
                          const DefinitionInput &d, const OperationOptions &o) {
    return accept(f, [&] { return make_state_edit_definition(f, d); }, d, o,
                  nullptr);
  }
  template <ContractValue A, ContractResult R>
  Result<void> external_effect(EffectReport<R> (*f)(const A &, EffectContext &),
                               const DefinitionInput &d,
                               const OperationOptions &o) {
    return accept(f, [&] { return make_effect_definition(f, d); }, d, o,
                  nullptr);
  }
  template <ContractValue A, ContractResult R>
  Result<void> lifecycle(TransitionReport<R> (*f)(const A &, TransitionView &),
                         const DefinitionInput &d, const OperationOptions &o) {
    return accept(f, [&] { return make_lifecycle_definition(f, d); }, d, o,
                  nullptr);
  }
  template <ContractValue A, ContractResult R, class Reader, class P>
  Result<void> candidate_read(
      Result<R> (*f)(const A &, WorkContext &, ReadServices<Reader> &),
      Result<R> (*c)(const A &, const typename P::CandidateReadPort &,
                     WorkContext &),
      const DefinitionInput &d, const OperationOptions &o) {
    return accept(
        std::pair{f, c},
        [&]() -> Result<OperationDefinition<A, R>> {
          auto r = make_read_definition(f, d);
          if (!r)
            return make_unexpected(r.error());
          return with_candidate_read<P>(*r, c, ProviderContract<P>::key());
        },
        d, o, &detail::candidate_read_native<A, R, Reader, P>);
  }
  template <class T>
  Result<std::shared_ptr<T>> service(const Name &m, const Name &n) {
    try {
      auto s = batch_.service(module_, {m, n}, CppTypeToken::of<T>());
      if (!s)
        return make_unexpected(s.error());
      return std::static_pointer_cast<T>(*s);
    } catch (...) {
      batch_.fail(RegistryErrc::CallbackException);
      throw;
    }
  }
};
} // namespace ock::runtime::registry

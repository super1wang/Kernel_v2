#pragma once
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <ock/runtime/registry.hpp>
#include <ock/runtime/policy.hpp>
#include <ock/runtime/native_types.hpp>
#include <ock/runtime/logging.hpp>

namespace ock::runtime::executions::detail { class InvocationRecordBase; }
namespace ock::runtime::host {
using namespace contracts;
using TimePoint = std::chrono::steady_clock::time_point;
namespace detail { struct HostControl; struct SessionState; template<class A,class R> struct HostedBinding; }
struct ModuleContext {
  observability::SafeLogger& log;
  Name module;
};
struct ModuleStopResult {
  bool quiescent;
  std::optional<Error> error;
};
class ModuleLifecyclePort : public PortLifetime {
public:
  virtual Result<void> start(const ModuleContext&) = 0;
  virtual ModuleStopResult stop() = 0;
};
struct HostModule {
  registry::ModuleInput registration;
  std::shared_ptr<ModuleLifecyclePort> lifecycle;
};
struct HostBudget {
  std::size_t active_admissions = 128;
  std::size_t cleanup_errors = 64;
  std::chrono::milliseconds failed_start_cleanup{1000};
};
struct HostOptions {
  registry::BatchBudget registration;
  policy::PolicyBudget policy;
  invocation::NativeBudget native;
  HostBudget host;
  contracts::LogLimits logging{128, contracts::LogOverflow::DropOldest,
                              contracts::LogLevel::Info};
};
enum class HostPhase : std::uint8_t {
  Constructed, Configuring, Validating, Recovering, Starting, Ready,
  StopAccepting, CancelOrFinish, Finalize, DrainExecutors,
  StopModulesObservers, ReleaseStorage, Stopped, Failed
};
enum class HostErrc : std::uint32_t {
  InvalidInput=1, InvalidOwner, BudgetExceeded, NotReady, HostDraining,
  Busy, AlreadyStarted, UnsupportedCapability, CallbackException,
  ReentrantShutdown, DeadlineExceeded, NotQuiescent, InvalidSession,
  IdentityUnavailable, LoggingUnavailable
};
inline constexpr foundation::ErrorDomain host_domain{"ock.host"};
inline Error host_error(HostErrc code) noexcept {
  return Error{foundation::ErrorCode::make<host_domain>(static_cast<std::uint32_t>(code))};
}
struct CleanupError {
  HostPhase phase;
  std::optional<Name> module;
  foundation::ErrorCode code;
};
struct HostCapabilities {
  bool native_read = true;
  bool native_compute = true;
  bool async_execution = false;
  bool execution_observation = false;
  bool state = false;
  bool storage = false;
  bool restore = false;
  bool ordinary_memory_logging = true;
};
struct HostSnapshot {
  HostPhase phase;
  bool quiescent;
  std::size_t active_admissions;
  std::size_t started_modules;
  std::size_t pending_modules;
  std::optional<foundation::ErrorCode> primary_error;
  std::uint64_t cleanup_error_count;
  std::size_t cleanup_written;
  bool cleanup_truncated;
  bool cleanup_count_saturated;
};
enum class ShutdownDisposition : std::uint8_t {
  Complete, CompleteWithErrors, DeadlineExceeded, NotQuiescent,
  Busy, Reentrant
};
enum class PendingKind : std::uint8_t { Admissions, PolicyStore, Module, Logging, Executions, Executors };
struct PendingCleanup {
  PendingKind kind;
  std::optional<Name> module;
  std::size_t active_admissions;
};
struct ShutdownReport {
  ShutdownDisposition disposition;
  HostPhase phase;
  bool quiescent;
  bool deadline_exceeded;
  std::size_t active_admissions;
  std::size_t pending_modules;
  std::optional<foundation::ErrorCode> primary_error;
  std::uint64_t cleanup_error_count;
  std::size_t pending_total;
  std::size_t pending_written;
  bool pending_truncated;
};
class HostLogFactoryPort : public PortLifetime {
public:
  virtual Result<std::shared_ptr<contracts::LogPort>> create(
      HostIncarnation, const contracts::LogLimits&) = 0;
};
// 可信组合根提供执行后端；工厂返回前的失败清理由工厂负责。
// Host 先关闭执行准入，再排空执行及物理投递，最后停止模块。
enum class ExecutionWaitState : std::uint8_t { Terminal, Timeout, Cancelled };
struct ExecutionWaitReply {
  ExecutionWaitState state;
  policy::AuthorizedSummary observed;
};
template<ContractResult R> struct ExecutionResult {
  std::shared_ptr<const InvokeReply<R>> value;
  std::shared_ptr<const policy::ResponseAuthorization> response;
};
struct ErasedExecutionResult {
  std::shared_ptr<const void> value;
  std::shared_ptr<const policy::ResponseAuthorization> response;
};
class HostExecutionPort : public PortLifetime {
public:
  virtual SubmitReply submit(std::shared_ptr<executions::detail::InvocationRecordBase>) {
    return Rejected{host_error(HostErrc::UnsupportedCapability)};
  }
  virtual Result<ErasedExecutionResult> result(const policy::VerifiedCaller&,
      std::shared_ptr<policy::SessionAuthority>, ExecutionRef, CppTypeToken) {
    return make_unexpected(host_error(HostErrc::UnsupportedCapability));
  }
  virtual Result<ExecutionWaitReply> wait(const policy::VerifiedCaller&,
      std::shared_ptr<policy::SessionAuthority>, std::shared_ptr<invocation::TrustedThreadPort>,
      ExecutionRef, TimePoint, std::stop_token) {
    return make_unexpected(host_error(HostErrc::UnsupportedCapability));
  }
  virtual Result<CancelDisposition> cancel(const policy::VerifiedCaller&,
      std::shared_ptr<policy::SessionAuthority>, ExecutionRef) {
    return make_unexpected(host_error(HostErrc::UnsupportedCapability));
  }
  virtual std::shared_ptr<policy::ExecutionAccessSourcePort> observations() const = 0;
  virtual bool in_execution_thread() const noexcept = 0;
  virtual void stop_accepting() = 0;
  virtual Result<bool> finish_until(TimePoint) = 0;
  virtual Result<bool> drain_executors_until(TimePoint) = 0;
};
class HostExecutionFactoryPort : public PortLifetime {
public:
  virtual Result<std::shared_ptr<HostExecutionPort>> create(HostIncarnation) = 0;
};
struct HostPorts {
  std::shared_ptr<policy::TrustedAuthenticationPort> authentication;
  std::shared_ptr<policy::ClockPort> clock;
  std::shared_ptr<policy::TrustedGroupDigestPort> group_digest;
  std::shared_ptr<invocation::TrustedThreadPort> threads;
  std::shared_ptr<HostLogFactoryPort> logging_factory;
  std::shared_ptr<HostExecutionFactoryPort> execution_factory;
};
class HostSession;
template<ContractValue A, ContractResult R> class HostBound;
class NativeHost final {
public:
  static Result<std::unique_ptr<NativeHost>> create(
      const HostOptions&, const policy::PolicyConfiguration&, const HostPorts&);
  Result<void> add(const HostModule&);
  Result<void> start();
  Result<HostSession> open(const policy::AuthenticationAttempt&,
                           const policy::DelegationInput&);
  ShutdownReport shutdown_until(TimePoint deadline,
                                std::span<PendingCleanup> pending = {});
  HostSnapshot snapshot(std::span<CleanupError> out) const noexcept;
  HostCapabilities capabilities() const noexcept;
  HostIncarnation incarnation() const noexcept;
  Result<observability::LogReadPage> copy_logs(
      contracts::LogPosition after, std::span<contracts::PublicLogRecord> out);
  ~NativeHost();
  NativeHost(const NativeHost&) = delete;
  NativeHost& operator=(const NativeHost&) = delete;
  NativeHost(NativeHost&&) = delete;
  NativeHost& operator=(NativeHost&&) = delete;
private:
  explicit NativeHost(std::shared_ptr<detail::HostControl>) noexcept;
  std::shared_ptr<detail::HostControl> state_;
};
class HostSession final {
public:
  struct CatalogContext {
    std::shared_ptr<const contracts::BindingPort> definitions;
    std::shared_ptr<const policy::SessionAuthority> authorization;
  };
  Result<CatalogContext> catalog_context() const;
  HostSession(HostSession&&) noexcept;
  HostSession& operator=(HostSession&&) noexcept;
  HostSession(const HostSession&) = delete;
  HostSession& operator=(const HostSession&) = delete;
  ~HostSession();
  Result<std::shared_ptr<const policy::VerifiedCaller>> verify(
      const CallerDescription&);
  template<ContractValue A, ContractResult R>
  Result<HostBound<A,R>> bind(const OperationKey&, ContractDigest, Shape,
      std::shared_ptr<const policy::VerifiedCaller>,
      std::span<const foundation::ObjectId>, invocation::TargetProjection<A>, Name);
  Result<void> restrict_delegation(const policy::DelegationInput&);
  template<ContractResult R>
  Result<ExecutionResult<R>> result(const policy::VerifiedCaller&, ExecutionRef) const;
  Result<ExecutionWaitReply> wait(const policy::VerifiedCaller&, ExecutionRef,
      TimePoint, std::stop_token = {}) const;
  Result<CancelDisposition> cancel(const policy::VerifiedCaller&, ExecutionRef) const;
  Result<void> close();
private:
  friend class NativeHost;
  Result<ErasedExecutionResult> result_erased(const policy::VerifiedCaller&,
      ExecutionRef, CppTypeToken) const;
  explicit HostSession(std::shared_ptr<detail::SessionState>) noexcept;
  std::shared_ptr<detail::SessionState> state_;
};
template<ContractValue A, ContractResult R> class HostBound final {
public:
  HostBound(HostBound&&) noexcept;
  HostBound& operator=(HostBound&&) noexcept;
  HostBound(const HostBound&) = delete;
  HostBound& operator=(const HostBound&) = delete;
  ~HostBound();
  InvokeReply<R> invoke(const A&, const invocation::InvokeOptions&) const;
  SubmitReply submit(A, const invocation::InvokeOptions&) const
    requires (AsyncInput<A> && (std::same_as<R,void> || AsyncInput<R>));
private:
  friend class HostSession;
  explicit HostBound(std::shared_ptr<detail::HostedBinding<A,R>>) noexcept;
  std::shared_ptr<detail::HostedBinding<A,R>> state_;
};
}
#include <ock/runtime/detail/host.hpp>

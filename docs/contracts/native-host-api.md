# D1.06 NativeHost 具体合同

状态：**Current Contract / Implemented**；D1.06/B1 已 Passed，最新影响修复与状态见 progress。依据 v3.3 A16、A15、A19、A21、A23 和 D1.06。旧候选与 ChangesRequested 原文保留，不用旧审核状态阻塞后续开发。日志数据见 `logging-api.md`，安装表面见 `native-sdk-surface.md`。

## 范围和类型位置

同一 Registry/Policy/Invocation 生产实现组成 NativeSubset。只执行无资源声明的同步 Read/Compute，保留完整参数、身份、目标、权限、线程、预算和 Outcome 检查。不创建 Task、Document、执行索引、恢复存储或第二个业务分派器。NativeSubset 不提供 submit、wait、RPC 或业务模块。

B5 开发扩展：可信组合根可通过 `HostPorts::execution_factory` 显式装配执行后端。工厂只在 start 时调用，使用本 HostIncarnation 创建观察源；默认空工厂保持以上 NativeSubset 行为。Host 在验证观察源之前登记后端清理责任，失败也必须排空；关闭顺序为停止执行准入、等待已准入调用、结束执行、关闭 Policy、排空执行器、停止模块。超时保留后端及其依赖，`PendingKind::Executions` / `Executors` 分别报告未完成责任；所属执行线程关闭返回 Reentrant。工厂返回失败前的临时 owner 清理由工厂负责，所有后端回调必须遵守传入截止时间。已接通 `HostBound::submit` 与 `HostSession::wait/result/cancel`，已覆盖拥有型同步 Read/Compute、结构化 child 与显式异步 Read 完成路径。可信工厂可调用公开 `make_executions` 创建真实执行后端，Control/CLI、必要记录器、完整预算与 G3 尚未闭合，capabilities 不宣称完整阶段验收。

公开头 `ock/runtime/host.hpp` 的名字位于 `ock::runtime::host`。以下 `Name`、`Error`、`Result`、`OperationKey`、`ContractDigest`、`Shape`、`CallerDescription`、`InvokeReply` 及概念沿用 CoreContracts；`TimePoint` 为 `std::chrono::steady_clock::time_point`。公开 `ock/runtime/native_types.hpp` 保存 D1.05 的 ThreadRole、ThreadObservation、TrustedThreadPort、NativeBudget、InvokeOptions、TargetProjection、InvocationErrc/Record/Snapshot 唯一定义，仍在原 `ock::runtime::invocation` 命名空间。模板内部使用安装的 detail，不要求应用直接包含 detail。


B5 提交存储由可信 Registrar 的四参数 `read/compute` 重载声明 `registry::SubmissionStorage<A,R>`：包含 input_limit、reply_limit 及输入/InvokeReply 的完整字节计量函数。额度与函数由目录独立拥有并带真实 C++ 类型见证；未声明的三参数注册保持 Invoke，但 Submit 拒绝。A 必须为 AsyncInput，R 必须为 void 或 AsyncInput。客户端 Submit 只接收拥有参数与 InvokeOptions，不能替换存储政策、资源声明或资源解析器。typed 记录创建入口为 private，HostBound 与 Runtime 内部适配器具有创建权；执行服务通过同一记录的窄接口复用原 typed dispatch。

`HostSession::result<R>` 在结果 pin 前后执行当前 ReadResult 授权，并核对真实 R 类型，返回拥有型 InvokeReply 与 ResponseAuthorization；传输适配仍需消费发送许可。`wait` 仅允许可信线程观测为 Application 的调用，在当前 Wait 授权期限与请求期限的较早者内等待，返回前重验授权。Timeout/Cancelled 只结束等待，不能声称执行已取消。Host 关闭后新的会话查询被拒绝，已取得的结果 owner 继续保活。

`HostSession::cancel` 使用当前 CancelExecution 授权，返回 CoreContracts 的 `CancelDisposition`：Requested 表示取消赢得尚未开始的退役仲裁，AlreadyClaimed 表示 start 已赢且只登记协作意图，AlreadyTerminal 表示权威执行已结束。分类来自同一 Scheduler start/retire 仲裁，不根据先读摘要再操作猜测；三种结果均不改写既有执行事实，不保证物理投递已排空。原始 InvokeOptions.stop 在接受后由执行 owner 保持注册，可在排队或等待资源时直接退役，回调仅弱引用 owner；运行期取消不提前释放业务 Lease。

`make_executions` 只依赖 CoreContracts 的 ExecutorControlPort，由可信组合根提供实际 executor。ExecutionOptions 冻结执行表/控制槽预算、主体调度和资源映射；ResourceRef 的目录 owner 只证明注册寿命，运行期 Lease 必须由后端唯一 ResourceManager 按冻结 claims 取得。未知键、重复 ResourceRef、超限或不合法 claims 在创建时拒绝；未映射的操作声明在接受前拒绝。无资源配置不创建 ResourceManager 或占位 slot。创建成功后 Host 排空执行及 executor，创建失败时工厂保留临时 executor 的排空责任。显式后端允许同步 Read/Compute 的非 inline 声明；异步 Read 必须使用下述拥有型注册重载；其他未支持 shape 仍拒绝，短 Invoke 的资源和线程检查保持。

`HostBound::submit_child(work,args,options)` 复用同一提交链路。只有受管理调用的 WorkContext 带有真实 ExecutionScopePort；Runtime 核对真实 owner、同一服务/SessionAuthority/主体、当前父授权及未关闭的创建阶段。child 具有独立 ExecutionRef，Summary.parent 在接受前固定，沿用当前会话委托范围，期限收紧至父 WorkContext 的期限。伪造、跨服务、跨会话、同步父已返回、异步父已提交 candidate、父已取消或超出有限 child/深度预算均拒绝；不从客户端 ExecutionRef 查找并授予父权限。

同步父调用返回即释放本次运行期 Lease 和 worker 配额，有未结束 child 时进入 WaitingChild；child 终态以可靠 pending 唤醒同一控制循环，所有 child 收尾后父才能 Finalizing/Terminal。父取消默认传给 child；父 body 已完成但仍在 WaitingChild 时取消返回 AlreadyClaimed，不能把 Scheduler body 完成冒充父执行 Terminal。父槽弱持有 child，child 强持有父至自身终态并解除关联。`children_per_execution` 默认 64（允许 1..256）、`max_depth` 默认 32（允许 1..64，顶层为 1）；不支持 detached child 或任意接管。异步 Read 的特殊收尾规则见下。

## 模块与预算

B5 异步 Read 的可信注册使用 `Registrar::read_async`，函数类型为 `Result<void> (*)(std::shared_ptr<AsyncReadCall<A,R,Reader>>)`，同样必须声明 SubmissionStorage。DefinitionSnapshot 的 `asynchronous_read()` 从实际签名生成，不能由客户端 DTO 写入；AtomicMode 必须为 Incompatible，inline_safe=false、requires_external_wait=true，注册 executor 须具备所声明能力。一个 Operation 只有一个 handler，Invoke 返回 SubmitRequired；Submit 沿原 Catalog/Policy 校验和同一 ExecutionRef 路径执行。

AsyncReadCall 提供 `input()`、`work()`、`reader()`、`complete(Result<R>)`；WorkContext 的只读 `stop_token()` 可订阅协作取消，不能发出取消请求。保存 shared_ptr 即保留这些访问的 owner；只能并发调用 complete，其余可变 WorkContext 访问由业务串行使用。接受前分配回调 owner 和 typed adapter；开始前重验授权/线程/目标，Callback 期间保持 Policy admission、调用槽、拥有输入及 Resource Lease。有效期限在接受准备时收紧到当前 Policy admission，WorkContext 在开始时再次采用当前较早期限。

complete 以原子 claim 接受第一个结果，重复返回 DuplicateCompletion；结果先到时可发布 Finalizing，但最后一个 callback owner 释放、输入/服务/context/Lease/admission 实际收尾后才允许 Terminal。遗漏 complete、启动失败/抛出均产生同一身份的失败事实；先到的完成不被迟到异常或取消覆盖。异步父创建 child 时复用 Lease::before_child_wait，含别名的冲突资源组合在 child 接受前拒绝，父槽回收；不释放仍供 callback 使用的 Lease。关闭与客户端丢通知不能跳过该排空责任。Runtime 标记异步完成、结果处理和 owner 析构回调的线程作用域；其中阻塞 wait 返回 ThreadRejected，Host 关闭返回 Reentrant，避免非 worker 回调等待自身排空。

```cpp
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
```

ModuleContext 仅同步借用；不得保存引用、启动延迟任务或从中获取整个 Host。模块已绑定的服务由模块自己的 owner 持有。生命周期端口允许抛出，Host 在锁外捕获。失败的 start 无论返回错误还是抛出，都必须在退出前通过自身局部 RAII 撤销未成功的资源；Host 只 stop 已返回成功的模块。测试检查真实析构哨兵，不能仅检查 mock 调用顺序。

stop 必须短且不等待；`quiescent=false` 或异常说明本步骤尚未安全排空，当前模块及其依赖继续保留。`quiescent=true,error!=nullopt` 说明已安全排空但清理有错误，Host 记录后继续逆序。Host 无法抢占恶意或违约的同步回调，不宣称硬实时超时。

`active_admissions` 有限范围 1..4096；`cleanup_errors` 1..4096；失败启动的自动清理预算 1..60000 毫秒。Registry/Policy/Native 预算沿原验证规则，所有实际数组/对象分配前 checked_mul/checked_add，禁止零或无限容量哨兵。配置上限不是 footprint 已批准资源预算；后者必须通过真实 pilot 独立审批。构造期预分配清理记录，稳态不扩容。

## 状态和固定报告

```cpp
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
Error host_error(HostErrc) noexcept;
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
```

报告不复制任意 ErrorInfo/字符串。`primary_error` 是首次启动/配置失败的 code，不被日志错误或后续清理覆盖。底层明确错误直接保留域/code；Host 自身失败用 ock.host。可传播至Host异常边界的bad_alloc映射BudgetExceeded，其他可传播回调异常映射CallbackException；平台库noexcept内不可恢复OOM按A03单列终止诊断，不冒充业务错误或清理成功；不记录 what()。清理错误每次实际失败最多追加一次，固定槽满后只饱和计数并置截断。`snapshot(out)` 拷贝最早 `min(out.size,stored_count)` 项，输出为空合法；不清空或转移记录。

`started_modules` 是历史成功启动总数；`pending_modules` 是尚未 quiescent 的成功步骤数量。Constructed/Configuring 在没有在途生命周期操作时 quiescent=true，Ready 时 false；Failed 只有全部已成功步骤与内部必要资源清理完毕才 true。存在外部旧绑定持有的不可变控制块并不等于有在途业务；保留字节在测量中另列。

## 入口与会话

```cpp
class HostLogFactoryPort : public PortLifetime {
public:
  virtual Result<std::shared_ptr<contracts::LogPort>> create(
      HostIncarnation, const contracts::LogLimits&) = 0;
};
struct ExecutionLimits {
  std::size_t records=4096, input_bytes=64*1024*1024, reply_bytes=64*1024*1024;
  std::size_t terminal_records=10000, terminal_bytes=64*1024*1024;
  std::uint32_t page_size=200,scan_limit=2000;
  std::size_t targets_per_record=64;
  std::size_t waiters=256;
};
struct ExecutionResource {
  registry::ResourceRef ref;
  std::vector<resources::Claim> claims;
};
struct ExecutionOptions {
  ExecutionLimits limits;
  std::size_t active=256,control_batch=64;
  std::size_t children_per_execution=64,max_depth=32;
  scheduler::Options scheduling;
  std::vector<scheduler::Subject> subjects;
  resources::Options resource_options;
  std::vector<resources::Slot> slots;
  std::vector<resources::Alias> aliases;
  std::vector<ExecutionResource> resources;
};
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
  virtual SubmitReply submit_child(std::shared_ptr<executions::detail::InvocationRecordBase>,
      std::shared_ptr<ExecutionScopePort>) {
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
// 可信工厂在 start 内调用。成功后 Host 持有并排空 executor；失败时调用方仍负责其排空。
// 配置被独立冻结；客户端 Submit 无法替换资源映射或预算。
Result<std::shared_ptr<HostExecutionPort>> make_executions(HostIncarnation,
    std::shared_ptr<ExecutorControlPort>, const ExecutionOptions&);
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
```

前四端口必须具有真实共享 owner。logging_factory 空值表示装配真实默认内存日志；非空也必须 owned。禁止非空指针且无控制块的别名 shared_ptr，不以仅 `get()!=nullptr` 验证寿命。factory 是可信宿主装配端口，仅start装配期在锁外调用一次，不能成为运行时 ServiceLocator。

create 内调用 Windows `BCryptGenRandom(nullptr, bytes, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG)`，检查真实 NTSTATUS，失败或全零返回 IdentityUnavailable，不使用固定值/时间戳回退。得到 HostIncarnation 后一次冻结，不允许请求传入或修改。create只拥有配置和端口，日志及Policy的有状态装配延至start；默认调用 `make_memory_logging(host,1,options.logging)`。自定义 factory 返回真实 owned LogPort 后，先登记为内部待清理步骤，再用 SafeLogger 读取 snapshot，验证 host 相同、stream=1、accepted_sequence=0、evicted=0、retained=0，不接受旧流充新宿主。验证失败仍必须关闭实际已返回的端口，关闭失败由Failed Host保留并允许显式shutdown重试；不能因尚无成功模块而释放未排空日志。factory在返回错误/抛出前对自身未发布资源承担局部RAII强保证。随机标识提供极低碰撞概率，不声称数学无碰撞或把标识当凭据。算法参数与链接依据 [Microsoft BCryptGenRandom 文档](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom)。bcrypt 是 Windows 系统链接依赖，在静态导出和真实链接测量中如实登记。

```cpp
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
  SubmitReply submit_child(const WorkContext&, A, const invocation::InvokeOptions&) const
    requires (AsyncInput<A> && (std::same_as<R,void> || AsyncInput<R>));
};
```

Session/Bound 不能公开构造，工厂/绑定只交付完整对象。移动后调用返回 InvalidSession/InvalidBinding。无 NativeEngine、NativeBound、Catalog handler、PolicyAdministration、Admission getter。`restrict_delegation` 仅调用既有 SessionAuthority 收缩规则及截止，不能扩大权限，也不声称支持管理级主体策略替换。

NativeHost 对象本身是可信装配 owner，`copy_logs` 只转发本宿主默认 MemoryDiagnostics 的有界复制；不暴露写端口、内部槽或业务接口。自定义 factory 的管理读取由 factory 自行保存的 owner 完成，此时 Host.copy_logs 返回 UnsupportedCapability。该方法不是外部日志 RPC，不能从业务 CallerDescription/bool admin 换取。Draining/Stopped 仍可读取已保留诊断；方法先复制内部管理 owner，不持 Host 锁调用端口。未装配时返回 LoggingUnavailable。

create/add 参数按 const& 进入，先核对数量/字节预算，再在受控错误边界深拥有配置/注册容器；共享端口按原 owner 规则保留。create 不认证用户、不调用模块。add 只在 Configuring 可用，第一次错误粘性，忽略错误也不能 Ready。Host 不支持重启或修补已失败批次。

HostSession 外壳析构或移动覆盖不隐式 close；已有 HostBound 继续拥有对应 SessionAuthority。显式 close 立即按既有 Policy 撤销该会话；最后一个底层 owner 释放触发原 SessionAuthority 析构 close，名额才回收。Host 停止则在准入归零后 close_store。不得把外壳 RAII 清理误当所有绑定立即撤权。每个外壳方法在触发任何可重入端口前先复制自身 shared state；端口中释放外壳不会造成返回阶段悬空。并发销毁正在被读取的同一个 C++ 外壳仍违反通常对象寿命规则，不作线程安全保证。

## 启动顺序与失败处理

1. create 核对全部 owner、预算和不可变配置拥有，预分配清理槽，进入 Configuring；前置失败不交付 Host。此时没有已打开的日志/PolicyStore，也不调用factory。start中日志装配先于Policy，两者均先于任何模块start，成功资源立即记入内部清理责任，之后出错不能遗漏。
2. start 独占生命周期操作标志，进入 Validating；原 RegistrationBatch 验证全部模块 DAG/版本/声明、回调与冻结目录。新增 `Catalog::module_order() const noexcept -> span<const Name>` 来自 publish 已实际算出的确定性 Kahn 顺序，Host 不重新实现 DAG 排序。
3. 验证冻结操作只允许 Shape::Read 的 Read/Compute，inline_safe=true、async_required=false、external_wait=false；拒绝资源/provider 依赖和不可用执行能力。D1.05 的底层其他 shape 拒绝合同继续保留，不把 Host 不可执行项宣称为已装能力。
4. 在锁外装配日志并验证snapshot身份，再真实创建PolicyStore，冻结可信线程端口。日志返回后，先用无分配shared_ptr保存owner并登记清理责任，再构造SafeLogger；构造失败时Host仍持有后端，管理清理直接调用该尚未交付facade的后端close，显式捕获异常、核对截止、不持Host锁、不递归写日志。清理失败继续保留Failed Host，下次显式shutdown重试；不能用失败后析构未关闭端口替代清理，也不为此新增公共可变setter。内部 ExecutionAccessSource identity 标记 RestoreMode::Absent，find/scan 明确返回能力不可用；不存在成功空任务列表。Host没有提交入口或内部异步执行器；Registry仍拥有真实外部ExecutorBinding，NativeSubset只接受inline_safe且非async/wait的Read/Compute，完整同步路径不调用ExecutorPort::submit，不分配队列、不创建线程。外部端口自身submit语义不等于Host提供的能力。
5. Recovering 明确无恢复 provider，进入 Starting，按目录顺序逐个 start。将每个实际成功项加入预分配清理栈。全部成功且必要端口条件保持后，在短仲裁内发布 Ready。
6. 任一步失败保存首次错误，停止发布业务。使用实际 steady_clock 的 `now+failed_start_cleanup` 作为清理截止，逆序清理已成功项；溢出在 create 拒绝。失败模块本地 RAII 自行清理，不调用其 stop。自动清理未完成则 Failed/quiescent=false，后续显式 shutdown 继续；清理完成仍 Failed/quiescent=true，不能重试 start。

目录在模块启动前是私有、不可达业务的冻结候选，不等于 Host Ready。注册回调本身也只能注册/初始化，不可绕过 Host 接受业务。此阶段名称不承诺尚未实现的恢复/任务/Storage 能力。

## 准入、返回和停止

Ready 检查、准入计数增量和 StopAccepting 迁移使用同一短 mutex 定义先后。有限容量满返回 Busy；Ready 前返回 NotReady，停止开始后返回 HostDraining。open/verify/bind/restrict_delegation 及 invoke 的外部调用部分都持 HostAdmission RAII；任何端口、用户校验、Handler、日志、模块回调均在 Host 锁外。无分配 TLS 链记录各同 Host 准入，嵌套跨 Host 不混淆。

HostBound 拥有 `shared_ptr<HostedBinding<A,R>>`，后者拥有 HostControl 和原 NativeBound。invoke 首先复制这一个 owner，取得 HostAdmission 后直接调用原 NativeBound 完整管线；不提取裸 thunk、不缓存可越过当前权限的成功状态。宿主准入要保留到最终 InvokeReply 返回对象构造完成后才释放，不能先将原绑定借出并提前 decrement。R 继续遵守 D1.05 transport_safe 限制。该固定 shared_ptr/栈 RAII 路径应在完整计数窗口内零新增堆分配。

shutdown 独占生命周期标志；并发 start/shutdown 返回 Busy，不改变主清理步骤。当前线程在同 Host 准入或生命周期回调中调用 shutdown 返回 Reentrant（无副作用），不得等待自己。回调可重入 snapshot；open/bind/invoke 按真实阶段拒绝，start 重入 Busy。独占标志保护长操作，mutex 仅保护状态与固定值，不能跨回调持锁。

shutdown 先 StopAccepting，再 CancelOrFinish 等待已准入的同步调用结束。没有新任务或伪取消；已进入的 Read 返回它的真实结果，不因停止意图改写 Cancelled。condition_variable 使用实际 steady_clock 截止，Policy 测试时钟不能延长该等待。截止在进入等待及每一个同步清理/日志 close 调用的前后核对，过期即返回，不执行下一项；无法抢占当前违约阻塞回调。若最后一步已经安全完成但返回时过期，则 quiescent=true、disposition=Complete/CompleteWithErrors、deadline_exceeded=true，不能假称全部依赖还活着；其他过期为 DeadlineExceeded/quiescent=false。

准入归零后 Finalize 调用 PolicyAdministration::close_store，DrainExecutors 无队列，StopModulesObservers 逆序 stop。每次只处理当前未完成节点：false/throw 返回 NotQuiescent 并保留本项及它依赖的所有剩余模块；后续显式 shutdown 重试；true 即从待停栈移除，错误另存，不重停。模块全部完成后日志 close；Busy/错误返回非 quiescent，下次显式重试，不忙轮询。日志 close 不改变已接受业务事实。全部必要清理完成才 ReleaseStorage（本包 absent）、Stopped。

close_store 或日志失败计为独立清理项；不覆盖主启动错误。Stopped 重复 shutdown 幂等返回完全停止报告。构造后尚未 start 的正常对象可 shutdown 释放已配置 owner；start/add 与 shutdown 不并行持有可变容器。

每份shutdown报告同时给出有界非quiescent清单：按Admissions（有在途时）、PolicyStore（已创建且未关闭）、待停模块逆序、Logging（已取得且未关闭）复制到调用者pending槽。模块条目必须含实际Name，其他项不伪造模块名；Admissions条目附真实在途数量。pending_total表示全部未完成条目数，pending_written为实际复制数，槽不足或空槽显式pending_truncated=true。调用者可按配置预算预留全部槽；不得把空输出或截断解释为排空。清单仅值复制，不返回内部借用指针、不分配，依赖保留覆盖全部未完成项而不只输出子集。Complete且quiescent时三计数为0/0/false。

Host普通日志只记录实际生命周期事件；每次成功invoke继续使用D1.05事实环，不额外写普通日志。日志关闭前最后事件为log_closing，不能提前记录真正Stopped；后者由实际shutdown报告证明。错误保存在Host固定清理记录，不能靠向失败日志递归写入表达。

NativeHost 析构只允许 Stopped，或尚无成功步骤的 Constructed/Configuring，或已清理的 Failed；其他状态调用 foundation::invariant(false)。真实子进程验证 Ready 未 shutdown、持准入时自毁及未排空失败的终止，不将退出码当正常停止。Session/Bound 外壳可在受控回调中释放，因为在途本地 owner 保活；Host 外壳不得在 Handler/生命周期回调中自毁。停止后的旧 Bound 只能拒绝，但可能保留不可变目录/服务 owner；测量分别报告保留绑定和最后绑定释放，不能声明 shutdown 消除了所有外部引用。

## 固定反例职责

测试主名与逐条子断言另写 D1.06 计划/expected，并在首次行为运行前独立冻结。必须覆盖真实成功 Read/Compute 与非法输入 handler=0；每个启动失败窗口/返回及异常 RAII；拓扑逆序和停止重试；回调锁外实际重入；双线程准入先后/超时 owner；会话外壳与显式撤权；完整返回期间 owner；真实析构 fail-fast；三配置有效分配正反控制和 40 次完整 HostBound；安装 C-A 及闭包。仅编译缺实现是启动 red，不冒称某个逻辑反例已检出缺陷。

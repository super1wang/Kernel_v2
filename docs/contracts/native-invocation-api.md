# D1.05 Native Invocation 具体接口草案

状态：**PendingReview，修订 2**。本文件仅为待独立 AI 规格复核的设计，不是已实现接口，不标记 Runtime SDK 可用。唯一上位规范为 `docs/01_Architecture_v3.3.md` A04、A05、A21.5 与 `docs/02_Execution_Plan_v3.3.md` D1.05。D1.03、D1.04 已 Passed；其现有接口变更必须随本增量一起审核，不改写历史报告。

## 1. 范围与入口

实现内部 `packages/runtime/invocation/`，验证消费者 `examples/native_service/` 与 `tests/contract/native/`。普通 Read、PureCompute、candidate_read 的普通只读投影在无资源声明且其他准入通过时实际执行原注册函数。具有资源声明的真实操作允许 bind，但 invoke 在业务前固定拒绝 ResourceUnavailable；注册资源 owner 仅证明 slot 寿命，不能当作本次调用 lease。StateEdit、ExternalEffect、Lifecycle 在本包没有对应运行协调器时明确 `ProviderUnavailable`，不得调用业务后返回空成功。candidate_read 不进入 EditView，不运行 pair 的候选读取函数。

不实现 Submit、Plan、持久接受、TaskId、ExecutionRef、任务表、DOM、线程池、Host 启停、公共 Runtime 安装接口、LoggingConformance 或 footprint 基线。线程不合法或要求异步/外部等待的调用拒绝，不偷偷阻塞或调度。D1.06 的 Host、内存日志共同合同与 NativeSubset 正式占用预算仍留在后续包。

对已识别但未进入业务的失败返回 `Rejected`。业务进入后 Read 错误、异常、无效 R 归入有证明的 `Completed{FailedBeforeApply}`；成功是 `Completed{ReadCompleted<R>}`。本包只读能力不可能产生治理状态提交/设备发送，不能借此路径把已经发生的 Effect/Commit 事实变成普通错误。任何受审扩展的通用结果归一化仍必须保留已知事实及未知边界。

## 2. 现有源码映射及阻碍

- `packages/contracts/include/ock/contracts/operation.hpp`：`BoundOperation<A,R>::bind/revalidate` 已验证精确 key、digest、shape、C++ token、TypeIdentity；不持有可调用 Handler。每次 `revalidate` 调用 `TypeContract::identity()`，其中 OperationVersion 拥有 string；合法长版本可产生每次分配，不能简单把现有 validate_inline_args 当作零分配实现。
- `packages/runtime/registry/registry.hpp`：Catalog 的 hot_/cold_ 私有，HotEntry 有 handler/type/owners，但没有受治理调用桥。Registrar 保存实际函数指针，candidate_read 保存实际 pair。仅有 A/R token 不足以安全恢复任意 Reader/P，必须在注册模板中固化实际 typed thunk。
- `packages/runtime/policy/policy.hpp/.cpp`：verify、resolve、prepare、issue 发行拥有对象，prepare 复制组/目标容器。ActionPermit 一次消费；不得重复使用旧 consumed permit 或批量预发无限许可来伪装零分配。已有 current、allowed、stamp_current、CallerPort validate 提供可复用的真实仲裁逻辑。
- `packages/contracts/include/ock/contracts/context.hpp`：WorkContext 拷贝资源 owner vector 并构建 views vector；需要受寿命约束的借用构造。Name 在 Foundation 中是固定 96 字节数组，不是动态 string。
- `packages/contracts/include/ock/contracts/outcome.hpp`：KnownFacts::create 每次发行会分配，可以绑定时创建共享真实空事实。Outcome::verify 的 unresolved 临时 vector 在 MSVC Debug 还可能分配 iterator proxy；复用空事实本身不解决该临时容器。必须保留全部事实验证并改用不分配的遍历，或把对应构建的分配如实判失败。
- `ExecutorPort` 只有 submit(unique_ptr<ReadyWork>)，没有证明当前线程身份的接口。不能把请求自报 affinity、inline_safe 或 is_worker 布尔当作线程证明。

## 3. Native 对外可用的内部类型

以下类型位于 `ock::runtime::invocation`，只用于内部库与验证消费者。实现头可以拆出私有细节，但不得增加 Handler getter。

```cpp
enum class InvocationErrc : std::uint32_t {
  InvalidBinding = 1, InvalidInput, InvalidOutput, ProviderUnavailable, ResourceUnavailable,
  ThreadRejected, SubmitRequired, Cancelled, Expired, BudgetExceeded,
  Busy, HandlerException, ContractMismatch
};
Error invocation_error(InvocationErrc) noexcept;

enum class ThreadRole { Application, Worker, Control, Domain, Database };
struct ThreadObservation {
  ThreadRole role;
  Name affinity;
  bool inline_allowed;
};
class TrustedThreadPort : public PortLifetime {
public:
  virtual Result<ThreadObservation> current() const noexcept = 0;
};
struct NativeBudget {
  std::size_t bindings = 128;
  std::size_t observation_capacity = 256;
  std::uint64_t observation_counter_limit = (std::numeric_limits<std::uint64_t>::max)();
  std::size_t targets_per_binding = 16;
  std::size_t resources_per_binding = 16;
  std::size_t concurrent_calls_per_binding = 1;
  std::uint64_t work_units = 1024;
};
struct InvokeOptions {
  std::stop_token stop;
  std::chrono::steady_clock::time_point deadline;
  std::uint64_t work_limit;
};
template<class A>
using TargetProjection = Result<std::size_t> (*)(
    const A&, std::span<foundation::ObjectId> output) noexcept;

enum class InvocationRecordKind { Rejected, ReadCompleted, FailedBeforeApply };
struct InvocationRecord {
  std::uint64_t sequence;
  Name static_trace_label;
  InvocationRecordKind kind;
  std::optional<foundation::ErrorCode> error;
};
struct InvocationSnapshot {
  std::size_t written;
  std::uint64_t dropped;
  bool dropped_saturated;
  bool sequence_exhausted;
};
class NativeEngine;
template<ContractValue A, ContractResult R> class NativeBound;
class NativeEngine final {
public:
  static Result<std::shared_ptr<NativeEngine>> create(
      std::shared_ptr<const registry::Catalog>,
      std::shared_ptr<policy::SessionAuthority>,
      std::shared_ptr<TrustedThreadPort>, NativeBudget);
  template<ContractValue A, ContractResult R>
  Result<NativeBound<A,R>> bind(
      const OperationKey&, ContractDigest, Shape,
      std::shared_ptr<const policy::VerifiedCaller>,
      std::span<const foundation::ObjectId> fixed_targets,
      TargetProjection<A>, Name static_trace_label);
  InvocationSnapshot snapshot(std::span<InvocationRecord> output) const noexcept;
private:
  struct State;
  explicit NativeEngine(std::shared_ptr<State>);
  std::shared_ptr<State> state_;
};
template<ContractValue A, ContractResult R> class NativeBound final {
public:
  NativeBound(NativeBound&&) noexcept;
  NativeBound& operator=(NativeBound&&) noexcept;
  NativeBound(const NativeBound&) = delete;
  NativeBound& operator=(const NativeBound&) = delete;
  ~NativeBound();
  InvokeReply<R> invoke(const A&, const InvokeOptions&) const;
private:
  friend class NativeEngine;
  struct State;
  explicit NativeBound(std::shared_ptr<State>);
  std::shared_ptr<State> state_;
};
```

create/bind/prepare_inline 的 Catalog、Session、ThreadPort、VerifiedCaller、DefinitionSnapshot、TargetView 及所有保存的 shared owner 均拒绝 !p 或 use_count()==0；非空 raw 指针的空控制块 alias 不构成拥有。既有 Resource owner 同样不能仅以 operator bool 当作真实拥有证明。只接受拥有关系不证明外部能力已经授予，不能把 lifetime owner 自动升级成调用授权。

所有非默认可构造类的构造均由声明的 friend 工厂完成；空/移出 NativeBound 调用返回 Rejected。State 仅保存不可变绑定材料及有界可用槽，不接收业务 std::function。Engine/Bound 的持有计入 NativeBudget；所有上限非零，乘加 checked；达到上限拒绝，不自动扩容。最后 Bound/正在运行调用的所有权释放才返还绑定占用。

TargetProjection 是必需的非空函数指针；null 在 bind 返回 InvalidBinding，不能保存后调用。TargetProjection 是可信注册/装配适配器，不是业务请求中的函数地址；其职责与 Args 合同一并审核，完整提取实际目标。此 bind 是可信装配入口，不开放给动态请求选择映射；未来远程入口只能选择已经绑定的能力。Reader 的受审服务合同须与映射所指的实际目标一致：返回 A 的授权不能用于服务读取 B，测试以两个目标返回不同值检查关联。纯计算与 Reader 的目标映射均随绑定被冻结。每次都执行它，不允许永远返回绑定目标以掩盖 Args 中的另一个目标。纯计算若业务确无目标字段，由受审适配器返回声明的固定计算作用域目标。输出目标不得为空、重复、超容量；采用有界扫描对照固定目标集合，无每次排序容器/堆分配。目标集合不同拒绝，不能用预解析身份批准另一个 Args 目标。

每个 Bound 在 bind 阶段预留有限个调用槽；每槽含定长容量的目标暂存及治理占用；本包 Native 资源视图为空。invoke 原子取得空槽，递归/并发超过容量返回 Busy，不等待同槽、不增加槽。槽归还在业务和结果处理完成后。预热不能消耗后再补发无限许可。

## 4. Registry 私有桥及完整绑定

Registry 保持对 CoreContracts 的依赖；Invocation 依赖 Registry 与 Policy。不得让 Registry 链接 Invocation 或形成环。typed thunk 的类型与模板定义固定放在 registry.hpp，由 Registrar 各注册模板生成并存入对应私有 Catalog 条目。registry.hpp 只前置声明 `invocation::NativeAccess`，Catalog 授予其 friend；具体类放在 `packages/runtime/invocation/private_bridge.hpp`，不安装为 SDK：

```cpp
namespace ock::runtime::invocation {
class NativeAccess final {
  friend class NativeEngine;
  template<ContractValue A, ContractResult R> friend class NativeBound;
  static Result<std::uint32_t> resolve(
      const registry::Catalog&, const OperationHandle&,
      const DefinitionSnapshot& expected) noexcept;
  static const registry::detail::HotEntry& entry(
      const registry::Catalog&, std::uint32_t validated_slot) noexcept;
  static void dispatch(const registry::Catalog&, std::uint32_t validated_slot,
                       const void* args, WorkContext&, void* result_slot);
};
}
```

上述方法均为 private。resolve 必须验证 Catalog 原身份/世代/范围及原 DefinitionSnapshot 指针；entry/dispatch 只能由已经成功 resolve 的本次管线调用，不能有对消费者公开的转发包装。private_bridge.hpp 可被负编译控制包含，但调用私有方法/提取裸 Handler必须失败。不在 BindingPort 增加 get_handler，不向模块返回 Handler、服务裸指针或无治理的 invoke 回调。

注册期为实际 F 生成以下逻辑等价的内部 thunk；其类型和字段都不是公共 SDK：

```cpp
// 只有内部 NativeAccess 能从 Catalog 对应条目选取、调用。
using NativeThunk = void (*)(const detail::HotEntry&, const void* args,
                            WorkContext&, void* result_slot);
// result_slot 实际为 std::optional<Result<R>>*，初始 disengaged；
// thunk 只能在本条目的 A/R/F token 已经匹配后使用，emplace 恰好一次。
```

compute thunk 调实际 `F(args,work)`；read thunk 根据注册时 Reader 类型与模块限定 ServiceRef 选择的真实 owner，构造 ReadServices<Reader> 并调用 F；candidate_read thunk 对原 pair 的 first 做同样操作，不能误把 pair 当函数指针、不能调用 second。各 thunk 不返回实际 F，也不返回函数 owner。shape 不支持时不给可执行 thunk，前置拒绝。typed 服务 owner 与 executor/resource owner 的角色必须明确存储，不能在运行时靠无类型 owners 数组猜位置。HotEntry 增加资源声明数量，注册期从经过验证的 OperationOptions.resources 保存；invoke 对该可信数量非零明确拒绝 ResourceUnavailable，不以资源 owner 是否非空作为准入成功条件。

bind 使用既有 CoreContracts typed bind 验证全部合同，同时保存 Catalog 身份/世代/slot、原 DefinitionSnapshot 对象、A/R token、冻结 TypeIdentity、精确 key/digest/shape、执行要求、私有 thunk 及限定服务/资源 owner。不把调用方参数保存为权威 snapshot。每次调用仍对实际 Catalog 身份/世代/slot/原快照和 token 做无分配重验；TypeContract 身份是绑定期间不可变的合同身份，不能在每次调用重新构造动态版本字符串。此新增路径不会改变现有 BoundOperation::revalidate 的行为；动态重绑定必须显式 bind，不能只复用旧编号。

固定拒绝优先级为无效绑定、不可用 shape Provider、非空资源声明 ResourceUnavailable，然后其余线程/输入/权限准入；所以资源操作在有效绑定和受支持 Read shape 下唯一资源拒绝原因可测试。参数及结果分别仍调用 TypeContract<A>::validate 与 TypeContract<R>::validate。冻结身份只免去身份材料重建，不免去实际值校验。不合法输入、绑定不匹配、Provider 不可用、线程不合法都不能进入 thunk。

## 5. Policy 的窄内联准入增量

只增加 Read Invocation 准入，不扩 ActionPermit 的能力或放宽其一次消费。Policy 仍只依赖 CoreContracts；用 DefinitionSnapshot 承载权威操作合同，不链接 Registry。拟在 policy.hpp 增加 operation.hpp 包含及：

```cpp
namespace detail { struct InlineRecord; struct Access; }
class InlineAuthorization;
class InlineAdmission;
// SessionAuthority 新方法：
Result<std::shared_ptr<const InlineAuthorization>> prepare_inline(
    const VerifiedCaller&,
    std::shared_ptr<const DefinitionSnapshot> catalog_definition,
    std::span<const std::shared_ptr<const TargetView>> original_targets,
    std::size_t maximum_concurrent_calls);

class InlineAdmission final {
public:
  InlineAdmission(InlineAdmission&&) noexcept;
  InlineAdmission& operator=(InlineAdmission&&) noexcept;
  InlineAdmission(const InlineAdmission&) = delete;
  InlineAdmission& operator=(const InlineAdmission&) = delete;
  ~InlineAdmission();
  TimePoint deadline() const noexcept;
private:
  friend class InlineAuthorization;
  friend struct detail::Access;
  explicit InlineAdmission(std::shared_ptr<detail::InlineRecord>, std::size_t slot, TimePoint) noexcept;
  std::shared_ptr<detail::InlineRecord> record_;
  std::size_t slot_;
  TimePoint deadline_;
};
class InlineAuthorization final {
public:
  ~InlineAuthorization();
  InlineAuthorization(const InlineAuthorization&) = delete;
  InlineAuthorization& operator=(const InlineAuthorization&) = delete;
  Result<InlineAdmission> admit(
      const DefinitionSnapshot& expected_catalog_definition,
      std::span<const ObjectId> actual_targets,
      std::stop_token, TimePoint requested_deadline) const;
private:
  friend struct detail::Access;
  struct State;
  explicit InlineAuthorization(std::shared_ptr<State>);
  std::shared_ptr<State> state_;
};
```

detail::InlineRecord 在 policy.cpp 定义为内部拥有记录，保存原 Session/VerifiedCaller/Target owner、DefinitionSnapshot、世代/期限、预留槽占用及 Hold 计费；不向头文件提供构造能力或可调用方法。InlineAuthorization::State 在 policy.cpp 定义并拥有 shared_ptr<detail::InlineRecord>。detail::Access 是既有内部工厂，作为两类明确的 friend 合法建立 State/InlineAdmission；admit 也可直接通过其 friend 关系构造 InlineAdmission。不存在另一个未声明 Record 工厂。

Native 调用缓冲槽与 Policy 活跃配额保留双层：先取得 Native 槽，再在 Policy 仲裁取得 InlineAdmission；后一项失败即归还前一项。两个槽池均不等待、不扩容；不持 Native 槽池短锁进入 Policy 锁，亦不持 Policy 锁记录观测。移动 Admission 只转移一次归还责任，移出对象无责任。正常、前置拒绝、业务/验证异常及记录完成后均由 RAII 在对应生命周期归还；不能在结果处理前提前释放活跃计量。PolicyBudget 新增 `inline_bindings=128`、`active_inline_calls=128` 上限；预留槽、目标 owner、元数据均并入既有 declarations/text_bytes；已关闭但仍活着的绑定继续计占用，进行中调用继续持有其源 owner。零分配范围不包含 prepare_inline。admit 返回的 deadline 为请求、绑定/认证/会话上限的最小值；WorkContext 使用该服务器收窄后的值，不能只使用请求期限。work_limit 必须非零且不超过 NativeBudget::work_units，否则进入前 BudgetExceeded；每次预算独立从零开始累计。

NativeEngine::bind 对有效 StateEdit/ExternalEffect/Lifecycle 或有资源声明的操作只冻结真实目录、shape 与资源事实，不创建 Read InlineAuthorization；只有受支持且无资源声明的 Read Bound 才逐项 resolve 原始目标并 prepare_inline，invoke 以 ProviderUnavailable 优先拒绝非 Read shape、以 ResourceUnavailable 拒绝 Read 非空资源，均业务零进入，不让 bind 提前拒绝而掩盖该运行时边界。prepare_inline 验证具体 VerifiedCaller 来源、同 Session 原 authority/grant、每个原 TargetView 的发行来源和生命周期，记录 Store/Session、委托/权限世代、目标 instance/lifecycle、原 DefinitionSnapshot 指针及截止时间上界。不得接受 self-reported target stamp 或原 DTO 伪造能力。

所需权限从 catalog_definition->description().required_permissions 读取。Policy 安装项必须精确匹配该 key/digest，且其 required_permissions 与 Catalog 规范化权限集合完全相等；遗漏、额外、重复不一致均 ContractMismatch，不能因为误装空权限而批准高权限 Catalog 操作。重复定义的集合规则沿用注册与 Policy 的唯一性校验，不跨规则合成。匹配时仍按现有完整 allowed tuple，对每个实际目标逐项验证 principal ACL、请求 scope、认证 ceiling、operation module rules 和 target rules；完整同一权限集合由每一来源中的同一条 rule 覆盖。

admit 与 close/restrict/revoke/lifecycle/配额使用同一 Store 短 mutex，重验原 caller、Store/Session 非 closed、所有期限、policy/委托世代、原 TargetView identity/lifecycle、精确操作、完整权限集合和全部目标，以及可用的已预留槽。slot 计数改变为 Read 的准入线性化点。取消在此点之前可见则拒绝；成功准入后撤权不追溯宣称业务未进入。Read 的后续取消通过 WorkContext 合作处理；它不是 Effect/Commit 许可，不能持它执行写入或发送。

仲裁内不调用业务、TargetProjection、TypeContract、任意端口回调，不分配，不释放最后 owner。ClockPort::now 是既有受信 noexcept 短端口，仍沿用既有时钟合同。槽归还/关闭仅在锁内更新计数，owner 最后析构在锁外。绑定失效后必须重新认证/解析/绑定；不能修改旧 InlineAuthorization 的 epoch 恢复它。

## 6. 线程与借用资源

TrustedThreadPort 在可信装配时固定，不接受每次请求传入。最小验证实现保存真实 std::thread::id 与允许的 role/affinity 映射；current 读取当前实际线程并查固定表，不信任请求写入身份。不属于该表、禁用 inline、affinity 不一致、非法枚举均拒绝。注册 inline_safe=false、requires_async_dispatch=true 或 requires_external_wait=true 均拒绝短调用。请求不能强制 worker/control/domain/database 自等待；本包没有阻塞包装器或隐式 submit。InvokeOptions 不提供 wait/block/force_inline 开关；试图请求阻塞包装为编译拒绝，已支持入口的运行时线程身份/模式拒绝分别用真实其他线程验证，不能把不存在的模式测试写成执行成功。

允许哪些注册 affinity 能在哪些可信角色上 inline，必须由固定绑定表显式给出，并在每次调用核对；不能笼统把所有 worker 禁止或把所有 Application 视为允许。吞吐/排队执行器仍后续实现。

WorkContext 保留现有拥有构造，增加明确借用构造：

```cpp
struct BorrowedResourceViews {
  std::span<const ResourceLease* const> values;
};
WorkContext(std::stop_token, std::chrono::steady_clock::time_point,
            foundation::CheckedCount<std::uint64_t>, Name trace,
            BorrowedResourceViews);
```

BorrowedResourceViews 可构造本身不授予权限。本包 Invocation 一律传空借用视图，并让调用栈持有 Bound 与 InlineAdmission，覆盖完整同步 Handler/结果验证。通用非空借用构造只通过 CoreContracts 受信手工 lease 的寿命正负控制验证；该测试不证明 Native 已有资源获取能力。上下文不可逃逸、不可捕获到异步回调；本入口没有异步成功返回。WorkContext 内部明确采用 `std::optional<OwnedResources>` 与共同的 span；OwnedResources 保存现有 owners/vector views，拥有构造才 emplace。借用构造让 optional 保持 disengaged，span 直接指向本次有寿命保证的视图，不构造任何隐藏 vector，包含 MSVC Debug proxy。其私有资源字段固定为：

```cpp
struct OwnedResources {
  std::vector<std::shared_ptr<ResourceLease>> owners;
  std::vector<const ResourceLease*> views;
};
std::optional<OwnedResources> owned_resources_;
std::span<const ResourceLease* const> resource_views_;
```

拥有构造在 owners 完成后建立 views，再将 resource_views_ 指向稳定 views；借用构造只设置 span。已有拥有构造和删除复制语义保持不变。每次 WorkContext 在栈上重建 stop/deadline/预算，不复用上次 charge 后的预算；Name 为固定值。资源 lease 的实际 acquire 语义不得从 TargetPolicy lifetime_owner 或 Registry ResourceBinding owner 伪造；本包没有 Native 资源获取端口、预授捷径或并发资源调度器。资源数量非零的 Native 调用始终业务零进入。

## 7. 调用顺序、异常与 Outcome

顺序：取得有限调用槽 → 验绑定/shape/线程 → TypeContract<A> 实际校验 → TargetProjection 实际目标核对 → Policy admit → 构造借用 WorkContext → private thunk → 验 R/归一 Outcome → 释放本次槽与准入租约。所有业务回调在任何 Registry/Policy 仲裁锁之外。任何前置失败释放已取得槽；一次成功 Handler 恰好执行一次。

受信输入/结果校验或 Handler 抛出异常均由 Invocation 边界捕获。进入前返回 Rejected 固定错误；进入后的只读异常返回 Completed/FailedBeforeApply，附 BeforeApply 证明，不伪造已取消、已提交或效果未知。bad_alloc 走相同阶段分类，最小失败结果不要求动态 ErrorInfo。Outcome 校验本身抛出（包括 TypeContract<R> 内抛出）也归入该进入后失败，不降格为 Rejected。失败 Outcome 使用不含 R 的 FailedBeforeApply 分支及已预留最小证明，不能递归调用刚刚抛出的 R 验证器；内部通用事实校验须无分配且固定错误返回，其不满足时属于实现合同失败，不能谎称完成。调用槽 RAII 保证异常后容量恢复；throwing noexcept 回调的 terminate 不可被宣称可捕获，TargetProjection/ThreadPort 合同必须非抛出。

绑定时一次创建经 KnownFacts::create 验证的空事实 owner，成功纯读共用其不可变 owner；不是伪造空 document/Task。每次 Outcome<R>::validate 仍执行全部 applicable 校验及实际 R 校验，返回正常 CoreContracts InvokeReply<R>。保留既有 detail::unresolved helper 的签名/行为以兼容已有消费者测试，但 Outcome::verify 不再使用它。新增内部无分配 helper：

```cpp
std::size_t count_unresolved(const KnownFacts&) noexcept;
bool is_unresolved(const KnownFacts&, FactId) noexcept;
```

两者只遍历不可变事实，按原 Unknown/Resolution 关联语义计算。verify 的全部九个 Outcome 分支统一以 count/成员检查代替临时 vector；要求集合相等的分支同时检查候选数量、每个成员是否真实未解决、候选自身无重复，不能只做 contains 从而接受重复替代缺项。保留已知事实唯一性、Resolution 引用合法性、追加语义、未知事实消解和各结果分支全部原约束。不以跳过通用验证的 test-only Outcome 快捷构造满足计数。Result<R>/Outcome<R> 直接值返回；小 R 不动态装箱。

## 8. 零分配与可验证边界

固定计数场景使用实际注册、实际 bind、实际身份认证、原目标解析与有限槽预热。成功窗口包含每次入场、真实 Args 校验、TargetProjection、完整 Policy 重验、可信线程检查、业务、实际 R 校验、Outcome 与清理；治理不得移出窗口。先保存有确定分配的正探针和不分配负探针，失败不会反填预算或删除校验。

受限合同仅对已审无业务分配、小定长 Args/R、易失同步、空 granted_resources、静态标签、有界内存观测成立；Native 固定成功场景仅空资源；非空声明的明确拒绝也有真实业务零进入控制。CoreContracts 借用上下文的通用非空寿命控制单列，不混写成 Native 资源授权/获取通过。long OperationVersion 在 bind 可分配，但成功重验不重建它。实际 TypeContract 验证若会分配，该具体类型不满足固定场景，仍可以一般调用并另报成本，不能禁掉校验。

统计覆盖 new/new[]/aligned new、相关 CRT/自定义分配器及实际链接模块；遗漏来源列出，不声称仅替换 operator new 就进程全局覆盖。Debug、Release、ASan 都测各自实际 new 系列通道；Debug CRT hook 为独立诊断通道，不把两通道计数相加。new/new[]/aligned 与 CRT 各入口各自正控制，另注入一次分配让零断言实际 red。Release CRT/DLL/自定义堆的覆盖盲区明确列出，源码与导入表仅作补强，不构成全局覆盖证明。无计数 Release 的正式延迟测量留在 D1.06。MSVC Debug 容器 proxy 与 Release 行为分别记录；若某被测配置有分配则该配置不得记零。后台观测若存在必须包括同调用因果成本，不把分配移到窗口外；本包验证消费者不创建后台线程。

首用、bind、有限预热、失败详情、可变 R、长字符串合同、并发槽耗尽、回收/峰值另报；不制定 D1.06 NativeSubset 的最终内存/体积/启动预算。资源、目标、slot 总量与生命周期必须有限并可测释放恢复。

## 9. 内部有界同步观测

Engine::create 按 NativeBudget::observation_capacity 一次预分配环形存储，上限必须非零，容量乘 sizeof(InvocationRecord) 做溢出检查；占用与其他预绑定存储一起记录峰值和释放。槽可使用 optional<InvocationRecord>，创建后调用期不得扩容。记录只有固定 Name、sequence、类别及可选固定 ErrorCode，没有动态文本、TaskId、ExecutionRef、事实 ledger 或回调。

每个调用最终只记一条：前置失败 Rejected；成功 ReadCompleted；进入后失败 FailedBeforeApply。空/移出 Bound 没有 Engine 归属，只能返回 InvalidBinding，不伪造某个 Engine 记录；所有具有 Engine 的调用，包括 Busy/权限拒绝、输入/结果异常，均记录。记录发生在返回结果之前，分配窗口包含结果处理、记录、槽/Admission 归还，不留后台补写。

环满时覆盖最旧诊断，dropped 加一；被覆盖的是易失调用诊断，不是执行事实/Effect/Commit 回执。observation_counter_limit 是可信装配的非零总序号/计数上限，缺省 UINT64_MAX，可降低以真实触达同一拒绝/饱和分支，不可运行时修改。dropped 在该上限饱和并置 dropped_saturated，不回绕。sequence 从 1 起 checked 递增，上限值可以写一次，此后 sequence_exhausted=true，停止追加、对每个新诊断只饱和累加 dropped，不复用序号、不影响已经完成的业务结果。无需每次申请身份对象。

Engine::snapshot(output) 只复制当前保留的完整记录，按 sequence 升序写入调用者已构造的 span，容量不足复制最新的 output.size() 条并维持升序；written 是实际复制数量。snapshot 不消费记录，不清掉 dropped/标志，不分配，output 为空合法。返回标志和数据在同一短锁内形成一致快照；调用者不持有内部指针。记录写入和快照只包固定值复制/索引操作，不包业务、TypeContract、TargetProjection、任意析构或 Policy 仲裁；无后台线程。

验证至少包含容量 1/2 覆盖次序、dropped、错误类别/错误码、空输出/小输出快照、记录读取一致性及完整调用窗口零分配。该内部记录不命名为公共 LoggingPort，不包含 exporter/backpressure/Host 生命周期，不代替 D1.06 LoggingConformance。

## 10. 修订 2 冻结候选

本修订已选择 registry.hpp 注册模板 thunk、private_bridge.hpp 私有 NativeAccess、Native 缓冲槽与 Policy 活跃计量双层、明确 Record/friend 构造关系、optional 拥有资源存储、九类 Outcome 无分配事实检查和固定环形诊断。上述都是明确的待审核设计，不再留实施者自行选择不同语义的未定接口。

初稿、修订 2 和后续独立审查应绑定各自 SHA，旧报告不覆盖。当前仍 PendingReview，没有行为实现授权或 D1.05 Passed 结论。

# D1.02 CoreContracts 原生 API 冻结稿

日期：2026-09-07。本文是实施前 AI 规格审核输入，尚无本包代码、编译结果或 Passed 结论。启动依据为 [D1.02-start.json](../reviews/D1.02-start.json)：D1.01、D0.02、D0.03、D0.05 已逐项核验 Passed；[已审实施计划](../plans/D1.02.md) 中的准备时状态保留为历史说明。唯一语义规范仍为 [架构 v3.3](../01_Architecture_v3.3.md) A04/A05/A06/A09/A17 及 [执行卡](../02_Execution_Plan_v3.3.md#d102)。

本文冻结公开命名、声明形态、所有权及拒绝规则；代码块省略 include、析构声明和机械访问器，不省略影响信任/类型推导的入口。正文表格列出的字段也是实施输入。不得将“受信端口”实现为永远成功的生产 stub。生产 Registry、Policy、Invocation、Executor、State、观察索引和存储仍由原后续工作包实现，本包只交付合同、纯验证及测试工厂。

## 1. 六头与共同规则

命名空间固定 `ock::contracts`，target 固定 `OCK::CoreContracts`，直接产品依赖只有 `OCK::Foundation`。复用实际 `ock::foundation::Result`、`Error`、`Name`、`Tagged128`、`BoundHandle`、`resolve_slot` 和受检整数运算；不重新实现 expected。

| 公开头 | 拥有的声明 | 项目内包含方向 |
|---|---|---|
| `identity.hpp` | 稳定身份、精确版本、类型 token、TypeContract | Foundation |
| `context.hpp` | Caller、可信授权端口、受限视图、shape/执行声明 | identity |
| `outcome.hpp` | 九种 Outcome、事实、发布证明端口、phase/回复 | identity |
| `ports.hpp` | Executor/Completion、AtomicProvider、准备材料、必要记录 | context、outcome |
| `observation.hpp` | 小摘要、观察版本、受权查询/监听/枚举 | context、outcome |
| `operation.hpp` | 受检定义工厂、typed 绑定；统一包含以上五头 | 以上五头 |

六头均可独立包含。仅使用标准 RAII 与领域接口，无 `void*`/`std::any` 服务查找、JSON、Payload、Document、SQL、Host、动态类型容器或自研 future/function/STL。接口析构为虚拟且 noexcept；所有抽象端口不可复制。共享端口指针只延长服务寿命，不代表可并发调用；线程要求见第 7 节。

错误以独立静态 `contracts_domain` 和 `ContractsErrc` 表达：`InvalidVersion, InvalidContract, TypeMismatch, ShapeMismatch, StaleBinding, InvalidAuthority, InvalidGrant, InvalidProof, InvalidFact, ContradictoryFact, InvalidPhase, BudgetExceeded, DuplicateCompletion, NotQuiescent, HostMismatch, StaleObservation, SubmitRequired, Rejected`。预期合同错误返回 `Result`；分配异常自然传播，不包装为可重试业务错误。所有字节/数量预算先校验再分配，失败不改变已发布对象。

`DefinitionSnapshot`、`KnownFacts`、`PreparedCommit`、`ExecutionSummary`等冻结类采用私有构造、删除复制/移动赋值，只暴露 const 访问器；受检工厂从输入描述深复制标准文本/序列，再创建 `shared_ptr<const X>`。不接纳调用者的 `shared_ptr<MutableX>` 并仅转换为 const，不把 `std::move(vector)` 当作已消除外部元素指针。所有冻结类没有返回可变容器的接口；如内部使用 shared_ptr，所指对象也须按同样规则创建。此规则沿 Foundation `ErrorInfo::create` 的私有创建路径与不可赋值保护；绝不能复制出一个可赋值的可变详情别名。

## 2. identity.hpp：身份与显式类型合同

```cpp
class OperationVersion final {
public:
    static Result<OperationVersion> parse(std::string_view, std::size_t max_bytes);
    std::string_view text() const noexcept;
    bool operator==(const OperationVersion&) const noexcept;
private:
    explicit OperationVersion(std::string);
};
struct OperationKey { Name name; OperationVersion version; };
struct ContractDigest { std::array<std::byte, 32> bytes; };
struct TypeIdentity { Name name; OperationVersion version; ContractDigest digest; };

class CppTypeToken final {
public:
    template<class T> static CppTypeToken of() noexcept;
    bool operator==(const CppTypeToken&) const noexcept;
private:
    explicit CppTypeToken(const unsigned char*) noexcept;
    const unsigned char* anchor_;
};
enum class AsyncOwnership { Disallowed, Owning };
template<class T> struct TypeContract; // 只有显式特化才有值合同。
template<class T> concept ContractValue = /* 下述精确检查 */;
template<class T> concept AsyncInput = /* 下述默认拒绝与拥有声明 */;
```

OperationVersion 为三个非空、无前导零的十进制分量（单个 `0` 合法），完整保留受预算限制的规范文本；不压为 uint32、不执行兼容范围排序。`parse` 的 max_bytes 是调用方已选定的输入预算，不能以一个超限值改变系统上限；公开配置所有者在接收外部输入时进一步限制它。所有精确键比较按名称和规范版本文本相等。ContractDigest 只是已有 canonical 契约摘要的 32 字节载体，本包不以 std::hash 或原始对象内存生成摘要。

为 CommitId、EffectId、TransitionId、PrincipalId、HostIncarnation、FactId、ReservationId 分别定义独立 Tag 并 alias Foundation Tagged128。`ExecutionRef{TaskId execution_id}`、`PrincipalRef{PrincipalId principal_id}`与正式 DTO 一致。AtomicProviderKey 使用 Name；AtomicDomainRef 包含 provider、ObjectId domain_id、RegistryGeneration generation。RuntimeEpoch、CppTypeToken、句柄和进程指针不能进入稳定身份/持久 DTO。

每个精确 T 使用独立非 const 静态字节地址产生 token，避免常量折叠合并；无需 RTTI、哈希或任意对象指针转换。工厂只对签名中的非 cv 值类型调用 `of<T>`，不把引用/const 错误签名 normalize 成合法值；`of<void>`合法。token 是进程内 C++ 类型相等见证，不保证任意 DLL 边界一致、不持久化，也不构成 ABI 承诺。

有效 `TypeContract<T>` 必须提供 `static TypeIdentity identity()` 与 `static Result<void> validate(const T&)`，返回型精确一致。T 是非引用、非 cv、可析构的值类型，Args 非 void；R 可为 move-only 或 void。void 结果由 `Result<void>` 的成功表示，不调用 `validate(const void&)`；内置 `TypeContract<void>::identity()` 有固定稳定描述。类型合同身份每次读取必须相同，定义工厂只冻结一次副本。

拥有声明采用可选 `static constexpr AsyncOwnership async_ownership`；不存在时按 Disallowed，不能仅检查某个字段“存在”。AsyncInput 额外要求明确 Owning 且 T 非标准借用类型：裸指针/引用、string_view、span、reference_wrapper 即使被误标也拒绝。嵌套借用合同默认拒绝，合法 Owning 是可信注册作者的审查承诺，C++20不能反射证明任意对象图。异步转交后 Args 及可达材料冻结或独占；shared_ptr<mutable T> 仅保活不证明快照。显式深拷贝适配函数先得到受审 carrier，再进入 AsyncInput 入口；适配失败不产生 Accepted。

## 3. context.hpp：窄能力与可信来源

### 3.1 描述不是凭据

`CallerDescription` 是拥有型 DTO：PrincipalRef、可选委托主体、受预算限制的调用标签。`PermitBinding` 为拥有 DTO：主体、OperationKey/组 ContractDigest、已解析 ObjectId target、权限世代、生命周期世代及 steady_clock 截止点。它们都不能直接构造有效 CallerView 或 ActionPermit。

```cpp
class CallerGrant {
public:
    virtual ~CallerGrant() = default;
    virtual const CallerDescription& description() const noexcept = 0;
protected:
    CallerGrant() = default;
};
class CallerAuthorityPort {
public:
    virtual Result<std::shared_ptr<const CallerGrant>> authenticate(
        const CallerDescription& request) = 0;
    virtual Result<void> validate(const CallerGrant&) const = 0;
};
class ActionPermit {
public:
    virtual ~ActionPermit() = default;
    virtual const PermitBinding& binding() const noexcept = 0;
protected:
    ActionPermit() = default;
};
class PermitAuthorityPort {
public:
    virtual Result<std::shared_ptr<const ActionPermit>> issue(
        const CallerGrant&, const PermitBinding&) = 0;
    virtual Result<void> consume(const ActionPermit&, const PermitBinding&) = 0;
};
```

以上是生产发放协议，不是 CoreContracts 内置认证器。authenticate 的 description 仅提出请求，真实端口必须利用自身可信入口上下文核实来源，不能相信自填 Principal。组合根固定并保有实际 authority，业务请求不能替换该实例。`validate/consume` 必须检查该实例真实发放记录、绑定、失效/世代及期限；不能转而调用凭据自报的 `valid()` 或仅比较它自报的 issuer_id。consume 与撤权的真正短仲裁由 D1.04 实现。

CallerView 是由 `CallerView::check(shared_ptr<CallerAuthorityPort>, shared_ptr<const CallerGrant>) -> Result<CallerView>` 建立的只读 owner，构造私有；`revalidate()`每次向其固定 authority 验证。此工厂用于组合根已选择的 authority，不能将请求中的 authority 传入。敏感接收端同时核对 view 的 authority 实例等于自身装配实例，然后 revalidate；持有业务自建 authority 下的 view 不能穿透真实接收端。authority 身份比较由 `belongs_to(const CallerAuthorityPort&)`完成，不公开 grant 原始存储或 issuer 可写字段。

authority真实发放实现的grant/proof记录必须使用私有构造和不可赋值冻结对象，仅从已核实材料深复制创建；不得向外返回可变发放记录，或者保留调用者可修改的description/PermitBinding别名。抽象 grant 的派生不等于发放能力：外部即使实现同接口，也不能进入配置 authority 的已发放集合。测试同时配置真实小型测试 authority 和独立伪 authority，验证同样字段/同样 Principal 的伪 grant 被前者拒绝。没有公开“生成有效凭据”值构造器、测试 friend、skip 或全局万能 authority。测试工厂只创建其自身信任域的 grant，不能创建另一 authority 的有效对象。

ActivityLease 与 ResourceLease 是不同抽象 RAII owner，只管理各自寿命/资源释放，没有到 ActionPermit 的转换。`EffectContext`保有已检查 CallerView、受限已解析 TargetView 和 ActionPermit 只读引用；授权消费仍走固定 PermitAuthorityPort。TargetView 的建立和重验沿同一已配置 authority 协议，不能由请求 ObjectId 隐式转为有效视图。

### 3.1.1 Target authority 与真实接收者复验（增量冻结）

下列具体入口落实原3.1的固定authority协议，依据[增量提案](../../evidence/bootstrap/D1.02/target-authority-proposal.md)及[独立AI规格批准](../../evidence/bootstrap/D1.02/spec-review-final-target-authority.md)。TargetView只描述已解析目标，不提供自报有效的revalidate；TargetAuthorityPort验证其真实发放集合、caller绑定、目标与失效状态。

```cpp
class TargetView : public PortLifetime {
public:
    virtual foundation::ObjectId target() const noexcept = 0;
protected:
    TargetView() = default;
};
class TargetAuthorityPort : public PortLifetime {
public:
    virtual Result<std::shared_ptr<const TargetView>> resolve(
        const CallerView&, foundation::ObjectId requested) = 0;
    virtual Result<void> validate(const TargetView&, const CallerView&,
                                  foundation::ObjectId expected) const = 0;
};

class EffectContext final {
public:
    static Result<std::unique_ptr<EffectContext>> check(
        std::shared_ptr<const CallerAuthorityPort> expected_caller_authority,
        std::shared_ptr<const TargetAuthorityPort> expected_target_authority,
        CallerView caller, std::shared_ptr<const TargetView> target,
        std::shared_ptr<const ActionPermit> permit,
        const PermitBinding& expected_binding,
        WorkContext& work, EffectRecordPort& records);
    Result<void> revalidate(const CallerAuthorityPort& expected_caller_authority,
                            const TargetAuthorityPort& expected_target_authority,
                            const PermitBinding& current_expected_binding) const;
    // 其余已有窄访问器不变；构造私有、禁止复制和移动。
};
class TransitionView final {
public:
    static Result<std::unique_ptr<TransitionView>> check(
        std::shared_ptr<const CallerAuthorityPort> expected_caller_authority,
        std::shared_ptr<const TargetAuthorityPort> expected_target_authority,
        CallerView caller, std::shared_ptr<const TargetView> target,
        Name before, std::uint64_t generation,
        std::shared_ptr<const ActionPermit> permit,
        const PermitBinding& expected_binding, TransitionPort& transition);
    Result<void> revalidate(const CallerAuthorityPort& expected_caller_authority,
                            const TargetAuthorityPort& expected_target_authority,
                            const PermitBinding& current_expected_binding,
                            const Name& current_before) const;
    // 其余已有窄访问器不变；构造私有、禁止复制和移动。
};
```

EffectContext/TransitionView的构造均私有，禁止复制和移动。check拒绝空owner，先验证CallerView属于组合根装配的CallerAuthority并向该实例验证真实发放记录，再核对permit材料与真实接收端生成的expected_binding完全一致，主体与caller一致、目标非零、期限有效，最后由TargetAuthority验证真实目标grant与主体/目标绑定。Context保存两个authority及target/permit的owner，不保留调用者可改写的绑定容器别名；work/records/transition只借用同步作用域。

expected/current PermitBinding必须由真实接收端当前已绑定Operation或组摘要、已解析目标、当前权限及生命周期世代和服务端截止政策生成，禁止用permit.binding()或请求字段直接充当expected。Transition的before/generation来自真实协调者当前状态，generation须与当前binding一致且非零。实际敏感接收端调用产品revalidate时先比较自身两个authority与Context记录的实例身份，再用自己的current_expected_binding/current_before重复当前记录、绑定、期限与状态检查。业务可在伪authority域构造本域Context，但不能穿过真实接收者复验。

许可字段匹配不是真伪证明或授权消费。实际协调者在发送/转换/提交附近仍调用自身固定PermitAuthorityPort::consume(original_permit,current_expected_binding)，且与上述revalidate使用同一份由接收端可信状态生成的binding。Context::check/revalidate不提前consume；本包不实现生产策略或设备发送。

既有context_capability_boundary/effect_shape/lifecycle_shape内补固定子断言：同一真实issuer的A/B两主体分别交换permit与target grant，均拒绝且consume计数为0；跨issuer、伪目标、撤销、过期另测。真实接收端改变当前operation/组摘要、目标、权限/生命周期世代或before，同时保持提交Context与permit不变，必须拒绝。合法上下文在真实接收端复验后才允许调用测试许可消费；38个CTest名称不变。

### 3.2 四种 shape 的声明

```cpp
enum class Shape { Read, StateEdit, ExternalEffect, Lifecycle };
enum class AtomicMode { Incompatible, PureCompute, CandidateRead, StateEdit };
struct ExecutionRequirements {
    bool inline_safe;
    bool requires_async_dispatch;
    bool requires_external_wait;
    Name executor;
    Name thread_affinity;
};
class WorkContext final {
public:
    bool stop_requested() const noexcept;
    std::chrono::steady_clock::time_point deadline() const noexcept;
    Result<void> charge(std::uint64_t units);
    const Name& trace_name() const noexcept;
    std::span<const ResourceLease* const> granted_resources() const noexcept;
};
template<class Reader> class ReadServices final;
template<class Provider> class EditView final;
class EffectContext final;
class TransitionView final;
```

WorkContext由协调入口用 stop_token、截止点、CheckedCount、trace Name和已授资源owner集合建立，不能继承扩张；返回资源 span 只借用当前调用，内部持有其owner。资源owner集合须复制shared owner条目到自己的存储，不接管仍可能被调用者持有元素别名的vector缓冲；调用者改写或销毁原集合不能令granted_resources悬空。预算 charge 失败不改余额，stop_token仅表达协作取消。无任意服务查找和外部发送入口。

ReadServices<Reader> 只暴露 `const Reader& reader() const noexcept`，持有 `shared_ptr<const Reader>` 形式的受信已安装只读端口owner，reader()引用仅在包装器及其owner有效时使用；端口必须保证const操作只读，不能通过const方法返回可变状态后门。Reader是注册时固定、经过公开声明审核的领域只读端口，不是运行时可选任意服务类型。EditView<Provider> 只暴露 `Provider::EditPort& edit() noexcept` 和 `const AtomicDomainRef& domain()`；EditPort由provider固定为候选编辑接口，不包含 commit/Host/SQL/外部发送。后续领域包定义实际方法，本包两个小型 Reader/EditPort 夹具证明没有跨provider隐式转换。EditView仅借用当前受信Frame的EditPort，Frame的unique_ptr由同步协调作用域独占，必须覆盖视图和整个Handler调用；视图不能延长候选寿命或提取可变裸Provider/Frame。两种视图均禁止复制/移动以避免把调用借用视图存入异步闭包；并不声称能阻止恶意代码保存引用。

EffectContext只公开当前 TargetView、许可绑定、取消/预算和效果记录接收口；TransitionView只公开目标、before状态、生命周期世代、许可和受限转换口。业务报告可以报告真实效果失败，但不能通过这些上下文替换 issuer、提交状态根或关闭任意 Host。

精确函数形态为：

- Read：`Result<R> (*)(const A&, WorkContext&, ReadServices<Reader>&)`。
- PureCompute：`Result<R> (*)(const A&, WorkContext&)`，是 Read shape 的无读服务分支。
- StateEdit：`Result<R> (*)(const A&, EditView<P>&, WorkContext&)`。
- ExternalEffect：`EffectReport<R> (*)(const A&, EffectContext&)`。
- Lifecycle：`TransitionReport<R> (*)(const A&, TransitionView&)`。
- CandidateRead适配：`Result<R> (*)(const A&, const typename P::CandidateReadPort&, WorkContext&)`，A/R必须等于原 Read，P/domain匹配原子组。

注册工厂为这些精确函数指针提供单独模板重载（可加对应 noexcept 指针重载，不放宽参数/返回型）；不使用调用可转换性代替真实签名。捕获闭包、泛型或重载 callable 须先由作者提供明确签名的静态适配函数；不在本包实现通用 callable traits/类型擦除框架。Atomic验证拒绝异步/外部等待及未声明候选适配，PureCompute不能取得 ReadServices。

## 4. outcome.hpp：事实验证与成功投影

复用 [Outcome合同](outcome.md) 的字段和全部交叉引用规则；以下列出原生数据与受检入口，不改变 wire Schema。

```cpp
enum class EvidenceState { Volatile, Durable, RequiredRecordFailed, PersistenceUncertain };
enum class ExecutionPhase { Queued, WaitingResources, Running, WaitingChild,
    Finalizing, Suspended, Terminal };
enum class Application { NotApplied, Applied, PartiallyApplied };
enum class BusinessStatus { Succeeded, Failed };
template<class R> using ResultMaterial = Result<R>;
struct FactBudget { std::size_t max_facts, max_steps, max_text_bytes; };
class KnownFacts final {
public:
    static Result<std::shared_ptr<const KnownFacts>> create(
        std::span<const Fact>, FactBudget);
    Result<std::shared_ptr<const KnownFacts>> appended(
        std::span<const Fact> suffix, FactBudget) const;
    std::span<const Fact> values() const noexcept;
};
Result<void> validate_facts_append(const KnownFacts& previous,
                                  const KnownFacts& next);
```

Fact是封闭 `std::variant<CommitFact,PublishedFact,EffectFact,LifecycleFact,UnknownFact,ResolutionRecord>`，各项包含 FactId。CommitFact含CommitId/domain/revision/Memory或DurableCommitted；PublishedFact含CommitId/published_version；EffectFact含EffectId/Application；LifecycleFact含TransitionId/before/after/generation；UnknownFact含StorageCommit或ExternalEffect边界、强类型CommitId或EffectId引用及非空对账文本；ResolutionRecord含前序Unknown FactId、Applied或NotApplied结论及非空证据引用。全部文本受FactBudget控制，create/append复制并验证全部最终事实集合，不只检查最后一条；旧记录保留完整前缀，最终确定结论不能冲突。公开create只能证明新集合内部合法，不能授予替换现有执行事实的权限；`validate_facts_append(previous,next)`逐字段要求previous为next完整前缀，并重新校验next最终集合的引用与确定性。真实完成/查询owner在接收更新时，必须以自己当前保存的previous调用该函数，不能信任提交者提供的空previous，也不能因为next来自create/Outcome::validate就跳过。首份事实以owner尚无历史为前提；后续对账同样检查，Terminal不重开。测试使用create重建内部合法的空/截短/改写集合，然后验证它们不能替换已有事实；不需要本包实现生产执行表。

`EffectReport<R>`包含EffectId、`optional<Application>`（空表示未知）、BusinessStatus、非空拥有型证据文本列表、ResultMaterial<R>及未知时的对账边界。`TransitionReport<R>`包含TransitionId、ObjectId目标、before/after Name、generation、BusinessStatus、ResultMaterial<R>。报告是待验证材料；公开可构造报告不等于已验证Outcome。输出验证失败写入result_error，不能删除应用/转换事实。

九个候选载荷名称精确为 `ReadCompleted<R>, StateCommitted<R>, EffectResolved<R>, LifecycleResolved<R>, PlanCompleted<R>, FailedBeforeApply, CancelledBeforeApply, PartialCompletion, Indeterminate`。共有EvidenceState与KnownFacts，差异字段如下：

| 载荷 | 必需字段 |
|---|---|
| ReadCompleted | R（void为成功标记）、ReadOnly/Candidate scope |
| StateCommitted | CommitId、AtomicDomainRef、revision、published_version、R |
| EffectResolved | 已确定Application的EffectReport，不能用空Application |
| LifecycleResolved | 完整TransitionReport |
| PlanCompleted | R类型exports、拥有型有序StepSummary |
| FailedBeforeApply | failure_phase Name、Error、NoAppliedStateOrEffect声明 |
| CancelledBeforeApply | Error、取消先赢且未应用的决定见证 |
| PartialCompletion | 有序StepSummary、确定应用事实且至少必要一步非成功 |
| Indeterminate | 精确列出全部未解决Unknown FactId |

StepSummary包含 Name step_id、Succeeded/Failed/Cancelled 状态、是否必要、实际子结果成功/失败/未知分类及本地收尾是否完成；这些是协调器提供的纯验证材料，不是 Plan IR。不能只凭“必要步骤Succeeded”字符串绕过实际子结果和收尾检查。

```cpp
class PublicationProof { /* 只读抽象基类，protected构造 */ };
struct PublishedCommit { CommitId commit; AtomicDomainRef domain;
    std::uint64_t revision, published_version; };
class PublicationAuthorityPort {
public:
    virtual Result<std::shared_ptr<const PublicationProof>> attest(
        const PublishedCommit&) = 0;
    virtual Result<void> validate(const PublicationProof&, const PublishedCommit&) const = 0;
};
enum class RequiredRecordState { NotRequired, Pending, Recorded, Failed };
enum class RepairKind { CommitLedger, ReceiptReconcile, ManualReview };
struct RecordFailure {
    Error reason;
    bool writes_blocked;
    RepairKind repair;
};
enum class ApplyDecision { NotReached, CancelWon, ClaimWon };
struct BeforeApplyDecision {
    bool business_entered;
    ApplyDecision decision;
    bool no_application_proven;
};
struct FinalizationFacts {
    RequiredRecordState record_state;
    std::uint64_t local_work_remaining;
    std::uint64_t required_children_unsettled;
};
struct OutcomeConditions {
    std::optional<BeforeApplyDecision> before_apply;
    std::optional<RecordFailure> record_failure;
    FinalizationFacts finalization;
};
Result<void> validate_outcome_conditions(const OutcomeConditions& submitted,
                                         const OutcomeConditions& expected);
struct OutcomeValidation {
    PublicationAuthorityPort& publication;
    std::span<const std::shared_ptr<const PublicationProof>> proofs;
    FactBudget budget;
};
template<class R> class Outcome final {
public:
    using Candidate = std::variant<ReadCompleted<R>, StateCommitted<R>,
        EffectResolved<R>, LifecycleResolved<R>, PlanCompleted<R>,
        FailedBeforeApply, CancelledBeforeApply, PartialCompletion, Indeterminate>;
    static Result<Outcome> validate(Candidate, std::shared_ptr<const KnownFacts>,
                                    EvidenceState, const OutcomeConditions&,
                                    const OutcomeValidation&);
    const Candidate& value() const noexcept;
    const KnownFacts& facts() const noexcept;
    EvidenceState evidence() const noexcept;
    const OutcomeConditions& conditions() const noexcept;
    Result<void> revalidate(const OutcomeValidation&,
        const OutcomeConditions& expected_conditions,
        const KnownFacts& previous_facts) const;
};
```

Outcome构造私有，赋值删除；工厂不能接受可变fact别名。R的深拥有/只读保证沿受审TypeContract，不能通过C++反射“自动冻结”自定义R。需要异步存留的R同样满足受审拥有约束；void不用构造虚假R对象。所有成功发布事实（包括组合KnownFacts内的PublishedFact）都要经配置的publication authority逐项验证对应proof。attest必须查其自身已发布记录，不能收到任意PublishedCommit就签发；第三方proof、自填bool、Claimed或DurableCommitted均不能建立成功投影。D1.02测试authority保存小型真实测试发布状态，生产发布器留原提交包。

`RecordFailure`、`BeforeApplyDecision`、`FinalizationFacts`、`OutcomeConditions`均定义于outcome.hpp；ports.hpp仅引用它们，不反向包含ports。Outcome::validate显式接收OutcomeConditions并复制保存在不可赋值Outcome中，故障Error的详情沿Foundation不可变ErrorInfo owner持有；conditions()不返回可变材料。PublicationAuthorityPort引用及proof span只在工厂验证期间借用，不保存在Outcome或持久事实中。这里采用“接收端重新验证”协议：工厂返回的Outcome只表示在该次验证输入下成立，类型本身不是跨信任域通行凭据。真正完成owner及成功投影接收端必须用自己由组合根固定的publication authority和可信协调者取得的proof集合调用Outcome::revalidate，并显式传入owner自己已有执行决定/记录状态形成的expected_conditions和自己保存的previous_facts，全部成功后才能存入可信执行记录/发出完成；不能转用提交者指定的authority或因值已是Outcome就跳过。

revalidate先执行validate_outcome_conditions(submitted,expected)，逐项比较before_apply的存在性、business_entered、ApplyDecision与未应用证明，record_failure的存在性、错误码/详情、writes_blocked与repair，以及finalization的记录状态/剩余本地工作/必要child计数；然后以expected_conditions执行载荷与EvidenceState交叉验证，并调用validate_facts_append(previous_facts,facts())，最后完整核验所有直接或组合PublishedFact的proof。不复制/移动R，因而支持move-only R。expected_conditions必须由真正接收owner当前的执行决定及记录结果形成，previous_facts必须来自该owner已保存的历史；二者不能取自submitted Outcome或请求附带材料。没有历史时owner明确提供其自身的合法空集合。记录状态在提交与接收之间变化时，旧conditions应被拒绝并由可信协调者根据新材料重建投影，不静默接受陈旧计数。纯函数只核对材料，不凭请求创建可信执行决定；实现共用const纯验证逻辑，不能只检查变体名称。proof由接收端自己的已发布状态取得，缺失/不匹配即拒绝，不从业务声称的published字段即时签发。该协议不需要持久化进程authority指针，也不提前实现生产完成owner；测试完成owner固定自己的authority，先让伪authority构造形式合法StateCommitted，再证明它在真实owner.revalidate下被拒绝。真实已发布材料在真实端通过。无PublishedFact的结果同样必须核对expected_conditions：测试让伪端构造带CancelWon的CancelledBeforeApply，再以真实owner的NotReached/ClaimWon决定接收，必须由validate_outcome_conditions拒绝，即使publication重验无需任何proof也不能放行。不能只补发布authority检查而把取消胜出、记录失败或本地排空条件继续交由提交者自证。D1.05/后续协调者接入此强制重验后才有可信投影，不得把本包私有构造器误当成完整发布信任边界。

BeforeApply条件由接收方已有可信执行决定记录投影，不能把请求自填取消布尔值当胜出；纯验证器只证明所给可信材料自洽，不证明硬件/数据库真伪。FailedBeforeApply要求before_apply存在、business_entered=true、no_application_proven=true且KnownFacts无已应用/未解决未知；ClaimWon之后只有可信协调者确认确切未应用并给出证明时才允许失败。CancelledBeforeApply要求decision=CancelWon及no_application_proven=true，禁止NotReached/ClaimWon或等待超时替代取消胜出；同样拒绝任何既有应用/未知。其它载荷不得携带与实际事实冲突的BeforeApply条件。

PersistenceUncertain只配Indeterminate；已知DurableCommitted不降为Volatile。RequiredRecordFailed要求finalization.record_state=Failed、record_failure存在、writes_blocked=true及有效reason/repair；Failed不能配普通EvidenceState，非Failed不能携带record_failure。已知耐久提交的额外索引故障使用CommitLedger，有效果/未知而没有可修复提交账本时使用ReceiptReconcile，其余使用ManualReview；不能把普通观察错误填成记录故障。local_work_remaining非零时允许保存Finalizing期间的事实Outcome，但禁止PhaseConditions验证到Terminal。必要child未收尾时不能形成PlanCompleted；它们已产生的事实仍保留。ManualReview只是既有修复分流枚举，不增加本包人工验收要求。

`PhaseConditions`同样定义于outcome.hpp，字段为FinalizationFacts、可选RecordFailure、显式resume目标ExecutionPhase及可恢复/已移交owner的可信决定材料；phase验证与Outcome条件对同一执行必须一致。Terminal要求本地代码排空、必要child实际收尾或经可信owner转交、记录不为Pending；RequiredRecordFailed不抹掉已知提交，也不能让仍在运行的本地代码提前释放。具体所有者发放/移交由后续Runtime提供，本包不以一个业务可填写的bool实现权限仲裁。

`SubmitReply=variant<Rejected{Error},Accepted{ExecutionRef,Volatile|DurableAccepted}>`；`InvokeReply<R>=variant<Rejected,Completed<Outcome<R>>>`。接受DTO不签发执行身份、不创建任务表。`validate_phase_change(from,to,PhaseConditions)->Result<void>`验证既有迁移表及显式resume位置、记录状态、local_work/children排空或可信转交；Terminal不重开。可靠完成与Finalizing分开。

## 5. ports.hpp：所有权与窄生产协议

### 5.1 Executor 与完成

```cpp
class CompletionPort {
public:
    virtual Result<void> candidate_ready(Result<void> work_status) noexcept = 0;
};
class ReadyWork {
public:
    virtual ~ReadyWork() = default;
    virtual void execute() noexcept = 0;
};
class ExecutorPort {
public:
    virtual Result<void> submit(std::unique_ptr<ReadyWork> work) = 0;
};
class TerminalReceiver {
public:
    virtual ~TerminalReceiver() = default;
    virtual void terminal(ExecutionRef) noexcept = 0;
};
class CompletionLatch final {
public:
    explicit CompletionLatch(std::shared_ptr<CompletionPort>);
    Result<void> complete(Result<void>) noexcept;
};
```

ReadyWork是领域work接口，不是任意函数包装器。具体work在提交前已经拥有冻结输入、绑定owner、完成接收端；execute归一业务结果并把类型化Outcome存入自身指定执行owner，再发送无R的候选就绪信号。不是将Outcome擦为void*。工厂测试可使用一个具体R的完成owner见证实际结果保存。Latch用标准原子状态实现一次领取，重复complete返回DuplicateCompletion；不重复转交，候选接收端拥有关系在进入可能inline的submit前建立。

submit按值接收unique_ptr意味着调用时转交所有权；成功后executor负责恰好一次execute，允许在返回前完成并销毁。返回错误/抛出前必须销毁work且不留回调/别名，调用方不得再次delete或重新提交原指针。分配失败异常自然传播；已对外Accepted时底层拒绝必须保留ExecutionRef，由上层形成该执行的失败结果。

execute/candidate_ready/terminal均为noexcept边界；具体work在边界内捕获业务异常并形成明确框架故障，不能伪装可重试业务错误。接收端内部故障必须在越过noexcept前进入配置故障处理；未处理异常终止进程属于违反合同，测试用真实子进程证明。OOM处理不能再要求分配错误详情。完成计数、资源回收与终态接收分开：Latch成功只到Finalizing；本地work/必要child/记录未收尾不能调用terminal。D1.02仅提供Once工具及纯finalize条件验证，不实现任务表、等待器或drain调度器。

### 5.2 Atomic 与必要记录

```cpp
struct PreparedIdentity { CommitId commit; ReservationId reservation;
    AtomicDomainRef domain; std::uint64_t base_revision, lifecycle_generation; };
class PreparedCommit final {
public:
    static Result<std::shared_ptr<const PreparedCommit>> create(
        const PreparedIdentity&, std::span<const std::byte> receipt,
        std::size_t max_receipt_bytes);
    const PreparedIdentity& identity() const noexcept;
    std::span<const std::byte> receipt() const noexcept;
};
struct CommitReport { PreparedIdentity identity;
    /* KnownNotCommitted / DurableCommitted / Published / Indeterminate */
    CommitDisposition disposition; std::optional<Error> error; };
class CommitReceiver {
public:
    virtual void completed(CommitReport) noexcept = 0;
};
template<class P> class AtomicProviderPort {
public:
    virtual Result<std::unique_ptr<typename P::Frame>> begin(const AtomicDomainRef&) = 0;
    virtual Result<std::shared_ptr<const PreparedCommit>> prepare(
        typename P::Frame&, const PreparedIdentity&) = 0;
    virtual Result<void> commit(std::shared_ptr<const PreparedCommit>,
        std::shared_ptr<const ActionPermit>, std::shared_ptr<CommitReceiver>) = 0;
};
```

P是显式provider合同，Frame只由受信协调者持有；业务仅借用它派生出的EditPort/CandidateReadPort，不获得Frame/AtomicProviderPort。PreparedCommit复制身份及有界最小回执字节；不是通用Payload/任意根对象容器。生产provider的实际候选root/history/outbox材料由provider自身冻结owner持有并与PreparedIdentity精确绑定，不能把借用指针塞进回执字节。D1.02不实现这些材料编码。

prepare之前完成业务R验证、所有必要材料及容量预留；commit不再次运行任意业务验证。commit只向已装配许可authority消费匹配许可，接收端核对当前reservation再发布；接受/拒绝、inline回调和异常清理沿Executor同一所有权规则。纯 `validate_prepared_match(expected,actual)` 与 `validate_commit_report(current,report)`验证身份及重复/迟到状态，不能替代真实域串行化或授予发布proof。允许配置型/对象型两个provider测试工厂，不提供通用状态容器。

`RecordRequest`字段为ExecutionRef、可选CommitId/EffectId、必要记录种类Name、深复制有界最小回执字节、容量计数；`RecordReservation`是独立RAII容量owner。`RequiredRecordPort::reserve(const RecordRequest&)->Result<unique_ptr<RecordReservation>>`先预留，`record(unique_ptr<RecordReservation>, shared_ptr<const RecordRequestSnapshot>, shared_ptr<RecordReceiver>)->Result<void>`消耗容量owner，失败不残留callback。RecordReceiver收Pending/Recorded/Failed及原身份；最终状态验证拒绝迟到/重复，Pending不冒充Recorded。

`RecordReceiver`结果复用outcome.hpp的RequiredRecordState、可选RecordFailure及请求原身份；quiescent由FinalizationFacts.local_work_remaining是否为零判断，不另设可矛盾bool。协调者把该结果和实际执行计数形成OutcomeConditions，再交Outcome::validate；不得缺失故障材料仍生成RequiredRecordFailed。记录失败保留确定事实，普通观察错误不能转换为必要记录故障。无数据库连接、DB worker、writer循环或自动重发。

## 6. observation.hpp：拥有型摘要与授权枚举

```cpp
class ObservationVersion final {
public:
    static Result<ObservationVersion> create(std::uint64_t); // 从1开始。
    std::uint64_t value() const noexcept;
    Result<ObservationVersion> next() const noexcept; // 上限拒绝。
};
struct SummaryInput {
    ExecutionRef execution; OperationKey operation; PrincipalRef owner;
    std::optional<ExecutionRef> parent; ExecutionPhase phase;
    HostIncarnation host; ObservationVersion version;
    /* 下表的有界进度、事实和收尾投影 */
};
class ExecutionSummary final {
public:
    static Result<std::shared_ptr<const ExecutionSummary>> create(const SummaryInput&);
    const SummaryInput& value() const noexcept;
};
struct ListBudget { std::uint32_t page_size = 50; std::uint32_t scan_limit = 2000; };
struct KeysetPosition { HostIncarnation host;
    std::uint64_t upper_ordinal, before_ordinal; };
enum class PhaseSet { Nonterminal, Terminal, All };
struct ListRequest { PrincipalRef owner; PhaseSet phases; ListBudget budget;
    std::optional<KeysetPosition> position; };
struct ListedSummary { std::uint64_t listing_ordinal;
    std::shared_ptr<const ExecutionSummary> summary; };
struct ListPage { HostIncarnation host;
    std::vector<ListedSummary> items;
    std::optional<KeysetPosition> next; Name retention_scope; };
```

SummaryInput中进度为受检completed/total计数及Candidate/Published scope；事实摘要最多8项，每项仅强类型事实身份、种类与小状态；故障为Foundation纯码Error及必要记录/封锁/修复小枚举，不拉取Error详情或Outcome正文。summary不含Args/R/Payload、大文本或根owner。create深复制字段并验证同次host/version/phase/事实状态，调用者随后改写输入不改变摘要。小摘要预算固定8；页面上限200，扫描上限由已安装ObservationPort预算配置限制，默认2000，不能由请求增大服务端上限。

`ObservationPort`精确窄方法：`get_summary(const CallerView&,ExecutionRef)->Result<shared_ptr<const ExecutionSummary>>`；`list_summaries(const CallerView&,const ListRequest&)->Result<ListPage>`；`observe_changes(const CallerView&,const ObservationFilter&,shared_ptr<ObservationReceiver>)->Result<unique_ptr<ObservationLease>>`。ObservationFilter仅有有界ExecutionRef列表或规范owner二选一及非空topic集合；三topic为Progress/Phase/Fact。Receiver接拥有型小ChangeHint，可合并/丢失；lease析构封住新回调，已进入回调由receiver owner保活。未宣称强制撤回在途回调。Completion的TerminalReceiver是独立可靠通道，取消监听不能取消执行/丢完成。

ObservationPort持有组合根配置的CallerAuthorityPort：每次请求和每次输出前核对authority身份并重验当前权限；CallerView本身不是永久allow缓存。未知/无权以同一不可枚举存在性的拒绝类别返回，不能泄露隐藏数量。真实权限规则留D1.04，测试工厂按其真实小型授权表过滤。

`validate_summary_update(previous,incoming,UpdateKind)`返回Accept/Ignore/Resync或合同Error：同host同execution严格更新；同版完整快照可补部分hint但不能覆盖已有同版冲突事实；旧版忽略；不同host要求重建基础，不拿较小数字当回退；Terminal不能被非终态重开。对账可增加事实而不重开phase。

`validate_list_page(request,page,configured_budget)`核对page/scan预算、host、固定upper、strictly decreasing扫描位置、条目ordinal顺序及范围。实际候选条目含不对外持久化的listing_ordinal，夹具以有界扫描见证权限变化/删除/新增及空页推进；不能只凭返回DTO声称实现生产索引。KeysetPosition不是RPC cursor、授权票据或服务端句柄池，Control另负责完整MAC绑定与TTL；本包无SubscriptionId/HMAC/JSON/连接。

## 7. operation.hpp：从实际签名到冻结 typed 绑定

```cpp
struct DefinitionInput { OperationKey key; ContractDigest contract_digest;
    ExecutionRequirements execution; AtomicMode atomic_mode;
    /* Policy声明、Docs输入及其预算 */ };
template<class A, class R> class OperationDefinition final;
template<class A, class R, class Reader>
Result<OperationDefinition<A,R>> make_read_definition(
    Result<R> (*)(const A&, WorkContext&, ReadServices<Reader>&), const DefinitionInput&);
template<class A, class R>
Result<OperationDefinition<A,R>> make_compute_definition(
    Result<R> (*)(const A&, WorkContext&), const DefinitionInput&);
template<class A, class R, class P>
Result<OperationDefinition<A,R>> make_state_edit_definition(
    Result<R> (*)(const A&, EditView<P>&, WorkContext&), const DefinitionInput&);
template<class A, class R>
Result<OperationDefinition<A,R>> make_effect_definition(
    EffectReport<R> (*)(const A&, EffectContext&), const DefinitionInput&);
template<class A, class R>
Result<OperationDefinition<A,R>> make_lifecycle_definition(
    TransitionReport<R> (*)(const A&, TransitionView&), const DefinitionInput&);
```

候选读取必须通过单独受检工厂从已有Read定义派生新的不可变定义，不能直接自填AtomicMode：

```cpp
template<class P> struct ProviderContract; // 显式提供 static AtomicProviderKey key()。
template<class P, class A, class R>
Result<OperationDefinition<A,R>> with_candidate_read(
    const OperationDefinition<A,R>& read,
    Result<R> (*adapter)(const A&, const typename P::CandidateReadPort&, WorkContext&),
    AtomicProviderKey expected_provider);
template<class A, class R>
Result<void> validate_atomic_admission(
    const OperationDefinition<A,R>& definition,
    const AtomicDomainRef& group_domain,
    CppTypeToken group_provider_type);
```

P由调用者显式选定，A/R同时受原定义与真实适配函数签名约束；不符时不能通过模板替换/编译。with_candidate_read要求原定义shape=Read且为普通只读分支、非空adapter、ProviderContract<P>::key()与expected_provider精确相同、无外部等待/异步要求；拒绝PureCompute或已有不匹配适配的定义。它保留原OperationKey、A/R合同与Reader约束，生成私有候选适配见证（实际A/R及P/CandidateReadPort token、provider key、兼容模式）并与新快照冻结。它不公开或保存通用erased Handler，测试工厂自行持有原函数与适配函数；D1.03须在同一次注册中保有对应实际适配。

make_read_definition只接受AtomicMode::Incompatible；直接传CandidateRead（包括未绑定adapter）拒绝。make_compute_definition固定PureCompute；make_state_edit_definition从实际P形成StateEdit provider见证；其它shape不能自报原子兼容。with_candidate_read是普通Read取得CandidateRead标记的唯一产品工厂，描述DTO无修改标记/见证入口。

validate_atomic_admission必须在受信目标解析后以真实组domain调用：候选/StateEdit见证的provider key与CppTypeToken::of<P>()均须匹配组provider，domain_id/generation必须有效且与所借用候选视图的domain精确一致；真实Frame/视图的domain核对由相同纯验证辅助 `validate_atomic_domain(expected, actual)->Result<void>`完成。跨domain、跨provider、相同provider名字但不同P、缺候选见证、外部等待/异步均拒绝；PureCompute不取得任何domain读视图。定义绑定provider而不提前固定一个业务对象domain，具体domain在每次原子准入时核对，保持配置型与对象型provider同一合同。

禁止模板实参或DTO覆盖实际签名推导结果。函数指针必须非空；返回的定义只有冻结描述，含实际A/R token、TypeIdentity、shape、provider/Reader约束、AtomicMode、Contract/Policy/Docs及owner，不公开Handler。Policy输入只声明影响/所需权限/目标解析契约，不是凭据；Docs为受预算深复制冷文本。ReadServices/Provider类型同样记录token，避免错误上下文类型复用名字。

具体实现可在私有定义快照中存储签名见证，但不在本包建立可调用Handler表。D1.03注册器须把同一次受检工厂使用的Handler和定义一起安装，不能事后重新指定不匹配Handler；这个生产安装步骤不是D1.02新增通用注册端口。定义工厂本包即执行真实Contract检查；测试可对已知具体函数进行直接调用验证值合同，不能宣称已走D1.05 Invocation。

```cpp
struct OperationHandleTag {};
using OperationHandle = foundation::BoundHandle<OperationHandleTag>;
class DefinitionSnapshot; // 私有构造，不可赋值；只从上述定义取得const owner。
class BindingPort {
public:
    virtual foundation::RegistryId identity() const noexcept = 0;
    virtual foundation::RegistryGeneration generation() const noexcept = 0;
    virtual std::size_t size() const noexcept = 0;
    virtual Result<OperationHandle> find(const OperationKey&) const = 0;
    virtual Result<std::shared_ptr<const DefinitionSnapshot>> describe(std::uint32_t slot) const = 0;
};
template<class A, class R> class BoundOperation final {
public:
    const OperationKey& key() const noexcept;
    Result<void> revalidate() const;
private:
    /* 仅受检bind能构造；不公开Handler、可变token或无检查slot索引。 */
};
template<class A, class R>
Result<BoundOperation<A,R>> bind(std::shared_ptr<const BindingPort>,
    const OperationKey&, ContractDigest expected_contract, Shape expected_shape);
```

模板bind是产品工厂的必要私有访问（可采用类静态工厂再转发，避免friend）；不是测试friend。先find得到句柄，再 `resolve_slot(handle,identity,generation,size)`，成功后才describe；核对A/R token、稳定TypeIdentity name/version/digest、OperationKey、期望总契约摘要、shape及上下文约束。find不能把“找到某字符串”当已通过typed检查。两个不同Cpp类型故意共享TypeIdentity必须拒绝，不能到运行时reinterpret存储。

BoundOperation持有固定BindingPort和DefinitionSnapshot owner；revalidate每次按先身份/世代后槽位/描述检查。owner保活不豁免逻辑失效。Registry的真实线程快照协议由D1.03提供：identity/generation/size/describe在一个不可变目录owner内一致；不能同时任意修改这些值造成检查后使用竞态。测试工厂换代采用明确串行步骤；本包不声称这些多方法调用单独构成并发原子快照。

本包不提供生产invoke/submit管线。为固定Args拥有约束，公开 `validate_inline_args(const BoundOperation<A,R>&,const A&)->Result<void>` 与 `prepare_submit_args(const BoundOperation<A,R>&,A&&)->Result<unique_ptr<const A>>`；后者仅当AsyncInput<A>成立，先revalidate和validate再独占移交到const owner，不创建ExecutionRef/Accepted或调度。自定义A可达材料深拥有/冻结依赖其受审声明；不得误称unique_ptr<const A>自动冻结外部共享对象。后续Invocation的submit必须复用这些相同约束并再做授权/配额/接受。异步结果R的受审拥有验证同理。

## 8. 38项固定用例映射与交付边界

下表完全保留已审计划的38个名称；单项包含正例和拒绝控制，未运行、不计Passed。编译负例须有同环境正向控制和精确拒绝诊断；运行断言在Release有效。正式测试发现来自实际注册，不能读取expected制造发现。

| 固定用例 | 本文冻结的实施落点与必要反例 |
|---|---|
| T01.contracts.component_closure | 仅CoreContracts→Foundation→expected；加入Runtime/Data/SQL/Control/未知依赖的反例拒绝 |
| T01.contracts.public_include_boundary | 公开头独立闭包无JSON、连接、Document、Payload、通用Host入口；意外公开头/宏/编译条件变动被清单与声明检查捕获 |
| T02.contracts.type_contract_constraints | 合法Args、move-only R、void R可用；缺失/不符TypeContract、错误validator返回型拒绝；未声明拥有性默认Disallowed，标准借用类型及包含嵌套借用的合同不能Submit，可复制/可移动不豁免；经审查Owning carrier及深拷贝适配正例在来源销毁/改写后仍有效 |
| T02.contracts.typed_value_validation | 合法值通过；非法Args在业务调用前拒绝且调用计数为零；非法R阻止结果转交/提交，不谎称产生R的业务函数从未运行；不把夹具宣称完整Invocation |
| T02.contracts.read_shape | Read/Compute接受规定签名和Result<R>；错误参数/返回型、可变写上下文拒绝 |
| T02.contracts.state_edit_shape | 对应provider候选编辑可用且返回Result<R>；错误provider、缺EditView、返回形态错误拒绝 |
| T02.contracts.effect_shape | EffectReport保留发送身份及事实；以Result<R>/普通Error替代效果报告的编译反例拒绝 |
| T02.contracts.lifecycle_shape | TransitionReport保留before/after/generation；普通Error及任意对象转换签名拒绝 |
| T02.contracts.context_capability_boundary | 四Context只具有授予能力；EditView.commit、Host/任意服务提取、直接SQL/发送后门编译失败 |
| T02.contracts.atomic_read_adapters | candidate_read同provider候选与pure_compute正例；普通只读服务冒充候选或纯计算偷读活动状态入口拒绝 |
| T02.contracts.atomic_async_rejected | 同域同步兼容描述通过；Atomic内外部等待、异步派发、缺provider或跨domain描述拒绝 |
| T05.contracts.operation_exact_identity | 名称大小写/规范版本精确相等；空/非规范版本、wildcard/latest、溢出或隐式截断拒绝 |
| T05.contracts.typed_binding_fingerprint | Handler实际Args/R推导token与稳定身份/版本/digest均匹配才成功；错类型、错shape、同名不同精确版本/指纹拒绝；两个不同C++类型故意复用全部稳定身份仍因token不符拒绝，受检定义不能伪报Handler类型，业务不能自行构造token或提取Handler |
| T05.contracts.binding_generation_before_access | 当前registry世代和槽位成功；其他registry/旧世代/空或越界句柄在取定义前拒绝，访问计数见证顺序 |
| T05.contracts.binding_owner_lifetime | 绑定/异步工作持有规定owner避免悬空；逻辑失效世代仍拒绝，不能因owner保活继续调用；无裸Handler或跨生命周期借用入口 |
| T05.contracts.execution_identity_separation | ExecutionRef/PrincipalRef沿正式Tagged128；重启观察Host改变不改业务ID，RuntimeEpoch/registry handle不能进入持久身份表示 |
| T06.contracts.outcome_closed_variants | 九类最小合法构造均可用；未知变体、缺必需字段、非法EvidenceState组合拒绝 |
| T06.contracts.known_facts_append_only | 合法追加/对账保留旧Unknown；删除、重写、重复fact_id、悬空引用和后追加相反确定事实拒绝 |
| T06.contracts.publication_proof_required | 匹配已发布报告可形成StateCommitted；permit/Claimed/DurableCommitted/伪自填发布标记不能形成成功投影 |
| T06.contracts.effect_report_preserves_fact | Applied/PartiallyApplied与结果错误并存时保留EffectResolved；未知为Indeterminate，不能降为FailedBeforeApply |
| T06.contracts.lifecycle_report_preserves_state | 成功转换和已进入Failed的失败转换均保留实际after；失败抹回before、错generation拒绝 |
| T06.contracts.before_apply_and_composite_proofs | 可证明未应用的失败/取消与纯读Plan成功合法；等待超时冒充取消、已有应用仍BeforeApply、必要子未定或未知被掩盖拒绝 |
| T06.contracts.reply_phase_completion_separation | Accepted无最终R，Finalizing可保留确定事实但未Terminal；Suspended/Terminal边界与必要记录故障投影不能伪报完成 |
| T06.contracts.executor_inline_ownership | 接受后owner有效且允许返回前inline完成；接收记录未准备、重复销毁或借栈回调由合同控制例检出 |
| T06.contracts.executor_reject_exception_cleanup | 提交拒绝/抛出不保有work/迟到callback；已对外Accepted不抹身份；work和接收端异常边界不遗留工作 |
| T06.contracts.completion_once_and_drain | 完成候选和可靠终态各按合同一次；重复/迟到故障工厂被检出，不重复发布/释放；尚有本地work不能假drained |
| T06.contracts.atomic_prepared_identity | 冻结材料/正确reservation和CommitId可接受；借用后改写、错base/provider/domain/世代及重复迟到报告拒绝 |
| T06.contracts.record_evidence_boundary | 容量预留/Recorded结果及确定未提交合法；容量拒绝无提前消费，Unknown保留边界，必要记录失败不抹提交，普通观察错误不伪装记录失败 |
| T19.contracts.summary_owned_snapshot | summary是同版拥有投影，不拼接phase/fact/version；来源销毁后仍有效，不读取大Args/R正文 |
| T19.contracts.observation_host_version | 同Host严格更新合法；溢出不回绕，旧Host响应/旧版本/Terminal重开拒绝，同版完整快照可修复部分提示 |
| T19.contracts.observation_budgets | 页/扫描/事实摘要在既有限额内成功；零或越限页、预算算术溢出与无限pin要求明确拒绝 |
| T19.contracts.authorized_owner_filter | 可信self/委托夹具只输出有权小摘要；自填owner、失效授权视图或跨主体查询不得泄露对象/总数 |
| T19.contracts.completion_independent_of_observation | 观察提示全部丢失/撤销监听仍可靠完成一次；观察背压不能改变Outcome或取消执行 |
| T19.contracts.live_keyset_progress | 固定上界/稳定ordinal分页；新条目/删除/权限变化按live_keyset说明，空页仍推进；重复位置和串Host状态拒绝 |
| T24.contracts.public_headers | 六个最终受审公开头各自真实包含编译，MSVC宏环境和固定expected/异常模式可消费；缺传递依赖明确失败 |
| T24.contracts.installed_component_consumer | BUILD_TESTING=OFF安装并搬迁，仅请求CoreContracts即可编译运行typed/Outcome/端口消费者；不借用源树/缓存路径 |
| T24.contracts.install_pruning_rejected | 隔离安装移除Foundation/expected后真实编译失败；Runtime/Data等未请求或未实现组件不意外可用，不以安装全部依赖伪造裁剪 |
| T24.contracts.port_factory_contract_manifest | 合法测试工厂运行固定共同集合；改合同版本/实现摘要/删必需项/用capability关闭必需规则拒绝，故障工厂不被登记qualified |

其中 `type_contract_constraints` 的深拷贝carrier正例在原shared_ptr指向材料改写后仍保持冻结副本，标准借用/未声明嵌套合同的反例被拒绝；不要求C++20自动检测作者错误声明的任意间接可变别名。`typed_binding_fingerprint` 实测冻结定义不能赋值/改token，以及实际函数签名与稳定同名Cpp类型碰撞；`publication_proof_required`包含自建authority先构造StateCommitted、再转交真实完成owner时完整revalidate拒绝的反例，同时测试伪proof直接送真实authority被拒绝；`authorized_owner_filter`包含自建issuer/同字段伪凭据在真实配置端口下被拒绝。`atomic_prepared_identity`与`summary_owned_snapshot`在来源改写/销毁后核对快照仍一致。`known_facts_append_only`另含create得到内部合法空/截短/改写集合却不能替换owner既有事实的反例；`before_apply_and_composite_proofs`包含伪端自填CancelWon先构造CancelledBeforeApply、转交真实owner后因其自有决定不符而拒绝的反例。上述是既有条目的完整子断言，不增加测试名称。

安装最终登记六头、CoreContracts→Foundation→expected闭包与Implemented阶段；搬迁安装消费者不借源树/cache，Runtime可用标记仍false。继承每配置CTest发现、ASan运行库环境和原始父子进程日志归档。端口工厂合同版本固定为 `ock.core-contracts.ports/1`，共同规则不能被capability关掉；D3.01真实Executor专项仍留后续包。

内部规格审核重点确认：显式函数指针适配的首版范围、ReadServices<Reader>窄模板视图、authority实例及实际发放集合的强制核对、私有冻结对象对mutable alias的阻断、无生产invoke/submit时拥有性准备入口与后续同约束衔接。审核提出必要变更时先修正文档并重新绑定SHA，再开始代码反例；不把本稿当作已经通过审核。用户已授权AI规格/代码自我复核与自动验收，不新增人工批准或虚构human记录。

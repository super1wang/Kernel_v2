# D1.04 内存授权与许可API草案

状态：**PendingReview**。本文件尚未获独立AI规格审核，不允许据此开始行为实现或声明Passed。只具体化架构v3.3 A06.1/A10/A17.4–A17.6和执行卡D1.04；不改变CoreContracts公开签名、RPC方法、身份编码或协议版本。

内部命名空间拟为`ock::runtime::policy`，头为`packages/runtime/policy/policy.hpp`，内部库`ock_policy_internal`仅依赖`OCK::CoreContracts`闭包，不安装，不使SDK Runtime可用。D1.04不依赖Registry查找：操作政策来自组合根可信安装的精确政策目录，D1.05再连接已冻结Registry。认证/目标/观察元数据和传输适配器是显式装配端口，没有动态ServiceLocator。

## 1. 基础值、预算与错误

沿用`contracts::Result/Error/PrincipalRef/OperationKey/ContractDigest/CallerView/CallerGrant/TargetView/ActionPermit/PermitBinding/ExecutionRef/ExecutionSummary/ListRequest/ListPage/ObservationFilter/KeysetPosition`；不新增第二套公共许可或caller。内部StoreId、ConnectionId、ActionId、WatchKey使用带类型的非零128位身份；它们不作为可持久化或RPC身份，不叫协议SubscriptionId。身份由对应发行者私有单调64位计数编码，不复用，耗尽永久拒绝新发行。所有单调世代从1起，不回绕。

```cpp
using TimePoint = std::chrono::steady_clock::time_point;
struct PolicyBudget {
  std::size_t principals=128, rules=4096, targets=4096, sessions=128;
  std::size_t active_actions=4096, active_watches=256, members=256;
  std::size_t declarations=32768, text_bytes=1048576;
  std::size_t credential_bytes=8192, queued_frames=1024, queued_bytes=1048576;
  std::size_t frame_bytes=16384, page_size=200, scan_limit=2000;
  std::chrono::milliseconds session_ttl{3600000}, action_ttl{30000};
  std::chrono::milliseconds page_ttl{300000}, queued_ttl{30000};
};
enum class PolicyErrc : std::uint32_t {
  InvalidInput=1, InvalidOwner, InvalidAuthority, AuthenticationFailed,
  SessionClosed, Expired, Denied, TargetUnavailable, PolicyNotInstalled,
  ContractMismatch, InvalidGroup, InvalidPermit, AlreadyConsumed, Cancelled,
  CursorInvalid, UnsupportedDelegation, BudgetExceeded, IdentityExhausted,
  GenerationExhausted, Busy, TransmissionUnknown
};
```

使用独立`ock.policy` ErrorDomain，错误正文不包含credential、隐藏主体/对象、查询总数或订阅存在性。未知/无权目标对不受信请求统一为TargetUnavailable；跨连接/未知/旧世代退订统一`removed=false`。内部诊断可区分原因，但有界且不能随公共错误泄露。

所有限制必须为正且有实现可表示上限；TTL checked转换/相加，不接受负数、无限期限或溢出。服务器确定有效deadline为认证、委托、请求上限和配置TTL的最小值，`now >= deadline`即过期。ClockPort使用steady_clock语义；测试钟不得导致公共Context的真实steady_clock检查被绕过。

容器数量、单字段及总文本在复制前checked累加，覆盖所有Name、版本、规则、成员、字段、owner、目标及嵌套scope；真实适配器返回的额外拥有型材料在入库前再次检查。不得因为输入vector被move就接管仍有外部元素别名的缓冲。规则仅含自有值和vector，不允许`shared_ptr<mutable vector>`、span或回调作为规则谓词。外部显式端口/资源owner要求非空且`use_count()>0`；不声称能验证恶意no-op deleter。

身份、记录与队列容量先预留，再一次提交；认证失败、预算失败、异常不得留下半会话、半permit或半监听。仲裁内不进行可抛分配、大对象析构或业务回调；预构造/销毁在外，计数、当前记录检查及最终状态变化在内。分配异常可传播，不能伪成功；不得因诊断构造失败复活旧授权。

## 2. 可信配置、认证与会话隔离

```cpp
enum class PrincipalKind { User, Service };
enum class AccessUse {
  Invoke, Catalog, GetSummary, ListSummary, Wait, CancelExecution,
  ReadResult, ReadLog, ReadAsset, Subscribe
};
enum class SummaryField { Identity, Owner, Parent, Phase, Progress, Facts };
struct OperationSelector { OperationKey operation; ContractDigest contract; };
struct ScopeRule {
  AccessUse use;
  std::optional<OperationSelector> operation; // Invoke必须有；其他用途不得充当通配符
  std::vector<Name> permissions;
  std::vector<foundation::ObjectId> targets;
  std::vector<PrincipalRef> owners;
  std::vector<SummaryField> fields;
};
struct DelegationInput {
  std::vector<ScopeRule> rules;
  TimePoint deadline;
  bool allow_redelegation=false;
};
struct AuthenticationAttempt { std::vector<std::byte> credential; };
struct AuthenticatedIdentity {
  PrincipalRef principal;
  PrincipalKind kind;
  std::optional<PrincipalRef> delegated_by;
  DelegationInput ceiling;
  TimePoint deadline;
};
class TrustedAuthenticationPort : public contracts::PortLifetime {
public:
  virtual Result<AuthenticatedIdentity>
    authenticate(const AuthenticationAttempt&) = 0;
};
class ClockPort : public contracts::PortLifetime {
public: virtual TimePoint now() const noexcept = 0;
};
```

`AuthenticatedIdentity`只作为**已配置可信认证适配器的返回值**；并无接收该DTO的公开会话创建重载。PolicyStore创建时固定authenticator，任何请求不能替换它、传入自己实现的authority或提供`role/trusted/approved`获得事实。D1.04实际提供确定性凭据验证消费者，不声称已实现OS/Named Pipe认证。认证适配器不得从请求标签直接回填Principal；测试用预置credential→身份表及不匹配控制。credential限量后同步传入适配器，不进入普通日志或冻结会话描述。

```cpp
struct PrincipalPolicyInput { PrincipalRef principal; std::vector<ScopeRule> rules; };
struct OperationPolicyInput {
  OperationSelector operation;
  std::vector<Name> required_permissions;
  std::vector<ScopeRule> module_rules;
};
struct TargetPolicyInput {
  foundation::ObjectId target;
  std::uint64_t lifecycle_generation;
  std::vector<ScopeRule> rules;
  std::shared_ptr<contracts::ResourceLease> lifetime_owner;
};
struct PolicyConfiguration {
  std::vector<PrincipalPolicyInput> principals;
  std::vector<OperationPolicyInput> operations;
  std::vector<TargetPolicyInput> targets;
};
class PolicyAdministration; class SessionAuthority;
struct PolicyAssembly {
  std::shared_ptr<PolicyStore> store;
  std::unique_ptr<PolicyAdministration> administration;
};
class PolicyStore final {
public:
  static Result<PolicyAssembly> create(
    PolicyBudget, const PolicyConfiguration&,
    std::shared_ptr<TrustedAuthenticationPort>, std::shared_ptr<ClockPort>,
    std::shared_ptr<TrustedGroupDigestPort>,
    std::shared_ptr<ExecutionAccessSourcePort>);
  Result<std::shared_ptr<SessionAuthority>> open(
    const AuthenticationAttempt&, const DelegationInput& requested);
};
```

PolicyStore/Administration/SessionAuthority不可复制、不可move；私有构造，不公开记录或可变集合。Administration只由create交给组合根，不出现在模块Context、请求处理DTO或SessionAuthority中。其具体写入口为`replace_principal_policy(const PrincipalPolicyInput&)`、`replace_operation_policy(const OperationPolicyInput&)`、`replace_target_policy(const TargetPolicyInput&)`、`retire_target(ObjectId)`、`set_lifecycle(ObjectId,uint64_t)`；更新使用完整新快照，不允许以裸generation赋值代替实质政策。`set_lifecycle`只接受严格递增值，供可信领域协调者报告事实；不获取资源锁，不替代领域生命周期协议。

四方均为明确allow集合，缺省deny。对每项操作、每个实际目标、每个所需permission分别求`主体ACL ∩ 当前会话委托 ∩ 可信安装模块政策 ∩ 该目标当前规则`。不同来源不能求并；同一来源多条匹配规则可形成该来源的有限allow集合，但不能用一条宽规则补另一个来源缺项。空targets/owners/permissions/fields分别表示空集合，**从不表示任意**；不需要的维度按use规则忽略，相关维度缺项即拒绝。非Invoke用途operation为空只表示该明确用途，不匹配Invoke。所有列表去重、规范化、范围有界，未知enum拒绝。

Invoke所需permissions只从精确`OperationSelector`安装项读取，请求不携带“我需要哪些权限”。查询/目录等use对应所需permission名称由安装政策显式指定，不由owner或字段列表产生授权。没有隐式administrator旁路；代表其他owner读取需要四方共同覆盖该owner。目标本身的lifetime_owner只保活，不增加ACL。

每次open发行独立ConnectionId和固定SessionAuthority，即使Principal相同也不共用caller发行者。请求scope与认证ceiling及主体政策求交，只能收缩；`allow_redelegation=true`首版明确UnsupportedDelegation，不隐式扩展委托链。ServicePrincipal也必须通过已固定的可信认证入口得到`kind=Service`及独立期限/政策；不得将断线用户session转为服务身份。

```cpp
class VerifiedCaller final {
public:
  const contracts::CallerView& view() const noexcept;
  std::shared_ptr<contracts::CallerAuthorityPort> authority() const noexcept;
private: // 私有配对CallerGrant、真实会话owner；不公开grant提取入口
  friend class SessionAuthority; friend class ActionAuthorization;
};
class SessionAuthority final : public contracts::CallerAuthorityPort,
                               public contracts::TargetAuthorityPort {
public:
  Result<VerifiedCaller> verify(const contracts::CallerDescription&);
  Result<std::shared_ptr<const CallerGrant>> authenticate(const CallerDescription&) override;
  Result<void> validate(const CallerGrant&) const override;
  Result<std::shared_ptr<const TargetView>> resolve(const CallerView&, ObjectId) override;
  Result<void> validate(const TargetView&, const CallerView&, ObjectId) const override;
  Result<void> restrict_delegation(const DelegationInput&); // 仅当前scope子集
  Result<void> close();
  Result<std::shared_ptr<ActionAuthorization>> prepare(
    const VerifiedCaller&, const ActionRequest&);
};
```

CallerDescription是投影声明，authenticate必须与该已认证会话实际principal/delegated_by和服务器标签完全一致，不把tags当权限。私有Grant对象、原始对象地址及会话发行记录一起检查；相同字段的自制Grant拒绝。verify内部取得该真实Grant并经现有CallerView::check配对；既有authenticate公开虚函数不能被绕过，会做同样核验。

所有入口核对view属于当前固定CallerAuthority且当前会话未断线、未过期、委托世代仍匹配。A/B同主体互换CallerView、TargetView、VerifiedCaller、ActionAuthorization均拒绝；不靠Principal相等判同连接。close只收回该连接授权与观察资源，不取消其他连接或业务执行。ACL政策、会话委托、生命周期保持不同记录与单调世代；它们只在短授权仲裁中读取/比较，不合成一个资源锁或生命周期计数。

## 3. 真实目标、精确组材料及许可

resolve只从可信目标目录找真实ObjectId，返回私有发行的TargetView，记录对象实例身份、生命周期世代和该会话caller配对。目标删除再建同ObjectId仍发行新实例身份并增加生命周期世代；旧视图不复活。无权/不存在同响应。多目标全部预解析、规范化并保有owner，不能因第一个目标通过就忽略其他目标；准入后不再查询UI选中状态。

```cpp
struct MemberRequest {
  OperationSelector operation;
  std::vector<ObjectId> targets;
};
struct ActionRequest {
  OperationSelector envelope; // 单操作=本操作；组=可信安装的batch/组操作
  ObjectId anchor_target;     // PermitBinding唯一target的真实锚点
  std::vector<MemberRequest> members;
  TimePoint requested_deadline;
};
class GroupSnapshot final {
public: // 只有不可变查看；由prepare独立拥有并规范化
  std::span<const MemberRequest> members() const noexcept;
private: friend class SessionAuthority;
};
class TrustedGroupDigestPort : public contracts::PortLifetime {
public:
  virtual Result<ContractDigest> fingerprint(const GroupSnapshot&) = 0;
};
class ActionAuthorization final : public contracts::PermitAuthorityPort {
public:
  Result<std::shared_ptr<const ActionPermit>> issue();
  Result<std::shared_ptr<const ActionPermit>> issue(const CallerGrant&,const PermitBinding&) override;
  Result<PermitBinding> current_expected_binding() const;
  Result<void> consume(const ActionPermit&,const PermitBinding&) override;
  Result<void> cancel();
};
```

prepare首先校验真正VerifiedCaller和精确安装的envelope政策。单操作必须正好一个相同operation成员，anchor必须在其显式目标集合中；组envelope必须是安装目录显式声明允许分组的操作（OperationPolicyInput补`bool permits_group=false`），也单独授权其anchor。成员保留顺序且保留重复步骤，**不得去重成员而漏验重复执行**；每个成员targets去重排序、非空、有界。每个成员精确operation版本/contract与其全部真实目标都重新授权，批次权限不代替成员权限。

GroupSnapshot完整保存envelope、anchor、成员序号、每成员精确OperationSelector及规范化目标；其members查看只作描述，不是可替换构造参数。TrustedGroupDigestPort固定于create、在仲裁外调用，对完整canonical材料产生ContractDigest；本包不改变PlanDigest/Intent指纹的既有定义，不提供客户端自报group摘要字段。确定性适配器须对成员/顺序/目标/精确契约变化产生差异，且有强制摘要碰撞反例：权限安全依赖**私有原始完整材料及其发行归属**，不能只比较摘要相同。生产摘要后端是否装配如实报告，不把确定性夹具声称为已认证密码学实现。

prepare在仲裁内以当前政策核对仲裁外准备的全部版本；期间有更新则拒绝或重新进行有界准备，不能偷用旧allow。返回的ActionAuthorization是**固定于一个会话、一个完整准备动作的PermitAuthorityPort**，不可复制/移动；没有万能“由DTO构造ActionAuthorization”。它保存所有真实目标视图/实例/lifecycle、操作政策revision、主体权限generation、委托generation、anchor、摘要和统一有效deadline。原始CallerGrant保留在私有owner中。无公开可写组、目标或issuer集合。

`PermitBinding.target`只投影anchor，不代表组内只有该目标；消费必须逐成员逐目标核对私有全集，包括未体现在公开DTO中的生命周期及授权规则。`permission_generation`投影PolicyStore的授权修改序号；主体/模块/目标ACL任一更新使该序号递增（保守失效其他动作允许），但仍保留并检查各来源记录，不把它与生命周期混为一体。`lifecycle_generation`投影anchor当前世代，其他目标世代在私有记录中检查。撤销后重授也使用新generation，旧许可不能复活。

current_expected_binding只从ActionAuthorization所绑定的真实接收动作及当前可信记录构造expected；不得从待消费permit.binding取回expected。真实接收者必须固定持有本动作的ActionAuthorization、caller/target authorities与原始permit，不能把另一ActionAuthorization或同字段permit装入该上下文。公开issue(grant,binding)重载也只接受该实例原始grant及当前expected；同一ActionAuthorization最多发行一个permit（重复issue返回同一未消费owner），消费/取消后不得再发行。

发行/消费以私有具体permit类型、原始对象身份、该ActionAuthorization发行记录、完整准备材料共同验证。一次状态为`Prepared → Issued → Consumed`，也可在消费前变为Cancelled/Invalidated/Expired；终结状态不复位。消费同时校验配置authority、原始permit、independently expected、当前会话/四方政策/全部生命周期/期限/取消。双线程消费只一个成功。

cancel与consume、撤权、委托收缩、会话close和目标事实更新都进入同一短仲裁。cancel获胜则Issued不能消费；consume先获胜则cancel只能报告已晚且不抹除已获准尝试。现有PermitBinding不新增stop_token：内部cancel位属于该ActionAuthorization；D1.05可将stop请求接到此同步仲裁入口，完成cancel调用才表示取消已线性化，不能用回调尚未执行的外部token自称已赢。cancel()对已消费返回AlreadyConsumed，对重复已取消幂等成功。

consume成功只表示一次效果尝试获准，不代表Commit/Effect/硬件已经应用；后续失败不能退回Issued或据此重发。资源和ActivityLease不参与授予许可。授权锁只完成固定有界检查/Issued→Consumed，任意实际提交和设备业务调用在外，并由其既有协议处理事实。

## 4. 查询、分页与观察授权窄接入

不实现execution索引、日志数据库、RPC或完整ObservationPort后端。本包提供真实授权包装和确定性接收消费者；可信元数据源与策略对象固定装配，源不能来自请求DTO。

```cpp
struct ExecutionAccessInput {
  std::shared_ptr<const ExecutionSummary> summary;
  std::vector<ObjectId> actual_targets;
};
struct AccessScanRequest { ListRequest list; };
struct AccessScanPage {
  std::vector<std::pair<std::uint64_t,ExecutionAccessInput>> candidates;
  std::optional<KeysetPosition> next_scan;
  HostIncarnation host;
  Name retention_scope;
};
class ExecutionAccessSourcePort : public contracts::PortLifetime {
public:
  virtual Result<ExecutionAccessInput> find(ExecutionRef) = 0;
  virtual Result<AccessScanPage> scan(const AccessScanRequest&) = 0;
};
class PageBinding final { /* 自有值，私有构造；无服务器cursor登记或pin */ };
struct AuthorizedPage {
  ListPage page;
  std::optional<PageBinding> continuation;
};
class ObservationAuthorization final {
public:
  Result<std::shared_ptr<const ExecutionSummary>> get(
    const VerifiedCaller&, ExecutionRef, AccessUse);
  Result<AuthorizedPage> list(const VerifiedCaller&,const ListRequest&,
    const std::optional<PageBinding>& continuation);
  Result<std::unique_ptr<WatchAuthorization>> subscribe(
    const VerifiedCaller&, const ObservationFilter&);
};
```

ObservationAuthorization由SessionAuthority私有创建并绑定该会话与固定源；SessionAuthority提供`observations()`返回其owner，调用者不能替换source。get的use只接受GetSummary/Wait/CancelExecution/ReadResult/ReadLog/ReadAsset相应授权检查；后三类**不返回大正文**，只返回表示该执行的最小授权摘要或拒绝。能读摘要不自动能取消或取结果；实际cancel/结果读取仍由后续受控消费者接该明确use，并再次在效果/发送起点检查，返回摘要不是长期票据。

元数据源返回完整已拥有的ExecutionSummary和实际目标绑定；核对execution、host、owner、相位、序号、目标非空/唯一及预算，不能把请求owner当对象owner。可见字段取四方fields交集，创建独立最小摘要；必需身份/phase字段无权限时隐藏整项，而不是泄露裸执行ID。optional owner/parent和facts/progress仅在授权时投影；现有SummaryInput有必需owner，因此首版不能授权owner字段时隐藏整项，不伪造另一owner。不包含Args、结果正文或资产内容。

ListRequest.owner为显式过滤，self在可信会话层解析为实际Principal；其他owner需要四方明确覆盖。先授权owner范围，再调用bounded source.scan；源返回候选每项在同次输出前按当前政策逐项重验，隐藏项不计可见数、不返回身份/错误，响应无总数。扫描限制、页大小和下降keyset检查沿D1.02合同，空可见页也必须按实际扫描位置前进；不会声称一致快照。源必须是受限已有索引接口的确定性消费者，不以本包名义建立新生产索引。

PageBinding内部保存StoreId、ConnectionId、会话委托generation、权限generation、规范化owner/phase/filter、host及可用restore身份、upper/before和deadline；不可由请求构造。每次list续页精确匹配并核对当前世代/期限，变化返回CursorInvalid/Expired，不静默重开。它是本进程调用链的自有**授权约束值**，不登记服务器cursor对象/句柄池、不pin结果、不具有网络完整性保护。真实`ock.execution.list/1` opaque cursor、canonical envelope及HMAC仍留D2.04；未来解码适配器必须先验证MAC再调用等价的授权约束校验，不能序列化一个C++地址充当cursor。本包不宣称生产cursor协议实现。

subscribe验证整个规范化filter/topics及配额；execution集合逐项检查，无权/未知统一失败，任一失败不安装部分Watch。owner模式授权整个明确owner范围，之后每条事件仍验证实际目标和字段。WatchAuthorization为私有不可伪造的连接内资源owner，包含内部WatchKey/generation及规范化filter；不含RPC SubscriptionId、通知sequence或业务执行owner。协议pending-ack屏障、通知排序/gap编码留D2.04；确定性消费者只证明在watch有效前不接受排入、失败无资源、发送前重验。

## 5. 撤权与真正开始传输的短仲裁

本节是D1.04必须实际验证的授权边界，不得实现成`if (allowed) post(send)`。队列不保存可永久复用的allow=true。

```cpp
enum class StartResult { NotStarted, Started, Unknown };
class PreparedTransmission final { /* 私有拥有型预编码最小投影与已预留容量 */ };
class TransmissionStartPort : public contracts::PortLifetime {
public:
  virtual StartResult start_now(PreparedTransmission&) noexcept = 0;
};
class SendCoordinator final {
public:
  static Result<std::unique_ptr<SendCoordinator>> create(
    std::shared_ptr<SessionAuthority>, std::shared_ptr<TransmissionStartPort>);
  Result<void> enqueue(const WatchAuthorization&, const contracts::ChangeHint&);
  Result<StartResult> start_next();
  Result<bool> unsubscribe(const WatchAuthorization&);
};
```

SendCoordinator只给可信观察发送适配器装配；请求不能提供TransmissionStartPort或PreparedTransmission。Watch属于同一固定会话且仍有效才可enqueue。入队前校验source中对应真实execution/目标、topic/filter、当前字段授权；保存拥有型最小投影与该原始会话/Watch/目标实例身份，不保存完整结果pin。预算、预编码、队列和发送端容量预留均在仲裁外完成；入队最终提交再次核对当前世代，过期/撤权期间失败无半帧。

PreparedTransmission私有保存精确字节与不可变投影归属，发送端只能消费同一个对象；不能在校验后替换成另一payload。预编码内容在队列等待期间不可增加字段；若当前字段权限缩小，丢弃旧帧，不“校验新投影却发送旧字节”。队列合并只能在相同会话/Watch/执行/主题范围使用新自有投影，保留待发送状态并重新绑定；不跨连接复用内容。完整notification sequence/gap调度仍是后续协议层职责。

start_next先在锁外准备所有只读访问材料并预留接收容量，然后在与撤权/close/退订相同的短仲裁内：核对当前会话、Watch、每个真实目标当前实例/ACL/lifecycle、字段和deadline → 若无权则Queued→Dropped → 否则调用固定可信start_now。**线性化点是start_now实际将首个字节交付既定接收介质的事件，且在该次仲裁返回/允许撤权线性化之前已经发生。** 单纯设置Started标志、移入另一个异步队列、返回send token或稍后任意执行的lambda都不是这个事件。

start_now是唯一受审的窄传输起点，不是任意业务回调：必须使用预持有缓冲/预留容量、同步非阻塞、无动态分配/任意析构、不重入PolicyStore或调用模块代码，不允许将工作推给任意未仲裁worker后提前报Started。执行固定有界的非阻塞交接；不能持授权仲裁等待完成I/O。若后端无法满足，不能装配为本接口。本包提供真实预分配**内存字节sink**作为确定性消费者：Started前至少一个被授权字节已写入接收槽，并在同一受控测试中核对字节内容/长度；不把bool状态写入当证据。此测试只证明该授权/发送接点，不宣称已实现OS Named Pipe或生产网络栈。

- NotStarted：能证明零字节越过起点，容量不足/明确零字节失败时保持Queued或按期限丢弃；再次尝试必须重新完整授权。
- Started：记录本帧越过真实起点，不返还Queued；迟到撤权/退订不能声称撤回已开始传输的字节。剩余传输受该已开始帧的既有有界传输规则约束，不夹带下一帧。
- Unknown：后端不能证明是否已有字节，进入隔离/关闭连接清理，不自动重试该帧，不伪称未发送，也不阻断业务执行收尾。noexcept失约不能当NotStarted。

调用期间整个授权检查到实际首字节保持同一次序化仲裁；撤权先赢则sink收到0字节，start先赢则sink已收到授权帧的真实前缀且随后撤权不改历史。测试用预置barrier/受控步骤决定两种顺序；测试barrier在仲裁外安排启动时机，不在生产仲裁里等待另一个线程。不能使用sleep猜测竞态。

unsubscribe在同一协议内先封住新排入，删除Queued未开始帧并回收Watch；Started/Unknown不声称撤回。其他连接/旧世代/未知Watch一律false，无存在性泄漏。close/会话失效清理该连接全部Queued/Watch，其他连接独立，业务execution寿命不变。对外get/list响应也须通过等价的发送起点重验；本包确定性consumer覆盖该组合，不把较早查询允许作为无限期发送凭证。

## 6. 审核与范围

上述全部为待审具体草案。需独立AI规格复核认证根、权限维度、完整组与单target DTO映射、取消仲裁、分页局部约束值及真实传输起点，再冻结头签名和固定测试集合。原始Caller/Target/Permit合同保持不变。

不实现审批UI、AI批准broker、真实OS身份隔离、递归长期委托、生产Schema、完整组执行器、执行索引、通知协议或无状态cursor密码学后端。进程内native模块和明确装配适配器可信；private/owner规则用于防误用，不声称抵抗任意恶意C++或同OS用户全部文件/进程权限。自动验收须实际三配置、固定expected和9CHECK；草案审核通过不等于代码通过。

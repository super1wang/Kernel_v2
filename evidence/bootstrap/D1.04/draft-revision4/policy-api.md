# D1.04 内存授权与许可API草案

状态：**PendingReview**。本文件尚未获独立AI规格审核，不允许据此开始行为实现或声明Passed。只具体化架构v3.3 A06.1/A10/A17.4–A17.6和执行卡D1.04；不改变CoreContracts公开签名、RPC方法、身份编码或协议版本。

修订2：针对`docs/reviews/D1.04-api-initial.md`五组ChangesRequested补齐具体入口。初版字节保存在`evidence/bootstrap/D1.04/draft-initial`；本修订仍待独立增量审核。

修订3：按`evidence/bootstrap/D1.04/source-lifetime-review.md`补充固定源寿命及Store关闭仲裁；保留既有审核历史。本修订仍为PendingReview，不增加行为实现范围。

内部命名空间拟为`ock::runtime::policy`，头为`packages/runtime/policy/policy.hpp`，内部库`ock_policy_internal`仅依赖`OCK::CoreContracts`闭包，不安装，不使SDK Runtime可用。D1.04不依赖Registry查找：操作政策来自组合根可信安装的精确政策目录，D1.05再连接已冻结Registry。认证/目标/观察元数据和传输适配器是显式装配端口，没有动态ServiceLocator。

## 1. 基础值、预算与错误

沿用`contracts::Result/Error/PrincipalRef/OperationKey/ContractDigest/CallerView/CallerGrant/TargetView/ActionPermit/PermitBinding/ExecutionRef/ExecutionSummary/ListRequest/ListPage/ObservationFilter/KeysetPosition`；不新增第二套公共许可或caller。内部StoreId、ConnectionId、ActionId、WatchKey使用带类型的非零128位身份；它们不作为可持久化或RPC身份，不叫协议SubscriptionId。身份由对应发行者私有单调64位计数编码，不复用，耗尽永久拒绝新发行。所有单调世代从1起，不回绕。

```cpp
using TimePoint = std::chrono::steady_clock::time_point;
struct PolicyBudget {
  std::size_t principals=128, rules=4096, targets=4096, sessions=128;
  std::size_t active_actions=4096, active_responses=1024, active_watches=256, members=256;
  std::size_t watches_per_session=32, watches_per_principal=128, diagnostics=128;
  std::size_t declarations=32768, text_bytes=1048576;
  std::size_t credential_bytes=8192, queued_frames=1024, queued_bytes=1048576;
  std::size_t frame_bytes=16384, page_size=200, scan_limit=2000;
  std::uint64_t identity_limit=UINT64_MAX, generation_limit=UINT64_MAX;
  std::chrono::milliseconds session_ttl{3600000}, action_ttl{30000};
  std::chrono::milliseconds page_ttl{300000}, queued_ttl{30000};
};
enum class PolicyErrc : std::uint32_t {
  InvalidInput=1, InvalidOwner, InvalidAuthority, AuthenticationFailed,
  SessionClosed, StoreClosed, Expired, Denied, TargetUnavailable, PolicyNotInstalled,
  ContractMismatch, InvalidGroup, InvalidPermit, AlreadyConsumed, Cancelled,
  CursorInvalid, UnsupportedDelegation, BudgetExceeded, IdentityExhausted,
  GenerationExhausted, Busy, TransmissionUnknown
};
```

使用独立`ock.policy` ErrorDomain，错误正文不包含credential、隐藏主体/对象、查询总数或订阅存在性。未知/无权目标对不受信请求统一为TargetUnavailable；跨连接/未知/旧世代退订统一`removed=false`。内部诊断可区分原因，但有界且不能随公共错误泄露。

所有限制必须为正且有实现可表示上限；TTL checked转换/相加，不接受负数、无限期限或溢出。服务器确定有效deadline为认证、委托、请求上限和配置TTL的最小值，`now >= deadline`即过期。ClockPort使用steady_clock语义；测试钟不得导致公共Context的真实steady_clock检查被绕过。

identity_limit是每Store合计子身份发行的终身上限，generation_limit是该Store及其记录的单调世代上限；可信装配可降低，均为非零uint64，不允许运行中提高或重置。所有发行/递增使用同一checked饱和比较实现，达到上限永久拒绝该发行者/记录的下一次操作；测试设小上限真实触达该分支。全进程Store序号使用同一饱和发行原语、固定UINT64_MAX上限且无重置入口；子128位身份编码进程Store序号与Store内序号。关闭/重新创建Store不能复用前一Store身份，即使每Store上限很小也不会碰撞；不提供任意写generation的测试后门。

容器数量、单字段及总文本在复制前checked累加，覆盖所有Name、版本、规则、成员、字段、owner、目标及嵌套scope；真实适配器返回的额外拥有型材料在入库前再次检查。不得因为输入vector被move就接管仍有外部元素别名的缓冲。规则仅含自有值和vector，不允许`shared_ptr<mutable vector>`、span或回调作为规则谓词。外部显式端口/资源owner要求非空且`use_count()>0`；不声称能验证恶意no-op deleter。

Watch同时计入连接、主体及全局配额；Queued/正在开始的发送同时占帧数及字节预算。发行记录不得无限保留历史：Session/Action/Response/Watch以私有对象自身保存终态，活动索引用weak owner或有界活跃记录；终结时从活动索引移除，回收释放在仲裁外。旧owner即使仍被调用者持有也只能返回终态拒绝，身份不复用，绝不能因记录移除按DTO重新认证。diagnostics是有界诊断记录数，满时计数饱和/标截断，业务拒绝不转成功。对象仍在途占用的实际内存继续计预算，不把索引移除冒充内存已释放。

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
  std::optional<OperationSelector> operation; // Invoke必须有；其他use为空表示该use内不额外限制operation
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
  bool permits_group=false;
};
struct UsePolicyInput {
  AccessUse use; // Invoke禁止出现在本表，其权限取OperationPolicyInput
  std::vector<Name> required_permissions;
  std::vector<ScopeRule> module_rules;
};
struct TargetPolicyInput {
  foundation::ObjectId target;
  std::uint64_t lifecycle_generation;
  std::vector<ScopeRule> rules;
  std::shared_ptr<contracts::PortLifetime> lifetime_owner;
};
struct PolicyConfiguration {
  std::vector<PrincipalPolicyInput> principals;
  std::vector<OperationPolicyInput> operations;
  std::vector<UsePolicyInput> uses;
  std::vector<TargetPolicyInput> targets;
};
class PolicyStore; class PolicyAdministration; class SessionAuthority;
class ActionAuthorization; class ObservationAuthorization; class WatchAuthorization;
class ResponseAuthorization; class TrustedGroupDigestPort; class ExecutionAccessSourcePort;
struct ActionRequest;
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
class PolicyAdministration final {
public:
  ~PolicyAdministration();
  PolicyAdministration(const PolicyAdministration&)=delete;
  PolicyAdministration& operator=(const PolicyAdministration&)=delete;
  Result<void> replace_principal_policy(const PrincipalPolicyInput&);
  Result<void> replace_operation_policy(const OperationPolicyInput&);
  Result<void> replace_use_policy(const UsePolicyInput&);
  Result<void> replace_target_policy(const TargetPolicyInput&);
  Result<void> retire_target(ObjectId);
  Result<void> set_lifecycle(ObjectId,std::uint64_t);
  Result<void> close_store();
private:
  friend class PolicyStore;
  struct State;
  explicit PolicyAdministration(std::shared_ptr<State>);
  std::shared_ptr<State> state_;
};
```

PolicyStore/Administration/SessionAuthority不可复制、不可move；私有构造，不公开记录或可变集合。Administration只由create交给组合根，不出现在模块Context、请求处理DTO或SessionAuthority中。其具体写入口为`replace_principal_policy(const PrincipalPolicyInput&)`、`replace_operation_policy(const OperationPolicyInput&)`、`replace_target_policy(const TargetPolicyInput&)`、`retire_target(ObjectId)`、`set_lifecycle(ObjectId,uint64_t)`；更新使用完整新快照，不允许以裸generation赋值代替实质政策。`set_lifecycle`只接受严格递增值，供可信领域协调者报告事实；不获取资源锁，不替代领域生命周期协议。

Store状态只有Open→StoreClosed，不可重开。`PolicyAdministration::close_store()`在与permit消费及发送起点相同的短仲裁内先以不分配操作永久设置StoreClosed；此点之后旧Session、未消费Action、Response、Watch及Queued帧全部失效，无需等待逐项清理才生效。重复close_store幂等成功；其他认证、政策更新、准备、解析、验证、发行、消费、观察、入队与start入口都在其最终仲裁处重验StoreClosed并拒绝新工作。不可变描述getter不授予权限，可继续读取已有描述；清理/重复退订保持幂等且不泄露存在性。失效返回StoreClosed（对外需不可枚举映射的入口仍遵守既有错误规则）。已Consumed或Started的历史事实不回滚，未开始帧零字节丢弃；owner/预留/队列内容的释放和任意析构在仲裁外完成。close_store完成返回只表示关闭已线性化，不表示已开始I/O或业务执行已结束。

四方均为明确allow集合，缺省deny。Invoke权限取精确OperationPolicyInput.required_permissions；其他use必须在可信UsePolicyInput表恰好有一个安装项，其required_permissions非空。未安装use拒绝，无默认权限名称。实际执行所属OperationSelector仍必须从可信元数据精确查OperationPolicyInput。非Invoke的“模块政策”同时满足该UsePolicyInput.module_rules及该实际OperationPolicyInput.module_rules，不能由调用方声明。Catalog使用可信安装目录条目自身的精确OperationSelector和其明确登记的目录逻辑目标，不枚举未授权条目。

完整检查tuple为`(use, exact operation, actual target, actual owner, required_permissions, requested field)`。以下矩阵固定相关维度：

| use | operation | target | owner | field |
|---|---|---|---|---|
| Invoke | 必需精确值 | 每个显式真实目标 | 忽略 | 忽略 |
| Catalog | 当前安装条目的精确值 | 显式目录逻辑目标 | 忽略 | 忽略；不输出执行摘要 |
| GetSummary/ListSummary/Wait/Subscribe | 源中执行的精确值 | 源中每个实际目标 | 源中实际owner | 每个输出SummaryField分别检查 |
| CancelExecution/ReadResult/ReadLog/ReadAsset | 源中执行的精确值 | 源中每个实际目标 | 源中实际owner | 检查用途时忽略；若输出摘要另按GetSummary检查 |

一条ScopeRule必须在**同一条规则内**同时匹配tuple的全部相关维度：use相等；operation有值时精确相等（Invoke规则不得为空）；target/owner包含当前实际值；该规则permissions必须包含完整required_permissions集合；输出字段须包含该field。一个来源有至少一条这样完整匹配的规则才允许该tuple。不得先并targets、permissions、owners或fields再匹配；例如规则(A,read)与(B,write)绝不推出(A,write)。四个来源分别完整匹配后取交；每目标、每输出字段都必须成功。不同规则可分别允许不同完整tuple，但不能拼接一条原本不存在的授权。

空targets/owners/permissions/fields均是空集合，相关维度缺项即拒绝；只有矩阵明示忽略的维度不参与匹配。非Invoke operation为空是该明确use内不额外限制operation，不匹配其他use，且不能绕过模块侧对真实operation精确安装项的查找。规则所有集合去重、有界，未知enum拒绝。owner扫描前检查只是有限候选检查：对主体、会话及已安装ListSummary模块use规则，须存在同一条规则同时覆盖ListSummary、requested owner和其完整所需权限；此时暂不匹配未知operation/target/field。该候选允许只准许受限scan，不产生可见数据授权，每项仍执行上述完整四方匹配，不返回隐藏计数。

没有administrator旁路。TargetPolicyInput.lifetime_owner须是仅保活、析构不释放正在持有并发许可的PortLifetime对象；不能传入ResourceLease/ActivityLease实例或其别名（实际类型拒绝），不永久占用并发资源。它不授予ACL，也不替代调用时取得ResourceLease。

每次open发行独立ConnectionId和固定SessionAuthority，即使Principal相同也不共用caller发行者。请求scope与认证ceiling及主体政策求交，只能收缩；`allow_redelegation=true`首版明确UnsupportedDelegation，不隐式扩展委托链。ServicePrincipal也必须通过已固定的可信认证入口得到`kind=Service`及独立期限/政策；不得将断线用户session转为服务身份。

```cpp
class VerifiedCaller final {
public:
  const contracts::CallerView& view() const noexcept;
  std::shared_ptr<contracts::CallerAuthorityPort> authority() const noexcept;
private: // 私有配对CallerGrant、真实会话owner；不公开grant提取入口
  friend class SessionAuthority; friend class ActionAuthorization;
};
class SessionAuthority final {
public:
  Result<std::shared_ptr<const VerifiedCaller>> verify(const contracts::CallerDescription&);
  std::shared_ptr<CallerAuthorityPort> callers() const noexcept;
  std::shared_ptr<TargetAuthorityPort> targets() const noexcept;
  std::shared_ptr<ObservationAuthorization> observations() const noexcept;
  Result<void> restrict_delegation(const DelegationInput&); // 仅当前scope子集
  Result<void> close();
  Result<std::shared_ptr<ActionAuthorization>> prepare(
    const VerifiedCaller&, const ActionRequest&);
};
```

SessionAuthority组合两个私有具体对象SessionCallerAuthority、SessionTargetAuthority，分别单继承既有CallerAuthorityPort、TargetAuthorityPort并实现其全部原签名；两者共享同一私有SessionState。callers()/targets()分别返回正确单继承分支owner，不存在多继承PortLifetime转换歧义；调用者不能更换这两个对象。verify内部只使用callers()，目标解析通过targets()->resolve/validate，既有公开合同不变。

两个具体端口的声明如下；SessionRecord是内部身份/委托/关闭记录，不反向拥有SessionAuthority或任一端口，因此没有shared_ptr自环。SessionAuthority析构执行close；外部仍持端口/VerifiedCaller时记录继续存在但已关闭，不能延长连接权限。

```cpp
namespace detail {
struct SessionRecord;
class SessionCallerAuthority final : public CallerAuthorityPort {
public:
  Result<std::shared_ptr<const CallerGrant>> authenticate(const CallerDescription&) override;
  Result<void> validate(const CallerGrant&) const override;
private:
  friend class ::ock::runtime::policy::SessionAuthority;
  explicit SessionCallerAuthority(std::shared_ptr<SessionRecord>);
  std::shared_ptr<SessionRecord> record_;
};
class SessionTargetAuthority final : public TargetAuthorityPort {
public:
  Result<std::shared_ptr<const TargetView>> resolve(const CallerView&,ObjectId) override;
  Result<void> validate(const TargetView&,const CallerView&,ObjectId) const override;
private:
  friend class ::ock::runtime::policy::SessionAuthority;
  explicit SessionTargetAuthority(std::shared_ptr<SessionRecord>);
  std::shared_ptr<SessionRecord> record_;
};
}
```

CallerDescription是投影声明，authenticate必须与该已认证会话实际principal/delegated_by和服务器标签完全一致，不把tags当权限。私有Grant对象、原始对象地址及会话发行记录一起检查；相同字段的自制Grant拒绝。verify内部取得该真实Grant并经现有CallerView::check配对；既有authenticate公开虚函数不能被绕过，会做同样核验。

所有入口核对view属于当前固定CallerAuthority且当前会话未断线、未过期、委托世代仍匹配。A/B同主体互换CallerView、TargetView、VerifiedCaller、ActionAuthorization均拒绝；不靠Principal相等判同连接。close只收回该连接授权与观察资源，不取消其他连接或业务执行。ACL政策、会话委托、生命周期保持不同记录与单调世代；它们只在短授权仲裁中读取/比较，不合成一个资源锁或生命周期计数。

## 3. 真实目标、精确组材料及许可

resolve只从可信目标目录找真实ObjectId，返回私有发行的TargetView，记录对象实例身份、生命周期世代和该会话caller配对。目标删除再建同ObjectId仍发行新实例身份并增加生命周期世代；旧视图不复活。无权/不存在同响应。多目标全部预解析、规范化并保有owner，不能因第一个目标通过就忽略其他目标；准入后不再查询UI选中状态。

修订4澄清resolve的“无权”：该签名尚无实际use、operation、owner、field，只作目标候选可见性预检。必须在可信已安装的精确OperationSelector与AccessUse范围，存在同一个`(use, operation, requested_target, required_permissions)`候选，主体ACL、当前会话委托、认证ceiling、操作module_rules、目标rules分别有一条规则完整覆盖；非Invoke还须安装对应UsePolicyInput并由其module_rules覆盖。所需权限仍按第1节取可信操作或UsePolicyInput，不能从调用者填写内容取得。每一来源不得跨规则拼接候选，更不能各来源选不同候选。不存在共同候选与不存在目标均返回TargetUnavailable。

这一预检不枚举其他主体或owner，不读取执行元数据，也不把未知owner/field补成当前主体或任意值：owner/field在此阶段不参与匹配。返回值仅证明当前会话可取得候选目标身份，不授予任何调用、查询、字段或发送权限。prepare及观察/发送仍对已知实际use、精确操作、owner、field和全部目标执行完整tuple授权，不得用resolve成功替代。validate同时重验发行对象身份、会话/委托/政策世代和目标实例/生命周期，不使旧视图复活。仅Read操作或仅GetSummary/ReadResult用途的完整候选应可解析，不能固定要求Invoke、写权限或某个权限名。

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
  const OperationSelector& envelope() const noexcept;
  ObjectId anchor_target() const noexcept;
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

prepare首先校验真正VerifiedCaller和精确安装的envelope政策。单操作必须正好一个相同operation成员，anchor必须在其显式目标集合中；组envelope必须是安装目录显式声明permits_group的操作，也单独授权其anchor。成员保留顺序且保留重复步骤，**不得去重成员而漏验重复执行**；每个成员targets去重排序、非空、有界。每个成员精确operation版本/contract与其全部真实目标都重新授权，批次权限不代替成员权限。

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
enum class RestoreMode { Absent, Present };
struct ObservationSourceIdentity { HostIncarnation host; RestoreMode restore; };
struct AccessScanRequest { ListRequest list; };
struct AccessScanPage {
  std::vector<std::pair<std::uint64_t,ExecutionAccessInput>> candidates;
  std::optional<KeysetPosition> next_scan;
  HostIncarnation host;
  Name retention_scope;
};
class ExecutionAccessSourcePort : public contracts::PortLifetime {
public:
  virtual ObservationSourceIdentity identity() const noexcept = 0;
  virtual Result<ExecutionAccessInput> find(ExecutionRef) = 0;
  virtual Result<AccessScanPage> scan(const AccessScanRequest&) = 0;
};
struct PageBindingData {
  StoreId store;
  ConnectionId connection;
  std::uint64_t delegation_generation, permission_generation;
  PrincipalRef owner;
  PhaseSet phases;
  ListBudget budget;
  HostIncarnation host;
  RestoreMode restore; // D1.04只接受Absent
  std::uint64_t upper_ordinal, before_ordinal;
  TimePoint deadline;
};
class PageBinding final {
public:
  PageBinding(const PageBinding&)=default;
  PageBinding& operator=(const PageBinding&)=default;
  const PageBindingData& value() const noexcept;
private:
  friend class ObservationAuthorization;
  explicit PageBinding(PageBindingData);
  PageBindingData value_;
};
enum class ProjectionKind { Summary, Page, Hint };
class ProjectionSnapshot final {
public:
  ProjectionKind kind() const noexcept;
  const std::variant<std::shared_ptr<const ExecutionSummary>,ListPage,ChangeHint>&
    value() const noexcept;
private:
  friend class ObservationAuthorization; friend class SendCoordinator;
  // 私有构造独立复制上述variant；只保存受权投影，不接收请求payload。
};
class ResponseAuthorization final {
public:
  const ProjectionSnapshot& projection() const noexcept;
  AccessUse use() const noexcept;
  TimePoint deadline() const noexcept;
private:
  friend class ObservationAuthorization; friend class SendCoordinator;
  // 私有ResponseState保存唯一身份、会话及四方规则/真实目标/投影归属。
};
struct AuthorizedSummary {
  std::shared_ptr<const ExecutionSummary> summary;
  std::shared_ptr<const ResponseAuthorization> response;
};
struct AuthorizedPage {
  ListPage page;
  std::optional<PageBinding> continuation;
  std::shared_ptr<const ResponseAuthorization> response;
};
class WatchAuthorization final {
public:
  ~WatchAuthorization();
  WatchKey key() const noexcept;
  std::uint64_t generation() const noexcept;
  const ObservationFilter& filter() const noexcept;
  TimePoint deadline() const noexcept;
private:
  friend class ObservationAuthorization; friend class SendCoordinator;
  // 私有WatchState保存Store/会话身份、状态、规范化filter和原始发行归属。
};
class ObservationAuthorization final {
public:
  Result<AuthorizedSummary> get(
    const VerifiedCaller&, ExecutionRef, AccessUse);
  Result<AuthorizedPage> list(const VerifiedCaller&,const ListRequest&,
    const std::optional<PageBinding>& continuation);
  Result<std::shared_ptr<WatchAuthorization>> subscribe(
    const VerifiedCaller&, const ObservationFilter&);
};
```

ObservationAuthorization由SessionAuthority私有创建并绑定该会话与固定源；observations()返回固定owner，不能替换source。get的use只接受GetSummary/Wait/CancelExecution/ReadResult/ReadLog/ReadAsset相应授权检查；后三类不返回大正文，只返回最小摘要及其私有response。除用途检查外，摘要输出另需GetSummary字段授权；不能以CancelExecution权限泄露无权摘要。能读摘要不自动能取消或取结果；实际业务动作仍由后续受控消费者执行。AuthorizedSummary/AuthorizedPage的普通summary/page值只供本次同步调用读取；异步发送必须使用同次response，不能拿自行修改的page借授权。

元数据源返回完整已拥有的ExecutionSummary和实际目标绑定；核对execution、host、owner、相位、序号、目标非空/唯一及预算，不能把请求owner当对象owner。可见字段取四方fields交集，创建独立最小摘要；必需身份/phase字段无权限时隐藏整项，而不是泄露裸执行ID。optional owner/parent和facts/progress仅在授权时投影；现有SummaryInput有必需owner，因此首版不能授权owner字段时隐藏整项，不伪造另一owner。不包含Args、结果正文或资产内容。

ListRequest.owner为显式过滤，self在可信会话层解析为实际Principal；其他owner需要四方明确覆盖。先授权owner范围，再调用bounded source.scan；源返回候选每项在同次输出前按当前政策逐项重验，隐藏项不计可见数、不返回身份/错误，响应无总数。扫描限制、页大小和下降keyset检查沿D1.02合同，空可见页也必须按实际扫描位置前进；不会声称一致快照。源必须是受限已有索引接口的确定性消费者，不以本包名义建立新生产索引。

PageBindingData精确记录本use固定为ListSummary时的全部过滤与keyset绑定。D1.04仅支持内存无restore配置：固定source.identity()必须返回非零host和RestoreMode::Absent，create及每次查询均核对；Present或未知enum明确拒绝，不静默丢弃真实restore域。PageBinding.restore始终Absent，续页与当前source精确匹配；不使用“可用时绑定”这种不明确缺省。未来支持持久source必须另审完整StoreId/RestoreGeneration字段，不能向此接口回报Absent冒充没有restore。

**固定源寿命合同：** create保存source的非零host/Absent作为Store不可变装配身份；在旧Store仍Open期间源不得切换host/restore。对同一个ExecutionRef，真实owner、精确OperationSelector、规范化actual_targets及绑定的目标实例是不可重绑定的授权事实；不能删除再用同一Ref表示另一身份。普通phase/progress/observation_version等观测值仍可按既有合同演进，不冻结业务状态。首版不支持原地重绑定这些授权事实。需要切换源身份或重绑定时，可信装配者必须先完成旧Administration.close_store的同步关闭仲裁，再更换source并创建拥有新StoreId的新Store；Present在新Store仍拒绝。不能先改变源、再靠异步通知或较晚关闭补救。

查询、subscribe、入队和start在锁外读取source.identity()并与Store固定身份比较；源返回的summary/page.host也须一致。观察到不匹配立即通过同一个关闭仲裁设置StoreClosed并拒绝本次工作；Source响应若与已绑定授权事实矛盾也作失约关闭，不接受新事实覆盖原绑定。随后最终授权仲裁再检查StoreClosed。锁外读取是失约检测，不是消除TOCTOU的保证：安全依赖可信source在Open期间不变、合法切换前必先关闭；不能用“再读一次identity”替代这一同步寿命合同。不为检测而建设生产execution索引或无界历史表；已发行Response/Watch/Action只保留其自身有界绑定，源承担不可重绑定合同。

PageBinding不可由请求构造，copy只复制局部自有约束值。每次list续页检查其Store/会话、委托/权限世代、owner/phase/budget、host/restore、upper/before及deadline；变化返回CursorInvalid/Expired，不静默重开。它不是服务器cursor登记或句柄池，不pin结果，不提供网络完整性保护。实际`ock.execution.list/1` opaque cursor/HMAC仍留D2.04；未来受信解码适配器须先验证MAC，再调用等价约束校验，不把C++地址序列化成cursor。source.scan返回的host和next_scan也须与本次identity()一致；改变则拒绝整页。

subscribe验证整个规范化filter/topics及配额；execution集合逐项检查，无权/未知统一失败，任一失败不安装部分Watch。owner模式授权整个明确owner范围，之后每条事件仍验证实际目标和字段。WatchAuthorization为私有不可伪造的连接内资源owner，包含内部WatchKey/generation及规范化filter；不含RPC SubscriptionId、通知sequence或业务执行owner。协议pending-ack屏障、通知排序/gap编码留D2.04；确定性消费者只证明在watch有效前不接受排入、失败无资源、发送前重验。

Watch析构相当于该连接内退订：先封住新排入，延迟释放队列owner；不取消业务执行。显式unsubscribe与析构重复无副作用。ResponseAuthorization不属于Watch，固定绑定本次get/list的use、真实会话、全部目标实例/生命周期、实际owner及精确投影字段，期限取本次查询和queued_ttl最小值；只有ObservationAuthorization可创建。

## 5. 撤权与真正开始传输的短仲裁

本节是D1.04必须实际验证的授权边界，不得实现成`if (allowed) post(send)`。队列不保存可永久复用的allow=true。

```cpp
enum class StartResult { NotStarted, Started, Unknown };
struct WatchStamp { WatchKey key; std::uint64_t generation; };
struct TransmissionBinding {
  StoreId store;
  ConnectionId connection;
  std::variant<WatchStamp,ResponseId> origin;
  TimePoint deadline;
};
class PreparedTransmission final {
public:
  std::span<const std::byte> bytes() const noexcept;
  const TransmissionBinding& binding() const noexcept;
  const ProjectionSnapshot& projection() const noexcept;
private:
  friend class SendCoordinator;
  PreparedTransmission(std::vector<std::byte>,TransmissionBinding,
                       std::shared_ptr<const ProjectionSnapshot>);
  const std::vector<std::byte> bytes_;
  const TransmissionBinding binding_;
  const std::shared_ptr<const ProjectionSnapshot> projection_;
};
class ProjectionEncoderPort : public contracts::PortLifetime {
public:
  virtual Result<std::vector<std::byte>> encode(
    const ProjectionSnapshot&,std::size_t max_bytes) = 0;
};
class TransmissionReservation : public contracts::PortLifetime {
public:
  virtual std::size_t capacity() const noexcept = 0;
protected: TransmissionReservation() = default;
};
class TransmissionStartPort : public contracts::PortLifetime {
public:
  virtual Result<std::unique_ptr<TransmissionReservation>> reserve(std::size_t bytes) = 0;
  virtual StartResult start_now(const PreparedTransmission&,
                               TransmissionReservation&) noexcept = 0;
};
class SendCoordinator final {
public:
  static Result<std::unique_ptr<SendCoordinator>> create(
    std::shared_ptr<SessionAuthority>, std::shared_ptr<TransmissionStartPort>,
    std::shared_ptr<ProjectionEncoderPort>);
  Result<void> enqueue(const WatchAuthorization&, const contracts::ChangeHint&);
  Result<void> enqueue_response(std::shared_ptr<const ResponseAuthorization>);
  Result<StartResult> start_next();
  Result<bool> unsubscribe(const WatchAuthorization&);
};
```

SendCoordinator只给可信观察发送适配器装配；请求不能提供TransmissionStartPort或PreparedTransmission。Watch属于同一固定会话且仍有效才可enqueue。入队前校验source中对应真实execution/目标、topic/filter、当前字段授权；保存拥有型最小投影与该原始会话/Watch/目标实例身份，不保存完整结果pin。预算、预编码、队列和发送端容量预留均在仲裁外完成；入队最终提交再次核对当前世代，过期/撤权期间失败无半帧。

编码端同样在create时固定，是可信、有界的投影→字节适配；只能看到ProjectionSnapshot，不能替换它的权限归属或引入未授权字段。编码在锁外，返回自有vector后由SendCoordinator限长并移入唯一私有PreparedTransmission；返回过长/空编码或失败不排入。D1.04用明确标识的确定性小型编码消费者，不宣称生产JSON-RPC帧编码。调用方没有接收任意字节vector的enqueue重载。

reserve在仲裁外调用，返回端口私有派生的RAII reservation owner；容量至少覆盖精确bytes().size()，该端口核验reservation真实发行归属/仍有效/未用，不能仅信任其capacity()自报。Coordinator不接受调用方提供的reservation。PreparedTransmission及reservation均由队列条目独占持有，在start_now前已完成全部分配；调用期间借用const transmission及reservation引用，返回后owner仍由Coordinator管理并在仲裁外析构。Started/Unknown将reservation终结且该帧不可重试；NotStarted可保留或在锁外换新reservation，但下一次start_next必须重验当前权限。保留容量票据从不代替实际首字节事件。

enqueue_response是独立get/list响应路径，不创建或伪造Watch。只接受同一会话ObservationAuthorization原始发行的ResponseAuthorization，保存它内部不可变投影及完整实际目标/owner/字段绑定，不能用AuthorizedPage中可自行修改的page替换payload。一个response只允许一次成功排入，重复排入拒绝；失败前尚未排入可在当前授权仍有效时重试。其Queued→Dropped/Started/Unknown和Watch帧共用start_next仲裁，发送前逐项重验所有返回项目及字段；任一已失权则整个原响应零字节丢弃，不把旧完整字节作为删减后的响应发送。普通同步summary/page的读取不产生网络授权，网络适配器必须沿本次response走这一入口。

PreparedTransmission私有保存精确字节与不可变投影归属，发送端只能消费同一个对象；不能在校验后替换成另一payload。预编码内容在队列等待期间不可增加字段；若当前字段权限缩小，丢弃旧帧，不“校验新投影却发送旧字节”。队列合并只能在相同会话/Watch/执行/主题范围使用新自有投影，保留待发送状态并重新绑定；不跨连接复用内容。完整notification sequence/gap调度仍是后续协议层职责。

start_next先在锁外准备只读材料、读取并匹配固定source身份、预留接收容量，然后在与撤权/close_store/会话close/退订相同的短仲裁内：核对Store仍Open、原Response/Watch绑定的source身份、当前会话、每个真实目标当前实例/ACL/lifecycle、字段和deadline → 若无权或已关闭则Queued→Dropped → 否则调用固定可信start_now。**线性化点是start_now实际将首个字节交付既定接收介质的事件，且在该次仲裁返回/允许撤权或close_store线性化之前已经发生。** 单纯设置Started标志、移入另一个异步队列、返回send token或稍后任意执行的lambda都不是这个事件。合法source切换必先close_store，因此锁外读取后即使关闭先赢，最终仲裁仍拒绝旧帧。

start_now是唯一受审的窄传输起点，不是任意业务回调：必须使用预持有缓冲/预留容量、同步非阻塞、无动态分配/任意析构、不重入PolicyStore或调用模块代码，不允许将工作推给任意未仲裁worker后提前报Started。执行固定有界的非阻塞交接；不能持授权仲裁等待完成I/O。若后端无法满足，不能装配为本接口。本包提供真实预分配**内存字节sink**作为确定性消费者：Started前至少一个被授权字节已写入接收槽，并在同一受控测试中核对字节内容/长度；不把bool状态写入当证据。此测试只证明该授权/发送接点，不宣称已实现OS Named Pipe或生产网络栈。

- NotStarted：能证明零字节越过起点，容量不足/明确零字节失败时保持Queued或按期限丢弃；再次尝试必须重新完整授权。
- Started：记录本帧越过真实起点，不返还Queued；迟到撤权/退订不能声称撤回已开始传输的字节。剩余传输受该已开始帧的既有有界传输规则约束，不夹带下一帧。
- Unknown：后端不能证明是否已有字节，进入隔离/关闭连接清理，不自动重试该帧，不伪称未发送，也不阻断业务执行收尾。noexcept失约不能当NotStarted。

调用期间整个授权检查到实际首字节保持同一次序化仲裁；撤权先赢则sink收到0字节，start先赢则sink已收到授权帧的真实前缀且随后撤权不改历史。测试用预置barrier/受控步骤决定两种顺序；测试barrier在仲裁外安排启动时机，不在生产仲裁里等待另一个线程。不能使用sleep猜测竞态。

unsubscribe在同一协议内先封住新排入，删除Queued未开始帧并回收Watch；Started/Unknown不声称撤回。其他连接/旧世代/未知Watch一律false，无存在性泄漏。close/会话失效清理该连接全部Queued/Watch，其他连接独立，业务execution寿命不变。对外get/list响应也须通过等价的发送起点重验；本包确定性consumer覆盖该组合，不把较早查询允许作为无限期发送凭证。

## 6. 审核与范围

### 6.1 声明组织与私有状态冻结

所有代码段共同使用`using namespace ock::contracts`及`using ObjectId=ock::foundation::ObjectId`。StoreId、ConnectionId、ActionId、WatchKey、ResponseId、TargetInstanceId分别是独立tag的`foundation::Tagged128<Tag>`别名，不是新wire DTO。声明顺序先前置所有类/struct，再定义值struct和类；不能把文档段落顺序当成省略forward declaration的理由。

PageBinding可按上文复制局部约束值。PolicyStore、PolicyAdministration、SessionAuthority、VerifiedCaller、GroupSnapshot、ActionAuthorization、ObservationAuthorization、WatchAuthorization、ResponseAuthorization、ProjectionSnapshot、PreparedTransmission及SendCoordinator均显式删除copy/move构造和赋值，其Result使用owner返回；前文verify已统一为`Result<std::shared_ptr<const VerifiedCaller>>`。其他普通值struct（PolicyAssembly、AuthorizedSummary、AuthorizedPage等）按成员能力保留移动/复制，不因私有实体不可move而误删其移动构造。VerifiedCaller不是可由DTO构造的值。

各类显式声明out-of-line析构；除已有示出的私有PreparedTransmission/PageBinding构造外，其统一私有构造为`explicit X(std::shared_ptr<State>)`，私有字段仅`std::shared_ptr<State> state_`，`State`在类内forward声明。State是实现细节，但以下必需内容是本合同，不允许实现时省略或变成可变外部别名：

| 私有State | 必需自有字段与唯一创建者 |
|---|---|
| PolicyStore | StoreId、Open/StoreClosed状态、全部预算、私有饱和发行器、当前权限generation、规范化配置、固定auth/clock/digest/source owner及create保存的ObservationSourceIdentity（host/Absent）、仲裁及有界活动索引；create创建 |
| PolicyAdministration | 相同Store内部状态owner，无独立政策副本；PolicyStore::create创建 |
| SessionAuthority | 共享无反向引用的SessionRecord（ConnectionId、PrincipalRef/kind/delegated_by、deadline、冻结委托及generation、Closed位），两个固定authority owner与observation owner；PolicyStore::open创建 |
| VerifiedCaller | 原始Grant owner、对应CallerView、固定Session状态owner；SessionAuthority::verify创建 |
| GroupSnapshot | 自有envelope、anchor及保序members；SessionAuthority::prepare创建 |
| ActionAuthorization | ActionId、真实VerifiedCaller owner、完整GroupSnapshot、摘要、全部TargetInstanceId/ObjectId/lifecycle及所选政策revision、deadline、取消/消费状态、唯一原permit owner；prepare创建 |
| ObservationAuthorization | 固定SessionRecord及ExecutionAccessSourcePort owner；SessionAuthority构造时创建并由observations返回，不反向拥有SessionAuthority |
| WatchAuthorization | WatchKey/generation、固定Session owner及Store保存的ObservationSourceIdentity、规范化ObservationFilter、已绑定执行的不可重绑定授权事实、deadline、Active/Closed状态及有界配额归属；subscribe创建 |
| ProjectionSnapshot | 自有kind及与之匹配的variant值；ObservationAuthorization创建get/list投影，SendCoordinator创建已重验hint投影 |
| ResponseAuthorization | ResponseId、固定Session owner及Store保存的ObservationSourceIdentity、use、deadline、原始ProjectionSnapshot、逐投影项目的ExecutionRef/精确OperationSelector/真实owner/所有目标实例与生命周期/实际输出字段、权限/委托generation、Fresh/Queued/Started/Dropped/Unknown终态；get/list创建 |
| SendCoordinator | 固定Session/start/encoder owner、拥有型有界队列；每项私有原始Watch或Response owner、PreparedTransmission、reservation及当前状态；create创建 |

表内列出的创建者均显式friend（ProjectionSnapshot列有两个合法创建者）；不存在从裸State、DTO、任意void owner或用户factory创建已验证对象的公共重载。创建者使用`shared_ptr<T>(new T(state))`或对应unique_ptr取得private构造对象，不假设std::make_shared自动获得friend权限。共享State不提供可变返回；跨对象通过不反向拥有外壳的SessionRecord/Store内部记录连接，无owner自环。Session两个端口分别返回单继承owner。Private状态的可变终态仅在Store仲裁内更新，不把它同返回的不可变描述混合。

GroupDigest端实际读取envelope()/anchor_target()/members()，编码端实际读取ProjectionSnapshot::value()，发送端实际读取PreparedTransmission::bytes()/binding()并消费自己reserve的材料，均有可实现的只读访问；编译正控制须实例化这些访问。与正常已安装native适配器的信任边界一致，不声称private可隔离恶意C++。

上述全部为待审具体草案。需独立AI规格复核认证根、权限维度、完整组与单target DTO映射、取消仲裁、分页局部约束值及真实传输起点，再冻结头签名和固定测试集合。原始Caller/Target/Permit合同保持不变。

不实现审批UI、AI批准broker、真实OS身份隔离、递归长期委托、生产Schema、完整组执行器、执行索引、通知协议或无状态cursor密码学后端。进程内native模块和明确装配适配器可信；private/owner规则用于防误用，不声称抵抗任意恶意C++或同OS用户全部文件/进程权限。自动验收须实际三配置、固定expected和9CHECK；草案审核通过不等于代码通过。

# D1.06 Host 与安装表面候选设计

状态：Candidate，尚未独立规格批准，不是实现或通过声明。依据唯一v3.3的D1.06、A16、A19、A21、A23；仅NativeSubset。D1.05已在9cd8439验收提交完成，D0.06前置的精确复核另行登记。

## 组合与公开边界

保留同一Registry、Policy、Native实现，不造第二个Runtime。生产`OCK::Runtime`成为真实STATIC_LIBRARY，直接公开依赖仅CoreContracts，经Foundation到expected；编译Registry/Policy/Invocation/Host/Observability实现。旧测试内部目标仍可从同一生产源编译以保留原边界断言，不安装这些验证目标。BUILD_TESTING=OFF仍可安装并运行C-A。

新增公开实验性入口`ock/runtime/host.hpp`、`ock/runtime/registry.hpp`、`ock/runtime/policy.hpp`、`ock/runtime/logging.hpp`。将现有registry/policy头的唯一定义迁入对应公开头，原源目录头只作内部转发；Invocation及private_bridge唯一定义移入`ock/runtime/detail/`供Host模板使用，仍不作为公开业务入口，私有成员访问控制不放宽。安装头的包含路径全部可搬迁，不能引用`packages/`或源码绝对根。所有内部直接include迁移及模板可见detail都登记SDK清单，detail不改称稳定API。Public HostBound不公开原生Bound、Engine、Catalog handler或Admission材料。

此选择让应用通过公开注册/可信策略类型装配宿主，真正的业务调用只使用HostSession/HostBound。D1.05底层受治理Native仍用于内部验证，不改变其无Host的原合同；直接使用detail不属于SDK承诺。首个正式SDK历史基准仍不存在，不将开发期对照冒称正式源码/ABI兼容。

建议候选版本升为`0.1.0-dev.2`：Runtime阶段为NativeSubset，`runtime_available=true`另配明确`runtime_profile="NativeSubset"`和`async_execution_available=false`。其他尚未实现产品组件仍ContractBaseline且find_package明确拒绝。SDK表面/能力变化与旧元数据消费者预期更新须单独审查；不能简单把Runtime标为完整Implemented。

## Host 生命周期与窄模块

HostPhase包括Constructed、Configuring、Validating、Recovering、Starting、Ready、StopAccepting、CancelOrFinish、Finalize、DrainExecutors、StopModulesObservers、ReleaseStorage、Stopped、Failed。NativeSubset的Recovering/DrainExecutors/ReleaseStorage不执行存储或队列操作；快照明确能力Absent，不能据状态名声称已有恢复、任务或存储实现。

Module生命周期独立于注册：

```cpp
struct ModuleContext { observability::LogPort& log; Name module; };
struct ModuleStopResult { bool quiescent; std::optional<Error> error; };
class ModuleLifecyclePort : public PortLifetime {
public:
  virtual Result<void> start(const ModuleContext&) = 0;
  virtual ModuleStopResult stop() = 0;
};
struct HostModule {
  registry::ModuleInput registration;
  std::shared_ptr<ModuleLifecyclePort> lifecycle;
};
```

Context是同步借用、不得保存，无Host/无ServiceLocator/无万能权限。服务由模块自身预绑定持有，生命周期接口只承担自身启动/停止，不发业务。start失败必须在返回前自行撤销该步未成功的局部资源；Host只逆序stop已成功start的模块。stop必须短且不等待，返回quiescent=false或抛异常时不释放该模块及其仍需的依赖，后续显式shutdown可重试。quiescent=true但附Error表示已安全排空并有清理错误，继续逆序清理且保留错误。

Registry公布只读模块拓扑顺序，来源于原publish实际排序；Host直接使用该冻结顺序，避免另建一个可能分歧的DAG算法。目录在Validating中只作为宿主私有候选；直到全部启动完成才原子开放Ready。注册回调、模块start/stop及日志调用均不在Host锁内执行。重入观察state或尝试业务不死锁；start/shutdown重入/并发时返回Busy，禁止嵌套生命周期操作。

## 候选公开签名

下列类型均位于`ock::runtime::host`，沿用contracts/foundation/Policy原错误与时间类型。具体预算字段及日志类型在合并候选时统一。

```cpp
struct HostBudget { std::size_t active_admissions=128, cleanup_errors=64; };
struct HostOptions {
  registry::BatchBudget registration;
  policy::PolicyBudget policy;
  invocation::NativeBudget native;
  HostBudget host;
};
struct HostPorts {
  std::shared_ptr<policy::TrustedAuthenticationPort> authentication;
  std::shared_ptr<policy::ClockPort> clock;
  std::shared_ptr<policy::TrustedGroupDigestPort> group_digest;
  std::shared_ptr<invocation::TrustedThreadPort> threads;
  std::shared_ptr<observability::LogPort> log;
};
class NativeHost {
public:
  static Result<std::unique_ptr<NativeHost>> create(
      HostOptions, policy::PolicyConfiguration, HostPorts);
  Result<void> add(HostModule);
  Result<void> start();
  Result<HostSession> open(const policy::AuthenticationAttempt&,
                           const policy::DelegationInput&);
  ShutdownReport shutdown_until(policy::TimePoint deadline);
  HostSnapshot snapshot(std::span<CleanupError>) const noexcept;
  HostCapabilities capabilities() const noexcept;
  ~NativeHost();
};
class HostSession {
public:
  Result<std::shared_ptr<const policy::VerifiedCaller>> verify(
      const CallerDescription&);
  template<ContractValue A,ContractResult R>
  Result<HostBound<A,R>> bind(const OperationKey&, ContractDigest, Shape,
      std::shared_ptr<const policy::VerifiedCaller>,
      std::span<const foundation::ObjectId>, invocation::TargetProjection<A>, Name);
  Result<void> close();
};
template<ContractValue A,ContractResult R> class HostBound {
public:
  InvokeReply<R> invoke(const A&,const invocation::InvokeOptions&) const;
};
```

以上Host、Session、Bound不可复制；Session/Bound可移动。HostOptions暴露的Native配置、投影和选项应通过公开别名或公开窄声明获得，不要求应用直接include detail，合并时必须明确具体位置。NativeHost无裸PolicyStore/administration/Engine getter。身份认证由固定可信端口完成，不存在request.trusted绕过。Host的Native执行访问源由内部明确不可用实现提供，find/scan均返回能力不可用，绝不生成假ExecutionRef、空任务列表或空Document。

HostSnapshot至少保存phase、quiescent、active_admissions、首要启动错误、已成功模块数、清理错误总数/截断及拷贝数量。CleanupError为固定Name+ErrorCode，不无界复制Error详情；ShutdownReport保存quiescent、phase、待清理数量、是否超时及首要错误，详细错误由span快照读取。所有报告必须区分非quiescent、已排空但有错误及完全成功。计数达到上限饱和并显式显示，禁止回绕。

## 准入与所有权

Host内部同一短mutex仲裁Ready检查、active_admissions增量以及StopAccepting转换；用户代码一律在释放锁之后执行。HostBound每次invoke先取HostAdmission RAII，再调用原NativeBound完整治理，最终返回对象构造完成后才释放宿主准入。bind/open/verify中可触发可信端口的部分同样持有有界准入但不持锁，防止shutdown销毁所借材料。Host准入不能替代Policy逐次检查。

HostBound持有宿主控制块和原NativeBound，确保调用栈内释放外部Host/Session/Bound句柄时材料不悬空。停后旧Bound明确拒绝，不能重新唤醒Ready。旧绑定可能继续持有不可变目录/类型及控制块，单列保留内存，最后Bound释放才回收；业务模块只能在所有实际准入归零后stop。测试与footprint显式销毁绑定以测完整回收，另列持有绑定的保留曲线。

shutdown先原子StopAccepting，NativeSubset只等待已接受短调用结束，不伪造取消任务。以真实steady_clock截止时间和condition_variable有界等待，超时返回非quiescent快照且全部依赖保持存活。再次shutdown可继续。来自当前同Host准入栈的shutdown返回ReentrantShutdown，不能自等待；使用无分配TLS链识别嵌套Host，不能只用单一布尔。

析构要求已Stopped或构造/启动失败后已安全清理；否则明确invariant终止，不能detach或隐式释放活依赖。测试以独立子进程验证，记录真实终止点。未shutdown析构不是成功停止，文档和示例必须明确调用shutdown。模块停止不quiescent/抛异常时停在该逆序节点，不先停它依赖的模块；所有已停步骤不重复执行。首要启动错误与清理错误分别保留。

## 启动与失败窗口

create校验非零、有限、乘加不溢出的模块/会话/绑定/观察/诊断/准入预算，预分配有界清理记录与控制块。add深拥有ModuleInput容器及生命周期owner，错误粘性；超过容量明确拒绝。start仅一次，失败不能原地重试构建部分目录。

实际顺序：收集冻结配置/模块 → 原Registry验证DAG/声明/注册并构造私有候选 → 验证NativeSubset仅Read/Compute、无资源Provider依赖/async/external_wait → 真实PolicyStore创建 → 按目录拓扑start模块 → 校验全部必需端口已可用 → 发布Ready。每一成功获得的内部所有者入RAII清理链；失败只撤销已获得部分，保存首因，必要日志不影响业务事实。普通日志失败只增加诊断错误计数，不使成功业务Outcome变失败。

NativeEngine按成功打开的受限Session创建，创建失败立即关闭该未交付Session；各session总容量由PolicyBudget+Host预算控制，禁止同Session反复创建无界Engine。Session关闭后原Bound每次Policy准入拒绝，闭包仍保持寿命。NativeOnlyExecutor只报告不支持submit；注册配置中不允许async/external_wait。该拒绝端口没有worker、队列或Task，不能因接口存在宣称异步。

## 具体反例与通过证据建议

- 每一实际启动窗口：零/溢出预算、重复模块/缺依赖/循环/忽略注册错误、不可用shape或资源、非法Policy配置、模块第k项返回错误/抛异常；未Ready时Handler进入次数始终0，首因与逆序成功清理列表一致。
- 多模块依赖顺序固定；失败模块start自行回收，Host不误调用其stop；已成功模块stop返回错误但quiescent可继续，非quiescent/异常不得提前销毁依赖，重试不重复已停步骤。
- start/stop回调重入snapshot、bind/open/start/shutdown，业务明确NotReady/HostDraining或Busy且不死锁；用另线程受控探针证明回调时锁可取得。
- 真实双线程：一个Handler用latch持有准入，shutdown到期非quiescent，随后新invoke拒绝；释放Handler后shutdown完成、模块依赖才释放。反向次序StopAccepting先赢时Handler为0。实际accepted工作返回原事实，不以停止意图改成Cancelled。
- Handler内shutdown拒绝自等；独立子进程验证未排空析构和handler内自毁为真实fail-fast，不当作成功。
- 真实HostSession认证、权限/目标/撤权、两个Read/Compute、无效参数及不可用async；既有D1.05全部回归保留，Host不能提供裸分派。
- 受限固定输入40次完整HostBound调用含宿主准入/Native治理/结果构造/观察/释放为零新增分配；正负探针、首用/错误/可变结果/最后Bound回收分列。
- 搬迁安装树、BUILD_TESTING=OFF、只Runtime/CoreContracts/Foundation/expected，真实stateless_service运行及能力输出；Data/State/SQLite/Asio不可进入闭包，无源码头或私有测试夹具引用。
- SDK具体声明与分类/宏/组件配置审查；Runtime NativeSubset可用，未实现其他组件仍拒绝；无历史正式SDK基准如实NA。G1 API表面只能声明本轮实际实现。

本候选仍需与Logging/footprint候选合并，消除公开类型名、启动顺序和验证覆盖差异，再独立AI规格审核与固定expected冻结。没有预算数值批准或D1.06行为Passed。

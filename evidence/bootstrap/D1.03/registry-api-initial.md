# D1.03 注册API草案（待AI规格复核）

本文件仅具体化D1.03，不代表公开SDK Runtime可用。命名空间为 `ock::runtime::registry`，内部头位于 `packages/runtime/registry/registry.hpp`；不安装该头。当前阶段的完整发布仅表示注册目录冻结，不表示Host Ready。

## 1. 基础类型与所有权

沿用 `contracts::Name/OperationKey/OperationVersion/DefinitionInput/DefinitionSnapshot/CppTypeToken/BindingPort` 与 `foundation::RegistryId/RegistryGeneration`。不新增另一套Operation身份、错误传输或Result。

`RegistryErrc` 为独立 `ock.registry` ErrorDomain，至少覆盖 InvalidManifest、DuplicateModule、MissingDependency、VersionMismatch、DependencyCycle、UndeclaredOperation、MissingOperation、DuplicateOperation、InvalidDefinition、MissingService、ServiceTypeMismatch、MissingProvider、ProviderMismatch、MissingExecutor、CapabilityMismatch、MissingResource、MissingConfiguration、UnsupportedSchema、CallbackException、BudgetExceeded、Frozen、InvalidIdentity。诊断 `RegistrationError { RegistryErrc code; optional<Name> module; optional<OperationKey> operation; }` 自有值，不保存异常what指针。

`BatchBudget { size_t modules=128, operations=4096, declarations=16384, text_bytes=1048576, diagnostics=128; }`，零限制拒绝创建；所有数量增加先做checked比较，不乘加溢出。诊断截断只限制记录数，单独的failed位一经置位不可复位；`diagnostics_truncated()`明确截断。

`ModuleDependency { Name name; OperationVersion version; }` 为精确模块版本需求；同一批次同模块name只允许一个版本。操作键允许同name不同version共存，精确key重复拒绝。

## 2. 提供项和声明

`ModuleManifest` 自有以下vector：模块 `Name name`、`OperationVersion version`、`dependencies`、`operations`（OperationKey）、`services`（Name）、`providers`（AtomicProviderKey）、`resources`（Name）、`required_configuration`（TypeIdentity）。还包括本模块所需服务的 `ServiceRequirement {Name module; Name service; CppTypeToken type;}` 与资源的 `ResourceRequirement {Name module; Name resource;}`。跨模块提供者必须为直接声明依赖，不能越过DAG偷取全局服务。需要的provider同样绑定声明提供者模块及精确key/type。

`ServiceBinding::make<T>(Name, shared_ptr<T>)` 返回Result；私有构造保存实际CppTypeToken和owner，拒绝空owner。T为非const对象类型。模块输入的同名服务必须恰好与manifest提供集合一致，不允许声明无实现或实现未声明。

`ProviderBinding::make<P>(shared_ptr<contracts::AtomicProviderPort<P>>)` 由 `ProviderContract<P>::key()` 和 `CppTypeToken::of<P>()`产生匹配信息，拒绝空owner，不接收可随意自报的type token/void owner组合。

`ResourceBinding {Name name; shared_ptr<contracts::ResourceLease> owner;}` 表示注册阶段已装配资源槽的寿命，不是调用授权；必须非空且与manifest一致。调用时租约与授权仍由后续管线取得，不能把该声明当许可。

`ConfigurationBinding::make<T>(T value)` 仅接受ContractValue<T>且可拷贝对象；内部调用TypeContract<T>::validate，并建立内部const副本，记录TypeIdentity。后续调用者修改原value不能改变配置快照。仅支持typed native配置，本包不解析TOML/JSON。声明的TypeIdentity必须全部有精确匹配，错误/遗漏拒绝。

`ExecutorBinding {Name name; Name affinity; bool async_dispatch; bool external_wait; shared_ptr<contracts::ExecutorPort> owner;}` 由组合根提供，必须有真实非空端口；本包验证静态装配要求而不启动线程或调用submit。不能把该检查称作D3执行器Conformance。

`ModuleInput` 自有manifest及以上提供项vector、`std::function<void(Registrar&)> register_operations`。注册钩子复制其函数对象；其显式借用捕获由可信模块负责保持至同步钩子执行结束，不能宣称可以深拷贝任意C++闭包引用。manifest、提供项vector及配置快照不借用调用者内存。注册后Catalog保有被绑定服务/provider/executor/resource owner。

## 3. 批次状态

```cpp
class RegistrationBatch final {
public:
  static Result<std::unique_ptr<RegistrationBatch>> create(BatchBudget);
  Result<void> add(ModuleInput); // 深保存声明、候选提供项和钩子
  Result<std::shared_ptr<const Catalog>> publish();
  std::span<const RegistrationError> errors() const noexcept;
  bool failed() const noexcept;
  bool diagnostics_truncated() const noexcept;
};
```

批次不可复制/移动。仅组合根持有RegistrationBatch；模块钩子仅得到Registrar。状态为Collecting→Validating→Published或Failed，所有失败为终结状态；成功目录只在全部验证/钩子/后验证成功后返回。publish重入或二次调用拒绝。add在非Collecting拒绝，不改变已发布目录；失败后不能清空错误重新开始，需创建新批次。

publish顺序：验证预算和精确manifest集合→DAG拓扑→逐模块验证提供项及依赖可用性→执行钩子、记录所有错误/异常→核对每模块实际与声明操作集合→构建完整冷/热候选→一次返回const Catalog。错误可继续收集但不得继续调用具有未满足依赖的模块钩子；全部批次仍失败，不存在部分发布。任何异常路径无部分可见状态，捕获模块异常记CallbackException；分配异常不伪造成业务成功，可传播但候选由RAII释放。

诊断span仅在批次稳定且存活时可用，是同步查看接口。调用者不得并发修改批次；冻结Catalog允许并发只读。批次不保存或公开之前发布目录的可变指针。

## 4. Registrar窄入口

`OperationOptions { vector<Name> resources; bool requires_dynamic_schema=false; }` 是操作声明的静态资源需求，必须绑定当前模块/已声明依赖的资源；不表示执行授权。requires_dynamic_schema=true在本阶段记录UnsupportedSchema，直到后续真实Schema能力实施才可另案扩展。

Registrar由批次私有构造，禁止复制/移动，寿命仅限同步钩子。所有注册入口返回Result<void>并由批次内部粘性记录错误：

- `read<A,R,Reader>(Result<R>(*)(const A&,WorkContext&,ReadServices<Reader>&), const DefinitionInput&, const OperationOptions&)`。
- `compute<A,R>(Result<R>(*)(const A&,WorkContext&), const DefinitionInput&, const OperationOptions&)`。
- `state_edit<A,R,P>(Result<R>(*)(const A&,EditView<P>&,WorkContext&), const DefinitionInput&, const OperationOptions&)`。
- `external_effect<A,R>(EffectReport<R>(*)(const A&,EffectContext&), const DefinitionInput&, const OperationOptions&)`。
- `lifecycle<A,R>(TransitionReport<R>(*)(const A&,TransitionView&), const DefinitionInput&, const OperationOptions&)`。
- `candidate_read<A,R,Reader,P>(read函数指针, Result<R>(*)(const A&,const P::CandidateReadPort&,WorkContext&), const DefinitionInput&, const OperationOptions&)`：内部构建Read definition再调用candidate转换，双方类型与ProviderContract匹配才注册。
- `service<T>(const Name& provider_module,const Name& service_name)` 返回Result<shared_ptr<T>>；仅当前模块声明所需服务可查找，结果类型与真实提供者相同且保有owner。不提供全局遍历/resolve-anything。

以上A/R约束沿用ContractValue/ContractResult；不能提供接收任意DefinitionSnapshot与void handler的公共“万能注册”入口。非法签名编译拒绝；空fn、工厂返回错误、未声明key、重复key、缺执行器/资源/provider都记入同一批次。DefinitionInput由D1.02工厂复制，调用者后续修改不能改变目录。

ReadServices<Reader>的Reader类型必须匹配当前模块声明服务；compute无需只读服务。StateEdit和candidate必须匹配当前模块已绑定真实provider的key和P类型。执行要求的executor/affinity精确匹配，并检查async/external_wait需求；Atomic仍使用D1.02的约束拒绝等待/异步。每个成功条目保存其所需依赖owner，后续调用无需动态定位。

Handler存入内部私有类型擦除owner，记录真实函数指针类型。Registrar不返回handler或内部槽；Catalog无handler获取方法；D1.05另行增加只给内部Invocation的受控适配。不能用测试专用public getter破坏本包边界。

## 5. Catalog与冷/热分离

```cpp
class Catalog final : public contracts::BindingPort {
public:
  RegistryId identity() const noexcept override;
  RegistryGeneration generation() const noexcept override;
  std::size_t size() const noexcept override;
  Result<OperationHandle> find(const OperationKey&) const override;
  Result<std::shared_ptr<const DefinitionSnapshot>> describe(std::uint32_t) const override;
};
```

Catalog只有批次可构造。identity来自当前进程内部不复用的单调64位发行序号编码为Tagged128，序号耗尽拒绝，不回绕；发行器不对模块开放。generation为该不可变目录内部有效非零世代，不提供更新接口；Native目录身份不作为可持久化跨进程票据。不同Catalog不可接受相同RegistryId，即使操作文本完全相同。BoundOperation按既有resolve_slot检查身份/世代/范围后访问describe；测试旧世代/伪槽使用真实Catalog身份与伪handle对照，不为测试暴露可变世代。

内部 `HotEntry` 只保存shape、私有handler owner与CppTypeToken、已绑定的服务/provider/executor/resource owner及执行标量。不得保存Docs、DefinitionSnapshot或指向冷描述的快捷指针。完整DefinitionSnapshot与精确key索引在独立cold结构；Catalog同时拥有两组存储，但热槽不可遍历冷Docs。结构检查与源依赖检查验证这一点；绑定/describe不是Invoke热路径，本包不宣称已测零分配Invoke。

describe返回不可变自有snapshot；目录销毁后已取得snapshot仍有效。BoundOperation保有Catalog，Catalog保有依赖，因此批次/输入销毁后绑定仍可校验。没有直接可写的冷热vector引用。

## 6. 验证和审核

25项新增测试名称及对应反例见D1.03计划。首轮前须冻结本API和expected，补足本草案被审核发现的缺口。实际实现与反例均要独立AI规格/代码复核。整包Passed只能来自固定完整三配置、9项CHECK、原始owned Job/编译正负例/源码与Git一致性；尚无任何D1.03实际实现通过记录。

# D1.03 早期独立代码/规格检查

actor_type：AI；review_contracts。结论：发现3项需要修复的P2；此为早期检查，不作最终Approved。受审源码及具体API已逐文件复制至本目录，精确SHA见source-inputs.json；后续实现代理的改动不自动继承本报告。只读源文件，没有与实施者并发大规模编译。

## P2-1：add移动输入缓冲，保留调用者的可变元素别名

位置：快照 registry.cpp:50（`modules_.push_back(std::move(m))`），registry.hpp 的 `add(ModuleInput)`。

按值参数不等于深保存。调用者先保存 `auto* slot=&m.manifest.operations[0]`，再 `b.add(std::move(m))`，该指针仍指向已移入批次的vector元素。修改 `*slot` 会改变候选声明；dependencies、提供服务/资源/executor等vector同样可被外部保留元素地址再改写。这违背已冻结API的manifest及提供项集合不借用调用者内存、后续修改不能改变批次的要求。

反例：operation_module声明key K并固定钩子注册K，保存operations[0]地址后move-add，外部将该元素改成K2。正确深保存应仍发布K成功，现实现会认为K未声明并失败。也可以保存 resources[0]后move-add再reset owner，令批次未发布前的真实装配被外部改变。现 manifest_owned_budget 仅验证lvalue add后修改原对象，未触发移动缓冲别名。

修复应复制拥有型声明/标准序列及提供项条目至批次自有存储，保留各服务owner的受信共享寿命语义；不能只把整ModuleInput再次move。增加移动输入后改写旧元素指针的控制反例。

## P2-2：其他模块的操作会满足本模块的遗漏检查

位置：快照 registry.cpp:125，遗漏检查对全局 cold_按OperationKey查找，没有绑定注册模块。

反例：模块a声明K并注册K；模块b也声明K，但register_operations为空。二者模块身份不同且无DAG冲突，单模块声明集合各自唯一；a成功后cold_含K，b的缺失检查找到a的K，publish便可能成功。违反精确操作key全局唯一以及每个模块必须实际提供自己声明操作的规则。现 exact_operation_versions 只测试两个模块都实际注册同K，第二次insert碰重复才失败，漏掉这个无需第二次insert的路径。

应在批次manifest验证阶段拒绝跨模块重复精确操作声明，并按模块归属/该模块本次成功登记集合检查完整性，不能用全局存在代替本模块注册成功。增加“第二模块仅声明同key、空钩子”的反例，拒绝时不发布。

## P2-3：注册条目只计Docs字节，未覆盖其余声明预算

位置：快照 registry.cpp:87与106，registry.hpp:129（先调用Definition工厂再进入insert）。

add统计manifest文本，但操作DefinitionInput中的required_permissions、execution.thread_affinity等新增文本没有计入text_bytes，OperationOptions中的refs/数量也没有统一计入declarations；insert只追加docs字节。多个操作可各自携带合法但很大的权限名集合，在manifest及Docs预算未超限时发布远超BatchBudget.text_bytes的冷描述材料。D1.02单条工厂96项权限上限不是整个批次的总文本/声明预算。当前也先创建并复制完整DefinitionSnapshot，然后才核对Docs总预算，未落实预算先检后分配的承诺。

可证控制：按实际manifest开销设置小BatchBudget.text_bytes，操作Docs置空，添加一个大权限Name（仍在Name与D1.02合同合法范围内），其他输入保持合法。正确实现应BudgetExceeded；现唯一涉及该字符串的地方是D1.02定义复制，没有批次charge，因此会通过。继续添加不同操作会累积绕过总预算。

应明确并实现输入计费口径，在进入Definition复制/资源去重的昂贵操作前检查文本与声明数量，并以checked累加处理。增加“manifest预算足够但权限/选项累计超限”的反例，确认失败粘性及无发布。

## 其他检查及尚未完成的验证

- 当前Module限定服务/provider/executor/resource选择与required集合校验未见另一个确定归属缺陷；配置freeze没有默认copy回退且冻结结果再验证，符合已批准具体API。
- fail先无分配置failed，诊断分配异常转truncated，入口catch保持失败；已见真实故障注入测试，而非仅自报capability。
- Catalog构造私有，冷热存储分开，Handler无Catalog公开getter；热槽只含执行信息/owner。RegistryId CAS序号在max拒绝，不回绕，静态路径正确；当前25主体尚未实际覆盖耗尽边界，不把未测分支记为Passed。
- wrapper尚未完成：目前shape_compile_contract只运行正例，handler_not_exposed仅断言Registrar不可外部构造，cold_docs_separation未检查HotEntry类型，internal_component_boundary未真实检查SDK/依赖。它们尚不足以独立证明对应25个声明，待主任务wrapper/结构检查完成再评价，不作为本轮“最终验收失败”结论。
- 本轮发现按快照静态可证，不声称已独立编译或实际执行新反例。建议实施者先新增上述反例，保留red，再修复重跑，之后按新SHA最终独立审核。

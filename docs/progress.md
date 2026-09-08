# 内核实施进度

更新：2026-09-08。唯一规范为 [架构 v3.3-r2](01_Architecture_v3.3.md) 与 [执行计划 v3.3-r2](02_Execution_Plan_v3.3.md)。恢复任务默认只读本文件顶部、当前 Development Batch 的相关规范/合同和 diff；历史 review/evidence 按需读取。

## 当前生产节点

- 当前分支：`work/d0-kernel-baseline`；恢复基线核对到 `d8a351e`，实际执行以实时 Git 为准。
- 已 Passed：G0、G1、D0.01–D0.06、D1.01–D1.06。**这些 Passed 前置不重验**；D1.05 不重跑。
- 当前 Development Batch：**B1 / D1.06 已完成并 Passed**；下一批 **B2 / D2.01–D2.04 NotStarted**。
- 当前门禁：**G1 Passed**；G2–G8 未开始。
- Host、Logging、NativeSubset/独立安装消费者及 footprint 正式测量/预算已交付并完成本包验收。
- 本次恢复已完成：精确工具链校准、三配置编译、Debug 270/270、Release 192/192、ASan 193/193 及各 3 个 CHECK；七模式正式测量均 Passed。D1.06/G1 共用同三份报告，来源 `0452fad`，见[本次交付与自动验收](validation/D1.06-resume-delivery.md)。
- 工具优化阶段已结束；禁止继续把 Fixture、Evidence、Install Consumer 性能优化或新流程文档设为 D1.06 前置。

## 当前执行硬规则

1. Passed 前置默认只消费状态和公开合同；当前修改触及其输入或发现证据损坏时，仅验证受影响范围，不重跑历史包矩阵。
2. Batch 内开发依赖使用 `Implementation-Ready`：上游所需接口已实现并通过直接验证即可继续写下游；它不是正式包状态。
3. 正式 Passed 仍按原 DAG 顺序：前包未 Passed，后包可以有代码但不能最终 Passed。
4. Batch 内开发跑 `S_changed`；批次稳定后集中 `S_required + SPEC/CODE`；阶段 Gate 跑 `S_gate`。
5. 同一最终 source/profile/build 的物理 configure/build/test 尽量一次执行并映射多个逻辑 task；不因包编号重复运行。
6. Review 一次加载批次上下文，输出各包独立结论；Fast 中间项不单独审核。
7. G Gate 承担阶段完整历史集成；普通包不机械累计所有历史 Passed 用例。

## Development Batch 路线

| Batch | 工作包 | 目标 |
|---|---|---|
| B1 | D1.06 | Host + Logging + NativeSubset + G1 |
| B2 | D2.01–D2.04 | Data/Binding/Catalog/Control |
| B3 | D2.05–D2.07 | IPC/CLI/Shell + G2 |
| B4 | D3.01–D3.03 | Executor/Scheduler/Resources |
| B5 | D3.04–D3.07 | Submit/Cancel/Lifetime/Observation + G3 |
| B6 | D4.01–D4.04 | Snapshot/Edit/Commit/Atomic |
| B7 | D4.05–D4.08 | Plan Compiler/Runner/Control Flow + G4 |
| B8 | D5.01–D5.04 | Storage/Canonical/Intent/DurableAccepted |
| B9 | D5.05–D5.09 | Durable Bridge/Effect/Outbox/Recovery + G5 |
| B10 | D6.01–D6.08 | Assets/Workspace/DurablePlan + G6 |
| B11 | D7.01–D7.06 | Client/Preview/Approval/MCP/AI + G7 |
| B12 | D8.01–D8.07 | Final validation/performance/docs/RC + G8 |

## 阶段门禁

| Gate | 状态 |
|---|---|
| G0 | Passed |
| G1 | Passed |
| G2 | NotStarted |
| G3 | NotStarted |
| G4 | NotStarted |
| G5 | NotStarted |
| G6 | NotStarted |
| G7 | NotStarted |
| G8 | NotStarted |

## 工作包状态索引

> 本表用于状态/前置快速读取；不要求恢复任务逐项打开历史证据。默认级别来自执行计划 E04，Batch 来自 E02。

| 工作包 | 名称 | 正式前置 | 状态 | 级别 | Batch |
|---|---|---|---|---|---|
| D0.01 | 登记需求、不变量和三个消费者 | 无 | Passed | Fast | 历史已完成 |
| D0.02 | 冻结target DAG、公开头和威胁模型 | D0.01 | Passed | Standard | 历史已完成 |
| D0.03 | Outcome、phase和完成回调参考模型 | D0.01 | Passed | Critical | 历史已完成 |
| D0.04 | Plan与Control wire、槽类型和观察合同 | D0.01、D0.03 | Passed | Critical | 历史已完成 |
| D0.05 | 提交、许可、epoch和备份恢复参考模型 | D0.01、D0.03 | Passed | Critical | 历史已完成 |
| D0.06 | 固定工具链、共享测试基建与自动证据采集 | D0.02 | Passed | Standard | 历史已完成 |
| D1.01 | 实现Foundation与错误/标识原语 | D0.03、D0.06 | Passed | Standard | 历史已完成 |
| D1.02 | 实现CoreContracts、四种shape与typed绑定 | D1.01、D0.02、D0.03、D0.05 | Passed | Standard | 历史已完成 |
| D1.03 | 实现注册批次与不可变目录绑定 | D1.02 | Passed | Standard | 历史已完成 |
| D1.04 | 实现授权主体、资源解析契约和许可原语 | D1.02、D0.05 | Passed | Critical | 历史已完成 |
| D1.05 | 实现Native Invocation与无任务短路径 | D1.03、D1.04 | Passed | Standard | 历史已完成 |
| D1.06 | 最小Host、Logging共同合同与原生占用基线 | D1.05、D0.06 | Passed | Standard | B1 |
| D2.01 | 实现单DOM Payload、View与预算构建 | D1.01、D0.06 | NotStarted | Standard | B2 |
| D2.02 | 实现TypeContract、Schema编译与Native等价绑定 | D2.01、D1.02、D1.05、D0.04 | NotStarted | Standard | B2 |
| D2.03 | 实现能力目录、精确命令卡和帮助导出 | D2.02、D1.03、D1.04 | NotStarted | Standard | B2 |
| D2.04 | Control方法、观察协议帧与结果映射 | D2.02、D1.04、D0.03、D0.04 | NotStarted | Critical | B2 |
| D2.05 | 实现真实Named Pipe与认证会话 | D2.04、D0.06 | NotStarted | Critical | B3 |
| D2.06 | 实现薄CLI、意图文件和原生/动态演示 | D2.03、D2.05、D1.06 | NotStarted | Standard | B3 |
| D2.07 | 验收双入口与首次Shell闭环 | D2.06 | NotStarted | Standard | B3 |
| D3.01 | Executor Conformance Kit与生产/测试后端 | D1.02、D0.06 | NotStarted | Critical | B4 |
| D3.02 | 实现公平Ready调度与依赖结构 | D3.01、D1.03 | NotStarted | Critical | B4 |
| D3.03 | 实现资源归一化、MultiClaim与租约 | D3.02、D1.04 | NotStarted | Critical | B4 |
| D3.04 | Submit、执行投影索引与拥有型输入 | D3.02、D3.03、D1.05 | NotStarted | Critical | B5 |
| D3.05 | 实现取消、期限与permit竞争 | D3.04、D0.05 | NotStarted | Critical | B5 |
| D3.06 | 实现父子寿命、Finalizing与失败收尾 | D3.04、D3.05 | NotStarted | Critical | B5 |
| D3.07 | 真实任务观察CLI、完整Embedded占用与停止门禁 | D3.06、D2.07 | NotStarted | Critical | B5 |
| D4.01 | 实现轻量状态域与结构共享Snapshot | D1.02、D0.06、D3.03 | NotStarted | Standard | B6 |
| D4.02 | 实现EditView、WriteSet、约束与内存History | D4.01 | NotStarted | Standard | B6 |
| D4.03 | 实现内存CommitCoordinator与发布gate | D4.02、D3.05、D0.05 | NotStarted | Critical | B6 |
| D4.04 | 实现独立编辑与Atomic组同源绑定 | D4.03、D1.05 | NotStarted | Critical | B6 |
| D4.05 | 实现PlanCompiler、slots与预算IR | D2.02、D0.04、D3.04 | NotStarted | Standard | B7 |
| D4.06 | 实现顺序Call/Await与Atomic调度 | D4.05、D4.04、D3.06 | NotStarted | Critical | B7 |
| D4.07 | 实现If/ForEach/Parallel与失败收尾 | D4.06 | NotStarted | Critical | B7 |
| D4.08 | 验收内存套件、Shell Plan和无文档Atomic | D4.07、D3.07、D2.03 | NotStarted | Critical | B7 |
| D5.01 | Storage Conformance与真实SQLite独占 | D0.05、D0.06、D3.01 | NotStarted | Critical | B8 |
| D5.02 | 实现记录schema、codec和canonical指纹 | D5.01、D2.02 | NotStarted | Critical | B8 |
| D5.03 | 实现外部Intent claim与epoch GC | D5.02、D1.04 | NotStarted | Critical | B8 |
| D5.04 | 实现DurableAccepted与任务记录收尾 | D5.03、D3.06 | NotStarted | Critical | B8 |
| D5.05 | 实现StateDurableBridge与无撕裂发布 | D5.04、D4.03 | NotStarted | Critical | B9 |
| D5.06 | 实现ExternalEffect claim、许可和对账 | D5.04、D1.04、D0.03 | NotStarted | Critical | B9 |
| D5.07 | 实现Outbox、pins与有界记录回收 | D5.05、D5.06 | NotStarted | Critical | B9 |
| D5.08 | 实现数据库恢复、备份和RestoreGeneration | D5.05、D5.06、D5.07 | NotStarted | Critical | B9 |
| D5.09 | 执行独立进程Durable门禁 | D5.08、D4.08 | NotStarted | Critical | B9 |
| D6.01 | 实现资产Storage与类型codec | D4.02、D5.02 | NotStarted | Critical | B10 |
| D6.02 | 实现资产pins、快照材料和备份一致性 | D6.01、D5.07、D5.08 | NotStarted | Critical | B10 |
| D6.03 | 实现Project/Document组织与关闭协调 | D4.03、D3.06、D5.05 | NotStarted | Critical | B10 |
| D6.04 | 完成持久History、Undo/Redo与状态消费者 | D6.02、D6.03 | NotStarted | Critical | B10 |
| D6.05 | 实现DurablePlan运行驱动与检查点codec | D4.07、D5.04、D5.07 | NotStarted | Critical | B10 |
| D6.06 | 实现内部StepKey、ChildAdmission与父落后恢复 | D6.05、D5.03、D5.06 | NotStarted | Critical | B10 |
| D6.07 | 实现resume、重试、补偿与并行恢复 | D6.06、D3.06 | NotStarted | Critical | B10 |
| D6.08 | 完成三个消费者与长寿命恢复门禁 | D6.04、D6.07、D5.09 | NotStarted | Critical | B10 |
| D7.01 | 完成统一Client SDK与PlanBuilder | D4.08、D5.03、D6.07 | NotStarted | Standard | B11 |
| D7.02 | 实现Check与PreparedChange Preview/Apply | D6.02、D6.04、D7.01 | NotStarted | Critical | B11 |
| D7.03 | 实现受限委托、可信批准与撤权闭环 | D7.02、D1.04、D5.06 | NotStarted | Critical | B11 |
| D7.04 | 实现MCP/模型投影和命令资料生成 | D2.03、D7.01、D7.03 | NotStarted | Standard | B11 |
| D7.05 | 结果投影、分页与既有观察协议压力集成 | D6.02、D7.01、D5.07、D3.07 | NotStarted | Critical | B11 |
| D7.06 | 执行AI确定性场景集与真实适配验收 | D7.04、D7.05、D6.08 | NotStarted | Standard | B11 |
| D8.01 | SDK公开表面、冻结消费者与安装兼容门禁 | D6.08、D7.05、D0.06 | NotStarted | Standard | B12 |
| D8.02 | 执行Profile矩阵与缺组件负例 | D8.01、D7.06 | NotStarted | Standard | B12 |
| D8.03 | 模型、属性、fuzz与内存/并发检查 | D7.06、D6.08 | NotStarted | Critical | B12 |
| D8.04 | 全故障窗口与恢复/存储组合复验 | D8.02、D8.03 | NotStarted | Critical | B12 |
| D8.05 | 固定占用、零分配范围与性能容量正式定案 | D8.04、D7.06 | NotStarted | Standard | B12 |
| D8.06 | 资料、Schema、例子、错误手册最终一致性 | D8.02、D8.05 | NotStarted | Fast | B12 |
| D8.07 | 发布候选、证据索引和最终放行 | D8.04、D8.05、D8.06 | NotStarted | Critical | B12 |

## 历史材料读取规则

- 详细 D0–D1 开发/失败/验收过程由 Git 历史、`docs/validation/**`、`docs/reviews/**`、`evidence/**` 保留；本 progress 不再复制长篇历史。
- 只有当前语义疑点、回归、证据完整性审计或正式 Gate 要求时才展开历史材料。
- 状态变化只根据真实实现、正式运行和审核更新；不得以本文件文字替代机器事实。

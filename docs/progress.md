# 内核实施进度

更新日期：2026-09-07。唯一规划仍为 [架构 v3.3](01_Architecture_v3.3.md) 与 [执行计划 v3.3](02_Execution_Plan_v3.3.md)，不新增或重新编号工作包。

## 当前节点

- D0.01 已收到用户“批准”，包级状态 Passed；[批准记录](reviews/D0.01-approval.json) 绑定已审查提交。D0.02/D0.03 也已收到用户“继续”作为本轮验收批准；D0.04–D0.06已完成AI复核与自动验收，G0 Passed。
- 当前分支：`work/d0-kernel-baseline`；远端：`https://github.com/super1wang/Kernel_v2.git`。用户已授权必要节点提交和推送。
- 首个本地节点：`cce1db5`，保存两份原始规范及范围约束。该基线已用已登录且具写权限的 XU-RUiXIANG 推送到同名远端分支；默认账号 XU-Bruce 的首次推送返回 403，未写入远端。后续节点以 Git 实际日志为准。
- D0.02 已交付 21 个实际 CMake 合同库、版本元数据安装验证及 29 项架构检查；技术复核通过、人工评审已批准。未实现 Runtime 或产品模块；52 个 future_cases 仍是未来预期。
- 当前已完成 D0.04（Plan/Control 合同）、D0.05（提交/epoch/恢复模型）及 D0.06（工具链/证据/Conformance 基建）的实现与技术复核；G0 放行后进入后续原生实现。

D0.02 材料：[评审](reviews/D0.02.md)、[验证索引](validation/D0.02.md)。D0.03 已交付 Outcome/phase 模型与 Schema，43 项模型 CTest、54 golden 及 6 项适配器检查通过，技术复核及人工评审已完成；见 [评审](reviews/D0.03.md) 与 [验证索引](validation/D0.03.md)。

材料：[需求登记](requirements.md)、[消费者目标](consumer-targets.md)、[D0.01 评审](reviews/D0.01.md)、[原始证据说明](../evidence/README.md)。

## D0.04–D0.06 当前交付

- D0.04：Plan/Control Schema、中文合同、无状态认证cursor与参考模型已完成；独立审查补齐客户端终态/Host、同版本完整快照、绑定后Domain校验及退订在途帧，53项逐项验证通过，已按用户明确政策完成AI复核与自动验收。
- D0.05：提交/许可/Intent/恢复模型及中文合同已完成；独立审查修复材料借用、未知盲重发及Effect一次发送边界，63项模型通过，实际产生的28份提交/29份效果Outcome通过D0.03校验，已按用户明确政策完成AI复核与自动验收。
- D0.06-a：十二项固定依赖与六项Python开发wheel锁已核验；独立空目录Debug/Release各15项probe、Embedded ASan8项通过。全部只是开发probe，不声明生产后端。
- D0.06-b/c：正式进程Job包装、来源/实际构建身份、固定expected与各轮JUnit、原始证据归档、门禁汇总、历史Incomplete导入及mock/fault共同harness已接入。统一G0工程Debug预构建成功；最终9组包/配置已全部通过，共372次CTest；来源142项与194dcdf提交逐字节一致，G0已按自动验收政策通过。
- 本批独立审查记录位于 `evidence/bootstrap/D0.04/independent-review.md`、`D0.05/parent-final-review.md`、`D0.06-a/independent-review.md`、`D0.06-b/independent-review.md`。所有失败轮次保留，旧批准不代替本轮新增实现审批。

## 已提交验证节点（历史记录）

- 登记交付：`f08519a`；原始日志字节保留修复：`3c413bd`；干净源码证据：`af64fa9`，均已推送同名工作分支。
- 最终运行来源为 `3c413bd`，dirty=false；登记检查和 29 项校验器测试均退出 0。见 [最终原始记录](../evidence/bootstrap/D0.01/20260907T033057Z-4ff7dc85a3b1/commands.json)。
- 6 个 bootstrap run 全部保留；最终提交的 24 份原始日志均核对了 Git blob 的长度与 SHA-256。当时D0.01人工评审已批准，G0尚未通过；当前G0已Passed，见门禁表。

- D0.02/D0.03 实现与资料节点：`8246ef6`，已提交并推送；两包最终 bootstrap 的全部源输入与该 Git 提交逐一匹配，原始日志及构建摘要核对一致。
- 本次实际结果：D0.02 4/4 CTest（含 29 项架构检查及真实 MSVC 安装消费）；D0.03 43/43 模型 CTest、54 golden、6/6 适配器检查；D0.01 的登记及 29 项反例回归也通过。
- [提审摘要](reviews/D0.02-D0.03-submission.json) 保留历史 Pending；当前人工结论见 [D0.02 批准](reviews/D0.02-approval.json)、[D0.03 批准](reviews/D0.03-approval.json)。下一批执行范围见 [节点实施记录](plans/D0.04-D0.06.md)；不提前标 G0 Passed。

## 阶段门禁

| 门禁 | 当前状态 | 事实 |
|---|---|---|
| G0 | Passed | 九组372次CTest、142项Git来源及AI规格/代码复核齐全，按用户政策自动验收 |
| G1 | InProgress | G0已Passed，D1.01正在实现与验证；G1尚未验收 |
| G2 | NotStarted | 前序门禁尚未通过，无本阶段运行证据 |
| G3 | NotStarted | 前序门禁尚未通过，无本阶段运行证据 |
| G4 | NotStarted | 前序门禁尚未通过，无本阶段运行证据 |
| G5 | NotStarted | 前序门禁尚未通过，无本阶段运行证据 |
| G6 | NotStarted | 前序门禁尚未通过，无本阶段运行证据 |
| G7 | NotStarted | 前序门禁尚未通过，无本阶段运行证据 |
| G8 | NotStarted | 前序门禁尚未通过，无本阶段运行证据 |

## 全部工作包

状态仅使用 NotStarted / InProgress / Blocked / Failed / Passed。尚未开工的包不假标 Blocked，尚有评审缺项的包不标 Passed。自动命令事实保存在证据中，与下表分离。

| 工作包 | 名称 | 前置包 | 包级状态 |
|---|---|---|---|
| D0.01 | 登记需求、不变量和三个消费者 | 无 | Passed |
| D0.02 | 冻结target DAG、公开头和威胁模型 | D0.01 | Passed |
| D0.03 | Outcome、phase和完成回调参考模型 | D0.01 | Passed |
| D0.04 | Plan与Control wire、槽类型和观察合同 | D0.01、D0.03 | Passed |
| D0.05 | 提交、许可、epoch和备份恢复参考模型 | D0.01、D0.03 | Passed |
| D0.06 | 固定工具链、共享测试基建与自动证据采集 | D0.02 | Passed |
| D1.01 | 实现Foundation与错误/标识原语 | D0.03、D0.06 | InProgress |
| D1.02 | 实现CoreContracts、四种shape与typed绑定 | D1.01、D0.02、D0.03、D0.05 | NotStarted |
| D1.03 | 实现注册批次与不可变目录绑定 | D1.02 | NotStarted |
| D1.04 | 实现授权主体、资源解析契约和许可原语 | D1.02、D0.05 | NotStarted |
| D1.05 | 实现Native Invocation与无任务短路径 | D1.03、D1.04 | NotStarted |
| D1.06 | 最小Host、Logging共同合同与原生占用基线 | D1.05、D0.06 | NotStarted |
| D2.01 | 实现单DOM Payload、View与预算构建 | D1.01、D0.06 | NotStarted |
| D2.02 | 实现TypeContract、Schema编译与Native等价绑定 | D2.01、D1.02、D1.05、D0.04 | NotStarted |
| D2.03 | 实现能力目录、精确命令卡和帮助导出 | D2.02、D1.03、D1.04 | NotStarted |
| D2.04 | Control方法、观察协议帧与结果映射 | D2.02、D1.04、D0.03、D0.04 | NotStarted |
| D2.05 | 实现真实Named Pipe与认证会话 | D2.04、D0.06 | NotStarted |
| D2.06 | 实现薄CLI、意图文件和原生/动态演示 | D2.03、D2.05、D1.06 | NotStarted |
| D2.07 | 验收双入口与首次Shell闭环 | D2.06 | NotStarted |
| D3.01 | Executor Conformance Kit与生产/测试后端 | D1.02、D0.06 | NotStarted |
| D3.02 | 实现公平Ready调度与依赖结构 | D3.01、D1.03 | NotStarted |
| D3.03 | 实现资源归一化、MultiClaim与租约 | D3.02、D1.04 | NotStarted |
| D3.04 | Submit、执行投影索引与拥有型输入 | D3.02、D3.03、D1.05 | NotStarted |
| D3.05 | 实现取消、期限与permit竞争 | D3.04、D0.05 | NotStarted |
| D3.06 | 实现父子寿命、Finalizing与失败收尾 | D3.04、D3.05 | NotStarted |
| D3.07 | 真实任务观察CLI、完整Embedded占用与停止门禁 | D3.06、D2.07 | NotStarted |
| D4.01 | 实现轻量状态域与结构共享Snapshot | D1.02、D0.06、D3.03 | NotStarted |
| D4.02 | 实现EditView、WriteSet、约束与内存History | D4.01 | NotStarted |
| D4.03 | 实现内存CommitCoordinator与发布gate | D4.02、D3.05、D0.05 | NotStarted |
| D4.04 | 实现独立编辑与Atomic组同源绑定 | D4.03、D1.05 | NotStarted |
| D4.05 | 实现PlanCompiler、slots与预算IR | D2.02、D0.04、D3.04 | NotStarted |
| D4.06 | 实现顺序Call/Await与Atomic调度 | D4.05、D4.04、D3.06 | NotStarted |
| D4.07 | 实现If/ForEach/Parallel与失败收尾 | D4.06 | NotStarted |
| D4.08 | 验收内存套件、Shell Plan和无文档Atomic | D4.07、D3.07、D2.03 | NotStarted |
| D5.01 | Storage Conformance与真实SQLite独占 | D0.05、D0.06、D3.01 | NotStarted |
| D5.02 | 实现记录schema、codec和canonical指纹 | D5.01、D2.02 | NotStarted |
| D5.03 | 实现外部Intent claim与epoch GC | D5.02、D1.04 | NotStarted |
| D5.04 | 实现DurableAccepted与任务记录收尾 | D5.03、D3.06 | NotStarted |
| D5.05 | 实现StateDurableBridge与无撕裂发布 | D5.04、D4.03 | NotStarted |
| D5.06 | 实现ExternalEffect claim、许可和对账 | D5.04、D1.04、D0.03 | NotStarted |
| D5.07 | 实现Outbox、pins与有界记录回收 | D5.05、D5.06 | NotStarted |
| D5.08 | 实现数据库恢复、备份和RestoreGeneration | D5.05、D5.06、D5.07 | NotStarted |
| D5.09 | 执行独立进程Durable门禁 | D5.08、D4.08 | NotStarted |
| D6.01 | 实现资产Storage与类型codec | D4.02、D5.02 | NotStarted |
| D6.02 | 实现资产pins、快照材料和备份一致性 | D6.01、D5.07、D5.08 | NotStarted |
| D6.03 | 实现Project/Document组织与关闭协调 | D4.03、D3.06、D5.05 | NotStarted |
| D6.04 | 完成持久History、Undo/Redo与状态消费者 | D6.02、D6.03 | NotStarted |
| D6.05 | 实现DurablePlan运行驱动与检查点codec | D4.07、D5.04、D5.07 | NotStarted |
| D6.06 | 实现内部StepKey、ChildAdmission与父落后恢复 | D6.05、D5.03、D5.06 | NotStarted |
| D6.07 | 实现resume、重试、补偿与并行恢复 | D6.06、D3.06 | NotStarted |
| D6.08 | 完成三个消费者与长寿命恢复门禁 | D6.04、D6.07、D5.09 | NotStarted |
| D7.01 | 完成统一Client SDK与PlanBuilder | D4.08、D5.03、D6.07 | NotStarted |
| D7.02 | 实现Check与PreparedChange Preview/Apply | D6.02、D6.04、D7.01 | NotStarted |
| D7.03 | 实现受限委托、可信批准与撤权闭环 | D7.02、D1.04、D5.06 | NotStarted |
| D7.04 | 实现MCP/模型投影和命令资料生成 | D2.03、D7.01、D7.03 | NotStarted |
| D7.05 | 结果投影、分页与既有观察协议压力集成 | D6.02、D7.01、D5.07、D3.07 | NotStarted |
| D7.06 | 执行AI确定性场景集与真实适配验收 | D7.04、D7.05、D6.08 | NotStarted |
| D8.01 | SDK公开表面、冻结消费者与安装兼容门禁 | D6.08、D7.05、D0.06 | NotStarted |
| D8.02 | 执行Profile矩阵与缺组件负例 | D8.01、D7.06 | NotStarted |
| D8.03 | 模型、属性、fuzz与内存/并发检查 | D7.06、D6.08 | NotStarted |
| D8.04 | 全故障窗口与恢复/存储组合复验 | D8.02、D8.03 | NotStarted |
| D8.05 | 固定占用、零分配范围与性能容量正式定案 | D8.04、D7.06 | NotStarted |
| D8.06 | 资料、Schema、例子、错误手册最终一致性 | D8.02、D8.05 | NotStarted |
| D8.07 | 发布候选、证据索引和最终放行 | D8.04、D8.05、D8.06 | NotStarted |

## 恢复工作时的规则

先读本文件及当前包评审材料，再核对 Git 状态和真实证据；只能将已收到的人工批准写入评审记录。D0.06 前保存 bootstrap 命令事实，正式采集器完成后核对早期来源、报告与适用性。运行失败必须修复并追加证据，不能删预期、重写历史退出码或以更新文档代替实现。

本批完整验收材料见 [G0验收记录](validation/G0.md) 与 [提交审查摘要](reviews/D0.04-D0.06-submission.json)。实现节点bf122b7、来源绑定修复194dcdf已推送；原始报告与最终验收文档随本次独立证据节点提交，具体提交号以Git日志为准。

## 后续自动执行与验收政策

用户已明确要求“自我复核和自动验收，无需人工”，按[版本化政策](reviews/automatic-acceptance-policy.json)完成AI规格复核、AI代码复核和自动验收，再按必要节点提交推送。不逐节点请求人工批准，AI审核明确记录身份，不改写原始人工记录和报告。

政策工具适配已经过独立规格/代码复核及冻结版本68项回归，见 `evidence/bootstrap/D0.06-review-policy/all-final-2c8d2e4cf3`。政策与工具提交6250cd7、G0自动验收提交665febc均已推送。G0的[机器决策](../evidence/G0/automatic-20260907T080144Z/gate-summary.json)为Passed；此前人工记录转换方案被拒绝的历史已由上述明确新政策处理，不再是当前阻断。

D1.01按[实施计划](plans/D1.01.md)完成Foundation实现和独立规格/代码复核，审核发现的可变详情别名、fail-fast反例、ASan路径及多配置路由已修复。最终修复后三配置各23项原语测试、35项架构检查通过；实现节点98a406f已推送；首轮正式Debug/Release各34项通过，ASan两项消费者配置超时，整包门禁Failed并保留原始报告。已局部隔离vcpkg和用户属性注入，正在复核最终驱动及重新准备完整34/34/36矩阵，D1.01保持InProgress。详见[验证记录](validation/D1.01.md)。

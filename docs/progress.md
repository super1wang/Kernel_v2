# 内核实施进度

更新日期：2026-09-08。唯一规划仍为 [架构 v3.3](01_Architecture_v3.3.md) 与 [执行计划 v3.3](02_Execution_Plan_v3.3.md)，不新增或重新编号工作包。

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
| G1 | InProgress | D1.01–D1.04自动验收Passed；D1.05、D1.06尚未完成，G1尚未验收 |
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
| D1.01 | 实现Foundation与错误/标识原语 | D0.03、D0.06 | Passed |
| D1.02 | 实现CoreContracts、四种shape与typed绑定 | D1.01、D0.02、D0.03、D0.05 | Passed |
| D1.03 | 实现注册批次与不可变目录绑定 | D1.02 | Passed |
| D1.04 | 实现授权主体、资源解析契约和许可原语 | D1.02、D0.05 | Passed |
| D1.05 | 实现Native Invocation与无任务短路径 | D1.03、D1.04 | InProgress |
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

D1.01先行阶段记录（以下状态已由后文最终验收更新）：按[实施计划](plans/D1.01.md)完成Foundation实现和独立规格/代码复核，审核发现的可变详情别名、fail-fast反例、ASan路径及多配置路由已修复。最终修复后三配置各23项原语测试、35项架构检查通过；实现节点98a406f已推送；首轮正式Debug/Release各34项通过，ASan两项消费者配置超时，整包门禁Failed并保留原始报告。已局部隔离vcpkg和用户属性注入，正在复核最终驱动及重新准备完整34/34/36矩阵，D1.01保持InProgress。详见[验证记录](validation/D1.01.md)。


D1.01最终验收：修复来源171907d已推送，Debug/Release/ASan正式34/34/36全部通过、163项Git来源一致、AI两类审核齐全，[自动门禁](../evidence/D1.01/automatic-20260907T100928Z/gate-summary.json)Passed。早期失败和修复过程为历史记录，保留上述原文以区分各轮事实。D1.02已重新核对全部前置Passed，按[已审计划](plans/D1.02.md)及[启动记录](reviews/D1.02-start.json)进入具体API/固定测试集合冻结；尚未实现或验收CoreContracts。


D1.02实施输入已冻结：[API声明](contracts/core-contracts-api.md)、[AI规格审核](reviews/D1.02-api.md)及[固定expected审核](reviews/D1.02-expected.md)。预期为新增38项，含既有回归共191/191/193与独立三CHECK；这些是待运行预期，不是通过计数。详见[验证登记](validation/D1.02.md)。


D1.02首轮实现与自我复核正在进行：公共宏漂移反例已修复并完成17项针对性检查，SDK驱动补实际ASan传播；原生编译的vctip子进程遗留与identity/context三项审核问题仍在处理。失败未计Passed，正式矩阵尚未开始，详见[过程登记](validation/D1.02.md)。继续按既有AI审核/自动验收政策推进，无人工批准节点。


D1.02本轮恢复点：局部工具副本对照正常退出；拥有性与owner别名已修复并验证，观察同版本冲突单项修复通过。Target补充规格已批准但实现未完成，正式矩阵尚未开始。独立执行/复核任务触发Codex用量限制，本轮不把未验收实现提交为通过；现场与后续顺序见[恢复记录](validation/D1.02-resume.md)。G0和D1.01的Passed保持不变。


## 2026-09-07 继续执行：最终审核与正式矩阵准备

以下更新替代上文恢复点的当前状态，上文作为历史保留。TargetAuthority补充实现、拥有性与观察快照修复已落实；六头公开合同没有扩展生产Runtime。确定性接收者现保存上下文owner及创建时原始许可，伪许可替换不能借用真实许可消费；发行集合及具体发行类型已私有化。独立复核 `evidence/bootstrap/D1.02/review-final-contracts-20260907-fixes-approved.md` 为AI Approved，旧反例在独立重新编译运行中均正确拒绝，历史失败保留。

局部锁定MSVC副本已集成，90文件逐字节校验、整目录原子发布、只读复用及Windows扩展路径等价性均经实际验证；不修改原安装或放宽进程树要求。`integration-ctest-a387f5757c` 的38项集成检查全部通过（早于最终测试工厂修复）；`asan-sdk-probe-b8d1960213` 的实际ASan公共头消费者通过。最终修复后 `review-fixes-green-b719840ef2` 的5项缓存检查、扩展路径旧反例、33项原生主体全部通过；独立工具复核另有并发与来源SHA实证。上述均为局部事实，不替代正式矩阵。

已准备三份D1.02正式run manifest，固定expected仍为191/191/193及每配置三CHECK；归档包含真实SDK安装/裁剪包、编译追踪、正例对象及三文件工厂组合摘要。归档模式增加明确层级以兼容采集器glob与独立validator的Path.match，仍保留递归采集。正式运行须在同一已审提交与输入摘要上执行；失败追加保留。D1.02当前仍InProgress，未开始D1.03。


## D1.02 最终三配置验收

实现提交 `c6950773dd4134f39059ee5e9fa0acc12541ff96` 已推送，189项源码与Git字节逐项一致；输入摘要 `daad9b3588510afff1865f9717ea4d3da47bc788d375a8b585c6e67b29943aca`。正式Debug 191/191、Release 191/191、ASan 193/193，共575次CTest与9项CHECK全部通过。三份原始报告位于 `evidence/c6950773dd41-daad9b358851/`，自动状态、各run包级状态均Passed，校验错误为空。

最终当前版本门禁 `evidence/D1.02/current-20260907T150316Z/gate-summary.json` 实际Passed，AI规格与代码审核完整绑定同一来源，G1保持InProgress。首次误用历史候选入口只因matrix未在原来源快照被拒绝，完整保留 `evidence/D1.02/automatic-20260907T150134Z`；当前入口与D1.01一致，重新核验完整工作树、原始报告和三配置，未更改工具、来源或固定expected。验收上下文和Git来源核对另行归档。

已完成D1.02的六头合同、确定性验证消费者及必要开发工具；不声明Registry、Host或其他产品模块完成。下一包仅在核对本次Passed及规划后进入D1.03。以上替代旧恢复段落的当前状态，历史失败和审核意见保持原文。

D1.03启动：已逐项核对D1.02的实际三配置和门禁Passed，见 `docs/reviews/D1.03-start.json`。计划AI复核Approved，具体API初审正在澄清跨模块服务/资源/provider选择和配置快照深拥有；尚无D1.03行为实现或测试Passed。


## 2026-09-08 D1.03 完整自动验收

实现节点 `fb08a1955ebfc8b8d97062962b61cac901880953` 已推送，204项输入与Git字节完全一致，摘要 `75090b8927158e4476b812c504be76a83144327ad4aaee5f20acfc63b3aa496b`。Debug 216/216、Release 216/216、ASan 218/218，共650次CTest和9项CHECK全部通过。现有门禁完整audit实际Passed，见 `evidence/D1.03/current-20260907T165757Z/gate-summary.json`；AI规格/代码复核明确绑定本轮来源，无人工放行要求。

本包交付注册批次、精确模块DAG、深拥有及预算、粘性错误、一次发布、不可变目录typed绑定与冷热存储分离。内部Runtime库只依赖CoreContracts闭包；公开SDK Runtime仍ContractBaseline且不可用，不宣称Invocation/Host或产品模块完成。历史失败和旧审核原文保留，当前包级状态Passed，G1仍InProgress。下一节点仅按D1.04前置D1.02与D0.05的实际Passed进入。

D1.03验收节点 `271e926` 已推送。D1.04进入计划/API冻结：前置D1.02和D0.05已逐报告SHA及最终门禁核对Passed，见 `docs/reviews/D1.04-prerequisites.json`；尚无D1.04实现或测试通过声明。

D1.04修订3具体API、计划及固定251/251/253+三CHECK均已独立AI审核Approved，准入记录 `docs/reviews/D1.04-start.json` 绑定精确文档与清单SHA。当前准备提交规划冻结节点，再进行真实反例及实现；尚无本包行为Passed。

D1.04当前更新：修订4共同候选可见性及修订5精确Key合同稳定已独立AI批准，增量准入记录分别保存且不改写旧审核。源码冻结候选 `implementation-8e072f1ca34a` 的35项Debug原生主体实际通过，37条owned命令均正常退出并清理。正在进行完整独立AI复核和根工程五组包装验证；正式251/251/253及九CHECK尚未运行，D1.04与G1保持InProgress。真实反例、控制偏差及修复链见 `docs/validation/D1.04.md`，不以切片全绿代替整包验收。


D1.04提交前状态：最后完整AI核心/规格/覆盖审核Approved，编码字节拥有与观察投影预算两个实际问题已修并独立红绿确认，全部既定子断言已挂接。局部Debug/Release/ASan各35项、共105次全部实际通过，最终224输入摘要 `1a044ad0c5b50f1efa53a82125430a7002a61a1fbbd989f8f1ca2e1e50ac8352`。ASan初轮开发驱动运行库路径失败保留，最小子进程环境修复经实际配对控制与独立审核。正在完成实现节点提交准备，正式251/251/253及九CHECK尚未开始；D1.04与G1仍InProgress。


D1.04实现节点 `8978b27` 已完成本地提交及远端推送，224项来源与Git blob字节核对Passed。正式Debug矩阵已开始；保持同提交、同输入摘要依次执行Release和ASan，再按既有自动政策做当前门禁和独立最终验收。正式结果齐备前D1.04保持InProgress。


## 2026-09-08 D1.04 完整自动验收

实现节点 `8978b277850beb98ab9da16838052d6f32a7a4c4` 已推送，224项输入与Git逐blob字节一致，摘要 `1a044ad0c5b50f1efa53a82125430a7002a61a1fbbd989f8f1ca2e1e50ac8352`。正式Debug251/251、Release251/251、ASan253/253，共755次CTest和九CHECK全部Passed。三份原始报告均自动Passed、run包级Passed、errors为空。

当前完整自动门禁 `evidence/D1.04/current-20260907T203550Z/gate-summary.json` 实际Passed（SHA `4f1e6b7131e75fbc4a78d7b97a8ff26c23c7e9cc08af7b8bc1bdbe01c0d77e13`）。最终独立AI验收 `evidence/D1.04/final-acceptance-review.md` Approved，独立重新汇总gate同为Passed；两类技术审核绑定相同来源，不需要人工放行。

本包完成内部授权主体/目标发行、四方tuple权限、一次性许可短仲裁、拥有型观察投影与分页/Watch及真实发送起点授权。编码元素别名、观察投影预算等真实缺口已修，历史失败、撤回候选和旧报告原字节保留。未扩展公开SDK Runtime、Host或产品模块。D1.04包级Passed，G1仍InProgress；下一包D1.05须先核对D1.03与D1.04实际Passed，再按v3.3规划进入设计冻结。

## D1.05 Native 实现与正式验收准备

D1.03、D1.04 全部前置实际 Passed 已在启动时逐项核对；规划冻结提交 `672239f` 已推送。现已完成内部受治理 Native Read/Compute、可信线程/身份/目标/权限、结果事实、有界观察和受限场景分配测量；仅覆盖内核及验证消费者。

最新开发集成 `integration-debug-61b6f6c6c86d`、`integration-release-40b5d325d591` 各 32 项实际通过，含八个真实编译/示例/安装/分配包装。254 项源码输入摘要相同为 `90f1fb73189b4c009ad3cf923f405e52f93f6ad58b6fc569729d009bc92a1291`，源前后稳定。独立复核发现的运输、资源预算、绑定寿命和计数断言问题已逐项修复；原始失败保留，详见 `docs/validation/D1.05.md`。

ASan、最终独立规格/代码复核及正式 283/283/285+九CHECK 尚未全部完成；D1.05 与 G1 仍 InProgress，不将开发通过当作包级验收。下一节点只在本包实际 Passed 后进入 D1.06。

D1.05 最新实现验证：已修复锁定 ASan CRT 钩子的实际覆盖缺口，独立 sanitizer 通道、逐入口反例和真实注册失败控制均已纳入原32主项。Debug/Release/ASan 最终开发集成各32/32（共96次），三配置255输入同摘要 `1685e9ccdd20ac4edb07e78249fb119970e7b8a89885490fb565a07ae57ca2df`，源码前后稳定。准备完成独立复核汇总、提交推送实现节点，随后运行正式矩阵；包级状态仍InProgress。

D1.05 提交前最终来源更新：已补齐基础独立CODE归档以及原SPEC要求的多Unknown集合、借用寿命和非空上下文计数控制。最终Debug/Release/ASan各32/32、零失败/跳过，255输入摘要更新为 `43ad6c2ecd162908932248ba430ab48c86a94a7d213cfca804cd0e7f972a02ef`，各来源逐项一致。原1685与90f1记录保留为历史，不混合作为最终来源；即将登记两类AI审核并提交实现，正式矩阵与包验收仍待运行。

D1.05 两类 AI 技术审核已登记 Approved（`docs/reviews/D1.05-spec.json`、`D1.05-code.json`）。提交前仅规范化运输说明末尾一个多余LF，独立复核确认其余254输入完全不变；最终提交输入摘要为 `028ca1c8afab6dfb2b5c3c6758ce4b71f13311957cc78bdc2463e6993a265560`，局部三配置原始通过仍如实绑定43ad。核对证据见 `evidence/bootstrap/D1.05/precommit-verification.json`。即将提交并推送实现，随后按028ca来源运行正式矩阵；不提前声明包级Passed。

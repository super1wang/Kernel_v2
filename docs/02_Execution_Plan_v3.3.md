# Open Command Kernel v3.3-r2｜拆分执行步骤与验收计划

**设计日期：2026-09-07 · 文档代号：OCK-EXEC-3.3**  
**唯一配套架构：[01_Architecture_v3.3.md](01_Architecture_v3.3.md)**  
**规模：9 个阶段、64 个稳定编号工作包、24 个需求/测试族、3 个长期消费者；在 v3.2 N1–N5 基础上合入 N6–N11，不另建平行计划。**  
**状态：建设计划；以下目标、路径、命令及测试是要求实现的产物，不是当前仓库已存在或已通过。**

> 本计划从零建设，旧代码只可作为经验参考，不包含迁移、旧API兼容、旧数据库读取或历史分支合并。每个工作包都以真实行为和证据结束，不以生成了若干类或更新了文档结束。

**2026-09-08 r2：进一步压缩中间交付：Passed 前置不重复验证；开发依赖与正式验收依赖分开；相关包连续开发；物理测试与 Review 批次化；G Gate 承担完整历史集成。文件名、规范代号、64 包编号、正式依赖、G0–G8 及行为合同不变。**

## E00｜如何使用这份计划

先以架构 A00–A24 作为行为规范，再按下面工作包依赖执行。D0 不是再次讨论设计，而是把已定规则变成可执行模型、Schema、targets、锁定依赖和测试夹具。D1 起以实际编译运行验证。

工作包编号继续作为需求、验收和追踪边界，而**不再等同于必须停顿一次的开发交付边界**。一个 Development Batch 可连续实现多个相关包；不能把不相关的协议修改、库升级和性能重写混入同一批次。正式 Passed 与 G Gate 仍按 E04 的依赖 DAG 顺序确认；开发启动条件按下文 Implementation-Ready 规则执行。

本版新增条款是已接受建议的明确化，不是默认实现已经具备。实现阶段有意保留未知的只有真实版本兼容、实测容量和平台结果。它们有指定任务，不允许实现者以“留待以后”为由跳过，也不允许在未验证时声称已关闭。

### Code-First：默认先交付代码

**执行前规划与 ADR（2026-09-09）：** 每次任务执行前必须有相应规划文档。相关小项复用并补充 `docs/plans/` 的当前批次计划，不逐项新增计划或审批链；历史缺失计划补录必须标明补录日期与原实现来源，不能写成事前批准。重大协议、权限/寿命、组件边界、责任分配变更必须输出 ADR，并同步本计划、架构及相关合同。当前导航：[B2 补录与收口](plans/B2.md)、[B3 执行前规划](plans/B3.md)。

D1–D7 普通生产工作包以生产行为为主要交付。对已经 Passed 的前置只读取状态与所需公开合同，**不重新读取其 review/evidence，不重跑其测试**。相关规范明确后立即编码；若上游尚在同一 Development Batch 内，只要当前下游所需接口达到 Implementation-Ready，也允许继续下游开发。测试、审核和 Evidence 服务于代码，不得把完善验证设施变成生产实现的非必要前置。规划明确要求的 Conformance、测量和故障工具仍须交付，但只建设当前行为所需的最小范围。

同包相关简单小项组成一个开发批次，连续实现、集中运行影响集、集中 review，不逐项建立“计划→SPEC→三配置→CODE→验收→提交”循环。新行为必须有对应验证；关键合同先补最小反例，简单实现允许代码与直接测试在同一步完成。批次内必要编译/自检可以即时运行，批次完成或出现失败时执行对应 S_changed，失败先修复，不累积未知问题。

### Passed 复用、Implementation-Ready 与验收 DAG

正式包状态仍只有 `NotStarted / InProgress / Blocked / Failed / Passed`；**不新增 ImplementationReady 状态**。Implementation-Ready 只是 Development Batch 内部的临时开发条件：

```text
上游所需接口/行为已实现
+ 直接编译/最小合同测试通过
+ 没有已知会推翻该接口的未决设计
= 可供下游继续编码
```

它只允许继续开发，不允许形成正式包级成功声明。最终验收始终满足：

```text
A 是 B 的正式前置
A Passed
    ↓
B 才可最终 Passed
```

但开发过程允许：

```text
A 实现 → A 直接验证 → A 对 B Implementation-Ready
                          ↓
                       继续写 B
                          ↓
A/B/相关包形成稳定批次来源
                          ↓
合并物理测试 + 批次 Review
                          ↓
按 DAG 顺序判定 A Passed → B Passed
```

**Passed 前置具有缓存语义。** 后续包默认只检查 `progress`/gate 中的 Passed 状态与公开契约；不得把“核对前置”扩张成再次读取全部历史审核、逐报告 SHA、重新运行前包矩阵。只有当前批次修改了该前置包的冻结输入/公共 API/构建边界、发现证据损坏或正式 Gate 明确要求复验时，才打开受影响范围。

### 任务分级与集中收口

| 级别 | 判定与默认流程 |
|---|---|
| Fast | 语义明确、低风险胶水、小数据结构、简单消费者或说明修改：读相关条款→代码及直接测试→批次集中 S_changed→继续下一项。不单建 plan、SPEC/CODE、acceptance 文档或全三配置循环 |
| Standard | 普通生产工作包（D1.06 默认）：代码与直接调试为主体，相关小项批次开发；稳定后集中审核并执行 S_required，再判定包级状态 |
| Critical | 权限/撤权、IPC认证、取消/Finalizing、Commit、耐久/外部效果、epoch、恢复/备份及并发安全边界：关键反例先行；仅有真实协议歧义或尚未解决的高风险设计时，编码前做简短风险设计/必要冻结；已明确的合同直接实现，包末正式复核 |

按实际影响定级，不按文件名定级。target/export、小 codec 若影响 SDK、公共编译选项、安全或规范化指纹，必须执行相应专项；D1.06 的停止寿命风险也不能因整体 Standard 而免测。Fast 主要用于批次内轻量任务；E04明确的纯登记/资料生成包可整体Fast，但不豁免工作包的正式审核、完成条件或其中的风险专项。

正式 SPEC/CODE 默认在 Development Batch 稳定后集中形成，并保留各包结论：先以计划/API/expected 为一次 SPEC 输入，确保 S_required 在正式发现与执行前已审定，再在同一最终来源上集中完成正式运行及 CODE 收口；两类结论职责独立，可共享一次上下文准备。开发期做内部自检，不为每个小项生成正式审核或 acceptance，不更新 Passed。Critical 的必要前置风险审核、明确语义变化及规定的预算前置批准是例外，不扩展为全部小项的审批链。

ChangesRequested 只复核实际修改与关联风险；发现公共影响时扩大范围并说明原因。最终所需结果必须对应最终来源，不能因为采用增量复核就拼接不同版本配置。自动验收遵循 `docs/reviews/automatic-acceptance-policy.json`：保留真实 AI SPEC/CODE，不能标为 human Approved，不新增人工节点。

### 工作包、Development Batch 与集中收口

E04 的 64 张卡仍定义**逻辑责任、正式前置和最终完成条件**；它们不强迫 64 次独立 Codex 上下文、64 次完整构建或 64 套重复 Review。E02 给出默认 Development Batch，允许相关包连续编码并共享直接测试准备。

“合并”分三层：

1. **开发合并**：相关包共享上下文、连续实现；有依赖的下游可在上游 Implementation-Ready 后开始。
2. **物理验证合并**：相同最终 source/profile/build 环境下，一次 configure/build/test 尽量覆盖批次多个逻辑集合；结果按 task/requirement 映射。
3. **Review 合并**：同一批次一次加载规范、diff、测试摘要；可用一个批次审核材料给出各包独立 SPEC/CODE 决策。逻辑结论必须可定位，不要求重复搬运同一上下文。

正式 Passed **不合并责任**：每个包的 required 条件仍分别满足，并按 DAG 顺序落状态。Development Batch 不是第 65 个工作包，也不是新的 Gate。

Fast 小项默认吞并到最近生产批次，不创建独立计划、正式审核、Evidence 或“交付提交”。Critical 包仍可连续编码；只有规范未定、高风险冻结点或 E06 明确要求时才在中途建立必要 checkpoint。

### 三种验证集合

| 集合 | 定义与用途 |
|---|---|
| S_changed | 当前新增验证＋直接影响测试＋必要传递消费者＋明确风险专项；用于批次开发，默认 Debug、fail-fast，按风险补 ASan/Release |
| S_required | 当前包完成条件与实际变更影响所需的受审正式集合；用于包级 Passed，**不等于全部历史测试累计集合** |
| S_gate | 当前阶段跨包集成＋代表性历史回归＋规范要求的安装/Profile/性能/故障组合；承担完整阶段集成责任，不以“代表性”删去明确必需门禁 |

不机械追加历史用例，也不临时删改已冻结集合；适用性、Profile、必要轮次与集合在正式执行前审定，不从当轮 discovered 反推 expected。集合与来源对应、现有工具明确支持时，同一次真实运行可供多个上层引用；公共运行只计实际执行次数，跨包审核/状态分别判定。工具未支持的分区或跨 Profile 公共证据不能手工拼成 Passed。

开发期不以“更保险”为由运行 full CTest 或完整三配置。扩大范围只记录一行“改动→风险→扩大集合”：构建/工具链/SDK/依赖/公共编译选项变化、无法窄覆盖的公共合同、测试注册/夹具/发现/expected/证据工具变化、映射未知或漏测、审核发现跨包风险、跨配置问题，或本次确属正式验收/阶段门禁。触发扩大也应先确定必要范围，不自动等于全部历史测试。

稳定后集中执行适用正式矩阵，不预先复制一轮相同局部全三配置。失败保留并追加重跑；源码变化按最终输入与适用配置规则补验，缺测/失败不能豁免。性能和预算仍按原工作包与 A21 规定执行，不因提速推迟必需前置批准。

### 批次、上下文与证据成本

- 一个 Development Batch 可包含有依赖关系的连续包；开始下游编码只要求其所需上游达到 Implementation-Ready。**批次最终状态仍按正式 DAG 顺序落地**，不能因为后包代码已经写完就跳过前包失败。
- 已 Passed 的批次外前置仅做状态/合同读取，不重复审计。若当前批次修改其冻结输入，则它从“缓存事实”变成受影响对象，只重验受影响集合。
- 恢复默认只读 progress 顶部、当前 Development Batch 相关规范/合同和 diff；失败、缺项、异常或语义疑点再展开历史 review/evidence。不要为了“确认已有 Passed”重复搬运历史上下文。
- 正式审核/证据在批次稳定后集中整理。开发原始失败随运行保留，但非正式临时调试日志无需全部入 Git；正式报告引用材料必须可取可校验。
- **物理运行优先合并。** 同一最终来源、Profile、构建/消费者环境中的多个 S_required/S_gate 子集，优先一次 configure/build/test 后按 manifest 分区引用；不得为了包编号不同机械重跑相同二进制和同一测试。
- **Review 优先合并。** 同批一次准备 architecture/plan/contracts/diff/evidence summary，输出各包独立 SPEC/CODE 结论；只有结论职责或来源不同才需要额外上下文。
- 主要推理与操作用于生产代码及直接调试；非当前阻塞、非工作包明确产物的测试/Evidence 基建不扩展。生产阶段原则上不连续两个流程性提交都没有生产源码变更。

### 工作包统一完成条件

| 维度 | 必须满足 |
|---|---|
| 实现 | 指定行为存在，未实现能力明确拒绝；无生产Null-success或假文档 |
| 构建 | 选定Profile干净构建，公开头可独立使用，依赖边界检查通过；SDK公开表面变更经审查 |
| 测试 | 正例、反例、相关故障/并发场景运行；确定性模型与实现一致；适配器运行同版适用Conformance，不各自改预期 |
| 资料 | Contract/Schema/目录/示例/错误语义随实现同步更新 |
| 性能 | 热路径/装配变更记录编码、分配、排队、锁、提交及固定占用的变化；比较已批准阶段预算，不必每包跑全部大基准 |
| 证据 | E03工具采集source/build/二进制、命令/退出、expected/discovered/executed与原始日志；SPEC/CODE与自动结果独立，按现行AI政策验收，不能手写自动通过 |

包级状态仅用 `NotStarted / InProgress / Blocked / Failed / Passed`，由当前证据、必需产物和评审共同判定。自动采集状态与单次测试结果见E03，不能混为一列。Blocked和Skipped不是Passed。仅文档评审通过不等于实现Passed；测试总数增加也不替代行为覆盖。生产部署或远端push需要明确授权，不由工作包自动触发。

### v3.3 增量的实施边界

v3.2 的 N1–N5 全部继续有效：N1观察/枚举、N2固定占用、N3端口Conformance、N4 SDK演进、N5自动证据。v3.3 新增 N6–N11：N6 将 `execution.list` v1 固定为无状态认证 cursor；N7 恢复 UI 统一 Operation 业务写入口；N8 固定“无通用 state RPC＋GUI 主人工前端＋DSL 可选”ADR；N9 为 D4.05 增加 IR 语义往返；N10 将 canonical CBOR feasibility 变成 D5.02 阻塞前置；N11 只修正文档/门禁口径与风险 checkpoint，不新增运行时产品组件。

所有增量保持D0–D8与64包编号。必要时在同一工作包内以 `-a/-b/-c` 标注可审查子项；子项不新增独立G门禁，不改变父包完成条件。D0.04-a是Plan合同，D0.04-b是Control观察合同；D0.06 固定 a=构建/依赖、b=证据采集器、c=Conformance 骨架三个内部 checkpoint，G0 对 c 只要求 harness/manifest/fault 示例能自证，不要求尚未实现的真实后端全部通过。D3.07 固定 G3-A=任务寿命、G3-B=观察/list/watch、G3-C=完整Embedded footprint 三个内部进度点，但只有三者全部满足父包条件才可 G3 Passed。D5.02-a canonical spike 是 D5.02 正式 codec/schema 的阻塞前置。

## E01｜阶段门禁与范围


| 阶段/门禁 | 交付范围 | 放行规则 |
|---|---|---|
| D0 / G0：合同、模型与工程基线 | 进入实现前固定可测试语义，不重画架构。 | D0.01–D0.06全过；Plan/Control/Outcome/commit/epoch模型一致；SDK政策、UI/state/DSL ADR、Conformance适用矩阵和证据采集器及反例可用；Control合同已固定无状态cursor；工具链/依赖可复现。 |
| D1 / G1：原生执行闭环 | 一个真实Native消费者，尚未承诺动态或异步完整能力。 | Native无DOM/假Task；注册、授权、Outcome、Host/Logging共同合同通过；NativeSubset占用口径、有限预算与零分配探针完成；独立安装消费者通过。 |
| D2 / G2：动态契约与首次真实CLI | 同一操作从Native和真实Shell子进程进入。 | 绑定一致性、目录、帧、认证、CLI编码/退出码和观察协议帧级合同通过；不用内存假传输代替IPC；未接真实执行的观察能力不宣称完成。 |
| D3 / G3：任务、资源与结构化寿命 | 一个Operation的真实受管理执行。 | 真实跨CLI submit/get/wait/cancel/list/subscribe/watch及竞态通过；Executor共同合同、满载控制、父子收尾/排空与完整Embedded有限占用预算通过。 |
| D4 / G4：内存状态、Plan与Atomic | 首个可交付的内存开放命令套件。 | 无状态与无文档状态消费者通过；一次Shell Plan、同域一次提交、typed slots与实际结果事实一致。 |
| D5 / G5：Durable、事务证据与恢复 | 可靠性协议从设计变成真实进程故障行为。 | D5.02-a canonical feasibility 已形成受审查 A/B/C 结论；Storage共同/耐久专项、接受/状态/Effect/epoch/Outbox/恢复门禁通过；FULL读回；旧备份新世代；恢复后的观察游标/版本不误用。 |
| D6 / G6：Workspace与持久Plan | 完整上层机制仍使用同一底座。 | 三个消费者、History/资产GC、稳定子键和父检查点落后恢复通过；无第二解释器。 |
| D7 / G7：AI自描述与完整外部控制 | 模型是调用者，不是权限来源或事务所有者。 | 确定性端到端场景必过；支持的模型/MCP适配分别验证，未实测项不计通过；预览批准不可越权。 |
| D8 / G8：容量、验证与发布 | 对最终实现签核声明范围，而不是对规划签核。 | G0–G7及D8必需验证完成；SDK表面/冻结消费者、Conformance矩阵、正式Embedded占用和自动证据完整性通过；安装包/矩阵/证据绑定同一最终版本。 |

**集成门禁的先后保持 G0→G1→…→G8。** 工作包可以在依赖允许时提前开发，不得提前宣布后一个集成门禁已通过。D2和D3的一些开发可在G1后并行，但G3报告必须包含真实CLI管理任务的结果。

G4可发布明确标注“内存版”的开发SDK，不能称完整生产套件。G5不包含全部持久Workflow；G6仍未完成AI与最终发布验证；所有完整承诺到G8才统一签核。

## E02｜关键依赖和并行边界

```text
D0 合同与模型
   └─ D1 原生执行
       ├─ D2 Dynamic/Catalog/IPC/CLI ─┐
       └─ D3 Task/Resource/Lifetime ──┴─ G3
                         ├─ D4 State/Commit/Atomic
                         └─ D4 PlanCompiler/Runner ── G4
                                      └─ D5 Durable/Effect/GC/Recovery ── G5
                                          ├─ D6 Assets/Workspace/History
                                          └─ D6 DurablePlan/StepKeys ── G6
                                                          └─ D7 AI面/Preview/Approval ── G7
                                                              └─ D8综合认证 ── G8
```

这不是人力或工期承诺。高风险关键路径是 **Outcome/许可模型→完成寿命→Commit发布→持久原子性→epoch与子执行恢复→全故障回归**，不应同时被多个独立实现者改出不同语义。

| 可并行工作线 | 共同前置条件 | 禁止交叉修改 |
|---|---|---|
| Data/Binding 与基础Task后端 | D1 Contracts固定 | CoreContracts由主集成者维护 |
| StateRoot/Edit 与PlanCompiler | D0 Plan/Atomic契约、D3稳定端口 | 不各自创造另一种Atomic/Outcome |
| 真实IPC与目录/CLI帮助 | Control合同明确 | 不用演示后门绕认证 |
| Assets/Workspace 与DurablePlan | Durable记录和pin合同稳定 | 同事务/子键协议禁止各自改写 |
| 文档导出、测试场景、性能工具 | 对应协议已定 | 不改变业务预期迎合实现缺陷 |

公共合同变化只同步实际受影响的架构条款、schema/头文件、golden 示例、reference model 和工作包；不适用的层级无需制造空变更。编译通过不能代替对应行为验证。

### Development Batch 默认安排

以下是**开发调度批次**，不是新工作包、不是新的正式状态，也不改变 E04 的包级 Passed DAG。目标是让 Codex 在一个上下文中持续写相关代码，把中间编号从“交付墙”变成“逻辑检查点”。

| Batch | 默认范围 | 开发推进方式 | 正式收口 |
|---|---|---|---|
| B1 | D1.06 | Host→Logging→NativeSubset/install→footprint 连续实现 | D1.06 S_required 后 G1 |
| B2 | D2.01–D2.04 | Data→Binding 后并行/连续推进 Catalog+Control；上游 Implementation-Ready 即继续 | 按 D2.01→D2.02→D2.03/D2.04 的 DAG 判 Passed |
| B3 | D2.05–D2.07 | IPC→CLI→双入口/Shell 闭环连续开发 | 批次物理测试合并，最终 G2 |
| B4 | D3.01–D3.03 | Executor→Scheduler→Resources 连续开发 | 各包逻辑结果独立 |
| B5 | D3.04–D3.07 | Submit→Cancel→Lifetime→真实观察/Embedded 连续开发 | 关键风险专项集中，最终 G3 |
| B6 | D4.01–D4.04 | Snapshot→Edit→Commit→Atomic 连续开发 | Commit/Atomic 风险仍单独映射 |
| B7 | D4.05–D4.08 | Compiler→Runner→ControlFlow→内存套件连续开发 | 最终 G4 集中 |
| B8 | D5.01–D5.04 | Storage→canonical/records→Intent→DurableAccepted | D5.02-a 是批次内真实阻塞 checkpoint |
| B9 | D5.05–D5.09 | StateDurable/Effect→Outbox→Recovery→真实 Durable Gate | 故障窗口集中物理运行，最终 G5 |
| B10 | D6.01–D6.08 | Assets/Workspace/DurablePlan 三线在接口 Ready 后协同，随后恢复集成 | 各逻辑包按 DAG 判定，最终 G6 |
| B11 | D7.01–D7.06 | SDK→Preview/Approval→MCP/Results→AI场景 | Review/适配场景批次化，最终 G7 |
| B12 | D8.01–D8.07 | 最终 SDK/Profile/Fuzz/Crash/Performance/Docs/RC | 尽量共享同一最终源码和构建矩阵，最终 G8 |

规则：

- Batch 内**开发依赖**使用 Implementation-Ready，不要求每个编号先完成完整 SPEC/CODE/Evidence 才允许写下一个编号。
- Batch 内**正式验收依赖**仍按 E04 DAG：上游未 Passed 时，下游可以已有实现，但不能最终 Passed。
- 已 Passed 的批次外前置不重验，只消费其公开合同；当前 Batch 若改动其冻结输入则按影响重新打开。
- Batch 内可以多次小提交，但不要为每个包编号强制提交；优先在可审查代码边界、Critical checkpoint、批次收口或 Gate 提交。
- 同一 source/profile 下，S_required 与 S_gate 重叠的测试优先只物理执行一次并按机器可追踪映射引用；工具暂不支持安全分区时才保守分开，不手工拼 Passed。
- Batch Review 可是一份材料中的多包决策，只要每个 task_id 的 SPEC/CODE 结论、source digest、未满足项可独立读取；现有验收工具若要求分文件，可由同一批次上下文生成薄的 per-task 机器记录，而不是重复完整审核。
- G0→G8 仍按顺序放行；**Gate 承担阶段完整历史集成**，普通中间包不复制整个阶段历史矩阵。

## E03｜自动证据、命令和工程目录约定

### E03.1 产物与三份清单

建议产物目录：

```text
packages/{foundation,contracts,runtime,dynamic,automation,state,workspace,durable,durable_plan,control}/
packages/bridges/state_durable/
packages/adapters/{cpu_pool,logging,config,sqlite,storage,local_ipc,mcp}/
apps/{ock,stateless_service,settings_service,workspace_app}/
schemas/{rpc-v1,plan,catalog,outcome}/   examples/plans/   sdk/
tests/{unit,contract,conformance,model,compile,integration,crash,fuzz,performance,install_consumer}/
tests/conformance/{executor,storage,logging,asset_storage}/
tests/manifests/   tests/install_consumer/frozen/   tools/{evidence,footprint,api_surface}/
evidence/<source-id>/<profile>/<task-id>/<run-id>/
```

`tests/manifests/expected.json`是受审查的需求/包→用例→Profile/后端适用性映射，不从当前发现结果重新生成；`discovered.json`是本次实际测试发现；执行报告是本次真实结果。删测试、改过滤器导致缺项必须失败。Task ID、Conformance版本、后端身份、重复轮次、故障探针与人工评审项在manifest中有独立字段。

### E03.2 D0.06 的执行包装器

D0.06交付 `tools/evidence/run.py`（或同等显式入口）及报告校验器、CTest发现/JUnit适配、模板和自测。它实际启动configure/build/test/非CTest检查，捕获argv、cwd、环境的允许公开子集、时间、exit/异常/timeout、stdout/stderr；通过文件或管道持久保存结果，不只解析日志中的“通过”文字。

D0定义 `win-msvc-debug`、`win-msvc-release`、`win-msvc-asan`（仅支持时）presets。以下是该工具必须生成并执行的命令形式，**不是本次文档交付已包含可运行工具或C++工程**：

```powershell
cmake --preset win-msvc-debug
if ($LASTEXITCODE -ne 0) { throw "configure failed" }
cmake --build --preset win-msvc-debug --parallel
if ($LASTEXITCODE -ne 0) { throw "build failed" }
ctest --preset win-msvc-debug --show-only=json-v1 -R "^T(06|07)\."
if ($LASTEXITCODE -ne 0) { throw "test discovery failed" }
# 包装器将上述结构化stdout保存为独立discovered.json并与受审查expected核对。
ctest --preset win-msvc-debug --no-tests=error --output-on-failure `
  --output-junit <unique-run-dir>/junit.xml -R "^T(06|07)\."
if ($LASTEXITCODE -ne 0) { throw "contract tests failed" }
```

尖括号表示包装器生成的真实路径，不直接照抄占位符运行。`--show-only=json-v1`不运行测试，不能计为执行；JUnit已有路径会被覆盖，因此每run/每重复轮次使用唯一文件；`--no-tests=error`仍不能替代完整expected差异检查。选定CTest版本对这些能力的支持在D0.06 probe，不以最新在线手册代替本机构建验证。[架构 E14]

CTest名称统一 `Txx.<component>.<behavior>`，Conformance包含backend/case；非CTest的模型/API/文档/产物检查使用独立command_result和检查ID，不伪造CTest通过数。Bootstrap期间可以先采集原始机器输出，G0前由采集器生成/核对证据；缺失的历史退出事实标Incomplete，不事后补填为成功。

### E03.3 身份、结果和评审分离

证据至少包含以下结构（模板，不是实际运行报告）：

```json
{
  "evidence_format": "ock.evidence/1",
  "task_id": "D3.01",
  "run_id": "<new unique run id>",
  "source": {"commit": "<actual>", "dirty": null, "build_inputs_sha256": "<actual>"},
  "build": {"profile": "<actual>", "preset": "<actual>", "toolchain": "<actual>", "dependency_lock_sha256": "<actual>", "binary_manifest": []},
  "requirements_manifest_sha256": "<reviewed expected manifest>",
  "conformance": {"contract_version": null, "backend": null, "capabilities": [], "not_applicable": []},
  "commands": [],
  "tests": {"expected": [], "discovered": [], "executed": [], "missing": [], "failed": [], "not_run": []},
  "checks": [],
  "raw_evidence": [],
  "automated_status": "NotRun",
  "review": {"required": [], "approvals": [], "notes": []},
  "package_status": "NotStarted"
}
```

实际报告不得保留`<actual>`或用null伪装已确认来源。dirty工作区的实际构建输入必须有指纹；不只记录HEAD。报告绑定CMake cache/编译模式、环境、依赖和被测二进制；产物内容变化或缺失时重新采集，不能拿另一个Profile的日志拼接。

`automated_status`采用 `NotRun|Passed|Failed|Incomplete|Blocked`，由规则生成。单用例记录Passed/Failed/Skipped/NotRun/Timeout/Crashed及原因；NotApplicable属于经审查能力适用判断，不能计入Passed。包级只用E00五状态：已执行必需项失败为Failed；前提不满足为Blocked；尚有实现/资料/评审缺项为InProgress；只有必需自动检查、产物和评审齐全才能Passed。

人只签评审、说明风险和记录批准，不修改exit_code、测试状态、hash或自动结果。仅CTest成功而代码合同/文档/产物不全，不足以包级Passed。批准未来范围调整须改需求与版本并审查，不能把当前失败变成通过。缺少历史正式消费者时按A19首发规则记录事实，不把不适用计为兼容测试成功。

### E03.4 必须先验证采集器本身

D0.06至少构造：命令失败、无测试、预期测试被删除、过滤错误、报告缺失/损坏、超时/进程被杀、输出打印成功但退出异常、上轮成功报告残留、本次二进制/源码指纹不符、失败后重跑、Conformance必需项伪装不可用、轮次不足等反例。每种均不能被判为Passed。

每次重跑追加run_id，不覆盖失败；repeat若要求多轮无失败，不能使用until-pass掩盖flaky。每轮输出与原始证据都保留，格式无法表达时由包装器逐轮执行，不从一个汇总JUnit推断全部轮次。crash测试以父验证器判定预期中断和新Host事实，保留父子状态，不简单以子exit非零定失败。

超时应记录实际进程树处置与未排空进程，不能自动kill真实产品或设备；测试材料/数据库/IPC实例与用户生产材料隔离。原始证据脱敏但保留足够审查信息，并标记截断/脱敏范围；hash是完整性核对，不等于外部审计签名。

### E03.5 阶段与发布汇总

生成器将run级事实汇总为task/gate报告，检查G门禁、expected集合、构建身份和允许的多配置矩阵；同一最终实现必须有对应配置的实际结果，不能把多个源码版本的成功片段拼为全绿。自动规则、Conformance合同版本、预算manifest和审批本身都有版本和摘要。

工具只属于开发/CI/适配器作者工具包，不在生产Runtime链接或每请求执行。D0.06提供基础，所有后续包实际使用，D8.07汇总发布；不得64个包完成后才手工补证据。

同一最终来源/Profile/构建环境的一次正式执行可以同时覆盖多个 task/gate 的已审适用集合；汇总器应记录“一次物理执行→多个逻辑引用”的映射，不因为 task_id 数量复制 configure/build/test。各 task 的 expected、缺项、失败与状态仍独立计算。

## E04｜逐工作包执行卡

每张卡列出的前置同时定义正式验收 DAG 和代码/合同依赖。**正式 Passed 必须满足这些前置已 Passed；开发编码可以按 E00 的 Implementation-Ready 在同一 Development Batch 内提前衔接。** 已 Passed 前置默认不重复验证。产物路径可按项目统一命名，但责任、合同和验证不能省略。


### D0｜合同、模型与工程基线


<a id="d001"></a>
#### D0.01｜登记需求、不变量和三个消费者

**执行分级：** Fast。

**合并安排：** 需求/不变量登记＋消费者边界＋测试族/增量责任映射，一批核对；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 登记完整性、无伪Document；仅补当前登记检查，不重跑未来行为。

**前置依赖：** 无；以用户批准的两份v3.3规范为输入  
**架构依据／测试族：** [A00](01_Architecture_v3.3.md#a00)、[A01](01_Architecture_v3.3.md#a01)、[A23](01_Architecture_v3.3.md#a23) ／ T01、T19、T20、T23、T24

**实施内容：** 将R01–R24、T01–T24、C1–C6及PlanCompleted建立可检索映射；确定C-A无状态、C-B无文档状态、C-C工作区的功能边界。记录不做旧代码/旧数据兼容。 登记N1–N11与具体测试子项：观察协议/执行枚举、NativeSubset与Embedded占用、端口Conformance、SDK Stable范围、机器证据、无状态cursor、UI统一入口、state/DSL ADR、IR roundtrip、canonical spike及文档门禁修正；保持24主族和64包编号。

**交付产物：** docs/requirements.md；docs/adr/；tests/manifest.json结构；三个consumer目标清单。 受审查expected/conformance适用manifest草案与N1–N11责任映射。

**通过条件：** 每项需求有主测试族与负责工作包；没有只写“全绿”而无行为定义的门禁；三个消费者不存在伪Document。 N1–N11均有首个真实闭环/验证位置与最终复验包，不用“已写条款”替代运行证据。

**本包禁止扩展：** 不重新讨论v4.0架构；不编写整个运行时。


<a id="d002"></a>
#### D0.02｜冻结target DAG、公开头和威胁模型

**执行分级：** Standard。

**合并安排：** target DAG/导出＋公开头/SDK政策＋威胁模型/ADR，一批建立；可与D0.03条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 独立安装、缺组件/越层负例；公共编译选项与权限边界专项。

**前置依赖：** [D0.01](#d001)  
**架构依据／测试族：** [A02](01_Architecture_v3.3.md#a02)、[A10](01_Architecture_v3.3.md#a10)、[A20](01_Architecture_v3.3.md#a20)、[A19](01_Architecture_v3.3.md#a19)、[A22](01_Architecture_v3.3.md#a22) ／ T01、T05、T20、T24

**实施内容：** 把A02转为实际CMake目标/导出依赖清单，含Data、ControlProtocol、ControlClient的无服务端依赖子目标；定义可信native模块、外部调用者、OS身份边界和存储所有权。设计越层include与缺组件负例。 定义SDK独立SemVer版本、0.x/1.0/Stable/experimental/detail边界、公共头/target manifest、冻结消费者策略；测试目标和证据工具不进入产品DAG。 将 A23.1 三项产品/API 决定写成正式 ADR：UI 业务写统一 Operation、无通用 `state.inspect/query` RPC、GUI 为主人工前端且 DSL/REPL 可选。

**交付产物：** cmake/TargetDependencies.cmake；docs/public-headers.md；docs/threat-model.md；tests/architecture/。 sdk_api_manifest草案；docs/sdk-version-policy.md；tests/install_consumer/frozen/规则；`docs/adr/ADR-ui-operation-surface.md`、`ADR-no-generic-state-rpc.md`、`ADR-human-ui-and-optional-dsl.md`（实际编号可在 D0 统一）。

**通过条件：** 目标图无环；Runtime到Data/State/SQL/IPC无路径；State通过CoreContracts接入；权限不是客户端自声明。 公开头增删、同头签名变化及公共编译要求都有审查路径；不将文档v3.3当SDK发布版本，不承诺任意ABI兼容。 架构负例能阻止 UI/Workspace 直接业务写旁路；Control 不注册万能 state RPC；DSL 缺失不使 G0/G8 因“命令驱动”自动失败。

**本包禁止扩展：** 不引入动态插件框架或远程OAuth。


<a id="d003"></a>
#### D0.03｜Outcome、phase和完成回调参考模型

**执行分级：** Critical。

**合并安排：** Outcome/phase＋完成回调＋观察版本模型，集中枚举；可与D0.02条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 迟到/重复回调、Finalizing与终态、结果事实非法组合。

**前置依赖：** [D0.01](#d001)  
**架构依据／测试族：** [A04](01_Architecture_v3.3.md#a04)、[A05](01_Architecture_v3.3.md#a05)、[A09](01_Architecture_v3.3.md#a09)、[A17](01_Architecture_v3.3.md#a17) ／ T02、T06、T12、T19、T20

**实施内容：** 定义Accepted/Completed与九类Outcome；枚举合法结果、证据和phase组合；定义inline/迟到/重复callback及Finalizing失败处理。 固定observation_version与ExecutionPhase/Outcome投影：观察版本不等于业务revision；同次get投影一致，Finalizing仍非Terminal。

**交付产物：** docs/contracts/outcome.md；schemas/outcome-v1.schema.json草案；tests/model/execution_model。

**通过条件：** Read、State、Effect部分成功、Lifecycle失败转态、Plan全成功均可表达；迟到取消不改写事实；模型能拒绝非法转换。 原子候选进度、DurableCommitted未Published、Terminal与迟到通知均有合法投影及拒绝反例。

**本包禁止扩展：** 不把模型测试写成真实C++ runtime通过。


<a id="d004"></a>
#### D0.04｜Plan与Control wire、槽类型和观察合同

**执行分级：** Critical。

**合并安排：** Plan Schema/slots/样例＋Control观察/cursor合同同批推进，目录与职责分开；可与D0.05、D0.06条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 类型/预算、ack/get竞态、MAC/身份/世代/TTL；保留a/b合同边界。

**前置依赖：** [D0.01](#d001)、[D0.03](#d003)  
**架构依据／测试族：** [A08](01_Architecture_v3.3.md#a08)、[A19](01_Architecture_v3.3.md#a19)、[A05](01_Architecture_v3.3.md#a05)、[A10](01_Architecture_v3.3.md#a10)、[A15](01_Architecture_v3.3.md#a15)、[A17](01_Architecture_v3.3.md#a17)、[A21](01_Architecture_v3.3.md#a21) ／ T04、T10、T11、T19、T20、T22

**实施内容：** 按A08生成JSON Schema和节点字段表；固定exports/return映射、pointer与bindings覆盖、ticket/Atomic结果类型、分支与预算语义。提供无文档与compute→apply样例。 本包拆成D0.04-a（上述Plan合同）与D0.04-b（独立Control合同）：定案subscribe/unsubscribe/event、execution.list、确认后get时序、序号/gap、授权、队列和连接寿命。`execution.list` v1 的 cursor 在合同层直接固定为无状态认证 opaque token：绑定 caller/过滤/host/restore/排序位置/TTL，使用 keyed MAC，最大编码长度按 A21；服务端逐cursor句柄=0。两个Schema集分目录，不将订阅混入Plan节点。

**交付产物：** schemas/plan-v1.schema.json；docs/contracts/plan.md；examples/plans/；tests/plan/golden/。 schemas/rpc-v1/{notifications,execution-list}.schema.json；docs/contracts/control-observation.md；cursor token字段/版本/MAC输入规范与golden；subscribe/get/revoke及list分页模型与golden消息。

**通过条件：** 每个节点至少有正反样例；kind=return拒绝；exports、Await票据、路径重叠和总预算反例齐全；原子动态domain标为待检查。 订阅已终态/建立中终态、ack前事件、旧版本覆盖、drop无后续、跨连接退订、cursor篡改/越权/过期/跨世代/超长/不推进均有正反规格；3个请求方法与1种服务端推送方向清楚；不得留“也可用服务端cursor句柄”的首版实现分支。

**本包禁止扩展：** 不另造DSL解释器；不称静态Schema已验证真实操作权限。


<a id="d005"></a>
#### D0.05｜提交、许可、epoch和备份恢复参考模型

**执行分级：** Critical。

**合并安排：** 许可/提交状态机＋epoch/claim＋备份恢复模型集中验证；可与D0.04、D0.06条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** cancel/revoke/commit先后、崩溃窗口、水位与旧备份盲重放。

**前置依赖：** [D0.01](#d001)、[D0.03](#d003)  
**架构依据／测试族：** [A06](01_Architecture_v3.3.md#a06)、[A11](01_Architecture_v3.3.md#a11)、[A12](01_Architecture_v3.3.md#a12)、[A14](01_Architecture_v3.3.md#a14) ／ T07、T14、T15、T16、T18

**实施内容：** 写出cancel/revoke/claim/publication小状态机及数据不变量；明确DB reservation、epoch+claim事务、子键空间、旧备份恢复世代。

**交付产物：** docs/contracts/commit.md；docs/contracts/intent.md；tests/model/commit_model；tests/model/dedup_model。

**通过条件：** 枚举双方先后与崩溃点；许可不是成功；发布不撕裂；先删除后推进水位被模型判错；旧备份不得盲重放。

**本包禁止扩展：** 不开发分布式事务或通用形式化系统。


<a id="d006"></a>
#### D0.06｜固定工具链、共享测试基建与自动证据采集

**执行分级：** Standard。

**合并安排：** a工具链依赖→b最小采集器＋c共同harness，父包集中收口；可与D0.04、D0.05条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 真实干净构建、错来源/删测/旧报告不得Passed；a/b/c不新立门禁。

**前置依赖：** [D0.02](#d002)  
**架构依据／测试族：** [A20](01_Architecture_v3.3.md#a20)、[A21](01_Architecture_v3.3.md#a21)、[A22](01_Architecture_v3.3.md#a22)、[A19](01_Architecture_v3.3.md#a19) ／ T01、T19、T23、T24

**实施内容：** 建立MSVC x64/C++20 presets；验证默认依赖最小程序、许可与Windows构建；锁定hash/选项。SQLite候选必须含WAL-reset修复；按组件取依赖。 D0.06-a锁定构建与依赖；D0.06-b按E03实现证据执行包装器/校验器、expected/discovered/executed对照、每轮原始报告与身份绑定；D0.06-c建立测试工厂/Conformance manifest与适用规则骨架，后续各端口填入共同合同。

**内部 checkpoint（不新增 G 门禁）**：`D0.06-a` 完成后应能空目录重复构建 dependency probes；`D0.06-b` 完成后 evidence runner 能被故意失败/删测试/旧报告等反例自证；`D0.06-c` 在 G0 只要求一个 mock 合法端口＋一个 fault/非法端口通过共同 harness、能力 manifest 和 NotApplicable 规则能够自证，**不要求尚未实现的 BS pool/SQLite/AssetStorage 在 G0 已完成正式 Conformance**。真实后端分别由 D1/D3/D5/D6 承担。

**交付产物：** CMakePresets.json；dependencies.lock；THIRD_PARTY_NOTICES；最小dependency probes；evidence/D0.06/。 tools/evidence/；tests/tools/evidence/；schemas/evidence-v1.schema.json；tests/conformance/support/；mock/fault conformance fixture；evidence模板与机器生成gate-summary。

**通过条件：** 从空build可重复配置；未选SQLite/Asio/State时不获取；支持的sanitizer注明实际运行结果；依赖锁不是未检查的latest。 采集器反例不能错误Passed；0测试/删测试/旧报告/错二进制/失败后重跑/缺轮次均被识别；包级通过需要产物与审查；无法导入的早期记录明确Incomplete。 Conformance 骨架能够证明“共同必需/可选能力/fault backend”三类语义，但不以空实现冒充未来后端已验证。

**本包禁止扩展：** 不升级为Preview要求，不导入旧工程构建路径。 不实现生产证据服务，不手填exit或Passed，不将测试工具作为Runtime依赖。


**G0 集成放行：** D0.01–D0.06全过；Plan/Control/Outcome/commit/epoch模型一致；SDK政策、Conformance适用矩阵和证据采集器及反例可用；工具链/依赖可复现。


### D1｜原生执行闭环


<a id="d101"></a>
#### D1.01｜实现Foundation与错误/标识原语

**执行分级：** Standard。

**合并安排：** expected/错误拥有性＋名称/身份＋受检计数一批编码；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** move-only/void、溢出、纯错误码分配；公共头与CRT专项。

**前置依赖：** [D0.03](#d003)、[D0.06](#d006)  
**架构依据／测试族：** [A03](01_Architecture_v3.3.md#a03) ／ T03、T04、T05

**实施内容：** 实现expected别名、ErrorCode和拥有型ErrorInfo、Tagged身份、名称解析与句柄代数、受检计数；错误详情不依赖Payload。

**交付产物：** packages/foundation/；tests/unit/foundation/；基础布局与分配统计。

**通过条件：** move-only/void/错误传播正确；纯错误码不因包装分配；UTF-8显示与ASCII名称分开；异常/溢出有反例。

**本包禁止扩展：** 不自研STL、Unicode全套或完整expected。


<a id="d102"></a>
#### D1.02｜实现CoreContracts、四种shape与typed绑定

**执行分级：** Standard。

**合并安排：** 四shape/typed绑定＋窄端口/上下文＋公开头与工厂接入同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 非法类型/能力提取、跨世代、回调所有权；模板/SDK配置专项。

**前置依赖：** [D1.01](#d101)、[D0.02](#d002)、[D0.03](#d003)、[D0.05](#d005)  
**架构依据／测试族：** [A02](01_Architecture_v3.3.md#a02)、[A04](01_Architecture_v3.3.md#a04)、[A05](01_Architecture_v3.3.md#a05)、[A06](01_Architecture_v3.3.md#a06)、[A09](01_Architecture_v3.3.md#a09)、[A17](01_Architecture_v3.3.md#a17)、[A19](01_Architecture_v3.3.md#a19)、[A22](01_Architecture_v3.3.md#a22) ／ T01、T02、T05、T06、T19、T24

**实施内容：** 给出实际可编译的OperationKey/BoundOperation、Caller、Outcome、Executor/Completion、Atomic/记录窄端口；为四种shape限制上下文能力。 提供类型化ExecutionSummary/观察版本和有界执行观察/枚举端口，具体RPC SubscriptionId留Control；公共头归入已审查manifest。

**交付产物：** packages/contracts/；tests/compile/contracts/；公共头依赖扫描。 公共端口适用合同与编译探针；按后端工厂运行的测试接入点。

**通过条件：** 非法Args/Result、错误shape、跨generation句柄拒绝；EditView无commit；仅链接Contracts无需DOM/SQL；公共模板依赖正确导出。 CoreContracts不引入JSON/连接对象；同一后端不能通过改测试预期改变公共合同。

**本包禁止扩展：** 不在万能Context内暴露整个Host。


<a id="d103"></a>
#### D1.03｜实现注册批次与不可变目录绑定

**执行分级：** Standard。

**合并安排：** manifest DAG＋候选批次/错误累积＋冻结目录/冷热分离同批；可与D1.04条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 部分失败零发布、缺服务/provider、Ready后替换拒绝。

**前置依赖：** [D1.02](#d102)  
**架构依据／测试族：** [A04](01_Architecture_v3.3.md#a04)、[A16](01_Architecture_v3.3.md#a16) ／ T02、T05、T24

**实施内容：** 实现模块manifest DAG、批次候选注册、唯一名称版本、固定绑定和冻结；注册错误累积不可被模块吞掉。

**交付产物：** packages/runtime/registry/；tests/contract/registration/。

**通过条件：** 第k项失败不发布任何本批操作；重复版本/缺服务/缺provider均拒绝；Ready后替换失败；冷Docs不进热结构。

**本包禁止扩展：** 不做热卸载、目录扫描或动态ServiceLocator。


<a id="d104"></a>
#### D1.04｜实现授权主体、资源解析契约和许可原语

**执行分级：** Critical。

**合并安排：** 主体/target解析＋四方权限/permit＋观察与发送授权同批；可与D1.03条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 伪造/跨主体、撤权与消费/发送竞争、分页与连接隔离。

**前置依赖：** [D1.02](#d102)、[D0.05](#d005)  
**架构依据／测试族：** [A06](01_Architecture_v3.3.md#a06)、[A10](01_Architecture_v3.3.md#a10)、[A17](01_Architecture_v3.3.md#a17) ／ T07、T19、T20

**实施内容：** 实现VerifiedCaller、会话委托、目标resolver、权限制定/撤销世代、一次性ActionPermit；内存授权服务先行。 补owner过滤/委托视图与发送授权的短仲裁；现有订阅缓存不得永久保留允许，queued敏感内容撤权后不新发。

**交付产物：** packages/runtime/policy/；tests/contract/authorization/；许可交错测试。

**通过条件：** 伪造caller/target不能提权；revoke与消费两种先后正确；expired/重复permit拒绝；组内权限不能由batch权限代替。 查询/订阅/发送/分页均不可用用户填写owner提权；不同连接的授权视图独立。

**本包禁止扩展：** 不把ACL、生命周期和资源锁混成一种许可。


<a id="d105"></a>
#### D1.05｜实现Native Invocation与无任务短路径

**执行分级：** Standard。

**合并安排：** 参数/目标/授权分派＋Read/Compute＋结果处理/示例一批；计数集中；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 权限与线程拒绝、结果运输/寿命、无DOM/假Task、分配正负控制。

**前置依赖：** [D1.03](#d103)、[D1.04](#d104)  
**架构依据／测试族：** [A04](01_Architecture_v3.3.md#a04)、[A05](01_Architecture_v3.3.md#a05)、[A21](01_Architecture_v3.3.md#a21) ／ T02、T03、T06、T23

**实施内容：** 实现共享参数验证、授权、目标解析、shape dispatch与结果处理；Read/Compute真实运行；不可用Provider明确拒绝，不返回空成功。 增加固定成功Native场景分配计数正负探针；编译绑定/预热成本与稳态调用分开，治理检查仍完整。

**交付产物：** packages/runtime/invocation/；examples/native_service/；tests/contract/native/。

**通过条件：** Native短调用无DOM/TaskId/任务表；非法输入不进业务；输出错误不吞事实；线程模式不合法拒绝。 无业务分配、有限小Args/R场景满足A21.5；首用/错误/可变结果/观测异步成本另报，不声称所有调用零分配。

**本包禁止扩展：** 不为演示提前开放裸SQL、设备发送或公共Handler提取。


<a id="d106"></a>
#### D1.06｜最小Host、Logging共同合同与原生占用基线

**执行分级：** Standard。

**合并安排：** Host→Logging→NativeSubset/安装消费者→footprint，一个包连续开发收口；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 停止/回调寿命、日志满载、独立安装；有限预算先审定再正式比较。

**前置依赖：** [D1.05](#d105)、[D0.06](#d006)  
**架构依据／测试族：** [A15](01_Architecture_v3.3.md#a15)、[A16](01_Architecture_v3.3.md#a16)、[A23](01_Architecture_v3.3.md#a23)、[A19](01_Architecture_v3.3.md#a19)、[A21](01_Architecture_v3.3.md#a21)、[A22](01_Architecture_v3.3.md#a22) ／ T01、T03、T19、T22、T23、T24

**实施内容：** 装配新Host，验证Ready前拒绝业务、失败逆序清理；加入有界内存诊断/日志端口，跑通C-A的Native子集并建立install-consumer。 以共享LoggingConformance子集测试默认内存日志及合法测试后端，保留满载策略差异。建设footprint基准启动器、计数器有效性与NativeSubset统计：真实链接增量、Private/WorkingSet、线程、Ready时延及退出。

**交付产物：** packages/runtime/host/；packages/runtime/observability/；examples/stateless_service/；阶段G1证据。 tests/conformance/logging/；tools/footprint/；footprint-budgets.json的NativeSubset审批值；evidence/G1/机器报告与API表面初版。

**通过条件：** 启动每一部分失败均清理；无用户回调锁内执行；无State/JSON/SQLite安装消费者运行；未实现异步不谎称支持。 NativeSubset新增线程=0；固定场景零新增分配；体积/内存/Ready的有限阶段预算在G1前审批并验证；报告不冒充尚未实现的完整Embedded。

**本包禁止扩展：** 不以假task或空document完成门禁。


**G1 集成放行：** Native无DOM/假Task；注册、授权、Outcome、Host/Logging共同合同通过；NativeSubset占用口径、有限预算与零分配探针完成；独立安装消费者通过。


### D2｜动态契约与首次真实CLI


<a id="d201"></a>
#### D2.01｜实现单DOM Payload、View与预算构建

**执行分级：** Standard。

**合并安排：** Payload/View＋clone/share/freeze＋预算解析/builder同批；可与D3.01条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 可变别名与View逃逸ASan；深度/节点/实际分配及重复键拒绝。

**前置依赖：** [D1.01](#d101)、[D0.06](#d006)  
**架构依据／测试族：** [A03](01_Architecture_v3.3.md#a03)、[A21](01_Architecture_v3.3.md#a21) ／ T04、T12

**实施内容：** 私有jsoncons后端；Payload move-only、显式clone/share、builder冻结；在解析中限制帧/token/深度/节点和实际分配，拒绝重复键。

**交付产物：** packages/dynamic/data/（导出OCK::Data）；tests/unit/data/；parser fuzz seeds。

**通过条件：** 冻结后无可变别名；大输入在预算内拒绝；View逃逸测试与ASan样例；零通用DOM来回转换。

**本包禁止扩展：** 不为每个节点分配PIMPL；不先造两套树再校验。


<a id="d202"></a>
#### D2.02｜实现TypeContract、Schema编译与Native等价绑定

**执行分级：** Standard。

**合并安排：** 字段描述/validator＋Schema编译＋Native/Dynamic绑定同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 缺失/null/数值边界等价、Schema并发与无网络ref；模板配置专项。

**前置依赖：** [D2.01](#d201)、[D1.02](#d102)、[D1.05](#d105)、[D0.04](#d004)  
**架构依据／测试族：** [A03](01_Architecture_v3.3.md#a03)、[A04](01_Architecture_v3.3.md#a04)、[A19](01_Architecture_v3.3.md#a19) ／ T02、T04、T23

**实施内容：** 字段描述生成常规validator、参数/结果绑定与2020-12投影；动态复杂约束显式标dynamic-only；注册编译Schema并限制引用。

**交付产物：** packages/dynamic/binding/；tests/contract/binding_parity/；Schema并发探针。

**通过条件：** 缺失/null、uint64、NaN、单位、未知字段在两入口同判；只编译一次；格式注释不冒充验证；无网络$ref。

**本包禁止扩展：** 不实现完整C++反射，不降低服务器约束适配模型。


<a id="d203"></a>
#### D2.03｜实现能力目录、精确命令卡和帮助导出

**执行分级：** Standard。

**合并安排：** search/describe＋Schema/帮助生成＋分页/指纹同批，生成项按Fast合并；可与D2.04条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** installed/visible/eligible区分、跨主体隐藏、长Docs不进入热复制。

**前置依赖：** [D2.02](#d202)、[D1.03](#d103)、[D1.04](#d104)  
**架构依据／测试族：** [A04](01_Architecture_v3.3.md#a04)、[A18](01_Architecture_v3.3.md#a18) ／ T05、T20、T21

**实施内容：** 从Contract/Policy/Docs生成search/describe、Schema和CLI帮助；目录区分installed/visible/eligible，分页和指纹有界。

**交付产物：** packages/dynamic/catalog/；schemas/catalog/；tests/contract/catalog/。

**通过条件：** 未装组件不显示可调用；字段和版本与绑定一致；跨主体隐藏内容；当前eligible不成为许可；长Docs无热路径复制。

**本包禁止扩展：** 不手写第二套AI参数说明。


<a id="d204"></a>
#### D2.04｜Control方法、观察协议帧与结果映射

**执行分级：** Critical。

**合并安排：** 帧/路由/Outcome映射＋订阅序列＋cursor codec同批复用观察合同；可与D2.03条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 非法帧与方向、MAC/TTL/身份、ack/event竞态；mock仅作帧级证明。

**前置依赖：** [D2.02](#d202)、[D1.04](#d104)、[D0.03](#d003)、[D0.04](#d004)  
**架构依据／测试族：** [A05](01_Architecture_v3.3.md#a05)、[A17](01_Architecture_v3.3.md#a17)、[A09](01_Architecture_v3.3.md#a09)、[A10](01_Architecture_v3.3.md#a10)、[A15](01_Architecture_v3.3.md#a15)、[A21](01_Architecture_v3.3.md#a21) ／ T04、T06、T19、T20、T22

**实施内容：** 实现OCK1帧解析与JSON-RPC路由、握手版本、预算、invoke及已声明的查询骨架；结构错误与业务Outcome分别映射。 实现N1的协议编解码、方向检查、连接级订阅表/序列门和list请求验证；观察源经窄端口注入。只运行mock源帧级合同，真实任务投影由D3接入；握手如实标明能力。 为 `execution.list` 实现 v1 无状态 cursor codec/MAC：不创建服务端 cursor 对象或 pin，MAC secret 绑定 host incarnation，解析先做长度/版本/MAC/TTL再进入扫描。

**交付产物：** packages/control/{protocol,server}/（ControlProtocol与Control）；schemas/rpc-v1/；tests/contract/protocol/。 `packages/control/observation/`仅作为 `OCK::Control` 内部源码目录；RPC订阅/列表golden、cursor token golden/tamper cases 与mock源竞态测试；不新增 `OCK::Observation` target。

**通过条件：** 非法长度/flags/fragment/batch及错误方向notification明确拒绝；未知效果事实不被RPC error抹掉；未装plan endpoint不暴露。 ack先于本订阅event，client伪造server event不执行业务；未接真实Task不宣称观察完成；subscribe失败/断线不泄漏，未知方法不伪造成功；list cursor超长/MAC错/过期/跨host/跨caller/过滤篡改均明确拒绝且不静默重启；服务端逐cursor句柄计数恒为0。

**本包禁止扩展：** 不在Control链接Automation或Workspace。


<a id="d205"></a>
#### D2.05｜实现真实Named Pipe与认证会话

**执行分级：** Critical。

**合并安排：** Pipe/DACL/对端认证＋部分读写/关闭＋控制优先与慢流同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 真实双进程认证拒绝、断线/在途撤权、跨连接控制可用。

**前置依赖：** [D2.04](#d204)、[D0.06](#d006)  
**架构依据／测试族：** [A10](01_Architecture_v3.3.md#a10)、[A17](01_Architecture_v3.3.md#a17)、[A15](01_Architecture_v3.3.md#a15)、[A21](01_Architecture_v3.3.md#a21) ／ T19、T20、T22

**实施内容：** 实现显式DACL、本地限制、对端验证、Asio部分读写、连接关闭和控制/日志分队列；会话创建VerifiedCaller。 验证通知帧上限与控制优先写调度；双连接观察/控制样例分别认证；处理部分写入、退订在途与持续慢读关闭，不假设分队列消除字节流队头阻塞。

**交付产物：** packages/adapters/local_ipc/；tests/integration/windows_ipc/。

**通过条件：** 两个真实进程通讯；未授权用户/远端/伪角色拒绝；断线清理；慢连接不能占满控制配额；不以仅内存的模拟传输冒充真实IPC。 同流已发送字节不可撤回的边界明确；其他连接取消可受理；断线只清订阅不影响服务器执行。

**本包禁止扩展：** 不增加互联网监听或内置AI。


<a id="d206"></a>
#### D2.06｜实现薄CLI、意图文件和原生/动态演示

**执行分级：** Standard。

**合并安排：** CLI参数/UTF-8/输出＋意图文件＋能力探测/list/watch语法同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 真实Shell、同key改参、Ctrl+C只停止等待、薄客户端依赖边界。

**前置依赖：** [D2.03](#d203)、[D2.05](#d205)、[D1.06](#d106)  
**架构依据／测试族：** [A17](01_Architecture_v3.3.md#a17)、[A18](01_Architecture_v3.3.md#a18) ／ T02、T20、T24

**实施内容：** CLI11子命令；实例选择、capabilities、invoke，保存VolatileHost范围的Intent文件；--file/--stdin、UTF-8、stdout/stderr和退出码。 加入execution list/watch语法与能力探测；未有真实执行Provider时明确拒绝/不暴露能力，不能用模拟列表宣布任务功能完成。watch客户端算法预备为subscribe后get。

**交付产物：** packages/control/client/（OCK::ControlClient）；apps/ock/；examples/stateless_service/；tests/integration/cli/。

**通过条件：** CLI不链接服务端Runtime且不创建第二Host；关闭终端不销毁宿主；同文件改参冲突；Ctrl+C默认仅停止等待；实际Shell子进程读取机器结果。 CLI不承载Runtime或订阅全局状态；退出/退订默认不发送业务cancel。

**本包禁止扩展：** Durable未实现前必须标记Volatile，不伪承诺跨重启去重。


<a id="d207"></a>
#### D2.07｜验收双入口与首次Shell闭环

**执行分级：** Standard。

**合并安排：** 双入口正反例＋真实Shell闭环＋首轮成本统计集中运行；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 真实IPC、未装能力拒绝、结果一致；观察帧不冒充真实任务。

**前置依赖：** [D2.06](#d206)  
**架构依据／测试族：** [A03](01_Architecture_v3.3.md#a03)、[A04](01_Architecture_v3.3.md#a04)、[A17](01_Architecture_v3.3.md#a17)、[A22](01_Architecture_v3.3.md#a22)、[A15](01_Architecture_v3.3.md#a15)、[A21](01_Architecture_v3.3.md#a21) ／ T02、T03、T04、T06、T19、T20、T23

**实施内容：** 以相同业务输入对比Native、JSON、本地CLI；完善非法参数、拒绝、结果事实和目录一致性；记录第一组执行成本而非宣传目标。 增加N1消息编码和能力缺失反例、第一组观察协议帧级证据；真正任务订阅/list整体验收留D3.07并在报告标清。

**交付产物：** evidence/G2/；parity cases；小请求样本和构建manifest。

**通过条件：** 正负行为一致；测试含真实IPC且未装能力明确失败；保留单次延迟和分配统计；不把样例成功当任务/事务完成。 测试证据由E03工具生成，发现清单与批准expected逐项对照；Native/Dynamic测量口径不混用。

**本包禁止扩展：** 不提前认证完整内核。


**G2 集成放行：** 绑定一致性、目录、帧、认证、CLI编码/退出码和观察协议帧级合同通过；不用内存假传输代替IPC；未接真实执行的观察能力不宣称完成。


### D3｜任务、资源与结构化寿命


<a id="d301"></a>
#### D3.01｜Executor Conformance Kit与生产/测试后端

**执行分级：** Critical。

**合并安排：** 生产池/可控/合法inline后端＋同版Executor合同同批；可与D2.01条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 恰好一次完成、拒绝无迟到work、worker自等待与真实线程排空。

**前置依赖：** [D1.02](#d102)、[D0.06](#d006)  
**架构依据／测试族：** [A04](01_Architecture_v3.3.md#a04)、[A09](01_Architecture_v3.3.md#a09)、[A22](01_Architecture_v3.3.md#a22) ／ T12、T22、T24

**实施内容：** 实现BS pool适配和可控测试Executor；submit失败不持有回调、inline completion、重复完成检测、worker检查与最终排空。 把公共接受/完成/拒绝/排空规则做成同一套版本化ExecutorConformance，以工厂注入BS pool、确定性和合法inline后端；故意违规后端只做消费者防御测试，真实线程另有专项。

**交付产物：** packages/adapters/cpu_pool/；tests/contract/executor/。 tests/conformance/executor/；conformance_manifest与capability适用矩阵；各后端共同合同、专项和运行结果。

**通过条件：** 成功恰好一次完成；拒绝/异常无迟到work；callback抛错不泄漏；worker自等待/自毁拒绝；明确停止失败。 共同必需用例不可skip；可选NotApplicable有合同原因不计Passed；生产池不被迫实际inline；Runtime必须能承受合法inline；更换实现复用同套预期。

**本包禁止扩展：** 不让线程池自行拥有业务Task状态。


<a id="d302"></a>
#### D3.02｜实现公平Ready调度与依赖结构

**执行分级：** Critical。

**合并安排：** Ready队列/权重＋依赖计数/deadline＋投递上限同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 满载/配额、公平与唤醒竞争、历史规模不线性pump。

**前置依赖：** [D3.01](#d301)、[D1.03](#d103)  
**架构依据／测试族：** [A09](01_Architecture_v3.3.md#a09)、[A21](01_Architecture_v3.3.md#a21) ／ T13、T23

**实施内容：** 就绪队列、主体权重、优先级上限、依赖计数与deadline结构；活跃表和终态历史分开；限制投递到线程池的数量。

**交付产物：** packages/runtime/scheduler/；tests/unit/scheduler/；饥饿/历史规模基准。

**通过条件：** 队列满明确拒绝；高优先级不绕主体配额；历史规模不造成线性pump；仅对可运行工作验证公平。

**本包禁止扩展：** 不自研无锁调度器或第二任务框架。


<a id="d303"></a>
#### D3.03｜实现资源归一化、MultiClaim与租约

**执行分级：** Critical。

**合并安排：** 资源归一＋MultiClaim/units＋租约释放/waiter唤醒同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 半获失败零占用、别名冲突、溢出、父子死锁与释放归属。

**前置依赖：** [D3.02](#d302)、[D1.04](#d104)  
**架构依据／测试族：** [A06](01_Architecture_v3.3.md#a06)、[A09](01_Architecture_v3.3.md#a09) ／ T07、T13

**实施内容：** 资源别名归一、Shared/Exclusive/units、溢出检查、原子全获取；waiter索引和释放唤醒；计算/提交阶段区分。

**交付产物：** packages/runtime/resources/；tests/contract/resources/。

**通过条件：** 读写同槽；半获失败不留占用；Lease只释放己有；等待子任务不持有其必需资源；未知键不能无限建槽。

**本包禁止扩展：** 不把快照读取变成长期写阻塞。


<a id="d304"></a>
#### D3.04｜Submit、执行投影索引与拥有型输入

**执行分级：** Critical。

**合并安排：** Submit拥有输入/接受＋get/wait终态缓存＋list/观察索引同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 接受前后失败事实、输入寿命/ASan、owner权限与一致观察版本。

**前置依赖：** [D3.02](#d302)、[D3.03](#d303)、[D1.05](#d105)  
**架构依据／测试族：** [A04](01_Architecture_v3.3.md#a04)、[A05](01_Architecture_v3.3.md#a05)、[A09](01_Architecture_v3.3.md#a09)、[A10](01_Architecture_v3.3.md#a10)、[A15](01_Architecture_v3.3.md#a15)、[A17](01_Architecture_v3.3.md#a17)、[A21](01_Architecture_v3.3.md#a21) ／ T02、T06、T12、T19、T20、T23

**实施内容：** 一个Operation可Invoke/Submit；预留ExecutionRef、owner输入和结果额度；只做Volatile接受；get/wait权限检查和terminal缓存。 维护Runtime的owner/保留状态索引与宿主listing_ordinal；一致ExecutionSummary和observation_version；类型化list/观察适配，不让RPC/Data进入核心。

**交付产物：** packages/runtime/executions/；tests/contract/submit/。 packages/runtime/executions/index/；tests/contract/execution_observation/；实时keyset分页与状态投影合同。

**通过条件：** 顶层接受前失败不运行；形成Accepted后Executor拒绝也保留身份并形成失败事实；输入不借用已释放请求；同业务不注册第二TaskHandler。 非终态包括Finalizing/Suspended；有界扫描游标必前进；短Invoke无执行表条目；通知满队列不能阻断内部完成或延长结果pins。

**本包禁止扩展：** 不在所有Invoke中强制ExecutionRef或JSON。


<a id="d305"></a>
#### D3.05｜实现取消、期限与permit竞争

**执行分级：** Critical。

**合并安排：** cancel/deadline仲裁＋等待超时分离＋控制预算同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 开始/取消/提交先后、时间边界、满载真实线程控制受理。

**前置依赖：** [D3.04](#d304)、[D0.05](#d005)  
**架构依据／测试族：** [A05](01_Architecture_v3.3.md#a05)、[A06](01_Architecture_v3.3.md#a06)、[A09](01_Architecture_v3.3.md#a09) ／ T07、T12、T13

**实施内容：** 实现cancel意图、queued/started仲裁、deadline转换、wait_timeout分离；测试对commit gate的协作接口；保留控制队列预算。

**交付产物：** packages/runtime/cancellation/；tests/model/cancel_commit/；真实线程压力。

**通过条件：** 排队取消防开始；开始后协作；迟到取消不能改已应用事实；满载cancel可受理；时间回拨/已过期恢复有明确定义。

**本包禁止扩展：** 不承诺抢占停止C++或物理机床。


<a id="d306"></a>
#### D3.06｜实现父子寿命、Finalizing与失败收尾

**执行分级：** Critical。

**合并安排：** 父子归属/传播＋WaitingChild/Finalizing＋可靠完成和停止同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 父不早终态、回调排空/重入、故障隔离、丢通知仍能查终态。

**前置依赖：** [D3.04](#d304)、[D3.05](#d305)  
**架构依据／测试族：** [A05](01_Architecture_v3.3.md#a05)、[A09](01_Architecture_v3.3.md#a09)、[A16](01_Architecture_v3.3.md#a16)、[A15](01_Architecture_v3.3.md#a15)、[A17](01_Architecture_v3.3.md#a17)、[A22](01_Architecture_v3.3.md#a22) ／ T06、T12、T19、T22

**实施内容：** parent/child归属、cancel传播、WaitingChild continuation、Finalizing必要回调；实现有界故障处置和隔离owner规则。 把可靠内部完成通道与有损外部Notification分开，事实/phase在合法发布边界更新观察版本；停止清理不依赖客户端确认终态。

**交付产物：** packages/runtime/completion/；tests/contract/structured_lifetime/。

**通过条件：** 父不早终态；callback全收尾后才quiescent；普通日志不阻断每任务完成；必需记录器的受控注入故障有明确事实与诊断。 丢最后一条终态提示、满订阅、断线仍可get/wait准确结果；Finalizing不提前Terminal；Conformance违规被检测且不双释放。

**本包禁止扩展：** 不通过detach线程获得关机成功。


<a id="d307"></a>
#### D3.07｜真实任务观察CLI、完整Embedded占用与停止门禁

**执行分级：** Critical。

**合并安排：** 依次合并G3-A寿命、G3-B真实观察、G3-C占用；一个父包收口；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 多进程慢流/撤权/关闭、完整Embedded预算；三个checkpoint不能替代G3。

**前置依赖：** [D3.06](#d306)、[D2.07](#d207)  
**架构依据／测试族：** [A09](01_Architecture_v3.3.md#a09)、[A16](01_Architecture_v3.3.md#a16)、[A17](01_Architecture_v3.3.md#a17)、[A22](01_Architecture_v3.3.md#a22)、[A10](01_Architecture_v3.3.md#a10)、[A15](01_Architecture_v3.3.md#a15)、[A19](01_Architecture_v3.3.md#a19)、[A21](01_Architecture_v3.3.md#a21) ／ T01、T03、T12、T13、T19、T20、T22、T23、T24

**实施内容：** 接入真实submit/get/wait/cancel；两个CLI进程查同任务；故障callback、慢日志、资源冲突与Host stop一起压测。 D3.07-a接真实list/subscribe/unsubscribe/event/watch，两个CLI进程观察控制同任务；D3.07-b测subscribe→get竞态、合并gap、撤权队列、退订在途、断线/重连、弱一致分页、无状态cursor篡改/超长/跨host/TTL、配额和慢流；D3.07-c按A21.4测完整Embedded（固定2 workers、≤3新增线程）并审批有限预算，观察开启压力单列AutomationHost。

**内部进度 checkpoint（不新增正式 G 门禁）**：`G3-A`=Submit/资源/取消/父子/Finalizing/drain 核心寿命；`G3-B`=真实多进程 list/subscribe/watch 与全部观察竞态；`G3-C`=完整 Embedded footprint/分配/线程/Ready。A/B/C 可以分别用于项目进度管理，但**只有父 D3.07 的全部条件满足才允许 G3 Passed**，后续 D4/D5 不得把某个子 checkpoint 当成 G3 的替代。

**交付产物：** evidence/G3/；examples/stateless_service任务版；Windows进程测试。 schemas/rpc-v1观察正式合同；CLI watch/list样例；evidence/G3/三子项报告；footprint-budgets.json的Embedded值；正式订阅quota配置；stateless cursor CPU/长度/拒绝路径样本。

**通过条件：** 等待超时/CLI退出不取消工作；控制不被挤占；stop超时依赖仍在；恢复未实现时不报可恢复；保存性能样本。 首次真实观察闭环在G3前完成；不能依赖最后通知判断终态；cursor身份、MAC、TTL与前进正确且不产生服务端句柄积累；无JSON/Asio的Embedded体积/内存/Ready/线程/分配预算均有事实，不以G1子集冒充。

**本包禁止扩展：** 不把G3称完整持久套件。 不延后N1到D7才首次实现，不加入ACK/持久进度历史，不因list给Native短调用创建Task。


**G3 集成放行：** 真实跨CLI submit/get/wait/cancel/list/subscribe/watch及竞态通过；Executor共同合同、满载控制、父子收尾/排空与完整Embedded有限占用预算通过。


### D4｜内存状态、Plan与Atomic


<a id="d401"></a>
#### D4.01｜实现轻量状态域与结构共享Snapshot

**执行分级：** Standard。

**合并安排：** StateDomain/配置根＋共享Snapshot＋revision/history/generation同批；可与D4.05条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 冻结别名/ASan、旧世代拒绝、无文档消费者与快照成本。

**前置依赖：** [D1.02](#d102)、[D0.06](#d006)、[D3.03](#d303)  
**架构依据／测试族：** [A07](01_Architecture_v3.3.md#a07) ／ T01、T08、T09、T23

**实施内容：** 实现AtomicProvider与StateDomain；轻量配置根先行，通用对象根使用私有immer；PublishedState整体包含revision/history/generation。

**交付产物：** packages/state/snapshot/；examples/settings_service/；tests/unit/state_roots/。

**通过条件：** 无Project/Document可取快照；快照取得不深复制；冻结后无可变别名；旧句柄/关闭重开generation拒绝。

**本包禁止扩展：** 不把所有状态强制JSON或数据库化。


<a id="d402"></a>
#### D4.02｜实现EditView、WriteSet、约束与内存History

**执行分级：** Standard。

**合并安排：** EditView/WriteSet＋约束/反向引用＋差量History准备同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 悬空引用、无commit能力、空delta/Undo新revision、锁外释放。

**前置依赖：** [D4.01](#d401)  
**架构依据／测试族：** [A07](01_Architecture_v3.3.md#a07)、[A14](01_Architecture_v3.3.md#a14) ／ T08、T09、T18

**实施内容：** 受限编辑、创建替换删除、read-your-writes、反向引用/索引更新、差量History准备；域级冲突为首版。

**交付产物：** packages/state/edit/；packages/state/history/；tests/contract/state_constraints/。

**通过条件：** 删除引用对象不会遗漏悬空引用；EditView无commit；有效空delta语义按A05；Undo为新revision；旧根释放在锁外。

**本包禁止扩展：** 不实现自动merge、任意MVCC或跨域原子。


<a id="d403"></a>
#### D4.03｜实现内存CommitCoordinator与发布gate

**执行分级：** Critical。

**合并安排：** Preparing/许可＋reservation＋PublishedState/回执gate同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 撤权/关闭/版本竞争、无撕裂发布、迟到回调不覆盖新根。

**前置依赖：** [D4.02](#d402)、[D3.05](#d305)、[D0.05](#d005)  
**架构依据／测试族：** [A05](01_Architecture_v3.3.md#a05)、[A06](01_Architecture_v3.3.md#a06)、[A07](01_Architecture_v3.3.md#a07) ／ T06、T07、T09

**实施内容：** 实现Preparing到Finalized，域reservation与许可消费；单个PublishedState替换，回执查询按发布门控制，失败不安装候选。

**交付产物：** packages/state/commit/；tests/contract/memory_commit/；deterministic schedules。

**通过条件：** cancel/revoke/close/revision先后均正确；拿到permit不是成功；无撕裂root/history；迟到回调不能覆盖新根。

**本包禁止扩展：** 不持锁调用业务或等待完成回调。


<a id="d404"></a>
#### D4.04｜实现独立编辑与Atomic组同源绑定

**执行分级：** Critical。

**合并安排：** 单操作/组同源绑定＋candidate_read/compute＋预检/逐项权限同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 单域一次提交、任一步失败零部分发布、Effect/Await/嵌套拒绝。

**前置依赖：** [D4.03](#d403)、[D1.05](#d105)  
**架构依据／测试族：** [A04](01_Architecture_v3.3.md#a04)、[A07](01_Architecture_v3.3.md#a07) ／ T02、T07、T08、T10

**实施内容：** 同一state_edit函数用于单操作与组；组内candidate_read/pure_compute资格；完整target/resource预检和逐项权限。

**交付产物：** packages/state/atomic/；tests/contract/atomic_provider/。

**通过条件：** 单provider/domain一次提交；任一步失败无状态/历史/事件部分发布；组内target继承只来自域；Effect/await/ticket/嵌套拒绝。

**本包禁止扩展：** 不循环普通StateEdit命令来伪装合并事务。


<a id="d405"></a>
#### D4.05｜实现PlanCompiler、slots与预算IR

**执行分级：** Standard。

**合并安排：** Schema/作用域/绑定＋预算IR/slots＋测试级roundtrip同批；可与D4.01条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 错类型/控制边/预算损坏、精确身份重绑、动态权限不缓存。

**前置依赖：** [D2.02](#d202)、[D0.04](#d004)、[D3.04](#d304)  
**架构依据／测试族：** [A08](01_Architecture_v3.3.md#a08)、[A19](01_Architecture_v3.3.md#a19) ／ T04、T05、T11

**实施内容：** 结构Schema、作用域、精确op/shape、pointer、引用类型、静态常量与运行期未决检查；生成有界IR和结果槽布局。 增加 N9 测试级 IR semantic roundtrip：把编译 IR 投影为 neutral test representation，再读回/重绑定并比较节点语义、精确Operation、slot/作用域、控制边、预算、Atomic/exports/dynamic-check 等价。

**交付产物：** packages/automation/compiler/；schemas/plan-v1.schema.json定稿；tests/plan/compiler/；tests/plan/ir_roundtrip/；测试表示 schema/说明与 semantic equality helper。

**通过条件：** 无wire return；不可伪造ExecutionRef；变量遮蔽/前向/重叠绑定拒绝；嵌套总预算生效；不缓存动态权限。 roundtrip 覆盖全部首版节点和边界，不保存/比较指针、函数地址、allocator、mutex、coroutine frame 或原始 registry handle；序列化后读回必须重新按精确身份解析合法绑定。至少一组故意丢 slot 类型/控制边/预算/Operation digest 的损坏样例被检测。

**本包禁止扩展：** 不引入任意eval或新增一套解释语言；不把 test representation 当作公开 Plan wire、内部 IR ABI 或 DurablePlan 持久格式。


<a id="d406"></a>
#### D4.06｜实现顺序Call/Await与Atomic调度

**执行分级：** Critical。

**合并安排：** 顺序Call＋Await续体＋Atomic exports/槽回收同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** worker不阻塞、提交后导出、部分/未知事实、结果拥有性。

**前置依赖：** [D4.05](#d405)、[D4.04](#d404)、[D3.06](#d306)  
**架构依据／测试族：** [A05](01_Architecture_v3.3.md#a05)、[A07](01_Architecture_v3.3.md#a07)、[A08](01_Architecture_v3.3.md#a08) ／ T06、T08、T10、T11、T12

**实施内容：** PlanRunner依次调用同一Invocation；delivery complete/ticket、Await挂起与Atomic exports；结果槽owning且可回收。

**交付产物：** packages/automation/runtime/；tests/plan/sequence_atomic/。

**通过条件：** Await不阻塞CPU worker；Atomic只有提交后可导出；顺序前步提交保留；完整成功PlanCompleted；部分与未知结果正确聚合。

**本包禁止扩展：** 不另建Workflow解释器，不把全Plan当事务。


<a id="d407"></a>
#### D4.07｜实现If/ForEach/Parallel与失败收尾

**执行分级：** Critical。

**合并安排：** If/ForEach＋有界Parallel＋预算和失败收尾同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 并行子寿命、同EditView不并发、固定序号与预算超限不截断成功。

**前置依赖：** [D4.06](#d406)  
**架构依据／测试族：** [A08](01_Architecture_v3.3.md#a08)、[A09](01_Architecture_v3.3.md#a09) ／ T10、T11、T12、T13

**实施内容：** 同类型分支exports、固定循环集合、稳定索引与有界并发；并行停止启动并收尾，结果按定义序号；总指令与slot预算。

**交付产物：** packages/automation/control_flow/；tests/plan/branches/；预算fuzz种子。

**通过条件：** 不依完成时间重排输出；失败后child不孤立；同EditView不并发；超过循环预算不截断当成功；所有故障有正确Outcome。

**本包禁止扩展：** 不自动推断无冲突并行，不开放首版Atomic控制流。


<a id="d408"></a>
#### D4.08｜验收内存套件、Shell Plan和无文档Atomic

**执行分级：** Critical。

**合并安排：** Control Plan入口＋C-A/C-B＋Shell/Atomic观察一次集成；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 中间候选不外泄、同域提交次数、真实无文档安装；仅放行内存套件。

**前置依赖：** [D4.07](#d407)、[D3.07](#d307)、[D2.03](#d203)  
**架构依据／测试族：** [A02](01_Architecture_v3.3.md#a02)、[A08](01_Architecture_v3.3.md#a08)、[A17](01_Architecture_v3.3.md#a17)、[A23](01_Architecture_v3.3.md#a23)、[A15](01_Architecture_v3.3.md#a15) ／ T01、T08、T10、T11、T19、T20、T23、T24

**实施内容：** Control注册plan.check/submit；C-A顺序任务，C-B两参数原子编辑；Shell一次提交引用前结果、后台等待、短应用。 用同一观察接口监控Plan与Atomic候选；候选步骤、状态Published、Finalizing、Terminal投影各有断言，不新建计划专属消息系统。

**交付产物：** evidence/G4/；examples/plans/；两个真实无文档消费者及性能对照。

**通过条件：** 一次调用多步骤；独立/顺序/原子/批量提交次数有统计；JSON和实际编译规则一致；缺Durable明示；安装无Workspace通过。 Atomic未提交中间值不能作为正式结果外泄；观察者退出不改变计划/提交语义。

**本包禁止扩展：** G4仅为可运行内存套件，不能宣称恢复、Workflow已完成。


**G4 集成放行：** 无状态与无文档状态消费者通过；一次Shell Plan、同域一次提交、typed slots与实际结果事实一致。


### D5｜Durable、事务证据与恢复


<a id="d501"></a>
#### D5.01｜Storage Conformance与真实SQLite独占

**执行分级：** Critical。

**合并安排：** 存储独占/WAL-FULL＋有界writer＋共同Storage合同同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 第二进程零写入、Busy/full/I/O、真实持久配置；内存不冒充耐久。

**前置依赖：** [D0.05](#d005)、[D0.06](#d006)、[D3.01](#d301)  
**架构依据／测试族：** [A11](01_Architecture_v3.3.md#a11)、[A20](01_Architecture_v3.3.md#a20)、[A22](01_Architecture_v3.3.md#a22) ／ T14、T16、T22、T24

**实施内容：** 实现规范化存储根OS独占、SQLite WAL/FULL/foreign_keys读回、DB writer有界队列、预备语句、短读/检查点和隔离状态。 建立StorageConformance事务共同合同，以内存测试实现和SQLite工厂运行；SQLite另跑真实独占/WAL/FULL/进程/I/O专项，能力适用由已审查manifest决定。

**交付产物：** packages/durable/storage/；packages/adapters/sqlite/；Windows第二进程竞争探针。 tests/conformance/storage/；storage capability矩阵与contract_version；数据库故障fixture。

**通过条件：** 独占先于迁表/恢复；第二Host失败无写入；已知修复版本记录；Busy/full/I/O错误明示；不降级NORMAL通过测试。 内存后端不宣称耐久；共同事务语义不能因backend不同被改写；可选故障条件不适用有原因，Profile必需持久能力不能降级。

**本包禁止扩展：** 不共享网络文件系统，不让业务获取裸SQL事务。


<a id="d502"></a>
#### D5.02｜实现记录schema、codec和canonical指纹

**执行分级：** Critical。

**合并安排：** 先完成a canonical spike并审定A/B/C，再合并schema/codec/指纹；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** spike是硬前置；独立路径字节、数值/缺失边界、未知组件与预算。

**前置依赖：** [D5.01](#d501)、[D2.02](#d202)  
**架构依据／测试族：** [A11](01_Architecture_v3.3.md#a11)、[A12](01_Architecture_v3.3.md#a12)、[A19](01_Architecture_v3.3.md#a19) ／ T04、T05、T14、T15

**实施内容：** 本包先执行阻塞子项 `D5.02-a canonical feasibility spike`，再进入正式 schema/codec。Spike 用固定 typed vectors 验证所选 jsoncons/候选路径是否满足 A12.4 的 deterministic map、最短整数、最短可精确浮点、`-0`、int/float、Missing/null、禁止非有限/indefinite/repeated-key 与跨独立路径字节一致。Spike 结论只允许：A 直接使用 jsoncons；B jsoncons＋薄 canonical policy；C 有限 profile 专用 encoder。结论经 ADR/评审后，才生成执行/intent/effect/commit表族和唯一约束；实现ock.canonical/1、SHA-256、稳定身份、组件schema注册和codec预算。

**交付产物：** `evidence/D5.02-a/`；`docs/adr/canonical-cbor-encoder.md`；canonical spike harness/golden vectors；packages/durable/records/；schema SQL；正式 canonical golden vectors；tests/codec/。

**通过条件：** D5.02-a 在所锁库/编译器上得到明确 A/B/C 结论且所有必需向量可重复；失败或未知时 D5.02 保持 Blocked，不允许先写幂等/PlanDigest 再补规则。正式实现跨独立编码路径字节一致；Missing/null、整数/浮点/-0按契约；不持久指针；未知组件拒绝写启；表只随组件注册。若选择 C，只实现 `ock.canonical/1` 有限类型，不形成第二通用CBOR框架。

**本包禁止扩展：** 不写旧项目迁移器；不把hash叫签名；不把普通 `encode_cbor()` 输出未经证明直接当 canonical。


<a id="d503"></a>
#### D5.03｜实现外部Intent claim与epoch GC

**执行分级：** Critical。

**合并安排：** existing-first/claim＋epoch/floor事务GC＋意图文件接入同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 同key并发唯一、改参冲突、水位竞争不复活、不得自动换key。

**前置依赖：** [D5.02](#d502)、[D1.04](#d104)  
**架构依据／测试族：** [A12](01_Architecture_v3.3.md#a12) ／ T07、T15、T20

**实施内容：** 实现scope、existing-first查询、epoch/floor窗口检查与唯一claim；水位推进和删除按事务；SDK意图文件接durable范围；Volatile窗口内intent同样不许被LRU提前淘汰。

**交付产物：** packages/durable/intents/；tests/contract/dedup/；GC参考模型对照。

**通过条件：** 同key并发仅一逻辑执行；改参冲突；过期/未来拒绝；水位竞争不复活；旧在途有记录仍可查询；不自动换key。

**本包禁止扩展：** 不承诺永久无界exactly-once。


<a id="d504"></a>
#### D5.04｜实现DurableAccepted与任务记录收尾

**执行分级：** Critical。

**合并安排：** 输入耐久资格＋接受回执/调度＋终态记录/查询投影同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 接受前后crash、临时输入拒绝、提交未发布不误报已应用。

**前置依赖：** [D5.03](#d503)、[D3.06](#d306)  
**架构依据／测试族：** [A05](01_Architecture_v3.3.md#a05)、[A09](01_Architecture_v3.3.md#a09)、[A11](01_Architecture_v3.3.md#a11)、[A17](01_Architecture_v3.3.md#a17) ／ T06、T12、T14、T16、T19、T20

**实施内容：** 输入与必要DataRef耐久资格检查、execution+接受回执同事务；提交后调度；最终事实保存与失败收尾、publication-aware查询。 恢复/查询投影使用当前host_incarnation的listing_ordinal与观察版本；phase/fact只在允许可见时更新，不要求把每次易失progress写数据库。

**交付产物：** packages/durable/execution_records/；Runtime记录adapter；crash acceptance probes。

**通过条件：** 接受前进程退出无假Accepted；接受后执行前可查；记录故障不虚报Durable；输入仅临时文件时拒绝durable接受。 durable已提交但未Published不能从get/list/notification误报已应用；正常重启后客户端重建订阅/游标。

**本包禁止扩展：** 不把普通日志flush当任务必需记录。


<a id="d505"></a>
#### D5.05｜实现StateDurableBridge与无撕裂发布

**执行分级：** Critical。

**合并安排：** CommitBatch材料＋DB等待reservation＋回域发布/gate同批；可与D5.06条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** SQL错误/异常/crash、COMMIT后不重执行业务、root/history无撕裂。

**前置依赖：** [D5.04](#d504)、[D4.03](#d403)  
**架构依据／测试族：** [A06](01_Architecture_v3.3.md#a06)、[A11](01_Architecture_v3.3.md#a11) ／ T06、T07、T09、T14、T16

**实施内容：** 把delta、history、outcome、intent和必要Outbox编成同一CommitBatch；reservation跨DB等待；完成续体回域；gate屏蔽提前成功查询。

**交付产物：** packages/bridges/state_durable/；tests/crash/state_commit/；raw SQL对照。

**通过条件：** Begin/Write/Commit返回错误和抛异常均检查所有材料；COMMIT后发布前恢复不重执行业务；root/history不可撕裂；旧回调不覆写。

**本包禁止扩展：** 不独立提交业务状态与成功回执；不持mutex等DB。


<a id="d506"></a>
#### D5.06｜实现ExternalEffect claim、许可和对账

**执行分级：** Critical。

**合并安排：** Effect claim/permit/发送＋结果记录＋追加对账同批；可与D5.05条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 动作前无许可不发、动作后未知不重发；真实本地模拟效果计数。

**前置依赖：** [D5.04](#d504)、[D1.04](#d104)、[D0.03](#d003)  
**架构依据／测试族：** [A04](01_Architecture_v3.3.md#a04)、[A05](01_Architecture_v3.3.md#a05)、[A06](01_Architecture_v3.3.md#a06)、[A11](01_Architecture_v3.3.md#a11)、[A12](01_Architecture_v3.3.md#a12) ／ T06、T07、T16

**实施内容：** 使用可注入模拟设备完成真实本地效果链：claim、许可消费、发送、结果保存；支持已知无/部分/全部效果及未知；ResolutionRecord追加。

**交付产物：** packages/durable/effects/；examples/mock_device_service/；effect crash probes。

**通过条件：** 动作前无记录/无许可则不发；动作后丢outcome未知；输出Schema错误不变未执行；对账不盲重发；不含真实机台运动。

**本包禁止扩展：** 不承诺一般设备exactly-once或软件取消替代急停。


<a id="d507"></a>
#### D5.07｜实现Outbox、pins与有界记录回收

**执行分级：** Critical。

**合并安排：** Outbox交付＋pins/引用GC＋通知与可靠事件隔离同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** Published屏障、重启重投不重执行、pin竞争、慢消费者不堵commit。

**前置依赖：** [D5.05](#d505)、[D5.06](#d506)  
**架构依据／测试族：** [A12](01_Architecture_v3.3.md#a12)、[A13](01_Architecture_v3.3.md#a13)、[A15](01_Architecture_v3.3.md#a15)、[A17](01_Architecture_v3.3.md#a17) ／ T14、T15、T19、T20

**实施内容：** 实现同事务Outbox、Published门、游标/重投/去重、pins和记录引用GC；状态缓存与必要历史分开。 验证N1有损进度协议与Outbox可靠业务事件严格分开；通知订阅不增加永久receipt/资产pins，也不拉入每步数据库写入。

**交付产物：** packages/durable/outbox/；packages/durable/retention/；tests/contract/outbox_gc/。

**通过条件：** 未Published不交付业务成功；重启先安装再重投；pin存在不删；普通慢消费者不堵commit；无订阅历史不谎称可靠补齐。 未装Durable的订阅仍可用；启用Durable后不自动宣称进度replay；结果保留与订阅寿命互不混淆。

**本包禁止扩展：** 不把重投事件变成重执行业务命令。


<a id="d508"></a>
#### D5.08｜实现数据库恢复、备份和RestoreGeneration

**执行分级：** Critical。

**合并安排：** 恢复/坏材料隔离＋备份manifest＋新世代/观察重建同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 旧备份封自动重放、损坏现场保留、游标失效；不冒充断电认证。

**前置依赖：** [D5.05](#d505)、[D5.06](#d506)、[D5.07](#d507)  
**架构依据／测试族：** [A12](01_Architecture_v3.3.md#a12)、[A14](01_Architecture_v3.3.md#a14)、[A17](01_Architecture_v3.3.md#a17) ／ T15、T16、T18、T19、T20、T22

**实施内容：** 恢复可信snapshot/delta/执行事实，隔离坏材料；Backup API＋manifest；显式旧备份恢复创建新世代并封自动效果重放。此时资产正文集成留D6.02。 恢复完成重建当前观察序号与执行列表索引，旧host/restore的cursor和stream句柄失效；历史创建时间不冒充当前listing顺序。

**交付产物：** packages/durable/recovery/；tools/store_inspect/；backup/restore工具；故障材料。

**通过条件：** 正常重启不换逻辑意图范围；旧备份恢复不会把缺失动作当未执行；未知组件不删表；损坏保持原现场；只读诊断可用。 相同逻辑执行新观察世代不被客户端判作sequence倒退；未知旧订阅不能接续发送。

**本包禁止扩展：** 不把进程崩溃当断电认证；不支持手工覆盖在线db。


<a id="d509"></a>
#### D5.09｜执行独立进程Durable门禁

**执行分级：** Critical。

**合并安排：** 接受/提交/效果各crash窗口＋CLI重查＋恢复观察集中集成；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 独立进程kill/FULL读回/执行计数；每个窗口分别判定，不合并成单一成功。

**前置依赖：** [D5.08](#d508)、[D4.08](#d408)  
**架构依据／测试族：** [A11](01_Architecture_v3.3.md#a11)、[A12](01_Architecture_v3.3.md#a12)、[A22](01_Architecture_v3.3.md#a22)、[A17](01_Architecture_v3.3.md#a17) ／ T06、T14、T15、T16、T18、T19、T20、T23、T24

**实施内容：** 对接受/状态提交/发布/effect/outcome/响应窗口逐点kill并新Host接管；CLI丢响应同意图查询；检查原始材料和执行计数。 复验StorageConformance证据与真实crash窗口；客户端在恢复后list/get并重订阅，旧cursor拒绝不改变Intent身份。

**交付产物：** evidence/G5/；crash manifest；durable C-B；磁盘/队列样本。

**通过条件：** 每个故障窗口有明确事实和唯一预期；无未知动作重发；FULL仍启用；退出码和成功标记同时验证；未测硬件边界明示。 报告不混用mock流与真实恢复结论，所有构建/进程/二进制通过E03绑定。

**本包禁止扩展：** 不将测试桩Error等同于全部真实恢复。


**G5 集成放行：** Storage共同/耐久专项、接受/状态/Effect/epoch/Outbox/恢复门禁通过；FULL读回；旧备份新世代；恢复后的观察游标/版本不误用。


### D6｜Workspace与持久Plan


<a id="d601"></a>
#### D6.01｜实现资产Storage与类型codec

**执行分级：** Critical。

**合并安排：** 资产codec/预算＋不可覆盖文件发布＋DataRef读取/共同合同同批；可与D6.03、D6.05条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 实际句柄路径/reparse、身份/长度/损坏、flush失败不留正式引用。

**前置依赖：** [D4.02](#d402)、[D5.02](#d502)  
**架构依据／测试族：** [A03](01_Architecture_v3.3.md#a03)、[A07](01_Architecture_v3.3.md#a07)、[A14](01_Architecture_v3.3.md#a14)、[A22](01_Architecture_v3.3.md#a22) ／ T04、T09、T18、T24

**实施内容：** 不可变对象codec、预算化资产写入、内容身份、flush/不可覆盖发布、实际句柄路径验证与DataRef授权读取。 将资产端口发布/读取/身份/长度共同规则提炼为AssetStorageConformance；复用工厂与能力manifest，Windows路径/flush另有专项。

**交付产物：** packages/adapters/storage/；packages/state/assets/；tests/integration/assets/。 tests/conformance/asset_storage/；文件存储共同与平台专项矩阵。

**通过条件：** 发布失败不创建正式引用；孤儿可回收；symlink/reparse逃逸和超长/损坏内容拒绝；大数据不经Plan反复DOM。 不复用SQL事务用例假装资产语义等同；每个合法实现运行同版适用规则，非法测试后端只用于防御。

**本包禁止扩展：** 不提供客户端任意文件路径读写。


<a id="d602"></a>
#### D6.02｜实现资产pins、快照材料和备份一致性

**执行分级：** Critical。

**合并安排：** 执行/历史/预览pins＋备份正文manifest＋引用释放/GC同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 长寿命竞争、db与正文一致、恢复完整性；不得只复制数据库。

**前置依赖：** [D6.01](#d601)、[D5.07](#d507)、[D5.08](#d508)  
**架构依据／测试族：** [A07](01_Architecture_v3.3.md#a07)、[A14](01_Architecture_v3.3.md#a14) ／ T09、T15、T18

**实施内容：** 将root/History/任务/预览/Plan/备份加入GC根；snapshot锚点与delta链；备份manifest在生成复制期间pin正文。

**交付产物：** packages/state/retention/；backup asset集成；tests/crash/assets_backup/。

**通过条件：** 长任务/历史/恢复仍用资产不被删；只复制db不算完成；备份恢复字段与内容匹配；pin释放与GC竞争正确。

**本包禁止扩展：** 不为了缩小磁盘悄悄删用户历史。


<a id="d603"></a>
#### D6.03｜实现Project/Document组织与关闭协调

**执行分级：** Critical。

**合并安排：** Project/Document注册＋关闭许可/归属＋墓碑/恢复目录同批；可与D6.01、D6.05条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 关闭重入/自lease死锁、旧generation拒绝、Runtime不依赖Document。

**前置依赖：** [D4.03](#d403)、[D3.06](#d306)、[D5.05](#d505)  
**架构依据／测试族：** [A02](01_Architecture_v3.3.md#a02)、[A07](01_Architecture_v3.3.md#a07)、[A16](01_Architecture_v3.3.md#a16) ／ T01、T07、T09、T22

**实施内容：** Workspace独立注册项目/文档生命周期，映射StateDomain；关闭转换许可、归属、活动任务、恢复目录与墓碑。

**交付产物：** packages/workspace/；examples/workspace_app/；tests/contract/workspace_lifecycle/。

**通过条件：** Runtime公开头不出现Document；关闭不被自身普通lease死锁；失败和重入不复活身份；旧编辑generation不能提交。

**本包禁止扩展：** 不添加CAD/CAM业务模型或Qt依赖。


<a id="d604"></a>
#### D6.04｜完成持久History、Undo/Redo与状态消费者

**执行分级：** Critical。

**合并安排：** History/游标/根同提交＋Undo/Redo＋两类状态消费者同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 新revision、授权/资产检查、不可撤销效果不伪装可Undo、截断GC。

**前置依赖：** [D6.02](#d602)、[D6.03](#d603)  
**架构依据／测试族：** [A07](01_Architecture_v3.3.md#a07)、[A14](01_Architecture_v3.3.md#a14) ／ T09、T14、T18、T24

**实施内容：** 将差量History、游标和StateRoot共同提交/发布；Undo/Redo授权、引用和资产检查；运行无文档State和Workspace两类消费者。

**交付产物：** packages/state/history持久集成；tests/contract/history/；三消费者工程。

**通过条件：** Undo生成新revision；原子组一个历史边界；不可撤销效果不伪装可Undo；历史截断显式且GC正确。

**本包禁止扩展：** 不把旧root指针当磁盘持久格式。


<a id="d605"></a>
#### D6.05｜实现DurablePlan运行驱动与检查点codec

**执行分级：** Critical。

**合并安排：** 唯一IR驱动＋PC/栈/槽检查点＋固定分支/循环决策同批；可与D6.01、D6.03条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 重启不改决策、精确版本缺失暂停、无指针/coroutine栈持久化。

**前置依赖：** [D4.07](#d407)、[D5.04](#d504)、[D5.07](#d507)  
**架构依据／测试族：** [A08](01_Architecture_v3.3.md#a08)、[A13](01_Architecture_v3.3.md#a13) ／ T11、T14、T17

**实施内容：** 持久保存同一IR身份、PC、控制栈、必要槽/DataRef、已选分支与循环集合；易失/持久解释动作共用。

**交付产物：** packages/durable_plan/driver/；checkpoint schema/codec；tests/contract/plan_checkpoint/。

**通过条件：** 不保存指针/coroutine栈；缺精确定义暂停；分支/随机/查询决策不因恢复变化；volatile节点语义一致。 checkpoint 恢复后与 D4.05 的 Plan 语义快照在精确 Operation、slot 类型、控制位置/边、预算剩余和 exports 规则上可证明一致，但 checkpoint codec 不等于 IR test codec。

**本包禁止扩展：** 不建设第二套Workflow语言。


<a id="d606"></a>
#### D6.06｜实现内部StepKey、ChildAdmission与父落后恢复

**执行分级：** Critical。

**合并安排：** StepKey/首次参数＋ChildAdmission/pins＋父落后查子结果同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 重试不换键、参数冲突、跨epoch内部准入、子回执先于父检查点。

**前置依赖：** [D6.05](#d605)、[D5.03](#d503)、[D5.06](#d506)  
**架构依据／测试族：** [A12](01_Architecture_v3.3.md#a12)、[A13](01_Architecture_v3.3.md#a13) ／ T15、T16、T17

**实施内容：** 固定parent/node/branch/iteration/occurrence子键与首次实际参数；内部parent准入、durable pins；父检查点落后先查子结果。

**交付产物：** packages/durable_plan/child_effects/；tests/crash/plan_parent_gap/。

**通过条件：** 普通retry不换键；长plan跨外部epoch仍有合法内部准入；pin保证receipt；参数变化冲突；不得伪造skipEpoch外部入口。

**本包禁止扩展：** 不通过新nonce或重新跑Handler补缺失父checkpoint。


<a id="d607"></a>
#### D6.07｜实现resume、重试、补偿与并行恢复

**执行分级：** Critical。

**合并安排：** Suspended/resume＋有限重试/补偿＋并行child恢复同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 新授权不替版本、未知不新attempt、补偿失败与部分事实保留。

**前置依赖：** [D6.06](#d606)、[D3.06](#d306)  
**架构依据／测试族：** [A05](01_Architecture_v3.3.md#a05)、[A08](01_Architecture_v3.3.md#a08)、[A13](01_Architecture_v3.3.md#a13) ／ T06、T10、T12、T16、T17

**实施内容：** 恢复为Suspended/NeedsReview；显式resume更新授权，不替换精确版本；有限重试、安全补偿和并行child收尾。

**交付产物：** packages/durable_plan/recovery/；tests/crash/plan_control_flow/。

**通过条件：** 只自动继续明确安全步骤；compensation是新效果可失败；未知效果不上新attempt；父Outcome含真实部分或未知结果。

**本包禁止扩展：** 不承诺整条Workflow全局原子回滚。


<a id="d608"></a>
#### D6.08｜完成三个消费者与长寿命恢复门禁

**执行分级：** Critical。

**合并安排：** 三个消费者＋长快照/资产/History＋重启恢复集中集成；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 唯一Runtime/Plan、状态资产一致、实际长期回收；共享场景分消费者报告。

**前置依赖：** [D6.04](#d604)、[D6.07](#d607)、[D5.09](#d509)  
**架构依据／测试族：** [A02](01_Architecture_v3.3.md#a02)、[A14](01_Architecture_v3.3.md#a14)、[A22](01_Architecture_v3.3.md#a22)、[A23](01_Architecture_v3.3.md#a23) ／ T01、T09、T12、T17、T18、T22、T24

**实施内容：** C-A无状态Plan，C-B无文档Atomic/Durable，C-C对象/资产/History/后台计算/持久计划；长快照与进程重启压力。

**交付产物：** evidence/G6/；三消费者组合manifest；长期pins/内存曲线。

**通过条件：** 三个消费者同Runtime/Plan；裁剪和实际功能兼容；状态与资产恢复正确；无第二状态真相；长任务结束内存回落有证据。

**本包禁止扩展：** 不把Workspace示例扩展为完整LaserCNC业务。


**G6 集成放行：** 三个消费者、History/资产GC、稳定子键和父检查点落后恢复通过；无第二解释器。


### D7｜AI自描述与完整外部控制


<a id="d701"></a>
#### D7.01｜完成统一Client SDK与PlanBuilder

**执行分级：** Standard。

**合并安排：** 统一Client/PlanBuilder＋意图resolve＋typed结果/观察辅助同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 重试同key、ticket/宿主、重连快照版本、SDK兼容与无Host任意exec。

**前置依赖：** [D4.08](#d408)、[D5.03](#d503)、[D6.07](#d607)  
**架构依据／测试族：** [A08](01_Architecture_v3.3.md#a08)、[A17](01_Architecture_v3.3.md#a17)、[A18](01_Architecture_v3.3.md#a18)、[A19](01_Architecture_v3.3.md#a19) ／ T02、T11、T19、T20、T21、T24

**实施内容：** 统一Native/CLI客户端、外部Python PlanBuilder、逻辑意图保存/resolve、typed结果/票据；Builder只产生同一wire。 Client SDK提供subscribe→snapshot合并与watch cleanup辅助、ExecutionRef/list游标类型；保留一期API快照和当前冻结消费者，不把底层私有通知方法任意投成模型标准方法。

**交付产物：** sdk/client/；sdk/python_plan_builder/；跨前端golden plans。

**通过条件：** Builder/JSON编译为等价IR；断线同key恢复；ticket不当R；子结果保留宿主；无用户脚本在Host任意exec。 接收旧观察版本不覆盖新状态；重连不复用旧订阅，自动重试不换业务意图；新增SDK重载经兼容审查。

**本包禁止扩展：** 不为Python增加另一套业务Handler。


<a id="d702"></a>
#### D7.02｜实现Check与PreparedChange Preview/Apply

**执行分级：** Critical。

**合并安排：** Check静态/未决项＋隔离Preview＋single-consumer Apply同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 无真实效果、过期/改参拒绝、并发不重复、同Intent查原结果。

**前置依赖：** [D6.02](#d602)、[D6.04](#d604)、[D7.01](#d701)  
**架构依据／测试族：** [A08](01_Architecture_v3.3.md#a08)、[A14](01_Architecture_v3.3.md#a14)、[A18](01_Architecture_v3.3.md#a18) ／ T07、T08、T18、T21

**实施内容：** 返回静态结论与动态未决项；隔离候选Preview、输入指纹、TTL、内存预算与single-consumer claim；同Intent查原应用结果。

**交付产物：** packages/automation/preview/；tests/contract/prepared_change/。

**通过条件：** Preview不产生真实外部效果；过期/改参/版本变化拒绝；并发双apply不重复；不占跨对话事务；有效候选可避免重算。

**本包禁止扩展：** 不以真实运行后尽量回滚实现dry-run。


<a id="d703"></a>
#### D7.03｜实现受限委托、可信批准与撤权闭环

**执行分级：** Critical。

**合并安排：** 委托范围＋可信批准材料＋撤权消费闭环同批；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 伪approved无效、身份/目标/版本/TTL绑定、撤权先后与OS边界。

**前置依赖：** [D7.02](#d702)、[D1.04](#d104)、[D5.06](#d506)  
**架构依据／测试族：** [A06](01_Architecture_v3.3.md#a06)、[A10](01_Architecture_v3.3.md#a10)、[A17](01_Architecture_v3.3.md#a17)、[A18](01_Architecture_v3.3.md#a18) ／ T07、T16、T20、T21

**实施内容：** 审批绑定PlanDigest/目标/版本/期限与principal；可信服务生成证据；分离OS用户、会话权限和业务Guard。

**交付产物：** packages/control/approval/；tests/security/delegation/；approval mock trusted UI。

**通过条件：** approved=true无效；扩大目标/过期/改计划拒绝；revoke与消费先后一致；同OS用户局限明确；审批不替代设备安全。

**本包禁止扩展：** 不在测试中接通真实机床/激光。


<a id="d704"></a>
#### D7.04｜实现MCP/模型投影和命令资料生成

**执行分级：** Standard。

**合并安排：** 协议协商/Schema投影＋工具卡/帮助＋按需发现同批；可与D7.05条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** annotations不授权、不降服务器验证、私有通知不冒充MCP方法。

**前置依赖：** [D2.03](#d203)、[D7.01](#d701)、[D7.03](#d703)  
**架构依据／测试族：** [A18](01_Architecture_v3.3.md#a18)、[A19](01_Architecture_v3.3.md#a19)、[A17](01_Architecture_v3.3.md#a17) ／ T04、T19、T20、T21

**实施内容：** 协商并锁定支持的MCP/模型工具版本；从唯一Contract/Docs生成工具卡，按需发现；不支持的Schema能力显式拒绝/报告。 按已协商协议投影执行枚举和观察能力；私有notifications.event只在私有传输使用，不未经协商加入MCP方法或回调。

**明确承接责任（B2 收口）：** 本包在暴露复杂 DynamicOnly 能力前补齐其服务端注册/执行适配及合同测试；它不生成弱化的 Native 等价入口。此前 B2 仅保证 SharedTypeContract 可执行，B3 CLI 不将 requires_dynamic_schema 注册失败的能力暴露为 eligible。责任决定见 [ADR-b2-closure-boundaries](adr/ADR-b2-closure-boundaries.md)。

**交付产物：** packages/adapters/mcp/；docs/commands自动输出；adapter contract tests。

**通过条件：** 已装/可见目录正确；annotations不成权限；参数语义/单位/重试与Native一致；Schema限制不会降低服务器验证。 工具资料说明进度可丢/终态查询/订阅连接寿命，不误导AI依赖最后一条日志或通知。

**本包禁止扩展：** 不依赖特定模型才可运行内核。


<a id="d705"></a>
#### D7.05｜结果投影、分页与既有观察协议压力集成

**执行分级：** Critical。

**合并安排：** 结果快照分页/DataRef＋复用观察队列＋慢流/重连压力同批；可与D7.04条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** owner/世代/TTL、不同cursor语义、撤权与回收、控制不被慢流堵塞。

**前置依赖：** [D6.02](#d602)、[D7.01](#d701)、[D5.07](#d507)、[D3.07](#d307)  
**架构依据／测试族：** [A14](01_Architecture_v3.3.md#a14)、[A15](01_Architecture_v3.3.md#a15)、[A17](01_Architecture_v3.3.md#a17)、[A18](01_Architecture_v3.3.md#a18)、[A09](01_Architecture_v3.3.md#a09)、[A21](01_Architecture_v3.3.md#a21) ／ T04、T18、T19、T20、T22、T23

**实施内容：** result.read的稳定快照cursor、DataRef owner检查、既有进度序号/gap与日志流独立队列；默认输出摘要，不回传巨大中间树。 复用D3已实现的通知与list协议，完善高并发/慢流/撤权/重连/结果过期组合；区分result.read快照cursor与execution.list实时keyset，不重写第二观察后端。

**交付产物：** packages/control/results/；tests/integration/slow_consumer/。 观察全链路压力报告；cursor/gap/retention与秘密脱敏测试；仅作用于选中Control组件的资源曲线。

**通过条件：** 跨主体猜ID拒绝；cursor世代/过期正确；慢流不阻塞cancel；终态无需看到最后一条日志；秘密脱敏。 订阅/枚举未实现不能等本包才补；有损终态仍可wait/get；列表空页续扫有界且前进；其他连接cancel在慢流下可用。

**本包禁止扩展：** 不宣称易失进度可完整补齐。


<a id="d706"></a>
#### D7.06｜执行AI确定性场景集与真实适配验收

**执行分级：** Standard。

**合并安排：** 确定性AI流程场景＋观察/断线场景＋可访问真实适配集中验收；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 无权限/冲突/混合效果拒绝；真实模型按访问事实报告，不用确定性结果冒充。

**前置依赖：** [D7.04](#d704)、[D7.05](#d705)、[D6.08](#d608)  
**架构依据／测试族：** [A18](01_Architecture_v3.3.md#a18)、[A22](01_Architecture_v3.3.md#a22)、[A17](01_Architecture_v3.3.md#a17)、[A19](01_Architecture_v3.3.md#a19) ／ T08、T10、T11、T16、T19、T20、T21、T23、T24

**实施内容：** 覆盖发现→查询→check/preview→submit→wait；批量/原子/分阶段选择、冲突修复、无权限、混合效果拒绝、需要新决策时返回。 固定AI场景增加运行执行枚举、订阅进展、收到gap后查终态、断线重订阅；程序客户端合并进度，只向模型返回必要摘要。

**交付产物：** evidence/G7/；ai task fixtures；工具往返/token/成功与拒绝统计。

**通过条件：** 确定性客户端全部必测；有可用模型时另跑端到端并记录模型/配置；缺访问的真实模型测试标Blocked不能计pass；无安全绕过。 模型未访问项不计通过；观察提示不赋予权限或自动触发设备动作；工具事实与评审状态分离。

**本包禁止扩展：** 不以单次AI演示签发生产可靠性。


**G7 集成放行：** 确定性端到端场景必过；支持的模型/MCP适配分别验证，未实测项不计通过；预览批准不可越权。


### D8｜容量、验证与发布


<a id="d801"></a>
#### D8.01｜SDK公开表面、冻结消费者与安装兼容门禁

**执行分级：** Standard。

**合并安排：** 导出/独立安装＋公开声明检查＋冻结消费者兼容集中验证；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 干净改根/缺组件/CRT与依赖、旧消费者不随实现改、首发不假报历史兼容。

**前置依赖：** [D6.08](#d608)、[D7.05](#d705)、[D0.06](#d006)  
**架构依据／测试族：** [A02](01_Architecture_v3.3.md#a02)、[A20](01_Architecture_v3.3.md#a20)、[A23](01_Architecture_v3.3.md#a23)、[A19](01_Architecture_v3.3.md#a19)、[A22](01_Architecture_v3.3.md#a22) ／ T01、T02、T05、T24

**实施内容：** 导出全部目标真实依赖，安装树改根，单独工程find_package；固定C++/CRT/expected后端；离线只取所选依赖。 按A19执行头/target清单、关键声明和行为审查、上一正式版本固定消费者对新SDK编译运行三层门禁；工具链/CRT/后端一致性不等同任意ABI承诺。

**交付产物：** cmake/package/；tests/install_consumer/；release layout。 sdk_api_manifest；api_surface差异报告；tests/install_consumer/frozen/版本化样例与结果；SDK SemVer兼容/弃用清单。

**通过条件：** 无源树绝对路径；未选组件不取依赖；static private依赖仍正确最终链接；版本/后端不一致明确拒绝。 同名头签名破坏可被拦住；不与SDK同时修改旧消费者来造兼容；首发无上一正式版本明示并建基线，不假报历史兼容通过；意外新增/删除头不能自动覆盖golden。

**本包禁止扩展：** 不承诺跨任意编译器二进制ABI。


<a id="d802"></a>
#### D8.02｜执行Profile矩阵与缺组件负例

**执行分级：** Standard。

**合并安排：** 四Profile及State内存组合＋三个消费者＋缺组件负例集中验证；可与D8.03条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 每组合独立发现/结果与依赖闭包；NativeSubset不冒充Embedded。

**前置依赖：** [D8.01](#d801)、[D7.06](#d706)  
**架构依据／测试族：** [A02](01_Architecture_v3.3.md#a02)、[A20](01_Architecture_v3.3.md#a20)、[A22](01_Architecture_v3.3.md#a22)、[A19](01_Architecture_v3.3.md#a19)、[A21](01_Architecture_v3.3.md#a21) ／ T01、T02、T03、T08、T23、T24

**实施内容：** Embedded/AutomationHost/DurableHost/WorkspaceHost及State内存组合，在干净目录逐项构建；缺provider/codec/权限策略必须拒绝。 验证测试Conformance/证据工具不进入默认产品依赖；正式Profile与NativeSubset测量标签分离，观察方法只随实际Control装配出现。

**交付产物：** evidence/profiles/；component manifests；负向编译/装配记录。

**通过条件：** 三个真实消费者共享库；无伪Document/Null-success；Ready目录与实际装配吻合；没有另一个执行器或状态真相。 Embedded不拉JSON/SQLite/Asio；增加观察面不迫使Native Invoke创建执行记录；API manifest与每Profile实际导出对应。

**本包禁止扩展：** 不靠链接器死代码消除证明裁剪。


<a id="d803"></a>
#### D8.03｜模型、属性、fuzz与内存/并发检查

**执行分级：** Critical。

**合并安排：** 模型/属性/fuzz＋同版各端口合同＋内存/真实并发专项集中运行；可与D8.02条件协同（见E02），逻辑完成条件分别判定，物理测试/review优先共享。

**专项重点：** 保留种子、探针有效、重入/迟到/观察故障；ASan不替代race验证。

**前置依赖：** [D7.06](#d706)、[D6.08](#d608)  
**架构依据／测试族：** [A03](01_Architecture_v3.3.md#a03)、[A06](01_Architecture_v3.3.md#a06)、[A08](01_Architecture_v3.3.md#a08)、[A09](01_Architecture_v3.3.md#a09)、[A22](01_Architecture_v3.3.md#a22) ／ T04、T05、T07、T10、T11、T12、T13、T19、T20、T24

**实施内容：** 执行参考模型对照、随机属性、parser/Plan fuzz；在正式支持工具链跑可用sanitizer和真实多线程；重入/迟到回调专项。 全部受支持端口按同一合同版本复跑共同＋能力专项，再跑真实线程/进程压力；新增通知序列、分页预算和采集器故障反例。

**交付产物：** evidence/safety/；repro seeds；sanitizer/threads matrix。

**通过条件：** 保留失败种子与复现；检测器有效性有真实probe；不支持项注明；ASan不能替代race证明；内部锁不调用用户代码。 NotApplicable/Skipped与Passed不混合；测试桩违规不能被登记为合格后端；适用矩阵与Profile最低要求一致。

**本包禁止扩展：** 不通过减少测试或修改预期掩盖缺陷。


<a id="d804"></a>
#### D8.04｜全故障窗口与恢复/存储组合复验

**执行分级：** Critical。

**合并安排：** 最终crash窗口＋存储故障＋备份/GC/撤权/观察交错集中复验；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 同一最终来源、每必需窗口有证据、未知不重发、失败不覆盖。

**前置依赖：** [D8.02](#d802)、[D8.03](#d803)  
**架构依据／测试族：** [A11](01_Architecture_v3.3.md#a11)、[A12](01_Architecture_v3.3.md#a12)、[A13](01_Architecture_v3.3.md#a13)、[A14](01_Architecture_v3.3.md#a14)、[A22](01_Architecture_v3.3.md#a22)、[A17](01_Architecture_v3.3.md#a17) ／ T06、T14、T15、T16、T17、T18、T19、T20、T22、T24

**实施内容：** 最终提交上重跑独立进程kill、SQL异常/满盘/Busy、Outbox/GC/备份、水位/撤权/停止交错；核对原始持久材料和执行次数。 加入恢复/订阅旧世代、撤权在途、列表回收与回执发布交错；复核Conformance与真实crash来自同一源码/构建矩阵。

**交付产物：** evidence/crash-final/；故障点manifest；recover多轮轨迹。

**通过条件：** 全部必需窗口覆盖；无未知动作重发；旧备份对账阻断；没有只用旧版本日志代替；支持范围不夸大到断电/功能安全。 有损通知不影响Durable结果衔接，旧cursor不越权，原始故障证据不能被后续成功覆盖。

**本包禁止扩展：** 不碰用户生产数据库或真实设备。


<a id="d805"></a>
#### D8.05｜固定占用、零分配范围与性能容量正式定案

**执行分级：** Standard。

**合并安排：** 各调用路径成本＋占用/线程/Ready＋容量样本集中测量；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 固定预算和正负探针、真实尾延迟样本、插桩与Release分开，不加余量抬线。

**前置依赖：** [D8.04](#d804)、[D7.06](#d706)  
**架构依据／测试族：** [A21](01_Architecture_v3.3.md#a21)、[A22](01_Architecture_v3.3.md#a22)、[A02](01_Architecture_v3.3.md#a02)、[A17](01_Architecture_v3.3.md#a17)、[A19](01_Architecture_v3.3.md#a19) ／ T01、T03、T09、T13、T19、T20、T22、T23、T24

**实施内容：** 测Native/Dynamic/RPC/Plan/Atomic/批量与同保障存储；状态规模/改动量/并发/慢流/GC矩阵；原始请求级分位与分配/锁剖析。 按A21.4–A21.6在最终Release重新测链接增量、私有/WorkingSet/峰值、Ready时延、线程/退出与限定分配；对照G1/G3已审批预算。观察流开/关与慢消费者单列Control Profile，严格保留治理和耐久。

**交付产物：** evidence/performance/；support-capacity.json；正式预算配置。 正式footprint-budgets.json；原生/Embedded/完整观测原始报告；计数器覆盖与正负探针；支持平台/配置/限制说明。

**通过条件：** 最终二进制/依赖/硬件绑定；样本足够；不降FULL、不跳权限、不用Accepted冒Completed；不达预算先归因并明确限制。 所有必需上限有限且独立审批，不能以本轮实测自动抬线；NativeSubset不代替Embedded；零分配只在声明范围断言且计入触发的后台分配；时延与插桩构建分开。

**本包禁止扩展：** 不挑一次最佳结果宣称最优或零开销。


<a id="d806"></a>
#### D8.06｜资料、Schema、例子、错误手册最终一致性

**执行分级：** Fast。

**合并安排：** 最终定义生成目录/Schema/帮助/示例＋错误/版本/预算资料集中核对；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 示例真实运行、wire/IR与能力一致、旧消费者不覆盖；行为变更升级并补相关回归。

**前置依赖：** [D8.02](#d802)、[D8.05](#d805)  
**架构依据／测试族：** [A00](01_Architecture_v3.3.md#a00)、[A05](01_Architecture_v3.3.md#a05)、[A08](01_Architecture_v3.3.md#a08)、[A17](01_Architecture_v3.3.md#a17)、[A18](01_Architecture_v3.3.md#a18)、[A19](01_Architecture_v3.3.md#a19)、[A15](01_Architecture_v3.3.md#a15)、[A21](01_Architecture_v3.3.md#a21)、[A22](01_Architecture_v3.3.md#a22) ／ T06、T11、T19、T20、T21、T23、T24

**实施内容：** 从最终注册定义生成目录/Schema/帮助/例子；核对wire与IR、Outcome、退出码、支持能力、文档引用与公开头。 对照唯一方法表及Schema生成subscribe/list/watch文档、观察例子、Conformance适用矩阵、SDK版本/弃用表与预算；保留上一个正式冻结消费者不随示例生成覆盖。

**交付产物：** docs/sdk/；docs/automation/；schemas/；examples/；一致性报告。 `packages/control/observation/` 若存在，只作为 `OCK::Control` 内部源码路径记录，不进入SDK target清单。

**通过条件：** 无return wire漂移；每个公开例子可执行；已装能力与文档吻合；所有未支持功能明示；两份总规范不冲突。 没有return wire漂移、进度replay虚承诺、所有cursor一致的误述、全局零分配和未实现已通过说法；实际公开头和导出targets与资料一致。

**本包禁止扩展：** 不靠长文档替代可运行样例。


<a id="d807"></a>
#### D8.07｜发布候选、证据索引和最终放行

**执行分级：** Critical。

**合并安排：** 版本/许可/支持矩阵＋证据索引＋全部门禁判定集中收口；纳入E02对应Development Batch连续实现；逻辑完成条件独立，物理测试/review优先批次复用。

**专项重点：** 最终来源完整、零缺项/混版本/假Passed；发布动作仍按授权，不能仅看包数量。

**前置依赖：** [D8.04](#d804)、[D8.05](#d805)、[D8.06](#d806)  
**架构依据／测试族：** [A20](01_Architecture_v3.3.md#a20)、[A22](01_Architecture_v3.3.md#a22)、[A23](01_Architecture_v3.3.md#a23)、[A19](01_Architecture_v3.3.md#a19) ／ T01、T06、T14、T16、T21、T23、T24

**实施内容：** 生成版本化SDK、依赖许可、支持矩阵、原始证据索引和发布检查清单；将Blocked/Fail未满足的承诺留为明确限制或阻止发布。 使用E03工具聚合当前最终源码的各Profile/工具链/Conformance、API兼容、占用和性能证据，检查必需manifest与人工评审，再形成release签核。

**交付产物：** release manifest；SBOM/许可证；gate-summary；可安装SDK包。 机器生成gate-summary/evidence index；完整原始记录及source/build摘要；SDK/协议/文档独立版本声明。

**通过条件：** G0–G7及D8必需验证逐项通过、无隐含skip；当前实现满足声明范围；发布签核只覆盖实测能力；未批准不自动推送发布。 自动结果不能手填；0测试/漏清单/错二进制/混源码/未测/未评审不放行；历史失败保留；只有声明范围的当前证据支持发布。

**本包禁止扩展：** 不以文档完成或任务勾完作为生产成熟证明。


**G8 集成放行：** G0–G7及D8必需验证完成；SDK表面/冻结消费者、Conformance矩阵、正式Embedded占用和自动证据完整性通过；安装包/矩阵/证据绑定同一最终版本。


## E05｜需求—测试族—工作包追踪表

### E05.1 24个主测试族

此表由本版工作包卡片的测试族映射汇总，证明每项要求有落实位置，不证明测试已经通过。用例必须细分进入受审查expected manifest；Conformance各后端结果不重复计算为不同需求通过。

| 需求/测试族 | 首要责任及交叉验证工作包 |
|---|---|
| R01 / T01 | [D0.01](#d001)、[D0.02](#d002)、[D0.06](#d006)、[D1.02](#d102)、[D1.06](#d106)、[D3.07](#d307)、[D4.01](#d401)、[D4.08](#d408)、[D6.03](#d603)、[D6.08](#d608)、[D8.01](#d801)、[D8.02](#d802)、[D8.05](#d805)、[D8.07](#d807) |
| R02 / T02 | [D0.03](#d003)、[D1.02](#d102)、[D1.03](#d103)、[D1.05](#d105)、[D2.02](#d202)、[D2.06](#d206)、[D2.07](#d207)、[D3.04](#d304)、[D4.04](#d404)、[D7.01](#d701)、[D8.01](#d801)、[D8.02](#d802) |
| R03 / T03 | [D1.01](#d101)、[D1.05](#d105)、[D1.06](#d106)、[D2.07](#d207)、[D3.07](#d307)、[D8.02](#d802)、[D8.05](#d805) |
| R04 / T04 | [D0.04](#d004)、[D1.01](#d101)、[D2.01](#d201)、[D2.02](#d202)、[D2.04](#d204)、[D2.07](#d207)、[D4.05](#d405)、[D5.02](#d502)、[D6.01](#d601)、[D7.04](#d704)、[D7.05](#d705)、[D8.03](#d803) |
| R05 / T05 | [D0.02](#d002)、[D1.01](#d101)、[D1.02](#d102)、[D1.03](#d103)、[D2.03](#d203)、[D4.05](#d405)、[D5.02](#d502)、[D8.01](#d801)、[D8.03](#d803) |
| R06 / T06 | [D0.03](#d003)、[D1.02](#d102)、[D1.05](#d105)、[D2.04](#d204)、[D2.07](#d207)、[D3.04](#d304)、[D3.06](#d306)、[D4.03](#d403)、[D4.06](#d406)、[D5.04](#d504)、[D5.05](#d505)、[D5.06](#d506)、[D5.09](#d509)、[D6.07](#d607)、[D8.04](#d804)、[D8.06](#d806)、[D8.07](#d807) |
| R07 / T07 | [D0.05](#d005)、[D1.04](#d104)、[D3.03](#d303)、[D3.05](#d305)、[D4.03](#d403)、[D4.04](#d404)、[D5.03](#d503)、[D5.05](#d505)、[D5.06](#d506)、[D6.03](#d603)、[D7.02](#d702)、[D7.03](#d703)、[D8.03](#d803) |
| R08 / T08 | [D4.01](#d401)、[D4.02](#d402)、[D4.04](#d404)、[D4.06](#d406)、[D4.08](#d408)、[D7.02](#d702)、[D7.06](#d706)、[D8.02](#d802) |
| R09 / T09 | [D4.01](#d401)、[D4.02](#d402)、[D4.03](#d403)、[D5.05](#d505)、[D6.01](#d601)、[D6.02](#d602)、[D6.03](#d603)、[D6.04](#d604)、[D6.08](#d608)、[D8.05](#d805) |
| R10 / T10 | [D0.04](#d004)、[D4.04](#d404)、[D4.06](#d406)、[D4.07](#d407)、[D4.08](#d408)、[D6.07](#d607)、[D7.06](#d706)、[D8.03](#d803) |
| R11 / T11 | [D0.04](#d004)、[D4.05](#d405)、[D4.06](#d406)、[D4.07](#d407)、[D4.08](#d408)、[D6.05](#d605)、[D7.01](#d701)、[D7.06](#d706)、[D8.03](#d803)、[D8.06](#d806) |
| R12 / T12 | [D0.03](#d003)、[D2.01](#d201)、[D3.01](#d301)、[D3.04](#d304)、[D3.05](#d305)、[D3.06](#d306)、[D3.07](#d307)、[D4.06](#d406)、[D4.07](#d407)、[D5.04](#d504)、[D6.07](#d607)、[D6.08](#d608)、[D8.03](#d803) |
| R13 / T13 | [D3.02](#d302)、[D3.03](#d303)、[D3.05](#d305)、[D3.07](#d307)、[D4.07](#d407)、[D8.03](#d803)、[D8.05](#d805) |
| R14 / T14 | [D0.05](#d005)、[D5.01](#d501)、[D5.02](#d502)、[D5.04](#d504)、[D5.05](#d505)、[D5.07](#d507)、[D5.09](#d509)、[D6.04](#d604)、[D6.05](#d605)、[D8.04](#d804)、[D8.07](#d807) |
| R15 / T15 | [D0.05](#d005)、[D5.02](#d502)、[D5.03](#d503)、[D5.07](#d507)、[D5.08](#d508)、[D5.09](#d509)、[D6.02](#d602)、[D6.06](#d606)、[D8.04](#d804) |
| R16 / T16 | [D0.05](#d005)、[D5.01](#d501)、[D5.04](#d504)、[D5.05](#d505)、[D5.06](#d506)、[D5.08](#d508)、[D5.09](#d509)、[D6.06](#d606)、[D6.07](#d607)、[D7.03](#d703)、[D7.06](#d706)、[D8.04](#d804)、[D8.07](#d807) |
| R17 / T17 | [D6.05](#d605)、[D6.06](#d606)、[D6.07](#d607)、[D6.08](#d608)、[D8.04](#d804) |
| R18 / T18 | [D0.05](#d005)、[D4.02](#d402)、[D5.08](#d508)、[D5.09](#d509)、[D6.01](#d601)、[D6.02](#d602)、[D6.04](#d604)、[D6.08](#d608)、[D7.02](#d702)、[D7.05](#d705)、[D8.04](#d804) |
| R19 / T19 | [D0.01](#d001)、[D0.03](#d003)、[D0.04](#d004)、[D0.06](#d006)、[D1.02](#d102)、[D1.04](#d104)、[D1.06](#d106)、[D2.04](#d204)、[D2.05](#d205)、[D2.07](#d207)、[D3.04](#d304)、[D3.06](#d306)、[D3.07](#d307)、[D4.08](#d408)、[D5.04](#d504)、[D5.07](#d507)、[D5.08](#d508)、[D5.09](#d509)、[D7.01](#d701)、[D7.04](#d704)、[D7.05](#d705)、[D7.06](#d706)、[D8.03](#d803)、[D8.04](#d804)、[D8.05](#d805)、[D8.06](#d806) |
| R20 / T20 | [D0.01](#d001)、[D0.02](#d002)、[D0.03](#d003)、[D0.04](#d004)、[D1.04](#d104)、[D2.03](#d203)、[D2.04](#d204)、[D2.05](#d205)、[D2.06](#d206)、[D2.07](#d207)、[D3.04](#d304)、[D3.07](#d307)、[D4.08](#d408)、[D5.03](#d503)、[D5.04](#d504)、[D5.07](#d507)、[D5.08](#d508)、[D5.09](#d509)、[D7.01](#d701)、[D7.03](#d703)、[D7.04](#d704)、[D7.05](#d705)、[D7.06](#d706)、[D8.03](#d803)、[D8.04](#d804)、[D8.05](#d805)、[D8.06](#d806) |
| R21 / T21 | [D2.03](#d203)、[D7.01](#d701)、[D7.02](#d702)、[D7.03](#d703)、[D7.04](#d704)、[D7.06](#d706)、[D8.06](#d806)、[D8.07](#d807) |
| R22 / T22 | [D0.04](#d004)、[D1.06](#d106)、[D2.04](#d204)、[D2.05](#d205)、[D3.01](#d301)、[D3.06](#d306)、[D3.07](#d307)、[D5.01](#d501)、[D5.08](#d508)、[D6.03](#d603)、[D6.08](#d608)、[D7.05](#d705)、[D8.04](#d804)、[D8.05](#d805) |
| R23 / T23 | [D0.01](#d001)、[D0.06](#d006)、[D1.05](#d105)、[D1.06](#d106)、[D2.02](#d202)、[D2.07](#d207)、[D3.02](#d302)、[D3.04](#d304)、[D3.07](#d307)、[D4.01](#d401)、[D4.08](#d408)、[D5.09](#d509)、[D7.05](#d705)、[D7.06](#d706)、[D8.02](#d802)、[D8.05](#d805)、[D8.06](#d806)、[D8.07](#d807) |
| R24 / T24 | [D0.01](#d001)、[D0.02](#d002)、[D0.06](#d006)、[D1.02](#d102)、[D1.03](#d103)、[D1.06](#d106)、[D2.06](#d206)、[D3.01](#d301)、[D3.07](#d307)、[D4.08](#d408)、[D5.01](#d501)、[D5.09](#d509)、[D6.01](#d601)、[D6.04](#d604)、[D6.08](#d608)、[D7.01](#d701)、[D7.06](#d706)、[D8.01](#d801)、[D8.02](#d802)、[D8.03](#d803)、[D8.04](#d804)、[D8.05](#d805)、[D8.06](#d806)、[D8.07](#d807) |

### E05.2 N1–N11增量责任闭环

| 更新 | 合同/基础 | 首个真实验证 | 完整集成与发布 |
|---|---|---|---|
| N1观察和枚举 | D0.03/D0.04-b、D1.04、D2.04/D2.05 | D3.04投影/索引、D3.07真实CLI订阅/list/watch | D4.08 Plan、D5恢复、D7.05压力、D7.06 AI、D8.04/D8.06 |
| N2固定占用 | D0.06测量环境、D1.05计数探针 | D1.06 NativeSubset、D3.07完整Embedded；分别审批有限预算 | D8.02组合、D8.05正式实测/预算、D8.07发布 |
| N3端口一致性 | D0.06工厂/manifest、D1.02窄合同 | D1.06 Logging、D3.01 Executor、D5.01 Storage、D6.01 Asset | D8.03共同/专项矩阵、D8.04真实故障、D8.07证据 |
| N4 SDK稳定 | D0.02政策/候选表面、D1安装消费者 | D7.01 SDK表面、D8.01三层兼容门禁 | D8.06资料/弃用、D8.07独立版本发布 |
| N5证据自动化 | D0.06采集器与失败反例 | 所有包从实际运行生成证据，G0前完成bootstrap核对 | D8.03工具回归、D8.07最终来源/配置/评审核对 |
| N6无状态cursor | D0.04-b合同/MAC输入/长度预算 | D2.04 token codec；D3.07真实篡改/跨host/TTL/压力 | D7.05慢客户端；D8.05容量；D8.06资料一致 |
| N7 UI统一入口 | D0.02 ADR/架构负例 | D1/D4 Operation/State 调用链；D6 Workspace UI集成合同 | D8.02组合；D8.06资料/样例 |
| N8 state/DSL范围ADR | D0.02 ADR、Catalog规则 | D2/D7只从 Read Operation 投影状态；DSL缺失不阻门禁 | D8.06工具面和产品文档 |
| N9 IR语义往返 | D0.04 Plan合同边界 | D4.05 test codec/semantic equality | D6.05 checkpoint交叉；D8.03 fuzz/属性 |
| N10 canonical前置 | A12.4合同、D5.02-a spike | D5.02 A/B/C结论与正式encoder | D8.04故障/字节向量；D8.06 ADR/格式资料 |
| N11门禁/文档修正 | D0.06 a/b/c；A17/T06/A02目录规则 | D3.07 G3-A/B/C；各父包通过条件 | D8.06/D8.07最终一致性 |

表中子项沿用父包通过条件，没有另增第65个工作包。协议、性能、测试和SDK变更须同步改实现卡片及本映射，不允许只在表格打勾。

## E06｜必须先写反例的高风险边界

| 边界 | 先建立的反例 | 不允许的捷径 |
|---|---|---|
| Operation统一 | StateEdit获得Effect权限；Native跳过参数范围 | 万能Context、两份业务Handler |
| 接受和终态 | executor返回失败却仍调用work；inline完成在记录前发生 | 把任务看成一个future不管理owner |
| 提交 | claim后DB失败；DB成功root未发布 | permit即Committed、两次独立保存 |
| 取消/撤权 | cancel与permit消费交错；旧grant继续发送 | 执行结束后看stop flag一律改Cancelled |
| Plan | Return wire漂移；票据被当R；分支未定义槽 | 为了通过加任意eval或动态字典后门 |
| Atomic | 中途步骤失败；新资源锁未知；组内Query读旧根 | 普通executeCommand循环伪装一次提交 |
| GC/Intent | 删除receipt后旧key重来；长Workflow跨epoch | 自动换nonce、无限保留全部历史 |
| 恢复 | 子效果已做父checkpoint没存；旧备份缺新动作 | 直接重跑Handler、默认选新版本 |
| 观测 | exporter异常/阻塞；Outbox早于root发布 | 业务事实改成失败、事件隐式驱动设备 |
| 生命周期 | stop超时；child仍在运行；自身worker析构 | detach后释放依赖 |
| 观察协议 | ack前事件、get覆盖新版本、撤权队列、终态提示全丢 | 用日志当结果、把有损通知接到内部完成链 |
| 执行分页 | phase变化、旧cursor、无权限owner、空页扫描不前进 | 无界历史扫描、宣称实时keyset是全局快照 |
| Embedded占用 | 未引用代码被链接器移除、测原生子集冒完整任务、计数器不生效 | 自动增加预算、隐藏后台分配、无限预分配 |
| 端口一致性 | 新后端吞掉拒绝或少一次完成、必需能力标不可用 | 每个后端改预期、把fault后端当合法实现 |
| SDK源码兼容 | 同名头改签名、PUBLIC宏改变、旧消费者被顺手修改 | 只比较文件名、自动覆盖快照、误称ABI保证 |
| 自动证据 | 删除测试、旧JUnit残留、打印成功但崩溃、缺轮次、混二进制 | 手填Passed、用本次发现生成预期、失败重跑覆盖 |
| cursor | 篡改token、跨host/caller、过期、超长、高频翻页 | 服务端无预算句柄池、失败后偷偷重开第一页 |
| UI业务入口 | UI直接改State/Document或发送设备动作 | “同进程可信”作为绕Operation治理的理由 |
| 状态工具面 | 添加万能state path/query并绕Catalog/TypeContract | 为AI方便建立第二状态API |
| IR往返 | roundtrip丢slot类型/控制边/预算/Operation digest | 把内部结构体/指针二进制固化成公共格式 |
| canonical | 普通encoder对half/float/-0/map顺序不一致 | 先上线幂等指纹，之后再定义canonical |

## E07｜可裁剪发布与功能范围声明

每个可发布节点的包内必须附“已实现/已验证/未实现”能力表。未实现功能不在运行目录声称可执行。推荐阶段名称：

| 节点 | 可交付名称 | 不能声称 |
|---|---|---|
| G2 | Native/Dynamic/CLI基础开发包 | 完整任务、事务或恢复 |
| G3 | 含真实执行订阅/枚举、明确Embedded占用的无文档异步开发包 | 原子计划/持久化完成，或进度必达/replay |
| G4 | 内存开放命令套件 | 跨重启可靠性、完整Workflow |
| G5 | 持久执行开发包 | 所有计划恢复与资产备份完成 |
| G6 | Workspace＋DurablePlan集成候选 | AI/容量/平台发布认证完成 |
| G8 | 完整支持矩阵下的发布版本 | 硬实时、设备功能安全或所有平台性能最优 |

GUI 是 Workspace/桌面产品的主要人工前端；其业务写、外部效果和生命周期仍必须经统一 Operation。CLI/JSON Plan/Client SDK 是稳定自动化面；可选DSL/REPL只是同一 Plan 的人类前端，**不属于首发必需门禁**。Core/Control 不提供通用 `state.inspect/query` 万能状态API，领域 Read Operation 已覆盖 AI/脚本事实查询。

远程网络部署、DeviceHost产品化、自动领域融合、其他存储后端为后续独立扩展。对D0定义的完整主目标不可用“以后再做”减少必需门禁；特别是原子事实、Finalizing、epoch与child回执不能降为文档承诺。

## E08｜交给开发 AI 的批次执行规则

```text
唯一规范：OCK-ARCH-3.3与OCK-EXEC-3.3（当前r2）。
当前目标按progress中的Development Batch推进，而不是把每个工作包编号当成独立Codex任务。
已Passed前置只读状态和公开合同，不重读历史review/evidence、不重跑测试；除非当前修改触及其冻结输入。
Batch内开发依赖使用Implementation-Ready：上游所需接口已实现并完成直接验证即可继续写下游。
Implementation-Ready不是正式状态；下游最终Passed仍要求正式前置按DAG已经Passed。
规范明确后立即写生产代码；Fast小项不先建plan/SPEC/CODE/acceptance/Evidence。
开发只执行S_changed：Debug最小影响集、fail-fast；按真实风险补ASan/Release。
同一最终source/profile/build环境的测试尽量一次物理执行，按task/requirement映射多个逻辑结论。
不因为切换工作包编号重新configure/build、重复完整CTest或重新读取同一规范上下文。
Batch稳定后集中确定expected/S_required，集中运行正式矩阵，集中SPEC/CODE review。
批次Review共享一次上下文；各task结论仍独立可追踪。ChangesRequested只复核实际变化及关联风险。
G Gate执行S_gate并承担阶段完整历史集成；普通包不机械累计所有历史Passed测试。
Critical只在真实未决风险、E06硬边界或预算冻结点设置必要checkpoint，不把所有小步骤变审批链。
不为缺能力返回空成功，不用假Document，不建立第二Handler/Plan运行时。
不改变Outcome、permit、publication、epoch、wire、安全或durability语义来迎合测试。
同版Conformance复用预期；NotApplicable由合同决定，不以skip逃避Profile必要能力。
使用E03记录正式真实结果；失败/缺项不覆盖，不手填exit/hash/Passed，不拼接不同源码成功配置。
非当前阻塞且非规划交付物，不扩展测试/Evidence基础设施。
完成一个Batch时报告：生产代码、直接测试、正式物理运行及其逻辑映射、各包状态、未运行/风险、下一Batch。
没有目标工具链/权限的必需测试标Blocked，不声称通过。
除用户授权外，不发布、不清理生产数据、不执行真实设备动作。
```

工作复杂时可以小提交，但“包编号变化”本身不是提交理由。遇到真正合同冲突只做最小 ADR 和受影响测试；已经明确的规则直接实现。一个 Batch 中如果前包正式验收失败，下游已有代码可以保留为未验收实现，但必须先修复前包，禁止跳过 DAG 宣布后包 Passed。

## E09｜停止条件与最终放行

出现以下情况停止扩大范围并修复：两个组件各自决定同一个commit的结果；不同入口验证不一致；需要关闭权限/耐久保证才能达到性能；Finalizing无人持有；旧请求可能被作为新操作；无文档消费者需要伪Document；多个模型会话各自修改公共DTO；订阅失去授权仍发送；观察堵塞导致内部完成丢失；不同后端各自降低合同预期；以当前发现覆盖expected；SDK破坏未审查；预算随实测自动抬升；execution.list重新引入无预算stateful cursor；UI出现业务写旁路；新增通用state RPC形成第二状态面；canonical规则未验证就用于Intent/PlanDigest。

G8放行要回答：三个消费者能否独立安装运行；是否只有一个Runtime和Plan引擎；相同输入的Native/Dynamic是否等价；每个关键崩溃窗口是否有可信结果；所有必要子执行是否有人持有；性能/容量是否在实际保障条件下测量；当前资料是否与二进制吻合；N1–N11是否分别有真实协议/固定占用/端口合同/SDK消费者/自动证据、无状态cursor、UI单入口、接口范围ADR、IR roundtrip和canonical前置验证的完整闭环。

不得用“64个工作包已经标记完成”代替这些回答。状态/效应未知必须保留未知，性能失败必须保留失败；所有可声明的成熟度都限定在实际测得的平台、版本和功能集合。

## E10｜本计划来源与本次交付边界

直接基底为 v3.2 两份完整规范 [B32A]/[B32E] 与本轮收口审查 [B33]；v3.2 已整合更早 [B3]/[B2]/[BR] 规则。原64包编号、阶段和主目标保留；本轮只把N6–N11及相应修正合入实施内容、交付、通过条件和追踪映射，不声称v3.2原文已有这些细节。未进行整套依赖重选或代码审阅。

本次交付是两份3.3完整规范文档。C++源码、正式JSON Schema、数据库DDL、编译器、运行时、Conformance套件、证据采集器及footprint工具均由对应任务建设；本文提到的工具路径不是本次已实现的程序。文档内部链接/依赖/映射和JSON语法检查只证明文档检查范围，不是SDK、并发、恢复、轻量化或性能通过，也不沿用旧设计包13项静态校验的成绩。

**B5/G3 post-gate 收口（2026-09-10）：** [B5 规划](plans/B5.md)落实 C1/C1b/C2，按 [接受与业务进入 ADR](adr/ADR-b5-accepted-outcome.md)同步 Outcome/生产 wire；历史 G3 Passed 保留，收口直接影响三配置与新 Runtime 的原预算 Embedded 刷新通过后才放行 B6 生产集成，不重开无关历史矩阵。

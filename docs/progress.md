# 内核实施进度

更新：2026-09-10。唯一规范为 [架构 v3.3-r2](01_Architecture_v3.3.md) 与 [执行计划 v3.3-r2](02_Execution_Plan_v3.3.md)。恢复任务默认只读本文件顶部、当前 Development Batch 的相关规范/合同和 diff；历史 review/evidence 按需读取。

## 当前生产节点

- 当前分支：`work/d0-kernel-baseline`；G2 历史被测实现 `e77c328`，G2 后 C1/C2 收口被测实现 `44246b8`；B4 被测实现 `0d71b4f`，B5 前置收口被测实现 `7a22d0e`；归档与推送状态以实时 Git 为准。
- 已 Passed：G0–G2、D0.01–D0.06、D1.01–D1.06、D2.01–D2.07、D3.01–D3.03。**这些 Passed 前置不重验**；本轮没有重跑 G1/B2 累计矩阵。
- 当前 Development Batch：**B5 开发 InProgress**，按已审阅的 [B5 规划](plans/B5.md)持续推进至 G3。已实现 Scheduler 逐票据 retire/start 仲裁、正常失效投递诊断修复及依赖就绪锁外通知；Runtime 内部 ResourceWaitBinding 已具备有限控制步、双世代 wake、接受/依赖门控、取消退役和运行期 Lease 独占转移。Debug 依赖影响集 6/6、资源影响集 6/6、绑定屏障专项 3/3；公开 Submit/观察已接线，取消控制、父子寿命及 G3 继续开发，尚未包级验收。B5 前置收口 Passed 保留，见 [前置交付](validation/B5-preclosure-delivery.md)；B4 原同来源 65/63/65 事实保持，见 [B4 交付](validation/B4-delivery.md)。
- 当前门禁：**G2 Passed**；G3–G8 未开始。正式同来源 Debug 28/28、Release 26/26、ASan 26/26，三包和 G2 顺序自动验收均无错误，见 [B3 交付](validation/B3-delivery.md)。
- B5 本次资源绑定直接影响集另经 ASan / RelWithDebInfo 9/9；包含早到/重复/迟到 wake、acquire 在途取消、运行期租约保持、析构重入及正常失效/Executor 违约区分。这是开发验证，尚未形成 B5 同来源正式矩阵或包级 Passed。
- B5 受管理调用基础已接入同一 Native typed dispatch：注册目录保留可信资源声明，绑定持有 VerifiedCaller，Submit 准备与开始共用参数/目标/预算/当前授权校验；worker 入口借用运行期 Lease，短 Invoke 原线程和资源限制保持。Debug 影响集 12/12、ASan / RelWithDebInfo 生命周期影响集 6/6。当前仅支持同步 Read 的内部接线，外部异步完成仍明确拒绝；Execution 表、拥有输入/结果、父子寿命与 Host/CLI Submit 继续开发，未发布 Submit capability。
- B5 新增 InvocationRecord、内部 ExecutionTable 与 ManagedExecution：拥有输入、接受前预留调用槽/固定结果槽/取消状态，按可信字节测量预留表项额度；接受发布后才允许资源准入和 typed 调用。已接通 Scheduler 排队/退役、资源等待/重试、运行期租约与同一身份终态摘要；结果 alias 保活表项，普通缓存淘汰不移除被 pin 的记录，索引摘除不提前归还拥有者额度。直接开发验证 Debug 5/5、ASan / RelWithDebInfo 5/5；首次测试因空资源配置被正确拒绝，原失败保留，修正 fixture 后重跑通过。身份分配服务、owner 观察索引、当前授权 get/wait/cancel、共享有限控制循环、完整分项预算和结构化父子寿命继续开发；尚未发布 Host/CLI Submit capability，不构成包级 Passed。
- B5 真实观察源已接入现有 Policy：表绑定 Host 世代，owner/ordinal 分别索引非终态与终态，完成时复用预留节点；有界分页在未接受条目处仍前进，续页保持首次 upper。类型化结果 pin、有限等待者及当前授权查询已实现；等待超时/取消不取消执行，禁止 worker/control/domain/database 阻塞等待，返回前重验权限并限制等待不超过授权期限。新增接线影响集 Debug 4/4、ASan / RelWithDebInfo 4/4，覆盖真实 source 的 get/list 授权、撤权后拒绝结果/等待摘要、等待配额和结果类型检查。身份分配、共享控制循环、取消服务入口、Host/CLI、父子寿命和 G3 验收继续开发，未形成包级 Passed。
- B5 已实现内部共享 ExecutionService：系统随机源生成完整 128 位身份，接受前预留有限控制槽，单控制线程以有限批次消费可靠唤醒并推进 Scheduler/资源/回收；当前策略授权后可提交取消意图。真实 CpuPool 固定 2 workers 的开发验证覆盖独立调用槽下的服务满额拒绝、运行期协作取消、关闭超时保活及后续排空。Debug 直接影响集 5/5、ASan / RelWithDebInfo 4/4；首次编译遇到 MSVC C1128，已将服务测试拆为独立源文件解决，失败原文保留。公开 Submit/Control/CLI、取消结果分类、父子寿命和完整 G3 占用/验收仍在开发；当前验证不代替 G3 线程峰值或正式 Profile 验收。
- B5 Host 寿命已接入真实内部执行服务：可信可选工厂在 start 时用同一 Host 世代装配观察源，供 Host 会话的现有 Policy 查询；默认 NativeSubset 不启用线程。关闭先拒绝执行准入、提交取消意图，执行和物理执行器分别排空后才停止模块；超时报告两类待清理责任并保活，worker 内关闭拒绝，错误源世代的启动失败也履行排空。Debug 影响集 **11/11**（[原始输出](../evidence/B5/resume-b5-host-debug-final-direct-5e73dbdec4/stdout.log)）、ASan / RelWithDebInfo **2/2**（[原始输出](../evidence/B5/resume-b5-host-relwithdebinfo-final-direct-39c19520ca/stdout.log)），公开头/实际依赖静态检查无错误。原构建未启用 Host 测试目标、旧版 CMake 配置失败的原文均保留；已用锁定 VS CMake 修正。此步是宿主寿命装配，验证中的 Submit 仍调用内部 NativeBound 服务入口；公开 HostBound Submit、会话 wait/result、Control/CLI 和父子执行继续开发，未发布完整 async/observation capability，未形成 B5/G3 正式验收。
- B5 公开拥有型调用已接到同一执行记录/服务：Registrar 保存可信 `SubmissionStorage<A,R>`，`HostBound::submit` 不接受客户端额度或资源解析器；`HostSession::wait/result<R>` 使用当前 Policy、有限等待与真实结果类型核对，返回结果 owner 及发送授权。实际 Host 注册/绑定/提交验证覆盖 worker 阻塞等待拒绝、等待超时/取消不取消执行、关闭后新查询拒绝及已有结果保活。最终 Debug **7/7**（[原始输出](../evidence/B5/resume-b5-public-final-debug-3ddb370738/test-stdout.log)）；ASan / RelWithDebInfo **7/7**（[原始输出](../evidence/B5/resume-b5-public-final-relwithdebinfo-bb8caac401/test-stdout.log)）。新增私有记录创建编译负例通过（[原始输出](../evidence/B5/resume-b5-public-submit-private-direct-d0f2742209/stdout.log)），公开头/实际依赖静态检查无错误。早期存储构建虽然编译退出为 0，但 MSBuild 后代未退出而被本次 Job 回收，原文保留，未记作完整构建通过；后续禁用节点复用后的构建均正常退出。具体工厂仍是内部装配；公开取消控制分类、资源组合、CLI、父子执行、异步完成及完整预算/G3 验收继续开发，不构成 B5/G3 Passed。
- B5 公开取消已接到同一执行服务：`HostSession::cancel` 经当前 CancelExecution 授权，返回 Requested / AlreadyClaimed / AlreadyTerminal；分类来自 Scheduler 的 start/retire 仲裁。原始 stop token 在接受后由 owner 注册，排队及等待资源时可直接退役，运行期取消不提前归还 Lease。直接影响集 Debug **7/7**（[原始输出](../evidence/B5/resume-b5-cancel-final-debug-348b5746b8/test-stdout.log)）、ASan / RelWithDebInfo **7/7**（[原始输出](../evidence/B5/resume-b5-cancel-final-asan-5772f2c3f2/test-stdout.log)），公开表面/实际依赖静态检查无错误。首次构建资源测试目标名错误的原文保留，修正目标后构建与验证通过。具体工厂公开装配、资源组合、CLI、父子执行、异步完成及完整预算/G3 验收继续开发；本次不构成包级 Passed。
- B5 公开执行装配和资源组合已实现：可信 Host 工厂可通过安装头的 `make_executions` 创建后端，只依赖 CoreContracts executor；执行预算和 ResourceRef→claims 独立冻结，无资源配置不创建占位槽。显式后端允许非 inline 的同步 Read/Compute，短 Invoke 的限制保持。实际两个调用争用同一资源验证串行准入、等待取消及关闭保活；未知键、重复映射创建失败并排空临时池。最终 Debug **16/16**（[原始输出](../evidence/B5/resume-b5-composition-final-debug-006898c7d4/test-stdout.log)）、ASan / RelWithDebInfo **8/8**（[原始输出](../evidence/B5/resume-b5-composition-final-asan-5872ecb22a/test-stdout.log)），公开头/实际依赖静态检查无错误。首轮 fixture 违反 PureCompute 的 async-dispatch 注册合同被拒绝，失败原文保留，修正为非 inline 同步计算声明后验证通过；未放宽原子注册合同。独立安装/Profile、Control/CLI、结构化父子、外部异步完成及完整预算/G3 继续开发，尚未正式验收。
- B5 接受边界补充确定性直接验证：在 Scheduler enqueue 的同步 wake 中关闭调度器，确认早到完成在发布前不可见，发布成功仍返回同一 Accepted 及失败终态；另覆盖关闭表准入导致发布失败，返回拒绝且释放记录额度。既有表内积存机制满足这两种先后，本次未增加生产仲裁机制。Debug **1/1**（[原始输出](../evidence/B5/resume-b5-early-completion-debug-c5c6be8eac/test-stdout.log)）、ASan / RelWithDebInfo **1/1**（[原始输出](../evidence/B5/resume-b5-early-completion-asan-588b22d9fe/test-stdout.log)）。父子寿命、异步完成及 B5/G3 完整条件继续开发，本项不构成正式验收。
- B5 结构化 child 已接入公开 Host 和同一执行服务：WorkContext 携带真实 scope，submit_child 校验同服务/会话/主体及当前父授权，固定独立身份/parent/期限并预留有限槽。父返回释放资源和 worker 配额，未结束 child 使父保持 WaitingChild；可靠信号推动后续 Finalizing/Terminal。父取消覆盖排队/运行期 child，默认不支持 detached；跨服务/会话、伪造/过期 scope、数量/深度超限拒绝。实际 Host/两个 workers/同一资源槽验证及直接影响集最终 Debug **12/12**（[原始输出](../evidence/B5/resume-b5-children-verified-debug-782f5402eb/test-stdout.log)）、ASan / RelWithDebInfo **12/12**（[原始输出](../evidence/B5/resume-b5-children-verified-asan-8f1919c37d/test-stdout.log)），包含 Native 短调用无任务/热路径分配检查；公开头/实际依赖静态检查无错误。首次 MSVC C1128 原文保留，测试目标启用 /bigobj 后通过；child 槽回收在共享控制块成功分配后才激活，避免失败分配在父锁内重入回收。外部异步完成、必要回调/完整预算、Control/CLI、安装/Profile 和 G3 继续开发；本次不是 D3.06 或 B5/G3 Passed。
- Host、Logging、NativeSubset/独立安装消费者及 footprint 正式测量/预算已交付并完成本包验收。
- B1 历史恢复结论保留：精确工具链校准、三配置编译、Debug 270/270、Release 192/192、ASan 193/193 及各 3 个 CHECK；七模式正式测量均 Passed。D1.06/G1 共用同三份报告，来源 `0452fad`，见[历史交付与自动验收](validation/D1.06-resume-delivery.md)。
- 工具优化阶段已结束；禁止继续把 Fixture、Evidence、Install Consumer 性能优化或新流程文档设为 D1.06 前置。

## B5 前置收口（2026-09-09）

- 已修复 Resource Lease/Waiter 锁外回调及 capture 析构重入时的 handle/State 寿命；ResourceKey 对齐 A21.1 的 1..96 ASCII 字节约束。
- 已在 [B5 规划](plans/B5.md)冻结 Execution/Scheduler 稳定 ticket 先发布、ResourceWaitBinding 早到 wake 安装、cancel/deadline/terminal 和双 generation 仲裁、make_ready 失败释放协议；完整 binding 实现仍属后续 B5。
- 同一已提交 `7a22d0e` 的正式影响集 Debug 16/16、Release 16/16、ASan 18/18，自动验收 Passed 且 errors/review_errors 为空；[交付与原始证据](validation/B5-preclosure-delivery.md)。未重跑无关历史 Passed，未新增 Runtime 反向依赖；G3 未开始。

## B4 交付（2026-09-09）

- D3.01、D3.02、D3.03 已依次 Passed；ExecutorConformance、公平 Scheduler 与资源 MultiClaim/Lease 已完成，三配置及独立验收均绑定已提交 `0d71b4f`，详见 [交付与原始证据](validation/B4-delivery.md)。
- 两轮用户规划建议全部落实：Started=start claim、依赖发布/完成同锁仲裁、inline 单调完成、envelope reservation 与资源锁外去重唤醒。合规拒绝/异常释放 envelope；违约仍持有者保留物理额度至真实收尾。
- 当前 SDK dev.5/B4Subset；新增四个公开头，生产 CpuPool 仅公开依赖 CoreContracts。迁移消费者、Runtime-only expected-only 实际构建/安装，以及 B3Subset 配置投影通过；B3 SDK 交付历史保持原证据。
- 真实 Task/ExecutionRef、协作取消、完整父子寿命与 Embedded/G3 仍属未开始的 B5。未扩展其他产品模块，也未用 ASan 或编译结果代替物理设备准入。

## B3 post-gate closure（2026-09-09）

- 已采纳并完成 C1 Watch 终态单调性与 C2 冻结动态契约材料复用；同 Host 100 次 Session 开闭只生成一次 Schema，Catalog fingerprint/card 稳定，关闭后旧绑定拒绝，当前会话权限仍独立检查。
- 同一已提交 `44246b8` 来源，受影响集 Debug **15/15**、Release **13/13**、ASan **13/13**；自动验收 Passed，errors/review_errors 均为空。Debug Runtime-only 仅取得 expected，真实构建、安装及 Native 消费通过；见[建议取舍、实现与证据](validation/B3-post-gate-closure-delivery.md)。
- `docs/AGENTS.md` 删除已按用户要求纳入实现提交。O1 验证去重、O2/O3 IPC 缓冲优化、Catalog 权限端口整理及独立 Service SID 暂缓；本次未改 Runtime/LocalIPC 生产代码，未改变历史 G1/G2 Passed 来源。
- 本段为 B3 收口时记录，B4 当前状态见文件顶部；真实 Task 观察与完整 Embedded footprint 由 D3.07 承接。G1 历史占用数字不代表本次二进制新测量。

## B3 / G2 交付（2026-09-09）

- D2.05、D2.06、D2.07 和 G2 已依 DAG 顺序 Passed；同一 `e77c328` 来源的三份 E03 报告共享物理执行，各包技术结论独立，见[交付与原始证据索引](validation/B3-delivery.md)。
- B3 交付时 SDK 为 `0.1.0-dev.4 / B3Subset`；薄客户端和实际 CLI 不链接服务端 Runtime/Control/Dynamic，迁移安装、新增公开头、Runtime-only 裁剪及 Native 消费通过。
- 各配置 Native/Dynamic 各 20 个单次成本样本完整归档。此前 CTest 输出截断和 ASan 正控制条件错误的原始材料均保留，未拼接旧来源成功结果。
- 真实 Task Provider 尚未接入，CLI list/watch 对 absent 能力拒绝；watch 脚本不代表真实任务验收，D3.07 承接整体任务观察。未扩展其他产品模块。B4 当前状态见文件顶部。

## B3 前影响收口（2026-09-09）

- 用户提供收口建议已核对；本次修复不改写 B1/B2 历史 Passed。已实现 Subscription 锁外外部调用与 ACK 预算、cursor 原始期限/完整授权视图、Host Logging limits、一致的 Runtime-only 生产裁剪。
- Debug 直接影响集 7/7 与 Runtime-only configure/build/install/真实 Native 消费专项 1/1 通过；首次 list 测试误用委托收缩前旧 VerifiedCaller 的失败已保留并纠正为新可信调用者检验 cursor 自身失效。
- [B2 规划补录及本次执行前计划](plans/B2.md)、[B3 执行前计划](plans/B3.md)和[重大决定 ADR](adr/ADR-b2-closure-boundaries.md)已建立。今后执行前须已有批次规划，重大变更必须 ADR。
- 同一已提交来源 `6662c4e` 的正式影响矩阵 Debug 11/11、Release 8/8、ASan 8/8 全部通过；自动验收 Passed，错误与审核错误均为空。**B3 开发放行，G2 未开始**，见[收口交付及证据](validation/B2-B3-closure-delivery.md)。未重跑历史 Passed 矩阵，DynamicOnly 执行注册明确由 D7.04 承接。

## B2 交付

- D2.01–D2.04 均已 Passed；正式 Debug 30/30、Release 28/28、ASan 29/29，同一实现来源，四包独立 SPEC/CODE 与自动验收，详见[交付及证据索引](validation/B2-delivery.md)。
- 已完成 Data/Binding/Catalog/Control 及安装 SDK；SDK 为 0.1.0-dev.3 / B2Subset。get/list 关闭竞态、复杂 Schema 标记和 void Outcome 编码已收口。
- B2 交付时观察仅为 mock 帧级证明，get 只提供摘要；当时真实 IPC、Task、完整结果与 Plan 均未宣称完成。真实 IPC/CLI 与 G2 已由上方 B3 后续交付，真实 Task/完整结果/Plan 仍属后续包。
- 开发过程及失败原文由 Git 历史和 evidence/bootstrap/B2 保留；不机械重跑 D0/D1 历史 Passed 集合。最终文档/证据归档提交及推送状态以实际 Git 为准。

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
| G2 | Passed |
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
| D2.01 | 实现单DOM Payload、View与预算构建 | D1.01、D0.06 | Passed | Standard | B2 |
| D2.02 | 实现TypeContract、Schema编译与Native等价绑定 | D2.01、D1.02、D1.05、D0.04 | Passed | Standard | B2 |
| D2.03 | 实现能力目录、精确命令卡和帮助导出 | D2.02、D1.03、D1.04 | Passed | Standard | B2 |
| D2.04 | Control方法、观察协议帧与结果映射 | D2.02、D1.04、D0.03、D0.04 | Passed | Critical | B2 |
| D2.05 | 实现真实Named Pipe与认证会话 | D2.04、D0.06 | Passed | Critical | B3 |
| D2.06 | 实现薄CLI、意图文件和原生/动态演示 | D2.03、D2.05、D1.06 | Passed | Standard | B3 |
| D2.07 | 验收双入口与首次Shell闭环 | D2.06 | Passed | Standard | B3 |
| D3.01 | Executor Conformance Kit与生产/测试后端 | D1.02、D0.06 | Passed | Critical | B4 |
| D3.02 | 实现公平Ready调度与依赖结构 | D3.01、D1.03 | Passed | Critical | B4 |
| D3.03 | 实现资源归一化、MultiClaim与租约 | D3.02、D1.04 | Passed | Critical | B4 |
| D3.04 | Submit、执行投影索引与拥有型输入 | D3.02、D3.03、D1.05 | InProgress | Critical | B5 |
| D3.05 | 实现取消、期限与permit竞争 | D3.04、D0.05 | InProgress | Critical | B5 |
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

# 内核需求、不变量与验证追踪

任务：D0.01。依据：[架构 v3.3](01_Architecture_v3.3.md) 与 [执行计划 v3.3](02_Execution_Plan_v3.3.md)。本文件是需求登记，不是内核运行或 G0 通过报告。

机器索引：[tests/manifest.json](../tests/manifest.json)；行为用例草案：[expected.json](../tests/manifests/expected.json)；端口适用草案：[conformance.json](../tests/manifests/conformance.json)。expected 独立维护，禁止用当前发现集合覆盖。

## 工作范围

仅从零建设内核及规划要求的验证夹具和消费者，不迁移旧代码、API、数据库、测试成绩或历史分支。不开发 GUI、CAD/CAM、设备产品或其他领域模块；C-C 只是 D6 起的内核集成消费者。64 个工作包及 G0→G8 顺序保持原样。

## 24 项需求与可判定行为

| 需求 / 测试族 | 规范目标 | 正例观察 | 必须拒绝或保留的反例 | 首个登记用例责任包 | 全部责任及交叉验证包 |
|---|---|---|---|---|---|
| R01 / T01 | 最小 Runtime 不依赖 Data、StateKit、Workspace、SQLite、Asio、AI | 最小 Runtime 依赖闭包仅含 Foundation/CoreContracts | 引入 Data、State、SQL、IPC 或 AI 路径时架构检查拒绝 | D1.06 | D0.01、D0.02、D0.06、D1.02、D1.06、D3.07、D4.01、D4.08、D6.03、D6.08、D8.01、D8.02、D8.05、D8.07 |
| R02 / T02 | Operation 只注册一次，Native/Dynamic/Submit/Plan/UI业务写共用逻辑和治理 | 同一 Operation 定义在 Native、Dynamic 与真实 CLI 入口复用类型化业务及治理 | 相同非法输入从 Native/Dynamic 进入均拒绝且 Handler 调用数为零 | D2.07 | D0.03、D1.02、D1.03、D1.05、D2.02、D2.06、D2.07、D3.04、D4.04、D7.01、D8.01、D8.02 |
| R03 / T03 | 原生短调用无 DOM/假任务，固定成功场景具有零新增分配门禁 | 受限固定成功场景原生调用不建 DOM/Task 且零新增分配 | 已知分配正探针必须增加计数，禁止失效计数器报零 | D1.06 | D1.01、D1.05、D1.06、D2.07、D3.07、D8.02、D8.05 |
| R04 / T04 | 参数、输出、引用、分配和执行量有界 | 合法边界输入在参数、输出和累计分配预算内完成 | 重复字段、非法 UTF-8、深度/节点/分配超限在预算内拒绝 | D2.01 | D0.04、D1.01、D2.01、D2.02、D2.04、D2.07、D4.05、D5.02、D6.01、D7.04、D7.05、D8.03 |
| R05 / T05 | 名称、稳定身份、临时句柄分开，句柄不可跨世代使用 | 稳定 ID 与带 generation 的临时句柄分开使用 | 旧 registry generation 句柄不可在新注册世代复用 | D1.03 | D0.02、D1.01、D1.02、D1.03、D2.03、D4.05、D5.02、D8.01、D8.03 |
| R06 / T06 | 接受、完成、已应用、不确定与收尾故障如实表达 | 参考模型分别表达接受、phase、九类 Outcome 和证据事实 | 模型拒绝将已应用或未知效果降为 FailedBeforeApply | D0.03 | D0.03、D1.02、D1.05、D2.04、D2.07、D3.04、D3.06、D4.03、D4.06、D5.04、D5.05、D5.06、D5.09、D6.07、D8.04、D8.06、D8.07 |
| R07 / T07 | 取消/撤权/提交/关闭具有明确仲裁 | 参考模型枚举取消/撤权与许可竞争的两种合法仲裁结果 | 模型中未获许可不得发布，迟到取消不得抹掉已发布事实 | D0.05 | D0.05、D1.04、D3.03、D3.05、D4.03、D4.04、D5.03、D5.05、D5.06、D6.03、D7.02、D7.03、D8.03 |
| R08 / T08 | 原子能力与 Project/Document 无关，单 provider 单 domain | 无 Document 的单 provider 单 domain 原子组只提交一次 | 跨 domain/provider 或不支持原子的操作在执行前拒绝 | D4.04 | D4.01、D4.02、D4.04、D4.06、D4.08、D7.02、D7.06、D8.02 |
| R09 / T09 | 不可变快照、差量编辑、索引与历史一致 | Edit 候选 read-your-writes、引用/索引与 History 差量准备一致，旧 Snapshot 不变 | 约束失败或丢弃候选不改变正式状态，EditView 不提供 commit 入口 | D4.02 | D4.01、D4.02、D4.03、D5.05、D6.01、D6.02、D6.03、D6.04、D6.08、D8.05 |
| R10 / T10 | 顺序/原子/并行执行不混淆提交范围 | 顺序已提交步骤与单域 Atomic 的提交范围各自明确 | 并行分支失败保留其他已提交事实并等待子执行收尾 | D4.07 | D0.04、D4.04、D4.06、D4.07、D4.08、D6.07、D7.06、D8.03 |
| R11 / T11 | 一个 Plan IR，引用、作用域、预算、等待语义一致 | Plan 合同与 golden/model 明确 slots/exports/控制流及预算语义 | 合同拒绝 kind=return、非法 ticket Await、引用越域和超预算 Plan | D0.04 | D0.04、D4.05、D4.06、D4.07、D4.08、D6.05、D7.01、D7.06、D8.03、D8.06 |
| R12 / T12 | 任务拥有输入与子执行，停止覆盖必要回调 | Submit 持有输入和必要子执行直到完成排空 | inline/重复/迟到 callback 不重复完成或提前释放 owner | D3.06 | D0.03、D2.01、D3.01、D3.04、D3.05、D3.06、D3.07、D4.06、D4.07、D5.04、D6.07、D6.08、D8.03 |
| R13 / T13 | 资源全部获取、公平调度、控制通道可用 | MultiClaim 全部取得后运行，可运行任务公平调度且满载控制仍可用 | 资源不足不可部分持有；满载时 cancel/control 仍可用 | D3.07 | D3.02、D3.03、D3.05、D3.07、D4.07、D8.03、D8.05 |
| R14 / T14 | 状态、幂等回执、必要审计/Outbox 同事务 | 状态、Intent 回执、必要 Audit/Outbox 在同一事务提交 | 注入 begin/write/commit 故障时无撕裂，未知提交如实保留 | D5.05 | D0.05、D5.01、D5.02、D5.04、D5.05、D5.07、D5.09、D6.04、D6.05、D8.04、D8.07 |
| R15 / T15 | 去重窗口、水位、内部子键与 GC 无旧请求复活 | 外部 Intent claim 与 epoch 水位同事务，GC 保留所需回执和 pin | GC/claim 竞争和旧 epoch 重试不得让老外部请求复活 | D5.03 | D0.05、D5.02、D5.03、D5.07、D5.08、D5.09、D6.02、D6.06、D8.04 |
| R16 / T16 | 外部效果未知时不盲重发，恢复不运行历史 Handler | 恢复只重建事实，外部未知效果进入查询或对账 | COMMIT 后发布前崩溃不可重执行业务或盲重发效果 | D5.09 | D0.05、D5.01、D5.04、D5.05、D5.06、D5.08、D5.09、D6.06、D6.07、D7.03、D7.06、D8.04、D8.07 |
| R17 / T17 | 父检查点落后于子结果时可靠衔接，不重复效果 | 父检查点落后时按同一 ChildKey 查询已完成子结果 | 子效果已发生后重启不得以新子键重复发送 | D6.06 | D6.05、D6.06、D6.07、D6.08、D8.04 |
| R18 / T18 | 资产、预览、备份及旧备份恢复有完整寿命边界 | 数据库备份/恢复有一致性校验，旧备份恢复创建新的 RestoreGeneration | 旧世代观察值不得复用，缺失外部历史保留未知并进入对账；资产正文集成留 D6.02 | D5.08 | D0.05、D4.02、D5.08、D5.09、D6.01、D6.02、D6.04、D6.08、D7.02、D7.05、D8.04 |
| R19 / T19 | 事件/通知/日志/必要审计分开，观察背压和丢失不改执行事实 | 通知允许丢失但 gap/版本明确，内部完成和必要审计独立 | 慢订阅、撤权与断线不得取消执行、无限 pin 或泄漏排队数据 | D3.07 | D0.01、D0.03、D0.04、D0.06、D1.02、D1.04、D1.06、D2.04、D2.05、D2.07、D3.04、D3.06、D3.07、D4.08、D5.04、D5.07、D5.08、D5.09、D7.01、D7.04、D7.05、D7.06、D8.03、D8.04、D8.05、D8.06 |
| R20 / T20 | CLI 连接常驻 Host；认证、意图、订阅、执行枚举及输出语义完整 | 真实 Shell 子进程经认证 Named Pipe 连接同一常驻 Host | 帧超限/非法编码/认证失败拒绝，CLI 不初始化第二 Host | D2.07 | D0.01、D0.02、D0.03、D0.04、D1.04、D2.03、D2.04、D2.05、D2.06、D2.07、D3.04、D3.07、D4.08、D5.03、D5.04、D5.07、D5.08、D5.09、D7.01、D7.03、D7.04、D7.05、D7.06、D8.03、D8.04、D8.05、D8.06 |
| R21 / T21 | AI 获得精确目录、真实状态、预检、预览和最少必要结果 | AI 从精确 Catalog 获取权限内 Read/Check/Preview 和有界结果 | 模型自声明身份或批准不得提升权限或应用过期预览 | D7.06 | D2.03、D7.01、D7.02、D7.03、D7.04、D7.06、D8.06、D8.07 |
| R22 / T22 | Host 启动失败逆序清理，停止超时不释放在用依赖 | 启动失败逆序撤销已启动服务，停止排空必要 callback | 停止超时不释放仍被 worker/回调使用的依赖 | D3.07 | D0.04、D1.06、D2.04、D2.05、D3.01、D3.06、D3.07、D5.01、D5.08、D6.03、D6.08、D7.05、D8.04、D8.05 |
| R23 / T23 | 实际性能、Embedded 固定占用、容量和平台支持有可重复证据 | 固定环境分别测 NativeSubset/Embedded 占用及有限阶段预算 | 不得用子集代替完整 Embedded，超预算不得自动提高上限 | D8.05 | D0.01、D0.06、D1.05、D1.06、D2.02、D2.07、D3.02、D3.04、D3.07、D4.01、D4.08、D5.09、D7.05、D7.06、D8.02、D8.05、D8.06、D8.07 |
| R24 / T24 | 安装树、SDK 源码演进、可复用端口合同、自动证据和三个消费者可验证 | 独立安装消费者验证当前公开 SDK 表面、声明、编译要求及版本政策 | 同名头破坏签名不能逃过检查，冻结消费者不能随实现改写 | D8.01 | D0.01、D0.02、D0.06、D1.02、D1.03、D1.06、D2.06、D3.01、D3.07、D4.08、D5.01、D5.09、D6.01、D6.04、D6.08、D7.01、D7.06、D8.01、D8.02、D8.03、D8.04、D8.05、D8.06、D8.07 |

主测试族和责任包来自 E05.1；首个登记用例只代表所述具体行为，不代表该族所有要求均在此包完成。所有 future_cases 当前均 Planned，尚无生产后端、运行时或 CTest 通过记录。

## 不变量

| 编号 | 决定 | 正例 | 反例边界 | 责任包 |
|---|---|---|---|---|
| C1 | 完成事实与 Finalizing | Accepted/phase/Outcome 分层且必要收尾有 owner | Finalizing 不可冒充 Terminal，迟到取消不改写已应用事实 | D0.03、D3.06 |
| C2 | 一种定义四种 shape | Read/StateEdit/ExternalEffect/Lifecycle 绑定同一类型化实现 | Invoke 不强制建 Task，Dynamic 不建立第二 Handler | D1.02、D1.05、D2.07 |
| C3 | 提交与可见性 | 域 reservation 保持到发布，Outbox 不越过发布屏障 | CommitClaimed 不是成功，DurableCommitted 不是已发布 | D0.05、D4.03、D5.05 |
| C4 | 唯一 Plan 格式 | ock.plan/1 用 exports，槽作用域/票据/预算有定义 | 拒绝 wire return 和第二 Plan 运行时 | D0.04、D4.05、D4.07 |
| C5 | epoch 与恢复 | claim/水位同事务，内部子身份独立，旧备份新世代 | 旧外部请求不复活，未知外部效果不盲重发 | D0.05、D5.03、D5.08、D6.06 |
| C6 | 真实依赖与交付 | 可裁剪 DAG 和三个真实消费者按门禁实测 | 不使用伪 Document、假后端或未锁定依赖冒充通过 | D0.02、D0.06、D8.02、D8.07 |
| PlanCompleted | 完整计划成功 | 所有必要节点成功并收尾，含 exports 和有序步骤事实 | 不得伪造全局 CommitId；未知效果不能降为 PartialCompletion | D0.03、D4.06、D6.07 |

## N1–N11 闭环与具体子项

| 增量 | 合同/基础 | 首次验证 | 集成/终验 | 用例 ID |
|---|---|---|---|---|
| N1 观察协议与执行枚举 | D0.03、D0.04 | D3.04、D3.07 | D8.04、D8.06 | T19.observation.ack_get_order、T19.observation.gap_revoke、T20.execution.list_visibility |
| N2 固定占用与零分配 | D0.06 | D1.06、D3.07 | D8.02、D8.05、D8.07 | T23.footprint.native_subset、T23.footprint.embedded、T23.allocation.counter_probe |
| N3 端口共同合同 | D0.06、D1.02 | D1.06、D3.01、D5.01、D6.01 | D8.03、D8.04、D8.07 | T24.conformance.common_required、T24.conformance.production_matrix |
| N4 SDK 独立版本和公开表面 | D0.02 | D1.06、D7.01、D8.01 | D8.06、D8.07 | T24.sdk.surface_declarations、T24.sdk.frozen_consumer |
| N5 自动证据与评审分离 | D0.06 | D0.06 | D8.03、D8.07 | T24.evidence.inventory_integrity、T24.evidence.process_failures、T24.evidence.identity_reruns |
| N6 无状态认证 cursor | D0.04 | D2.04、D3.07 | D8.05、D8.06 | T20.cursor.mac_binding、T20.cursor.bounded_progress |
| N7 UI 统一 Operation 业务入口 | D0.02 | D1.05、D4.04、D6.03 | D8.02、D8.06 | T01.ui.operation_write_boundary |
| N8 无通用 state RPC 与可选 DSL | D0.02 | D2.03、D7.04 | D8.06 | T20.catalog.domain_read_only、T24.scope.optional_dsl |
| N9 IR 语义往返 | D0.04 | D4.05 | D6.05、D8.03 | T11.plan.semantic_roundtrip |
| N10 canonical CBOR 前置验证 | D5.02 | D5.02 | D8.04、D8.06 | T15.canonical.feasibility_vectors、T15.canonical.fingerprint_gate |
| N11 父包门禁及文档口径 | D0.06 | D0.06、D3.07 | D8.06、D8.07 | T24.gates.parent_checkpoints、T01.control.observation_directory |

N1 的 D0.04 属 b 子项；N6 的 keyed MAC、TTL/长度/扫描预算在 D0.04-b 定案。N9 仅测试语义往返，不冻结 IR ABI。N10 的 D5.02-a 是正式 codec 的阻塞前置。N11 不新增工作包：D0.06-a/b/c 和 G3-A/B/C 均需满足原父包条件。

## 消费者、评审和证据

三个消费者的目标、依赖和禁止范围见 [consumer-targets.md](consumer-targets.md)。[ADR 登记](adr/README.md) 保留已定规则及对应责任包。

D0.01 自动检查只核对登记完整性、规范引用、责任映射和清单反例；尚无 C++ 构建适用结果。D0.06 前按 E03 保存 bootstrap 原始命令事实，正式采集器须在 G0 前核对早期证据。人工评审单独保存在 [D0.01 评审材料](reviews/D0.01.md)，未获得评审时包仍为 InProgress。

后续具体 Profile、backend、fixture、重复轮次、故障窗口在责任包实现前评审固定；草案的 RelevantSelectedProfile 不能用于执行或证明适用性。任何必需能力缺失不能靠 NotApplicable 或 skip 变成 Passed。

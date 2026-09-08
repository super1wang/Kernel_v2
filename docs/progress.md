# 内核实施进度

更新：2026-09-08。唯一规范为 [架构 v3.3-r2](01_Architecture_v3.3.md) 与 [执行计划 v3.3-r2](02_Execution_Plan_v3.3.md)。恢复任务默认只读本文件顶部、当前 Development Batch 的相关规范/合同和 diff；历史 review/evidence 按需读取。

## 当前生产节点

- 当前分支：`work/d0-kernel-baseline`；恢复基线核对到 `e347234`，实际执行以实时 Git 为准。
- 已 Passed：G0、G1、D0.01–D0.06、D1.01–D1.06。**这些 Passed 前置不重验**；D1.05 不重跑。
- 当前 Development Batch：**B2 / D2.01–D2.04 InProgress**；B1 / D1.06 与 G1 已 Passed。
- 当前门禁：**G1 Passed**；G2–G8 未开始。
- Host、Logging、NativeSubset/独立安装消费者及 footprint 正式测量/预算已交付并完成本包验收。
- 本次恢复已完成：精确工具链校准、三配置编译、Debug 270/270、Release 192/192、ASan 193/193 及各 3 个 CHECK；七模式正式测量均 Passed。D1.06/G1 共用同三份报告，来源 `0452fad`，见[本次交付与自动验收](validation/D1.06-resume-delivery.md)。
- 工具优化阶段已结束；禁止继续把 Fixture、Evidence、Install Consumer 性能优化或新流程文档设为 D1.06 前置。

## B2 当前开发

- Native 安装消费者在 B2 SDK 下真实调用通过（`resume-root-native-final-1b67a2cd7b/`；详细 `sdk-installed_host-dd4910bd91/`），实际 CL/link tlog 验证 Runtime/bcrypt 存在、Data/Dynamic/Control/jsoncons 未进入 Native 链接。示例的旧 SDK 固定值已同步，之前失败保留。

- SDK 根工程接线已落地：`OCK::Data/Dynamic/ControlProtocol/Control` 为可安装生产静态库；jsoncons 私有编译、不进入导出依赖；Runtime 继续只依赖 CoreContracts/Foundation 与 Windows bcrypt。开发 SDK 版本 `0.1.0-dev.3`，阶段 `B2Subset`，**不是包级 Passed**。
- 根工程 Debug 编译完成；真实 CMake DAG 检查无错误（`resume-root-sdk-graph-86b2b1380d/`），受影响架构守卫 62/62（`resume-root-sdk-guards-fixed-6ff81f78fd/`）。历史 CoreContracts 夹具原先未重置新增组件状态，已修正夹具，未放松阶段守卫。
- 根工程集中 S_changed **26/26**（`evidence/bootstrap/B2/resume-root-b2-direct-58ffc568bb/`）：迁移安装后的 B2 消费者、逐公开头独立编译、Data/Binding/Catalog/Control 合同。安装导出无源码绝对路径和 jsoncons target。初次消费者漏填 cursor view 的失败保留；补正消费者，未改变生产验证。
- D2.01 的 `tests/unit/data/seeds/` 已补 12 个严格 JSON/重复键/数值溢出/UTF-8/截断/深度种子；测试执行确定性字节变异及接受结果再编码，未声称完成长时间 coverage-guided fuzz。

- 最新推进：`operation.invoke` 使用真实 `InvocationBinding` → `BoundOperation` → `HostBound`；非法参数/合同摘要不进入 Handler，输出合同失败保留 Completed/FailedBeforeApply。实际 Router 调用已由 NativeHost 消费者覆盖。
- SDK 接线第一步：Dynamic 公开头统一到 `ock/dynamic/binding/`、`ock/dynamic/catalog/`；ControlProtocol 统一到 `ock/control_protocol/`，消除与 Control 的公开前缀冲突。Schema 后端改用规范私有包含路径。根 CMake、清单和安装消费者现已完成首轮接线验证，见下方最新记录。
- 修复 Schema 将业务字段名 `default`/常量对象误作默认值关键字拒绝的问题；新增正反例，真正 Schema default 仍拒绝。集中 Debug 最小影响集 **11/11** 通过，原始证据：`evidence/bootstrap/B2/resume-sdk-path-direct-9321236ea7/`；编译：`resume-sdk-path-schema-build-afa92734fc/`。范围覆盖 Binding/Catalog/Control，不重跑历史 Passed 包，不作为正式 B2 Passed。

- D2.01：单 DOM、借用 View、显式 clone/share、冻结 Builder、预算解析及 Buffer 已有直接验证。继续保留先前预算耗尽崩溃的失败记录和真实 View 逃逸 ASan 反例。
- D2.02：新增显式字段合同、Missing/null、精确整数/有限浮点、长度/枚举/单位元数据、跨字段 validator、参数/结果编码与 2020-12 投影；注册绑定强制复用 Native TypeContract 的同一字段来源。Schema 注册编译与有界缓存、仅注册资源引用、并发读取已验证。jsoncons 0.178.0 通过私有 Key/allocator 适配；编译元数据保留来源 owner，实例不建第二 DOM。
- D2.02 真实调用接线：新增 `BoundOperation<A,R>`，注册时缓存共享字段 Schema，动态参数解码后仅调用生产 `HostBound`，不接裸 Handler。真实 NativeHost 消费者核对 Native/Dynamic 成功结果、非法输入不入 Handler、线程拒绝、会话关闭和输出合同失败的一致 Outcome。最终 Debug 1/1：`evidence/bootstrap/B2/resume-dynamic-parity-direct-97cde55100/`。保留初次消费者缺生命周期/缺 shutdown 及错误测试预期的失败记录；未修改 Host 的关闭要求或已有 Native Outcome 语义。
- 当前动态调用返回原始强类型 `InvokeReply<R>`，不将 Completed/FailedBeforeApply 改成 Rejected。新增生产 `encode_invoke` 与 `schemas/rpc-v1/outcome.schema.json`，覆盖九类 Outcome、六类 KnownFact、完整原子域、结构化错误、收尾/必要记录条件；结果编码失败写 result_error/exports_error 并保留事实。D0 模型 Schema/golden 不改写。Control operation.invoke 已接真实 HostBound，见下方接线记录。
- Outcome 校验定位并修复了 jsoncons 0.178.0 无 default 也积累 null patch 的适配问题：只读 validator 包装关闭默认值投影，不关闭验证器、不增加预算，不修改依赖缓存；保留 `resume-outcome-stack-direct-75ba4c7150/` 等失败证据。当前默认值关键字拒绝策略仍保留；检查只遍历 Schema 位置，业务属性名和 const/enum 数据不作为关键字拒绝。
- 本批最终直接结果：Debug 8/8（`evidence/bootstrap/B2/resume-outcome-affected-direct-b9d80c1fe3/`），覆盖绑定/并发 Schema/目录/Router/订阅/list/Native-Dynamic/Outcome；ASan 2/2（`evidence/bootstrap/B2/resume-outcome-asan-direct-5747ec8b63/`），覆盖复杂 Outcome 编码及并发 Schema 生命周期。仅 S_changed，未重跑历史包或冒充 B2 正式矩阵。
- D2.03：能力目录 search/describe、分页/指纹、基于 Schema 字段的帮助导出已有首轮实现；复用当前 Session 政策，区分 installed/visible/eligible，查询不创建许可，冷 Docs 通过共享定义引用。精确命令卡完整内容及真实注册绑定的集成仍待完成。
- D2.04：已实现 OCK1 部分读取、帧头/预算/客户端请求方向检查；无状态 cursor 使用 Windows 随机密钥与 HMAC-SHA-256，匹配 D0.04 固定 golden，覆盖身份/过滤/世代绑定、篡改、规范编码、TTL/时钟回拨，逐 cursor 对象/句柄为零。固定 canonical 直接编码，不重建输入 DOM。
- D2.04 后续推进：新增连接级 Router，握手校验 `ock.control/1`、规范身份与当前 Caller；从实际装配方法导出能力列表，未安装 Plan 路由返回 MethodNotFound。注册 Schema 校验参数后分派；业务 Payload 保留在 JSON-RPC result，未知效果事实不会被普通 RPC error 替换。未取得结构化业务结果的端口异常关闭连接，不伪造 Rejected。直接合同覆盖握手顺序/版本、方向、未知方法、参数反例、事实封装及异常关闭；基础路由验证使用受控端口；后续 operation.invoke 已通过真实 NativeHost 接线验证，其他端口按各自证据区分。
- N1 wire 已推进：subscribe/unsubscribe/list 直接解码为现有 CoreContracts DTO，校验规范身份、uint64 溢出、单一过滤器、重复目标/topic、50ms 最小间隔、分页及 cursor 文本边界；同步把请求 id 收紧到 D0.04 的 1–96 个可打印 ASCII 字符。直接影响集 Debug 3/3：`evidence/bootstrap/B2/resume-observation-bounds-direct-06d9d873d5/`。订阅后续接线见下方记录；不把参数解码当成观察闭环。
- 当前正式接线入口为根 `CMakeLists.txt`，B2 合同测试直接链接同一生产库；`tests/unit/data/CMakeLists.txt` 保留独立开发入口。公开头清单已同步；SDK 接线首轮通过不代表 B2 包级验收。
- 剩余主线：完成精确命令卡与真实目录装配、execution.get 查询适配及剩余专项反例，集中 SPEC/CODE 与正式验收。B2 全部仍为 InProgress，不提前写 Passed。
- 本轮集中直接结果：Debug **17/17**（`evidence/bootstrap/B2/resume-b2-debug-direct-64f8f457ca/`）、ASan **18/18**（`evidence/bootstrap/B2/resume-b2-asan-direct-b0f63a216e/`）。随后 cursor 改为固定直接编码、绑定补充共享 TypeContract 静态约束，分别只重跑受影响用例，日志见 `resume-cursor-canonical-*`、`resume-shared-contract-*`。这些是开发 S_changed，不能拼为包级正式验收。
- Router 当前直接验证：`evidence/bootstrap/B2/resume-router-failure-direct-b1432ab583/`（T06.protocol.router，Debug 1/1 通过）。
- N1 订阅生产实现已推进到 progress/phase 生命周期：注册屏障保证 ack 在事件之前；复用政策发送仲裁，发送前重验权限；限流丢弃保留 sequence/gap；退订、注册/ack 失败、序号耗尽及慢读超时清理监听和队列。外部 reserve 返回后重验配额，租约在连接锁外释放。实际 ack/progress/phase 帧通过冻结 N1 Schema 校验；首轮字段不符失败保留在 `resume-subscription-schema-direct-1735ca9a55/`。
- 本次订阅最小影响集：Debug 1/1（`evidence/bootstrap/B2/resume-subscription-timeout-direct-656a2d49d0/`）、ASan 1/1（`evidence/bootstrap/B2/resume-subscription-timeout-asan-direct-387c5d9972/`）。这是直接开发验证，慢读由受控 transport/clock 验证，不代表真实 IPC 超时验证或包级 Passed。
- 事实通知映射已补齐：CoreContracts `FactSummary` 新增可选强类型业务引用及从 Fact 提取的 `summarize_fact`；校验引用类型/非空，Published/Effect/Lifecycle 分别映射冻结 N1 的三类摘要。恢复 Fact topic，缺失引用不回退 FactId；合并或无法编码提示记 gap，完整事实仍由 get/wait 获得。新增公开合同字段的 SDK 摘要将在 SDK 接线时同步，不重写 D1.02 历史验收。
- Router 已接入真实订阅适配器：从冻结 N1 资源编译参数 Schema；成功 subscribe 的完整 ack 由连接队列持有，Router 标记 queued、不重复包装响应；退订返回 removed；Router 的显式关闭、方向错误及析构同步断开订阅。受控注册/断线竞态覆盖来源尚未返回时关闭连接，最终无 ack、监听或队列遗留。
- 本次事实与 Router 接线直接验证：Debug 2/2（`evidence/bootstrap/B2/resume-subscription-integration-direct-ab8801dd73/`）、ASan 2/2（`evidence/bootstrap/B2/resume-subscription-integration-asan-direct-68f4cd7495/`），覆盖实际 ack/progress/phase/fact 帧 Schema、事实业务引用、Router 单次响应和并发注册清理。仍为 mock 源帧级 S_changed，不代表真实 Task/IPC 或 B2 正式验收。
- list/cursor 生产接线已补齐：`ListMethod` 通过 Runtime 授权分页，`PageContinuationPort` 恢复本次短寿命绑定，Control 校验无状态 MAC/TTL/身份/过滤；响应经 SendCoordinator 排队并在发送前重验权限，Router 用 queued 避免二次响应。实际页帧符合冻结 execution-list Schema，公开错误区分 CursorInvalid/Expired，未知及无权统一 NotAvailable。
- 本次 list 接线直接验证：Debug 3/3（`evidence/bootstrap/B2/resume-list-final-direct-0546d49deb/`）、ASan 1/1（`evidence/bootstrap/B2/resume-list-final-asan-direct-e44ba7b883/`）；覆盖两页、实际帧 Schema、MAC 篡改/过滤变化/跨 caller/TTL 在扫描前拒绝、Router 装配及发送前撤权，cursor 句柄为零。仅 mock 源帧级开发验证；真实索引/任务/IPC 未在 B2 宣称完成。
- 观察剩余边界：execution.get 查询适配及整批合同复核仍待完成；list 的真实传输超时归 D2.05 接线。共享政策 Host 字节预算作为更严格主体上界，实际 ack 公布生效额度，不声称独立主体配额已实现。
- 未重跑 D1.06 或其他历史包矩阵；未新增流程政策文档。本次归档 B2 生产实现与 SDK 接线开发检查点；B2 仍 InProgress，提交与推送状态以实际 Git 为准。归档前公开头按仓库 LF 规则归一并更新 SHA，已核对 staged 字节与清单一致；不改变接口语义。

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
| D2.01 | 实现单DOM Payload、View与预算构建 | D1.01、D0.06 | InProgress | Standard | B2 |
| D2.02 | 实现TypeContract、Schema编译与Native等价绑定 | D2.01、D1.02、D1.05、D0.04 | InProgress | Standard | B2 |
| D2.03 | 实现能力目录、精确命令卡和帮助导出 | D2.02、D1.03、D1.04 | InProgress | Standard | B2 |
| D2.04 | Control方法、观察协议帧与结果映射 | D2.02、D1.04、D0.03、D0.04 | InProgress | Critical | B2 |
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

# Open Command Kernel v3.3-r2｜最终架构与执行契约

**设计日期：2026-09-07 · 文档代号：OCK-ARCH-3.3**  
**配套文件：[02_Execution_Plan_v3.3.md](02_Execution_Plan_v3.3.md)**  
**状态：以 v3.2 完整规范为直接基底，合入 cursor、UI 单入口、接口范围 ADR、IR 往返与 canonical CBOR 前置验证等收口项，供从零实施。不是已实现 SDK、已通过的并发验证或性能认证。**

> 一套 Operation 定义、一份类型化业务实现、一套 Plan 执行语义；每个原子域一个提交所有者。Native、CLI、Shell、脚本与 AI 共享治理，不共享不必要的编码、排队和持久成本。

**2026-09-08 r2：进一步澄清 Passed 复用、开发依赖/验收依赖分离、批次物理测试复用与 Gate 集成责任；文件名、规范代号、行为合同、64 包编号及 G0–G8 边界不变。**

## 阅读与规范

本文规定“系统必须如何工作”，配套执行计划规定“按什么依赖和门禁实现”。开发只使用本版与配套步骤，不再需要拼接 v2.0、v3.0、v3.1、v3.2 或审查附录。文档章节号 A00–A24、需求编号 R01–R24、测试族 T01–T24 在两份文件之间对应。

“必须/禁止”为规范约束；“默认”为首个正式实现的选择，可通过明确 ADR 修订；“预算初值”为待测目标，不是已确认的支持上限。示意类型描述语义，不冒充可编译公共头。机器 Schema、数据库 DDL、实际库版本由指定工作包产生并接受合同测试，不能由实现者静默改变本文语义。

### 实现与验证的关系

- **验证设施服务生产实现。** 测试、Evidence、Review、Conformance 工具不属于生产内核能力；除工作包明确列为交付物或确实阻断当前行为验证，不得扩张为后续编码的额外前置。
- **Passed 是可复用事实。** 一个工作包已在精确来源上正式 Passed 后，后续消费者默认信任其已冻结行为、审核和证据；架构不要求每个下游包重新证明同一事实。只有后续修改触及该 Passed 包的冻结公共输入、契约/构建边界，或发现证据完整性问题时，才重新验证受影响范围。
- **开发依赖与正式验收依赖分离。** 为减少无意义的中间交付，执行计划允许同一开发批次中的下游在上游所需接口已实现且完成直接验证后继续编码；这只是开发调度条件，不是新的正式包状态，也不允许在上游正式前置尚未 Passed 时宣布下游 Passed 或越过 G Gate。
- **最小充分验证与物理复用。** 要求行为覆盖与最终可证明性，不要求每个实现步骤、每个包编号重复全量验证。在最终来源、配置、适用范围可核对且工具支持时，一次真实 configure/build/test 可以同时支持多个工作包/门禁的验收引用；逻辑完成条件分别判定，不混淆引用次数与执行次数，不拼接不同版本成功结果。
- **阶段 Gate 承担完整集成。** 包级验证重点证明新增行为和受影响边界；跨包历史组合、代表性回归、安装/Profile/性能/故障组合由对应 G0–G8 集中承担，不把同一完整历史矩阵机械复制到每个中间工作包。
- **流程数量不代表可靠性。** 更多审核文件、CTest 数量、重复 Profile 或重复前置核验本身不构成质量证据；验证必须对应真实风险或完成条件。Code-First、Development Batch、S_changed/S_required/S_gate 及批次 Review 规则以执行计划 E00/E02 为准，不削弱本架构安全、寿命、预算与恢复合同。

---

<a id="a00"></a>
## A00｜来源、继承关系与本轮收口

### A00.1 依据与优先级

直接修订依据为《Open Command Kernel v3.2｜最终架构与执行契约》[B32A]、《Open Command Kernel v3.2｜拆分执行步骤与验收计划》[B32E]，以及本对话对 v3.2 的逐项收口审查 [B33]。v3.2 已经完整吸收 v3.1、v3.0、审查附录 C1–C6、v2.0 基础约束及 N1–N5，因此本文只在其上增量定案，不要求开发者回读旧规范。本文是**完整统一规范**，不要求旧项目代码、API 或历史数据兼容，不包含搬迁任务；A19 的兼容政策约束本 SDK 今后的正式发布，不是旧项目迁移要求。

本轮在原方案基础上补齐的是设计决定，不能写成“原文已经有完整定义”或“实现已验证”。外部规范只支持相应技术边界，不为整个内核背书。

| 已由 v3.1 收口并在本版保留的项目 | 明确决定 | 正文位置 |
|---|---|---|
| C1：完成事实与 Finalizing | 接受、执行阶段、业务事实分层；补齐外部效果、生命周期、完整计划成功；必要收尾有失败处置 | A05、A09 |
| C2：统一操作与函数签名 | 一种定义、四种受限 shape；Invoke 不强制建任务；异步完成适配不重复业务实现 | A04 |
| C3：提交及可见性 | CommitClaimed ≠ 成功；DurableCommitted ≠ 内存已发布；域 reservation 贯穿发布 | A06、A07 |
| C4：Plan 格式 | `ock.plan/1` 使用 `exports`；无 wire `return`；引用、分支、循环、票据与预算明确 | A08 |
| C5：epoch 与恢复 | claim/水位同事务；内部子执行身份与外部 epoch 分开；旧备份恢复有新世代和对账 | A11、A12、A14 |
| C6：真实依赖与交付 | 固定 target DAG、三个消费者、完整范围与可运行子集；库版本通过门禁后锁定 | A02、A20–A23 |
| 补充 S1：计划成功结果 | `PlanCompleted` 覆盖多个已提交步骤的完整成功，不能伪造单 CommitId | A05 |
| 补充 S2：单条编辑与原子读取 | 同一 Edit 业务函数；候选读取显式绑定；无效 provider 在注册/编译拒绝 | A04、A07 |
| 补充 S3：发布与可靠事件 | Outbox 不越过本机发布屏障；恢复完成前不驱动订阅者 | A06、A13 |

### A00.2 明确保留的 v2.0 细则

Payload 默认 move-only，显式 clone/share；View 不拥有；每个动态节点不另配堆上 PIMPL。错误域对象具有进程级寿命，错误文本拥有内存。内部互斥锁下不调用业务 Handler、可重入 validator 或 exporter。不可变根发布仍需要同步，`atomic<shared_ptr>` 不被称为必然 lock-free；旧根析构移出提交关键区。内存 Snapshot 不等于 SQLite 长读事务。[B2 §4–6、§9]

### A00.3 v3.2 新增收口与范围边界

下表是本次在 v3.1 上新增或明确化的设计决定，不声称原文已具备，也不声称新增测试或工具已经实现。A00–A24、R01–R24、T01–T24、D0–D8 和 64 个工作包编号保持稳定；新增用例作为既有测试族的子项，工作内容合入原卡片。

| 更新项 | 本版决定 | 规范位置 | 首次落实与终验 |
|---|---|---|---|
| N1 订阅与执行枚举 | 最小 `notifications.subscribe` / `notifications.unsubscribe`、服务端 `notifications.event`、`execution.list`；连接寿命、观察版本、订阅空窗、权限及背压明确 | A09.5、A10、A15、A17.4–A17.7、A21.1 | D0.04-b；D2.04；D3.04/D3.07；D7.05/D8 |
| N2 轻量化可测门禁 | 区分原生子集与完整 Embedded；体积、Private Bytes/Working Set、线程、Ready 时间、受限零分配与回收均有口径 | A21.4–A21.6 | D1.06；D3.07；D8.05 |
| N3 端口一致性套件 | 版本化共享合同＋能力适用矩阵＋后端专项；测试套件不进入产品依赖 | A22.2 | D0.06 基建；D1.06 Logging；D3.01 Executor；D5.01/D6.01 Storage |
| N4 SDK 演进政策 | 文档/SDK/协议版本分离；开发期与 Stable API 分开；清单、声明审查和冻结消费者三层门禁 | A19.2–A19.4 | D0.02；D8.01/D8.06 |
| N5 证据自动采集 | 工具运行并记录真实命令、预期/发现/执行清单和产物；自动事实与人工评审分离 | A22.3、执行计划 E03 | D0.06；所有工作包；D8.07 |

本版不是新一轮主架构重设计。不增加可靠进度消息中间件、全局执行串行器、强制任务记录、第二套测试框架或运行时证据服务。`execution.wait` 继续提供终态等待；订阅补主动观察面，不能说之前只能轮询。N2 的体积/内存/启动绝对值须在固定构建与测量环境中按指定阶段审批，不填入未经测量的生产承诺。


### A00.4 v3.3 新增收口与范围边界

v3.3 保持 A00–A24、R01–R24、T01–T24、D0–D8 与 64 个工作包编号不变；新增内容继续作为既有测试族和工作包的子项，不另造平行架构或第 65 个工作包。

| 更新项 | 本版决定 | 规范位置 | 首次落实与终验 |
|---|---|---|---|
| N6 cursor 资源边界 | `execution.list` v1 只使用**无状态、认证完整性保护的 opaque cursor**；服务端不保存逐 cursor 句柄，限制 token 大小/TTL/扫描预算；未来 stateful cursor 必须新版本并另定数量/内存配额 | A17.6、A21.1 | D0.04-b；D2.04；D3.07；D8.05 |
| N7 UI 单一业务入口 | 产品 UI 是同一 Operation 面的进程内消费者；状态写、外部效果和生命周期转换不得绕过 Operation。渲染、局部视图计算与已授权不可变只读可直接类型化读取 | A16.3、A23.1 | D0.02；D1/D4；D6 Workspace；D8.06 |
| N8 外部状态/API 与人类前端 ADR | 不提供通用无类型 `state.inspect/query` RPC；领域状态通过 Read Operation 暴露。GUI 是桌面产品主要人工前端；结构化 Plan/CLI 是稳定自动化面；人类 DSL/REPL 可选且不阻塞首发 | A18、A23.1 | D0.02；D2/D7；D8.06 |
| N9 Plan IR 语义往返 | D4.05 增加测试级 IR semantic roundtrip；往返覆盖 opcode/精确 Operation/slot/控制边/预算/exports，但不冻结 C++ 对象布局、指针、registry handle 或持久 ABI | A08.9 | D4.05；D6.05 交叉验证；D8.03 |
| N10 canonical CBOR 前置验证 | D5.02 在正式记录/指纹实现前先完成 `ock.canonical/1` feasibility spike；jsoncons 不被假设天然满足最短整数/浮点、-0、map 顺序等规则；结论只能是直接使用、薄策略层或有限自有 encoder | A12.4 | D5.02-a；D5.02；D8.04/D8.06 |
| N11 文档/项目门禁细化 | D0.06 与 G3 保留原门禁但固定内部 checkpoint；A17.7/T06 口径、Control observation 目录归属及重复规则文字同步修正 | A02、A17.7、A22、A23 | D0.06；D3.07；D8.06 |

以上新增均为**收口与实施保险**。N6 通过选择无状态 cursor 从根本上消除句柄堆积，而不是再增加一个服务端 cursor 池；N9 的测试 codec 不是公共 wire 或 DurablePlan 持久格式；N10 的 spike 允许选择极小专用编码器，但禁止因此自研完整通用 CBOR 库。

<a id="a01"></a>
## A01｜范围、目标和硬约束

首发支持 Windows x64 / C++20、单机多客户端、单活动写 Host；模块化单体、静态模块与显式组合根。第三方自身使用 DLL 不等于支持业务模块热卸载。无文档、无数据库、无网络的应用必须真实可构建。

| 需求 | 规范目标 | 主测试族 |
|---|---|---|
| R01 | 最小 Runtime 不依赖 Data、StateKit、Workspace、SQLite、Asio、AI | T01 |
| R02 | Operation 只注册一次，Native/Dynamic/Submit/Plan/UI业务写共用逻辑和治理 | T02 |
| R03 | 原生短调用无 DOM/假任务，固定成功场景具有零新增分配门禁 | T03 |
| R04 | 参数、输出、引用、分配和执行量有界 | T04 |
| R05 | 名称、稳定身份、临时句柄分开，句柄不可跨世代使用 | T05 |
| R06 | 接受、完成、已应用、不确定与收尾故障如实表达 | T06 |
| R07 | 取消/撤权/提交/关闭具有明确仲裁 | T07 |
| R08 | 原子能力与 Project/Document 无关，单 provider 单 domain | T08 |
| R09 | 不可变快照、差量编辑、索引与历史一致 | T09 |
| R10 | 顺序/原子/并行执行不混淆提交范围 | T10 |
| R11 | 一个 Plan IR，引用、作用域、预算、等待语义一致 | T11 |
| R12 | 任务拥有输入与子执行，停止覆盖必要回调 | T12 |
| R13 | 资源全部获取、公平调度、控制通道可用 | T13 |
| R14 | 状态、幂等回执、必要审计/Outbox 同事务 | T14 |
| R15 | 去重窗口、水位、内部子键与 GC 无旧请求复活 | T15 |
| R16 | 外部效果未知时不盲重发，恢复不运行历史 Handler | T16 |
| R17 | 父检查点落后于子结果时可靠衔接，不重复效果 | T17 |
| R18 | 资产、预览、备份及旧备份恢复有完整寿命边界 | T18 |
| R19 | 事件/通知/日志/必要审计分开，观察背压和丢失不改执行事实 | T19 |
| R20 | CLI 连接常驻 Host；认证、意图、订阅、执行枚举及输出语义完整 | T20 |
| R21 | AI 获得精确目录、真实状态、预检、预览和最少必要结果 | T21 |
| R22 | Host 启动失败逆序清理，停止超时不释放在用依赖 | T22 |
| R23 | 实际性能、Embedded 固定占用、容量和平台支持有可重复证据 | T23 |
| R24 | 安装树、SDK 源码演进、可复用端口合同、自动证据和三个消费者可验证 | T24 |

**不属于本版**：硬实时伺服、功能安全认证、任意 OS Shell 执行、热卸载、分布式事务、跨域原子写、无限脚本、自动猜测融合、通用 ORM、完整反射语言。设备急停、联锁和失联停止由设备安全层保证，不依赖 AI 或普通任务队列。

<a id="a02"></a>
## A02｜组件 DAG、组合与公开边界

### A02.1 实际目标依赖

下表为直接组件依赖；传递依赖由 CMake 正确导出，不能只靠 include 路径隐藏。`CoreContracts` 只容纳跨组件必须共享的窄协议，不收纳 SQL、DOM、Document 或通用 ServiceLocator。

| CMake 目标 | 直接内部依赖 | 默认后端/说明 |
|---|---|---|
| `OCK::Foundation` | 无 | 公开 expected 模板依赖；标准库 |
| `OCK::CoreContracts` | Foundation | 身份、Outcome 语义、执行/完成/原子端口 |
| `OCK::Runtime` | CoreContracts | 注册、Invocation、任务、资源、准入、观测；不含可选组件 |
| `OCK::Data` | Foundation | 私有 jsoncons；Payload、View、预算构建与基础编码 |
| `OCK::Dynamic` | Runtime、Data | TypeContract 动态绑定、Schema、目录；不再建第二DOM |
| `OCK::Automation` | Runtime、Dynamic | Plan 编译及易失执行，不依赖 DurablePlan |
| `OCK::State` | CoreContracts | StateKit；私有 immer；经 registrar/执行端口安装 |
| `OCK::Workspace` | State | 组织、生命周期；通过 CoreContracts 注册/运行端口接入 |
| `OCK::Durable` | CoreContracts | 记录与存储端口、恢复协议；不直接包含 SQLite API |
| `OCK::StateDurable` | State、Durable | 唯一状态/回执事务集成路径 |
| `OCK::DurablePlan` | Automation、Durable | 持久运行驱动，不另写解释器 |
| `OCK::ControlProtocol` | CoreContracts、Data | RPC帧/消息与版本契约；无服务端Registry |
| `OCK::Control` | Runtime、Dynamic、ControlProtocol | 服务端会话/RPC路由；不强依赖Automation |
| `OCK::ControlClient` | ControlProtocol | 薄客户端；不依赖Runtime或服务端认证实现 |
| `OCK::Adapter::CpuPool` | CoreContracts | BS::thread_pool；通过 Executor 端口装配 |
| `OCK::Adapter::Logging` | CoreContracts | spdlog；普通日志 |
| `OCK::Adapter::Config` | Dynamic | toml11；可裁剪配置投影 |
| `OCK::Adapter::SQLite` | Durable | SQLite、平台存储独占 |
| `OCK::Adapter::Storage` | CoreContracts | 文件、内容摘要、资产字节端口；不含业务目录 |
| `OCK::Adapter::LocalIPC` | ControlProtocol | standalone Asio＋Win32；连接/字节与OS身份，服务端授权交给Control |
| `OCK::Adapter::MCP` | Control | 按协商协议投影，AI 厂商依赖不进入 Runtime |
| `ock` 可执行程序 | ControlClient、LocalIPC、CLI11 | 不链接服务端Runtime/Workspace，不初始化第二Host |

**DAG 实现约束**：State 和 Workspace 通过 CoreContracts 的注册/执行接口，不调用 Runtime 私有符号；否则必须将对应桥接实现放入组合层，不能反向修改 Runtime。Control 的 `plan.*` 方法由已安装 Automation 的 endpoint adapter 注册；Control 不 include Plan IR。宿主样例是组合根，只有它同时选择所有需要的组件。

一个逻辑组件可以导出少数子目标；Data/ControlProtocol/ControlClient的分离只为纯基础消费者与薄CLI消除服务端链接，不能发展成每个类一个库。jsoncons仅在Data/Dynamic技术后端内部；两者通过受限内部访问约定复用同一个DOM，不能复制为第二棵通用树。

### A02.2 正式组合

| Profile | 包含 | 明确不要求 |
|---|---|---|
| Embedded | Runtime、CpuPool、内存观测/必要日志 | JSON、State、数据库、网络 |
| AutomationHost | Embedded＋Dynamic＋Automation＋Control/LocalIPC/CLI | State、Workspace、Durable |
| DurableHost | AutomationHost＋Durable/SQLite；可加 DurablePlan | Workspace |
| WorkspaceHost | State＋Workspace＋StateDurable 及所需上述能力 | 具体 CAD/CAM/设备/UI |

额外长期验证**无文档状态服务**：Runtime＋State＋可选 Dynamic/Automation/Durable；以及 State 的无数据库组合。简单配置 provider 可以仅使用一个类型化不可变根，不必购买通用对象表的成本。

裁剪覆盖构建、依赖取得、安装头、链接、服务、线程、表、目录与权限面。配置缺能力时拒绝依赖它的注册，或不安装该操作；必要功能无 Null-success 实现。纯内存支持必须公开标记，不冒充耐久。

**测试与工具边界（N3/N5）**：`OCK::TestSupport`、`OCK::PortConformance`（若使用这些测试目标名）和 `tools/evidence/` 只在测试/工具构建中出现，不列入上表产品依赖，不随 Embedded 启动。公共 SDK 清单与稳定政策见 A19；Embedded 占用规格见 A21.4，不能用原生子集的数值代表完整 Embedded。

**Control 源码组织边界（N11）**：执行计划中若出现 `packages/control/observation/`，它只是 `OCK::Control` 目标内部的源码目录/命名空间组织，**不是新的 CMake target、公开组件或依赖层**。观察协议的公共 wire 类型仍属于 `OCK::ControlProtocol`，服务端索引/订阅实现属于 `OCK::Control`；不得因目录命名新增 `OCK::Observation` 或让 Runtime 反向依赖 Control。

<a id="a03"></a>
## A03｜基础值、动态数据与契约

### A03.1 Foundation

默认 C++20，`Result<T,E>` 使用一个固定 expected 后端；`Error` 使用静态错误域代码＋可选拥有型有界详情。相同 SDK 只使用一种后端和工具链模式。Foundation 不包含动态 Payload。错误域具有进程级寿命；不支持卸载其代码。普通业务失败使用 Result；不变量破坏及不可恢复 OOM 不伪装成可重试业务错误。[B2 §4]

注册名称采用有界 ASCII、按字节区分大小写比较，不自动转小写或作Unicode归一化；展示文本 UTF-8。TaskId/ObjectId 等稳定身份默认 Tagged 128 位值；外部编码规范化且不可作为授权。BoundHandle 含 registry generation；runtime epoch 不持久化为业务身份。值/长度累计做溢出检查。

### A03.2 单一动态表示

Dynamic 提供 move-only Payload、PayloadBuilder、借用 ValueView、显式 SharedPayload 与 Buffer/DataRef。显式 `clone` 才深复制。一个输入最多构建一份通用 DOM；向具体 Args 绑定不是再建一套通用树。每节点不添加堆包装。只有实际 fan-out 时共享引用计数。

数据冻结后 Builder 及可变别名失效。Submit 持有拥有型 Args/Buffer/快照租约；同步调用可借用。请求 arena 不得早于后台数据 owner 释放。大几何、刀路、遥测使用类型化缓冲和 DataRef，不把每个点包装成命令。

解析先查帧长，再限制 token、深度、节点、容器、文本和实际累计分配。重复字段在合并前拒绝，非法 UTF-8、NaN/Infinity、无界结构拒绝。Missing 与 null 不合并；identity/revision/sequence 使用精确文本。其余 int64/uint64/float 由字段契约规定，不做隐式精度损失转换。

### A03.3 TypeContract 与 Schema

有限显式字段描述是 Native/Dynamic 唯一来源：字段、required/nullable、上下界、长度、枚举、单位与说明。共享 typed validator 验证跨字段规则。JSON Schema 采用 2020-12；第一版 `format` 只作注释，涉及安全/业务格式的验证放入显式字段验证器并在目录披露。禁止网络 `$ref`，所有引用来自已注册资源包。[E04]

不可等价投影的复杂 Schema 操作标记 dynamic-only，不能自动生成一个少检查的 Native 入口。Schema/引用在注册期编译，实例值在执行期校验；缓存键包括 dialect、内容和依赖指纹。固定对象并发可读性必须对后端版本做专项测试。

Typed 与 Dynamic 一致性套件必须比较同逻辑输入的接受/拒绝、归一化值、权限和结果，包括缺失/null、溢出、浮点、单位、目标和资源。原生只省去解析编码，不省去治理。

<a id="a04"></a>
## A04｜Operation、注册与受限函数形态

### A04.1 一种定义，四种 shape

`OperationDefinition<Args,R>` 由 Contract、Policy、Docs 组成。一份业务能力只注册一个精确版本。Query 是 Read 的投影，Task 是该操作的受管理执行实例；不是另一份业务 Handler。

| shape | 实现取得的能力 | 提交/完成所有者 |
|---|---|---|
| Read/Compute | 不可变输入、WorkContext、受限只读服务 | Invocation / Task 完成器 |
| StateEdit | provider 的受限 EditView＋WorkContext | 状态/原子协调器 |
| ExternalEffect | EffectContext、限定目标与发送 permit | 效果协调器记录实际 EffectReport |
| Lifecycle | 对指定对象的 TransitionView/许可 | 生命周期协调器 |

WorkContext 只包含取消、截止时间、预算、追踪及已授资源视图，不是所有服务与状态的后门。EffectContext 不允许把调用者任意填写的 target/resource 直接当作授权结果。

Read 若要在原子组中读取候选，必须注册 `candidate_read` 适配，语义上读取 EditView；普通只读服务默认不兼容原子组。pure_compute 可标记为与状态无关并进入 Atomic，但不能在过程中另行读取活动状态。外部等待和异步派发在 Atomic 中禁止。

实现函数的语义形态固定为：Read/Compute 返回 `Result<R>`；StateEdit 接收 provider 的 `EditView` 并返回 `Result<R>`；Effect 返回携带发送事实的 `EffectReport<R>`；Lifecycle 返回携带前后状态的 `TransitionReport<R>`。后两者不能仅返回一个丢失已发生事实的普通Error。框架把它们归一为A05的Outcome；同步/异步适配共用同一函数语义，不强迫四种shape具有完全相同签名。

### A04.2 注册与重复代码边界

`registrar.read/state_edit/external_effect/lifecycle` 在注册期验证实现 shape、能力、执行器要求和原子 provider。批量操作声明完整实际影响；业务服务直调不能用于偷渡高权限效果。组合复用服务函数，不向模块暴露 Registry 中的裸 Handler。

允许 callback/coroutine 完成适配器用于真正异步 I/O，但它只是对同一实现的调度适配；接受成功需恰好一次框架完成，失败/异常不得留下稍后仍会执行的回调。完成函数重复调用触发检测，不重复发布或释放资源。

注册批次先构建不可见候选目录，统一验证 DAG、重复、版本、Schema、资源、provider；全成功才发布。Ready 后不替换定义。模块代码可信，不提供恶意 native code 沙箱。

### A04.3 Invoke、Submit、Plan

| 入口 | 规则 |
|---|---|
| `invoke(bound,args,options)` | 只有绑定声明 inline-safe 且调用线程合法时可直达；短易失调用不创建 TaskId、任务表或 PlanFrame |
| `submit(bound,owning_args,options)` | 先预留输入/结果/执行配额，必要时持久接受；返回 ExecutionRef |
| `plan.submit(definition,intent,options)` | 计划数据经编译准入；外部通常返回计划执行票据，内部逐节点选择以上两种执行边界 |

线程亲和或长操作不能被客户端 `invoke` 选项强迫在 GUI/IPC 线程执行。Native 阻塞包装器只能用于允许阻塞的应用线程；在 worker/control/domain/DB 内拒绝同池/同序列自等待。系统 `get/cancel/health` 采用短控制入口，不为管理任务再递归创建管理任务。

对要求持久接受的调用，无论外部等待多久，内部都是受管理执行；不能在未持久时先返回 Accepted。外部响应可以先返回票据再查询，但不能把票据当最终 R。

<a id="a05"></a>
## A05｜结果事实、协议响应和任务收尾

### A05.1 三层模型

**传输响应**回答请求是否被理解/接受；**ExecutionPhase**回答工作运行到哪里；**Outcome**回答实际发生什么。三者不互相代替。

`SubmitReply = Rejected | Accepted{execution_ref, acceptance_guarantee}`；`InvokeReply<R> = Rejected | Completed{outcome<R>}`。协议结构错误、认证失败可用 RPC error；已经被识别且发生业务事实的失败在 Outcome 内，不因非零错误而丢失事实。

### A05.2 完成结果变体（本版唯一集合）

| kind | 必要语义 |
|---|---|
| `ReadCompleted` | 读取/计算成功；可返回候选 DataRef，不表示已应用 |
| `StateCommitted` | CommitId、domain、revision、已发布版本、R、证据状态；成功时对应一个状态提交 |
| `EffectResolved` | EffectId、明确的 NotApplied/Applied/PartiallyApplied、成功或失败状态、外部证据及可用结果；无需 CommitId |
| `LifecycleResolved` | 转换身份、before/after/lifecycle generation、成功或失败状态；失败也可能已进入 Failed 等真实状态 |
| `PlanCompleted` | 所有必要节点成功且子执行收尾完毕；exports＋有序步骤事实摘要；不伪造一个全局 CommitId |
| `FailedBeforeApply` | 业务已进入或执行已 Accepted，且可证明没有正式状态/外部效果；两种事实分别表达，包含失败阶段和原因 |
| `CancelledBeforeApply` | 取消先赢且可证明未应用；不同于等待超时 |
| `PartialCompletion` | 组合未完整成功，已有确定效果，且不存在未解决的未知效果；列明成功/失败/取消步骤 |
| `Indeterminate` | 效果或存储提交无法判断；携带已知事实、unknown 边界、CommitId/EffectId、对账方法 |

单个 Effect 的已知部分应用用 EffectResolved；整个 Plan 任一必要效果未知，顶层用 Indeterminate 并保留其他已知提交，不用 PartialCompletion 掩盖未知。Plan 失败且所有先前节点都是无效果读/计算，可以 FailedBeforeApply。成功的纯读 Plan 仍统一用 PlanCompleted。

所有 state edit 成功按一个明确 CommitId 完成；首版即使有效差量为空也形成一次受治理的空 delta commit 并推进 revision，避免缺省 no-op 引发两种语义。以后如优化 no-op，必须显式版本化该契约。

输出 R 在 StateEdit 提交前验证。Effect 实际发送后若 R 校验或序列化失败，仍保留 EffectResolved/Indeterminate 与最小可编码事实；不能返回暗示未执行的普通错误。必要最小回执预算在提交/发送前预留。

### A05.3 事实与证据

`KnownFacts` 只追加，不覆盖已发生事实。Indeterminate 后对账可追加 ResolutionRecord，查询投影得到已解决结果，原未知记录仍留作证据。证据状态限定为 `Volatile | Durable | RequiredRecordFailed | PersistenceUncertain`；不得由多个自由布尔值构造矛盾组合。

读结果无必要持久性时，日志 exporter 失败只附 PostObservationError。状态耐久成功后，额外终态索引写失败不抹掉已提交事实；可由 commit ledger 修复，修复策略必须显式。

### A05.4 ExecutionPhase

正常执行：`Queued → WaitingResources → Running ↔ WaitingChild → Finalizing → Terminal`，可省略不需要的中间阶段。取消是独立意图。可恢复执行可处于 `Suspended`；它不占 worker，但持有必要 durable pins，不能被当作已完成结果回收。Terminal不可原地转回Running；Suspended实例可以在显式resume后继续同一身份。必须终结的纯计算中断用FailedBeforeApply(reason=Interrupted)，未知效果用Indeterminate；对账追加记录而不重开已终结执行。

Finalizing 意味着必要回执记录、发布、回调/资源释放尚未完成。已知提交可以查询，但 `wait(completion=terminal)` 仍等待必要收尾。普通日志 flush 不作为每条执行的必要收尾。

必要记录失败采用有界尝试后明确分流：保留确定事实及 RequiredRecordFailed、封锁相关新写入、发布可查询故障；如所有本地回调已经排空可以 Terminal。仍有 in-process 代码运行，则执行仍未 quiescent，不能 Terminal 后释放依赖；需要隔离时由显式 IsolationOwner 接管其寿命。挂起的未知外部物理动作不等于本地线程仍在运行，两者分别记录。

### A05.5 对外观察版本

受管理执行的查询摘要附带 `observation_version`：当前 `host_incarnation` 内、同一 `ExecutionRef` 的单调观察版本，覆盖公开 phase、进度摘要、已发布事实及证据状态的变更。它不是业务 revision、CommitId 或持久提交序号；重启/恢复后必须连同新宿主世代比较。一次查询返回同一个观察投影，不能拼接互不对应的 phase/事实/版本。

更新执行状态和观察版本由已有执行记录的短同步或串行协议协调，不在锁内发送外部消息。状态提交事实仍须经过 A06 的 publication gate 才进入成功投影；版本到达上限时明确拒绝或隔离，禁止回绕。没有 `ExecutionRef` 的短 Native Invoke 不为观察协议新建执行记录。

<a id="a06"></a>
## A06｜许可、取消、提交和可见性

### A06.1 三种不同保护

ActivityLease 保护运行对象寿命；ResourceLease 保护并发资源；ActionPermit 决定一次授权效果的提交/发送。任一都不能代替另两者。快照读取通常只需 owner/lease，不持有长期阻止提交的 Shared 锁。

ActionPermit 由可信 PolicyEngine 创建，绑定 caller、operation/组摘要、目标、权限世代、生命周期世代、期限与一次性消费状态。检查/消费与撤权在同一个短仲裁机制中确定先后；消费前取消/撤权胜出则不执行，消费后不允许迟到取消抹掉事实。

取消赢不等于此前独立 Plan 步骤被回滚。设备 Guard 在发送附近复核，硬件联锁仍独立。

### A06.2 状态提交协议

```text
Preparing → ReadyToCommit → CommitClaimed → [DurableCommitted] → Published → Finalized
       ↘ Failed/Cancelled      ↘ KnownNotCommitted / Indeterminate
```

| 点 | 决定什么 | 不意味着什么 |
|---|---|---|
| HostAdmission | 接受工作 vs 停止的先后 | 业务一定会成功 |
| CommitClaimed | 许可已消费；之后取消不强制撤回该提交尝试 | DB 或内存提交已成功 |
| DurableCommitted | 同事务状态/回执已落入数据库事实 | 本机内存读者已看到新根 |
| Published | StateRoot、revision、history cursor 一次可见 | 所有日志已经 flush |
| Finalized | 必要记录/回调完成，执行可达 Terminal | 跨设备的所有物理风险已消失 |

先准备所有新 root、history、回执、Outbox 编码及发布材料；进入域提交序列，再核对 base revision、生命周期和许可，完成一次性消费。不得拿到许可后重新运行任意长业务验证。

### A06.3 域序列与 DB writer 的时序

同域保留逻辑 `CommitReservation`，直到 Published 或已明确失败/域隔离。准备阶段并行；最终提交串行。等待 DB 用 continuation，不占着 CPU worker 或内部 mutex；回调返回域序列时检查 reservation_id，迟到/重复完成不能发布旧根。DB writer 不反向同步等待域线程。

DB 队列满在许可消费前尽量预留容量并拒绝；消费后提交失败仍必须返回实际 KnownNotCommitted 或 Indeterminate，不能因为“获准”就 StateCommitted。

根、单调 revision、History 游标及必要版本通过一个 `PublishedState` 拥有型记录一起替换。读者取得同一发布记录，避免读到新 root＋旧 cursor。外部 `execution.get` 经发布 gate，不能从另一 SQL 连接提前把 durable 行当作 Published。

Outbox dispatcher 同样不能早于 Published 交付该本机提交的业务成功事件；重启先安装可信 root，再开放重投。提交后普通观察者在锁外执行。老根和大对象析构移出关键路径。

### A06.4 失败处置

明确未提交且回滚确认：放弃候选、释放 reservation，返回 FailedBeforeApply；事务错误但提交结果不能确定：保留 CommitId，隔离域、复查连接/持久材料，不继续叠加写入；DB 已提交而发布异常：保持 DurableCommitted 事实并隔离，恢复安装，不宣称 rollback。

无 Durable 时，发布就是正式状态可见点，准备/许可失败均不发布。即使内存模式也必须遵守同样的取消、History 和候选规则。

<a id="a07"></a>
## A07｜StateKit 与 AtomicProvider

### A07.1 Provider 契约

CoreContracts 只定义 AtomicProviderKey、AtomicDomainRef、受限运行帧、prepared commit 与提交结果端口。State 是默认实现，不是 Runtime 必选项。简单类型化配置 provider 与通用对象表 provider 实现相同的提交契约。

```text
Domain → PublishedState{revision, immutable_root, history_cursor, lifecycle_generation}
SnapshotHandle → owning PublishedState
EditSession → base_snapshot + WriteSet + affected_constraints
PreparedCommit → prepared_publication + delta + result + audit/outbox_material
```

domain 稳定 ID 不复用；生命周期代数防止关闭/重开后拿旧句柄继续编辑。对外 Atomic 绑定完成后必须具有精确 revision 前置条件；单 StateEdit 可由具体操作声明 ServerCapture 或 RequireExplicitRevision，不能由请求随意关闭冲突检查。

### A07.2 单条与分组共用逻辑

独立 StateEdit 创建一个 EditSession，调用实现后提交；Atomic 创建一个 EditSession，顺序执行多步后提交。受限 EditView 没有 commit/rollback/裸 SQL/通用外部发送接口。

同域候选读取提供 read-your-writes。纯计算只能使用组内已有值或不可变输入，不偷读漂移的“当前选中对象”。每步解析实际对象身份，验证目标、权限和类型。新增候选对象身份在组内稳定；组失败后不会变成正式对象。

### A07.3 原子组范围

wire 子集只有 `call/let/assert`；Call 必须 complete，且为 state_edit/candidate_read/pure_compute。禁止 ticket、await、if、foreach、parallel、嵌套 Atomic、Lifecycle、外部副作用。SDK 可以提前将固定小循环展开为合法步骤，但展开后的全部预算同样计算。

必须同 provider、同 domain。额外独占资源集合需要在组开始前确定并一次获得；如果只能从中间结果推导新的外部锁域，该操作在组模式下拒绝。CPU/内存配额不是另建外部事务域。

一组正常只形成一个 History 单元和一个组级幂等结果。要求 Undo 时全部编辑具备逆向差量；不可撤销操作不谎称可撤销。原子候选进度明确标记未发布。

### A07.4 State 数据结构与约束

默认对象不可变，正文为类型化对象＋codec，不强制 JSON。通用对象表私有采用结构共享容器；小配置根可直接采用不可变 struct。初期域级 revision 冲突检查，保留读/写集但不建设自动 merge 或数据库级谓词锁。

更新 k 项不因容器实现本身复制 N 项；全局业务约束另计。反向引用、索引与对象差量在同一个 PreparedCommit 中准备和发布；删除不能遗漏被引用方。Snapshot 是内存 owner，不打开长期 SQL transaction。

History 使用差量与前后对象/资产引用；Undo/Redo 是新的提交，revision 单调增长。revision 达到上限明确拒绝/隔离，不回绕。单纯读取与空参数查询不产生历史。

<a id="a08"></a>
## A08｜唯一 Plan 格式、类型与执行语义

### A08.1 三层表示与版本

Wire 规范为 `format="ock.plan/1"` 的 JSON 结构。它编译成 Plan IR；可选 DSL/PlanBuilder 都产生同一 wire/IR。Workflow 只是持久运行，不另建表达式语言。

顶层必需字段：`format,durability,on_error,inputs,steps,exports`。`durability=volatile|durable` 表示计划运行能否恢复，不表示整条计划是一个事务；`on_error=stop`。只接受本文节点和注册扩展版本，未知字段默认拒绝。预算属于已认证提交选项和服务器政策，不允许在计划里提高配额。

wire 节点固定为 `call,atomic,await,let,assert,if,foreach,parallel`。**没有 `kind=return`。** 每个 block 用 `exports` 导出；DSL 只允许块末尾 `return`，它是 exports 语法糖。IR 可以有 Return 终结指令，但不是外部语法。首发 CLI 的 `--file/--stdin` 已满足 Shell 提交；可选人类 DSL 不阻塞首发 SDK。

顶层与 block.steps 静态长度初值 1000；不意味着嵌套执行总量可无限扩大。具体 JSON Schema 要从下列规范产生并在 D0/D4 通过正负测试，不复用不一致的旧草案。

### A08.2 节点字段与结果

| 节点 | 必需业务字段（均含 kind/id） | 可选/结果语义 |
|---|---|---|
| call | `op{name,version},args,delivery` | 可有 target/bindings/out；complete 的 out 为 R，ticket 的 out 为内部类型化 ExecutionRef |
| atomic | `preconditions,steps,exports,out`；绑定后必须有 domain | domain 为 provider/id；preconditions.revision 必需在运行前完成绑定；out={values,commit} |
| await | `task:{slot,path},out` | 仅消费类型化 ExecutionRef，等待 terminal 后将成功 R 写 out |
| let | `value:Expr,out` | 不修改外部状态 |
| assert | `predicate:Expr` | 可有 message；失败停止当前块 |
| if | `predicate,then,else,out` | 两分支均为 block；out 为实际分支 exports |
| foreach | `collection:Ref,item_slot,index_slot,max_iterations,body,out` | 串行、有界、集合固定；out 为每次 body.exports 的顺序数组 |
| parallel | `branches,max_concurrency,on_error,out` | on_error 固定 stop_launch_and_drain；out 按分支定义顺序排列 |

OperationKey 为精确 name/version；执行接受时固定契约指纹。输入当前对象、选择集或 domain 不由字符串占位猜测，使用真实查询返回值。`Call` 的外部错误事实被 Plan 收集，不把 Failed 回执伪装成普通 R 给后续步骤。

### A08.3 槽与作用域

每块局部槽单赋值，id 在块内唯一；可读取上层槽和只读 `$input`，首版禁止遮蔽任何可见槽。子块不能修改父槽，只有 exports 创建父块的 out。未声明槽、前向引用、路径类型不匹配和循环依赖编译拒绝；运行期长度/可选字段仍在使用前验证。

内部类型至少区分 BusinessResult、ExecutionRef、AtomicResult、DataRef、BlockExports。形状相同的 JSON 对象不能伪装 ExecutionRef；只有合法 ticket 返回或获授权的执行查询适配器能产生该值。日志/完整 Outcome 不自动成为业务 R。

Call(ticket) 必须有 out，票据必须被 Await 消费或显式导出为允许脱离父执行的执行引用。首版 Plan 不允许任意 detached：缺显式授权寿命策略时，块结束仍持有所有 child 并等待收尾。ExternalEffect 或 StateEdit 的 ticket 不允许在 Atomic 内出现。

If 两分支 exports 键和类型必须一致，缺值使用明确 nullable 类型；不做猜测式 union。ForEach 在进入时固定不可变集合，超出 max_iterations 直接失败而不是截断。Parallel 每个分支独立读输入，只按定义序号导出；资源治理始终生效。失败停止新分支并收尾既有子执行，已提交效果不回滚。

### A08.4 引用和绑定

Ref 为 `{slot,path}`，path 使用 RFC 6901 字符串形式，不接受 URI fragment。[E03] 空字符串指根，`~0` 为 `~`，`~1` 为 `/`；非法转义拒绝。数组索引只能 `0` 或非零开头十进制，禁止 `-`、负数和越界。Missing 不等于 null；普通引用 Missing 失败，`exists(ref)` 对已声明槽中缺路径返回 false。

Bindings 为 `{to,value:Ref}`。Call 的 to 仅允许 `/args/...`、`/target` 或其已声明字段；Atomic 仅允许 `/domain` 或其字段、`/preconditions/revision`。禁止修改 kind/op/version/delivery/intent/caller/permission。同一节点内重复、祖先/后代重叠绑定拒绝；不得用对象键顺序决定覆盖先后。

绑定目标的最终叶字段应在字面量中省略；不允许常量与绑定同时给同一叶赋值（null 也是常量）。父容器必须已存在或整体由一个合法根绑定产生，不自动创造任意路径。完成绑定后再对整个 Args/Target/Preconditions 校验；不是只校验被替换字段。

### A08.5 表达式最小集

Expr 为 literal、Ref 或 operator{arguments}。eq/ne 接受可比较的同类型标量；lt/le/gt/ge 接受同类有限数值；and/or 只接受布尔并短路；not 一元；exists 只接受一个 Ref。不同数值类型不隐式转换，转换须由公开类型契约完成。禁止任意代码、字符串 eval、正则执行器或反射字段赋值。

literal 数据也受相同 Payload 预算。错误要包含 step path、引用 path、期望类型和实际类型，同时脱敏。静态值可编译一次；动态路径必须在使用时检查。

### A08.6 PlanCompiler 与 RunFrame

编译：结构/预算→作用域与引用类型→精确版本/shape→静态资源与原子兼容→常量绑定→IR/常量池/槽布局/依赖指纹。缓存保存结构和编译校验器，不缓存当前授权、新鲜度和安全 Guard。

Runtime 帧保存 PC、控制栈、槽 owner、实际消耗预算、子执行与已知效果；Await 使用事件/continuation 挂起，不占 worker 阻塞。自动化调用同一 InvocationEngine；Atomic 委托 provider，不在解释器里另做事务。

消费总指令、循环展开、分支数、嵌套深度、槽内存、结果输出和总执行期限；短路/重试也计预算。结果槽按活跃分析释放，DurablePlan 需保留的值先形成 durable pin。

### A08.7 Check、Preview 与错误

Check 不执行业务效果，返回已验证项和未决动态检查；domain/资源来自前一步结果时标记 DynamicCheckRequired，直到运行准入验证才允许执行。Preview 仅对声明支持的纯计算/隔离候选可用，绝不真实执行后“尽量回滚”。

plan.on_error=stop：不启动后续节点；收集并收尾在途 child。每个已发生的独立提交保留。完整成功返回 PlanCompleted；失败根据已知/未知效果形成 A05 的结果。一个已接受计划的 check 错误也不能掩盖先前执行事实。

### A08.8 无文档 Atomic 示例

以下 `settings.*` 是测试/示例模块的目标操作，不是声称现有二进制已注册。id 使用示例 provider 自己的稳定文本规则。

```json
{
  "format": "ock.plan/1",
  "durability": "volatile",
  "on_error": "stop",
  "inputs": {"domain": {"provider": "settings", "id": "settings.machine-a"}, "revision": "7"},
  "steps": [
    {
      "kind": "atomic", "id": "edit", "preconditions": {},
      "bindings": [
        {"to": "/domain", "value": {"slot": "$input", "path": "/domain"}},
        {"to": "/preconditions/revision", "value": {"slot": "$input", "path": "/revision"}}
      ],
      "steps": [
        {"kind": "call", "id": "set_a", "op": {"name": "settings.set_limit", "version": "1.0.0"}, "args": {"axis": "x", "limit": 100}, "delivery": "complete", "out": "a"},
        {"kind": "call", "id": "set_b", "op": {"name": "settings.set_limit", "version": "1.0.0"}, "args": {"axis": "y", "limit": 80}, "delivery": "complete", "out": "b"}
      ],
      "exports": {"x": {"slot": "a", "path": ""}, "y": {"slot": "b", "path": ""}},
      "out": "group"
    }
  ],
  "exports": {"edit": {"slot": "group", "path": ""}}
}
```

原子子操作缺显式 target 时仅可继承本组 domain；不能继承 GUI 当前选中域。独立调用相同操作仍要带明确 target。完成后 group 为 AtomicResult，不是单纯对象列表。

### A08.9 Plan IR 语义往返合同（N9）

D4.05 必须提供**测试级、版本化的 IR 语义检查表示**（例如 `ock.plan-ir-test/1`，实际名称由 D0/D4 固定），用于把已编译 IR 投影为无指针、无 allocator、无 registry 内存地址的 neutral representation，再读回并比较语义等价。至少覆盖：节点/opcode、精确 Operation name/version/contract digest、slot 类型与作用域、控制边、bindings、常量、预算、Atomic 边界、exports、delivery 和动态检查标记。

roundtrip 断言的是**编译语义不丢失**，不是冻结 Runtime 私有对象布局。测试表示不得保存进程内 BoundHandle 原值、函数指针、mutex、coroutine frame、容器地址或 allocator 状态；读回时需要按精确 Operation 身份重新解析合法测试绑定。IR 的内部 opcode 编号、结构体布局和二进制编码可以随同一 SDK 开发版本演进，只要编译器/runner 语义合同和测试同步。

DurablePlan 不直接把该测试表示当持久恢复格式。A13/D6 保存的是可重建的 PlanDigest、精确定义身份、PC、控制栈、必要槽/决策/子键等稳定材料；D6.05 需要用 checkpoint codec 与 D4.05 语义快照交叉验证“恢复前后计划语义一致”，但不能由测试 roundtrip 倒推内部 IR ABI 成为产品兼容承诺。


<a id="a09"></a>
## A09｜任务、资源、结构化寿命与公平

### A09.1 接受与回调

Submit 必须先获取执行表、输入 owner、结果/最小回执和完成通知容量。Volatile 接受在内存发布记录后返回；Durable 接受在 A11 的接受事务提交后返回。执行器拒绝/抛出表示未持有工作；inline completion 允许，调度代码不能假设 submit 返回后才回调。

CPU 后端只执行已就绪工作；逻辑排队和历史不推入第三方池。业务时间未知的 Read 也使用 Submit。异步 I/O shape 可以返回后继续完成，但必须由同一执行记录持有生命周期；不可引用调用栈。

B4 端口寿命实施细化见 [Executor/资源 ADR](adr/ADR-b4-executor-resources.md)：可选 ExecutorControlPort 显式排空/关闭；所属 worker 非法等待/关闭拒绝，直接非法析构 fail-fast，超时不 detach 或释放在途 owner。Scheduler 内部 ticket 不替代 ExecutionRef，真实 Submit/Task 仍由 D3.04–D3.07 接入。

B4 的 Started 以当前 attempt_generation 在 Scheduler 仲裁内取得 start claim 为唯一判据，与 deadline expiration 竞争；先过期的 envelope 不进入业务。依赖集合在 entry 发布时冻结，首版仅引用已发布 predecessor，attach/completion 同锁仲裁、adjacency 只 drain 一次。worker_delivery reservation 绑定 envelope 寿命；合规拒绝/异常已释放 envelope，违约仍持有时保留物理额度至真实收尾，详见上述 ADR。

对外接受与底层Executor接受不是同一边界：顶层配额/参数失败可Rejected；一旦执行记录已对外形成Accepted，随后Executor拒绝只能形成该执行的FailedBeforeApply，不能抹掉已接受身份。内部调度先发布可由完成器引用的记录，再提交可能inline完成的回调；外层Accept回复与完成先后均须合法。

### A09.2 调度与资源

Ready 按权重主体队列＋有界优先级；依赖用未完成计数/邻接关系；资源等待按资源索引；deadline 使用定时结构。Scheduler 不扫描终态库。活动队列、历史缓存、订阅和每主体并发分别限额。

ResourceClaim 来自可信 resolver；别名归一为同一槽；Shared/Exclusive 不拆成两个互不冲突的 ID。MultiClaim 聚合校验溢出，一次全获或不持有。Lease 只释放实际获得的数量。冻结拓扑后运行中新资源必须通过注册 resource factory，不允许任意字符串无限建槽。

计算阶段不持有 DB 或域提交许可。内层并行算法使用父 CPU 配额和受管执行器，不各自开启全核线程池。等待 child 前释放 child 必需资源；无法安全释放/恢复的组合在规划时拒绝。优先级不是抢占任意 C++ 的保证。

### A09.3 父子执行

child 创建时绑定 parent、独立身份、owner、权限委托范围和生命周期。默认父取消传播；父 Terminal 前所有 child 必须 terminal，或被可信独立 owner 接管。原子候选不能由未结束 child 继续访问。

系统取消请求返回 Requested/AlreadyClaimed/AlreadyTerminal 等控制结果，不声称一定已停止。父存在任何未知效果按 A05 保留 Indeterminate；不因父失败删除子回执。

### A09.4 deadline 与等待

`execution_deadline` 管工作预算，`wait_timeout` 管客户端等待，`transport_timeout` 管连接。协议传 duration 或明确 UTC deadline，进入 Host 时换算成单调预算；持久格式记录 UTC 限制和使用过的预算，不保存 steady_clock 原始值。

重启后已超过绝对期限的步骤不再新发出；已跨过效果决定点的操作仍需收尾/对账。时钟明显回拨不能使授权/计划期限无限延长，恢复可进入 NeedsReview。stop_token 仅协作，不代表强制中断。

### A09.5 执行枚举与内部观察端口

Runtime 基于已有受管理执行表提供类型化的受授权枚举/观察端口。执行创建时分配当前宿主内唯一、递增且不复用的 `listing_ordinal`；恢复安装的执行在当前世代重新获得序号。它仅供稳定分页顺序，不等于原始创建时间、业务 revision 或持久历史顺序。活跃集合、保留终态和 owner 索引不与 Ready 调度队列混用。

`execution.list` 的协议投影在 Control；Runtime 不包含 RPC、JSON 或连接 SubscriptionId。元数据枚举不遍历大型输入/结果，不为了 list 产生完整 Payload，不枚举没有 ExecutionRef 的短 Invoke，也不把日志扫描当执行索引。恢复时只重建声明支持的有界索引；不要求启动即加载全部长期历史。

内部完成信号/Task wait/Await 是可靠的本地寿命协议；外部 Notification 是允许合并/丢失的观察面。外部订阅满队列、取消或断线均不能吞掉内部完成信号，也不能阻止执行到 Terminal。对外观察依 A17.4–A17.6 执行。

<a id="a10"></a>
## A10｜身份、权限、批准与真实安全边界

VerifiedCaller 由认证适配器构造；外部 principal/role/trusted 字段不能成为事实。有效权限为主体权限、会话委托、模块政策和实际目标规则交集。操作调用、目录、任务/计划查询取消、日志、结果和资产读取均授权。服务内部的自治动作使用明确 ServicePrincipal，不偷借旧用户 Session。

注册期静态目标规则和每步实际目标检查分开；查询结果不是无限扩大后续权限的凭证。选中对象在请求准入时解析为明确集合，后台不读取漂移的选择状态。长期计划必须有明确 delegation 与期限，不能持久化一个永不过期的临时会话 token。

审批证据绑定具体操作/PlanDigest、目标范围、输入版本、权限与执行期限。参数变化、扩大目标、过期或需要重新计算候选时原批准无效。AI 不可通过 approved=true 提权；模型输出中的文件内容和标签只作为数据。

同一 OS 用户的 AI 与人工不天然隔离；若 AI 同时有该用户全部文件/进程权限，就不能仅靠应用会话对抗其读取高权限凭据。对抗边界需不同 OS 身份、沙箱或可信 broker。进程内 native 模块可信；C++ private 与受限 Context 防误用，不承诺恶意代码隔离。

**观察权限补充（N1）**：执行枚举、订阅、发送和退订都受已认证主体与会话委托约束。指定 owner 只提出过滤条件，不产生权限。订阅时验证过滤范围；发送前以当前有效授权世代核验目标与最小字段，撤权后丢弃尚未开始传输的无权数据。不能撤回已经发送的字节；权限变更与发送授权按短仲裁定义先后。跨主体计数、错误信息、游标和退订响应均不得泄露隐藏执行或订阅是否存在。

<a id="a11"></a>
## A11｜Durable、保障与统一存储事务

### A11.1 保障不是一个可任意比较的枚举

记录三个维度：接受记录（Volatile/DurableAccepted）、状态提交（Memory/DurableCommitted）、外部效果能力（NoReconcile/Reconciliable/DeviceDeduplicated）。ExternalReconciliable 不是比 DurableCommitted 更高的通用等级。操作声明最低要求，调用者只能要求更强且实际可提供的能力，不能降级。

计划 durability=durable 要求计划自身和必要子效果可恢复。单独启用 durable 接受不使 volatile State 域变成 durable 状态；这种组合在计划绑定/接受时拒绝，不能在成功回执里伪造恢复保证。

### A11.2 SQLite 与部署范围

首个持久默认为本地 SQLite WAL＋FULL＋foreign_keys=ON，读回实际值；一个活动 Host 持有规范化存储根的 OS 独占，直至排空结束。SQLite 文件锁不能代替整个 Host 的所有权管理。通过含 WAL-reset 修复的正式库构建；官方指出修复在 3.51.3 及以后并有特定回补版本，实际锁定版本仍须验证。[E05]

独立 DB writer 持有写连接和预备语句；短只读连接有时限，检查点/备份有独立工作预算。WAL 只有一个同时写入者，不支持任意网络文件系统；FULL 保障依赖 VFS/OS/硬件正确履约。[E05,E06] 不通过 NORMAL/OFF 获得性能成绩。

### A11.3 存储职责与建议表集

| 组件 | 表族/材料 | 关键约束 |
|---|---|---|
| Durable | store_meta、component_schema、intent_scopes、intents、executions、effect_records、resolutions、pins、outbox | store/restore/主体范围；唯一键；单调水位；可审计事实 |
| StateDurable | state_domains、state_deltas、state_snapshots、history_entries、asset_refs | domain+revision 唯一；commit 与对应 intent 同事务 |
| Workspace 持久桥接 | workspace_projects、workspace_documents、lifecycle_records | 归属、关闭与墓碑，不由 Runtime 建表 |
| DurablePlan | plan_instances、step_intents、plan_checkpoints、slot_records | 稳定子键、精确定义、必要结果 pin |

DDL 由 D5/D6 工作包生成；这里只固定所有权、不变量和事务组合，不给业务 Handler 裸 SQL。未安装组件不创建其表；发现已有未知必需组件材料默认拒绝写启动，可只读诊断。schema 版本管理用于这个新产品未来升级，不承担旧项目兼容。

### A11.4 提交批次

StorageSession 是单一事务所有者。StateDurable 将准备好的 delta、最终 StateCommitted 事实、幂等 outcome、必要审计及 Outbox 编译为一个受信任 CommitBatch。由 DB writer 一个事务写入，不分别调用“保存状态”和“保存成功”两次提交。

接受异步工作：execution/input durable ref＋intent receipt 同事务，完成后再发布调度记录。State 的最终提交包括 StateCommitted 事实，后续任务终态摘要属于可修复派生索引，不能强制再写一遍完整状态。

外部效果：先 durable claim 和稳定 EffectId，获得发送许可后实际调用，再记录 outcome。SQL 事务不包住设备操作。未证明没有发送时进入 Indeterminate，对账前不自动重发。已知部分效果写 EffectResolved，不一律归类成未知。

SQLite 的原子性只覆盖其同一个事务；状态内存发布、文件系统和外部效果必须按本文各自协议协调，不能从数据库能力推出跨系统原子。[E07]

<a id="a12"></a>
## A12｜Intent epoch、规范化与恢复

### A12.1 外部逻辑身份

首次发送前客户端保存 `IntentKey{epoch,nonce}`。namespace 经服务端校验；完整范围是 `StoreId/RestoreGeneration/Principal/namespace/epoch/nonce`，其中存储身份与恢复世代由握手确定而非 caller 任填。纯 Volatile Host 使用自己的 incarnation 作为范围，明确重启后原意图不可自动换成新范围重发。

指纹覆盖精确 Operation/契约版本、归一化 Args、target、前置条件和 PlanDigest，不含 RequestId、TraceId、重试 attempt 或临时 Session。请求同 key 不同指纹冲突；相同 key 返回既有事实或在途引用。查询也检查当前权限。

### A12.2 claim 与 GC 原子算法

在同一个 DB writer transaction 中：先确认调用范围→查询现有 intent→有则核对指纹与权限、返回事实→无则读取 floor/current epoch 和窗口→不允许则 IntentExpired/EpochInvalid→允许则插入唯一 claim 和相关接受材料。未来 epoch 也拒绝，不能绕过保留窗口。

GC 在同一有序写协议中先确认 pins，持久推进 minimumAcceptedEpoch，再删除允许删除的旧回执。崩溃只能留下多保留材料，不能留下水位旧而记录已删的复活窗口。已有在途/未知/被引用记录优先查询，不因低于 floor 删除。

客户端 SDK 在自动重试中永不换 epoch/nonce；新 key 代表新的显式业务意图。水位跨范围独立，重新建立用户 Session 不改变 Principal。这里只保证声明窗口和水位政策，不承诺无限去重历史。

Volatile模式在当前Host incarnation内也使用内存epoch/floor协议：窗口内必要intent/outcome不能被普通终态LRU淘汰。内存不足时拒绝新接受，或按已声明窗口先结束epoch并推进内存拒绝水位再回收；不能删完记录把相同请求当新请求执行。进程重启丢失该保证，客户端不得自动改incarnation继续重试未知效果。

### A12.3 子执行不用伪造新外部 epoch

DurablePlan 使用内部 `StepExecutionKey = parent_execution + static_node_path + branch/iteration_path + logical_occurrence`。transport retry/重发回调不进入键。绑定参数/目标第一次确定后保存其指纹；同一步再次产生不同参数即冲突，不能直接覆盖。

已持久 parent 发放内部 `ChildAdmission`，只准入其固定计划、指定步骤和有效寿命内的子操作，仍做当前授权、版本和目标检查。它独立于普通外部 epoch 窗口；不是向所有请求开放的 skipEpoch 参数。parent 恢复所需 child intent/receipt 持有 durable pin。

### A12.4 稳定编码

采用 `ock.canonical/1` 应用 profile：确定性 CBOR，禁止 indefinite-length 和重复 map 键；文本 UTF-8；map 键只用字符串并按 RFC 8949 core deterministic 的编码字节顺序排序。整数最短编码；字段声明为 float 的值按保持精确值的最短浮点宽度编码，整数与 float 是否等价在 TypeContract 归一化阶段决定；有限值限定，-0 保留，除非字段契约显式规定归一为 +0。缺失与 null 分开。[E08]

使用固定摘要算法 SHA-256；PlanDigest 还覆盖 wire format 和所有精确操作契约指纹。通用 encode_cbor 或 std::hash 不能作为规范化实现。用独立实现字节向量证明排序、整数边界、浮点、默认值与 JSON/Native 等价；无法精确支持的字段拒绝接受 durable key。

**N10 实现前置**：D5.02 的记录/Intent/Plan 指纹实现不得直接假设 jsoncons 普通 CBOR encoder 满足 `ock.canonical/1`。D5.02-a 先建立 feasibility spike，使用固定 typed 值与独立 golden vectors 验证 map 排序、最短有/无符号整数、可精确表示时的最短浮点宽度、`-0.0` 保留/字段归一化、int/float 区分、Missing/null、拒绝 NaN/Infinity、禁止 indefinite-length/重复键及跨两条独立编码路径的字节一致。

spike 只允许三种受审查结论：**A)** jsoncons 直接满足；**B)** jsoncons 加一个薄的受控 canonical policy/visitor 满足；**C)** 为本 profile 的有限标量/array/map 类型实现一个极小专用 encoder。选择 C 也禁止扩展成新的通用 CBOR 框架；解析、普通协议 CBOR（若未来使用）与 canonical fingerprint encoder 的责任必须分开。结论、库版本、测试向量和理由写入 ADR/依赖锁，并成为 D5.02 后续实现的阻塞前置。


### A12.5 恢复策略

先存储独占→结构/预算/摘要/归属/版本链验证→安装 StateRoot 与派生索引→恢复执行查询视图→开放业务与 Outbox。恢复读取 snapshot+delta，不运行历史 Handler。

| 故障点 | 处置 |
|---|---|
| durable accept 前 | 不承诺接受；同键可重新尝试 |
| accept 后执行前 | 保留执行；默认 Suspended；可证明无效果且不恢复的计算终结为Interrupted原因，安全策略可显式继续 |
| 状态 commit 前，确认回滚 | 无发布；确定失败 |
| DB commit 后内存发布前 | 重建 root，衔接原回执，不执行业务 |
| 外部 claim 后发送状态不确定 | 对账，不因未见 outcome 就重发 |
| 外部效果后 outcome 前 | Indeterminate；设备可去重则按其证据处理 |
| outcome 后响应丢失 | 原键读原结果 |
| 必要记录/材料损坏 | 隔离相关写入，保留诊断；不改坏材料凑 Ready |

拒绝依赖缺失的精确定义，不能选最新版本继续。普通结果 hash 是完整性检查，不是对抗修改者的签名。

<a id="a13"></a>
## A13｜DurablePlan、检查点与补偿

易失与持久计划共用 IR、类型规则和解释动作，只有状态存储/推进驱动不同。持久记录 PlanDigest、PC、控制栈、已固定决策、集合快照/索引、必要槽值/DataRef、child key 和收尾状态；不保存 native 指针或 coroutine 栈。

在发出有状态/外部效果步骤前，记录稳定子键、实际绑定 Args/目标与必要决策。子已完成父检查点未落盘时先查子回执；缺明确信息按同一键协议处理，不分配新键。影响后续效果的时间、随机、查询值、If 决策和 ForEach 集合须持久固定。

首版恢复默认 Suspended/NeedsReview；自动继续只能由绑定政策授权安全步骤，仍检查当前权限及期限。自动重试有次数/时间上限，只用于声明可重试的读/计算、去重 StateEdit 或有证据支持的 Effect。补偿是新 Operation 和新子键，可能失败，不能叫数据库回滚。

一个 Workflow 可以包含多个短 Atomic 组，整条流程不保证回滚。父结果 pins、child receipt 与资产 refs 在 checkpoint 安全推进后再释放。可靠 Outbox 至少一次重投，消费者按 EventId 去重；重投事件不等于重新发命令，事件本身不赋予设备动作权限。

<a id="a14"></a>
## A14｜资产、预览、备份与显式恢复世代

资产写入顺序：受控临时文件→校验与部署要求的 flush→不可覆盖的原子发布→状态引用提交。允许无引用孤儿，不允许正式引用缺失资产。公开 DataRef 是身份/内容/长度/类型与访问策略，不是任意文件路径；实际打开验证句柄、根目录、reparse point/symlink 逃逸及主体。[B3 §11]

PreparedChangeRef 保存候选 root/WriteSet、基准 revision、输入依赖指纹、实际操作清单和范围、预算、owner、过期时间。它不是跨对话持锁事务。apply 时完整重验当前许可和依赖，只应用已算好的候选；版本/目标变化拒绝，不静默重算并沿用旧批准。候选 single-consumer claim 防止不同意图同时重复应用；相同 IntentKey 查原回执。

GC 根包括当前状态、History、快照、预览、在途提交、task 输入/结果、Plan slots/checkpoint、child receipts、备份 manifest 与活动读租约。回收先更新可信引用，再释放材料；持久执行所需资产在 durable accept 前必须已有耐久引用，不能只保存本机临时地址。

备份通过 SQLite Backup API 或受控一致窗口，manifest 记录需要的资产并在备份期间 pin。不能仅复制活跃 .db 文件。普通重启保持 StoreId 与 RestoreGeneration；**显式恢复旧备份**生成新 RestoreGeneration，撤销旧运行句柄/会话授权、封住自动效果重放，待外部历史对账。恢复后找不到的备份之后动作不能判作未发生。

无法识别的文件级恶意回滚不在单个可回滚数据库保证内；需防此攻击时必须使用备份之外的可信单调记录。恢复/clone 工具必须走显式入口，不能鼓励手工覆盖在线数据库。

<a id="a15"></a>
## A15｜事件、日志、审计和观测

### A15.1 四类通道

| 通道 | 承诺 | 满载/失败策略 |
|---|---|---|
| DomainEvent | 已提交事实；缺 Durable 时只为本进程易失交付 | 需要可靠时使用同事务 Outbox；不可当普通 UI 合并 |
| Notification/Telemetry | UI、进度、缓存提示 | 有界 coalesce/drop/reject，sequence/gap 明确 |
| Log/Trace/Metric | 诊断与性能，不改变业务提交 | 普通日志可丢弃并计数；不可递归无限记日志 |
| RequiredAudit/Receipt | 操作声明必需的执行证据 | 动作前失败则阻止；动作后失败保留实际效果及故障 |

### A15.2 回调、队列与寿命

同步 observer 只用于显式批准的短内部用途；UI、RPC、文件日志默认为有界异步。内部锁下不调用外部订阅者。订阅按 event/target 索引，不每条消息复制全目录和 Payload。普通日志队列不得阻塞取消、提交发布或控制入口；必要审计不依赖这个可丢队列。

Trace 以 Operation/Plan 为根，步骤记录有界；算法内循环不逐点建 span。Metrics 标签限基数，不用 TaskId/ObjectId 作默认标签。秘密、token、大模型和完整几何参数默认不入日志；错误信息按调用者权限脱敏。日志与任务结果读取同样授权。

Host 启停需要自己的 flush/drain 顺序，但不要求每个短调用都同步刷日志。Outbox 的 publication gate 按 A06，consumer 必须去重，游标绑定 store/restore/stream generation，不把世代改变误解为 sequence 倒退。

### A15.3 外部通知不是第二份状态真相

`notifications.event` 仅投影已有执行观察信息：`execution.progress`、`execution.phase`、`execution.fact`。它不是 DomainEvent、任务结果存储、持久订阅或业务 Handler 触发器；候选进度仍标记未发布，Finalizing 不伪装 Terminal。客户端需要准确状态时使用 `execution.get/wait`，需要结果时使用 `result.read`。

队列按连接/订阅/主体及全局分别计量；进度可按 `(subscription,execution_ref,topic)` 合并，phase/fact 也只保留提示而不承诺完整状态跃迁史。丢弃/合并由序号和 gap 显示，不能阻塞计算、取消或提交。通知没有持久 ACK/replay；可靠业务事件仍是 A13 的独立 Outbox 语义。

订阅持有连接级注册句柄和最小观察元数据，不永久 pin 执行输入、结果、History 或资产。执行回收后 `get` 可以按既定结果保留政策返回 NotFound/Expired；订阅不是延长结果保留的隐式机制。精确方法、建立时序和清理规则见 A17.4–A17.6。

<a id="a16"></a>
## A16｜Host、模块、配置与故障隔离

### A16.1 启动

`Constructed → Configuring → Validating → Recovering → Starting → Ready`。依次确认模块 DAG/版本→候选注册批次→类型与执行 shape→预算/资源/provider→必要存储独占与恢复→模块初始化和 executor 就绪→冻结服务与定义→开放业务。

启动失败按实际成功步骤逆序清理，保存首要错误和清理错误；不得因为某模块忽略注册返回值就 Ready。恢复前可开放独立的只读诊断面，不开放业务写。模块启动后创建任务也必须等到合法内部准入，不依赖“尚未对外所以随便操作”。

ModuleManifest 包含提供操作、服务、依赖、配置、资源要求；ModuleContext 是最小窄服务，不是整个产品 Host。初始化绑定稳定依赖，热路径不不断 ServiceLocator.resolve。模块注册冻结不阻止会话和业务对象动态变化。

### A16.2 停止

`Ready → StopAccepting → CancelOrFinish → Finalize → DrainExecutors → StopModules/Observers → ReleaseStorage → Stopped`。StopAccepting 与 HostAdmission 在同一短仲裁定义先后；已接受工作按注册策略完成或取消，不能借“内部”身份启动无界新业务链。

Draining 保留只读执行状态、执行枚举、已有订阅的有界发送/退订、取消、对账及管理诊断控制预算；新订阅明确返回 HostDraining，不扩大停止期间的观察资源。现有执行的最终提交路径需要其原 admission token；不得借此接受新的顶层业务。普通 get/cancel 也不能在对象销毁后访问借用指针。

停止等待超时返回非 quiescent 清单，所有依赖仍存活；不等于停止成功。析构要求此前 shutdown 完成，或执行最终安全排空；禁止 worker 自毁 Host、detach 后析构依赖。无法保证不变量时明确隔离/终止，不继续运行损坏状态。

### A16.3 配置与隔离

TOML 是默认配置编码，经 TypeContract/Schema 校验形成不可变快照和指纹。运行可更新项白名单化（如日志级别），由系统 Operation 修改；资源拓扑、存储耐久策略和操作定义不任意热替换。秘密来自独立 credentials 端口，不写入计划常量/持久输入。

DeviceHost/WorkerHost 仅用于真实故障/信任边界，不给所有模块强加 IPC。协议保留身份、取消和事实；终止进程不能保证物理设备停止，需设备 watchdog/联锁。UI 不在主线程执行无界 CPU 操作；OCCT/驱动线程亲和由领域 executor 声明。

**产品 UI 单一业务入口（N7）**：GUI 是同一 Operation 面的进程内消费者。凡是会修改 State、触发 ExternalEffect 或执行 Lifecycle 转换的用户动作，必须经过与 CLI/Plan/AI 相同的 OperationDefinition、目标解析、权限、资源、许可和 Outcome 语义；不得提供 UI 专用“直接改 Document/State/设备”的旁路。UI 可以为渲染、拾取、局部视图计算和纯展示直接读取已授权的不可变 Snapshot/类型化只读服务，不要求每帧渲染、相机移动或局部缓存访问都进入 Operation。业务状态变化通过 Notification 或显式 Read Operation 刷新，不能让 UI 内部可变模型成为第二个状态真相。


<a id="a17"></a>
## A17｜本地协议、CLI 与 Shell

### A17.1 分帧和版本

本地默认 Win32 byte-mode Named Pipe：显式 DACL、仅本地客户端、受控实例名和服务端身份确认；Asio 处理异步字节流，创建与安全属性由 Win32 adapter 实现。默认 DACL 不是本产品安全策略。[E09]

B3 实施接点见 `docs/adr/ADR-b3-pipe-start.md`：观察授权只在真实首字节完成时报告 Started，待完成则 Unknown/关闭，后续帧体沿 Asio 发送。目录装配通过 HostSession 的只读 `catalog_context()` 取得同一 Host 已发布 BindingPort 与该会话 const PolicyAuthority，不复制注册真相、不增加 Runtime 对 Dynamic 的依赖。

私有帧 v1 固定 12 字节：4 字节 magic `OCK1`、2 字节大端 framing_version=1、2 字节 flags=0、4 字节大端 payload_length；首版只接受 flags=0，无压缩。检查 magic/version/长度后读取 UTF-8 JSON。报文为单条 JSON-RPC 2.0 对象；首版不接受顶层 RPC batch 数组。支持分片/部分读写，异常帧关闭连接并留下有界诊断。MCP 端使用其协议传输，不能套此私有帧。

首条握手 `host.hello` 返回 application_id、instance_id、host_incarnation、StoreId/RestoreGeneration（有则）、API/Plan 版本、已装能力、当前预算和 dedup epoch 摘要。版本不兼容拒绝，不静默回退。请求 id 为字符串；有业务效果的方法禁止无响应 notification。进度 notification 仅依 A17.4 的订阅协议发出。握手同时返回实际启用的 `supported_methods`、`notification_protocol`（启用时为 `ock.notifications/1`）及观察预算；未实现/未装配的观察方法不宣称可用。文档 v3.3 不自动将 OCK1、ock.plan/1 或 SDK 版本改成 3.3。

JSON-RPC 仅提供调用封装，不定义原子性、任务所有权或取消，本文另行规定这些语义。[E01]

### A17.2 稳定方法面

| 方法组 | 语义 |
|---|---|
| `capabilities.search/describe` | 权限过滤、分页、精确版本、契约/文档摘要 |
| `operation.invoke/submit` | 指定 op、args、target/options；修改需 IntentKey |
| `plan.check/preview/submit` | 已安装 Automation 才出现；不强迫 Control 链接解释器 |
| `execution.get/wait/cancel` | 所有者/委托检查；get 返回观察版本；wait 默认等待 Terminal，超时不修改执行 |
| `execution.list` | 按 owner/phase 过滤、有界 keyset 分页；仅受管理且当前有权读取的执行 |
| `notifications.subscribe` / `notifications.unsubscribe` | 有响应的连接级观察管理；不创建 Task，不需要业务 IntentKey |
| `notifications.event`（服务端推送） | 无 request id 的服务端 JSON-RPC notification；只提示变化，不承诺补齐 |
| `intent.resolve` | 响应丢失后按原逻辑键查事实或在途执行 |
| `result.read` | 受限投影、分页、DataRef 读取；游标绑定快照/世代 |
| `logs.read/follow`、`health.inspect` | 独立预算；不产生递归任务 |

invoke 不适合外部线程的操作返回 SubmitRequired 或由明确请求选项转成 Submit，不偷偷无限阻塞。R 过大返回 DataRef。`Accepted` 的结果码不表示后台工作成功。

### A17.3 CLI 契约

客户端保存/加载 Intent 文件，内容含服务端范围、epoch、nonce、逻辑请求指纹；相同文件改参冲突，禁止自动覆盖为新意图。第一发送前原子写入本地意图文件。凭据不混入其中。首次握手失败或不兼容不得随机重新生成域/epoch 后盲重试。

```powershell
ock capabilities describe settings.set_limit --version 1.0.0 --json
ock plan check --file edit.plan.json --json
ock plan submit --file edit.plan.json --intent-file edit.intent.json --json
ock execution wait <execution-id> --timeout-ms 5000 --json
ock execution cancel <execution-id> --json
ock execution list --owner self --phase nonterminal --page-size 50 --json
ock execution watch <execution-id> --jsonl
```

Shell `--stdin` 与 `--file` 输入同一 UTF-8 wire plan；Windows PowerShell 编码必须独立测试，默认推荐 --file。可选 `ock shell` 只为同一格式的前端，不支持任意子进程、shell expansion 或脚本 import。

JSON 模式 stdout 只有一份完整机器结果，诊断 stderr；follow/watch 模式显式 JSONL。watch 订阅后读取快照，按 A17.5 合并版本；需要准确终态时执行 wait/get，并在退出时尽力退订，断线也会释放订阅。watch/退订本身不取消任务。退出码固定：0 成功获得 Accepted 或成功终态；2 用法/参数；3 准入/权限/缺能力拒绝；4 确定业务失败/部分完成；5 等待超时；6 传输失败；7 效果未知；8 用户等待中断；9 执行取消且未应用。CLI Ctrl+C 默认只中断等待，只有 `--cancel-on-interrupt` 才发送 cancel。

<a id="a17-observe"></a>
### A17.4 最小订阅协议 `ock.notifications/1`

**请求面只有两个方法；推送不是第三个客户端请求方法。** `subscribe/unsubscribe` 必须携带 RPC request id；改变的是连接级观察资源，不是业务状态，不要求 IntentKey。客户端不得发送 `notifications.event` 驱动服务器状态；错误方向不执行业务，按协议错误关闭或记录有界诊断，且不向无 id 的 notification 发送响应。[E01]

| 项目 | 规范字段和行为 |
|---|---|
| subscribe 请求 | `filter` 必须二选一：`{executions:[ExecutionRef,...]}` 或 `{owner:"self"\|PrincipalRef}`；`topics` 是非空去重集合；可选 `min_interval_ms` |
| 允许 topic | `execution.progress`、`execution.phase`、`execution.fact`；未知 topic 拒绝，不默默降级 |
| filter 验证 | execution ref 使用 A04/A09 的同一个正式 DTO；不另造身份编码。列表去重、有界；逐一验证有权读取。未知/无权目标使用不可枚举存在性的失败响应，整项订阅不部分生效 |
| owner 过滤 | 默认用 `self`，解析为当前 Principal；其他 owner 必须具有显式管理/委托权限，且每次输出仍按实际目标权限过滤。不支持任意表达式或通配全系统 |
| subscribe 结果 | `subscription_id`、`stream_generation`、`host_incarnation`、规范化后的 filter/topics、`effective_min_interval_ms`、`first_sequence:"1"`、`replay_supported:false`、生效容量预算 |
| unsubscribe 请求/结果 | 请求 `subscription_id,stream_generation`；只在当前连接拥有的活动订阅中查找，返回 `{removed:true\|false}`。重复、已移除、不属于本连接、旧世代均可返回 false，不改变其他连接，不暴露其他订阅 |
| 连接寿命 | 断线、会话失效或 Host 停止最终清理时释放订阅；不影响执行寿命。重连创建新订阅/stream_generation，不复用旧句柄或声称重放旧进度 |
| 时间与限速 | interval 只影响 progress 的合并频率，phase/fact 提示可立即排入有界队列；服务器将过小请求提高到政策下限。没有即时交付或硬实时承诺 |

服务端通知格式为 JSON-RPC 2.0 无 id 消息：method=`notifications.event`，params 包含 `subscription_id,stream_generation,host_incarnation,sequence,topic,execution_ref,observation_version,gap,data`。所有 64 位计数使用十进制字符串；身份沿用现有契约。`data` 只承载该 topic 的受权小型观察摘要，不默认带完整 Args、Outcome 正文或资产。需要完整事实或结果时查询。

`sequence` 是订阅内序号，从 1 开始，**在通过初步授权/过滤后、合并或丢弃前**递增；不按全系统隐藏事件计数。`gap=true` 表示自上次发送以来有丢弃/合并，序号不连续也要求重新读取快照。消息必须按其 sequence 顺序发送；合并后的记录移到正确顺序位置，不能替换队首内容后发送一个倒序编号。序号不回绕，耗尽则关闭订阅并要求重建。

`observation_version` 是 A05.5 的每执行观察版本，不与 sequence 混用；get、list 条目及通知中同执行的版本可在同宿主世代比较。外部可能永远收不到最后一个提示，因此序号/gap 不构成消息必达承诺；`execution.wait` 始终保留准确的本地终态等待能力。

订阅不自动附带全系统初始快照。指定已 Terminal 执行仍可成功订阅，但不会补发历史；客户端随后 get/wait 能得到已保留结果。owner 订阅建立前已有的执行由 execution.list 获取，通知只覆盖建立后被选中的变化。

### A17.5 建立竞态、背压、撤权与客户端算法

订阅建立使用一个短观察注册屏障：验证配额和权限→安装处于 pending-ack 的监听→将 subscribe 成功响应放入该连接的发送序列→允许该订阅通知发送。屏障期间变化进入有界 pending 区，允许合并/丢弃但必须标记 gap。**该订阅的成功响应必须先于它的事件帧**；失败时不遗留活动监听。subscribe 响应丢失时客户端关闭该连接以释放未知订阅，再重连，不无界重试制造泄漏。

客户端顺序固定为：`subscribe 成功 → execution.get（或 owner 的 execution.list）→ 合并此后通知`。订阅成功后的通知可先缓存在客户端有界区；收到快照后忽略同世代且版本不更新的通知。gap、重连、世代变化、客户端本地丢弃或需要准确终态时重新 get/wait。默认不采用 `get → subscribe`，避免两者之间执行结束的观察空窗。服务器普通任务完成不依赖这个有损协议，A09 的内部完成通知单独可靠。

退订先在同一短注册协议中封住新排入，再移除尚未开始传输的消息并回收句柄；已经进入 OS 传输的字节不能撤回。客户端在本地退订后丢弃对应世代的迟到帧。撤权在发送前生效，尚未发送的无权数据丢弃；已授权并开始发送的帧按明确先后处理，不承诺撤回已经发送的秘密。权限检查使用当前世代，可缓存静态规则但不能永远缓存允许。

每连接/主体/全局计量订阅数量与队列内存。队列满时 coalesce/drop 并记录 gap；持续不读且超过受控发送期限的连接关闭。满载绝不阻止其他连接的 cancel/get、任务收尾或状态提交。**分队列不消除同一字节流已开始大帧的队头阻塞**：通知单帧有小上限，写调度优先控制应答；需要可靠及时控制的客户端默认使用独立观察连接和控制连接，两者分别认证。这个连接分离不要求新建第二 Host。

没有 ACK、持久 replay、无限历史缓冲、无限结果 pin 或隐含 durable 订阅；也不新增要求每条 Native Invoke 都产生通知的机制。

### A17.6 `execution.list` 的分页与保留合同

实施细化（B2 收口，2026-09-09）：固定 v1 payload 的 view 与 MAC 的可信 connection/delegation 附加上下文共同绑定完整授权视图，具体编码见 `docs/contracts/cursor-v1.md` 与 `docs/adr/ADR-b2-closure-boundaries.md`。生产校验不允许回退到缺少会话绑定的基础 codec profile；翻页只推进扫描位置，保留首次 issued/expires，不续命。此细化不增加服务端 cursor 对象或权限能力。

请求字段：`owner`（缺省 self）、`phase_set`（`nonterminal|terminal|all`，缺省 nonterminal）、`page_size`、可选不透明 `cursor`。Nonterminal 包含 Queued、WaitingResources、Running、WaitingChild、Finalizing 和 Suspended；Terminal 按 A05.4 定义。仅返回有 ExecutionRef 的受管理执行，短 Native Invoke 不被包装为可列举任务。

响应包含 `items`、可选 `next_cursor`、`consistency:"live_keyset"`、`host_incarnation` 和 `retention_scope`。每项只含 execution ref、操作身份、允许公开的 owner/parent、phase、观察版本及小型进度/事实摘要；不取出输入/结果大正文。终态只覆盖当前 Profile 的已保留结果，完整审计历史不是该方法承诺；过期记录遵守 A12 的 Intent 与 pins 政策。

按当前宿主 `listing_ordinal` 降序分页。第一页固定 `upper_ordinal`，后续继续严格小于上一扫描位置；新接受/新恢复条目在下一次新列表请求出现。排序键在该宿主内不变，不使用会变化的 phase/progress 作游标。状态和权限仍按每页读取时判断，因此这是**非一致实时分页**：已翻过条目的状态之后变化可能要重开列表才能看到；不能宣称全局快照或精确总数。

cursor 绑定协议版本、host_incarnation、可用时的 StoreId/RestoreGeneration、VerifiedCaller 及委托视图、规范化过滤、排序/上界/扫描位置和过期时间。**`ock.execution.list/1` 首版只允许无状态 opaque cursor**：服务器把上述字段按固定 canonical 编码形成 payload，使用当前 Host incarnation 的随机秘密做 keyed MAC（首版默认 HMAC-SHA-256；具体 envelope/secret 生命周期在 D0.04/D2.04 固定），再以有界 base64url/等价文本封装返回。服务端不为每个 list cursor 保存对象、pin 或句柄，因此不存在“并发 cursor 句柄池”这一隐式资源；token 长度和 TTL 仍受 A21.1 预算。

cursor 是完整性保护的分页状态，不提供机密性，也不是权限票据；payload 不放 token/秘密或调用者原本无权知道的信息，每页仍重验当前权限。MAC 错误、未知算法/版本、过滤被修改、主体不符、host/restore 世代改变、过期或受约束授权视图失效时返回明确 CursorInvalid/Expired，不静默从第一页重启。Host 重启后旧 incarnation 的 MAC secret 不再接受旧 cursor；这不改变 ExecutionRef/Intent 的业务身份。无权 owner 查询不能泄露总数。CLI的`--phase`映射为`phase_set`，`--owner`映射为owner，不另定状态集合。

如果以后某种查询确实需要服务端 stateful cursor（例如固定资产快照 reader），必须使用独立方法/协议版本并在 A21 类预算中明确单连接/主体/Host 数量、实际内存、pin 和清理政策；不得把该实现选项偷偷重新塞回 `execution.list` v1。

使用 owner/状态索引及服务端扫描预算，不线性扫描无限历史；如果过滤后本页没有可见匹配但扫描预算已到，可以返回空 items 和继续前进的 next_cursor。next_cursor 表示候选扫描未结束，不保证下一页一定非空；禁止游标不前进导致无限循环。该规则与需要一致快照的 `result.read` 分页不同，不共用含混的“所有 cursor 都一致”承诺。

### A17.7 观察面必须通过的合同场景

订阅前已结束、建立时刚结束、确认前突发进度、get 与通知乱序、合并/全丢且无后续消息、重复退订、退订在途、断线/重连/Host 重启、撤权时队列残留、猜其他订阅/执行ID、慢读导致单流阻塞、达到配额、Finalizing 与 Terminal 区分、分页间状态变化/删除/新增、cursor 篡改/过期/越权、有限扫描空页推进，均进入 T19/T20 及相关 T04/T06/T12/T22/T23 子用例。

D0.04-b 固定合同和 Schema；D2.04 实现协议生命周期与帧级测试，但此时没有真实任务不得宣称完整观察已完成；D3.04 提供执行投影/索引，D3.07 完成真实多进程 subscribe/list/watch，D7.05 做结果投影和高压力集成，不再首次补协议。

<a id="a18"></a>
## A18｜AI 自描述、预览与效率策略

每个操作的 TypeContract＋Policy＋Docs＋测试示例是一份源定义，生成 CLI help、Schema、命令卡、编辑器和模型工具投影。当前目录按 installed/visible/eligible 区分，目录指纹只用于缓存，不是执行许可。实时 ID、修订、设备状态必须通过查询获得，不能固化在提示词。

推荐选择：已有批量操作优先；多个不同编辑必须一起生效时 Atomic；查询/计算/等待/多个提交使用局部 Plan；需要新语义判断时只返回摘要给 AI。模型不应逐刀路点调用，也不应被迫预先猜测所有分支。

命令卡必须包括用途/反例、单位、坐标、绝对/相对、影响范围、真实 ID 来源、原子资格、取消/重试、耐久能力、结果阶段和错误修复。天然幂等、框架去重和设备去重分开。模型工具可以按需搜索/加载，函数 Schema 只帮助结构正确，不能代替业务/权限验证。[E10]

MCP/特定模型 adapter 根据协商版本和实际能力导出；不能为兼容模型的 Schema 子集而降低服务器校验。低权限模型不会看到未装模块假能力。资料中的标签、用户文件和工具输出视为数据，不能作为执行策略指令。

Check、Preview、Approve、Submit 四阶段按 A08/A10/A14；PreparedChange 有效时提交可避免算法重算，但必须复验依赖。审批界面展示实际计划摘要与目标范围，不只显示 AI 自写的“安全说明”。

AI 评价使用固定任务集和独立实测：成功率、非法计划拒绝、修复次数、往返数、token、业务耗时、要求人工判断时是否停止。无需商业模型也必须通过 deterministic client 合同测试；模型测试有权限与环境条件，不得把未执行项计为通过。

**持续观察（N1）**：AI/脚本通过 execution.list 恢复当前可见执行清单，通过 subscribe→get→有界通知了解进展，必要时 wait 获得精确终态；不要求模型不断主动轮询或从日志推断成功。通知可由客户端程序消费并只向模型提供需要的新摘要，不把每次进度更新都变成模型工具往返。MCP/模型适配只能按该协议支持的观察能力投影，不能把私有 `notifications.event` 当成未经协商的 MCP 方法。

**状态查询边界（N8）**：Core/Control 首版不提供一个通用、无类型、可遍历任意业务状态的 `state.inspect/query` RPC。实时状态、revision、设备/文档/设置等事实由对应领域注册的 **Read Operation**（例如 `settings.inspect`、`workspace.document.inspect`、`machine.status.read`）公开，并自动进入同一 Catalog/Schema/权限/结果体系。这样 AI 仍能按需查询事实，但不会形成第二套绕开 Operation 的路径字符串/动态字典状态 API。若未来确有跨领域统一查询语言需求，必须独立 ADR、预算和权限模型，不得以内核调试接口悄悄演化。

**人类交互定位（N8）**：桌面产品以 GUI 为主要人工前端；CLI/JSON Plan/Client SDK 是稳定自动化与集成接口。可选 `ock shell`、领域 DSL/REPL 只能编译为同一 `ock.plan/1`/Plan IR，不建立第二执行器、第二事务或第二权限语义；其缺失不阻塞首个正式 SDK/Workspace 产品发布。高级用户是否需要 DSL 由实际产品需求决定，而不是“命令驱动内核”这一架构自动要求。

<a id="a19"></a>
## A19｜版本、规范化数据与演进

### A19.1 契约与数据版本

Operation、类型/codec、Plan wire、RPC API、持久格式、目录文档分别版本化。可靠执行锁定精确 Operation 版本与契约指纹；同版本不可原地改语义。Docs 纯措辞变化可独立 docs_digest；改变参数意义、默认值、effects、资源、取消或重试政策必须改契约版本/指纹。

运行实例持久的是稳定身份和已编译计划的可重建数据，不持久 registry handle、指针、mutex、coroutine 栈或 allocator 地址。依赖版本缺失默认 UnsupportedDefinition，不能恢复时自动选新版。首版不提供兼容解析的隐式升级；将来显式 compatibility resolver 需独立 ADR 和测试。

本文件未要求旧项目兼容。新产品将来的数据升级必须：备份/校验→组件版本检查→单事务/可恢复阶段升级→旧/新读取边界明确；未经检查的降级拒绝。任何源码/依赖变更不能偷偷改变 canonical profile 字节。

<a id="a19-sdk"></a>
### A19.2 SDK 版本域与 Stable API

架构文档 v3.3、SDK `MAJOR.MINOR.PATCH`、Operation 版本、RPC/Plan/存储版本分别演进；第一个正式 SDK 可以为 1.0.0，不能因文档为 3.3 就暗示 SDK 已发布 3.3。开发期使用 0.x，公共变更仍需 ADR、契约与测试同步，但不承担尚未发布的永久兼容承诺。

1.0 冻结的是**稳定 API 范围与兼容规则**，不是禁止添加公开头。`sdk_api_manifest` 记录各组件公开头、导出 target、入口命名空间、公共宏/选项、可用特性和 stable/experimental/detail 分类。Stable 集合不得依赖 detail/experimental 才能完成正式示例；实验性接口及其风险明确披露，不能事后才将已承诺接口改称内部。

采用 SemVer 的公共 API 规则：[E13] Patch 用于兼容修复；Minor 用于经验证的兼容扩展和弃用声明；Major 用于不兼容源码/契约变化。新增重载、默认参数变化、模板约束、noexcept/异常行为、所有权/线程安全契约、导出 target 或 PUBLIC 编译条件变化都需评估，不能因“只增加声明”就假设源码兼容。旧 SDK 消费者无修改地重新编译、链接并运行是主要承诺，不是跨任意编译器/CRT 的二进制兼容。

弃用项在至少一个已发布 Minor 中给出替代和说明，只在后续 Major 移除；紧急安全调整仍需明确版本与公告，不能静默破坏合同。SDK 升级不自动允许改变同版本 Operation、Plan、canonical 或持久数据含义。

### A19.3 三层公共表面门禁

| 门禁 | 检查什么 | 不能替代什么 |
|---|---|---|
| 公开表面快照 | 头路径、组件/target、包含关系、PUBLIC 宏/编译要求及导出入口的受审查变化 | 单纯文件名/整文件 hash 看不到源码兼容语义 |
| 关键声明与行为审查 | 签名、模板、默认参数、布局使用约束、异常/所有权/线程合同与版本决定 | 文本 diff 不是完整 C++ 兼容性证明；可选AST工具也不能代替行为测试 |
| 冻结消费者 | 将上一受支持正式版本的固定消费工程和测试原样对新 SDK 编译、链接、运行 | 不得把消费者与SDK一起改完再说旧调用兼容；本层不自动证明ABI兼容 |

快照中批准的增加可以通过，意外增加/删除/可见性改变必须阻断并审查，不自动覆盖 golden。第一次正式发布没有上一正式版本时，明确记录 previous-release 基准不存在，建立本次冻结样本；不能虚报旧版本兼容测试通过。从已有0.x候选到1.0可保留开发对照，但不将其误称发布兼容承诺。

### A19.4 构建组合与版本检测

D0.02 固定候选公共集合、SDK_VERSION 与预发布规则；D1 起构建公开头独立包含与安装消费者；D8.01/D8.06 验证 manifest、关键声明差异、冻结消费者和支持矩阵。SDK 明确报告工具链、标准模式、CRT、expected 后端、必要编译选项及组件版本；不允许一个构建中混用不兼容后端。

所有公开稳定源码依赖如实导出。语义化版本不是二进制ABI保证；没有专项ABI范围、工具链条件和测试时，不承诺用新动态库直接替换旧二进制。该政策只约束新 SDK 未来版本，不引入旧 LaserCNC API/数据迁移任务。

<a id="a20"></a>
## A20｜实现选型、支持矩阵和发布边界

| 能力 | 默认 | 固定版本前必须验证 |
|---|---|---|
| Result | tl::expected、C++20 | move-only/void/错误传播、异常/ABI一致、许可证 |
| Data/Schema/CBOR | jsoncons | 预算前置、重复键、零双DOM、Schema并发、canonical向量 |
| CPU executor | BS::thread_pool | inline/异步回调、拒绝不留工作、排空/自等待 |
| I/O | standalone Asio | Named Pipe真实部分读写、断线、控制通道满载 |
| 结构共享 | State 私有 immer | MSVC构建、并发不可变、transient冻结、释放峰值 |
| 普通日志 | spdlog | 满队列政策、异常隔离、退出flush顺序 |
| 持久化 | SQLite WAL/FULL | 已知修复、FULL读回、单Host、故障/备份/检查点 |
| 配置 | toml11 | 精确数值/不支持类型拒绝、secret脱敏 |
| CLI | CLI11 | 参数转义、UTF-8、stdin/file、退出码 |
| 测试 | Catch2/CTest、Google Benchmark | 断言/测试发现真实有效、逐请求分位采样 |

不是要求所有第三方不能出现在任何公开模板中；expected 是明确公开依赖，具体 jsoncons/immer/SQLite/Asio 不泄漏核心类型。无需完整 CTK、CppMicroServices、Python VM、第二 Taskflow、全套反射/遥测/分布式服务。

**版本锁定是 D0 的输出，不在未经编译时编造已经认证的版本号。** `dependencies.lock` 必须记录 commit/hash、编译开关、许可证和供应来源；SQLite 必须含上述 WAL-reset 修复。开发准备可候选试编，D1 起使用锁定组合，之后升级单独提交与回归。

安装导出组件真实 PUBLIC/最终静态链接依赖，不以 PRIVATE 字样使依赖凭空消失。不导出本机绝对路径；离线使用预提供依赖；安装树移到不同根后，独立消费者仍能 find_package。CMake 官方导出机制支持该边界，具体包需自己验证。[E02]

<a id="a21"></a>
## A21｜预算、容量和性能门禁

### A21.1 开发 Profile 初值

以下是本版可执行起始配置，不是生产认证；服务器可进一步收紧。扩大限制必须有容量测试和配置审计，调用者不能自行扩大。

| 维度 | 初值 |
|---|---|
| 单 RPC JSON 帧 | 4 MiB；不包含通过 DataRef 传输的单独大数据 |
| 动态值深度/节点/文本/实际分配 | 64 / 100,000 / 2 MiB / 32 MiB；取最先触发者 |
| 单键/名称/槽长度 | 名称与slot ≤96 ASCII字节；诊断文本受结果预算 |
| Plan 静态节点/总执行指令/控制深度 | 1,000 / 10,000 / 16；循环/分支按实际展开计费 |
| Atomic 步数/候选增量与新分配 | 128 / 64 MiB；共享既有快照另计 pin预算 |
| Parallel 分支/最大并发 | 32 / 16，并受全局CPU与主体配额限制 |
| 单连接 outstanding / 单主体 queued / 全局 queued | 32 / 256 / 4,096 |
| 控制保留预算 | 独立128槽；处理短控制，不挤入普通工作队列 |
| Plan 槽拥有型新增内存 | 64 MiB；DataRef正文计相应资产/读取预算 |
| 普通终态热缓存 | 10,000条或64 MiB，先触发者；固定引用不能被当作无引用淘汰 |
| 预览寿命/每主体候选内存 | 默认5分钟 / 128 MiB；过期不取消已认领正式提交 |
| 外部 dedup epoch 周期/新接受窗口/最低重试保留 | 初值1天 / 当前epoch / 7天；水位以存储事实为准，绝不按客户端钟推进 |
| N1 订阅数：单连接/单主体/Host | 新增开发初值 8 / 32 / 256；计量表有界，最先达到者拒绝 |
| N1 每订阅执行过滤集合 | 最多32个去重 ExecutionRef；不限制owner订阅可见执行总数，但队列和索引有独立预算 |
| N1 pending消息数/通知单帧 | 每订阅最多128条；每条编码≤16 KiB，不能用DataRef正文撑大观察帧 |
| N1 队列实际拥有内存：连接/主体/Host | 新增开发初值1 MiB / 4 MiB / 16 MiB；共享owner只计一次，计元数据，不预分配全部容量 |
| N1 progress interval | 默认100ms，服务器允许下限50ms；调用者只能降低频率，phase/fact不受该间隔强制延迟 |
| N1 list页大小/扫描量/cursor token/寿命 | 默认50、最大200项；每页扫描候选默认最多2000项；无状态认证 cursor 编码≤2 KiB、有效期2分钟；首版服务端逐cursor句柄数=0 |

内存预算避免重复计算共享数据，按 owner 分配和引用 pin 明确记账。预算拒绝用明确错误，不悄悄截断、丢步骤、分块提交或降低耐久。小程序可以选择更小 Profile。新增 N1 数值是本次明确的可测试开发默认，不是已测吞吐或生产容量；它们按 D0.04/D3.07/D8.05 验证。具体连接发送超时复用服务器受控 transport_timeout，不由订阅无限延长。

### A21.2 性能硬约束

原生短易失调用无 DOM/任务表/全目录复制/强制排队；动态单表示和一次绑定；Plan 常量预绑定、Schema不逐步编译；Atomic 一次候选/提交；Snapshot 不全表深复制；Scheduler 不扫描历史；控制预算不被普通工作耗尽；普通日志不堵关键执行。

控制、编排、算法和存储四类成本分开。专用批量可减少算法重复；Atomic 减少提交；Plan 减少往返；Native 减少编码。不把其中一种优势归因于全部场景。

### A21.3 性能目标与复现

固定目标机、小参数≤1 KiB、预热、明确日志/保障配置后，初始目标：原生短调用 P50 数微秒到10微秒量级/P99≤50微秒；本地小RPC P99≤2ms；满载取消意图受理 P99≤10ms。它们不是算法停止/机床停止时间；持久写单列排队、DB、fsync，不要求所有磁盘相同。

比较 Native、Dynamic、直接服务下限、N次客户端调用、顺序Plan、Atomic、批量业务；N=1/10/100/1000（超限组应拒绝），状态1k/10k/100k对象，修改1/10/100项，1/4/16调用者，冲突/无冲突、长快照/GC、慢流/满载。记录P50/P95/P99、吞吐、分配、峰值、锁等待、实际编码/提交次数。

至少多轮独立进程、预热和交错采样，保存单请求原始样本与样本数；不拿少数轮均值当P99。Google Benchmark 可承担基础测量，真实请求尾延迟另采。[E11] 达不到目标先归因；不得删权限、降低FULL、提前返回Accepted冒充完成。发布只承诺已测支持矩阵。

<a id="a21-footprint"></a>
### A21.4 Embedded 固定占用规格（N2）

除运行期容量/延迟外，必须维护 `footprint-budgets.json` 和实际测量报告。文件记录 Profile、组件/配置、CPU worker数、内存观测容量、构建/CRT/LTO模式、二进制/依赖摘要、测量环境、基准程序、预热/静置/重复方法，以及每项单位、有限上限、基线来源和批准依据。

**两个测量对象不可混用**：`NativeSubset` 是 D1 的开发子集，不宣称包含异步能力；`Embedded` 在 D3 装配完整任务/资源/取消机制后验证。两个都不链接 Data/jsoncons、State/immer、SQLite 或 Asio，不通过 CLI 进程间通信完成测试。

| 指标 | 必须固定的口径和门禁 |
|---|---|
| 实际链接增量 | 同编译器/CRT/优化/LTO下，最小消费者真正引用并运行已声明能力，对比等配置基准程序；统计最终EXE/必需新分发模块的字节，PDB、静态归档库和调试文件单列。仅把未引用库挂target后被链接器移除不算 |
| Ready 空载占用 | Ready 后固定静置窗口内采样 Private Bytes/Working Set，记录稳态统计和进程启动峰值；用同测试启动器的基准进程对照，增量与绝对数同时保存。Windows按PrivateUsage、WorkingSetSize等字段解释，不混成一个RSS指标 [E15] |
| 线程 | 标准测量固定 `cpu_workers=2`，Embedded新增线程预算≤3（2个CPU worker＋最多1个控制/定时线程），默认内存观测不另开文件日志线程；NativeSubset新增线程预算=0。统计峰值、Ready空闲和shutdown后；不能按机器全部逻辑核自动扩大该测量配置 |
| Ready 时延 | 分开测 Host构造/初始化到Ready 和子进程创建到Ready，使用单调时钟与明确Ready标记，记录冷/热条件和分位/最大值。外部启动器不额外给NativeSubset创建内核线程 |
| 回收 | shutdown 后内核线程/回调实际排空；记录保留内存和释放曲线。分配器缓存和OS工作集不立即下降须解释，不要求测量进程的所有CRT缓存归零 |
| 观测模式 | 最低完整内存观测与文件日志/完整Trace分别测量；不把关闭已承诺治理后的数字作为正式Embedded。文件日志模式列出额外线程/内存，不能偷偷并入≤3线程配置 |

链接增量、私有内存/Working Set、启动峰值、Ready时延的**绝对上限不在无实现时捏造**：D1.06 用目标工具链实测并在G1前形成独立审批的有限阶段预算，D3.07在G3前冻结完整Embedded的有限预算，D8.05形成正式支持值。预算必须存在于当前报告之前的批准配置，不能由本次实测值自动加余量生成，不能每轮上调掩盖回退。尚无基线可先采集，但相应G门禁在数字/口径未审批前不得Passed。

后续每个改变热路径/装配的工作包复测受影响指标并比较固定预算；超出时归因、修复或明确受审查的合同变更，不以“最终再测”跳过阶段门禁。线程数和零分配等结构性数值从本版直接生效；其他数字必须由测量与产品要求共同决定，非运行时功能追加。

### A21.5 受限原生短调用零分配合同

固定场景：已注册并绑定、已完成明确有限预热、小型定长Args/R、无业务堆分配、内存易失Invoke、无Task/Plan/动态Payload/持久服务；真实参数、权限、目标/准入检查仍然执行。预分配有界内存观测，已声明静态标签；不得删校验或默认所有请求可信。**该场景每次成功调用的框架新增堆分配数必须为0**。

基准必须有确定会分配的正探针和不分配的负探针，确认计数器真的工作。记录`new/new[]/aligned new`、CRT/自定义分配器及参与模块的覆盖范围；未覆盖的分配来源明确列出，不能仅替换一种new就宣称进程全局零分配。线程局部测量用于定位，同一调用触发的后台观测/缓冲扩容还需端到端统计，不能移出窗口隐藏。

首次调用、拥有型Submit、错误详情、可变结果、完整Trace/文件日志、业务数据和动态协议分别记录预算，不承诺它们全部零分配。释放/长期分配峰值也单独记录，不能用无限预分配换来零分配字样。分配计数构建用于诊断，正式延迟使用等语义的无计数Release构建并注明差异。

### A21.6 分阶段采样与预算签核

D1.06 建 NativeSubset 基准、计数器有效性和有限占用预算；D3.07 对完整Embedded测相同项目，并加订阅/慢观察者开启与关闭的对照（该观察对照属于AutomationHost，不混入无Asio的Embedded）；D8.05在最终源码/二进制/硬件/依赖和正式配置上复测。

测量工具、公开头/占用快照和预算文件只属开发/发布工程，不进入生产每请求路径。报告分别显示硬约束通过、预算通过、无历史基线和未测项目；任何未知项不自动等同通过。

<a id="a22"></a>
## A22｜测试门禁、故障矩阵与证据

### A22.1 测试族与故障窗口

T01–T24 与 R01–R24 一一对应，Txx 是必须存在的测试族，不是已通过的测试。

| 关键窗口 | 必须观察 |
|---|---|
| 注册第k步失败 | 无部分可调用目录、无泄漏服务 |
| Native/Dynamic非法输入 | 相同拒绝，Handler未运行 |
| 取消/撤权 vs CommitClaimed | 两种先后均明确，未获许可无发布 |
| begin/write/commit 返回错误或异常 | 原始SQL、root、History、Intent、事件和回调同时检查 |
| COMMIT后发布前进程终止 | 新Host只重建事实，不重执行业务 |
| Effect成功后输出无效 | 实际效果保留，不能FailedBeforeApply |
| 必要记录失败 | 明确证据故障/隔离，不虚假Terminal或永远无解释Finalizing |
| inline completion/重复callback | 接受与完成协议正确，恰好一次发布与释放 |
| Plan中途失败/并行取消 | 已提交事实保留，child有owner，输出按定义顺序 |
| intent水位与claim/GC竞争 | 老key不复活，pin不断裂 |
| 子效果完成、父checkpoint落后 | 查询同子键衔接，不重复发送 |
| 旧备份恢复 | 新世代与对账，缺失外部历史不当未执行 |
| parser深度/重复key/分配超限 | 在预算内拒绝，不先OOM再声明校验成功 |
| shutdown超时/worker自毁 | 不早释放，不产生悬空回调 |
| subscribe/ack/get/完成交错 | 确认先于本订阅事件，快照与版本无观察空窗，内部完成不依赖有损通知 |
| coalesce/drop/revoke/断线 | gap明确，无权排队数据不再发送，执行不被取消或无限pin |
| list分页/回收/新增/权限变化 | 有界且游标前进，弱一致性如实声明，隐藏owner无信息泄漏 |
| 分配计数器或测试发现失效 | 已知正探针触发失败；0个测试/缺预期用例不得绿色 |
| SDK同名头签名变化 | 清单未变仍由声明检查/冻结消费者捕获，不自动覆盖golden |
| 后端声明与合同行为不符 | conformance失败，不能通过减少capability或skip必需合同避过 |
| evidence截断/失败后重跑/错误二进制 | 事实保留且摘要不符拒绝，不能手填Passed或拼接成同次成功 |

确定性 Executor/Clock/Storage 注入测试、真实线程压力、平台支持的 sanitizers、fuzz、独立进程 crash probes、实际IPC/文件系统、安装树测试共同使用。ASan不是线程竞争证明；进程崩溃不是断电；普通摘要不是签名；测试桩返回错误不代替真实故障窗口。

每个工作包输出 commit、工具链、依赖锁摘要、实际命令/退出码、测试manifest、通过/失败/未运行、原始日志/样本路径。失败项不能从清单删除。Schema样例校验只证明其覆盖范围，不证明内核运行。

<a id="a22-conformance"></a>
### A22.2 版本化 Port Conformance Kit（N3）

端口已有行为要求集中为可复用测试套件，不要求每个后端重写相似断言。套件通过公开工厂、受限测试控制器和能力描述装配后端，不在生产接口中增加裸写入、friend、skipPolicy或测试开关。它本身不是第二个Runtime。

| 套件 | 共同必需行为 | 按声明能力增加的专项 |
|---|---|---|
| ExecutorConformance | 接受/拒绝的所有权转移；恰好一次完成；失败不遗留work；异常边界；wait/drain/shutdown寿命；所属worker非法等待 | 真实并行/线程数、后端特有队列与停止故障；合法inline后端验证Runtime可重入，纯异步池不被迫实际inline |
| StorageConformance | 同一事务所有者、已确认提交/回滚语义、错误事实、访问寿命、会话政策与能力查询 | Durable存储独占、WAL/FULL读回、真实进程kill、磁盘故障；内存后端不得声称耐久，也不因无磁盘“跳过”共同事务约束 |
| LoggingConformance | 接受/拒绝/丢弃计数语义、公开格式/脱敏、错误隔离、flush覆盖的已接受范围、关闭与回调寿命 | 异步满队列/轮转/磁盘/后端失败；合法drop按政策测试，不把RequiredAudit强加给普通日志 |
| AssetStorageConformance | 不可覆盖发布、完整读取、身份/长度验证、引用外部存储的公开协议 | 平台路径逃逸、flush/原子发布、真实文件与备份故障；仅与资产端口相关，不混用SQL测试 |

`conformance_manifest` 固定 port_contract_version、实现身份/摘要、capabilities、适用用例、所需fixture和证据。共同必需用例不可由后端capability关闭。可选用例标记NotApplicable须由合同表达式决定并带原因，不写成Passed；选择的产品Profile要求某能力时，不具备该能力就是装配/门禁失败。

故意重复callback、迟到work或破坏事务的**非合格故障后端**只用于消费者防御测试，不登记为合格实现。可控测试Executor与生产CPU池分别运行共同合同，再运行各自专项；确定性测试不替代真实线程和进程验证。合同变更时所有已支持后端同版本复测，禁止仅复制测试然后逐后端改预期。

D0.06 提供测试工厂/能力manifest和runner基础结构；D1.06先形成Logging共同子集；D3.01形成Executor完整套件与两类合法实现；D5.01建立Storage事务/持久套件；D6.01接AssetStorage；D8合并适用矩阵。测试套件可以单独供适配器作者使用，但不成为产品包链接或运行依赖。

<a id="a22-evidence"></a>
### A22.3 自动证据采集与不可手写通过（N5）

D0.06 必须交付执行包装器而非仅抓取一句日志的脚本。它从实际configure/build/test进程采集 argv、cwd、时间、退出/异常/超时、stdout/stderr及报告；绑定被测源码身份（commit＋是否dirty＋实际构建输入指纹）、工具链/依赖锁/配置、被测二进制与产物hash。发布证据要求可重建的明确源码快照；本地dirty运行必须如实标记，不能冒充相同commit的干净构建。

测试有三份清单：**经过审查的expected manifest**、本次CTest发现的discovered manifest、本次实际executed结果。预期不能从本次发现自动重建；否则删测试会变成“全通过”。CTest可用`--show-only=json-v1`、`--output-junit`和`--no-tests=error`，实际选定版本须验证，原始进程退出和报告一致性同时检查。[E14]

人类/AI不能填写或修改exit_code、测试结果、hash或`automated_status=Passed`。机器规则依据当前运行材料计算；人工只提供设计/资料审查、故障解释、风险和发布批准。**工作包Passed要求机器门禁、必需产物和必需评审同时完成**，不是只看CTest绿色。允许批准缩小未来发布范围，但不能改写已失败事实或免除当前必需验收。

每次运行有独立run_id，失败重跑追加而不覆盖。JUnit、测试stdout、crash parent/child证据不互相冒充；超时/崩溃/缺报告/截断/未发现预期测试/轮次不足都进入明确Failed、Incomplete或Blocked。合法crash probe由父验证器核验预期终止点和新Host结果；并非所有非零子进程退出都是测试失败，也不因打印成功标记就忽略退出事实。

合并证据要验证source/build/profile/依赖/工具链/二进制身份及已审查的多配置矩阵。不同源码的成功片段不能拼成“当前全部通过”。报告hash提供完整性核对，不是第三方安全证明；秘密/凭据和用户材料按测试策略隔离并脱敏。自动工具为开发期基础设施，不部署进内核执行链。

### A22.4 v3.2–v3.3 补充项的验收归属

N1使用T04/T06/T12/T19/T20/T22/T23；N2使用T01/T03/T22/T23；N3使用T12/T14/T18/T19/T22/T24；N4使用T01/T02/T05/T24；N5使用T23/T24。v3.3 新增：N6使用T04/T20/T22/T23；N7使用T02/T07/T09/T19/T20/T24；N8使用T02/T20/T21/T24；N9使用T04/T05/T10/T11/T17/T24；N10使用T04/T14/T15/T16/T23/T24；N11按其修订位置复用T06/T19/T20/T23/T24。

保留24个主测试族，具体用例用如`T20.notifications.subscribe_get_race`、`T20.execution_list.stateless_cursor_tamper`、`T02.ui.state_write_uses_operation`、`T11.plan_ir.semantic_roundtrip`、`T14.canonical.float_shortest_width`、`T23.footprint.embedded_ready`、`T12.conformance.executor.<backend>.<case>`命名。没有实际实现前它们只是必须建设的测试，不计入通过数。

<a id="a23"></a>
## A23｜完成定义、阶段放行与变更纪律

三个长期消费者：C-A 无状态自动化服务；C-B 无文档事务配置服务；C-C Workspace对象/资产/后台算法应用。它们共享 Runtime、Plan引擎和 Outcome，不允许平行第二核心。

分阶段：D0合同/模型/依赖→D1原生→D2动态和真实CLI→D3任务/寿命→D4内存状态/Plan/Atomic→D5Durable→D6Workspace/DurablePlan→D7完整AI面→D8发布。详细任务以配套执行计划为准。

D4 是“可运行内存套件”，不是完整生产完成；D5完成持久链，不等于Workflow完成；D8通过后才可声明当前支持矩阵下的完整交付。可选人类DSL、远程TLS、DeviceHost产品化不阻塞首版，但接口不能以此假定不存在相应风险。

### A23.1 已冻结的产品交互/API ADR（N7/N8）

以下三项在 v3.3 作为正式架构决定记录，实施中不得因局部方便重新引入平行接口：

1. **UI 单一业务写面**：GUI 对 StateEdit、ExternalEffect、Lifecycle 一律调用同一 Operation 面；只读渲染/视图可直接消费已授权不可变 Snapshot/类型化读服务。不存在 UI 专用业务写后门。
2. **无通用 `state.inspect/query` RPC**：外部状态事实通过领域 Read Operation 暴露并进入 Catalog。内核不提供任意 path/dictionary 形式的“万能状态 API”；未来若新增统一查询语言必须独立版本和 ADR。
3. **GUI 主人工前端，DSL 可选**：桌面产品人机交互优先 GUI；CLI/Plan/Client SDK 是自动化稳定面；人类 DSL/REPL 仅为可选前端并编译到同一 Plan，不是首版发布必需项。

D0.02 将这三项写成正式 `docs/adr/` 文件和反向架构测试；D8.06 核对产品资料、SDK 示例和实际调用路径没有漂移。

本文合同变更必须记录 ADR，更新 Schema、双方入口一致性测试、故障模型和受影响任务。纯文档措辞可独立改。性能优化不得改变提交/取消/权限语义；不能为通过测试直接改变预期事实。新实现不受旧源码布局约束，也不安排旧数据兼容任务。

**v3.3 放行补充**：G1含原生占用/分配仪表和经审批的阶段预算；G3含完整Embedded占用、真实订阅/执行枚举和多客户端收尾；每个新增后端通过同版适用Conformance；G8必须有SDK公开表面与冻结消费者报告、自动证据完整性和正式占用预算。保持原64包编号，但这些通过条件不能只作为“可选优化”附在实施内容中。

<a id="a24"></a>
## A24｜资料来源与已定案清单

### A24.1 本轮状态

继承的C1–C6、v3.2 的 N1–N5 以及 v3.3 的 N6–N11 已在本文作设计决定，不再作为未决讨论留给实现者。**仍需工程验证的项目**是具体库/工具链版本、实际内存与延迟上限、平台故障行为，以及真实编译和运行的公共API；这些是执行步骤中的交付物，不是“主架构未决定”。

PlanCompleted、统一事实与阶段、wire exports、原子空delta提交、严格绑定覆盖、内部子准入和私有IPC分帧继续保留。v3.2 已固定订阅/列表、观察预算、Embedded测量、受限零分配、端口一致性、SDK演进和证据自动化；v3.3 在此基础上进一步固定 execution.list 无状态认证 cursor、UI 单一业务入口、无通用 state RPC/GUI 主前端 ADR、Plan IR 语义往返、canonical CBOR feasibility gate 及相关门禁细则。它们都是对既有架构的收口，不是新的平行运行时。

### A24.2 用户材料

- [B32A] 本对话《01_Architecture_v3.2.md》，本次直接逐节修订基底。
- [B32E] 本对话《02_Execution_Plan_v3.2.md》，64工作包与 N1–N5 门禁直接基底。
- [B33] 本对话对 v3.2 的收口审查：cursor 数值/状态问题、UI 统一入口、state API/DSL ADR、D0.06/G3 风险、D4.05 IR roundtrip、D5.02 canonical spike 及三处文档小瑕疵；本版按最终采纳边界合入。
- [B3] 用户上传《Open_Command_Kernel_Greenfield_Architecture_v3.0(1).md》，历史主架构溯源。
- [B2] 用户上传《Portable_Command_Runtime_Greenfield_Design_v2.0(1).md》，历史基础值/数据所有权/提交锁细则。
- [BR] 本对话《V3_vs_V2_Review_and_Implementation_Addendum.md》，历史 C1–C6 审查来源。

以上材料只作为溯源，不要求开发者再拼接其规则；本文与配套步骤已自足。来源原件未修改。

### A24.3 外部依据与核验范围

E02–E12保留v3.1列明的2026-09-06来源记录，本次不将其写成重新验证全部库。E01与新增E13–E15于2026-09-07核对；它们支持协议/版本/测量/工具事实，具体产品规则和预算仍是本方案决定。

- [E01] JSON-RPC 2.0：[规范](https://www.jsonrpc.org/specification)。批请求不等于事务；本产品任务与原子语义另定义。
- [E02] CMake：[Importing and Exporting Guide](https://cmake.org/cmake/help/latest/guide/importing-exporting/index.html)。用于安装导出、重定位与真实依赖。
- [E03] RFC 6901：[JSON Pointer](https://www.rfc-editor.org/rfc/rfc6901)。本文限制字符串形式和可绑定路径。
- [E04] JSON Schema：[Draft 2020-12](https://json-schema.org/draft/2020-12)。dialect/vocabulary及format策略需明确。
- [E05] SQLite：[WAL](https://www.sqlite.org/wal.html)。单writer、本地共享、WAL-reset修复与检查点边界。
- [E06] SQLite：[PRAGMA synchronous](https://www.sqlite.org/pragma.html#pragma_synchronous)。FULL/NORMAL耐久性不同。
- [E07] SQLite：[Atomic Commit](https://www.sqlite.org/atomiccommit.html)。同事务原子性及底层系统假设。
- [E08] RFC 8949：[CBOR](https://www.rfc-editor.org/rfc/rfc8949.html)。确定性编码仍需固定本产品profile与向量。
- [E09] Microsoft：[Named Pipe Security](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights)。不能将默认DACL当产品权限。
- [E10] OpenAI：[Function Calling](https://developers.openai.com/api/docs/guides/function-calling)。Schema工具描述与动态发现；不替代服务器业务治理。
- [E11] Google Benchmark：[User Guide](https://google.github.io/benchmark/user_guide.html)。预热/重复，不替代请求级尾样本。
- [E12] immer：[Design](https://sinusoid.es/immer/design.html)。结构共享；共享变量赋值仍需同步。

- [E13] SemVer：[Semantic Versioning 2.0.0](https://semver.org/)。公共API、开发期0.x及Major/Minor/Patch政策。
- [E14] CMake：[CTest命令手册](https://cmake.org/cmake/help/latest/manual/ctest.1.html)。结构化发现、JUnit输出、无测试错误及重复执行；版本能力在D0.06实测。
- [E15] Microsoft：[PROCESS_MEMORY_COUNTERS_EX](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex)。WorkingSetSize与PrivateUsage等测量字段，不是内核已测占用。

除上述有限事实外，组件划分、Outcome、许可、水位、预算和实施顺序是本方案的工程决定。它们必须由实现和测试证明，不能把官方库说明当作整套系统已认证。

**B5/G3 post-gate C2（2026-09-10）：** 按 [接受与业务进入 ADR](adr/ADR-b5-accepted-outcome.md)落实 A09.1。A05 的 FailedBeforeApply 除业务已进入外，也允许执行已 Accepted、业务尚未开始的失败；BeforeApplyDecision 明确分列 business_entered 与默认 false 的 execution_accepted，二者至少一个为真，仍必须证明未应用且无未知事实。取消先赢保持 CancelledBeforeApply。该澄清不改变 Native 接受前 Rejected，不增加 Outcome 变体或设备/持久能力。

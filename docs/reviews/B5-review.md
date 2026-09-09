# B5 集中 SPEC/CODE 技术复核

日期：2026-09-10。范围为 D3.04–D3.07，依据 v3.3-r2 的 A04/A05/A06/A09/A10/A15/A16/A17/A19/A21/A22 与执行计划 E00/E02/E03。复核者为 Codex AI，沿用 `automatic-acceptance-policy.json`；不表示人工批准。精确实现摘要由同批 SPEC/CODE JSON 绑定。本材料的技术结论与正式机器运行、包级 DAG 和 G3 放行分开。

## 独立包结论与验收范围

| 包 | SPEC 技术结论 | CODE 技术结论 | 正式完成所需机器事实 |
|---|---|---|---|
| D3.04 | 通过：同一 Operation/typed Handler 的 owning Submit，Volatile 接受、有限索引/结果/观察配额与短 Invoke 边界一致 | 通过：接受发布、请求拥有、结果与权限寿命、通知不 pin 结果的路径已复核 | 接受前拒绝、接受后执行失败身份、输入释放、缓存/会话配额、list 前进、观察一致性及满队列；映射至 `b5-requirement-map.json` 的 Runtime/Protocol 组 |
| D3.05 | 通过：取消为意图，等待超时不取消业务，deadline 单调且不可因 child/当前权限而延长 | 通过：开始仲裁、预留定时节点、控制循环和资源等待取消路径一致 | 真线程运行期取消/期限、排队资源取消、满载控制；permit/commit 协作模型单列，不冒充 D4/D5 后端 |
| D3.06 | 通过：父子归属、有界深度、Finalizing 与 callback/必要记录 owner 的寿命相容 | 通过：父强/child 弱关系、可靠 pending、candidate/settled 分离以及排空后发布终态 | 父不早终态、取消传播、异步遗漏/重复/异常、必要记录失败/迟到回调、观察回调与关闭组合、Conformance 违规反例 |
| D3.07 | 通过：真实 CLI 观察、停止、cursor、完整 Embedded 与额外观测成本全部列为必需 | 通过：当前 Control/CLI、Installed SDK 与测量范围没有将 mock/NativeSubset 代替完整执行 | 三配置共享语义矩阵、真实多进程竞态、G3-C 三报告与附加观测、源码适用范围审计；全部完成才可 G3 放行 |

每个包的 Passed 仍须其正式前置已 Passed。批次内不因代码已可用或某个子 checkpoint 通过而跳过 DAG。D3.04 的批次外前置只消费已提交的状态和公开合同；当前修改的 Runtime/Executor/Control 边界在本批影响集与 G3 代表性回归中验证。

## SPEC：需求映射和边界

1. `tests/manifests/b5.expected.json` 的 126 个用例在正式运行前由上述完成条件及源文件声明确定。`b5-requirement-map.json` 分列新增 Runtime/Protocol、真实 CLI、IPC、Scheduler、Resources、Executor、协作模型与 SDK/边界。三配置使用同一 expected，ASan 使用 RelWithDebInfo；按 `toolchain.md` 使用已实测可加载原锁定校验包的 Python 3.11.9（系统 3.13 无兼容 rpds 扩展，不用于该模型矩阵）。不从本次 CTest discovered 反推或删减失败项。完整三个配置只在这次正式收口执行，不回放全历史 Gate。
2. D3.04 的 `tests/contract/submit/`、`execution_observation/` 与 D3.06 的 `structured_lifetime/` 逻辑用例具体落于 `tests/contract/native/managed_cases.hpp`、`execution_table_cases.hpp`、`observation_cases.cpp`、`structured_cases.cpp`、`async_cases.cpp`、`session_cases.cpp` 和 `combined_cases.cpp`；对应 CTest ID 显式映射。复用真实 Host fixture，不为目录名称复制第二套测试或 Handler。
3. D3.05 的取消/commit 竞争使用 `tests/model/commit_model/test_commit_model.py` 中六项现有 permit/发送先后模型，以及真正生产 ExecutionService/Resources 的线程测试。实现仅 Read/PureCompute 的执行接线，不宣称 State/Effect 的提交后端、持久接受或恢复。生产期限使用 steady clock；RPC TTL 通过捕获 UTC 基准与单调增量计算，时钟倒退拒绝，恢复能力未实现即不发布。
4. D3.07 的任务版消费者在 `examples/managed_service/`，与 `stateless_service` 共存；后者仍用于短调用/无任务能力反例。任务版从真实安装 Schema 与 HostBound 解码、提交、查询同一执行，不将无状态示例改名冒充正向验证。
5. G3 只证明内核和规划验证消费者，不涉及 GUI/CAD/CAM/设备、抢占停止、硬实时或持久套件。普通 Trace/File 可丢弃、有界，不作为 RequiredRecord/业务事实或 Durable 日志。

## CODE：关键路径与反例

- `packages/runtime/include/ock/runtime/invocation_detail.hpp`、`executions/managed_execution.hpp`：准备 owning 输入、最小结果和额度后再接受；claimed 仲裁防重复开始；同步与异步复用注册的真实类型/Handler，未注册第二 TaskHandler。异步 candidate 与 callback owner 排空分开，返回/抛出/遗漏完成不提前释放输入、admission 或 Lease。
- `executions/execution_service.hpp`、`resource_wait_binding.cpp`：预留 deadline 节点和控制预算，开始后取消保留业务协作事实；等待资源时可取消，运行期 Lease 由业务 owner 释放，取消不能越权释放正在使用的资源。可靠 pending 与单个控制线程重试不依赖外部通知到达。Host stop 超时保留执行/资源/回调 owner，后续可以完成真实排空。
- `executions/required_completion.hpp`、结构化执行与 `execution_table.hpp`：parent 保活与弱 child 索引避免环；关闭 child 创建后等待必要 child/回调收尾。必要记录失败保持 Finalizing/诊断与原业务结果，迟到/重复回调不覆盖原 candidate，不把 Read 收尾事件说成业务值 Durable。终态发布前释放会话/admission 引用，结果保留类型代码寿命及独立 Catalog pin。
- 执行索引/`execution_observation.hpp`：owner 和权限投影分离，实时 keyset 采用有限扫描并推进 cursor；终态状态及 observation_version 一致发布。固定通知 ring 不持有业务结果，观察 lease 覆盖在途 callback，缺最后通知仍可从同一执行表 get/wait。外部回调在内部锁外执行。
- `packages/control/observation/execution_method.cpp`、`list_method.cpp`、`subscription.cpp` 与 Runtime Policy/SendCoordinator：查询等待和 RPC 超时分开，断线不提交 cancel；当前授权在编码/排队/首字节边界复核。观察队列预留控制额度，ACK 前不发送事件，generation/sequence/gap 和单连接/主体/全局配额有界，慢连接超时只关闭该观察连接。
- `packages/control/server/cursor.cpp`：HMAC 覆盖 canonical 上下文及连接/委托绑定；2048 字节上限在解码前检查，常量长度 MAC 对比、host/filter/owner/view/上界/原始 TTL 校验与单调前进一致。最终 CODE 发现的 BCrypt provider 分配异常泄漏已在 `a1299c0` 修正，认证输入先完成再打开 provider；直接合同与真实 120 秒 TTL/续页过期测试已通过。未声称执行 OOM 注入。
- `packages/control/client/{client,watch}.cpp`、`apps/ock/{main.cpp,pipe_exchange.hpp}`：先订阅后 get，seq/gap 与周期 get 避免依赖最后提示；默认中断只关观察，显式 `--cancel-on-interrupt` 才通过独立控制连接取消。两个 hello 核对同一 Host；list 保留同一连接且页数/游标长度有界；满客户端通知队列记 gap。
- `packages/adapters/cpu_pool/executor.cpp`：固定 worker 数，shutdown 成功前真正 join；当前 Windows 句柄/超时/重复关闭与 Host 组合验证已具备，正式 G3 复验共同 Executor 合同及违规后端防御。标准 Embedded 固定两 worker 加一个控制线程，无 resize/detach。

## G3-C 与额外观测来源

标准完整 Embedded 正式来源为 `cde99f2e0114c897a46f80d3142c985102430d8c`，预算在运行前经独立 AI 技术复核批准。Release/Debug/ASan 报告分别为：

- `evidence/G3/C/cb67dc769b5746b9b07ef558dd630ee7/report.json`
- `evidence/G3/C/86685a7b20dd4702b3f2ec352ff57fa4/report.json`
- `evidence/G3/C/13354720f40a4bf497fb5327439abd37/report.json`

三份各 6 组 ABBA；Release 同一无计数 EXE 另有 6 组无握手启动 ABBA，Debug/ASan 各 480 个完整 Invoke 零新增分配窗口。有限内存、分发增量、Ready、线程结构上界/阶段实际 ID 和回收分别验算；诊断数值不混作 Release 性能，不声称连续 ETW 或强制冷缓存。

`evidence/B5/observed-embedded-c53de988af/prerequisite-audit.json` 重新核对当前报告原始命令、预算、原始模块/安装/二进制摘要及方法源。原方法全部既有输入中只有未链接的 Control `cursor.cpp` 改动；Runtime/Foundation/CoreContracts/CpuPool、SDK、CMake、fixture、标准测量/分配源保持逐字节一致。新增观察消费者也不进入原编译目标。因此保留并复用原来源报告，不把当前扩大的全目录 method digest 冒充原 digest，不因无关 Control 修正重复物理采样。

额外模式在 `tools/footprint/observed_embedded/`，复用同一完整 fixture 与原 Release 安装。36 个进程覆盖 default/完整操作 Trace/异步文件/慢文件；每个 85 个实际调用窗口。Trace 包含每个操作边界，属于验证消费者级别，未发布通用 Plan/内部逐步骤 Trace SDK。四模式均保留 Host 默认内存日志与治理，额外 ring=128，文件读批次=16，单个 writer 必须 join。实际每次 170 条接受、42 条内存淘汰；文件写出 136–152 条并保留 gap，未把可丢普通日志当可靠完成。Ready 新线程分别为 3/3/4/4，shutdown 后退出。Ready Private 最大分别 1486848/1523712/1589248/1568768 字节。进程 CPU 量化明显，原文逐项保存而不据此声明稳定零开销；纯启动时延仍只采用标准 G3-C 无握手测量。

AutomationHost 开/关观察与慢消费者、cursor 有效/MAC 错误/超长成本已有 `evidence/B5/managed-cost-reader-c3be8ab991/` 原始样本；该样本不替代最终真实 CLI 的语义用例，也不套用 Embedded 三线程预算。其共享执行/观察生产路径未因 cursor 字符串构造顺序修正而改变协议或调度；最终 cursor 语义用例必须按新来源运行。

## 放行条件

本次 SPEC/CODE 技术审查未留未解决的生产代码缺陷。正式运行前绑定本材料、expected、方法/预算和当前源摘要；正式机器结果尚待生成时不宣称 Passed。机器失败必须保留并修复，再按真实影响范围判断是否需要新候选来源；不删用例、不拼接不同来源 Passed。

首次候选提交字节检查发现 Git 的 LF 自动转换与 20 个既有工作区 CRLF 文件不同，故未启动正式矩阵。已将仓库属性改为保留实际字节，差异逐项核对仅为行尾后提交原工作区字节，源/预算/测量内容没有改变；`.gitattributes` 纳入最终输入。原 G3-C 的 `cde99f2` 是当时 Git HEAD 导航，其真实源以保存的 method/source SHA 为准，不声称当时 Git blob 与所有工作区字节一致。本批最终候选必须另通过全部输入的 Git blob 精确核对，才作为已提交事实验收。

`c4b5c97` 首轮正式 Debug 的 124/126 及 Schema 校验失败保留原文。安装探针仍使用旧 Runtime 阶段和未实施 ControlClient 的链接预期，现按当前声明精确检查 Runtime=B5Subset、已实施客户端仅 ControlProtocol 加私有系统 bcrypt（合同占位分支仍只有 ControlProtocol），不允许任意服务端依赖。Executor 清单仅重绑已复核的 ports.hpp、CpuPool 真正 join 与对应线程句柄测试三个摘要，共同用例/能力/资格规则不变。Evidence Schema 增加可缺省的 creation_clock，两个正值有界整数、必需键和未知字段拒绝均明确；真实失败报告的格式及七种损坏变体作为直接回归，历史报告仍保持 Failed。最终矩阵保持 126 项，并将该格式回归作为显式 check，安装子命令和导出材料一并归档。

最终批次验收须逐包给出同一来源的独立 SPEC/CODE 结论与对应机器用例，按 D3.04→D3.05→D3.06→D3.07 的正式前置顺序提交事实，最后才给出 G3 总决定。`progress.md` 是当前状态入口，旧 B1/B2 等 Passed 与所有历史原文保持不变。

`38802f3` Debug 的 126 项和既定 Schema 检查全部成功，但独立 expected 遗漏该 check，整体报告仍保持 Failed。修正只将正式运行前已经在运行清单和本审查中确定的 `CHECK.evidence.creation_clock_schema` 同步到 expected；不改测试数量、行为或从 discovered 选择条件。最终来源重新绑定，三配置必须完整通过。

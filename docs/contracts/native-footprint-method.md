# D1.06 NativeSubset 占用与进程观测方法

状态：**D1.06 固定测量方法**，属于集中 SPEC 输入；方法标识 `ock.native-footprint/2`（[有限采样增量审核](../../evidence/bootstrap/D1.06/footprint-sampling-amendment-review.md)）。本文固定测量行为，不声明工具、消费者、预算或 G1 已通过。原始候选保持不变，来源摘要见末尾。依据唯一架构 v3.3 [A16](../01_Architecture_v3.3.md#a16)、[A19](../01_Architecture_v3.3.md#a19)、[A21.4–A21.6](../01_Architecture_v3.3.md#a21-footprint)、[A22](../01_Architecture_v3.3.md#a22)及执行计划 [D1.06](../02_Execution_Plan_v3.3.md#d106)。Host/Logging/SDK 的具体声明分别归本包对应合同；不在这里另造公开业务接口。

## 1. 测量对象与实际能力

对象只有 `NativeSubset`：真实 NativeHost、已实现 Read/Compute、注册/绑定、认证与每次授权、目标/线程/准入检查、Outcome、D1.05 有界 Invocation 事实环及有界普通内存日志。不创建 Task、空 Document、队列、异步 worker、文件日志线程或恢复存储，不通过 CLI/RPC/JSON 进程链执行业务。真实目标、导入和模块闭包不得引入 Data/jsoncons、State/immer、SQLite、Asio。

报告保留标准配置语义 `cpu_workers=2`，另明确 NativeSubset 实际 worker 实例数为 0、async unavailable；不为满足配置字段创建两个线程。内核新增线程硬约束为 0，不能引用完整 Embedded 的 ≤3 线程预算。

Host 只在实际生命周期事件写普通日志，普通成功 invoke 不增加普通日志。每次调用仍执行既有 D1.05 事实环及完整治理。零分配窗口必须计入这些真实工作和结果析构；不能声称测量了每调用普通日志，也不能关闭实际生命周期日志取得较小数字。

被测 NativeSubset EXE 必须在本进程完成以下能力证明，任一失败本轮即失败：

1. 创建 Host，Ready 前业务入口拒绝且 handler 计数为 0；模块真实注册/启动，之后 Read/Compute 各返回实际正确值；非法参数不进入 handler。
2. 证明未装配的异步能力不可用，无成功空任务列表、假 Task 或只返回成功的替身。
3. 在一个实际模块的 `start(const ModuleContext&)` 有效借用期间，以日志合同中受审静态事件/字段调用 `log.try_write`，保存 Accepted 水位 H，立即 `log.flush(H)` 并验证真实覆盖 H。只保存固定值证明，不保存 ModuleContext/logger 引用或通过 detail 取得端口。证明事件与 Host 实际生命周期事件均计入日志容量。
4. Ready 后通过公开 `Host.copy_logs({incarnation,1,0}, out)` 验证默认内存日志中的公共记录、接受上界及实际淘汰/gap；返回页只含公共固定值。启动期 Accepted/flush 证明与 Ready 后读取同属本次 Host/stream，不能另建无关日志实例代替。
5. 真实 shutdown 成功并排空，之后旧 Bound 只能拒绝；保留业务结果、关闭状态、生命周期/析构哨兵，不以进程退出替代清理。

当前默认容量 128、DropOldest、Info，沿 HostOptions 和 Logging 合同登记，属于装配输入而不是占用审批值。证明事件数及对应保留/淘汰预期由固定消费者定义；不能假定启动后一定未淘汰。flush 的旧水位/淘汰前缀等完整行为由同生产默认内存实现参加的 LoggingConformance 验证，不从 shutdown 报告推造不存在的日志水位字段。

## 2. 工具与成对消费者

开发工具布局固定为 `tools/footprint/run.py`（调度/原始采集）、`windows_process.py`（有限观测配置与 Win32 查询）、`analyze.py`（报告计算）、`controls/`（独立控制工件）。进程创建/归属/恢复/终止/排空的唯一 owner 仍是 `tools/evidence/process.py`。这些工具及 Conformance 不导出到生产 SDK，不进入每请求路径。

baseline 与 NativeSubset 必须采用相同测量 shim、固定共享记录/事件、QPC、主线程屏障、构建/CRT、输出方式、阶段数量和静置窗口。baseline 执行有可验证结果的最小本地计算，但不链接 OCK Host/Native；不能链接完整内核再关闭业务抵消其成本。NativeSubset 必须真实引用并运行上述能力，不以仅挂接随后被链接器丢弃的库计算增量。业务始终在各消费者主进程执行。

SDK 消费者使用 `examples/stateless_service/` 的安装消费形态，只包含已安装公开头并链接实际导出 target，独立配置、编译、运行同一能力；BUILD_TESTING=OFF 安装与搬迁前缀均验证，不借用源码树、private/detail 显式入口或 tests fixture。模板内部由公开头包含安装 detail 不等于消费者自行依赖 detail。

### 构建与工件身份

诊断配置固定 Debug `/MDd`、Release `/MD`、ASan Debug `/MDd + /fsanitize=address`，x64、C++20、LTO 关闭；锁定本仓实际 MSVC、Windows SDK、expected 后端和依赖锁。实际 compile/link 命令、cache/toolchain、CRT/优化/LTO与禁用组件必须核对，不能由配置名推断。增加 /GL、/LTCG 或静态 CRT 必须另立方法键及匹配 baseline，不能跨模式比较。

每对记录最终 EXE 字节/SHA、有符号差值、实际必需新分发模块路径/角色/字节/SHA/导入和加载证据；仅完全同身份的共有模块抵消一次。OS 系统 DLL（含 bcrypt）作为环境依赖登记，不能冒充 SDK 新分发字节。CRT/ASan 运行时的配置与实际分发条件单列；Debug/ASan 不充当 Release 发布占用或分发承诺。静态库、PDB、对象、中间文件和头文件单列，不混入 EXE+必要分发模块总量。

模块枚举在 WarmupComplete 和全部能力完成后分别执行，核对静态导入及延迟加载；未加载不等于无需分发。查询当前受管 root 的真实创建句柄，验证容量、路径截断和完整性；有限重试未取得完整快照即 Incomplete，不关掉枚举得到的 HMODULE。只有处理子进程始终属于本轮 Job，禁止按名称附着或将 Job 中其他进程代替 root 指标。

## 3. 固定重复、窗口与统计

| 方法参数 | 本方法值/要求 |
|---|---|
| 进程配对 | A=baseline，B=NativeSubset；每次均新进程，顺序固定 ABBA |
| pilot | 每个配置/测量模式 3 个 ABBA 块，A/B 各 6 轮 |
| 预算后正式验证 | 每个适用配置/测量模式 6 个 ABBA 块，A/B 各 12 轮；新 run_id，不复用 pilot 为最终样本 |
| 内存采样 | memory_sampling=due_5ms；从恢复线程起每5ms到期尝试，记录实际时刻/间隔与缺样，不宣称OS实时周期 |
| 线程采样 | thread_sampling=phase_boundaries；仅assigned_suspended及十一固定阶段边界查询真实线程ID集，周期内存点thread_ids=null，不复制旧集合 |
| Ready 屏障 | 子写真实 Ready ticks 后等待父；父先读取 Ready 高水位，才准许预热 |
| 预热 | 每个 Native 进程固定 4 次完整成功调用，逐次校验值；不因失败增加预热 |
| Ready 空载 | 预热及能力证明完成后静置 500 ms，再采样 1000 ms |
| 释放曲线 | 下文每个释放阶段停留 500 ms；禁止事后延长某轮挑取更小占用 |
| 分配诊断 | 每轮固定 40 次完整成功 HostBound 调用，4 次有限预热在单独窗口；每次分别判定 0，不以总数/均值抵消 |
| 正式 Ready 时延 | 无分配计数的 Release、轻量时延模式独立进程/配对；保留阶段协议，禁高频内存/线程采样，不能混入诊断轮分位 |

高频占用模式覆盖 Debug、Release、ASan；计数模式覆盖这些配置的各自有效通道；轻量 Ready 时延仅无计数 Release 形成正式时延统计。配置/模式不得混合配对，新增模式要有自己的 baseline 和预算键，不为证明提速重复已验收历史矩阵。

原始样本不挑选、不用两个全局最小值相减。内存窗口报告 count/min/median/p95/max；分位采用 nearest-rank，保存 n。每轮 B 与同 ABBA 块最近的唯一 A 配对（第一个 B 配前 A，第二个 B 配后 A），保留有符号差值，负值不截零。12 轮不能作为可靠 P99/SLO；报告只展示已定义样本统计。少轮、重复、身份不同或缺样不得从剩余好样本中重算为通过。

新进程只表示进程状态重建，不代表 OS 冷缓存。cold-OS 未受控则 NotMeasured。记录 OS build、CPU/核数/RAM、虚拟化、活动安全工具、负载、CRT/ASan/LTO及第一次/后续运行顺序；不重启用户机器、清 OS cache、改 affinity/电源或定时器分辨率。任何异常噪声剔除必须预先成为方法规则，否则保留并报告；不得看结果增减重复次数。

运行 deadline、setup/stage deadline、样本和模块容量、校准区域字节/保持期限都是方法配置的必填有限输入，启动前校验并绑定摘要；不是占用上限。缺失、0/无限哨兵、乘加溢出、NaN/Infinity 拒绝。setup/stage 限制只能提前失败，不扩大原运行 deadline；改变任何方法输入须产生不同方法身份和成对 baseline，不能在同报告临时补值。

## 4. 阶段与释放所有者

固定消息阶段为：ProcessMainEntered → HostConstructionBegin → HostReady → WarmupComplete → IdleSamplingBegin → ShutdownBegin → ShutdownComplete → BoundReleased → SessionReleased → OwnersReleased → ExitPermitted。baseline 发出同构阶段。失败启动不能发 Ready；超时/未排空不能发 ShutdownComplete；父只能确认合法阶段，不能替子补写。

ShutdownComplete 时仍保留 Bound、Session、Host 和模块配置/服务 owner，先观察 500 ms；BoundReleased 释放全部外部 Bound，再观察 500 ms；SessionReleased 释放外部 Session，再观察 500 ms；OwnersReleased 释放 Host、注册/认证装配和其他外部 owner，观察 500 ms 后允许正常退出。每个阶段都记录实际拥有列表、销毁哨兵、线程及内存，不把“尚有旧 Bound 的不可变控制块”误称在途业务。

默认管理日志 owner 位于 Host 内部，最后释放阶段与 Host 内部状态一同释放；不能虚构可独立释放的私有诊断句柄。另设**非标准 baseline 的寿命专项**：通过合法公开 logging_factory 装配同一生产内存日志实现，测试装配者保留管理读取 owner；在 Host/Bound/Session 已释放后确认仅该 owner 保留公共日志状态，再释放并核对最后析构。此专项单独标记 retained_management，不将其额外 owner 成本混入标准默认模式差值，不增加公共 getter。

shutdown 后不再访问借用 ModuleContext/SafeLogger，亦不从 Host.copy_logs 推造 flush 回执。正常停止的日志 close 由 Host 管理；只读日志在 owner 有效时通过公开合同使用，释放最后 owner 后禁止解引用旧页/端口。外部保留、CRT 缓存、工作集策略、ASan quarantine 分开解释；不得调用 EmptyWorkingSet、purge allocator 或主动 trim 美化数字。进程退出后 memory API 字段记 process_exited/不可测，不补 PrivateUsage=0。杀进程后的 OS 回收不是 Host 成功停止。

## 5. Windows 原始指标

| 指标 | 精确口径 |
|---|---|
| Private Bytes | PROCESS_MEMORY_COUNTERS_EX.PrivateUsage，私有提交 bytes，不是驻留物理内存 |
| Working Set | WorkingSetSize，含共享驻留页，不与 PrivateUsage 求和 |
| 启动高水位 | Ready 屏障时的 PeakWorkingSetSize、PeakPagefileUsage 分列；后者名为 peak commit，不当作磁盘 I/O |
| sampled maximum | 逐点 PrivateUsage 最大值，不能代替生命周期高水位或声称捕获所有瞬时峰值 |
| 内部 Ready | 子 HostReady ticks − 子 HostConstructionBegin ticks |
| 创建到 Ready | 子 HostReady ticks − 父调用 CreateProcessW 前 ticks |
| 父观察 Ready | 父收到合法 Ready 的 ticks − 父创建前 ticks；通知/调度滞后单列 |
| 线程 | root PID在固定边界的真实线程ID集，记录边界观测峰值、Ready空闲和各释放阶段；全局 Toolhelp 快照必须按实际 PID 筛选 |

保留内存与线程查询各自起止ticks，以及每次Win32成功位、错误码、结构长度、原始整数、QPC ticks/frequency。查询失败为缺样和原因，不填 0、不复制旧样本冒新时刻。Ready 标记与父读取高水位有实际时间差，记录延迟，不宣称精确截断在一条指令。QPC 仅用于同机间隔，UTC 用于审计日期；跨线程一个 tick 内顺序不作强结论。

线程差值 0 必须逐轮成立，并结合 Host/日志/Native 路径无线程创建/异步装配的结构性证据。固定边界线程快照只能叫边界观测峰值，可能漏掉阶段之间的短命线程；不靠平均/min 消除额外线程。出现负差值或不明线程归属先归因，未知不算通过。专项外部生命周期追踪可诊断，但它有自己的权限/丢事件/开销记录，不能冒称本方法已具备。

## 6. 分配整窗与有效性控制

固定场景采用小型定长 Args/R（≤1 KiB）、真实已注册/绑定、无资源声明、无业务堆分配、内存易失调用。每次计数从 HostBound.invoke 之前开始，覆盖 HostAdmission、每次 Policy/目标/线程/参数检查、原 Native 返回验证、D1.05 有界事实环、完整返回对象构造、调用返回后结果析构，最后读数。不得把框架工作移出窗口、后台执行或删校验；不为记录测量结果在开启的窗口中分配。

保持普通内存生命周期日志开启，但正常 invoke 不新增普通日志；启动/关闭日志成本计入相应生命周期窗口。首次调用、注册/bind、4 次预热、错误详情、可变结果、释放与长期峰值分别记录，不承诺这些全部为 0。40 次每次真实成功值正确且框架新增分配 0；任何计数器失效先使窗口无效，不允许免责声明后继续通过。

原样复用 D1.05 受审分配通道边界：12 个适用入口的独立确定分配正探针、无分配负探针、真实 new 注入 red、ASan hook 注册/槽满失败控制，保存各配置真实可观察范围。替换 C++ new 的驻留字节不等于 PrivateUsage；Debug CRT、Release null、ASan 独立 hook 不相加；ASan 原 CRT 不可观察字段不能解释为零。后续出现自定义分配器须先补计数覆盖，不隐去来源。

必须运行独立校准消费者：有限 VirtualAlloc 提交并逐页触碰区域/释放，配无新增区域负控制；一条测试线程保持至父采样确认后 join，配只有原主线程负控制；受控 Ready 延迟能出现在定义时延中；实际必需测试 DLL 的遗漏能被分发集合核对发现。校准参数在该轮前显式固定并保存；pilot 验证可观测性和噪声，不能以一次 WorkingSet 未立即下降认定泄漏，也不能按本次结果自动调大区域直到“成功”。

## 7. 唯一进程 owner 的有限观测扩展

在现有五参数 execute 上扩展为 `execute(argv,cwd,stdout,stderr,timeout,*,observation=None)`，新增参数仅关键字；该签名现已实现；下面描述现行行为。None 分支保持原创建/归属/恢复、ResumeThread 后运行 timeout 起点、10 ms 轮询、返回/异常语义及原始流；不额外创建测量通道、线程或文件。现有真实进程反例继续证明该分支没有回退。

observation 只能是已验证的不可变有限方法配置，不能携带任意 callback、Python 表达式、命令、外部 PID/handle 或忽略错误开关。`windows_process.py` 的受审查询通过 process.py 的同一 owner 循环执行，不 sleep、不启动子进程/线程、不递归 execute、不等待消费者退出。统计/重格式化/hash 在进程结束后做。给任意同步 callback 加超时数字不能使其可抢占，本方法不提供这种扩展。

owner 暂停创建后先归属独占 Job，再向内部 ChildObservationScope 发布 `(run_nonce,generation,actual_pid,owner_process_handle)`。scope 不公开裸 HANDLE/Job/线程/终止能力，读取使用原创建句柄，不按 PID 重新附着。scope 仅作本轮只读查询和固定阶段确认；退出后永久失效，晚到调用拒绝 ScopeClosed，不重新绑定复用 PID。

固定共享记录和有限事件由 owner 创建，记录先写完再发阶段事件；输入包含版本、nonce、真实 PID、严格阶段序号、ticks、结果校验。stdin 保持 devnull，stdout/stderr 仍是原生继承文件句柄直写的独立 xb 原始字节，不能改成解析日志即 Ready 的管道。观测分支采用明确句柄继承允许列表，仅包含标准句柄和本轮固定通道，不顺带继承父其他句柄；None 分支不借此改变旧行为。

owner 时点固定为：创建前准备及 pre_create QPC → 创建返回 → assigned_suspended 首次查询 → resumed → 每次 poll 先处理退出/绝对期限再做到期有限查询 → Ready 高水位后确认预热 → 阶段采样 → root_exited 停止查询/确认 → Job drained → scope 失效/关闭句柄。不能先 Resume 再纳入 Job，不能让采样或阶段等待延长 timeout，也不能把 root 退出后 Job 里其他进程当 root。

准备失败关闭本次资源；已创建未归属时只终止本次创建句柄；归属后观测异常/协议错/容量满/阶段超时进入同一 owned Job 有限终止及排空路径。记录真实根退出码、状态、采集失败原因、清理错误、actual active_after；不能只依靠 KILL_ON_JOB_CLOSE 推定已排空。root exit 0 但观测未完成仍为测量 Incomplete/Failed。采集器所有异常必须收敛到该清理路径，不仅捕获原有 OSError/ValueError；finally 先使 scope 失效再关句柄，不能吞首因或补造清理成功。

观测结果采用独立错误字段表达 ObserverFailed/ObservationIncomplete；正式 evidence 状态/schema 的接入和允许字段随本包共同审核，不私自把未知状态塞进现有校验器。采样失败日志独立于消费者原始流；观测失败不能覆盖业务退出事实，业务 exit 0 也不能覆盖采集失败。

## 8. 固定验证职责与原始证据

正式 expected 主名/配置映射随 D1.06 合并 SPEC 一次登记，不从发现列表反推；以下职责不可删除：

| 职责 | 必须检出的反例 |
|---|---|
| 真实及安装 Native 消费者/禁依赖 | 假 Ready、错业务结果、只挂未引用库、源码 include、移除必需组件、引入禁止组件 |
| 体积/分发/配置身份 | 少算必需 DLL、静态库冒最终增量、binary/CRT/LTO 不同却配对 |
| 内存/线程/时钟校准 | 空计数器、查询失败补零、错 PID/未筛全局线程、隐藏额外线程、延迟未被计入 |
| 阶段/重复/窗口 | 错 nonce/版本、重复越序或未来 ticks、缺 Ready/少轮/挑样、Ready 后崩溃、预热污染 Ready 高水位 |
| 分配整窗 | 每适用入口正负探针、注入分配、hook 失效；少于 40 次、未含结果析构或关闭原治理 |
| shutdown/owner | 未排空仍发 Complete、仅 kill 代替停止、保留引用却宣称已释放、最后 owner 后回调/读取 |
| owned 进程观测 | 创建前/未归属/暂停归属/运行/Ready/root退出后故障，同名外部 sentinel 不受影响；root退而子孙活、scope迟到、容量耗尽、任意callback、超时被采样延长 |
| 原始流/摘要/预算 | NUL/非UTF8损坏、流混写、缺样/伪hash/重复run、缺Job结尾、缺预算/自动抬线/错误方法键 |

每轮独立目录保存 source commit/dirty/实际输入清单、方法配置、工具/依赖/环境、configure/build/install/运行 argv/cwd 与原始流、实际二进制/模块摘要、nonce/PID/Job 顺序、所有阶段 ticks、逐点采样/错误、实际退出和清理。expected/discovered/executed 分别保留，缺测试/少轮/跳过必需控制拒绝。失败重跑追加，不覆盖历史。analyze 只从原始材料计算，AI 记录审核决定而不手写机器 Passed。

## 9. pilot、有限预算与 G1

本合同不填写任何 bytes/ms 占用上限。方法与测试先合入本包 SPEC；实现后按 3 块 pilot 取得真实控制、样本和结构性事实，无批准预算时保留 budget_status=NotApproved。随后沿既有 AI 自动政策，根据 pilot、环境和阶段需要，对 `footprint-budgets.json` 的 NativeSubset 有限上限作有来源的独立决定，不新增人工节点或平行批准链。

预算键至少包含方法版本/摘要、NativeSubset、配置/架构/CRT/ASan/LTO、counter_mode、logging_mode、工具/消费者/依赖/环境摘要及 pilot 来源；每指标列单位、absolute/paired increment/sample max/lifetime peak 等统计含义、有限上限与 AI 依据。不得把本轮值乘系数/加余量自动当批准，不允许负容量、NaN/Infinity、缺字段或无限上限。

预算配置必须先于最终新报告存在，正式 6 块使用新进程/run_id；不能回填时间或改写 pilot。超预算保持 Failed，归因/修复或在同包内形成有证据的合同变更，不每轮抬线。新增线程 0、成功整窗分配 0 已是硬约束，不待 pilot 调整。最终 G1 分别记录硬约束、有限预算、未测/无基线、机器结果、AI SPEC/CODE/预算来源；材料未齐不 Passed，也不以 NativeSubset 代表完整 Embedded。

## 合并来源

- `evidence/bootstrap/D1.06/footprint-design-candidate.md`：`296097d9a509982d2c163c726333e50f8a5ee8cd8d36cfc038a6871a641059a5`
- `evidence/bootstrap/D1.06/footprint-process-hook-candidate.md`：`d648c2bcd81a9c98a8a03648eff80f6040520b19740007b613a4a178faa3359e`
- `docs/01_Architecture_v3.3.md`：`f20644428ed8c307109e26fe690e951f7e191659903bb496ba8dee51125bca2d`
- `docs/02_Execution_Plan_v3.3.md`：`7daf3c8a7e68462a97cd593d2ab9a35c1888b720428c72bc9f915049050df511`

以上合并来源为最初 Candidate 的历史身份；当前观测、分配及单对消费者已实现，正式 pilot/预算/验收状态以 progress 和实际新报告为准。原候选中的 Logging §7 意见由日志合同收口，不把测量方法变成第二份日志 API 定义。

采样v2沿同一进程owner执行查询，耗时计入原绝对运行/阶段截止。旧v1轮次只保留诊断，不作为v2 pilot；新配置/工具/消费者/baseline及预算身份必须同步绑定。调整线程采样不放宽新增线程0；真实系统/CRT基线线程必须归因，不预填仅主线程。

轻量时延的身份细化已通过[有限SPEC复核](../../evidence/bootstrap/D1.06/footprint-latency-identity-spec-review.md)：latency的memory_sampling=phase_boundaries、sample_interval_ms=null，仅阶段查询，禁止periodic样本；occupancy/allocation保持due_5ms及5，摘要不变。旧latency错误标签不重新解释为新模式数据。

# D1.06 NativeSubset footprint 方法与固定验证候选

本文是候选设计，不是实施批准、预算批准或测量报告；未运行 D1.06 pilot，未填写任何占用预算 Passed。依据为架构 v3.3 的 A19、A21.4–A21.6、A22，以及执行计划 D1.06。工作包前置状态由主任务另行核对，本文不以父任务状态通知替代正式前置证据。

## 1. 对象和不可扩展的边界

对象固定为 `NativeSubset`：新最小 Host、已实现 Native Read/Compute、真实注册与绑定、认证/授权/目标检查、Outcome，以及有界内存诊断/日志。使用 C-A 的原生子集，明确异步能力尚不可用。公共 Host/Native API 的命名、目标和导出范围由主任务设计，本候选不另造公开接口。

不得以关闭已承诺治理、关闭内存观测、恒定返回成功或挂接未被引用的库得到占用结果。不引入 Data/jsoncons、State/immer、SQLite、Asio、假 Task、空 Document、CLI/RPC/JSON 协议执行链或产品模块。进程间仅允许测试启动器的固定测量标记/阶段控制，业务始终在消费者进程内通过真实 C++ Host/Native API 完成，不能转交另一个 CLI 进程执行。

`NativeSubset` 不是 D3.07 的完整 `Embedded`。标准记录保留架构规定的 `cpu_workers=2` 配置语义，同时明确 NativeSubset 实际 CPU worker 实例数为 0、异步能力 unavailable；不能据此启动两个线程。内核新增线程硬约束为 0，不使用未来 Embedded 的 ≤3 线程预算。

## 2. 候选工具及等条件消费者

候选开发工具布局如下，名称均待主任务确定；不随 SDK/产品部署，也不进入每请求路径。

| 候选产物 | 必须实际承担的工作 |
|---|---|
| `tools/footprint/run.py` | 固定矩阵/重复顺序，启动真实进程，采样，归档原始数据；不只解析一行“通过”日志 |
| `tools/footprint/windows_process.py` | 复用已受审独占 Windows Job 的暂停创建、先归属后恢复、超时及退出清理，增加测量钩子；不复制一套放松的进程管理 |
| `tools/footprint/analyze.py` | 从不可覆盖的原始数据计算绝对数、成对差值、分位、最大值；检测缺样和身份不一致；不得生成或上调批准预算 |
| `tools/footprint/controls/` | 独立内存、线程、Ready 延迟、模块增量与采集器失效正负控制；仅测试工件 |
| 等配置 baseline 消费者 | 与 NativeSubset 共享固定测量 shim、标记布局、时钟、主线程等待协议、CRT/构建选项；执行最小有观测结果的本地计算，但不链接 OCK Host/Native |
| NativeSubset 消费者 | 真正创建/启动 Host，注册绑定 Read/Compute，返回正确值；拒绝非法输入与不可用异步，使用真实内存日志，执行 shutdown 后释放所有外部所有者 |
| SDK 安装消费者 | 只使用安装前缀中的公开头和导出 target，独立配置、编译、链接、运行真实 Host/Native；不包含源码树的 private header 或 tests fixture |

测量 shim 使用固定大小共享记录及有限数量的继承事件句柄，包含版本、随机 run nonce、PID、阶段序号、QPC ticks、业务结果校验摘要。父进程只继承明确允许的句柄。子进程使用自身主线程等待阶段指令，不为测量自行创建内核线程。共享记录先写完再发出阶段事件，父进程核对 nonce、进程身份、阶段单调性；stdout/stderr 只作另外保留的诊断，不能充当唯一 Ready 证据。shim 本身的内存、句柄、等待与输出策略在 baseline 和 NativeSubset 中相同。

baseline 不通过一段空 main 糊弄差值，也不能链接同一完整内核后关闭业务来抵消其实际成本。两者均运行相同外部测量协议、相同数量的固定标记，并在测量窗口后输出摘要。若计数构建需要替换分配器，两者都携带相同探针代码；正式 Release 时延/体积构建两者均不携带计数探针。

## 3. 构建与真正链接增量

每对消费者必须绑定同一 MSVC/Windows SDK、架构、C++ 模式、expected 后端、CRT、优化、LTO、链接选项及依赖锁。建议首轮采用已验证的 Debug `/MDd`、Release `/MD`、ASan Debug `/MDd + /fsanitize=address` 三配置，LTO 明确为关闭；从实际 compile/link command 和工具链报告核验，不按配置名猜测。若增加 `/GL`、`/LTCG` 或静态 CRT，必须建立独立 baseline、pilot 和预算键，不能跨模式抵扣，也不把不受工具链支持的组合列为 Passed。

运行能力证明至少包括：Host Ready 前拒绝业务、Ready 后 Compute/Read 校验实际结果、一次非法输入未进入 handler、一次内存日志接受/查询/flush 范围校验、shutdown 完成且之后拒绝业务。具体调用形式待公共 API 设计；这份证明必须来自被测 EXE 的实际运行，而非另一个测试进程。建立可观察的结果依赖，结合链接命令、map/符号或可复核的节贡献，防止优化器将未使用能力移除。安装消费者还应核对编译命令不借用源码目录，安装目标图无禁止组件。

体积报告同时列出：

- baseline 与 NativeSubset 的最终 EXE 精确字节、SHA-256，以及有符号差值。
- 各自运行这些声明能力所必需的新分发模块清单：规范化路径、角色、字节、SHA、导入关系、实际已加载证据。以两份必要分发集合的总字节相减计算包增量，共有的完全同身份模块抵消一次，不能重复算 CRT。
- OS 系统 DLL 记录为环境依赖，不假称 SDK 新分发模块；CRT/ASan 运行时是否为本配置必要随包模块及实际分发条件单列。Debug/ASan 的调试分发统计不冒充正式发布许可或 Release 占用承诺。
- PDB、静态 `.lib`、对象、中间文件、头文件分别列示，不加进“最终 EXE + 必要分发模块”数值。

静态导入/链接清单与真实模块枚举相互校验；在 WarmupComplete 与最终能力运行后取稳定快照，处理延迟加载，不能把“此时没加载”当成永远不需要。64 位父工具使用 `EnumProcessModulesEx` 与 `GetModuleFileNameExW`，校验返回数组容量、路径截断及模块变化；有限重试仍无法取得完整快照时报告 Incomplete。枚举得到的 HMODULE 不是可关闭的独立句柄。仅记录这一组受管子进程，不按系统进程名称附着。[模块枚举](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-enumprocessmodulesex)、[模块文件路径](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getmodulefilenameexw)

## 4. 单轮阶段及 Windows 口径

建议用同一个真实消费者明确发出下列标记：`ProcessMainEntered`、`HostConstructionBegin`、`HostReady`、`WarmupComplete`、`IdleSamplingBegin`、`ShutdownBegin`、`ShutdownComplete`、`OwnersReleased`、`ExitPermitted`。失败启动不得发送 Ready；已发送标记不能靠父脚本补造。

HostReady 标记后，子进程主线程先在测量 shim 的阶段屏障等待父确认；父取得 Ready 高水位后才准许有限预热，避免把预热分配混入启动至 Ready 的峰值。baseline执行同一屏障。这个等待发生在 Ready ticks 已写入之后，等待时长另记，不算作内部 Ready 时延。

### 4.1 内存与启动峰值

父启动器持有创建所得目标进程句柄，使用 `GetProcessMemoryInfo` 的 `PROCESS_MEMORY_COUNTERS_EX`，记录函数成功位、错误码、结构长度、原始整数与采样 QPC 时刻。权限/结构/采样错误为缺失值加原因，不能记 0；API 要求进程查询权限，现有创建句柄仍需实际核验可用。[GetProcessMemoryInfo](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getprocessmemoryinfo)

| 字段 | 候选报告含义 |
|---|---|
| `PrivateUsage` | 当前进程的私有提交量（bytes），作为 Private Bytes 口径；不是“当前驻留物理内存” |
| `WorkingSetSize` | 当前工作集（bytes），包含共享驻留页；不改名为 Private Bytes 或与之求和 |
| `PeakWorkingSetSize` | 进程截至读取时的工作集峰值；Ready 标记处读取的高水位用于启动至 Ready 的峰值证据 |
| `PeakPagefileUsage` | 进程截至读取时的生命周期 commit 峰值；另记名称为 peak commit，不解释为实际磁盘 pagefile I/O |
| 采样序列的最大 `PrivateUsage` | 明确叫 sampled maximum；不能代替 API 的生命周期高水位，也不能宣称捕捉到所有瞬时峰值 |

上述字段含义依 Windows 结构定义；首次 Ready 高水位读取与标记之间仍有采样误差，报告实际读取时刻和延迟，不声称精确截断在 Ready 那一指令。[PROCESS_MEMORY_COUNTERS_EX](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex)

候选采样方法参数（不是占用批准预算）：从恢复线程开始，每 5 ms 尝试一次进程内存采样；Ready 后执行固定 4 次完整有限预热及固定能力证明，再静置 500 ms，随后以相同频率采样 1000 ms 空载窗口。保留请求与实际时间间隔、丢样/抖动，不将“设置 5 ms”说成精确实时采样。报告启动至 Ready、Ready 至预热完成的各段采样值及高水位，不能将预热增长移出报告。时间参数需在 pilot 后作为方法配置冻结，若改变则重新取得对应 baseline。

Ready 空闲统计包括 count、min、median、p95、max，原始采样不抽样丢弃。每轮 Native 与相邻匹配 baseline 分别保留绝对统计，并计算同一统计定义的成对有符号差值；负差值原样保留，不截断为 0，不用“两个全局最小值之差”制造优势。

### 4.2 Ready 时延

使用同机 `QueryPerformanceCounter` / `QueryPerformanceFrequency`，保存原始 ticks 和频率：

1. 内部 Ready 时延 = 子进程实际 `HostReady` ticks − `HostConstructionBegin` ticks，覆盖真实 Host 构造/初始化。
2. 创建至 Ready = 子进程 `HostReady` ticks − 父进程调用 `CreateProcessW` 之前的 ticks，包含创建、暂停归属 Job、恢复、装载和初始化。
3. 父观察时延另列 = 父实际接收 Ready 事件的 ticks − 父创建开始 ticks；同时列出通知/调度滞后，不能混作纯 Host 时间。

父另记 CreateProcess 返回、AssignJob、ResumeThread ticks，避免把测量脚手架开销解释为内核开销。QPC 用于同机/同虚拟机跨进程间隔；跨线程差一个 tick 内的顺序不作强结论，UTC 仅标记审计日期，不参与性能差值。[QPC 官方说明](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)

正式 Ready 延迟采用与 footprint 消费者等语义的无分配计数 Release 构建；另外保存 Debug/ASan 和有计数构建的诊断时延，但不混入正式 Release 分位。建议将高频内存/线程采样轮与轻采样时延轮分开，协议和业务不变，各有匹配 baseline 与独立方法键，清楚报告外部采样干扰。

### 4.3 线程硬约束

只在父进程枚举线程，按本轮进程句柄/PID身份筛选 `THREADENTRY32.th32OwnerProcessID`。`TH32CS_SNAPTHREAD` 的 PID 参数本身不会将列表限制在一个进程，必须实际筛选；保存阶段线程 ID 集与计数，不能只读一个总数。[CreateToolhelp32Snapshot](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot)

每对同配置消费者记录启动观测峰值、Ready 空载计数及线程集合、shutdown 完成后的计数与集合；NativeSubset 的对应 baseline 新增线程目标为 0。必须保存有符号原差值及每轮结果；不能按 CPU 逻辑核数重设预算，也不能把实验后端的线程混入标准内存观测。

快照最大值只能称“已采样线程峰值”，可能漏掉采样间的短命线程。结构性硬门禁还须核对最小 Host/日志/Native 路径无内核线程创建、无异步 worker 装配、无 timer/file logging 线程；若出现无法归属的进程线程差异，需独立追踪，不能仅取最小轮或平均后宣称 0。需要严密排除短命线程时，可增加受审的外部线程生命周期追踪作为专门诊断，其权限、丢事件和环境成本另审，不把尚未实现的追踪写成当前保证。

候选验收采用对应阶段重复观测的基准差值 0 加结构性无内核线程证据。负差值/不稳定差值保留，先归因工具链/加载器/外部注入/采样问题；未知归属不自动通过。测量控制器可创建一条明确持有到采样确认才退出的测试线程验证计数器，但该线程只存在于单独正控制进程，不能成为标准 NativeSubset 的一部分。

### 4.4 shutdown 与释放曲线

子进程真实调用 shutdown、flush 已接受范围、排空回调，依次释放 Bound、Host、Registry/认证及观测所有者；主线程继续存活固定 1500 ms 供父采样，然后允许进程正常退出。保留 ShutdownBegin、ShutdownComplete、OwnersReleased 前后完整 PrivateUsage/WorkingSet 曲线、线程集合、句柄/回调生命周期证据及现有替换 new 通道保留块数。

不能用“进程杀掉后 OS 必然回收”代替 Host shutdown 验证。进程退出后的 memory API 不再代表有效可采样对象，应记 `process_exited` 而不是虚构 PrivateUsage=0；独占 Job active_after=0证明进程树清理。CRT 缓存、工作集策略、ASan quarantine可能不立即下降；保留残余和解释，不自动 flush/trim/调用 EmptyWorkingSet 或 purge allocator 来美化标准数据。异常退出/超时归入单独故障证据，不能混作正常释放。

## 5. 有限预热、重复与环境分层

pilot 候选先每配置执行 3 个 ABBA 块（每块 baseline/native/native/baseline，均为新进程，共 6 轮每种消费者），检查方法可用、噪声、采样开销、实际模块和字段完整性。正式验证候选采用 6 个 ABBA 块（每种 12 个独立进程），也可采用预先冻结 seed 的成对平衡 AB/BA 顺序；不得看见好结果后延长/终止采样。具体次数和顺序须经方法审查再冻结，不能自动根据本次失败加轮直到通过。

每个 Native 进程仅进行固定 4 次预热，明确 Args/R 小型定长、无资源声明、观测容量和静态标签等配置；每次都校验实际成功结果。观测容量为有限配置字段，来自装配设计，不以无限预分配换零分配。计数版另运行受审 D1.05 四项探针和固定稳态循环，保留 first/bind/warmup/error/variable/release各窗口；新的 Host/日志成本必须在已承诺治理均启用时计量。

“新进程”只表示进程本地状态重建，不等于 OS 磁盘缓存冷。保存顺序、首次运行与后续运行条件；未控制 OS cache/reboot 的 cold-OS 指标明确 NotMeasured，不用空目录冒充冷启动。候选不重启用户机器、不清 OS cache、不调整系统定时器分辨率/CPU affinity/电源策略。环境记录 OS build、CPU、核心数、RAM、虚拟化、活动安全工具、工具链、CRT、ASan、LTO和可获得的负载信息；异常噪声按预先定义规则处理，原样保留，不能事后挑选样本。

小样本 p95 明确算法（例如 nearest-rank）和 n，max 原样报告；12 轮的 p99没有可靠尾部估计含义，不当成服务 SLO。结构性 0 分配/0 新增线程按每次验证，不用统计平均数替代。

## 6. 计数器有效性和候选固定用例

下列是测试建议名，不是已经批准的 expected 清单；主任务须在实施前冻结最终主体、分配置适用矩阵及所需 child artifacts，实际发现与 expected 独立产生。

| 候选固定主体 | 正常证据与必须拒绝的反例 |
|---|---|
| `T23.footprint.real_native_consumer` | 真正运行声明的 Host/Native/日志行为；仅链接未引用库、伪造 Ready 或错业务结果必须失败 |
| `T24.footprint.installed_consumer` | 只用已安装公开 SDK 完成同一行为；移除真实必需组件/偷用源码私有头不得仍成功 |
| `T01.footprint.forbidden_dependencies` | actual target/import/module 闭包核对；注入任一禁止组件必须被检测 |
| `T23.footprint.binary_increment` | 确认 EXE与必要分发模块字节/摘要、共有模块抵消；故意少算一个必需测试 DLL 或只量静态库被拒绝 |
| `T23.footprint.memory_counter_control` | 独立进程使用有限 VirtualAlloc 提交并逐页触碰固定区域，确认 PrivateUsage与驻留变化方向/可观测性；测量失败不能变0；无新增块负控制验证无人工加值 |
| `T23.footprint.thread_counter_control` | 独立测试线程持有到父确认，检测新增1再join回落；普通主线程负控制没有该增量；错误PID/不筛选全局快照被拒绝 |
| `T23.footprint.ready_clock_control` | 固定受控延迟在两个时延中出现；错nonce、重复/越序Ready、非单调ticks、Ready后立即崩溃均不得伪造成功 |
| `T23.footprint.startup_ready_memory` | 原始startup/Ready采样和高水位完整、两种内存口径分列；缺样/结构长度错/访问错误被拒绝 |
| `T03.footprint.native_thread_delta_zero` | 标准NativeSubset无内核线程创建且阶段基准差值0；意外额外线程不能以worker配置或平均数掩盖 |
| `T23.footprint.finite_warmup_allocation` | 复用D1.05已审12入口独立正probe、无分配负probe、new注入red、ASan hook槽满失败；每次Native成功值及治理均校验 |
| `T22.footprint.shutdown_reclaim` | 真实shutdown与所有者释放曲线/回调排空；残留回调、线程、超时退出或只kill进程不可通过 |
| `T23.footprint.interleaved_repetition` | 固定次数和交错顺序均实际执行；少轮、挑样、不同binary/CRT配对被拒绝 |
| `T23.footprint.budget_provenance` | 验证预算批准时间/方法/source/配置绑定先于本次报告；未批准、无限/NaN上限、自动本次值加余量、错Profile不可通过 |
| `T23.footprint.evidence_integrity` | argv/cwd/Job归属/退出/raw hash/采样/阶段/发现执行清单齐全；截断、重复run、伪造hash、缺进程树结尾被拒绝 |

内存控制的固定区域大小与有限保持时间属于计数器校准协议参数，需由 pilot 证明高于实际噪声并冻结；不把某一次 OS WorkingSet 没立即下降等同于泄漏，释放主证据是实际 VirtualFree/进程状态与PrivateUsage趋势。VirtualAlloc的提交与实际物理页使用不同，逐页触碰用于建立明确驻留正控制。[VirtualAlloc](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc)

D1.05 分配通道覆盖必须原样继承受审边界：替换 C++ new 的驻留字节不是进程 PrivateUsage；普通 Debug CRT、Release null、ASan 独立 hook不相加，ASan原CRT不可观察字段不解释为无分配。计数器失效应阻断使用该计数结果，不可仅增加一行免责声明后通过。

## 7. pilot → 独立预算审批 → 新运行验证

1. **先审方法与固定合同**：确定实际能力、环境、工件边界、采样、重复、构建模式、原始schema和固定测试；本候选只是输入，不自动成为 Approved。
2. **实现后 pilot**：运行真实 baseline/NativeSubset、工具正负控制与全部口径，保存来源/构建/数据。pilot可以得到有效测量及结构性事实，但没有预算批准时 `budget_status=NotApproved`、包级验收不得 Passed。
3. **独立 AI 审批有限预算配置**：根据 pilot、目标环境和阶段需求审查明确的 bytes/ms上限及适用统计（absolute、paired increment、sample max、lifetime peak分开）。`footprint-budgets.json` 的 NativeSubset项必须绑定方法/工具/消费者/构建/依赖/环境摘要、pilot来源、审批依据与 actor_type=AI。不能用本次实测乘系数或加固定余量自动变成“批准”；每个数字须有独立决定。线程新增0、固定成功调用新增分配0沿用规范硬约束，不等待可调预算。
4. **预算冻结后新报告验证**：新run_id、独立进程重复、实际计算结果，并核对批准配置先于本次报告存在。不能回填批准时间，再把pilot改写成通过。超预算保持Failed，先归因/修复或另行受审合同变更，不自动涨预算。
5. **G1汇总**：机器结果、包级状态、AI规格/代码/预算审核来源分别记录；未测、无基线、预算未批、NotApplicable保留真实含义。NativeSubset预算不得充当Embedded预算；后续热路径/装配变化必须按A21复测受影响项。

建议预算键至少含 `profile=NativeSubset`、configuration、architecture、CRT、ASan、LTO、counter_mode、logging_mode、方法版本、消费者摘要；每个有量纲指标有unit和有限上限。JSON以整数bytes/ticks或有界明确精度的ms表示，拒绝负容量、NaN/Infinity、缺字段、过期/不匹配审批。候选不填写任何具体占用上限。

## 8. 原始证据与SDK表面关联

每轮输出独立目录，保存source commit/dirty/实际快照清单、工具和依赖版本、configure/build/install原始命令与流、二进制与分发集合摘要、基准身份、参数、nonce/PID/Job归属、阶段原始ticks、逐点memory/thread样本、模块枚举及error、正常/异常退出、父清理结果。计算后报告引用这些文件SHA；失败重跑追加，原始记录不覆盖。

expected、本次实际discovered和executed三清单分别保存，零测试/缺用例/缺轮次失败。报告计算程序只从原始材料得出自动结果，AI只写审查与预算依据。机器结果禁止手写通过。按A19记录新公开头/target/宏与依赖表面及声明评审；D1.06安装消费者建立本次开发样本，不存在上一正式版本时明确记录，不虚构已发布ABI兼容性证明。

本文仅提交候选方法和固定用例建议。尚未实现启动器、采样器或预算配置，尚未进行pilot，全部数值通过结论留待后续真实证据与独立审批。

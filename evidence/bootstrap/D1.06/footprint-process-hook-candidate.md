# D1.06 进程观测扩展与 Logging 候选只读复核

状态：候选接口设计与只读意见，不是实施批准；没有修改 `tools/evidence/process.py` 或 Logging 候选，没有执行新反例或声明实现 Passed。

## 1. 现有进程所有权必须保留

已逐段阅读 `tools/evidence/process.py`。当前 execute 的真实顺序是：独占创建原始 stdout/stderr 文件 → 创建带 KILL_ON_JOB_CLOSE 的 Job → `CreateProcessW` 以隐藏、暂停标志创建 → `AssignProcessToJobObject` → 设置 assigned_before_resume → ResumeThread → 关闭初始线程句柄 → 循环观察进程/Job → 超时只终止自己的 Job 并限时排空 → 查询退出码和实际 active_after → finally 关闭句柄。创建成功但尚未归属 Job 时，错误清理只终止这次创建的进程。

候选扩展只能把观测时点嵌入这条实际路径。不能在 footprint 工具另写 Popen＋按 PID 杀进程的简化版本，不能等 execute 返回后才猜启动内存峰值，也不能先运行消费者再附着到同名进程。进程创建、恢复、超时、终止、排空及句柄关闭始终只有 process.py 一个所有者。

现有 execute 只有五个位置参数且完成后返回，不能提供 Ready 前采样。建议新增一个**仅关键字、默认 None** 的有限观测参数，概念形状为 `execute(..., timeout, *, observation=None)`。当其为 None 时，不创建测量通道、观测线程或额外文件，不改变现有 timeout 起点（当前在 ResumeThread 后）、轮询节奏、返回字段、异常分类和原始流写法；用现有真实进程合同回归证明这一点。

## 2. 首选有限观测方案，不开放任意同步回调

首选把 observation 定义为经过验证的不可变有限配置和受工具实现约束的观测 scope，而不是允许用户输入任意 Python callback：包含 schema/method版本、采样字段枚举、有限频率/样本上限/阶段超时、允许的阶段图、容量有界的测量通道描述和独立输出位置。不能携带任意 PID、进程句柄、待执行命令、Python表达式或“忽略错误”函数。

在 process.py 的同一个 owner 循环内，少量时点通知交给开发工具的受审观测实现；可由内部适配器表达为 `prepared/assigned/resumed/poll/root_exited/drained/finalized`，但它不是可以运行任意产品回调的公开扩展系统。该实现只能执行有界的本轮 Win32 查询、读取固定测量记录、推进已声明阶段以及追加自己的原始采样文件，不能 sleep、等待消费者退出、调用 subprocess、递归 execute、创建线程或获得终止能力。较重的统计、hash、格式化汇总全部放在 execute 完成后。

这样扩展的是观测操作，不是第二套进程管理。由 process.py 维护的 `ChildObservationScope` 封装真实创建所得进程 handle/PID、归属状态、nonce和阶段；观测实现通过只查询/阶段控制的受限方法获得数据，拿不到裸 HANDLE 整数、Job handle、初始线程 handle 或可 Close/Duplicate/Terminate 的对象。方法在内部直接使用 pi.hProcess，不重新 OpenProcess(pid)。需要的创建句柄信息由 owner 用于实际查询，返回的只是不可变身份和采样值。

这也避免“给同步 callback 一个毫秒预算便解决挂死”的错误：任意 Python callback 若永久阻塞，owner循环根本不能及时检查原timeout，调用结束后检测耗时不能补救。若主设计坚持任意回调，则必须另审具有真实隔离/取消/排空能力的执行设施；本候选不把普通线程的 join(timeout) 当成可终止线程，也不允许遗留 daemon线程继续持有 scope。因此本次建议不接受任意可执行 callback，只接受上述有限操作集合。

## 3. 具体时点、时间与可见能力

| 时点 | 采集/允许操作 | 禁止或失败处理 |
|---|---|---|
| 创建前准备 | 验证 observation，建立固定测量记录/阶段事件，初始化采样文件；记录父 QPC频率及 pre_create ticks | 不执行消费者代码；失败时尚无子进程，关闭本次测量资源并记录失败 |
| CreateProcess 紧前/紧后 | owner在真实调用边界采QPC；保存返回的真实PID、创建结果与 created_suspended ticks | 创建失败记录真实 Win32错误；不可构造虚拟 PID/Ready |
| assigned_suspended | AssignJob成功后才向 scope发布受管身份；允许获取第一次内存/身份原始快照 | 此时消费者仍未运行；观测不能调用ResumeThread；失败走同一owned清理 |
| resumed | owner记录ResumeThread前后ticks并照旧关闭初始线程handle | scope不能保留线程handle；恢复失败不得记启动成功 |
| running/poll | owner先处理进程状态和绝对deadline，再执行到期有限采样、非阻塞读取阶段记录及允许的阶段ack | 不等待事件无限signal，不因采样或Busy延长deadline；样本满/采集失败不能静默丢弃 |
| HostReady已验证 | 检查nonce、真实PID、阶段顺序和QPC；子主线程处于Ready屏障；先采高水位，再发布允许预热的ack | 任意日志文字不能替代合法Ready；不能让预热先发生再回填Ready峰值 |
| WarmupComplete/OwnersReleased | 依据真实阶段切换固定静置/采样窗口、停止前后曲线 | 不能由父补造Host内部完成状态，不能替消费者调用业务/Host shutdown |
| root_exited首次观测 | 固定实际退出时刻，停止向已退出root发送命令；只记录最后成功样本和终止状态 | 不给退出后内存字段补零；scope不可转向Job中的另一个进程 |
| Job drained | 继续复用原Job统计/超时排空，记录实际active_after/total_processes | root退出而子孙未排空仍按原规则失败/超时，观测成功不能覆盖 |
| finally/finalized | 先永久失效scope，再关闭本次测量资源及原进程/Job句柄；汇总引用原始采样 | 任何保留的scope引用均只能得到 ScopeClosed；清理错误另附，不能覆盖首个真实错误 |

QPC原始ticks由实际owner时点采集，不让观测代码事后提供。内部Host构造→Ready仍来自消费者记录，父创建→子Ready、父创建→父观察Ready分别计算，保留通知调度滞后。跨进程QPC只适用同机，±1 tick的排序不做强推论；UTC仅用于审计日期。[QPC主资料](https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps)

配置中的采样间隔只控制 owner轮询的最早到期时点，不保证OS实时性。计算下一次睡眠时仍受原进程deadline及最近观测期限约束，不用一个过长“等阶段”的调用挡住原退出检查。默认 observation=None 继续原10 ms轮询；有观测时的频率变化必须记录到方法身份，并用于同等baseline。

## 4. 阶段通道、句柄和 only-this-child

建议内部测量通道由owner准备，包含固定大小共享记录及有限事件；不是stdin命令解释器。stdin继续devnull。观测分支需要额外继承句柄时，应使用明确允许列表，仅含原三个标准句柄与本轮固定通道句柄；不要把整个父进程的 inheritable handle 集合传入子进程。默认分支不顺便改变既有继承行为，句柄允许列表扩展须单独验证。[句柄继承说明](https://learn.microsoft.com/en-us/windows/win32/procthread/inheritance)

每个scope固定绑定 `(run_nonce, generation, actual_pid, owner_process_handle)`，读/写前都核对scope仍活动及阶段合法。测量消息中的PID只是核对值，不是OpenProcess依据。scope方法不接受其他PID、不枚举并终止系统进程、不为重启或PID复用重新绑定。以相同EXE启动的另一个进程，以及nonce错误但PID相同的记录，都不能推进本轮阶段。

观察数据中的线程信息可来自全局Toolhelp快照，但必须只筛本轮root PID；模块/内存直接查询创建句柄。Job只负责这一轮树的所有权和清理，不将“Job中某个进程”的内存替换root指标。若NativeSubset意外衍生进程，记录事实并由测量合同拒绝；即使被拒绝，清理仍使用现有同一Job。

scope、record视图和通道句柄不得返回给最终用户或写成可再次使用的handle；结果只保留PID、时间、方法版本、计数、不可复用的关联ID及证据路径。实现不向扩展暴露handle getter，不支持DuplicateHandle；失效后清除内部引用，任何迟到采样/ack都被拒绝，不能出现“最后一次读取”侥幸使用已关闭handle。

测量准备动作发生在恢复之前，但不能在暂停态无限等待observer。观测配置需要单独有限的 setup/stage期限；它们仅用于提前失败，不扩大原运行timeout。owner在每次有限操作前后检查期限。这里的保证依赖内部操作集合无任意阻塞代码；Win32 API本身异常或不支持时记录失败，不能用虚构样本继续。

## 5. 异常、退出和原始输出

子进程stdout/stderr仍由原生继承的文件句柄直接写入原 `xb` 文件，保留原始字节和退出后的hash。观测不把stdout改成pipe、不代写“Passed”、不合并其诊断到消费者stdout。采样器的API错误/阶段错误写独立 observer日志与机器错误字段；现有默认错误流语义保持不变。

可选观测分支新增明示错误类别，例如 `ObserverFailed`、`ObservationIncomplete`；真实进程的 observed_exit_code、正常/崩溃/超时事实与观测失败原因分别保存，不能把“观测失败而终止进程”伪称消费者自行Exited 0。可选新schema字段和状态必须与上层 evidence校验器一起受审；不静默塞进现有只认可固定状态的验证链。

异常处理要求：

- 在CreateProcess之前失败：关闭本次测量资源，没有进程清理事实可编造。
- 创建后、Job归属前失败：只处置本次pi.hProcess，沿原路径终止/等待；不让观测接触该未归属handle。
- Job归属后任何观测异常、解析错误、样本溢出、阶段超时或启动器取消：转入同一Job的有限终止/排空逻辑；仍记录终止调用成功与否、实际active_after，不仅依靠finally的KILL_ON_JOB_CLOSE便宣称排空。
- root已正常退出但最后样本/observer收尾失败：保留真实exit=0，同时测量为Incomplete/Failed；两者不可互相覆盖。
- 某次API偶发错误：记录时间和Win32错误；只有预先冻结方法允许有限重试时才重试。重试仍失败或达到样本缺失上限即测量失败，不把旧样本复写为新时点。
- 清理又发生异常：保留首因与清理错误列表，scope仍失效，尽力关闭本次owner资源；不吞首因，也不把清理失败标为active_after=0。

请注意原execute的运行错误分支目前主要捕获OSError/ValueError，异常时不会执行与正常超时完全相同的统计排空流程。候选实现不能只在现有try内直接调用新observer任由其他异常逃出；必须把观测异常收敛到明确owned清理入口。是否抽取复用的清理helper应在实施时最小化，并证明默认既有合同没有回退。不能为了默认语义不变而保留新分支缺失排空事实。

## 6. 必需真实反例建议

以下仅是待冻结的测试建议，未实施、未运行：

| 控制 | 必须取得的真实证据 |
|---|---|
| 无observer回归 | 已有正常退出、异常退出、timeout、子孙残留、原始非UTF8流、启动失败等真实控制仍有相同语义；None不启动新线程/IPC |
| 暂停归属顺序 | 子进程入口写可观察marker；assigned_suspended前marker不能出现，恢复后才出现；实际assigned_before_resume为真 |
| 同名外部进程隔离 | 同时运行独立同名sentinel，目标超时仅目标Job排空，sentinel仍正常；不按名称终止 |
| 错误身份与迟到scope | 另一run的nonce/PID或已关闭scope不能采样/ack；实际第二消费者不受影响 |
| 观测时点故障 | 创建前、已创建未归属、已归属未恢复、运行采样、Ready屏障、root退出后各自注入受控观测错误；完整记录唯一owned清理和真实状态 |
| 阶段协议反例 | 少Ready、重复Ready、越序、未来ticks、错版本/nonce、记录截断及Ready后崩溃均失败；不能补写阶段 |
| 样本容量/错权限 | 固定样本上限耗尽、真实查询失败、结构或handle失效不得记零/复用旧样本；清理仍完成 |
| 不延长timeout | 子进程等待阶段指令但不推进，owner原deadline仍终止Job；周期采样与有限重试不能持续重置deadline |
| 任意回调拒绝 | observation不接受任意callable、脚本、外部PID或危险操作；不以一个永不返回的callback测试来验证根本无法提供的取消保证 |
| 原始流完整 | 消费者输出带NUL/非UTF8及stdout/stderr分别可核hash；observer失败日志不混入消费者流 |
| 子孙及退出竞态 | root退出后子孙仍活、正好采样时退出、cleanup失败等真实进程控制不变成成功；停止采样后不重新附着PID |
| 配对采样不污染Ready | 目标在Ready屏障后等待，父先读高水位再放行预热；固定大预热分配不能被算入启动至Ready字段 |

## 7. Logging 候选只读审阅意见

已只读审阅 `evidence/bootstrap/D1.06/logging-design-candidate.md`。以下是合并前需澄清的合同问题，不是已实现缺陷；没有修改其候选文件。

### 7.1 flush旧水位与淘汰水位存在直接矛盾（优先修正）

`LogFlushResult.evicted_through` 注释要求 `<= covered_through.sequence`，正文又要求精确报告已淘汰前缀。容量2，接受1、2时保留旧H=1，再接受3、4、5，当前全局evicted_through=3。此时flush(H=1)若照抄全局E=3，就违反返回结构约束；若把H扩大到3，又违反covered_through固定为请求H的合同。

建议：flush返回的淘汰水位明确是**请求前缀内**的淘汰部分，即 `min(snapshot_evicted_through, H.sequence)`，covered_through恒为H。全局最新E仍由snapshot独立提供。空H=0必须返回该前缀evicted=0，即使全局已经淘汰很多记录。当前没有区间中洞，因DropOldest淘汰始终为连续前缀，这个裁剪有精确定义。

“反复flush同一H幂等”也须限定：不新增副作用、不扩大覆盖水位；不能承诺整个结果字节恒等。第一次flush(H)后新写入可能淘汰H中的记录，第二次返回的前缀淘汰水位可以增长。若要求严格恒定结果，就要保存历史flush快照，与有限无逐flush状态的目标冲突。共同测试应覆盖E<H、E=H、E>H、H=0、两次flush中间继续淘汰、close后旧H。

### 7.2 SafeLogger重入与同步自毁的所有权尚未具体化（优先修正）

候选宣称共享所有权保证在途调用稳定，但SafeLogger给出的类形状没有规定在进入第三方虚端口前冻结哪个所有者，也没有给出跨方法重入算法。仅复制backend shared_ptr不能保护稍后写入`this`中的facade计数器：第三方try_write若同步释放最后一个SafeLogger owner，外壳已经析构，回调返回后再触碰`this`会悬空。这与另段所说“并发销毁借用引用属于无效调用”不同；必须分别界定同步自毁和无owner的并发入口。

建议SafeLogger外壳持有限内部共享State，进入方法时先复制State到栈上；State拥有backend及固定facade计数器。在任何外部虚调用之前完成此冻结，外部调用之后仅访问局部State，不再解引用this。不能在持有facade/后端锁时调用虚端口或释放最后一个外部owner。若主设计不支持同步自毁，也须把它明确列为调用前置条件并收窄“在途共享所有权保证”，不能留下相反承诺。

重入guard应覆盖try_write/snapshot/flush/close所有互调，而不只是try_write自身递归。建议使用有界、无堆分配的线程局部栈帧链，识别同一State/后端身份的在途调用；需要明确多个SafeLogger包装同一backend，以及A→B→A的处理。简单一个TLS bool会错误拒绝不相干logger，单个current指针又可能漏掉A→B→A；全局标记会把正常跨线程并发误判为重入。返错路径不得再调用backend、what()或写日志。

需实际验证：各方法交叉重入、A→B→A、共享backend的两个外壳、正常跨线程调用、后端同步释放最后一个外壳owner、异常回程自毁及计数可见性；完整窗口纳入分配计数及ASan。无owner的并发进入仍可作为明确不支持的C++寿命违例，不应伪造可防御保证。

### 7.3 后端异常与“实际接受未知”不能被统一计数遮蔽

候选已正确限制合法后端异常只能发生在接受前，并承认“接受后抛出”是非合格后端、不可恢复未知接受事实。但SafeLogger catch返回`Rejected/BackendFailure`及“attempts按最终返回分类”的语义还需明确：Rejected究竟表示facade没有给出接受回执，还是断言后端从未接受。普通异常本身不能让facade知道后端是否遵守了接受前失败约定。

可以在合法后端合同假设内使用当前三态返回，但必须明确对非合格后端不承诺后端接受次数守恒、无重复或可安全重试；不能把facade_rejected_backend与backend.rejected合并为“已证实拒绝”。若主设计希望对任意不合格后端也保留接受不确定性，就需显式unknown/contract_violation事实，而不是catch后凭空补一个确切拒绝。这个选择属于公共返回语义，需主设计裁定，不能靠实现注释消解。

另外，facade的flush/close/snapshot异常目前只有write类计数形状，后端并未必增加其flush_failures/close_failures。应明确独立facade控制错误计数或固定诊断字段，避免把SafeLogger捕获事实冒充后端已记录，或让它完全消失。无论如何，日志控制错误不能改写业务Outcome；但Host停机能否声明quiescent仍取决于真实清理完成，二者不能混为一谈。

### 7.4 其他需在主设计落定的小边界

- `copy_records(after)`已有跨Host/stream校验，但需明确未来after是否InvalidPosition，以及全局E越过after时next/gap/accepted_upper的关系；返回页面内淘汰信息与全局snapshot信息不能含混。
- Closing已经建立不再接受栅栏但close收尾失败时，try_write应明确归入Closed/停止接受原因，而不是重新开放或永久把事实藏在Busy中。
- 饱和后关闭精确守恒断言的描述基本一致；仍需防止已饱和的facade计数与未饱和后端计数被直接相减，得到伪造的丢失数。

整体上，固定公共格式、默认脱敏、无普通日志代替RequiredRecord、callback-free共同套件不整体NA、DropOldest与RejectNewest分别保留接受/淘汰事实等边界是清楚的。以上意见集中在旧水位返回、facade跨虚调用寿命、重入和不确定接受事实，建议在冻结公共声明与fixed expected之前消除歧义。

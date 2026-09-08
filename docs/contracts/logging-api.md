# D1.06 普通 Logging 与有界内存诊断 API

状态：**现行 D1.06 实现合同，最终来源集中复核中**。这是既有 D1.06 设计输入的合并稿，不是实现、测试 Passed 或新增审批节点。依据架构 A15、A16、A22.2 与执行计划 D1.06；合并 design-consolidation-review 第 3/4 条及 footprint-process-hook-candidate §7。旧候选和审核记录保留。

## 1. 范围和唯一声明

公共数据与 `ock::contracts::LogPort` 放在 `ock/contracts/logging.hpp`；工厂、`SafeLogger` 和诊断管理类型放在 `ock/runtime/logging.hpp` 的 `ock::runtime::observability`。只沿用 `foundation::Result<T>`、`foundation::Error` 和既有 `PortLifetime`，不引入 LoggingPort 别名或第二套 expected。SDK 分类和 Host 装配以同包对应合同统一登记。

本包仅同步、易失、固定容量内存普通日志，无新增线程、异步队列、文件日志、轮转、订阅/完成回调或 RequiredAudit。日志不是 DomainEvent、Notification、执行索引、Outcome 或 RequiredRecordPort；日志接受/丢弃/错误不能更改权限、准入、业务结果、提交或效果事实。日志控制失败是否妨碍 Host 宣布 quiescent，按实际关闭事实判断，不能因“不改业务结果”就当作停止成功。

## 2. 公共数据和固定错误

下列是唯一候选声明；代码块仅声明，不提供行为实现。整数编码为本版本固定值。所有 Error 均使用 `ock.logging` 域、下列固定非零码，`Error::info()` 为空，不携带 what()、输入正文或动态错误详情。

```cpp
namespace ock::contracts {
inline constexpr foundation::ErrorDomain logging_domain{"ock.logging"};
enum class LogErrc : std::uint32_t {
  InvalidConfiguration=1, BudgetExceeded=2, AllocationFailure=3,
  InvalidPosition=4, Busy=5, BackendFailure=6, Reentrant=7,
  InvalidLogger=8, BackendProtocolFailure=9, InvalidReadPage=10,
  UnsupportedCapability=11
};
inline foundation::Error log_error(LogErrc) noexcept;

enum class LogLevel : std::uint8_t { Trace=0, Debug=1, Info=2, Warning=3, Error=4 };
enum class LogComponent : std::uint8_t { Host=0, Registry=1, Policy=2, Invocation=3 };
enum class LogEvent : std::uint16_t {
  Configured=1, Starting=2, ModuleStarted=3, Ready=4, StartFailed=5,
  StopAccepting=6, ModuleStopped=7, LogClosing=8, Diagnostic=9
};
enum class LogKey : std::uint16_t { Status=1, Count=2, Module=3, Detail=4 };
enum class LogValueClass : std::uint8_t { Redacted=0, PublicCode=1, PublicCount=2 };
struct LogField {
  LogKey key;
  LogValueClass visibility{LogValueClass::Redacted};
  std::uint64_t value{};
};
struct LogInput {
  LogLevel level;
  LogComponent component;
  LogEvent event;
  std::span<const LogField> fields;
};
struct LogPosition {
  HostIncarnation host;
  std::uint64_t stream;
  std::uint64_t accepted_sequence;
  bool operator==(const LogPosition&) const = default;
};
enum class LogDecision : std::uint8_t { Accepted=0, Rejected=1, Dropped=2 };
enum class LogReason : std::uint8_t {
  None=0, Filtered=1, InvalidRecord=2, Full=3, Busy=4, Closed=5,
  BackendFailure=6, SequenceExhausted=7, Reentrant=8,
  InvalidLogger=9, BackendProtocolFailure=10
};
struct LogWriteResult {
  LogDecision decision;
  LogReason reason;
  std::optional<LogPosition> accepted;
};
enum class LogOverflow : std::uint8_t { DropOldest=0, RejectNewest=1 };
struct LogLimits {
  std::uint32_t record_capacity{128};
  LogOverflow full{LogOverflow::DropOldest};
  LogLevel minimum_level{LogLevel::Info};
};
enum class LogState : std::uint8_t { Open=0, Closing=1, Closed=2 };
struct LogCounters {
  std::uint64_t accepted, rejected, dropped_before_accept, evicted_after_accept;
  std::uint64_t rejected_invalid, rejected_full, rejected_busy, rejected_closed;
  std::uint64_t rejected_backend, rejected_sequence;
  std::uint64_t dropped_filtered;
  std::uint64_t snapshot_failures, flush_failures, close_failures;
  bool saturated;
};
struct PublicLogRecord {
  LogPosition position;
  LogLevel level;
  std::uint16_t text_size;
  std::array<char,256> text;
};
struct LogSnapshot {
  LogPosition accepted_through;
  std::uint64_t evicted_through, retained_count;
  LogLimits limits;
  LogState state;
  LogCounters counters;
};
struct LogFlushResult {
  LogPosition covered_through;
  std::uint64_t evicted_through;
  bool volatile_only;
};
class LogPort : public PortLifetime {
public:
  virtual LogWriteResult try_write(const LogInput&) = 0;
  virtual foundation::Result<LogSnapshot> snapshot() = 0;
  virtual foundation::Result<LogFlushResult> flush(LogPosition through) = 0;
  virtual foundation::Result<LogFlushResult> close() = 0;
};
}
```

`LogWriteResult` 保留固定三态，不再用 expected 包装写入判定。合法组合仅为：Accepted/None/非空位置；Dropped/Filtered/空位置；Rejected/某个非 None、非 Filtered 原因/空位置。无效枚举和组合是后端合同违例。端口本身不产生 facade 专属 Reentrant、InvalidLogger、BackendProtocolFailure；其余拒绝原因按下文使用。

控制方法用既有 Result 的 unexpected Error。Busy→LogErrc::Busy；跨流/未来位置→InvalidPosition；页容量非法→InvalidReadPage；正常后端故障或合法接受前异常→BackendFailure。配置非法枚举、空 Host、stream=0、容量=0→InvalidConfiguration；容量>4096 或受检乘加超限→BudgetExceeded；初始化分配异常→AllocationFailure。SafeLogger 的空 owner、重入、非法后端报告分别为 InvalidLogger、Reentrant、BackendProtocolFailure。Host 对自定义后端没有默认内存诊断句柄时，copy_logs 返回 `ock.host/HostErrc::UnsupportedCapability`，不能回退到另一份假日志；有诊断句柄时底层读取失败的 `ock.logging` 原错误直接保留。“所有Error使用ock.logging”仅限定本日志API，不覆盖Host自身错误域。

log_error必须在CoreContracts公开头内提供inline定义，沿既有固定ErrorCode构造方式；CoreContracts仍为INTERFACE，独立日志适配器仅依赖CoreContracts即可构造日志错误，不依赖Runtime的外部符号。

## 3. 静态事件、字段与公共格式

事件和键只来自下表，不接受 Name、自由 message、格式模板或字符串值。Host 生命周期事件的 component 必须为 Host，level 由表固定；Diagnostic 允许四个 component 和五个有效 level，用于明确的管理/模块诊断。其他组合拒绝 InvalidRecord。

| event 编码 | 固定输出名 | level | 实际发出位置 |
|---|---|---|---|
| 1 Configured | configured | Info | Host start 内日志 factory、facade 与身份验证全部成功后 |
| 2 Starting | starting | Info | 本次实际启动流程中日志可用后，紧随 Configured，模块启动前 |
| 3 ModuleStarted | module_started | Info | 某模块 start 实际成功后 |
| 4 Ready | ready | Info | 实际完成 Ready 转换后 |
| 5 StartFailed | start_failed | Error | 首次启动失败事实已保存在独立 Host 首错结构后 |
| 6 StopAccepting | stop_accepting | Info | 首次实际建立停止准入栅栏后 |
| 7 ModuleStopped | module_stopped | Info | 某模块实际停妥后 |
| 8 LogClosing | log_closing | Info | 模块收尾已完成、首次尝试日志 close 之前 |
| 9 Diagnostic | diagnostic | 输入的有效 level | 显式模块/管理诊断，不自动附加到每个 invoke |

这些是尽力写入的诊断，不是第二份状态真相：每个已发生的事件最多尝试一次，不因 Full/Busy/异常重发。重复 start/shutdown 不重放已经记录或丢弃的事件；Pending 模块真正停止前不发 ModuleStopped。Host create 仅预分配/拥有配置，不调用日志 factory、不声称已写日志。Configured 前失败无法写普通日志时仍保留 Host 固定首错材料。LogClosing 不是 Stopped；日志成功 close 后不再尝试写“Host stopped”，真实 Stopped 由 Host 状态和 shutdown 返回证明。Host 所有日志调用在 Host 锁外。

| key | 输出名 | 允许公开类型和值 | 默认行为 |
|---|---|---|---|
| Status=1 | status | PublicCode，0..65535 | Redacted |
| Count=2 | count | PublicCount，0..UINT64_MAX | Redacted |
| Module=3 | module | PublicCode，1..65535，Host 使用本次有界装配序号 | Redacted |
| Detail=4 | detail | 仅 Redacted，不接受公开原值 | Redacted |

每条 0..4 个字段；同键重复、未知键、无效 visibility、公开类型不匹配或公开值越界均拒绝。Redacted 对任一有效 key 合法，不验证、读取或存储其 value 内容；只写 `<redacted>`。字段按 key 数值排序，输入顺序不影响输出。模块原始名称、主体/对象/任务 ID、ErrorInfo、token、秘密、配置正文、模型/几何等没有自动序列化入口；调用者不能通过自由文本的“Public”标记绕过此表。对数字键的显式公开选择仍由可信诊断生产者负责，不接受未授权用户 DTO 直接作为日志字段。

格式为 ASCII，无尾换行，不将 position/内存地址隐式写入 text：

```text
ock.log/1 level=<level> component=<component> event=<event>[ <key>=<value>...]
```

level 为 trace/debug/info/warning/error；component 为 host/registry/policy/invocation；PublicCode 是 `code:<十进制整数>`，PublicCount 是 `count:<十进制整数>`，不加前导零，零仅为 `0`。固定 golden：

```text
ock.log/1 level=info component=host event=ready
ock.log/1 level=info component=host event=ready status=code:0 count=count:42 module=code:7 detail=<redacted>
ock.log/1 level=debug component=policy event=diagnostic detail=<redacted>
```

256 是整块文本存储大小，含至少一个尾零；`text_size` 是上述有效字节数，必须 <256；text[text_size..255] 全零，不截断合法内容，不保留旧槽秘密残余。静态表的最大格式必须在实现检查中可容纳；不能为过长格式默默裁剪。LogInput 和字段 span 只在调用期间借用，返回后零引用保留；Accepted 只保存格式化后的固定公共值。

## 4. 容量、满载策略与接受计数

初始化一次分配有限固定槽，默认 128、合法范围 1..4096；每条最多 4 字段、256 文本。分配前受检计算槽字节和固定管理开销，禁止无界 reserve、扩容和无限预热。本包工厂默认 DropOldest；独立合法测试后端使用 RejectNewest，二者运行同一合同。只接受这两个策略；不额外实现 DropNewest。

try_write 先在栈上验证/格式化。非法输入立即 Rejected/InvalidRecord；有效输入尝试一次短锁，未取得则 Rejected/Busy，不等待消费、不自旋重试。取得锁后依次判断 Closing/Closed、级别过滤、序号耗尽、满载。Closing/Closed→Rejected/Closed；低于 minimum_level→Dropped/Filtered；无法再产生唯一序号→Rejected/SequenceExhausted。其余写入在线性化点分配连续接受序号并保存记录。

空前缀序号为 0；接受从 1 连续增加，位置包含固定 HostIncarnation 与非零 stream。位置是流内游标，不是权限凭据。Host/stream 不重用；序号不能饱和后复用或回绕。RejectNewest 满载时返回 Rejected/Full，不分配序号、不改变已有内容。DropOldest 满载时淘汰最老记录并接受新记录，返回 Accepted；accepted 与 evicted_after_accept 各增加 1，不能把旧接受事实倒改成拒绝。

在无在途调用且未饱和时：

- attempts = accepted + rejected + dropped_before_accept。
- rejected = rejected_invalid + rejected_full + rejected_busy + rejected_closed + rejected_backend + rejected_sequence。
- dropped_before_accept = dropped_filtered。
- retained_count = accepted - evicted_after_accept <= capacity；读取和 close 不清空存储。
- accepted_through.sequence = accepted，evicted_through = evicted_after_accept；淘汰是连续前缀，保留是其后连续后缀。

拒绝/过滤各次在返回前完成对应计数，且只归入一个原因。snapshot/flush/close 的正常失败分别增加同名 failures，不混入写入 attempts。允许并发，累计计数用原子方式更新；快照的环状态/水位/accepted/evicted 在同一锁下保持一致，但 Busy 等无锁拒绝计数只承诺各字段单调，不承诺跨字段全局瞬时一致。精确守恒只在静止点断言。

统计计数尝试溢出时保持 UINT64_MAX 并设置 saturated，不回绕；饱和后不得做精确守恒或跨 facade/后端相减。接受序号和环水位仍保持精确，耗尽即拒绝。测试极值使用既有受检纯值逻辑，不给生产端口加计数器写入开关。

## 5. flush、close 与显式重试

snapshot 返回当前固定流身份、水位、有限 limits、state 和计数；flush(H) 接受同 Host/stream、0<=H.sequence<=当前 S 的位置。跨流、未来位置拒绝 InvalidPosition。成功时：

```text
covered_through = H
returned.evicted_through = min(current_global_evicted_through, H.accepted_sequence)
volatile_only = true
```

含义是该次线性化点已完成 H 前缀所有接受记录的同步内存写入，或记录已经按保留策略淘汰；不表示磁盘 flush、耐久、导出成功或永久保留。并发后来接受的记录不扩大 covered_through。全局 E 仍由 snapshot 单独提供。H=0 的返回淘汰水位恒 0，即使当前 E>0。

flush 的“幂等”指无新增记录、无重复消费/回调/外部动作，且覆盖目标不扩大；不承诺两次返回字节相同。两次之间继续写入可以增大 min(E,H)。显式调用/失败的诊断计数按实际次数变化，不属于被禁止的重复日志语义副作用。必须覆盖 E<H、E=H、E>H、H=0、间隔淘汰、close 后旧 H。

close 第一次成功取得锁时建立不再接受栅栏，捕获最终接受水位 Hclose，进入 Closing；同期写入在线性化点之前接受则归入 Hclose，之后返回 Closed。close 完成其实际收尾后变 Closed 并返回覆盖 Hclose 的 LogFlushResult。默认内存实现没有异步尾工，取得锁后同步完成。合法测试后端可在栅栏之后返回 BackendFailure，保持 Closing；不能失败后重新开放。

close 的 Busy 不等于已建立栅栏；BackendFailure 也不等于关闭成功。后续**显式** close 可推进 Closing；SafeLogger 不做自动重试，Host 也只在用户/消费者下一次显式 shutdown 时重试，不能在一次 shutdown 内围绕日志失败/Busy 私自循环。默认安全停止预算属于 Host 管理语义，不成为 try_write 的等待预算。

重复成功 close 不新增关闭动作，返回同一个 Hclose；已关闭后 snapshot、合法旧 H 的 flush 和诊断复制仍可使用。close 不销毁槽或外部 owner；最终共享内部状态释放时才回收。后端只能在接受之前抛出写入异常；控制方法异常不自动表示已有栅栏/收尾成功，Host 保留依赖和待清理日志项，不从异常猜 Stopped。

## 6. SafeLogger：共享 State、重入和独立计数

```cpp
namespace ock::runtime::observability {
struct FacadeCounters {
  std::uint64_t write_attempts, accepted, rejected, dropped;
  std::uint64_t write_backend_exceptions, write_protocol_failures, write_reentrant;
  std::uint64_t acceptance_unknown;
  std::uint64_t snapshot_attempts, snapshot_failures, snapshot_backend_exceptions;
  std::uint64_t snapshot_protocol_failures, snapshot_reentrant;
  std::uint64_t flush_attempts, flush_failures, flush_backend_exceptions;
  std::uint64_t flush_protocol_failures, flush_reentrant;
  std::uint64_t close_attempts, close_failures, close_backend_exceptions;
  std::uint64_t close_protocol_failures, close_reentrant;
  bool saturated;
};
class SafeLogger final {
public:
  static foundation::Result<SafeLogger> create(std::shared_ptr<contracts::LogPort>);
  SafeLogger(const SafeLogger&) noexcept;
  SafeLogger& operator=(const SafeLogger&) noexcept;
  SafeLogger(SafeLogger&&) noexcept;
  SafeLogger& operator=(SafeLogger&&) noexcept;
  ~SafeLogger();
  contracts::LogWriteResult try_write(const contracts::LogInput&) noexcept;
  foundation::Result<contracts::LogSnapshot> snapshot() noexcept;
  foundation::Result<contracts::LogFlushResult> flush(contracts::LogPosition) noexcept;
  foundation::Result<contracts::LogFlushResult> close() noexcept;
  foundation::Result<FacadeCounters> counters() const noexcept;
private:
  struct State;
  explicit SafeLogger(std::shared_ptr<State>) noexcept;
  std::shared_ptr<State> state_;
};
}
```

State 拥有 backend 的 shared_ptr、已验证固定流身份及全部 facade 计数；外壳拷贝共享 State，分别 create 同一 backend 则各有自己的 facade 计数。无公共默认构造；create(nullptr)→InvalidLogger，State 分配失败→AllocationFailure。create 在锁外调用初始 snapshot 验证非空 Host、非零 stream、合法枚举/容量、水位与保留范围；后端异常→BackendFailure，非法结果→BackendProtocolFailure，不发布半成品。create 可包装已活动或关闭的合法流，不强制空流；Host factory 装配另核对同 Host/stream=1、Open、零接受/保留。

Host 在 start 内取得 factory 成功返回的原 backend owner 后，必须先持有它并登记 Logging 待清理项，再调用 snapshot/创建 SafeLogger；即使身份非法、snapshot 异常或 State 分配失败，该 owner 也不能被未检查地丢弃。未装成 facade 时，Host 直接执行受隔离的管理 close：锁外调用，显式捕获异常并只保存固定错误，不递归写日志。关闭失败保留原 owner、依赖和 Failed Host，下一次显式 shutdown 才重试；不新增 SafeLogger setter/prepare 接口。同步 LogPort 不具备取消/抢占虚调用的能力，有限截止只能约束调用前后的预算检查和后续推进，不能把截止到达或异常解释为端口已经 quiescent。

每个方法在任何虚调用前先把 state_ 复制到栈上局部 owner，此后包括计数写入、返回值校验和析构路径只使用局部 State，**不再解引用 this**。后端同步释放最后一个 SafeLogger 外壳 owner（含异常回程）不会破坏在途 State/计数；外壳另一个拷贝可观察同一计数。仅复制 backend 不足以满足合同。外壳拷贝/赋值/销毁与无 owner 的另一线程同时进入同一外壳仍是调用者 C++ 寿命/数据竞争违例，不承诺防御。此支持不扩展为允许模块回调自毁 NativeHost，Host 的独立限制保持。

try_write/snapshot/flush/close 和 create 的初始 snapshot 使用同一线程局部栈帧链。帧在调用栈分配，不分配堆；包含 backend 对象身份与上一帧。进入虚端口前遍历最多 16 个在途帧：已有同一 backend，或深度达到 16，就拒绝 Reentrant；不调用后端。按 backend 而不只按 State 检查，覆盖同后端两个 facade、任意方法互调、A→B→A；A→独立 B 在深度预算内允许。线程局部链不拒绝另一线程对同后端的正常并发。局部 owner 持有 backend 防止在途身份复用。退出先移除帧，再释放局部 owner；不能在任何 facade 锁下调用虚端口或销毁最后外部 owner。

counters() 只读局部 State 的原子值，不调用 backend，不参加重入链、不再产生一轮自身计数。移后空外壳的 try_write 返回 Rejected/InvalidLogger，控制/counters 返回 InvalidLogger；没有 State 时无法登记计数，不伪造全局计数。create 失败也没有可发布的 facade 计数，create 本身不计为正式 snapshot_attempts。

写入计数规则：每次有效 State 的 try_write 增 write_attempts，按最终返回三态恰好增加 accepted/rejected/dropped 之一。直接传回合格 backend 的三态不补写 backend 计数。捕获异常：Rejected/BackendFailure，另增 write_backend_exceptions；重入：Rejected/Reentrant，另增 write_reentrant；非法后端结果：Rejected/BackendProtocolFailure，另增 write_protocol_failures 和 acceptance_unknown。后端记录本身不被 facade 修改。无饱和且静止时 write_attempts=accepted+rejected+dropped；后三个专项计数是 rejected 的子集，不再相加到总 attempts。

控制方法各增自己的 attempts；后端返回 unexpected、异常、重入或非法结果均增加该方法 failures。异常另增 backend_exceptions，重入另增 reentrant，非法结果另增 protocol_failures；正常合格 Error 仅计 failures。每次只选一个错误来源。控制返回错误必须为本域固定码且无 ErrorInfo，否则视为 BackendProtocolFailure；成功快照/水位/volatile_only 必须符合本合同及初始固定流身份，否则同样拒绝。不能让后端伪造另一流或未来 flush 覆盖。先验证请求的流身份即可拒绝跨流 InvalidPosition；未来位置仍由后端在线性化点判断。facade 失败计数与 backend 的 snapshot/flush/close_failures 分开，不合并为“后端已记录”。

异常语义在合格后端合同假设内为接受前拒绝：try_write 的 catch 返回 BackendFailure 不代表 facade 有能力观察后端内部接受线性化点。故意接受后抛出的非合格后端，无法仅凭 throw 区分；facade 不承诺其后端接受守恒、无重复或安全重试，也不回滚/重建真实接受事实。acceptance_unknown 仅记录已经检测到的非法写入报告，不是对任意违例的全知探测器。非合格后端必须在独立测试控制器的实际记录中被检出，永不 qualified；不能通过 catch 后凭空增加 backend.rejected 抹去实际接受。

所有计数饱和规则同第 4 节。未知/饱和的 facade 与后端计数不能相减推断丢失。所有异常路径不调用 what()、格式化或再次记日志；只返回固定值/固定 Error，不自动重试。

## 7. 默认工厂和有界诊断管理

```cpp
namespace ock::runtime::observability {
struct LogReadPage {
  std::uint32_t copied;
  contracts::LogPosition next, accepted_upper;
  std::uint64_t evicted_through;
  bool gap;
};
class MemoryDiagnostics : public contracts::PortLifetime {
public:
  virtual foundation::Result<LogReadPage>
  copy_records(contracts::LogPosition after,
               std::span<contracts::PublicLogRecord> out) = 0;
};
struct MemoryLoggingBundle {
  std::shared_ptr<contracts::LogPort> writer;
  std::shared_ptr<MemoryDiagnostics> diagnostics;
};
foundation::Result<MemoryLoggingBundle>
make_memory_logging(contracts::HostIncarnation host, std::uint64_t stream,
                    contracts::LogLimits limits = {});
}
```

工厂成功返回两个非空端口，共有同一有限内部 State。Host 使用自身实际新 HostIncarnation、stream=1；自定义 HostLogFactoryPort 属于 host.hpp，本合同不增加 CoreContracts factory。默认 Host 保存 diagnostics 并经 copy_logs 提供窄管理复制；自定义后端的管理 owner 由 factory 自己保留，Host 不凭类型转换冒取，copy_logs 明确 UnsupportedCapability。

copy_records 不是 visitor，不返回后端槽 span；输出是调用者提供的固定值数组。out.size 必须在 1..64 内，包含页上限，越界或 0 返回 InvalidReadPage。after 必须同流且 sequence<=当前 S，否则 InvalidPosition；锁争用→Busy。默认实现只尝试一次短锁，在同一锁下捕获全局 E、接受上界 U=S 并复制；无日志格式化、外部回调或最后 owner 释放发生在锁内。合法输入的失败不改 out，成功仅改前 copied 项。

精确页面关系：base=max(after.sequence,E)，copied=min(U-base,out.size)，记录是 base+1 至 base+copied；next.sequence=base+copied，accepted_upper.sequence=U，evicted_through=E，gap=(E>after.sequence)。所有返回位置仍属于原流。零复制时 next=base，故不会把 gap 掩盖为空页重读；未来游标拒绝，不做静默钳位。这里的 evicted_through 是页面捕获的全局 E，区别于 flush 对 H 的裁剪。读取不清空、不 pin 保留，不改变接受/淘汰计数；下一页间可能发生淘汰，并按 gap 显示。

诊断句柄仅由可信 Host/factory 装配持有。本包没有外部日志 RPC 或无需授权的日志读 Operation；PublicLogRecord 名称指公开格式，不代表公开访问权限。CallerDescription 或 admin 布尔值不能构造既有 Host 的读取能力。未来对外读取必须经过真实 Policy 授权及必要撤权复核，不能绕过 A15。读取、close 之后仍可保留管理 owner；最后 logger/diagnostics/在途局部 owner 全部释放才回收内部槽。

## 8. 同一 LoggingConformance 共同集与 Host 集成

两个合法工厂分别装配真实默认 MemoryLogging/DropOldest 与独立实现的 TestLogging/RejectNewest，注入同一套件，不逐后端复制断言。controller 只在测试侧控制接受前异常、控制方法故障和非合格行为，不给生产 API 添加开关、friend 或裸槽写入。共同五项不可由 capability 关闭：

| 共同项 | 必须覆盖的子断言 |
|---|---|
| accept_reject_drop_accounting | 0/超容量拒绝；capacity=2 的三次写精确验证 DropOldest 的新接受+旧淘汰与 RejectNewest 的明确拒绝；过滤、非法字段、Closed/Busy 分类；静止点守恒、有限并发、无序号复用 |
| format_redaction | 上述 golden；0/4字段、排序、重复/未知键拒绝；默认秘密哨兵不保存、尾部零清理、返回后输入改变不影响记录；无自由消息接口 |
| error_isolation | 后端接受前故障/异常、非法返回、各控制错误独立计数、不递归；业务实际结果不受日志失败改变；接受后抛故障后端必须不合格，不能假装修复其接受事实 |
| flush_accepted_range | 空/旧/未来/跨流 H；E<H/E=H/E>H；两次 flush 间淘汰；不扩大覆盖、不重复语义副作用；关闭后合法旧 H；失败不得返回假成功 |
| shutdown_callback_lifetime | Closing 后拒绝；close 失败保留状态、显式重试、幂等关闭；span 不逃逸；全部跨方法 TLS 互调、同 backend 两 facade、A→B→A、独立 B 与跨线程；同步释放 facade 外壳/异常回程仍保活；最终 owner 回收 |

async=false、file=false、callbacks=false、volatile_retention=true；只有 async 专项/文件轮转和磁盘专项按合同表达式 NotApplicable，共同满载和寿命项仍执行。Profile 要求本包没有的能力时装配失败，不能以 NA 放行。正常 MemoryLogging 没有可强行抛出的生产控制开关；其错误隔离共同项仍包含真实非法/满载等路径，第三方异常与消费者防御用独立合法/非合格测试后端实测。

Host 的 ModuleContext 只同步借用 SafeLogger&，模块不得保存过期引用。Native 普通 invoke **不额外写普通日志**：D1.05 既有固定 invocation ring、真实权限/目标/准入与完整返回析构窗口继续计量，不把未发生的日志工作宣称为已测。生命周期日志只在本合同列明位置发生；footprint 消费者可在 ModuleContext 有效的 start 内显式接受记录、snapshot/flush 断言，再通过 Host.copy_logs 校验有界保留，不能保存过期 context 或访问 detail/裸槽。

单独测固定日志写入（含持续满载）的分配、错误和回收，与原 Native 成功窗分开列明。默认内存日志预分配后不扩容、不新增线程；初始化/首用/有限预热/最后 owner 回收实际成本单列。C++/CRT/ASan 的覆盖边界沿 D1.05 计数合同，ASan 原 CRT 盲区不能用通道相加掩盖。正式数值预算和过程观测另由同包 footprint 方法确定，本合同不编造占用或时延 Passed。

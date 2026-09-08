# D1.06 Logging 与有界内存诊断候选设计

状态：候选，待主设计整合和独立 AI 规格审核。本文不冻结 API、expected、能力清单或占用数字，不声明实现或测试 Passed。只写本文件，不实现文件日志、异步工作、RequiredAudit、任务表或第二份业务事实。

## 依据与现状

已先读 docs/progress.md，并核对架构 A15.1–A15.3、A16.1–A16.3、A22.2–A22.3、A21.4–A21.6，以及执行计划 D1.06。普通日志可以按声明策略丢弃并计数，不阻塞取消、提交发布或控制入口；flush 不属于每次执行的必要收尾。Host 必须记录实际启动/清理顺序，停止未排空不能伪称成功。NativeSubset 新增线程为 0，有限预分配观测不能破坏固定原生窗口零新增分配。

现有 packages/contracts/include/ock/contracts/ports.hpp 有 RequiredRecordPort、RecordReceiver 等必要记录合同，不能改名或复用为普通日志。observation.hpp 是执行事实/进度投影，其可靠终态不能由日志覆盖。PortLifetime 已有虚析构与禁止复制规则，可作为新窄端口基类。现阶段不存在 LoggingPort 的生产实现。

tests/manifests/conformance.json 已登记 Logging 五个共同项：accept_reject_drop_accounting、format_redaction、error_isolation、flush_accepted_range、shutdown_callback_lifetime；async/file 才决定队列、轮转和磁盘专项。tests/conformance/support/harness.py 目前是 ock.executor.bootstrap/1 的 Python 工厂骨架，只证明工厂/manifest/共同集不可关闭的基础规则，不证明 C++ Logging 行为。D1.06 应建设 tests/conformance/logging 的同一 C++ 共同套件，由 runner 装配两个独立工厂；不复制断言后逐后端改预期。

## 最小数据模型与签名

候选公共窄合同可放新增 ock/contracts/logging.hpp，运行实现位于 packages/runtime/observability；是否进入初版稳定 SDK 由主设计统一登记。固定结构只含值、枚举和 std::array，不含 std::string、std::function、共享业务对象或 ErrorInfo。下列数字是待审查的记录形状候选上限，不是 footprint 已批准预算。

```cpp
namespace ock::contracts {
enum class LogLevel : uint8_t { Trace, Debug, Info, Warning, Error };
enum class LogComponent : uint8_t { Host, Registry, Policy, Invocation };
enum class LogValueClass : uint8_t { Redacted, PublicCode, PublicCount };
struct LogField {
  uint16_t key;                 // 版本化静态键表，未知键拒绝
  LogValueClass visibility{LogValueClass::Redacted};
  uint64_t value{};             // Redacted 时不读取、不保存该值
};
struct LogInput {
  LogLevel level;
  LogComponent component;
  uint16_t event_code;          // 版本化静态模板表，非自由文本
  std::span<const LogField> fields; // 最多 4 项，仅调用期间借用
};
struct LogPosition {
  HostIncarnation host;
  uint64_t stream;              // Host 内唯一、不复用；不是授权凭据
  uint64_t accepted_sequence;   // 0 表示空前缀
};
enum class LogDecision : uint8_t { Accepted, Rejected, Dropped };
enum class LogReason : uint8_t {
  None, Filtered, InvalidRecord, Full, Busy, Closed, BackendFailure,
  SequenceExhausted, Reentrant
};
struct LogWriteResult {
  LogDecision decision;
  LogReason reason;
  std::optional<LogPosition> accepted; // 当且仅当 Accepted
};
enum class LogOverflow : uint8_t { DropOldest, RejectNewest, DropNewest };
struct LogLimits {
  uint32_t record_capacity;     // 必须 >0 且 <= 主设计批准的有限上限
  LogOverflow full;
  LogLevel minimum_level;
};
struct LogCounters {
  uint64_t accepted, rejected, dropped_before_accept, evicted_after_accept;
  uint64_t rejected_invalid, rejected_full, rejected_busy, rejected_closed;
  uint64_t rejected_backend, rejected_reentrant, rejected_sequence;
  uint64_t dropped_filtered, dropped_full;
  uint64_t flush_failures, close_failures;
  bool saturated;              // 任一统计加法达到上限后置真，禁止回绕
};
struct PublicLogRecord {
  LogPosition position;
  LogLevel level;
  uint16_t text_size;
  std::array<char, 256> text;   // 固定公共格式，尾部清零
};
struct LogSnapshot {
  LogPosition accepted_through;
  uint64_t evicted_through, retained_count;
  LogCounters counters;
};
struct LogFlushResult {
  LogPosition covered_through;
  uint64_t evicted_through;    // <= covered_through.sequence
  bool volatile_only;         // 本包恒 true；不表示磁盘/耐久
};
enum class LogControlError : uint8_t {
  Busy, InvalidPosition, BackendFailure, Reentrant
};
// LogResult<T> 为 expected<T, LogControlError>，固定错误码，不分配详情。
class LoggingPort : public PortLifetime {
public:
  virtual LogWriteResult try_write(const LogInput&) = 0;
  virtual LogResult<LogSnapshot> snapshot() = 0;
  virtual LogResult<LogFlushResult> flush(LogPosition through) = 0;
  virtual LogResult<LogFlushResult> close() = 0;
};
}
namespace ock::runtime::observability {
class MemoryDiagnostics;
struct MemoryLoggingBundle {
  std::shared_ptr<contracts::LoggingPort> writer;
  std::shared_ptr<MemoryDiagnostics> diagnostics;
};
Result<MemoryLoggingBundle>
make_memory_logging(HostIncarnation, uint64_t stream,
                    contracts::LogLimits); // 构造期分配，失败不发布半成品
class SafeLogger final {
public:
  explicit SafeLogger(std::shared_ptr<contracts::LoggingPort>);
  contracts::LogWriteResult try_write(const contracts::LogInput&) noexcept;
  LogResult<LogSnapshot> snapshot() noexcept;
  LogResult<LogFlushResult> flush(LogPosition through) noexcept;
  LogResult<LogFlushResult> close() noexcept;
  FacadeCounters facade_counters() const noexcept; // 固定值、静止点精确
  // FacadeCounters：attempts、accepted、rejected、dropped、
  // rejected_backend、rejected_reentrant、saturated；各为 uint64_t，末项 bool。
};
}
```

LogResult 的具体别名需沿现有 expected 基础统一命名，不新建通用 Result 框架。这里把虚端口方法保留可抛出，是为了 SafeLogger 真正能隔离第三方实现的正常后端异常；不能把 noexcept 虚方法抛出导致 terminate 误称“已隔离”。合法后端的异常只能发生在接受之前；接受之后的后端失败必须保留 Accepted 及其水位，以 flush 的错误报告表达。故意接受后抛出而不返回水位的非合格实现只能用于消费者防御反例，SafeLogger 不得假装能恢复未知接受事实。

SafeLogger 自身使用固定故障/重入计数器。对外快照必须单独报告 facade_rejected_backend、facade_rejected_reentrant，不能冒充后端已经计数；总 attempts 按最终 facade 返回值统计，在静止点核对其与后端计数的覆盖关系。异常详情/what() 不入日志，catch 中禁止再次调用 LoggingPort；任何只读诊断错误也不得递归写日志。

## 有界存储、计数和并发

默认内存后端候选使用 DropOldest 固定槽环：初始化一次分配 capacity 个 PublicLogRecord，之后没有扩容。合法测试后端使用独立实现的固定槽容器和 RejectNewest；DropNewest 可以作为同一测试后端的合法策略专项，但产品默认只选一个明确策略。配置必须 checked_mul 核算槽字节、固定控制块与分配上限，拒绝 0、超限及乘加溢出，不允许“无限容量”哨兵。

try_write 在栈上检查静态模板/字段并构造最终公共记录。对共享状态只尝试一次取得短锁，失败返回 Rejected/Busy，不等待消费者、重试轮询、让出后再试或排队。锁内仅执行固定尺寸值复制、环索引、原子/整数计量；不格式化动态详情、不调用模块/用户端口、不运行 arbitrary callback、不释放最后一个外部 owner。snapshot/flush/close 同样可返回 Busy，由 Host 在自身有限停止预算内重试；业务路径不等待管理 flush。

接受线性化点为持锁时分配连续 accepted_sequence 并写入槽。被拒绝或入队前丢弃均无水位、不保留输入 span。DropOldest 满时先淘汰一个最老已接受记录再接受新记录，返回 Accepted/None：accepted 与 evicted_after_accept 各加 1；被淘汰记录不是倒改成 Rejected。RejectNewest 满时返回 Rejected/Full，只增加 rejected/rejected_full。DropNewest 满时返回 Dropped/Full，只增加 dropped_before_accept/dropped_full。低于最小级别返回 Dropped/Filtered，不给水位；非法记录返回 Rejected/InvalidRecord。先验证记录，再按互斥状态检查 Closed、过滤、满载；Busy 在未取得锁时直接决定，不能称已经看到了 Closed。

在没有在途调用的静止点，精确守恒为：

- attempts = accepted + rejected + dropped_before_accept；evicted_after_accept 不是 attempts 的第四类。
- rejected 等于所有 rejected_* 分类之和；dropped_before_accept = dropped_filtered + dropped_full。
- 本包无记录移出读取，故 retained_count = accepted - evicted_after_accept，并始终 <= capacity。
- DropOldest 的已淘汰接受序号是连续前缀 [1, evicted_through]；环中是其后的完整连续接受后缀。无日志丢失也不能据此推断业务事实或执行结果。

计数在各并发调用内原子增加，返回前完成本次计数；诊断快照中的各累计计数单独单调，只有静止点承诺跨字段守恒。snapshot 不承诺把 Busy 拒绝计数与持锁槽状态拍成同一全局瞬间；此限制必须写入公共合同，不能用 racy 普通整数。accepted/evicted/retained 和接受水位在锁内保持一致。使用饱和统计并报告 saturated；一旦统计饱和，禁止继续宣称精确守恒。接受序号本身不能饱和复用：耗尽后拒绝新接受，返回 SequenceExhausted。stream/Host 世代耗尽同理，禁止回绕再用。溢出算术可以独立测基础纯值 helper 的极限，不能为执行极限测试给生产后端加“设置计数器”开关。

## 公共格式、脱敏和读取权限

默认无自由消息字符串、Error::info、token、几何/模型参数、TaskId/ObjectId 或主体名称字段。候选公共文本格式固定为 `ock.log/1 level=info component=host event=ready key=status value=code:0`，字段按注册键排序；同字段重复、未知键/事件、无效枚举、字段数 >4 拒绝。Redacted 默认输出 `value=<redacted>`，原 value 不进入存储；PublicCode/PublicCount 必须匹配静态键类型。零字段合法。event/键对应表是静态受审查标签；不能仅靠调用者把自由输入标为 Public 来承诺脱敏。编码器不接受 printf/fmt 模板，不执行格式参数；不将错误消息、指针或资源名称隐式转为标签。纯固定表编码不产生换行注入、无效 UTF-8 或超长自由文本。256 字节不能容纳的合法模板要在表注册/构造时拒绝，而非静默截断有效内容。

建议 D1.06 的诊断读取只作为可信 Host 装配获得的私有管理能力，使用 `copy_records(after, span<PublicLogRecord>) -> LogReadPage`，输出 copied、next、accepted_upper、gap/evicted_through；不返回后端存储 span，也不调用 visitor。after 含 Host/stream，跨世代拒绝；输出容量 0 明确拒绝，页上限受配置约束；只复制截至调用开始捕获的接受水位，不 pin 历史。读取不清空记录、不影响 accepted/evicted 计数，锁争用返回 Busy。

候选读取签名（runtime 内部管理表面，非新增业务旁路）：

```cpp
struct LogReadPage {
  uint32_t copied;
  LogPosition next, accepted_upper;
  uint64_t evicted_through;
  bool gap;
};
class MemoryDiagnostics : public PortLifetime {
public:
  virtual LogResult<LogReadPage>
  copy_records(LogPosition after, std::span<PublicLogRecord> out) = 0;
};
```

MemoryLoggingBundle 的两个端口共持有同一有限内部状态，关闭不清空记录，最后一个端口 owner 释放时回收槽。工厂不能为已有 Host 重新取得诊断句柄；普通调用者自行创建日志实例只能读自己的实例。读取句柄身份与写入流严格绑定。

该读取句柄不能由任意 Native 参数、CallerDescription 或 bool admin 构造，只由 Host 持有并按装配授予可信管理消费者。本包不开放外部日志 RPC/Native 读取操作，不能宣称已有逐主体授权日志服务；未来对外读取必须先经过现有 Policy 验证并在复制/传输前复核有效授权，不能把 Public 格式当成允许所有人访问的授权。测试控制器可通过相同受控装配得到读取能力，不加 friend/裸槽访问。

## flush、关闭、借用与错误隔离

snapshot 返回的 LogPosition 或 Accepted 回执可作为 flush 目标。flush 只接受相同 Host/stream 且 sequence <= 当前接受水位的目标；0 是空前缀，未来/跨实例水位拒绝。它返回 `covered_through=through`，含义是所有序号 <= through 的已接受记录在该次 flush 点已经完成此后端的同步内存写入，或按已声明保留政策被淘汰；evicted_through 精确区分两者。已淘汰记录不重新出现，不因 flush 改计数；flush 成功不声称导出/持久化或永久保留。并发后来接受的记录可以顺便完成，但不扩大此次声明的覆盖水位。反复 flush 同一水位幂等；已关闭后仍允许验证合法旧水位。后端故障返回 BackendFailure 并增加 flush_failures，不能用空成功覆盖已经接受范围。

close 在取得锁后建立拒绝新写的栅栏，捕获最后接受水位 H 并完成同语义内存 flush，成功后返回 H。相竞争写入在线性化点前则计入 H，之后则 Closed 拒绝；Busy 重试不算 close 成功。合法测试后端 close 的后端故障允许状态停留 Closing、已停止接受，重试继续收尾；不能失败后重新开放写入。重复成功 close 返回同一 H；close 后 snapshot 和有界读取仍可用，最终所有者销毁才释放缓冲。close 内不销毁共享依赖，析构也不能发生在后端锁内。

本包端口没有订阅/完成回调参数、没有后台线程或延迟任务。LogInput/span 仅同步借用：调用返回即不保留，对 Accepted 也只保留公共固定值。LoggingPort 与 SafeLogger 的共享所有权保证在途调用持有稳定对象；Host 停止应先阻止新生产者并等待在途业务/模块停止，再完成日志 close，最后释放日志和管理读取句柄。任何借用 Logger 引用不得越过其 owner；对最后 owner 的并发销毁属于无效调用，不用所谓 close 成功掩盖该 C++ 寿命要求。

共同 shutdown_callback_lifetime 项仍必须执行：检查返回后源缓冲可改写/销毁，关闭后的写不保留对象，完整 close/释放后无未来动作，活 owner 时并发 close/写的返回范围守恒。零回调表面本身不能把整个共同项设为 NotApplicable。Conformance 的记录器/析构哨兵是测试对象；若合法测试后端有内部捕获器，其所有调用必须在自身锁外并受测试工厂寿命拥有，本候选更倾向两个后端均完全无 callback。

普通日志错误（满载、Busy、格式拒绝、后端异常、flush/close 错误）不能修改 BusinessStatus、Application、许可、目标、提交/效果事实或执行回执。Host 首要启动/清理错误保存在独立固定诊断结构：写日志失败只形成附加诊断计数，不能覆盖首错或递归记录。RequiredRecordPort 与普通日志不互相实现，不从环缓冲重建任务、DomainEvent、Outcome 或 RequiredAudit。

## 共享 Conformance 子断言候选

工厂形状候选 `LoggingFixture make_fixture(LogLimits)`：返回公共 LoggingPort、可信诊断读取句柄、仅测试侧 controller 和实现 descriptor。默认工厂装配真实 MemoryLogging；第二工厂装配独立固定容量合法 TestLogging，必须运行同一共同函数，不能复用 default 内部存储实现充当第二实现。controller 仅控制合法后端故障（接受前失败、flush 失败、close 收尾失败），不能读写生产私有槽、修改共同集合或注入生产测试开关。非合格后端（谎报接受、接受后抛出、借用逃逸、伪 flush 等）单独证明套件能拒绝，永不 qualified。

| 已登记共同项 | 两工厂必须实际执行的子断言 |
|---|---|
| accept_reject_drop_accounting | 空计数；有效写唯一连续回执；capacity=2 后第3次按 descriptor 策略精确验证返回、槽内容及三类计数；非法枚举/重复键/超字段拒绝；过滤、关闭后拒绝；有限真实多线程并发结束后精确守恒、无重复序号/越界；固定上限拒绝与算术溢出 |
| format_redaction | 零字段和四字段格式 golden；默认 Redacted 对多种非零秘密哨兵都不出现原值；PublicCode/Count 仅合法静态键；排列规范化；源字段返回后改写不影响已存值；无 ErrorInfo/对象地址/自由字符串入口 |
| error_isolation | 满载/非法日志后正常业务仍返回原实际值与 Outcome；SafeLogger 包装合法可失败测试后端，接受前抛出计 facade 拒绝、之后还能写；flush/close 失败不改变已接受水位与业务事实；错误路径不递归；非合格“接受后抛”不能 qualified |
| flush_accepted_range | 空前缀；多个接受后捕获 H，加入后续记录后 flush(H) 覆盖只声明 H；DropOldest 精确报告已淘汰前缀；RejectNewest 被拒绝不进入水位；跨实例/未来目标拒绝；同 H 重复幂等；合法测试后端失败后重试只承诺真实覆盖 |
| shutdown_callback_lifetime | 先 close 后写拒绝；重复 close 相同水位；实际并发 close/有限生产者的 H 归属和结束守恒；输入 span 栈寿命不保留；释放外壳/读取句柄与最后 owner 顺序、析构计数/ASan；关闭失败不谎称 quiescent 或重新开放；零后台动作 |

默认实现没有可注入故障点，但 error_isolation 共同项不能 NA：它仍跑满载/格式错误与消费者故障隔离组合；对合法测试后端的可控后端失败检查是同一共同套件接收 fixture 能力后的附加路径，不把默认实现“从不失败”视为无需测试错误隔离。若审核要求每个后端都实际经历系统级资源失败，应在进程级分配故障消费者实现，并明确与生产工厂/正常运行来源一致，不给生产 API 加开关。

manifest 候选版本 `ock.logging/1`，记录实现路径/摘要、工厂身份、overflow_policy、有限 limits、capabilities（async=false、file=false、callbacks=false、volatile_retention=true）和测试证据。共同五项固定于套件，descriptor 不允许 skip/expected 覆写。`async==false` 只使原 async queue_full 专项 NA；内存 capacity 满载始终在共同项运行。`file==false` 使 rotation/disk_faults NA，带合同表达式和理由；未来 Profile 要求 async/file 时，本包两实现装配失败，不能 NA 放行。callbacks=false 不能关闭 shutdown_callback_lifetime。故障控制能力仅存在测试 descriptor，不成为产品必需可选能力。工厂少跑共同项、未知字段、摘要不符、伪 capability、故障后端被标 qualified 都要有 runner 反例。

Conformance 的真实并发由测试消费者自己创建有限线程，用于检验端口共享调用；这些线程不属于 NativeSubset 产品启动配置，不能将测试配置的线程数伪装成产品线程测量。

## D1.06 最小 Host 装配及 D1.05 边界

Constructing/Configuring 验证预算并创建日志固定槽；Validating 确认静态模板、实现身份和必需同步内存能力；后续启动失败使用独立首错结构，按已成功步骤逆序清理，日志仍存活到最后可记录清理摘要。Ready 前不开放业务。日志的初始化、预热次数和保留容量都是报告里的有限量，不能无限 warmup 换零分配。候选静态生命周期事件至少包括配置完成、Ready、StopAccepting、Stopped/启动失败摘要，但不记录任意配置值或凭据。

不修改 D1.05 固定成功窗口的定义、授权/目标检查或执行语义。Host 在启动时给最小消费者绑定稳定 SafeLogger；固定场景选择已声明静态事件和预分配存储，日志 try_write 本身在完整调用窗内计数。若主设计决定固定 Native 每次发一条诊断，必须把格式、写槽、满载淘汰及返回连同调用后结果析构一起纳入 D1.06 新整窗；不能把写入延迟到窗外或线程上。至少分别测空闲容量和持续满载时的成功调用，并断言原业务结果。D1.05 原独立用例和证据保留，不把未发生的日志工作混称已测。

零新增分配计数仍沿 D1.05 C++/CRT/ASan 独立通道及正负探针，不合并渠道掩盖 ASan 原 CRT 盲区。记录初始化、首次使用、有限预热、满载驻留/峰值、错误详情隔离及最后 owner 回收的独立窗口。Release 正式延迟采用等语义无计数构建。默认内存日志不创建线程；NativeSubset 真实链接、Ready 内存/线程、退出曲线和有限预算交由主任务 footprint 设计，本文不编造其批准值。

## 提交主设计需裁定的有限选择

建议主设计采纳固定静态模板/键表，默认 DropOldest、合法测试后端 RejectNewest，以及 callback-free 同步端口；这能在本包完整覆盖共同语义且保持范围有限。需统一裁定新增公共头与诊断读取句柄的稳定级别、静态模板表和记录形状上限、Host 首错固定结构、SafeLogger 汇总计数表面。若改为自由文本、异步 consumer、回调订阅或文件日志，须重新审核脱敏、错误及寿命合同，不能当作本文方案的实现细节直接扩大范围。

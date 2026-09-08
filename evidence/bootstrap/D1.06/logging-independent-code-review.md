# D1.06 Logging 独立 CODE 复核

actor_type: AI  
reviewer: review_foundations（未参与 Logging 实现）  
status: ChangesRequested  
范围：下列九文件、已批准 logging-api 合同及本次局部 Debug 十项证据；不是 D1.06 包级验收。

## 来源绑定

logging-api.md SHA256：`007317558b76b499dd6272c95e4637b59eaa443b2b947fa914310a7d7f1651aa`。九文件集合摘要：`5d72a029b7295ca7431130950765bb0b4c72a5cf6c3c88b311fda8383e69369a`，算法 SHA256(sorted(path + TAB + sha256 + LF))。逐文件当前字节与最终局部运行 source 快照已独立比较相等。

| 文件 | SHA256 |
|---|---|
| `packages/contracts/include/ock/contracts/logging.hpp` | `efbaf36bbb24fbbc4edbe55a576e24b27ffef9a3b8f25dd30fbe043713a3b7a7` |
| `packages/runtime/include/ock/runtime/logging.hpp` | `fea2a70741172afe7f2d6f690176de02976ab1be03aac2fab011395877aa320c` |
| `packages/runtime/observability/logging.cpp` | `188e2d0a7e0680470be364351fb1b6272923906a7b6c0c51cd3021b49acc00ba` |
| `tests/conformance/logging/CMakeLists.txt` | `e01d0d2a64bf3b4de9a368cfef3669630909a971b2b27be6c135e70757400f9b` |
| `tests/conformance/logging/README.md` | `2a88f1338eb651a66332bc0c0ecdc20ced1db93477de9b86dbf317266b623d1b` |
| `tests/conformance/logging/cases.json` | `748de72c00261d90278ebff40f746e0b000da953d9292a5519fe8617f5971e7a` |
| `tests/conformance/logging/develop.py` | `a6126d51911faec4a058e802781e92817693b1e8424210c06ff4a026af830959` |
| `tests/conformance/logging/logging_tests.cpp` | `1be5a951a2fe58f0017cd30b9e621dbe83872e9b8c24832be6f8a7c7561d5aa0` |
| `tests/conformance/logging/reference_backend.hpp` | `85e6f421a6b3841a3533e482a720f55aa03ca5f44f31e41d2df5e503a2587ef0` |

## 必须修复

1. **P1：空共享 owner 的非空别名被接受。** `packages/runtime/observability/logging.cpp:255–256` 的 create 仅检查 `!backend`。调用者可合法构造 `shared_ptr<LogPort>(empty_owner, raw_backend)`，get 非空而 use_count 为零；当前会调用 snapshot 并发布 facade。State 复制此指针不能保活后端，随后独立原 owner 回收会令在途/后续虚调用悬空。违反合同 §2 空 owner→InvalidLogger、§6 State 拥有 backend 与在途身份保活。创建边界须同时拒绝 use_count==0，且在任何虚调用前拒绝；补空别名返回 InvalidLogger、snapshot 调用数不变，以及真实 owner 的合法别名正控制。不得解引用释放后的指针。此项为源码确认，本复核未运行逻辑 red。

2. **P2：共同寿命项没有验证默认后端最终回收。** `tests/conformance/logging/logging_tests.cpp:105–138` 对注入 Fixture 的 weak 仅在所有 owner 仍在作用域内时断言非过期。最终销毁计数断言只针对另建的 TestBackend；所以 memory 名下执行同函数不能证明真正 MemoryLogging 的最后 writer/logger/diagnostics owner 释放。合同 §7 及 §8 的共同寿命职责要求补相同参数化控制：真实 Fixture 作用域中释放写入/facade 后，管理 owner 仍可读取且 weak 未过期；最后管理 owner 释放后 weak 过期。保持原十个主名，无需新增审批或生产接口。

## 已核对实现与测试

- CoreContracts 提供 inline log_error；固定记录、公开字段格式、Redacted 不读取 value、尾零和静态上界；默认固定槽不扩容。输入 span 不保留，分页失败先验证后复制。
- 默认后端同步短锁保护接受/淘汰和关闭，Busy 与过滤/非法/关闭原因分别计数。flush 返回 H 及 min(E,H)，分页捕获全局 E 与 U；关闭后旧 H、读取和重复 close 可用。测试后端为独立 RejectNewest，默认后端为 DropOldest，共用五个函数而非复制两套断言。
- SafeLogger 的四个方法先复制 State，此后不访问外壳；回调在 facade 锁外。TLS 以 LogPort 身份覆盖 create、4×4 互调、同后端多 facade、A→B→A、十六层预算。接受前异常、控制错误、非法域/详情/报告与故意接受后抛的非合格控制分开，facade 不伪造 backend 计数或自动重试。
- 已有自毁外壳/异常回程、最后在途 State、Closing 失败保留及显式重试控制真实挂接。上面两项修复之前，这些正常 owned 输入的检查不能扩大成所有输入安全。
- `error_isolation` 的局部 `int business_result=17` 未接真实 Host/Native，只是局部消费者示例；不得用该恒定变量断言证明实际业务结果、权限或 Outcome 隔离。主集成仍须承担已规定的真实组合职责。

## 原始运行核对

独立读取 `evidence/bootstrap/D1.06/logging-debug-32c863fedb48` 的 sources/result/commands/JUnit、各命令原始摘要及 JUnit 完成标记。当前九文件和冻结源码字节相等；expected/discovered/executed 十项精确一致且无重复，JUnit failures=0、disabled=0、skipped=0，每项 status=run 且有自身 assertions_completed 标记。

configure/build/list/CTest 四命令均 Exited、exit_code=0；WindowsJobObject 均 assigned_before_resume=true、active_after=0、terminated_owned_job=false。八份 stdout/stderr 摘要与 commands 登记一致。

- result.json SHA256：`d9d3f765e584d076384b956dfc746e4a8932bc37b0545db647193e7e1ebb8a0e`
- junit.xml SHA256：`375fc194336eae5b2a85091221ea733f7d891086ed1b65b35d46773328a33839`
- commands.json SHA256：`ce6c7770e37d137799bad695471e88e0517102a8036b4af8c434258acc159323`

这是已执行局部 Debug 的十项 Passed，不是根工程、Release/ASan、安装、Host 或 footprint 的 Passed。develop.py 此次直接编译冻结 logging.cpp 与消费者；根 CMake 的 OCK::Runtime 链接与安装闭包需要主集成验证。本复核仅执行只读摘要/XML核对，没有编译或重跑矩阵，没有覆盖旧失败记录。两项修复及真实对应控制通过后应针对新 SHA 增量复核。


---

## 增量闭合复核（2026-09-08）

actor_type: AI  
reviewer: review_foundations（独立 CODE）  
status: Approved（仅本九文件 Logging 实现及共同职责；保留上文历史 ChangesRequested）

当前九文件集合 SHA256：`eb4d620021221d3dba5fd9b81f771d415b7463f7047bad5911f8e17a363bb399`，按上文算法独立重算相同。仅两项来源变化：

| 文件 | 当前 SHA256 |
|---|---|
| packages/runtime/observability/logging.cpp | a28ab62996a4360eb0907dff6cff65ead4b1ce5939b0f3ed7bd2bc3887c79067 |
| tests/conformance/logging/logging_tests.cpp | c57d91df0b45823420cf9024a3edafb4f5a883af1d0a021791764851c2ce5350 |

其余七文件 SHA 保持上表值；九文件当前字节与增量 green/source 相等。逐字比较原十项运行快照，生产仅增加空控制块拒绝，测试仅增加两类控制；没有修改格式、环、flush、计数或重入实现。

- **P1 已闭合。** create 在 CallFrame 与任何虚调用前拒绝 `!backend || backend.use_count()==0`。测试验证非空借用别名确实 use_count=0、返回 InvalidLogger 且 backend calls 不变；真实拥有控制块的别名仍成功，外部 alias/owner 释放后 facade 可 snapshot。此检查不声称验证任意别名指针一定属于控制块对象；调用者仍须满足实际对象寿命关系。
- **P2 已闭合。** 同一 lifetime(bool memory) 通过真实 Fixture::bundle 生成两类后端，分别释放 bundle/writer/logger 后证明 diagnostics 仍能读取，最后释放 diagnostics 后两类 weak 均过期。默认 MemoryLogging 与独立 TestBackend 都执行了该控制。

修复前 `logging-debug-153137881b23`：configure/build/list 均 Exited0，CTest Exited8；四项中的两个 error_isolation 在 borrowed_alias 的 InvalidLogger 断言实际失败，两个寿命项通过。这是原生产创建缺口的真实逻辑 red；寿命补测本身没有声称修复前失败。

修复后 `logging-debug-07dbd6b738d9`：两后端 error_isolation、shutdown_callback_lifetime 共四项精确执行、无跳过、均有完成标记、全部 Passed；四条命令均 Exited0。两轮各四命令均 Job 在 Resume 前挂接、active_after=0、未终止 Job；全部 stdout/stderr 摘要已独立重算一致。green JUnit SHA256 为 `629a9218d79d277961e897d5c739a3e572fa3aa8a94fe58f318947e472a0cabb`，commands SHA256 为 `63de1c30e4701c791113dfc6ed3dd2226742a0a51d747f83f6a676fa01e33d46`。

结论基于已核验原 Debug 十项、有限两文件差异和本轮四项增量，不把旧十项记为当前全套重跑。本次审核没有重新编译或执行消费者，只做源码与原始记录核对。根工程三配置、SDK/安装、Host 真实结果隔离、footprint 数值与包级自动验收仍在此范围之外；本页不批准尚未实测的这些事项，也不代表 D1.06 Passed。


---

## 分页与固定写分配补充复核（2026-09-08）

actor_type: AI  
reviewer: review_foundations（独立 CODE）  
status: Approved（仅本补充实现与已有 Debug 增量事实）

当前十二源集合 SHA256=`c2210fef93a5185f1f48e8929a16772ae6bd08f803950bf206343f099bb90a67`，按 path+TAB+SHA+LF 排序算法独立重算一致；逐文件当前SHA与 logging-supplemental-self-review.json（文件SHA `68e9563eed4151d2725b39f45c716a25443ccff1e4ef808948e6b79d3f532ab1`）相等。该JSON保留完整十二路径/SHA，作为本页精确输入表。生产Logging三文件及原D1.05探针两文件未改。

绿色冻结源码中实现、测试和驱动十文件与当前相同；README.md和cases.json是运行后说明增量，分别按JSON的tested_sha256核对，不宣称当前十二文件整个集合已原样执行。两者不参与C++或发现逻辑，阅读差异未发现行为改变。

独立TestBackend只将动态string/to_string改为有限逐字符追加及栈上20字节反向十进制转换，输入验证/事件表保持，包含0与uint64最大值的边界容量充足；未调用生产格式器。没有改变实际后端身份或满载策略以掩盖成本。两后端format/accounting共同职责仍使用原函数。

新增memory_pages实际检查容量2、两次读之间继续淘汰、精确E/U/next/gap、尾部不改、空页、跨Host/stream及未来游标/页长拒绝、失败哨兵不改和关闭后读；生产读取逻辑未改。固定写测试直接复用原探针，12入口正控+无分配负控先执行，每后端初始化/首次/4次预热/填满/最终释放单列，40次接受及40次持续满载的完整返回析构都在窗口内；内存后端满载接受淘汰，测试后端明确Full拒绝，真实保留计数也检查。

原始核验：

- `logging-debug-31f4d8578d6c`：configure/build/list Exited0，CTest Exited8，两项中分页通过、分配失败。red没有独立allocation-report.json，本次从唯一原始CTest stdout JSON读取189条报告；verified=false，memory80窗cpp/crt均0、test80窗cpp/crt各1200。
- `logging-debug-a38cae37b3cc`：四命令均Exited0，发现12项，本次精确执行六项、JUnit无跳过且全通过。189条报告verified=true；逐后端独立数得40个fixed_accepted_write和40个sustained_full_write，实际决策正确且全部cpp/crt为0，asan_allocations为null。12入口与第13个无分配负控名称顺序真实存在。
- 两轮所有命令归属前暂停、Job实际排空且未清理杀死；每份raw SHA核验一致。green allocation-report SHA=`24c9e3a54eb6cd5762fd879eb1e11775843aee94995d5f9552b8bd0c26fcc4d3`，JUnit=`da64565d01329ba299cefd3baa837554ca6562e1a6f1d37facccfbba1cf08590`，commands=`7c8a24d876ff8e0f68b17d61ae58cffb3a625e985621f2b81a5b84b86f2aaba9`。

代码增量未发现新阻断。本页不重跑或冒称重跑原十项；只批准上述新增消费者与独立格式器修改。仅Debug无ASan事实，其他配置有效通道仍须实际验证；C++账本活字节不是PrivateUsage，普通日志窗口不是HostBound/footprint整包结果。原故障、owner修复和此前范围记录全部保留，不代表D1.06/G1 Passed。

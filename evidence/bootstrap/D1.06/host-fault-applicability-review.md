# Host 故障适用性独立复核（待诊断）

actor_type: AI。结论：PendingDiagnostic；仅 SPEC 适用性意见，不批准修改测试，不是 CODE 或包级 Passed。来源逐文件 SHA 及原始记录回读结果见 host-fault-applicability-inputs.json（SHA256 c26e92f7ad4102d24b7484d43e8df35b0acbbdbb7cadcb9733dcafd3ada9d706）。

## 已核实事实

锁定 MSVC 14.44.35207 的 vector/xmemory 整文件 SHA 与 source.json 一致。vector:670 默认构造声明条件 noexcept（默认 allocator 满足），内部调用 _Alloc_proxy；xmemory:1219 的 Debug 代理通过 allocate(1) 实际分配。因此该路径抛 bad_alloc 会进入 terminate，外层 Host catch 不能恢复。此源码结论不代替新故障探针的运行定位。

原 sdk-stage-da08b48728 的 create 仍是实际 Exited 3；不能称完整扫描通过，也不能仅凭该退出码认定具体 terminate 原因。host-fault-followup-c925c3b707 的 start/internal/公开 shutdown_errors 三项实际 Exited 0。上述原始字节摘要及 Job assigned-before-resume、active_after=0、未强制清理已独立核对。

## 有限适用性判断

1. 架构 v3.3 A03.1 明确不可恢复 OOM 不伪装可重试业务错误；执行计划 D1.06 要求每个启动部分失败均清理。已审 native-test-scope:13/16 冻结的是真实随机源、登记/目录、factory、facade State/初始 snapshot、Policy 和逐模块返回/异常等逻辑窗口，没有要求每配置每一次 new 穷举。测试后来添加的完整逐 ordinal 扫描可以准确区分平台不可恢复路径，但不能倒写失败历史。
2. Host API:120 的 bad_alloc 映射仅能指可传播到该异常边界的异常。若独立诊断成立，应在现有合同中作这处窄适用性澄清，并明确具体工具链/模式；不能推广为所有 OOM 都无需恢复，亦不能将任意未知崩溃当合法探针。
3. Debug 首次可恢复 create 分配与深拥有正控制，只证明该精确窗口；不能声称穷举 Debug 可恢复分配。facade/Policy 指定失败、已取得 backend 的实际清理/重试和全部原逻辑职责仍保留。Release 同源全扫是补充验证，不能替代 Debug 未测窗口，且实际结果未取得前不能假定无 Debug proxy 就全部可恢复。
4. native-test-scope:5 的“不因参数化省略故障窗口”和 :115 的风险配置职责仍适用。不得按 size 跳失败、改变 iterator ABI、修改旧 expected/报告来使其变绿。新的明确模式映射应在执行前受审；Debug 平台 fail-fast 是独立终止验证，不能计为 Host RAII 成功。

## 尚未满足

待新 Debug ordinal2 的固定 terminate86/ordinal/size/calls 标记及孤立 vector 对照、对应源码/二进制身份和 owned 原始材料；待 Release 同源完整扫描的实际结果。随后才能给最终适用性判断与有限测试映射意见。正式 Debug/Release/ASan 职责、完整子断言及 D1.06/G1 验收均不由本记录关闭。本次未运行测试、未修改源码或冻结测试输入。


---

## 诊断齐备后的有限 SPEC / CODE 增量（2026-09-08）

actor_type: AI。SPEC 适用性：Approved，仅批准下表明确平台模式及 A03 的可传播异常边界解释；四份 C++ 故障源 CODE：Approved，限定这些控制自身，不代表原 Host 全部子职责通过。fault_windows.md 的模式说明以及正式包装器映射尚须同步，下文给出精确要求；不存在对未写出的改动的 CODE 批准。前文 PendingDiagnostic 和历史 Debug 全扫失败保留。

机器回读材料：host-fault-applicability-diagnostic-check.json，SHA256 `52f35c71fd5f11cc376ae6361b0ffcc807f8bb76ebe0c5c8e47048b2563ed642`。绑定五文件：

| 文件（tests/contract/host/） | SHA256 |
| --- | --- |
| fault_main.cpp | 78bed297889ce089ddeb54bdb7d53f532a21f1a16c28099395651c8ba3529789 |
| fault_allocations.cpp | 289df71358ea3d8ecd12d23cf9bd5dcbb18aef3467d5053b5f00dd0d5e6f1016 |
| fault_allocations.hpp | 0d853395860673fdd483f1a500b3b16484cba8464e013cb08db0df6f499c8d45 |
| fault_host_unit.cpp | 0704cff1078d5aacb4f4fcbb008a909b101fd0b2003281c4796a2a0bd739a264 |
| fault_windows.md | 571479914e66201fc3696eaf25e7707dce41b2959072610bb46505bdab5fe6a7 |

### 原始证据与代码判断

host-diagnosis-71e02b8f56 共九命令逐项验证 observed_exit_code、原始 stdout/stderr 字节 SHA/大小、binary SHA、build_inputs SHA 和严格 Job 排空。两构建 host-fault-incremental-9a33ff654c / 65f554f44b 的全部输入、生成项目、配置/构建原始字节及导入 archive 均回读匹配。实际仅编三个 fault 翻译单元，使用各自冻结源/配置对应 Runtime。fault_host_unit 直接包含生产 host.cpp，仅在本翻译单元替换 BCrypt 调用；宏随后解除，Real 正控调用真实 BCrypt。正常链接无 FORCE:MULTIPLE/WHOLEARCHIVE，直接对象中仅 fault_host_unit 定义 Host，成功链接未引入另一强定义 Host；不能将该测试副本称为未改随机调用的产品 Host 二进制。

分配器覆盖本 EXE 的 scalar/array、aligned、nothrow 及对应释放形式；单线程一次故障在抛出前解除 fail_at，计数仍继续，异常后第二次分配正控成立。它只计该 EXE C++ new 活块，不是 CRT/DLL/ASan 通道有效性证明。create 先测固定输入成功次数，再真实命中每个被选 ordinal，验证错误 code、活块和 owner 计数恢复、无端口回调；深拥有正控观察最终 weak 失效。Release 实际基线为 9，1–9 全部恢复，不能推广为其他输入所有分配窗口。

start 的后端从 factory 真正返回后才 arm，facade 首次 snapshot 无分配且 State 分配失败；Policy 在第二次 snapshot 后 arm，域/code、快照次数和 Configured/Starting 事件区别两窗口。关闭成功/错误/异常与显式重试均有实际断言。该控制验证 pending/quiescent 和真实 close，不宣称它单独穷举所有最终 owner 泄漏；完整 owner 回收职责仍须原 Host 测试支持。internal 直接调用真实 HostControl 的计数边界与 finish，旧 SessionAuthority 观察 StoreClosed，确证 Policy 先关闭；不冒称公开 Host 启动流程。

Debug create 第二次分配实际 `host_fault_terminate ordinal=2 size=16 calls=2`；孤立 vector 为 `ordinal=1 size=16 calls=1`，均 Exited 86，且无成功结束标记。结合已核实的默认 vector noexcept→Debug proxy 分配源码，足以认定这一锁定平台不可恢复路径。Release 孤立 vector 实际无代理分配并退出 0，构成正交对照；不能把旧 Exited 3 改写为合法探针。

### 可采用的固定模式

| 固定配置 | create 控制 | 独立 STL 对照 | 其余故障控制 |
| --- | --- | --- | --- |
| Debug | create-recoverable → 0；create-ordinal-2 → 86，精确标记 2/16/2 | stl-default-vector → 86，精确标记 1/16/1 | start、internal → 0 |
| 当前锁定 Debug + ASan | 同 Debug 的预定模式；必须在 ASan 本配置实际验证 | 同 Debug 的预定标记，不能引用普通 Debug 实测替代 | start、internal → 0，ASan 实测仍待补 |
| Release | create-full → 0，成功基线范围 1–128 且每 ordinal 全部完成 | stl-default-vector → 0，实际 calls=0、无命中 | start、internal → 0 |

所有 0 模式要求固定成功结束标记、空 stderr 和严格 owned 成功；86 模式要求 observed/actual exit 均 86、stderr 完整精确匹配当前标记、无成功结束标记，并验证 Job 排空。不同崩溃、截断、缺运行、未知配置不可按 86 接受。配置必须来自已冻结构建身份/显式固定参数，不能依据运行结果、分配 size 或探针结果动态选择宽松模式；保持 iterator ABI，不从普通 Debug 二进制冒认 ASan。ASan 当前是 Debug 配置的事实来自受锁配置；未来更改该模式必须重新核对，不能默认任何叫 ASan 的配置都相同。

将 create 控制归入原 T03.host.configuration，start 归入 T03.host.start_failures，internal 及公开关闭异常控制仍归原 shutdown_errors 等既有职责；不得移除主名。fault_windows.md 现有无条件逐 ordinal 和“仅三模式、全部 0”说明应改为上表分配置限定，保留完整扫描诊断入口及原失败证据。Host API 的表述可以窄化为“可传播至边界的 bad_alloc 映射 BudgetExceeded；锁定标准库 noexcept 内不可恢复 OOM 按 A03 单列终止，不承诺捕获或 RAII 返回”，不豁免可恢复的逻辑窗口。

本增量关闭的是诊断及其有限 SPEC 适用性，未执行新测试、未修改生产/测试源码。原配置/启动各逻辑故障职责继续有效；正式包装器、ASan、自身分配通道正反控制及全包正式验收仍未由本记录关闭，D1.06/G1 保持 InProgress。


---

## 三配置包装器及 ASan 实测闭环（2026-09-08）

actor_type: AI。原 wrapper `aadb7e7da43b49e063cbadcc1fde275c17e3597838637677dd6c22539fff094b` 的 CODE 一度 ChangesRequested：混合 stdout/stderr 的子串检查会接受错误流、附加错误或混合成功标记，且未核 observed_exit_code；不是否定其九轮实际子证据。旧记录不改。

修复后限定 CODE Approved：verify_runtime.py `d956d97abf1a791725027e7484637c127816373b64753423879f439547b5356d`；test_wrapper.py `3b24023db645c66e3c6f6061cf4084e50466f66b46e522934aeebcd072d9064b`；CMakeLists.txt `8497b78f25028edfeb86b5c7257f6556b3004bc3eaf3b04cb848e9bfef6734fd`；fault_windows.md `6fad67d3c0580548b3ee0c909ef4e4b251540f1fb2afc18f587aab33fa5f4f04`。Host API `e2de47d23ebfa02720208b9799efb2436b665097a9ba649789e50248139b93a4` 的可传播异常边界文字与既有有限 SPEC 判断一致。CMake 显式传 $<CONFIG>，三个配置沿固定模式，不依运行结果选预期；历史诊断段已明确标为历史。

独立仅重跑两个很小的纯包装器测试轮：原冻结 wrapper 的七种污染子断言真实失败（Exited 1），当前两测试/七种污染拒绝全部通过（Exited 0）。分别由原 execute 拥有并确认排空；没有重跑 Host 矩阵。当前检查要求 observed/actual 退出一致；终止 stderr 仅允许精确单行 LF/CRLF，stdout 无成功标记；成功故障控制要求空 stderr、唯一末行成功标记。修复闭合本次问题。

host-wrapper-check-23e52628d1 的 9 包装/23 子命令原始记录、二进制及 Job 已逐项核对。ASan fault 独立构建 host-fault-asan-1de3e8c585 的全部输入、实际 archive、生成项目、构建 raw 匹配；实际使用 /fsanitize=address、/MDd 和 OCK_ENABLE_ASAN=ON，无第二份生产 Host 强定义。ASan 的可恢复 create/start/internal 为 0，两个 Debug proxy 终止分别为 86 及精确 2/16/2、1/16/1 标记；这关闭此前 ASan 平台诊断“未实测”，不冒充 RAII 成功或 ASan 分配通道校准。

新严格 wrapper 的 host-wrapper-check-6cf27412d0 再次 9 包装/23 子命令，逐项回读退出、原始流摘要、当前二进制及实际标记均成立；复用原同配置二进制，不拼成新生产构建。独立机器材料：host-wrapper-independent-check.json SHA256 `0e7caf42182aa0f9751df1ca844f20c5ae64d13fe6efbd7fbe1e9131204861b8`；host-wrapper-strict-independent/checks.json SHA256 `cff48ae018d88e093933e6cef62be269b777216255143b4e1e7f11d476f47f32`。两轮每配置仍只有 configuration/start_failures/shutdown_errors 的有限包装职责；未重跑原全部 20 主名或正式矩阵，不据此批准整个 Host、D1.06 或 G1。

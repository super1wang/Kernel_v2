# D1.06 Host 独立 CODE 收口复核

actor_type: AI  
reviewer: review_foundations（未实施 Host 代码）  
status: Approved（仅下列核心实现与现有消费者的代码审阅；包级仍 InProgress）

## 精确范围

逐项核验当前六文件 SHA 与 `sdk-stage-3593be88f8/source` 字节相同，输入索引为 `host-review-inputs.json`。故障补充 `fault_host_unit`、allocations 随机/分配窗口与私有饱和探针不在本范围。

| 文件 | SHA256 |
|---|---|
| `packages/runtime/host/host.cpp` | `ddc0996b9fc95dbe46c4c46654e119e9442844f973d18acbf70aa6de841ab74c` |
| `packages/runtime/include/ock/runtime/host.hpp` | `6c53eddaa6ad8a343c6d06b113374afe3795337aadaea853f3851d80dac9a279` |
| `packages/runtime/include/ock/runtime/detail/host.hpp` | `4c8ad948aa390d42e9284e749ed7d71e3d0df2ae6896d2062a243a514e50e7d6` |
| `tests/contract/host/host_tests.cpp` | `310ab0e5f12f48a929bca8fa4ef27bd9964fa168dc997a713eaf91c054852cae` |
| `tests/contract/host/verify_runtime.py` | `027cd47e508186fe6366d0c02da1248791ecda4b26f02b79f46ebba9fd805590` |
| `tests/contract/host/CMakeLists.txt` | `577d99f73a08b7dddf07de6d9328e88f61b39a1897e49a01ed83735eae1b38ff` |

规格依据为原合并五合同及已批准的 D1.06-spec-applicability-delta.md（Host 合同 0220e3c7…ae0f、测试范围 11b8b47e…df4a6）；Logging/SDK 的独立报告作为既有配套复核，不在本页重复扩大其批准范围。

## 初始四项闭合

原 `host-initial-code-review.md` 不改写。本轮核对：

1. report 区分生命周期占用与最终 Complete/CompleteWithErrors；factory 正在运行时，Reentrant/Busy 不再因暂时无 pending 而误报 quiescent。测试同时覆盖 factory 内重入和另一线程 Busy。
2. create 调用原 Native `detail::validate_budget` 受检乘加总预算，先于任何日志 factory/模块；NativeEngine 自身仍调用同一 helper。configuration 的 observation_capacity=SIZE_MAX 拒绝已挂接，其他超限仍按 helper 源码审阅和各自既有控制归属，不将这一断言称全部字节极值实测。
3. close_logging 在 LogClosing 回调后再次检查实际 steady deadline，再决定是否 close；模块 stop 返回后先检查是否完成/是否超时，不在过期后开启后续日志工作。shutdown_deadline 包含 LogClosing 吞完预算后 close_calls=0、已过期零新回调、最终 close 实际完成但超时仍同时 quiescent=true/deadline_exceeded=true。
4. 非 Ready 准入依据 stopping 区分 NotReady/HostDraining，Starting/factory 的精确 NotReady 控制存在。stopping 与 Ready→StopAccepting 的仲裁均在 Host mutex 内，已准入与关闭有确定边界。

此外 diagnostics 读写都使用 Host mutex；失败启动因日志身份不合格时，真实后端合法关闭回执不再错误要求 Host 本体身份而永久挂起。

## 当前生产逻辑核对

- 生命周期由短锁标志串行化；add/start/shutdown 回调在 Host 锁外。只在准入数量及状态转换时锁定，等待真实在途用 condition_variable；非静止项保留 owner 和 pending，下一次显式 shutdown 才推进。同步虚调用不可强行抢占，超时不冒充回调已完成。
- 成功日志 backend 在 SafeLogger 创建前即持有并登记清理；facade 创建/快照失败仍走原后端受隔离 close。成功模块才进入预分配 pending 栈；失败模块不被错误 stop。失败启动 modules→Policy→logging，正常停止 Policy→模块逆拓扑→logging；失败项不弹栈，quiescent=true+error 可记录后继续。
- HostSession/HostBound 的可重入方法先复制共享 state；HostBound 保持原 NativeBound 并调用原完整治理管线，无裸分派旁路。准入局部对象保持到最终返回构造，Result 值负责自己返回后的 owner；现有移动结果测试在每次移动中观察 active_admissions，并实际在 handler 中销毁 Bound/Session 外壳。
- Session 壳析构/移动赋值不隐式 close；Bound 延长原 authority/engine/HostControl 寿命，显式 close/restrict 仍走原 Policy，逐次调用复核撤权和有效期。安全停止后 Host 壳可销毁而保留 Bound 只得到拒绝；未排空 Host 析构 fail-fast，不承诺运行中自毁 Host。
- 固定清理记录只保留 ErrorCode，不读取 what()；日志失败不覆盖原业务结果。现有 logged_compute 通过真实 HostBound 执行，观察安全日志异常计数且实际结果仍为4，比局部恒定变量示例具有组合证据。

## 已核验运行适用性

`host-ctest-59106d6953` 对 `sdk-stage-8f6e72bfae` 的旧开发快照实际发现/执行20个主名，JUnit 20项无失败/跳过，SHA256=`765d4be648430461d33771be706503a3f605eaf4902014d89939847b26cb4fde`；仅记录该快照的事实。

后续 `sdk-stage-41f224b68b` 实测 shutdown_deadline/logging_ownership 两项，`sdk-stage-754054ec5d` 实测 start_order/start_failures/session_ownership/delegation_revoke/lifecycle_reentrancy 五项，`sdk-stage-3593be88f8` 实测 catalog_registration/session_ownership/shutdown_errors/return_lifetime/start_order 五项。三轮核心 host.cpp 均为本表 ddc0996…ab74c，测试输入各按自己的快照保存，不把增量拼成最终整套重跑。

四目录共20条顶层命令均 Exited0、Job Resume前挂接、active_after=0、未清理杀死Job，各自raw SHA核验相等。旧20项中的两个包装继续沿JUnit指向核对原子记录：析构safe退出0，ready/active/failed三个边界各退出3；校准退出0、真实注入分配反例退出1。原始流摘要和子Job排空相同。未将任意非零退出当成对应合同失败。

## 尚不能据此验收的职责

- 本页不把20个主名Passed等价为 native-test-scope 全部子断言完成。当前新增fault单元、真实随机失败/全零、SafeLogger State/Policy装配分配失败、64位计数饱和边界由另一有限任务补充，必须另外绑定SHA及原始事实审核。
- 当前 ControlledLog::close 仅有返回错误控制，没有抛异常控制；尤其 facade 未发布时原 backend->close 的异常隔离应补实际可达反例，不能借 SafeLogger 自己的控制方法异常测试冒充 Host 原始清理路径执行。
- shutdown_order 已观察模块次序/阶段和true+error继续，但该主名本身不证明全部 Policy关闭子事实；Policy 当前close_store无可重复失败路径，按已批准适用性增量分别保留真实成功/关闭后拒绝/owner职责与静态异常分支审阅，不制造伪故障。
- 新 Host 返回/并发/生命周期风险的 Release、ASan 配置，统一最终源正式集合、所有可达故障剩余控制、有效分配通道及真实 footprint/预算尚须完成。默认日志占用与全Owner释放字节不由弱引用或40次零分配单独证明。

未发现要求修改本表核心三生产文件的新阻断。以上代码批准不关闭测试职责债，不改写旧ChangesRequested/失败记录，不声明D1.06/G1 Passed。本次未构建或重跑测试，仅审阅源码和已有原始记录。

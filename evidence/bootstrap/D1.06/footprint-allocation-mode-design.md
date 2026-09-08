# D1.06 allocation 模式有限增量设计

状态：Candidate，待主集成审核后实施；本页只细化已批准方法 §6，不改生产合同、计数器或预算。第一轮仅 Debug 局部反例/正控与一个真实单对，不执行 pilot。线程归因仍 Unresolved。

## 入口、构建与身份

同一 `consumer/pair_main.cpp` 保留 baseline/Native 两个目标，增加有限 CMake 模式枚举 `occupancy|allocation`，默认 occupancy。occupancy 不编译或链接替换 new 的探针，继续输出 counter_mode=disabled；allocation 两目标直接编译既有 `tests/contract/native/allocation_probe.cpp`，使用其原头与实现，不创建第二套计数器，不改 SDK/Runtime。

分配模式复用固定阶段、原始通道和安装公共 API，`ObservationConfig.mode=allocation` 与原 occupancy 不同，方法摘要随之变化。报告进一步绑定配置、CRT、ASan、真实探针源 SHA、counter_mode 与两目标工件，不把两个模式交叉配对。只有校验过对应配置的 SDK prefix 才允许消费；不能将 Debug Runtime 用于 Release/ASan 并宣称该配置合格。

Debug 用原 C++ new/Debug CRT 两个独立可读通道；Release 的 CRT 字段为 null；ASan 用原 C++ new、独立 sanitizer allocation/free 字段，原 Debug CRT 保留实测 0 和 unobserved_asan_intercepted 状态。实际构建风险专项后才能声明后两配置运行过。本轮不改 Release/ASan 系统策略、hook 注册和故障码。

## 窗口及有限数据

计数开始/停止都在消费者主线程，同步调用不产生工作线程。固定栈内样本数组上限 96 条，溢出明确失败；样本记录 Counts、固定标签、索引、是否要求零与能力断言结果。所有格式化、stdout 输出和摘要写入放在计数窗外。

为精确分开首次调用与 bind，把现有 prove 拆为等价的日志证明/Session-open-verify-bind、首次 Read/Compute、非法输入证明三个小步骤，不删现有断言。保持总共两个首次成功调用、四次 warmup、四十次稳态调用，handler 总数仍为 46，非法及停止后的拒绝不增加 handler。

| 窗口 | 范围与判断 |
|---|---|
| construct_register_start | Scenario emplace，包括装配/注册/Host start/实际生命周期日志；成本单列，不要求零 |
| log_proof_open_verify_bind | 同一 Host 的公共日志页/接受水位证明、认证/verify 和两个真实 bind；成本单列 |
| first_compute、first_read | 各一个完整成功调用及结果析构，成本单列 |
| invalid_input | 原始非法参数不进 handler 证明及拒绝结果析构，成本单列 |
| warmup[0..3] | 四次固定成功调用分别计数；成本单列，不临时加次数 |
| invoke[0..39] | 每次开始于 Scenario.call 之前；包含 InvokeOptions/可信时钟、HostBound.invoke 的全部准入/治理/handler/事实环、Outcome/完整返回值构造、业务值检查及结果析构；Scenario.call 返回后才 stop。逐窗要求所有适用新增分配通道为 0，不能按合计抵消 |
| shutdown | 原日志水位不变检查、真实 shutdown、旧 Bound 拒绝及相关结果析构；成本单列 |
| bound_release、session_release | 对应外部 owner 释放和原断言，各自单列 |
| last_owner_release | release_owners 与 Scenario 自身析构合在同一窗，包含最后 weak/control block 释放；记录析构哨兵及 C++ live/peak，不当作进程 PrivateUsage |

`Scenario.call` 内的结果局部对象在函数返回前析构，计数器放在外层，避免在 result 仍活时读取计数。异常路径用本地 RAII 关闭计数，报告失败窗口，退出非零；不能在异常后保持窗口开启造成后续格式化污染。没有公开测试 setter，也没有在任何生产锁内调用用户回调。

baseline 仍只做本地计算和同构阶段，不链接 Runtime；相应初始化/bind/释放标签仅表示 baseline 对齐点的本地工作，不伪造 Host 或服务 owner。两个模式都保持日志默认启用和 Native 全部能力断言；普通 invoke 无新增普通日志的既有事实也仍验证。

## 探针有效性与实际反例

初始化调用两次验证原幂等约定。沿 D1.05 十二个固定分配入口逐项真实正控、无分配负控；保留 new/delete usable bytes 账本和各通道独立字段，不将它们相加。为避免改变 Ready 前进程高水位，allocation 模式的正负探针在 ProcessMainEntered 后、HostConstructionBegin 前完成，并在报告注明该模式含计数器与探针成本；它不会被当作 occupancy/正式时延样本。

新增仅作用于测量消费者的固定编译期注入控制：allocation Native 的第一个稳态窗内，在真实成功 invoke 前直接调用一次全局 operator new/delete。该实际子进程必须完成同样业务/释放断言，报告 verified=false 并退出 1；驱动验证具体窗的 cpp>0（Debug CRT 同时可见，ASan 时独立 asan>0），不能仅凭非零退出声称 red。正常编译不包含注入代码，生产库不变。

每轮分配报告必须严格发现四个 warmup 和四十个稳态索引、完整生命周期标签、适用入口正控、无分配负控和成功业务事实；缺项、重复索引、无效通道、verified=false 均不能通过。原 ASan 注册失败 87/marker 路径在该配置适用时复用既有探针提供的固定测试入口，以独立子进程验证，不能把未运行声明为 Passed。

首轮执行顺序为：新增读数/窗口缺项反例 → 本地 Debug 注入真实 red → 无注入 Debug 分配单对。占用消费者分拆受到影响的能力断言通过同源码无计数构建有限检查，不重复完整旧矩阵。后续配置只在各自真实 SDK/工具链齐备时做必要专项，pilot/数值预算仍沿 D1.06 原审批政策，提交本包后暂停。

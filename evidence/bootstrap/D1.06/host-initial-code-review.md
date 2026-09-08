# D1.06 Host 初稿有限 CODE 风险复核

actor_type：AI。CODE 结论：**ChangesRequested，限定以下初稿来源**。只读检查 Host 回调、失败清理、返回 owner、pending/截止/并发路径；未编译、未运行新增反例，不宣称已取得逻辑 red，也不是全体实现审查。已知 diagnostics 发布并发与错误 Host 日志 close 身份问题由根处理，本报告不重复登记。

| 文件 | SHA-256 |
|---|---|
| `packages/runtime/host/host.cpp` | `c460ffe4b67a3b98a9c246c131799e3d48dd6fd9510ace4d648928047f26dda3` |
| `packages/runtime/include/ock/runtime/host.hpp` | `caa2fe3d1c77a0575388bcfedda36be92357cf9f9d77ff7ffa0c435c27972ca3` |
| `packages/runtime/include/ock/runtime/detail/host.hpp` | `4c8ad948aa390d42e9284e749ed7d71e3d0df2ae6896d2062a243a514e50e7d6` |

## 实际需修问题

1. **P1：在途生命周期可能被报告为 quiescent。** host.cpp:104 的 report 直接使用 drained()；该 helper 仅看 active/modules/Policy/logging。start 在 publish 或 factory 尚未返回时，真实生命周期操作仍在途，而全部 pending 尚空；此时重入/并发 shutdown 返回 Reentrant/Busy 却可带 quiescent=true，与 snapshot 的阶段判断和 Host 合同冲突。统一报告的阶段/生命周期静止条件；正常 finish 完成时处理本次生命周期标志尚未解除的受控情况。增加 factory 屏障下 Busy 和同线程重入报告的真实断言。

2. **P1：无效 Native 字节预算延至 open 才失败。** host.cpp:22–34 的 budgets 漏掉 NativeEngine::create 原有完整受检字节/总和规则。例如 observation_capacity=SIZE_MAX 仅作为非零值通过，create/start 可启动模块并 Ready，所有 open 才在 observation_capacity*sizeof(optional<InvocationRecord>) 校验失败。create 前复用完整纯预算校验，至少覆盖 observation 容量、targets 字节及累计和溢出；用数值边界反例，不实际尝试巨大分配。

3. **P1：LogClosing 回调后仍可能越过截止执行 close。** finish 在调用 close_logging 前检查 deadline，但 host.cpp:118 先执行外部 LogClosing try_write，随后直接调用 logger/backend.close，二者之间没有重新核对实际 steady_clock。回调执行/调度耗尽剩余时间后仍进入下一清理项，违反每次同步 close 前的截止检查。传递/复核截止，过期保留 Logging pending，只在后续显式 shutdown 调用 close。控制应让 LogClosing 回程时截止已到，断言本次 close_calls=0；不把无法抢占回调本身当作此问题。模块 stop 回程后也应核对日志事件与后续清理的截止顺序。

4. **P2：正在启动被误判为 HostDraining。** host.cpp:179 使用 started 判定拒绝码，而第 245 行在进入 Validating 时就置 started=true；日志 factory/Starting 阶段的 open 因此返回 HostDraining，合同要求 Ready 前 NotReady、停止开始后才 HostDraining。改用真实停止阶段/标志，启动屏障断言精确错误码，不能只验证 handler=0。

## 已检查的边界与下一步

本次所见 HostBound 先复制 HostedBinding owner 再取得 RAII 准入，并直接返回原 NativeBound 完整管线；未发现这三份初稿自行提取裸 thunk。生命周期回调主要在 Host mutex 外，失败启动与正常停止采用不同 Policy 清理次序，符合合并 SPEC 的方向；这些观察不构成整体 CODE Approved。

以上四项已交根按同一批次补实际断言/修复，再核对变更及关联证据。新增测试目前仅作为待运行工作，禁止把源码中的断言或计划写成实际 red/green。无须据此重跑全部历史矩阵；其他未审实现/正式预算/正式运行不在本报告批准范围内。

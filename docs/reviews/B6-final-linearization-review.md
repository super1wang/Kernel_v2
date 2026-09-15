# B6 最终 Expiry-Cancel 线性化收口技术复核

AI 复核时间：2026-09-15。被审来源 `5fd2310aec5f8a24ac14b948677930e2a7ced7e3`（唯一生产提交 `B6: close pre-start expiry cancellation race`，基于 `d79ba62` 门禁重开）。执行方案为 `docs/plans/B6 最终 Expiry-Cancel 线性化收口执行文档 v1.0（L4.5）.md`。生产 diff 仅 `packages/runtime/executions/execution_service.hpp`、`packages/runtime/executions/managed_execution.hpp` 与 `tests/unit/state_roots/runtime_state.cpp`（含 Host 固定并发槽 2→64 的夹具预算调整，供压力反例在波内并发）。

## Red→Green 事实

- 新增 F6-01..F6-07 反例在未修复的 912cb75 生产行为上运行：F6-02（Accepted→安装前 expiry 压力探针，8 波×32 个 `deadline=now-1ms` State 提交）8/8 次稳定失败，实际结果 `CancelledBeforeApply`（CancelWon），期望 `FailedBeforeApply`；失败原文见 `evidence/bootstrap/B6/f6-red-912cb75/`。F6-01 单发在旧代码上偶然通过（冷控制线程下 install 先行、Scheduler 自身 sweep 兜底），不作为合同证明。
- 修复后同一测试目标 6/6 次通过（Debug 直接运行）；31 项三配置正式矩阵另按机器证据归档。

## CODE review（R1–R5）

- **R1 expiry-before-install 是否仍会调用 cancel()？** 否。服务 deadline 循环在 `slot.execution` 尚未安装时只记录 `DeferredControl::Expire`（first-writer-wins，`None` 之外不覆写）；`submit()` 安装时在服务锁内消费该原因并只在锁外调用 `expire()`；`Cancel` 原因只在 service close 路径记录。`slot.expired` 布尔与 `closing` 共用 cancel 路径的旧实现已删除。
- **R2 State pre-start classification 是否仍读取 cancel_requested_ 判断 winner？** 否。`ManagedInvocation::completed()` 对 StateEdit 无 reply 的 pre-start 完成只读 `reason.code()==scheduler::error(scheduler::Errc::CancelledBeforeStart).code()`；`cancel_requested_` 仅保留为意图（阻止接受 child、父取消传播、协作 stop），不再承担终态 winner 职责。
- **R3 CancelledBeforeStart 是否只来自 Scheduler 真实 cancel retirement？** 是。运行时内默认原因 `CancelledBeforeStart` 的 `retire` 仅来自 `ResourceWaitBinding::cancel()`（由 `ManagedInvocation::cancel()` 与其 stop 回调调用，即真实 owner/stop 取消入口）。`ManagedInvocation::create` 异常清理路径原先的默认原因 retire 已改为显式 `ContractsErrc::Rejected`，不再制造取消终态。
- **R4 ExpiredBeforeDispatch 是否始终产生 FailedBeforeApply/NotReached？** 是。StateEdit pre-start 收到 `ExpiredBeforeDispatch` 时 `cancelled=false` → `complete_before_start(reason,false)` → `FailedBeforeApply`/`NotReached`/`no_application_proven`；已开始 State 保持既有 Action deadline / consume_claimed / CommitClaim 仲裁（F6-09 回归保留）。
- **R5 late cancel 是否无法改写已经确定的 expiry terminal cause？** 是。Scheduler 在自身 mutex 下形成 `ExpiredBeforeDispatch` 终态后，迟到的 `cancel()` 只设置 `cancel_requested_`（意图）并得到 `AlreadyTerminal/AlreadyClaimed`；`completed()` 读取的是 Scheduler 传入的终态原因，结果不可被重解释（F6-04 反例）。

## 槽生命周期与关闭

- 槽复用：reserve、安装消费、slot 退休、Reservation 失败清理四处均重置 `deferred=None`；F6-06 反例证明过期槽复用后正常 `StateCommitted`。
- `close()` 在服务锁内收集已安装 owner 并对 `reserved&&未安装` 槽记录 `DeferredControl::Cancel`，锁外逐个 cancel，再 `scheduler->close()`；未在服务锁下调用 Scheduler/业务回调。F6-07 以独立第二 Host 验证 running+waiting-resource 下 close 排空、`shutdown_until` quiescent、无死锁。
- 服务 deadline 循环、`close()`、`submit()` 三处均不产生 timer 双 erase、owner 双 retire 或 slot 遗留状态；ASan 由正式矩阵承担。

## SPEC review

- 未修改 `packages/contracts/**`、`packages/state/**`、`packages/runtime/include/ock/runtime/detail/invocation.hpp`、`packages/runtime/policy/**`、`packages/runtime/scheduler/**`、`packages/runtime/resources/**`（git diff 912cb75→bb7ca66 仅上述 3 文件）。A05 Outcome fact model、A09 Accepted identity、State provider 合同、CoreContracts ABI、Atomic 语义、Policy 语义不变。
- `DeferredControl` 是 `ExecutionService` 私有内部枚举，不进公开头、SDK、wire 或持久化；公共摘要门禁预期不变（T24/T01/T24.contracts 由 31 项矩阵验证）。
- Scheduler 无任何修改；本轮只是 Runtime 私有线性化事实传递纠偏，无需新 ADR。

## 结论

规格与代码 Approved。机器验收（31×3 正式矩阵、SDK/install、Runtime-only/Embedded 投影、Native 7/Embedded 3 footprint、Release startup）独立执行并另行绑定本来源；全部门禁通过前 B7 保持 HOLD。

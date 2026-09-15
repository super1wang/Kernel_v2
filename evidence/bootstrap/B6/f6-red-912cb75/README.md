# F6 Red 原始失败记录（912cb75 生产行为）

- 运行来源：HEAD `d79ba62`（packages/ sdk/ cmake/ 与 `912cb7554cd4c78ea9bf6db5615a2875d215e5ed` 完全一致，仅新增本批文档与本 F6 测试）。
- 构建与运行：`build/b6-formal-debug`（win-msvc-debug 预设，B6Subset），目标 `ock_state_runtime`，直接运行 `tests/unit/state_roots/Debug/ock_state_runtime.exe`。
- 新增反例在未修复生产代码上 8/8 次运行全部失败（本目录保留一次正式原文，另 7 次重复输出一致）：
  - F6-01（单个已过期 deadline 提交）在旧代码上偶然通过：冷启动控制线程下 install 先于服务 deadline 触发，Scheduler 自身 sweep 兜底。该偶然正确不可靠，不作为合同证明。
  - F6-02（Accepted→安装前 expiry 压力探针，8 波 × 32 个 `deadline=now-1ms` State 提交）稳定失败：`exact_failure(...,true,false)` 期望 `FailedBeforeApply`，实际得到 `CancelledBeforeApply`（F6-A：安装路径把已观察到的 expiry 事实重解释为 cancel，赢得 Scheduler retire 后投影 CancelWon）。
- 失败原文：`1-stderr.log`（`std::cerr<<e.what()` 输出），`exit.txt` 为退出码 1，`1-stdout.log` 为空。
- 本记录为开发失败原文，不覆盖、不拼接任何 912cb75 正式 evidence；修复后的同来源绿测与正式矩阵另行归档。

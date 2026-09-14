# B6 closure 集中 SPEC/CODE 技术复核

本材料复核 2026-09-12 审计 C1–C6，范围 D4.01–D4.04；不授予 B7/G4 或产品放行。审核者为 Codex AI，使用仓库自动验收政策。机器事实以冻结来源后的新报告为准，不沿用旧 62 项报告代替新测试。

## SPEC 结论

采纳 ADR-b6-closure-ownership：Started 与 ClaimWon 分开；每次发布必须拥有独立 proof，Proof 不因后来提交或生命周期重开失效；准备帧精确保活。有限 Atomic 以冻结 Catalog 中的真实注册项组装，使用同一 StateEdit 候选和 Runtime Policy/Resources/Executor。可信注册方选择 revision policy。History 为共享 before/after root 表示，单次 Undo/Redo；索引实际分配、逻辑根权重、History retained/ring 分列。此差额已同步唯一规范与 commit 合同。上述解释满足 B6 范围，无 Plan IR。

## CODE 结论

- StateDomain 的 phase 仅标记回调在途，PreparedState.CommitClaim 是唯一仲裁；真实 Policy 在自身锁内重验权威并 consume_claimed，close 对同一 claim 取消。claim 后错误不可伪报回滚。
- CommitReport 携带 owning publication_proof；Runtime 对完整 identity 校验并用本次 proof 封口。最新诊断缓存不再承担历史回报权威。
- Snapshot 在同一域锁捕获 PublishedState 与 lifecycle identity。prepared 使用 weak reservation/Frame owner；失败与显式放弃只解除匹配项，外部回调前持有 self，退休根锁外析构。
- CandidateBindings 从冻结目录获取真实 handler、Args/R 类型、provider、资源；Input 检查同目录/同 provider/同域和完整成员。拒绝嵌套、异步及错误模式。实际参数投影在调用前重验，组内每步核对当前 Policy；同一 Scheduler 取得完整排他资源，单次提交。
- History 在 ring 未满前按字节压力回收未 pin 项；HistoryCharge 跟随最后 owner，重开不抹去保活字节。immer 私有 allocator 记录真实 index 节点和峰值，限额计算先检查后乘减；不声称逻辑字节等于全部进程内存。
- Host 默认不开 State，显式 enable_state 后由公开 HostBound invoke/submit 进入。必要记录失败保留 StateCommitted/facts，并等待 receiver 排空后 Terminal。SDK dev.8 反映公开合同扩展。

技术结论：上述实现可进入正式验证。SPEC/CODE Approved 记录绑定精确来源后生成；包级保持 InProgress，直到三配置 89 项、安装投影与原预算 footprint 独立通过并核验 DAG。

## 审计映射

- R01：Started 后取消仍在真实 Policy claim 前拒绝；`T06.state.runtime_native`。
- R02：close/cancel/revoke/deadline 各自 claim 两侧；`T06.state.runtime_native`。
- R03：C1 report 屏障、C2 发布封口、C1 再封口；`T06.state.runtime_native`。
- R04：完整 PreparedIdentity 与旧回报/证明；`T06.contracts.atomic_prepared_identity T10.state.atomic_group`。
- R05：并发 close/reopen/snapshot 捕获同版身份；`T09.state.memory_commit T08.state.snapshot_ownership`。
- R06：帧丢弃、精确 abandon、proof bad_alloc 后重试；`T09.state.memory_commit T10.state.atomic_group`。
- R07：回调释放最后 owner、锁外释放；`T08.state.snapshot_ownership`。
- R08：真实目录 handler、TypeContract 输入、伪 digest；`T10.state.atomic_group T06.state.runtime_native`。
- R09：实际 target 投影不匹配、后成员权限和每步当前权威；`T06.state.runtime_native`。
- R10：真实 Host 非空排他资源、第二执行等待后冲突；`T06.state.runtime_native`。
- R11：Native/Host group、值绑定、标量断言、128/129、拒绝嵌套；`T06.state.runtime_native T10.state.atomic_group`。
- R12：未满 ring 的字节压力、全 pin 拒绝和恢复；`T09.state.memory_commit`。
- R13：实际 index 分配台账、共享 root、retained History、溢出；`T09.state.object_references T09.state.memory_commit T23.state.structural_sharing_cost`。
- R14：公开 revision policy：缺失/过期在业务前拒绝；`T06.state.runtime_native`。
- R15：迁移安装后的公开 Host+CpuPool StateEdit/Atomic；`T24.state.installed_consumer T24.state.settings_service`。
- R16：Published 后输出/必要记录失败及 Finalizing 排空；`T06.state.runtime_native T03.native.host_observation_drain T03.native.host_required_record_drain`。
- R17：18 个 N×k×引用组合，真实 snapshot/prepare/commit/reclaim；`T23.state.structural_sharing_cost`。
- R18：受影响 B5/Host/SDK；投影和原预算 footprint 独立运行；`T20.cli.managed_wire T24.sdk.install_and_missing_components T24.state.installed_consumer`。

原 B6 计划第 9 节 23 行逐行映射见 `tests/manifests/b6-closure-requirement-map.json`。复合测试内子场景不是新的 CTest 数量。StateNative/Runtime-only 和 Native/Embedded footprint 单独采集；正式失败原文保留，禁止拼接来源。

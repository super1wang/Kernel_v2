# B6 State / Commit / Atomic 收口交付

日期：2026-09-12。范围仅为内核 B6（D4.01–D4.04）及规划中的验证消费者；未扩展 GUI、CAD、CAM、设备、PlanCompiler、Durable 或产品数据。

## 结论

B6 已完成并通过自动验收。最终实现提交为 `b6203fde6ce0438b05136cd6f5b295e6a57dd587`，634 项受审输入摘要为 `ab332d1b0e163579537611c83f2f65a1c7e99b6eeb1c67b33ddcf25d86d77992`。三配置使用同一提交、同一输入集合和同一 62 项 expected；最终[自动验收决定](../../evidence/B6/final-b6203fd/acceptance.json)为 `Passed`，`errors` 与 `review_errors` 均为空。

按 [B6 requirement map](../../tests/manifests/b6-requirement-map.json) 的独立映射，四个包结论如下：

| 工作包 | 已交付合同 | 结论 |
|---|---|---|
| D4.01 | owning `PublishedState`、不可变 Snapshot、授权/pin 寿命、结构共享根与计量 | Passed |
| D4.02 | 候选 `ObjectEdit`、WriteSet、约束、反向引用、有界 History 与 Undo/Redo | Passed |
| D4.03 | 单在途 reservation、permit 一次消费、发布 gate、CommitId/revision 证明、关闭/重开代际 | Passed |
| D4.04 | CandidateRead/PureCompute/StateEdit 同源绑定、有限 Atomic 组、禁止异步和跨域形态 | Passed |

发布后的状态事实不会被后续序列化、回执或证明错误改写为 `FailedBeforeApply`。Runtime 桥接在发布前准备业务结果与 KnownFacts，发布后保留 `StateCommitted`；内部证明不一致 fail-fast。

## 正式矩阵

| 配置 | 结果 | 完整审计 | 报告 SHA-256 |
|---|---:|---:|---|
| Debug | 62/62 Passed | 0 error，12 个运行时子产物 | [`3042b4c4…`](../../evidence/b6203fde6ce0-ab332d1b0e16/win-msvc-debug/D4.04/20260912T160716Z-ec2742a9a2a1/report.json) |
| Release | 62/62 Passed | 0 error，12 个运行时子产物 | [`76c026a0…`](../../evidence/b6203fde6ce0-ab332d1b0e16/win-msvc-release/D4.04/20260912T161021Z-3f7e5a817b3e/report.json) |
| ASan | 62/62 Passed | 0 error，12 个运行时子产物 | [`d3ee27f2…`](../../evidence/b6203fde6ce0-ab332d1b0e16/win-msvc-asan/D4.04/20260912T161326Z-04749bbcd365/report.json) |

SPEC/CODE 使用批次集中复核，但 requirement map 对 D4.01–D4.04 保留独立完成条件。State 的关键反例覆盖关闭与撤权后的旧快照寿命、重入关闭、并发读/提交无撕裂、伪造同 revision CommitId、重开后旧证明失效、许可/授权异常恢复、失败零发布、History/pin/字节上限以及 Atomic 禁止形态。

## Profile、裁剪与成本

- `StateNative` Debug 仅取得 `expected` 与私有 `immer`，9/9 State 测试及迁移安装消费者通过；安装消费者结果见 [state-install-2df0488a61](../../evidence/bootstrap/B6/state-install-2df0488a61/result.json)。
- `B6Subset` 的 Debug/Release/ASan 迁移安装消费者均包含在正式矩阵的 12 个子产物中；State 导出仅公开依赖 CoreContracts，安装公共头不泄漏 immer。
- Runtime-only 最终复核仅取得 `expected`，安装组件为 CoreContracts/Foundation/Runtime，真实迁移消费者运行通过；见 [native-pruned-7b4612beea](../../evidence/bootstrap/B2/native-pruned-7b4612beea/result.json)。Embedded 三配置目标图均不含 State/immer。
- State 结构共享直接样本为 4096 个对象、100000 次 Snapshot 复制、根拥有计量 1310760 字节，`shared_body=true`；正式矩阵中的 `T23.state.structural_sharing_cost` 三配置均通过。该样本用于本批实现核对，不建立新的发布性能门槛。

受 Runtime/CoreContracts 输入影响的 Embedded footprint 已按运行前批准且未放宽的预算重新采样：Debug、Release、ASan 各 6 组 ABBA 均 `Passed`，方法摘要与预算绑定一致，结构线程上界均为 3。Debug/ASan 的 40 次短 Invoke 零新增分配窗口成立；Release 构造到 Ready 最大 0.872 ms，进程创建到 Ready 最大 41.2223 ms。

- [Embedded Debug 报告](../../evidence/G3/C/02ed9b5d8e744b8b9a85edc72e87778b/report.json)（SHA-256 `f98833b4…`）
- [Embedded Release 报告](../../evidence/G3/C/10dad1bff8f143148f2ed0c5e30cf8f0/report.json)（SHA-256 `3f63d230…`）
- [Embedded ASan 报告](../../evidence/G3/C/4cb0a5f7bd004e44af9c3bc416356111/report.json)（SHA-256 `acbef0a4…`）

## 保留的失败事实

首次 ASan 正式轮在提交 `b8d7322` 上失败，原始[报告](../../evidence/b8d732292f1c-3a9b3bae1b26/win-msvc-asan/D4.04/20260912T154341Z-23429f93ee32/report.json)保持 `Failed`，未覆盖或拼接。直接 State 程序因 CTest 未注入锁定 MSVC ASan 运行目录而以 `0xc0000135` 退出；安装消费者未同步启用 ASan，链接时报 `annotate_string`/`annotate_vector` 的 `LNK2038` 不一致。提交 `4f0bf9e` 为直接程序设置运行目录，并使安装消费者继承生产者 ASan 模式；提交 `b6203fd` 又修正独立 Runtime 消费者残留的旧 SDK 身份。最终三配置全部从 `b6203fd` 重跑，未复用中间绿色报告。

## 边界

B6 Passed 不等于 G4 Passed。G4 仍为 `NotStarted`，D4.05–D4.08 的 PlanCompiler、Runner、控制流与 Shell Plan 属于 B7；Durable、Project/Document、GUI/CAD/CAM、设备和物理硬件验收均不在本批范围。

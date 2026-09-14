# B6 最终精确化交付

当前最终放行决定由 [v2 最终交付](B6-final-semantic-precision-delivery.md)取代；本文件及旧来源机器事实作为历史保留。v2 已完成新来源验证，当前 B7 准入 GO。

日期：2026-09-14。生产来源 `3abde2ebc1f11acccbad106142c96760a95f5d47`，范围仅为 B6 post-closure P2-A/P2-B；历史 D4.01–D4.04、C1–C6 和其 89/89 原始报告保持不变。

## 门禁更正（2026-09-14）

公共 CommitReport 布局变化触发 footprint 刷新，原交付遗漏该项；已先以 `55bf336` 更正为正式验证 Pending、B7 HOLD。现已完成原预算测量并提交机器证据 `f9426ee327a004c7242f3c3e6827cf549f1b3c60`，恢复 B6 final precision Passed / Frozen，解除 B7 准入 HOLD。生产代码及原功能验收不变。

## 实现结论

- Atomic `register_group()` fail-closed：只有 `RequireExplicitRevision` 可以注册；普通单条 StateEdit 仍可由可信注册者选择既有 revision policy。
- `CommitClaim` 将取消胜出写入同次 `CommitReport.cancelled_before_claim`。Runtime 不根据迟到 stop 推断，而是据该事实产生 `CancelledBeforeApply / CancelWon`。
- `business_entered` 移至实际 handler 调用前。revision 或 lifecycle 前置失败保持 `FailedBeforeApply`，并准确记录业务未进入；已取得 ActionPermit 的 Native 前置失败记录执行已进入仲裁。
- SDK 公共头摘要已同步；未引入 Runtime→State 依赖、State/immer 安装边界或新的执行框架。

## 机器验证

| 配置 | 固定直接集 | 结果 |
|---|---|---|
| Debug | State runtime、Atomic、State commit/snapshot、Outcome、Policy cancel | 7/7 Passed |
| Release | State runtime precision 回归 | 1/1 Passed |
| ASan Debug | State runtime precision 回归 | 1/1 Passed |
| Debug 安装 | State Host managed consumer | Passed |
| Debug SDK | public headers、component closure、public include boundary、version | Passed |

State runtime 新增反例覆盖 ServerCapture 组注册拒绝、Native 缺失 expected_state、revision/lifecycle 过期时业务 0 次进入、Native 和 Host 的 CancelWon、claim 后状态不回退，以及同 base 的组冲突。

安装消费者曾由机器默认 CMake 3.29 拒绝，因为仓库锁定的 preset 需要 CMake 3.31；在独立的 CMake 3.31.6 下，B6 State 安装消费者通过。该环境拒绝原文未作为产品失败重写。

本交付不声称 B7/G4、持久化或产品验收。

## 最终 footprint 门禁补齐

- Native：Debug / Release / ASan 的 occupancy、allocation，加 Release latency，共 7/7 Passed；每项 6 组 ABBA / 24 个主样本，新增 Native 线程为 0。
- Embedded：Debug、Release、ASan 共 3/3 Passed；每项 6 组 ABBA / 24 个主样本，Release startup 另有 6 组 / 24 个样本。Debug / ASan 各 480 个完整 Invoke 零新增分配窗口通过，内核线程结构上界仍为 3。
- 六个生产投影重新配置/构建：Runtime-only 只获取 expected，Embedded 只获取 expected / thread_pool；目标图没有 State，未获取 immer。
- 使用锁定 CMake 3.31.6-msvc6、MSVC 工具链及 Python 3.11.9；190 个测量输入逐字节匹配 `3abde2ebc1f11acccbad106142c96760a95f5d47`。验证 checkout 是 `55bf336`，仅含后续文档/门禁绑定，不把它冒充新的生产来源。
- 11 项预算均沿用原数值、pilot 与采样方法，只刷新来源摘要与审批绑定。没有正式采样失败或重跑；汇总脚本首次误判预期失败注入已单独记录并纠正，未修改正式原始证据。

[机器 acceptance](../../evidence/B6/precision-3abde2e-footprint/acceptance.json)绑定报告、commands、samples、artifacts、来源与裁剪证据；[汇总校验说明](../../evidence/B6/precision-3abde2e-footprint/verification-notes.md)保留校验过程。

该结果完成最终规划触发的遗漏验证，B6 为 Full GO。仅解除 B7 准入；B7 未实现，G4 NotStarted，不增加 CI required checks 或产品/设备放行声明。

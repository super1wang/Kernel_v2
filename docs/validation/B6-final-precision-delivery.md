# B6 最终精确化交付

日期：2026-09-14。生产来源 `3abde2ebc1f11acccbad106142c96760a95f5d47`，范围仅为 B6 post-closure P2-A/P2-B；历史 D4.01–D4.04、C1–C6 和其 89/89 原始报告保持不变。

## 门禁更正（2026-09-14）

代码结论保留。公共 CommitReport 布局变化要求刷新 footprint，原交付未完成该项，故正式验证 Pending、B7 HOLD。原下列直接验证不能代替 Native/Embedded 原预算测量；新增证据通过后再解除 HOLD。

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

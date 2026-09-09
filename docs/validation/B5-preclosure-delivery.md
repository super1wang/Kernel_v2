# B5 前置收口交付

日期：2026-09-09。结论：C1/C2 修复与 C3 规划收口完成，自动验收 Passed，**B5 Ready for Development**。这不是工作包新增状态：D3.04–D3.07 仍 NotStarted，G3 未开始。被测实现 `7a22d0e15546b78cf96f0257c72232cc46bfd398`，输入摘要 `2532a291a422e802141922473d00749ed0a8ee1061a1fbb225ec6497c1df8655`。

## 建议取舍与实现

用户附件为 `B5_PreClosure_Recommendations_2026-09-09.md`；按当前源码与唯一规范 v3.3-r2 核实，不将附件的建议状态作为既成验收事实。

| 建议 | 分析与本次结果 |
|---|---|
| C1 / P1 重入生命周期 | 成立并修复。Lease 在 wake 前清空持有并局部保活 State；Waiter 在 cancel 前将 generation 归零并局部保活 State/active。回调或 capture 析构删除当前 handle、manager、其他 owner 后不再访问 this。异常记账与剩余 pending 收尾仍有 State 存活。 |
| C2 / P2 ResourceKey | 成立并修复。slot、alias、alias target、claim 统一 1..96 ASCII 字节；空串/97+/非 ASCII 为 InvalidInput，合法未知为 UnknownKey，不创建 slot。ASCII 按字节 0..127，不擅加可打印限制。 |
| C3 / B5 接线 | 采纳并完成 SPEC。[B5 计划](../plans/B5.md)明确先发布稳定 ticket，预建 binding 承接早到 terminal，Installing 缓存早到 wake、双 generation 仲裁、唯一重试驱动、在途 acquire 终止、make_ready 失败释放及 Started 后协作取消。底层集成示例改为先发布 ticket；完整并发 ResourceWaitBinding 尚待 B5 实现与验证。 |
| controlled drain_until | 暂缓，B5 确定性测试实际需要时处理，不是本次前置。 |
| IPC send / Router 编解码 / Shared IOCP | 暂缓，G3 成本与 footprint/并发证据确认收益后再决定。 |
| SharedTypeContract 去重 | 暂缓，须先证明 parity，保留现有验证。 |

本次没有主架构变更，不新增 ADR；没有公开头/ABI/SDK manifest 或 Runtime 依赖变更，SDK 仍 dev.5/B4Subset。修改仅生产 Resources 与直接合同测试，未扩展证据工具或产品模块。

## 正式验证

[执行前计划与固定范围](../plans/B5.md)、[AI SPEC/CODE 集中复核](../reviews/B5-preclosure-review.md)均已提交。expected 在正式执行前固定；同一来源分别执行以下影响集，不重跑 B4 65/63/65 或 B1–B3/G1/G2 全历史矩阵。

| 配置 | 结果 | 原始报告 |
|---|---|---|
| Debug | 16/16 Passed | [E03 report](../../evidence/7a22d0e15546-2532a291a422/win-msvc-debug/D3.03/20260909T081151Z-2e2afe6dc203/report.json) |
| Release | 16/16 Passed | [E03 report](../../evidence/7a22d0e15546-2532a291a422/win-msvc-release/D3.03/20260909T081216Z-1c2a914e90cb/report.json) |
| ASan | 18/18 Passed | [E03 report](../../evidence/7a22d0e15546-2532a291a422/win-msvc-asan/D3.03/20260909T081356Z-b8ef3371cb0a/report.json) |

共同集为 resources 15 项（含两项重入销毁、key 边界、既有 waiter/release/cancel/多资源/阶段及 scheduler-resource 集成）加依赖 DAG 1 项；ASan 另有 healthy 和真实 heap overflow 正控制。Release 用于优化构建下的重入寿命验证。

ASan 重入用例正常完成；真实溢出正控制子进程 exit=1，stderr 3511 字节含 `AddressSanitizer: heap-buffer-overflow`，已从 [runtime-artifacts.zip](../../evidence/7a22d0e15546-2532a291a422/win-msvc-asan/D3.03/20260909T081356Z-b8ef3371cb0a/runtime-artifacts.zip)核对完整日志与 SHA256。该预期失败证明检测器有效，不是本次资源实现失败。

[自动验收决策](../../evidence/B5/preclosure-7a22d0e-acceptance.json)为 Passed，errors/review_errors 均为空。矩阵复用 D3.03 标识，仅代表本次受影响资源合同收口，不替换历史 B4 决策；未写 human Approved。报告 dirty=true 来自新增未归档证据，自动验收逐一核对实际输入与被测提交的 Git 字节，全部一致。

报告目录保留源码/构建快照、expected/discovered、JUnit 与原始命令日志。开发 Debug resources 15/15 及正式执行包装日志保存于 `evidence/bootstrap/B2/resume-b5-*`；本次正式运行无失败、跳过或证据拼接。

G0–G2、B1–B4 的历史 Passed 保留；B5 规划覆盖 D3.04–D3.07 的连续实现、正式前置与 G3-A/B/C 完成条件。当前只放行开发，真实任务、协作取消、结构化寿命、多进程观察和完整 Embedded 占用仍待 B5，未宣称 G3 或设备验收完成。

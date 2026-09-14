# B6 最终语义精确化 v2 交付

日期：2026-09-14。范围仅 B6 F1–F5 修复与重新收口。v2 supersedes v1 final-precision release decision；历史 B6 / C1–C6 Passed 及旧来源机器证据完整保留。

| 项 | 最终结果 |
|---|---|
| 1. FINAL_CODE_SHA | `912cb7554cd4c78ea9bf6db5615a2875d215e5ed`；源树 `0b891dce0302c95c99560faab7aef934a615ce8b` |
| 2. 实际修改文件 | `packages/state/include/ock/state/domain.hpp`、`packages/runtime/include/ock/runtime/detail/invocation.hpp`、`packages/runtime/executions/managed_execution.hpp`、`packages/runtime/executions/managed_control.hpp`、`packages/runtime/executions/execution_service.hpp`、`tests/unit/state_roots/runtime_state.cpp`、`sdk/sdk_api_manifest.json`；其余为计划、审核、门禁与证据 |
| 3. F1 | abandon 只释放精确 reservation，不制造 claim cancellation；保留 close / Action 的真实取消仲裁 |
| 4. F2 | Native 业务前普通失败为 Rejected；execution_accepted 只来自 managed，不再由未进入 handler 伪造 |
| 5. F3 | State dispatch 局部捕获异常，依据 call.business_entered 区分 provider/membership 前置异常和真正 handler 异常 |
| 6. F4 | C0 记录 owning accepted execution 的取消事件；C1 向 Action cancel 确认；C2 只认 CommitReport 仲裁；late stop 不覆盖 Published |
| 7. F5 | State 执行层已 Accepted 后普通失败保持 Completed/FailedBeforeApply，普通 expiry 不再由存储兜底错误码判断为 CancelWon；Host deadline 走 State 私有 expire 路由，未启动完成仅接受 owning cancel 实际请求，已进入调用仍由 Action deadline 阻止发布 |
| 8. 新增反例 | 普通 abandon、无效 binding 清理、真实 cancel/revoke/expiry；Native/Managed stale revision/lifecycle；membership bad_alloc/失败、provider begin 两类异常、handler 异常；Native/Host Atomic 中途取消、普通断言、revoke/expiry、发布后 late stop；Accepted 前 Action 取消、Host 资源等待取消及 missing expected_state；新增 Host 资源等待过期与 handler 已进入后过期，真实时钟与 Scheduler 一致，两者均 FailedBeforeApply、无 revision 增长 |
| 9. Debug | 正式 31/31 Passed |
| 10. Release | 正式 31/31 Passed |
| 11. ASan Debug | 正式 31/31 Passed，本影响集未报告 ASan 错误 |
| 12. installation / SDK | 三配置 State installed consumer、public headers、component closure、public include boundary、SDK version 全部 Passed；摘要由既有 sha_file 计算并经架构检查验证 |
| 13. Runtime-only / Embedded | 六个投影重建与独立安装消费者验证通过；Runtime-only 仅 expected，Embedded 仅 expected + thread_pool；无 State/immer |
| 14. Native footprint | 7/7 Passed，各 6 组 ABBA / 24 主样本，新增 Native 线程 0，沿用原绝对与 paired 数值预算 |
| 15. Embedded footprint | 3/3 Passed，各 6 组 / 24 主样本；Release startup 另 6 组 / 24 样本；Debug / ASan 各 480 个完整 Invoke 零新增分配窗口，结构线程上界 3 |
| 16. SPEC review | AI Approved，三配置分别绑定共同输入摘要；[复核说明](../reviews/B6-semantic-v2-wait-review.md) |
| 17. CODE review | AI Approved，逐项核对 F1–F5 和生命周期；无 human Approved，无 CoreContracts 协议扩展 |
| 18. acceptance | [机器最终决定](../../evidence/B6/semantic-precision-912cb75/acceptance.json)，绑定正式自动验收与 footprint 完整性 |
| 19. 残余 blocker | 当前 B6 semantic v2 范围无阻断项 |
| 20. 最终放行 | B6 semantic precision v2 = Passed / Frozen；B7 entry GO，仅准入，B7 未实现，G4 NotStarted |

机器证据先提交为 `c03daa1a8a3e0a9a4bf8f9e6781089f08a37da70`，之后才更新最终状态。生产提交为 F1 `4086695` 、F2–F5 第一候选 `32c38ad` 与 Host expiry 最终修复 `912cb75`。不回退或重写历史。

正式矩阵包含 648 个源码/规范/方法输入，共同摘要 `7b939f2a758fbdee63654ed6c95ec2e896e56d8a3523c3e1a42b6c7978648014`；footprint 的 190 个方法输入另外逐字节绑定同一生产提交。锁定 CMake 3.31.6-msvc6、MSVC 工具链、Python 3.11.9。11 项 footprint 预算仅刷新来源审批，不改数值、pilot 或算法。

## 原始失败与最终同源归档

旧 32c38ad 候选的首轮审核摘要缺失、第二轮机器 Passed 及 footprint 均作为历史事实保留；随后固定资源等待反例证明仍遗漏 State Host expiry。未回退历史，新增 912cb75 修正该边界。开发首次局部修复和测试假时钟错误也保留，最终 Host 使用与 Scheduler 一致的 steady_clock。

912cb75 首轮 Debug 为 30/31：SDK public_headers 在重命名隔离安装目录时发生 Windows WinError 5，属于真实失败报告，未覆盖。使用新隔离目录重跑相同冻结输入和原 31 项名单，最终 Debug/Release/ASan 均通过，见 [最终正式验收](../../evidence/B6/semantic-precision-912cb75/formal-acceptance-02.json)。机器错误与 AI 审核错误均为 0，Native/Embedded 全部重新按原预算采样；未拼接旧候选结果。

原 InvocationRecord::store 已有 Accepted 防回退兜底；本次修正 State 执行层、未启动完成和 Host deadline 取消误分类。Read 原行为保留，真实 cancel 继续走原 owner 请求，CoreContracts、Outcome validator、PublicationProof 架构未重写。私有 expire 方法用于区分 deadline 与取消，不新增公共协议。

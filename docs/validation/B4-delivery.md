# B4 交付：Executor、Scheduler 与 Resources

日期：2026-09-09。范围 D3.01–D3.03，仅内核与规划验证消费者。

**D3.01、D3.02、D3.03 已按 DAG 顺序 Passed。** 被测且已提交的实现为 `0d71b4fa7999ce239f6948a3a5ab2badaa5ab2df`（`B4: implement executor scheduling and resource leases`），三配置共享输入摘要 `93882f1f740c3e035f6e8f5a1afa53dd8c59e91cac7863231492e9233e19265d`。此文与原始证据随后作为独立归档提交；最终提交/远端状态以交付消息中的 Git 核对为准。

## 用户追加规划建议

三处均需要明确，已落实到 [B4 规划](../plans/B4.md)、[ADR](../adr/ADR-b4-executor-resources.md)及 A09 细化。

| 建议 | 最终边界与验证 |
|---|---|
| Started 的唯一判据 | envelope 以当前 attempt_generation 在 Scheduler 仲裁内取得 start claim；与 deadline 同锁竞争。submit 接受、dequeue、execute 入口均不足以表示 Started。已开始/未开始的有序案例及 100 次 start/deadline 竞争验证一次完成和过期无业务副作用。 |
| Dependency set 冻结 | entry 发布时拥有并冻结依赖，仅引用已发布 predecessor；self/duplicate/unknown 拒绝。attach/completion 同锁，已完成者不计 unresolved，未完成先挂 adjacency 再计数，一次 drain 防下溢/丢 wake；另有 100 次真实线程竞争。 |
| reservation 绑定 envelope | 采用此表达，并限定为**合规** Rejected/exception 返回已释放 envelope。拒绝或异常后仍持有 envelope 的违约后端仅触发逻辑拒绝/attempt 失效，物理额度仍保留到真实收尾；inline 已完成后再拒绝/抛错不回滚、不重复通知或释放。 |

这些是已经实现并验证的 B4 底层规则；不将内部完成事实包装为 B5 的 TaskOutcome 或 Execution Terminal。

## 实现与安装交付

- **D3.01**：生产 BS 5.0.0 CpuPool、可控/合法 inline 工厂、同版本 ExecutorConformance。七个共同必需项在三个后端全部执行；真实并行、停止超时、重复调用及 worker 非法析构分别验证。资格清单摘要编入测试程序并与实际二进制核对；NotApplicable 不计 Passed，故障后端不登记合格。
- **D3.02**：有限权重/优先级 Ready 队列，主体与全局排队/在途配额、独立物理投递 reservation、依赖邻接与 deadline 索引。终态待通知 entry 也有容量上限；业务 captures 锁外释放，history 不参与 pump 扫描。Scheduler 弱引用 Executor，由独立控制 owner 保持至排空。
- **D3.03**：冻结资源拓扑/别名，同槽 Shared/Exclusive/units、聚合溢出检查、MultiClaim 全获或零占用、拥有型 Lease、按资源索引去重的锁外 waiter 唤醒/取消。计算与提交资源区分；child 必需资源未释放则拒绝等待组合。首版 resource_factory=absent，未知键拒绝且不无限建槽。
- SDK 为 **0.1.0-dev.5 / B4Subset**。新增四个公开头单独安装编译，真实迁移消费者调用 Executor/Scheduler/Resources；CpuPool 仅导出 CoreContracts 闭包，BS 私有。Runtime-only 实际构建/安装/Native 消费只取得 expected；B3Subset 配置投影保留 CpuPool 合同占位，错误 B4 acquisition 在取得依赖前拒绝。新增表面仍为 experimental，消费者按 dev.5 重编译，见 [SDK 合同](../contracts/native-sdk-surface.md)。

## 正式机器事实与独立验收

预期集合在正式发现前冻结于 [b4.expected.json](../../tests/manifests/b4.expected.json)。每配置物理执行一次，三包共享报告并按独立技术结论验收。Debug 65/65、Release 63/63、ASan 65/65；全部用例 Passed，无 skip，三份报告 errors 均为空。

- win-msvc-debug：[65/65 report](../../evidence/0d71b4fa7999-93882f1f740c/win-msvc-debug/D3.03/20260909T060601Z-ccf6d4598e54/report.json)、[JUnit](../../evidence/0d71b4fa7999-93882f1f740c/win-msvc-debug/D3.03/20260909T060601Z-ccf6d4598e54/round-001-junit.xml)、[完整运行子产物](../../evidence/0d71b4fa7999-93882f1f740c/win-msvc-debug/D3.03/20260909T060601Z-ccf6d4598e54/runtime-artifacts.zip)。
- win-msvc-release：[63/63 report](../../evidence/0d71b4fa7999-93882f1f740c/win-msvc-release/D3.03/20260909T060914Z-7f9974366e18/report.json)、[JUnit](../../evidence/0d71b4fa7999-93882f1f740c/win-msvc-release/D3.03/20260909T060914Z-7f9974366e18/round-001-junit.xml)、[完整运行子产物](../../evidence/0d71b4fa7999-93882f1f740c/win-msvc-release/D3.03/20260909T060914Z-7f9974366e18/runtime-artifacts.zip)。
- win-msvc-asan：[65/65 report](../../evidence/0d71b4fa7999-93882f1f740c/win-msvc-asan/D3.03/20260909T061531Z-948964e30707/report.json)、[JUnit](../../evidence/0d71b4fa7999-93882f1f740c/win-msvc-asan/D3.03/20260909T061531Z-948964e30707/round-001-junit.xml)、[完整运行子产物](../../evidence/0d71b4fa7999-93882f1f740c/win-msvc-asan/D3.03/20260909T061531Z-948964e30707/runtime-artifacts.zip)。

ASan 既有健康探针，也有真实 heap-buffer-overflow 正控制。正控制子进程实际退出码为 1，完整 stderr 为 3509 字节，连同 process.json、SHA 和完整日志保存在 ASan runtime-artifacts.zip；JUnit 的 1024 字节摘要不替代该原始诊断。附加核对曾错误要求摘要尾部标记，失败日志保留于 [初次核对](../../evidence/bootstrap/B2/resume-b4-acceptance-eddaa6e5e3/)，随后读取同次已归档完整原文通过，未改写或重跑正式结果。

| 包 | 自动验收 | 前置 |
|---|---|---|
| D3.01 | [Passed](../../evidence/B4/d301-0d71b4f-acceptance.json)，errors/review_errors 均空 | D1.02、D0.06 既有 Passed |
| D3.02 | [Passed](../../evidence/B4/d302-0d71b4f-acceptance.json)，errors/review_errors 均空 | D3.01 已先 Passed；D1.03 既有 Passed |
| D3.03 | [Passed](../../evidence/B4/d303-0d71b4f-acceptance.json)，errors/review_errors 均空 | D3.02 已先 Passed；D1.04 既有 Passed |

[集中 SPEC/CODE](../reviews/B4-review.md)为 AI 技术审核，不伪造 human Approved。自动验收额外逐项核对被测提交的 Git 字节、三配置相同身份及审核绑定。历史 G0–G2/D0–D2 Passed 保持原证据，不重验 G1/B2 累计矩阵。

## 公平与历史规模原始样本

三个配置均保存完整 120 次主体调度顺序：主体权重 1:3，对应 30/90 次机会，主体 1 的最大间隔为 3；低优先级在主体内部也有有界份额。此断言只针对持续 Runnable 且有并发额度者。

下表为每组 **1000 次空 pump 的总耗时（毫秒）**；同组主体检查次数均为 2000。它证明所测 history 范围未增加线性调度工作量，不代表完整 Embedded 或机器实时性预算验收。

| history 条目 | Debug ms | Release ms | ASan ms | 主体检查次数 |
|---:|---:|---:|---:|---:|
| 0 | 0.7073 | 0.1448 | 2.8237 | 2000 |
| 100 | 0.6971 | 0.1425 | 2.7391 | 2000 |
| 1000 | 0.7074 | 0.1417 | 2.6732 | 2000 |
| 10000 | 0.6992 | 0.1436 | 2.7480 | 2000 |

## 后续边界

B4 完成，**B5 / D3.04–D3.07 与 G3 未开始**。真实 Submit/ExecutionRef、协作取消、Task 父子结构化寿命、CLI 真实 Task 观察及完整 Embedded footprint 由 B5 承接。未扩展 GUI/CAD/CAM/设备产品模块；本次结果不等于物理设备准入。

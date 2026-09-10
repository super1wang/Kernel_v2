# B5/G3 Post-Gate Closure 交付

日期：2026-09-10。状态：**Passed**。历史 G0–G3、B1–B5 Passed 保留；B6 / D4.01–D4.04 **Ready for Development，尚未开始**。范围仅内核及规划验证消费者。

## 已关闭问题

- C1 / P1：硬准入压力触发一次有界普通终态回收并重试；原额度不放大。只删除无外部 pin 的 Accepted Terminal，保护未发布、活跃、Finalizing；析构在表锁外。
- C1b / P2：InvocationRecord 真正 settled、实际 input owner 释放后幂等归还输入额度；异步 candidate 不提前归还，结果仍承担 record/reply 预算。
- C2 / P2：接受前丢弃与接受后完成分开。Accepted 的 Executor、期限、资源、当前权限、取消及结果计量/异步构造失败均保存同一 ExecutionRef 的 Completed Outcome。execution_accepted 与 business_entered 分别表达；未放宽无应用证明，短 Native Invoke 行为保留。合同及安装 SDK 头摘要已同步。

生产修复提交为 `3ebd4f4`，SDK 指纹和父取消子执行的合同断言同步于 `9b63967`。SPEC/CODE 见 [集中复核](../reviews/B5-post-closure-review.md)，合同决定见 [ADR](../adr/ADR-b5-accepted-outcome.md)。未发现未解决 P0/P1/P2 blocker；未混入协议拷贝等非阻塞优化。

## 同来源正式影响集

最终来源：`9b63967214160a02fbc5f9eeab58dbaf1781e64a`。608 个实际输入与 Git blob 字节一致，输入摘要为 `94d50dfbf40acb92f73a56681fa4301e75c4ebffe2aab01a1cc1b565340b8ee0`。每配置固定 49 项 expected，事前映射 C1/C1b/C2 和受影响 G3-A/G3-B；没有重跑历史 126×3 或无关 Gate。

| 配置 | 结果 | 原始报告 |
|---|---:|---|
| Debug | 49/49 | [报告](../../evidence/9b6396721416-94d50dfbf40a/win-msvc-debug/D3.07/20260910T032444Z-98aa59be21a2/report.json) |
| Release | 49/49 | [报告](../../evidence/9b6396721416-94d50dfbf40a/win-msvc-release/D3.07/20260910T032526Z-16f6acf5369f/report.json) |
| ASan / RelWithDebInfo | 49/49 | [报告](../../evidence/9b6396721416-94d50dfbf40a/win-msvc-asan/D3.07/20260910T032739Z-6b2f62942137/report.json) |

包含默认限额 12,000 次连续接收、record/reply 压力、全部 pin 拒绝与释放恢复、析构重入、异步拥有输入排空、接受后失败分类、真实跨连接 get/wait/result/list、公开 Schema 正反例、安装 SDK 与 Runtime 依赖 DAG。真实连接测试保持样例每绑定并发额度，使用独立连接占用两个 worker，等待任务取消/过期均为 Completed/CancelledBeforeApply。

首轮 Debug 46/49 的两个旧 SDK 头摘要及一处旧子执行 Rejected 断言失败完整保留（[原报告](../../evidence/3ebd4f48995b-21cd1b564b57/win-msvc-debug/D3.07/20260910T032133Z-a232692569ee/report.json)）；修正后重新冻结并执行三配置，不拼接不同来源结果。

## 新来源 G3-C

四组数值预算与历史 `85cf798` 逐值一致，NativeSubset 字段不变。新来源方法在正式运行前由 AI 技术复核绑定（[预算决定](../reviews/B5-post-closure-budget-decision.md)），沿用 6 组 ABBA。每配置独立构建、安装完整 Embedded，实际图仅 Foundation/CoreContracts/Runtime/CpuPool，依赖仅 expected/thread_pool。

| 配置 | 新报告 | 结果 |
|---|---|---|
| Release 占用及同一二进制启动 | [报告](../../evidence/G3/C/298a935007cc4a38bd03daa54c2889e4/report.json) | 两项原预算全部通过 |
| Debug 分配诊断 | [报告](../../evidence/G3/C/f6e00a0ddab045a88eadcc9a50092e01/report.json) | 480 个完整 Invoke 窗口零新增分配 |
| ASan 分配诊断 | [报告](../../evidence/G3/C/244d8dc4ef3541679746491c681c68c7/report.json) | 480 个完整 Invoke 窗口零新增分配 |

Release Private 峰值 1,699,840 字节，分发增量 1,345,912 字节；构造到 Ready 最大 1.1783 ms，创建到 Ready 最大 47.1265 ms。每次 Ready 新增 3 个内核线程 ID，shutdown 后消失，并结合原结构上界证明；不把阶段采样称为连续 ETW。Debug/ASan 仅为分配诊断，不作为 Release 性能。旧 G3-C 原文不变，本次不复用其旧 Runtime 编译输入结论。

## 最终决定与边界

[语义矩阵自动验收](../../evidence/B5/post-closure-9b63967/semantic-acceptance.json)核验原始档案、固定 expected、AI SPEC/CODE 和提交字节，errors/review_errors 均为空；[G3-C 材料复核](../../evidence/B5/post-closure-9b63967/footprint-integrity.json)核验 455 项唯一材料、方法身份、原预算、真实命令与分配/线程事实。[最终收口决定](../../evidence/B5/post-closure-9b63967/acceptance.json)同时绑定两者，解除 B6 HOLD。

归档提交只增加证据和进度/交付文档，不改变被测输入。安装目录、独立消费者二进制和本机原始库存继续保留在各报告记录的 build 路径。未开始 State/Commit/Atomic、Durable/SQLite 或 GUI/CAD/CAM/设备开发；不声明最终产品或物理设备验收。

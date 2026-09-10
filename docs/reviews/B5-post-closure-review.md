# B5/G3 收口 SPEC/CODE 技术复核

日期：2026-09-10。审核者：Codex AI；按 automatic-acceptance-policy.json 自我复核。范围仅 C1、C1b、C2，历史 Passed 不重开。机器运行和最终放行独立于本技术结论。

## SPEC

C1 保留原硬额度及普通终态保留上限，硬准入失败只做一次 scan_limit 内扫描和一次重新预留。只有已发布 Accepted、Terminal 且无三个内部索引之外 owner 的条目可回收；未发布、活跃、Finalizing、结果/查询 pin 均不能回收。扫描游标跨尾部继续，删除后的弱一致列表允许跳过已回收项。

C1b 输入额度与 record/reply 额度独立；只在 InvocationRecord settled 且实际 input owner 已释放后幂等归还。异步 candidate 不等于 drained，结果 pin 不再保留已释放输入的额度。

C2 采用 ADR-b5-accepted-outcome.md：接受身份与业务进入分别表达。接受前仍可 Rejected；接受后同一 ExecutionRef 只有 Completed，开始前取消/到期为 CancelledBeforeApply，其他失败为 FailedBeforeApply。业务结果封装失败保持真实 business_entered；不伪造应用事实，不扩展 State/Effect。Outcome 重验、wire 可选布尔字段与严格 Schema 同步。

SPEC 结论：批准本次窄范围合同及事前 expected。安装 SDK 属开发版同来源更新，不声明旧严格消费者兼容新增字段。

## CODE

检查 ExecutionTable 的 reservation、三个索引、回收条件和锁顺序：硬额度修改仅在 ledger 锁下，回收持表锁查询 ledger，无持 ledger 锁调用表的路径；条目从三个索引摘除后移至局部 retired，在表锁外析构。容量不足、所有 pin 和不可回收条目保持拒绝，未扩大 limits。

检查 ManagedInvocation 完成顺序：只有 record settled 才 release_input_charge，Reservation 使用 exchange(input,0)；结果仍占 record/reply，重复 finish 不重复扣费。异步完成只发布候选，call 最后释放后才 settled。RequiredRecord 与子执行排空继续约束 Terminal。

检查 InvocationRecord：接受前 discard 独立于完成入口；Scheduler 未开始错误与 run 的接受后 Rejected 被规范化为合法 Completed。结果计量失败只替换成功 ReadCompleted，已有固定失败/取消回执保留原始原因。异步 Outcome 验证的 bad_alloc 与其他异常使用无结果失败回执。失败回执使用已有空 facts 和有界 Name，不再次验证可能抛出的 R。

直接测试已覆盖默认额度 12,000 次顺序执行、record/reply 压力、所有 pin 拒绝及释放恢复、保护状态、析构重入、异步真实 input owner 与相同大输入再次准入、接受前与接受后各错误分类。真实 Control 多连接占用两个 worker 后验证等待取消/过期的 get/wait/result/list；未改变服务并发额度。协议正例和非法类型/未知字段反例均直接通过。

CODE 结论：本次变更未发现 P0/P1/P2 阻断。正式 Debug/Release/ASan 影响矩阵、安装/DAG 以及原预算 G3-C 刷新仍须真实通过；本文件不是 Passed 决定。

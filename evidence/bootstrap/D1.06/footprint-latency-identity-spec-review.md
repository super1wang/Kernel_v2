# 轻量时延身份有限 SPEC 复核

actor_type: AI；结论：Approved，仅限候选所述身份/分析器修正及单对验证方案，不批准实现、正式时延数值或 pilot。

现有 poll 对 latency 已跳过 periodic，修正为 memory_sampling=phase_boundaries、sample_interval_ms=null 能准确表达实际行为。occupancy/allocation 保持 due_5ms/5，thread_sampling 仍为阶段边界；配置决定预期字段，方法摘要包含实际取值，latency 任何 periodic 点都拒绝。无需改变 owner 调度、固定停留、ACK、模块查询或原绝对期限，符合已审方法 §3 的轻量模式。

旧错误身份的 latency 记录保留为历史，不重命名或继承为新口径结果；既有 occupancy/allocation 参数和摘要不得因该窄修无故变化。对应反例应覆盖新 latency 身份、错误旧标签/null处理及伪 periodic 拒绝，同时保留两种周期模式正控。无计数 Release 同消费者的新固定单对只证明连通/身份与三种实际 Ready 间隔，不能把含阶段查询的父观察时间说成零开销，也不能从单对推正式分位或预算。

候选仅修正表述与实际执行一致性，没有放宽线程/分配硬约束、缺样政策或正式采样轮数。本次未修改代码、未运行测试；实现仍需有限 CODE 复核。

输入候选 SHA256：`c692f124073da58f991f0a33494247c3036306590e22a986494bc4d8aac9aa49`。当前方法合同 SHA256：`fdd83a79899712c65ae3a4e0f1e879badd3fcc9a50f9da0dd1519cf7af29609e`。

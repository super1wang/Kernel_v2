# P0 Evidence 摘要独立复核

AI SPEC：Approved。AI CODE：Approved。复核者为主集成 AI，实施者为 inline_policy；结论仅适用于下列摘要工具输入，不是 D1.06 或 G1 验收。

规格依据为提速方案 v2 的快读入口、完整原始证据保留和正式门禁语义不变要求。代码核对覆盖实际 expected/discovered/JUnit 对照、重复轮次、失败/缺项、原始流与归档 SHA、快照 JSON 重排时原成员校验、摘要自身摘要及确定性重建。报告声明保留在 report_claims，不产生独立 Passed；摘要写入失败让 CLI 非零且不改写原报告。顶层命令成本明确不代表嵌套编译次数，不输出虚构 Token 节省率。

独立复核提出的原 manifest/expected SHA 漏核问题已通过真实失败反例修复；review.archive 分支原本正确，只补回归，不虚构缺陷。最终四文件 SHA 和字节数与 [输入记录](p0-summary-final-snapshots-c2fbfcc0c56e/changed-source-inputs.json) 逐项匹配，集合摘要 `606e06c66100807ed5a96d31d3bbc9f64976685ef4d82fd483edd06d7d865594`。最终 15 项执行及三份历史报告只读兼容命令的退出、Job 排空和 raw 长度/SHA 已核验。

最终 15 项包含真实采集器及摘要失败隔离；已有 34 项 runner 回归发生在最后快照校验修复之前，仅作为未改采集主流程的相关证据，不称最终版本重新跑了 34 项。D1.05 三配置只读提取得到 283/283/285，原目录字节未变，没有重跑正式矩阵。详见 [实施者自审及全部失败索引](p0-summary-self-review.md)。

范围内无未关闭阻断。摘要不是签名认证或完整 audit 的替代；后续正式 run 仍执行原完整机器审计。

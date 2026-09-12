# 架构决定登记

本目录记录实现期间的正式 ADR，不能以草案修改唯一规范。

| 决定 | 来源 | 责任包 | 当前事实 |
|---|---|---|---|
| 从零建设，不迁移旧 API/旧数据 | A00.1、E00 | D0.01 | 已登记现有规范 |
| UI 业务写统一 Operation | A23.1 | D0.02 | 已交付，D0.02 Passed；历史人工评审字段不改写 |
| 无通用 state.inspect/query RPC | A23.1 | D0.02 | 已交付，D0.02 Passed；历史人工评审字段不改写 |
| GUI 为主人工前端，DSL 可选 | A23.1 | D0.02 | 已交付，D0.02 Passed；本阶段不开发 GUI |
| SDK 独立 SemVer 与公开边界 | A19.2–A19.4 | D0.02 | 已交付，当前安装裁剪见下条；历史证据不改写 |
| [B2 收口：认证上下文、生产裁剪与 DynamicOnly 责任](ADR-b2-closure-boundaries.md) | A17/A19/A21、E00/E02 | D1.06、D2.04；D7.04 承接后续能力 | 设计已采纳；本次实现/验证以 plans/B2 与 progress 为准 |
| [B3：OS 身份、真实首字节、单 Host 目录和 Volatile 意图](ADR-b3-pipe-start.md) | A17/A19、E00/E02 | D2.05–D2.07 | 实施中；已有真实 IPC 直接验证，正式批次审核未完成 |
| canonical CBOR feasibility A/B/C 结论 | A12.4、D5.02-a | D5.02 | 尚未验证，禁止提前选结论 |
| [B4：Executor 寿命与资源边界](ADR-b4-executor-resources.md) | A09/A21/A22、E06 | D3.01–D3.03 | 实施前决定；正式结果以 B4 计划及 progress 为准 |

D0.01 已批准；D0.02 沿用既有架构决定形成具体 ADR，未更改唯一规范。正式决定必须同步合同、测试、资料和受影响包。
| [B5：接受身份与业务进入](ADR-b5-accepted-outcome.md) | A05/A09.1，E00/E02 | B5/G3 post-gate closure | C2 编码前决定，验证及状态见 B5 规划/progress |
| [B6：AtomicProvider 调用期权威与内存同步提交边界](ADR-b6-atomic-provider-context.md) | A05/A07、E00/E02 | D4.01–D4.04 | 已采纳；B6 正式结果以 plans/B6 与 progress 为准 |

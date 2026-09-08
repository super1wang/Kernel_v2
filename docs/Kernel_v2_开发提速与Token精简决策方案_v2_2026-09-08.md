# 内核开发提速与 Token 精简决策方案 v2｜历史索引

> **状态：Superseded。** 本文件原有有效规则已经进一步收敛并并入
> `01_Architecture_v3.3.md` 与 `02_Execution_Plan_v3.3.md` 的 **v3.3-r2**。
> 当前开发 AI **默认不得读取本历史文件作为执行输入**；需要追溯 2026-09-08
> 测试提速决策、Fixture/dev-fast/Evidence summary 的来由时再通过 Git 历史读取原版本。

## 被 r2 吸收并升级的原则

1. Code-First：生产代码和直接调试优先于流程工具、审核材料和 Evidence 编排。
2. Passed 前置具有缓存语义：后续默认不重复审核或测试已经 Passed 的包。
3. 开发依赖与正式验收依赖分开：Development Batch 内使用临时 `Implementation-Ready` 条件继续编码，但正式 Passed 仍遵守原 DAG。
4. 相关工作包连续开发，不把每个编号变成独立 Codex 会话或中间交付墙。
5. `S_changed` 用于开发、`S_required` 用于包/批次正式完成条件、`S_gate` 由阶段 Gate 承担完整集成。
6. 同一 source/profile/build 环境中的物理 configure/build/test 尽量一次执行，多包按 manifest 映射逻辑结果。
7. Review 批次化：一次上下文准备，可产生多个 task 的独立 SPEC/CODE 结论。
8. Fast 小项不独立建 plan/review/evidence；Critical 只保留真正风险 checkpoint。
9. G0–G8 顺序、64 个工作包编号、行为合同、安全/寿命/耐久/恢复门禁均不因提速而降低。

## 历史 P0 工具结论

此前已完成 Compile Contracts 配置复用、`dev-fast`、AI 导航与 Evidence summary。
其中配置次数下降但墙钟收益未证实，因此**工具提速阶段已经结束**；不得继续把
Install Consumer 优化、Evidence 平台化或测试工具重构设为生产开发前置。

## 当前执行来源

- 架构：`docs/01_Architecture_v3.3.md`（v3.3-r2）
- 执行：`docs/02_Execution_Plan_v3.3.md`（v3.3-r2，E00/E02/E08）
- 当前状态：`docs/progress.md`
- 当前包计划：`docs/plans/D1.06.md`

原 v2 全文由 Git 历史保留，不在当前文件重复 1000+ 行内容，以避免 AI 上下文浪费。

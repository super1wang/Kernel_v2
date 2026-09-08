# B3 前影响收口交付

日期：2026-09-09。仅覆盖内核与验证消费者；B1/B2 历史 Passed 保持不变。本轮修复来源为 `6662c4e75601`，输入摘要为 `bb382114a7da0af12dd388ae63a5fa3df821c9aafd86841f7261e3ab8d596cf7`。

## 建议取舍与交付

用户收口文档作为评估输入，已逐项对照当前源码与唯一规范。执行前建立的 [B2 计划](../plans/B2.md)包含四包历史事实补录及本次修复安排；历史补录没有冒充原开发前计划。[B3 计划](../plans/B3.md)已经建立，后续须按该计划推进。执行前规划、重大决策 ADR 的规则已写入 E00 与根 AGENTS.md。

| 建议 | 本轮结果 |
|---|---|
| C-B2-01 | Subscription 外部 reserve、Policy/source 调用移至连接锁外；短锁复核关闭/退订及队列预算，跨线程反例验证不会死锁或继续出帧 |
| C-B2-02 | 后续页延续首次 cursor 的 issued/expires，翻页不续期 |
| C-B2-03 | cursor MAC 认证完整连接、委托和权限视图；跨会话及权限收缩在扫描前拒绝 |
| C-B2-04 | ACK 与 event 一致使用含 12 字节头的 frame_bytes 预算，超限回滚 watch/lease/queue |
| C-B2-05 | Runtime/B2Subset 显式生产选择，Runtime 不取得、构建或安装 Data/jsoncons；安装清单与 find_package 能力以实际目标为准 |
| C-B1-01 | Ready 前比较 Logging 实际容量、overflow 和最低级别；不符沿既有失败关闭/重试路径处理 |
| C-B1-02/C-B2-06 | 更新现行合同状态及 Native SDK 表面说明；DynamicOnly 执行注册由 D7.04 承接，之前保持 requires_dynamic_schema 且不 eligible |
| O-01 | 去除 cursor read 时无用重签；CNG provider 缓存不扩展 |
| O-02/O-03 | 延后：尚无测量收益支撑，不删除 HostBound 最终检查、不新增 single-flight 平台 |

认证输入、Runtime 裁剪和 DynamicOnly 责任划分见 [ADR](../adr/ADR-b2-closure-boundaries.md)。集中 [SPEC/CODE 复核](../reviews/B2-closure-review.md)区分 D1.06、D2.04 与 SDK 的独立责任；AI 审核不冒充人工批准。

## 验证与原始证据

正式运行前固定 expected，三配置共用同一已提交源码字节，未重跑无关历史 Passed 矩阵。

| 配置 | 结果 | 原始报告 |
|---|---|---|
| Debug | 11/11 Passed | [report.json](../../evidence/6662c4e75601-bb382114a7da/win-msvc-debug/D2.04/20260908T172207Z-a5f1b8d0628a/report.json) |
| Release | 8/8 Passed | [report.json](../../evidence/6662c4e75601-bb382114a7da/win-msvc-release/D2.04/20260908T172312Z-8fb4bac953af/report.json) |
| ASan | 8/8 Passed | [report.json](../../evidence/6662c4e75601-bb382114a7da/win-msvc-asan/D2.04/20260908T172356Z-a5f5a014ffad/report.json) |

共同集合覆盖 subscription/list/cursor、Host 启动失败/关闭错误/日志所有权/Native 结果及 B2 安装消费者。Debug 增加架构 DAG/负例与 Runtime-only 无测试生产构建、安装及真实 Native 消费者专项。ASan 和 Release 的增加由并发生命周期与公开安装布局修改触发。

开发直接验证 7/7、Runtime 裁剪专项 1/1 通过。首次 list 反例误用权限收缩前的旧 VerifiedCaller，原失败保留于 `evidence/bootstrap/B2/resume-closure-direct-3c7bded8e1`；改用新可信调用者，验证 cursor 自身失效，未通过放宽生产校验绕过失败。开发与正式原始记录均保留，不覆盖失败或拼接来源。

最终自动决策见 [验收记录](../../evidence/bootstrap/B2/closure-acceptance-6662c4e.json)。该决策使用既有验收器核对三配置矩阵、提交字节、原始证据及 AI SPEC/CODE；矩阵标识 D2.04 用于本次影响收口，不新增 Gate，也不重新宣告整个 B2 历史矩阵运行。

## 放行边界

本次影响收口 Passed，B3 可进入开发；B3 生产实现尚未开始，G2 未开始。真实 IPC/CLI/Shell、Task、完整结果及 Plan 不由本轮测试证明完成。DynamicOnly 服务端执行仍须由 D7.04 交付后才能开放对应能力。

被测生产提交为 `6662c4e`；本交付文档及 progress 是后续归档，不声称对归档提交重新运行相同测试。工作区原有 `docs/AGENTS.md` 删除保留且不纳入本次提交；构建目录保留本地。

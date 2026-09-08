# B2→B3 影响收口复核

日期：2026-09-09；本材料复核本轮修复，不改写 B1/B2 历史 Passed。执行前规划与建议取舍见 plans/B2.md，重大决定见 ADR-b2-closure-boundaries。

| 责任 | SPEC 结论 | CODE 结论 |
|---|---|---|
| D1.06 Logging | 容量/overflow/level 必须兑现 Host 配置；失败 owner 沿原关闭/重试模型处理 | 三字段比较在 Ready 前；负例覆盖三种不符及关闭失败后重试，不改变业务 Outcome |
| D2.04 Subscription | 外部 reserve 与 Policy/source 不持连接锁；ACK/event 同一含头预算 | 编码上下文串行、忙时 gap；回调前后短锁复核、start_now 与 close 仲裁；跨线程 close/unsubscribe、ACK rollback 反例通过直接验证 |
| D2.04 Cursor/List | 首次期限不续期；权限/委托/连接共同认证，失效在扫描前拒绝 | 保留恢复时间戳，MAC 附加可信上下文且无旧 profile 回退；新 VerifiedCaller 验证委托收缩负例；read 不再生成丢弃 token |
| B2 SDK／B1 Runtime | 生产取得、构建、安装均支持 Runtime 裁剪，Global SDK stage 不等同安装能力 | 明确 Runtime/B2Subset 选择；实际目标判定 find_package；清单按实际目标投影；无测试 Native producer 和真实安装消费者通过直接验证 |
| 文档与后续责任 | 执行前已有批次计划，重大变化 ADR；历史规划补录明示 | B2/B3 计划已补，现行合同去除过期阻塞文字；DynamicOnly 执行注册归 D7.04，在此之前拒绝暴露为 eligible |

本批冻结影响集合为 Debug 11、Release 8、ASan 8。SPEC 已逐项对照用户附件与规范，O-02/O-03 无测量收益支撑而延后；O-01 仅消除 read 重签，provider 缓存不扩展。CODE 已检查锁序（Policy→连接的 start_now 方向，订阅/退订/关闭无连接→Policy 反向持锁）、关闭后队列回滚、SDK实际表面与测试反例。未发现阻止执行该影响矩阵的剩余问题。

最终准入由同一提交字节来源的三份正式报告决定；矩阵未齐备不视为 B3 放行。记录为 AI SPEC/CODE，不是 human Approved；仅覆盖当前内核与验证消费者，不宣称真实 IPC/Task 已实现。

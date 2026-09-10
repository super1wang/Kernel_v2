# B5 收口：接受身份与业务进入分别表达

日期：2026-09-10，C2 编码前决定；范围为 B5/G3 post-gate closure。

## 依据和冲突

架构 A09.1 要求已 Accepted 后的 Executor 拒绝形成同一执行的 FailedBeforeApply；A05 及当前 CoreContracts 却把该变体限于 business_entered=true。不能为绕过校验把未调用的 Handler 记作已进入，也不能把已接受结果降为 Rejected。

## 决定

BeforeApplyDecision 追加默认 false 的 execution_accepted。FailedBeforeApply 要求 business_entered 或 execution_accepted 至少一项为真，仍要求无应用证明、无已应用/未知事实。该值由可靠执行 owner 生成，不接受客户端提供的授权或业务事实；早到完成仅内部暂存，只有表中 Accepted 发布成功才可对外投影，否则放弃未发布条目。

既有三字段聚合初始化保持默认 false。生产 wire 的 before_apply 允许可选 execution_accepted:boolean；编码仅在 true 时输出，短 Native Invoke 的既有 wire 保持。CoreContracts 重验比较完整条件，不能用不同的接受事实重验同一 Outcome。严格的开发消费者须按同一更新后的 Schema 构建，本项目未冻结最终 SDK ABI，不宣称旧消费者兼容新字段。

受管理开始前取消仲裁/到期产生 CancelledBeforeApply，其他开始前失败产生 FailedBeforeApply；business_entered=false，execution_accepted=true。业务已经进入后的结果计量/封装失败，在本批只支持的 Read/PureCompute 中使用 FailedBeforeApply，business_entered=true，保留无应用事实；不放宽 ReadCompleted 必须有成功结果的合同，不提前决定 State/Effect 的结果存储策略。

接受前的丢弃与接受后的完成使用不同内部入口。普通 Native Invoke 的拒绝、失败和调用线程规则不变。RequiredRecord 继续在同一 Completed Outcome 追加记录事实，不因失败绕过排空。

## 验证和后果

新增未进入且未接受的 FailedBeforeApply 拒绝、仅接受合法、条件重验不匹配拒绝和 wire Schema 正反例。受管理 Executor/cancel/deadline/resource/current-admission、同步/异步结果计量与构造异常分别直接验证。刷新安装头摘要、受影响 G3-A/B/C；不重跑不受影响历史 Gate，不扩展性能优化或 B6。

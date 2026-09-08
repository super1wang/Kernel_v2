# ADR：B2→B3 收口的认证、裁剪与能力责任

日期：2026-09-09。状态：设计采纳，实现与验证见 plans/B2.md；不改写 B1/B2 历史验收。依据 A17/A19/A21、E00/E02 及用户收口请求。

## 决定

1. Subscription 的外部 reserve/来源回调不持连接锁。序号、额度与条目状态在短锁内处理；回调返回重验 active/closed。start_now 仍遵守既有可信、非阻塞、不可重入合同。不同订阅的事件可有损合并，必须保留 gap，不用通用 actor 框架替代。
2. cursor 固定 v1 payload、120 秒初始期限和零句柄模型不变。生产 list 将 Policy 的 connection identity 与 delegation generation 作为 MAC 的附加认证上下文，payload 的 view 继续绑定 permission generation。完整视图由三者共同认证，不能简单相加或截断散列成 uint64。基础无附加上下文的固定 golden 仅验证 codec 基础 profile；生产 list 必须提供连接/委托上下文，旧 token 在新上下文不回退验证。MAC 输入扩展必须同步 cursor-v1 合同与正反例。
3. 构建选择采用 `OCK_BUILD_COMPONENTS=Runtime|B2Subset`。默认 B2Subset；Runtime 仅构造/安装 Foundation/CoreContracts/Runtime 和 expected，Data/jsoncons 完全不进入依赖取得和目标图。SDK 版本和全仓 API 清单保持统一，安装清单与可用组件必须按实际选择投影；不宣称裁剪安装具有未装组件。Runtime 业务依赖边不变。
4. 当前仅 SharedTypeContract 动态绑定可执行。复杂 DynamicOnly Schema 的服务端注册/执行适配及对应能力声明由 **D7.04** 明确接管，进入 MCP 能力投影前完成；此前 B3 CLI 和其他入口继续 fail-closed，不暴露为 eligible。若未来实测需求要求提前实现，先修订本 ADR 与责任包计划，不悄悄越界。

## 取舍与验证

附加认证上下文保持固定 payload/golden，不增加服务端 cursor 表；代价是生产 MAC 的校验必须携带当前可信会话上下文。相比扩展 token 字段或修改 Policy 全局世代，它避免数据格式迁移和无关会话的授权失效。不同会话同主体、委托收缩及原始期限反例必须在 source scan 前失败。

Runtime 与 B2Subset 两种已实现生产集合足够当前任务；不引入任意组件求解平台。实测 Native-only 无 jsoncons configure/build/install 与实际安装消费者，B2Subset 原路径继续通过。未来扩展组件选择须保持依赖取得、目标与安装表面一致。

Logging limits 必须严格兑现而不新增配置协商；ACK/event 的 frame_bytes 统一包含 12 字节 OCK1 头。O-02/O-03 性能重构等待真实测量，不能作为本轮 B3 前置。最终收口仅重验影响范围，历史审核和证据保持不可改写。

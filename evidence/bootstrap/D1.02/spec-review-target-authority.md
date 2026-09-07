# D1.02 Target authority 提案独立 AI 规格意见

日期：2026-09-07。actor_type：AI。结论：认可补充 TargetAuthority 和 Context 接收端复验的设计方向；下列接收端绑定来源及同 issuer 跨主体反例应在正式 API 增量中明确后再冻结。本意见不表示相关实现或测试已经 Passed，不增加人工节点。

审核对象为 target-authority-proposal.md，依据已冻结 core-contracts-api.md 的可信组合根 authority、真实发放记录和接收者独立复验原则，以及架构 A04/A06。只读提案及原合同，未修改提案、API、实现或 Git。

## 已闭合的边界

- TargetView 只报告目标；删除由目标对象自身声称有效的 revalidate，改由 TargetAuthorityPort 以自己的发放集合核对对象、caller、目标及失效/世代。业务派生同接口或填写相同 ObjectId 不会自动加入真实 authority 的发放集合。
- EffectContext/TransitionView 私有构造，check 首先核对 CallerView 对预期 CallerAuthority 的归属及当前有效性，再检查 permit 主体/目标/期限和 TargetAuthority 发放记录。Context 保有 authority、target、permit 的 owner；work/records/transition 的同步借用寿命已有明确要求。没有可复制/移动 Context 从而延长候选或借用作用域的承诺。
- 公共 check 本身允许业务在其自建 authority 域内得到 Context；真实安全边界是敏感接收端用自己的两个 authority 调用 revalidate，先比实例再重验当前记录。提案明确该调用是接收端责任，不能由请求自行调用后省略真实接收者验证。
- check/revalidate 不调用 PermitAuthorityPort::consume，字段匹配明确不证明许可真伪。真正消费仍在发送/转换附近由实际端口仲裁。该分离符合 A06，不需要为创建 Context 提前 claim，也不把 ActivityLease/ResourceLease 当许可。

## 正式 API 必须明确的绑定来源

1. 第 5 步消费许可时，`consume(permit, expected_binding)` 的 expected_binding 应由真实接收者依据当前已绑定 operation/组摘要、已解析 target、主体、权限世代、生命周期世代及其当前约束形成；不得直接将 `permit.binding()` 或请求提供的整份绑定原样作为 expected。仅以真实 authority 查到合法 permit，再拿它与自身字段比较，不能防止同主体同目标的 A 操作许可被用于 B 操作。检查此独立绑定不等于提前消费，真正一次消费仍位于原决定点。
2. Transition 的 before/generation 必须来自实际协调者当前状态；真实转换接收端应核对当前状态/世代，而不是只验证传入 generation 与 permit 自报字段相等。TargetAuthority 的发放记录验证应沿提案约定核对当前世代失效；后续真正 consume 继续核对接收者自己的生命周期绑定。无需在 D1.02 实现生产状态机或 PolicyEngine。
3. 固定测试子断言应包含同一真实 CallerAuthority 内两个不同主体 A/B，交换 permit 与 target grant 均拒绝；“不同 CallerAuthority”只覆盖跨 issuer，不能替代这组跨主体测试。合法 check、合法重复 revalidate、上述失败、过期及世代失配过程中，consume 调用计数均须为 0；实际执行接收端消费的正例另证明只消费一次。复用已有 context_capability_boundary/effect_shape/lifecycle_shape，不增加名称或删减范围。

这些要求沿原 API 的可信接收者原则具体化，不要求增加通用 issuer、认证器、数据库或提前实施设备发送。提案中私有不可赋值 grant 及真实发放集合仍须在实现中兑现；静态文字不能证明对象不会保留可变别名。

## 输入 SHA-256

- 提案：`35dcfebefbb1f750263959eb862e0f16064b650aed86417549cc2c3e103c7724`。
- 原冻结 API：`48a8fae8e34d9e2532095a437c6b10088cd54b62c3e84e36cc78d5029c245bf7`。

后续提案或 API 更改需追加真实字节审核；本轮没有运行 C++ 测试，不将模型/测试工厂当作生产授权实现。

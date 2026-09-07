# D1.02 Target authority 最终增量 AI 规格审核

日期：2026-09-07。actor_type：AI。status：Approved（仅提案规格）。复核更新后的提案，初版提出的绑定来源与跨主体覆盖要求均已闭合，可按此精确版本更新正式 API。初版意见保留，不覆盖；本结论不表示实现、测试或包级验收已经 Passed，不增加人工节点。

1. EffectContext::check/revalidate 显式接收真实协调者提供的 expected/current PermitBinding；TransitionView 另核对真实接收端 current_before，生命周期世代来自当前绑定。提案逐字段要求与许可一致，并明确期望材料不能取自 permit.binding() 或请求。这闭合了同主体同目标的跨操作/组摘要、权限及生命周期绑定混用风险。
2. CallerView 先核对真实 CallerAuthority 归属与当前发放记录，TargetAuthority 再核对自身发放集合中的对象、主体、目标及世代。Context 私有构造且保存 authority owner；真实敏感接收端比较其自身实例并重新验证，因此自建 authority 域内的 Context 不能跨入真实接收者。
3. check/revalidate 不消费 ActionPermit。真正执行协调者先产品 revalidate，再以同一份自身 current_binding 调用固定 PermitAuthority 的 consume；字段比较不代替许可真实性及一次消费仲裁。既有 ActivityLease/ResourceLease 分离保持不变。
4. 固定子断言已加入同一真实 CallerAuthority 的 A/B 主体交换 permit、交换 target grant，以及真实协调者当前操作/摘要、目标、权限世代、生命周期世代、before 改变后的拒绝。失败必须保持 consume=0，跨 issuer、过期和撤销反例继续保留。合法 check/重复 revalidate 不提前消费及实际接收一次消费仍按初版要求实测；不新增测试名称。

声明可用少量领域端口、私有构造和现有 Result/RAII 落实，不要求提前实现认证器、PolicyEngine、生产设备发送或状态机。Target grant 的私有不可赋值创建、发放集合绑定和当前状态读取均仍须在代码审核与真实测试中兑现；本轮只读提案及原合同，没有修改 API 或实现，也未运行 C++ 用例。

输入绑定：

| 文件 | SHA-256 |
|---|---|
| `evidence/bootstrap/D1.02/target-authority-proposal.md` | `c10db74d135df3dee9abebfe291d15be42f4a20be40ffabd6f7bb3d6bea05dde` |
| `evidence/bootstrap/D1.02/spec-review-target-authority.md` | `2ce81b65fa354a66d9a2c6acd95924cac2670cd579d17ae01307e35849c81bf3` |
| `docs/contracts/core-contracts-api.md` | `48a8fae8e34d9e2532095a437c6b10088cd54b62c3e84e36cc78d5029c245bf7` |

正式 API 更新后应单独绑定其新 SHA；本批准不自动覆盖超出提案的声明变化。

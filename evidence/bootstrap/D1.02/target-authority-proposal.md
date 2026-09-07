# D1.02 Target authority 补充声明提案

仅供内部规格复核；尚未修改冻结API或产品实现。本提案闭合原合同3.1“TargetView沿同一配置authority协议建立重验”的实际调用路径，不实现认证器/Policy仲裁器，也不增加人工节点。

## 受影响声明

```cpp
class TargetView : public PortLifetime {
public:
    virtual foundation::ObjectId target() const noexcept = 0;
protected:
    TargetView() = default;
};
class TargetAuthorityPort : public PortLifetime {
public:
    virtual Result<std::shared_ptr<const TargetView>> resolve(
        const CallerView&, foundation::ObjectId requested) = 0;
    virtual Result<void> validate(const TargetView&, const CallerView&,
                                  foundation::ObjectId expected) const = 0;
};

class EffectContext final {
public:
    static Result<std::unique_ptr<EffectContext>> check(
        std::shared_ptr<const CallerAuthorityPort> expected_caller_authority,
        std::shared_ptr<const TargetAuthorityPort> expected_target_authority,
        CallerView caller, std::shared_ptr<const TargetView> target,
        std::shared_ptr<const ActionPermit> permit,
        const PermitBinding& expected_binding,
        WorkContext& work, EffectRecordPort& records);
    Result<void> revalidate(const CallerAuthorityPort& expected_caller_authority,
                            const TargetAuthorityPort& expected_target_authority,
                            const PermitBinding& current_expected_binding) const;
    // 其余已有窄访问器不变；构造私有、禁止复制和移动。
};
class TransitionView final {
public:
    static Result<std::unique_ptr<TransitionView>> check(
        std::shared_ptr<const CallerAuthorityPort> expected_caller_authority,
        std::shared_ptr<const TargetAuthorityPort> expected_target_authority,
        CallerView caller, std::shared_ptr<const TargetView> target,
        Name before, std::uint64_t generation,
        std::shared_ptr<const ActionPermit> permit,
        const PermitBinding& expected_binding, TransitionPort& transition);
    Result<void> revalidate(const CallerAuthorityPort& expected_caller_authority,
                            const TargetAuthorityPort& expected_target_authority,
                            const PermitBinding& current_expected_binding,
                            const Name& current_before) const;
    // 其余已有窄访问器不变；构造私有、禁止复制和移动。
};
```

删除当前实现中TargetView自报的revalidate()；它只提供目标描述。resolve/validate真实实现必须持有自己的发放集合，并核对视图对象确由该实例发放、未失效、绑定caller/目标/世代；不能只读取target()相等便成功。TargetAuthority的resolve是请求端口，不表示任意requested对象均可签发；真实权限与资源解析留D1.04，测试工厂只对明确授权表内目标签发。

## 可执行检查顺序与来源

1. Context::check拒绝空authority/target/permit；两个authority由组合根或已经装配的协调者提供，不能取请求随带实例。首先以expected_caller_authority调用validate_caller，要求CallerView的authority归属及其当前发放记录均有效。
2. expected_binding必须由真实接收端当前已绑定Operation/组摘要、已解析目标、当前权限/生命周期世代及服务端截止政策生成，不能取permit.binding()或请求值直接充当expected。逐字段要求permit.binding()==expected_binding，再核对expected主体等于已验证CallerView、目标非零且期限有效。Transition的before/generation同样来自真实协调者当前状态，要求generation等于expected_binding的生命周期世代且非零；不能用请求传入值与permit仅做自洽检查。这些字段匹配不是许可消费或许可真伪证明。
3. 以expected_target_authority.validate(*target,caller,expected_binding.target)检查真实目标发放记录及绑定。只有验证成功才用私有构造建立Context；内部持有两个authority和target/permit的owner，借用work/records/transition的同步作用域须覆盖Context使用。
4. 敏感实际效果/转换接收端在使用Context前调用revalidate(自己的caller_authority,自己的target_authority)。revalidate首先比较两个实例身份与Context建立时持有者完全相同，再按接收端传入的current_expected_binding重复当前Caller和Target验证及全部绑定/期限检查；Transition还要求其受检保存的before与接收端current_before一致，generation与当前绑定一致。因而业务在伪authority域先创建Context，也不能转交真实接收端取得有效目标能力。不得仅在注释中要求caller自行validate。
5. 真正发送/转换/提交附近仍由实际协调端口使用其固定PermitAuthorityPort::consume(original_permit,current_expected_binding)，以真实发放记录完成一次授权仲裁；current_expected_binding来自该接收端当前可信状态，不是original_permit.binding()。Context::check/revalidate不消费许可、不以许可字段匹配代替消费；ActivityLease/ResourceLease仍不能构造许可。本包不模拟实际设备发送或生产Policy。

所有getter都是对持有对象的同步只读借用，无public构造有效凭据的后门；目标grant的可变发放状态保留在issuer内，grant描述私有创建且不可赋值，不能沿用调用者可改写缓冲。无friend/skip或新的通用issuer模板。

## 既有用例内的正反见证

不新增33/38名称：context_capability_boundary增加同一实际工厂合法check/revalidate、不同caller authority、不同target authority、伪目标对象、自填另一target、撤销后的拒绝；effect_shape/lifecycle_shape仍验证精确函数指针签名，分别增加Context有效创建及过期/世代不匹配拒绝。伪authority可以创建其自身合法Context，但真实接收端以自己的实例重验必须拒绝；真实目标工厂通过真实发放记录而非target自报。

补充实际测试入口：测试协调者保存 `PermitBinding current_binding` 与 `Name current_before`，其 `accept_effect(context)`/`accept_transition(view)`先调用上述产品revalidate，再把同一current_binding交测试PermitAuthority.consume；失败前consume计数必须保持0。改当前operation/组摘要、目标、权限世代、生命周期世代或before（Context与permit材料保持原样），都必须拒绝，不能只测试两个伪造材料互相不等。同一真实CallerAuthority分别发放A/B，TargetAuthority和PermitAuthority各为两主体发放匹配材料；A/B之间分别交换permit及target grant，真实协调者均拒绝且consume计数0。不同issuer、过期和撤销反例另保留，不能用跨issuer检查替代同issuer跨主体检查。上述断言均放既有context_capability_boundary/effect_shape/lifecycle_shape内，不增加测试名称。

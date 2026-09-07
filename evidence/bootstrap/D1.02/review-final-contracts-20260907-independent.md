# D1.02 独立 AI 代码与规格复核

- 复核身份：AI，独立子代理 review_contracts；不是 human Approved。
- 基线 HEAD：8cd018c737b7418802b34dd4740c8897ef9825eb；审查对象为未提交源码，不把 HEAD 当作实现提交。
- 结论：ChangesRequested，2 项 P2；未发现确定 P1。
- 只读审查六个产品头、合同主体测试及两个工厂；未修改产品或测试，未代替父任务执行工具链、安装及正式矩阵。
- 规范输入：AGENTS.md、docs/progress.md、架构 v3.3 A04/A05/A06、执行计划 D1.02、docs/plans/D1.02.md、具体 API 及 Target authority 增量登记。

## 已审来源

具体 API SHA256：34fe36860dd33079f754eda209af9ab0d5093dfcda6ab10c81cbf52a25b53d3d。

六头与测试的内容散列实查与 `target-authority-final-19948aeb83/source-inputs.json` 对应项一致。特别记录：

| 文件 | SHA256 |
|---|---|
| context.hpp | 42d85bac31c267050b6d6ac89d5df4bd209d059a1e2f616345060a35fc58cc9a |
| outcome.hpp | 9496e2c8224090d54446bfc93bfad2013a7b97da5e4d5fe5d11541d5ff2fa38e |
| authority_factories.hpp | 1784a0e6fb99663770e85f7b26e369a5f21c99622393b37a34aaa5add60b9bd5 |
| factories.hpp | 63c746338c0b76afac74ebed6487d3fda6d31d9f81c42f0360a57302bf4bbee4 |
| test_support.hpp | 974e58cf8f62ceb59a50fe0a02ae62ea537da8652f8178633951c6cb0164beb0 |
| contracts_tests.cpp | 634dc827852fb37336ec98e7b9e15f6e954ef81e58216e9c69aecdd966037c51 |

## P2：真实接收者工厂消费了另一个许可

位置：`tests/conformance/core_contracts/authority_factories.hpp:252` 与 `:258`。

两个 Scenario::accept 在核验传入 Context/View 后，均调用 `permits->consume(*permit_a, current)`。这个 permit_a 是 Scenario 的成员，未证明是传入上下文所携带的原始许可。具体 API 3.1.1 明确要求消费 original_permit；Context 的字段匹配有意不承担许可真伪认证，因此此处不能换成另一个同字段的合法许可。

静态可证反例：以 s.a、s.target_a、s.current 和自定义 ActionPermit（binding 返回 s.current，但从未由 s.permits 发放）调用 EffectContext::check。check 按其冻结职责成功。将所得 Context 交给 s.accept，revalidate 仍成功，随后工厂消费真实 s.permit_a，整个 accept 成功。直接对该伪许可调用 s.permits->consume 本应失败。TransitionView 也可同样构造。现有同主体跨 issuer/目标/当前绑定用例不能触发这个差异。

影响是 D1.02 真实端口接收测试存在认证边界空洞，不据此宣称生产 Runtime 漏洞。需要接收夹具保证所消费对象与本次上下文使用的原始许可为同一对象，增加同字段伪许可及其他 issuer 许可的拒绝反例，合法控制路径仍消费一次。本次未编译执行该新增反例；上述为精确调用路径静态证明。

## P2：既有调用者和发布工厂仍公开可变发行集合

位置：`tests/conformance/core_contracts/factories.hpp:135`；同类位置 `tests/compile/contracts/test_support.hpp:210`。

Authority::issued 和 Publication::issued 都是 public 可变 vector，且外部可以直接构造 Grant/Proof。因此调用者可以绕过 authenticate/publish 所代表的可信材料输入，将自行构造对象直接插入真实实例的已发行集合。随后 validate 会依据同一对象地址认可它。比如 `authority->issued.push_back(std::make_shared<const Grant>(forged_description))` 即可令未认证主体被真实观察工厂接受；Publication 同样可直接插入 Proof 再验证通过。

这直接违反具体 API 3.1 的“不能公开可变发放记录”及测试工厂不得创建另一 authority 有效对象的要求。Target 新增工厂已把集合及具体发行类型隐藏，但既有观察/Outcome 工厂尚未同步。应把发行集合及实际凭据具体类型封闭，保留明确的测试可信入口/撤销方法，并增加不可公开写 issuance 的编译反例及伪对象拒绝控制测试。

## 其余重点结论与边界

- 产品 Target 检查向固定 caller/target authority 核验，并用接收端 current binding 重验 operation、group、target、两种 generation 和 deadline；Transition 另外核对 current_before。跨 authority、同 issuer A/B 和撤销/过期已有实际测试断言，不依凭据自报 valid()。
- 零世代：Transition 增量规范明确 generation 非零；架构规定许可绑定权限及生命周期世代，却没有明确给出 ExternalEffect 的零世代合法控制输入。不能据这种缺省推断要求删除现有拒绝，更不能绕过此前自动审批拒绝。当前未据此登记代码缺陷；如后续确需零值，须先有具体规范依据和相应合法/非法控制对。
- Outcome 的事实追加、未知对账、发布证明、真实 owner 当前条件复验和实际已发生 effect/lifecycle 保留路径在本轮静态审核中未发现另一个确定 P1/P2。已有测试证明 result 错误不会抹去 Applied，以及 after/generation 不一致拒绝。
- 33 项 green 原始证据已提供并有真实命名/输出；它们不消除以上未覆盖反例。本轮不声明额外 11 项、SDK、Debug/Release/ASan 或包级自动验收 Passed。
- 修复后需按新源码散列独立复核并重跑受影响测试，保留本报告为历史，不能改写成已批准。

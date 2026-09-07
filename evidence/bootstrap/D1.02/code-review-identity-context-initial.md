# D1.02 identity/context 初版独立代码增量审核

审核者：AI，独立代理 `/root/implement_d004`。审核时间：2026-09-07T11:03:18.564413+00:00。

结论：发现三项必须关闭的问题，当前不批准所审增量。本文仅覆盖下列 SHA 对应的 identity.hpp/context.hpp 与相关冻结规格，不是六头、D1.02 整包或任何门禁批准；其余头仍在实现，未重复审查。三项已直接发送实施代理 `/root/implement_review_policy` 并抄报主代理；实施者已确认两项实现缺陷及 Target 授权协议缺口，拟先增加真实反例再修复。本文不把该修复计划记作完成。

本次采用只读静态代码与规格核对，没有编译或运行本代理编写的 C++ PoC，不声称已有运行失败日志。未修改实现、测试、JSON 或 Git，仅新增本记录。后续实际 red/green 及独立复验由下一轮材料补充，原结论按本次快照保留。

## F01｜P2：WorkContext 的资源拥有集合仍有可变外部别名

位置：context.hpp 第 53 行，构造初始化 `resources_(std::move(resources))`，随后把资源的 `r.get()` 缓存到 views_。

参数按值接收 vector，再移动其缓冲到成员。调用者在移动自己的 vector 前保留的 shared_ptr 元素指针仍可指向该已转移元素。合法复现路径为：vector 是资源唯一强拥有者；保存 `auto* slot=&owners.front()` 与 weak_ptr；使用 `std::move(owners)` 构造 WorkContext；再执行 `slot->reset()`。这会移除 WorkContext 内部对应 owner，而 views_ 中仍保留原 ResourceLease 指针；资源可能已经析构。无需解引用悬空指针即可用 weak_ptr 是否过期验证违约。

冻结合同第 3.2 节要求 WorkContext 内部保有授予资源 owner，span 只借用当前调用。修复应从输入集合独立复制共享拥有权，不依赖 vector move 消除旧元素指针。建议反例让原 owner 元素 reset 后 weak_ptr 仍可 lock，且资源直到 WorkContext 结束才释放；同时保留空/合法 owner、预算 charge 失败不改计数的正例。实施者确认将改为 const vector& 输入并复制，在修改行为前运行该反例。

## F02｜P2：AsyncInput 接受了不符合冻结类型的拥有声明

位置：identity.hpp 第 66 行，detail::owning 仅要求 async_ownership 可转换为 AsyncOwnership，之后执行可被重载的相等比较。

冻结合同第 2 节要求可选声明为 `static constexpr AsyncOwnership async_ownership`。当前一个自定义 constexpr 对象只要提供向 AsyncOwnership 的转换就能越过类型检查；其 operator== 甚至可独立决定与 Owning 比较的结果。该字段并不是受审要求的枚举常量，仍可能令 AsyncInput 成立。这不是要求自动反射用户对象图，而是应精确核对已有明确声明的类型与常量值。

建议检查去除顶层 const 后的声明类型精确为 AsyncOwnership，并在约束可判定范围内确认它是常量表达式，最后比较枚举值。缺失、Disallowed、错误自定义转换对象、非 constexpr 声明及标准借用类型均应被拒绝；合法显式 Owning carrier 继续通过。实施者已确认并计划先加入错误转换对象的真实反例。

## F03｜P2：TargetView 尚未表达固定 authority 的建立与重验协议

位置：context.hpp 第 40 行，TargetView 只有可由派生对象自报的 target()/revalidate()；EffectContext、TransitionView 接收其共享指针，没有与 CallerView 相同的配置 authority 身份见证或检查入口。

冻结合同第 3.1 节明确要求 TargetView 的建立和重验沿同一已配置 authority 协议。CallerView 已具备固定端口 owner、belongs_to 和 validate_caller；TargetView 尚无对应 TargetAuthorityPort resolve/validate 或等价边界。任意派生类可以自报任意 ObjectId 和成功 revalidate，现有声明无法通过该视图协议向敏感接收端证明来自其实际配置 authority。这里只认定公开合同的缺口，不声称尚未实现的 Runtime 已出现实际授权绕过。

应先在冻结 API 中具体化目标 authority、来源见证、配置实例比较与当前有效性重验，然后实现并测试同字段伪 Target、自建 authority 下的视图、跨 authority 转交及失效目标均不能通过真实配置端口。不得把对象自身的 valid/revalidate 或 issuer 字段当作发放证明。实施者已认可该缺口，拟具体化协议；截至本轮尚未将新协议视为冻结或通过。

## 其余所审范围

OperationVersion 先按字节预算与规范十进制分量检查，再拥有文本；未把分量窄化为 uint32。各稳定身份使用独立 Tag；CppTypeToken 采用每个精确 T 的非 const 静态字节地址，不持久化或声明 DLL ABI 一致。TypeContract 的 identity/validate 返回型按精确类型检查，void 结果分支与值 Args 分离，标准借用类型有显式排除。

Caller DTO 与凭据分离，CallerView 向保有的 authority 重验，敏感接收辅助函数先检查实例身份；这些声明没有内置永远成功的生产认证器。ReadServices 只暴露 const Reader，EditView 只暴露对应 P::EditPort 与 domain；删除复制抑制隐式移动，视图不提供 Provider/Frame、commit、Host 或任意服务提取。它们依赖冻结合同规定的可信安装端口与同步 Frame 寿命，不被描述成恶意 Native 代码沙箱。未对未落地的 handler 定义、typed binding、端口生产实现或完整测试结果作结论。

## 本次读取快照 SHA-256

以下为实际读取时计算的摘要，问题与行号均针对两头初读版本。写记录时相较这些快照的变化文件：无。

| 文件 | SHA-256 |
| --- | --- |
| `packages/contracts/include/ock/contracts/identity.hpp` | `eb3a821145df38d7d0473dd3a0aac1b66027798805e0bd38f5aa578ea7a49eb8` |
| `packages/contracts/include/ock/contracts/context.hpp` | `34aeebdb9d97a7cd0ee4ab73c61a44303917bbf605133f9db7b96765c52c7480` |
| `docs/contracts/core-contracts-api.md` | `48a8fae8e34d9e2532095a437c6b10088cd54b62c3e84e36cc78d5029c245bf7` |
| `docs/plans/D1.02.md` | `2e45fe3dba30e6604ca8c7d91c1f3b7e298d7ef8a741a99d9a19176d2a19b212` |

# B6：AtomicProvider 调用期权威与内存同步提交边界

状态：已采纳，2026-09-12；适用于 D4.01–D4.04。

## 背景

原 `AtomicProviderPort` 的 `begin(domain)` 只有描述性 domain，`commit(prepared, permit, receiver)` 只有 permit DTO。它不能让多会话 Runtime 把当前 `CallerView` 交给 State 做新读授权，也不能让 provider 用接收端构造的 expected binding 调用真实 `PermitAuthorityPort::consume`。公开 `PreparedCommit` 还不足以证明候选根所有权。若让 provider 在注册时捕获固定 caller 或 ActionAuthorization，同一个注册 provider 无法安全服务后续会话，并会把 Runtime 私有 Policy 对象反向泄漏给 State。

内存 provider 的回报可以在调用栈内确定；未来 Durable provider 允许迟到 callback。Native/managed 同步 StateEdit 若接受任意异步 provider，会在 `commit()` 返回但 callback 未到时无法区分“尚未发布”和“稍后发布”，从而可能错误形成 FailedBeforeApply。

## 决定

CoreContracts 的窄 provider 协议补充以下材料：

- `resolve(target)` 由实际 provider 把受信 anchor target 解析为唯一 `AtomicDomainRef`；Runtime 不猜 provider generation。
- `begin(domain, caller)` 传入本次已验证 caller；State 在创建新 Snapshot 时继续做当前授权，旧 Snapshot 本身不授予新编辑能力。
- `base(frame)` 返回 provider 实际取得的 revision 和 lifecycle generation，协调器据此构造完整 `PreparedIdentity`。
- `commit` 同时接收调用期 `PermitAuthorityPort` 和由协调器保存的 expected `PermitBinding`。State 只把这组受信组合材料交给 `consume`，不从 permit 反取 expected。
- 增加 `InlineAtomicProviderPort`。只有明确保证调用返回前产生唯一 `CommitReport` 的内存 provider 才能进入同步 Native/managed StateEdit；普通 `AtomicProviderPort` 保留 callback 形态，供后续拥有型异步协调器使用。

State 的 `ObjectMemoryProvider` 同时保存私有 `PreparedState`，并以公开 `PreparedCommit` 的对象身份和完整 `PreparedIdentity` 双重核对。公开 DTO、另造相同字段或单独拿到 permit 都不能替换候选 owner。发布后 provider 通过 `PublicationAuthorityPort` 返回同一 StateDomain 预留并激活的证明。

Runtime 只依赖 CoreContracts：注册条目保存类型擦除 provider owner、domain resolver 和已注册 StateEdit thunk；调用期仍经现有 Policy 形成 ActionAuthorization、permit 和 expected binding。State 只依赖 CoreContracts；没有包含 Runtime 私有头或链接 Runtime。

## 后果与验证责任

这是 v0.1 开发 SDK 的合同演进，所有现有 provider 验证消费者必须同步实现新增方法；SDK 版本在 B6 收口提升为 dev.7。D1.02 已提交事实不被改写，但因冻结输入实际变化，本批正式 CODE/SPEC 与受影响合同编译测试必须覆盖该窄协议。

必须验证：错误 target/domain/caller/base 拒绝；相同字段伪造 PreparedCommit 拒绝；permit authority 拒绝零发布；close 与 consume 重入有唯一结果；内存同步 provider 不产生迟到 callback；Runtime Native 与现有 managed InvocationRecord 都形成带有效 PublicationProof 的 StateCommitted。未来 Durable 异步接线不得借 `InlineAtomicProviderPort` 伪造同步完成。

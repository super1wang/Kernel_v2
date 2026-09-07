# resolve授权语义限定规格核查

- actor_type：AI；review_contracts；只读核查，不是实现批准。
- 对照：policy-api修订3 SHA256 `e8a2d6c3f449a88aaef6673609c3b98437e63c35b244c83e4a74a24ee329ecc7` 的159–172、217、233；架构A10（503、505行）。

结论：早期报告第3项所指“目录存在即返回成功”没有满足API无权/不存在同响应。但冻结API没有定义这里的无权判据：完整tuple需要use/operation/owner/field，resolve仅接收CallerView和ObjectId。唯一架构要求最终四方交集并区分静态目标规则与每步实际检查，没有指定resolve专用权限。须局部澄清，不能由实现擅自固定Invoke、写权限或owner=caller来修复。Read Operation仍属于Invoke，非Invoke观察则还需单独正控制。

## 建议的最小合同补充（供主集成选定并冻结）

将resolve明确为“目标候选可见性”预检：可信已安装目录中，须存在一个共同候选 `(use, exact operation, requested target, required_permissions)`，让主体规则、当前会话委托、认证ceiling、精确OperationPolicyInput模块规则、目标规则以及非Invoke时的UsePolicyInput模块规则分别有一条规则完整覆盖该候选。Invoke所需权限取精确操作；非Invoke取明确安装用途。所有来源必须使用同一个use/operation/target/完整所需权限，不能分别选不同用途或拼权限。未安装操作/用途不能成为候选。

这一预检暂不匹配尚未知的执行owner和输出field，其成功只准返回目标身份/生命周期视图，不授权业务操作或执行摘要。它必须在文字上明确具有这一限制，与既有owner_candidate只准候选scan的分层一致。prepare和观察接口仍使用真实operation/owner/目标及逐字段的完整tuple，不能复用resolve成功作为最终allow。目标未知或不满足候选投影统一TargetUnavailable。

这是明确新增的局部消歧建议，不把它冒充原规范已经给出的唯一算法；如主集成选择其他最小规则，也应冻结明确语义和相应正反例。无需变更CoreContracts签名或增加生产索引。

## 应补的最小验证

1. 同会话对A有候选授权，B存在但各方没有共同候选，C不存在：A成功，B/C同拒绝。
2. 只有合法Read Operation授权仍可解析A，不能要求写权限。
3. 只有明确安装的非Invoke观察用途授权、没有Invoke权限时仍能解析A；之后真实owner/field无权的观察仍拒绝，不能借TargetView扩大权限。
4. 主体仅允许use X而模块/目标仅允许use Y，或权限分别散在不同规则：不存在共同候选，解析拒绝。
5. 跨连接caller拒绝，close/revoke先赢后不能发行新的有效视图。

未写源码或测试，未运行反例。原第3项仍应以选定合同后的真实red/green关闭，而非只增加一个Invoke-only判断。

# B6 最终语义精确化修复与重新收口执行方案 v2.0

**日期：2026-09-14**  
**仓库：`super1wang/Kernel_v2`**  
**分支：`work/d0-kernel-baseline`**  
**制定基线 HEAD：`55bf3369dc34934bf2ffc84b7c94981e6646cdcd`**  
**当前生产代码来源：`3abde2ebc1f11acccbad106142c96760a95f5d47`**  
**执行粒度：L4.5**  
**执行对象：Astra 规划 / Terra、Codex 实施均适用**

---

# 0. 执行结论

B6 不需要推倒重做。

保留：

- B6 / D4.01–D4.04 历史 Passed；
- C1–C6 历史 closure Passed；
- Atomic `RequireExplicitRevision` 注册约束；
- CommitClaim 提交仲裁；
- 每提交独立 PublicationProof；
- State/Atomic 共用 Registry / Policy / Permit / Publication；
- Runtime 不依赖 State；
- State 私有 immer；
- 现有 History / Snapshot / Atomic CandidateBindings 架构。

但当前 **B6 final precision 不允许继续标记 Code Passed**。

执行状态必须调整为：

```text
B6 / D4.01–D4.04               Historical Passed
B6 audit closure C1–C6         Historical Passed
B6 final precision v1          Superseded
B6 semantic precision v2       ChangesRequested
B6 formal validation v2        Pending
B7 entry                       HOLD
```

本轮只关闭：

```text
F1  abandon / cancel claim provenance 混用
F2  Native pre-handler failure 伪造 execution_accepted
F3  State dispatch exception 丢失 business_entered 真实事实
F4  Atomic/State 在 prepare 前取消无法形成精确 CancelledBeforeApply
F5  managed 已 Accepted 后仍可能退回 Rejected
```

完成后重新冻结一个新的生产来源。

**不得继续把 `3abde2e` 当作最终放行来源。**

---

# 1. 本轮硬性范围

## 1.1 允许修改

优先只允许修改：

```text
packages/state/include/ock/state/domain.hpp

packages/runtime/include/ock/runtime/detail/invocation.hpp

tests/unit/state_roots/runtime_state.cpp
```

只有确有必要时才允许修改：

```text
packages/state/include/ock/state/provider.hpp
packages/runtime/include/ock/runtime/registry.hpp
packages/runtime/include/ock/runtime/atomic.hpp
```

文档与证据：

```text
docs/progress.md
docs/plans/...
docs/reviews/...
docs/validation/...
evidence/B6/...
SDK manifest / public-header hash 生成结果
```

## 1.2 默认禁止修改

除非实施中证明现有合同无法满足，否则禁止修改：

```text
packages/contracts/include/ock/contracts/outcome.hpp
packages/contracts/include/ock/contracts/context.hpp
packages/contracts/include/ock/contracts/identity.hpp
```

尤其禁止：

- 新增第二套 Outcome；
- 新增第二套 State executor；
- 新增第二个 CommitClaim；
- 新增脚本/Plan IR；
- 新增持久化；
- 新增 B7 功能；
- 为修复测试而放宽 Outcome validator；
- 增加新的公共 Cancel 状态枚举；
- 增加新的 CoreContracts virtual port；
- 根据 ErrorCode 猜测 `CancelWon`；
- 根据函数返回时 `stop_requested()==true` 直接猜测 `CancelWon`；
- 提高任何 footprint 阈值。

若发现必须修改 CoreContracts 才能完成，应停止编码并将状态改为：

```text
NeedsDesignReview
```

不得擅自扩大公共协议。

---

# 2. 开工前首先纠正旧冻结指令

## Step B6V2-00：撤销错误的“代码已 Passed”状态

### 修改目标

当前 `55bf3369` 明确要求：

```text
冻结 3abde2e
不改 P2-A/P2-B
只刷新 footprint
```

本轮审阅已经证明该决定失效。

### 必须修改

`docs/progress.md`

将：

```text
B6 final precision：代码 Passed；正式验证 Pending
```

更新为：

```text
B6 semantic precision v2：ChangesRequested
B6 final precision v1：保留历史记录，但被 v2 审阅结论 supersede
B7：HOLD
```

原 `3abde2e` footprint refresh 批准保留历史，不删除。

追加说明：

```text
原 footprint 批准仅绑定 3abde2e。
新的生产代码提交产生后，任何绑定 3abde2e 的新 footprint
测量均不得作为最终 v2 放行证据。
```

### 禁止

不得：

- 删除旧 Passed 报告；
- 修改旧 acceptance 让它看起来从未通过；
- 覆盖历史 C1–C6；
- 把历史 Passed 改成 Failed。

历史事实与当前状态必须分开。

### Step 完成条件

```text
[ ] 当前状态明确为 ChangesRequested
[ ] B7 HOLD
[ ] 3abde2e 不再是最终冻结目标
[ ] 旧证据仍保持原文
```

建议提交：

```text
B6: reopen semantic precision closure
```

---

# 3. 先建立统一事实模型

实施任何代码前，执行者必须接受以下事实表。

## 3.1 application 状态

B6 State 路径只允许：

```text
Not entered business
        ↓
Entered business
        ↓
Prepared candidate
        ↓
ClaimWon
        ↓
Published
```

一旦 Published：

```text
禁止因为
stop
encoding failure
observation failure
late cancellation
required-record failure
```

重新解释成：

```text
FailedBeforeApply
CancelledBeforeApply
Rejected
```

---

# 4. 最终 Outcome 真值表

这是本轮实现的最高优先级合同。

## 4.1 Native

### N0：尚未进入 State 执行

例如：

- 无效绑定；
- 参数非法；
- 线程不允许；
- work budget 不合法；
- stop 在调用进入前已经请求。

结果：

```text
Rejected
```

---

### N1：Action/State 前置已经开始，但业务 Handler 未进入

例如：

- provider/domain 获取失败；
- 缺 expected_state；
- stale revision；
- stale lifecycle；
- Atomic membership 前置失败；
- 业务前异常。

无真实取消：

```text
Rejected
```

原因：

```text
Native 没有 ExecutionRef/Accepted 事实
business_entered = false
execution_accepted = false
```

不能为了通过 Outcome validator 伪造：

```text
execution_accepted=true
```

---

### N2：Handler 已真正进入，随后失败，未应用

```text
Completed
  └─ FailedBeforeApply

business_entered = true
execution_accepted = false
decision = NotReached
no_application_proven = true
```

---

### N3：真实取消在 application/claim 前赢

```text
Completed
  └─ CancelledBeforeApply

decision = CancelWon
no_application_proven = true
execution_accepted = false
business_entered = 依实际情况
```

---

### N4：发布成功

```text
Completed
  └─ StateCommitted
```

之后 stop 到达不能改变结果。

---

# 5. Managed 真值表

一旦：

```text
submit → Accepted{ExecutionRef}
```

之后该 ExecutionRef 的结果**禁止重新退化为顶层 `Rejected`**。

## 5.1 已 Accepted，Handler 未进入，普通失败

```text
Completed
  └─ FailedBeforeApply

execution_accepted = true
business_entered = false
decision = NotReached
```

## 5.2 已 Accepted，Handler 进入后失败

```text
Completed
  └─ FailedBeforeApply

execution_accepted = true
business_entered = true
decision = NotReached
```

## 5.3 已 Accepted，真实取消胜出

```text
Completed
  └─ CancelledBeforeApply

execution_accepted = true
decision = CancelWon
```

## 5.4 已 Accepted，最终 Published

```text
Completed
  └─ StateCommitted
```

---

# 6. F1：彻底分离 abandon 与 cancellation

## Step B6V2-10

目标文件：

```text
packages/state/include/ock/state/domain.hpp
```

目标函数：

```cpp
StateDomain<T>::abandon(...)
StateDomain<T>::close(...)
StateDomain<T>::commit(...)
StateDomain<T>::finish_failed(...)
```

---

## 6.1 当前问题

当前：

```cpp
abandon(prepared)
```

会执行：

```cpp
prepared->claim().cancel();
```

但 `abandon` 实际语义只是：

> 放弃这个 prepared reservation / candidate。

它不等价于：

> 某个可信取消方赢得了提交仲裁。

因此普通失败清理可能污染：

```text
CommitClaim = cancelled
```

最终导致：

```text
普通 BudgetExceeded
     ↓
CancelledBeforeApply
     ↓
CancelWon
```

---

# 7. F1 推荐实现

## 7.1 修改 `abandon()`

目标语义：

```text
abandon = 释放精确 reservation
        ≠ cancel
```

修改后：

```text
锁内：
    验证 prepared 是 active owner
    验证 phase == Ready
    清除 active reservation

锁外：
    正常释放 owner

禁止：
    prepared->claim().cancel()
```

即：

```cpp
abandon()
    must NOT mutate CommitClaim
```

---

## 7.2 保留真实 cancel 的地方

以下位置继续允许：

```cpp
claim.cancel()
```

### A. `close()`

当 domain close 与正在进入 commit claim 的提交竞争时：

```text
close
   ↘
    CommitClaim
   ↗
commit
```

这是实际仲裁。

### B. Policy `ActionAuthorization::consume_claimed()`

Action 已被真正取消：

```text
ActionStatus::Cancelled
        ↓
claim.cancel()
```

这是可信取消事实。

---

## 7.3 禁止修法

禁止新增：

```cpp
if(error == BudgetExceeded)
    cancelled=false;
```

禁止：

```cpp
if(error != Cancelled)
    cancelled=false;
```

这是按 ErrorCode 猜时序。

正确方式必须是：

```text
取消事实来自真正 cancel 操作；
cleanup 永远不制造 cancel。
```

---

# 8. F1 精确反例

在开发过程中先增加测试，再改代码。

## T-F1-01：普通 abandon 不取消 claim

步骤：

```text
1. 创建 domain
2. snapshot revision=0
3. prepare candidate
4. 调用 abandon(prepared)
5. 检查：
   abandon == true
   claim.claimed == false
   claim.cancelled == false
6. 再使用同 prepared commit
7. 必须失败
8. revision 保持 0
```

---

## T-F1-02：无效提交清理不能产生 CancelWon

构造：

```text
真实 prepared
真实 permit
真实 authority
故意传入错误 lifecycle/target binding
```

触发：

```text
StateDomain::commit()
    → InvalidCandidate
    → abandon
```

必须得到：

```text
KnownNotCommitted
cancelled_before_claim == false
```

---

## T-F1-03：真实 Action cancel 必须仍产生 CancelWon

```text
prepare
issue permit
ActionAuthorization::cancel()
commit
```

必须：

```text
claim.cancelled == true
CommitReport.cancelled_before_claim == true
revision unchanged
```

---

## T-F1-04：revoke / expiry 不是 CancelWon

分别构造：

```text
permission revoke
deadline expiry
```

要求：

```text
KnownNotCommitted
cancelled_before_claim == false
```

---

# 9. F2 + F5：统一 Native / Managed 的失败分类

目标文件：

```text
packages/runtime/include/ock/runtime/detail/invocation.hpp
```

---

# 10. 新增两个内部分类辅助语义

不要求一定做成函数，但代码结构必须等价。

## 10.1 `state_pre_handler_failure`

逻辑：

```text
if managed:
    Completed(
        FailedBeforeApply,
        execution_accepted=true,
        business_entered=false
    )
else:
    Rejected(error)
```

## 10.2 `state_business_failure`

逻辑：

```text
Completed(
    FailedBeforeApply,
    execution_accepted=managed,
    business_entered=true
)
```

不要继续使用：

```cpp
managed || !business_entered
```

去制造 `execution_accepted`。

---

# 11. 修复当前错误表达式

必须删除语义：

```cpp
managed || !call.business_entered
```

特别是类似：

```cpp
failed(
    error,
    call.business_entered,
    call.before_apply,
    managed || !call.business_entered
)
```

禁止 Native 因为：

```text
business_entered == false
```

反而得到：

```text
execution_accepted == true
```

---

# 12. 修复 managed Accepted → Rejected 回退

当前 State 路径以下位置都必须审阅：

```text
state->targets.empty()
resolve_domain()
validate_state_request()
projection / actual target
atomic_membership()
atomic registry/generation validation
resource validation
session->prepare()
ActionAuthorization::issue()
current_expected_binding()
new_commit_ids()
```

当前很多位置执行：

```cpp
return rejected(...)
```

对于：

```text
managed == true
```

这是错误的。

---

# 13. managed 分类规则

所有已经进入：

```cpp
bound_.run(..., true, ...)
```

的 StateEdit managed 调用，应认为：

```text
ExecutionRef 已 Accepted
```

因此：

### 非取消普通前置失败

```text
Completed
 └ FailedBeforeApply

execution_accepted=true
business_entered=false
```

### 取消

```text
Completed
 └ CancelledBeforeApply

execution_accepted=true
decision=CancelWon
```

### Native 同一位置

仍然：

```text
Rejected
```

只要：

```text
business_entered=false
且尚无真实 CancelWon
```

---

# 14. 不要大范围修改 Read

`NativeBound::run()` 同时处理：

```text
Read
StateEdit
```

本轮禁止因为方便而重写整个函数。

建议：

```text
Common admission
      ↓
Read 继续现有行为
      ↓
StateEdit 使用新的 state-specific classification
```

除非发现 Read 本身独立违反既有 Passed 合同，否则不扩大本轮范围。

---

# 15. F3：异常也必须保留真实 business_entered

当前问题：

```text
entered = true
NativeAccess::dispatch(...)
```

外部 catch 只知道：

```text
进入 dispatch
```

不知道：

```text
业务 handler 是否真正进入
```

这两个事实不能等价。

---

# 16. F3 推荐实现

不要重写 `state_edit_native()` 主流程。

在 StateEdit 分支调用：

```cpp
NativeAccess::dispatch(...)
```

周围增加 State 专属异常转换：

```text
try:
    dispatch(..., &call)

catch bad_alloc:
    call.failure = BudgetExceeded

catch ...:
    call.failure = HandlerException
```

然后统一通过：

```text
call.business_entered
```

决定结果。

因为 `state_edit_native()` 已经只在真正调用 handler 前执行：

```cpp
call.business_entered = true;
```

所以：

```text
membership/provider/base/revision 阶段异常
    → false

handler 内异常
    → true
```

---

# 17. F3 禁止

禁止继续使用：

```cpp
entered
```

作为 State 的业务进入事实。

`entered` 可以继续服务 Read 或其他既有路径，但：

```text
State business_entered
```

唯一来源必须是：

```text
StateNativeCall.business_entered
```

---

# 18. F3 测试

## T-F3-01：Atomic membership 抛 `bad_alloc`

增加测试专用：

```cpp
Result<AtomicMembership> throwing_membership(...)
{
    throw std::bad_alloc{};
}
```

要求：

### Native

```text
Rejected(BudgetExceeded)
handler calls == 0
revision unchanged
```

### Managed

```text
Accepted
→ Completed(FailedBeforeApply)

execution_accepted=true
business_entered=false
revision unchanged
```

---

## T-F3-02：provider begin 前置异常

若现有测试框架可低成本构造 throwing provider，则增加：

```text
begin() throws
```

要求同上。

若实现该 fixture 会大幅扩大代码，可由 T-F3-01 作为业务前异常主证明。

---

## T-F3-03：业务 Handler 真正进入后抛异常

新增 test handler：

```text
++handler_entries
throw runtime_error
```

必须：

### Native

```text
Completed(FailedBeforeApply)
business_entered=true
execution_accepted=false
```

### Managed

```text
Completed(FailedBeforeApply)
business_entered=true
execution_accepted=true
```

共同：

```text
revision unchanged
```

---

# 19. F4：prepare 前取消必须有可信事实来源

这是本轮最容易再次修错的位置。

禁止：

```cpp
if(options.stop.stop_requested())
    CancelWon;
```

因为这仍然只是观察 stop 状态。

---

# 20. 取消的三个可信阶段

本轮定义：

## C0：Action 尚未存在

例如：

```text
managed execution 已 Accepted
随后 stop token 被 owning execution 请求
State Action 尚未 prepare
```

这是 execution owner 的实际取消请求。

Native 尚未进入执行时：

```text
Rejected(Cancelled)
```

Managed 已 Accepted：

```text
CancelledBeforeApply
```

---

## C1：ActionAuthorization 已存在，commit claim 尚未开始

必须通过：

```text
ActionAuthorization::cancel()
```

成功确认取消，而不是只观察 token。

---

## C2：commit 已进入 claim 仲裁

唯一来源：

```text
CommitReport.cancelled_before_claim
```

由：

```text
ActionAuthorization::consume_claimed()
        +
CommitClaim
```

产生。

---

# 21. F4 最小实现策略

不新增公共 API。

保留现有：

```cpp
std::stop_callback commit_cancel(
    options.stop,
    [owner=*action]() noexcept {
        (void)owner->cancel();
    }
);
```

但在 State handler/Atomic 提前失败后，如果：

```text
stop 已请求
且未进入 commit report
```

不能直接猜 `CancelWon`。

执行一次**可信确认**：

```text
ActionAuthorization::cancel()
```

判断结果。

### cancel 成功

说明：

```text
Action 尚未 consumed
取消已经实际写入 Policy owner
```

于是：

```text
CancelWon
```

### cancel 返回 AlreadyConsumed

不得覆盖：

```text
StateCommitted / ClaimWon
```

### cancel 返回其他 Policy 失败

不得谎称 CancelWon。

使用原失败事实。

---

# 22. 取消与普通错误同时发生时的优先级

本轮固定：

```text
如果 Action 仍未 consumed，
并且可信 cancel 成功，
则 cancel 是最后一次 application 前控制仲裁赢家。
```

因此：

```text
CancelledBeforeApply
```

优先于同时存在的普通未应用错误。

但：

```text
ClaimWon / Published
```

永远优先于之后的 stop。

---

# 23. Atomic 中途取消

当前：

```cpp
Input<P>::apply()
```

在每个成员前检查：

```cpp
work.stop_requested()
```

该行为保留。

不要求重写 Atomic。

目标流程变为：

```text
member 1 enters
    ↓
stop requested
    ↓
ActionAuthorization cancel
    ↓
member 2 前 stop check
    ↓
Atomic handler 提前返回
    ↓
State invocation 发现 handler failure
    ↓
向 ActionAuthorization 确认 cancellation
    ↓
CancelledBeforeApply / CancelWon
```

而不是：

```text
FailedBeforeApply / NotReached
```

---

# 24. F4 精确测试

## T-F4-01：Native 两成员 Atomic，中途 cancel

步骤：

```text
1. valid expected_state
2. member1 进入
3. member1 return 前 request_stop()
4. member2 不允许进入
```

要求：

```text
member1 entries = 1
member2 entries = 0

Completed
 └ CancelledBeforeApply

decision = CancelWon
business_entered = true
execution_accepted = false

revision unchanged
```

---

## T-F4-02：Managed 两成员 Atomic，中途 cancel

要求：

```text
submit → Accepted(ref)

wait(ref) → Terminal

result(ref):
  Completed
    CancelledBeforeApply

execution_accepted=true
business_entered=true
decision=CancelWon

revision unchanged
```

---

## T-F4-03：Atomic assertion 普通失败

不请求 stop。

要求：

```text
FailedBeforeApply
decision != CancelWon
```

防止把：

```text
ContractsErrc::Rejected
```

自动解释为 cancel。

---

## T-F4-04：第一成员后 revoke

要求：

```text
member2 = 0
not StateCommitted
not CancelWon
revision unchanged
```

---

## T-F4-05：第一成员后 expiry

同样：

```text
not CancelWon
```

---

## T-F4-06：claim/publish 后 late stop

要求：

```text
StateCommitted
```

任何：

```text
CancelledBeforeApply
FailedBeforeApply
```

均视为 blocker。

---

# 25. F5 专项：Accepted 不可回退

新增一组专门测试，不允许只依赖 stale revision。

## T-F5-01：Managed stale revision

```text
submit
  → Accepted(ref)

worker start
  → stale expected revision

result(ref)
  → Completed(FailedBeforeApply)
```

必须：

```text
execution_accepted=true
business_entered=false
```

禁止：

```text
Rejected
```

---

## T-F5-02：Managed stale lifecycle

同上。

---

## T-F5-03：Managed Atomic membership 前置失败

要求：

```text
Accepted
→ FailedBeforeApply
```

而非：

```text
Accepted
→ Rejected
```

---

## T-F5-04：Managed cancel 发生在 Action prepare / permit 前后边界

利用现有 worker/resource gate 固定时序。

验证：

```text
Accepted
→ CancelledBeforeApply
```

不能得到：

```text
Rejected(Cancelled)
```

---

# 26. 本轮完整测试真值矩阵

| 场景 | Native | Managed |
|---|---|---|
| 调用进入前 invalid input | Rejected | submit 前拒绝 |
| State pre-handler 普通失败 | Rejected | FailedBeforeApply |
| missing expected_state | Rejected | FailedBeforeApply |
| stale revision | Rejected | FailedBeforeApply |
| stale lifecycle | Rejected | FailedBeforeApply |
| pre-handler exception | Rejected | FailedBeforeApply |
| handler entered + failure | FailedBeforeApply | FailedBeforeApply |
| cancel before apply | CancelledBeforeApply | CancelledBeforeApply |
| revoke before apply | FailedBeforeApply | FailedBeforeApply |
| expiry before apply | FailedBeforeApply | FailedBeforeApply |
| cleanup/abandon | 非 CancelWon | 非 CancelWon |
| CommitClaim cancel wins | CancelledBeforeApply | CancelledBeforeApply |
| claim/publish wins | StateCommitted | StateCommitted |
| stop after publish | StateCommitted | StateCommitted |

---

# 27. 推荐代码修改顺序

严格按以下顺序。

```text
B6V2-00  状态纠正
    ↓
B6V2-10  F1 abandon / cancel provenance
    ↓
B6V2-20  State failure classification helper
    ↓
B6V2-30  F3 dispatch exception preservation
    ↓
B6V2-40  F4 authoritative pre-claim cancel
    ↓
B6V2-50  F5 managed Accepted closure
    ↓
B6V2-60  direct regression
    ↓
B6V2-70  freeze source
    ↓
B6V2-80  formal validation
    ↓
B6V2-90  footprint
    ↓
B6V2-100 review + acceptance + freeze
```

F2–F5 可以物理位于同一个 `invocation.hpp` patch，但实施时必须逐项验证。

---

# 28. 推荐生产提交边界

不要产生大量微型生产提交。

## Commit 1

```text
B6: separate abandon from cancel arbitration
```

包含：

```text
domain.hpp
F1 tests
```

---

## Commit 2

```text
B6: make state pre-apply outcomes exact
```

包含：

```text
invocation.hpp
必要时 registry.hpp
F2/F3/F4/F5 tests
```

---

## Commit 3

仅当确有必要：

```text
B6: close atomic cancellation edge cases
```

否则不要人为拆出第三个生产提交。

---

# 29. 开发期 Red → Green 流程

每项修复执行：

```text
写最小反例
    ↓
运行并证明旧代码失败
    ↓
修改生产代码
    ↓
只跑当前反例
    ↓
通过
    ↓
跑 B6 State 直接集
```

旧代码失败属于开发诊断。

不得把不同源码版本的局部测试拼成正式 acceptance。

---

# 30. 开发直接集

生产修复全部完成后，至少覆盖：

```text
State runtime
State commit/snapshot
Atomic
Outcome
Policy cancel/revoke/expiry
Native State
Host managed State
State publication proof
```

必须使用仓库当前正式测试入口。

不要凭本文件猜 CTest target 名称。

执行者应从：

```text
历史 B6 commands.json
B6 closure delivery
ctest -N
CMakePresets.json
```

恢复真实命令。

---

# 31. Debug 收口

Debug 必须覆盖所有新增反例：

```text
F1-01 ~ F1-04
F3-01 ~ F3-03
F4-01 ~ F4-06
F5-01 ~ F5-04
```

加现有：

```text
Atomic RequireExplicitRevision
same-base conflict
publication proof interleaving
revoke
expiry
resource
Host submit/wait/result/cancel
```

要求：

```text
100% Passed
```

任何一个失败：

```text
停止 Release / ASan / footprint
```

---

# 32. Release

Release 至少重新执行：

```text
State runtime precision
Atomic precision
Policy cancellation
State commit/provider
Host State
```

若现有正式 `S_changed/S_required` 包含更多测试，按既有列表执行。

禁止为了减少执行量自行缩小历史定义的 S_required。

---

# 33. ASan

ASan Debug 至少覆盖：

```text
State runtime precision
State provider/domain
Atomic
Host managed State
```

重点确认：

```text
abandon 后无 stale owner
stop_callback 生命周期安全
Action owner 生命周期安全
prepared owner 无 UAF
异常路径无泄漏
```

要求：

```text
ASan error = 0
```

---

# 34. SDK / 安装边界

由于本轮再次修改 Runtime/State 头文件：

必须重新：

```text
public header scan
component closure
SDK manifest/hash
installed State consumer
Runtime-only consumer
Embedded consumer
```

不得手工修改 hash。

必须调用既有生成/验证工具。

---

# 35. Runtime-only 依赖门禁

必须确认：

```text
Runtime-only:
    expected
    + 原已批准 Runtime 依赖
```

没有新增：

```text
State
immer
```

---

# 36. Embedded 门禁

必须确认 Embedded 依赖仍符合原冻结依赖集合。

若发现：

```text
Runtime → State
```

或：

```text
Embedded 因本修复新增 immer
```

直接：

```text
NO-GO
```

---

# 37. 最终生产来源冻结

所有代码和测试修改结束后：

```text
git status clean
完整 direct set 通过
```

形成：

```text
FINAL_CODE_SHA=<new sha>
```

从这一步开始：

```text
禁止再改 packages/
禁止再改 tests/ 中参与正式输入的文件
禁止再改 footprint 方法输入
```

若修改：

```text
FINAL_CODE_SHA 作废
正式验证重新开始
```

---

# 38. 旧 footprint 批准处理

`55bf3369` 的 footprint refresh 绑定：

```text
3abde2e
```

新的代码修复后：

```text
不得作为最终 B6 v2 机器证据。
```

处理规则：

### 如果尚未运行

直接：

```text
Superseded before execution
```

### 如果已经运行

保留：

```text
Historical / non-final
```

不得与新提交结果拼接。

---

# 39. 新 footprint 来源批准

在 `FINAL_CODE_SHA` 冻结后重新做一次**来源批准**。

沿用原：

```text
Native 7 modes
Embedded 3 configs
Release startup
每项 6 组 ABBA
原绝对预算
原 paired budget
原 allocation window
原线程上界
原启动口径
```

禁止：

```text
修改数值
修改阈值
修改 pilot
提高预算
改变采样算法
```

除非正式重新开预算审批，本轮不得做。

---

# 40. 为什么 footprint 必须最后运行

本轮修改触及：

```text
Runtime hot State path
State domain control path
```

并且之前 `CommitReport` 公共布局变化本就已触发 footprint refresh。

因此：

```text
任何在最终代码冻结前测得的 footprint
    ≠ 最终放行证据
```

---

# 41. Native footprint

使用既有 7 模式。

要求所有：

```text
occupancy
allocation
latency（适用配置）
thread count
zero-allocation window
```

均不超过已批准阈值。

ABBA：

```text
Baseline
Candidate
Candidate
Baseline
```

必须继续使用既有方法，不允许变成简单 A/B。

---

# 42. Embedded footprint

保持既有三个配置：

```text
Debug
Release
ASan
```

以及：

```text
Release startup
```

继续验证：

```text
occupancy
allocation
thread structure
startup
shutdown recovery
```

---

# 43. 正式 evidence

建议新建：

```text
evidence/B6/semantic-precision-<FINAL_SHORT_SHA>/
```

不得覆盖：

```text
evidence/B6/final-*
evidence/B6/closure-a03304a/
旧 final precision evidence
```

---

# 44. evidence 最少内容

```text
source.json
commands.json
environment.json
expected.json
executed.json
report.json
acceptance.json

debug/
release/
asan/
install/
sdk/
native-footprint/
embedded-footprint/
```

如果仓库现有 schema 使用不同文件名：

```text
严格复用现有 schema
```

不要自行创造并行证据格式。

---

# 45. 来源一致性

正式 acceptance 必须绑定：

```text
FINAL_CODE_SHA
tree SHA
build preset
compiler
CMake version
test expected list
SDK manifest
footprint method digest
```

正式运行前检查：

```text
所有生产输入逐字节匹配 FINAL_CODE_SHA
```

---

# 46. 不允许拼接

禁止：

```text
Debug @ SHA-A
Release @ SHA-B
ASan @ SHA-C
footprint @ SHA-D
```

拼成：

```text
Passed
```

正式验证必须遵循仓库已有“同一最终来源”规则。

---

# 47. CODE Review

新的 CODE review 必须专门回答：

## F1

```text
abandon 是否完全不再制造 cancellation？
```

## F2

```text
Native 是否还有伪 execution_accepted=true？
```

## F3

```text
所有 State 异常分支是否保留真实 business_entered？
```

## F4

```text
CancelWon 是否只来自可信控制/仲裁事实，
而非 ErrorCode/最终 stop 猜测？
```

## F5

```text
Accepted ExecutionRef 是否还有运行阶段 Rejected 回退？
```

---

# 48. SPEC Review

必须核对：

```text
Architecture A04–A07
B6 closure ADR
B6 final precision 原计划
本 v2 修订计划
Outcome validator
```

并明确写：

```text
v2 supersedes v1 final-precision release decision
but does not invalidate historical B6 closure evidence
```

---

# 49. 最终 acceptance 条件

只有以下全部为真：

```text
[ ] F1 全部反例通过
[ ] F2 全部反例通过
[ ] F3 全部反例通过
[ ] F4 全部反例通过
[ ] F5 全部反例通过

[ ] Native/Managed truth table 全匹配
[ ] no false CancelWon
[ ] no false execution_accepted
[ ] no false business_entered
[ ] Accepted 不回退 Rejected
[ ] revoke/expiry 不伪报 CancelWon
[ ] late stop 不覆盖 StateCommitted

[ ] Debug 正式影响集 Passed
[ ] Release 正式影响集 Passed
[ ] ASan 正式影响集 Passed
[ ] State installation Passed
[ ] SDK boundary Passed
[ ] Runtime-only boundary Passed
[ ] Embedded boundary Passed

[ ] Native footprint Passed
[ ] Embedded footprint Passed

[ ] SPEC review Approved
[ ] CODE review Approved

[ ] acceptance 绑定 FINAL_CODE_SHA
```

才允许：

```text
B6 semantic precision v2 = Passed / Frozen
B7 entry = GO
```

---

# 50. 任一以下情况直接 NO-GO

```text
普通 abandon → CancelWon

BudgetExceeded → CancelWon
且没有真实 cancel

revoke → CancelWon

expiry → CancelWon

Native stale revision
→ execution_accepted=true

Native pre-handler failure
→ managed.dispatch

managed Accepted
→ Rejected

业务前异常
→ business_entered=true

handler 已进入
→ business_entered=false

member1 后 stop
→ member2 仍进入

cancel 在 claim 后到达
→ StateCommitted 被覆盖

Runtime-only 新增 State/immer

ASan 新增错误

footprint 超批准预算

正式 evidence 混用两个生产 SHA
```

---

# 51. 文档最终状态

正式 Passed 后才修改：

`docs/progress.md`

为：

```text
B6 final semantic precision v2
Passed / Frozen

Production source:
<FINAL_CODE_SHA>

Historical B6 closure:
Passed / preserved

B7 entry:
GO
```

新建：

```text
docs/validation/B6-final-semantic-precision-delivery.md
```

不要覆盖旧：

```text
B6-final-precision-delivery.md
```

旧文件增加状态引用即可：

```text
Superseded for final release decision by v2.
Historical machine facts preserved.
```

---

# 52. 推荐最终提交序列

理想情况下最终历史类似：

```text
55bf3369
    ↓
<docs>
B6: reopen semantic precision closure
    ↓
<code>
B6: separate abandon from cancel arbitration
    ↓
<code>
B6: make state pre-apply outcomes exact
    ↓
<optional code>
B6: close atomic cancellation edge cases
    ↓
FINAL_CODE_SHA
    ↓
<machine evidence + review docs only>
B6: archive semantic precision validation
    ↓
<docs only>
B6: freeze semantic precision closure
```

最后两个提交不得修改 production tree。

---

# 53. Terra / Codex 执行规则

执行者必须：

1. 先读取本方案。
2. 再读取当前 `docs/progress.md`。
3. 只按 F1–F5 修改。
4. 不重新设计 State。
5. 不重写 Atomic。
6. 不进入 B7。
7. 不修改 footprint 阈值。
8. 不删除失败证据。
9. 不用旧成功结果替代新源码验证。
10. 每完成一个 F 项立即运行最小反例。
11. 全部 F 项 Green 后再进入正式机器验证。
12. 最终代码冻结后才能运行正式 footprint。
13. 测量后发现需要改生产代码，则全部最终 source-bound evidence 作废重跑。

---

# 54. 执行者交付报告格式

最终必须返回：

```text
1. FINAL_CODE_SHA
2. 实际修改文件
3. F1 修复说明
4. F2 修复说明
5. F3 修复说明
6. F4 修复说明
7. F5 修复说明
8. 新增反例列表
9. Debug 结果
10. Release 结果
11. ASan 结果
12. installation / SDK 结果
13. Runtime-only / Embedded 结果
14. Native footprint
15. Embedded footprint
16. SPEC review
17. CODE review
18. acceptance 路径
19. 是否存在残余 blocker
20. 最终 GO / NO-GO
```

不接受仅返回：

```text
“所有测试通过”
```

---

# 55. 本轮 Definition of Done

本轮真正完成的定义不是：

```text
代码能编译
```

也不是：

```text
runtime_state 通过
```

而是：

> 对每一次 StateEdit，内核都能准确回答五个问题：
>
> 1. Execution 是否真的已经 Accepted；
> 2. 业务 Handler 是否真的进入；
> 3. application/claim 是否真的到达；
> 4. cancel 是否真的赢得控制/提交前仲裁；
> 5. State 是否真的 Published。

这五个事实不得由：

```text
ErrorCode
最终 stop 状态
cleanup side effect
调用函数是否被 dispatch
```

间接猜测。

只有这些事实与 Outcome 完全一一对应，并在同一最终源码上通过 Debug / Release / ASan / install / SDK / footprint，B6 才能真正：

```text
Passed / Frozen
```

并允许：

```text
B7 = GO
```

## 本地执行补充（开工前）

实际开工 HEAD 为 `28b6054`；相对制定基线 `55bf336` 无生产代码变化。保留已完成 v1 footprint 原文，仅作为旧来源历史事实。本轮修复落实 A05/A09 已有事实合同，不新增公共协议；无需回退历史。使用现有 runtime_state 测试与正式收集入口，先直接反例、后同源正式集与 footprint。


### 开工期固定验证映射

正式三配置各 31 项，名单为 `tests/manifests/b6-semantic-v2.expected.json`，矩阵为 `tests/runs/b6-semantic-v2-matrix.json`。F1–F5 与 Host 流程均在既有 `T06.state.runtime_native` 内增加精确反例；State snapshot/commit/Atomic、Policy cancel/revoke/expiry、Outcome、Native/Managed、安装/SDK 作为影响回归。没有改变历史 S_required，只为本次修复建立独立增量集。

架构 A05/A09 已要求真实 Accepted 与业务进入事实。本轮属于合同实现纠偏，无新公共协议或重大架构决策，不新增 ADR。SDK 仓库没有独立摘要生成 CLI：使用既有 `tools.evidence.common.sha_file` 自动重算受影响头摘要并调用 `tools/architecture/check.py` 验证，不手写摘要常量。

审阅补充：Managed `InvocationRecord::store()` 原已有拒绝回退兜底；F5 修复的是 State 执行层本身的分类及普通 expiry 被兜底按错误码解释为取消的问题，不能把原实现描述为所有对外结果必然 Rejected。保留 Read 的既有兜底，不扩大重写。

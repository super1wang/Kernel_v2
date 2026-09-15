# B6 最终 Expiry / Cancel 线性化收口执行文档 v1.0

**日期：2026-09-14**  
**仓库：`super1wang/Kernel_v2`**  
**分支：`work/d0-kernel-baseline`**  
**当前 HEAD：`e87808263465b8a75bd7a7a76d425f299237da2f`**  
**当前冻结生产来源：`912cb7554cd4c78ea9bf6db5615a2875d215e5ed`**  
**执行粒度：L4.5**  
**修复编号：B6-F6 / Pre-start Expiry-Cancel Linearization Closure**

---

# 0. 执行结论

B6 主体架构、历史 C1–C6、F1–F5 修复以及 `912cb755` 已完成的机器验证全部保留。

本轮**不重新设计 B6**，只修复最后一个 Runtime 私有控制竞态：

```text
F6-A
deadline 在：
Execution Accepted
        ↓
slot.execution 安装
之间到达

当前可能：
expiry → slot.expired=true
      → install
      → cancel()
      → CancelWon

正确：
expiry → expire()
      → FailedBeforeApply / NotReached
```

以及同根问题：

```text
F6-B
Scheduler 已由 expiry 终止
        ↓
迟到 cancel()
        ↓
cancel_requested_=true
        ↓
completed() 读取 true
        ↓
错误投影成 CancelWon
```

正确原则：

> **State pre-start Outcome 必须由真正赢得 Scheduler 线性化仲裁的 terminal cause 决定，而不是由“是否曾经有人请求过 cancel”决定。**

执行前状态必须临时调整为：

```text
B6 / D4.01–D4.04             Historical Passed
B6 C1–C6                     Passed
B6 semantic precision v2     Reopened / ChangesRequested
912cb755 validation          Historical Passed for tested scope
B7 entry                     HOLD
G4                           NotStarted
```

完成新源码验证后才能恢复：

```text
B6 semantic precision        Passed / Frozen
B7 entry                     GO
```

---

# 1. 当前问题的代码事实

## 1.1 slot deadline 在 execution 安装前已经存在

当前 `ExecutionService::submit()` 先执行：

```cpp
s->slots[slot].timer =
    s->deadlines.emplace(record->options().deadline, slot);

s->slots[slot].expired = false;
s->slots[slot].reserved = true;
```

之后才调用：

```cpp
ManagedInvocation::create(...)
```

最后才执行：

```cpp
s->slots[slot].execution = *execution;
```

因此：

```text
deadline timer 生命周期
    早于
slot.execution 生命周期
```

是当前真实设计。

---

## 1.2 deadline 到期时 execution 可能尚不存在

控制线程当前：

```cpp
auto& slot = s->slots[timer->second];

slot.expired = true;
slot.timer.reset();

expired = slot.execution;

s->deadlines.erase(timer);
```

然后：

```cpp
if(expired)
    expired->expire();
```

如果：

```text
slot.execution == nullptr
```

则 expiry 事实只留下：

```text
slot.expired = true
```

没有真正调用：

```text
ManagedInvocation::expire()
```

。

---

## 1.3 install 阶段又把 expiry 当成 cancel

当前：

```cpp
bool closing;

{
    std::lock_guard lock(s->mutex);

    s->slots[slot].execution = *execution;
    reservation.installed = true;

    closing =
        s->closing ||
        s->slots[slot].expired;
}

if(closing)
    (*execution)->cancel();
```

因此：

```text
slot.expired
```

与：

```text
service closing
```

失去了原因区别。

这是 F6-A 的直接根因。

---

# 2. 第二根因：cancel_requested_ 不是终态仲裁证明

当前：

```cpp
ManagedInvocation::cancel()
```

会先执行：

```cpp
cancel_requested_ = true;
```

然后再调用 Scheduler retirement。

而：

```cpp
ManagedInvocation::completed()
```

对于 State pre-start completion 当前执行：

```cpp
cancelled = cancel_requested_;
```

再传给：

```cpp
record_->complete_before_start(...)
```

。

因此存在：

```text
Scheduler expiry 已先赢
        ↓
Entry terminal = ExpiredBeforeDispatch
        ↓
迟到 cancel()
        ↓
cancel_requested_ = true
        ↓
completion 读取 true
        ↓
CancelWon
```

这里：

```text
cancel_requested_
```

只表示：

> 某个取消意图曾经出现。

它不能证明：

> cancel 赢得了 pre-start Scheduler 仲裁。

---

# 3. Scheduler 已经具有正确的线性化事实

不需要新增公共协议。

Scheduler 已经明确拥有：

```cpp
Errc::ExpiredBeforeDispatch
Errc::CancelledBeforeStart
```

以及：

```cpp
Retirement::Retired
Retirement::AlreadyStarted
Retirement::AlreadyTerminal
```

。

`Scheduler::retire()` 和 deadline expiration 都在 Scheduler 自己的 mutex 下修改 Entry terminal 状态。

例如 expiry：

```cpp
finish(
    entry,
    unexpected(
        error(Errc::ExpiredBeforeDispatch)
    )
);
```

而普通 cancel 默认：

```cpp
retire(
    ticket,
    error(Errc::CancelledBeforeStart)
);
```

。

因此本轮不应该再制造第三套取消证明。

唯一原则：

```text
Scheduler terminal cause
        =
pre-start State control outcome 的可信来源
```

---

# 4. 本轮硬范围

## 4.1 允许修改

首选仅修改：

```text
packages/runtime/executions/execution_service.hpp
packages/runtime/executions/managed_execution.hpp

tests/unit/state_roots/runtime_state.cpp
```

如确实需要辅助测试，可修改：

```text
packages/runtime/executions/managed_control.hpp
tests/contract/...
```

---

## 4.2 禁止修改

除非发现独立 blocker，否则禁止修改：

```text
packages/contracts/**
packages/state/**
packages/runtime/include/ock/runtime/detail/invocation.hpp
packages/runtime/policy/**
packages/runtime/scheduler/**
packages/runtime/resources/**
```

特别禁止修改：

```text
Outcome
CommitClaim
PublicationProof
ActionAuthorization
StateDomain
Atomic
RevisionPolicy
```

本轮不得再次扩大 public SDK。

---

# 5. Step F6-00：重新打开最终门禁

修改：

```text
docs/progress.md
```

将当前：

```text
B6 semantic precision v2 Passed / Frozen
B7 GO
```

暂时更新为：

```text
B6 semantic precision final linearization:
ChangesRequested

912cb755:
historical validated candidate

B7:
HOLD
```

必须注明：

```text
912cb755 的 31/31、
Native 7/7、
Embedded 3/3、
SDK/install/footprint
全部作为历史真实机器事实保留。

它们没有失效或被覆盖，
但未覆盖 F6 新发现的窗口。
```

不得删除旧 evidence。

---

# 6. Step F6-10：建立 Deferred Control 原因

目标：

```text
ExecutionService::Slot
```

当前：

```cpp
struct Slot {
    bool reserved = false,
         expired = false;

    std::shared_ptr<ManagedControl> execution;
    std::optional<Deadlines::iterator> timer;
};
```

建议改成内部原因状态：

```cpp
enum class DeferredControl {
    None,
    Expire,
    Cancel
};

struct Slot {
    bool reserved = false;

    DeferredControl deferred =
        DeferredControl::None;

    std::shared_ptr<ManagedControl> execution;

    std::optional<Deadlines::iterator> timer;
};
```

这是 Runtime 私有结构。

不得进入：

```text
CoreContracts
SDK public headers
wire format
Persistence
```

---

# 7. DeferredControl 的精确含义

它只处理：

> control event 已经发生，但 `slot.execution` 仍未安装。

不是新的业务状态。

定义：

```text
None
    尚无延迟控制事件

Expire
    deadline expiry 已经先在线性化锁下观察到

Cancel
    service shutdown/cancel 已先在线性化锁下发生
```

必须遵循：

```text
First writer wins
```

一旦：

```text
None → Expire
```

禁止改成：

```text
Expire → Cancel
```

反之亦然。

---

# 8. Step F6-20：修复 deadline-before-install

修改：

```text
ExecutionService::run()
```

deadline loop。

当前逻辑：

```text
timer 到期
    ↓
slot.expired=true
    ↓
若 execution 存在：
    expire()
否则：
    只留下 bool
```

新逻辑：

```text
timer 到期
        ↓
锁内读取 slot

if slot.execution exists:
    保存 execution owner
else:
    if slot.deferred == None:
        slot.deferred = Expire

清除 timer
        ↓
锁外

if execution owner exists:
    execution->expire()
```

伪代码：

```cpp
std::shared_ptr<ManagedControl> expired;

{
    std::lock_guard lock(s->mutex);

    auto timer = s->deadlines.begin();

    ...

    auto& slot = s->slots[timer->second];

    if(slot.execution) {
        expired = slot.execution;
    } else if(slot.deferred ==
              DeferredControl::None) {
        slot.deferred =
            DeferredControl::Expire;
    }

    slot.timer.reset();
    s->deadlines.erase(timer);
}

if(expired)
    expired->expire();
```

---

# 9. Step F6-30：修复 close-before-install

当前 `close()`：

```text
s->closing=true
    ↓
只 cancel 已安装的 slot.execution
```

对：

```text
reserved == true
execution == nullptr
```

的 slot 没有保存原因。

修改为：

```text
锁内：

s->closing = true

遍历 slot：

if execution exists:
    收集 owner
else if reserved &&
        deferred == None:
    deferred = Cancel
```

然后锁外：

```text
cancel 已安装 owner
scheduler->close()
wake
```

禁止：

```text
在 service mutex 下调用
execution->cancel()
execution->expire()
scheduler callback
业务 callback
```

---

# 10. 为什么要记录 Cancel

如果不记录，以下窗口仍不准确：

```text
close()
    ↓
slot 尚未 install
    ↓
ManagedInvocation create 完成
    ↓
slot install
```

新的 DeferredControl 使：

```text
expiry-before-install
    → Expire

close-before-install
    → Cancel
```

不再共用：

```text
bool closing
```

---

# 11. Step F6-40：安装 execution 时消费原因

修改：

```text
ExecutionService::submit()
```

在：

```cpp
s->slots[slot].execution = *execution;
```

后，不再：

```cpp
closing =
    s->closing ||
    slot.expired;
```

改成：

```cpp
DeferredControl deferred;

{
    std::lock_guard lock(s->mutex);

    auto& current = s->slots[slot];

    current.execution = *execution;
    reservation.installed = true;

    if(current.deferred ==
       DeferredControl::None &&
       s->closing)
        current.deferred =
            DeferredControl::Cancel;

    deferred = current.deferred;
}
```

锁外：

```cpp
switch(deferred) {
case DeferredControl::Expire:
    (*execution)->expire();
    break;

case DeferredControl::Cancel:
    (void)(*execution)->cancel();
    break;

case DeferredControl::None:
    break;
}
```

最后：

```cpp
s->wake->signal();
```

---

# 12. 这里的核心禁止事项

不得继续：

```cpp
if(closing || expired)
    execution->cancel();
```

不得：

```cpp
if(deadline <= now)
    execution->cancel();
```

不得：

```cpp
if(stop_requested())
    assume CancelWon;
```

不得根据：

```text
最终 stop token 状态
```

重建历史控制原因。

---

# 13. Step F6-50：State pre-start completion 只消费 Scheduler 原因

目标：

```text
ManagedInvocation::completed()
```

当前 State：

```cpp
std::lock_guard lock(lifetime_mutex_);
cancelled = cancel_requested_;
```

必须删除这个分类依据。

改成：

```cpp
const bool cancelled =
    reason.code() ==
    scheduler::error(
        scheduler::Errc::CancelledBeforeStart
    ).code();
```

但只适用于：

```text
StateEdit
pre-start
无 reply
```

正确结构建议：

```cpp
if(!record_->reply_pointer()) {
    auto reason =
        status
        ? contracts::error(
              ContractsErrc::Rejected)
        : status.error();

    bool cancelled = false;

    if(record_->material()
           ->entry->shape ==
       invocation::Shape::StateEdit) {

        cancelled =
            reason.code() ==
            scheduler::error(
                scheduler::Errc::
                    CancelledBeforeStart
            ).code();

    } else {
        // Read 保持当前既有合同，
        // 本轮不重新定义。
        cancelled = ...
    }

    record_->complete_before_start(
        reason,
        cancelled
    );
}
```

---

# 14. 这不是“根据任意 ErrorCode 猜取消”

本轮允许使用：

```text
scheduler::Errc::CancelledBeforeStart
scheduler::Errc::ExpiredBeforeDispatch
```

是因为它们是 Scheduler 自己在同一个 start/retire mutex 中形成的：

```text
terminal cause
```

。

这是：

```text
线性化结果读取
```

而不是：

```text
从业务错误猜时序
```

禁止重新使用：

```text
PolicyErrc::Expired
InvocationErrc::Expired
ContractsErrc::Rejected
```

推导 cancel。

---

# 15. Step F6-60：保留 cancel_requested_，但降低其职责

不要删除：

```cpp
cancel_requested_
```

它仍用于：

```text
阻止继续接受 child
父取消传播
协作 stop
运行期取消意图
```

例如当前：

```cpp
accept_children_ =
    !cancel_requested_;
```

仍然合理。

但它不再允许承担：

```text
pre-start terminal winner
```

这一职责。

最终：

```text
cancel_requested_
    = intent

Scheduler terminal cause
    = winner
```

必须明确分离。

---

# 16. 最终 State pre-start 真值表

## Case A：真正 cancel 先赢

Scheduler：

```text
CancelledBeforeStart
```

最终：

```text
Completed
└─ CancelledBeforeApply

execution_accepted = true
business_entered = false
decision = CancelWon
no_application_proven = true
```

---

## Case B：expiry 先赢

Scheduler：

```text
ExpiredBeforeDispatch
```

最终：

```text
Completed
└─ FailedBeforeApply

execution_accepted = true
business_entered = false
decision = NotReached
no_application_proven = true
```

---

## Case C：resource failure 先赢

例如：

```text
Resource Full
WouldBlock fatal
Executor rejection
```

最终：

```text
FailedBeforeApply
NotReached
```

不能 `CancelWon`。

---

## Case D：service close / Scheduler Closed

如果实际 Scheduler terminal cause 是：

```text
Errc::Closed
```

则：

```text
FailedBeforeApply
```

禁止事后因为：

```text
cancel_requested_ == true
```

覆盖成 `CancelWon`。

---

# 17. 已开始 State 的规则完全不改

一旦 Scheduler：

```text
AlreadyStarted
```

pre-start classification 不再参与。

继续使用：

```text
WorkContext stop
ActionAuthorization
Action deadline
CommitClaim
Publication gate
```

决定真实结果。

因此：

```text
deadline after handler entry
```

仍应由：

```text
Action deadline / consume_claimed
```

阻止 publish。

已有：

```text
business_entered=true
FailedBeforeApply
NotReached
```

反例继续保持。

---

# 18. Step F6-70：新增确定性反例一——已过期提交

新增一个 Host State 用例：

```text
deadline =
steady_clock::now() -
1ms
```

提交必须仍遵循现有 State managed 接受合同：

```text
submit
→ Accepted(ref)
```

之后：

```text
wait(ref)
→ Terminal
```

结果：

```text
FailedBeforeApply

execution_accepted = true
business_entered = false
decision = NotReached
```

禁止：

```text
CancelledBeforeApply
CancelWon
StateCommitted
```

revision 不增加。

---

# 19. Step F6-80：Accepted→Install 竞态压力反例

为了放大：

```text
Accepted publication
        ↓
slot install
```

窗口，增加专用 Host stress。

建议：

```text
N = 256 或 512
```

连续提交：

```text
deadline <= now
```

的 State execution。

每个必须：

```text
Accepted
→ Terminal
→ FailedBeforeApply
```

并检查：

```text
CancelWon count == 0
StateCommitted count == 0
revision delta == 0
```

该测试不是性能测试。

目标只是：

> 在多次 thread scheduling 下证明不存在 expiry→cancel 分类泄漏。

Debug/ASan 建议：

```text
N = 256
```

Release：

```text
N = 512
```

若现有正式测试必须完全一致，可固定一个共同 N。

---

# 20. Step F6-90：真正 cancel 对照组

必须与 expiry stress 同时存在。

构造：

```text
Accepted State
resource waiting
        ↓
session.cancel(ref)
```

要求：

```text
CancelledBeforeApply
CancelWon
```

并且：

```text
revision unchanged
```

这是防止修复过度：

```text
所有 pre-start termination
都变成 FailedBeforeApply
```

。

---

# 21. Step F6-100：expiry 先赢，迟到 cancel

必须补这个反例。

目标时序：

```text
Accepted
    ↓
Scheduler expiry terminal
    ↓
execution 尚未完全从 table 清理
    ↓
cancel(ref)
```

最终原执行结果必须仍：

```text
FailedBeforeApply
NotReached
```

不能变成：

```text
CancelledBeforeApply
```

cancel API 可以返回：

```text
AlreadyTerminal
```

或当前合同允许的已终态结果。

关键验收：

```text
Outcome 不改变
```

。

---

# 22. 确定性不足时的测试原则

禁止依靠：

```text
sleep(1ms)
希望刚好撞到 race
```

作为唯一证明。

优先使用：

```text
semaphore
resource hold
fixed expired deadline
existing Scheduler/Host gates
```

控制顺序。

若 `Accepted → slot install` 窗口无法通过现有 fixture 稳定命中，允许：

```text
添加 Runtime 私有 test-only synchronization seam
```

但必须满足：

```text
不安装
不进 public header
不进 SDK
生产 Release 不启用
只在 test target 定义下存在
```

优先级：

```text
现有同步 fixture
>
内部测试 helper
>
test-only seam
>
概率 sleep
```

---

# 23. 既有回归必须保留

当前已经存在的两条 Host expiry 反例不得删除。

继续验证：

### 等待资源后 expiry

```text
Accepted=true
business_entered=false
FailedBeforeApply
NotReached
```

### handler 已进入后 expiry

```text
Accepted=true
business_entered=true
FailedBeforeApply
NotReached
revision unchanged
```

现有测试已经覆盖这两类，本轮只补安装前窗口。当前源码中的对应 Host 回归不允许被弱化。

---

# 24. Red → Green 顺序

严格执行：

```text
1. 仅增加 F6 新反例
2. 在 912cb755 行为上运行
3. 至少证明安装前 expiry 用例能够暴露问题
4. 保存失败原文
5. 再修改 production code
6. 单独跑 F6 测试
7. 跑完整 T06.state.runtime_native
8. 再进入正式矩阵
```

若因为竞态难以在原实现稳定 Red：

```text
不得伪造失败
```

保留静态代码证明和专用压力探针，同时确保 Green 后形成确定性合同测试。

---

# 25. 推荐生产修改文件

理想最终 diff：

```text
packages/runtime/executions/
    execution_service.hpp
    managed_execution.hpp

tests/unit/state_roots/
    runtime_state.cpp
```

预计：

```text
3 files
```

即可完成。

如果实际生产 diff 超过：

```text
5 个生产文件
```

必须重新审查是否发生范围扩张。

---

# 26. 不应修改 SDK manifest

本轮目标文件属于 Runtime 私有 execution implementation。

因此正常情况下：

```text
sdk/sdk_api_manifest.json
```

不应发生变化。

正式 SDK gate 必须验证：

```text
public header hash unchanged
public include closure unchanged
component closure unchanged
SDK version unchanged
```

如果 generator 得出公共摘要发生变化：

```text
STOP
```

检查是否意外修改了 public surface。

不得人工手改 hash 去适配。

---

# 27. 推荐生产提交

建议只产生一个生产提交：

```text
B6: close pre-start expiry cancellation race
```

包括：

```text
execution_service.hpp
managed_execution.hpp
runtime_state.cpp
```

不要把：

```text
代码修复
正式机器 evidence
最终 Passed 文档
```

混在同一个 commit。

---

# 28. 新 FINAL_CODE_SHA

生产提交完成后：

```text
NEW_FINAL_CODE_SHA=<sha>
```

必须确认：

```text
git status clean
F6 direct tests Passed
T06.state.runtime_native Passed
```

然后冻结。

从这里开始：

```text
packages/**
tests/manifests/**
正式 footprint method inputs
```

全部禁止修改。

若修改：

```text
NEW_FINAL_CODE_SHA 作废
```

。

---

# 29. 正式功能矩阵

沿用当前 B6 semantic v2 固定：

```text
31 tests
```

不得增加后临时改变 expected 数量。

必须完整执行：

```text
win-msvc-debug
31 / 31

win-msvc-release
31 / 31

win-msvc-asan
31 / 31
```

如果新增 F6 测试合并到现有：

```text
T06.state.runtime_native
```

则正式测试数量仍可保持 31。

这是推荐方案。

不要为了 F6 再把 formal matrix 人为增加成 32/33，除非现有测试结构要求独立 test target。

---

# 30. ASan 必须关注

除了：

```text
ASan error = 0
```

重点检查：

```text
DeferredControl slot 生命周期
timer iterator 生命周期
late cancel owner 生命周期
shared_ptr ManagedControl 生命周期
slot retire/reset
ExecutionService shutdown
```

尤其禁止产生：

```text
timer 已 erase 后再次 erase
slot reuse 遗留 deferred state
expired owner 双调用
cancel + expire 双重 retire 导致 UAF
```

---

# 31. Slot reuse 必须重置

每次 reserve 新 slot 时必须：

```cpp
slot.deferred =
    DeferredControl::None;
```

每次 slot 退休时必须确保：

```text
execution.reset()
timer.reset()
deferred=None
reserved=false
```

否则下一 execution 可能继承上一次：

```text
Expire
Cancel
```

这是本修复最重要的次生风险之一。

必须新增至少一个复用测试：

```text
slot 1：
expired

slot 释放

同一容量下 slot 2：
正常执行

slot 2 必须成功 StateCommitted
```

---

# 32. Shutdown 回归

本轮修改 close/deferred 交界，因此必须回归：

```text
Host close with:
    queued execution
    waiting-resource execution
    running execution
```

要求：

```text
无死锁
无泄漏
shutdown_until 可排空
active == 0
scheduler active == 0
worker_delivery == 0
```

本轮不重新规定：

```text
service close
```

必须对外表现成哪一种业务取消 Outcome。

只要求：

```text
不能被 expiry 事实错误覆盖
不能把已 Published 结果降级
```

。

---

# 33. Scheduler 不修改原则

本轮不需要修改 Scheduler。

原因：

Scheduler 已经有：

```text
CancelledBeforeStart
ExpiredBeforeDispatch
```

和真实互斥仲裁。

如果实施者认为必须改：

```text
packages/runtime/scheduler/**
```

才能解决本问题：

```text
STOP / Design Review
```

先证明为什么现有 terminal cause 不足。

---

# 34. Native / Embedded footprint

新修复触及：

```text
ExecutionService
ManagedInvocation
```

最终仍按现有 B6 收口策略重新测量：

```text
Native 7 项
Embedded 3 配置
Release startup
```

全部使用原：

```text
budget
pilot
ABBA
sample count
method digest
threshold
```

不得提高任何预算。

---

# 35. Footprint 原因

虽然 NativeSubset 理论上未直接执行：

```text
ExecutionService
```

但为了保持 B6 最终来源证据完整一致：

```text
Native 7/7
```

仍重新绑定新 SHA。

这样最终 acceptance 无需混用：

```text
912cb755 Native evidence
+
NEW_SHA Embedded evidence
```

。

---

# 36. Runtime-only / Embedded 依赖

重新验证六投影：

```text
Runtime-only
Embedded
×
Debug / Release / ASan
```

要求继续保持：

```text
Runtime-only:
    expected

Embedded:
    expected + thread_pool
```

禁止新增：

```text
State
immer
```

。

---

# 37. SDK / Installation

正式运行：

```text
State installed consumer
public headers
component closure
public include boundary
SDK version
```

预期：

```text
全部 Passed
public API digest unchanged
```

---

# 38. 新 evidence 目录

不得覆盖：

```text
evidence/B6/semantic-precision-912cb75/
```

建议新建：

```text
evidence/B6/
semantic-precision-<NEW_SHORT_SHA>/
```

旧 `912cb755`：

```text
Historical validated candidate
```

继续保留。

---

# 39. 新 acceptance 必须绑定

至少包括：

```text
NEW_FINAL_CODE_SHA
tree SHA
inputs_sha256
formal acceptance sha256
footprint integrity sha256
Debug expected/executed
Release expected/executed
ASan expected/executed
Native footprint
Embedded footprint
B7_entry
```

最终不能引用：

```text
912cb755
```

作为新的 source_commit。

---

# 40. Review 必须新增 F6 专项

CODE review 必须回答以下五项：

```text
R1
expiry-before-install 是否仍会调用 cancel()？

R2
State pre-start classification 是否仍读取
cancel_requested_ 判断 winner？

R3
CancelledBeforeStart 是否只来自 Scheduler
真实 cancel retirement？

R4
ExpiredBeforeDispatch 是否始终产生
FailedBeforeApply / NotReached？

R5
late cancel 是否无法改写已经确定的 expiry terminal cause？
```

任一回答不是明确：

```text
Yes / No blocker
```

不得 Passed。

---

# 41. SPEC review

必须确认没有修改：

```text
A05 Outcome fact model
A09 Accepted identity
State provider contract
CoreContracts ABI
Atomic semantics
Policy semantics
```

本修复只是：

```text
Runtime 私有线性化事实传递纠偏
```

因此原则上：

```text
无需新 ADR
```

。

---

# 42. 最终真值矩阵

| 时序 | 最终结果 |
|---|---|
| Cancel 先于 Scheduler start | CancelledBeforeApply / CancelWon |
| Expiry 先于 Scheduler start | FailedBeforeApply / NotReached |
| Resource failure 先于 start | FailedBeforeApply / NotReached |
| Scheduler Closed 先于 start | FailedBeforeApply / NotReached |
| Cancel 在 expiry terminal 后到达 | 保持 expiry 结果 |
| Expiry 在 cancel terminal 后到达 | 保持 cancel 结果 |
| Start 已赢，随后 cancel | 进入运行期 Action/Commit 仲裁 |
| Start 已赢，随后 expiry | Action deadline/commit gate 决定 |
| Published 后 cancel/expiry | StateCommitted 不降级 |

---

# 43. 必须新增/保留的反例清单

最终至少：

```text
F6-01 already-expired State submit
F6-02 pre-install expiry stress
F6-03 genuine pre-start cancel
F6-04 expiry then late cancel
F6-05 cancel then late expiry
F6-06 expired slot reuse
F6-07 close with reserved/uninstalled State
F6-08 waiting-resource expiry
F6-09 handler-entered expiry
F6-10 late stop after Published
```

其中：

```text
F6-08
F6-09
F6-10
```

可复用当前已有测试。

---

# 44. NO-GO 条件

出现任一项：

```text
expiry → CancelledBeforeApply

ExpiredBeforeDispatch
→ CancelWon

cancel_requested_
仍作为 State pre-start winner

slot.expired
仍和 service closing
共用 cancel 路径

late cancel
能改变已确定的 expiry outcome

expired slot reuse
污染下一 execution

Published
被后续 control event 降级

Debug != 31/31
Release != 31/31
ASan != 31/31

ASan error > 0

SDK public digest 意外变化

Runtime-only / Embedded
出现 State/immer

任何 footprint 超原预算

formal evidence
不是同一个 NEW_FINAL_CODE_SHA
```

一律：

```text
B7 HOLD
```

。

---

# 45. GO 条件

只有全部满足：

```text
[ ] F6-A 关闭
[ ] F6-B 关闭
[ ] expiry/cancel winner 唯一来自真实 Scheduler cause
[ ] slot deferred control first-wins
[ ] slot reuse clean
[ ] close/shutdown 无死锁
[ ] existing F1–F5 回归全部保持
[ ] Debug 31/31
[ ] Release 31/31
[ ] ASan 31/31
[ ] State install Passed
[ ] SDK Passed
[ ] Runtime-only Passed
[ ] Embedded Passed
[ ] Native 7/7 footprint
[ ] Embedded 3/3 footprint
[ ] Release startup Passed
[ ] CODE review Approved
[ ] SPEC review Approved
[ ] acceptance 绑定 NEW_FINAL_CODE_SHA
```

才能：

```text
B6 = Passed / Frozen
B7 = GO
```

---

# 46. 推荐提交序列

推荐历史：

```text
e878082
    ↓

docs:
B6: reopen final expiry linearization gate
    ↓

production + tests:
B6: close pre-start expiry cancellation race
    ↓

NEW_FINAL_CODE_SHA
    ↓

machine evidence:
B6: archive final expiry linearization validation
    ↓

docs only:
B6: freeze final validated B6 baseline
```

最后两个提交：

```text
不得修改 production tree
```

。

---

# 47. Codex / Terra 执行约束

执行模型必须：

1. 以本文件为本轮唯一补充执行方案。
2. 不重新执行 F1–F5 架构设计。
3. 不进入 B7。
4. 不修改 CoreContracts。
5. 不修改 Scheduler，除非先证明阻断。
6. 不修改预算。
7. 不删除旧失败。
8. 不覆盖 `912cb755` evidence。
9. 新代码完成后先冻结 SHA。
10. 冻结后再正式跑矩阵和 footprint。
11. 生产代码变更后不得继续沿用旧机器验收。
12. 所有控制事实必须来自真实 owner/linearization point。

---

# 48. 执行者最终报告格式

必须输出：

```text
1. NEW_FINAL_CODE_SHA
2. tree SHA
3. 修改文件
4. F6-A 实现说明
5. F6-B 实现说明
6. DeferredControl 实现说明
7. Scheduler cause 分类说明
8. 新增反例
9. T06 direct result
10. Debug 31/31
11. Release 31/31
12. ASan 31/31
13. SDK/install
14. Runtime-only/Embedded
15. Native footprint
16. Embedded footprint
17. CODE review
18. SPEC review
19. acceptance path
20. residual blocker
21. B7 GO/HOLD
```

不接受只写：

```text
tests passed
```

。

---

# 49. 最终 Definition of Done

本轮真正的完成标准是：

> 对一个已 Accepted 但尚未开始执行的 State execution，系统可以准确区分：
>
> - 是用户/owner cancel 先赢；
> - 还是 deadline expiry 先赢；
> - 还是其他 Scheduler failure 先赢。

而且这个结果必须在真实线性化后永久保持：

```text
Cancel win
→ CancelWon

Expiry win
→ NotReached

Other failure
→ NotReached
```

任何稍后到来的：

```text
cancel
expiry
close
stop
```

都不能重新解释已经确定的历史事实。

只有做到这一点，B6 的：

```text
Accepted
BusinessEntered
CancelWon
ClaimWon
Published
```

五类关键事实才真正形成闭环。

**完成本文件后，不再存在已知 B6 架构或语义 blocker，可正式冻结 B6，并放行 B7。**
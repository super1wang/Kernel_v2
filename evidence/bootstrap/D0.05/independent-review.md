# D0.05 独立只读审查（发现轮次冻结）

审查者：D0.04实施代理；本轮此前未修改D0.05实现。依据A06/A11/A12/A14及D0.05执行卡，范围为commit/intent中文合同、两个参考模型及测试、固定expected清单。以下是D0模型正确性问题的优先级，不是对未发布Runtime给出生产漏洞等级。

冻结结论：**存在5项需修复问题，不能据现有54项全绿关闭D0.05技术审查。** 已向主集成者报告。自动结果、技术审查、人工批准分开；本报告不是用户验收或G0批准。

读取实现SHA-256：

- `tests/model/commit_model/model.py`：`59142604af33ec440f815ed8b1f5227d5ca62e1ed442c346f9503ff1a6c804f7`
- `tests/model/dedup_model/model.py`：`a6d7658e007a878b67df24a38d5c4478f39ee5134620e3538d819b916d11eed4`

实际运行 `python -X utf8 tests/model/commit_model/run.py --all` 返回0：54 tests，OK。临时反例从标准库内存脚本导入模型，禁止写入bytecode，没有修改实现。固定预期、源发现与正常测试数量一致，但现有用例漏掉以下路径。

## P1：易失域 recover 回退状态却保留成功事实

位置：`commit_model/model.py:337`，特别是341直接安装初始化disk根，且未检查durable。

复现：`CommitDomain(durable=False)` → prepare(value=7) → claim → publish → recover。真实输出：

```text
before: PublishedState(value=7, revision=1, history=('c1',), lifecycle_generation=1)
after:  PublishedState(value=0, revision=0, history=(), lifecycle_generation=1)
outcome: StateCommitted, revision='1', published_version='1', result={'value':7}
```

A06.4规定无Durable时publish就是正式提交；空disk不能作为已发布内存状态的恢复来源。恢复调用目前成功回退root/revision，却留下已有CommitFact/PublishedFact和StateCommitted，形成可见状态与查询事实矛盾。

验收：易失域恢复入口在任何状态变更前明确拒绝；已发布snapshot、事实、Outcome均不变。Durable正常恢复继续安装真实镜像。

## P1：Effect 的durable claim与发送身份可以漂移

位置：`commit_model/model.py:383–404`，claim保存effect_id/binding，authorize与send没有对照该冻结材料。

复现：Effect('effect1',binding,device) → claim → authorize → `effect.effect_id='effect2'` → send(Applied,Succeeded) → record。真实结果：

```text
disk.claim.effect_id = effect1
device.calls = ['effect2']
disk.outcome.effect_id = effect2
```

另一个正交反例：claim后改binding.target为device2，给新binding发合法permit，authorize也成功，disk.claim.binding.target仍是旧domain-1。

这违反A06.1一次动作许可绑定与A11.4稳定EffectId先claim后发送的关系；已有claim不能给不同动作提供耐久身份。A12.5恢复按claim查证也会指向错误身份。

验收：claim后保留不可改写的EffectId、binding及真实Device所有者/发送材料；许可与发送核对同一份材料。篡改在消费前拒绝且permit未消费，在发送前拒绝且Device没有调用。发送后record/restart/outcome只读冻结的事实身份，不从可变字段重造回执。

## P1：claim前可安装不存在的提交历史链

位置：`commit_model/model.py:169–184`，仅检查base_revision与binding；`validate_publication:263`只检验revision==history长度及与disk镜像相等。

复现：在初始域prepare('c1',7)后，将prepared替换为 `PublishedState(99,2,('ghost','c1'),1)`，然后claim→submit→writer_step→deliver→publish。真实结果：成功Published(value99,revision2)、StateCommitted(revision2)；随后check_disk返回“状态与 ledger/审计/Outbox 撕裂”。

A06.2要求许可前准备并验证完整root/history/回执材料；与自己刚写入的损坏disk相等不能证明候选合法。消费许可前必须校验候选revision恰为base+1、history恰为当前已发布history加本CommitId、生命周期一致，以及准备材料/结果冻结关系。

验收：上述候选与生命周期/输出材料变化在许可前失败，permit未消费、disk/visible root均不变；许可后与publish后的查询继续使用被冻结的材料。

## P2：Request冻结dataclass没有冻结嵌套参数，指纹随调用者变化

位置：`dedup_model/model.py:58–60,159–173`。fingerprint元组持有args/preconditions里的可变引用，新record直接保存该指纹，而input另外deepcopy。

复现：Request.args=({'x':1},)，claim成功后把原字典x改为2，使用原key重试。真实输出：

```text
retry_returned_existing = True
record.fingerprint.args = ({'x':2},)
accepted input.args = ({'x':1},)
```

模型合同明确使用不可变已归一化符号值，实际没有拒绝或拥有型冻结嵌套内容。因此参数变化改变原指纹，既不IntentConflict，也不与接受时输入一致。无需实现D5的canonical CBOR；本包应保证符号值确实不可变且按类型比较，或严格拒绝模型不支持的可变值。

验收：接受后外部参数变更不能改既有fingerprint/input；改参重试冲突；不把True/1或1/1.0默认当成相同符号类型，除非已有明确TypeContract归一化事实。

## P1：正常状态接口允许Unknown恢复为可再次发送的Claimed

位置：`dedup_model/model.py:195–200` 与 `387–398`。set_status只检查outcome为空，没有检查已有发送/未知边界。

只用正常接口复现：claim → authorize_effect_send → restart（status变Unknown）→ set_status(key,'Claimed') → authorize_effect_send。实际输出：

```text
after_restart = Unknown
dispatch_count = 2
same_key = True
```

A12.5明确未知效果不能因outcome缺失盲重发。通用状态setter可以抹掉已许可/恢复未知边界，使恢复门的“只准入Claimed”失去意义。

验收：EffectPermitted/Unknown不得经通用setter退回可发送状态；发送事实独立于投影状态保留。已确定NotApplied等需要另一个明确证据路径，不能把任意状态重置当作对账。

## 正向核对与结论边界

已检查取消/撤权与消费双方顺序、同域reservation及callback身份、单镜像事务崩溃窗口、Outbox publication gate、恢复先验证后开放、claim先查既有记录、GC先推进水位、内部StepKey、旧备份新世代与不自动重播。现有模型的这些路径有清晰正反例和真实54项测试结果；本报告不据此宣称真实SQLite、C++线程、OS认证或持久备份后端通过。

## 角色转换记录

主集成者在发现报告后授权同一代理执行D0.05范围内修复。**本页以上独立发现轮次保持冻结，不改写原问题为已通过。** 后续反例、修复和新证据由本代理以“修复实施者”身份提供，必须交主集成者及原实现者交叉复核；本代理不得自签关闭独立审查。

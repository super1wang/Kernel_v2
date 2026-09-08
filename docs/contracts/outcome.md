# D0.03｜Outcome、phase 与完成寿命合同草案

## D2.04 生产 wire 接线（2026-09-08）

历史 D0 Schema 与 golden 保留不变。生产入口使用 [rpc-v1/outcome.schema.json](../../schemas/rpc-v1/outcome.schema.json) 和 `encode_invoke`，仍只有 Rejected/Completed 及原九类 Outcome、六类 KnownFact。Tagged128 使用规范小写 hex；原子域完整保留 provider/domain_id/generation；Error 使用 domain/code/可选 message。conditions 保留收尾、必要记录失败及应用前决定材料，不能仅输出成功布尔值。

结果编码失败或抛出时写入 result_error（Plan 为 exports_error），保留 Outcome kind/evidence/known_facts；不降级为 Rejected。Plan exports 必须为对象，不合格的结果材料明确报编码错误。整体输出预算仍生效，不能编码完整回执时返回编码失败，由 Control 关闭不确定传输而非伪造业务拒绝。原生 failure_phase 是阶段标签（例如 native），不冒充 ExecutionPhase；PartialCompletion 允许已发生应用事实且有一个失败步骤，与现行 CoreContracts 校验一致。业务事实来源、跨事实关系及追加历史仍由 CoreContracts 和实际 owner 验证，Schema 不承担真实性证明。

本节只记录 B2 生产编码，不将后文历史 D0 的模型验收改写为当前实现验收。

本合同落实唯一规范 [架构 v3.3](../01_Architecture_v3.3.md) 的 A04、A05、A09、A17，以及 [执行计划 D0.03](../02_Execution_Plan_v3.3.md#d003)。A06 仅引用 publication gate 的可见性要求；不在本包实现 permit、epoch、持久提交或恢复协议。前置 D0.01 的实际用户批准见 [批准记录](../reviews/D0.01-approval.json)。

当前产物是可执行 Python 参考模型、Draft 2020-12 JSON Schema 草案和固定 golden。自动运行结果与包级人工评审分离；本合同不声称 D0.03 已人工批准、G0 已通过或真实 C++ Runtime 已实现。

## 1. 三层结果与身份

| 层 | 固定含义 | 不允许的替代 |
|---|---|---|
| `SubmitReply` | `Rejected{reason}` 或 `Accepted{execution_ref, acceptance_guarantee}` | Accepted 不携带最终 R，也不保证后台成功 |
| `InvokeReply` | `Rejected{reason}` 或 `Completed{outcome}` | 短易失 Native Invoke 不因此创建执行记录、TaskId 或观察版本 |
| `ExecutionPhase` | 执行、等待及必要收尾的位置 | Running/Finalizing 不是成功或失败 Outcome |
| `Outcome` | 已发生的业务事实、证据与可用结果 | 传输错误、取消意图、日志错误不能抹掉事实 |

模型的 `execution_ref`、CommitId、EffectId 等是非空不透明标签；它们只用来核对同一模型对象，不是另一套正式身份编码。正式 ExecutionRef DTO、Control wire 和字节布局由 D0.04 沿既有身份契约确定，本包不发布 RPC 格式。

`acceptance_guarantee` 只取 `Volatile | DurableAccepted`。模型 `publish_record()` 表示执行表、输入 owner、最小回执和完成通知容量已预留后发布记录；实际配额分配不在模型内。DurableAccepted 还须先 `persist_acceptance()`，未完成时查询接受响应及派发均拒绝。底层 Executor 之前必须已经有可供 inline 完成器引用的记录。

## 2. 唯一九类 Outcome

所有 Outcome 均带单值 `evidence` 和追加式 `known_facts`。Schema 使用封闭对象和 `oneOf` 区分变体，不接纳第十种 Outcome 或自由持久性布尔值。

| kind | 必要材料与约束 |
|---|---|
| `ReadCompleted` | `result` 与 `result_scope=ReadOnly|Candidate`；不能含已应用事实。候选结果可表达 DataRef，但不表示正式发布 |
| `StateCommitted` | `commit_id,domain,revision,published_version,result`；恰好一个匹配 CommitFact 和 PublishedFact；成功投影须经过发布 gate |
| `EffectResolved` | `effect_id,application=NotApplied|Applied|PartiallyApplied,status=Succeeded|Failed,external_evidence`；与 EffectFact 一致；携带 `result` 或明确 `result_error`，结果编码失败也保留最小发送回执 |
| `LifecycleResolved` | `transition_id,before,after,generation,status`；与 LifecycleFact 一致。失败可真实进入 `Failed`，不能把 after 改回 before |
| `PlanCompleted` | `exports`、有序 `steps{step_id,status}` 摘要及 `known_facts`；必要步骤全部 Succeeded，必要子执行的实际 Outcome 全部成功且已收尾，不携带伪造的全局 CommitId。纯读计划也使用此变体 |
| `FailedBeforeApply` | `failure_phase,reason,proof=NoAppliedStateOrEffect`；必须无已应用事实、无未解决未知；已进入业务的失败才使用它 |
| `CancelledBeforeApply` | `reason,proof=NoAppliedStateOrEffect`；模型必须先有取消意图并在应用前赢得决定点；等待超时不能生成它 |
| `PartialCompletion` | 有序成功/失败/取消步骤摘要；有确定应用事实，至少一个必要步骤未成功，没有未解决未知 |
| `Indeterminate` | `unknown_ids` 精确列出所有未解决 UnknownFact；每个 UnknownFact 携带边界、CommitId/EffectId 引用及对账方法；其他已知提交继续保留 |

`steps` 的数组顺序是必要步骤的摘要顺序；各步的确定状态与 `known_facts` 的应用/提交证据一起构成模型结果。它不定义 Plan IR、槽类型或 checkpoint 编码。单个 Effect 的部分应用仍为 EffectResolved；组合任一必要效果未知则必须 Indeterminate，不能以 PartialCompletion 或 PlanCompleted 掩盖。没有先前应用、仅有读/计算失败的组合可以 FailedBeforeApply。

StateEdit 的 R 必须在提交前验证，成功始终对应一个正式提交。即使 delta 为空也不引入本包之外的 no-op 成功变体；revision 推进和实际 EditView/提交协议由 D0.05 及 D4 验证。

## 3. KnownFacts 与证据组合

事实包含不可重复的 `fact_id`。模型区分以下记录，避免在同一记录上从未知覆盖成已知、从耐久覆盖成已发布：

| 记录 | 内容 |
|---|---|
| `CommitFact` | commit、domain、revision、`durability=Memory|DurableCommitted`；确定提交不自动代表发布 |
| `PublishedFact` | 引用前序 commit 与 published_version；同一 commit 只出现一次，版本与 revision 匹配 |
| `EffectFact` | EffectId 与实际 NotApplied/Applied/PartiallyApplied |
| `LifecycleFact` | 转换身份、before、after、generation |
| `UnknownFact` | `boundary=ExternalEffect|StorageCommit`、reference_id、reconcile |
| `ResolutionRecord` | 引用先前未解决的 unknown_id、determination、evidence_ref；必须与追加的确定效果/提交相符 |

接受新的 Outcome 时，已存事实必须是新事实列表的完整前缀。重复 fact_id、重写同一 CommitId/EffectId/转换身份、悬空发布、悬空或重复 ResolutionRecord 均拒绝。每次追加后还按最终事实集合复核全部历史 ResolutionRecord，禁止在同一提交已确定 NotApplied 后追加相反 CommitFact；新事实只能增加认知，不能推翻同一身份的确定事实。Terminal 后对账只追加记录并改变查询投影，不重开执行，不重发完成信号，也不重新释放资源。原 UnknownFact 继续可查。

| evidence | 合法语义及结果组合 |
|---|---|
| `Volatile` | 当前仅具易失证据；允许确定结果或外部未知；不得把已确认 DurableCommitted 的状态成功降级为 Volatile |
| `Durable` | 所声明的证据已耐久；可有尚待外部对账的 Indeterminate，耐久保存未知并不消除未知 |
| `RequiredRecordFailed` | 必要记录有界尝试失败；保留业务事实，执行观察必须同时显示 Failed 记录状态、封锁新写入、可查询故障和修复策略 |
| `PersistenceUncertain` | 存储提交或效果持久事实不确定；只能搭配 Indeterminate，不能搭配确定成功/失败 |

模型只声明符号化证据来源，不验证 SQLite、设备回执真伪、事务原子性或磁盘耐久性。Schema 检查字段形状；事实相等、引用、版本范围和历史追加关系由模型检查，两者不能互相冒充。

## 4. Phase 与完成回调

| 起点 | 正常允许到达的位置 |
|---|---|
| Queued | WaitingResources、Running、Finalizing、Suspended |
| WaitingResources | Running、Finalizing、Suspended |
| Running | WaitingChild、Finalizing、Suspended |
| WaitingChild | Running、Finalizing、Suspended |
| Finalizing | Terminal |
| Suspended | 仅显式 `resume()` 恢复同一身份此前的队列/资源等待/运行/子等待位置 |
| Terminal | 不再转移；对账和观察错误追加不改变 phase |

表是候选边；实际动作还必须满足 owner、回调、记录、可恢复性等前置条件。不得绕过 `dispatch/complete/suspend/finalize` 的专门检查直接修改 phase。中间阶段不需要时可省略，例如排队期间取消或 Executor 拒绝可直接进入 Finalizing。可恢复挂起表示本地代码已经协作让出、没有本地 worker 持续运行且持有必要 durable pins；不是强制暂停任意线程。不可恢复纯计算必须终结时可用 FailedBeforeApply(reason=Interrupted)。

Executor 接受后，模型只允许一次框架完成。inline 完成可先于外部 Accepted 回复发生，甚至终态可先于该回复；记录仍须先发布。Executor 拒绝或异常表示未持有工作，work 计数为零，后续 callback 拒绝；已经向外接受的执行保留身份并形成 FailedBeforeApply。重复/迟到 callback 触发 ContractError，不重复 publication、release 或可靠完成信号。

`complete()` 到 Finalizing，`finalize()` 才发出恰好一次可靠完成并释放当前执行资源。StateCommitted 可在 Finalizing 查询；`wait_terminal()` 此时仍为 false。External Notification 可以全部丢失，本地完成仍恰好一次。

## 5. Finalizing 故障、父子和隔离

必要记录默认有界重试（模型参数默认为 3 次）；仍 Pending 时不得 Terminal。尝试成功进入 Recorded；预算耗尽进入 Failed，同时设置 RequiredRecordFailed、writes_blocked、fault 和修复策略：

- 有已知耐久状态提交：`CommitLedger`，由既有 commit ledger 修复额外终态索引，原 CommitFact/PublishedFact 不变。
- 有效果或未知边界：`ReceiptReconcile`，依据保存的 EffectId/边界与最小回执继续对账。
- 其余必要记录故障：`ManualReview`，明确保留故障供人工处理。

这些值是本包固定的失败分流声明，尚不实现真实修复服务。对账可解决未知，但不自动消除未修复的 RequiredRecordFailed。普通 exporter/log flush 失败只追加 `post_observation_errors`，不改 Outcome、不封锁写入，也不阻止必要收尾。

父执行持有 child 的独立身份和 owner。模型以独立的父引用和转交标记约束唯一 owner，不以可能与 ExecutionRef 撞名的字符串当作未绑定哨兵；祖先不能再次成为 child，父子持有图必须无环。父必要 child 未收尾时，普通完成回调可把自身结果候选留在内部，但不能对外伪装已确定组合 Outcome；Finalizing 投影允许 outcome 为 null。PlanCompleted 则在提交该结果时直接拒绝尚未排空或实际未成功的必要 child。实际成功按 child Outcome 判定：ReadCompleted/StateCommitted/PlanCompleted，或 status=Succeeded 的 EffectResolved/LifecycleResolved；失败、取消、部分完成、未知和未修复的必要记录故障均不能冒充全成功。`collect_children()` 只允许收集此前保留的内部 pending 父结果，在收尾后核对所有子事实与实际结果并形成父 Outcome；已确定或已 Recorded 的 Outcome 不得通过该入口重写，父自身已知失败也不得升级为 PlanCompleted；遗漏子未知、删除子回执或不追加已发生提交都阻止父终结。

父 Terminal 前，每个 child 必须 Terminal 或由显式可信独立 owner 接管；转交时已有事实仍须计入父事实，转交后新事实由新 owner 持有。可信 owner 参数表示框架内已验证权限的持有者，模型不模拟认证。原子候选不得由未结束 child 继续访问；实际 candidate 生命周期约束由 D0.04/D0.05 及后续 Runtime 验证。

仍有本地代码运行时，即使必要记录失败也不能 Terminal/释放依赖。`isolate(owner)` 指明 IsolationOwner 接管寿命，执行仍 Finalizing，直到本地代码实际排空后才可终结。未知物理效果与本地代码运行分别记录：没有本地代码的未知 Effect 可 Terminal 并等候对账，不假装外部效果已停止。

## 6. 一致观察与迟到通知

受管理执行返回同次拥有型观察投影：ExecutionRef、host_incarnation、十进制字符串 observation_version、phase、progress、known_facts、可空 outcome、必要收尾状态及故障。版本是当前 host 与 execution 的公开观察版本，独立于业务 revision/CommitId，覆盖公开字段的每次变更；达到 uint64 上限时拒绝变更，不回绕。跨宿主必须重新取快照，不能只比较数字。

模型把每个方法作为一个原子语义步骤；`get()` 复制同一状态，不返回内部可变列表。派生 child 状态不未经父模型步骤混入公开投影，因此不能在同版本下悄悄改变父摘要。真实短锁/串行化与锁外发通知由后续 Runtime 实现，不以 Python 测试证明线程安全。

| 情况 | 合法投影 | 拒绝反例 |
|---|---|---|
| 原子候选进度 | progress.scope=Candidate、没有 PublishedFact、不声称状态成功 | 仅凭候选 percent 把 scope 写为 Published |
| DurableCommitted 未 Published | 已知 CommitFact，outcome 仍为空 | StateCommitted 没有匹配 PublishedFact；callback 自带假发布事实绕过协调器 |
| 已发布、必要收尾未结束 | StateCommitted + Finalizing + quiescent=false | 提前改 Terminal、wait 提前返回真、提前释放 |
| 必要记录失败且排空 | RequiredRecordFailed + 明确故障/封锁/修复策略，可 Terminal | 确定提交改成 FailedBeforeApply，或故障无解释地永远 Finalizing |
| 终态后旧通知/旧快照 | 忽略同 host/version 不更新的投影 | 用旧 Running 覆盖新 Terminal |
| gap/重连/host 改变 | 标记 needs_resync，再 get/wait | 把新 host 的较小版本当作旧 host 的回退数据直接合并 |

真实客户端顺序仍为 `subscribe 成功 → get/list → 合并有界缓存的通知`。Observer 只验证同执行版本合并与重新同步条件；不实现订阅管理、JSON-RPC、鉴权、cursor、背压队列或完整 A17 wire。丢失所有终态通知的测试仍通过独立的可靠本地 `wait_terminal()` 得到准确 Terminal。

取消意图与执行事实分离。父取消默认递归传递给仍由父持有的 child；已转交独立 owner 的 child 不再接收原父取消，取消后再绑定的 child 继承已有意图。即使父已应用并返回 AlreadyClaimed，尚在途的自有 child 仍接收取消意图，已发生事实不变。调用 `cancel()` 返回 Requested/AlreadyClaimed/AlreadyTerminal；应用后及终态后请求不改写任何已发生事实。取消控制结果不声称物理动作已经停止，客户端等待中断也不自动取消执行。

## 7. 实际验证入口与限制

```powershell
$env:PYTHONPATH = Join-Path (Get-Location) 'build/python-deps'
python -X utf8 -m unittest discover -s tests/model/execution_model -v
```

当前固定 expected manifest 为 [d0.03.expected.json](../../tests/manifests/d0.03.expected.json)，列出 43 个可独立执行的 T02/T06/T12/T19/T20 用例及精确 argv；自校验不从当前发现结果重写 expected。固定逐项 argv 通过严格适配器运行，要求恰好执行一个存在的用例；跳过、预期失败和缺失均退出非零。本包可用 CTest bootstrap 逐项执行；D0.06 负责正式工具链、JUnit 适配与正式采集器。

[Schema](../../schemas/outcome-v1.schema.json) 使用已安装的 `jsonschema==4.23.0` 官方 Draft202012Validator 校验 schema 本身和 golden；没有自写 JSON Schema 验证器。开发依赖安装在 build/python-deps；正式依赖锁定和重复验证归 D0.06。

[固定 golden](../../tests/model/execution_model/golden/outcomes.json) 共 54 份：24 份合法、14 份 Schema 层拒绝、16 份 Schema 结构合法但语义模型拒绝。每个向量分别记录 `schema_valid` 与整体 `valid`，测试不把模型拒绝伪称 Schema 自己能够拒绝。

本包原始运行证据位于 evidence/bootstrap/D0.03 的独立 run 目录。初始红为模型尚不存在的导入失败；后续行为红捕获 4 个失败和 3 个缺能力错误，另有必要记录故障投影和资源等待挂起恢复反例红；修复后保留独立绿记录。所有记录保留实际 argv、cwd、exit_code、输入哈希及原始 stdout/stderr，不覆盖失败、不手写 Passed。独立复核还增加实际子结果判定、对账后相反事实、owner 环、取消传播及已记录 Outcome 重写的反例，并保留对应失败与修复运行。最新完整结果由主集成者的证据索引绑定最终工作区版本。

未运行/未实现范围：真实 C++、Executor Conformance 后端、真实线程竞态、进程崩溃/磁盘耐久、正式 DTO/wire、设备或外部服务、D0.04/D0.05、D0.06 正式采集及 G0。模型方法的可信事实来源是合同前置，不构成针对恶意 native 模块的安全沙箱。本包人工评审待批准。

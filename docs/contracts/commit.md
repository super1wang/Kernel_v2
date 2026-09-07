# D0.05｜提交、许可与发布合同草案

本合同落实唯一规范 [架构 v3.3 A06/A11](../01_Architecture_v3.3.md#a06) 与 [D0.05 执行卡](../02_Execution_Plan_v3.3.md#d005)，并保持 [D0.03 Outcome 合同](outcome.md) 的九种结果与追加事实语义。前置 D0.01、D0.03 的包级 Passed 依据分别为[批准记录](../reviews/D0.01-approval.json)和[批准记录](../reviews/D0.03-approval.json)。本包是确定性 Python 参考模型；自动测试事实、D0.05 人工评审及 G0 放行分别记录。

## 1. 仲裁与状态机

```text
准备完整材料 → ReadyToCommit → CommitClaimed → [DurableCommitted] → Published → Finalized
                    ↓                 ↓
               Cancelled/Failed   KnownNotCommitted / Indeterminate（隔离）
```

`prepare()` 是已完成业务计算和输出验证后的候选建立；模型用整数 root、不可变 History 元组及 revision 表示一份准备好的状态。`run_validator()` 只允许在 ReadyToCommit。进入短提交序列后核对 revision、生命周期、ActivityLease、ResourceLease、DB 容量，再消费一次 ActionPermit，建立逻辑 CommitReservation。已 claim 的 root、binding 与 IntentKey 均冻结，writer 和 publication 不接受其被改写。

ActivityLease 只保护寿命，ResourceLease 只保护并发资源，ActionPermit 只决定一次效果尝试。模型分别检查三者；持有租约不能构造许可。Policy 发放的许可绑定 caller、精确 operation/组摘要、target、permission generation、lifecycle generation 和 deadline，并在服务端保存一次消费状态。

每次模型方法是一个原子语义步，因此 cancel/revoke 与消费的两种排列给出确定先后：先取消或撤权则不能消费、不能发送/提交；先消费则迟到取消或撤权保留已经得到授权的这次尝试。取消胜出只证明当前提交未应用，不回滚其他独立 Plan 步骤。许可截止时刻 `now >= deadline` 拒绝；重复许可、伪造 token 或任一绑定变化拒绝。可信 Policy/认证调用入口是模型前置，不是面向恶意 Native 模块的安全沙箱。

## 2. 同域 reservation 与等待

准备阶段可同时产生多个候选；同域只有一个 reservation，贯穿 DB 队列、writer 完成及 Published。第二候选在占用期间不能 claim；前序发布后仍必须重新核对其 base revision，不能悄悄用旧根覆盖新根。关闭先到则生命周期失效；claim 先到则关闭须等待该 reservation 明确结束。

模型固定参考锁序为 `domain 短锁 → policy 短仲裁`。内部互斥锁下禁止业务 Handler、可重入 validator、普通观察者和旧根析构；DB 等待时锁栈为空，worker 不被占用，仅 reservation 保留。writer 不反向同步等待 domain。真实 C++ 可以用等价串行协议，但不能改变线性化点或引入相反锁序。

队列容量在 permit 消费前保留；容量为零时拒绝且许可未消费。回调带 reservation_id 和 CommitId，必须匹配当前 reservation 及实际 writer 完成项，重复和迟到回调不能发布旧根或释放其他提交的 reservation。

## 3. 单一状态事务与可见性

StateDurable 的受信任 CommitBatch 在一个模拟 StorageSession 中依次准备以下字段，最后一次替换 durable 镜像：

| 字段 | 必须保存的对应事实 |
|---|---|
| state | 准备好的 root、revision、history cursor、lifecycle generation |
| ledger | CommitId、该 revision 的拥有型状态材料、对应独立 IntentKey |
| receipts | 以 IntentKey 为键的最终 StateCommitted 事实、CommitId、revision、结果 R |
| audit | 同一 CommitId/revision 的必要审计材料 |
| outbox | 同一 CommitId/revision 的业务成功事件材料 |

IntentKey 与 CommitId 是不同身份；测试显式使用 `Store/Restore/Principal/namespace/epoch/nonce` 六元组作为已验证外部身份，证明其回执与状态及 ledger 同存同失。同 IntentKey 已有确定提交时，另一个 CommitId 不能重新消费许可。模型默认短标签只是 fixture，不新增正式 DTO。外部 claim 的范围、epoch 及接受事务见 [Intent 合同](intent.md)。两个小模型固定两段协议的边界，不声称已实现真实 SQLite 集成。

逐字段写入前和 commit 前都可注入失败；私有 staged 镜像尚未提交，因此实际 durable 镜像完整保持旧值。结果明确回滚时为 KnownNotCommitted，释放 reservation、返回 FailedBeforeApply。模拟 DB 回应不能确定时，即使底层实际镜像可能已替换，也只记录带原 CommitId 的 UnknownFact，隔离域并等待读取 ledger 证明。

DB 已提交仅产生 DurableCommitted 和 CommitFact。`outcome()` 仍返回空，旧 Snapshot 仍读旧状态，Outbox 不能送出成功事件。Published 通过一次拥有型 `PublishedState` 替换同时公开 root/revision/history/lifecycle；之后才能投影 StateCommitted 并开放该提交的 Outbox。旧快照保有旧元组，不会读到新 root 加旧 cursor。老根退役和通知在短锁外完成。

内存模式也必须消费许可并通过同一个发布步骤。它不写 durable ledger，成功证据为 Volatile；空 delta 仍形成一次提交并推进 revision/History。Finalized 只在 Published 后完成一次必要收尾；本包不重复实现 D0.03 的完整 Finalizing、父子寿命与有界记录故障状态机。

## 4. 崩溃、恢复与事实

| 实际故障窗口 | 模型结果/恢复行为 | 固定反例 |
|---|---|---|
| 准备/容量/许可前 | 没有消费、没有正式状态 | T07.permit.*、T07.commit.capacity_reserved_before_claim |
| claim 后各事务写入前/commit 前确认回滚 | KnownNotCommitted；旧 root；无新 ledger/receipt/audit/outbox | T14.commit.all_transaction_crash_windows |
| commit 前结果未确认 | Indeterminate；实际镜像无提交；验证 ledger 后追加 NotApplied ResolutionRecord | T16.commit.unknown_result_reconciles_actual_storage |
| commit 后回调结果未确认 | Indeterminate；实际镜像含完整事务；验证后安装 root、追加 CommitFact/PublishedFact/Applied ResolutionRecord | 同上 |
| DB 成功而发布失败 | 保留 DurableCommitted；隔离；不能报 rollback，不能驱动 Outbox | T16.commit.publish_failure_preserves_ledger |
| ledger、回执、审计或 Outbox 损坏 | 隔离且不开放业务/Outbox；旧可见 root 不变 | T16.commit.recovery_validates_before_opening |
| Published 后重复/迟到回调 | 拒绝，不替换新根、不重复释放 | T14.commit.duplicate_and_late_callback |

恢复首先关闭业务和 Outbox，检查 state/ledger/Intent 回执/审计/Outbox 的身份与版本链，再安装可信 root，衔接原事实，最后开放读写及事件派发；`handler_runs` 不增加。UnknownFact 原记录保留，确定结论用 ResolutionRecord 追加。真正的存储独占、结构/预算/codec/组件版本链验证、SQLite 连接复核、磁盘与进程故障由 D5 实现，本模型不声称已经完成这些后端保证。

## 5. ExternalEffect

```text
未 claim → durable claim（稳定 EffectId）→ permit 消费 → 实际 Device 调用 → outcome 记录
                                  ↘ 取消先赢：CancelledBeforeApply
重启且 claim 存在、outcome 缺失 → Indeterminate → 外部证据对账 → EffectResolved + ResolutionRecord
```

Device 的调用历史和回执独立于本机 durable 镜像。SQL 事务不包住外部调用。获得 permit 不调用 Device、不产生成功；实际发送后但 outcome 尚未落盘仍不能向查询伪造已记录成功。结果丢失后的重启不会再次调用 Device；相同 EffectId 的确定 Applied/NotApplied/PartiallyApplied 只从 Device 回执取得。

缺少回执不能证明没发过，必须保持未知。已知部分效果使用 EffectResolved(application=PartiallyApplied,status=Failed)，不一律变成 Indeterminate。R 编码失败保留 EffectResolved 与最小回执 result_error。external_evidence 是 D0.03 规定的非空字符串数组，ResolutionRecord 的 evidence_ref 是独立证据引用字符串。

取消在消费前且仍能证明未发送时持久记录 CancelledBeforeApply；重启保留该事实。已消费后不再按取消改写结果。崩溃发生在任何 durable claim 之前可证明本地从未准入该效果，同一身份可以重新 claim；已经存在 claim 的恢复不得走此安全重试入口。

## 6. 本包核对与运行

| 规范项 | 固定模型覆盖 |
|---|---|
| A06.1 许可绑定、期限、一次性、三种保护 | T07.permit.cancel_claim_orders / revoke_claim_orders / binding_expiry_single_use / leases_do_not_authorize |
| A06.2 准备、消费、内存正式提交 | T07.commit.materials_prepared_before_consumption / memory_commit_and_empty_delta；T14.commit.materials_frozen_after_claim |
| A06.3 reservation、队列、锁序、回调 | T14.commit.reservation_serializes_and_rejects_stale_base / wait_and_lock_order / duplicate_and_late_callback |
| A06.3 发布一致与 Outbox gate | T14.commit.permit_and_durable_are_not_published / publication_is_one_owned_record |
| A06.4 存储不确定与发布异常 | T16.commit.unknown_result_reconciles_actual_storage / publish_failure_preserves_ledger / recovery_validates_before_opening |
| A11.4 state、Intent、ledger、audit、Outbox 同事务 | T14.commit.all_transaction_crash_windows / intent_and_state_share_transaction |
| A11.4/A12.5 外部效果与实际事实 | T16.effect.*、T07.effect.* |

固定清单 [d0.05.expected.json](../../tests/manifests/d0.05.expected.json) 合计 63 个实际用例（提交/许可/效果 34；去重/恢复 29），逐条使用 `test_Txx_component_behavior` 命名。清单独立维护，测试只比较预期与 AST 发现结果，不用发现结果重写预期。

```powershell
python -X utf8 tests/model/commit_model/run.py --all
python -X utf8 tests/model/commit_model/run.py --list
python -X utf8 tests/model/commit_model/run.py --case commit_model.test_commit_model.CommitContracts.test_T14_commit_intent_and_state_share_transaction
```

严格入口要求选定用例存在且实际执行一次；缺失、跳过、预期失败均失败。实现只用 Python 标准库；源文件 UTF-8/LF。开发期每次执行的完整源快照、argv/cwd/时间/真实 exit/commit/dirty/输入 SHA/原始 stdout-stderr SHA 保存在 [D0.05 bootstrap](../../evidence/bootstrap/D0.05/)。先有缺实现 red，后有五项行为边界 red（3 failures、2 errors），以及 IntentBatch、恢复后发送准入的缺能力 red；所有重跑使用新目录，不覆盖历史。D0.06 负责正式采集和 CTest 集成，独立技术审查及用户验收另记，不由本文件宣称 Passed。

交叉验证额外复用了已批准 D0.03 的真实 Schema/语义 validator：原54个用例内产生的22份提交Outcome与20份效果Outcome全部接受（该数量为独立审查前的历史运行）；工具只复用已有 jsonschema 开发依赖，不给模型新增产品依赖。首次交叉运行捕获 failure_phase 错用提交阶段 `Commit` 的 2 个错误，修复为实际执行阶段 `Running` 后独立重跑通过，失败日志保留。复验入口为 `python -X utf8 evidence/bootstrap/D0.05/outcome_crosscheck.py`；它不是新增测试族或替代正式采集器。

## 7. 独立审查后的材料冻结修复（待交叉复核）

独立发现轮次冻结于 `evidence/bootstrap/D0.05/independent-review.md`。随后同一审查代理经主集成者授权转换为修复实施者，先保留原54项源码/合同/expected快照，再增加8项固定反例并实际观察8 failures，随后实施修复。该角色转换不构成自行关闭独立审查；主集成者及原实施者另行交叉复核。

- recover仅适用于Durable域，易失域在改变隔离/开放状态、snapshot或Outcome之前明确拒绝；不从初始化空disk回退已Published的内存状态。
- prepare的整数root及commit/binding/base/history/Intent材料存为独立冻结见证；claim前必须与原准备材料一致，且revision=base+1、history=当前history+本CommitId、生命周期一致。改变候选内容必须重新prepare/validate，不能沿用旧结果已验证标记。原子模型根只接受真正整数，拒绝可变对象及bool伪装整数。
- publish与可信恢复安装时生成拥有型成功Outcome投影；查询返回该投影的副本，不能根据后来被更改的Attempt.commit_id/prepared/facts重造已经发生的结果。已有UnknownFact/ResolutionRecord在恢复投影中仍按原语义保留。
- Effect在durable claim时冻结EffectId、binding和Device所有者；authorize及send核对同一材料，变化在消费或发送前拒绝。实际发送回报形成独立拥有型事实，record/restart/reconcile沿原冻结EffectId和Device证据查询，不能由后来变化的公开字段/report擦掉已发生效果。

新增对应测试：`T16.commit.memory_recovery_rejected_before_mutation`、`T14.commit.candidate_chain_validated_before_claim`、`T14.commit.published_outcome_uses_frozen_material`、`T07.effect.claim_material_binding_is_frozen`、`T16.effect.sent_facts_use_frozen_identity`。它们只是本包模型反例，不将Python封装解释为恶意Native模块隔离边界。

## 8. 发送尝试的一次状态（交叉发现后的限定修复，待主集成者复核）

Effect.send 在进入外部调用前消费独立私有的一次状态；公开 report、已保存回执及 Outcome 都不能承担可重置的发送准入标志。一旦尝试，清空 report 或再次读取/记录结果都不重新开放发送。外部调用抛出异常时无法证明未生效，模型先形成 Indeterminate，再向调用方传播原异常；稳定 EffectId 只允许查外部证据并追加 ResolutionRecord。没有回执继续未知，已有 Applied 回执可对账收敛，均不得再次调用 Device。restart 不重置已经消费的一次状态。

固定反例 `T16.effect.send_attempt_is_once_before_external_call` 枚举记录结果前/后清空公开报告、外部生效前/后抛出异常，以及重启和对账后的再次发送。最初 red 原样保留于 `evidence/bootstrap/D0.05/20260907T062241Z-send-once-red-298e1b67/`；本条修复由发现者经授权实施，最终独立关闭由主集成者完成。

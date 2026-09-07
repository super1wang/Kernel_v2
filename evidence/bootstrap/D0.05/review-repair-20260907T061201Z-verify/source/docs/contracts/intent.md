# D0.05｜Intent、epoch、内部子键与恢复世代合同草案

依据仅为 [架构 v3.3 A12/A14](../01_Architecture_v3.3.md#a12)、[A11](../01_Architecture_v3.3.md#a11)及 [D0.05 执行卡](../02_Execution_Plan_v3.3.md#d005)。前置 D0.01/D0.03 已有真实用户批准；本文件固定可执行参考模型，不宣称 D0.05 人工批准或真实 Durable/SQLite 已完成。提交和外部效果许可语义见 [commit.md](commit.md)。

## 1. 外部逻辑身份与指纹

完整外部范围为 `StoreId / RestoreGeneration / Principal / namespace / epoch / nonce`。会话由服务端认证入口创建，StoreId、RestoreGeneration、Principal 和 host incarnation 不能由请求任填。namespace 经过服务器白名单验证。重新连接只创建新的会话 token，不改变 Principal，因此相同意图在新 Session 仍返回原执行身份。查询既有事实同样核对当前权限，撤权后不能靠已知 IntentKey 读取。

客户端 `ClientIntent` 在首次发送前保存范围、epoch 和 nonce；自动重试永远返回原 epoch/nonce。超过 floor 仍尝试原 key 并得到过期错误，不能自动换 nonce 或 epoch 使业务重新执行。新 key 表示新的显式业务意图。

模型 Request 的业务指纹覆盖精确 operation、contract、已归一化 args、稳定 target、前置条件及 PlanDigest；RequestId、TraceId、retry attempt 不进入指纹。同 key 不同业务指纹冲突；同 key 同指纹返回原结果或在途执行引用。

这里的指纹是不可变符号值元组的相等关系，**不是 ock.canonical/1 编码实现**。实际 canonical CBOR/SHA-256 指纹仍须在 D5.02-a 完成独立字节向量与正式后端可行性验证；不把 Python repr、model_bytes、普通 JSON/CBOR 编码器或 std::hash 当作规范指纹。模型 `model_bytes()` 只给 fixture 备份镜像做确定性完整性摘要，不能充当对抗修改者的签名。

## 2. claim 与接受事务

`claim(session, namespace, epoch, nonce, request)` 没有 skipEpoch 参数。它进入统一 writer 后按以下不可交错顺序操作：

1. 验证当前范围、会话权限与 namespace。
2. 查找既有 intent；存在则核对指纹并返回既有材料，先于 floor 拒绝规则。
3. 不存在时读取该范围的 `floor/current`；拒绝 epoch 小于 floor、未来 epoch、非法值和容量不足。
4. 在同一 staged transaction 插入唯一 claim、execution、拥有型 input 和最小 acceptance receipt。
5. 一次提交镜像后才能发布调度记录/Accepted。

测试在 claim、execution、input、receipt、commit 前逐点崩溃，确认不会留下部分接受或提前调度。commit 后但响应/调度前崩溃已经存在完整 durable 接受；重启保留同一执行并默认 Suspended，不能因客户端没收到回复重建第二个执行。模型接受事实保存与 State 的最终提交是各自正确的事务阶段，不能用 DurableAccepted 代替 State 的耐久能力。

同 writer 的重入由合同拒绝；claim 与 GC 两种先后分别枚举。claim 先赢时新建记录属于在途材料而被保留，即使其 epoch 随后低于 floor；GC 先赢时尚不存在的旧 key 被拒绝。事务方法的原子步是对真实 DB writer 的参考约束，不证明 Python 或 C++ 多线程锁正确。

## 3. 有界窗口与 GC

每个 `Store/Restore/Principal/namespace` 范围独立保存 current 与 minimumAcceptedEpoch。current 只能推进，floor 不得回退或超过 current。GC 先在有序 writer 下计算可回收集并确认 pins/references；只有 Terminal、无 pin、无引用且 epoch 低于新 floor 的回执可删除。

模型选用 A12.2 允许的安全写序：先持久推进 floor，再通过后续有序清理事务删除旧回执和其接受材料。两段同属同一个 writer 协议，禁止 claim 插入中间；即使进程在推进 floor 后崩溃，也只多保留旧材料。真实实现也可以把推进和删除放在一个原子 DB 事务，只要不留下旧 floor 加已删除回执。

| 崩溃位置 | 实际镜像 | 再次提交旧 key |
|---|---|---|
| floor 提交前 | 旧 floor、原记录完整 | 返回原事实/原执行 |
| floor 提交后、删除前 | 新 floor、原记录多保留 | 先查询仍返回原事实 |
| 删除事务提交后 | 新 floor、允许删除的记录已去除 | IntentExpired，不能复活 |
| 错误顺序：先删除、未推进 floor | 旧 floor、原记录缺失 | `check_invariants()` 判定复活窗口，固定反例必须失败 |

在途、Suspended、Unknown、已许可效果或有 pins/references 的材料不按低 floor 清除。已确定事实也只能在声明窗口结束后清理，不承诺无限保留。`accepted_keys` 是仅供模型断言使用的历史观察器；它使测试能发现错误 GC，不是要求产品无限保存所有过去的 key。

Volatile Host 在本次 host incarnation 内执行同样的 epoch/floor 协议。普通终态 LRU 不得删掉窗口内去重材料。容量耗尽拒绝新接受，或先结束旧 epoch/推进 floor 再回收；模型覆盖这两条行为。进程重启产生新 incarnation 丢失易失保证，ClientIntent 拒绝自动改范围重试未知效果。

## 4. 内部子执行命名空间

内部键是独立类型：

```text
StepKey(parent_execution, static_node_path, branch_iteration_path, logical_occurrence)
```

transport retry 和重复回调不进入该键。不同分支、循环路径或 logical occurrence 是不同子执行；相同步骤初次绑定的参数/target 指纹不能再改写。内部键不经过外部 current/floor 检查，因此持久 parent 可以跨外部 epoch 窗口继续有效子步骤；普通外部调用仍没有跳过 epoch 的参数或伪造 StepKey 的入口。

服务端先持久注册固定 parent、PlanDigest、精确允许步骤、principal 和寿命，再发放 ChildAdmission。消费时重新检查当前权限、父寿命、固定计划、精确 operation/contract、target、恢复世代与服务端 admission token。parent 记录和 child receipt 的 durable pin 一同作为模型不变量；GC 外部窗口不能删除该内部键空间。

恢复后的 parent 可在当前有效寿命和计划下重新发放可信准入，不能复用失效的旧会话 token。显式旧备份恢复立即撤销全部旧 ChildAdmission，新的恢复世代也不会自动调用历史 Handler。完整 Plan 驱动、child 执行事务、checkpoint codec、补偿与父终结后 pin 回收属于 D6，本包只固定上述身份和准入边界。

## 5. 普通重启、备份与显式旧备份恢复

普通 durable 重启保持 StoreId 与 RestoreGeneration，更新 host incarnation，旧 session/admission token 失效。已有确定 outcome 可在新认证会话下查询。已许可但无结果的效果进入 Unknown；不因为缺少 outcome 再次准入发送。没有发送许可的 durable 接受默认 Suspended；安全恢复策略须显式继续。

备份通过受控一致窗口复制已验证镜像并记录资产 manifest。资产在备份进行中和 manifest 存续期间保持 pin；即使 live state 不再引用，GC 仍不能删除备份需要的字节。模型验证镜像摘要、接受/父子材料结构、资产清单与实际摘要；合法摘要不代替结构检查。失败必须发生在替换镜像/世代之前，不能部分恢复后再发现材料损坏。

显式 `restore_backup()` 走独立入口，完成验证后保持 StoreId、产生新的 RestoreGeneration 和 host incarnation，撤销旧会话、授权句柄及 ChildAdmission，清空自动调度队列并封住效果发送。备份内旧事实仍按其原完整身份留作历史查询；备份后动作缺失返回 UnknownAfterBackup 诊断，不宣称 NotApplied。UnknownAfterBackup 是恢复诊断标签，不是新增 Outcome 变体。

旧 ClientIntent 不能自动改成新世代重发。外部历史对账必须提供非空证据引用，记录在本次恢复世代；对账开放当前新意图的发送门，也不会自动执行旧 key。`authorize_effect_send()` 实际检查当前范围、存在的 Claim、未发送状态和无确定 outcome，记录 EffectPermitted 并追加实际准入轨迹；重复、旧世代、Unknown 和既有结果都拒绝。固定测试证明“对账前阻止新发送 → 对账后仅新显式 key 可发送一次 → 重启无结果变 Unknown”的正反两条路径。该恢复门不替代 commit_model 中单独测试的 ActionPermit 仲裁。

模型不复制在线 .db 文件，也没有自动重播历史 Handler 的代码。真正的 SQLite Backup API、OS 存储独占、文件 flush/atomic rename、路径与 reparse point 检查、设备对账真实性及备份工具由 D5/D6 处理。对无法识别的恶意文件回滚，单个可回滚数据库不能提供外部单调性；需要时必须增加备份之外的可信记录，本包不虚构该保证。

## 6. 规格核对表

| 规范项 | 固定用例 |
|---|---|
| A12.1 稳定范围、当前权限、同意图重试 | T15.intent.same_key_returns_existing_identity / stable_principal_scope_and_permissions / server_scope_and_namespace_reject_forgery / client_retry_never_changes_identity |
| A12.1 语义指纹覆盖与冲突 | T15.intent.fingerprint_covers_all_semantic_fields |
| A11.4/A12.2 接受及 claim 原子事务 | T14.intent.acceptance_transaction_crash_windows；T15.intent.claim_gc_both_orders / writer_transaction_prevents_interleaving |
| A12.2 先查既有记录、未来/过期拒绝 | T15.intent.future_expired_and_existing_before_floor |
| A12.2 安全 GC 与水位独立 | T15.gc.watermark_and_delete_crash_windows / delete_before_watermark_is_detected / pins_inflight_unknown_and_references / scope_watermarks_are_independent_monotonic |
| A12.2 Volatile 不被 LRU 破坏 | T15.intent.volatile_capacity_preserves_window / client_retry_never_changes_identity |
| A12.3 内部键、固定参数与父 pin | T15.step.internal_key_and_parameter_binding / branch_iteration_and_occurrence_are_identity |
| A12.3 ChildAdmission 的权限/版本/寿命与外部隔离 | T15.step.admission_lifetime_definition_and_authorization / external_interface_has_no_epoch_bypass |
| A12.5 普通重启保留 store/restore、默认挂起 | T18.restore.restart_preserves_store_and_generation；T14.intent.acceptance_transaction_crash_windows |
| A14 备份资产 manifest/pins/结构验证 | T18.backup.manifest_pins_and_validation；T18.restore.structure_checked_before_generation_change |
| A14 新恢复世代、撤销旧准入、缺失不等于未发生 | T18.restore.old_backup_changes_generation_and_seals_effects / invalidates_child_admission |
| A14 对账与恢复后发送边界 | T18.restore.reconciliation_does_not_replay_history / new_effect_requires_reconciliation_and_new_intent |

完整的 62 项固定预期、精确命令和严格运行入口见 [manifest](../../tests/manifests/d0.05.expected.json) 与 [commit.md](commit.md)。bootstrap 运行事实位于 [D0.05 evidence](../../evidence/bootstrap/D0.05/)，正式采集/独立审查/用户批准仍分别由相应流程完成。该模型不新增 Runtime、产品模块、旧数据迁移、分布式事务或通用形式化框架。

## 7. 独立审查后的符号值与发送事实修复（待交叉复核）

Request.fingerprint现在构造拥有型、递归不可变且带类型标签的模型符号值：map按文本键排序，tuple/array保持次序与类型，integer/boolean/float明确区分；float以Python精确hex值见证保留负零，拒绝NaN/Infinity、未知对象类型、非文本map键及超深/循环结构。它仍是符号等价关系，不是新的wire格式、通用编码器或D5 canonical CBOR实现。接受时输入另行deepcopy；调用者后来改动嵌套dict/list不能改变既有fingerprint，改参重试得到IntentConflict。恢复验证还核对拥有型input重新产生的指纹与接受记录相同，重算普通备份hash不能绕过材料一致性。

发送准入作为record.effect_permitted独立保留，并与status/outcome一起进入恢复镜像。EffectPermitted或Unknown不能经set_status退回Claimed/Suspended；authorize_effect_send同时检查未许可标记、当前身份和Claimed状态。即使一个错误投影把status写成Claimed，独立标记仍阻止再次发送；重启无确定outcome的已许可项恢复Unknown。明确的外部NotApplied证据应走专门结果/对账路径，普通setter不是允许盲重发的入口。

新增固定反例为 `T15.intent.request_symbol_values_are_owned`、`T15.intent.fingerprint_preserves_scalar_types`、`T18.restore.unknown_effect_cannot_reenter_claimed`。原54项及其完整来源快照保留，现合计62项；修复实施者不自行把独立审查或人工状态改为Passed。

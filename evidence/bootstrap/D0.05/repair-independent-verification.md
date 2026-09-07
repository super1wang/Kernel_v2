# D0.05 审查后修复的交叉验证

角色：D0.05 原实施者，交叉检查 D0.04 代理实施的修复。本轮起初仅只读，不把修复实施者的 verification.json 当成独立批准；不改变此前冻结的 independent-review.md。

## 第一轮交叉检查

以 `review-repair-20260907T061201Z-verify/verification.json` 及 `repair-status.md` 为修复来源，与本代理原始 54 项冻结 ZIP 逐文件对比。检查了全部 8 项新增反例，以及候选材料、成功 Outcome、Effect 身份/发送报告、Request 归一化和恢复输入的一致性修复。

实际通过 8/8 定向测试：

- T16.commit.memory_recovery_rejected_before_mutation
- T14.commit.candidate_chain_validated_before_claim
- T14.commit.published_outcome_uses_frozen_material
- T07.effect.claim_material_binding_is_frozen
- T16.effect.sent_facts_use_frozen_identity
- T15.intent.request_symbol_values_are_owned
- T15.intent.fingerprint_preserves_scalar_types
- T18.restore.unknown_effect_cannot_reenter_claimed

原 5 项发现对应的主要修复路径成立：内存 recover 在修改前拒绝；候选在消费前与原准备材料/版本链匹配；发布结果以拥有型副本读取；Effect 使用 claim 时冻结的身份/绑定/Device；Request 的符号指纹按类型保存并隔离嵌套别名；已许可效果独立标志不能被普通状态 setter 抹掉。当前未把这些单项通过等同于完整包级批准。

## 新发现：P1，公开 report 清空仍能重复发送

冻结修复版本的 Effect.send() 虽已保存私有 `_sent_report`，发送前仍只用 `self.report is None` 防重复。只读内存探针实际执行：

```text
claim → authorize → send(Applied,Succeeded)
effect.report = None
send(Applied,Succeeded)
device.calls = ['effect-1', 'effect-1']
```

第二次发送被接受，同一已消费许可触发两次 Device 调用。公开 report 已明确被当作不可信可变投影处理，不能同时承担权威的一次发送状态。外部调用抛出异常时，报告也可能尚未产生；因此防护必须在进入实际外部调用前消费一个独立私有一次状态。不能只在成功返回后检查 `_sent_report` 是否存在。无法证明未发送的异常进入 Indeterminate，并沿原 EffectId 对账；禁止重新发送。

该发现已先报告主集成者，当前阻止整体关闭 D0.05 技术审查。主集成者随后授权本代理转换为修复实施者，仅修改这一条 Effect.send 防护及相应模型反例/预期/必要合同，保留真实 red/green。**本报告以上独立发现保持保留；该后续修复不得由本代理自签关闭，需父代理再核对最终 diff 与证据。**

## 限定修复实施记录（非独立关闭）

在以上发现留证之后，按主集成者授权仅增加 `_send_attempted` 权威一次状态，在进入 Device.apply 之前消费。公开 report 被清空不影响该状态。外部调用或返回材料处理抛出 Exception 时先进入既有未知/对账路径，再传播原异常；restart 不重置一次状态。

新增 1 个固定 case（总 63 项）实际枚举四条边界：结果落盘前/后清空 report；外部调用在生效前/后抛出异常。它还检查异常后 record 拒绝、重启后发送拒绝、无回执保持未知、有回执对账到 Applied、对账后继续拒绝发送，实际 Device 尝试计数保持 1。

| 独立运行目录 | 实际结果 |
|---|---|
| 20260907T062241Z-send-once-red-298e1b67 | exit 1；1 case，2 failures + 2 errors |
| 20260907T062351Z-send-once-green-b54981e2 | exit 0；定向 1/1 |
| 20260907T062414Z-send-once-full-fa0bc5cf | exit 0；全量 63/63 |
| 20260907T062408Z-send-once-outcome-1110ef21 | exit 0；63/63；D0.03 validator 接受 CommitDomain 28、Effect 29 份 Outcome |

四次运行均有各自 argv/cwd/时间/真实退出码/commit/dirty/输入 SHA/完整源码 ZIP/原始输出 hash。已重新计算 8 份原始日志和 4 个源码 ZIP 的 SHA，全部相符；三次 green 使用同一输入 SHA，运行期间来源未变化。5 个本轮修改文件 UTF-8/LF 核查通过，冻结 SHA 与日志复核记录见 `send-once-verification.json`。

本轮仅修改 commit_model/model.py、commit_model/test_commit_model.py、原 d0.05.expected.json、commit.md 的必要合同和数量，以及 intent.md 的数量引用。未改 dedup 行为、D0.03、共享工具或 Git；此前所有独立审查与失败证据保留。**发现者已转为此限定修复的实施者，不能自签关闭 P1；主集成者须独立检查最终 diff/证据后决定审查结论。**

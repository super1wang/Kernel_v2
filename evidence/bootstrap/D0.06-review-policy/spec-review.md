# D0.06 自动复核与验收规格复核

复核者：Codex 独立规格复核代理（actor_type=AI）。
复核时间：2026-09-07T07:50:20.457523+00:00。
结论：规格复核通过；本结论仅覆盖下列文件字节，不是人工批准、最终代码质量批准或 G0 自动运行结果。

依据：docs/01_Architecture_v3.3.md A22.3、docs/02_Execution_Plan_v3.3.md E03.3–E03.5、D0.06/G0，以及用户明确指令“自我复核和自动验收，无需人工”。本次政策只调整评审身份及验收流程，内核范围、工作包前置、门禁顺序和必需技术检查保留。

1. 历史 D0.01–D0.06 报告和 human 状态保留原字节；accept_gate 另写新决策，输出已存在时拒绝覆盖，不补造人审或退出事实。
2. 新 AI 模式要求归档显式政策、精确输入摘要及 spec/code 两种 AI 复核。校验器验证政策来源清单、ZIP 内字节、记录摘要和快照；缺任一批准不得 Passed。无显式政策的旧模式仍要求 human，不通过删除该要求偷免。
3. 历史验收先 audit 原始证据，再核对矩阵完整性、每配置 expected 与实际结果、同一来源身份、全部声明输入的 Git blob 字节及原 AI 复核文件和技术证据摘要。验收规则和政策摘要进入新决策。accept_gate 是通用历史候选工具；本次调用限定 G0 九份报告、194dcdf 实现和142项输入，不把该历史结论冒称当前修改后的工具已运行旧矩阵。
4. 本轮发现政策 version 未校验，缺失或未知政策版本仍可认可。实现者已补 type(version) is int 且 version == 1，明确排除 bool；新增缺失、2、true、字符串1四种反例。已读取修复后的实现和测试，问题在规格层面关闭。原始反例证据 20260907T074718Z-version-red-524d5c1a/command.json 记录 exit_code=1，stderr 记录四种子情况失败；失败材料未覆盖。

未发现其余本范围内的规格阻断或削减自动通过条件。本代理未运行全量旧矩阵或新 AI fixture，也未修改实现/测试、提交或推送。新版本测试实测和最终代码复核由独立证据及记录负责，不能仅凭本文推导其 Passed。

## 审核输入 SHA-256

| 文件 | SHA-256 |
|---|---|
| tools/evidence/review_policy.py | `35f3628a58d464d27978fbe9eaa58d2944f32eca766c31215e729f9048bbdd40` |
| tools/evidence/accept_gate.py | `a33a4efe90b080a64ca6579922eb0ec55816076a018faef1f358096203016d38` |
| tools/evidence/common.py | `f4b2e097027f4933dc6204c8300ed20f35c90523ad5f8e2fa0fed266fe08fd62` |
| tools/evidence/run.py | `8dedb1393268b5f07f200c16ee64293f531e11aeaba2a71fe82012f958519835` |
| tools/evidence/validate.py | `d391a1a854ddf0ef3f7f773cd70287de543617c88b4309033cb31d9c27ab192d` |
| schemas/evidence-v1.schema.json | `eeeca84a12fa7e9f62234902aa77f2147fb174f65c8a7a748456fcc2c9ab2a32` |
| tests/tools/evidence/test_review_policy.py | `8b2808cfb233e9b0a58b9d47908ade708c84e3cf3e567db374df01078d11e8f3` |
| tests/tools/evidence/test_gate_acceptance.py | `0f0ae77272e35d4e5915d8456f6b1edd3eb56314b540656db4bf19db57669486` |
| tests/tools/evidence/test_review_policy_runner.py | `95830eafadf0a1ca69e1e00e68d79a026cc5aa22b949f19ca713db173d934f5c` |
| docs/reviews/automatic-acceptance-policy.json | `712ad1e661300f538cacac5af1d5111fdf6418624369f02a900e992610786d80` |
| docs/plans/automatic-acceptance.md | `ad5f9c3d75a293e6ccb9dd63d85cfbb8429f2c8acc435d4f24d253aab8407c54` |
| tools/evidence/README.md | `bc6ab19e5908e8072ab7a99718175419735503974a68dcb3d030e1a59ec3c3f8` |
| tests/runs/g0.json | `e153e524235f933d1f1360aaf2ae6d600bd8ae3517b0036b44ce2e79ecbe880a` |

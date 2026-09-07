# D0.06 自动复核与验收工具独立代码审核

- 结论：Approved（本轮代码质量审核通过，未发现未解决的必须修复项）。
- 审核身份：AI，独立审核代理 `/root/implement_d004`；未参与本轮工具实现或修复，不代表人工批准。
- 审核时间：2026-09-07T08:00:41.036966+00:00。
- 差异基线：`817354ab61973d45601f7a802f3b9320bb468741`。新增未跟踪的政策、验收实现及测试亦按完整文件审核；不以 `git diff` 只显示已有跟踪文件作为完整范围。
- 范围：父任务指定的五个工具模块、evidence Schema、两组政策测试及验收测试；参照已通过的 `spec-review.md` 与用户“自我复核和自动验收，无需人工”的政策。仅写本记录，未改实现、测试、历史报告或 Git。

## 代码结论

1. 新 AI 模式必须显式绑定政策格式、模式、规范相对路径及 SHA-256；政策字节纳入来源输入和源归档。评估使用归档字节，严格检查整数版本 1、用户授权文本、spec/code 两类要求、AI 身份以及禁止伪造 human 字段。缺失评审保持 InProgress，矛盾或篡改形成 Failed；不以缺失评审自动批准。
2. 新报告额外归档原始评审 JSON，校验 ZIP 摘要、唯一成员、成员集合、原始字节摘要和嵌入快照。当前源码检查与历史归档检查保留独立含义；政策或评审快照变化不能替换被审核输入。错误沿 audit 汇入包级失败；验收端也检查 audit 错误与自动执行状态。未捕获的损坏输入异常会终止命令，不能生成成功决策。
3. 没有显式政策时沿用原 human 必需模式，原报告的 review_required、自动结果和包级状态仍参加校验。新 Schema 的 review.archive 保持可选，由显式 AI 模式的语义检查要求存在，兼容已有报告。
4. 追加验收逐份重验原始报告，要求固定 task/profile 矩阵无缺项、无重复、无额外项；矩阵本身须属于被测来源。全组使用同一提交、实现输入摘要及依赖摘要，逐输入读取候选 Git blob 核实字节。dirty 字段仅作为原始事实记录，不能代替源码一致性证明。
5. 追加验收验证 AI 评审的任务、被审核输入、身份、完成状态及技术证据摘要，记录政策、规则、报告和其他决策输入摘要，并在结束时再次检查输入/已加载规则变化。历史 human 字段保持原样。结果明确声明覆盖历史候选及原始运行，未把当前新增工具测试表述为原 G0 矩阵重跑。

## 独立验证

- 亲自以 `python -X utf8 -`、`sys.dont_write_bytecode=True` 在内存中运行 `test_review_policy` 的 unittest 套件：21/21，0 failures、0 errors，退出码 0。包括缺失/不支持政策版本、归档成员重复、来源或任务错误、错误身份、human 冒充、弱化必需评审、Pending 及旧 human 兼容反例。
- 2026-09-07T07:58:09.192202+00:00，直接调用 `accept()` 对 `acceptance-probe-907a3324fc/integration-command.json` 中原九份报告进行只读重算：gate_status=Passed、automated_status=Passed、errors=[]、review_errors=[]，九组技术评审齐全。原报告记录合计 372 次测试执行。此次运行仅重验原证据，没有重新执行该历史矩阵的 CMake/CTest，也没有写正式验收决策。
- 对真实矩阵分别移除一项、增加重复项、增加额外项，以及空矩阵，四个反例均拒绝；完整矩阵通过。以候选实际源文件配全零错误 SHA 调用 `candidate_errors()`，返回 `candidate Git bytes differ: cmake/Dependencies.cmake`。该独立探针退出码 0。
- 独立读取并核实最终回归 `all-final-2c8d2e4cf3/verification.json`：21 项政策、10 项 AI 真实夹具、34 项 legacy、3 项 Git/矩阵，共 68/68。逐一检查四份原始 unittest 日志的 SHA、实际数量及 OK，四份命令记录的 SHA、Exited、exit_code=0、observed_exit_code=0、active_after=0，以及全部冻结源码 SHA。44 个夹具的 `selftest-fixtures.zip` 摘要匹配，2,975 个归档成员 CRC 检查通过。此 68 项为核实主代理真实运行证据，不宣称本审核代理独立重跑全部真实构建夹具。
- 最终回归 verification.json SHA-256：`2e195c6525ff3800a118f548d843b03b458d0dcebb9f41030b51d3b10a287c95`。

本次只读验收重算覆盖的历史实现提交为 `194dcdfeeac55cf7088b8fce8bdb65184e40fd95`，实现输入摘要为 `63230d872612046ee7eec0ad30a9806926d8c9118253993b7b6103298d02d84b`，依赖锁摘要为 `e6ee3e75590bb5d7d8fcf8d133fd237f823f496512444a0cc344684c88c492e7`。本记录仅完成工具代码质量审核；正式追加 G0 自动验收由父任务使用冻结工具另行生成。

## 本轮冻结文件

以下 SHA-256 在审核结束时重新从实际文件读取，并与最终回归记录交叉核对；源码均为 UTF-8 LF。实现或政策变更后必须重新核对本记录适用性。

| 文件 | SHA-256 |
| --- | --- |
| `tools/evidence/review_policy.py` | `35f3628a58d464d27978fbe9eaa58d2944f32eca766c31215e729f9048bbdd40` |
| `tools/evidence/accept_gate.py` | `a33a4efe90b080a64ca6579922eb0ec55816076a018faef1f358096203016d38` |
| `tools/evidence/common.py` | `f4b2e097027f4933dc6204c8300ed20f35c90523ad5f8e2fa0fed266fe08fd62` |
| `tools/evidence/run.py` | `8dedb1393268b5f07f200c16ee64293f531e11aeaba2a71fe82012f958519835` |
| `tools/evidence/validate.py` | `d391a1a854ddf0ef3f7f773cd70287de543617c88b4309033cb31d9c27ab192d` |
| `schemas/evidence-v1.schema.json` | `eeeca84a12fa7e9f62234902aa77f2147fb174f65c8a7a748456fcc2c9ab2a32` |
| `tests/tools/evidence/test_review_policy.py` | `8b2808cfb233e9b0a58b9d47908ade708c84e3cf3e567db374df01078d11e8f3` |
| `tests/tools/evidence/test_review_policy_runner.py` | `95830eafadf0a1ca69e1e00e68d79a026cc5aa22b949f19ca713db173d934f5c` |
| `tests/tools/evidence/test_gate_acceptance.py` | `0f0ae77272e35d4e5915d8456f6b1edd3eb56314b540656db4bf19db57669486` |
| `docs/reviews/automatic-acceptance-policy.json` | `712ad1e661300f538cacac5af1d5111fdf6418624369f02a900e992610786d80` |
| `docs/plans/automatic-acceptance.md` | `ad5f9c3d75a293e6ccb9dd63d85cfbb8429f2c8acc435d4f24d253aab8407c54` |
| `tests/runs/g0.json` | `e153e524235f933d1f1360aaf2ae6d600bd8ae3517b0036b44ce2e79ecbe880a` |

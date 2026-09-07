# D1.02 当前源码自动验收入口独立 AI 意见

结论：AI Approved。当前D1.02可以使用未修改的tools/evidence/gate.py，对完整三配置正式报告执行当前源码audit并汇总；结合本次实际Git字节核对与AI审核证据核对，满足当前自动验收政策。这不是把失败的accept_gate.py报告改为成功，也不是删除检查或减少矩阵。

## 政策及入口差异

automatic-acceptance-policy.json要求真实固定用例、真实规格/代码AI审核且绑定精确输入、保留历史并追加决策、绑定政策和验收规则摘要，未规定唯一CLI必须为accept_gate.py。

accept_gate.py是历史候选验收入口：内部audit(path,None)，另外要求matrix路径及其SHA存在于每份被测source inputs。首次调用使用evidence下新建matrix，不在source输入中，故三份均得到gate matrix differs from tested source；该检查按本入口合同正确失败。原目录automatic-20260907T150134Z及Failed报告必须永久保留，本意见不豁免或改写该入口结果。

gate.py调用audit(path,root)，完整核对当前来源集合、collector、固定expected、实际CTest发现/每轮执行/JUnit、3CHECK、构建及runtime归档、AI审核快照和政策，然后核对矩阵missing/extra/duplicate及单一commit/input/依赖锁身份。当前任务属于当前未变化源码汇总，使用此既有入口适用；D1.01也是同一入口的追加验收，但本结论依据当前源码及实测，不仅依赖历史先例。

## 本次独立实际核查

- 使用失败轮原matrix，不修改其三个required_runs：D1.02 Debug、Release、ASan齐全，固定expected分别191/191/193，3CHECK/配置齐全。矩阵与之前冻结并独立批准的三个正式run manifest、expected规格审核一致；未删除任何必需配置、用例、检查或重复要求。
- 独立执行未修改gate.summarize(matrix,全部三份reports,当前ROOT)：errors=[]、missing=[]、gate_status=Passed。运行结果保存在review-acceptance-route-460d7b/independent-gate-audit.json。这重新验证原始归档和实际结果，不只读取报告中的Passed。
- 独立调用既有candidate_errors逐项比较189个来源输入与Git提交c6950773dd4134f39059ee5e9fa0acc12541ff96：errors=[]。共同输入摘要daad9b3588510afff1865f9717ea4d3da47bc788d375a8b585c6e67b29943aca。Git核对记录见git-verification.json。
- spec/code均actor_type=AI、Approved、绑定上述摘要；所有声明的技术证据当前SHA重新核对一致，见ai-review-evidence.json。不存在伪造human Approved。
- 矩阵、政策、未修改gate/validate/accept_gate规则以及三正式报告SHA见binding.json。

## 追加验收记录要求

主任务应在新的决策目录调用既有gate.py生成正式摘要，并追加acceptance-context，明确当前源码入口、政策SHA、规则SHA、完整矩阵/三报告SHA、Git核对、两类AI复核以及本次失败目录和失败原因。保留旧工具通用人工说明原文，用上下文明确实际AI政策；不能编辑旧报告。验收仅覆盖D1.02，不能据此将G1或后续包直接标Passed。

在上述准确记录下，选择当前源码汇总入口并未规避自动验收政策：未冻结临时matrix的问题由冻结完整profile清单、固定expected和独立矩阵一致性核对提供依据；当前audit和Git验证均实际执行且无错误，不以减少矩阵来获得Passed。

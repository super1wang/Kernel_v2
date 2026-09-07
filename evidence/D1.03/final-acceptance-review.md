# D1.03 最终验收证据独立 AI 复核

actor_type：AI；审核者：review_tools。结论：Approved，当前D1.03完整验收证据支持包级Passed；不代表G1或后续工作包自动通过。

## 独立实际核验

本轮再次调用未修改tools.evidence.gate.summarize，对tests/runs/d1.03-matrix.json列出的三份正式报告执行当前ROOT完整audit，而非只读取已有Passed。结果errors=[]、missing=[]、gate_status=Passed，机器结果保存final-acceptance-review/audit.json。

| 配置 | CTest实际通过 | 独立CHECK | registration主体 | wrapper/原始流 |
|---|---:|---:|---:|---:|
| Debug | 216 | 3 | 25 | 5 / 46 |
| Release | 216 | 3 | 25 | 5 / 46 |
| ASan | 218 | 3 | 25 | 5 / 46 |
| 合计 | 650 | 9 | 75 | 15 / 138 |

完整audit重新核对固定expected、发现与JUnit实际执行、命令状态/原始流、来源与构建/运行归档及审核身份，没有缩减矩阵。三个CHECK均为dependencies.python、conformance.bootstrap、models.outcome_consistency；不计入CTest数。

## 五个wrapper归档

直接打开每份正式runtime-artifacts.zip，逐一核对registration五份commands.json及所有raw引用的实际归档字节size/SHA。每配置共23条子命令：cold_docs_separation 1、manifest_owned_budget 4、shape_compile_contract 7、internal_component_boundary 4、handler_not_exposed 7；总46份原始流，无缺失引用。

所有子命令status=Exited，assigned_before_resume=true、active_after=0、terminated_owned_job=false。编译拒绝及Runtime不可用反例的非零exit_code按固定驱动预期保留，不被伪写为0；wrapper主体本身经CTest实际Passed。具体各命令exit_code序列见final-acceptance-review/verification.json。实际产物最低项与归档匹配均已由完整audit重算。

## 来源及审核

三份报告共同commit为fb08a1955ebfc8b8d97062962b61cac901880953、共同输入摘要75090b8927158e4476b812c504be76a83144327ad4aaee5f20acfc63b3aa496b。本轮独立调用既有candidate_errors对204项来源逐项与该Git对象比较，errors=[]，并由当前audit确认当前被测输入仍一致。

spec/code均明确actor_type=AI、review_status=Approved、绑定共同精确输入摘要；所列技术审核材料当前SHA全部重新核对一致。没有改写成human Approved，也没有将旧包审批作为当前实现审批。

正式报告SHA与门禁记录逐项核对一致：

- Debug：9ef19842233a6b385401e5d83431f2139e341dc73ce9a6f195fbe310641e7765。
- Release：074e62d4404e673080275313d62a01521cbd159bb4d29264692255707b2327cb。
- ASan：511e4b0569660b76fb0996bc005579a37e819d7db7129148f8f3e7dd3a07b4fb。

本意见依据真实报告和归档复算，不重新编译注册器，不覆盖旧失败/修复报告、不修改源码、不提交。可由主任务将当前门禁、来源核对和中文范围/证据索引作为新的验收节点提交；后续包仍需逐项核对自身前置。

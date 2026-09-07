# D1.04 正式 Release 验收证据独立 AI 复核

审核者：review_tools；actor_type：AI。结论：**Release 单配置 Approved / 实际 Passed。整包等待 ASan 和完整矩阵 gate。** 仅只读核验既有报告与归档，没有重跑SDK/CTest或改源码。

报告：evidence/8978b277850b-1a044ad0c5b5/win-msvc-release/D1.04/20260907T200010Z-4529dec17f39/report.json；重算SHA256为 `8667c58e316f049ff0ec1e260abae1c820fa7dafd32c306dc80ab12e7491c886`。

独立调用当前未修改tools.evidence.validate.audit(report, 当前仓库)，返回Passed、errors=[]；完整核对expected、发现/实际结果、命令与原始流、源码/build/runtime归档、最低材料、当前来源和AI审核绑定。实际Release单轮251个CTest全部Passed，包含35个policy主体；dependencies.python、conformance.bootstrap、models.outcome_consistency三CHECK全部Passed。构建ZIP177项、runtime ZIP1583项在完整audit中核验。

另解析原始JUnit和授权子命令归档，确认5个policy wrapper、24条子命令、48份原始流，raw字节SHA匹配；所有子命令Exited、active_after=0、assigned_before_resume=true。预期负编译/Runtime拒绝仍保留实际非0，未冒充成功退出。

来源commit `8978b277850beb98ab9da16838052d6f32a7a4c4`、224项摘要 `1a044ad0c5b50f1efa53a82125430a7002a61a1fbbd989f8f1ca2e1e50ac8352`；与正式Debug的全部输入对象及commit逐项相同。Debug独立审核已重新读取该commit的224个Git blob并核对字节、长度与身份，因此此处相同集合沿用该Git证明，不声称重复执行过Git核验。报告原dirty标志不改写为clean。

spec/code审核均为AI Approved，绑定相同输入摘要；review归档和所引证据SHA通过完整audit，不标记human Approved。机器可读结果见release-acceptance-audit.json。

目前正式已完成Debug251+Release251与六CHECK；不得据此宣称D1.04整包Passed。待ASan253及三CHECK完成后，继续核对同commit/同来源和固定三run矩阵，最终应为755CTest、9CHECK。

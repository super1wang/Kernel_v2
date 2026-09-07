# D1.04 正式 Debug 验收证据独立 AI 复核

审核者：review_tools；actor_type：AI。结论：**Debug 单配置 Approved / 实际 Passed。整包等待 Release、ASan 及完整矩阵门禁。** 本次只读审核，没有重跑SDK、CTest或修改源码。

正式报告：evidence/8978b277850b-1a044ad0c5b5/win-msvc-debug/D1.04/20260907T194244Z-e3a6ad2e91a8/report.json。SHA256 `44e16d1f26dbf890769f4cd83197b5b53a45cc0c0d504a21c4a5493491c50029` 已重新核对。

调用当前未修改 tools.evidence.validate.audit(report, 当前仓库) 完整核验，返回 package=Passed、errors=[]。它核对固定expected、发现/实际JUnit、命令与原始流、源码/构建/runtime归档、最低材料、当前来源和审核绑定；不是仅信任报告自称Passed。构建ZIP177项、runtime ZIP1583项均在完整audit范围内。

实际单轮251个CTest全部Passed，包含D1.04全部35个policy主体；三CHECK分别为dependencies.python、conformance.bootstrap、models.outcome_consistency，全部Passed。独立再次解析原始JUnit及授权子命令归档，确认5个policy wrapper、24条命令、48份原始流，子命令均Exited、active_after=0、assigned_before_resume=true，raw字节SHA匹配。负编译/Runtime拒绝属于预期负控制，并不把其非0伪写成功退出。

来源提交 `8978b277850beb98ab9da16838052d6f32a7a4c4`，完整224项摘要 `1a044ad0c5b50f1efa53a82125430a7002a61a1fbbd989f8f1ca2e1e50ac8352`。除检查source-provenance-8978b27.json外，本审核独立重新运行只读git cat-file --batch，从该commit读取224个blob，逐项核对长度、SHA256及Git blob身份，全部等于正式报告输入。报告dirty=true按原样保留，不称工作树clean；受审全部输入与指定Git提交的原字节一致，已用直接blob核对证明，不能仅以dirty标志推翻或替代该证明。

spec/code两份审核记录均actor_type=AI、Approved并绑定同一最终输入摘要；历史技术报告SHA及review archive通过audit。审核身份不伪称human，局部实现测试由本代理编写的事实沿用独立review_contracts覆盖复核分工。

新增机器可读核验：debug-acceptance-audit.json。此文件和本报告只确认Debug配置，未把单run的package_status=Passed外推为D1.04整个三配置包通过；后续两份正式报告完成后需继续核对同commit/同输入及总计755CTest、9CHECK。

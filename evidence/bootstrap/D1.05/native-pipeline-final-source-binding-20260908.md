# D1.05 管线最终复核来源摘要补充

actor_type：AI。本文件补充 native-pipeline-independent-final-20260908.md 的输入摘要绑定，不修改原报告内容、范围或结论。

已由审查员读取 tools/evidence/common.py 的 digest 定义，并实际执行其规范化 JSON 摘要：对 integration-debug-61b6f6c6c86d/source-inputs.json 的254项输入得到：

`90f1fb73189b4c009ad3cf923f405e52f93f6ad58b6fc569729d009bc92a1291`

该值是正式 build_inputs_sha256 语义的完整输入摘要。它与原报告的原始 source-inputs.json 文件字节 SHA-256（d9da21400d2348ccddf2973f71fa6b19bf20d4231b0fb9127ce75a895d472e16）用途不同，不构成来源矛盾。Debug/Release 两份清单文件字节一致，且原报告已重新核验两份归档的全部254项与当前工作树，0不匹配。

因此最终核心管线 **SPEC Approved；CODE Approved** 明确绑定上述规范化输入摘要及原报告的逐文件 SHA。所绑定原报告为：

- 文件：evidence/bootstrap/D1.05/native-pipeline-independent-final-20260908.md
- SHA-256：a62c5774770d2fe9491897f3ba6f57dd0efe80c807cf2d7a9fee8b1bd419db8c

仍待完成的不是核心源码静态复核，而是ASan最终运行、非本报告范围的分配/包装器独立审核，以及完整工作包正式manifest和包级验收；本补充不将这些项目记为Passed。

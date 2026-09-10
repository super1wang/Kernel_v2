# B5/G3 收口 Embedded 预算来源更新

日期：2026-09-10T03:24:31.982259+00:00。审核者：Codex AI，依据自动验收政策。正式运行前批准新来源绑定；不表示测量或 G3 已 Passed。

C1/C1b/C2 改变 Runtime、CoreContracts 及安装头，旧 G3-C 报告保留历史事实，本轮不复用其编译输入结论。固定消费者、6 组 ABBA、Release 占用与同一二进制启动、Debug/ASan 全线程分配诊断、三线程结构上界及 40 次 Invoke 零分配要求均保持原合同。

原四组 budgets.limits 逐值核对不变，其规范化摘要为 `b9f73588c0f0f2da729d9a678daa938c46425c731b4c20015c9f15573f75981a`。NativeSubset 所有字段亦保持不变；不根据新运行结果调整阈值或样本。已有校准仅解释原阈值，不能代替新采样。

新方法摘要（同机 Python 3.13.3，包含当前全部 Embedded 方法输入）：

- win-msvc-debug：`a4ad35b5d7c9f75cd69ee86e7b918b930abe69d75771038a4b3784c0a1592a1e`
- win-msvc-release：`c013ebbbfcd80255dd41482f4bdff393633d0ce604dcdde5bdc6ab36b800f49d`
- win-msvc-asan：`29ab11246f5b90da9e6e18583ca3d9313f242a7f89c742d24870b53bf3b524b0`

AI 技术结论：批准在上述原预算下刷新。正式报告须核对实际安装组件、依赖图、编译配置、原始进程/模块/线程/分配样本与结束时输入摘要；任一失败保持原文并阻止收口。

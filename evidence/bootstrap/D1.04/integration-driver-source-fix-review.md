# D1.04 集成来源稳定检查 P2 关闭复核

actor_type：AI；review_tools。结论：Approved，原integration-driver-review.md的来源新增遗漏P2已关闭。只批准限定集成工具修复，不代表35主体或整包Passed。

当前build/d1.04-integration.py SHA：7de37ff3aab400966f3cffffc7110dc65d3b21e932431938110105018a88b9f3；旧已审SHA：569b4c3cc5ef398e15e41fcfe8048f42119f0f8a95a6ed56c5def129f40badbb。初始expected已在开始时冻结，末尾重新读取当前spec并执行完整inputs，将added/removed/modified逐项比较，变化返回SourceChanged并断言拒绝。

## 实际限定反例

独立证据目录integration-source-controls-3fc26a2290包含old/new-driver.py原字节、check.py、两条实际owned命令、stdout/stderr、逐场景结果及SHA清单。未import或执行驱动顶层：仅AST截取旧changed表达式至断言、新after_rows至断言；实际调用未修改tools.evidence.common.inputs。在build下为每个场景创建独立小型自有fixture，不接触工程行为来源。

旧块在新增src/b.txt后仍status=Passed、rejected=false，要求拒绝的外层断言实际失败，owned命令Exited1。该结果重现原P2，不是手填红色。

新块四场景实际结果：

| 场景 | 结果 | 是否拒绝 |
|---|---|---|
| 新增src/b.txt | SourceChanged，added记录准确 | 是 |
| 删除src/a.txt | SourceChanged，removed记录准确 | 是 |
| 修改src/a.txt字节 | SourceChanged，modified记录准确 | 是 |
| 文件集合/字节不变 | Passed，三差异集合为空 | 否 |

四个限定断言全部成功，新owned命令Exited0；两轮外层active_after均0，未启动CMake或完整构建。fixture结果中的cases为空是明确隔离的来源检查测试上下文，不是35项CTest通过记录。

旧失败报告与初版driver保留，新增本关闭意见，不回写旧结论。源码最终停止变化后才可启动完整integration；完整native、五wrapper安装及正式三配置验收仍须实际运行和独立归档审核。

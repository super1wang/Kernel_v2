# D1.02 正式运行归档清单最终独立 AI 复核

结论：AI Approved。范围仅为三份当前正式运行清单及其归档匹配规则；不是human Approved，不代表尚未执行的正式运行或D1.02包级验收已Passed。

## 绑定文件

| 文件 | SHA-256 |
|---|---|
| tests/runs/d1.02-win-msvc-debug.json | 1f651ad3114c73d9d9b018925f93f59aa0c2bbe35a48beb50078bfac32e99174 |
| tests/runs/d1.02-win-msvc-release.json | f664d1289d501af4ad64cfceff3d72b6a471a82129e6c27bb6e45f457e5d7b7f |
| tests/runs/d1.02-win-msvc-asan.json | a4722b6687c4ebb8ed2ad208a5d24667bfa3b5c80866d1c5455c20253b737b8e |

## 独立实测

以当前磁盘所有runtime_artifact_patterns执行与collector相同的glob收集，再对每一个相对路径执行与validator相同的Path.match检查。Debug收集1612文件、Release1569文件、ASan1612文件，三者均0未匹配；此前递归glob与Path.match差异导致的归档拒绝已解决，既有失败报告保留。追加的明确深度模式仍限定在各自SDK/子构建原目录，没有扩大到工作区外。

三份build_outputs均已改为实际VS bundled CMake版本目录3.31.6-msvc6/CMakeCXXCompiler.cmake；该目录与此前已核对的真实integration编译器识别文件一致。

expected分别191/191/193，regex全部匹配，均有CHECK.dependencies.python、CHECK.conformance.bootstrap、CHECK.models.outcome_consistency三个检查。通过正式inputs()函数独立计算，三配置源码集合摘要均为daad9b3588510afff1865f9717ea4d3da47bc788d375a8b585c6e67b29943aca；实际正式运行仍需绑定同一已提交源码身份，清单复核本身不虚构commit验证。

SDK relocated/pruned头与CMake导出、消费者原始命令及exe/tlog、CompilerId/ABI识别材料、主构建工厂输入JSON、正控制positive.obj均已声明收集。最低存在约束包含4份relocated导出及operation.hpp、2份pruned导出和10份positive.obj，与既定SDK用例/10个编译反例驱动数量一致。既有collector只归档本轮新产生runtime路径，因此此处已有文件匹配检查不替代正式运行所需的新产物数量检查。

机器可读结果：review-final-manifests-e08463/verification.json。工具实现及三支持头工厂摘要的AI Approved范围继续见review-final-tools-a0159d.md；该旧报告中两项归档待改问题由本报告记录为已解决，不改写旧报告。

# D1.03 三配置正式运行清单草案预审

actor_type：AI；审核者：独立代理review_tools。结论：已检查的继承/矩阵/配置部分可接受；注册消费者仍在实现，整体状态为待集成复核，不作无条件Approved，不声明任何D1.03实际运行Passed。

## 本轮草案SHA

| 文件 | SHA-256 |
|---|---|
| tests/runs/d1.03-win-msvc-debug.json | 5b5a2b67969cdd98ef0ef8d3e82c40a6bdce4b3de7978210878e6a8c02db0c43 |
| tests/runs/d1.03-win-msvc-release.json | 81c1a21bedb73f7fe1a51db698c784674b24ad42e833bc1ed6dee3b906923ba3 |
| tests/runs/d1.03-win-msvc-asan.json | 8f382fc6a4aaf3e22b842cae5f0d5176b5e99f7f99484e88c6ddc8d5c07dfd0a |

## 已做只读核对

- 按正式expected_cases函数选择配置，216/216/218个固定用例逐项匹配regex，未漏任何旧用例或25新增主体。
- 三个CHECK及argv与D1.02清单逐对象一致。repeat、配置仍沿用原约束；Debug/Release显式ASan OFF，ASan配置显式ON并构建Debug。均打开OCK_BUILD_REGISTRATION_TESTS，沿用锁定preset、offline Foundation依赖、每构建2并行和/nr:false。
- 仅将主构建路径build/d1.02-{配置}替换为build/d1.03-{配置}后，旧build_outputs、runtime_artifact_patterns和required_runtime_artifacts全部原样保留，无丢失/降minimum。SDK驱动仍用build/d1.01-sdk和build/d1.02-sdk，清单正确保留这些实际驱动路径，不能机械改为d1.03-sdk。
- 编译器识别文件使用3.31.6-msvc6目录；三配置exe/lib后缀分别Debug、Release、Debug。原SDK归档的明确层数匹配模式、构建日志、factory-inputs.json、positive.obj及最低计数仍保留。
- source_patterns保持旧范围并增加D1.03计划；required_artifacts显式含registry-api.md及tests/runs/d1.03-matrix.json。实际inputs()收集均包含matrix，避免上一包临时matrix不属于来源的问题。三份清单通过tests/**互相包含，首次读取同集合摘要fc66fc7e5deff502018b91074e65e91298eafbf4ca7550e19097d1b696374590；并发实现仍在增加文件，此摘要仅为读取时快照，不能作为最终冻结审核摘要。

## 首次正式运行前必须完成的集成核对

1. 本轮tests/contract/registration只有正在编辑的registration_tests.cpp，尚无最终CMakeLists/discover/child驱动。清单登记的registration-tests.cmake、registration-tests-{Config}.cmake、ock_registration_tests.exe和ock_registry_internal.lib路径是预期，必须用实际生成产物核对。尤其内部lib的输出目录取决于add_library所在CMake目录，不能凭拟定路径认定存在。
2. 新registration runtime patterns已列JSON、原始log、cpp、CMakeLists、子构建cache、CompilerId/ABI和tlog，但当前required_runtime_artifacts没有任何registration专用最低计数。驱动完成后必须按固定主体所需实际子命令数量补入commands.json最低项，不能默认为空也能通过。
3. registration子编译正控制产物尚未声明。驱动定稿后按实际OBJECT/lib/exe类型加入其产物pattern及合理最低数量；负例预期失败，不要求伪造负例产物。确认配置目录与Debug/Release/ASan一一一致，不复制历史子运行计数。
4. 实际完成一次注册编译子运行后，逐项检查本轮glob收集的所有路径均能被validator相同Path.match规则接受，并核对原始commands引用的stdout/stderr均被收集；当前明确深度模式方向正确，但尚无真实新驱动路径可验证。
5. 最终源码停止变化后，重算三配置inputs一致性，重新绑定spec/code AI记录和三清单最终SHA，再执行固定216/216/218与每配置三CHECK。当前预审不能代替冻结后的完整审核。

本轮只新增本预审文档，没有修改清单、注册器源码或既有证据。未发现需删除测试或放宽证据规则的理由；待核对事项均为新驱动完成后使声明与实际布局一致。

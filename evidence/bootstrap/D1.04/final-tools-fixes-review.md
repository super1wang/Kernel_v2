# D1.04 最终修复来源工具与三轮局部集成 AI 复核

审核者：review_tools，actor_type=AI。结论：**工具、固定清单及最终来源三轮局部集成证据 Approved。** 本结论供技术审核及提交使用，不是正式251/251/253完整矩阵和每配置三CHECK的自动验收，也不将包级状态提前写为Passed。

## 最终来源和审核分工

三份正式manifest按实际 tools.evidence.common.inputs 重算，逐项等于 freeze-after-review/source-inputs.json，共224项，统一摘要 `1a044ad0c5b50f1efa53a82125430a7002a61a1fbbd989f8f1ca2e1e50ac8352`。三轮源码ZIP均与此集合一致，source_changed 的added/removed/modified均空。

新增observation_cases.hpp由本代理实施，不能由本报告冒充独立代码审核；其独立规格、代码及覆盖意见来自review_contracts的final-contracts-fixes-review.md（AI Approved）。本代理核对最终头与最初绿色快照仅CRLF→LF，三个函数确实挂接原query_field_projection、page_binding_isolation、queued_revoke_drop；未增加CTest名。核心预算、异常及encoder别名修复的完整复核属于该独立审核，本次不重复作越界结论。

QueueState共有所有权、Frame的reservation_owner、单次charged扣款和锁外retired释放机制保持；Unknown全会话清理已补，原pending剩余问题及Sink双票据问题的控制保留并进入实际35主体。历史send/queue失败报告、encoder red及旧冻结报告保持原文，本报告仅绑定本节新来源。

## 三轮实际归档

| 局部运行目录 | 实际配置 / ASan | JUnit | wrapper / 命令 / raw / cpp / 正obj | ZIP条目 |
| --- | --- | --- | --- | --- |
| integration-debug-75627b76d082 | Debug / OFF | 35/35 | 5 / 24 / 48 / 12 / 4 | 470 |
| integration-release-af5477237b1b | Release / OFF | 35/35 | 5 / 24 / 48 / 12 / 4 | 468 |
| integration-asan-38238230de77 | Debug / ON | 35/35 | 5 / 24 / 48 / 12 / 4 | 469 |

合计105次policy主体、15个wrapper实例、72条子命令、144份原始流、36份编译源及12个正控制对象。每轮JUnit名字精确等于原固定35新增集合，所有外层命令Exited/0，子命令的负控制按预期非0；所有owned Job均assigned_before_resume=true、active_after=0。

每轮源ZIP224项与构建ZIP所有条目逐个核验大小和SHA、无重名。四个编译wrapper各native/configure/positive成功、两个negative预期失败；全部8个负诊断逐个匹配相应源码。安装wrapper实际install与CoreContracts成功、Runtime因约定组件不可用错误拒绝；内部policy头/库不安装，实际目标闭包为OCK::CoreContracts→OCK::Foundation。

各轮authorization runtime声明实际收集254文件，所有文件均在ZIP；逐文件执行validator相同Path.match语义未匹配0，全部10项minimum满足（含5commands/5structure、24stdout/24stderr、12cpp、4positive.obj及安装材料）。四个子构建CMakeCXXCompiler.cmake均指向固定msvc-validation/67414627e4e76f7e/bin/cl.exe，CompilerId/ABI材料位于明确声明层级。

工具链均为MSVC19.44.35228.0、v143 version14.44.35207、SDK10.0.26100.0。Debug/Release配置与各自CRT相符；ASan使用Debug配置、asan_requested=ON，并核对ock_policy_internal与ock_policy_tests两个实际CL.command.1.tlog均含/FSANITIZE=ADDRESS，不仅根据profile名称推断启用。

## ASan开发驱动修复与保留失败

首次ASan integration-asan-bc3cce35dc7a在额外直接native-list阶段Crashed/3221225781，未隐去或覆盖。同binary的asan-runtime-control-07a51e79705a证明：未带runtime路径失败，使用已生成policy-tests中的注册runtime路径则Exited/0并精确列35。

最小ignored driver修复从本次构建/配置的注册文件读路径，通过cmake -E env只作用子进程；不修改全局环境、源码、正式manifest或owned Job规则，不跳过名字核对。Debug/Release实际使用旧profile驱动，ASan完整重跑使用修后驱动，两者SHA分别绑定如下，不能声称三轮开发driver完全相同。正式source224不包含ignored build脚本，但每轮已单独归档driver.py；代码与清单来源没有分叉。既有Debug/Release成功事实无须因列表环境修复重复执行。

## 固定集合与后续验收边界

D1.03的216/216/218全部case对象及三CHECK原样继承，追加35后固定251/251/253；三份正式manifest及matrix没有减项、regex/旧档案约束完整保留。当前三轮仅policy局部集成，因此不声称继承的旧主体或三CHECK在这三轮已经执行；正式完整矩阵与现有自动gate仍是后续步骤。

## 精确SHA绑定

| 材料 | SHA256 |
| --- | --- |
| packages/runtime/policy/policy.cpp | c54e87301880bde0548d59a686f5bfa0cb2ca909920fb2dc2757c8ee3168e0f6 |
| packages/runtime/policy/policy.hpp | bcd5fcb28468b6f7f519c3e99671a1f0a6554871da1be043bced89c2b1ee4fee |
| tests/contract/authorization/observation_cases.hpp | c2129f58dc4ac0db63c365b213bb83c3d36caa1c4a133e092d53bef2ac0150a9 |
| tests/contract/authorization/policy_tests.cpp | bbf1679ebb252fedb4f4d90344803c3b4cb203869ff3282bea7c4b0830591a30 |
| tests/contract/authorization/CMakeLists.txt | 02b088132c84abd1fb90258ca7a918f64a237e9b54fcf161f37b2c8e3976af30 |
| tests/contract/authorization/develop.py | 3132328ddfbf2598764f7c27f0ae40e6a23938ad973922d7e07669a507c67224 |
| tests/contract/authorization/discover.py | 5a4a594fa6654cf7e3dbcd7670aa88c91d062d0b73c61b10bb3736734b7cb2f8 |
| tests/contract/authorization/verify_children.py | 6817cc0f140fa4cea8920314e2248b3b66e0b27b275eadd54609a8a96764caff |
| tests/manifests/d1.04.expected.json | a4d410e2f75610e61300559d3e73f0eb1bf6baacf24f74d070ed5a686195774f |
| tests/runs/d1.04-matrix.json | 3c58c8bcc67999054374c06bd03e89a759759cd0b458df4210d4113b872cc506 |
| tests/runs/d1.04-win-msvc-debug.json | 084f14e7d767a34b0323163fdf939ae3e331670da19aadbbcd0b0230e1c8a05a |
| tests/runs/d1.04-win-msvc-release.json | 13d16de429137c7ba7c75e6fef168494e25e35c82eef43ae1cfe915a98cba138 |
| tests/runs/d1.04-win-msvc-asan.json | 381ddaf5b086a732f04859d828b41991de0018f75f4f0adbc45761925dcadf5f |
| evidence\bootstrap\D1.04\final-contracts-fixes-review.md | d95aa99302c2426b99edfe2e0b1179c8ac4a0500f051eb0407036b5016ea7a41 |
| evidence\bootstrap\D1.04\asan-runtime-driver-fix-review.md | fb7ca38c8d7ff361cb858caa3c2e873bf6b063c91c9a7ce5aa7a9270ff1b765c |
| integration-debug-75627b76d082/driver.py | e3a9aaf78855eb1cb197d618d9f864a2a4324fd4e442923025df869f954388d6 |
| integration-debug-75627b76d082/result.json | c18bd9eefd1aace919569e2d6dbdff3f2b231ada85ab899bca7872d84770aba6 |
| integration-debug-75627b76d082/build-artifacts.zip | 5d0899b096db5087c8fbf6f580c3a3dd79d538350a0191d40a0dec6770a4d972 |
| integration-release-af5477237b1b/driver.py | e3a9aaf78855eb1cb197d618d9f864a2a4324fd4e442923025df869f954388d6 |
| integration-release-af5477237b1b/result.json | 1694c0cca10108a4986efe37a1fde264fd98608250eb93c53a19ca1be07a9a31 |
| integration-release-af5477237b1b/build-artifacts.zip | 1767430efab1611f9cfadc06f1a4e10809bcb75e7b20afe43ac57b80a86dddac |
| integration-asan-38238230de77/driver.py | c4a0c23a9232fc65f748abc93a44addaf0afa16ec2ebf8d97a306c4a9ddbb337 |
| integration-asan-38238230de77/result.json | 7d5a83ebb3f915ee05149b20f5d9766cfd9ff594982e3c29cdc4409ef93359d8 |
| integration-asan-38238230de77/build-artifacts.zip | f78e677a1e19f298217610f71ed64ff8679642a65562e264670cc17edeeed64c |
| final-tools-fixes-win-msvc-debug-audit.json | 67cf3fe2600cfd39dbb316e393b6b62c3975f50f04427a3050b4e580808db727 |
| final-tools-fixes-win-msvc-release-audit.json | 762e554a7372d3f34b53f9b0ba0cc2d148ce20f6479ee8745eb4fb27e13d229f |
| final-tools-fixes-win-msvc-asan-audit.json | 6f85ee36a7e086ae92c5c7e6ddf3a3472bf6434ea0ef2245a223b0bd52208916 |

本次最终复核未改实现、规范、历史或提交，只新增独立归档核验与本报告。

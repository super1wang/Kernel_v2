# D1.04 三配置运行清单初始冻结独立 AI 预审

actor_type：AI；review_tools。结论：Approved，限定当前初始固定清单。实际authorization驱动及产物未生成，集成后必须二次核对布局/最低数量及真实命令，不能据此称构建或测试Passed；API修订3批准另由review_contracts负责。

## 清单SHA-256

| 配置 | SHA-256 |
|---|---|
| tests/runs/d1.04-win-msvc-debug.json | 084f14e7d767a34b0323163fdf939ae3e331670da19aadbbcd0b0230e1c8a05a |
| tests/runs/d1.04-win-msvc-release.json | 13d16de429137c7ba7c75e6fef168494e25e35c82eef43ae1cfe915a98cba138 |
| tests/runs/d1.04-win-msvc-asan.json | 381ddaf5b086a732f04859d828b41991de0018f75f4f0adbc45761925dcadf5f |

## 实际只读核对

仅映射主构建目录build/d1.03-{配置}到build/d1.04-{配置}，D1.03所有build_outputs/runtime patterns/required minima完整继承，缺项为0。仍由旧驱动产生的d1.01-sdk/d1.02-sdk目录正确保留。三CHECK及argv逐对象相同，原注册组件回归保持开启。

配置沿用固定MSVC preset、Foundation offline依赖、独占Job链所需/nr:false。Debug/Release显式ASan OFF，ASan显式ON并构建Debug。新增OCK_BUILD_AUTHORIZATION_TESTS=ON，其他固定测试开关保持。251/251/253个expected名称全部匹配regex，spec/code AI审核要求未删。

sourcepatterns相同并包含全packages/tests/tools/cmake、原规范和D1.04计划；tests/runs/d1.04-matrix.json显式进入required_artifacts且实际inputs收集包含它。读取时三个配置均211项、共同摘要8e8727e7e7d03fa42bd9ca8a4730e2fcd6e4c4c86b9152c9c411679cd2627d81。该值是无行为实现阶段快照，不是后续最终实现摘要；实现完成后必须重新冻结及绑定。

## 新产物与最低数量

每配置新增6项固定build_outputs：authorization/CTestTestfile.cmake、policy-tests.cmake、policy-tests-Config.cmake、Config/ock_policy_tests.exe、Config/ock_policy_internal.lib、policy-target-Config.json。配置目录Debug/Release/Debug对应正确。

固定5wrapper的最低项与已审提案一致：5commands、5structure、24stdout/24stderr、12cpp、4positive.obj；真实安装OCKConfig、CoreContracts/Runtime各自CMakeLists、target-metadata各1。四组native/configure/positive/negative1/negative2加安装组4命令，共24命令。没有新增隐藏CTest或降低原包minimum。

CompilerId/ABI材料已明确收集：CMakeConfigureLog、版本目录下CMakeCXXCompiler.cmake与CMakeDetermineCompilerABI_CXX.bin、CompilerIdCXX递归内容并辅以1至6级明确星号模式。安装及tlog也保留明确深度，以兼容Python3.11 validator Path.match非递归**语义。

对三配置各21种代表路径实际调用与validator相同Path.match：含CompilerId深层CL.command.1.tlog、ABI bin、positive.obj、安装深层operation.hpp、安装Config和两个组件源码，全部匹配，pattern无重复。这里是路径模式核对，未冒充新驱动实际glob产物；集成后二次审查须逐个真实收集路径再次验证。

## 集成后仍须验证

实际CMake target定义位置必须生成上述lib/exe/metadata路径；五个wrapper真实24命令、8负例诊断及四个同入口positive须逐条审查。归档每个raw引用应核对存在、size/SHA、完整owned状态；minimum只有计数，不能替代各主体内容核对。实际glob与Path.match收集集需零漏档。源冻结后重新计算同commit/同inputs，API/代码AI审核绑定准确摘要，再运行正式三矩阵。

无当前必须修改的清单项。本轮仅新增本报告和D1.04-expected.md，没有修改实现、清单、expected或提交；历史预审及候选失败记录保持。

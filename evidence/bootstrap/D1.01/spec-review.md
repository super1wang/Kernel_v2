# D1.01 Foundation 独立规格复核

复核者：Codex 独立规格复核代理（actor_type=AI）。
时间：2026-09-07T08:43:15.900249+00:00。
规格结论：Approved。下列字节对应的最小原语、固定用例和集成方案符合 A03.1、D1.01 及本包实施计划；没有未关闭的规格阻断。此结论不代表 D1.01 包级 Passed，不替代独立代码审核或正式运行矩阵。

## 核对结果

- Result/Unexpected直接复用锁定tl::expected，不自研expected或STL；纯码Error无详情分配路径；静态域工厂拒绝任意指针及自动寿命域；有界UTF-8详情先校验后分配并拥有文本。异常、OOM及不变量不伪装成业务失败；空详情的无分配语义已明文规定。
- Name拥有1–96字节ASCII，大小写敏感；UTF-8显示检查分离；Tagged128无跨Tag转换/比较，规范小写hex仅表达值而不授权。句柄先registry/世代后slot，空句柄和旧世代拒绝；epoch不进入业务ID，算术及预算失败不更新值。
- 原生23项注册不读取expected；Release使用CHECK。计划27项Foundation用例与固定manifest逐项一致，无缺项/多项；Debug和Release各34项，ASan36项（另含健康与真实越界检测控制），固定集合唯一且匹配过滤器。三配置输入集合及显式AI政策绑定一致，独立构建目录、实际二进制与对应配置注册文件均列为证据产物。
- Foundation作为唯一Implemented组件前进，保持INTERFACE目标和空产品依赖；expected作为单独公开第三方闭包安装导出，公开头进入实际manifest并匹配SHA。其余20个组件仍为ContractBaseline；未知依赖/越层守卫保留。Foundation可按组件发现，Runtime和未知组件继续拒绝；安装搬迁、BUILD_TESTING=OFF、缺expected、错误后端/异常模式与独立头消费均有实际命令验证入口。

## 本轮发现及修复复核

1. 原fail-fast测试在不变量意外返回后exit90，父验证器只检查非零，可能错收no-op实现。修复后共用返回路径输出后置标记并exit0；父验证器先运行no-op控制并确认拒绝，再要求真实abort退出3、前置标记、无返回标记、无Job强杀与遗留。已重读实现和合同，规格缺口关闭；实际反例/重跑材料由独立运行证据记录。
2. 原单一注册文件可能在同一VS构建树跨配置构建后选择错误二进制。修复为按配置生成，wrapper明确要求CTest配置且对应文件存在；正式manifest归档wrapper和本配置注册文件。已核对修复，风险关闭。
3. ASan所需运行库目录由CMake实际MSVC编译器目录传递，并在发现、CTest、原生子进程的PATH前置；未取消sanitizer或减少用例，合同已同步。本审核确认方案，不替代ASan实际执行。

## 证据边界

本次为只读代码/规格复核及固定名称/清单/SHA静态核对。正式Debug、Release、ASan矩阵尚未由本代理核验；SDK首轮及其他代理运行结果不自动成为本文的包级通过事实。后续须完成同一最终输入的完整实际运行、独立代码审核，再按已生效AI政策验收。仅写本文件，未修改实现、测试或Git；没有人工批准要求或伪造human记录。后续审核文件字节变化须补审。

## 审核输入 SHA-256

| 文件 | SHA-256 |
|---|---|
| docs/01_Architecture_v3.3.md | `f20644428ed8c307109e26fe690e951f7e191659903bb496ba8dee51125bca2d` |
| docs/02_Execution_Plan_v3.3.md | `7daf3c8a7e68462a97cd593d2ab9a35c1888b720428c72bc9f915049050df511` |
| docs/plans/D1.01.md | `732cdc3bad889f98bd593819984cc1798ad22a7c26319a42d5de7a248b6f1825` |
| docs/contracts/foundation.md | `42c1e06495fa9d8541a7454bc0e4a4685e34583cbde58ce0ad0af24db1910509` |
| packages/foundation/include/ock/foundation/foundation.hpp | `41a2c0786e8622881e526912e7cb486089f257228e6b3d58df3d0ea2914145b1` |
| tests/unit/foundation/foundation_tests.cpp | `b1dcdfd3d055e0b838a71c68f28a2b3fd924b5ab703245f8fbc6ec948a86f38d` |
| tests/unit/foundation/CMakeLists.txt | `b9e1b7f8b5c939b175335db597ed39bac199f94e509dbaada465b30ab71df222` |
| tests/unit/foundation/discover.py | `c9fbe7f73e017fac6061f48339c28c126d9454f4a0bca6b640f362a70248d9c3` |
| tests/unit/foundation/verify_children.py | `66320cadde74a58249459c96c8e1572e3dff9e5fb1ccb7aa5485382de6607995` |
| CMakeLists.txt | `8fa61c11410819899a14c15a4f28f676b55185149504ddbe1209e97bada7a7f1` |
| cmake/Dependencies.cmake | `39802a87f2820d316fb5dcc8240db96b77b80400dd06fff5f0e32873d13c2624` |
| cmake/TargetDependencies.cmake | `956b5082453e7056af083d9e862e0099fbd41698dd82c665a1d3a460ad58db57` |
| cmake/OCKConfig.cmake.in | `554f12faad9f69830e0abc5379e549444a3a9178db58394d5eaff596f4e02bc3` |
| sdk/sdk_api_manifest.json | `b3783d0c47ba0d2b65033d5b6981f9a268a9e7ab65dedc04ddecaf481ab6c2f6` |
| docs/public-headers.md | `e1f6591ff1120ba6a32acb524cd49800eee75a68939b9dde514192578347fb7e` |
| tools/architecture/check.py | `f599f239ca45e7de133f586011391d1d33aa4bf3a6534626916f90958222ee1c` |
| tests/architecture/test_foundation_stage.py | `68a34c7238957f36da6c5de08110b269e534dd1e815f49578722bcddf1f110be` |
| tests/install_consumer/verify_foundation.py | `fdc2bbd1f2691431cf3866423c48efe69366aabd477dae55d0ec45c2467d9719` |
| tests/install_consumer/verify_baseline.py | `8dccc70e7c454932668721e3ca34d1e36647ac9514d8c7ed3b36acd82f8161b1` |
| tests/manifests/d1.01.expected.json | `4956ea4ac363954329090c1f1e4afac63e7753c4906fea925d605801b62d0448` |
| tests/runs/d1.01-win-msvc-debug.json | `e46b9b76cf6ba04d20e9b7716d20cea96985cd630e87052ca3bd6a1812dedc5f` |
| tests/runs/d1.01-win-msvc-release.json | `19c5a875ab1be943db44b945feb5aa2b60f21c94c339af59eec8bf7d1336ed98` |
| tests/runs/d1.01-win-msvc-asan.json | `bf12e6f41fe3cc15f202440365d9335ce14ec5d6e9ec92cd37d4a060acf82c8e` |

## 同轮补审：SDK元数据ASan运行环境

补审时间：2026-09-07T08:44:09.877582+00:00。已只读核对根CMakeLists.txt在T24.sdk.version_header注册后新增的两行：从CMAKE_CXX_COMPILER取实际编译器目录，对该测试的PATH作path_list_prepend。该调整为已启用ASan的元数据探针提供运行库搜索路径，不关闭ASan、不改预期用例或退出判断；规格复核认可。此前缺DLL失败保留于evidence/bootstrap/D1.01/asan-metadata-red-a5ff2232d6，实际修复后运行另由主集成证据验证。根CMake SHA-256为 `8fa61c11410819899a14c15a4f28f676b55185149504ddbe1209e97bada7a7f1`（与上表一致）；本文仍不替代三配置正式矩阵验收。

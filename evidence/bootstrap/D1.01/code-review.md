# D1.01 独立代码审核

审核身份：AI，独立代码审核代理 `/root/implement_d004`。时间：2026-09-07T08:53:44.608208+00:00。

结论：Approved。指定范围内未发现未解决的必须修复项。本轮发现并实证的 ErrorInfo 可变别名问题已修复且独立复验关闭。此结论是代码审核，不表示 D1.01 包级或 G1 已 Passed；完整三配置正式矩阵由主任务另行核验。遵循用户“自我复核和自动验收，无需人工”，不创建人工批准。

## 范围及结论

对照 A03.1、D1.01、中文合同与已批准规格审核，检查本轮未提交的 Foundation 公开头、原生测试/注册/子进程验证、根 CMake、依赖及导出配置、架构检查、SDK 安装消费者、manifest 和三份正式 run 清单。未修改实现、测试、合同或 Git；独立反例只使用自己的 `build/d1.01-review-9027715f95`，未构建主任务 ASan 目录。

- Result/Unexpected 直接复用锁定 expected 后端；错误码与纯码 Error 不引入分配，静态域入口排除自动寿命域。详情在拥有文本前检查字节上限和 UTF-8；空详情不分配。用户异常、bad_alloc、bad_expected_access 与 fail-fast 保持合同边界。修复后 ErrorInfo 不可复制、移动或赋值，Error 自身仍可复制/移动并共享只读详情。
- Name 拥有固定缓冲并按字节比较；身份编码只接受规范小写 hex；不同 Tag 保持类型区分。句柄先 registry、再世代、后槽位；受检加减乘、窄化、预算更新及 epoch 推进在边界失败前不改变状态。未发现此范围内的溢出运算、异常吞并或悬空输入视图。
- Foundation 仅通过显式 expected 公开第三方目标导出，产品 DAG 仍无内部依赖；安装 include 使用相对前缀，组件发现只放行已实现的 Foundation。消费者验证搬迁、无测试生产配置、独立头包含、缺 expected、错误版本及关闭异常模式；负例先有合法消费控制，实际编译失败诊断须匹配预期。其余组件与未知依赖守卫保留。
- 原生用例直接由代码注册，固定 expected 未参与注册生成；Release 使用 CHECK。静态域负例有合法编译控制；fail-fast 有实际返回的 no-op 控制、精确 abort 退出与前后标记，启动失败不能作为成功。CTest 按明确配置读取注册文件，ASan 运行库路径来自实际 MSVC 目录。
- 固定集合与代码注册独立比对：23 个原生用例一致；含 SDK/回归后的 Debug、Release、ASan 分别为 34、34、36 项，ASan 包含健康与真实越界检测控制。三份清单使用独立构建目录、对应配置的注册文件/二进制、相同来源模式及有效政策摘要，运行子证据有归档声明与最低数量约束。实际完整矩阵完成情况不得从该静态匹配推断。

## 本轮发现、修复及验证

P2，已关闭：原 ErrorInfo 隐式公开复制与赋值允许调用者从合法详情复制出 `shared_ptr<ErrorInfo>`，发布为 Error 的只读指针后，再经原可变别名赋值改变已发布详情，违反“详情不可变”合同。

独立最小反例位于 `independent-alias-probe-9027715f95/`：锁定 MSVC 配置及编译均实际 Exited 0；运行实际 Exited 1，输出 `published details after alias assignment: changed`。源码、CMake 和三份命令的原始 stdout/stderr/Job 事实均保留，没有将失败覆盖成成功。

主任务先把四项不可复制/移动/赋值 CHECK 加入既有 ownership 用例，真实 red 位于 `immutable-details-red-e1f8a2b816/`，本审核读取确认 build Exited 0、测试 Exited 1。随后删除 ErrorInfo 的四种特殊成员，保留 Error 共享拥有权正例，并同步合同与公开头 SHA。

独立复验 `independent-alias-recheck-099a57e3d1/` 保持原 PoC 与 CMake 字节完全不变；在最终头上重编译实际 Exited 1，MSVC C2280 明确指向已删除的 `ErrorInfo::operator=`，符合负例要求。原 PoC 源码 SHA-256 为 `727685fc73d14ca05c448d3629b45d8757f496fd7ed2b012b36aad0af11de6c9`。

本审核还独立运行六项 Foundation 阶段守卫，6/6、退出码 0；实际 Debug 目标图与 SDK 清单校验 errors=[]；通过只读脚本核对上述固定注册及三配置矩阵。修复后的主任务 Debug 正向证据 `immutable-details-green-67056e8c89/` 已读取核实 build/test 均 Exited 0、Job 无遗留，JUnit 为 23 个 testcase 且无 failure/error/skipped。Release、ASan 及正式矩阵仍由主任务继续记录；本文不预写其结果。

## 冻结审核输入 SHA-256

下列摘要在审核结束时从实际文件重新计算；所有文件为 UTF-8 LF，公开头与 SDK manifest 摘要一致。后续改变这些字节须核对本审核适用性。

| 文件 | SHA-256 |
| --- | --- |
| `packages/foundation/include/ock/foundation/foundation.hpp` | `1e3325f6077dd42d2f3ec8460d1eb4eeeca169c668975c41fb90556773940ae0` |
| `tests/unit/foundation/foundation_tests.cpp` | `f103640fd05d488b97562dde33df7f04f3c32fb017e6cd8951adceac6ed1d9cc` |
| `tests/unit/foundation/CMakeLists.txt` | `b9e1b7f8b5c939b175335db597ed39bac199f94e509dbaada465b30ab71df222` |
| `tests/unit/foundation/discover.py` | `c9fbe7f73e017fac6061f48339c28c126d9454f4a0bca6b640f362a70248d9c3` |
| `tests/unit/foundation/verify_children.py` | `66320cadde74a58249459c96c8e1572e3dff9e5fb1ccb7aa5485382de6607995` |
| `CMakeLists.txt` | `8fa61c11410819899a14c15a4f28f676b55185149504ddbe1209e97bada7a7f1` |
| `cmake/Dependencies.cmake` | `39802a87f2820d316fb5dcc8240db96b77b80400dd06fff5f0e32873d13c2624` |
| `cmake/TargetDependencies.cmake` | `956b5082453e7056af083d9e862e0099fbd41698dd82c665a1d3a460ad58db57` |
| `cmake/OCKConfig.cmake.in` | `554f12faad9f69830e0abc5379e549444a3a9178db58394d5eaff596f4e02bc3` |
| `tools/architecture/check.py` | `f599f239ca45e7de133f586011391d1d33aa4bf3a6534626916f90958222ee1c` |
| `tests/architecture/test_foundation_stage.py` | `68a34c7238957f36da6c5de08110b269e534dd1e815f49578722bcddf1f110be` |
| `tests/install_consumer/verify_foundation.py` | `fdc2bbd1f2691431cf3866423c48efe69366aabd477dae55d0ec45c2467d9719` |
| `tests/install_consumer/verify_baseline.py` | `8dccc70e7c454932668721e3ca34d1e36647ac9514d8c7ed3b36acd82f8161b1` |
| `sdk/sdk_api_manifest.json` | `2a81b8bf6f8a5a856511f349026058394d007b82ff6879a7a9718d74ac6df70c` |
| `docs/public-headers.md` | `e1f6591ff1120ba6a32acb524cd49800eee75a68939b9dde514192578347fb7e` |
| `docs/contracts/foundation.md` | `333024870b90eacb9be98036e7ca7861e8bcb738530e5427b08d7fa4a9e26f9b` |
| `docs/plans/D1.01.md` | `732cdc3bad889f98bd593819984cc1798ad22a7c26319a42d5de7a248b6f1825` |
| `tests/runs/d1.01-win-msvc-debug.json` | `e46b9b76cf6ba04d20e9b7716d20cea96985cd630e87052ca3bd6a1812dedc5f` |
| `tests/runs/d1.01-win-msvc-release.json` | `19c5a875ab1be943db44b945feb5aa2b60f21c94c339af59eec8bf7d1336ed98` |
| `tests/runs/d1.01-win-msvc-asan.json` | `bf12e6f41fe3cc15f202440365d9335ce14ec5d6e9ec92cd37d4a060acf82c8e` |
| `tests/manifests/d1.01.expected.json` | `4956ea4ac363954329090c1f1e4afac63e7753c4906fea925d605801b62d0448` |

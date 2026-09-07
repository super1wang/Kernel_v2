# D1.01 构建环境局部隔离增量规格复核

复核者：Codex 独立规格复核代理（actor_type=AI）。时间：2026-09-07T09:48:14.053119+00:00。
增量规格结论：Approved。下列10文件的受控验证隔离与D1.01计划的锁定工具链、安装消费及失败保留要求一致，未改变产品API、组件架构或原自动通过条件。本结论不代表修复后的完整矩阵、D1.01包或阶段门禁Passed。

## 复核结果

- LockedMSVC在project/编译器识别前设置VcpkgEnabled=false及仓库专用UserRootDir，仅替换这两项、保留其他CMAKE_VS_GLOBALS，并向ABI try_compile传播。专用目录实查仅有README，没有Microsoft.Cpp用户属性文件。修改局限采用该工具链的工程，不操作机器全局vcpkg/MSBuild配置，不宣称隔离所有系统扩展或环境变量。
- 受控preset和三个验证驱动显式接入相同工具链；SDK导出逻辑未增加源树工具链依赖，搬迁消费者的绝对源码/安装路径检查保留。三份D1.01 manifest只增加--fresh以重新探测编译器，原34/34/36固定清单不变；Foundation公开头及expected已实际与HEAD逐字节核对相等。
- 安装驱动的子命令上限仍为180秒，失败/超时继续导致验证失败，没有放大超时、删测试、豁免失败或修改证据生成规则。首轮ASan报告和汇总门禁的现存字段仍为Failed，Debug/Release的历史Passed不拼成当前全矩阵通过。docs/toolchain如实记录超时与外部注入/资源争用，未把vcpkg断言为唯一原因。
- 公开头消费者持续查询CompilerId、ABI try_compile和consumer三类实际工程的VcpkgEnabled/UserRootDir；查询不以/p覆盖被测值。首集成实测出现CMAKE_MAKE_PROGRAM不在本机cache导致StopIteration，失败保留于isolation-integration-ee8812ee3f。已重读修复：通过消费者CMake的CMAKE_VS_MSBUILD_COMMAND导出实际MSBuild路径，验证文件存在后继续三类查询，没有删除验证项。

## 已读取的实际证据及限制

full-isolation-probe-ba20bf2d12的配置命令实际exit0；verification.json记录三次MSBuild查询、一次build及一次run均exit0、Job无遗留且未强杀。三份查询原始JSON均显示VcpkgEnabled=false及实验隔离user-props目录。该实验使用临时toolchain，证明隔离机制，不等同最终cmake/LockedMSVC.cmake及三个驱动全部集成通过。CompilerId查询的默认工具链环境也不单独证明全部编译配置，实际配置/工具链身份仍由正式证据核对。

本次审核完成时最终驱动重跑及新三配置正式矩阵由主集成另行完成；本文不据静态检查或临时probe提前放行。旧失败报告、StopIteration失败与后续成功必须分开保留，以新的实际source输入及AI复核记录形成新验收。仅新增本审查文件，未修改实现、测试、review JSON、旧报告或Git；无需人工流程。

## 审核输入 SHA-256

| 文件 | SHA-256 |
|---|---|
| cmake/LockedMSVC.cmake | `eb9ff2df55c536656411b0f25a25066232bdeed883bac6829294d761d4b6a58b` |
| cmake/msbuild-user/README.md | `185a7de1c71b550805f74d6b66324410066b271bd2d3497cc2ce52bf136e4615` |
| CMakePresets.json | `f58398635bf46904c23cf387a2b01a4bf9db69fb08037d30659cc48f1b1779f6` |
| tests/install_consumer/verify_baseline.py | `c230fce24ff5b3b42e064ca857b0a7262fb6cfa90927b479448ac1ba5e3a4706` |
| tests/install_consumer/verify_foundation.py | `735936fdab5616d4140a769b0e32386e373eb7da95e02e238c07db1dfcf720cd` |
| tests/unit/foundation/verify_children.py | `449d706c09df1f456ce0cafae84303b85665019fe8b98d97a45d79dd3e92a064` |
| tests/runs/d1.01-win-msvc-debug.json | `1f31059dfa804f3a5711ca059b6dc8d8c6b68ec9f3631c3c86f8eddfbe7f3409` |
| tests/runs/d1.01-win-msvc-release.json | `5b2d8c3dfabec52c963b9f4fd22af09d090bf522a2270133c1a1da2be7e5410b` |
| tests/runs/d1.01-win-msvc-asan.json | `771a552540c49c32f73ed87cb8d3629256ef09cf1f2087d81dbd31153109cb64` |
| docs/toolchain.md | `6f3de27eaa8e19530c9cb35b8d5cecedd9b9b6bc55cdd31ca00505148f45a41c` |

证据读取位置：`evidence/bootstrap/D1.01/full-isolation-probe-ba20bf2d12/`、`evidence/bootstrap/D1.01/isolation-integration-ee8812ee3f/`、`evidence/98a406f2b612-1728e978a21b/`及`evidence/D1.01/automatic-20260907T090632Z/gate-summary.json`。

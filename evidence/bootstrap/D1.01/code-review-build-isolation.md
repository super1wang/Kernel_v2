# D1.01 构建隔离独立代码增量复核

复核身份：AI，独立代码审核代理 `/root/implement_d004`。时间：2026-09-07T09:53:14.666688+00:00。

结论：Approved。限定增量范围未发现未解决的必须修复项；本轮发现的 MSBuild 查询配置身份错误已修复并经实际正确配置查询核对。此结论不替代后续正式三配置矩阵，不改写原 ASan Timeout，也不重新审核已通过的 Foundation API。仅新增本文，未改源码、JSON、用户全局设置或 Git。

## 审核结论

1. `LockedMSVC.cmake` 在 project/编译器识别前由受控 preset 或独立 driver 作为 toolchain 加载；仅替换大小写不敏感的 VcpkgEnabled/UserRootDir 键，保留无关 globals 的原值。路径基于工具链自身 `CMAKE_CURRENT_LIST_DIR`，在 ABI 子工程中保持相同定位；显式追加并去重 `CMAKE_TRY_COMPILE_PLATFORM_VARIABLES`，覆盖之前仅 compiler ID/消费者生效而 ABI 仍被注入的缺口。重复加载保持规范化结果，非 Visual Studio generator 明确失败。
2. 本机 Microsoft.Cpp.Default.props 仅在 UserRootDir 为空时填默认用户目录。仓库隔离目录无 Microsoft.Cpp.*.user.props，标准带 exists 条件的用户属性导入因此跳过；全局 vcpkg 集成仍通过 VcpkgEnabled=false 单独禁用。该方案不改用户文件，也未声称隔离全部环境变量或系统扩展。
3. 三个独立配置 driver 均传同一工具链；无测试生产配置通过受控 preset 取得相同设置。设置只用于本项目验证工程，不作为 Foundation INTERFACE 依赖或 SDK 安装配置导出。SDK 搬迁检查继续逐个检查已安装 cmake 文件，拒绝源根及原安装前缀；缺依赖、错误版本/异常模式、静态域拒绝和 fail-fast 控制保留。
4. public_headers 通过消费者 CMake 实际生成器变量 CMAKE_VS_MSBUILD_COMMAND 写入、读回真实 MSBuild 路径，再查询 CompilerId、ABI 和正式 consumer；缺文件、查询进程失败、非 JSON 或属性错误均拒绝。最终代码对前两者指定 Debug/x64，对消费者指定调用参数的配置/x64，只选择正确配置，不覆盖被测 VcpkgEnabled/UserRootDir。SDK 子命令仍由原 Job 包装并检查 Exited/预期退出和诊断，180 秒上限未放宽；静态域及 fail-fast 的更严格子进程控制保持原样。
5. 三份正式 manifest 均恰好加入一次 --fresh，通过既有 preset 选择隔离工具链，以清除旧缓存并重做编译器探测。源码模式包含工具链与隔离目录；测试集合未削减。--fresh 是重做配置探测的声明，不被描述成删除一切旧构建产物或已完成完整矩阵。

## 本轮发现与验证

本审核独立执行 CMake 脚本模式及展开跟踪：输入混合大小写的旧隔离键、两个无关 globals（其中一个值含等号）和已有平台变量列表。实际最终 CMAKE_VS_GLOBALS 保留无关项，仅产生一份规范隔离键；正常执行退出 0，Ninja generator 反例退出 1 并带明确诊断。独立读取三个 manifest，--fresh 均恰好一次且 preset 路由正确；隔离目录没有用户 props。三个 Python driver 通过 AST 解析，未生成字节码。

P2，已关闭：初版属性查询没有指定 Configuration/Platform。独立对同一 CompilerId 工程实际查询得到默认 Debug/Win32/v100，而显式 Debug/x64 得到 v143；两项 globals 相同不能证明查询了真正构建配置。主任务已按上述配置路由修改 driver。原默认查询日志保留，未改写为正确配置结果。

已读取核实 `full-isolation-probe-ba20bf2d12/` 的真实 configure/build/run 及三阶段查询命令，均 Exited 0、observed_exit_code=0、无 Job 遗留或强杀；ConfigureLog 不含已知 vcpkg/VLD 路径。该机制实验使用当时的临时隔离工具链，不能当作所有最终文件逐字节的全矩阵运行。追加 `configured-query-verification.json` 与 configured-query-0/1/2 原始输出，在正确 Debug/x64 下三类工程均为 VcpkgEnabled=false、隔离 UserRootDir、_ZVcpkgClassicOrManifest=false；参数仅指定配置/平台，没有覆盖被测隔离值。

主任务首轮 driver 因错误假设 cache 存在 CMAKE_MAKE_PROGRAM 而触发 StopIteration，失败保留；最新代码改用上述真实生成器变量。`isolation-integration-eeeb45b739/commands.json` 中四项 SDK driver 已逐一核实 Exited 0 且无遗留，涵盖 public_headers、installed_consumer、missing_dependency_rejected、backend_mode_rejected。此四项运行早于最后的查询配置参数修正；最终 Release public_headers 路由复验及新完整矩阵由主任务另外记录，本文不预写结果。

## 冻结审核输入 SHA-256

以下十份文件均在写入本文前再次读取确认 SHA-256，编码为 UTF-8 LF。后续字节变化须核对本增量审核适用性。

| 文件 | SHA-256 |
| --- | --- |
| `cmake/LockedMSVC.cmake` | `eb9ff2df55c536656411b0f25a25066232bdeed883bac6829294d761d4b6a58b` |
| `cmake/msbuild-user/README.md` | `185a7de1c71b550805f74d6b66324410066b271bd2d3497cc2ce52bf136e4615` |
| `CMakePresets.json` | `f58398635bf46904c23cf387a2b01a4bf9db69fb08037d30659cc48f1b1779f6` |
| `tests/install_consumer/verify_foundation.py` | `8f2f5fee45b156f2226bab86f3cb5829ed331dc2547def8b6206c7a56cce1ecb` |
| `tests/install_consumer/verify_baseline.py` | `c230fce24ff5b3b42e064ca857b0a7262fb6cfa90927b479448ac1ba5e3a4706` |
| `tests/unit/foundation/verify_children.py` | `449d706c09df1f456ce0cafae84303b85665019fe8b98d97a45d79dd3e92a064` |
| `tests/runs/d1.01-win-msvc-debug.json` | `1f31059dfa804f3a5711ca059b6dc8d8c6b68ec9f3631c3c86f8eddfbe7f3409` |
| `tests/runs/d1.01-win-msvc-release.json` | `5b2d8c3dfabec52c963b9f4fd22af09d090bf522a2270133c1a1da2be7e5410b` |
| `tests/runs/d1.01-win-msvc-asan.json` | `771a552540c49c32f73ed87cb8d3629256ef09cf1f2087d81dbd31153109cb64` |
| `docs/toolchain.md` | `6f3de27eaa8e19530c9cb35b8d5cecedd9b9b6bc55cdd31ca00505148f45a41c` |

## 最终 Release 查询路由复验补充

在本记录交付前已读取核实 `configured-headers-0a93f8affa/`：最终 driver SHA-256 为 `8f2f5fee45b156f2226bab86f3cb5829ed331dc2547def8b6206c7a56cce1ecb`，真实 Release public_headers 命令 Exited 0、observed_exit_code=0、无 Job 遗留或强杀。`child-runs.zip` SHA-256 `c2ac74e6998867c42e2a3fc55fedfdf781672b114ef632d75ba6d34fde945d7d` 与记录一致且 CRC 全部通过；归档中的子命令均真实 Exited 0，三份原始查询分别选择 CompilerId Debug/x64、ABI Debug/x64、consumer Release/x64，隔离属性逐项正确，未以参数覆盖被测属性。此补充关闭前述最后查询路由复验待运行项；新完整三配置矩阵仍由主任务单独登记。审核结论 Approved 不变。

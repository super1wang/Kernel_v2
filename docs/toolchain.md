# D0.06-a 工具链与依赖 checkpoint

本 checkpoint 按架构 A02/A19/A20 与执行卡 D0.06 固定 Windows 最小 dependency probes。前置 D0.02 已实际 Passed，D0.02/D0.03 已获人工批准。这里不实现 Runtime、适配器或产品模块；探针成功不等于 G0、生产认证、真实端口 Conformance 或 D5.02-a canonical feasibility 通过。包级结论与人工评审由 D0.06 总集成单独登记。

## 当前恢复环境（2026-09-08）

本机 VS 位于 `E:/vs2022IDE`；相同 toolset 目录实际编译器为 `19.44.35216.0`（Microsoft 有效签名），与历史 checkpoint 的 `19.44.35228.0` 不同。本次显式更新 dependencies.lock、精确二进制 hash 清单和编译器宏探针；保留历史锁及 62 项字节变化于 `evidence/bootstrap/D1.06/resume-environment-lock/`。不将新工具链套用历史运行结论，也不放宽锁定检查。

CMake/CTest 使用该 VS 随附 `3.31.6-msvc6`，Windows SDK `10.0.26100.0`；工作区恢复 Python `3.11.9` 与原 SHA 锁定的六个 wheel，通过每次进程环境选择工具，不更改系统 PATH。Debug 编译及 Host/Logging 32 项直接验证已实际通过；其他配置及正式验收见当前进度。

当前系统为 Windows 11 Insider Preview 27749，i5-12600KF、16 逻辑处理器、约 32 GiB RAM；测量时实际安全工具和负载快照保留于 `evidence/bootstrap/D1.06/resume-environment.json`，不将其写成历史 Windows 10 环境。

恢复时仅为当前 PowerShell 进程设置 PATH（Python311、VS 随附 CMake/bin 与 Ninja）及 `PYTHONPATH=J:/Code/Kernel_v2/build/python-deps;J:/Code/Kernel_v2`。直接 configure 显式传 `-DPython3_EXECUTABLE=J:/Code/Kernel_v2/build/runtime/python311/python.exe`；PATH 优先级不能阻止 CMake 从注册表选中其他 Python。正式 manifest 已固定本次实际解释器。

## 原 checkpoint 固定工具链（历史）

| 项目 | 本机真实值/固定选项 |
|---|---|
| 系统/架构 | Windows 10 19045 / x64 |
| CMake/CTest | 3.31.6-msvc6，VS 2022 随附版本 |
| Generator | Visual Studio 17 2022，x64 |
| Toolset | v143,version=14.44.35207；实际 MSVC 19.44.35228.0，`_MSC_FULL_VER=194435228` |
| Windows SDK | 10.0.26100.0 |
| C++ | C++20，禁扩展，`/Zc:__cplusplus /permissive- /EHsc /utf-8` |
| CRT | Debug `/MDd`；Release `/MD`；不承诺跨工具链/CRT 二进制 ABI |
| 调试信息 | `/Z7`（CMake Embedded），避免编译期 PDB 服务寿命影响证据进程树 |
| Python | 3.11.9 x64；命令均用 `-X utf8` |
| MSBuild | 开发进程 `MSBUILDDISABLENODEREUSE=1`、`/nr:false` |

`CMakePresets.json` 提供 `win-msvc-debug`、`win-msvc-release`、`win-msvc-asan`，默认启用 G0 测试与 Embedded 四项依赖探针。配置期核对已锁定 compiler/CMake/SDK；工具链程序还检查 C++20、指针位宽和实际编译器宏。依赖锁同时记录工具链与上游编译开关。每个配置生成 `toolchain-<配置>.json`，实际构建输入和二进制身份由正式证据采集器绑定。

ASan 使用 `/fsanitize=address`，去掉不兼容的 `/RTC` 并关闭增量链接。健康程序和真实 heap-buffer-overflow 由同一工具链构建；故障父验证器要求子进程实际非零且包含 ASan 对应诊断，DLL 缺失/启动失败不能冒充检测成功。CTest 为子进程增加当前编译器目录以定位 ASan DLL。支持结论仅覆盖本文件实跑矩阵，不承诺所有可选依赖已完成 sanitizer 专项。[MSVC 官方 ASan 支持与约束](https://learn.microsoft.com/en-us/cpp/sanitizers/asan?view=msvc-170)。

Python 开发校验包与 C++ Runtime 依赖分离。已有 [requirements-validation.txt](../tests/model/requirements-validation.txt) 固定 jsonschema 4.23.0 及五个传递包版本，工作区 `build/python-deps` 已安装；完整 wheel/工具链指纹由 D0.06-b 开发工具锁与环境描述记录。该 requirements 文件本身不冒称带所有 wheel hash 的完整 PyPI 锁，Python 包不进入产品链接或请求链。

## 已检验的源码与许可

`dependencies.lock` 是唯一版本、Git commit/SQLite SOURCE_ID、归档 SHA-256、归档格式、物化树 SHA-256、许可证路径/hash、来源与编译开关清单。以下链接均为官方项目的固定发布页；不在构建时查询 latest。归档只保存于 build 内，源码不提交。[THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES) 保留实际下载版本的完整许可证文本（SQLite 使用头部 public-domain blessing）。

| 依赖 | 固定版本 | 许可证 | 官方核验 |
|---|---|---|---|
| expected | 1.1.0 | CC0-1.0 | [固定发布](https://github.com/TartanLlama/expected/releases/tag/v1.1.0) |
| jsoncons | 0.178.0 | BSL-1.0 | [固定发布](https://github.com/danielaparker/jsoncons/releases/tag/v0.178.0) |
| thread_pool | 5.0.0 | MIT | [固定发布](https://github.com/bshoshany/thread-pool/releases/tag/v5.0.0) |
| asio | 1.30.2 | BSL-1.0 | [固定发布](https://github.com/chriskohlhoff/asio/releases/tag/asio-1-30-2) |
| immer | 0.8.1 | BSL-1.0 | [固定发布](https://github.com/arximboldi/immer/releases/tag/v0.8.1) |
| spdlog | 1.15.1 | MIT | [固定发布](https://github.com/gabime/spdlog/releases/tag/v1.15.1) |
| fmt | 11.1.4 | MIT | [固定发布](https://github.com/fmtlib/fmt/releases/tag/11.1.4) |
| toml11 | 4.4.0 | MIT | [固定发布](https://github.com/ToruNiina/toml11/releases/tag/v4.4.0) |
| cli11 | 2.5.0 | BSD-3-Clause | [固定发布](https://github.com/CLIUtils/CLI11/releases/tag/v2.5.0) |
| catch2 | 3.8.1 | BSL-1.0 | [固定发布](https://github.com/catchorg/Catch2/releases/tag/v3.8.1) |
| benchmark | 1.9.1 | Apache-2.0 | [固定发布](https://github.com/google/benchmark/releases/tag/v1.9.1) |
| sqlite | 3.51.3 | blessing | [固定发布](https://www.sqlite.org/releaselog/3_51_3.html) |

SQLite 选定 **3.51.3**。官方说明 WAL-reset 缺陷影响 3.7.0 至 3.51.2，3.51.3 已修复；本包拒绝低版本和被撤回的 3.52.0，不以整数版本比较自动接纳未知升级。[WAL 官方说明](https://www.sqlite.org/wal.html)、[3.51.3 发布说明](https://www.sqlite.org/releaselog/3_51_3.html)、[官方变更记录](https://www.sqlite.org/changes.html)。下载时还将 `sqlite3.c` 的 SHA3-256 与官方公布的 `32d5424f97e0a7fc5ed2f6335afbb58be4e0298bd7117a34e39d345ff13d859e` 核对一致；归档 SHA-256 与 SOURCE_ID 另存锁中。

SQLite 固定 THREADSAFE=1、默认 synchronous/FULL=2、WAL synchronous/FULL=2、DQS=0、OMIT_LOAD_EXTENSION=1、ENABLE_API_ARMOR=1。探针真实创建本地数据库、读回 WAL/FULL、验证 commit/rollback/checkpoint，并核对编译版本和 SOURCE_ID；尚未验证电源故障、耐久崩溃恢复或单 Host 产品治理。spdlog 固定静态构建与外部单一 fmt；上游例子、安装、测试和自行下载测试依赖均关闭。Catch2 探针使用固定发布携带的 amalgamation，Google Benchmark 禁止自行下载 GTest。

## 按组件取依赖与离线复现

`OCK_DEPENDENCY_COMPONENTS` 以分号列出显式组件。默认 `Embedded` 只获取 expected、thread_pool、spdlog、fmt；`All` 是显式测试全表。当前映射是探针选择，不修改 A02 产品目标 DAG：

| 选择 | 依赖 |
|---|---|
| Foundation | expected |
| Embedded | expected、thread_pool、spdlog、fmt |
| Data | expected、jsoncons |
| CpuPool | expected、thread_pool |
| LocalIPC | expected、jsoncons、asio |
| State | expected、immer |
| Logging | expected、spdlog、fmt |
| SQLite | expected、sqlite |
| Config | expected、jsoncons、toml11 |
| CLI | expected、jsoncons、asio、cli11 |
| Tests / Benchmarks | catch2 / benchmark |

未选择 SQLite、LocalIPC/CLI、State 时不会取得 sqlite、asio、immer。空组件列表不创建缓存；未知组件硬失败。`OCK_DEPENDENCY_CACHE` 可指向预提供归档缓存，`OCK_DEPENDENCIES_OFFLINE=ON` 禁止网络且缺失即失败。每次配置核对归档、全部物化文件与许可证 hash，缓存源码修改不能静默沿用。归档拒绝路径越界、特殊文件和大小预算超限；Immer 的两个上游文档链接只解析到同归档已存在普通文件并复制字节，不创建 OS 符号链接。

```powershell
cmake --preset win-msvc-debug
cmake --build --preset win-msvc-debug
ctest --preset win-msvc-debug
# Release / ASan 对应替换 preset 名字。
```

不依赖根产品实现的独立项目为 `tests/dependencies`。例如在两个不同的新目录重复执行以下命令；PowerShell 中带盘符的 `-D` 参数必须整体加引号：

```powershell
cmake -S tests/dependencies -B build/d0.06-a/repro-1 -G "Visual Studio 17 2022" -A x64 -T "v143,version=14.44.35207" "-DOCK_DEPENDENCY_COMPONENTS=All" "-DOCK_DEPENDENCY_CACHE=E:/VS2019Qt5.15/3D/build/d0.06-a/cache" "-DOCK_DEPENDENCIES_OFFLINE=ON" "-DCMAKE_SYSTEM_VERSION=10.0.26100.0"
cmake -E env MSBUILDDISABLENODEREUSE=1 cmake --build build/d0.06-a/repro-1 --config Debug --parallel 4 -- /nr:false
ctest --test-dir build/d0.06-a/repro-1 -C Debug --output-on-failure --no-tests=error
```

缓存只有本机构建路径，未导出产品安装目标。本包 `OCKThirdParty::*` 仅为探针链接别名；D1 Foundation 实现时必须把 expected 的实际公共依赖正确导出。jsoncons/immer/SQLite/Asio 继续是组件私有实现依赖，不能泄漏核心类型；静态最终链接依赖不能靠 PRIVATE 隐藏。安装重定位/公开头/冻结消费者由对应 SDK 工作包验证，依赖升级需独立审查与回归，不隐式改变 canonical/持久数据。

## 最小测试范围与后续责任

All 配置固定 15 项 CTest：12 个 `T01.dependencies.<依赖名>`，加 `toolchain`、`lock_contracts`、`catch2_assertion_guard`。Embedded 为四个依赖加后两项普通检查，共 6 项；ASan 加 `T23.dependencies.asan_healthy` 与 `T23.dependencies.asan_detects_heap_overflow`，共 8 项。lock_contracts 的入口为 `python -X utf8 -m unittest discover -s tests/dependencies -p test_lock.py -v`，目前 14 项反例/合同。Release 使用显式非零检查及 Catch2，不依赖可能被 NDEBUG 关闭的 assert。故障父验证器的每个真实子进程 argv、退出与原始输出保存在构建目录的独立 fault 子目录。

| 能力 | 本 checkpoint 实跑内容 | 未完成专项及责任 |
|---|---|---|
| expected | move-only、void、错误传播、异常路径 | D1.01 SDK 错误/ABI合同与消费边界 |
| jsoncons | JSON、Schema正反例与并发只读、普通CBOR往返、重复键上游行为记录 | D1.03–D1.04 预算/重复键拒绝/单DOM；D5.02-a canonical向量阻塞spike |
| BS thread_pool | 值/异常future、异步执行及wait | D3.01 inline/拒绝/自等待/排空和真实Executor Conformance |
| Asio | 真实Windows Named Pipe分段写入与聚合读取 | D7 IPC断线、控制满载、权限和取消专项 |
| immer | MSVC、不可变快照并发读取、transient后冻结与旧值不变 | D5 State发布与释放峰值专项 |
| spdlog/fmt | 实际格式化、sink写入和flush | D1.06 满队列/异常隔离/退出顺序与Logging Conformance |
| SQLite | 固定修复版、WAL/FULL读回、commit/rollback/checkpoint | D5.01/D6.01 存储事务故障/备份/Host独占/真实Conformance |
| toml11/CLI11 | 精确整数/溢出拒绝、UTF-8、空格参数、未知参数退出 | D7 配置secret/不支持类型、stdin/file与CLI完整协议 |
| Catch2/CTest/Benchmark | 真实断言与故意失败检查、CTest执行、固定迭代基准可运行 | D0.06-b发现/完整预期对照；D8逐请求分位/预算与正式测量 |

普通 CBOR 往返不判断 canonical feasibility，重复键上游投影不替代内核拒绝政策，Benchmark smoke 不产生性能门禁结论。

## 原始证据与当前结论

`tools/dependencies/record.py` 仅保存本 checkpoint 开发过程的 argv/cwd/时间/退出、commit/dirty、当时本包输入 hash 与 stdout/stderr 原始字节 hash；不自行产生包级 Passed。中间源码变化如实标记，不能把不同输入的成功片段拼为最终全绿。开发失败、重跑均在 [bootstrap/D0.06-a](../evidence/bootstrap/D0.06-a/) 的独立目录保留；正式整包来源、CTest发现/执行、产物及多配置关联由 D0.06-b 冻结后采集。

首次锁合同在实现缺失时真实失败；工具链不兼容反例也先失败，再加入最小拒绝验证。首次上游归档检查因 Immer 文档链接被拒绝，之后仅增加受限普通文件物化；未放开路径越界。第一次 CMake 参数因 PowerShell 未整体引用盘符路径真实失败，正确引用后通过。所有失败日志保留，不改写为成功。

实际试运行结果如下；源码输入均按各 run 保存，后续正式全量采集以冻结版本重建为准。两次新空目录均独立 configure/build/test，不要求重新从网络取得已有校验归档。初始配置期间工具链锁检查有明确追加，build 自动重新配置后才生成对应二进制，原始记录没有隐去这一步。

| 构建目录/选择/配置 | configure | build | 实际 CTest |
|---|---|---|---|
| trial-debug / All / Debug | exit 0 | exit 0 | 15/15，20260907T053915Z-82239e74 |
| empty-repeat-debug / All / Debug | exit 0 | exit 0，20260907T054826Z-8f3376d5 | 15/15，20260907T060041Z-50ecb6fe |
| empty-release / All / Release | exit 0 | exit 0，20260907T054818Z-05ed9657 | 15/15，20260907T060117Z-36ea6181 |
| asan / Embedded / Debug | exit 0 | exit 0 | 8/8，20260907T054829Z-45447d87 |

14/14 锁合同通过（20260907T054512Z-c190d8ea）。新空缓存 Embedded 实际只下载四项（20260907T054128Z-e1ce7f4d），完整库存核对 exit 0（20260907T055011Z-63b5c2a8）。ASan 故障子进程真实 exit 1，stderr 3518 字节，包含 heap-buffer-overflow；父检查的成功不改写该原始失败。两组全量构建并发时曾引起机器资源争用，后续测试已在构建结束后串行运行；正式采集应顺序执行配置矩阵。人工评审待进行。

## D1.01 验证工具链局部隔离

2026-09-07 的首轮 D1.01 正式矩阵中，Debug 34/34、Release 34/34 通过；ASan 两项 T24 消费者配置超过原定 180 秒，按失败保存，尚不构成 D1.01 验收。配置日志发现全局 vcpkg 自动链接及用户 VLD 搜索目录注入；同时存在其他工程编译占用资源，不能把 vcpkg 当作超时的唯一原因。原报告保留于 `evidence/98a406f2b612-1728e978a21b/`。

受控 preset 及三个独立验证驱动统一使用 `cmake/LockedMSVC.cmake`，在编译器识别前设置 `VcpkgEnabled=false`，将 `UserRootDir` 指向仓库无用户属性文件的目录，并把 `CMAKE_VS_GLOBALS` 显式传播给 ABI 的 `try_compile`。工具链保留其他 globals；正式矩阵使用 `--fresh` 重新进行编译器探测。SDK 导出不携带该验证工具链路径，消费者仍可独立选择其构建环境。此设置不修改机器全局配置，也不宣称屏蔽所有系统扩展或环境变量。[CMake globals](https://cmake.org/cmake/help/v3.31/variable/CMAKE_VS_GLOBALS.html)、[try_compile 传播](https://cmake.org/cmake/help/v3.31/variable/CMAKE_TRY_COMPILE_PLATFORM_VARIABLES.html)、[微软 vcpkg 开关](https://learn.microsoft.com/en-us/vcpkg/users/buildsystems/msbuild-integration#vcpkgenabled-use-vcpkg)。

隔离实验 `evidence/bootstrap/D1.01/full-isolation-probe-ba20bf2d12/` 保存实际配置、编译、运行及无 `/p` 覆盖的 MSBuild 查询。CompilerId、ABI、消费者三类项目均实际求值为关闭 vcpkg、使用隔离用户目录，配置日志不再带本机两项已知外部路径。T24 公开头消费者持续查询这三类项目；超时仍为 180 秒，失败仍保留且拒绝放行。后续以新来源、新完整矩阵另行验收。

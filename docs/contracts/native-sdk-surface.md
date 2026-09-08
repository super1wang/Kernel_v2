# D1.06 NativeSubset SDK 表面合同

状态：**Candidate，待并入 D1.06 独立 AI 规格复核**。本文件依据 v3.3 A19、A20、A21.4–A21.6、A22 和 D1.06，配合 [NativeHost 合同](native-host-api.md)与本包日志合同。只统一这次实现的 SDK 输入，不表示实现、测试、预算或审核已通过；Host/日志的签名、所有权、错误与停止语义仍以各自合同为准。

## 已核对的实际基线

当前生产文件仍是 `0.1.0-dev.1` / `CoreContracts` 阶段。

| 实际文件 | 当前事实 | D1.06 必要变化 |
|---|---|---|
| `cmake/TargetDependencies.cmake` | `OCK::Runtime` 是 `INTERFACE` 合同占位，依赖 CoreContracts | 换成真正可安装的 STATIC Runtime；保留组件 DAG |
| `cmake/OCKConfig.cmake.in` | `OCK_RUNTIME_AVAILABLE=FALSE`，只接受 Foundation/CoreContracts | 明确接受 Runtime，并仅报告 NativeSubset 能力 |
| 根 `CMakeLists.txt` | 安装 Foundation/CoreContracts 头，未安装 Runtime 实现 | 在 `BUILD_TESTING=OFF` 时也构建、安装 Runtime 与所需头 |
| `sdk/sdk_api_manifest.json`、`sdk_version.hpp` | dev.1；Runtime ContractBaseline；候选 runtime.hpp 尚不是实际公开头 | 审核新增表面、类型、版本与能力后同步为 dev.2 |
| `tests/contract/{registration,authorization,native}/CMakeLists.txt` | 分别把生产 registry.cpp、policy.cpp、invocation.cpp 编为三个测试装配 STATIC 库 | 改为链接同一个生产 Runtime，不在测试目录维持另一份实现编译 |
| `examples/native_service/CMakeLists.txt` | 依赖三个内部库及源码根 include 路径 | 安装示例使用公开 Host 和 `OCK::Runtime`；已有 D1.05 行为证据保留 |
| `tools/architecture/check.py` | 仅认识前三个旧阶段；公开头一律拒绝 detail include；图提取不识别 LINK_ONLY | 为本合同增加精确规则，不能放开任意 detail 或未知依赖 |

当前 `packages/runtime/{registry,policy,invocation}` 的生产逻辑已经存在，头文件注释仍明确为内部 API。Runtime 可安装化不等于重新实现注册、授权或调用管线。上述事实来自只读源码核对，本次未运行构建或测试。

## 生产 target 与真实依赖

固定生产 target 为本地 `ock_Runtime`、别名及导出名 `OCK::Runtime`，类型 `STATIC_LIBRARY`。其源文件集合包括原 `registry.cpp`、`policy.cpp`、`invocation.cpp`，以及本包新增 `packages/runtime/host/`、`packages/runtime/observability/` 实现。每个生产翻译单元只归属这个 target 编译一次。测试工厂、分配探针、conformance、benchmark 和证据工具不得进入 Runtime 的 SOURCES 或依赖闭包。

候选链接声明为：

```cmake
target_link_libraries(ock_Runtime PUBLIC OCK::CoreContracts PRIVATE bcrypt)
```

CoreContracts 继续只依赖 Foundation，Foundation 继续公开已锁定的 `OCKThirdParty::expected`。Runtime 的 OCK 组件闭包严格为 `{CoreContracts, Foundation}`；不引入 Data/jsoncons、State/immer、SQLite、Asio、Control、Workspace、CLI 或产品模块。默认内存日志是 Runtime 内实现，不把尚未实施的 spdlog Adapter::Logging、执行观察或文件日志宣称为可用组件。

静态库的 PRIVATE 链接项仍是最终消费者的必要链接输入。安装导出的 `OCK::Runtime` 必须保留 CoreContracts 以及 `$<LINK_ONLY:bcrypt>`，不能仅在本树可执行文件上手工补 `bcrypt`，也不能从 export 中删掉。LINK_ONLY 限定链接传播，不向消费者传播该依赖的编译使用要求；参见 [CMake LINK_ONLY](https://cmake.org/cmake/help/latest/manual/cmake-generator-expressions.7.html#genex:LINK_ONLY) 与 [target_link_libraries](https://cmake.org/cmake/help/latest/command/target_link_libraries.html)。实际锁定工具链的导出内容、链接命令和运行必须共同验证。

`bcrypt` 为 Windows 系统库，供 NativeHost 的 `BCryptGenRandom` 使用；Microsoft 列出的导入库为 Bcrypt.lib、系统 DLL 为 Bcrypt.dll。[BCryptGenRandom 官方说明](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom) 它不是新增 OCK 组件，也不是要从依赖缓存获取和随 SDK 复制的第三方 DLL。footprint 报告同时列明实际系统加载模块与应分发模块，不能把系统 bcrypt 隐去，也不能把整个系统 DLL 大小算作 SDK 分发增量。

target 图和 SDK manifest 中保留组件 `dependencies`、公开第三方 `external_dependencies`，为 NativeSubset 明确增加 `system_dependencies`：Runtime 仅有 `{name:"bcrypt", platform:"Windows", link_only:true}`，其余组件为空。保留原始链接项供核对；图提取只对受审的 `$<LINK_ONLY:bcrypt>` 作明确归类，未知生成表达式、未知系统库和本机绝对路径仍拒绝。增加 target 实际类型核对，不能仅凭名字或 manifest 声称 STATIC。

公开 C++20、异常/UTF-8/标准模式要求沿现有 CoreContracts/Foundation 传播；Runtime 自身的私有选项与公共选项分别登记。Debug/Release 的动态 CRT、ASan、调试信息及实际编译选项必须与生产者匹配，不能只从 `Debug` 标签猜测。不开启默认 LTO，不新增全局编译定义，不向 SDK 消费者传播本机隔离工具链路径或测试开关。跨 CRT、任意编译器 ABI 或直接替换 DLL 均不是本阶段承诺。

## 公开头、detail 与唯一来源

下面是本批次新增/整理的完整候选头集合。实际 manifest 必须同时登记源码路径、安装路径、组件、分类、包含边与摘要；旧 Foundation/CoreContracts 公开头继续逐项保留。

| 安装路径 | 组件 / 分类 | 唯一定义与职责 |
|---|---|---|
| `include/ock/contracts/logging.hpp` | CoreContracts / experimental | 普通日志固定值及窄端口；不得混用 RequiredRecordPort，不包含 Runtime |
| `include/ock/runtime/registry.hpp` | Runtime / experimental | 将现有 registry.hpp 迁至规范 include 根，保留既有命名空间与真实注册实现 |
| `include/ock/runtime/policy.hpp` | Runtime / experimental | 将现有 policy.hpp 迁至规范 include 根，提供 Host 装配所需预算、配置、可信端口与身份类型 |
| `include/ock/runtime/native_types.hpp` | Runtime / experimental | D1.05 的 ThreadRole、ThreadObservation、TrustedThreadPort、NativeBudget、InvokeOptions、TargetProjection、InvocationErrc/Record/Snapshot 唯一定义 |
| `include/ock/runtime/host.hpp` | Runtime / experimental | NativeHost、HostSession、HostBound 及固定 Host 配置/报告；具体签名沿 Host 合同 |
| `include/ock/runtime/logging.hpp` | Runtime / experimental | SafeLogger、默认内存日志工厂及 MemoryDiagnostics 等管理读取表面；名字/签名与日志合同合并 |
| `include/ock/runtime/detail/invocation.hpp` | Runtime / detail | 原 NativeEngine/NativeBound 与模板实现；直接复用原完整调用管线 |
| `include/ock/runtime/detail/private_bridge.hpp` | Runtime / detail | 原 NativeAccess 私有桥，dispatch/check/inspect 保持 private |

公开 Runtime 源根统一为 `packages/runtime/include/ock/runtime/`；日志窄合同源根为 `packages/contracts/include/ock/contracts/`。Host 模板所需实现如拆出 `detail/host.hpp`，须在同次声明审查中显式登记，不能因“detail”自动增加任意安装文件。本候选不新增空 `runtime.hpp` 占位，不让仍是 Planned 的候选路径冒充已交付头。

迁移是唯一源的移动与 include 修正，不复制一个“SDK 版”Registry/Policy/Invocation。旧内部路径如果暂为既有测试保留，只能作为单向 `#include` 转发，不再含定义、布局、模板逻辑或独立分派；生产实现与安装模板都引用规范头。上述 native_types 从 invocation.hpp 提取后，旧文件只包含该头，禁止用重复枚举/结构或测试专用声明掩盖 ODR 与版本漂移。

`host.hpp` 可在受审的包含边上使用本 Runtime 的 detail 实现，消费者的完整示例不得直接包含 detail，也不得依靠 detail 名字取得 Host 内部 engine、授权管理句柄或裸 handler。物理安装 detail 是模板编译需求，不授予公共稳定 API 身份；C++ private、原 NativeAccess 编译反例和 HostSession 的无 getter 边界仍是实际限制。不得用宏、friend 测试开关或第二个裸 dispatch 绕过。

现有 checker 对所有公开→detail 边一律拒绝，因此必须精确登记本组件模板实现包含边：只接受 manifest 列出的源头与 detail 目标；仍拒绝跨组件 detail、应用直接依赖 detail 完成公开示例、未登记 private 头和根目录逃逸 include。不能把 `/detail/` 检查整体关闭。Windows/bcrypt 类型与头只留在 `.cpp`，公开头不泄漏 HANDLE、Windows 宏、spdlog 或其他私有第三方类型。

所有本阶段公开 API 均为 experimental。保留 D1.05 的 Result 传输约束、错误/Outcome、身份、权限、目标、线程与预算语义；特别是 HostBound 不把旧 NativeBound 替换为轻量但缺授权的直接函数调用。CoreContracts 中新增 Logging 合同仍保持依赖方向，不引入 Host、Document、Runtime 指针或 ServiceLocator。

## 版本与能力的一致声明

本次候选统一开发版本为 `0.1.0-dev.2`，CMake project 的数值版本仍是 `0.1.0`，架构文档版本仍为 `3.3`。`sdk_version.hpp`、根 SDK_VERSION、OCKConfig、安装 manifest 和实际消费者输出必须一致。

候选公开元数据固定为：

- `OCK_IMPLEMENTATION_STAGE="NativeSubset"`，`OCK_RUNTIME_AVAILABLE=TRUE`；Runtime target 的 `OCK_IMPLEMENTATION_STAGE="NativeSubset"`，Foundation/CoreContracts 仍为 `Implemented`，其他占位组件仍为 `ContractBaseline`。
- `ock::sdk::version="0.1.0-dev.2"`、`ock::sdk::runtime_available=true`，另明确 `ock::sdk::implementation_stage="NativeSubset"`；`OCK_CONTRACT_BASELINE=0` 表示整个 SDK 不再仅有合同占位，不表示所有组件已实现。
- `find_package(OCK 0.1.0 CONFIG REQUIRED COMPONENTS Runtime)` 成功；Foundation/CoreContracts 继续可用。未实现组件仍 REQUIRED 拒绝，尤其 Data、State、Durable、Control、Adapter::Logging 及不存在的 Observation，不能因导出文件中存在合同 target 就称可用。
- HostCapabilities 中仅 Native Read、Native Compute、普通内存日志为真；异步执行、执行观察、状态、存储、恢复均为假。`runtime_available=true` 只表示有 NativeSubset，实现程度必须结合 stage/capabilities 读取，不能解释为完整 Embedded。

由于 CMake 数值 package version 无法独自证明预发布身份，独立消费者还必须核对 dev.2 字符串、NativeSubset stage 和能力。第一次正式 SDK 的 previous-release 仍不存在，不把 dev.1→dev.2 的开发对照称作正式旧版兼容证明，不写 ABI Passed。公共表面增加、关键声明审查和真实冻结消费样本三层分别保留。

## 独立安装消费者与最小测试影响

`examples/stateless_service/` 作为 C-A Native 子集消费者，使用独立工程与安装后的 `find_package`，只链接 `OCK::Runtime`，不得手加内部静态库、源码根 include、测试 fixture 或 bcrypt。消费者实际装配固定可信端口与有限配置，注册一个 Read 和一个 Compute，通过 NativeHost start→Ready→session verify/bind→HostBound.invoke 检查具体结果，并验证非法输入不进入 handler、停止后的准入拒绝以及安全停止。不能只做空 main、`sizeof(Host)` 或只引用头后凭链接器裁剪宣称 Runtime 已执行。

独立生产者采用 `BUILD_TESTING=OFF`，先真实 build Runtime，再 install；将安装树复制到新路径并使原前缀不可用，消费者只以新 OCK_DIR 配置。保存实际编译、链接命令、EXE、必要分发模块与加载模块身份；运行中的 `BCryptGenRandom` 来自真实 Host.create，不用强行引用无用符号或 `/WHOLEARCHIVE` 代替有效业务消费。不能通过 CLI 子进程代办内核业务。

下列是最小影响清单与候选主名，供 D1.06 一次冻结 expected；本文件不注册测试，不修改历史 expected，不把候选名当已发现或已执行。

| 候选主名 / 复用范围 | 必须保留的正控与反例 | 实际影响文件 |
|---|---|---|
| `T24.native_sdk.metadata` | dev.2/stage/能力/真实 STATIC 一致；错版本、把全部 Runtime 能力置真或未知组件必须拒绝 | sdk manifest、sdk_version.hpp、OCKConfig、sdk_metadata、阶段检查 |
| `T24.native_sdk.public_headers` | 每个 experimental 头独立包含，两个 TU 一起真实链接；min/max 宏保留；公开样例不直接依赖 detail | 新公开头、原 CoreContracts 头独立编译消费者、声明快照 |
| `T24.native_sdk.installed_host` | 搬迁安装树，真实 Read/Compute/错误/停止；移除原源码与测试 include 仍成功 | 新独立 C-A 消费者、install-consumer 驱动 |
| `T24.native_sdk.no_tests_producer` | 关闭测试后仍有实际 Runtime 静态库并成功安装运行；无测试工厂或探针链接 | 根生产 target/安装规则 |
| `T24.native_sdk.link_closure` | CoreContracts PUBLIC、bcrypt LINK_ONLY、最小闭包；消费工程不手补依赖；删 bcrypt 导出后实际 Host 链接必须失败并有对应未解析符号 | TargetDependencies、图提取、导出、实际 link trace |
| `T24.native_sdk.install_pruning_rejected` | 正控成功后，分别移除所需 Runtime `.lib`、一个公开头、一个实际被模板包含的 detail 头、expected 头；每次真实失败并核对对应缺失诊断 | 已有裁剪驱动的受审扩展，独立副本不复用失败产物 |
| `T02.native_sdk.private_dispatch` | 公开 Host 正控可编译；NativeAccess dispatch/check/inspect、裸 handler/getter、伪造 HostSession/HostBound 独立负例实际拒绝 | 复用原 D1.05 正/负片段、补 Host 表面负例 |
| `T01.native_sdk.surface_guard` | 删除/偷加头、未登记 detail 边、未知 LINK_ONLY、丢失系统依赖或错误 implementation stage 不能过关 | architecture checker 与其已有单测 |

原 `tests/install_consumer/{verify_baseline,verify_foundation,verify_contracts}.py`、`baseline/CMakeLists.txt`、`contracts.cpp`、`tests/compile/sdk_metadata.cpp` 及 D1.05 `component_boundary` 中写死的 Runtime 不可用、dev.1 和三内部库关系，需要在本批次同步审查。旧阶段规则作为历史样本保留；当前 NativeSubset 的预期必须先独立冻结，不能按本次实际导出反向生成期望，也不能删掉旧反例后声称历史 SDK 兼容。

Registry/Policy/Invocation 头迁移与统一链接直接影响其既有消费者、模板约束及私有分派编译片段；复用同一实现的 Debug 影响集验证这些边界。SDK/公开头/导出/CRT 变化触发干净安装消费，并为 Release、ASan 做真实配置传播专项。LoggingConformance、Host 生命周期/错误/零分配和 footprint 各自已有本包责任集合，此表不另建第二套运行时或要求每次微小改动重跑历史全矩阵。最终 D1.06 验收仍使用独立审核后冻结的包级集合与受审配置。

所有真实子命令复用现有 owned process 执行器，保存原始 stdout/stderr、诊断、退出、排空及源/工具链/配置身份；配置失败、错误二进制、缺少预期消费者、缺少链接/运行或裁剪后未失败均拒绝。测试事实、AI 审核与包状态分列。占用数字仍遵循 pilot→独立审批有限预算→新报告验证，不在 SDK 表面 Candidate 中填写预算或 Passed。

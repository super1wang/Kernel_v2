# D1.02 集成初版独立 AI 规格审核

日期：2026-09-07。actor_type：AI。结论：发现两项必须修复的集成规格缺口，本初版不批准该快照进入最终验收。尚无本轮 SDK green，主任务当时正在采集真实 SDK red；文件审核及下面的只读诊断不构成测试矩阵或包级 Passed。

审核范围仅为父任务负责的 T01.contracts.component_closure、T01.contracts.public_include_boundary，以及 T24.contracts.public_headers、installed_component_consumer、install_pruning_rejected 对应四个文件。对照已冻结 D1.02 计划和 core-contracts-api.md；六个尚在实施的公开头不在本轮审查范围。只写本记录，无源码、测试、计划、expected、旧报告或 Git 修改。

## 必须修复

1. **公开编译宏漂移未被守卫捕获。** `validate_manifest` 精确核对依赖、compile_features 和 compile_options，却不核对 `INTERFACE_COMPILE_DEFINITIONS` 的实际导出值或公开清单值；新增 PublicBoundaryTests 也没有对应宏漂移反例。只读 Python 诊断把 `UNREVIEWED_PUBLIC_MACRO=1` 放进模拟实际 CoreContracts graph 的 `compile_definitions`，以及同时放进 manifest 的 `public_compile_definitions`，三次完整检查返回的错误列表完全相同（原始基线因进行中来源有 2 项错误，新增宏两种情况下均增加 0 项）。这证明检查忽略该字段；不把基线已有错误当作宏被检出。

   建议先固定反例，再把实际 target graph 的公开 compile definitions 与 manifest 显式字段纳入逐目标精确比较；当前没有公开宏的目标应登记空集合，而非允许任意值。覆盖只改实际值、只改登记值、同时新增未受审宏，以及缺失字段的拒绝；现有 features/options 精确检查继续保留。此项落实计划关于“意外公开头/宏/编译条件变动被清单与声明检查捕获”的原要求。

2. **ASan 配置没有传到独立 SDK 消费者。** `verify_contracts.py` 只从入口获得 Debug/Release，重建的 consumer 使用独立 VS 工程，没有读取 producer cache 的 `OCK_ENABLE_ASAN`，也没有设置 AddressSanitizer、匹配的调试信息/RTC/增量链接条件。无测试 producer 始终选 `win-msvc-debug`。因此由 ASan profile 调用这三项时，实际新编译的消费者仍是普通 Debug；其成功不能证明既定 ASan 适用集合中的 SDK 消费路径。

   建议读取被测 producer 的实际配置，显式传播 ASan 到消费者与对应 no-tests producer；同时沿已验证工具链设置 Embedded、去除不兼容 RTC、禁增量链接，并为实际 ASan 运行添加实际编译器 runtime 目录。保留消费者真实 cache、实际 MSBuild 配置/属性或编译命令，证明其 Config/Platform/工具链与 sanitizer 状态，而不是只记录父 profile 名称。不要修改 fixed expected 来跳过这个差异，也不要把未插桩 Debug 的成功计为已验证 ASan。

## 已有保护与补强建议

- 裁剪测试先真实运行完整安装正例，再在独立副本逐一删除 Foundation 与 tl::expected 头；负例要求进程正常退出、非零、C1083 和具体目标头同时出现。configure 仍必须成功，超时/Job 异常不被当作负例通过。未实施 Runtime/Data 和不存在的 Observation 组件检查具体 find_package 拒绝诊断，没有启用生产运行时模块。
- SDK 从安装树复制并将原前缀改名，扫描导出 CMake 不得带源码/原安装绝对路径；消费者仅链接 OCK::CoreContracts，检查其链接闭包为 OCK::Foundation，并检查实现阶段与 runtime_available=false。BUILD_TESTING=OFF 的另一个 producer 也进入安装消费路径。当前这是静态检查到的逻辑，不是已完成的 SDK 实测。
- 六头分别生成独立编译单元，另有 min/max 宏环境消费者。该消费者目前只检查宏仍有定义；建议再用宏的原始展开结果做编译断言，防止“保留名字却改变定义”漏检。
- Foundation 头及其既有 T24 回归保留固定 expected 版本、异常启用、C++20/__cplusplus 检查；CoreContracts 的链接闭包使这些公共条件进入消费者。此次未发现刻意关闭异常模式的代码，但正例默认编译成功本身不能替代公开选项/宏的漂移检查。修复第一项时应保留并实际运行已有选项漂移、无异常后端拒绝回归。
- `contracts.cpp` 已消费版本、真实类型 token、非法绑定顺序、九类 Outcome 类型、合法只读 Outcome 和重复完成检测。建议增加一个合法 compute 定义/成功 typed 绑定及输入拥有准备的小型安装正例，使安装消费者同时经过 typed 成功路径；仍只用测试目录端口，不建立生产 Registry/Invocation。

## 后续核验边界

主任务已接受上述两项并计划先保存对应真实反例。初版冻结的是下表读取字节；后续修复需另行补审新来源，不能把本文件当作修复已完成的批准。实际 SDK green、全部配置、安装裁剪原始日志和源/配置/二进制绑定仍由正式采集证明。D1.01 原报告、当前固定 38 项及 191/191/193 与三 CHECK 均不得因本轮修复删减。

## 读取快照 SHA-256

| 文件 | SHA-256 |
|---|---|
| `tools/architecture/check.py` | `73d40ec7e75a082d7f1223c88111c8fc491c94c5af573c30fb18247a5687feae` |
| `tests/architecture/test_contracts_stage.py` | `17aa770323927f93ecf218a2f4ff7f53635c6cc725ea780e1277c20b698b0767` |
| `tests/install_consumer/verify_contracts.py` | `4024cb71d8404e53bc2e064d7814b398c2e266f1d0c4ee058fb85a0fd02b63fc` |
| `tests/install_consumer/contracts.cpp` | `40c7e5d2e8b1b9d09e5c27a53d088a2d7a94240944ce94ee2ac30d01724ec62a` |
| `docs/plans/D1.02.md` | `2e45fe3dba30e6604ca8c7d91c1f3b7e298d7ef8a741a99d9a19176d2a19b212` |
| `docs/contracts/core-contracts-api.md` | `48a8fae8e34d9e2532095a437c6b10088cd54b62c3e84e36cc78d5029c245bf7` |

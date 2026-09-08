# P0 CompileContracts 夹具局部验证

本记录仅描述开发支持工具，不修改 D1.01–D1.05 历史验收，也不声明 D1.06 包级 Passed。最终七文件输入与实际命令计数见 [compile-summary.json](compile-summary.json)，组合 SHA-256 为 `eb5fc3027ee9513b7a0b27e0f696b3119a037105eac636d5ff25d88ead241327`。独立 AI 复核结论由主任务另记。

同一次 CTest 读取期产生独立随机 run_id。`compile_fixture_setup` 只配置一个含 33 个独立 OBJECT target 的工程；十个原编译包装各自执行 native、一个正控和所有负例。完整名称、源码与短 target 映射写入 ready 身份。每次编译前后核对实际输入及生成工程；正控对象只在本夹具、本 target、本配置下删除，以强制实际重编。不同 CTest 运行不会复用 ready 或旧构建目录，共享工程使用 RESOURCE_LOCK 串行访问。

## 实际结果

| 范围 | 结果 | 原始目录 |
|---|---:|---|
| Debug 十编译包装、原 noexcept、两个工具项 | 13/13 | `compile-ctest-a54cabb9c181` |
| Debug 实际独立编译 | 1 configure、10 正控、23 负例 | 同上，逐 wrapper commands.json |
| Release 配置传播 | setup + read_shape 2/2；1 正控、2 负例 | `compile-risk-release-off-cca18e07b8` |
| ASan 配置传播 | setup + read_shape 2/2；1 正控、2 负例 | `compile-risk-debug-on-aca361896c` |
| ASan 工程属性只读核验 | 实际 `/fsanitize=address`、`EnableAsan=true`、Debug DLL CRT、无 RTC | `compile-risk-debug-on-63a76f56a0` |
| 六项实际 MSVC 控制 | 全部成立 | `compile-controls-76275046d617` |

六项真实控制分别是：同一正控确实重编两次；`#error` 正控失败不能接受；实际缺失 PlatformToolset 产生 MSB8020，不能冒充合同拒绝；既有 run 不可重新准备；实际错误 toolset 导致 setup 失败且不发布 ready；把真实已编译对象复制到失败目录，也不能绕过 ready 门禁。额外护栏包含实际 Python `-O` 子进程、身份每维漂移、生成文件删除、漏执行/重复执行、discovery 少报以及 Job 被清理终止后不能 Passed。

ASan 首个只读检查器误写 XML 元素 `EnableASAN`；实际 CMake 元素为 `EnableAsan`。原始 CTest 已为 2/2，原失败读器快照和日志保留。只修正证据读器并另开只读核验目录，没有重跑编译或改写原始测试结果。

## 性能结论

最终一次串行对照使用相同 Debug native 二进制、工具链、头路径及两个包装（read_shape 含两个负例，typed_value_validation 含一个负例）。原版通过显式 `--baseline-snapshot` 执行保留的原始代码字节，维持原 `__file__` 和支持头查找语义。两个命令均在本次测量进程局部添加实际 compiler runtime 路径。

| 最终单次对照 | 总秒数 | configure | 正控 build | 负例 build |
|---|---:|---:|---:|---:|
| 原版 `compile-baseline-ee5f1a463a23` | 58.21 | 2 | 2 | 3 |
| 夹具 `compile-fixture-60c7d4cd0af4` | 65.69 | 1 | 2 | 3 |

**配置复用有效，稳定墙钟提速未证实。** 最终对照慢约 12.9%。实际配置阶段合计从 25.45 秒降到 11.64 秒，但该轮后三次 build 从原版 5.68/8.32/5.52 秒变为 11.97/12.87/10.78 秒。早期预览为 71.82→61.63 秒，方向与最终轮相反；两者全部保留，不选择有利轮次，也不把单次波动外推为完整 Profile 收益。

## 失败记录与材料

- `compile-red-b55886d52e92`：新增夹具实现前的导入失败。
- `compile-red-899c2aa5903a`：长 target 名与 Python 优化模式护栏的实际 red。
- `compile-ctest-b432d830ed36`：Windows 260 字符路径限制导致 MSB3491；驱动正确拒绝。后续改短内部名，原测试主名不变。
- 沙箱实际 compiler 配置失败、CXX profile 生成冲突、局部未构建 Foundation 的根目录 discovery 失败及中间护栏失败均保留各自原目录。

[compile-raw-text.zip](compile-raw-text.zip) 选择归档了原始流、命令、JUnit、源文件、生成 CMake/vcxproj、配置及身份，共 1139 项、1,731,431 字节。归档已逐项读回校验 SHA 和长度；完整成员索引与归档 SHA 见 [compile-archive-index.json](compile-archive-index.json)。未删除或迁移任何原目录，重复对象、DLL、PDB 和二进制缓存未加入归档。建议提交此报告、摘要、归档、索引及两个局部证据驱动，不机械加入所有原构建目录。

`file(GENERATE)` 的配置/语言限定依据 [CMake 官方文档](https://cmake.org/cmake/help/latest/command/file.html#generate)。无生产 Host/Logging/API 改动，无全矩阵重跑，无系统环境或 Windows 长路径策略修改。

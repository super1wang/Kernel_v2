# D1.02 工具修复独立 AI 复核（a0159d）

结论：工具实现与工厂摘要修复 AI Approved；本轮整体归档配置仍 NeedsChanges（见两项必改）。不是 human Approved，不代表 D1.02 包级自动验收完成。

## 已验证修复

resolved_path()先解析实际目标，再仅将 Windows 扩展盘符路径与扩展UNC转换为普通等价表示；未删除实际containment检查，也没有将外部目标映射到工作区。source、workspace、cache成员和stage清理边界均使用同一规范化。旧 P2 扩展前缀误拒绝已修复；此前 NeedsChanges 报告与失败证据保留。

本复核在既有 WindowsJobObject 包装下实际运行旧独立8方并发探针与当前5项单元测试。退出0、active_after=0：8调用1胜7负、发布前完整metadata、复用所有文件mtime不变；等价扩展盘符可用、外部路径仍拒绝、UNC表示相同、metadata hardlink不外写、篡改拒绝全部通过。真实原安装91文件与副本90文件SHA再核验一致。原始命令和流见 review-final-tools-a0159d/command.json、stdout.log、stderr.log；源SHA见同目录sha256.json。探针中的BOUNDARY行是外部路径反例预期拒绝，测试通过。

工厂摘要现在覆盖固定顺序的test_support.hpp、factories.hpp、authority_factories.hpp，每个头均CONFIGURE_DEPENDS。独立重算当前构建factory-inputs.json，三个文件SHA及路径+SHA组合摘要一致：e275586c46815e9080715224e00cf393c81a45f2b9b27230202dbfe59110ed1e。副本已归档本证据目录。

## 仍需修改的归档配置

1. 三份 run manifest build_outputs 指定 CMakeFiles/3.31.6/CMakeCXXCompiler.cmake，但实际锁定VS bundled CMake生成目录为3.31.6-msvc6。将按当前声明触发missing build artifact。应使用实际目录，或工程生成稳定命名证据文件。
2. 新增递归归档pattern与当前validator语义不一致：run.py使用Path.glob递归**，validate.py第46行使用Python3.11 Path.match，**不会递归。因此include/ock/contracts/operation.hpp不能匹配include/**/*；多层CompilerId/consumer tlog不能匹配build/**/*.tlog。对本机已有Debug manifest glob收集集合实际检查发现366个路径会被validator拒绝。应在manifest中精确声明实际深度或统一两者语义；修后逐一核对收集集的匹配，避免正式验收运行结束后才失败。

其余manifest新增SDK relocated/pruned头与导出、最低4/2份导出、4份operation.hpp、10份positive.obj、配置日志和factory-inputs.json方向正确。三个配置仍为191/191/193 expected且3 CHECK；本次读取时inputs摘要同为7b103aca4f20aaab51c28e176e67a8058419ec3ed848ec150a11f6739377b949。此摘要不覆盖随后修订，不作为未来运行已通过的声明。

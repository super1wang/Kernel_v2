# D1.02 开发工具最终独立 AI 代码复核

结论：NeedsChanges（1 项 P2）；这是 AI 代码复核，不是 human Approved，也不代表工作包验收。

范围：tools/development/msvc_isolation.py、cmake/LockedMSVC.cmake、cmake/msvc-validation-tools.json、tests/architecture/test_msvc_isolation.py。对照 progress、v3.3 执行计划 D1.02 与父任务给定固定工具链约束。受审文件 SHA-256 见 review-final-tools-70b712/sha256.json。

## P2：首次并发创建缓存时，Windows 扩展路径前缀导致误拒绝

位置：tools/development/msvc_isolation.py 的 parent.resolve().is_relative_to(workspace)（约第47行）。实际 Windows Python3.11 下，8 个线程并发执行 prepare 时，在 parent 尚不存在、被其它线程创建的时间窗，Path.resolve() 会返回带扩展盘符前缀的路径，workspace 则无此前缀。is_relative_to 将两者视为不同 anchor，错误抛出 cache escaped workspace；所有路径实际都在同一临时测试工作区。

确切实测值（diagnostic6/stdout.log）：parent 为 `\\?\E:\VS2019Qt5.15\3D\build\review-tools-probe-70b712-rerun6\build\msvc-validation`，workspace 为 `E:\VS2019Qt5.15\3D\build\review-tools-probe-70b712-rerun6`。本轮2调用因此拒绝，其余6调用因探针预设8方同步发布而发生 BrokenBarrierError；barrier超时是探针后果，路径误拒绝是实际原因。该问题造成偶发 configure 失败，不造成外写。

建议在保留真实边界验证的前提下统一 Windows 等价路径表示，并加入此前缀反例与首次并发准备回归；不能通过删除边界检查解决。失败证据独立保存于 review-final-tools-70b712、-rerun、-diagnostic6，没有被后续绿色运行覆盖。

## 已确认通过的范围

- 旧 metadata-hardlink 红轮确实退出1，新绿色合同轮3项退出0；代码现对已有缓存只读 exact verify，不再将元数据写入旧 hardlink，因此原安装文件不再被覆盖。
- stage 包含完整 bin、local-tools.props 和 inputs.json 后一次 rename；独立同步探针成功轮实际8调用、1胜7负，全部返回同一缓存，失败竞争者删除自己stage，无partial残留。成功证据 review-final-tools-70b712-diagnostic 与 -diagnostic2 均由既有 WindowsJobObject 包装运行、退出0、active_after=0。
- 已有缓存复用前后全部文件 mtime 不变；3项正式单元测试再次通过；真实安装目录91文件与锁全部SHA一致，缓存90文件与锁全部SHA一致，仅排除vctip.exe。
- stable-tools-configure-e778cac256 命令退出0。其 build/d1.02-debug-integration 的 CMakeConfigureLog.yaml 显示 CompilerId 的 CL/link、ABI 的 CL/link、linker识别均使用 build/msvc-validation/67414627e4e76f7e/bin；ABI使用 /Z7。CMakeCXXCompiler.cmake 中 compiler/ar/link均指向副本。CMakeCache固定工具集14.44.35207、Windows SDK10.0.26100.0。上述构建文件SHA已保存。
- 受审实现无改写原安装、按名称终止外部process或弱化WindowsJob的代码。

## 边界与证据说明

独立探针只写 build 下自己的小型假安装/缓存fixture；真实安装与正式90文件缓存全程只读。原始探针最终版本位于 build/review-tools-probe-70b712.py。绿色运行证明发布竞争分支正常，不能覆盖首次父目录并发边界误拒绝。父任务另外报告的MSBuild PATH/Path环境冲突不在本报告中推定为C++产品问题。

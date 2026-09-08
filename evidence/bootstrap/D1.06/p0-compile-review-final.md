# P0 Compile Fixture 最终独立增量复核

日期：2026-09-08。SPEC：Approved；CODE：Approved（编译夹具正确性及工具范围）。第一轮 p0-compile-review.md 的 ChangesRequested 原样保留。审核者未实施七文件，本轮只读修订、必要原始证据和产物，不重跑测试、不操作共享 build、不宣布 D1.06 包级 Passed。

C1–C4 均已关闭，无未关闭的必修代码问题：

- C1：native 判定改为 check_native 显式 ValueError，noexcept 两种控制的 assert 全部换显式拒绝。实际 -O 子进程负控制在 13 项 guards 中通过；CTest JUnit 的 guard 输出被 1024 字节阈值截断，因此另读本轮 LastTest.log 的完整 13 项及 OK，未把截断内容当完整原始输出。
- C2：wrapper_arguments 丢弃旧物化值及新版双引号动态 fixture/run-id 参数，随后添加当前独立值；controls 复用这一解析器。新 discovery 的实际六控制全部成立，不再只靠旧格式的历史成功。
- C3：短内部目标 ccNN_p/ccNN_nNN 与完整 case/name/body 一起绑定 ready 身份，保持一项一编译、清理和诊断归属。完整局部 13/13 不再遇到原 MSB3491；旧长路径失败保留。
- C4：baseline 必须显式传原始 snapshot，保存并绑定其 SHA，以原 __file__ 语义执行所选原始字节；fixture 执行当前版本。核对本轮原始 snapshot、inputs.json 和实际命令一致，没有将当前实现标成原版基线。

独立核验 compile-ctest-a54cabb9c181 的 discovered/JUnit/expected 精确 13 项、无失败或跳过；经 JUnit 内实际 evidence 路径逐个展开，确认 10 次正控退出 0、23 次独立负例各有其短源名与允许 C++ 合同诊断且非基础设施失败，11 个 native 包装均实际执行，noexcept 退出 3 与 marker 一致。setup 和十编译包装使用唯一 94d81f3151631e917c7607af，-N 的夹具目录不存在、未被执行复用。33 个 target 无重复/缺项，当前 ready 中全部输入 SHA 与实物一致。相应子命令 raw 大小/SHA、Job assigned-before-resume/active-after=0/未依赖强制终止均核对。

compile-controls-76275046d617 的六条真实命令及六项控制已核对：正控两次实编、坏正控确实失败、MSB8020 不算合同拒绝、旧 run 不重用、实际失败 setup 无 ready、遗留真实旧对象仍不能通过。Release 配置专项 2 项与 ASan 专项 2 项的 JUnit、三个实际工程文件 SHA 也核对；ASan 确有 EnableAsan=true、/fsanitize=address 且无 /RTC。原 EnableASAN 大小写读器失败与随后只读核验分开保留，没有改称一次新的编译执行。

compile-raw-text.zip 的 SHA 为 02514fd4fcdbfab60ba262849f82cc99f4c9622d12f39407cc114c1b50a749be；独立读回全部 1139 成员并与 index 的长度/SHA 对照，无重复成员。compile-summary.json SHA 为 88f5e14f70c2dc0ec26bef3f0b734a8056dd14619b77dc46363c2b3bc917e69d。

性能结论单独保留限制：最终串行两包装对照，configure 2→1、实际 build 5→5、内层 owned commands 9→8 已证实；墙钟 58.2084879→65.6880972 秒，约慢 12.85%。没有证实稳定提速，不签“墙钟优化 Passed”，也不能选取更有利的早期预览轮次替代最终结果。本代码批准仅说明共享准备与必要覆盖正确，不赋予性能收益结论。

当前七文件集合 SHA-256：eb5fc3027ee9513b7a0b27e0f696b3119a037105eac636d5ff25d88ead241327（已独立重算）。

- `tests/compile/contracts/CMakeLists.txt`：`ff5f540b5ffbab5eb7417f3a2eebd5e8b27be1d218067e16a3af314fc9328644`
- `tests/compile/contracts/discover.py`：`e5b94f92063d84f59fd3388ff81e5d5aa8b5a39a0da14d78aef2fe0c5b1e2098`
- `tests/compile/contracts/fixture.py`：`243b292a785087afbd941bfe0a693a56c848d484dd07a00c9b4cd801eb32b96f`
- `tests/compile/contracts/verify_children.py`：`99efc3f582c9e3805444fb3676aa819dbcd1fc3e1dc8e9f8d56cbaf6520e58b5`
- `tests/tools/compile/test_fixture.py`：`69d5b8fd962ad3a7fba7e448d95fffa7a4062de04f19c24e88c76c5aebea57af`
- `tests/tools/compile/measure.py`：`bc14bfcc1d6e7ef5e27606b58343f7af1c4cc50670c14cd2efa0f3e41c811c48`
- `tests/tools/compile/controls.py`：`ec47bd123da871bac3e063f85dff8a4983ce5f4f80d6eb973fafe2007e9341f6`

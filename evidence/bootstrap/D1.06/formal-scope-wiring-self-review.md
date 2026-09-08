# D1.06 正式集合候选与 Logging 资格接线自审

状态：**本有限开发任务完成，未正式验收**；D1.06/G1 仍未 Passed。按用户最新“只完成开发任务，测试不要做了”指令，停止后续测试、构建、消费者、pilot 和正式 footprint 接线计划。收到指令时没有运行中的 owned 子进程。下列结果均为指令到达前已结束的历史局部验证，不安排补跑或新增审批链。没有运行正式三配置矩阵，没有改生产实现、历史 expected、通用 run/audit 或预算，没有提交。

## 冻结输入及交付

24 个本次修改/新增输入绑定于 `formal-scope-wiring-index.json`，集合 SHA256 `3ef687ef5379da892750677b85daa140cbe3a4f8ef8aa67c57f75241113a8c98`。算法为按相对路径排序后的 UTF-8 `path TAB sha256 LF` 拼接再 SHA256。归档 `formal-scope-wiring-raw.zip` SHA256 `b2b5f86b71e7d1991744da69ca47edda540ee8295ea6cdb5d3f57a7a7b0ca12f`，563 个逐项 SHA/size 成员；保留原目录，不收 obj/编译缓存。

候选 expected 由原 D1.05/D0.06 固定 SHA 和手写职责展开，无 discovery 反推。Debug 270、Release 192、ASan 193 CTest，另各保留三个原 CHECK。Host20、Logging12、SDK8、footprint11 都有配置映射，公共 Runtime/合同头影响 C/R/P/N/Host/Logging/SDK，Foundation 和未知路径保守扩大。九个遗漏反例先实际失败，再使 dev 检查通过；原模型跨包 CHECK 保留而不机械三配置重跑全部历史 Python 模型。

正式三 run 都加载无条件拒绝的候选 preload；D1.06/G1 两 gate 引用同三份未来报告。footprint 正式入口与先批准有限预算未冻结，必需报告位置登记但未造占位工件。限定 L0 明确移除此专用 preload 并记录，完整包选择保留；这不是正式解锁开关。

Logging 保留原 12 主名。编译标记绑定真实 logging.cpp、公开合同、实际测试工厂、探针和资格源码；CTest 每次读取时生成新 run_id，`-N` 不创建结果。11 项结果与末项真实分配测试都独立 owned execute；末项核对每条原始 argv、退出、assigned-before-resume、active_after=0、无 Job 强制清理、流 SHA/size、完成标记、同 binary/source/run，再聚合两工厂共同五项。依赖只控制顺序，不代替资格。版本固定 `ock.logging.conformance/1`；复用原通用描述符检查，Executor bootstrap 保持原合同及范围。

## 实际结果

| 材料 | 实际结论 |
|---|---|
| `scope-mapping-red-7d3f06d6b6` | 九项漏选反例：5 failure + 4 error；原驱动保存 |
| `scope-mapping-green-d6cd0a5211` | tests/tools/dev 27 项通过 |
| `logging-qualification-unwired-65a3bd2856` | 原 CMake 未接资格包装的实际断言失败 |
| `logging-qualification-5d3a4658f7` | 冻结局部 Debug 编译、12 CTest 全通过；41 工具检查通过；正式 preload 实际退出1且固定阻断原因成立 |
| `logging-qualification-controls-479cbc9ea6` | 三个正式选择守卫通过；`-N` 无副作用；新 CTest 仅末项真实退出8，新run不能借旧11项，旧资格 SHA 不变；实际记录跨run/binary/source/Failed变异均被拒绝 |

成功 Logging run_id `fc1eb05d09d3dacb38354df0`；binary SHA `999a9b1f419b67c3b09ade4c3372792a5d4752dff55cafe62e29f34a05f4c26b`；编译输入 SHA `b8376bd4977c51184d709b40072dc0d9db7030d1df5bcf7a006a94b33a79c631`。实际生产工厂源码 SHA `a28ab62996a4360eb0907dff6cff65ead4b1ce5939b0f3ed7bd2bc3887c79067`。两个资格都为 true；async/file 能力为 false 的可选项为 NotApplicable，不能算 Passed 或替代新后端共同合同。

原失败 `8d73c52c02`、`a58636db31` 是新局部快照遗漏 LockedMSVC 支持文件，已补齐快照而未改工具链；`5382e06abf` 是沙箱编译器发现失败，按授权 require_escalated 重试为 `5d3a4658f7`，未改全局 PATH。所有失败命令和 raw 保留，未转写为逻辑通过。

## 限制与最终接线注意

本轮局部工程只静态编译真实 logging.cpp，用原目录 CMake 验证资格接线，未重建完整 Runtime 或 Host，也没有把既有 10+6 开发结果拼成这次 12 项。正式配置仍须在同源完整 Runtime 上运行适用职责；本轮没有声称 Release/ASan 资格接线已经实跑。

直接筛选固定分配末项而没有同轮其他 Logging 项会按设计失败；L0 的 Logging family 选全12。可独立使用旧 develop.py 做行为开发，但其 `development-unbound` 不能生成正式资格。最终正式入口/预算闭环齐备之后，应一次修订候选 preload/报告路径并独立核对，不自动添加余量、不以本轮材料批准预算。

当前暂留待办而不继续执行：footprint 11 个正式入口与证据聚合、pilot/独立有限预算决定/新正式报告、完整 Runtime 上的三配置正式验收及 D1.06/G1 放行。formal preload 保持明确拒绝。根任务负责按未验收状态提交、推送后暂停，本子任务不自行提交或扩大。

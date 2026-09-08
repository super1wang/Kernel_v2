# P0 Compile Fixture 独立交叉复核（第一轮）

日期：2026-09-08。审核者未实施本范围，仅阅读规格、七个源码文件及最新结果摘要；未操作共享 build。SPEC：Approved（限定设计方向）；CODE：ChangesRequested。不是整体测试 Passed，也不是正式包验收。

依据：Kernel_v2_开发提速与Token精简决策方案_v2_2026-09-08.md 第 6、14 节。只复用同一 Profile 的 configure；每个正负 target 仍独立编译、验证退出码/目标诊断及 Job 排空，集合对照不得吞漏测。

## 已核对成立的设计

ready 绑定 run_id、实际编译器/前后端 DLL/CMake SHA、父 target 的 CRT/ASan/选项与配置、SDK/toolset/平台、依赖锁、公开头和工厂源。prepare 用 exist_ok=False 建新目录，configure 失败不发布 ready；require_ready 在每个 target 前后核对当前输入及生成工程/缓存身份。对象仅限本夹具 target/config 范围删除，随后日志必须含相应源文件编译；不同负例各自命令与诊断，不合并为一个失败目标。

CTest include 每次读取时产生新的随机 run_id；setup 与消费使用同一读取期值，RESOURCE_LOCK 限制共享 MSBuild 并发。原生主用例仍先执行；编译集合由明确映射逐项执行，10 个编译包装、setup/guards 和 noexcept 包装在局部 CTest 对照中独立列出。good/check_compile 拒绝 Job 强制清理、未排空和工具链错误。这些安排符合“复用准备、不复用结论”。

## 必修发现

| ID | 级别 | 位置与触发 | 必要修复 |
|---|---|---|---|
| C1 | P1 | verify_children.py:47、50、51 仍用 assert 判断 native 与 noexcept 结果。Python -O 或 PYTHONOPTIMIZE 会删掉判定，native 非零可能被后续编译成功覆盖，最后打印 Passed；noexcept marker/返回码也会不再检查。 | 换显式 if/raise，实际增加优化模式下失败判定反例。即使为继承旧代码，本轮包装不能继续允许这一验收逃逸。 |
| C2 | P2 | measure.py:77、controls.py:26 的 bracket 正则读取新版 discovery 时，能取出 --fixture/--run-id 标志，却丢失双引号动态 ${...} 值；后追加参数前已有孤立标志。 | 明确解析/移除动态参数后再放入本次独立身份，或改读 CTest JSON 命令；新增旧/新版 discovery 解析回归。现有成功对照使用旧 discovery，不证明新格式可用。 |
| C3 | P1 | fixture.py:168–171、192 将完整逻辑 target 用作 CMake 内部目标/文件名；当前完整局部 CTest 摘要 success=false。实施者已确认 MSB3491 长路径超限。 | 使用有确定映射的短内部名，保留逻辑主名/诊断/命令归属；相同 Profile 的完整 13 项局部 CTest 实测通过后才能关闭，不得用两例成功替代。 |
| C4 | P2 | measure.py:78 及 baseline 分支使用当前工作树 verify_children.py。改造后这个脚本已要求新夹具参数，不能再以旧 discovery 重现原版基线。 | baseline 显式读取已归档原版 snapshot，绑定 hash，不能当前改造版标 baseline；保留旧真实测量，不改写历史。 |

C1/C2 已直接发根任务和实施者；实施者确认并计划修复。C3 为已报告的真实失败，不能跳过；C4 的明确 snapshot 重现也已获实施者认领。需要复核修订代码和新增控制结果后追加结论，本报告不预签后续版本。

## 当前证据边界

compile-fixture-59c38a0cb48b/result.json 记录局部两例 success=true、61.6284854 秒；compile-ctest-b432d830ed36/result.json 记录完整局部 success=false、201.6071887 秒。compile-controls-8fe5a80cde6d/checks.json 记录正控制重复实编、故意坏正控、真实坏工具链、旧目录拒绝、失败 prepare 无 ready、真实旧对象不能绕过失败 setup 六项 true。本轮依要求仅读这些摘要，不把它们冒称新增独立执行或已独立逐流审计；13 项整组与修订后控制尚待完成。

不请求 D1.05 正式重跑。仅要求上述直接受影响工具反例及局部 CTest，符合 v2 最小影响集；P0 的测试注册新增项仍须由根任务保持独立 expected/discovered/executed 登记，不能修改旧正式通过记录。

## 本轮精确来源

- `tests/compile/contracts/fixture.py`：`243b292a785087afbd941bfe0a693a56c848d484dd07a00c9b4cd801eb32b96f`
- `tests/compile/contracts/verify_children.py`：`99efc3f582c9e3805444fb3676aa819dbcd1fc3e1dc8e9f8d56cbaf6520e58b5`
- `tests/compile/contracts/discover.py`：`e5b94f92063d84f59fd3388ff81e5d5aa8b5a39a0da14d78aef2fe0c5b1e2098`
- `tests/compile/contracts/CMakeLists.txt`：`ff5f540b5ffbab5eb7417f3a2eebd5e8b27be1d218067e16a3af314fc9328644`
- `tests/tools/compile/test_fixture.py`：`7395833f984b72beb4676fba7e3ed87b51e824caf5827e1f1c5a83423a25f604`
- `tests/tools/compile/measure.py`：`96ebca90ff36229d200cd6c527a2c443937ac5f19193e032b48f1f354191dff9`
- `tests/tools/compile/controls.py`：`8ae86e836f008b8fa2b8ecb662602d7b109be199fb2237966bb51cd6f827805d`

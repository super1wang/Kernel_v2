# D1.05 分配探针、包装器及独立示例审核（第一轮）

审核日期：2026-09-08。来源：独立 AI 规格/代码复核；本审核者未实施本报告范围内源码。审核者先前实施的 Policy 文件明确不在本次独立结论范围中。没有修改被审源码。

结论：**ChangesRequested**。以下两处测试断言缺口修复并取得新的实际证据后复核。已有 Debug/Release 分配结果及旧包装器证据有效，但不能据此声明当前三配置正式门禁或 D1.05 Passed。

## 一、先行规格复核

先阅读 `docs/plans/D1.05.md`、`docs/contracts/native-invocation-api.md` 第 7–9 节及 `docs/reviews/D1.05-transport-amendment.md`，再检查实现。

规格将固定成功场景限定为有限预热、小定长 Args/R、空资源声明、同步调用与有界观察环；计数必须包括每次完整治理、业务、返回值构造、结果验证、观察和清理。逐入口正控制与故意分配的 red 控制是必要的可证伪条件。Debug CRT 与 C++ new 通道分别报告，Release CRT/DLL/自定义堆盲区必须明示。首用/绑定/可变结果/有限预热/驻留峰值/最后释放应独立记账，不推导 D1.06 正式占用预算。

transport 增量对锁定 expected 的内部 noexcept 运输限制明确收窄 Native 可执行类型；noexcept 违约子进程退出 86 只证明终止边界，不是 Invocation 成功恢复。此规格没有以吞掉异常代替治理，也没有扩展产品模块；就本审核范围，规格可实施且未发现冲突。

## 二、必须修复

### A1（P2）：calloc/realloc 合并正探针掩盖单入口漏计

被审原版 `allocation_cases.hpp` 的 `allocation_probe` 把 calloc 和 realloc 放在一个窗口，最终只检查 `c.crt > 0`。只计到其中一个入口仍然通过，与规格要求的 CRT 各入口独立正控制不符。原 Debug 实测合并样本 crt=2 不能弥补断言自身缺口。

要求：calloc 独立窗口；realloc 的前置块在窗口外取得，窗口内只有 realloc 分配操作，分别记录并检查命中；Release 继续记 CRT=null。本轮审核过程中已观察到主任务实现该拆分，静态符合要求，但本第一轮报告不把尚未复核的新运行标作通过。

### A2（P2）：可变结果样本将进入后失败也标作 success

被审原版 `allocation_other_costs` 的 variable_result 仅用 `holds_alternative<Completed<Own>>` 判成功。Completed 同时包含 FailedBeforeApply，因而结果构造后出现失败时仍可能标 `success=true`，不能支撑 transport 增量要求的 owning-string 合法成功控制。

要求：检查 `Completed<Own>` 中的 `ReadCompleted<Own>`、内部 Result 成功及返回 text 与 input.text 完全相等。首个 first_invoke 样本同样应从实际返回值计算 success，不能在未检查返回内容时使用默认 true。重新运行对应 Debug/Release 成本控制并核对报告。

## 三、代码审查已确认的边界

- `allocation_probe.cpp` 的记账状态为 constinit 原子计数与线程局部窗口；不为计账分配容器。new/new[]、aligned、nothrow 及配对 delete 进入相同分配/释放函数。普通块与 aligned 块分别以 `_msize` / `_aligned_msize` 取实际 CRT usable bytes，释放保持配对；下溢 abort，未发现本固定消费者中的重复释放或内联记账分配。
- Debug `_CrtSetAllocHook` 单独计 ALLOC/REALLOC；Release 报告 null。报表明列 DLL 私有分配器、custom heaps、Release malloc 与后台活动盲区，没有把两个通道相加、也没有声称进程全覆盖。ASan 下实际 hook/CRT 行为仍须本配置运行验证。
- steady 用 4 次有限预热，随后 40 次不同输入逐次 start/invoke/销毁 reply/stop。input/output/业务计数及容量 2 观察环 dropped=42 控制仍在执行；没有把 Policy/TypeContract/thunk/Outcome 清理移出计数窗。
- governance 在同一真实绑定上预热后注入非法输入、撤权、生命周期变化，以 Rejected 且业务零进入判定；不把治理拒绝认成零成本成功。
- 成本报告区分 Engine 外壳与 Bound 持有内部状态。释放外部包装后仍能调用，最后 Bound 释放 Catalog 与状态，弱 owner 控制块独立释放；另一变量结果 Env 尚存，因此没有错误要求全进程驻留归零。
- discover 从实际 runner 的 --list 生成注册，8 个固定包装选择不读取 expected 制造通过；原始 discovery 流保存。包装器每个子进程通过 execute 创建 Windows Job，核对 Exited/精确退出码/assigned_before_resume/active_after=0，并保存原始流 SHA 与大小。
- private_dispatch/no_self_wait 的合法控制真实编译，负例同时限定错误诊断码和自身源文件，防止缺 include/无编译器误当负例。transport 增量包装明确要求退出86及 stderr 标记；这一新增调用仍待本轮根工程重跑。
- component_boundary 实际安装后检查内部头/库未导出，CoreContracts 消费可用而 Runtime 请求得到指定失败；元数据检查 Invocation→Registry/Policy/CoreContracts 及其下游边。
- 示例不包含 tests 路径；自己组装真实注册、认证、目标、Read/Compute 及不可用 StateEdit。进程对 7、12、非法输入原因/业务零进入及 ProviderUnavailable 作断言后才打印四项 true，失败退出1。仅内部验证目标，无安装规则。

## 四、独立核验的实际证据

`pipeline-a5d3e6c67ff6`（Debug）与 `pipeline-d45d2ff7b11b`（Release）各 7 条命令（configure/build/list/四项分配）均退出0、owned Job 清空。两轮范围内源码散列与下表被审原版完全一致。独立解析每轮 12/40/3/12 个样本，共 134 个，逐项确认字节守恒、块数守恒、peak 下界不低于前后驻留且上界不超过 before+allocated。Debug hook 可用、iterator level=2；Release CRT=null、iterator level=0。稳态完整窗口均为零新增分配，未据此扩大覆盖范围。

旧根工程 `integration-9beaf7aaa98d`：CTest 31/32；invalid_output 超时600秒保留。8 份包装 structure 已生成，共26条子命令的原始流 size/SHA 与 Windows Job 字段均独立核对一致：示例2条、分配探针2条（故意分配退出1）、组件边界4条、私有桥8条、禁止自等待7条、其余三项分配各1条。此轮私有桥未运行新增 transport 子进程，不能代替修订后的包装证据。不是全矩阵通过结论。

ASan：本次只核对 `cmake/Dependencies.cmake` 受控启用 /fsanitize=address、关闭冲突 RTC 与增量链接的构建路径；尚未收到当前分配源码对应的 ASan 原始运行结果，因此 **NotVerified**。

## 五、原始被审 SHA-256

| 路径 | SHA-256 |
|---|---|
| docs/plans/D1.05.md | 5437add4bef4cde29ba1d0123e3309361c4260c5125cb025c1a94aeb133ae0ec |
| docs/contracts/native-invocation-api.md | 31757ff610fb162e639c714f85de9655706b7432632dc5599772cc7d0566d796 |
| docs/reviews/D1.05-transport-amendment.md | e3c6eb64f71d4fbef5aee5e58e4b57ad26993ca205fa622777e0c99094e42fab |
| tests/contract/native/allocation_probe.hpp | 89602e0718aba305bf1a346b1618e854dfd5432266b6f2f9ccb0f5fbd408b683 |
| tests/contract/native/allocation_probe.cpp | 613a9d699e9737b7f01d022194961cfc0b68e00803191afb11a24eb37e25f938 |
| tests/contract/native/allocation_cases.hpp | fa527a70ca67875f6f4d7b3423ffd222d9a27b47164fbd8c7505bf39f0ec5572 |
| tests/contract/native/verify_children.py | e42f6773dd985f815da9506a9a470ca11699a3b808c214cb1f64c0c17151c6e6 |
| tests/contract/native/discover.py | 2429d3956f4a4a4d32c5bd34d2eb2c5cd02de5c12d1de233a2dde0f27406122d |
| tests/contract/native/CMakeLists.txt | fcdb5f199d8ad9756d431100e6d2bb13bdb7a381829bf0c7c96fe5b91f9659bc |
| examples/native_service/CMakeLists.txt | a4bd1a3e8c49ac1faedcc56a690aa59ffff1b5819e2781bef3c35a20c3577851 |
| examples/native_service/main.cpp | 403f7fce8eb82a7d0d40fa70cac1a3890010ef7f0dbe68eef6cff41291227997 |
| examples/native_service/README.md | 094ab93f2d15f37eae1fc9c4a79bf3a25192201ce78ad61a6b03bef9feccff34 |
| examples/native_service/value.hpp | 57b6e450ed5f28aa30f29bf1965055480e3358ae33c6d325def25c4727b157f4 |

`allocation-retained-review.md` 只作为定位资料读取，未把实施者自审作为独立结论。后续修复与新证据应另加复核记录，不改写本轮发现与旧运行结果。

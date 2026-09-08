# D1.06 allocation 开发收口静态自审

用户已要求只收口 D1.06 开发、停止后续测试。收到指令时本代理全部 owned 任务均已返回，当前活动 owned 进程为 0；此后仅整理源码和已有材料，没有启动测试、构建、消费者、pilot 或预算验证。本页为实施者静态自审，不是独立 CODE Approved，不声明 D1.06/G1 已验收。

## 文件与来源

本增量自有九文件：consumer 下 allocation_trace.hpp、CMakeLists.txt、pair_main.cpp、native_fixture.hpp、baseline_fixture.hpp；tools/footprint 下 allocation_record.py、allocation_develop.py、pair_record.py；tests/tools/footprint/test_allocation_record.py。源码摘要及完整列表见 `footprint-allocation-development-self-review.json`，索引 SHA256 `d113acb65efcc1ee8edf79faab8ae25f3b1ef93765052b037cd6bf7db70e7406`。

当前九文件集合摘要 `0e5455c9643fc70c572340d781e840ad6cb5fdba1b0cb679632fe40bbc817e53`；主 Debug 运行 `footprint-allocation-883396c98018` 的完整 34 输入摘要 `eb2cbfe74597d2efb6c11432d41fe052e0e4c467d68bda787c3a7054ccc0dc23`。算法均为排序 path/TAB/SHA/LF。当前自有源相对主运行有三处后续差异，索引分别记录：报告窗口文字按 baseline/Native 澄清，以及读数账本校验器和对应反例。

主集成同期更新的 windows_process/analyze/thread_origin 属其他明确分工；本代理没有覆盖这些修改，也不将当前全仓源谎称为主运行快照。主运行完整源、标签单独快照和账本验证源均单独保存。原生产 Runtime、原 D1.05 allocation_probe.hpp/.cpp 没有由本增量修改。

## 已实现内容

同一个主文件通过有限 occupancy/allocation 模式构建；occupancy 不编译/链接替换 new 的探针，counter_mode=disabled。allocation 的两个消费者直接编译原 D1.05 探针，所有新增测量代码只在消费者内。固定编译期注入目标也仅属于测量消费者，生产 API 没有测试开关。

完整 Scenario.call 外层开启计数，其内部的 HostBound 调用、治理、既有事实环、业务值检查和整个 InvokeReply 析构都结束后才 stop。固定样本数组上限 96，实际每份报告 66 行；四次 warmup、四十次 invoke 均有独立索引。构造/注册/start、日志证明/open/verify/bind、首次 Compute/Read、非法输入、shutdown、Bound/Session/最后 owner 释放分别保留成本，不能把这些成本混入零新增分配断言。

本地 Window RAII 在异常时也关闭计数；格式化及 stdout 输出都在计数窗外。正常和注入路径都必须完成相同业务及释放阶段，不以非零退出码本身证明 red。期望索引集合、业务 success、通道适用性、逐窗零及 verified 均由读数校验器检查。

账本字段必须为非负整数；每行检查 live_after=live_before+allocated−released、peak 至少覆盖前后 live，new/free 次数与字节不能相互矛盾。八个替换 new 正探针要求独立 new/free 各一、实际 usable bytes 至少为请求大小、分配释放相等及前后 live 相等；四个 CRT 原生入口不得伪计 C++ free。这里是固定单线程调用窗口的完整性条件：全局 live 与当前线程增减若受并发影响而不一致，窗口失效，不解释成业务泄漏或通用并发恒等式。

报告保留 Debug CRT、Release null、ASan 独立字段的表达，不把通道相加。两种原始窗口的说明已明确 Native 是 HostBound，baseline 只是本地计算。留出主集成要求的编译期 thread_origin include/调用位置（四 warmup 后、阶段 3 前）；本轮测量均未启用诊断宏。

## 指令到达前已有事实

`footprint-allocation-record-red-b48e838631e3` 保存新读数模块缺失实现 red；它不是 Native 业务反例。随后主 Debug 运行使用已核对并复制的 `host-install-5fb4d2956f` SDK，三项记录护栏、配置与构建成功，零编译警告/错误。

真实注入 Native 在 invoke[0] 内执行一次 new/delete，并完整完成业务/释放协议：该窗 success=true、cpp=1、crt=1、分配/释放 37 bytes、前后 live 相等，66 行报告 verified=false，实际退出 1。其他 39 个稳态窗零。无注入的 baseline/Native 均退出 0，各四十个稳态窗 cpp/crt 为 0；四 warmup 和首次两个调用的实际读数也为 0，但没有把它们改成额外稳态承诺。所有 ASan 字段为 null。

| Native 成本窗 | C++ new | C++ free | live before → after |
|---|---:|---:|---:|
| 构造/注册/start | 642 | 398 | 0 → 87,988 |
| 日志/open/verify/bind | 221 | 120 | 87,988 → 131,945 |
| shutdown | 0 | 0 | 131,945 → 131,945 |
| Bound 释放 | 0 | 40 | 131,945 → 128,455 |
| Session 释放 | 0 | 61 | 128,455 → 87,988 |
| 最后 owner 释放 | 0 | 244 | 87,988 → 0 |

这些是替换 new/delete 的 usable bytes 账本，不是进程 PrivateUsage；最后账本归零不代表整个进程内存归零。相同源码无计数 A/B 两目标也完成原能力/阶段断言，且未链接探针。

后续报告标签修订只构建运行 baseline，证据 `footprint-allocation-label-693a48ed3518`，仍为 66 行合格读数；没有重复 Native/注入。主集成指出账本字段缺失可绕过校验后，`footprint-allocation-ledger-red-c85b5b160313` 保存真实反例，修复后的 `footprint-allocation-ledger-green-59066a0bcea9` 五项最小单测通过，并只读重验四份既有报告（包含注入与标签修订）；没有新增 Native 运行。上述任务在用户停止测试指令到达前均已结束。

## 未完成事项与风险

Release/ASan 没有实际执行，不能声明对应配置合格。ASan 87 注册失败独立入口尚未接入此新消费者驱动；已有原探针接口仍保留，不能以旧 D1.05 控制替代本模式未来适用验证。正式配置接线、风险专项、独立 allocation CODE、pilot、预算及预算后矩阵未完成，按用户指令不再启动。

线程来源、缺样、短命线程覆盖和正式时延不由这份分配结果解决。当前仅开发源码收口，最终开发提交由主集成处理，提交后暂停，不推进后续工作包。

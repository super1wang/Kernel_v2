# D1.05 分配通道覆盖增量规格

状态：增量设计已由独立 AI 复核 Approved；实施结果另行复核。本文补充实际锁定工具链下的测量边界，不改写已经冻结的 API、实施计划或 expected 主项。

## 已确认根因

原 ASan 集成 `integration-asan-40c7511005de` 的 `T23.native.allocation_probe` 实际失败于 `c.crt>0`，原始失败保留。独立对照诊断 `asan-crt-diagnosis-581ee85e3e8f` 使用相同的本地替换 new/delete 与 Debug CRT，仅改变 ASan 编译和诊断 hook：普通 Debug 的 12 个独立入口各观察到 CRT 分配事件 1 次；ASan 的 12 个入口全部观察到原 CRT hook 事件 0 次，实际分配均成功。最早失败入口是普通标量 `new`，不是拆分后的 calloc/realloc 特有问题。

诊断中独立调用 sanitizer 的 hook 注册接口返回 1。12 个入口各观察到 sanitizer 分配事件 1 次；realloc 窗口观察到释放事件 2 次，其他入口各 1 次。该结果说明本锁定 ASan 运行时拦截后的分配没有经过 `_CrtSetAllocHook` 的可观测路径，不能把原 CRT 通道的零次数解释为没有分配。微软文档说明 ASan 运行时会拦截 CRT 分配函数；本地锁定头文件 `sanitizer/allocator_interface.h` 提供可链式注册的分配/释放 hook，并规定在启动主线程、其他线程启动前注册。[ASan 运行时](https://learn.microsoft.com/en-us/cpp/sanitizers/asan-runtime?view=msvc-170)

## 独立通道与报告

三个通道的事件不相加：

| 通道 | 普通 Debug | Release | ASan Debug |
|---|---|---|---|
| 本可执行程序替换 C++ new/delete | 实测 | 实测 | 实测 |
| 原 Debug CRT hook | 12 入口可观测 | 不可用，null | 保留原始实测次数 0，明确不可观察被 ASan 拦截的入口 |
| sanitizer 分配/释放 hook | 不可用，null | 不可用，null | 独立实测，12 入口正探针校准 |

`coverage.debug_crt` 继续表示原 Debug CRT hook 是否安装，不能单独表示有效覆盖；新增 `debug_crt_effective`，取值为 `full`、`unobserved_asan_intercepted` 或 `unavailable`。其中 `full` 仅表示 12 个声明入口的正控制全部可观察，仍有下文列出的盲区，不表示整个进程覆盖。新增 `coverage.asan_allocator_hooks` 表示 sanitizer hook 是否实际安装成功。每条 sample 保留 `cpp`、`crt` 及已有 C++ 驻留/峰值字节字段，新增独立的 `asan_allocations`、`asan_frees`；无该通道时使用 null，不使用零冒充可观察结果。正探针报告列出实际 `crt_missing_positive_entries`，其他未执行入口校准的报告为 null，保留原通道缺失证据。

ASan 通道只提供已注册之后、当前线程测量窗口内的 hook 次数，不推断启动前、未覆盖 DLL/自定义堆或后台线程成本。已有 C++ 驻留字节仍只对应本可执行程序替换 new/delete 的 CRT 可用块大小，不扩展为 ASan 整堆驻留、隔离区大小或进程总内存。

## 初始化与门禁

仅测试消费者的 allocation 编译单元通过编译器 `__SANITIZE_ADDRESS__` 选择 sanitizer hook；不修改生产 ABI、内核分配器或进程的 ASan 检测策略。注册在主线程测量开始之前发生，初始化幂等。正控制连续调用 initialize 两次，随后单次 new 的 sanitizer 分配计数必须恰好为 1，以检测重复注册。`__sanitizer_install_malloc_and_free_hooks` 返回 0 时，输出固定 marker `native_asan_hook_registration_failed`，fflush 后调用 `std::_Exit(87)`，不进入计数窗口或业务。回调只更新线程局部计数，不执行分配、格式化或日志。

ASan 的 allocation_probe 包装器另启动注册失败子进程：在初始化前有限尝试注册空 hook 对，实际耗尽锁定头规定的最多 5 对槽（最多尝试 8 次并必须观察到返回 0），随后让正常 initialize 真实注册失败。仅 exit 87 与上述 marker 同时成立才通过该负控制。无法耗尽或意外继续执行都以其他非零状态拒绝，不能伪造成功。非 ASan 配置不运行该模式、不声称该通道覆盖。

12 个正探针保持互相独立，包含 8 种 C++ new 形式及 malloc、calloc、realloc、aligned_malloc。realloc 前置 malloc 始终放在窗口外。普通 Debug 每项都必须命中 CRT 通道；ASan 每项都必须独立命中 sanitizer 分配通道，并保存原 CRT 的零观察事实。锁定 ASan 诊断下如出现原 CRT 正观察，必须报告覆盖行为改变并重新复核，不能静默改写既有界限。

无分配负控制与 40 次完整稳态 invoke 窗口分别要求 C++、可读原 CRT、可用 sanitizer 分配计数为零；不通过通道相加或扣除来得到零。sanitizer 释放数单独报告，realloc 不假设分配/释放次数一比一。注入单次 new 的 red 进程必须观察到 C++ 分配、ASan 配置下还必须观察到 sanitizer 分配，再以 `verified=false` 和非零退出码证明错误预算会被拒绝。

创建、绑定、首次调用、有限预热、错误详情、可变结果、释放外部外壳、最终 Bound/内部状态/观察表回收及弱引用控制块回收各窗口均报告上述独立通道，不把首用或回收成本混入稳态保证。正式包装器需校验新增字段的可用性、null 边界及注入 red 结果。

## 验证与历史

实施前保留原 ASan 失败和 12 入口对照诊断；实施后分别运行普通 Debug、Release、ASan 的正负探针、全部四项 allocation 主体和包装器，并保存独立源码快照、命令、原始输出及通道结果。历史 Debug/Release 已通过结果不改写，增量结果不代替包级自动验收。

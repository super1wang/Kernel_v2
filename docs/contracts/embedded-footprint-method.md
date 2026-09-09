# 完整 Embedded 占用与有限预算口径

本合同服务 D3.07/G3-C，依据架构 A21.4–A21.6 与 B5 规划。预算审批记录在 `docs/reviews/B5-embedded-budget-decision.md`；批准不等于运行通过或 B5/G3 Passed。NativeSubset 的历史预算和结论保持原样。

## 固定装配与执行

使用 `OCK_BUILD_COMPONENTS=Embedded`、`OCK_DEPENDENCY_COMPONENTS=Foundation;CpuPool` 的真实生产库与安装 SDK。取得依赖仅 expected/thread_pool，导出 Foundation/CoreContracts/Runtime/CpuPool。实际 fixture 为 `examples/embedded_service/fixture.hpp`：cpu_workers=2、Executor max_inflight=8、active=8、records=32、terminal_records=16；输入/结果/终态各 65536 字节、waiters=8、notice_entries=32、observation_leases=8、children_per_execution=2、max_depth=4。默认内存日志 record_capacity=128，不新增文件日志线程。权限、参数检查、资源、取消、拥有输入/结果与内存观测保持开启。

同一 typed 实现预热 4 次，随后 40 次短 Invoke 与 40 次 owning Submit/等待/取结果；另运行共享独占资源的父子等待、取消传播、双方结果及最终排空。阶段保留 Host 前、Ready、预热、空闲、shutdown、Bound、Session 和 owner 释放。每项固定 6 组 ABBA，每组 baseline/Embedded/Embedded/baseline，不按结果增加重复次数、删除离群值或混用来源。

Release 为无分配计数的性能配置，同一二进制测占用和无握手启动。Debug 与 ASan/RelWithDebInfo 为分配诊断配置，其内存只解释为诊断构建占用，不代表 Release 支持值。均为 MSVC 14.44.35207、Windows SDK 10.0.26100.0、x64、动态 CRT（Debug /MDd，其余 /MD）、LTO 关闭；实际编译 tlog、CMake cache、依赖清单、安装库存、EXE/模块摘要和允许公开的本机环境由报告保存。

## 占用、时延与回收

占用复用已验证的 Windows 5 ms 到期采样，记录漏采周期；每个阶段的 PrivateUsage 与 WorkingSetSize 保留 min/median/p95/max。Ready 稳态取预热后 IdleSamplingBegin 的固定 1 s 窗口最大值；启动峰值取 HostReady 时 OS 的 PeakPagefileUsage/PeakWorkingSetSize；全生命周期采样峰值另列。ABBA 以 B1−A1、B2−A2 计算每组增量，不先合并不同运行的峰值。shutdown/各层 owner 释放保留原始曲线；CRT 缓存无需归零，但所有 fixture owner 必须实际释放。

分发增量为同配置 Embedded EXE−baseline EXE，加成对运行中 Embedded 必需新增分发模块的字节。SystemRoot 内的 OS 自带模块不算新分发；MSVCP/VCRUNTIME/CONCRT/Clang sanitizer 运行库即使位于 SystemRoot 仍算分发候选，全部实际模块包括 OS 模块都保留摘要。PDB、静态库及构建中间文件只作为诊断库存，不累加到运行分发量。

启动模式在子进程真实 Host Ready 处保存 QPC，使用启动器紧邻 CreateProcess 前的同一 QPC 频率计算外部创建到 Ready；内部构造到 Ready 使用消费者的直接窗口。Ready 前不等待采样 ACK，不执行分配探针；外部窗口包含纳入独占 Job 和恢复线程的固定安全成本。首轮和随后重复分列，OS 文件缓存不强制清空，不声称可复现断电冷启动或硬实时时限。正式预算针对固定受控环境中的观测最大值和成对最大增量。

## 线程瞬时上界及实际生命周期

线程采样为每阶段真实 OS ID，报告同时给出总线程采样峰值、Host 前集合、Ready 新增集合和 shutdown 后集合，明确不是连续 ETW 跟踪。内核瞬时上界另由以下生产结构证明：

1. `packages/runtime/host/host.cpp` 的 start 在锁内只准许一次 Configuring→Validating，并只调用一次 execution_factory；重复 start 不创建第二后端。
2. fixture 工厂仅创建一个 `{2,8}` CpuPool。`packages/adapters/cpu_pool/executor.cpp` 的 State 构造只创建一次 `BS::wdc_thread_pool(options.workers)`，公开 Executor 没有 reset/resize；锁定 thread_pool 依赖的 `create_threads` 对正数 2 原样返回，并只执行两次线程构造循环。
3. `packages/runtime/executions/execution_service.hpp` 的 create 只创建一个控制线程。资源/期限/观察推进共用该线程；Runtime/CpuPool 生产源没有其它线程创建点，默认 MemoryLogging 同步存储。
4. ExecutionService 的 shutdown_until 在退出条件后实际 join；CpuPool shutdown 在 drain 后销毁唯一池并 join。fixture 成功后同时要求 Host shutdown、owner 释放及双方终态。正式采样要求 Ready 恰好新增 3 个 ID，且这些 ID 在 ShutdownComplete 及所有后续阶段消失；阶段采样增量不得超过 3。

因此标准 fixture 的内核瞬时线程峰值上界为 3（两个 worker 加一个控制线程），与阶段观测相互核对；不是用低频采样推断“没有漏掉瞬时线程”。Debug/ASan 分配诊断的额外正控制线程在 HostConstructionBegin 前已 join，单列为测量工具成本；它不与内核三线程重叠。线程句柄的实际 join 反例由 B5 已实现的 shutdown 直接集承担，最终 B5/G3 的 S_required 仍须覆盖这些用例。

## 受限分配和范围

Debug 的全线程 Debug CRT hook、ASan 的全线程 allocator hook 与调用线程 C++ 入口计数一起验证 40 个完整成功 Invoke 窗口的新增分配精确为 0。保留 13 类分配探针、后台 malloc/free 正控制与负控制；Submit 和父子取消窗口单列，不承诺零分配。覆盖可执行程序的 C++ 分配入口及明确接入的各线程 allocator；未接入的私有 DLL/custom heap 不在该零分配声明内。结果报告不得将计数构建的启动时延提升为 Release 性能。

文件日志/完整 Trace 是额外观测模式，不混入本最小完整内存观测的三线程配置。当前完整 Embedded 正式对象的默认日志即内存模式；已有日志故障组合、文件适配能力与 AutomationHost 的观察开关/慢消费者成本分别按其合同与 G3 证据审查，不用本报告代替。正式 G3 总审查仍须逐项判断 A21 的其它明确适用模式和成本交付是否齐备。

# B5 完整 Embedded 有限预算决策

状态：**Approved（AI 预算审查）**。批准时间：`2026-09-09T22:50:59.393272+00:00`。actor_type=`AI`，依据自动验收政策 `712ad1e661300f538cacac5af1d5111fdf6418624369f02a900e992610786d80`，不包含 human Approved，不代表 B5/G3 Passed。

本次独立审查 [固定测量口径](../contracts/embedded-footprint-method.md) 与 `tools/footprint/embedded_formal.py`：Release 无计数占用/启动分开，Debug/ASan 诊断分开；固定 6 组 ABBA，预热/40 整窗 Invoke/40 owning Submit/父子资源取消/排空不变；实际源、依赖、安装、编译参数、模块、原始命令和预算在正式采样之前绑定。默认内存日志 128，不关闭授权或其它治理。时钟字段的跨真实进程验证已通过；新汇总的真实样本缺失/重复/非零分配/错通道/后台探针失败及非有限预算输入拒绝检查 3/3，原失败保留。

## 预算依据与固定值

将既有 NativeSubset 阶段的工程资源上限原值用于完整 Embedded 的独立阶段约束，不因增加异步/资源/取消实现而放宽上限。新增 Ready Working Set 项沿用同配置 Working Set 上限。此选择是完整装配仍须保持轻量的工程准入要求，校准仅证明该要求具有现实可达性；没有以本次或未来正式实测最大值自动加余量。NativeSubset 的字段和值全部保留，数值相同不构成两个测量对象相同的声明。

| 配置 | Private 绝对/成对增量 | Working Set 绝对/成对增量 | 分发增量 |
|---|---:|---:|---:|
| Release 无计数 | 16 / 4 MiB | 64 / 8 MiB | 4 MiB |
| Debug 分配诊断 | 64 / 16 MiB | 128 / 32 MiB | 16 MiB |
| ASan 分配诊断 | 512 / 128 MiB | 512 / 128 MiB | 32 MiB |

同一类上限分别用于全过程采样峰值、Ready 稳态和 Ready 处 OS 启动峰值，各自原值和成对增量均须通过，不相互替代。Release 内部构造到 Ready 为绝对最大 250 ms、成对增量最大 100 ms；创建到 Ready 为绝对最大 1000 ms、成对增量最大 250 ms，同样沿用阶段工程时限，而非实时保证。所有上限保持固定，正式失败应保留并归因，不能自动提高预算。

结构性硬约束为固定 2 workers、内核瞬时新增线程上界 3、shutdown 后这 3 个线程实际退出；受限成功 Invoke 的 40 个整窗 C++/命名进程 allocator 分配精确为 0。零值硬约束由测量口径单独执行，不塞入只允许正数的通用容量上限字段。分配诊断工具的后台正控制在 Host 前已 join，不冒充生产线程。

## 方法与校准绑定

- `win-msvc-debug`：`0c91c7cd024b5d90e0a9836c078b65ed693b4db5e4a121c01ad9f76845afd7c3`。
- `win-msvc-release`：`5f942d300bb4e7ff9d5dbbe55a553b71079e9518b131a4fa87dc1b57e09e5b92`。
- `win-msvc-asan`：`7bc31641878e5ab71eeb279735f429ea4d4a653b9669d350ecb25ad9d74a6c8c`。

方法摘要包含当前生产/测量源码、固定 fixture、公开 SDK、依赖锁、编译/Profile 参数和本机环境。实际二进制、编译 tlog 与安装库存由后续各报告另行绑定；方法或边界变化必须重新审查，不借用本批准。

校准来源（均不是未来正式 run）：

- `evidence/B5/embedded-pilot-1df84b5c12/1-embedded-stdout.log`，SHA-256 `ec81324afccbf512504bdde7447ab39e339c43360ec5a2717958167d5d0b7faa`。
- `evidence/B5/embedded-pilot-d8d809d523/1-embedded-stdout.log`，SHA-256 `2ab547821273f5ccae800adfceee6e5577558d71659eae32ff9f9d909648f93a`。
- `evidence/B5/embedded-pilot-e2cb99d25c/pilot.json`，SHA-256 `a8fcbc90257b960b30a0f6e6386dd08d0afb5f9e65e01513378f9c9392521c64`。
- `evidence/B5/embedded-startup-f771053399/samples/summary.json`，SHA-256 `621786b67f6f77611d1aa3b185ac021737b23a4fa1e4b568cd3db986bb734722`。

本决定只批准 Embedded 最低完整内存观测及诊断/时延预算。AutomationHost 订阅/慢消费者、文件日志/完整 Trace 的适用范围与成本，以及全部 B5 语义/SPEC/CODE 和 G3-A/B/C 父包条件，仍由最终 G3 审查逐项判定，不由此审批豁免。

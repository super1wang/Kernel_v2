# D1.06 v2 有限采样实现自审

本页为实施者自审，不替代独立 CODE 审核。依据已获 AI SPEC Approved 的采样增量及同步后的 `ock.native-footprint/2` 合同；旧三份自审、v1 工件、失败记录不改写。本轮不运行 pilot 或预算矩阵。

## 来源与实现

当前 26 输入集合摘要为 `8778564bab2711ad492c17dcd55ba269bcfe338f5e70aff2a6b0e5d0f527976e`（排序 path/TAB/SHA/LF）。逐文件、规范和原始命令/结果摘要见 `footprint-v2-sampling-self-review.json`，索引 SHA256 为 `1b563fb0166f7fca7dfcf957e2756ae9729098d8680c92264e526006d9a6869c`。该集合与本轮新单对来源逐字相同；held 控制所包含的源码也与当前一致。

仅改 windows_process.py 的有限采样和方法身份、analyze.py 的 v2 元数据校验及空线程点处理；新增 `test_sampling_v2.py` 和 `sampling_develop.py`。process.py、原 run.py、生产库、Native/baseline 消费者及原共享通道均未改。消费源与 v1 首对逐字相同，本轮 SDK 仍来自同一 `host-install-5fb4d2956f` 安装清单。

身份现在包含方法 /2、memory_sampling=due_5ms、thread_sampling=phase_boundaries 及全部原有限配置。周期点只查询内存，thread_ids/thread_query 均为 null；assigned_suspended 和十一固定阶段分别查询真实线程集合。内存、线程查询各自保存起止 ticks、成功位、错误码与结构长度；失败保留已开始的查询时刻、null 指标和原因，不能补零或复制旧集合。

所有查询仍在原 owner 循环，未新增采样线程、外部 PID、回调或计时器设置。Ready 查询仍在 ACK 前，两个模块枚举点、固定停留、原绝对期限及 root 退出后的禁止查询均保留。分析器校验新身份、两种查询事实、周期线程 null、恰好十二个边界；线程峰值只计算实际边界集合并附 thread_peak_scope。

## 实际反例与有限验证

- `footprint-v2-red-58b142149ece`：新 v2 回归在旧实现上实际退出 1，保存旧 windows_process 源和原始输出；覆盖周期线程调用未移除、独立查询时刻/错误事实缺失及方法身份未变化。
- `footprint-v2-held-d3d9e5cba335`：18 项局部护栏实际通过；随后只运行旧校准消费者的 none/thread 两个控制。EXE/DLL SHA 和三个消费者源逐字核对后复用，未重编或重跑完整校准。
- 同 thread 进程在 HostConstructionBegin 为四个实际 ID，Ready 为原四个加 37772，ShutdownComplete 恢复原四个；证明该线程保持到边界且 join 后消失。独立 none 基线也为四线程，来源归因仍为 Unresolved，未伪填“仅主线程”。
- `footprint-native-pair-2fe91f109e7e`：同消费者的新 Debug 单对，记录校验器、独立配置/编译、两次导入和 A/B 运行全部 owned Exited 0、active_after 0、未终止 Job。真实 Native 能力及四 warmup/四十调用/释放断言继续成立，counter_mode 仍 disabled。

| v2 单对原始指标 | baseline | Native |
|---|---:|---:|
| 内存样本数 | 304 | 305 |
| 线程边界样本数 | 12 | 12 |
| missed intervals | 742 | 650 |
| Ready 实际线程数 | 4 | 4 |
| 边界线程观测峰值 | 4 | 4 |
| 最大内存查询间隔 ticks | 111,054 | 26,789 |
| 最大线程查询间隔 ticks | 2,392,576 | 1,577,861 |

本机 QPC frequency 为 10,000,000。表中的查询间隔包含实际调用包围范围内的调度时间，不是纯 API CPU 耗时，不用于自动调高任何预算。A/B 同一方法摘要为 `63fc080f01f6066fcceba092d844a9554cc6e40137710297883cf3079babb3a7`；v1 旧轮没有重命名为 v2。

## 边界与自审结论

必要路由、身份、字段和 held-thread 正控成立；仍有实际缺样，不能把“到期尝试”说成实时 5 ms 完整覆盖。边界线程快照会漏短命线程，四线程中未知来源没有被本轮消除；相同计数不是线程硬约束的全部证据。原始结果保留 sampling_coverage=NotEstablished、allocation_counting/formal_latency=NotMeasured、budget_status=NotApproved、pilot=false。

未改系统环境、计时器分辨率、线程调度或预算。接下来仍是 D1.06 的有限线程归因、真实分配/轻量模式、规定 pilot 与有来源预算工作；当前局部结果不构成 D1.06/G1 Passed，本包提交后暂停，不启动后续工作包。

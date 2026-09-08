# 诊断与 latency 身份开发静态复核

actor_type: AI。结论：StaticReviewed，当前限定范围未发现尚未处理的实质开发阻断；不是测试 Passed、正式 CODE 验收或 D1.06/G1 验收。遵照用户最新“只完成开发，测试不要做了”，本轮仅阅读源码、既有结果及文件 SHA，未运行测试、构建、消费者或诊断。

范围为 thread_origin.hpp、thread_record.py、diagnostic_develop.py，以及 windows_process.py/analyze.py 的 latency 身份增量。安装目标、消费入口与正常模式的编译开关只作必要关联阅读；不扩展审核 allocation 新实现。

静态判断：线程诊断使用本进程 PSS_CAPTURE_THREADS，固定 64 条上限并实际检查枚举终点；线程起点仅用于模块范围/RVA 计算，不解引用。Snapshot/marker 与 held worker 均有局部回收路径；正控线程的实际 TID 与主线程区分，正常诊断目标未创建该正控 worker。记录验证器拒绝错误返回、非法/重复 TID、域外地址和缺失释放回执。驱动使用原 execute、固定配置与安装清单，独立诊断产物不混入正式占用或时延统计。模块入口归属仍不等于线程创建因果。

初读 thread_origin.hpp（SHA c843473096866ccb31d03b1032e8bf8ea65d0ca4041a0217b84c5092cd6b5b04）发现诊断错误材料缺口：未解析起点丢失实际地址；null 起点读取陈旧 GetLastError；部分 Win32 失败和容量拒绝仅剩通用异常。主任务仅补固定错误字段后，本轮再次静态核对：null 单列，解析失败保留 start/tid/error，模块信息/路径错误保留 API 错误和长度，容量、PID、模块范围失败有明确事实。该问题静态闭合，最新修订未编译、未运行，不能继承旧诊断成功。

latency 的 memory_sampling=phase_boundaries、sample_interval_ms=null 已进入方法摘要；occupancy/allocation 仍为 due_5ms/5。分析器按实际 configuration 核对策略并拒绝 latency periodic 点；owner 既有调度不变。Release 无计数消费者和三个 Ready 间隔是独立诊断口径，不能从有限单对推正式分位或预算。

只读看到历史 f721715dc61e、c65c1c108ae0 的结果为 DevelopmentChecksPassed，且均 pilot=false、budget NotApproved、thread_causal_attribution NotEstablished；本轮未重新审计其全部运行材料，也未把旧冻结输入成功推广到当前头文件。未知线程、短命线程盲区、完整故障覆盖、计数/风险配置、pilot、有限预算与正式验收仍未完成；本轮不要求补跑。

## 当前五文件 SHA256

| 文件 | SHA256 |
| --- | --- |
| tools/footprint/consumer/thread_origin.hpp | 0b0ed7f5a48861b09b6a51dcfaf57d8c0c17d891b0f80ccb1af030e1051516e9 |
| tools/footprint/thread_record.py | e44eca12675e590fd185c9a6af30166276669ac1036ffb9dfb73793031bfb6c2 |
| tools/footprint/diagnostic_develop.py | 87b2bb17b5f306182d05a753faa81b93611db2a2eaafceca51ae92c715a9ab1f |
| tools/footprint/windows_process.py | 76ceb88e93360a233baa921220fffaf7cf3626963e2214560b8a5f1501654124 |
| tools/footprint/analyze.py | 2707dec624a56abca7e9b44614950a9703e256d985e47c999b7c5018a3b3e0f7 |

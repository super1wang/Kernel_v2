# B6 Embedded 受影响预算来源更新

日期：2026-09-12T15:52:22.965766Z。审核者：Codex AI，依据 `automatic-acceptance-policy.json`。这是正式运行前的预算决定，不表示测量、B6 或 G4 已 Passed。

B6 修改 CoreContracts、Runtime StateEdit 桥接、SDK 版本与构建投影。Embedded 不装入 State 或 immer，但 Runtime 二进制与公开头输入发生变化，因此旧 G3-C 运行只保留历史事实，本批对相同消费者和相同数值上限重新采样。

验证方法保持 6 组 ABBA、Release 占用和启动、Debug/ASan 分配诊断、固定三线程结构上界、40 次短 Invoke 零新增分配以及 40 次 owning Submit。四组 `limits` 逐值沿用 B5 已批准上限，不依据本次待运行结果调整；B5 校准样本仅用于说明阈值来源，不能替代本轮原始运行。

本机锁定 Python 3.11.9 下、包含当前全部方法输入的摘要为：

- win-msvc-debug：`ea57147b8b1e663f2d8b38274c8fda0bd3b8c1ae587a199ab827ea0a9c857499`
- win-msvc-release：`dd858fb7e9b4b6cf696185eb789125cbf9167f0aae4c389d3259fb2cadc214d9`
- win-msvc-asan：`c9e1be0868473ed18e7bc620caad2e2799b338daf24e0dfd919222ac54741068`

AI 结论：批准按上述新来源摘要与原数值上限运行。正式报告必须核对 Embedded 目标图仅有 Foundation/CoreContracts/Runtime/Adapter::CpuPool、锁定依赖、编译配置、安装清单、原始进程/模块/线程/分配样本、启动样本与结束时方法摘要；任一不符阻止 B6 收口。

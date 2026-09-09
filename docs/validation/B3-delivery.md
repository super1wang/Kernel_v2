# B3 / G2 交付与自动验收

日期：2026-09-09。B3（D2.05–D2.07）和 G2 已 Passed；仅覆盖内核及规划的无状态验证消费者。被测实现来源 `e77c328e75af19d27afa5a5b6e2720173d07e803`，统一输入摘要 `3b176665ad4d3b50ae431c793dcc0be4f1b91557edb9ba82ffe95c22019914f8`。B1/B2 历史 Passed 保留，未重跑其累计矩阵。

## 交付内容

- 真实 Named Pipe、显式 DACL、本机限定、OS SID/PID 认证、部分读写、分队列预算与慢连接隔离；Policy 授权首字节接入真实传输，Unknown 关闭和仲裁外排空。
- 薄 ControlClient/CLI、UTF-8 file/stdin 与 Windows Shell、VolatileHost 意图改参拒绝、能力探测、Ctrl+C 仅停止等待；CLI 退出不销毁 Host，默认不发送业务 cancel。
- 同一 HostBound 操作的 Native/JSON/CLI 正负结果一致；真实 cmd/Windows PowerShell 原始材料、单次延迟和分配计数完整归档。
- SDK `0.1.0-dev.4 / B3Subset`，新增静态组件及公开头可迁移安装；薄客户端和真实 CLI 链接映射排除服务端库，Runtime-only 裁剪与原生消费者保持可用。

现行接口与边界见 [B3 合同](../contracts/b3-ipc-client.md)。[B3 规划](../plans/B3.md)、ADR 和技术复核是正式执行前冻结材料；当前状态以本交付和 progress 为准。

## 同来源正式事实

| Profile | CTest | 证据 |
|---|---|---|
| win-msvc-debug | 28/28 | [原始报告](../../evidence/e77c328e75af-3b176665ad4d/win-msvc-debug/D2.07/20260909T023339Z-88b314c47cb5/report.json) |
| win-msvc-release | 26/26 | [原始报告](../../evidence/e77c328e75af-3b176665ad4d/win-msvc-release/D2.07/20260909T023531Z-8389316f3edc/report.json) |
| win-msvc-asan | 26/26 | [原始报告](../../evidence/e77c328e75af-3b176665ad4d/win-msvc-asan/D2.07/20260909T023155Z-347662961ca6/report.json) |

每个 Profile 只引用最终来源的一次运行，不混用旧来源结果。expected 在 discovery 之前按包卡/G2 冻结；Debug 多出的两项是架构负守卫和 Runtime-only 实际裁剪。原始子进程输出、安装映射、Shell 文件、完整成本 JSON 均随 E03 归档。3 份报告共享物理执行，分包技术结论在 [B3 SPEC/CODE](../reviews/B3-review.md) 中独立给出。

| 逻辑节点 | 结果 | 决策 |
|---|---|---|
| D2.05 | Passed | [自动验收](../../evidence/G2/b3-e77c328-d205-acceptance.json) |
| D2.06 | Passed | [自动验收](../../evidence/G2/b3-e77c328-d206-acceptance.json) |
| D2.07 | Passed | [自动验收](../../evidence/G2/b3-e77c328-d207-acceptance.json) |
| G2 | Passed | [自动验收](../../evidence/G2/b3-e77c328-g2-acceptance.json) |

按上表顺序放行，D2.05 消费已 Passed 的正式前置，后续节点仅在前一节点 Passed 后验收。四份决策错误及审核错误均为空。使用已授权的 AI 自我复核政策，不存在人工 Approved 声明。

## 首组成本样本

每配置 Native/Dynamic 各 20 次，以下为纳秒统计；分配字节是替换 new/delete 通道观测值，不是进程驻留内存。原始 120 个样本及正控制计数见 [成本 JSON](../../evidence/G2/b3-e77c328-costs.json)。ASan 使用 ASan 分配通道，Debug 非 ASan 使用 CRT 通道，Release 不宣称 CRT 通道可用。

| Profile | 入口 | 样本 | 最小/中位/最大 ns | 每次 C++ 分配次数 | 每次分配字节 |
|---|---|---|---|---|---|
| win-msvc-debug | Native | 20 | 15500 / 15700 / 17300 | 12 | 192 |
| win-msvc-debug | Dynamic | 20 | 107100 / 109450 / 118700 | 208 | 13952 |
| win-msvc-release | Native | 20 | 600 / 600 / 700 | 0 | 0 |
| win-msvc-release | Dynamic | 20 | 4600 / 4700 / 5900 | 63 | 2976 |
| win-msvc-asan | Native | 20 | 48000 / 48750 / 61200 | 12 | 192 |
| win-msvc-asan | Dynamic | 20 | 647400 / 682850 / 780500 | 208 | 13952 |

窗口为预热并绑定后的调用；Dynamic 包含结构解码与验证，不含 Host 初始化、文本 JSON 解析、IPC 或 CLI 启动。不据此宣称端到端吞吐量或硬实时性能。

## 保留的失败与范围边界

首次来源 fe0893e 的 Debug/Release 成功输出被 CTest 截断，未用于最终验收；f0cd96a 的 ASan 成本正控制条件错误，整组来源被替代。修复仅涉及 B3 测量消费者及其复核记录，没有修改既有分配探针，也没有覆盖原始失败。全部排除原因和报告摘要见 [交付索引](../../evidence/G2/b3-e77c328-delivery-index.json)。

当前真实执行 Provider 为 absent，CLI list/watch 明确拒绝；watch 脚本只证明客户端算法，真实任务整体观察留 D3.07。VolatileHost 文件不提供服务端去重或跨重启保证。同流已开始字节不可撤回。Task、State、Plan、Durable、GUI、CAD/CAM 和设备产品均不在本批放行范围。

实现提交依次为 fe0893e、f0cd96a、e77c328；交付归档另行提交到 `work/d0-kernel-baseline` 并推送该任务分支。用户已有 `docs/AGENTS.md` 删除保留，不纳入提交。B4 尚未开始。

# B5 / G3 交付（2026-09-10）

B5 的 D3.04–D3.07 已按正式依赖顺序提交 Passed 事实，G3 自动验收 Passed。范围仅为内核与规划验证消费者；B1/B2 等既有 Passed 保留。

## 最终来源与机器事实

- 被测实现：`dacc540c03744f68379214ffbb8b342f5ec60a34`。
- 618 个实际输入的 SHA-256 摘要：`9ca9dc06f5ab45410cd3f923aa93421d4a818a8506132c16717b10d9da44f882`，全部与该提交的 Git blob 逐字节一致。
- 三配置共享一次需求集，未拼接不同来源结果；126 项 expected 在运行前固定，另有一个显式 Schema check（内部 3 个直接测试）。

| Profile | CTest | Schema check | 证据 |
|---|---:|---|---|
| win-msvc-asan | 126/126 | Passed | [原始报告](../../evidence/dacc540c0374-9ca9dc06f5ab/win-msvc-asan/D3.07/20260910T000512Z-c82d2acf4391/report.json) |
| win-msvc-debug | 126/126 | Passed | [原始报告](../../evidence/dacc540c0374-9ca9dc06f5ab/win-msvc-debug/D3.07/20260909T235341Z-58c98623415d/report.json) |
| win-msvc-release | 126/126 | Passed | [原始报告](../../evidence/dacc540c0374-9ca9dc06f5ab/win-msvc-release/D3.07/20260909T235800Z-0da18bb74759/report.json) |

ASan 使用 RelWithDebInfo。验证包含真实安装/缺组件消费者、三种 Executor 的共同合同与违规后端、线程 join、资源争用/取消、拥有输入/结果、异步和必要回调、父子收尾、Host 超时保活、真实 Named Pipe/CLI 与 120 秒 TTL、慢流/中断/撤权/gap；不重跑整个历史累计矩阵。

## 独立包级验收

- [D3.04 包级决定](../../evidence/B5/final-dacc540/D3.04-acceptance.json)：Submit、拥有型输入与执行投影
- [D3.05 包级决定](../../evidence/B5/final-dacc540/D3.05-acceptance.json)：取消、期限和控制预算
- [D3.06 包级决定](../../evidence/B5/final-dacc540/D3.06-acceptance.json)：父子、Finalizing、回调与停止排空
- [D3.07 包级决定](../../evidence/B5/final-dacc540/D3.07-acceptance.json)：真实观察 CLI、完整 Embedded 与停止门禁

每个决定绑定本包的独立 AI SPEC/CODE、冻结需求映射、同一三配置机器报告和已提交正式前置。AI 技术复核与机器事实分离，没有填写 human Approved。共享物理执行的原始结果不复制成新运行。

- [共享审计](../../evidence/B5/final-dacc540/shared-audit.json)
- [SPEC/CODE 材料](../reviews/B5-review.md)
- [冻结需求映射](../../tests/manifests/b5-requirement-map.json)
- [G3 最终自动决定](../../evidence/G3/acceptance-dacc540.json)：errors 与 review_errors 均为空。

## G3 三项条件

- [G3-A](../../evidence/G3/A/b5-final.json)：真实执行、资源、取消、父子、Finalizing 和排空。
- [G3-B](../../evidence/G3/B/b5-final.json)：真实 list/subscribe/watch 及观察竞态；保留 AutomationHost 开/关/慢消费者和 cursor 有效/MAC 错误/超长的原始性能样本及其独立来源。
- [G3-C](../../evidence/G3/C/b5-final.json)：完整无 JSON/Asio 的 Embedded，固定两 workers 与单控制线程，三配置有限预算、分配诊断、Ready、线程生命周期和实际分发库存。

标准 G3-C 原测量以保存的方法源 SHA 为真实输入，`cde99f2` 是当时的 Git 导航。最终复用核对确认仅未链接的 Control cursor 源改动，Embedded 实际编译依赖、预算和测量源字节不变；原报告、原始二进制和采样均保留，未把最终扩大的全目录摘要冒充原摘要。Git 行尾转换发现及修正见 SPEC/CODE 材料。

Release 6 组占用 ABBA 与同一二进制 6 组无握手启动 ABBA：Private 峰值 1,687,552 字节，分发增量 1,341,304 字节，构造到 Ready 最大 1.0519 ms，创建到 Ready 最大 46.8851 ms；绝对值及成对增量均通过预先批准的有限预算。Debug/ASan 分别完成 480 个完整 Invoke 零新增分配窗口。线程瞬时上界使用固定创建/唯一工厂/真实 join 的结构证明，阶段实际 ID 另行记录，不宣称连续 ETW 或强制冷缓存。

额外完整操作 Trace、异步文件、慢文件采用独立安装消费者，共 36 个进程。默认 Host 日志和治理保留，Ready 新增线程分别为 3/3/4/4，shutdown 后全部退出。普通日志有界、允许淘汰并记录 gap，不充当可靠完成或持久事实；CPU 量化不支持稳定开销比例声明。该项明确限于操作边界 Trace，不宣称通用 Plan/内部逐步骤 Trace SDK。

## 失败保留与交付边界

正式过程中的 `c4b5c97`（旧安装/Conformance 绑定及 Schema 缺项）和 `38802f3`（126 项成功但独立 expected 遗漏已声明 check）均保留原 Failed 报告。最终修复了实际绑定错误，并在 `dacc540` 完整通过三配置；没有降低条件或覆盖失败。最后的生产 CODE 修正为 MAC 输入分配移到 BCrypt provider 打开前，避免异常句柄泄漏。

本交付允许进入后续 B6/D4 规划开发，未启动 D4 实现；不包含 State/Effect 提交后端、持久接受/恢复、完整 SDK ABI 冻结或 G8 发布，也不包含 GUI/CAD/CAM/设备准入。现有演示 PDF/PPTX 与锁文件保留在工作区，不纳入内核提交。

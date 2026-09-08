# D1.06 有限 owner 控制增量自审

本页为实施者自审，未代替独立审核。此前 `footprint-review-fix-37d8fc9332` 的冻结输入与原始记录未改写；本轮不运行完整校准、Native 样本、pilot 或正式矩阵，预算保持 NotApproved。

## 来源与改动

当前十二源摘要为 `3744dda7de3d321396c60e41efb9e23115f75775d823c0f09a30ad9125f4024f`，逐文件及原始证据摘要见 `footprint-owner-increment-index.json`（SHA256 `07d889f7a1d9a9507c336a57e1f01ffdfa16e16daeb5a0789902405af292f259`）。集合算法为排序 `path + TAB + sha256 + LF` 再 SHA256。

相对于上一节点，本轮新增永久回归 `test_regressions.py`、固定协议控制消费者 `owner_consumer.py`、有限运行入口 `owner_controls.py`。`windows_process.py` 的 root 退出分支在禁止后续进程查询前检查本轮自有阶段事件，拒绝未消费通知，不再让根退出优先路径吞掉第十二阶段。只检查自有事件句柄，不读取退出进程指标，不给消费者 ACK，也不重新附着 PID。

`analyze.py` 的 `owned_success` 增加 process_tree 为 dict 的必要条件；None 树返回 False。现有 `run.py` 对配置/构建/导入的严格 owned 成功判断不改动。`process.py`、CoreHost、Logging 及已有计数器未在本轮修改。

## 实际证据及运行差异

1. `footprint-owner-extra-6233535fd310` 是真实 red。Python 控制消费者通过全部十一阶段，再通知额外阶段并立即退出。旧 owner 实际返回 Exited、exit 0、Complete；断言失败，驱动 exit 1。它是固定继承通道的真实 OS 消费者，不是模拟 root_exited。
2. 修复后的 `footprint-owner-all-2407ed0d4d7c` 保留整体 Failed：十二护栏中 None 树用例抛 AttributeError。该轮五个真实 OS 断言均成立，不能把整个 result 改成 Passed。
3. None 树类型保护后只运行 `footprint-owner-guards-05c96273fe61`：十二永久护栏实际全通过，owned Exited 0、active_after 0、未终止 Job。该快照和当前十二源逐字一致。

真实 OS 轮与当前源的唯一差异为 analyze.py 的上述类型检查，旧/新 SHA 在索引中分别保留。真实控制未重复运行，也不声称它们在新十二源集合上重新执行。

| 真实控制 | 实际结果 |
|---|---|
| 完整协议正控 | Exited 0、Complete，十一阶段、十二个阶段/暂停样本 |
| 末尾额外事件 | Exited 0、ObserverFailed，明确 UnconsumedPhaseEventAtRootExit |
| root 退、子孙活 | root exit 0、DescendantsAlive，实际 Job 总进程 3、终止本 Job 后 active_after 0；观测 Incomplete |
| 同名外部 sentinel | 与被测消费者为同一 Python EXE、PID 不同、另一个 execute-owned Job；被测 Job 清理后仍等待释放，随后自身正常 Exited 0，未被终止 |
| 绝对阶段期限 | stage_timeout_ms=1600、总 timeout=8 秒；首阶段确认后停顿，ObserverFailed/StageDeadlineReached，实际 root 125，owned Job 排空 |

Job 总进程数字原样保留，不能把 Python 启动链进程误称为内核 worker。sentinel 辅助线程只在测试驱动协调另一轮同一 execute，不进入观察器或任何占用样本；每个 Job 均有有限运行/清理期限，无外部 PID 注入和按名终止。控制采用 latency 协议模式，保留真实阶段内存/线程查询，不声称测得高频占用或正式 Release Ready 时延。

永久回归分别覆盖查询耗尽原运行期限、耗尽原阶段期限后不 ACK/不重置期限；配置前驱 root=0 但 DescendantsAlive 不调度构建；owned 成功必须具备全部树事实；root 退出的事件检查及后续 memory/thread 调用拒绝。原八个护栏一并执行，未重跑 D1.05。

## 最小方法增量建议（未实施）

当前合同 §3 明确内存和线程都每 5 ms 到期尝试，§6 校准要求只有原主线程的负控制。已有校准显示全局 Toolhelp 枚举显著阻塞 owner，且 baseline 实际有多个线程；这些事实不能由本轮生命周期控制关闭。

建议仅将高频采样拆为每 5 ms 到期的原句柄进程内存查询，线程枚举固定在 assigned_suspended、各阶段边界及释放后，模块枚举继续原两处。需先修改 §3 的方法输入/身份并重新绑定同配置 baseline；记录两通道独立查询耗时、实际间隔和缺样，不将少枚举算作采样覆盖改善的旧方法结果。校准中的 held-thread 正控仍要求在持有边界可见、join 后消失。

边界枚举不能排除两个边界之间创建又退出的短命线程，因此不能凭这一改动声称“内核新增线程 0”已证实。该硬约束仍需真实 Native 路径的独立创建行为证据及相同 CRT baseline 的线程来源说明；若仅有边界快照，报告必须明确未覆盖短命线程，不能放宽成“边界时线程差为 0”。只提方法差异，不在本轮加入新的跟踪工具或改写硬约束。

下一有限工作应在主集成收口方法后直接转真实安装 Native/baseline 配对和规定 pilot，再形成有来源的预算决定。当前仍缺这些材料及正式预算后样本，D1.06 不因本页 Passed 子断言成为包级 Passed；本包提交后按用户要求暂停，不开启后续工作包。

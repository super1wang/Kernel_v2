# D1.06 采样方法有限增量候选

状态：Candidate，等待现有 AI 独立规格审核；本页不改动受审观察器，不批准预算。依据 `native-footprint-method.md` §3、§5、§6、§7 及 bootstrap 实测：全局 Toolhelp 查询明显阻塞 owner，原始 5 ms 缺样不能抹掉。

建议新方法身份明确 `memory_sampling=due_5ms`、`thread_sampling=phase_boundaries`。进程内存沿原创建句柄，每 5 ms 到期尝试；线程 ID 集只在 assigned_suspended 及十一固定阶段边界查询。Ready 高水位仍在 ACK 前取得；模块仍在 WarmupComplete/ShutdownComplete 两点枚举。所有查询在同一 owner 循环执行，不增线程、回调或外部 PID 参数，查询耗时仍计入原绝对运行/阶段期限。

原始记录分别保存内存与线程查询起止 ticks、成功位、缺样及实际间隔；周期点的 thread_ids 为 null，不能复制旧集合或填空集合。线程 observed maximum 只取实际边界集合；内存 sampled maximum 只取实际成功样本，均不冒称完整生命周期瞬时峰值。保留 Ready 高水位的操作系统独立字段。

此变更减少线程可观察时点，可能漏掉阶段之间的短命线程。不得据此放宽“内核新增线程 0”：仍需逐轮匹配 baseline 的真实线程事实和 Host/Logging/Native 无线程创建路径的结构性证据；未知归属、负差或缺证据不通过。held-thread 正控必须保持到相应边界被观察，join 后下一边界应消失。baseline 的 CRT 等既有线程需真实归因，不能预填“仅主线程”。

方法身份、配置、报告口径及相同 shim 的 baseline 必须一同重新绑定，不复用旧采样轮作新方法 pilot。首个编译运行单对仅用于消费者连通及能力证明，原方法缺样照录。独立审核并合并合同前不实施本候选，也不启动 pilot 或给预算自动加余量。

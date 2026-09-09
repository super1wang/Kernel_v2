# ADR：B4 Executor 寿命、调度与资源边界

日期：2026-09-09。状态：实施前决定；落实 A09/A21/A22，不改变 B5 的 Task 责任。

同日按用户规划复审补充下述并发/Profile 细节；补充发生于首轮 Executor 直接测试与 Scheduler 草稿之后，继续实现前冻结，不宣称事前已包含这些细节。

CoreContracts 继续使用唯一 ReadyWork/ExecutorPort，新增可选的 ExecutorControlPort 管理排空/关闭及所属 worker 检查。生产 CpuPool、可控和合法 inline 后端实现同一控制合同。CpuPool 仅私有包含已锁定 BS 5.0.0，Runtime 只消费端口，不认识第三方池。

shutdown_until 从所属 worker 调用必须返回错误且不能关闭池；从控制线程开始关闭后拒绝新 work，期限到达只报告超时，所有在途 work 和依赖仍由 owner 保留。可在控制线程再次排空。析构是最终控制线程回收，等待真实排空，不 detach；从所属 worker 直接析构违反寿命合同，必须 fail-fast，不能悄悄 detach 后释放依赖。共同合同测试 worker 控制拒绝，进程专项检测非法直接析构。生产 CallbackWork 对业务/完成函数的抛错分别隔离并只运行一次；不能捕获违反 ReadyWork::execute noexcept 的未定义用户行为来伪称继续安全执行。

Scheduler 是 Runtime 内部 Ready 调度器，内部 ticket 不是 ExecutionRef，不维护另一份业务 Task/结果目录。它在投递前发布拥有型条目，使用提交中/已接受/已拒绝的仲裁处理合法 inline 和故障 Executor；锁外调用外部端口与完成通知。线程池只持有受限数量可运行 work；资源等待、依赖和期限在 Runtime 索引中。历史完成摘要仅供有界依赖解析/诊断，不参与 pump 遍历。

资源池创建时冻结可信拓扑与别名。每槽统一读写模式与 units，MultiClaim 先归一聚合/溢出检查，再原子全获。Lease 仅持有实际获得条目；等待句柄可撤销，按受影响资源索引唤醒且锁外通知。计算与提交资源阶段显式分开。等待 child 前必须释放重叠资源；未释放时拒绝等待组合，不能形成父持锁等 child 的隐式死锁。当前动态创建资源不支持，请求未知键拒绝；未来资源工厂须独立注册和预算，不能隐式创建。

本轮仅新增明确的底层公开合同，不开启 Host submit/list managed capability。完整接受记录、结构化父子寿命与控制定时线程在 B5 装配；B4 测试直接组合真实端口，不能以此宣布 G3 Passed。

补充决定：submit 本身必须在 Scheduler mutex 外。调度尝试具有不复用的 generation；inline 完成后后端再拒绝/抛异常只记违约，不能改写已完成事实。尚未 Started 的 deadline 精确移除 Ready/依赖/资源等待索引并使 token 失效，已投递 work 的物理 delivery 额度直到所有权归还才释放。Started 后的协作取消留 B5。

资源释放先在自身锁内完成 waiter 去重/摘除/生成 token，锁外回调。wake 不授予 Lease，只提示重新归一并尝试完整 MultiClaim；重挂采用新 generation。Scheduler 不持锁调用 ResourceManager，避免形成反向锁环。首版 resource_factory=absent；不否定后续已注册、有界工厂能力。

构建选择为累计 B4Subset（CLI + CpuPool acquisition），Scheduler/Resources 属 Runtime，CpuPool 升为生产静态库、BS 仅私有；Runtime-only 仍 expected-only。排队、主体 inflight、全局 inflight、实际 worker delivery 独立限额；调度历史仅为最小依赖元数据，控制保留仅为接口容量，不提前实现 Execution 层。

追加澄清（2026-09-09，SDK 集成验证期间）：Started 唯一由 envelope 在业务前、以当前 attempt_generation 于 Scheduler mutex 内成功取得 start claim 决定；同一 mutex 仲裁 deadline expiration。先取得 start claim 才能进入业务；expiration 先赢的 execute 仅做 envelope 收尾。submit 接受、dequeue、execute 函数入口均不是 Started。

Dependency set 随 entry 发布冻结，首版只接受已发布 predecessor，self/duplicate/unknown 拒绝。attach 与 completion 共用 Scheduler 仲裁，已完成者不增加 unresolved_count，未完成者先挂 adjacency 再计数；completion 只摘除/推进一次，终态 generation 防重复 drain。取消/过期删除反向 adjacency，不在长期 predecessor 上累积失效 child。

worker_delivery reservation 随 ReadyWork envelope 寿命且精确释放一次。合规 Rejected/exception 返回已释放 envelope；accepted 最终收尾释放。违约后端拒绝/异常后仍持有 envelope 时，逻辑 attempt 失效但 reservation 保留至真实收尾，以免通过故障后端绕过物理投递上限。Scheduler 对 Executor 只保留弱引用；独立控制 owner 保留池直到排空，避免最后一个强引用由 worker 完成路径释放。

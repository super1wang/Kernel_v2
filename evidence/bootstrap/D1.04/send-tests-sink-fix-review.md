# D1.04 发送消费者 Sink 修复增量 AI 复核

审核者：独立 AI 子代理 review_tools。结论：**原 P2 重复容量预留缺口在限定快照中已关闭；五个发送主体的断言可接受。整体队列实现及最终集成仍 Pending。** 本次仅阅读固定源码及已有原始记录，没有修改实现、启动构建或将局部结果称为整包 Passed。

## 来源与实际运行范围

审核快照 `implementation-d2168bddef74/source` 的 source.json 共 11 项已重新计算 SHA256，全部一致。绑定材料：

| 材料 | SHA256 |
| --- | --- |
| fixtures.hpp | a377312b852e6f0139c8ccd29521a33d65403cca7520a7576f37e94ad64e9740 |
| send_cases.hpp | b8b78d9242c4a46f9aff1f752cb9788d84abfb155bc2b58aa7da5cfec3496200 |
| policy.cpp | 15ae1c2722127d30079d9b170162735271ac335358d2c2b81553255380000fe5 |
| policy.hpp | a6fca851ce64d6985a77b5d543be1b73683b8797f58299c46584f272fff3764a |
| implementation-d2168bddef74/commands.json | b1e1c527d64782da526cef084fd83de1ac56ea5d8b15c56cd6c7f81c36062fcd |
| send-tests-a7b2ed5b4cca/commands.json | 654b208b3df84523877d0e0bae1886adf65cf5f41cd782f0b3d0468a98fc0320 |
| 审核时 policy-api.md（含修订4） | 5f6f054582b003f7d2f38a842691545f03d9cadc48f5faf525b47a9bafb79203 |

本轮 commands.json 只有 configure、build、--list 和三个主体：subscription_connection_cleanup、failed_start_not_started、unsubscribe_inflight。六条实际记录均 Exited/exit_code=0，WindowsJobObject assigned_before_resume=true、active_after=0。35 个名字接线或列出不等于执行 35 个主体；另外两项 transmission_start_arbitration、unknown_start_no_retry 在本快照未重新执行，不能从旧实现的 green 推导新实现也通过。

## 原 P2 的真实 red 与修复

send-tests-a7b2ed5b4cca 中 failed_start_not_started 实际 Exited/1，stderr 为 `!send->enqueue_response(second)`；已逐个核对该目录 commands.json 声明的 raw 文件 SHA。该 red 与本轮 green 的 send_cases.hpp 原字节一致，反例未减弱：介质仅余两字节时第一张票据占用全部剩余容量，第二帧必须拒绝；第一帧写满真实介质后，测试显式模拟排空，原先未入队的第二 Response 可重试并实际写入两字节。

修复后的 reserve 检查 size、pending 与剩余容量，先成功构造 reservation，再增加 pending；先前未 start 的票据因此也占用容量。Reservation 析构在未消费时减回 pending；Started/Unknown 置 used 并扣除 pending、写入已预分配数组；NotStarted 不消费票据、不写字节，仍保留容量。回调抛出时局部 reservation 同样返还 pending。此次控制足以关闭重复预留的原 P2，真实发送仍以数组长度和字节内容作证。

当前测试中协调器及局部 shared_ptr 保持 Sink 存活，且同一 Sink 的容量操作没有并行交错；这里不把裸 owner 指针与非原子 pending 当作通用并发端口保证。最终队列寿命修改必须重新证明 reservation 生命周期不会超过端口 owner，并核对取消析构、reserve 与 start 同时发生时的访问规则。

## 五个主体断言复核

- 两种先后顺序均由 API 返回后的信号控制：撤权先完成则零字节、零 start；首字节已交付后才撤权则保留既有两字节。semaphore 等待没有塞入授权仲裁或 start_now；线程写入的结果在 join 后读取。
- NotStarted 包含真实容量不足、近满双票据、零字节后正常重试和撤权后拒绝重试；再次尝试不能只凭已有 reservation。当前重试用例没有通过丢弃原 Response 来掩盖状态错误。
- Unknown 模拟实际两字节已写，但起点结果不确定；随后 caller 失效、剩余帧与原帧均不得重试，业务仍 Running。需在最终快照重新实际运行此主体。
- 退订与 Session.close 仅将 queued_frames 设为 1，要求删除 Queued 后无须旧协调器 pump 即可重新占用队列预算；测试仍持有旧 Watch/Session 不构成要求释放终态 owner 的内存预算。重复退订、跨会话拒绝以及已开始字节不回收均保持。

## 尚未冻结的队列路径

d216 快照中 Session.close、unsubscribe 将被摘除 Frame 链放到局部 retired，锁作用域结束后析构，方向符合第5节对锁外释放的要求；本轮两个原 red 确实变绿。但 Store 仍维护 raw Coordinator* 索引，不能据此批准后续 shared QueueState 方案。

最终复核必须绑定新源码，检查索引登记/注销与协调器析构并发、close/退订与正在 start 的帧并发、队列预算恰好返还一次、Dropped/Unknown 不复活、Frame/reservation/sink owner 的析构顺序，以及所有大对象和任意端口析构均在仲裁外。建议在现有主体内部补 pending 的保留与取消归零断言，特别是退订/close 后无需 pump 即可再次预留近满容量；目前三个 green 证明队列计数返还，但没有直接断言每条清理路径的 Sink pending 值。无需为此扩张固定 CTest 集合。

本报告关闭先前 send-tests-review.md 的限定 Sink P2；历史报告和失败目录保持原文。最终五项、完整35项、5 wrapper及正式矩阵的实际结果须由冻结后的独立记录支持。

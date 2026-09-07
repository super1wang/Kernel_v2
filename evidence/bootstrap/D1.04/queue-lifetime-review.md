# D1.04 QueueState 与发送寿命独立 AI 审核

审核者：review_tools（独立 AI）。结论：**ChangesRequested：Unknown 没有清空同会话剩余 Queued 帧，已由真实限定反例确认。** QueueState 共有所有权及 Frame 端口 owner 修订改善了并发生命周期；本报告不批准整包，也不将本轮已有 exception/budget 子集或名字注册当作五项发送完整通过。

## 绑定来源

仅审核 `implementation-143f7ea6042d/source`，其 source.json 项已逐项重算 SHA 一致。核心 SHA256 为 `554ee42fa2fdc527adb771c88cc1ef85d05090621dfcc2f443208d5740508534`；policy.hpp 为 `a6fca851ce64d6985a77b5d543be1b73683b8797f58299c46584f272fff3764a`；fixtures.hpp 为 `a377312b852e6f0139c8ccd29521a33d65403cca7520a7576f37e94ad64e9740`。没有改动工作树实现。

## 必须修复：Unknown 的连接清理不完整（P2）

policy.cpp start_next（约397行）在 Unknown 时仅设置 `c->session->closed=true`，随后扣除并移除当前帧。它没有调用 drain 清理该会话全部 QueueState。后续帧虽不能发送，仍占用全 Store 的 queued_frames、queued_bytes 及传输端 reservation，直到调用者另行 close、pump 或销毁协调器；其他正常会话可能因此被拒绝入队。这不满足 API 第5节 Unknown 的关闭连接清理，不能仅凭已失效 caller 与禁止重试判定完整正确。

独立证据 `queue-lifetime-probe-5bd109a08c13` 从上述快照复制源码编译，并保存 driver、main、source SHA、全部命令及原始流。实际 configure/build 均 Exited/0；两个场景均先排入两帧，Sink pending=4：

| 场景 | 真实结果 | 原始观测 |
| --- | --- | --- |
| close_control | Exited/0 | pending=0，size=0 |
| unknown_cleanup | Exited/1 | pending=2，size=2；断言 pending==0 失败 |

所有命令属于先归入 WindowsJobObject 再启动的进程，active_after=0；commands.json SHA256 `212133f750dceb73b93055ef9a1b8d220869ab742e6f005e0a06a4b533dfb5f1`，61 个构建归档条目已逐项核对 ZIP 原字节 SHA。第一次沙箱内配置不能识别编译器的失败另存 `queue-lifetime-probe-e163bec40ac9`，未覆盖；正常环境重跑仍使用同一 owned Job 驱动。

修复应在同一次关闭仲裁中将该会话所有剩余队列摘出、单次返还队列预算，保留当前帧 Unknown 事实并禁止重试；reservation 和其他 owner 在锁外析构。建议在已有 unknown 主体内补“同会话另一协调器也被清理、不同会话恢复使用共享队列容量”的断言。反例只要求清理排队内容和预约容量，不要求释放外部仍持有的终态 Response/Watch/Session 内存预算。

## QueueState 与原始 owner 的源码判断

- Store 与 Coordinator 共同持有独立 QueueState，drain 不再解引用正在析构的 Coordinator。索引登记和移除由 Store mutex 串行化；Coordinator 自持 queues，移除索引时不会成为 QueueState 最后 owner。队列及索引预先 reserve，成功入队前还检查全局 queued_frames，所审 push_back 路径无需扩容。
- QueueState.session 虽是裸比较键，但不解引用；登记期间 Coordinator 保持 Session，注销先于其成员析构。没有从该比较键观察到原先 raw Coordinator 索引的同类访问问题。
- start_next 持局部 shared Coordinator、Frame，并用 pumping 阻止同协调器两个 start 同时选择队头。close/退订先摘队时会清掉 charged；已借用 Frame 的 start 在最终仲裁看见 false 后拒绝，因此不会因队头变化误删下一帧。反向先完成首字节则终态不回滚。
- Frame 在 reservation 前声明 reservation_owner，逆序析构保证 reservation 先于 Sink owner 释放。被 close 摘出的 Frame 即使比 Coordinator 活得更久，也不再仅依赖 Coordinator.sink 保活。临时 reserve 返回值尚未移入 Frame 时由方法局部 Coordinator 保持 Sink。
- drain 将 Frame 移入 retired 链再 erase 空槽，末端链在锁外释放；start 的 erase 有局部 f 保活；Coordinator 注销时自持 queues，正常成员析构在 mutex 作用域退出后发生。charged 标志统一在 Store mutex 下检查并扣款，已清理帧不会被 start/析构重复扣除。本判断为这些明确队列路径的静态复核，不代替所有政策路径的无分配审核。

## 最终复核边界

须修复并保留本次 red，绑定新摘要后重新跑五项发送与清理反例。现有 Sink.pending/size 是非原子字段；五项顺序控制未让 reserve、取消析构与 start 同时操作同一个 Sink，因此不能将这套夹具直接推广为任意并发端口。若新增并发寿命诊断，应使用符合接口约束的并发安全预留消费者，不能在授权锁内等待，也不能以夹具数据竞争误判内核。

本次没有跑整包、正式矩阵或 wrapper；未改实现、未提交，历史报告和失败证据保留。

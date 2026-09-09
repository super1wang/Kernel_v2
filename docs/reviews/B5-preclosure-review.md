# B5 前置收口 SPEC / CODE 集中复核

2026-09-09；actor=AI，自我复核，不代表人工 Approved。绑定输入见同名 spec/code JSON；机器 Passed 仅由正式影响报告和自动验收决定。

## SPEC 结论

采纳附件 C1/C2/C3。C1 是 D3.03 锁外回调与 owner 寿命实现缺陷，C2 是 A21.1 既定 96 ASCII 字节限制偏差；不改变主架构、包 DAG 或容量默认值，无重大决策变更，不新增 ADR。B5.md 的 ResourceWaitBinding 是现有 Scheduler/ResourceManager 单向端口的接线细化，当前只有规划，不声称实现。

C3 在 acquire 前发布稳定 ticket 之外，明确预建 owner 承接 enqueue 返回前 terminal、Installing 缓存早到 wake、双 generation 的不同用途、唯一重试驱动、terminal 失效在途 acquisition、lease 安装先于 make_ready、make_ready 失败释放以及 Started 后协作取消。各交错成为 B5 必测项；当前顺序集成测试只验证既有底层合同。

非阻塞优化保留到有测量/直接编码需求时，不扩大本轮代码。B5 Ready for Development 是放行说明，D3.04–D3.07 状态仍 NotStarted，不新造工作包状态。

## CODE 结论

- Lease 在外部 wake 前复制 State 并清空 held，当前 handle 即使被回调删除，也不再访问 this；外部抛出后的 State callback_errors 记账及 pending 链继续受局部 shared_ptr 保护。
- Waiter 在任何 capture 析构前将 generation 交换为 0，局部 State/active 持有到 cancel 返回。重入 cancel 无操作；最后 capture 可删除 handle/manager/其他 lease，不产生返回后字段写入。
- State 的资源互斥、跨索引去重、pending 预算、锁外 wake/capture 销毁均保留。close/acquire 当前已在外部析构前结束成员使用或持有局部 State，无需扩大修改。
- 单一 valid_key 用于 slots、aliases、alias targets 与 claims；格式非法为 InvalidInput，合法未知为 UnknownKey。0..127 ASCII 字节不擅自加 printable/NUL 禁令。未知键不创建 slot，alias 仍只引用先前发布名称。
- 新测试触发 release 回调删除全部 State owner，含抛出与后续 pending waiter；cancel capture 析构验证 generation 先归零并重入删除。key 边界覆盖 slot/alias/target/claim、96/97、多字节非 ASCII 和全部 ASCII 字节。原资源竞争/锁外回调与 scheduler 集成保留。
- 无公开头/ABI/SDK manifest 变更，无 Runtime 新依赖。DAG 作为实际构建图验证。仅 packages/runtime/resources 与直接测试改变；不修改证据采集/验收工具。

直接 Debug resources 15/15 已通过。正式 expected 在执行前固定：Debug 16、Release 16、ASan 18（资源 15 + DAG 1；ASan 加 healthy 与真实 heap overflow 正控制 2）。正式全部绑定同一已提交来源；只重开 D3.03 的受影响合同，不重跑 B4 累计或 G1/G2。

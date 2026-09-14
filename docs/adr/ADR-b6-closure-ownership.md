# B6 收口：提交仲裁与完成材料所有权

日期：2026-09-13。状态：Adopted / Closed（2026-09-14）；本决定不代替机器验收。

在原 B6 责任内增加窄的 CommitClaim 仲裁对象。State 为每次准备创建独立对象；close 只能将尚未消费的 claim 标记取消。可信 PermitAuthority 在自身权限、期限、撤权和一次消费锁内，最后以 compare/exchange 赢取该 claim，然后标记许可已消费。域锁外调用 authority；准备进入 consume 不等于 CommitClaimed。未实现该能力的 authority 明确拒绝，禁止退回不受治理消费。Runtime 在业务前安装 owning stop relay，把调用取消接到真实 ActionAuthorization。

CommitReport 随本次完整 PreparedIdentity 交付 owning PublicationProof。Runtime 校验 report 身份及 disposition 后，使用本次 proof 封口，不查询 provider latest 槽。已发布证明是历史事实，生命周期重开禁止旧编辑，但不能使尚未封口的合法历史提交失去证明；证明保留独立域实例身份与已发布材料。

State 在域锁内一致捕获 PublishedState 与生命周期 identity。prepared reservation 的唯一性由实际存活 lease 表示；放弃或错误只释放精确匹配 owner，ClaimWon 后由完成 owner 排空。外部 authority 和用户正文析构在域锁外执行，跨回调的方法局部保活自身。

History 明确区分环内保留量与外部 owner；压力扫描有限且不丢失被保活数据。Atomic 注册派生调用和显式 revision 的具体接线继续按 C4–C6 实施，不新增 Plan IR 或第二执行表。

公开 Host 接线决定（实施前补充）：HostOptions 显式 enable_state，默认关闭；开启后允许已注册的 StateEdit provider，仍经同一 Registry、Policy、HostBound 和执行后端，不引入 Runtime 对 State 的依赖。单条 StateEdit 的 RevisionPolicy 可由可信 OperationOptions 声明 ServerCapture 或 RequireExplicitRevision；外部 Atomic 组注册则固定要求 RequireExplicitRevision，客户端 InvokeOptions 只携带 expected_state（revision 与 lifecycle_generation），不能改变策略。匹配在业务进入前完成，managed 使用相同快照及 commit 路径。

History 继续采用拥有 before/after 的结构共享快照表示，delta_bytes 是逻辑编辑权重而非真实堆分配。新增独立 retained 账本随 HistoryRecord 最后 owner 释放，ring 字节只表示索引保留。压力扫描最多遍历配置条数，每次在锁外析构一条，外部 pin/准备材料保活者不淘汰；快照正文仍由共享根保护。Undo/Redo 保留单次切换，不宣称多级导航。实际索引节点及增量分配计量已实现，C6 正式运行独立验证，不把新增 retained 账本等同整个堆硬上限。

C5 分配边界决定（实现前）：为 ObjectRoot 私有 immer 对象索引和反向索引采用计量 heap policy；同一根谱系的共享节点只计费一次，节点 header 持有账本至真实释放。设置独立 index_bytes 上限，分配前保留额度，失败回滚额度及候选，不用逻辑正文 bytes 冒充索引堆占用。空索引的库级固定 singleton 与业务正文/标准库控制块分开说明。公开只返回数值快照，immer 类型仍不泄露。

C4 接线细化：CandidateBindings 从冻结 Catalog 校验真实 handler 类型、provider 和合同后产生只读步骤，保有 Catalog/code/input owner；CoreContracts CandidateCallPort 仅暴露候选工作能力。原始 AtomicStep 构造器仅为受信低层装配。有限 Input 由已绑定步骤构成，允许仅引用前序同类型结果的 slot 和标量等值断言，最多 128 步，不接受控制流或脚本。注册组仍是一项 StateEdit：可信 SubmissionStorage 提供完整 AtomicMembership，Native/Host 共用一次真实 ActionAuthorization、一次 CommitReport；执行器按组声明取一次资源集合，并核验成员资源没有遗漏。WorkContext 的当前授权端口只重验本次 Action，不能创建许可。Host 默认关闭 State；显式开启同时支持已注册 CandidateRead provider。SDK 更新为 dev.8。

最终精确化：CommitClaim 的取消胜出会进入同次 CommitReport 的 cancelled_before_claim 事实；StateNativeCall 只在实际进入 handler 前置 business_entered，并把该事实传给 Outcome 完成器。显式 revision/lifecycle 前置失败因此记录为业务未进入；CancelWon 仅来自真实 claim 仲裁，不能由结束时 stop 状态推断。

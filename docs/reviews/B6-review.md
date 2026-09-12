# B6 D4.01–D4.04 集中 SPEC/CODE 技术复核

日期：2026-09-12。审核者：Codex AI；按 `automatic-acceptance-policy.json` 自我复核。范围仅内核 State、CoreContracts 窄端口、Runtime StateEdit 桥接及规划验证消费者；不包含 B7/G4、Plan、Durable、Project/Document 或产品模块。机器运行和最终 Passed 决定独立于本技术结论。

## SPEC

D4.01 的公开模型由 `FrozenRoot<T>`、拥有型 `PublishedState`、授权 Snapshot 和 lifecycle generation 组成。冻结合同必须切断可变别名；一个发布记录同时拥有 root、revision、History cursor 与 generation。关闭阻止新读，已有 Snapshot 继续拥有旧根；重开轮换 generation 与域内证明身份，使旧 Snapshot、PreparedState 和 PublicationProof 不能进入新生命周期。配置根可直接类型化冻结，通用对象根私有使用 immer，State 的安装闭包仍只有 CoreContracts。

D4.02 的 `ObjectEdit` 只持 base/candidate/WriteSet，没有 commit 能力。创建、替换、删除和 read-your-writes 同步维护反向引用；悬空引用、删除被引用对象、对象数/根字节/差量字节超限均在候选阶段拒绝。History 是有限环，保存前后结构共享根及差量；有效空 delta 仍形成提交，Undo/Redo 都创建新 revision，关闭重开清理旧环且释放发生在锁外的有限批次。

D4.03 遵守 A06：候选、History、回执与发布证明存储在 permit 消费前预备；同域首版只有一个 reservation。revision、generation、真实 PreparedState owner 与 expected PermitBinding 均在 claim 前核对。PermitAuthority 在域锁外消费；消费失败不发布，消费成功后的内存路径只执行已预留的固定拥有权交换。根、revision 与 cursor 在一次锁内替换，旧根锁外释放。发布后的 Outcome 仅核验真实 PublicationProof；若可信 provider 违反这一内部不变量则终止，不能改写为 FailedBeforeApply。

D4.04 允许同一签名的 StateEdit 在独立 Native/managed 调用与 AtomicStep 中复用。Atomic 在同一 provider/domain 的一个候选上顺序执行 StateEdit、CandidateRead 与 PureCompute，组前校验全部描述和资源，逐步复核权限，最后只消费一次 permit 并发布一个 History 单元。Await、ticket、嵌套、条件、循环、并行、异步派发、外部等待、错误 shape/provider、纯计算组及 128 步以上均在发布前拒绝。

SPEC 结论：批准 B6 的窄合同、ADR 与事前 expected。`StateNative` 是最小安装裁剪，`B6Subset` 是同时含现有 CLI/CpuPool 与 State 的开发 SDK Profile；两者都不提升 G4 或后续组件状态。

## CODE

复核 StateDomain 的锁顺序与重入路径：外部读授权、permit 消费、业务函数、receiver 和大对象析构均不在域锁内。Snapshot/History pin 使用独立有限账本；环中 History 以 entry/byte 双上限约束，环外仍被 HistoryView 持有的记录由有限 pin 数约束。revision/cursor 溢出显式拒绝，旧 base 不重算，关闭与 claim 竞争只有消费前关闭拒绝或消费后完成发布两种结果。并发直接测试在 100 次发布中检查 revision 单调且两字段从未撕裂。

复核对象根与编辑：immer 类型只出现在 State 私有实现，公开头和安装 CMake 不泄漏依赖。每次更新返回新根，未改正文的 owner 保持共享；WriteSet 修改失败不替换 candidate。反向索引与对象表在同一新 Impl 中形成，RootContract 再检查所有被引用目标存在。4096 对象样本验证 100000 次 Snapshot copy 和单对象更新，原始 JSON 成本输出由测试保留，不把一次机器时延提升为跨机预算。

复核 Runtime StateEdit：provider 先按实际 target 解析 domain，再运行输入投影；caller、base revision/lifecycle、协调器当前 permit authority 和 expected binding 明确传入 provider。业务 R、KnownFacts、Outcome 条件与结果字节均在 provider prepare/commit 前完成；真实提交后只取得缓存发布证明并封口。managed reply 计量在发布后失败时保留 StateCommitted，并按完整 reply_limit 计费，避免既回滚事实又少记 owner。同步路径只接受 `InlineAtomicProviderPort`；普通 callback provider 继续返回 ProviderUnavailable，未伪装为同步完成。

复核中发现并在冻结来源前修复两处生命周期证明缺陷：StateDomain 的通用 `attest` 原先仅凭当前 revision/domain 可接受不同 CommitId，现改为在 History 中同时匹配 commit/revision；重开原先复用证明 identity，现随 lifecycle 轮换 identity。直接反例覆盖同 revision 伪造 CommitId、关闭后新 History 读取和重开后旧 PublicationProof。

复核 SDK/构建：Runtime 没有链接 State，State 公开只链接 CoreContracts；bcrypt 仅为 Runtime 内部随机 CommitId/ReservationId 所需系统库。公开清单固定 `0.1.0-dev.7`、`B6Subset`、State 静态目标及九个 State 头摘要；安装消费者在迁移后的 prefix 中构建运行，并拒绝 Workspace。Runtime-only/Embedded 的目标图不包含 State/immer，正式 closure 仍须追加独立裁剪与原预算 footprint 刷新。

CODE 结论：当前技术复核未发现未解决的 P0/P1/P2 阻断。D4.01–D4.04 的正式 Debug/Release/ASan 影响矩阵、State 安装、Runtime/Embedded 裁剪、受影响 footprint 和最终证据验收仍须从冻结提交真实通过；本文件本身不是 Passed 决定。

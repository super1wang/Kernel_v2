# B4 集中 SPEC / CODE 技术复核

日期：2026-09-09。审核者：Codex AI；采用现行自动验收政策，不代表人工 Approved。范围 D3.01–D3.03，按唯一规范 v3.3-r2、B4 规划及 ADR 核对。此文记录技术判断；机器完成条件由同一已提交来源的正式矩阵决定，最终状态另行归档。

## D3.01 独立结论

SPEC Approved。生产 BS 5.0.0、可控与合法 inline 后端使用同一 CoreContracts ExecutorPort/ReadyWork；控制 owner 的 ExecutorControlPort 管理 worker 检查、关闭和真实排空。工厂、合同版本、源码摘要与 capability 适用项冻结于 tests/conformance/executor/conformance_manifest.json；七个共同必需项覆盖三个工厂，不允许 skip。parallel 表示后端内部并行 worker 能力，不禁止多个调用线程并发进入 inline 后端；不适用专项明确 NotApplicable。故障后端只验证消费者防御，不能登记合格。

CODE Approved。CpuPool 创建前校验 worker/在途上限，submit 所有权交给单一 envelope slot，拒绝及分配/入队异常在锁外清理；BS 队列插入失败不发布迟到 work。适配器先归还自身容量，再析构 work 触发 Scheduler 唤醒；BS wait 仍覆盖最终析构。CallbackWork 用原子 claim 保证业务/完成只运行一次，并分别隔离抛错。所属 worker 的 drain/shutdown 拒绝且不关闭池；直接非法析构在隔离进程验证 abort，外部停止超时保留 owner，最终控制线程排空而不 detach。

直接依据：packages/contracts/include/ock/contracts/executor.hpp、packages/adapters/cpu_pool/executor.cpp、tests/conformance/executor/contract.cpp、tests/contract/executor。共同必需项、满载清理时可重入、八线程重复检测、真实并行/停止超时和三个 worker 非法析构专项均有直接测试。T13.scheduler.fault_attempts 与 inline_reentry 是本包 Runtime 消费者侧的共用证据。

正式 Passed 仍要求 D1.02/D0.06 既有 Passed 与本批固定机器事实。历史前置不重新取证。

## D3.02 独立结论

SPEC Approved。Ready 按有限主体权重与 0–7 优先级份额处理，只对持续 Runnable 且有主体配额者承诺公平。global/subject queued、global/subject inflight、worker_delivery 独立记账；已完成但待通知的 entry 也受 global_queued + max_inflight 容量约束。控制容量独立但不增加 execution.cancel。历史只存内部依赖/完成元数据，清除业务 captures，不成为 Task/ExecutionRef 目录。

CODE Approved。Scheduler 在 mutex 内发布 dispatch attempt 并取 generation 快照，然后锁外调用 submit。Started 仅由当前 generation 在业务前取得 start claim；与 deadline 同锁竞争。已 Started 删除 deadline，不强制中断 C++；先过期者精确摘 Ready/反向依赖，并锁外撤销资源 waiter，迟到 execute 无业务副作用。inline 可在 submit 返回前迁移终态；随后 Rejected/throw 只增加违约诊断，不能回滚完成。reservation 随 envelope 析构精确归还；拒绝却仍持有 envelope 的故障后端保留物理额度。

dependency set 随 entry 发布冻结，只接受已发布 predecessor；self/duplicate/unknown 拒绝。attach/completion 同锁，已完成者不计 unresolved，未完成者先挂 adjacency 后计数；一次 drain/失效 generation 防止下溢和丢 wake。过期 child 同时从所有 predecessor 摘除。Scheduler 只持有 Executor 弱引用，控制 owner 必须保持至排空，worker 完成不成为池析构者。业务、通知、wake、captures 析构与资源清理均位于 Scheduler mutex 外。

直接依据：packages/runtime/scheduler/scheduler.cpp 与 tests/unit/scheduler。验证包括合法 inline、拒绝/抛错后迟到 work、重复 execute、投递未 Started 时过期、Started 后期限、两组各 100 次仲裁竞态、真实池并发完成、容量/通知背压和控制槽。公平测试保存 120 次调度顺序、1:3 主体份额及最大间隔；优先级保证低优先级有界等待。history 0/100/1000/10000 分别记录 1000 次空 pump 的实测时间及主体检查次数，判据是工作量不随历史线性增加，不宣称整机性能预算通过。

正式 Passed 要求 D3.01 先 Passed、D1.03 既有 Passed，以及本批固定机器事实。

## D3.03 独立结论

SPEC Approved。资源拓扑和别名冻结；alias 指向 canonical 或已解析 alias，普通未知键拒绝，首版 resource_factory=absent。Shared/Exclusive 在同一槽，units 先聚合并检测溢出；MultiClaim 全获或零占用。拥有型 Lease 只释放己有份额，unique_ptr 转移句柄所有权；单句柄并发访问由调用方同步。计算/提交阶段和 child wait 规则不替代 ActionPermit 或 B5 结构化寿命。

CODE Approved。所有容量与 waiter 交叉索引在 ResourceManager mutex 内变更，分配失败回滚索引且不占资源。release 去重整个 generation 并移除其全部索引，锁外通知；wake 只是重新申请完整 claims 的提示，失败重挂使用新 generation。cancel 的共享原子标志可抑制已摘索引而尚未开始的回调；待通知者仍占 waiter 预算，至回调开始归还。close 取消未开始的等待，Lease 可在 manager 壳析构后安全释放。compute 不持有 commit-only 资源，child 必需槽未释放则拒绝等待组合。

直接依据：packages/runtime/resources/resources.cpp 与 tests/contract/resources。覆盖别名冲突、Shared/Exclusive/units、聚合溢出、半获失败、只释放己有、未知键/claim/waiter 限额、多线程释放和取消、可重入回调、重挂新 generation、阶段拒绝、child wait 及 Scheduler wake/过期集成。

正式 Passed 要求 D3.02 先 Passed、D1.04 既有 Passed，以及本批固定机器事实。

## 共用安装和机器范围

SDK 升为 0.1.0-dev.5/B4Subset，四个新增公开头登记摘要并单独安装编译。CpuPool 导出为静态库且只 PUBLIC 依赖 CoreContracts，BS 仅私有编译。累计 B4 的真实迁移消费者执行 Executor/Scheduler/Resources；Runtime-only 真实构建/安装/Native 消费证明 expected-only；B3Subset 另核对配置投影，未把其 CpuPool 占位报成生产库。SDK 当前版本检查随源码更新，历史 Passed 报告不改写。

正式 S_required 在发现前固定为 Debug 65、Release 63、ASan 65：Debug 的两个 Profile 专项不重复跑 Release；ASan 增加健康探针及真实 heap-buffer-overflow 正控制。三配置共用一份 B4 逻辑完成映射与来源，每配置物理执行一次。AI SPEC/CODE 记录绑定上述精确输入；自动验收按 D3.01→D3.02→D3.03 顺序执行。

B5/G3 未在本批完成：真实 Submit/ExecutionRef、TaskOutcome、协作取消、父子结构化生命周期、CLI 真实 Task 观察和完整 Embedded footprint 仍为后续交付。未扩展 GUI/CAD/CAM/设备模块，编译/ASan 也不代表物理设备准入。

# B6 审计收口交付

日期：2026-09-13。最终实现与语义验证来源 `a03304a407b7716c1c68cb601cedc7c08d2ffa2c`，647 个输入，摘要 `5d15ef17eb4e1305e9275a077a80c3f8a2fde46d24b6f553b4d886f7607d1e9a`。范围为 D4.01–D4.04 / C1–C6，SDK `0.1.0-dev.8`。最终包状态及 DAG 顺序以 [进度](../progress.md) 和 [收口决定](../../evidence/B6/closure-a03304a/acceptance.json) 为准。

## 完成内容

- C1：Started 不代表 ClaimWon；close/cancel 与真实 Policy consume_claimed 共享每次提交的唯一仲裁，撤权/期限重验在同一 Policy 临界区完成；claim 后保留已应用事实。
- C2：每次 CommitReport 自带 owning proof，完整 PreparedIdentity 核验后封口。C1 发布、C2 发布封口、C1 再封口可独立完成；历史证明不受重开/后续提交影响。
- C3：Snapshot 同锁捕获版本和生命周期身份；Frame 拥有 prepared，弱 reservation 不形成永久占用；弃置/OOM 仅释放精确本次 owner；回调期间保活、旧根锁外释放。
- C4：有限组由冻结 Registry 的真实注册函数、Args/R、provider、targets/resources 组成，经过真实 Policy、一次资源集合和一次提交；支持前序值绑定和标量断言，拒绝嵌套/异步/跨域及 129 步。Native 与公开 Host managed 共用同一业务路径，动态目标和第一步后撤权均阻止后一步进入。
- C5：History 字节先满时有限回收未 pin 记录；全 pin 拒绝、释放可恢复。retained owner 与 ring 分列，私有 immer allocator 对共享索引节点实际计费并限制预算；逻辑正文权重不冒充整个堆占用。保留单次 Undo/Redo，不声明多级历史导航。
- C6：可信注册声明 ServerCapture/RequireExplicitRevision；公开 Host 安装消费者跑真实 StateEdit/Atomic submit/wait/result/stop；必要记录故障保留 StateCommitted 并等真实排空。

## 最终机器验证

| Profile | 配置 | 固定集合 | 证据 |
|---|---|---|---|
| win-msvc-debug | Debug | 89/89 | [原始报告](../../evidence/a03304a407b7-5d15ef17eb4e/win-msvc-debug/D4.04/20260914T025922Z-dcbffecc8e6e/report.json) |
| win-msvc-release | Release | 89/89 | [原始报告](../../evidence/a03304a407b7-5d15ef17eb4e/win-msvc-release/D4.04/20260914T031646Z-966938c407fe/report.json) |
| win-msvc-asan | Debug | 89/89 | [原始报告](../../evidence/a03304a407b7-5d15ef17eb4e/win-msvc-asan/D4.04/20260914T032231Z-bb776b904382/report.json) |

三配置均为完整独立运行，同一提交/输入；[矩阵自动验收](../../evidence/B6/closure-a03304a/matrix-acceptance.json) 的机器及 AI 审核错误均为零。SPEC/CODE 按 [批次复核](../reviews/B6-closure-review.md) 和 [包级独立判断](../reviews/B6-closure-package-conclusions.md) 判定，无 human Approved。

- StateNative 最小投影 **9/9**，包括公开调用和迁移安装；Runtime-only 另有真实安装 Host 消费者。各自依赖取得与链接裁剪独立检查，StateNative 不取得 CpuPool，Runtime/Embedded 不取得 State/immer。
- Native **7 个配置/模式组合**、Embedded **3 个配置**全部按原数值预算通过，每项 6 组 ABBA；Release Embedded 同时含启动。Debug/ASan 完整 Invoke 零新增分配窗口保持，Embedded 的内核线程结构上界为 3。Embedded ASan 沿用原 RelWithDebInfo 测量方法，与语义矩阵 ASan Debug 分开标明。
- 成本方法输入在 `6b817c3` 采样后与最终 `a03304a` 逐项相同，最终变化仅为 Runtime State 测试/审核材料；[完整性复核](../../evidence/B6/closure-6b817c3/footprint-integrity.json) 核验全部方法与原始产物，故不重复物理采样。另有 [构建产物补充归档](../../evidence/B6/closure-6b817c3/supporting-archives.json)。
- 每配置完整执行 N={1k,10k,100k} × k={1,10,100} × 有/无引用，共 18 种组合，均未缩小或拒绝。真实 Snapshot、约束扫描、prepare、commit、reclaim 和实际 index 分配分列于 [54 份样本](../../evidence/B6/closure-a03304a/cost-samples.json)。每组合一次采样不支持 P99/SLA 声明；回收测量为释放新域/候选差额，仍保活的基线根不冒称已释放。JUnit 的 1024 字节截断保持原样，完整值来自本轮 LastTest.log，前缀和运行时间交叉核对后补充归档。

## 失败与来源保留

`8743c15` 首轮 Debug 89/89 仅为中间来源；最终三配置不混入该轮结果。Native Release 启动首轮有一个进程在 main 前少一个系统线程，配对等式失败；[原始失败复核](../../evidence/B6/closure-6b817c3/latency-first-run-review.json) 与一次限定完整重采样分别保留，未改线程等式或预算。Release 首轮 88/89 是安装目录 rename 的 WinError 5；安装进程已排空，同一确切目录之后搬迁成功，保持测试不变重新完整 89/89；[原始失败](../../evidence/B6/closure-a03304a/release-install-failure-review.json) 和 [恢复条件](../../evidence/B6/closure-a03304a/release-install-condition-restored.json) 均保留，未确定具体外部占用者。早期 State 安装夹具失败也保留，不改写或拼接结果。

历史 B1/B2 及 B3/B5/G3 Passed 不改写；仅重开本次受影响合同。B7 准入阻断随 B6 收口解除，但 B7 尚未开始，G4 仍为 NotStarted；不扩展 Plan IR、Durable、GUI/CAD/CAM 或设备模块。

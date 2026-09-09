# B3 / G2 后建议收口交付

日期：2026-09-09。结论：本次 C1/C2 收口已完成。被测实现 `44246b8de3502dd2f2b61eec697dd2409e70da86`，输入摘要 `53846351ba6330cb8fa094a2bd4aeafff4b6f23ec1963cfdf48ba3064e7bcba8`。历史 G2 的 `e77c328` 来源和 28/26/26 正式事实保持不变，本次不是重跑或替换 G2。

## 建议分析与取舍

输入为用户提供的 `B1-B3_Closure_Recommendations_2026-09-09.md`；其中“必须/禁止/建议提交说明”均作为审阅意见核实，没有将附件当成新的执行规范。唯一规范仍是 v3.3-r2。

| 建议 | 决定与依据 | 本次结果 |
|---|---|---|
| C1 Watch 终态不可重开 | 采纳；原版本递增检查不足以保证 A05.4 终态单调性 | 接受 Terminal 后拒绝所有非终态快照；准确快照、提示、gap、身份变化分别验证 |
| C2 动态材料冻结周期复用 | 采纳；原连接装配路径确实重复生成 Schema/Record/Catalog，不符合 A03.3 注册期材料寿命 | 显式 RegisteredInvocation/RegisteredRecord，Host 组合层强持有；会话不再生成/编译材料 |
| O1 SharedTypeContract 验证去重 | 暂缓；收益可能存在，但删除校验需完整结构、数值、Unicode、跨字段与错误映射等价证明 | generic Schema、typed 与 HostBound 最终验证全部保留 |
| O2/O3 IPC 去复制、缩短锁区 | 暂缓；本次绑定成本没有衡量 IPC，且更改涉及配额、独占缓冲和首字节仲裁 | LocalIPC 生产路径没有修改；不把这些优化作为 B4 前置 |
| CatalogInspectionPort | 暂缓；属于 experimental API 整理，当前无已确认提权缺陷 | 不扩大 Policy API 拆分 |
| 独立 Service SID | 留作正式部署设计；同 SID 的 OS 主体边界判断有价值 | 不在内核验证仓库创建产品服务或改变账户 |
| 历史占用口径 | 采纳文档口径 | G1 数字仅代表当时来源；D3.07 刷新完整 Embedded |

## 修改与边界

- Watch 完整快照只接受七个合法 phase；所有检查及快照复制成功后才提交版本和终态。Terminal→Running 等错误不覆盖原快照；Terminal→Terminal 的更高版本有效。终态后的 progress 提示不改变终态或推进版本，gap/周期重同步继续 get。
- `RegisteredInvocation<A,R>` 在冻结组合期创建并拥有 Args/Result 材料，同型复用一个 Record。`BoundOperation::create` 与 `InvocationBinding::bind` 必须显式传入材料；会话 bind 不再进入 Schema 生成/编译。实验性 API 调用迁移见 [B3 合同](../contracts/b3-ipc-client.md)，不承诺旧源码签名或 ABI 兼容。
- Service 在一个冻结 NativeHost 的寿命内拥有材料与 const Catalog；Catalog 使用该 Host 的 BindingPort 和同一 Schema owner。临时装配会话立即关闭，不缓存其授权。每个连接继续单独认证、verify、获取当前 SessionAuthority 并绑定 HostBound。当前 Host 不支持原地换注册表，重建 Host 会重建动态材料。
- 100 次真实 HostSession 开闭直接测试证明：Field::schema 实际生成次数为 1；绑定/调用期间不增加，Catalog fingerprint/card 不变，新会话正确调用、关闭后旧绑定拒绝；新材料周期重新生成。没有新增生产计数器或依赖 weak cache 的寿命保证。
- `docs/AGENTS.md` 删除已包含在实现提交中。根目录 AGENTS.md 未修改。Runtime、Policy、Native Invocation 与 LocalIPC 生产实现未修改，未引入反向依赖或第二 Registry/Dispatcher。

## 正式验证与证据

执行前复用 [B3 规划](../plans/B3.md)补充范围。AI [SPEC/CODE 技术复核](../reviews/B3-closure-review.md)与机器事实独立；未生成 human Approved。expected 在执行前固定，未从发现清单反推，失败原文未覆盖。

| 配置 | 固定影响集 | 原始报告 |
|---|---|---|
| Debug | 15/15 Passed | [E03 report](../../evidence/44246b8de350-53846351ba63/win-msvc-debug/D2.07/20260909T043032Z-14b84ba6ed5f/report.json) |
| Release | 13/13 Passed | [E03 report](../../evidence/44246b8de350-53846351ba63/win-msvc-release/D2.07/20260909T043226Z-ee44528a18de/report.json) |
| ASan | 13/13 Passed | [E03 report](../../evidence/44246b8de350-53846351ba63/win-msvc-asan/D2.07/20260909T043319Z-308fd5f40e54/report.json) |

[自动验收](../../evidence/G2/b3-closure-44246b8-acceptance.json)：Passed，errors 与 review_errors 均为空。矩阵标识复用 D2.07，作用范围仅为此次 post-gate closure，不修改历史 G2 决策。各报告目录保存 expected/discovered、JUnit、构建/源码/运行产物及原始命令日志，三配置来自同一提交和输入摘要。

共同集覆盖 watch、Native/Dynamic/字段 parity、共享 Schema 并发、目录权限、真实 CLI/双入口/Windows Shell、Router 安全、成本正控制、依赖 DAG 与 SDK 安装。Debug 额外验证依赖负守卫和 [Runtime-only 裁剪](../../evidence/bootstrap/B2/native-pruned-8f24583418/result.json)：仅取得 expected，安装 CoreContracts/Foundation/Runtime 并运行真实 Native 消费者。SDK 安装触发的 [独立 Host 消费](../../evidence/bootstrap/B2/sdk-installed_host-92dd2b3032/result.json)也已归档；它属于局部安装验证，不是 D1.06 重新验收。

[成本原始样本汇总](../../evidence/G2/b3-closure-44246b8-costs.json)保留三配置各 40 个样本（Native/Dynamic 各 20），共 120 个；全部通过各配置适用的分配正控制。测量窗口不包含注册、连接装配、JSON 文本解析、IPC 或 CLI 启动，本次不据此宣称性能提升或端到端吞吐。

开发直接验证 3/3 通过。最初脚本默认编码导致测试适配未执行，以及测试计数器基类 Value 名字遮蔽导致的构建失败均保留在 `evidence/bootstrap/B2/resume-b3-closure-*`；已纠正，没有放宽完成条件。最终三配置没有失败或跳过，不拼接旧来源成功片段。

B4 尚未开始。真实 Task Provider/CLI 观察和完整 Embedded 占用由 D3.07 承接；本次只覆盖内核与无状态验证消费者，不是 GUI/CAD/CAM、设备或生产服务部署验收。

# B3 / G2 后 C1、C2 集中技术复核

日期：2026-09-09。性质：Codex AI 自审 SPEC/CODE，与机器事实分离，不是 human Approved。审核范围为 G2 归档 `e014053` 后 C1/C2 修改；历史 B1–B3/G2 状态与来源不变。

## 建议取舍与 SPEC

用户附件 B1-B3_Closure_Recommendations_2026-09-09.md 是审阅建议，不是规范或自动授权指令。已按 A03.3（注册期 Schema）、A05.4（Terminal 不重开）、A17.5（提示与准确快照）逐项核对生产源码。

- C1 采纳：原 Watch 仅检查版本/身份，确实缺少 terminal latch。完整 phase 白名单、终态只从准确快照提交、拒绝时保留原状态、终态后提示不推进版本，均属于现行合同落实。
- C2 采纳：原 BoundOperation/InvocationBinding 每次生成 Args/Result RegisteredRecord，服务 compose 每次生成 TypeSchema/Catalog，违反冻结期材料的预期寿命。改为显式材料参数与 Host 组合层强持有，不增加第二 Registry，也不扩展 Runtime 公共 API。
- O1 暂缓：可能有收益，但没有完整结构/数值/Unicode/跨字段拒绝 parity 与精确错误映射证明，当前保留所有验证层。
- O2/O3 暂缓：有减少复制的潜力，但涉及缓冲所有权、配额与首字节并发仲裁，应在后续有端到端成本依据时单独覆盖，不能由当前绑定成本样本推导 IPC 收益。
- CatalogInspectionPort 属实验性 API 后续整理；独立 Service SID 属产品部署选择，本轮不拆 Policy、不创建 Windows 服务。
- 历史 footprint 数字仍只代表 G1 当时测量；D3.07 承接完整 Embedded 刷新。B4 不提前开始。

执行前规划复用 docs/plans/B3.md 顶部补充。expected 固定在 tests/manifests/b3-closure.expected.json：Debug 15、Release 13、ASan 13，各一轮；没有根据 discovered 反推。共享一次每配置物理运行，矩阵仅验证本次 D2.07 post-gate closure，不重新作 G2 历史验收。

## CODE

1. Watch::refresh 在完整身份、版本、合法 phase 与 clone 成功后一起提交 snapshot/version/terminal；错误不覆盖已接受终态。next 对终态后普通 progress 返回空，对 gap、phase/fact 与周期刷新仍准确 get。测试包含 Running9→Terminal10→Running11 拒绝、Terminal11 接受、旧提示与高版本提示不污染、旧 gap、非法初始 phase、Host/ExecutionRef 更换。
2. BoundOperation::create 不再调用 Arguments::create。RegisteredInvocation::create 在组合期创建 Args/Result，类型相同共享同一 RegisteredRecord；void 输出分支保留。会话绑定复制拥有型共享句柄，无借用临时 Record。getter 禁止从临时对象取引用。
3. Service::State 随一个不可变 NativeHost 持有 RegisteredInvocation 与 const Catalog。初始化的临时 Session 关闭后，不保存其 SessionAuthority。compose 仍从当前 Session 取得 authorization、verify caller 并走 HostBound；旧会话关闭不会影响强持有 Schema，也不会让新会话继承旧权限。当前 Host 没有就地更新注册表 API，重建 Host 就重建材料。
4. Catalog 自身继续持有 Runtime 发布的 BindingPort，TypeSchema 从 RegisteredRecord 的 SharedPayload 投影。Catalog search/describe 仍每次按当前权限检查；fingerprint/docs 是冻结定义的投影，未缓存某个会话的 eligible。
5. 100 次真实 HostSession 开闭测试：计数 Field::schema 实际生成调用为 1，绑定与调用期间保持不变；每次结果正确、关闭后绑定拒绝、Catalog fingerprint/card 一致；新注册材料生命周期使计数增加。此计数不是性能采样，也没有新增生产全局计数器。源码直接确认会话 bind 不再进入 CompiledSchema::compile，弱缓存不再承担材料寿命保证。
6. Runtime、Policy、LocalIPC 发送和 Native 生产实现无修改。现有 generic Schema 与 typed/HostBound 验证保留。公开头 experimental 签名有显式迁移说明，刷新 SDK 头摘要；不宣称 ABI/旧源码兼容。

开发直接测试 watch、binding.native、entry_parity 3/3 通过。最初编辑脚本编码错误及测试计数器基类 Value 名字遮蔽的构建失败保留为开发记录，后者通过全局 ::Value 修正，不修改生产合同或测试预期。最终机器验证另由 E03 报告和自动验收记录给出，不在此预写成功。

技术结论：C1/C2 的 SPEC 与 CODE 批准此精确输入；正式完成仍要求同来源固定影响集、SDK/裁剪专项和自动证据检查全部通过。仅覆盖内核与规划无状态消费者，真实 Task 观察和设备部署不在本次声明内。

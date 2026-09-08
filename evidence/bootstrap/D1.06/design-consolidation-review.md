# D1.06 一次 SPEC 输入合并复核

AI 结论：**尚需收口，非实现/验收批准**。本轮只读，不运行测试、不改候选或源码；以下合并到既有 D1.06 输入即可，不新增审批链。

已明确且不必重开：真实 HostIncarnation；同锁 Ready/StopAccepting 准入先后；每次走原 Native/Policy；返回构造期间保活；模块失败 start 自身强保证、仅逆停已成功模块；超时/未排空保留依赖并显式重试；Session 外壳与显式撤权区分；不支持 Host 回调中自毁；无假 Task/空 Document。

## 冻结前短清单

1. **把实际 SDK 增量并入输入。** Host 第 3 行引用的 `docs/contracts/logging-api.md`、`native-sdk-surface.md`、`native-footprint-method.md` 当前均不存在，不能把旧 evidence 候选当已合并正式合同。当前 sdk_api_manifest 仍是 dev.1/CoreContracts、Runtime INTERFACE。落定 dev.2/NativeSubset、Runtime STATIC、产品直接 PUBLIC 仅 CoreContracts、系统 bcrypt 的静态传递链接、其余组件 unavailable；逐一列 CoreContracts logging 第七头、Runtime host/registry/policy/native_types/logging 与安装 detail，保证 Registry/Policy 唯一定义、原路径只转发。同步明确 `test_contracts_stage.py`、`test_foundation_stage.py`、`test_architecture.py`、`verify_contracts.py`/`verify_baseline.py` 的历史断言迁移：冻结 dev.1 对照不随实现改写，保留主项，新阶段实际 BUILD_TESTING=OFF 安装、搬迁 C-A 消费者无源码/test include，链接闭包无 Data/State/JSON/SQLite/Asio。依据 A19.2–A19.4、A21.4、D1.06。

2. **补内部成功步骤与未排空清单。** Host 第 140 行附近 create 已装配真实日志，但第 219 行又允许“无成功步骤”的 Configuring 直接析构。必须说明成功步骤是否包含日志/factory/Policy 内部装配，以及 create 后续失败、尚未 start 的析构如何完成这些端口的 close；不能只用成功模块数为零证明自定义日志已经安全结束。另第 213–217 行的超时报告仅给 active/pending 数量，无法返回 A16.2 所要求的非 quiescent 清单；冻结有界复制的待停模块身份与 Policy/日志内部项，或明确已有哪一返回字段承担该职责。保持锁外清理与现有 fail-fast 边界，不引入异步排空体系。

3. **统一日志公共声明及水位语义。** Host 使用 `contracts::LogPort`、`Result<LogReadPage>`，日志候选使用 `LoggingPort`、`LogResult<LogReadPage>`；需固定唯一命名、错误域/转换、有限容量/页上限和静态 event/key 表。flush(H) 的 covered_through 恒 H，但淘汰前缀必须明确为 min(global_evicted,H)，否则 E>H 时违反结果自身约束；“幂等”限定无新增副作用/覆盖不扩大，不能承诺后续淘汰后的返回字节恒等。已有详细意见在 `footprint-process-hook-candidate.md` §7.1、§7.3–7.4，直接合并即可。Host 第 215 行只允许后续显式 shutdown 重试，日志候选第 118 行“在有限停止预算内重试”应统一为这一规则。依据 A15.1–A15.2、A22.2 LoggingConformance。

4. **把 SafeLogger 的跨端口寿命规则写实。** Host 已承诺外壳方法先复制 shared state，但日志候选尚未说明 SafeLogger 的 facade 计数由谁保活；仅复制 backend 不能保护回调释放外壳后对 this 的写入。采用同样局部共享 State，或明确拒绝同步自毁；同时固定 try_write/snapshot/flush/close 交叉重入、同后端多外壳、A→B→A 的识别和独立 facade 控制失败计数。沿已存在 §7.2 意见收口，不给生产端口增加测试开关。两种合法后端继续运行同一五项共同集，callbacks=false 不能关闭寿命项。依据 A15.2、A16.2、A22.2。

5. **把 footprint 的能力证明接到真实可调用表面。** 方法候选 §3 要被测 EXE 完成内存日志接受/查询/flush 范围证明，§4.4 又直接调用 flush；当前默认 NativeHost 只公开 copy_logs 和 shutdown，没有 flush 水位回执，ModuleContext.log 又只允许 start 期间借用。需选定实际装配路径（例如在 ModuleContext 有效的 start 内写入并断言 flush 回执，再由 Host.copy_logs 核对保留；或已有可信 logging_factory 保留管理 owner）并明确 shutdown 后的证明由 Host close 与同生产 LoggingConformance 如何分工，不让消费者 include detail/访问私有槽/保存过期 ModuleContext。明确 Host 实际哪些事件会写日志、固定成功调用是否每次写入、满载窗口如何覆盖；不能测未发生的日志成本。释放阶段至少区分 shutdown 完成且旧 Bound 保留、最后 Bound/Session 释放和管理日志 owner 释放，避免把外部保留误报泄漏或漏计。依据 A21.4–A21.5、D1.06。

6. **固定方法与测试集合，数值预算留给真实 pilot。** 当前 footprint 中预热、ABBA 次数、采样周期/轻量时延轮仍是“建议”；与 Host 40 次整窗、默认日志容量、具体配置一起定成一次受审方法及 expected 子断言，新增 Host/Logging/安装的实际测试不能由 D1.05 family 自动代替。A21.4/6 已要求真实 pilot 后形成先于最终报告的有限 NativeSubset 预算；本次不捏造数值或多建节点，沿既有 AI 政策在同包中完成预算决定与新运行验证。线程新增 0、成功整窗分配 0 直接保持硬约束；G1 在预算/实际证据未齐前不能 Passed。

## 只读来源

- `docs/contracts/native-host-api.md`：`b6949355179f97077854c23c2f6dbb919dfc7361d6e562bc3ea5d04a4743dc9c`
- `evidence/bootstrap/D1.06/logging-design-candidate.md`：`11270d6e765decbb93dc9475c8b8f37ba2466e7a5662b125a6cff8a423eb0f74`
- `evidence/bootstrap/D1.06/footprint-design-candidate.md`：`296097d9a509982d2c163c726333e50f8a5ee8cd8d36cfc038a6871a641059a5`
- `evidence/bootstrap/D1.06/footprint-process-hook-candidate.md`：`d648c2bcd81a9c98a8a03648eff80f6040520b19740007b613a4a178faa3359e`
- `docs/01_Architecture_v3.3.md`：`f20644428ed8c307109e26fe690e951f7e191659903bb496ba8dee51125bca2d`
- `docs/02_Execution_Plan_v3.3.md`：`7daf3c8a7e68462a97cd593d2ab9a35c1888b720428c72bc9f915049050df511`

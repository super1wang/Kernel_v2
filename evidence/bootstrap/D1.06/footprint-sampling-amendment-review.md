# Footprint 采样方法有限 SPEC 增量复核

actor_type: AI；结论：Approved，仅批准候选的方法变更供同步现有合同与实现。不批准任何数值预算、pilot、CODE 或 D1.06/G1 验收。审核者曾整理旧 footprint 合同，本次是对他人增量候选的有限复核；不重称原合同为全程独立撰写。

A21.4 要求真实内存指标、线程峰值/Ready/退出事实及 NativeSubset 新增线程 0；未规定 Toolhelp 必须每 5 ms。旧方法 §5 本已说明 5 ms 快照会漏短命线程，观测峰值不是完整生命周期瞬时峰值。候选将内存保持每 5 ms 到期尝试，线程固定为 assigned_suspended 与十一阶段边界，明确进一步减少时间覆盖，因此属于必须产生新身份的方法变更，不能说旧方法性能自动改善。

候选保留真实新增线程 0、逐轮 baseline 对照、结构性无线程创建证据及未知归属/负差/缺证据不得通过；没有把约束降为“边界差值 0”。周期 thread_ids=null、两查询各自 ticks/成功位/缺样、Ready 高水位 ACK 前取得、模块仍在原两点、绝对期限不延长，以及 held-thread 持有/退出对照均合理。提速策略允许只验证受影响集、复用准备，不允许复用结论；本候选要求重新绑定方法/配置/同 shim baseline、不复用旧 pilot，符合该边界。

合并时必须同步方法 §3、§5、§6 及 native-test-scope 中旧“内存/线程共同 5 ms”和“baseline 仅主线程”的说明，逐项准确保留原始事实。这里的同步是落实本候选，不能扩成新预算或删除故障职责。内存到期缺样仍如实计入，不因拆分线程查询自动获得完整覆盖；边界线程观测只能证明实际时点，未知 CRT/运行库线程归属不能靠结构性搜索一句话消除。未来正式 Passed 仍需真实同配置来源归因与控制，不要求在此次有限 SPEC 增量中另造一套追踪工具。

本次只读规范、候选和既有策略；未运行新测试。新方法实现及测试需要后续独立 CODE 复核，正式 pilot 必须使用新身份。

## 精确输入

- `evidence/bootstrap/D1.06/footprint-sampling-amendment-candidate.md`：`242dc6ca8073cfaef67b1e7d2b21a2a1c982d700879c77a2acc62c7b86bf60c2`
- `docs/contracts/native-footprint-method.md`：`041ecfd7602a194dc4b50f8aa74619485b159dfbcdfed8d6b668beb17fb6e950`
- `docs/01_Architecture_v3.3.md`：`f20644428ed8c307109e26fe690e951f7e191659903bb496ba8dee51125bca2d`
- `docs/02_Execution_Plan_v3.3.md`：`7daf3c8a7e68462a97cd593d2ab9a35c1888b720428c72bc9f915049050df511`
- `docs/Kernel_v2_开发提速与Token精简决策方案_v2_2026-09-08.md`：`8d08a0a69a36833703f4f6bf152106ab964f3567606c85257264e36017f69299`

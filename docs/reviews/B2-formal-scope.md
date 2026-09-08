# B2 正式验证范围

来源：v3.3-r2 的 D2.01–D2.04 包卡。此文件记录本批验收输入，不新增流程政策。2026-09-08。

| 包 | 正式责任 | Debug / Release / ASan 用例数 |
|---|---|---|
| D2.01 | 单 DOM、预算、重复键、冻结/拥有性、解析种子；ASan 借用逃逸正控制 | 13 / 13 / 14 |
| D2.02 | 同字段合同绑定、Native 实际调用等价、Schema 注册编译/并发/引用边界 | 3 / 3 / 3 |
| D2.03 | 真实注册目录、权限可见性、精确命令卡、帮助/分页/指纹 | 1 / 1 / 1 |
| D2.04 | 帧/Router/Outcome、观察 mock 生命周期、get/list/cursor、安装 SDK 与 Native 依赖隔离 | 13 / 11 / 11 |

依据合同责任事先列举 expected，不从发现结果反推集合。每项实际包含对应正反例；三配置合计 30 / 28 / 29。模板/优化与所有权变化需要 Release/ASan；架构静态守卫仅 Debug 执行。每个配置复用根生产构建目录，包间增量构建；四包各有独立 expected、运行记录和 SPEC/CODE 结论。按 D2.01→D2.02→D2.03/D2.04 前置顺序判定 Passed。G1 和历史包不重复验收。

范围边界：复杂 CompiledSchema 明确 DynamicOnly，不能构造共享 Native RegisteredRecord；当前 Registrar 对 requires_dynamic_schema 保持拒绝，不宣称任意复杂 Schema 操作执行器。绑定使用共享 TypeContract。execution.get 仅摘要且 full_result_available=false；真实 Task、完整结果和真实 IPC 属 D3/B3。Plan endpoint 未安装；Outcome 序列化测试不等于 Plan 执行能力。解析种子是确定性变异，不等于长期覆盖引导 fuzz。

正式验收前检查：共享字段与命令卡来源一致；发送回调不持连接锁，close 后不得发送；void Plan exports 为对象；ASan 消费者使用相同工具链和运行库路径。结果、审核和包状态分别记录，任何配置失败保留原证据，并在最终统一来源重验适用集合。

本文件是范围冻结输入；SPEC/CODE 的最终来源摘要与真实结论记录于各包 technical-review JSON，未生成或未通过时不得视为 Approved。

# 内核开发约束

- 项目文档用中文；仅开发内核与规划验证消费者，不扩展 GUI/CAD/CAM/设备产品模块，不迁移旧代码或旧数据。
- 唯一规范为 `docs/01_Architecture_v3.3.md` 与 `docs/02_Execution_Plan_v3.3.md`（当前 v3.3-r2）；流程规则以执行计划 E00/E02 为准。
- 恢复任务只读 `docs/progress.md` 顶部、当前开发批次对应的规范/合同和当前 diff；历史 review/evidence 按异常或审计需要再读。
- **Passed 前置是已提交事实。** 后续默认只核对状态/公开合同，不重新读取其 review/evidence，不重跑其测试；只有当前修改触及其冻结输入或发现证据损坏时才重开受影响范围。
- **开发依赖与正式验收依赖分开。** 批次内上游达到 Implementation-Ready（所需接口已实现＋直接验证可用）即可继续下游编码；它不是新包状态，也不能代替 Passed。下游最终 Passed 仍要求其正式前置按 DAG 已 Passed。
- 任务执行前必须已有对应规划文档；简单任务复用/补充当前批次 `docs/plans/`，不逐项新建计划。重大决策变更必须输出 ADR，并同步规范、计划和相关合同；事后补录须标明，不冒充事前计划。
- D1–D7 默认 Code-First：在已有规划和规范明确后立即实现生产代码。简单任务配最小直接测试，不先建独立 SPEC/CODE、acceptance 或 Evidence 文档。
- 相关工作包按 E02 Development Batch 连续开发；中间只跑 `S_changed`，不因任务编号切换制造交付墙。必要时可有小提交，但不逐包重复上下文、完整矩阵和正式 review。
- 开发默认 Debug + fail-fast 的最小影响集；按真实风险补 ASan/Release。`full CTest`、完整三配置和历史累计回归仅用于明确风险升级、正式收口或 G Gate。
- 一个批次共享同一最终来源/Profile/构建环境时，物理 configure/build/test 尽量只执行一次；结果按包/需求映射，**逻辑验收独立，物理执行不重复**。
- 正式 SPEC/CODE 默认在 Development Batch 收口时集中完成，可共享一次上下文并在一个批次审核材料中给出各包独立结论；不为每个简单中间节点单独生成审核链。
- `S_required` 只证明当前包/批次新增完成条件，不机械累计所有历史 Passed 测试；`S_gate` 由 G0–G8 承担阶段完整集成、代表性历史回归、安装/Profile/性能/故障组合。
- expected 必须在正式运行前由规范/完成条件确定，不能从本次 discovered 反推；失败和正式原始证据不覆盖，不拼接不同来源结果。
- 非当前编码阻塞、非工作包明确交付物，不扩展测试/Evidence/流程工具；主要精力用于 `packages/**`、`apps/**` 生产代码和直接调试。
- 生产阶段若连续两个提交都没有生产源码变更，应立即检查是否偏离 Code-First；纯文档包、Critical 前置冻结、真实工具阻断修复和最终 Gate 归档例外。
- 自动验收继续使用 `docs/reviews/automatic-acceptance-policy.json`：AI SPEC/CODE 与机器事实分离，不写 human Approved，不逐节点请求人工批准。
- 已授权必要节点提交并推送本任务分支；不强推、不覆盖他人历史。提交说明带工作包/批次标识且用英文。
- 当前状态以 `docs/progress.md` 为准；B1/B2 历史 Passed 保留。B3 前收口及后续开发分别按 `docs/plans/B2.md`、`B3.md` 执行；不重开 D1.06 工具优化。

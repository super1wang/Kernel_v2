# D0.05 审查后修复交付（已冻结，待交叉复核）

本记录由修复实施者编写，不关闭独立审查，也不是用户批准。原独立发现页 `independent-review.md` 保持冻结；主集成者已授权审查代理转换角色实施修复，之后由主集成者和原实施者另行交叉复核。

- 原54项完整相关源码、合同、expected快照：`review-repair-20260907T060536Z-baseline/`。
- 新增8项固定反例先跑真实red：`review-repair-20260907T060537Z-red/`，62项中8 failures。
- 首轮修复62项全绿：`review-repair-20260907T060818Z-check/`。
- 同一拥有权修复内补充整数根类型与恢复输入/指纹一致性反例：`review-repair-20260907T060948Z-ownership-red/`，两项实际失败；原case数量不变。
- 最终冻结来源与结果：`review-repair-20260907T061201Z-verify/verification.json`。

最终实测：expected/discovered/executed均62项，集合完全一致；62/62通过，无skip/expectedFailure。复用已批准D0.03真实Outcome Schema和语义检查器，接受28份CommitDomain及23份Effect投影；交叉检查也完整运行62项。13个来源/验证依赖输入前后SHA-256一致，源快照随run保存。

修复内容：

1. 易失域recover在任何修改前拒绝，保留已发布snapshot与事实。
2. claim前验证候选与原准备材料一致，且revision/history/lifecycle续接合法；整数模型根拒绝可变对象和bool。
3. publish/可信恢复生成拥有型成功Outcome，查询不读取后来变动的Attempt事实字段。
4. Effect在claim时冻结身份、绑定和Device；许可/发送前核对，真实发送报告独立保存；记录、重启和对账沿冻结身份/证据，后续公开字段变化不能重造事实。
5. Request构造不可变且带类型的符号指纹，隔离嵌套参数别名，区分integer/boolean/float/负零，拒绝非有限/未知值；恢复同时检查接受input与指纹一致。
6. 独立保存effect_permitted，Unknown/已许可效果禁止经普通状态接口回到可发送状态；即使错误状态投影为Claimed也不能覆盖原发送准入事实。

新增8项仍属于原D0.05/T07/T14/T15/T16/T18职责，不新建工作包或Runtime能力。固定清单现为62项：commit/permit/effect33项、dedup/restore29项。中文commit/intent合同已同步；未修改root CMake、共享tools、D0.03或Git。

源码/测试/合同/expected已冻结；后续修订必须新证据轮次，不覆盖以上原始输出。独立交叉复核Pending，人工批准Pending，包级由主集成者按全部前置和评审事实管理。

# D0.04 独立审查后修复交付（源码冻结，待独立复核）

本记录由原实施者编写，不自行关闭独立审查或代替人工批准。独立复核责任由implement_d003承担；正式formal.json/tests/runs由父代理刷新。

保留父代理50项基线（含Terminal不可重开、subscribe ack绑定Host两项修复）：`review-repair-20260907T061907Z-baseline/`。原父代理测试与所有历史red未覆盖。

新增固定3项，当前53项（Plan22、Control31）：

- `T19.control.full_snapshot_completes_partial_same_version`：丢phase后progress v3、及仅订阅progress的无gap路径，完整同版get都可补全；更旧get和完整同版重复仍拒绝。
- `T04.plan.bound_domain_and_target_validate_full_schema`：即使Atomic只有pure_compute，绑定后domain仍按provider/id完整Schema验证；StateEdit target同样拒绝非对象/null/错误类型/空值/超长/非ASCII/未知字段。
- `T19.control.client_retirement_rejects_inflight_streams`：未确认、退订后、重连后旧stream以及断线后帧均拒绝；精确活动新stream可接受，空活动集合不作为通配。

实际先行red：`review-repair-20260907T061908Z-red/` 中快照case失败、Domain case捕获4 failures+1 error；`review-repair-20260907T062141Z-lifetime-red/` 中未确认stream被错误接受。修复后对应三项单独通过，见 `review-repair-20260907T062247Z-check/`。

实现：Client分别记录完整快照版本与部分通知合并版本；同版本get能够补全部分字段，保留旧版/Terminal/ackHost保护。添加retire/disconnect本地寿命步骤，event必须属于已确认精确active stream。Plan绑定后复用原domain字段Schema并恢复provider/id必需约束，也用于测试目录中StateEdit target完整检查。相关中文合同已同步。

最终逐项实测：`review-repair-20260907T062403Z-verify/verification.json`。53/53实际通过，expected/discovered/executed集合完全一致，无skip/expectedFailure；20个源码/合同/Schema/样例/固定清单输入前后SHA-256一致，完整源快照和每case原始stdout/stderr随run保留。

源码、测试、合同与fixed expected已冻结，不在父代理正式证据采集期间继续修改。独立技术复核Pending，人工批准Pending，包级仍由主集成者按完整事实管理；不宣称真实Runtime、Named Pipe或授权安全后端已通过。

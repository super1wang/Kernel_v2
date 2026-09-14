# B6 semantic precision v2 SPEC / CODE 复核

复核者：Codex AI（非人工审批）。生产来源：`32c38ad54e875a2812a875685421a46074c292ff`。本复核结论为代码/规格 Approved；机器验收仍须独立完成。

## SPEC

核对架构 A05/A09、既有 Accepted/Outcome ADR、B6 closure 所有权合同、原 final precision 计划与 v2 修订。v2 取代 v1 最终放行决定，历史 B6 / C1–C6 与旧机器证据不作废、不重写。Native 业务前普通失败是 Rejected；已 Accepted 的 State 调用是业务未进入的 FailedBeforeApply；真实取消先赢才是 CancelWon。未修改 Outcome validator、公共 enum/port 或 CoreContracts 布局，无新的架构决定。

正式影响集在执行前固定为三配置各 31 项，具体名单在 `tests/manifests/b6-semantic-v2.expected.json`。其范围包含新的全部 State 反例以及 snapshot、commit、Atomic、Policy、Outcome、Native/Managed、State 安装与 SDK。footprint 单独按新来源、原 11 项数值预算及原方法验证；任何旧来源测量不得混入本次最终决定。

## CODE

- F1：abandon 验证 active owner / Ready 后只清除 reservation；不修改 CommitClaim。close 与 Action consume_claimed 的真实取消仲裁保持。无效 lifecycle 清理、普通 abandon、真实 cancel、revoke、expiry 均有直接反例。
- F2：删除 `managed || !business_entered`；execution_accepted 仅来自 managed 参数，Native 业务前普通失败返回 Rejected。Native 缺 expected_state、stale revision/lifecycle 与 Managed 相应结果分别验证。
- F3：State dispatch 周围局部捕获异常写入 call.failure，唯一业务进入事实仍是 StateNativeCall.business_entered；Read 保留原流程。membership bad_alloc、provider begin 的 bad_alloc/普通异常和真正 handler 异常覆盖业务前后区别。
- F4：C0 由 accepted execution 的 stop callback 记录取消请求事件；C1 必须由 ActionAuthorization::cancel 成功确认；C2 一旦存在 report 就只用其仲裁事实。不会根据 ErrorCode 或结束时 stop 状态覆盖 report。Native/Host 两成员 Atomic 中途取消、普通断言失败、revoke、expiry、published 后 late stop 均覆盖。
- F5：State 执行层前置失败统一分类为 Completed，避免把 State 交给存储层按错误码兜底。原 InvocationRecord::store 本已有对外 Accepted 防回退保护，本次不冒称该保护不存在；新增 expiry 反例证明旧路径会错报 CancelWon，修复后为普通 FailedBeforeApply。真实 Host stale 输入、资源等待取消保留 Accepted 身份和准确业务进入事实。

生命周期检查：Action owner 在 callback 存活期间有 shared_ptr；callback 析构先于其引用对象；跨线程请求标志使用 atomic；State call 在异常转换时仍存活。发布后的证明封装继续走原 registry 的不可降级路径。Runtime 不依赖 State、immer 仍私有。未重写 Atomic、Read 或引入 B7。

## 验证边界

冻结前 30 项 Debug 直接集通过；开发期失败与纠正记录保留在 `evidence/bootstrap/B6/v2-*`。其结果不是正式矩阵。正式 Debug/Release/ASan、安装、SDK、Native/Embedded footprint 完整通过且机器证据已提交前，B7 仍 HOLD。

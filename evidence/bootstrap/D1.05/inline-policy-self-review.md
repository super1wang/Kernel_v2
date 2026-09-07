# D1.05 Policy 窄内联准入自我复核

日期：2026-09-08。此记录为实现者自我复核和独立开发命令事实，不是独立 AI 审核、人工批准或 D1.05 包级 Passed。

## 范围与历史

仅修改 `packages/runtime/policy/policy.hpp/.cpp`，新增 `tests/contract/native/policy_inline_cases.hpp` 及本组独立验证材料。此前部分 `authorize_inline` 实现已由父任务保存在 `pipeline-82c5130c811c/source/packages/runtime/policy/`；本次以冻结的 `prepare_inline` / `InlineAuthorization` / `InlineAdmission` 替换，不将旧片段描述为完整准入。ActionPermit 原有一次消费路径未修改。

## 复核结果

- 原始 Verified 内部拥有记录、SessionEntity、Grant/authority、原始 Target 对象与原 DefinitionSnapshot 均受持有保护；调用方 DTO、自造 TargetView、跨 Session/Store 的目标和调用者不构成能力。
- prepare 阶段校验真正控制块、Read shape、目标唯一性、安装权限集合精确相等及完整五来源规则；Record/slots/targets/快照文本先经过 checked 计量，再分配，在同一 Store mutex 中完成状态验证和 Hold acquire。
- admit 保存冻结 selector，不重建带 string 的 key/type 身份；每次核对原定义地址、实际目标集合、Grant、权限/委托世代、目标 instance/lifecycle、期限及完整规则。锁内仅允许可信 ClockPort::now，未调用业务、TypeContract、TargetProjection 或授权端口。
- 预留槽池不扩容；同绑定容量耗尽 Busy，全 Store 活跃配额耗尽 BudgetExceeded。成功返回 move-only Admission。取消观察紧邻槽计数改变；成功后撤权不回溯已有 Admission。deadline 是请求与绑定上界的最小值；绑定上界来自已经收窄认证/ceiling/scope/TTL 的 Session deadline。
- Admission 移动转移唯一退还责任，自移动安全。归还锁只变更 slot/active count；最后 Record/Target/Grant/Session owner 析构均在锁外。已关闭但仍持有的绑定及仍在进行的 Admission 保留 Hold 计费。
- D1.04 安装入口本来就禁止空/重复权限，本次保持该规则。Catalog 与安装的非空权限遗漏/额外均拒绝；等价权限集合允许顺序不同，五来源中任一来源把两个权限拆在不同规则均拒绝。

## 实际命令事实

1. `inline-policy-b85dee7508a9`：sandbox 内 configure 退出 1。MSVC CompilerId 因继承环境同时包含 PATH/Path 失败；属于环境失败，不记 API red。
2. `inline-policy-171ced4b9fdc`：使用已授权宿主 MSVC/受控工具链与独占 Windows Job。configure 退出 0；以保存的旧 Policy 源码编译新测试，build 退出 1，包含 C2039 `prepare_inline` 不存在。这里只证明新 API 缺失的 compile-red，不冒充业务反例运行。
3. `inline-policy-7bc202b81563`：当前源码独立快照，Debug configure/build 均退出 0；七组独立运行全部退出 0：ownership_and_identity、invalidation、deadlines_slots_and_cancellation、required_permissions_and_tuple、global_quota_and_move_assignment、close_and_original_retention、metadata_budget。全部命令 active_after=0；原始 stdout/stderr、命令及来源散列保存在该目录。

父任务仍需完整 Native 集成、正式三配置/计数门禁及独立 AI 规格/代码复核；本组不单独宣称完整调用窗口零分配。

## 已验证当前文件 SHA-256

| 文件 | SHA-256 |
|---|---|
| policy.hpp | 84bbea42de4fa84a910012c8f7841162406bb9e623aa9d294dab06b6f1f44566 |
| policy.cpp | 92d5c821443a96bbda40fdeb45185b81ca148fd86abe72696e178bf9a57c7481 |
| policy_inline_cases.hpp | a6ac7ede4f0e91d2e04958e1ea1f56430e083444a5715e2ce2766e5fc5170883 |
| inline-policy-run.py | cda3f877e85c1a5bf48e076e3d531d263bd9825f0913bb08b6be77e939f91f55 |

未提交、未推送。实现文件与验证快照一致；`git diff --check` 对拥有的改动退出 0。

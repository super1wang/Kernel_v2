# D1.05 Policy 窄内联准入独立 AI 复核

日期：2026-09-08。actor_type：AI。复核顺序：先完成 SPEC，通过后对同一冻结输入完成 CODE。结论：**SPEC Approved；CODE Approved（限定本报告三文件增量）**。这不是 human Approved，不批准 D1.05 整包，不覆盖 Invocation 管线，也不替代正式自动验收。

## 依据与输入

依据 docs/contracts/native-invocation-api.md 第 5 节及架构 v3.3 的原调用/权限边界。已读 docs/progress.md 与自动验收政策，未改历史报告。审查三文件：packages/runtime/policy/policy.hpp、packages/runtime/policy/policy.cpp、tests/contract/native/policy_inline_cases.hpp。阅读旧路径仅为判定新增逻辑的调用、所有权及互斥约束，不重做 D1.04 全包审核。

| 输入 | SHA-256 |
|---|---|
| packages/runtime/policy/policy.hpp | 84bbea42de4fa84a910012c8f7841162406bb9e623aa9d294dab06b6f1f44566 |
| packages/runtime/policy/policy.cpp | 92d5c821443a96bbda40fdeb45185b81ca148fd86abe72696e178bf9a57c7481 |
| tests/contract/native/policy_inline_cases.hpp | a6ac7ede4f0e91d2e04958e1ea1f56430e083444a5715e2ce2766e5fc5170883 |
| docs/contracts/native-invocation-api.md | 31757ff610fb162e639c714f85de9655706b7432632dc5599772cc7d0566d796 |

## SPEC 结论

未发现需要修改的规格缺陷。

- policy.hpp:267–298：prepare_inline 返回不可变授权 owner；InlineAdmission 为 move-only，构造私有，deadline 只读。InlineRecord、State 与工厂定义留在 cpp；接口没有允许调用者伪造内部记录的公开入口。
- policy.cpp:572–592、1384–1480：保留原 SessionEntity/Verified/Grant/authority/DefinitionSnapshot/Target owner，显式拒绝空 owner 或无控制块 alias。Target 必须是内部 final Target 类型，来自原 Session/TargetPort，且目标唯一；定义只接受 Read。最大槽数与目标数先检查预算，目标 storage 乘法有溢出检查，元数据和 selector/snapshot 文本计入原 declarations/text_bytes。
- policy.cpp:1325–1376：同一 Store mutex 内调用 current 和固定记录重验；Grant 来源及授权世代、Store 权限世代、Target instance/lifecycle/发行来源均检查。权限读取原 DefinitionSnapshot，安装 key/digest 精确匹配，数量/唯一性/包含共同形成集合相等。复用 allowed/match，对 principal、scope、ceiling、module、target 五来源分别要求一条完整规则覆盖全部权限，不拼接规则。
- policy.cpp:1504–1547：每次 actual_targets 与预解析对象目标集合等价检查，拒绝重复、遗漏、额外目标以及另一个 DefinitionSnapshot 地址。在准入锁内重新核对关闭/期限/世代/权限/目标，再取得固定 slot，检查全 Store active_inline_calls，紧邻槽占用观察取消，返回请求与绑定上界的最小 deadline。绑定上界继承 open 时认证、ceiling、scope 与 TTL 的最小期限；restrict_delegation 改变世代，不能恢复旧授权。
- policy.cpp:1482–1503、1378–1382：Admission 正常析构和移动赋值均只归还一次槽及活跃计数；自移动无效应。正在进行的 Admission 持有整个 Record，关闭不早退还 Hold。ActionPermit 原来的一次消费状态机没有变更。

## CODE 结论

未发现需要修改的正确性、生命周期或锁顺序缺陷。

1. admit 在加锁前保存 record shared owner；返回时锁 guard 先析构，本地 owner 随后析构。成功构造的临时 Admission 移动后为空，不在仍持锁时二次归还槽。所有拒绝仅构造固定 Policy ErrorCode。
2. release_inline_slot 仅写固定 slot 与 active count，其 lock_guard 在函数返回前解除；Admission 的 record_ 赋值/成员析构发生在外层锁外。InlineRecord 的 Hold 声明在首位，最后析构；其余持有对象和 vector 在锁外释放，Hold 自己只锁住计费退还。未发现 owner 最后析构递归取得同一 Store mutex 的路径。
3. inline_current 不创建临时 TargetStamp/OperationSelector/string，不调用 CallerPort::validate、TargetPort、TypeContract、业务或目标投影；唯一虚调用为规范允许的可信 ClockPort::now。其余检查只扫描已拥有的固定容器。slots 在 prepare 阶段 resize，调用期不扩容，不创建新绑定、不重复发行 ActionPermit。
4. Store 上限比较先于递增；每槽只有一个非空 Admission 负责归还，移动赋值会先归还自己的旧槽。checked Meter/acquire 继续使用已有统一预算，计费只在完整 prepare 验证成功后生效，失败记录未 counted，释放不会减错计数。
5. 目标与来源字段在发行后不可通过公共接口修改；冻结 Record 的可变 slots 仅在同一 Store mutex 下操作。共享 Policy 配置更新、撤权、关闭和 Inline 准入沿用同一 mutex。

## 实际证据核对

本审查员未运行共享构建或测试。已独立读取下列已有原始命令和测试源，不仅引用实现者自述：

- inline-policy-171ced4b9fdc：旧 Policy 源码编译新增测试的 build-stdout.log 包含 prepare_inline 不存在的 C2039。属于 API compile-red，不声称是执行层业务反例。
- inline-policy-7bc202b81563：commands.json 的 configure/build 和七个独立 case 均 exit_code=0、status=Exited、active_after=0；七项分别是 ownership_and_identity、invalidation、deadlines_slots_and_cancellation、required_permissions_and_tuple、global_quota_and_move_assignment、close_and_original_retention、metadata_budget。
- 阅读该快照 CMakeLists.txt 与 inline-policy-main.cpp，确认实际编译保存的 policy.cpp 和保存测试头，七个命令分别运行对应 case；异常返回 1，未知 case 返回 2。
- 独立重新计算 source.json 的全部 13 个保存源码 SHA-256，**13 项匹配、0 不匹配**；当前三文件也与保存 SHA 一致。
- 本审查员运行限定 policy.hpp/.cpp 的 git diff --check，实际退出 0。

| 证据 | SHA-256 |
|---|---|
| inline-policy-7bc202b81563/commands.json | bb6961b458027ea1337cfc684fc12966e1266433218b94c4ba43bfcdd858177a |
| inline-policy-7bc202b81563/source.json | 887ec1d03a3d9dcb6445cc14a0d8c3711fe975f12d41b8382fd3bdc6d0f3770a |
| inline-policy-171ced4b9fdc/commands.json | 059e3f49bb422a3ef7bea873939ee1aae88de0dfc0507ab90a6911ea98ba9f6c |

## 验收边界

七组是 Debug 独立开发控制，覆盖所有权伪造、旧授权失效、期限/取消/槽和配额、完整权限 tuple、移动与关闭持有、元数据预算。它们没有直接计量完整 Native 窗口分配，也未在真正竞争线程中穷举关闭/撤权/取消交错。父任务仍需正式三配置集成、完整调用计数、异常后配额恢复与并发边界证据。本报告静态锁分析不是这些运行项目的 Passed。输入改变后必须重新复核相关增量。

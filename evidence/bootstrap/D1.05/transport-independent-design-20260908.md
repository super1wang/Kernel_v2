# D1.05 结果运输条件独立 AI 增量设计审核

日期：2026-09-08。actor_type：AI。结论：**SPEC Design Approved**。仅批准下列增量设计，不等同于当前实现 CODE Approved、自动测试 Passed 或 D1.05 全包验收。

## 审核输入

| 输入 | SHA-256 |
|---|---|
| docs/reviews/D1.05-transport-amendment.md | e3c6eb64f71d4fbef5aee5e58e4b57ad26993ca205fa622777e0c99094e42fab |
| build/d0.06-a/cache/sources/expected-fe3b18aecb84/include/tl/expected.hpp | 5a6a46da2e46ee3360bd8fbd759affefc46b8bfd6295043262ec867acac1fa14 |

已独立读取锁定依赖真实源码：expected_operations_base::construct_with（约705行）无条件 noexcept；expected_move_base（约986–990行）即使外层条件 noexcept，也调用该辅助函数。潜在抛出的 R 移动可以终止进程，不能由 Native 外层 catch 保证转换为 Outcome。

## 同意的最小约束

保持 Registry、CoreContracts ContractResult、依赖内容和锁不变。只在 NativeEngine::bind、任何 typed bind 或 Policy 准备前，用 if constexpr 检查以下运输链的无抛出移动能力：R（void 例外）、Result<R>、Outcome<R>、Result<Outcome<R>>、InvokeReply<R>。失败返回 InvalidBinding，业务零进入，不消耗绑定槽；原注册定义仍存在。其余业务、输入和结果校验异常按原先进入前/后规则处理，不跳过任何真实校验。

v3.3 A04/A21.5 没有要求 D1.05 支持任意潜在抛出移动的 C++ 结果运输。本设计保留小定长、void、move-only/noexcept 及可无抛出移动的动态拥有结果，属于本包内部 Native 的明确适用条件。它是原冻结 API 的追加限制，必须保留本增量记录；不能悄悄修改历史审核的条件，也不能把不支持的类型描述为已可 Native 执行。

FinalObservation 可继续采用统一栈退出记录；它不承诺捕获 noexcept 违约，不为 terminate 后的任务补造完成诊断。原先 double-record 的 throwing-move 测试不能继续写成“业务异常被捕获”：这条结果类型在新设计中于 bind 拒绝。

## 要求的验证语义

1. 在旧 Native 源码上运行“Moving 合法注册但 Native bind 必须拒绝”断言，旧版 bind 成功应实际 red；新版本准确 InvalidBinding、业务零进入应 green。这一回归不调用危险 Handler。
2. int、小定长 Value、void、move-only/noexcept 与可无抛出移动的 owning string 保持实际调用正例。后两类结果的成本不能混写为固定场景零分配。
3. 依赖负控制在独立有界子进程中安装 terminate handler，输出固定标记并以86退出，直接触发锁定 Result 移动边界。它证明依赖限制，不是 Completed 或成功恢复的业务用例。
4. 此前长等待尝试只能按其实际命令结果记 Timeout/未完成；在命令事实落盘前，不写成已观测 terminate。所有失败目录保留，不能用新负控制覆盖旧尝试。

审核时实现与测试仍由父任务追加，故未将滚动源码 SHA 绑定为最终实现批准。后续独立 SPEC/CODE 与正式运行需使用父任务冻结的新输入和完整证据。

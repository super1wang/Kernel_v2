# D1.05 Native 管线独立 AI 规格与代码复核（修正后）

日期：2026-09-08。actor_type：AI。顺序：先完成 SPEC，再对同一冻结输入完成 CODE。结论：**SPEC Approved；CODE Approved（仅本报告范围）**。本报告不批准 D1.05 全包，不代表 human Approved。原首轮 ChangesRequested、运输限制初版与 void 误拒绝失败保持原样；本报告是新的复核结论。

## 范围与冻结来源

审查 packages/runtime/invocation/invocation.hpp/.cpp，以及 tests/contract/native 下 fixtures.hpp、pipeline_cases.hpp、value_cases.hpp、shape_cases.hpp、lifetime_cases.hpp、native_tests.cpp 的实际管线/寿命控制。依据原 native-invocation-api 修订2、v3.3 D1.05，以及现行 docs/contracts/native-invocation-transport.md。Registry 基础和 Policy 独立报告仍有效，其精确源码也在本轮来源清单中。

分配计数器实现、verify_children/运行驱动、示例和构建集成的代码审核由其他角色负责；本报告观察它们的 CTest 结果，但不扩充为这些文件的代码审核结论。

Debug 来源：integration-debug-61b6f6c6c86d；Release 来源：integration-release-40b5d325d591。二者 source-inputs.json 的 SHA-256 相同：

`d9da21400d2348ccddf2973f71fa6b19bf20d4231b0fb9127ce75a895d472e16`

审查员重新读取两份 source-inputs.zip，逐项计算全部 **254 个归档文件**的 SHA-256/长度，并与各自清单及当前工作树对照：每份 **0 归档不匹配、0 当前源码不匹配**。这是来源核验，不是对全部254个文件进行了代码审查。

| 受审输入 | SHA-256 |
|---|---|
| packages/runtime/invocation/invocation.hpp | 957e1d64342932c9144daf79d25af59142581849159663dd2b448a69717b4bf6 |
| packages/runtime/invocation/invocation.cpp | 456e79a613f4e43301591ba671e0f771c9aec2ba4c50155219e7ed5423a05c33 |
| tests/contract/native/fixtures.hpp | 70ea4f5ea260ee4c73e75dbd93dc03bd707a01831231fdfc22e74eb17e31dda7 |
| tests/contract/native/pipeline_cases.hpp | 53a2bce7f5febba10f13a467382cda7d142d7e89653352dd6b7b40f8e7287a90 |
| tests/contract/native/value_cases.hpp | 153f47af9950848b3d7d2ff45ea2702649fba5971700dc74287432c19ddede00 |
| tests/contract/native/shape_cases.hpp | db357c0463b164ba211f3ea57646d73227a8134beaf6c81adcceaafcf9d9c9b2 |
| tests/contract/native/lifetime_cases.hpp | aa21c0d8148bd5a5c009bf8ecd6e8b8bd93a28fc900f6ac7748a596867fffa1b |
| tests/contract/native/native_tests.cpp | a3f8bc060471bf632cdf755b9e31fa16e40956603daef84b8850b627ea3e0379 |
| docs/contracts/native-invocation-transport.md | 5e6974e86337bee90882f7c1e5909772bbd94440db3219e7afca3ad1ac9668af |

运输修订历史文档的本次读取 SHA 为 21b080276513d264d3b66934186a70acbc2233fa575d66f30228655acd2876f0；该历史文件不代替上述进入源输入清单的现行合同。

## SPEC 复核

首轮四项源码请求已解决：

1. bind_erased 从真实 Catalog entry 取得 resource_count 后与 resources_per_binding 比较，超限返回 BudgetExceeded；预算内非空资源依旧允许 bind，并在 invoke 返回 ResourceUnavailable。没有把 Registry owner 当调用 lease。
2. create 使用 checked_mul 计算全部 bindings×slots，并用 checked multiply/add 累加 Engine、BoundState、CallSlot、每槽目标、固定目标与观测环存储；总字节溢出拒绝。该检查限定内部静态存储规划，不冒充 D1.06 的最终 footprint 预算。
3. invoke 在端口/TypeContract 回调前同时复制 state_ 与 projection_，之后不再读取原 Bound 成员。输入校验中替换外部 Bound 不改变当前旧绑定的映射，旧 state 的目录、权限、槽仍由本次栈持有。
4. dropped 恰好达到 counter_limit 就设置 dropped_saturated，同锁 snapshot 可看到一致状态。序号最后合法值仍可写一次；后续追加尝试置 sequence_exhausted 并只累计 dropped，不回绕、不改变业务结果。

其余合同仍完整：typed bind 验精确 key/digest/shape/类型身份；私有 check 重验原 Catalog handle 身份、世代、slot、原 DefinitionSnapshot 指针与 A/R token；不在每次调用重建长版本 TypeIdentity。每次实际参数校验、投影集合校验及 Policy admit 均保留。Provider/资源不支持在进入业务前拒绝。成功和失败 Outcome 都经过原通用验证，结果验证异常不会降为 Rejected，失败分支不含 R，不递归调用结果验证器。

## 运输条件及 void 修订

已独立读取锁定 expected.hpp（SHA 5a6a46da2e46ee3360bd8fbd759affefc46b8bfd6295043262ec867acac1fa14）及 Foundation Error/Outcome 实际定义：

- 非 void 的潜在抛移动 R 不能穿过锁定 Result 的 noexcept 辅助函数。因此 bind 的 if constexpr 保留 R、Result<R>、Outcome<R>、Result<Outcome<R>>、InvokeReply<R> 全部无抛移动 trait；不满足者 InvalidBinding，原注册仍合法，不进入业务。
- 初版 trait 链误拒绝 void，不能通过修改测试期望保留该错误。当前显式 void && Error 无抛移动例外正确：expected_operations_base<void,E>::construct_with 只写成功标志，错误分支仅移动 unexpected<Error>。Error 是固定码与 shared owner，其移动无抛。Native 的 ReadCompleted<void>/FailedBeforeApply 运输没有 R 对象；上层保守异常说明不是实际有可抛 R 移动的证明。
- 当前 void 例外没有放宽任何非 void 条件，也没有放宽用户在 noexcept 内抛异常的合同。普通 Handler/Args/R 校验异常仍按原进入前后分类。

## CODE 复核

未发现剩余可操作正确性或生命周期缺陷。

- CallLease 独占对应 Native 槽，取得用 acquire CAS、归还用 release store，目标暂存随槽隔离。Policy Admission 独立持有真实授权记录；其生命周期覆盖 Handler、R 校验、Outcome 和最终记录。没有在返回处理前提前归还槽。
- FinalObservation 声明晚于 lease/admission，析构早于二者；只在退出时写一条最终类别。成功返回对象完成构造后才写 ReadCompleted，进入后错误写 FailedBeforeApply，拥有 Engine 的 Busy/前置失败也由同一记录器覆盖。移出空 Bound 不伪造其他 Engine 记录。可执行的非 void 运输类型已经无抛，void 特例也经过上述实际实现分析。
- 调用栈的 state owner 保证外部 Bound/Engine 释放或替换后，观测环、原 Catalog、Policy、ThreadPort 仍可用；记录器仅借用该栈持有的 state。所有 Registry/Policy 业务分派发生在各自仲裁锁外，观测锁只复制固定记录和索引，不持观测锁进入 Policy。
- snapshot 同一短锁取得 flags 和记录，容量不足返回最新若干条且顺序升序，空输出合法，不消费内部记录、不返回内部指针。索引算术受记录容量字节上限约束；没有新增动态文本或后台工作。
- bind 计数失败、分配失败、Policy 拒绝均依靠已计费 BoundState 生命周期退还，不泄漏占用；不以无限预发一次性许可获得无分配。函数指针及 Reader 分派仍来自原注册模板，NativeAccess 没有 public 分派入口。
- 审查员执行限定 invocation.hpp/.cpp 的 git diff --check，实际退出0。

## 反例与实际通过证据

已独立读取原始命令/日志：

- pipeline-fc96c50794d3：旧实现的 aggregate budget 与 resource cap 两项实际退出1，构建退出0。
- pipeline-3c335c7a6787：旧实现 no_task_path 与 reentrant_and_concurrent 实际退出1，对应饱和和投影替换控制。危险结果移动长等待不作为异常捕获证据，本报告不拿该运行推定 terminate 已被捕获。
- pipeline-1e41c14e44e5：旧实现安全运输反例构建退出0，invalid_output 退出1，stderr 为 !b，即旧 bind 错误接受 Moving。这里没有调用危险 Handler。
- pipeline-b4ce2aa7a9db：新增 void 正控制暴露的 invalid_output 退出1，保留历史失败；本轮来源修正后的同名测试在 Debug/Release 均实际通过。
- wrapper-transport-ff1c9bc50710/throwing-transport.json：独立进程实际 Exited/86，active_after=0；stderr 为 locked_result_noexcept_transport_terminated。此控制证明依赖 noexcept 运输限制，不是 Native Completed 或成功恢复。
- Debug integration-debug-61b6f6c6c86d 与 Release integration-release-40b5d325d591：分别独立核对 commands.json、ctest-native-stdout.log 与 native-junit.xml；configure/build/list/CTest 四命令均 Exited/0、active_after=0，JUnit 各32项、failures=0，原始 CTest 各显示32/32。两轮 result.json 的源码前后 added/removed/modified 均空，另有本审查员254项重新核验。

最新实际 runner 注册中已包含双目标 Reader 返回11/22及错目标零进入、输出验证期间重入拒绝、真实角色线程控制、服务器收窄 deadline 与合作 stop、Handler 内 barrier 强制双槽重叠/第三调用拒绝/独立 WorkContext、校验回调替换 Bound、void 成功/业务错误/异常以及 unique_ptr 正例。它们不再只是未调用的辅助函数。

## 验收限制

本报告引用的两轮为 bootstrap-integration-only、Native32项，不是完整工作包 manifest 的正式最终证据。ASan 本轮仍待父任务提供最终原始结果及相同来源摘要，未在此记为 Passed。分配覆盖声明、负编译、示例与驱动需结合各自独立审核；不能仅凭32个主项名称推出所有测试族、全部原规划依赖或整个进程分配来源均已验收。整包最终验收和 G1 状态仍由父任务按独立来源、正式检查与政策另行记录。

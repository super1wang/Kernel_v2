# D1.05 Native 管线独立 AI 规格复核（首轮）

日期：2026-09-08。actor_type：AI。结论：**ChangesRequested**。本轮只做 SPEC；在以下问题修复并以冻结输入复核之前不进入 CODE Approved，不批准 D1.05 全包。未运行共享构建，也未伪造 red/green。

## 范围与精确输入

依据 docs/contracts/native-invocation-api.md 修订 2（SHA-256 31757ff610fb162e639c714f85de9655706b7432632dc5599772cc7d0566d796）及 docs/plans/D1.05.md、上位 v3.3 D1.05。审查 invocation.hpp/.cpp 与 fixtures/pipeline/value/shape/native_tests。父任务正在修改实现，本报告绑定最后读取的下列输入；初读发现的一次记录问题在期间已有静态修改，单独记录，不将前后版本混称通过。

| 文件 | SHA-256 |
|---|---|
| packages/runtime/invocation/invocation.hpp | dac425bf9d29435f11e0a38308c3bef352ce450f29c0d4951666b5b05dabe84a |
| packages/runtime/invocation/invocation.cpp | e8be6663b5774128a1d98d4ded0fbbebc979fcec8608090ea3872d56d7ff193f |
| tests/contract/native/fixtures.hpp | 70ea4f5ea260ee4c73e75dbd93dc03bd707a01831231fdfc22e74eb17e31dda7 |
| tests/contract/native/pipeline_cases.hpp | 278db666c69f225c64ca03b666a58e741cfecb2b79350088b512e0391df6249e |
| tests/contract/native/value_cases.hpp | 280a461cda3b14d4fc24c98478a00ace462a28034d1dffb6a6b4bce22182d459 |
| tests/contract/native/shape_cases.hpp | f8a4e13dae3e26b282bfd2aab41031d396a14da84f73e055a59f537d52069496 |
| tests/contract/native/native_tests.cpp | 0b27dfd08d0013d6888b2f59254f9bdd65e95cb962957601048fbf51cb1ff305 |

明确排除：allocation_cases/probe、verify_children、discover、CMake 集成与示例的最终审核；这些正由其他角色实现。注册基础与 Policy 已另有独立复核，本轮只检查管线对其实际使用。

## 变更请求

### N1 [P2] 实际资源声明没有执行 resources_per_binding 上限

位置：invocation.cpp:125、166–187。该上限仅在 create 检查非零，bind_erased 取得 resource_count 后没有比较。有效目录含两个资源声明、NativeBudget.resources_per_binding=1 时仍可 bind，违反 §3 每项上限到达必须拒绝的有界绑定合同。

请在取得真实条目后，以可信资源声明数量执行预算拒绝，保持预算内资源操作能够 bind 并在 invoke 返回 ResourceUnavailable。先增加超上限与恰好上限反例/正控制，不把 Resource owner 当 lease。

### N2 [P2] create 未完整检查整个配置的字节乘加

位置：invocation.cpp:129–135。现有条件检查单个数组 sizeof 和 bindings×slots×targets 的元素数，但未检查该乘积乘 sizeof(ObjectId)、全部 CallSlot 字节、固定目标数组，以及与观测存储等各项相加的总量。比如每绑定单槽单目标而 bindings 取 SIZE_MAX 附近，现有 create 可以只分配小观测环而接受不可能表示总字节的配置。

§3 要求所有乘加 checked。应使用统一 checked multiply/add 建立有限配置占用界限并在溢出时 BudgetExceeded，测试选择不需真正巨大分配、但总字节计算已溢出的预算；保留合法小边界正控制。

### N3 [P1] 回调后仍读取 this->projection_，没有冻结整次调用映射

位置：invocation.hpp:143–178。入口只复制 state_；可信 ThreadPort/TypeContract<A> 是回调边界，之后才访问成员 projection_。若参数校验器同步释放外部 Bound，后续会从已结束对象读取 projection_；若回调替换同一 Bound，则本次旧 state 会组合新 projection，违反绑定材料及实际映射必须对整次调用冻结的要求。保存 state 虽保护目录、授权和槽，未保护该独立成员。

请在任何端口/校验回调前将 projection 捕获为本次栈变量，之后不再读取原 Bound 成员。增加可信校验回调中替换或释放外部 Bound 的寿命控制，检查使用原绑定映射且没有悬空读取。不要通过禁止实际参数校验规避。

### N4 [P2] dropped_saturated 到达上限时晚一条记录才设置

位置：invocation.cpp:78–80。当 dropped 从 limit-1 增至 limit，标志仍为 false，下一次 drop 才变 true。§9 明确写“dropped 在该上限饱和并置 dropped_saturated”。当前 capacity=1、counter_limit=2 的六次调用测试只看最终状态，掩盖了精确到限时刻。

请在精确达到上限的同一快照中设置饱和标志，并逐次测试上限前、到限、继续丢弃；同时固定最后合法 sequence 写入后的 sequence_exhausted 状态语义，不仅测试多次越界后的状态。

## 已在审查期间修改的发现

原成功路径先 observe(ReadCompleted) 再 return 命名 reply。ContractResult 不要求 noexcept move；在 NRVO 未采用时，返回 R 的移动可以抛出，catch 又生成 FailedBeforeApply 并 observe，造成单次两条记录。

最后读取版本已引入 FinalObservation，在统一退出时记录；其声明顺序使观测早于 admission/Native 槽归还，消除了该静态重复分支。本报告不将其标为运行 Passed。请保存旧版实际 red 与新版 green，使用允许抛出移动的 R、关闭可选 NRVO 或等效确定性控制，验证最终只有一条 FailedBeforeApply 记录并恢复容量。

## 本范围尚缺的可观察合同控制

以下是冻结 API/计划的验收项，不能因 32 个名称存在就声称各子断言全部通过：

- target_binding 当前只用 compute 的 Args.target 投影；read 的 int Args 根本没有目标字段。尚未提供两个目标返回不同数据的真实 Reader，不能证明授权目标与服务实际读取目标关联。须补合法 A/B 返回各自数据、绑定 A 却传 B 业务零进入的控制。
- thread_modes 主要在同一主线程修改 role/allow，Threads::any_thread 也用于并发放行。需要固定真实 thread::id→role/affinity 表及真实角色线程正负控制，不能把修改观测字段当作已验证全部角色线程。
- cancel_and_budget 检查入口预取消与 charge，但 Handler 未观察 WorkContext.deadline/stop_requested；session_and_deadline 主要调用独立 Policy 测试。需 Native 层验证服务器收窄 deadline 进入 WorkContext，以及准入后合作取消观察。
- reentrant_and_concurrent 检查 Handler 运行中的单槽 Busy，但尚未检查 R 验证期间槽/Policy 活跃计量仍被持有，也未验证配置多槽的合法并行上下文隔离。应在输出验证器受控停留时测试容量，再确认异常/结束后恢复。
- resource_lifetime 的通用借用只检查非空指针投影；尚无明确借用寿命负控制。它不等于 Native 资源获取能力，现有资源拒绝本身正确。
- 最新 no_task_path 已增加容量 1/2、顺序、小输出与最终饱和检查；仍需精确错误码/FailedBeforeApply/Busy 每次一条、并发 snapshot 完整一致性，以及 N4 所述逐边界控制。
- exact_binding 已覆盖精确版本/digest/shape/A-R token/TypeIdentity 和移出对象，但错 Registry/slot/世代/context 的计划子断言仍需要对应现有合同证据或编译不可构造边界映射；不应为测试新增公开篡改入口。

allocation 覆盖、未知事实集合反例、所有权 alias/规模异常、编译私有桥、真实示例与依赖闭包由父任务/其他实现角色正在补充，本轮既不声称这些完成，也不对未完成占位函数给整包 Approved。

## 确认正确的管线结构

有效绑定先用原 BoundOperation typed bind，再冻结原 Catalog DefinitionSnapshot；每次原 handle 身份/世代/slot/快照指针与 A/R token 重验不重建 TypeIdentity。Shape/资源失败先于业务；每次仍实际执行输入校验、目标投影、真实 Policy admit；结果通过 Outcome::validate 的实际 R 校验。entered 分类保留业务前 Rejected 与业务后 Completed/FailedBeforeApply，不因异常降为 Rejected。失败分支不包含 R，避免递归调用抛出的结果验证器。NativeAccess 无公开方法，不存在从管线公开提取真实 Handler 的入口。

BoundState 析构退还 bindings 的时点不列为必修缺陷：确实已无最后 shared owner 才会进入析构；成员释放与其他绑定构造的可能短暂重叠应体现在峰值/回收测量中，不凭这一点断言绑定计数合同违约。

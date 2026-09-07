# D0.04-a：Plan wire 与静态合同

本合同由架构 A03/A07/A08/A19/A21 与执行卡 D0.04 产生。格式固定 `ock.plan/1`，不新增 DSL；Schema 在 `schemas/plan-v1.schema.json`，Control 独立位于 `schemas/rpc-v1/`。Python 模型是 D0 参考检查器，尚不是 D4 的 PlanCompiler、Runtime、事务、真实资源或授权实现。模型操作目录仅为类型见证，没有业务执行。

## Wire 与节点字段

顶层仅接受必需的 `format,durability,on_error,inputs,steps,exports`；durability 为 volatile/durable，on_error 为 stop。未知字段拒绝；预算只由服务器和已认证提交选项给出，不能写入 Plan 扩大。节点 `id` 在块内唯一；槽和名称为大小写敏感、最多 96 ASCII 字节。Operation 必须给出精确 name/version；首版示例使用数字三段版本，缺少目录精确版本返回 UnsupportedDefinition，不选择 latest。

| kind | 必需字段（另有 kind/id） | 可选字段 | 结果 |
|---|---|---|---|
| call | op{name,version}, args, delivery | target, bindings, out | complete：BusinessResult R；ticket：有 out 的内部 ExecutionRef<R> |
| atomic | preconditions, steps, exports, out | domain, bindings（完成绑定后 domain 和 revision 必需） | AtomicResult{values:exports,commit:CommitReceipt} |
| await | task:{slot,path}, out | 无 | terminal 且成功的 ticket R；失败 Outcome 不伪装为 R |
| let | value:Expr, out | 无 | 单赋值、无外部效果的值 |
| assert | predicate:Expr | message | 布尔断言；执行时 false 停止当前块 |
| if | predicate, then, else, out | 无 | 被选中分支的 BlockExports |
| foreach | collection:Ref, item_slot, index_slot, max_iterations, body, out | 无 | 固定集合的串行 body.exports 顺序数组 |
| parallel | branches, max_concurrency, on_error, out | 无 | 按定义次序的各分支 exports；on_error 固定 stop_launch_and_drain |

每个 block 是 `{steps,exports}`，exports 是名称到 Ref 的映射。没有外部 `kind=return`；未来 DSL 的块末 return 只能映射 exports。测试 IR 标识预留 `ock.plan-ir-test/1`；D4.05 才实现 neutral IR 往返，不冻结私有 opcode/ABI、allocator、地址或 DurablePlan 恢复字节。

## 引用、绑定和内部类型

Expr 唯一三种 wire 形态：`{"literal": JSON值}`、`{"slot":"name","path":"/field"}`、`{"operator":"eq","arguments":[Expr,Expr]}`。运算为 eq/ne、lt/le/gt/ge、and/or/not、exists；exists 只接受一个 Ref。比较要求同类型标量，顺序比较要求同类有限数值，不把 bool 当 int，也不暗转 int/float。and/or 运行时短路，所有操作数仍须先通过静态类型及作用域检查。

槽单赋值、只读 `$input`、不得遮蔽任何可见槽。读取只允许当前已定义或上层槽；前向引用、循环引用、路径类型不符拒绝。子块不能修改父槽；只能以 exports 创建父块 out。if 两支导出键/类型完全一致；nullable 应由明确类型合同提供，不能猜测 union。

RFC 6901 path 只接受字符串形式；空串为根，~0/~1 为唯一转义。URI fragment、非法转义、数组 `-`/负数/前导零/越界拒绝。Missing 不等于 null，普通引用缺失拒绝，exists 对已声明槽的缺失路径返回 false。模型为编译类型使用固定输入及测试操作的输出类型见证；没有凭借某个样本值宣称动态目标已授权。

Bindings 项为 `{to,value:Ref}`。Call 只写 `/args/...`、`/target` 及声明字段；Atomic 只写 `/domain` 及字段、`/preconditions/revision`。kind/op/version/delivery/intent/caller/permission 不可绑定。绑定路径先统一解码比较，重复和祖先/后代重叠拒绝，不依赖对象键顺序。最终叶必须从字面量省略，null 仍属于常量；父容器必须存在或由合法根绑定完整产生，不补建任意对象。全部绑定完成后校验整个 Args/Target/Preconditions。

BusinessResult、ExecutionRef、AtomicResult、DataRef、BlockExports 在模型中有独立标签。形状相同的 literal 或 `$input` 对象不能伪造票据；测试目录的合法 ticket 创建 ExecutionRef，Await 才取成功 R。完整 Outcome/日志不作为 R。AtomicResult 的 `/values/...` 与 `/commit` 保持类型，不能当普通对象数组。DataRef 不是 ticket。

每个 ticket 必须被 Await 消费，或经显式允许脱离父执行的授权寿命政策直接导出。默认不允许 detached；某一 if 分支消费票据不等于所有路径都消费。块结束仍持有并收尾全部 child，失败停止后续启动，已提交事实不回滚。这些寿命规定交 D3/D4 对真实 child 验证，D0 模型仅核对静态消费路径。

## Atomic 与预算边界

A07.3 子集只允许 call/let/assert；call 必须 complete，shape 为 state_edit/candidate_read/pure_compute。拒绝 ticket、Await、If、ForEach、Parallel、嵌套 Atomic、Lifecycle 和 ExternalEffect。全部操作同 provider/domain；没有 target 时只继承本组 domain，独立 StateEdit 必须显式 target。动态 domain/revision 绑定输出 `DynamicCheckRequired`，实际运行准入重新验证；Check 不执行业务效果、不提交、不证明动态权限。

默认静态节点总量与单块 steps 上限 1000，Atomic steps 128，控制深度16，Parallel 分支32/并发16，实际总指令10000。foreach 固定集合，超过 max_iterations 失败而非截断；每次迭代的控制步骤也计费，空 body 不免预算。嵌套分支和循环不得借单块限制无限展开。模型 Meter 明确拒绝总指令、槽/结果字节、控制深度及时间单位超额，反例分别验证；生产实际 allocator、单调时钟、重试和 continuation 计费由 D4 验证。

模型对静态控制按实际字面 predicate 计算已知成本，动态分支保守保留预算检查；动态长度在使用前复查。空数组没有元素类型见证时，模型拒绝非空 body 并报告 ElementTypeRequired；这是参考模型输入见证限制，不是产品语言禁止空集合，D4 的 TypeContract 将提供元素类型。Assert(false) 在 Check 可完成类型检查，执行时停止由后续 Runtime 合同落实。

成功 Plan 统一 PlanCompleted，含 exports、有序事实及 child 已收尾；不造全局 CommitId。未知必要效果优先 Indeterminate，已知部分完成为 PartialCompletion，先前只有纯读且无效果可 FailedBeforeApply。Outcome 聚合使用 D0.03 合同，本包不重复制造第二套 Outcome。

## 样例与验证

`examples/plans/settings-atomic.plan.json` 是无文档配置域，直接遵守 A08.8；`compute-apply.plan.json` 展示计算候选后独立显式 target 应用。示例操作尚未在真实二进制注册，不扩展 GUI、CAD/CAM、设备模块。

`tests/plan/golden/nodes.json` 固定每节点正反两项（16条），Schema 与语义错误分别验证。实际逐项入口 `tests/plan/run_cases.py` 从源函数发现，固定预期另存 `tests/manifests/d0.04.expected.json`。独立证据在 `evidence/bootstrap/D0.04/`；包级状态及人工评审由主集成资料记录，自动绿测不等于批准。


独立审查补充：绑定前的domain字面量可省略叶，但绑定完成后必须使用同一domain Schema恢复provider/id必需约束并完整校验；即使Atomic只有pure_compute也不能跳过。provider/id必须是非空、最多96字节的规范ASCII名称，拒绝未知字段、非对象、null、数字、数组、超长或非ASCII值。测试目录中StateEdit target声明为同样的provider/id合同，根绑定及字面target均使用该完整校验；Schema合格仍不代表实际provider/domain/目标已授权。

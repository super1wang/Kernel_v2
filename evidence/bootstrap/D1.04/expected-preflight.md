# D1.04 候选固定集合与验证布局独立 AI 预审

actor_type：AI；review_tools。结论：候选CTest集合的继承、名称及算术正确；初版三CHECK遗漏已在下方增量复核关闭；仍须等待API修订后复审。本预审不是最终Approved，不授权按未闭合API实现，不代表任何D1.04测试已经运行。

## 精确输入与只读核对

- candidate-expected.json：7645460e9705062787ffa38acfcf462804a0cb099210648c65562a34b79a4750。
- D1.03基线expected：e5666284a1d2eb641bea318508cb7450c062673aba2f0a8b3e29f90085129964。
- D1.04原始计划：f49e007e97b1a11f6aa06b96a5bb3e686b8be006b146bd53a3ce7691b277858f。
- D1.04-api-initial.md：c1009a484082f3e3420b2875f04fb7cea8495da60ebb5d600f5b2178828742ab。

通过Python逐对象比较：D1.03全部218个case原样保留（包括profiles、repeat_required），253个总名称唯一；相对基线新增35个名称与计划表正则提取集合精确一致，missing/extra均为空。正式expected_cases函数给出Debug251、Release251、ASan253，总755，均为候选预期。

**必须修正：候选文件目前没有checks字段。** D1.03的CHECK.dependencies.python、CHECK.conformance.bootstrap、CHECK.models.outcome_consistency三个对象未继承。正式expected须原样补回，各repeat_required=1；三正式run manifests须同步保留原argv、每配置实际运行。仅CTest对象继承正确不能称完整检查集合已继承。本轮不修改候选文件。

## 35项独立可运行性

名称与原计划必测行为足够区分测试职责，可按每个名字建立全新PolicyStore/连接/owner/sink夹具，单名称独立执行；不应依赖上一测试留下的发行ID或全局授权状态。下列逐项分组涵盖35项，组内每个名称仍独立注册，编译子命令不增加CTest主体。

| 主体（省略共同.policy.） | 各自主断言及独立性要求 |
|---|---|
| T07.authentication_source / session_isolation / service_principal | 固定认证来源拒自填；相同Principal两独立连接交换失败；独立服务期限与用户断线互不偷借 |
| T07.scope_four_way / delegation_shrink / recursive_delegation_denied / owner_scope | 四方逐源削减；委托收缩且拒扩张/延寿；首层正例与再委托负例；owner过滤不能成为管理权限 |
| T07.target_issued_identity / target_lifecycle / target_frozen_set | 实际发行身份；删除重建旧材料失效；输入别名/选择变更不能漂移冻结目标 |
| T07.operation_exact_contract / group_members_complete / group_substitution | 安装精确政策来源；每成员真实目标全部授权；替换/重排/碰撞不能借旧组材料 |
| T07.permit_origin_binding / permit_once / permit_concurrent | 原动作原发行owner；一动作只能一次发行消费；真实双线程同permit一次获准 |
| T07.revoke_before_consume / consume_before_revoke / cancel_arbitration | 可控先后两种胜出事实，效果调用计数独立；获准尝试不冒充应用或回滚 |
| T07.expiry_boundary / generation_exhaustion / owned_inputs_budget / exception_atomicity | 服务器时钟边界/溢出；身份世代饱和；所有嵌套拥有与预算；失败点无半发行/消费/监听及泄漏 |
| T19.query_field_projection / page_owner_authorization / page_binding_isolation / page_revoke_live_keyset | 真实投影隐藏字段；扫描前owner拒绝；完整分页绑定；页间撤权逐项重验和空页前进 |
| T19.subscription_scope_atomic / subscription_connection_cleanup | 全过滤集授权或零Watch；连接A释放不影响B与业务执行 |
| T20.queued_revoke_drop / transmission_start_arbitration / failed_start_not_started / unknown_start_no_retry / unsubscribe_inflight | 队列旧字节撤权丢弃；真实首字节仲裁；未开始失败重试重验；不确定起点禁止重发；退订与已发字节边界 |
| T20.internal_component_boundary | 实际内部依赖闭包、真实CoreContracts安装正控制及Runtime不可用 |

并发与期限测试必须用barrier/latch或受控服务端时钟，不能用sleep碰运气或改客户端时间。每项失败后独立释放owner/监听/队列，检查不是上一项结果。API尚待修订，故这里只确认职责可分，不能保证当前草案已提供可编译的所有调用入口。

## API初审要求必须落入现有主体的子断言

- scope_four_way：同一来源两规则分别允许(A,read)、(B,write)，必须拒(A,write)；提供真正匹配完整tuple的正例。对四个来源逐一测试，不能各维先求并集后制造授权。owner_scope/page_owner_authorization同时验证预扫描owner候选检查不能取代后续精确operation/target四方授权。
- operation_exact_contract/query_field_projection：非Invoke各AccessUse权限从实际安装映射取得，summary/cancel/result字段用途相互隔离；不能由请求自填所需权限。隐藏字段必须不在实际输出字节/投影内。
- group_substitution：修订后摘要端口实际读取envelope、anchor、完整有序members；改变每一维均有对应控制，强制摘要碰撞仍拒绝不同私有材料，不能只比较digest。
- queued_revoke_drop/transmission_start_arbitration：get和list各自通过真实ResponseAuthorization/PreparedResponse或修订等价接口进入发送协调器，query后撤权、入队后撤权、start前撤权均0新字节；不能伪造Watch替代响应许可。响应不能替换为用户自填payload。合法get/list响应在未撤权时确实送出同投影字节。
- failed_start_not_started/unknown_start_no_retry：预留容量失败发生在仲裁外；start返回真实NotStarted/Started/Unknown，Started必须实际写入sink字节，Unknown阻止自动重试。原响应普通值不是无限期发送凭证。
- page_binding_isolation/subscription_scope_atomic/owned_inputs_budget：完整restore/host当前可信元数据、Watch不可复制/发行来源、订阅配额及关闭连接计费释放，按修订API逐字段正反例；无restore配置须明确拒绝矛盾材料，不能悄悄忽略身份。

## 建议嵌套编译与安装控制

建议在authentication_source、target_issued_identity、permit_origin_binding、group_substitution、subscription_scope_atomic及transmission_start_arbitration对应主体中，按最终API私有/只读边界分别建立合法发行/只读端口正控制，再编译拒绝直接构造、复制私有发行身份、提取内部记录或修改只读材料。传输端口合法派生实现须真正能访问已预留材料和只读帧，响应入口也必须有可编译正控制。不要统一使用不涉及受检入口的空main“正控制”，也不要只靠类型traits冒充真实编译拒绝。具体wrapper主体数须待API定稿决定，不在此猜最低数量。

internal_component_boundary应在实际生成target metadata中核对ock_policy_internal仅链接CoreContracts/Foundation闭包，不依赖Registry；真实cmake install后，CoreContracts消费配置成功，Runtime请求按具体诊断失败，并核对内部policy头/lib未进入安装树。保存安装真实CMake导出及必要头，不只留stdout。

## 未来归档布局建议

沿用tests/contract/authorization/child-runs/<唯一ID>/：commands.json逐命令记录owned状态及raw SHA；stdout/stderr、生成cpp/CMakeLists、CMakeCache、CompilerId/ABI、tlog、合法控制obj全部归档。安装主体另保留install/include及lib/cmake/OCK、CoreContracts/Runtime消费源码/配置、target-metadata/结构断言。三配置目标exe、内部lib、每配置发现文件和policy-target-Config.json列为build_outputs。

并发/发送/响应控制的事件序列、获准次数、效果次数、实际sink字节或字节SHA/长度、取消/撤权顺序应作为可复查材料；不能用最终bool替代首字节事实。若日志属单例进程输出，正式JUnit与原始流保留该信息即可，不凭空新增隐藏CTest。

root glob与validator Path.match语义须同时验证：递归模式配明确深度，逐个实际收集路径检查可匹配。最低commands/日志/cpp/obj/安装材料数量由固定最终wrapper布局计算，在正式运行前冻结；失败重跑用新目录。旧D1.03所有runtime patterns/minimum与三CHECK原样继承，不能为新增集合清理旧回归。

以上仅候选预审建议；API修订后应以新SHA复核tuple/响应/预留/身份完整声明和最终expected，再批准首轮行为实现。只新增本意见文件，不修改源码、实际expected或提交。

## 候选修订增量核对

主任务保留candidate-expected-initial.json原字节，SHA仍为7645460e9705062787ffa38acfcf462804a0cb099210648c65562a34b79a4750。当前candidate-expected.json SHA为09f068de7dd487df4cac888dc84873ea525dd0e33d0e517ba21d07fec2518469。再次只读核对：唯一JSON结构变化是添加checks数组，三个对象与D1.03逐对象完全一致；其余scope、253个case及属性均未改变。初版三CHECK缺口已关闭，251/251/253+每配置三CHECK候选完整。上文缺项描述是初版发现记录，不表示修订后仍缺失。

当前结论仍为Pending最终API修订复审；不将候选集合或本增量核查称为实际运行Passed。

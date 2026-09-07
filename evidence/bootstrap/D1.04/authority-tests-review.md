# D1.04 authority新增控制独立 AI 复核

- actor_type：AI；review_contracts。
- 结论：ChangesRequested（仅剩下述两处别名反例有效性）；认证伪造、共同tuple及列明owner拒绝控制认可。不作整包结论。
- 冻结证据：`authority-tests-d7f52d393faf`。实际核对五份source SHA一致，configure/build及三个主体共5命令全部Exited/0，owned Job active_after均0；未重跑构建。
- authority_cases.hpp：`dfa35d63448ac5fd951f11d16ea3828afefc3595f0684e6c43b2cbe95cab335b`。
- policy.cpp：`554ee42fa2fdc527adb771c88cc1ef85d05090621dfcc2f443208d5740508534`。
- policy.hpp：`a6fca851ce64d6985a77b5d543be1b73683b8797f58299c46584f272fff3764a`。
- fixtures.hpp：`a377312b852e6f0139c8ccd29521a33d65403cca7520a7576f37e94ad64e9740`。
- test_support.hpp：`86e94e83c42d0121e787bc7dae6b2f02785570b5f8c3b38d033f620aad66363a`。

## 已关闭的对应缺口

authentication_source通过verify和公开authenticate分别拒绝错误Principal、delegated_by、自报admin/approved/trusted标签；将同description的自制Grant交真实authority.validate和CallerView::check均拒绝。假authority自报validate成功产生的view也真实送入目标解析及固定authority校验后拒绝，最后真prepare成功。覆盖不再停留于错误凭据或Action.issue入口。

scope_four_way对每个来源单独构造(A,read)/(B,write)，A/write拒绝；合法B准备成功。所有缺权控制使用新会话/新caller，避免旧generation先行失效掩盖错误。对每个来源另构造owner1不含Progress、owner2含Progress，同一目标下owner1输出无progress、另Env真实owner2输出progress；还有owner和所需permission错配拒绝。共同tuple和owner/field不拼接缺口可关闭。

inputs_ownership的配置别名是principals向量元素内部的rules引用，若错误搬走principals缓冲仍可通过该引用修改接管后的元素，现修改后prepare成功，是真正的冻结控制。空控制块目标lifetime及auth/clock/digest/source各端口拒绝，ResourceLease/ActivityLease作为目标lifetime拒绝，具有实际入口调用及有效装配正控制。

## 剩余P2：两个变更没有触及可能被接管的返回缓冲

authority_cases.hpp:116–119：aliased_scope引用顶层`scope.rules`对象。若错误实现搬走该vector，源vector成为搬空状态，随后clear仅作用于源vector而非已转移元素，该测试仍通过。应保留`scope.rules[0].permissions`等元素内部成员引用，在open后修改；这样搬走缓冲的错误实现才会受到别名修改影响。

同处`helpers.auth->identity.ceiling.rules.clear()`修改的是认证器原DTO，而fixture的authenticate执行`return identity`已经深复制返回值。即使PolicyStore错误直接move接管返回DTO，原identity也不会影响它。应使用一个可信测试认证器：保存待返回payload的规则元素内部别名，再`return std::move(payload)`；open完成后用保存别名修改，随后新授权仍须基于冻结的原ceiling。该反例不自报授权，仍通过固定凭据认证，只测试拥有语义。

补这两个真实元素别名控制即可，不要求重写已有有效认证/tuple测试。当前未发现因此证实的产品缺陷，结论针对反例不能检出所声明错误的覆盖问题。其余嵌套预算、source响应共享别名、自制TargetView及source/分页/端口异常未由本三主体全部覆盖，继续沿coverage-preflight.md逐项关闭；不从这次green推定全部完成。

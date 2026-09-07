# D1.04 可信观察源寿命追加 AI 规格复核

- actor_type：AI；独立复核者：review_contracts。
- 结论：ChangesRequested，仅追加源事实寿命合同；保留既有修订2批准报告原文。
- 已审 API SHA256：`4b493389f60812eb5a1b1694aa52ac7e666208aa5204dbfbb7f0a3a67287cc6b`。
- 范围：A10/A17 的查询授权到真实发送起点之间的事实有效性；无行为实现，无测试 Passed 声明。

## P2：源事实可以在锁外更新，但没有固定寿命或同步失效合同

位置：`docs/contracts/policy-api.md:296` 的源接口、380 的 identity 检查时点、448–452 的响应发送复验、485 的 ResponseState。

当前合同在 create 和每次查询核对非零 host/Absent，在扫描返回时核对 host 一致。发送前列举会话、目标实例/ACL/lifecycle、字段和期限，未要求源身份当前一致。源接口也未明确同 ExecutionRef 的 owner、精确 operation 和 actual_targets 不可重绑定。因此存在满足当前文字接口的组合：get/scan 读取 host H1、owner A、目标 T1 并发行 response；源随后切换为 H2 或 Present，或将同一 Ref 改成 owner B/目标 T2；PolicyStore 的既有权限和目标世代没有变化，旧 response 的发送复验仍全部通过。锁外再调用一次 find/identity 本身也不能消除其返回到首字节之间的变更窗口。

## 最小补充建议

首版明确把源装配为有寿命约束的可信端口：在一个 PolicyStore 的整个存活期内，source.identity 的 host 非零且固定、restore 恒为 Absent；同一个 ExecutionRef 对应的 owner、精确 OperationSelector、实际目标集合及所绑定实例属于不可重绑定的授权事实。phase/progress 等观测内容仍可变化，不要求冻结业务状态。源事实不能以复用同 Ref 的方式改变。

首版可以明确不支持改变上述身份/授权事实：需要切换 host、restore 模式或重绑定授权事实时，必须先通过原 Store 的同步关闭/会话失效路径完成仲裁，永久封住旧 Store 的查询和发送，再装配新源/新 Store；Present 仍明确拒绝。关闭之前不能先切换源。若决定支持在线变更，则必须另外给出受信更新入口，使事实替换和相关 Response/Watch 失效在同一发送仲裁内线性化，不能依赖锁外读取或异步通知。两种设计选定其一即可，无需增加生产 Host、执行索引或持久恢复实现。

固定用例需覆盖被选择的合同：源切换只能在旧会话关闭仲裁后发生，旧 response/watch 此后零字节且新 Store 不认旧发行对象；或者在线变更先赢则旧帧零字节、发送先赢则保留已开始的历史事实。使用受控步骤，无需扩大现有测试框架或伪造已实现证据。

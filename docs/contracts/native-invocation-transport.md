# D1.05 Native 结果运输现行约束

本文件将已独立审核的运输增量纳入源码输入摘要。原始审核和失败历史见 `docs/reviews/D1.05-transport-amendment.md`。

仅本包 Native 绑定限制可能抛出移动的非 void 结果；Registry 和 CoreContracts 的通用合同不变。非 void 的 R、Result<R>、Outcome<R>、Result<Outcome<R>> 和 InvokeReply<R> 均须具有无抛出移动类型特征；不满足时在 typed bind、Policy 准备或 Handler 之前返回 InvalidBinding。

void 是经独立源码复核的例外：锁定 tl::expected 1.1.0 的异常说明使用 void trait，导致上层保守标记，但成功分支没有 R 对象，错误分支只移动 Error。因此 void 且 Error 无抛出移动时允许绑定。测试同时覆盖成功、业务错误与 Handler 异常；非 void 的全部约束保留。

统一栈退出观察记录只覆盖实际返回构造；不声称捕获 noexcept 违约。依赖运输负控制由独立子进程 terminate handler 输出 `locked_result_noexcept_transport_terminated` 并退出 86，不能计为可恢复业务成功。固定小定长无资源声明的 A21.5 稳态窗口及其他规划范围保持不变。

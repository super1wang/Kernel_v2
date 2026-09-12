# Settings State 验证消费者

本程序是 B6 的无 Document、无数据库验证消费者。它注册一个真实 `StateEdit`，经 Policy、Registry 和 NativeEngine 更新内存配置；随后用同一绑定进入现有 managed `InvocationRecord` 再提交一次。两次结果都必须是带 `CommitFact`、`PublishedFact` 和有效发布证明的 `StateCommitted`，最终 Snapshot revision 为 2。

固定认证字节、身份和本地端口只服务本进程验证，不是产品认证方案。程序不启动 GUI、CAD/CAM、设备或后台线程。

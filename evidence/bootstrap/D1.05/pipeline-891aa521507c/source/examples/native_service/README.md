# Native 验证消费者

本目录是 D1.05 规划要求的独立进程消费者，通过 Registry 注册 Compute、Read 和 StateEdit，再经 NativeEngine 绑定与调用。源码自带有限整数合同、固定可信认证端口、时钟和空观察源，不依赖测试夹具。

运行成功时，标准输出包含四项实际检查：Compute 得到 7、Read 得到 12、非法输入被拒绝且不进入业务、当前没有执行能力的 StateEdit 返回 ProviderUnavailable 且不进入业务。任何检查失败均以非零状态退出。可信端口只用于本进程验证，固定认证字节不是产品认证方案。

该目标仅在内部 Native 验证构建中启用，不安装为 SDK，不扩展 GUI、设备或其他产品模块。正式验证由 `tests/contract/native/verify_children.py` 在独占 Windows Job 中启动，并归档命令、退出码、原始输出和进程树清理结果。

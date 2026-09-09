# Embedded 公开 SDK 验证消费者

本消费者仅使用 Runtime/CoreContracts 与 CpuPool 的安装公开头，固定两个 workers；Host 装配同一执行服务，不链接 Data、Dynamic、Control 或 LocalIPC。可信认证端口仅服务本进程演示，不提供外部认证入口。

生产构建选择 `OCK_BUILD_COMPONENTS=Embedded`，依赖选择必须为 `OCK_DEPENDENCY_COMPONENTS=Foundation;CpuPool`。安装后，将安装目录设为 `CMAKE_PREFIX_PATH`，独立配置本目录即可构建 `ock_embedded_service`。

消费者对同一个注册的 typed Compute 连续执行 40 次 Invoke/Submit 对比，提交后修改调用者输入，确认异步结果仍对应原始值；终态上限 16、记录上限 32，运行跨过缓存保留窗口。独立 structured 操作使用单容量独占资源，验证 child 的 WaitingResources、父返回后的 WaitingChild、父取消向 child 传播及两份最终结果；通过公开接口读取默认内存日志，最后验证 Host quiescent 与模块停止。固定屏障只用于消费者验证，有超时与失败释放。它证明实际任务/资源/父子/取消接线，不代替线程峰值或 G3 footprint。

`python -X utf8 tests/install_consumer/verify_embedded.py` 使用锁定工具链，检查实际依赖取得及目标图，构建并安装生产库，迁移安装位置后独立编译本消费者，同时验证 Data 不可用与链接 map。运行环境的 CMake 必须符合仓库锁定版本。

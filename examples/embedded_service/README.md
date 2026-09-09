# Embedded 公开 SDK 验证消费者

本消费者仅使用 Runtime/CoreContracts 与 CpuPool 的安装公开头，固定两个 workers；Host 装配同一执行服务，不链接 Data、Dynamic、Control 或 LocalIPC。可信认证端口仅服务本进程演示，不提供外部认证入口。

生产构建选择 `OCK_BUILD_COMPONENTS=Embedded`，依赖选择必须为 `OCK_DEPENDENCY_COMPONENTS=Foundation;CpuPool`。安装后，将安装目录设为 `CMAKE_PREFIX_PATH`，独立配置本目录即可构建 `ock_embedded_service`。

消费者对同一个注册的 typed Compute 连续执行 40 次 Invoke/Submit 对比，提交后修改调用者输入，确认异步结果仍对应原始值；终态上限 16、记录上限 32，运行跨过缓存保留窗口。独立 structured 操作使用单容量独占资源，验证 child 的 WaitingResources、父返回后的 WaitingChild、父取消向 child 传播及两份最终结果；通过公开接口读取默认内存日志，最后验证 Host quiescent 与模块停止。固定屏障只用于消费者验证，有超时与失败释放。它证明实际任务/资源/父子/取消接线，不代替线程峰值或 G3 footprint。

`python -X utf8 tests/install_consumer/verify_embedded.py` 使用锁定工具链，检查实际依赖取得及目标图，构建并安装生产库，迁移安装位置后独立编译本消费者，同时验证 Data 不可用与链接 map。运行环境的 CMake 必须符合仓库锁定版本。

`fixture.hpp` 是普通样例与占用消费者共用的实际执行流程。普通 `main.cpp` 使用空 hooks；占用消费者在真实作用域边界发出 Ready、shutdown、Bound/Session/最后 owner 释放标记。`tools/footprint/embedded_pilot.py --prefix <Embedded安装目录>` 只采集一组 ABBA pilot，不批准预算、不声称连续线程峰值或正式延迟。

QPC `construction_ticks/qpc_frequency` 从构造阶段握手返回后开始，到 Host Ready 校验后、Ready 握手之前结束；两次采样之间没有启动器等待。原阶段 `internal_ticks` 保留为含握手的阶段间隔，不能代替直接窗口。`--config Release` 必须使用实际安装的 Release 库；默认仍为 Debug。

`--allocation` 默认用于 Debug CRT；ASan 使用独立插桩的 Embedded 安装树，加 `--asan --config RelWithDebInfo --allocation`。保留 12 类分配正探针与负探针，另用真实后台线程验证对应进程 allocator hook，ASan hook 注册失败即失败。全部 40 个 Invoke 窗口同时要求调用线程 C++ 与进程 allocator 分配为零；v2 记录明确标记 `DebugCRT` 或 `ASan`。40 个 Submit/wait/result 和资源父子取消窗口单列实际计数及 C++ 保留字节，未承诺零分配。私有 DLL/custom heap 不在覆盖声明内；计数模式的体积、线程与时延不能替代正式无计数测量。

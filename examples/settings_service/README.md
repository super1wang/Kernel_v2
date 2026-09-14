# 无文档 Settings 内核消费者

样例只使用安装公开头和 OCK::State、OCK::Runtime；B6Subset 另外链接 OCK::Adapter::CpuPool。显式启用 Host State、注册单次 StateEdit 与有限 Atomic 组；组的第二步绑定第一步类型化结果，共享一个候选与一次发布。所有请求要求明确 revision/lifecycle。

B6Subset 经过公开 HostBound.submit、实际资源集合、线程池、执行表、wait/result 和 Host shutdown；StateNative 使用同一 HostBound.invoke，无线程池与资源配置。不再使用内部 InvocationAccess 或 run_once 模拟 managed。

迁移安装消费者与本例使用相同公开实现，验证两步结果为 8/9、正式值为 8、revision 仅增加 1。认证、摘要和目标配置是本地验证装配，不暴露远程写 RPC，也不代表设备或持久化验收。

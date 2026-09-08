# Kernel_v2 · Open Command Kernel

按 v3.3 规划从零建设的 Windows x64 / C++20 开放命令内核。D1.01–D1.05 已通过验收；本次交付 D1.06 的 Host、内存 Logging、NativeSubset 安装消费者及测量工具开发代码。

**按用户最新要求，本次只做开发提交，停止后续测试，提交推送后暂停。D1.06 未完成正式验收，G1 尚未放行。** 既有局部运行只对各自冻结源码有效，不能替代最终提交的完整测试。

- [当前进度与工作包状态](docs/progress.md)
- [本次开发交付与未验收范围](docs/validation/D1.06-development-delivery.md)
- [D1.06 审核索引](docs/reviews/D1.06-index.md)与[既有局部验证记录](docs/validation/D1.06.md)
- [唯一架构规范](docs/01_Architecture_v3.3.md)与[唯一执行计划](docs/02_Execution_Plan_v3.3.md)
- [提速及 Token 精简策略](docs/plans/D1.06.md)
- [G0 历史验收](docs/validation/G0.md)与[证据规则](evidence/README.md)

当前 NativeSubset 支持真实 Read/Compute 注册、会话授权与绑定调用；未装配的异步能力明确拒绝。SDK 的运行组件为静态 `OCK::Runtime`，公共合同经 `OCK::CoreContracts` 导出；安装消费形态见 `examples/stateless_service/`。

本阶段仅覆盖内核及规划要求的验证消费者，不开发 GUI、CAD/CAM、设备等产品模块，不迁移旧源码、API 或数据。文档 v3.3 不表示 SDK 已发布 3.3。

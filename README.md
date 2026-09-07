# Kernel_v2 · Open Command Kernel

按 v3.3 规划从零建设的 Windows x64 / C++20 开放命令内核。D0.01 已批准，当前推进 **D0.02 工程边界 / D0.03 完成模型**，尚无可运行内核或 SDK；G0 未通过。

- [当前进度与 64 个工作包](docs/progress.md)
- [唯一架构规范](docs/01_Architecture_v3.3.md) 与 [唯一执行计划](docs/02_Execution_Plan_v3.3.md)
- [需求、不变量与 N1–N11 追踪](docs/requirements.md)
- [三个内核验证消费者](docs/consumer-targets.md)
- [D0.01 评审材料](docs/reviews/D0.01.md) 与 [证据说明](evidence/README.md)

本阶段仅覆盖内核及规划要求的验证消费者，不开发 GUI、CAD/CAM、设备等产品模块，不迁移旧源码/API/数据。文档版本 v3.3 不代表 SDK 已发布 3.3。

当前可运行的登记检查（Python 3.11）：

```powershell
python -X utf8 tools/requirements/check.py
python -X utf8 -m unittest discover -s tests/requirements -v
python -X utf8 tools/bootstrap/record.py
```

最后一条会运行前两项并保存本次独立原始命令记录。这些是 D0.01 登记校验，不能计为未来 52 个行为用例、C++ Runtime、端口后端、IPC、并发或性能通过。

实现必须保持前置包与 G0→G8 门禁顺序，自动结果、人工评审及包级状态分别记录。详见 [AGENTS.md](AGENTS.md)。

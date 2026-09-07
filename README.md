# Kernel_v2 · Open Command Kernel

按 v3.3 规划从零建设的 Windows x64 / C++20 开放命令内核。D0.01 已批准；**D0.02 工程边界 / D0.03 完成模型**已实现并通过技术复核，待人工评审。当前有 CMake 合同目标和可执行参考模型，G0 未通过。

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

当前节点交付：[D0.02 评审](docs/reviews/D0.02.md)、[D0.03 评审](docs/reviews/D0.03.md)。版本元数据支持 MSVC 安装消费验证；运行能力组件请求明确失败，尚无可运行的业务内核。

```powershell
python -X utf8 tools/bootstrap/run_package.py tests/manifests/d0.02.expected.json
python -m pip install --target build/python-deps -r tests/model/requirements-validation.txt
python -X utf8 tools/bootstrap/run_package.py tests/manifests/d0.03.run.json
```

以上是开发 bootstrap 验证；真实端口、生产依赖锁和正式证据门禁按后续责任包交付。

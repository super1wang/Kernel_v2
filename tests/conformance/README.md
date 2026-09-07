# D0.06-c Conformance 骨架

固定入口为 `python -X utf8 tests/conformance/run.py`。同一harness接收合法inline mock与故意重复完成的fault工厂；工厂不能更改共同必需集合。manifest记录合同版本、factory身份、mock/fault类别及capabilities；运行报告补充实际源码摘要。

共同规则包含提交所有权、恰好一次完成、拒绝后不持有工作/回调、工作/回调异常边界、排空/关闭寿命，以及包括嵌套inline在内的worker自等/自毁拒绝。可选真实parallel为NotApplicable，其能力被profile要求时必须失败。fault能被正确检出属于harness自测通过，fault自身永远不qualified。

本目录不实现生产Executor，也不代替执行规划后续真实线程、持久化、设备、footprint或后端Conformance。固定共同用例manifest改变必须评审，同步固定expected与证据输入版本，不从发现结果补齐清单。

# 开发证据

当前仅有 D0.01 的 bootstrap 原始命令记录；这不是 D0.06 正式采集器的通过报告。

每次运行 `python -X utf8 tools/bootstrap/record.py` 都创建新的 `bootstrap/D0.01/<UTC时间>-<随机ID>/`，保存：

- `source-inputs.json`：本次输入文件的实际 SHA-256；绑定运行前 HEAD 与 dirty 事实。
- `commands.json`：本次 Python 版本、命令 argv/cwd、起止时间、真实退出/超时/启动异常、原始日志摘要。
- `01-stdout.log` / `01-stderr.log`：需求登记检查的原始输出。
- `02-stdout.log` / `02-stderr.log`：校验器单元测试的原始输出。

这些命令检查不会执行 expected 草案中的 future_cases。`collection_status=Captured` 只表示记录器中列明的命令正常退出且既有输入未改变，不是工作包 Passed；`formal_evidence_status=Incomplete` 明确表示尚未由 E03 正式采集器核对。G0 前必须由 D0.06 导入/重新采集所需材料，不能事后补造缺失的 C++、CTest、后端或二进制事实。

原始规范的两空格 Markdown 硬换行保持不变，`.gitattributes` 仅为这两份原始文件保留相应格式检查例外。测试日志与用户提供的原始规范不得手工改写。

首次 bootstrap 运行发生于基线提交前，源码身份如实指向当时远端初始提交并标 dirty；后续运行绑定实际新 HEAD 与输入摘要，两者不得拼成一次运行。后续提交会改变仓库身份，但该 run 的输入快照可由各文件摘要核对，不冒充提交后的干净构建。

正式 evidence runner、CTest discovery/JUnit、重复轮次/进程树/旧报告/二进制核验、gate summary 属 D0.06，当前没有实现或宣称通过。

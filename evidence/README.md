# 开发证据

当前有 D0.01–D0.03 的 bootstrap 原始命令记录；这不是 D0.06 正式采集器的通过报告。

每次运行 `python -X utf8 tools/bootstrap/record.py` 都创建新的 `bootstrap/D0.01/<UTC时间>-<随机ID>/`，保存：

- `source-inputs.json`：本次输入文件的实际 SHA-256；绑定运行前 HEAD 与 dirty 事实。
- `commands.json`：本次 Python 版本、命令 argv/cwd、起止时间、真实退出/超时/启动异常、原始日志摘要。
- `01-stdout.log` / `01-stderr.log`：需求登记检查的原始输出。
- `02-stdout.log` / `02-stderr.log`：校验器单元测试的原始输出。

这些命令检查不会执行 expected 草案中的 future_cases。`collection_status=Captured` 只表示记录器中列明的命令正常退出且既有输入未改变，不是工作包 Passed；`formal_evidence_status=Incomplete` 明确表示尚未由 E03 正式采集器核对。G0 前必须由 D0.06 导入/重新采集所需材料，不能事后补造缺失的 C++、CTest、后端或二进制事实。

原始规范的两空格 Markdown 硬换行保持不变，`.gitattributes` 仅为这两份原始文件保留相应格式检查例外。测试日志与用户提供的原始规范不得手工改写。

首次 bootstrap 运行发生于基线提交前，源码身份如实指向当时远端初始提交并标 dirty；后续运行绑定实际新 HEAD 与输入摘要，两者不得拼成一次运行。后续提交会改变仓库身份，但该 run 的输入快照可由各文件摘要核对，不冒充提交后的干净构建。

D0.02/D0.03 的有限包装器已实际采集 CTest discovery/JUnit 和产物摘要；正式 evidence runner、完整重复轮次/进程树/旧报告/二进制核验自测、gate summary 仍属 D0.06，当前不宣称该包通过。

原始进程输出按字节保存，`evidence/bootstrap/**` 使用 `-text` 禁止 Git 自动转换 CRLF/LF；提交时同时核对工作树及 Git blob 中的原始日志摘要，保证远端检出仍可校验。

## D0.02 / D0.03 的有限 bootstrap

`tools/bootstrap/run_package.py` 接收两包的运行清单，保存源 ZIP/指纹、实际工具身份、命令退出、raw stdout/stderr、CTest JSON、唯一 JUnit 与构建产物摘要。expected 独立于实际发现；缺项、跳过、失败或运行中源变化会标 FailedOrIncomplete。当前 D0.03 依赖输出还记录隔离 Python 验证库的实际文件摘要。

原始简单 red/模型记录的字段少于最终包装器；缺失历史时间、dirty 或完整输入不能补造，正式状态仍 Incomplete。所有 run 均保留，详见 [D0.02 索引](../docs/validation/D0.02.md) 和 [D0.03 索引](../docs/validation/D0.03.md)。ZIP 是源文件快照，不是构建/运行时发布包。

CTest JSON 与 MSVC 原始输出可能包含行末空格或空行；仅 evidence/bootstrap 下关闭对应空白 lint，不修改原始日志迎合源码格式检查。产品源码的差异检查仍启用。

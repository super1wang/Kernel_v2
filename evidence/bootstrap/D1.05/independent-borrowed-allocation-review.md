# D1.05 非空借用上下文计数增量独立 AI 复核

日期：2026-09-08。规格结论：Approved；代码结论：Approved。本记录追加于 independent-allocation-wrapper-final-review.md，不覆盖第一轮 ChangesRequested 或上一轮 Approved。没有以 AI 结论冒充人工批准，也不单独宣布 D1.05 包级验收。

本轮依据 docs/plans/D1.05.md、docs/contracts/native-invocation-api.md 及 native-allocation-coverage.md，独立审核实施者新增的 borrowed_nonempty_context_only 样本。审核者未实施此样本。本人曾实施 Policy，因此本独立结论不覆盖 Policy；shape_cases.hpp 的新增语义断言由另一独立审核者 review_foundations 负责，本记录仅核对其来源差异。

## 规格与代码核对

相对上一轮 255 输入摘要 1685e9ccdd20ac4edb07e78249fb119970e7b8a89885490fb565a07ae57ca2df，本轮完整来源摘要为 43ad6c2ecd162908932248ba430ab48c86a94a7d213cfca804cd0e7f972a02ef，仍为 255 项。逐项核对清单及当前文件字节后，唯一变化为 allocation_cases.hpp 和 shape_cases.hpp，没有新增或删除；生产源、分配探针、包装器、驱动、示例与此前批准版本完全一致。

allocation_cases.hpp SHA-256：ce92b0b29aad20668740fd65660a3b93e3df404379cc2973dfbd07ac55cef8b7。新增 9 行位于 allocation_other_costs。ResourceLease、CheckedCount 和 Name 的准备在 start 之前完成；WorkContext 的非空借用构造、span 长度及原始指针有效性检查、析构均在 start/stop 之间。stop 之后才收集报告并 CHECK(borrowed_valid && zero(borrowed_count))，报告分配不污染测量。样本名称和中文注释明确是独立栈上下文，不能据此推断 Native 接受非空资源调用；Native 对该范围的拒绝合同没有改变。没有发现待修复问题。

## 独立原始证据复核

使用本轮专用 independent-borrowed-allocation-recheck.py 审计已有原始证据，并保存 independent-borrowed-allocation-recheck.json。此脚本没有重新运行或替代正式 Native 测试。逐一核对来源清单、来源 zip、当前 255 项输入字节、构建产物 zip 与摘要、命令原始流长度及 SHA-256、JUnit 的精确 32 名称和无失败状态。每轮 4 条根命令均实际退出 0；所有子命令都核对 Windows Job 在恢复前已拥有进程且结束后 active_after=0，以及各自预期退出码。

| 配置 | 正式独立运行目录 | Native / 包装器 | 包装子命令 | 分配样本 |
|---|---|---|---|---|
| Debug | integration-debug-eb47edd21d57 | 32 / 8 | 27 | 69 |
| Release | integration-release-871673f28330 | 32 / 8 | 27 | 69 |
| ASan Debug | integration-asan-f65332a33b8b | 32 / 8 | 28 | 69 |

三轮均绑定上述同一完整来源，source_changed 为空。总计 96 个正式 Native 执行、24 个包装器、82 条包装子命令、207 条分配样本。新增借用样本三轮 success=true，C++ 分配/释放与字节增减均 0；普通 Debug CRT=0，Release CRT=null；ASan 原 CRT=0，同时独立 asan_allocations=0、asan_frees=0。非 ASan 的 sanitizer 字段为 null。驻留/峰值在该窗口保持原值，未声称整个进程驻留为零。

同时重新核验原有 40 次完整稳态窗、12 个正入口、注入单 new 的 verified=false/exit1、真实注册槽耗尽 exit87/marker（仅 ASan）、私有 transport 终止 exit86/marker，以及 8 类包装实际执行。ASan 各正入口原 CRT 仍为 0，独立 sanitizer 分配各为 1；该盲区继续显式报告。实际构建项目保留 /fsanitize=address，ASan 工具链请求为 ON，没有把普通 Debug 结果当作 ASan。

本轮新增样本已通过实际运行，不用上一轮的 32 项绿色替代其此前不存在的断言。与上一轮完整独立审核合并后，分配/包装/示例/驱动这一范围在新完整来源上无遗留必修问题；生产与基础语义的总体独立批准仍以其他审核者记录为准。

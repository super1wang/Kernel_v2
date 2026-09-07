# D1.05 基础 CODE 后续运行独立核对

结论：**Approved（AI 运行证据核对，关闭基础 CODE 报告中的新增控制 Pending）**。原 `foundations-independent-code-20260908.md` 保留不改，其 SHA-256 为 `4be9a1c077a9ce3b789d714158cb729242e856d9a6d7666009c1594566745a20`；五个生产文件 CODE Approved 及原 SPEC 输入匹配结论保持成立。本报告不作 human 或全包批准。

本轮独立读取三配置的源码清单、命令、JUnit、结果和原始归档；实际计算每份 255 项规范化源码摘要，均为 `43ad6c2ecd162908932248ba430ab48c86a94a7d213cfca804cd0e7f972a02ef`。source-inputs.zip 与 build-artifacts.zip 中清单列出的每个文件，其长度和 SHA-256 均匹配；五个生产文件及两个新增控制源码的当前字节也匹配。构建前后 source_changed 均为空。

| 实际运行目录 | JUnit | 驱动命令 |
|---|---|---|
| integration-debug-eb47edd21d57 | 32/32，0 failures、0 skipped | 四条 Exited/0，进程树 active_after=0 |
| integration-release-871673f28330 | 32/32，0 failures、0 skipped | 四条 Exited/0，进程树 active_after=0 |
| integration-asan-f65332a33b8b | 32/32，0 failures、0 skipped | 四条 Exited/0，进程树 active_after=0 |

三轮 JUnit 中 `T03.native.resource_lifetime`、`T06.native.outcome_consistency`、`T23.native.allocation_other_costs` 均为 status=run，无 failure；归档的 native_tests.cpp 将其绑定到包含新增子断言的对应函数。前两者不单独输出每条 CHECK 的日志，结论依据归档源码中的无条件 CHECK 与对应 case 成功退出，不虚构逐断言日志。

新增控制实际覆盖：非空借用在 owner 有效时读取视图，owner 释放后 weak.expired 且不再访问失效视图；三个未知事实的精确/置换列表成功，duplicate/missing/foreign/resolved 列表拒绝，以及空/原始/已解析事实集与旧 unresolved 的数量及逐成员等价。

非空借用窗口另外核对了各归档 `allocation.json` 的 `borrowed_nonempty_context_only` 样本，以及其对应子进程命令 Exited/0、active_after=0、success=true 和 checks.verified=true：

| 配置 | C++ 分配 | CRT 分配 | ASan 分配 | 限定 |
|---|---:|---:|---:|---|
| Debug | 0 | 0 | 不可用 | iterator debug level=2，CRT 通道 full |
| Release | 0 | 不可用 | 不可用 | iterator debug level=0，不推断 Release malloc 覆盖 |
| ASan | 0 | 0（非有效覆盖证明） | 0 | iterator debug level=2，CRT 为 unobserved_asan_intercepted |

该窗口只证明独立非空 Borrowed WorkContext 构造、读取、析构在有效计数通道中为零，不声称 Native 支持非空资源执行，也不扩大分配探针的完整覆盖范围。

本轮没有重新运行共享构建；上述结论来自对已完成实际运行的源码、原始证据及归档进行独立核验。此前基础报告列出的新增测试运行 Pending 已关闭，本范围没有剩余验收缺口。

# D1.05 正式 Debug 配置独立 AI 运行复核

结论：**Approved（仅正式 win-msvc-debug 运行证据）**。Debug 报告的自动/单运行包状态均被独立重算为 Passed，不能由此推出三配置矩阵或 D1.05 全包已验收。Release、ASan 与最终门禁仍待齐全。

受审报告：`evidence/a8049df35d74-028ca1c8afab/win-msvc-debug/D1.05/20260907T235925Z-a07bf46d5760/report.json`，SHA-256 `cd162ff112b27110569381db14bc223748df0ba3b613831c9302afa85cd2acf1`。

实现为 `a8049df35d7428e79d89df9b0f2f223fa7002bda`，255 输入摘要 `028ca1c8afab6dfb2b5c3c6758ce4b71f13311957cc78bdc2463e6993a265560`。来源dirty=true保留；此前准备审核已逐项确认255输入与该Git提交字节一致，不能将dirty标记直接解释为实现不匹配。

## 完整报告审计

实际执行只读 `tools.evidence.validate.audit(report_path, None)`，返回 `([], 'Passed')`。核对内容包括证据schema、来源归档、固定manifest/expected、依赖及实际构建身份、构建与runtime归档及最低材料、原始命令流、发现集合、JUnit精确集合/次数/状态、CHECK、复核附件和报告声明。没有重新构建或修改被审证据。

正式单轮283项CTest全部通过，集合与固定预期相等，无缺失、额外、失败或跳过。三项CHECK为 dependencies.python、conformance.bootstrap、models.outcome_consistency，各实际退出0。九条根命令包括版本、配置、构建、三CHECK、发现与CTest，均Exited/0、Job active_after=0。

## 八包装与原始子证据

在上述审计之外，直接打开runtime-artifacts.zip，对八组Native commands.json重新核对27条子命令的原始stdout/stderr长度及SHA-256、精确预期退出码、WindowsJobObject、assigned_before_resume=true与active_after=0：

| 包装 | 子命令 | 独立核对 |
|---|---:|---|
| private_dispatch | 9 | 正编译成功；五项私有访问/裸handler负编译仅出现对应源码与C2248/C2039；运输probe退出86且有固定terminate标记 |
| no_self_wait | 7 | 正编译成功；四项阻塞/强制内联入口负编译为对应源码C2039 |
| component_boundary | 4 | 主体与安装成功；CoreContracts消费者配置成功；Runtime配置退出1且有精确未实现标记 |
| example_consumer | 2 | 独立demo实际compute/read/invalid_input/provider_unavailable四项检查为true |
| allocation_probe | 2 | 主体成功，真实单次分配注入退出1且verified=false |
| allocation_steady | 1 | 主体成功，40次完整稳态样本符合零通道及驻留检查 |
| allocation_governance | 1 | 主体成功，各治理样本success=true |
| allocation_other_costs | 1 | 主体成功，首用/可变结果/非空借用/最终回收样本核对 |

27条子命令均按各自预期结果结束，预期非零退出没有被误记为进程异常或真实业务成功。归档 runtime SHA-256 为 `2d53af0938fe241ccd29a33e45768633e744503b28c5a12f289809b68c7b9e5b`。

## 分配通道

四份allocation.json合计69个样本，逐份与对应原始stdout的JSON一致，checks.verified及所有success为true。Debug iterator level=2、CRT effective=full，ASan关闭，ASan字段均null。

12个入口分别观察到CRT正事件；前8个C++ new入口另有C++正事件，malloc/calloc/realloc/aligned_malloc四项C++事件为0；独立无分配负窗口两个通道均0。注入单次分配实际C++=1、CRT>0，测试按预期拒绝，不能把探针失效当零。

40次完整稳态invoke均C++/CRT分配为0，C++释放为0，窗口驻留及峰值不变。非空Borrowed独立上下文窗口C++/CRT为0。首用与可变结果均实际成功，可变结果分配为正；最后Bound释放后的内部状态/Catalog/观察存储样本有实际释放和驻留下降，最后weak控制块释放至少2块。

零分配结论限本配置、当前线程、已校准通道和既定固定小值/有限预热窗口；非空Borrowed样本是独立栈上下文，不表示Native非空资源执行已支持。本报告不批准Release CRT或ASan覆盖。

当前本配置没有剩余证据缺口；等待另外两配置完成后另作统一实现身份及最终包级验收。

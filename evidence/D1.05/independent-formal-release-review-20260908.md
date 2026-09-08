# D1.05 正式 Release 配置独立 AI 运行复核

结论：**Approved（仅正式 win-msvc-release 运行证据）**。本配置无剩余证据缺口；ASan与最终统一门禁尚待完成，不批准D1.05全包。

受审报告：`evidence/a8049df35d74-028ca1c8afab/win-msvc-release/D1.05/20260908T001937Z-799b5f40252d/report.json`，SHA-256 `e70a92bc34d039e8e85eb75b3d63680406dc4c2bfead934106c9310a2baa7ab4`。

实现为 `a8049df35d7428e79d89df9b0f2f223fa7002bda`，255输入摘要 `028ca1c8afab6dfb2b5c3c6758ce4b71f13311957cc78bdc2463e6993a265560`，与已审正式Debug相同。原dirty=true保留；此前准备复核已逐项确认全部255输入与该Git提交字节一致。

## 完整证据核对

实际执行只读 `tools.evidence.validate.audit(report_path, None)`，返回 `([], 'Passed')`。该审计重新核对schema、来源与构建归档、固定manifest/expected、依赖及实际构建身份、runtime归档最低项和所有清单字节、原始命令流、精确发现/执行集合、JUnit、CHECK及复核附件。没有重新构建或修改原始证据。

实际单轮283/283 CTest，无缺失、额外、失败或跳过；三CHECK dependencies.python、conformance.bootstrap、models.outcome_consistency全部通过。九条根命令均Exited/0、Job active_after=0。原报告自动与单运行包状态Passed经独立重算成立，不能扩展为三配置门禁通过。

JUnit SHA-256：`a008fbffbc4eece824237c6fe29cc774cbdef6c0f60cbbcfed96f5c5642a75b5`。runtime-artifacts.zip SHA-256：`4ab8c306ef0ee8db5b7b8abfcbeda8fb26a0ff9cd9f2d92947bd29729241301a`。

## 八包装及27条子命令

在完整audit之外，直接读取runtime归档，逐条校验Native子命令两份原始流的长度/SHA、精确退出码、WindowsJobObject、assigned_before_resume=true和active_after=0。

| 包装 | 子命令 | 实际控制 |
|---|---:|---|
| private_dispatch | 9 | 正编译成功；五负编译出现且仅出现对应源码的C2248/C2039；运输probe退出86并有固定terminate标记 |
| no_self_wait | 7 | 正编译成功；四阻塞/强制内联负编译为对应源码C2039 |
| component_boundary | 4 | 主体、安装、CoreContracts配置成功；Runtime配置退出1且有精确未实现标记 |
| example_consumer | 2 | 独立demo四项compute/read/invalid_input/provider_unavailable均为true |
| allocation_probe | 2 | 主体成功；实际单次分配注入退出1并verified=false |
| allocation_steady | 1 | 40次完整稳态窗口通过 |
| allocation_governance | 1 | 治理样本成功 |
| allocation_other_costs | 1 | 首用、可变结果、非空借用和最终回收样本成功 |

预期非零结果按各自合同核对，没有把编译失败/运输终止/分配注入拒绝冒称为业务成功。

## Release分配边界

四份allocation.json共69个样本，与各自原始stdout JSON完全一致，checks.verified与全部success为true。iterator debug level=0；debug_crt_effective=unavailable；所有样本CRT、ASan allocation/free均为null，没有将不可用通道记成零。

前8个C++ new入口分别观察正事件，后4个malloc家族入口C++事件为0；后者不构成Release CRT覆盖证明。无分配负窗口C++=0；真实注入C++=1并按预期拒绝。40次完整稳态invoke均C++分配/释放为0，驻留和峰值不变。非空Borrowed独立上下文C++=0；首用与可变结果实际成功，可变结果分配为正；最后Bound释放样本有实际释放及驻留下降，weak控制块释放至少2块。

本结论限Release已校准C++通道、当前线程及固定场景；不证明Release malloc零分配，不声称ASan覆盖，也不把非空Borrowed独立上下文视为Native非空资源执行。等待ASan及最终门禁后另行做完整包验收。

# D1.03 独立 AI 代码复核

- actor_type：AI；review_contracts。
- 结论：ChangesRequested，剩余1项P2；不因native25通过自动Approved。
- hpp SHA：7f79f13c45ec38968bce552533be8c92d29010eb07aa507abbe55dfe2f87e5ae。
- cpp SHA：62a61c6fe673f3307f4812224a1f5ae92ec3d0f577c2f48158db88fd94a6c514。
- registration_tests.cpp SHA：b2c970975f31bf3ccda859ff404b755980c461cb655a74b79503f952748b8477。
- fixtures.hpp SHA：c972132513f6c1ca506db1680313856ad8f5bb927e0dca838f805c2255ccfb71。
- API SHA及对应字节见本目录spec-review.md/source-inputs.json；源码快照已保存。

## P2：冻结TypeIdentity文本未计入批次总预算

位置：registry.hpp:264的factory调用及随后insert；registry.cpp:329的preflight。

preflight目前核对DefinitionInput及OperationOptions文本，修复了此前权限/affinity/资源引用的遗漏。但D1.02工厂还会读取并冻结TypeContract<A/R>::identity()，其Name与OperationVersion被保存在DefinitionSnapshot内，却没有在工厂后计费。OperationVersion允许受自身max_bytes约束的任意长规范版本，不能假定固定长度。

本目录type_identity_budget_probe.cpp给出精确反例：合法TypeContract返回1028字节规范版本；批次text_bytes=512，manifest/Docs/其余Options均合法。现preflight不会看到该TypeIdentity，insert亦不核对，结果会保存比整个预算更大的类型版本。该源码本轮未编译执行，避免与父任务并发大规模构建；静态路径可证，实际red应由实施者运行归档后修复。

应在工厂返回冻结snapshot后、进入候选目录前检查并checked累加两个TypeIdentity文本；若要在复制前限制可以另加受检工厂支持，但本发现不要求改变D1.02或管控可信TypeContract内部临时分配。反例及合法对照可并入现manifest_owned_budget，不减少或新增工作包。

## 已关闭及其他核对

- 移动缓冲别名：add现在const引用并在异常保护内复制ModuleInput，独立容器消除原元素指针改写；已有std::move后改operations[0]真实控制。
- 跨模块完整性：在钩子前扫描跨模块重复精确key；逐模块以first_slot限定该模块成功登记。另一模块条目不能满足本模块声明。
- 原权限/Options预算缺口：preflight在Definition工厂及资源去重前计费，add亦覆盖实际提供项集合和文本；这部分修复正确。剩余冻结类型身份入口如上单列。
- 重入publish：publishing_短路只置失败，不清理外层仍执行的钩子/模块；RAII guard恢复标志，外层统一释放候选。已有二次嵌套调用及owner在钩子内存活、退出后释放控制，未见同路径UAF。
- 错误台账在可能分配前先置failed；诊断分配异常记truncated；入口异常使批次不可重用。精确服务/provider绑定、配置freeze策略、私有Catalog及冷热owner结构在本轮未见其他确定P1/P2。
- RegistryId CAS在max拒绝不回绕，静态实现正确；当前native测试未实际覆盖耗尽，未虚构该分支Passed。
- 已核对implementation-99202379fa7a/source.json四个核心输入与当前字节一致，commands.json共27条（build/list/native25）均Exited 0。这是命令事实，不是上述遗漏预算反例通过证明。
- wrapper/整体边界由父任务正在集成，本报告不据此宣称shape编译负例、冷热结构检查或SDK门禁已全部最终通过。

# D0.04 实施前规格核对表

依据 A08/A19/A05/A10/A15/A17/A21 与执行卡 D0.04；前置 D0.01/D0.03 已实际 Passed。本文件不是人工验收批准。

- [x] Plan 八类节点逐一正反 golden；拒绝 return、未知字段、无精确版本。
- [x] 单赋值/作用域、exports、一致分支、typed ticket/Await、AtomicResult、DataRef 区分。
- [x] RFC6901、Missing/null、binding 叶省略/父存在/覆盖冲突；动态 Atomic domain 保留 DynamicCheckRequired。
- [x] 无文档 Atomic、compute→apply 样例；静态和实际展开预算、结构化 child 收尾合同。
- [x] 独立三种请求方法、一种服务端推送；共用 ExecutionRef DTO；不是 Runtime/授权实现。
- [x] pending-ack、subscribe→get、已终态/建立中终态、版本乱序、无后续 drop、gap/coalesce 顺序。
- [x] 当前发送授权、跨连接/旧世代退订、不泄露隐藏目标、断线/撤权/限额/慢读。
- [x] HMAC-SHA-256 无状态 cursor、固定 canonical/MAC输入/golden、2KiB/120秒、零逐cursor句柄。
- [x] live_keyset 状态变化/新增/删除、空页推进、篡改/越权/过期/世代/超长/不推进。
- [x] 测试先红后绿，真实日志每次独立目录；expected 固定来源不由发现重建；冻结后交独立复核（独立评审尚未完成，不能视为人工批准）。

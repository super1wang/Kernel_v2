# D0.04 实施者自查

本文件是实施者核对，不是独立审查或用户批准。自动、包级、人工状态分开记录；当前包级 InProgress，独立/人工评审 Pending。

1. 逐条核对 A08 八类节点、A07.3 Atomic 子集、A17 建立时序与列表、A21 初值；Plan/Control Schema 分离，没有return/DSL或有状态cursor分支。
2. Schema采用实际jsonschema 4.23.0 / Draft202012Validator，本地注册Resource解析 `$ref`。ExecutionRef/PrincipalRef/各世代与句柄共用Tagged128规范小写hex，不靠format注释验证，不将ID当授权。
3. 先写固定正反例，再实施模型；初次原始失败中Plan实际为Python保留字语法错误、Control为缺模型导入错误。初次commands中的“预期导入失败”是当时意图，不是Plan实际异常类别；不得把这次启动失败当行为反例。原始日志和commands均未改写。
4. 后续真实行为红测独立保留：edge-red捕获Atomic叶省略、静态短路类型、DataRef类型见证、客户端旧stream、progress间隔与phase关系、有效预算；budget-red捕获空循环控制未计费；validation-red捕获Payload独立预算和非法观察投影部分更新。修复均在对应反例之后。
5. 固定预期48项来自手工规格列表，与源AST发现分别校验；21个Plan case、27个Control case。Plan逐节点16条golden，Control24条固定消息golden及一个逐字节MAC golden。每次验证使用独立目录，没有覆盖失败日志。
6. 明确模型局限：类型见证代替真实操作输出；空集合需元素类型见证；没有真实Atomic提交、业务assert执行、可靠Await调度、C++allocator、Named Pipe、DACL、真实主体或线程仲裁。对应产品合同仍由D1–D4验证，D0没有伪造Runtime Passed。
7. 规范容量为服务器可收紧的起点；队列采用编码大小加128字节逻辑元数据见证，不等于生产内存测量。list模型排序只验证分页语义，不声称无限历史扫描性能。cursor每Host一个随机256位secret、每cursor句柄/pin=0，固定测试key仅存在golden。
8. 源码/JSON/中文合同UTF-8 LF，未改根CMake、共享tools、Outcome、progress、其他包或Git。已提供tests/plan/run_cases.py供主集成复用，后续正式证据由D0.06集成采集。

冻结在最后一次48项逐项验证成功后交主代理；最终正式采集前独立审查的修订必须作为新版本复验，不改既有原始输出。

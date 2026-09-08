# D1.06 Host候选独立AI规格复核

结论：**ChangesRequested（冻结前需解决下列签名、顺序及所有权问题）**。这是候选设计审核，不是实施批准；未运行实现测试，不批准预算数字、完整API或D1.06/G1验收。

已对照唯一执行计划D1.06及架构A16、A19、A21、A23。候选保留同一Registry/Policy/Invocation，以HostAdmission与StopAccepting同锁短仲裁、Ready前不开放业务、退出保留未排空依赖、真实NativeSubset安装消费者和无假Task/空Document为目标，方向符合规范。未要求补完整异步任务体系。

## 冻结前必须收口

### H1：失败start的异常清理保证不完整

候选35行要求start失败“在返回前”自行撤销局部资源，Host只stop已成功start模块；111–112行又要求测试start抛异常。若模块已取得部分资源后抛异常，该模块不进入Host逆序清理链，现有签名也没有部分启动/未排空结果，无法判断后续释放依赖是否安全。A16.1要求按实际成功步骤清理，不只按正常返回的模块计数。

冻结时需二选一明确：start对返回错误及任何允许越界异常都提供同一强清理保证，并以模块内部RAII及部分取得资源后抛异常控制验证；或Host在进入start前登记可清理节点，签名明确失败模块的清理/未排空状态。不得把“catch异常”本身当成清理证明。无法兑现同步强保证的可信端口违约须有明确隔离/终止政策，不能以失败状态继续销毁依赖。

### H2：必需端口验证顺序与ModuleContext借用冲突

22行ModuleContext已经借用LogPort&；51–56行HostPorts是可为空或无真实控制块的shared_ptr；105行却把“校验全部必需端口已可用”放在模块start之后。按文字顺序，start可能在日志/可信端口未验证时取得引用。

应在create或进入任何注册/start/日志回调前检查必需端口非空、真实共享控制块、允许能力及owner归属；把启动后的检查明确为“已验证端口的运行就绪状态”，不能承担首次指针有效性校验。还应明确log外部提供/默认内部创建的选择及其启动失败清理次序。正负控制需证明非法端口情况下模块start与业务均零进入。

### H3：shutdown_until截止与非quiescent重试没有完整合同

66行只有deadline参数，27行stop没有deadline；97行只规定等待准入的condition_variable截止，35/99行则允许stop非quiescent或抛异常后重试。当前无法判断截止后是否继续进入下一个模块stop、是否在单次shutdown内反复重试，以及同步stop超过截止后的报告含义。

应冻结：使用真实steady_clock；等待准入及进入每个新清理节点前如何检查截止；每节点单次调用还是允许有限重试；超时/非quiescent时保存的逆序游标和未释放owner集合；再次shutdown不重复已完成节点。同步用户回调不能由condition_variable超时抢占，必须如实写出“端口须短且不阻塞”的信任边界及违约处理，不能宣称任意stop可被deadline强行中断。增加截止在准入归零前后、两个stop之间及非quiescent重试中的控制即可，不需新增异步任务系统。

### H4：Session外壳的RAII及可重入调用的局部所有权未冻结

71–87行HostSession只有close且允许移动，未定义析构和移动赋值是否关闭旧会话。107行NativeEngine按Session持有底层权威；现有SessionAuthority析构才自动close，而旧HostBound/Engine可以继续持有它。因此“销毁Session外壳”不能自然等价于“最后底层owner销毁并撤权”。

需明确Session外壳销毁/移动覆盖是否关闭会话、close幂等及失败政策、已交付Bound后续invoke的预期；若允许Bound延长会话可调用寿命，必须明确披露及计入有限session预算。95行只写HostBound成员持有材料，不能证明对象在自身回调中被移动覆盖/销毁后继续访问成员安全；invoke/open/verify/bind以及生命周期入口在首次可重入回调前须保存所需局部owner/值，回调后不再依赖可被替换的this成员。保留Handler自毁NativeHost的明确fail-fast边界，不能将其描述为可恢复的正常退出。

### H5：公开撤权验证缺少合法操作路径

87行不提供PolicyStore/administration/Engine getter，公开HostSession也没有收窄委托入口，但116行承诺真实HostSession权限/目标/撤权验证。内部创建并持有PolicyStore时，仅传入初始PolicyConfiguration与认证端口不能实现运行中主体策略替换；close只证明会话关闭，不等于主体撤权或生命周期变更。

冻结时明确这些控制的合法触发路径：可以保留一个严格受信任、用途有限的管理面，或把具体主体撤权回归归属既有Policy/Native测试，并准确限定新增Host层只验证其可达的close/准入拒绝行为。不能为了测试通过公开裸administration或将close测试改名为主体策略撤销。Host每次调用仍必须进入原Native/Policy治理，不能以初次verify缓存取代。

## 合并时已确认方向与保留条件

主集成补充的native_types.hpp公开窄预算/线程端口/投影/选项，以及host/registry/policy/native_types/logging统一experimental、invocation/private_bridge独立detail登记，能解决候选公开签名对detail类型名称的待定位置；最终需以实际完整声明与可搬迁include链复核。保留D1.05实际运输限制和私有真实分派访问控制，不因Host包装扩大可安全运输结果类型。

Runtime成为真实STATIC与NativeSubset可用会有意改变原“Runtime未实现”安装断言。使用明确受审阶段迁移、保持既定主case、区分历史CoreContracts基线夹具与当前NativeSubset消费者，是合理方向；不能声称旧断言在新SDK上仍原样通过。0.1.0-dev.2是开发候选版本决定，仍需A19声明/manifest/宏/target门禁，无上一正式发布时如实记录NA。

还须在合并API中固定Constructed/Configuring/Failed且未启动模块时的shutdown与析构状态表、Host/Policy/每session NativeEngine预算的聚合乘加、准入释放早于业务模块stop且晚于最终返回对象构造、日志停止/flush与owner最终析构顺序。候选已提出这些目的，未把此清单当成额外实现范围；它们应成为上述生命周期与预算合同的精确落点。

没有发现必须增加Task、Document、存储恢复、队列、线程或完整异步Execution实现才能成立的前置条件。H1–H5收口并与Logging/footprint合并后，再审精确API及固定expected；本报告不自动批准后续版本。

## 精确输入SHA-256

| 文件 | SHA-256 |
|---|---|
| evidence/bootstrap/D1.06/host-design-candidate.md | 087fa91b6ebbe5755b2f152b533cdc710e80bbf4fb777cd2e05e9ff790872a90 |
| docs/01_Architecture_v3.3.md | f20644428ed8c307109e26fe690e951f7e191659903bb496ba8dee51125bca2d |
| docs/02_Execution_Plan_v3.3.md | 7daf3c8a7e68462a97cd593d2ab9a35c1888b720428c72bc9f915049050df511 |
| packages/runtime/policy/policy.hpp | 84bbea42de4fa84a910012c8f7841162406bb9e623aa9d294dab06b6f1f44566 |
| packages/runtime/policy/policy.cpp | 92d5c821443a96bbda40fdeb45185b81ca148fd86abe72696e178bf9a57c7481 |
| packages/runtime/invocation/invocation.hpp | 957e1d64342932c9144daf79d25af59142581849159663dd2b448a69717b4bf6 |

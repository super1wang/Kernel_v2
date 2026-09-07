# D1.04 验证驱动与归档布局提案

状态：PendingReview。配合35项候选集合，API修订3未批准前不写行为实现。仅确定验证消费者布局，不增加CTest主体或产品模块。

## 固定5组驱动

| 主体 | 原生之后的真实步骤 | 子命令总数 |
|---|---|---|
| T07.policy.authentication_source | 配置、同接口正编译、直接构造VerifiedCaller拒绝、复制SessionAuthority拒绝 | 5 |
| T07.policy.permit_origin_binding | 配置、合法prepare/issue/consume接口正编译、直接构造ActionAuthorization拒绝、VerifiedCaller提取私有grant拒绝 | 5 |
| T07.policy.group_substitution | 配置、真实envelope/anchor/members只读摘要端正编译、修改members元素拒绝、直接构造GroupSnapshot拒绝 | 5 |
| T20.policy.transmission_start_arbitration | 配置、实现reserve/start及读取const bytes/binding的端口正编译、直接构造PreparedTransmission拒绝、修改bytes拒绝 | 5 |
| T20.policy.internal_component_boundary | 实际install、同安装CoreContracts配置成功、Runtime按明确未实现诊断拒绝 | 4 |

每组先实际运行同名native测试，列入表内总数。四组编译驱动均先运行相同工具链且实际使用受检接口的positive.cpp；其后negative-1.cpp/negative-2.cpp必须Exited非0、无残留并匹配该文件和相应C++诊断，工具链/依赖错误不算合同拒绝。内部边界核对真实target metadata及安装目录无policy内部头/lib，CoreContracts正控制先成功。

## 源与产物

内部STATIC库ock_policy_internal只链接OCK::CoreContracts，消费者ock_policy_tests。build/tests/contract/authorization下生成CTestTestfile.cmake、policy-tests.cmake、policy-tests-Config.cmake、Config/ock_policy_tests.exe、Config/ock_policy_internal.lib及policy-target-Config.json，共6项固定build_outputs。discover明确接收root-build与target-metadata，不猜父目录。

独立child-runs/UUID存commands.json、structure.json、每条stdout/stderr、生成cpp/CMakeLists、CMakeCache/CompilerId/ABI/编译追踪和positive.obj。安装组保留install/include、install/lib/cmake/OCK、CoreContracts/Runtime源与CMakeCache和target-metadata.json。

最低布局：5份commands.json、5份structure.json、24条子命令对应24stdout/24stderr、12份cpp、4份positive.obj、1份安装OCKConfig、1份CoreContracts CMakeLists、1份Runtime CMakeLists、1份target-metadata。沿用递归glob并展开明确层级兼容validator Path.match；首次集成实际产物与这些固定模式逐项核对，不能按发现删减预期。

35项native各自独立夹具。真实双消费、撤权/取消、sender字节长度/内容摘要/0字节反例、Unknown不重试等事件输出保存在同进程stdout和JUnit，不额外造隐藏CTest。原始并发/字节事实不由最终bool替代。

正式清单继承D1.03完整runtime_patterns/minima与三CHECK；增加本包输出和布局最低要求。三配置固定同源码摘要、同提交，顺序执行，所有失败和重跑独立保留。

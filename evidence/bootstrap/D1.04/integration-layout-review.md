# D1.04 具体驱动与归档布局独立 AI 预审

actor_type：AI；review_tools。结论：布局提案可接受，未发现必须改变主体数或最低数量的缺陷；这是验证布局预审，不批准尚在复审的API修订3或行为实现，不声明编译/运行Passed。

提案SHA：1955b10911630697049b28772e450c92da667b1f9344714728c671509634de73。读取时policy-api.md SHA：aa3e06db0dbd858cb75f2ec24e177dd65df77c1b18993726e637a95de6f61b52；对照修订2的具体签名和6.1统一私有构造/删除copy-move规则。source寿命关闭语义的后续修订不改变本提案命令计数，但其行为须在原生主体中真实验证。

## 四个编译positive与八个negative可由现有签名表达

| 主体 | 合法正控制必须实际实例化的接口 | 两个负控制 |
|---|---|---|
| authentication_source | 调用固定PolicyStore::open及SessionAuthority::verify取得shared_ptr<const VerifiedCaller>，再读取view()/authority()；不从DTO直接构造 | VerifiedCaller直接构造拒绝；SessionAuthority复制构造拒绝 |
| permit_origin_binding | prepare(VerifiedCaller,ActionRequest)取得原动作，issue()取得原permit，独立调用current_expected_binding()并将原permit与expected交给consume | ActionAuthorization直接构造拒绝；从VerifiedCaller取得内部grant/State拒绝 |
| group_substitution | 具体派生TrustedGroupDigestPort覆盖fingerprint(const GroupSnapshot&)，其函数体同时读取envelope、anchor_target、members，并使用成员operation/targets | 通过members()修改成员字段拒绝；GroupSnapshot直接构造拒绝 |
| transmission_start_arbitration | 具体派生TransmissionReservation实现capacity及TransmissionStartPort实现reserve/start_now；start_now实际读取bytes()/binding()/projection()；明确reservation受保护基类构造可由合法派生使用 | PreparedTransmission直接构造拒绝；通过bytes()修改std::byte拒绝 |

这些合法路径不需要暴露私有CallerGrant，也不要求GroupSnapshot或PreparedTransmission由测试模块直接构造。positive可以定义接受真实对象引用的函数并作为OBJECT目标编译；不得为绕过私有边界自制State或增设测试专用公有构造。

落实时的具体诊断口径：复制拒绝应指向已delete复制函数；只读修改应指向const成员/const byte；私有访问拒绝应指向实际受审类边界。不能把无关拼写错误、缺头文件或工具链失败当负例成功。每个positive使用相同头集合和具体受检入口，先编译成功，再对两个独立negative源匹配该源文件名与预期MSVC合同诊断。无需为诊断号改变固定CTest主体。

## 计数和布局自洽

四个编译wrapper各native+configure+positive+negative1+negative2=5命令；安装wrapper为native+install+CoreContracts configure+Runtime configure=4命令。合计24命令、24stdout/24stderr；四组三个cpp=12cpp，四个positive OBJECT目标=4positive.obj。5commands.json/5structure.json及安装Config、两个组件源码和target-metadata各1份与提案一致。

六项build_outputs计数正确：CTestTestfile、policy-tests总入口、配置发现文件、配置exe、配置内部lib、policy-target配置JSON。内部库应在提案指定CMake目录定义或显式设置输出目录；真实生成后核验路径，不能仅凭target名字假定lib位置。Debug/ASan使用Debug目录，Release使用Release目录；发现入口必须显式验证CTest配置。

安装wrapper的CoreContracts正控制是与Runtime负控制对应的实际find_package配置成功，不能以只检查安装目录存在代替。须核对实际target metadata是CoreContracts→Foundation闭包、没有Registry；检查policy内部头/lib不安装。提案已有真实install材料，运行时保留其完整导出与所需头，以便复核Runtime拒绝来源。

## 首轮集成时核验，不能由提案替代

- 5个wrapper之外其余30主体仍为独立native用例；固定35新增和全量251/251/253不变。双消费/撤权/响应发送/Unknown控制的真实事实留stdout/JUnit，不能用编译控制代替运行行为。
- 每条命令须Exited、符合预期exit_code、active_after0且未通过终止残留Job伪造正常结束；编译负例非0按合同保留。commands里raw引用应逐项匹配归档size/SHA。
- 每组即使只做编译也要写structure.json；安装组写target-metadata.json。4个positive.obj及真实安装Config按最低数要求实际存在。检查pattern收集集的每个路径均能被validator Path.match接受，递归glob需配明确深度。
- 正式最低计数保持固定预期，首次真实产物只能用来核对声明布局，不能因某负例没有运行就向下改成已发现数量。若API修订确实增加受检步骤，应在行为运行前显式修订布局并重审，而非事后豁免。
- 本包新增pattern/minimum之外，D1.03完整回归档案、原三CHECK、固定三配置matrix与源码身份核对全部保留。

目前无需修改布局提案本身。最终批准仍需API修订3闭合、四个positive和八个negative的实际源码审核、真实首轮产物及三清单最终SHA；本次只新增本意见，不修改实现、expected或提交。

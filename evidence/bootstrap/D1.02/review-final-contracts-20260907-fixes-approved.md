# D1.02 独立 AI 修复最终复核

- actor_type：AI；复核者：独立子代理 review_contracts。
- 结论：Approved（本报告范围内代码及其已冻结规格符合性）；不是 human Approved，不代替包级自动验收。
- HEAD 基线：8cd018c737b7418802b34dd4740c8897ef9825eb；工作区未提交实现按下列 SHA 绑定。
- 前轮 ChangesRequested 和真实 red 目录完整保留；两项 P2 本轮关闭，没有新增确定 P1/P2。

## 精确输入

| 文件 | SHA256 |
|---|---|
| tests/conformance/core_contracts/authority_factories.hpp | 2fd2cb3ebff00b9208b2119495d80d500d2f01decbeb20be5ab5c9f12c4f724e |
| tests/conformance/core_contracts/factories.hpp | f89c124b0f436c856ebb32fcc2bd954feed2b46dc7d78ba1c6839cc601ce349c |
| tests/compile/contracts/test_support.hpp | 86e94e83c42d0121e787bc7dae6b2f02785570b5f8c3b38d033f620aad66363a |
| tests/compile/contracts/contracts_tests.cpp | dd797c52b68a58a1f861b31be161d45883cac497f8c29dc29c8331c911dfdef4 |
| tests/compile/contracts/CMakeLists.txt | fce39769fb037b4dbb06ded52fed01d21d77250bf2a4096c3bdc9cc6817a3a2c |

产品六头实查均与前轮 `target-authority-final-19948aeb83/source-inputs.json` 一致。具体 API 仍为 SHA 34fe36860dd33079f754eda209af9ab0d5093dfcda6ab10c81cbf52a25b53d3d。前轮针对六头、Outcome、当前绑定和零世代的审核结论继续适用于未变文件。

## 关闭依据

1. Scenario 在创建 Context/View 时，私有集合保存该对象的 shared owner 和当次原始 permit。accept 先以协调者当前状态复验，再仅对同一对象找到的原始 permit 消费。替换公共 permit_a 无法改变既有上下文的许可；集合保有 owner，地址回收重用不能冒领旧条目。外部直接构造但未被该夹具保存的上下文不能借用默认许可。
2. Authority/Publication 的具体发行类型和 issued 集合均移入 private；公开入口只能走该测试信任域的 authenticate/publish。实际对象身份集合校验仍保留，未改成凭据自报授权。新增静态断言明确验证发行集合不可公开访问。
3. 新的两种 shape 反例保持伪 permit 与真实 binding 相同，确保 check 成功后才观察真实 consume 拒绝，随后合法上下文仍成功一次。没有通过提前拒绝所有上下文或删除预期使测试变绿。
4. CMake 工厂摘要现在按固定三文件路径及各自 SHA 组成，配置依赖覆盖三文件。已归档 factory-inputs.json 的三项与实查匹配，组合 SHA 为 e275586c46815e9080715224e00cf393c81a45f2b9b27230202dbfe59110ed1e。

## 独立实际复验

本代理新建并执行 `independent-fix-verification-20260907T140715Z`，同 LockedMSVC、标准 include 闭包及独占进程 Job，全部实际 Exited 0：配置、控制消费者编译、合法两种 shape/实际伪 permit consume 拒绝控制、旧 Effect 反例、旧 Transition 反例及原始 private issuance 静态断言编译。

旧两种 shape 反例的调用序列原样保留：先把 permit_a 换成伪对象创建上下文，再换回真实 permit_a 调用 accept。先前退出17，现在退出0且上下文拒绝、consumed=0。旧 private issuance 断言原样保留，先前两条 C2338，现在实际编译退出0。由于公开 Grant/Proof 类型和 issued 已不可访问，旧直接注入分支不再作为 runtime 编译输入；它们的可达前提由原始 requires/static_assert 独立验证关闭。旧 red 证据未覆盖。

另核对父任务 `review-fixes-green-b719840ef2`：2-command.json 构建 Exited 0，native-commands.json 33 条均 Exited 0。该目录首条历史环境失败仍保留，本报告不把它改写为成功。

## 范围限制

Approved 仅涵盖本次六头合同和确定性消费者/工厂的代码与冻结规格审核。工具链整体、SDK安装、最终 Debug/Release/ASan 矩阵和包级自动门禁由主任务分别验收。本轮未扩展生产 Runtime，未移除或减弱零 permission/lifecycle generation 检查。

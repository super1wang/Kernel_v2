# D1.05 基础与 Registry 独立 AI CODE 复核

结论：**Approved（仅本报告列出的五个生产文件的独立代码质量复核）**。先核对原 SPEC 输入未变，再独立复读所有权、类型擦除、异常清理、校验逻辑与私有访问边界；没有发现必须修改这些文件的代码缺陷。本报告不是 human Approved，不批准 D1.05 全包，也不把历史 SPEC 报告重新解释为 CODE 结论。

## SPEC 前置与范围

原 `spec-foundations-20260908-independent.md` 仅提供 SPEC Approved，其 SHA-256 为 `e4100050635be2be352688ec88c96b464cd3081f9792086103f5e9638c63c913`。本轮五个生产文件及 `docs/contracts/native-invocation-api.md` 的 SHA 逐项与原 SPEC 完全相同。API SHA 为 `31757ff610fb162e639c714f85de9655706b7432632dc5599772cc7d0566d796`。因此原静态规格结论仍适用于本轮生产输入；CODE 是此次新增归档的独立复核，不依赖未归档的口头结论。

本轮限 CoreContracts 的 WorkContext/Outcome 增量、Registry typed thunk/owner 增量及 private_bridge.hpp 声明边界。Native 管线和 Policy 实现分别已有其他独立报告；本报告不重复批准它们，也不独立批准分配探针整体能力。

## CODE 审查结果

- `context.hpp:123–175`：拥有构造在 optional 内建立 owners/views，最后才发布共同 span；任何 vector 分配异常均由成员 RAII 清理。借用构造保持 optional 未参与，不实例化 vector 及 MSVC Debug 代理。WorkContext 禁止复制且没有隐式移动，不会因对象移动使内部 span 指向旧存储。借用 span 和指针的寿命由调用者维持，借用本身没有共享所有权或授权含义。
- `outcome.hpp:380–399,564–674`：无分配 helper 保留旧 unresolved 的 UnknownFact/ResolutionRecord 关联语义；完整事实校验先于结果变体校验。九个变体原有业务值、事实、发布证明、证据、BeforeApply、步骤和终结条件均保留。Indeterminate 通过数量、成员和去重联合实现集合相等。重复扫描复杂度为二次且受已验证事实预算约束，没有为无分配而引入临时容器或弱化证明。任意 R 校验器的成本和异常不在 helper 的无分配断言内。
- `registry.hpp:185–231,327–372`：真实函数对象 F 与对应 typed thunk 由同一注册模板建立；恢复 Function、Args、Result 槽的类型与注册约束一致。read 持有实际类型的 Reader owner；candidate_read 恢复真实 pair，只调用 first。未支持的执行形态保存空 thunk。Catalog 不公开真实 hot 条目，公开可命名的 detail 模板不能从其提取真实 Handler。
- `registry.cpp:409–432` 及前置注册校验：read service 的模块、声明和实际类型检查保留；reader_owner 独立于一般 owners，调用时 ReadServices 再保有局部共享所有权。Service/Provider 工厂及 Resource/Executor 模块验证拒绝没有真实控制块的 owner；注册前验证和失败状态阻止发布部分 Catalog。资源计数来自已去重且经过声明验证的列表。所有权转移和失败退出有局部 RAII 保证。
- `private_bridge.hpp:7–28`：NativeEntry 只包含冻结描述与句柄，没有裸 Handler/Reader。inspect/check/dispatch 默认 private，仅授予 NativeEngine/NativeBound 访问；无新增公共跳过逐调用治理的真实分派入口。具体桥方法执行顺序属于管线审核范围。

## 原 SPEC 后续控制的当前落实

| 控制 | 当前源码及实际状态 |
|---|---|
| candidate 真实 first 调用、second 零进入 | shape_cases.hpp:18–27 的 candidate_read_dispatch 检查结果 10、entered=1、candidate_entered=0；已在 native_tests.cpp 注册，旧三配置实际通过 |
| Reader 真实持有与目标关联 | registered_read 释放外部 Reader 后调用；value_cases 的 target_read_association 检查两目标不同实际 Reader 和错误目标拒绝。既有挂接由旧三配置运行 |
| private bridge/裸 Handler 负编译 | verify_children.py 为 dispatch/check/inspect/私有构造检查 C2248、裸 handler 检查 C2039，并先有正编译控制；discover.py 将 private_dispatch 接到包装执行。旧三配置对应包装实际通过 |
| 非空借用资源寿命正负控制 | 新 shape_cases.hpp:62–70 在 owner 有效时检查视图，释放 owner 后仅检查 weak.expired，之后不读取失效视图。resource_lifetime 已挂接；新子断言运行 Pending |
| 两项以上未知事实集合反例及 helper 等价 | 新 outcome_consistency 使用三个 unknown，检查精确/置换合法列表、duplicate/missing/foreign/resolved 列表拒绝；空、原始、已解析三组逐 ID 与旧 unresolved 比较数量和成员。native_tests.cpp 已挂接；新子断言运行 Pending |
| Debug/Release 非空借用构造分配窗口 | 新 allocation_other_costs 的 borrowed_nonempty_context_only 在窗口外准备资源、标签和预算，窗口内覆盖 WorkContext 构造、读取和析构，要求观测通道零分配。已经挂接包装；新实际计数 Pending。该独立上下文控制不证明 Native 非空资源执行受支持；分配探针整体审核由另一审查员负责 |

新增控制的静态设计与挂接 **Approved**。原来缺少的子断言已补入，当前剩余是这些新输入的实际运行证据；不得引用旧 32 项通过将新子断言记作 Passed。

## 运行证据与绑定

本轮未运行共享构建。独立读取旧三配置的 source-inputs.json、commands.json、native-junit.xml：`integration-debug-474c25032e22`、`integration-release-ee8d09f863ec`、`integration-asan-dd8b40540919` 各有四条 Exited/0 命令，JUnit 均 32 项、0 failures、0 disabled、0 skipped；255 项规范化输入摘要均为 `1685e9ccdd20ac4edb07e78249fb119970e7b8a89885490fb565a07ae57ca2df`。这些结果证明旧输入已有 case 的运行，不含新子断言。

本报告新输入规范化摘要为 **`43ad6c2ecd162908932248ba430ab48c86a94a7d213cfca804cd0e7f972a02ef`**，255 项；已读取 `integration-debug-eb47edd21d57/source-inputs.json` 并实际计算摘要，逐项核对下列受审源码。父任务指定的新轮为 `integration-debug-eb47edd21d57`、`integration-release-871673f28330`、`integration-asan-f65332a33b8b`，本报告写入时均按 **Pending** 处理，后续应追加独立运行核对，不改写历史报告。

## 精确 SHA-256

| 文件 | SHA-256 |
|---|---|
| packages/contracts/include/ock/contracts/context.hpp | 326bc830ed564a3489c67465f70ee07fb817a7880d706f990d4a95782e16f5b1 |
| packages/contracts/include/ock/contracts/outcome.hpp | 371115831086ff1afb03cedac709f9c51bc8acf86f92c94886072c098f266dac |
| packages/runtime/registry/registry.hpp | c36f24c7cdb8ad0cc58783882199af2261350e20bf0914578fcc468a1c62bcc0 |
| packages/runtime/registry/registry.cpp | 5e3f62694546c7f6feb92f8dcc7d6f0c960869cd1b1abcacaf110576e80d45e3 |
| packages/runtime/invocation/private_bridge.hpp | 9bda77d55e77b5f81e8aa98d009552008e2f1703217b1ef11b3a1a345e19db10 |
| tests/contract/native/shape_cases.hpp | 32d31b67a2a84c50f47b2f5eb07f2fc2f706d25bf82ed89e4a504e1d1444a2a5 |
| tests/contract/native/allocation_cases.hpp | ce92b0b29aad20668740fd65660a3b93e3df404379cc2973dfbd07ac55cef8b7 |
| tests/contract/native/native_tests.cpp | 151b5bcec1beda868e64efa112cbe7b5dd599fcf66dd55963b3d03a3a8cdceae |
| tests/contract/native/discover.py | 2429d3956f4a4a4d32c5bd34d2eb2c5cd02de5c12d1de233a2dde0f27406122d |
| tests/contract/native/verify_children.py | 5f92a4adce0cc6ca4d406d094e7047a8c67f0f533fd7b153293d1b2b51d99ddd |

最终范围状态：五文件 SPEC 前置仍匹配；独立 CODE Approved；新增控制静态挂接 Approved；新增输入运行 Pending；无包级验收结论。任一受审文件变化后须重新核对。

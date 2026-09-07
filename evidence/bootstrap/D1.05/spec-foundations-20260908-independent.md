# D1.05 基础增量独立 AI 规格复核

结论：**Approved（仅下列源码的静态规格符合性）**。审核类型为 AI SPEC review，不是 human Approved，不批准 D1.05 全包，不替代自动命令结果或后续 AI 代码质量复核。本轮没有运行共享构建，也没有运行 CTest 或分配计数。

依据：先读 docs/progress.md，核对唯一规范架构 v3.3 A04、A05、A21.5 与执行计划 v3.3 D1.05，以及 docs/contracts/native-invocation-api.md 第 4、6、7 节。对当前工作树与 Git 基线的五文件增量进行独立阅读。

## 静态结论

- context.hpp:128–175：拥有构造才 emplace OwnedResources，owners 建好后生成 views，并令共同 span 指向稳定 views；借用构造不构造 OwnedResources，所以 MSVC Debug 中的 vector proxy 也不会在该路径实例化。借用只保存 span，复制保持删除，stop/deadline/预算仍为单个上下文的独立值。借用视图不产生 lease 或授权。显式 BorrowedResourceViews(span) 构造与指定借用机制相符。
- outcome.hpp:380–399、564–674：旧 unresolved() 签名及实现保留；新增 count_unresolved/is_unresolved 仅扫描 KnownFacts 的 span，按原 UnknownFact/ResolutionRecord 关联计算。verify 的九种结果分支保留原事实、发布证明、证据、终结、BeforeApply、业务结果及步骤校验；原临时 vector/复制排序已替换。Indeterminate 同时核对数量、真实未解决成员、候选去重，因而保持集合相等，不能用重复项替换缺项。KnownFacts 工厂唯一性、Resolution 历史验证、追加验证均未删改。结论只涉及框架 helper 本身，不声称任意 R 校验器均无分配。
- registry.hpp:185–231、327–372：注册模板冻结实际 F 对应的 typed thunk；compute 调实际函数，read 按真实 Reader 类型恢复明确 reader_owner 并构造 ReadServices，candidate_read 恢复原 pair 并仅调用 first。state_edit/external_effect/lifecycle 保存 nullptr thunk。模板没有从 Catalog 提取真实条目的能力。
- registry.cpp:409–432：非重复且经过注册检查的资源声明数量保存为 resource_count；Reader 与一般 owners 另设 reader_owner/reader_type。原 service() 的模块限定、声明和类型检查仍生效，Reader 持有真实 shared owner，函数执行时另持副本。原 ResourceBinding/Executor owner 的真实控制块检查仍在 validate_module 中。
- registry.hpp:233–255 与 private_bridge.hpp:16–28：Catalog 的 hot_/cold_ 未公开，唯一新增 friend 为 NativeAccess；其 inspect/check/dispatch 全部 private，只授予 NativeEngine/NativeBound。NativeEntry 是描述材料，没有 Handler、Reader 裸指针或无治理回调。方法命名由草案 resolve/entry 调整为 inspect/check，不改变所需私有性。具体调用顺序及桥实现归父审查员负责，本轮不批准其实现。

## 验证边界与后续验收项

阅读既有 tests/compile/contracts/contracts_tests.cpp 的九变体、追加事实和输出事实测试，以及 tests/contract/registration/registration_tests.cpp 的 candidate 注册合同。当前 tests/contract/native/native_tests.cpp 只有七个实际 case；registered_read 通过重置装配 Reader 指针检查持有关系，但尚未涵盖本范围全部新边界。

在整包自动验收前仍应提供：借用非空资源的寿命正负控制、Debug/Release 实际借用窗口分配计数、两项以上未知事实下 duplicate/missing/foreign/resolved unknown 清单反例与 helper 等价检查、candidate 的真实 first 分派且 second 零进入，以及 private bridge/裸 Handler 负编译控制。本轮这些项目均**未测**，不得引用本报告将其标为 Passed；测试实现与最终证据由父任务统一收口。

没有发现需要修改上述五个源码文件的静态规格违约。

## 本轮精确输入 SHA-256

| 文件 | SHA-256 |
|---|---|
| packages/contracts/include/ock/contracts/context.hpp | 326BC830ED564A3489C67465F70EE07FB817A7880D706F990D4A95782E16F5B1 |
| packages/contracts/include/ock/contracts/outcome.hpp | 371115831086FF1AFB03CEDAC709F9C51BC8ACF86F92C94886072C098F266DAC |
| packages/runtime/registry/registry.hpp | C36F24C7CDB8AD0CC58783882199AF2261350E20BF0914578FCC468A1C62BCC0 |
| packages/runtime/registry/registry.cpp | 5E3F62694546C7F6FEB92F8DCC7D6F0C960869CD1B1ABCACAF110576E80D45E3 |
| packages/runtime/invocation/private_bridge.hpp | 9BDA77D55E77B5F81E8AA98D009552008E2F1703217B1EF11B3A1A345E19DB10 |
| docs/contracts/native-invocation-api.md | 31757FF610FB162E639C714F85DE9655706B7432632DC5599772CC7D0566D796 |

任一受审输入改变后，本报告不自动覆盖新输入。

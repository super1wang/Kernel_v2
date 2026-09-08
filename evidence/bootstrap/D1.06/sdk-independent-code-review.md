# D1.06 SDK 独立 CODE 复核

actor_type: AI  
reviewer: review_foundations（未实施本批 SDK 代码）  
status: ChangesRequested  
范围：统一 Runtime、唯一规范头迁移、安装边界、架构与原生包装修订，以及冻结节点的开发证据；不替代 Host/Logging 全行为、footprint 或包级验收。

## 来源

41 个自有源码逐文件与 sdk-source-identity.json 相等。集合 SHA256：`779f46ea9d0cbee7d27bd423e1b02216769a4fb24785330c131f9e0ce907a465`。已独立重算：owned 按 path 排序，每行含 path/sha256/size，`SHA256(json.dumps(owned, sort_keys=True, separators=(',',':')).encode())`，UTF-8，无末尾换行；不含 shared_installed_headers。完整路径与 SHA 以同目录 sdk-source-identity.json 为逐项输入表，其文件 SHA256 为 `ee25809e82a63dbcf984eb07b9f2daabeee6b3c660517355bcbc166982b0b094`。

sdk-self-review.md SHA256：`8ab906568a743cf34485995a1c805dcba92fd31933d857aae5302bdf7ae6e410`。sdk-raw-text.zip SHA256：`4a36d0580fdf215073920645068cc076525d9832e43c39d6537babb2c2413bc0`；已独立核验 ZIP 全部 4065 成员与 sdk-archive-index.json 的集合、长度、SHA，无重复项。

## 必须修复：P2 安装 detail 来源绕过精确包含边守卫

`tools/architecture/check.py:92` 仅对 `public and not approved` 拒绝同组件 detail 引用；`validate_manifest` 在第 216/229 行附近把安装 detail 源作为 public=False 校验。因此 NATIVE_IMPLEMENTATION_INCLUDES 的三条精确登记仅校验表本身，不能约束 detail→detail 的实际源码。

本次用当前 checker 做只读函数调用，以下两例均实际返回空错误列表：

- source_path=`packages/runtime/include/ock/runtime/detail/host.hpp`，include=`ock/runtime/detail/private_bridge.hpp`；这是未登记但目标真实存在的边。
- source_path=`packages/runtime/include/ock/runtime/detail/invocation.hpp`，include=`ock/runtime/detail/unknown.hpp`；未知 detail 同样未被此守卫拒绝。

调用参数 component=Runtime、public=False，与完整检查对这些安装文件的传参相同。当前生产源码三条边本身正确；问题是 SDK 合同“未知 detail/包含边漂移不得过关”的验证职责未落实。应仅对已安装 Runtime detail 来源也验证精确登记边，保留非安装生产源码内部同组件引用规则；补两条反例及三条合法边正控制。无需重跑完整 SDK 消费或矩阵。

## 实现审阅结论

- 实际 CMake 唯一 STATIC ock_Runtime 编译五个生产 cpp；三个历史 internal 名称均为同 target ALIAS，测试目录删除重复生产库定义。PUBLIC CoreContracts，PRIVATE bcrypt 的安装导出为 LINK_ONLY，未把测试工厂或探针并入库。
- 对照 HEAD 旧规范头逐字差异：Policy 仅迁移注释；Registry 增加 Catalog 自有 module_order 向量/span，publish 保存已有拓扑 order；Invocation 只拆出 native_types 和改 include，模板治理/传输约束未重写。旧路径只转发，NativeAccess inspect/check/dispatch 仍 private。两个源内预算 helper 复用原计量，不安装，Native 原 owner 检查保留。
- dev.2/NativeSubset 元数据一致；其他未实施组件仍 REQUIRED 拒绝。CoreContracts 七头、Runtime 八头及独立 C-A 公共头消费者均存在；真实消费者只链接 Runtime，无源码根 include、手补 bcrypt 或 WHOLEARCHIVE，执行 Read=12、Compute=7、非法输入 handler 不进入和停止门禁。
- 历史 CoreContracts stage 单测仍是从当前 manifest 派生的合成规则夹具，只固定旧 stage/Runtime 类型/六头，不是前版 SDK 字节冻结或兼容证明。真正旧 SDK 拒绝材料是 sdk-stage-e96c22e3ee 冻结源及 sdk-installed_host-0c72d2ee31 的实际 find_package 拒绝；不得混称正式 previous-release 兼容。

## 原始证据核对与适用范围

sdk-summary 的 20 条结果引用已逐项核对文件 SHA 及 JSON 内容。以下九轮安装职责共 64 条 owned 子命令及各自 raw/artifacts SHA 已独立核对：installed_host-21e36661b9、installed_host-d39aba3297、installed_host-51f8de4220、public_headers-63e0c2800e、private_dispatch-fb54aac90a、install_pruning_rejected-4c393e71e2、no_tests_producer-267cce3738、metadata-802cb712c2、link_closure-e2676ce911（目录均加 sdk- 前缀）。全部按预期 Exited，Job 在 Resume 前挂接、active_after=0、未因终止 Job 产生假绿色；负例的非零退出独立保留，不记为正常执行成功。

已读取私有桥真实 C2248、无 handler/engine 的 C2039，以及删除 bcrypt 后 Host 对 BCryptGenRandom 的 LNK2019；公共两 TU 真链接运行、测试关闭生产者、四类裁剪、未实施组件拒绝由实际子命令承载。各运行的 exe/lib 文件摘要与原 artifacts 相等。sdk-final-guards-cc9bc9ced6 的原始 unittest 输出为 48 项 OK；sdk-final-surface-a573cb46db 为 8 项 OK 加真实图检查；sdk-boundaries-8251730a86 四包装命令均 owned Exited0。这些旧成功未覆盖上文新发现的 detail 来源反例。

Release/ASan 证据仅是对应 Runtime 构建及安装消费者专项，不能据此声明各自完整矩阵。各自冻结源、CRLF/LF 字节、driver 版本保持其原始事实；不把这些不同节点拼成当前 41 文件统一重跑。后续 Host deadline 修复不在旧 SDK 运行快照，Host 的完整行为及当前源码正式执行另行负责。原十项 Logging、原三库合同主项、正式 D1.06/G1、加载模块全量和 footprint 预算均不由本页批准。

本次只读审源、摘要/归档/XML或原始流与有限 checker 调用，没有重编译或重跑三矩阵。待上述守卫问题按真实反例修复后，对变更输入增量复核；此页保留历史状态。


---

## 守卫增量闭合（2026-09-08）

actor_type: AI  
reviewer: review_foundations（独立 CODE）  
status: Approved（仅本页 SDK 范围；原 ChangesRequested 历史保留）

P2 已闭合：checker 将来源位于本组件 include_root 的安装头纳入精确 detail 包含边校验，即使分类是 detail/public=False 也必须登记；跨组件规则不放宽，非安装生产源码的同组件内部引用仍按原规则允许。新增同一测试实际验证三条已登记边成功、两条错误边拒绝和一个非安装内部引用正控制。代码改动仅该判断与本测试，没有修改 Runtime/安装实现。

旧 41 文件集合与原归档不变；当前仅以下两文件变化，其余 39 项当前 SHA 与原索引相同：

| 文件 | 当前 SHA256 |
|---|---|
| tools/architecture/check.py | 28b642dff2e90292ac06ac08e647c16481a053b208ebccacffad69b0817675b9 |
| tests/architecture/test_native_sdk.py | 2329378777e46ef6b4747dd2a877e102cea874c36a6b4a442bd91def2374ab41 |

按原精确算法独立重算当前 41 文件集合为 `e132eeb365d7fcc163d10fdb749861f8afaade10c419ad09cb1542011d088f80`。增量源、before/after checker、测试及两轮原始流由 sdk-detail-fix-index.json 绑定；sdk-detail-fix-raw.zip SHA256=`7097c81e4018d5552e6380d7d0c61eb40ef84788ef5ddbf87ee06a4698309bff`，12 个成员全部逐字节核验 SHA/长度及原文件相等、无重复。

`sdk-detail-red-51f1abd3f9` 实际 Exited1，一个测试的两条目标子断言均因空错误列表失败；不是源码推测或环境失败。`sdk-detail-green-ed30fb0b71` 实际 Exited0，49 个既有架构/旧阶段/SDK/包装守卫全部 OK，无跳过；新增目标测试包含在真实命令内。两轮均 Job Resume 前挂接、active_after=0、未终止 Job。未重跑无关安装/构建矩阵，之前不同运行来源的边界仍按本页保留，不能把 49 个工具守卫解释为当前完整 SDK 三配置消费。

结合先前实现审阅、原始安装证据核验与此有限修复，本范围 CODE Approved。Host 后续修复、正式配置与来源统一执行、footprint 和 D1.06/G1 包级状态仍不在本结论内。

# D1.06 Runtime / SDK 实现自审

作者自审，非独立 CODE Approved；仅覆盖本次 Runtime 合并、SDK 表面与必要开发验证，不表示 D1.06/G1 Passed。对应一次合并 SPEC 的 `native-sdk-surface.md`、`native-test-scope.md` 未改写。

## 实现与边界

- `ock_Runtime` 是唯一生产 STATIC target，编译 Registry、Policy、Invocation、Host、Logging 五个真实 `.cpp`；历史三个 internal 名称只是同一 target 的 ALIAS。PUBLIC 只有 CoreContracts，Windows bcrypt PRIVATE，导出实测为 `$<LINK_ONLY:bcrypt>`；CoreContracts→Foundation 闭包保持。
- Registry/Policy/Native 模板声明迁入唯一规范 include 根。旧路径只转发；Native 类型从 Invocation 拆出，私有桥仍 private。Runtime 八个安装头、CoreContracts 七个头和三个模板实现包含边均精确受守卫约束；未知 detail 和跨组件 detail 继续拒绝。固定 `LogComponent::Host` 枚举不再被词法规则误认作通用 Host 类型，额外 `class Host` 仍拒绝。
- `Catalog::module_order()` 返回 Catalog 自有只读 span，保存原 publish 的 Kahn 顺序。新增链依赖反例检查 `z,a,c`，与输入顺序及字典重新排序都不同，释放 Batch 后仍有效。不重新运行排序。
- 依根任务要求，两个仅源内 helper 分别复用原 Policy 配置计数/预算检查、原 Native 字节乘加预算算法。原 Store 的端口/身份/TTL等验证保留，原 NativeEngine 仍验证 owner 后调用同一预算算法。helper 不安装、不调用可信端口。
- SDK dev.2 / NativeSubset / 真实 STATIC 与能力一致。独立 `examples/stateless_service` 仅公共头和 `find_package(OCK COMPONENTS Runtime)`，只链接 Runtime，真实 Read=12、Compute=7、非法输入不进入 handler、Ready门禁、停止及停止后拒绝。BUILD_TESTING=OFF生产者另行真实构建，未添加产品模块或测试工厂到 Runtime。
- 旧安装/组件边界断言按现阶段更新；没有修改历史 expected。原有三个原生包装显式拒绝 Python 优化模式，成功门禁同时要求暂停创建后先归属 Job、无遗留子进程、没有清理杀死本 Job。对应真实 `-O --help` 与伪终结记录反例已保存。

## 已观察的开发证据

| 职责 | 独立证据目录 / 实际结论 |
|---|---|
| module_order 缺API编译 red | `sdk-stage-e96c22e3ee`，真实 MSVC 缺成员；此前沙箱找不到编译器的 `sdk-stage-60224d5e7f` 保留，未冒充合同 red |
| 单一库编译与目录原顺序 | `sdk-stage-7aef73cd8f`，五个生产 cpp 与四 runner 编译成功，module_dag_order 实际退出0 |
| 旧SDK拒绝Runtime消费者 | `sdk-installed_host-0c72d2ee31`，真实 find_package 对未实现 Runtime 拒绝，不宣称执行了Host |
| Debug SDK职责 | `sdk-installed_host-6dd95e48d1`、`sdk-public_headers-63e0c2800e`、`sdk-private_dispatch-fb54aac90a`、`sdk-install_pruning_rejected-4c393e71e2`、`sdk-no_tests_producer-267cce3738`、`sdk-metadata-802cb712c2`、`sdk-link_closure-e2676ce911`；七包装均有实际正控与对应子断言 |
| 链接裁剪初轮失败 | `sdk-link_closure-0372250765`：包装器没处理CMake导出中的转义字符，仍有bcrypt，被闭包正守卫拒绝。修后新目录确实移除该导出项，得到 BCryptGenRandom 未解析符号；原失败未覆盖 |
| 原生安装包装影响 | `sdk-boundaries-8251730a86`，Registry/Policy/Native安装边界和Registry冷描述4包装实际成功，复用已编译runner，独立新child目录 |
| 最新Host Debug消费 | `sdk-installed_host-0790a66f2e`，复用根 `sdk-stage-c7bae4df64`，两个预算helper接入后真实C-A闭环与实际trace通过 |
| Release / ASan风险专项 | `sdk-stage-4dcee1ee3b`→`sdk-installed_host-d39aba3297`；`sdk-stage-c8ebabb534`→`sdk-installed_host-51f8de4220`；每配置只构建Runtime和实际消费者，包含Logging最终owner修复 |
| 实际编译链接复核 | `sdk-trace-audit-41b99e53e0` 对9份既有运行只读核验：实际 /MDd或/MD、ASan/noRTC、LTO关闭、最终 Runtime+bcrypt 链接。是增量轨迹核验，不重写旧报告或假称重跑 |
| 旧包装反例→修复 | `sdk-wrapper-red-e65cca5f3b`→`sdk-wrapper-green-7a6c594c41`，原优化模式被接受和终结Job假绿色均检出；修后拒绝 |
| 守卫与真实图 | `sdk-architecture-e89ba46444` 原45项及真实target图；`sdk-final-guards-cc9bc9ced6` 扩展48项；LF与Host头摘要归一后 `sdk-final-surface-a573cb46db` 8项与真实图再次通过 |

最后一个无ASan Debug当前源码安装基准的路径、原始编译工件及摘要由 `sdk-summary.json` 记录，供 footprint 只读消费。上述历史开发目录保持原样，各自快照和源摘要不拼接为统一最终来源。

## 复核范围与材料

`sdk-owned-files.json` 给出本任务41个源码文件；`sdk-source-identity.json` 绑定最终LF字节及共享公开头/生产依赖。`sdk-raw-text.zip` 与 `sdk-archive-index.json` 只收可复核原始流、命令、生成CMake、消费者/负例源码、身份与编译/链接tlog；对象、PDB、重复二进制和生成缓存不一股脑纳入Git。实际EXE/Runtime.lib仍留原目录，报告保存其SHA与大小；原目录不删除或迁移。早期包装器源在结束前发生了小幅编辑的材料，已恢复与运行时预先记录SHA逐字一致的旧driver副本，不把新driver当旧执行代码。

Windows运行全部使用既有 owned process；只有局部子进程环境补实际编译器runtime路径，无全局PATH修改。源码按仓库LF约定归一，原始流完全不归一。编译来源包含此前明确保存的CRLF快照；这些真实开发事实保留，正式D1.06集合应由主任务按最终受审来源生成和执行，不手工把旧成功拼成正式报告。

本次没有完整历史三配置矩阵、ABI/前版正式兼容、完整Embedded、实际加载模块全量测量或footprint预算验收结论；加载模块/占用与Ready时延按footprint责任另行观测。Host/Logging完整行为由其负责人及后续正式影响集承担，SDK消费者不替代它们。

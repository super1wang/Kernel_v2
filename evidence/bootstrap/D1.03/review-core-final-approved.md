# D1.03 核心最终增量 AI 复核

- actor_type：AI；复核者：独立子代理 review_contracts。
- 规格结论：Approved；API修订2未变，沿用前轮独立规格复核。
- 核心代码结论：Approved；前轮剩余TypeIdentity预算P2已关闭，未发现新必改项。
- 这两项是规格/核心代码结论，不代替wrapper、整包矩阵和自动门禁。

## 精确输入

| 文件 | SHA256 |
|---|---|
| packages/runtime/registry/registry.cpp | 7f2063cfca9b922ddecbc1e00c812f9de77c870739a76fb7be3959898d072c2a |
| packages/runtime/registry/registry.hpp | 7f79f13c45ec38968bce552533be8c92d29010eb07aa507abbe55dfe2f87e5ae |
| tests/contract/registration/registration_tests.cpp | bd9b065fb84ebafe40f8154e7a62d1fb3834062e6cf7c0c5f257c6592c2a71d4 |
| tests/contract/registration/fixtures.hpp | c972132513f6c1ca506db1680313856ad8f5bb927e0dca838f805c2255ccfb71 |
| docs/contracts/registry-api.md | ecdd0b6e24c1065fd19e5c27f8f786cc3a096bc1e6c62679314b0eced0893fa5 |

以上均实查与implementation-0e97826404f5对应核心source.json一致。相对前轮审查源码的产品增量仅为insert中的冻结身份文本计费；测试增量为Args/R两方向身份预算、提供项移动别名及add复制异常控制。旧ChangesRequested报告和red字节保持不变。

## 剩余P2关闭依据

insert在任何条目进入冷热候选前检查snapshot实际args_contract/result_contract的Name及OperationVersion文本，并计入可选provider key；使用 `extra > limit - used` 比较后累加，超限不更新累计值，走已有无分配粘性fail。检查的是工厂已冻结的真实身份，而不是重新调用可能返回不同值的TypeContract。可信类型合同函数内部临时分配不被虚称为预先可预算，但其超限产物不能进入候选或发布目录。

新增同一1028字节精确版本分别作为Args和R合同，512字节批次预算下均要求发布失败且首要错误为BudgetExceeded。合法typed绑定控制仍保留，没有通过拒绝所有类型注册掩盖问题。

原独立probe已由实施者实际运行并保存在implementation-type-identity-red：配置、编译和typed合法控制均Exited 0，probe Exited 17，原始stdout为published=1和version bytes=1028。不是任意编译失败或子进程崩溃冒充red。

本次implementation-0e97826404f5命令记录build/list/native25均Exited 0，覆盖新Args/R预算断言。独立审查已核对原始命令退出和源码绑定；没有宣称本代理重新执行了整套矩阵。

## 其他修复保持有效

- const引用add及独立容器复制未回退，新增services元素替换后仍读取原11而非22，证明共享owner槽不被原vector元素别名修改。
- add复制bad_alloc实际故障注入被捕获后failed仍为true、publish仍拒绝，符合入口内复制和粘性异常规则。
- 跨模块重复key预检、逐模块first_slot完成性、不可见候选和固定具体ref绑定未改变。
- publishing_重入保护及延迟清理未改变；外层钩子结束前owner不被释放。
- 冷Docs不进HotEntry、Handler不对模块暴露、RegistryId耗尽拒绝而不回绕的静态规则未改变。未实际运行耗尽分支的事实仍保留，不虚构该分支Passed。

本审核只关闭核心实现与消费者范围内已发现问题。父任务仍须完成最终wrapper配对、独立工具/边界审核及正式三配置自动验收，不能把本Approved改写为D1.03包级Passed。

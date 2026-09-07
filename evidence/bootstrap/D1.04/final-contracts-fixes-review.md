# D1.04 最终核心/规格/覆盖修复独立 AI 复核

- actor_type：AI；独立复核者：review_contracts。
- 规格结论：Approved（API/计划修订5保持成立）。
- 核心代码及固定35项覆盖结论：Approved，关闭final-contracts-review.md的三组剩余问题；不是包级Passed或三配置验收结论。
- API SHA256：`b7c73b6401cca3efb3c43f0e59ac8ce1cb5f597bb9710f0c5908faff5e054002`；计划：`71283f43812a3be88f6e8d457185d15dc494cbac73a5035430b21e97f328b07e`，准入沿start-revision5.json。

## 最终受审来源

implementation-freeze-9a841021c0ef的13份来源已与implementation-d6b561947b73/source逐项核对。该轮38条owned命令（configure/build/list/35主体）均Exited/0、active_after=0。当前源除下述换行归一外均与冻结SHA相符。

|文件|最终SHA256|
|---|---|
|policy.cpp|`c54e87301880bde0548d59a686f5bfa0cb2ca909920fb2dc2757c8ee3168e0f6`|
|policy.hpp|`bcd5fcb28468b6f7f519c3e99671a1f0a6554871da1be043bced89c2b1ee4fee`|
|policy_tests.cpp|`bbf1679ebb252fedb4f4d90344803c3b4cb203869ff3282bea7c4b0830591a30`|
|fixtures.hpp|`abb7b43d76cb3986e07159943c4ad02046c72b032bc2513f5739d088378d34a2`|
|action_cases.hpp|`6b7764d720644e10d33716177ca7419e40d729c847e15d2f718475c8ba61d4d6`|
|authority_cases.hpp|`94b378ccea15926a3fff2daeba93725d7c07c4755e9c532f506d68a23082375f`|
|session_cases.hpp|`fa093665543df391c79f1879d618671846de72a43a7acce54f3d641edc95e528`|
|send_cases.hpp|`27dc8edbab6c0df5aceea69a6e14eaedea9b828b96c7789e562803efea679730`|
|observation_cases.hpp|`c2129f58dc4ac0db63c365b213bb83c3d36caa1c4a133e092d53bef2ac0150a9`|

observation_cases旧SHA为`0d96ac513686f35c722936c7637d49c1645acea2ea3580e6667890158aa87416`。已读取freeze-after-review/newline-proof.json并独立比较旧原文仅CRLF→LF与当前完全相等，不能将新字节冒称旧run直接编译来源。最终224输入清单见freeze-after-review/source-inputs.json，父任务登记摘要`1a044ad0c5b50f1efa53a82125430a7002a61a1fbbd989f8f1ca2e1e50ac8352`；完整工具来源/正式矩阵由父任务另审，本报告绑定上述实际核心及测试SHA。

## P2-1 编码缓冲独立拥有：关闭

冻结原始red encoder-alias-3caf9bd610bd已有实际首字节126而非81。同一独立探针在encoder-alias-1672f565ca5f中configure/build/control/alias全部0，stdout `mutation=1 bytes=2 first=81`，证明仍真实修改活着的返回元素，但发送副本保持原字节。两轮main.cpp完全相同，SHA `81d7e6b02acb29731316481ee348e11d11bed1bda0ad82469ce0065939684077`。

最终Access::transmission从const vector引用构造独立副本，在限长后/锁外/最终发送仲裁前完成；Response及Watch两处共用。35集成中的send_cases另含两shape真实reserve别名回归及安全delete观察正控制，允许原buffer提前释放而不解引用旧指针。修复符合原合同，不新增API或限制可信编码器正常功能。

## P2-2 观察投影预算：关闭

独立原red projection-budget-1efdbfdef52a在base=6885、key=14、总预算6899时get仍成功。独立修后projection-budget-a66771c38322四命令均Exited/0、active_after=0；同样打印预算6899但get=0，6913足额控制成功。两轮probe源码SHA相同：`f0a34118d9ea4b293e93d942e9240f54974ec7203ddd40098475011ddf3502a6`。修后核心SHA正是本报告最终cpp。

entry_usage现在区分是否新持有projection：Entry.operation一份key；新投影再计一份key及实际投影facts声明；共享的已有不可变source Summary不虚增收费。Watch仅保存Entry，不再创建马上丢弃的投影；Watch帧拥有新Entry和投影，计两份；Response帧共享原projection，仅计新增Entry副本。prepare_entry在创建投影前执行计量。

实施者真实red implementation-80da80079094的owned_inputs_budget退出1，原始错误为`bool(response) == (keys >= 2)`；同轮auth/subscribe异常控制为0。最终35增加1/2/3份key阈值：get从2份起成功、Response排帧总共3份、Watch本体1份且Watch帧加入后总共3份；退订释放后容量可恢复。独立probe与集成控制验证实际新增拥有量，不将source共享体重复收费，也不靠扩大预算掩盖失败。

## P2-3 剩余覆盖：关闭

observation_cases三个函数已经实际挂入原query_field_projection、page_binding_isolation、queued_revoke_drop，最终35执行了这些调用；其独立审查见observation-coverage-fix-review.md。包括UsePolicyInput所需权限的逐方补齐及新会话正控制、委托缩减后新caller下旧分页绑定拒绝、owner/phase变化、先关闭再换host建新Store、旧get/list/Watch/caller/cursor拒绝、三类新帧真实字节成功。

exception_atomicity补认证端bad_alloc后第二个会话成功（sessions=2、已有1会话），以及源find异常导致subscribe失败后有限Watch配额仍可成功安装；达到1个Watch限额后第二次拒绝、释放后再次成功。它们和既有digest/find/scan/encoder/reserve、管理分配扫描、析构重入一起实际通过，填补此前auth/订阅异常容量控制缺口。没有把noexcept失约当可安全吞掉的NotStarted。

此前完整报告的35逐项表中待修/待补项现由上述实现与真实控制闭合；其他条目沿用完整核心审查及相应切片的已证判断。没有新增必改项。

## 结论限制

本轮检查最终diff、新测试接线、真实红绿与来源，并独立运行预算复测；未修改产品或冻结规范。旧ChangesRequested及探针通道失败全部保留。被撤回的不可变ExecutionSummary别名/巨大facts候选不算缺陷或修复证据。

批准覆盖D1.04内部内存Policy、可信适配器、CoreContracts消费者及35项已冻结合同；不宣称生产OS认证/密码学/网络/执行索引、D2/D3或公开SDK Runtime完成。根wrapper及正式Debug/Release/ASan矩阵尚由父任务继续完成，当前原生35成功不能替代整包最终验收。

# D1.05 分配、包装及示例最终增量审核

审核日期：2026-09-08。审核来源：**独立 AI**。本审核者未实施被审分配探针、包装器、独立示例或开发驱动；曾实施的 Policy 不在本报告独立代码结论范围内。被审源码只读。

**SPEC：Approved。CODE：Approved。未发现本范围剩余必须修复的问题。**

批准绑定完整 **255 个输入**，摘要为 `1685e9ccdd20ac4edb07e78249fb119970e7b8a89885490fb565a07ae57ca2df`。这是一份范围明确的 AI 技术复核，不是 human Approved，也不单独授予 D1.05 包级 Passed 或替代正式 `tools/evidence/run.py` 矩阵。

## 审核顺序与变更范围

先完成原计划、API 与 transport 增量的规格审核，再完成第一轮代码审核，见保留的 `independent-allocation-wrapper-review.md`。随后依据实际 ASan 失败及同源诊断审核第三通道设计，在实施前明确 Approved，并要求幂等正控制、注册失败无对话框退出和真实槽耗尽反例。最后复核新增覆盖规格、六个测试/驱动改动及新三配置实证。

独立比较旧 254 个来源和本轮 255 个来源：只新增 `docs/contracts/native-allocation-coverage.md`；只修改 allocation_probe.hpp/.cpp、allocation_cases.hpp、native_tests.cpp、verify_children.py、develop.py，共六个文件；无删除，生产核心源码没有变化。新三轮的来源摘要相同，原始 source zip 的全部文件 size/SHA 均核对通过，且与当前工作区全部 255 个输入一致。示例 main.cpp 的本轮 SHA 更新已逐字节证明只是 CRLF→LF，逻辑不变。

## 第一轮发现关闭

- **A1 已关闭**：calloc 与 realloc 使用各自窗口，realloc 的前置 malloc 在窗外。普通 Debug 两入口各观察到 CRT=1；新 ASan 通道两入口各观察到独立 ASan allocations=1，原 CRT=0 被保留并明确标为未观察到，未被另一入口掩盖。
- **A2 已关闭**：first_invoke 检查实际 ReadCompleted<int> 的成功值3；variable_result 检查 ReadCompleted<Own>、Result 成功及完整字符串相等。三配置对应样本均真实 success=true；进入后失败不能再冒充合法变量结果正例。
- **开发驱动空发现缺口已关闭**：develop.py 加入 `not names`，空 --list 不会无测试返回0。此为驱动断言补充，未标作原生业务逻辑 red。

## ASan 规格与实现复核

原 `integration-asan-40c7511005de` 的 allocation_probe 实际失败保留。`asan-crt-diagnosis-581ee85e3e8f` 同源对照证明12个入口在普通 Debug 的 CRT 事件各为1，在 ASan 的原 CRT 事件全为0。锁定 sanitizer header 与实际 DLL 导出均提供 hook 安装接口，不能将这种 CRT 零观察解释为零分配。

新增覆盖规格明确保留三个独立通道，不相加、不扣除：C++ new/delete；原 Debug CRT raw；ASan allocator hook。ASan 的 `crt_missing_positive_entries` 保存实际12个缺失入口，`debug_crt_effective=unobserved_asan_intercepted`；Release CRT 与未安装 ASan 字段保持 null。`full` 只指12个声明入口正控制，不是全进程覆盖。

实现只在测试 allocation 单元按 `__SANITIZE_ADDRESS__` 编译 sanitizer 接口。回调只操作当前线程计数；主线程 initialize 幂等，重复初始化不会多装 hook。真实安装返回0打印固定 marker、fflush、`_Exit(87)`，不会继续计数或进入业务，也不会弹 Debug abort 对话框。槽耗尽模式最多8次注册空 hook 对，只有真实观察0后才调用正常 initialize；包装必须同时取得退出87及 marker。

12个正探针实际断言 ASan allocations 恰为1，原 CRT=0；无分配负探针、40次完整 invoke 及结果/观察/RAII 清理窗口分别检查各通道零事件。释放事件独立报告，realloc 的2次释放不被错误强制成1次。原有驻留/峰值字节仍仅代表替换 C++ new/delete 的 CRT usable bytes，不改成 ASan 隔离区或进程总内存。

独立读取 `asan-channel-verification-59bf0b8f1efe` 的原始流，并核对五条命令 size/SHA、退出码、Job 清空及两份正式 probe 源码副本完全相等：正式 hook 与第二个诊断 hook 对12入口逐一一致；两次 initialize 只占一个 hook 对；真实槽满退出87/marker。这是实测互校，不引用实施者自审结论代替判断。

## 新三配置实际复核

独立审计脚本及详细结果保存于 `independent-allocation-recheck.py` 与 `independent-allocation-recheck-final.json`。读取原始 JUnit 而非只信 result 状态；逐项检查32个用例、8份包装、每条子命令预期退出码、原始流 size/SHA、WindowsJobObject/assigned_before_resume/active_after=0、source zip 和 build-artifacts zip 全部条目。ASan 的实际 Native、Invocation、Policy、Registry、独立示例工程均包含 `/fsanitize=address`，实际工具链 metadata 为 asan_requested=ON。

| 独立根工程轮次 | JUnit | 包装 | 子命令 | 分配样本 | 归档产物 |
|---|---:|---:|---:|---:|---:|
| integration-debug-474c25032e22 | 32/32 | 8 | 27 | 68 | 378 |
| integration-release-ee8d09f863ec | 32/32 | 8 | 27 | 68 | 376 |
| integration-asan-dd8b40540919 | 32/32 | 8 | 28 | 68 | 380 |

- 合计96次 Native JUnit 全通过，24份包装共82条子命令均符合各自正负退出协议。每轮私有桥包含实际 transport 退出86及标记；每轮 allocation_probe 包含故意分配退出1、verified=false；ASan 另有真实注册失败退出87及标记。
- 三配置204个分配样本的字节/块数守恒与峰值上下界独立核验通过。各40个完整稳态窗口：C++分配/释放为0；普通Debug可读CRT为0；ASan新增分配/释放为0，原CRT raw=0且仍标明拦截不可观察。
- 普通Debug12入口各CRT=1，Release CRT=null；ASan12入口各ASan allocations=1且CRT=0，缺失入口数组逐字匹配。没有以取消CRT诊断或只检查C++通道掩盖ASan差异。
- 8包装包含真实示例进程、安装后组件可用/不可用边界、合法编译和指定私有访问/不存在API负编译，原始退出码分别核对，非只检查文件存在。
- 旧失败和第一轮 ChangesRequested 报告保持原样；上述为追加修复与新增实证。

## 当前精确 SHA-256

完整来源摘要覆盖其余255项，下列列出本轮增量及补审驱动：

| 文件 | SHA-256 |
|---|---|
| docs/contracts/native-allocation-coverage.md | 8132d519d83680dfa92c1c8e6d2682197aa9ba33ac26a478094dd62df304255a |
| tests/contract/native/allocation_probe.hpp | 69cada784e0d8646d0e1fc42577ef083ef4b20a269b71bc8bce5c8e314d872ac |
| tests/contract/native/allocation_probe.cpp | 92343e81324bc4cf3895b89160f6b3768ac97387edcf0a1251dea51de5f1f448 |
| tests/contract/native/allocation_cases.hpp | 79c89aa59cabe8537cc0df55f9243c98d1d23c24126c3e6f3f58077f7f3a1515 |
| tests/contract/native/native_tests.cpp | 151b5bcec1beda868e64efa112cbe7b5dd599fcf66dd55963b3d03a3a8cdceae |
| tests/contract/native/verify_children.py | 5f92a4adce0cc6ca4d406d094e7047a8c67f0f533fd7b153293d1b2b51d99ddd |
| tests/contract/native/develop.py | 00792050fde57eb27595c1bd279c7e901d13b62b0d04a3c5c747bb8cd4ee5210 |
| tests/contract/native/integrate.py | 250cdd915b47f74557823b914c320b69c10dd14a31550ac9f532b10df747dd65 |
| examples/native_service/main.cpp | 81484abcd6b7e37255d6db336bc3bade05edc97e4f68dad47de3b8a83bdc0f91 |

## 有效范围

本批准支持当前完整来源下分配探针、包装/驱动、独立示例及两个测量/transport边界的技术审核收口。固定窗口之外的Release CRT、DLL私有分配、自定义堆、sanitizer注册前、后台线程与ASan隔离区成本仍是明示盲区。这里不宣称进程全堆零分配、不提供D1.06占用/延迟基线、不替代另外审核者的生产内核代码结论或正式提交来源验收。

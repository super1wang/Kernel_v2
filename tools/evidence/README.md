# G0 开发证据工具

这些工具只用于开发验证，不链接生产 Runtime。依据执行计划 E03、D0.06；输出不是审计签名，不能替代人工评审。

## 固定输入与实际执行

`tests/manifests/d0.01.formal.json` 至 `d0.05.formal.json`、`d0.06.expected.json` 是固定必需集合。`tests/runs/` 明确包、配置、过滤、构建产物、重复轮次、检查和审查类型。CTest 的发现入口从真实测试函数读取，不读 expected；运行时逐项比较固定集合、实际发现、各轮 JUnit。需求总表的未来测试仍是规划，不混入已执行计数。

在仓库根目录执行，例如：

```powershell
python -X utf8 tools/evidence/run.py tests/runs/d0.06-win-msvc-debug.json
python -X utf8 tools/evidence/validate.py <本次report.json>
python -X utf8 tools/evidence/gate.py tests/runs/g0.json <各必需report.json> --output <新的gate-summary.json>
```

尖括号是工具实际产生的路径，不能作为命令字面量。G0 矩阵为9组：D0.01、D0.03–D0.05的Python模型在Debug工程运行；D0.02实际C++基线跑Debug/Release；D0.06全量依赖跑Debug/Release，Embedded另跑ASan。模型不因配置标签而成为多线程或真实持久化验证。构建和测试串行，MSBuild禁用node reuse，避免后台worker改变进程排空事实。

运行目录为 `evidence/<commit12>-<输入摘要12>/<profile>/<task>/<唯一run_id>/`。失败重跑追加目录，保留每轮stdout/stderr、CTest发现、独立JUnit、命令及退出。JSON副本不代替raw流。源输入ZIP和构建产物ZIP逐项保存摘要/字节数，包含源码dirty事实、所有被测可执行文件、CMakeCache、实际工具链和Python安装文件。源输入模式覆盖本阶段全部模型/合同/检查规则，跨包只允许相同最终输入摘要。

## 构建和进程边界

配置与构建实际调用CMake；校验器从归档中的Cache核对源目录、单配置BuildType或多配置集合，从实际build参数/preset核对所选配置，并核对生成的toolchain配置、compiler、toolset、SDK、CRT和ASan与固定锁。报告标签不是配置证据。

Windows子进程先以挂起状态创建，加入本次独占Job，再恢复主线程；超时只终止该Job并记录剩余进程。主进程退出而子进程未排空不能Passed。NaN、Infinity、bool及非正/过大timeout拒绝。开发夹具与用户材料隔离在build中，无按进程名或系统PID批量终止。

短命令测试采用30秒启动期限；故意泄漏/超时场景用5秒固定期限和120秒睡眠子进程。并发全量编译曾使原5秒/1.2秒夹具失败，控制台错误保留在任务记录，缺少完整raw的部分不补造成正式报告；后续夹具改为持久目录，正式外层运行将其所有原始运行和故障副本一起打包。有限预算内无法排空仍失败，不进行until-pass。

## 审查与状态

自动状态和包级状态分开。必需测试失败、跳过、零集合、错过滤、缺轮次、损坏材料、源码或二进制变化均不能Passed。历史资料不完整输出Incomplete；当前正式校验错误输出Failed并列出原因。没有审批时自动检查可Passed，包级仍InProgress。

原G0 manifest固定 `review_required=[spec,code,human]`，按生成时旧政策保留。评审记录必须指向该task和精确输入摘要，明确review_kind、review_status=Approved及审批文字；全部必需类型匹配才可包Passed。测试使用的合成审批只在隔离fixture，绝不是用户审批。D0.01–D0.03原有人工批准仍有效，但不据此伪称本轮新增构建/工具已受人工签核；G0当前变更提交审查后再绑定批准。

`--historical` 只核对归档字节，不宣称当前工作区符合。`import_bootstrap.py` 只给早期记录建立材料指纹索引并核对可解析的本地hash引用，不补写缺失退出/构建/轮次事实；这些历史记录保持Incomplete，当前包另用正式入口重新运行。

## Conformance与依赖

`tests/conformance/support/manifest.json` 固定同一版共同用例、mock/fault工厂及可选能力表达式；`tests/conformance/run.py` 输出实际工厂摘要和结果。fault重复callback必须被共同exactly_once测试检出，不能被视作合格生产后端。可选parallel=false对应NotApplicable，不计Passed；profile要求parallel时无法豁免。真实BS pool/SQLite/AssetStorage共同及专项合同分别由D1/D3/D5/D6承担。

生产候选依赖见 `dependencies.lock` 和 [工具链说明](../../docs/toolchain.md)；JSON Schema验证用六个固定Python包另见 `tools/dependencies/python-lock.json`。先通过官方PyPI元数据核对wheel SHA，再逐字节核对隔离安装及实际导入位置。只使用build下缓存，不全局安装。验证命令与锁自身均进入输入摘要。

实现参照本机真实CTest 3.31.6-msvc6的JSON发现、JUnit与no-tests=error实测。平台机制参考微软 [Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects)；CTest参数参考 [官方手册](https://cmake.org/cmake/help/v3.29/manual/ctest.1.html)。在线文档不替代本机执行证据。

`gate.py` 默认退出码只表示自动汇总是否齐备，人工未批准时仍可输出自动Passed与gate_status=InProgress。放行者必须检查gate_status=Passed，不能仅用该报告生成命令的exit=0作为G0批准。

最终来源复核修正：仅排除仓库根部的build/evidence/.git产物目录，tools/evidence与tests/tools/evidence必须进入源码快照。采集进程启动时记录实际加载规则的指纹；源码变更后必须启动新进程，不能跨run把新文件hash标到旧内存代码上。首轮bf122b7矩阵由此作废为旧来源验证记录，保留全部报告，最终门禁重新采集。

## 用户明确指定的自动验收模式

用户现明确要求“自我复核和自动验收，无需人工”。政策记录为 `docs/reviews/automatic-acceptance-policy.json`，以版本1和SHA-256绑定。新manifest使用review_required=[spec,code]及显式review_policy绑定；规格与代码记录均明确actor_type=AI。政策必须进入源码归档，AI复核记录另行ZIP归档；缺政策、错版本/摘要、缺任何复核、伪造审核身份或损坏附件不能放行。没有显式政策的旧manifest保持原规则。

`accept_gate.py` 用于对已提交的历史实现追加当前政策下的验收决策，是通用历史候选验收命令；本次调用仅选择G0原九组报告、194dcdf实现及142项输入。它复核原始archive/raw/JUnit、固定matrix与各配置expected、同一来源与实际Git对象字节、原AI复核及证据摘要。工作区dirty标志可能来自后续未跟踪证据；是否对应提交由逐项Git字节核对决定，dirty原值仍保留。

输出为独立ock.automatic-gate-acceptance/1决策，绑定实现、原报告、政策、复核和工具规则摘要，不修改原run的任何字段；旧human Pending仍是旧报告生成时的事实。该结论不声称后续已改源码运行过旧矩阵。新实现必须另行运行相应包的正式验证。

```powershell
python -X utf8 tools/evidence/accept_gate.py tests/runs/g0.json <九份已选定report.json> --policy docs/reviews/automatic-acceptance-policy.json --output <全新验收决策.json>
```

尖括号代表实际参数；输出路径已存在时拒绝覆盖。退出0要求自动证据与AI复核全部通过，不能仅凭原报告automated_status文字放行。

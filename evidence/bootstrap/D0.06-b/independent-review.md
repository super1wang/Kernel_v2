# D0.06-b/c 独立规格与代码复核

复核者：D0.05 实施子代理；未实施本次检查的 evidence collector、Schema、process wrapper 或 Conformance 文件。仅本报告由复核者写入，未改被审查实现、测试或 Git。

依据为唯一规范 `docs/02_Execution_Plan_v3.3.md` 的 D0.06、E03.1–E03.5，以及 `docs/01_Architecture_v3.3.md` 的 A22.2/A22.3。本报告不把父代理正在接入的根 CMake、run/expected manifest、gate summary 当作既存代码缺陷。

## 当前结论

发现 4 项需要处理的问题；嵌套 inline worker 问题已由实施者修复，并通过本复核者的单项独立重验。构建模式/工具链绑定、必需评审集合及非有限 timeout 仍待最终实现和针对性复核，因此当前不作 D0.06-b/c 技术批准，更不代替用户人工批准或 G0 放行。

本轮先以静态检查为主。系统同时运行依赖构建时，简单文件读取也出现数十秒延迟；父代理要求暂停启动完整测试集。复核者没有把这段资源拥塞下的进程超时当作实现失败，也没有重写父代理已保留的失败证据。下列实际探针均为低成本、独立操作，未启动新 CMake 构建。

## F1｜P1：实际构建模式与工具链身份未被独立核对

位置：`tools/evidence/run.py` 的 report.build 构造（原第 66–70 行），`tools/evidence/validate.py` 的 build identity 检查（原第 68–69 行）。

采集器将 profile/configuration/preset 从 run manifest 复制进报告；校验器又与同份 manifest 比较。检查到的版本没有解析实际 CMakeCache 的单/多配置类型，也未把实际 compiler/toolset/SDK 身份作为独立核验项。CMakeCache 即使列入 build_outputs，也仅校验文件摘要，不能证明其中实际编译模式等于报告标签。环境中的 VCToolsVersion/WindowsSDKVersion 也只是环境值，不能代替实际编译器事实。

具体反例：单配置 Ninja 以 `-DCMAKE_BUILD_TYPE=Release` 配置，但 manifest.configuration 声称 Debug。CTest 的 `-C Debug` 不会把单配置 Release 二进制重新构建成 Debug；当前路径仍可以得到标成 Debug 的自动通过。这使 E03.3 要求的源码/实际编译模式/工具链/二进制身份绑定失效，属于采集器自身问题。

证据等级：静态确定的数据流与具体可执行反例；本复核者尚未启动该 CMake 反例。父代理已确认正在添加单配置 CMAKE_BUILD_TYPE、多配置 CMAKE_CONFIGURATION_TYPES、build --config/build preset 实际配置及真实 toolchain metadata 的核对。最终应分别实跑错模式反例与合法配置，不能仅改变报告文字。

## F2｜P2：必需评审集合没有参与包级通过计算

位置：`tools/evidence/validate.py` 原第 171–180 行；`tools/evidence/run.py` 原第 73、158–161 行；Schema 的 review.required。

Schema 允许非空字符串数组 review.required，但校验器没有读取该集合，只用一个 human 布尔值：任意一个 task、输入摘要、Approved 和 approval_text 匹配的记录就令包状态成为 Passed。收集器也仅采集 spec.review_records 中当前存在的文件，未对必需项逐项检查。因此 required=['human','independent-code'] 而缺少独立代码审查时，没有相应拒绝规则；多个列出的评审记录也不能表达/确保哪些必须齐全。

这不意味着当前用户已经批准，也不指控存在伪造审批。问题是通用机器规则无法兑现 E03.3 的“必需自动检查、产物和评审全部齐全才能 Passed”。建议从受审查 manifest 取得必需评审类型/身份，对每项唯一匹配当前输入摘要的有效记录；缺项保持 InProgress，拒绝仅修改 report.required 来放宽要求。静态结论已报父代理，待实现者补具体负例和独立重验。

## F3｜P2：非有限 timeout 能越过进程前置检查

位置：`tools/evidence/process.py` 原第 20、115、127 行；`tools/evidence/run.py` 调用 check.timeout_seconds 的路径。

execute() 只检查 timeout <= 0。NaN 和正无穷均通过；NaN 参与 deadline 后，time.monotonic() >= deadline 永远为 False，可能使本来应有界的测试树无限等待。顶层 run timeout 有有限区间限制，但每个非 CTest check 的独立 timeout 没有同样约束，且 JSON 读取默认接受 Python 的 NaN/Infinity 扩展。

本复核者实际运行了无进程、无文件写入的探针：用 unittest.mock 把 ctypes.WinDLL 替换成即时异常，在调用 execute() 时分别传入 float('nan') 和 float('inf')。两者均抵达 Win32 边界，而没有被 ValueError 拒绝。实际输出：

```text
[('not_rejected', 'nan', 'entered Win32 before validation'),
 ('not_rejected', 'inf', 'entered Win32 before validation')]
```

应在任何进程/API/输出文件操作前验证数值类型、math.isfinite 和允许区间，并覆盖独立 check timeout。修复后重跑此负例；不得为证明问题实际启动无限等待进程。

## F4｜P2：嵌套 inline 完成会丢失 worker 状态（已独立确认修复）

位置：`tests/conformance/support/fixtures.py` 的 MockExecutor.submit/drain/shutdown；`tests/conformance/support/harness.py` 的共同 worker_wait_rejection。

初始代码以布尔 in_worker 表示 worker 内部调用。outer work 中同步 submit nested 后，nested 的 finally 将其写为 False；outer 仍在运行却能调用 drain()/shutdown()。初始独立探针输出如下，同时当前 harness 仍给合法 mock qualified=True：

```text
nested_worker_actions = [('drain', 'returned', False), ('shutdown', 'returned', False)]
mock_qualified_by_current_harness = True
```

shutdown 还会提前置 closed=True，违反 A22.2 的所属 worker 非法等待/关闭要求。父代理已保存真实 red，将 submit 改为保存/恢复 previous_worker，并在共同 worker_wait_rejection 内增加 nested submit；新增独立测试 `test_T19_conformance_nested_worker_rejection`。

复核者实际重跑：

```text
python -B -X utf8 tests/conformance/test_bootstrap.py ConformanceBootstrapTests.test_T19_conformance_nested_worker_rejection -v
Ran 1 test in 0.000s
OK
```

该缺陷的修复已由独立单项验证支持；完整 Conformance 回归仍应由正式采集器保留，不能把一个单项重验写成全套通过。

## 已核对的有效设计

- Win32 子进程以 suspended 状态创建，先纳入独占 Job 再 ResumeThread；仅终止当前创建的 Job，不枚举/终止真实产品或设备进程。Job 设置 kill-on-close，保留 root exit、active/total process 数及是否终止 owned job。
- stdout/stderr 直接写二进制流文件并保存字节数和 SHA；子孙未排空、timeout、异常退出及打印 Passed 后非零退出不会按普通成功处理。
- fixed expected 与 CTest discovery/JUnit 分开；固定名称、零测试、缺/多测试、skip、发现结果与本轮 raw、每轮独立 JUnit、重复轮次集合和实际命令 provenance 均有具体检查。
- 每轮显式执行并保留原始命令；没有 until-pass 逻辑。failed-then-rerun 创建不同 run_id，旧结果不能覆盖。
- 输入 ZIP/内容清单、依赖锁、产物 ZIP、当前源文件集合、当前二进制和 collector 文件摘要参与验证；历史模式明确仅检验快照，不能声称当前工作区已验证。hash 被明确作为完整性机制，不冒充外部审计签名。
- Conformance 的共同 6 项从固定 COMMON_CASES 运行，后端 descriptor 不能提供 skip_cases；实现身份/源码摘要/合同版本被核对。合法 mock 与重复 callback fault 使用同一 harness，fault 不登记为合格实现。
- parallel=false 的专项明示 NotApplicable 与合同表达式原因；Profile 要求缺失能力会失败，parallel=true 但没有真实线程 fixture 也会失败。此 G0 mock 骨架没有冒称 SQLite/BS pool/AssetStorage 的正式 Conformance。

## 验证边界与后续条件

本报告基于逐文件静态读取、两个独立轻量反例，以及修复后的单项嵌套 inline 测试。没有声称完整 process、evidence runner 或全部 Conformance 测试在本轮独立通过。父代理正在修复的文件可能继续变化；本报告发现依据保存了具体数据流、原行号和实际输出。实施者最终冻结后，应再次核对 F1–F3 的新增反例与完整回归，并将真实结果绑定最终源快照。

包级 Passed 仍需最终自动门禁、必需产物、完整独立审查与实际用户批准，不由本报告或实施者自签产生。

## 第二轮｜F1–F3 修复的独立定向复核

结论：本轮检查的 F1、F2、F3 均已有对应修复，并得到下述独立实际反例/正例支持；连同第一轮已独立确认修复的 F4，首轮四项问题可以关闭。该结论只覆盖所述修复及初始 b/c 审查范围，不覆盖父代理本轮后续新增的 gate/import 功能，不替代最终全量正式采集、包级审批或 G0 放行。首轮记录完整保留。

### F1：实际配置及工具链绑定

新增 `tools/evidence/build_identity.py` 从归档的实际 CMakeCache 读取源目录、单配置 build type、多配置可选集合；多配置还核对实际 build --config 或 build preset 所指定的 configuration/configurePreset。编译项目要求 toolchain artifact，按实际产物核对 dependency lock 中的编译器、编译器版本、生成器、toolset、SDK、CMake/C++ 标准，以及本次配置/CRT/ASan。validate.py 从源 ZIP/产物 ZIP 独立重算 observed，不接受只改 report 标签。

本复核者实际运行以下两个现有单项，退出 0、2/2 通过：

```text
python -B -X utf8 tests/tools/evidence/test_runner.py EvidenceTests.test_T23_evidence_configuration_mismatch EvidenceTests.test_T23_evidence_review_required_not_waived -v
```

错配置夹具的实际报告为 `build/evidence-fixtures/ock-evidence-fixture-hggtzqfa/evidence/626e0eaffce0-610e6a8045b2/fixture-debug/D0.06/20260907T060944Z-007ac1332847/report.json`。已回读 errors，精确包含 `build identity: single-config CMake mode differs from reported configuration`；不是其他构建失败意外满足“非 Passed”断言。该 fixture 的原始命令、归档与报告由现有测试采集器真实生成。

此外直接对 observe() 运行 7 个无进程分支向量：合法多配置、错误 --config、未生成的配置、错误 build preset→configurePreset 关系、编译项目缺 toolchain artifact、合法工具链、错误 compiler_version。两个合法向量无错误，其余向量均得到对应 build identity 错误。它们验证分支规则，不冒充真实 MSVC 二进制构建；最终真实 toolchain artifact 的产品组合证据仍由 D0.06-a/正式采集提供。

### F2：必需评审集合全部匹配

validate.py 现在从 spec.review_required 取得集合，要求含 human 且无重复，并校验报告 review.required 不得缩减；只把 task、输入摘要、Approved、approval_text 匹配的记录加入 approved_reviews，全部必需 kind 齐备后才允许包级 Passed。

除现有篡改 required 数组的单项通过外，本复核者用既有 EvidenceTests 建立隔离运行夹具，实际执行了三次完整 configure/build/discover/test 采集，并保持同一个 build_inputs_sha256：

| 夹具内评审材料 | automated_status | package_status |
|---|---|---|
| 无评审 | Passed | InProgress |
| 只有 human，仍缺 code | Passed | InProgress |
| human 与 code 均齐全 | Passed | Passed |

夹具位于 `build/evidence-fixtures/ock-evidence-fixture-3e1e68d5/`；三个独立 run 分别为 `20260907T061153Z-707a956f0470`、`20260907T061155Z-7c95f22e0dc8`、`20260907T061157Z-d3ee97f10709`，统一 source-id 为 `4901e3f8c848-cd4971451c6a`、profile 为 fixture-debug、task 为 D0.06。每次采集均由机器计算上述状态并保留 original-runs ZIP。

**这些是负例/正例测试中的合成审批材料，不是本任务用户批准。** approval_text 明确写有 `SYNTHETIC SELF-TEST FIXTURE; NOT USER APPROVAL`；其 Passed 只证明隔离夹具的集合算法行为，不能作为正式 D0.06 包批准记录。

### F3：有限 timeout 与严格 JSON

execute() 已在 Win32/输出文件操作前拒绝非 int/float、bool、非有限值以及超出允许区间的 timeout；JSON 读取器通过 parse_constant 拒绝 NaN/Infinity/-Infinity。

独立执行：

```text
python -B -X utf8 tests/tools/evidence/test_process.py ProcessTests.test_T23_process_nonfinite_timeout_rejected -v
Ran 1 test in 0.004s
OK
```

该单项实际覆盖 NaN、Infinity、-1、0、True、3601。复核者额外用 mocked Path.read_text 向真实 read_json() 提供 NaN/Infinity/-Infinity，三个输入均抛 ValueError；无进程、无文件创建。此前可能无界等待的值现在在进入 OS 前拒绝。

### 本轮界限

没有重复运行父代理正在进行的全套回归 session；本复核者只跑所列三个 unittest 单项、七个 build identity 分支向量、三个 JSON 非有限常量向量，以及三次独立审批集合夹具采集。parent 新增 gate/import 尚待另轮审查。当前结论是首轮缺陷修复获得独立验证，不填写用户批准，也不把局部验证写成所有平台/配置的正式通过。

第二轮复核结束时源文件摘要（UTC 2026-09-07T06:13:49.491022+00:00）：

| 文件 | SHA-256 |
|---|---|
| tools/evidence/build_identity.py | 23620838037ab4dd49ad30bc7ff96cdb29abbd67036a87edcd8d76a8119dee86 |
| tools/evidence/validate.py | fe359b85370ca75c02df153bdeeb4f5f92a2fc6142fb6df0e8a361432d379fcc |
| tools/evidence/process.py | b2a34e3ef1c02518b624e5295bc73e558a1a3f84d70aa753e78619c430a688a8 |
| tools/evidence/common.py | 874e19b1d999f0c262c6a1400025cccf3d3132454c5510363699347daffd054e |
| tests/tools/evidence/test_process.py | 5fa651141303eb38960b2b92bda96debe4ccc714f7bf768fc4ba7015c7c3b44d |
| tests/tools/evidence/test_runner.py | 7c9f0c967495e235e3d6efcb20db2256ec5a20b34504660c766e08c05e5c3ec6 |
| tests/conformance/support/fixtures.py | 5526a65ef59adffb4de6bf2b3e236e0020dddf149ac0e4de531fe084f3835b94 |
| tests/conformance/support/harness.py | e4710e89c7f328e0646b9d4765494156da9be056487726731a3cdee4c715b280 |


## 第三轮：gate/import 与故障子产物归档

本轮继续只读审查主集成者实现，不修改共享实现、测试或审批。对照 E03.3/E03.5 与 D0.06，gate 逐个调用真实 audit 并检查固定 task/profile 集合及同一 source/dependency 身份；import 最好也只输出 Incomplete，不把历史缺失的退出或构建来源补造为成功。gate CLI 返回的是自动检查状态，人工 gate_status 单列；主集成者已明确会在使用说明中强调，当前 G0 仍为 InProgress。

### gate/import 已执行的检查

实际运行以下 3 个新定向用例，3/3 通过：`test_T23_evidence_gate_missing_profiles`、`test_T23_evidence_gate_duplicate_runs`、`test_T23_evidence_legacy_corruption`。前两项各自实际配置/构建/执行 CTest，夹具报告为：

- `build/evidence-fixtures/ock-evidence-fixture-w7ai_hjg/evidence/cab2fb4d6314-d994f628122b/fixture-debug/D0.06/20260907T062612Z-3ed7d340cb1c/report.json`
- `build/evidence-fixtures/ock-evidence-fixture-5544g76o/evidence/26417b478cc7-b74614460166/fixture-debug/D0.06/20260907T062615Z-10eada7b0853/report.json`

另外运行 8 个纯汇总分支向量，明确以 mock audit 隔离聚合逻辑，不冒充实际进程证据：全部齐备、缺评审、缺配置、重复、额外配置、不同 commit、不同 build_inputs_sha256、不同 dependency_lock_sha256。结果分别为 Passed/Passed、Passed/InProgress、Incomplete/InProgress，以及其余五项 Failed/Failed，符合预期。

### 故障子产物新增三项：首次 2/3

独立执行 `runtime_artifact_integrity`、`stale_runtime_artifact_rejected`、`gate_mixed_source_rejected` 三个实际定向用例。后两项通过；第一项因测试 fixture 路径错误失败，没有被伪报成功。

失败完整目录：`build/evidence-fixtures/ock-evidence-fixture-bbl6tsb9/evidence/84e4ee61566d-02b518033d6b/fixture-debug/D0.06/20260907T062908Z-f3960ed139f3/`。原始 CTest 输出证明 cwd 为 fixture/build，而 case.py 又使用 Path('build')/fault-...，导致不存在的 build/build 子目录 FileNotFoundError；父进程观测实际 exit 8。报告还保留当前 CTest `status="fail"` 未被解析器识别的拒绝信息。已向主集成者回报；主集成者随后修正 fixture 绝对来源路径并支持实际 fail 状态，独立重验等待其冻结。

### 新发现 F5：P1，重复 runtime 行可逃逸最低产物数量

validate.py 对 ZIP 内容使用集合比较，却按 runtime_artifacts 的列表行数匹配 minimum；没有先拒绝相同路径重复。复核者额外建立真实隔离 CMake/CTest 夹具，仅生成 1 个新 artifact.bin，固定 manifest 要求 minimum=2。机器原报告正确 Failed，唯一错误为 `required runtime child artifacts missing`。

原始报告、源码、二进制、JUnit、子产物 ZIP 和原始命令全部保留于：`build/evidence-fixtures/ock-evidence-fixture-t45q9m3_/evidence/2d01c911c137-5f675f2b145a/fixture-debug/D0.06/20260907T063152Z-87e179ad09a1/`。

未修改任何原始文件，只对 read_json(report_path) 的内存返回值复制该 runtime 行，使列表为两行，并声称 automated_status=Passed、package_status=InProgress、errors=[]。随后调用真实 historical audit（所有原始归档、源码、退出、JUnit、ZIP 都正常重算），返回 errors=[]、package=InProgress，错误接受自动通过。选择 historical 是因为主集成者已更新 common.py；原报告仍绑定此前 collector，不能把版本变化当成本反例的拒绝理由。首次投影遗漏 package_status 更新时正确命中包状态不一致，补为同样缺人工评审的 InProgress 后才暴露以上数量逃逸。

建议在 validator 先验证 runtime 相对路径规范化、build 边界和声明 pattern，拒绝重复路径及 ZIP 重复成员，并按唯一匹配路径计数；只加 uniqueItems 不足以阻止同一路径使用不同元数据重复。run.py 当前通过集合发现新文件，但独立 validator 仍必须拒绝这类报告篡改。该发现已经交给主集成者修复；**F5 尚未独立关闭，不能以本轮其他局部用例通过代替归档完整性通过。**


## 第四轮：F5 与实际故障 fixture 修复的独立关闭

主集成者修复冻结后，本复核者独立检查了最终 validator：runtime 每行先要求规范相对路径、workspace/build 边界与声明 pattern；按解析路径的 normcase 身份拒绝重复；minimum 按唯一匹配路径计数。runtime ZIP 成员与行集合及数量一致；source/build ZIP 也增加重复成员检查。检查过程未修改实现或测试。

实际执行：

```text
python -B -X utf8 tests/tools/evidence/test_runner.py EvidenceTests.test_T23_evidence_runtime_artifact_integrity EvidenceTests.test_T23_evidence_runtime_duplicate_rejected -v
Ran 2 tests in 5.540s
OK
```

正常归档及篡改拒绝 fixture 的原始机器报告：

- `build/evidence-fixtures/ock-evidence-fixture-y432x4aw/evidence/a881b5c65181-3dbd7176144e/fixture-debug/D0.06/20260907T063543Z-b0fc78fcfee4/report.json`
- `build/evidence-fixtures/ock-evidence-fixture-b3ihet8n/evidence/69077d580950-43227fef4dee/fixture-debug/D0.06/20260907T063545Z-2aa526be20b9/report.json`

测试各自在故障注入前保留 original-runs ZIP；前一项随后故意损坏 runtime ZIP 并验证拒绝，后一项随后故意重复 runtime 行并验证精确 duplicate runtime 错误。因此当前夹具中的故障副本不是应当通过的正式报告。

更关键的是重新播放第三轮的**同一 minimum=2 实际归档、同一两行重复投影**，未用新输入替换反例。修复后的真实 historical audit 返回：

```text
duplicate runtime artifact path
runtime archive contents differ or contain duplicate entries
required runtime child artifacts missing
automated status is inconsistent with actual evidence
package status lacks complete evidence/review
package = Failed
```

原始 1 个文件仍不满足 2 个文件要求，不能再通过重复行冒充。另将第三轮真实失败留下的 JUnit 交给修复后的 junit_cases，准确返回 `T23.fixture.executed / Failed`，不再因 CTest 的实际 status=fail 只得到未知状态错误。

**独立结论：F5 已按原反例验证修复；测试工作目录错误及实际 fail 解析已验证修复。本轮 gate/import/runtime 新增范围未留未解决阻断发现。** 此结论是所列实现/反例的独立技术检查，不是 D0.06 全量正式采集结果或用户批准；父代理继续负责全量 68 项及各配置正式证据，G0 人工状态另行记录。第一至第三轮发现、失败、角色和局部验证界限全部原样保留。

第四轮完成时源码摘要（UTC 2026-09-07T06:37:36.603595+00:00）：

| 文件 | SHA-256 |
|---|---|
| tools/evidence/gate.py | 8e1526662d21c341baf6ad35cf139b039e6df65dd0e0fb774c7f8d3e3856013f |
| tools/evidence/import_bootstrap.py | 1e4260dc6356ab17749772457f27873fa1b2c5864290e38d1494e4aa0ee2b759 |
| tools/evidence/run.py | dba28a5db20ed02b903766d571373c3f7b8bf5e4fa0de0a327c55154615300f5 |
| tools/evidence/validate.py | a7691b7941097e3992351ff244487dd5876e2e1b767b142df9bd0220ef2f0581 |
| tools/evidence/common.py | 8d71265993b8cf1dfa8f4fdced0b88cf04dc24dc0d05bfccd02ebd832afe405d |
| schemas/evidence-v1.schema.json | cc735fbeb93419ddbc3d9667c7239c9de9a237936f0d860384064485eb0b410c |
| tests/tools/evidence/test_runner.py | ab13c61a04c97d40a39b5422a4343bc7a914a03d7a2f6c4b01a9949f4a58adee |

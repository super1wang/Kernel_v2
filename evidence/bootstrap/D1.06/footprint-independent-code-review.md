# D1.06 footprint 有限 hook 首批独立 CODE 复核

actor_type: AI  
reviewer: review_foundations（未实现本批 hook；此前整理 footprint 方法合同，该方法本身不在本页重新独立批准）  
status: ChangesRequested  
范围：有限观测 hook、纯工具与校准事实。不是完整方法、Native测量、pilot、预算或包级验收。

## 冻结输入

九项当前字节在本次读取时与 footprint-bootstrap-index.json 相同，集合摘要按 `SHA256(sorted(path+TAB+sha256+LF))` 独立重算为 `2341f290d6ac6e2b7fffd31bee0b5c5a3e4023439b6a8304ee4e266e785fb767`。

| 文件 | SHA256 |
|---|---|
| `tests/tools/footprint/test_observation.py` | `9aee637bcda66d2f416b252ed1354b206fc5db036a607e751cc3330b780143e6` |
| `tools/evidence/process.py` | `1f49872091ab5ec967a5c900a48e714284290e29c4f2baffad7cb614933c3c94` |
| `tools/footprint/analyze.py` | `1c8bf188f7730055d7ce673065c3f0d5dcff8b5fafe7018f702314f42c398db7` |
| `tools/footprint/configuration-development.json` | `4d735d553c4a2968ae0bc19aeb46ddca4bfc3590656fc3ee257b39bcc74429ea` |
| `tools/footprint/consumer/channel.hpp` | `30cea85d5563f603fe6935acb69481b73ffd74e9e2048c8d08c1f73a151fba08` |
| `tools/footprint/controls/calibration.cpp` | `edcac651533bbfac8cbb57387b4ebb0e225470f5c9d5aa7e307883fa67fd5e62` |
| `tools/footprint/controls/required.cpp` | `9d96040f24c83abf05cfbc4b0e53979d67f63697a615fa18c8c9def6ee97bb94` |
| `tools/footprint/run.py` | `827196aac705fc90e16d78549c4c3e49c8c2f33b1cfdaa0cc48af908e764c1c9` |
| `tools/footprint/windows_process.py` | `afd352c0efe47e753aad30c132d40f80ed131e491e45f65dfc51ec902e301476` |

## 两项新增阻断

### P1：耗时查询后仍可能在绝对运行截止之后 ACK

`tools/footprint/windows_process.py` 的 `_Scope.poll` 只在入口检查 `_deadline`，之后执行 `_sample`/`_modules`，最后直接 `SetEvent(_ack)`。查询或调度耗尽剩余预算后，仍允许消费者继续下一阶段，直到 process owner 下一轮才处理超时。方法 §6/§10 要求查询、阶段等待不能延长原 deadline；不可抢占同步 Win32 查询并不授权查询返回后继续发 ACK。

独立有限控制保存于 `footprint-independent-red-b1745514c0/test_deadline.py`：执行该目录冻结原 windows_process.py 的真实 poll，伪时钟固定 deadline=1，sample 把时间推进到2，实际 ACK 列表为[2]；应为空的断言真实失败。command.json 记录 Exited1、Job Resume前挂接、active_after=0、未终止Job。它是实际执行生产 Python 函数的纯时钟控制，不是一次新的 Win32测量或真实OS超时实验。

修复需在可能耗时查询后、每次 ACK 前重查原绝对运行截止；如同时涉及阶段截止也不能通过重设窗口掩盖已过期事实。沿原 owner 异常终止/排空记录首因，不新增采样线程或第二进程管理器。

### P1：校准前置命令只检查退出码，可忽略失败的 Job 状态

`tools/footprint/run.py` 校准 configure/build/imports 分支只检查 `exit_code`。真实 process.execute 可以在根退出0而子孙存活时返回 DescendantsAlive、exit_code=0、terminated_owned_job=true；这不是前置构建成功。当前代码继续调度下一命令，后续观测控制若正常，顶层局部 result 仍可写 Passed，未覆盖前置的失败状态。

同目录 `test_prerequisite_v2.py` 执行冻结原 run.py 的真实 main，注入上述结构化结果和纯文件夹具，观察命令列表实际从 configure 继续到 build；要求仅configure的断言真实 Exited1。`prerequisite-v2-command.json` 证明测试进程正常owned排空；没有真实运行CMake，也没有假称观察到真实存活子孙。首版 prerequisite-command.json 因独立导入路径不足失败，保留为测试夹具导入错误，不作为逻辑red。

configure/build/imports 以及 guards 成功门禁应统一要求 Exited、exit_code0、assigned_before_resume=true、active_after0、terminated_owned_job=false，并增加各失败记录拒绝和正常记录正控制。错误原始流/根退出码继续独立记录；不能只改最终显示文字。

## 已核对的实现与原事实

- process.execute 仍是唯一 CreateProcess/Job/Resume/Terminate owner；观测配置不是回调/PID/handle输入。创建暂停、归属后首次查询、显式 STARTUPINFOEX 继承列表、运行失败沿同Job清理；查询使用原hProcess，HMODULE不CloseHandle，线程快照句柄在finally关闭。
- None 源码差异保持原Resume后timeout起点、10ms轮询和原始输出路径；八项新护栏与原七项真实process回归分别核对，未用None子孙测试代替观测分支未做的故障窗口。
- 固定80字节协议验证身份、nonce、generation、PID、阶段/序号、非未来ticks与checks；root正常早退仍记录Incomplete，坏nonce/样本容量异常有独立ObserverFailed；不从root0补造观测完成。原始二进制stdout/stderr不被采集错误改写。
- 独立读取 footprint-guards-41ff95618ae3：原5项护栏及7项真实process回归OK；footprint-guards-final-9efd3a19f05a：8项OK；各命令原始流SHA相等、active_after0。
- footprint-calibrate-62da4e053df4 的12命令及raw摘要核对相等，九项局部controls为真；configure/build/imports在这一实际轮次均确实Exited0，所以本次记录不因上文潜在门禁缺口被追溯改写失败。坏nonce/容量分别LaunchFailed且真实根125/ObserverFailed，早退Exited0/Incomplete，运行期限Timeout，所有actual active_after0。这些失败没有被写成方法成功。

## 保留的已知边界

自审已明确的5ms覆盖不足、基线多线程尚未归因、末尾额外阶段事件、创建/未归属/阶段/子孙/sentinel等完整故障窗口、轻量时延/窗口报告、正式身份与预算schema、Native/pilot/formal未执行，均保持未完成，不重复列为本次新发现。尤其Complete目前只代表协议采集完成，局部Passed只对应九项校准控制，不代表采样覆盖达标或线程0；budget_status仍NotApproved。

本页要求修复两项新增控制流缺陷后再做限定增量CODE复核；不要求重跑无关正式矩阵。未修改生产源码，未运行正式测量。最初九文件冻结、自审及失败证据全部保留。


---

## 两项阻断修复的有限增量复核（2026-09-08）

actor_type: AI  
reviewer: review_foundations（独立 CODE）  
status: Approved（仅上述两项缺陷修复；原首批ChangesRequested与完整方法未完成事实保留）

逐字核对 `footprint-review-fix-37d8fc9332/source` 与当前四文件相同：

| 文件 | SHA256 |
|---|---|
| `tools/footprint/windows_process.py` | `62b82880f7088d5d6eb262d515977709e5bf8c0aa21abc3430b2eb28a2a4c052` |
| `tools/footprint/run.py` | `3471a9a301e5fdc0622ef160eb842b36baa61300c14d15c6be606017ed341d45` |
| `tools/footprint/analyze.py` | `63cc4db5babac2af3f947e68d90872a66c4ce7f7de7cd6233719ee8af04d1981` |
| `tests/tools/footprint/test_observation.py` | `9e75e490b785d3a46ef82752f36bcd4b2c5424a3b0af5b146a11467cc784de4c` |

process.py 保持原 SHA `1f49872091ab5ec967a5c900a48e714284290e29c4f2baffad7cb614933c3c94`，本修复未改None进程路径。

1. poll 在阶段 sample/modules 后同时重查原运行与旧阶段截止，再允许建立下一阶段期限；任何ACK前又检查运行/阶段截止。原始绝对预算没有被查询或hold延长；仍沿既有异常清理路径失败，不增加线程或callback。
2. owned_success统一要求Exited、exit0、assigned_before_resume=true、active_after0、terminated_owned_job=false；configure/build/imports/guards均使用它，require_complete也接入并补缺assigned拒绝。已检查调用点不是仅增加未使用的helper。

两份独立red控制 `test_deadline.py` 和 `test_prerequisite_v2.py` 与原失败文件字节完全相同，本轮只换为修复后的被测源；两者实际Exited0。既有八项护栏同轮实际OK，包括require_complete的归属材料拒绝；共三条命令均owned正常排空、未终止Job，raw SHA逐项相等。四源码和运行材料绑定于该轮inputs/commands，未重跑原校准或正式测量。

本页仅关闭这两项具体代码缺陷；此前列明的5ms覆盖、线程归因、末尾额外事件、完整故障窗口和Native/pilot/预算等仍未完成。没有把这次纯控制通过称作新Win32绝对截止校准、完整有限hook验证或D1.06/G1 Passed。


---

## 有限 owner 控制增量独立 CODE 复核（2026-09-08）

actor_type: AI。结论：Approved，仅针对十二源集合 `3744dda7de3d321396c60e41efb9e23115f75775d823c0f09a30ad9125f4024f` 的末尾事件检测、严格 owned 返回类型保护、永久回归与固定控制消费者/驱动增量；不是 footprint 全方法或包级 Passed。此前 ChangesRequested、已关闭两个阻断及所有失败记录保持原样。

逐文件 SHA、集合重算、三轮冻结源差异、全部索引/原始日志摘要及 owned 事实回读见 `footprint-owner-independent-check.json`，SHA256 `15ae4886e17d977047aa876e9846688c2bf8ebf3546f3871c938970dcd56e2f2`。十二文件当前字节逐项匹配受审索引；集合算法为排序 path/TAB/sha256/LF 后 SHA256。相较真实 OS 轮，当前仅 analyze.py 多一个 isinstance(tree,dict) 条件；本次未重跑 OS，不拼接为当前十二源上的整轮成功。

代码核对：root_exited 在原 root 已退出后只非阻塞检查本次自己持有的事件句柄；有未消费事件即 ObserverFailed，正常路径仍要求完整协议且无 pending ACK；finally 使 scope 失去绑定，后续内存/线程查询拒绝。没有查询退出进程、替消费者补 ACK、外部 PID 附着或新观察线程。事件 Win32 失败继续抛向原 owned 异常/清理边界。None tree 明确返回 False，未改变严格 assigned_before_resume/active_after/terminated 条件。

固定 Python 消费者只使用继承通道，提供完整十一阶段、尾后额外事件、root 退出但所属子孙仍活和绝对阶段期限等控制。sentinel 由另一轮同一 execute 拥有，辅助线程仅在测试驱动协调；同 EXE 不同 PID，原 Job 清理后它仍等待释放并正常结束。没有按名杀进程。消费者退出依赖 OS 回收其测试句柄，不冒称这是产品 Host owner 回收证据。

永久回归保留原两项独立发现：查询耗尽运行/阶段期限后不得 ACK/重置 deadline，以及不合格前驱不得构建；新增 root 退出事件检查和此后查询拒绝、None tree 等严格成功条件。它们的伪时钟/伪命令是确定性护栏，明确不代替实际 OS 控制。

| 原始轮次 | 独立核对结果 |
| --- | --- |
| footprint-owner-extra-6233535fd310 | 真实控制发额外事件后退出；旧 owner 实际 Exited 0、11 阶段/12 样本且误报 Complete，断言 false；原 result Failed 保留 |
| footprint-owner-all-2407ed0d4d7c | 整体仍 Failed：永久护栏 Exited 1。其余五 OS 断言成立：正常 Complete；额外事件 ObserverFailed/UnconsumedPhaseEventAtRootExit；root=0 子孙存活由 Job 清理并归 Incomplete；外部 sentinel 正常存活至释放；阶段限期实际 LaunchFailed/root 125、ObserverFailed/StageDeadlineReached 后排空 |
| footprint-owner-guards-05c96273fe61 | 与当前十二源一致；12 护栏、无 skip，实际 Exited/observed 0、Job 正常排空。只重跑护栏，不称重跑了 OS |

每轮原始业务退出码、观测状态、终止 Job 与 active_after 分开记录；失败控制的预期观测拒绝不伪造业务成功。actual OS 轮整体 Failed 与之后护栏通过保留来源差异，证据足以支持上述局部修复的代码结论。

范围外继续保留：新采样方法尚待实现/校准，5 ms 覆盖、线程真实归因/短命盲区、完整其余故障职责、Native 安装配对、pilot、正式预算和新正式样本。另页 sampling amendment 的有限 SPEC Approved 不能自动把本旧采样实现认作新方法，也不能复用这些 latency 协议控制为占用统计或正式 Ready 时延。


---

## v2 有限采样与身份修复独立 CODE 增量（2026-09-08）

actor_type: AI。初版 26 源集合 `8778564bab2711ad492c17dcd55ba269bcfe338f5e70aff2a6b0e5d0f527976e` 一度 ChangesRequested：require_complete 仅在 method 恰为 /2 时执行新身份检查，未知/缺失 method 可绕过查询元数据验证。我独立用真实单对记录副本删除方法摘要和查询字段，设置 null 或 /999，冻结原分析器的两子断言真实失败，owned Exited 1，保存在 footprint-v2-independent-method-red；原始测量记录未动。

修复后有限 CODE Approved：采样 occupancy 路由/查询事实、held-thread 边界控制、新 v2 身份入口及对应护栏。结论绑定原 26 源加三个明确替换文件：analyze.py `4bf8208897c7766c178f74db3dcf08eb4b2edc2ba6b764ba87c48116b0557a13`，test_observation.py `79e0970390c25c11feadffed2549bbd96d659417f2c5a0a7b64a88bc344555b2`，test_sampling_v2.py `93a25fd2eef1c5cbda2d7c2527fce07939ac40b783f5ad8e9d7420a6696c879e`。其余来源不移花接木；后续 allocation 或 latency 身份增量不由本记录批准。

实际实现将 periodic 限为原进程句柄的内存查询，thread_ids/query=null；assigned_suspended 与十一固定阶段均真实查询线程，分别记录查询起止、成功位、错误码与结构长度，失败保留 null/时刻而非复制旧集合。Ready 查询仍先于 ACK，原两处模块枚举、固定停留、容量和绝对期限不改变；process.py 未改，无新增观察线程。摘要只统计真实边界线程点并标明 scope，方法摘要包含 /2 与采样策略，不能把新样本当旧 v1。

方法入口现强制 /2 并无条件核对配置摘要、两通道策略和十二个边界；缺失/null/v1/未知都拒绝，不为旧历史报告添加假兼容。基础合格合成记录补齐 v2 事实，原错误树/缺样反例仍在。修复红 263478fef16d 原始 Exited 1、绿 552bff69828a 冻结当前三源后 18 护栏 Exited 0/无 skip，均实际排空。独立再次只读校验旧 none/thread/baseline/native 四条 v2 真实记录，重算摘要与归档完全相同；未重跑 OS。

原始证据核对：v2-red-58b142149ece 实际失败；held-d3d9e5cba335 的 18 护栏及两控制 raw、Job 均匹配。复用 EXE/DLL 的 SHA/字节与三消费者源回读一致。thread 进程同一组四线程在 Ready 增为五、ShutdownComplete 恢复原集合，证明保持到边界和 join 后消失；独立 none 的四线程归因仍 Unresolved。2fe91f109e7e 七个命令包含校验/配置/编译/两导入/A-B，均真实正常退出与 Job 排空，26 冻结源、安装文件摘要及消费者原始记录对应。不在本增量重复批准新 Native 消费者全部实现。

Debug 单对 A/B 各 304/305 内存样本、12 线程边界；实际 missed intervals 为 742/650，方法摘要均 `63fc080f01f6066fcceba092d844a9554cc6e40137710297883cf3079babb3a7`。不能把减少 Toolhelp 次数称为完整 5 ms 覆盖。四线程未知、边界间短命线程盲区、counter_mode=disabled、sampling_coverage=NotEstablished、allocation_counting/formal_latency=NotMeasured、pilot=false、budget NotApproved 均保留；相同数量不证明新增线程 0。

机器核对：footprint-v2-independent-check.json SHA256 `b5e3ec8a08f257cbf273ea1a26939c8f49829d1a4a3612f59cbaf641334c26a4`；footprint-v2-identity-independent-check.json SHA256 `9e239fb8d1f9a5d27c25127e43685f83c270f1578762db275ae7751a8b7c42e1`。报告身份修复未改变已有 OS 采样。另已知 latency 执行不做周期采样但元数据仍写 due_5ms，主任务正在另作有限身份修订；本结论不批准该旧 latency 口径或正式时延结果。D1.06/G1 继续 InProgress。

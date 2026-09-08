# D1.06 NativeSubset 测试范围

状态：**Candidate**。本文件与 [Host](native-host-api.md)、[Logging](logging-api.md)、[SDK](native-sdk-surface.md)、[footprint 方法](native-footprint-method.md)一起作为一次合并 SPEC 输入；依据 v3.3 D1.06、A19、A21、A22 及[本批次策略](../plans/D1.06.md)。这里只固定新增主名、必需子断言、影响映射和集合选择原则，不生成 discovery/expected/run manifest，不表示已实现、已运行或 Passed，不新增审批链。

实现稳定后，主任务把下列职责落实到正式 manifest，核对名称、配置、子工件及执行身份；不得从发现列表反推 expected。每个主名下的子断言必须全部成立，有限循环/参数化不是跳过某个故障窗口的理由。编译失败只证明对应编译边界，空测试、缺产物和缺运行不得算业务反例通过。

## 1. Host 固定新增主名

以下主名属于新 Host runner/包装。同步回调采用有限测试控制器，线程交错使用明确屏障；真实对象析构哨兵与实际返回值必须被观察，不以预先写好的事件数组替代事实。

| 主名 | 必需子断言 |
|---|---|
| `T03.host.configuration` | 空 owner、非空指针却无控制块的别名 owner、非法/零/越界预算及乘加溢出拒绝；配置深拥有；create 不认证、不调用模块或日志 factory；真实 HostIncarnation 冻结，随机源失败/全零拒绝且无时间戳回退。故障注入不得增加公开伪身份入口 |
| `T03.host.catalog_registration` | add 仅 Configuring；首次失败粘性；DAG/版本/重复/声明错误无部分可用目录；真实 Catalog.module_order 是原 publish 顺序；NativeSubset capability 与实际端口一致：async/resource/provider 不可用声明拒绝，RestoreMode::Absent，find/scan 返回能力不可用，不支持的async/wait声明启动拒绝，实际同步调用submit计数为0（测试端口意外调用即失败），无队列/新线程，不造 Task/Document |
| `T03.host.start_order` | 验证→日志装配→Policy→模块拓扑启动→Ready；成功步骤立即承担清理责任；Ready 只在所有必要条件成立后发布；start 不可重启 |
| `T03.host.start_failures` | 登记、冻结目录、factory 返回/异常、日志 owner/身份、SafeLogger State 分配、初始 snapshot、Policy 装配、逐个模块 start 返回/异常各有实际窗口；后端一旦返回先登记 owner，facade 构造/验证失败也实际关闭，异常/关闭失败保留 owner 并可重试；尚未返回的 factory 资源及失败模块靠自身 RAII 回收。失败启动实际按成功步骤逆序 modules→Policy→logging 清理，与正常 shutdown 的 Finalize 先关闭 Policy 区分；首错保留，未排空 Failed 可继续 shutdown |
| `T03.host.ready_gate` | Ready 前 open/verify/bind/invoke 拒绝且 handler=0；Ready 后可用；StopAccepting 后拒绝；active_admissions 达有限上限返回 Busy，无半个准入 |
| `T03.host.native_results` | 真实 Read/Compute 值及 ReadCompleted/Result 成功；非法参数 handler=0；沿原授权、目标、线程、预算、结果传输与错误规则；不以 Completed 外层掩盖 FailedBeforeApply；不新增裸分派 |
| `T03.host.session_ownership` | 外壳析构/移动覆盖不提前撤销已有 Bound；显式 close 立即沿 Policy 撤权；最后底层 owner 才回收名额；移后会话/绑定拒绝；错误主体、错误会话和错误 Host 不串用 |
| `T03.host.delegation_revoke` | restrict 只收缩已有授权；扩大/过期/错误身份拒绝；已绑定操作仍逐次观察真实撤权/截止，不能缓存成功绕过 Policy |
| `T22.host.lifecycle_reentrancy` | 所有模块/认证/线程/日志端口在 Host 锁外；回调可真实重入 snapshot；start/shutdown 并发 Busy；同 Host 准入及生命周期回调中 shutdown 返回 Reentrant 且无状态副作用；跨 Host TLS 不误判 |
| `T22.host.shutdown_order` | StopAccepting→等待真实在途→Policy close_store→模块逆拓扑 stop→日志 close→Stopped；quiescent=false 保留当前及依赖；true+error 记录后继续；已完成项不重停，成功重复 shutdown 幂等 |
| `T22.host.shutdown_errors` | 模块与日志关闭的可达错误/异常分别保留实际首因及清理责任；当前Policy close_store在准入归零、无异步frame时只返回成功，实测成功/顺序/关闭后拒绝及owner寿命；Host对Policy错误/异常的保留分支做源码核对，注明无可重复触发路径，不冒称该分支实际Passed；不清理完成不能报 Stopped；清理记录固定容量、截断/饱和与域/code正确，异常 what() 不入报告或递归日志 |
| `T22.host.shutdown_deadline` | 已过期截止不执行下一步；实际 steady_clock 截止不受 Policy 测试时钟延长；等待在途及每次回调前后检查；超时保留 owner，显式后续调用可继续；最后步骤实际完成但返回过期时 quiescent 与 deadline_exceeded 同时如实报告；不声称抢占违约回调 |
| `T22.host.pending_report` | Admissions/PolicyStore/逆序 Module/Logging 按真实未完成项列出；模块名、在途数、pending_total/written/truncated 精确；空/小输出不代表排空；Complete 为 0/0/false；输出仅值复制，不转移 owner |
| `T22.host.admission_race` | 双线程分别强制 invoke 先准入与 StopAccepting 先迁移；已进入调用完成原真实结果，后到调用拒绝；截止时仍在途不得早释放；本测试线程只属于测试工件，不计作标准 NativeSubset 新线程 |
| `T22.host.return_lifetime` | Host owner 与准入保持到最终 InvokeReply 返回对象构造完成，不能提前 decrement；支持回调中释放 Session/Bound 外壳；通过实际受支持 move-only/void 结果验证，危险传输类型仍拒绝。调用侧结果析构纳入完整分配窗口，其所需 owner 由结果值合同负责；不要求 HostAdmission 延续到调用者析构，不改变 InvokeReply 为持准入容器 |
| `T22.host.destructor_guard` | 正常 Stopped、无成功步骤的配置态、已排空 Failed 有返回正控；Ready 未停止、持准入自毁、未排空 Failed 分别真实子进程 fail-fast；记录特定诊断与实际退出，非任意非零即成功 |
| `T19.host.logging_ownership` | 默认日志与 Host/stream 同身份；模块 start 内真实 Accepted、snapshot/flush；公开 copy_logs 有界读取/gap；自定义 factory 无默认管理句柄时明确 UnsupportedCapability；关闭最后事件为 log_closing，真实 Stopped 来自报告；日志故障不更改业务 Outcome，但关闭失败妨碍 quiescent |
| `T23.host.allocation_controls` | 复用 D1.05 受审 C++/Debug CRT/独立 ASan 通道：12 入口独立正控、无分配负控、真实注入 new、ASan 幂等注册和真实槽满失败；Release CRT 为 null，ASan 原 CRT 盲区保留，通道不相加 |
| `T23.host.steady_allocation` | 小型定长 Args/R、真实 HostBound、4 次有限预热后 40 次逐次成功；每个整窗包含准入、原治理、事实环、最终返回和结果析构，各有效通道新增分配分别为 0；日志保持装配，普通 invoke 不额外生成普通日志；计数失效使窗口无效 |
| `T23.host.owner_reclamation` | 首次/注册/bind/预热/错误/可变结果与释放实际成本另报；ShutdownComplete、BoundReleased、SessionReleased、OwnersReleased 各真实 owner/析构/保留字节可区分；停止后旧 Bound 只拒绝，不假称外部 owner 全释放；独立管理日志 owner 专项不混入默认占用 |

## 2. Logging 共同五项 × 两个合法后端

两工厂固定为真实 `memory_drop_oldest` 与独立实现的合法 `test_reject_newest`。同一个 LoggingConformance 套件接收工厂和测试侧控制器，不能逐后端复制宽松断言。以下十个主名是完整笛卡尔展开；故障后端只用于证明拒绝资格，永不列为第三个合格后端。

| 共同职责 | Memory 主名 | Test 主名 |
|---|---|---|
| 接受/拒绝/丢弃 | `T22.logging.memory.accept_reject_drop_accounting` | `T22.logging.test.accept_reject_drop_accounting` |
| 格式/脱敏 | `T22.logging.memory.format_redaction` | `T22.logging.test.format_redaction` |
| 错误隔离 | `T22.logging.memory.error_isolation` | `T22.logging.test.error_isolation` |
| flush 接受前缀 | `T22.logging.memory.flush_accepted_range` | `T22.logging.test.flush_accepted_range` |
| 关闭/回调寿命 | `T22.logging.memory.shutdown_callback_lifetime` | `T22.logging.test.shutdown_callback_lifetime` |

共同项的完整子断言沿 Logging 合同 §8；这里固定不得遗漏的边界：

- capacity=2 连续三写，分别观察新接受+旧淘汰与明确 Full 拒绝；过滤、非法记录、Busy、Closed 各归一个原因；仅静止未饱和时断言守恒，极值不回绕且无序号复用。
- 固定事件/字段 golden、0/4 字段、排序、未知/重复拒绝、秘密哨兵默认不保存、尾部清零；返回后修改输入不能改变记录，没有自由 message API。
- facade 与 backend 独立计数；接受前异常、控制错误和非法返回不递归且不改真实业务结果；接受后抛的非合格后端由控制器检出，不伪造已修复的接受事实。生产 Memory 无故障开关，其必需错误路径仍实际执行，第三方异常由合法/故障测试后端承担。
- `flush(H)` 覆盖值保持 H，返回淘汰前缀为 `min(E,H)`；E<H/E=H/E>H、H=0、跨流/未来、间隔淘汰及关闭后旧 H 都覆盖。幂等不要求两次返回字节相同，不重复业务语义动作。
- Closing 拒绝新写，失败保留状态并显式重试；跨方法重入、同 backend 两 facade、A→B→A、独立 B、16 帧上限及跨线程实际验证；同步释放 facade 外壳和异常回程仍由局部 State 保活；移后空外壳与最后 owner 回收可观察。

补充两项本包实现职责：`T19.logging.memory_pages` 覆盖真实 copy_records 页公式、全局 E/gap、未来游标、空/超页容量、失败不改输出与分页间淘汰；`T23.logging.fixed_write_allocation` 在两个合法后端的固定写入及持续满载下测有效通道零新增分配，初始化/首用/预热/最终释放成本另报，不与 Host invoke 窗口混称同一工作。

固定能力为 async=false、file=false、callbacks=false、volatile_retention=true。只有对应异步/磁盘专项可按合同表达式 NotApplicable；共同满载、错误和寿命不可跳过。共同缺项、错版本/工厂摘要、伪 capability、fault 冒充 qualified 必须由既有 conformance harness 护栏拒绝。测试工厂和控制器不进入生产 Runtime。

## 3. SDK 八项

沿 SDK 合同固定如下八个主名，子断言不再建立第二份相异定义：

| 主名 | 本次必须观察 |
|---|---|
| `T24.native_sdk.metadata` | dev.2 / NativeSubset / 能力与实际 STATIC 一致；未知组件和伪完整 Runtime 拒绝 |
| `T24.native_sdk.public_headers` | 每公开头独立包含、两 TU 真实链接、宏不污染、唯一声明与受审 detail 包含边 |
| `T24.native_sdk.installed_host` | 独立搬迁 find_package，实际 Read/Compute/拒绝/停止；没有源码或 tests include |
| `T24.native_sdk.no_tests_producer` | BUILD_TESTING=OFF 仍实际构建、安装、链接和运行同一 Runtime |
| `T24.native_sdk.link_closure` | CoreContracts PUBLIC、系统 bcrypt LINK_ONLY、真实最终链接；删必要链接后出现对应未解析符号 |
| `T24.native_sdk.install_pruning_rejected` | 正控后分别裁剪 `.lib`、公开头、实际模板所需 detail、expected，独立副本逐次核对真实缺失诊断 |
| `T02.native_sdk.private_dispatch` | 公开 Host 正控；dispatch/check/inspect、裸 getter/handler、伪构造 Session/Bound 独立编译拒绝 |
| `T01.native_sdk.surface_guard` | 意外头变化、未知 LINK_ONLY、丢依赖、错分类/阶段不能过关 |

旧 dev.1/Runtime 不可用及内部库关系断言需按 SDK 合同受审更新，不回写旧 expected 或历史 Passed；元数据变更不是正式旧版兼容证明。

## 4. footprint：方法验证与实际报告分开

方法主名固定如下。合成反例仅证明分析/判定逻辑；真实控制的子命令、OS 观测和退出事实不可被合成结果替代。

| 方法主名 | 反例/正控制职责 |
|---|---|
| `T23.footprint.method_identity` | 对称 A/B shim、真实被引用的 Host/Native、配置/CRT/ASan/LTO/二进制/依赖摘要；禁止空消费者与不匹配配对 |
| `T23.footprint.module_accounting` | EXE+必要分发模块增量；遗漏实际必需测试 DLL 必须失败；系统 bcrypt、CRT/ASan、PDB/静态库分列，不偷减或冒充分发 |
| `T23.footprint.memory_calibration` | 有限 VirtualAlloc 提交并触页与无新增区负控；查询失败、截断或缺样不能补零；PrivateUsage、WorkingSet、寿命峰值和采样峰值不混淆 |
| `T23.footprint.thread_clock_calibration` | 真实保持测试线程与不新建测试线程的基线负控，真实系统/CRT既有线程单列归因；按实际 root PID 过滤；受控 Ready 延迟进入正确定义，QPC/frequency/ticks有效，不用 UTC 推间隔 |
| `T23.footprint.phase_repetition` | nonce/版本/阶段/序号/ticks、Ready 屏障先捕获高水位再预热；少轮/重复/越序/挑样/Ready 后崩溃拒绝，保留 ABBA 有符号配对 |
| `T23.footprint.owned_process` | 复用唯一 process.execute；None 原行为正控；暂停→Job→恢复顺序；各故障窗口、外部同名 sentinel、root退而子孙存活、scope迟到、容量满、任意callback拒绝及超时不被采样延长；真实排空和句柄寿命 |
| `T23.footprint.raw_integrity` | NUL/非UTF8原始流、独立stdout/stderr、伪hash/缺样/错binary/重复run/缺Job结尾均检出；root exit 0但观测不全不Passed |
| `T23.footprint.budget_provenance` | 缺预算、错误方法键、非有限值、自动加余量、后补审批时间或复用pilot作最终样本均拒绝；先审批有限配置，再新报告 |

实际测量主名为 `T23.footprint.native_occupancy`、`T23.footprint.allocation_report`、`T23.footprint.ready_latency`。前者承担各阶段 Private/WorkingSet、线程、Ready 高水位、实际分发及释放曲线；第二项承担独立计数模式中真实 HostBound 与治理完整整窗及其他成本表；第三项只用无计数 Release 的轻量模式测内部/创建到/父观察 Ready，不能把高频诊断样本混入时延统计。Host/日志直接分配测试验证行为边界，allocation_report 验证按测量方法生成的真实轮次；它们不是互相替代关系。

实际报告严格沿 footprint 方法：每个适用配置/模式 pilot 3 个 ABBA 块，预算后新 run 6 块；4 次有限预热，40 次逐次成功整窗；内存5ms到期尝试、线程仅assigned_suspended及十一固定阶段边界（v2新身份，短命线程盲区单列，未知归属不Passed），Ready 后静置 500 ms、采样 1000 ms，各释放阶段 500 ms。控制校准必须对应本报告的实际模式/身份，不能拿不同配置旧绿控制代替。标准 NativeSubset 新增线程=0；固定完整成功窗新增分配=0。有限占用/时延预算仍走已有 AI 自动政策，以实际 pilot 为依据，不增加平行批准链；预算未齐时可采集，G1不能Passed。

## 5. L0 影响映射

`S_changed` 采用显式路径映射加必要传递影响。下表是本批次必须补入开发入口的规则；当前 tools/dev 尚未支持的新 Host/Logging/footprint family 不能靠“名字看起来匹配”假称已选择。未知路径退回当前包全影响集；固定集合尚未存在时报告缺失，不使用 D1.05 旧成功替代。

| 变更 | 默认 Debug 影响集 / 扩大触发 |
|---|---|
| `packages/runtime/host/**`、Host 私有实现与测试 | 新 Host + 直接 Native 调用；准入/返回/关闭/owner 变化加相关 Policy/Native 寿命和 ASan |
| `packages/runtime/observability/**`、日志实现及 `tests/conformance/logging/**` | 两后端共同集、管理分页/分配、Host日志装配/停止；寿命或重入变化加 ASan |
| `packages/runtime/include/**`、`packages/contracts/**` | 公开头/模板/声明、Contracts、Registry、Policy、Native、Host、SDK 全直接传递消费者；Release与ASan风险专项 |
| 原 Registry/Policy/Invocation 源及对应测试 | 本组件全部直接合同，加依赖它的 Host/Native；修改目录顺序、权限或返回寿命时扩大相邻组件 |
| Foundation、根CMake、`cmake/**`、SDK manifest/安装 | 广泛原生合同、架构、干净构建及独立安装；核对实际CRT/ASan/链接闭包；不能只跑新增Host |
| `tools/footprint/**`、方法控制和消费者 | 方法护栏、对应真实校准/小范围采集；进程owner变更必须加原Evidence进程与原始流护栏 |
| `tools/evidence/**`、固定集合/discovery/fixture | 对应工具反例与真实结果核验；受影响消费者精确集合、setup附加项及少跑拒绝 |
| 示例或测试工厂/支持头 | 对应业务消费者及所有使用者；不能因文件在tests下而省掉传递影响 |

L0 默认 fail-fast，先新增反例/正控和直接影响，再继续微批次。纯文档不伪造测试结论。扩大理由只记录“改动→风险→集合”，沿已有自动审查政策，不逐项建立审批节点。

## 6. 正式 S_required 与 G1 S_gate

`S_required(profile)` 由以上新增职责、该配置适用项、受影响既有合同、SDK及工具护栏共同组成。稳定实现后一次形成可核对的具体ID/配置/工件集合；来源是规范与本合同，而不是当轮discovered。附加 CTest fixture setup 也纳入 expected/executed，不当作可忽略噪声。

- 新 Host 与 Logging 行为在 Debug、Release、ASan 验证；分配通道按配置分别成立。纯值/JSON工具护栏无需机械复制到三个 C++ 配置，但真实配置传播、编译/链接、生命周期和计数风险不能只由 Python 单测代替。
- SDK 八项在 Debug完整覆盖；Release/ASan至少覆盖实际公开模板/私有编译边界、安装Host、无测试生产者及链接闭包。纯元数据/裁剪/图规则的配置无关反例可单次运行，其适用性与当前配置身份仍需在实际消费者中核对。
- 当前公开头迁移、CoreContracts新增日志合同、生产Runtime统一静态链接直接影响既有 `.contracts.`、`.registration.`、`.policy.`、`.native.` 家族；Debug必须广泛执行这些固定原生合同及必要Foundation消费者，不能只选Host冒烟。Release/ASan根据实际声明、所有权、撤权/调用、错误与模板变化纳入相应既有用例，具体ID在稳定manifest中展开。旧案例的新运行属于D1.06回归，不重新判定D1.05历史包状态。
- 既有 conformance harness 的共同项不可豁免、伪capability/fault资格、工厂/版本/实现摘要等规则需验证本次接入。原证据原始流、Job、discovery/fixture护栏按工具改动进入影响集合。
- 不机械继承旧 Python execution/outcome/commit/intent/effect/restore/backup/gc/observer 等完整三配置矩阵。其对应生产能力本包不存在；只有公共合同语义或工具输入实际影响这些模型、或独立审查指出明确跨包风险时，选取有依据的具体回归，通常一次工具环境即可。不能把排除无关模型扩大为排除现有原生治理检查。

`S_gate` 面向 G1 的真实 Native 闭环：正式 Host/Logging/原生治理关键回归、C-A安装消费者、最小组件闭包、有效计数、线程0、全部真实footprint模式、已先审批的有限预算及来源一致的机器报告。它不是“所有历史CTest在三配置再跑一遍”，也不能只剩占用图表而缺授权/非法输入/停止事实。

D1.06与G1重叠项，在现有工具能核验**同一最终来源、工具链/配置、精确集合与原始工件**时，可引用同一真实报告；否则保留明确缺口并执行必要项，不虚构尚未实现的分区复用工具。既有D1.01–D1.05批准只证明前置已满足，不能替代本次修改后影响集；本次回归也不覆盖或改写旧失败/成功报告。SPEC/CODE、预算决定、自动结果和包/G状态继续分列。

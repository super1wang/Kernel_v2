# D1.06 有限进程观测首批事实与自审

当前是 **校准与原始采集 bootstrap**，不是完整方法达标、pilot、NativeSubset 占用报告或独立 CODE Approved。九文件来源集合为 `2341f290d6ac6e2b7fffd31bee0b5c5a3e4023439b6a8304ee4e266e785fb767`；逐文件及原始摘要见 `footprint-bootstrap-index.json`。实现暂停在这一明确边界，后续不得把本页局部 Passed 扩成 G1。

## 本批实际交付

`execute(..., *, observation=None)` 的 None 路径仍使用原先暂停创建、Job 归属后 Resume、Resume 后 timeout 起点、10 ms 轮询与原始文件继承。观测分支严格接收冻结有限配置，无任意 callback、命令/PID/handle 配置；同一个 owner 发布实际创建 PID/hProcess，执行已归属暂停查询、运行查询、阶段确认和退出后失效。没有新采样线程、第二进程管理器或全局环境修改。

观测使用匿名固定映射与两个事件，STARTUPINFOEX 显式继承标准句柄和该轮通道。80 字节协议验证版本、nonce/generation、实际 PID、严格阶段/序号、非未来 ticks 和消费者 checks。正常阶段等待期间 owner 继续到期查询，先处理 root 退出/运行绝对期限；异常复用同一 Job 终止和实际排空。root exit 0 与 ObservationIncomplete 分别保存，采集诊断不混写消费者原始 stderr。

PSAPI 内存查询返回原始 PrivateUsage、WorkingSet、两个 lifetime peak，线程 Toolhelp 按 root PID 筛选；模块从同一 hProcess 有限枚举并复核句柄集，HMODULE 不关闭。查询失败字段不填零；scope 关闭后拒绝读取/绑定。脚本仅支持 bootstrap guards/calibrate，不运行正式 ABBA，所有输出 budget_status 均为 NotApproved。

## 已运行证据

- `footprint-red-17bfa644d2a5`：新增观测模块未实现，真实导入失败。
- `footprint-module-red-e557fc100e13`：必需模块集合检查未实现，新增第六护栏真实失败。
- `footprint-guards-41ff95618ae3`：首五项纯值护栏及原七项真实 process 回归 Passed；process.py 与当前字节一致。原超时树、root 退出而子孙存活、退出码、崩溃与原始字节路径都实际执行。
- `footprint-calibrate-ee21c4f3b47a`：控制 DLL 缺少 `/utf-8` 选项，C4819 后没有导入库、最终 LNK1104。修正控制 target 的同工具链编译选项，失败保留。
- `footprint-calibrate-62da4e053df4`：独立无 OCK 内核依赖的 Debug 校准 EXE/DLL，九个局部正负控制成立；真实 imports、两个阶段实际加载模块、字节/SHA、Win32 样本、阶段、原始流、Job 退出全部保存。每个命令 actual active_after=0。
- `footprint-guards-final-9efd3a19f05a`：最终八项工具护栏 Passed，额外覆盖实际创建/关闭内部 scope 后迟到查询和重绑拒绝，以及任意 observer 输入在创建文件/进程前拒绝。没有再次重复原七项真实进程回归。

真实校准事实包括：提交触页 16 MiB 时 Private 从 671,744 到 17,481,728 bytes；保持线程时 Ready 线程数 4→5，join 后为 4；受控 100 ms 延迟进入 QPC 内部 Ready 差；NUL/非 UTF-8 stdout/stderr 字节原样保留。EXE 确实静态导入并调用 `footprint_required.dll`，实际加载并哈希，遗漏这份必需分发记录被拒绝。错误 nonce 和样本容量耗尽为 ObserverFailed/根终止码125；提前正常退出为 root exit0、ObservationIncomplete；运行期限为 Timeout，均未变成测量成功。

## 明确未达标与待处理项

1. **5 ms 实际覆盖未达到。** 基线只有 47 个样本，记录 1,512 个错过间隔；Toolhelp 查询在本机很重，不能宣称 5 ms 有效采样，更不能把剩余样本作为正式方法通过。当前 summary 的 Complete 只说明协议/查询正常结束，局部 result 的 Passed 只对应九项列出的校准控制，均不代表采样方法达标。
2. **“仅原主线程”负控制未建立。** 本机校准基线 Ready 实际四个线程，已保存具体 ID；保持控制是相对于该真实基线新增一条并 join，不等于只有主线程。尚未归因额外三个线程，不能据此宣布内核新增线程0。没有按平均、最小值或删除线程记录隐藏差异。
3. **规范目前不允许悄悄把线程降为阶段采样。** 方法 §3 将内存/线程统一规定为每5ms到期尝试；若改成5ms仅内存、线程只在阶段边界，必须提出有限方法增量、记录漏短命线程的新边界及不同方法/成对身份，不能直接沿用旧方法键。
4. **尚缺完整观测故障窗口验证。** 已覆盖实际 nonce、早退、容量、运行绝对期限以及纯协议反例；还没有实测所有创建/未归属/阶段截止、外部同名 sentinel、观测分支的存活子孙等职责。None 的旧子孙测试不能冒充观测分支全部覆盖。最终阶段确认之后到 root 退出间若额外发阶段事件，目前 root_exited 只按已接受序列判定，仍需补直接反例及严格末尾事件检查；不能用 ProtocolState 的纯额外阶段反例代替 owner 此窗口。
5. **完整报告层尚未完成。** 当前分析给全程原始分位与有符号 ABBA 纯值护栏，尚未形成各固定窗口的正式统计/Native能力/分发闭包和完整环境身份。`method_digest` 当前只是有限配置摘要；正式预算键还必须绑定工具、消费者、源码/依赖、环境、配置/计数模式和对应 pilot。预算 helper 不是完整 AI 审批来源/预算 schema 验证器，当前未用于准入任何正式预算。
6. 没有运行 Native/安装测量消费者的单对采集、分配模式、轻量 Release 时延或 retained_management 专项；没有运行 pilot3、formal6、三配置或设置数值上限。SDK 安装消费者的已有业务验证仍是别的证据，不能替代这里缺失的测量闭环。

因此本批可交独立审查的节点是“有限 hook 与校准原始事实”，后续方法变化和剩余职责保持待完成。按根任务安排，先转入 Logging 的两个补充主项，暂停扩展 footprint，旧失败/局部成功和以上限制均保留。

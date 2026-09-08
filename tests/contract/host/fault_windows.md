# Host 故障窗口（测试专用）

本文件记录逻辑窗口及明确配置。Debug及Debug-ASan使用create-recoverable/start/internal返回0；create-ordinal-2与stl-default-vector必须分别返回86及精确ordinal/size/calls终止标记。Release使用create-full/stl-default-vector/start/internal全部返回0。成功控制完成才输出host_fault_controls_checked；其他异常返回1。包装显式接收Debug/Release，禁止根据实际退出码反推预期配置。终止控制只证明锁定平台的noexcept边界，不是Host清理成功，也不覆盖普通可恢复异常。主任务按原Host主名归属，不增加生产入口。

历史诊断增量（下述待审状态属于原诊断时点，现已由独立适用性审核收口）：原 `create` 完整扫描入口及门禁保持原状，另提供 `create-full` 同义完整扫描、`create-recoverable` 精确 ordinal1（首个 new RegistrationBatch）异常恢复与深拥有正控制、`create-ordinal-2` 指定第二次分配，以及 `stl-default-vector` 独立标准库控制。terminate handler 先关闭计数窗，再输出固定 ordinal/size/calls 标记并退出 86。原 Debug sdk-stage-da08b48728 已真实退出 3，不能改写成完整扫描通过。锁定 MSVC vector:670 的默认构造是 noexcept，调用 xmemory:1219 的代理分配；原始源码摘录与整文件 SHA 位于 evidence/bootstrap/D1.06/host-fault-msvc-noexcept-diagnosis。Debug 代理导致 terminate 的适用性须由独立规格复核；完整逐 ordinal 的 Release 结果不得替代 Debug 的平台限制。尚未根据该诊断更改20主名包装门禁，也没有通过按大小筛除或修改 iterator ABI 来绕过失败。

`fault_host_unit.cpp` 先包含真实 Windows/bcrypt 声明，再在本翻译单元把 Host.cpp 的 BCrypt 调用替换到测试函数，随即解除宏。Real 模式实际调用系统 BCrypt；Failure 返回失败状态；Zero 返回成功但写入全零。三模式均检查参数（16 字节、系统首选 RNG、空算法句柄）和次数。失败须返回 IdentityUnavailable、无端口回调、无本 EXE C++ new 块泄漏。产品 Runtime 始终保留原 BCrypt，无测试宏。测试 EXE 自带 Host 定义后只从静态 Runtime 提取其他生产对象；链接日志须确认没有第二份 Host 定义。

`fault_allocations.cpp` 替换仅本测试 EXE 的完整 scalar/array、aligned、nothrow 及对应 delete 形式；用 malloc/free 或 aligned 对应入口供底层分配，不替换 CRT 入口、不声称 CRT/DLL 覆盖。计数窗为本线程；live_blocks 为此 EXE 替换的 C++ new 通道。失败 ordinal 只有一次，抛出前解除。正负控制验证单次抛出后第二次成功、aligned nothrow 返回空，以及无故障实际分配和释放。没有更改 D1.05 probe。

Release create-full成功基线给出固定输入的有限实际new次数，必须在1–128；之后每个ordinal独立失败。Debug逐次全扫旧失败保留；create-recoverable只注入首个确定可恢复的RegistrationBatch分配，并保留配置深拥有和预算正控。Debug未逐次覆盖的可恢复分配不冒称已全扫；指定facade/Policy/模块/日志逻辑窗口仍必须验证。每次须观察真实命中、BudgetExceeded、所有输入 owner 引用计数和 live_blocks 回到窗口前。配置包含 principal 与真实 target owner，覆盖配置深复制及 Host/Registry 预留表构造；输入配置释放后 Host 继续拥有 target，Host 析构后释放。SIZE_MAX native observation budget 须在随机源及分配之前拒绝。逐 ordinal 输出实际次数/命中/大小；不把某个未定位 ordinal 伪称特定私有类型。

start 先运行无故障真实模块启动/停止正控制。factory 先返回真实已拥有 backend 再 arm 首个 new；SafeLogger 初始 snapshot 无分配，随后真实 make_shared<State> 失败，预期 logging AllocationFailure，模块从未启动，登记 backend 仍真实 close。分别验证 close 成功、返回 BackendFailure、抛异常三条路径；后两条 Failed 保留未关闭职责和一条 cleanup 错误，再恢复 backend 显式 shutdown 成功。

Policy 故障在 backend 的第二次 snapshot 返回前 arm（首次属于 SafeLogger 创建）；固定无分配日志端口记录 Configured/Starting 两事件，随后 PolicyStore::create 的首个 Store 分配失败，必须原样 Policy BudgetExceeded，模块未启动，backend 已 close。此窗口同时输出真实故障位置次数和大小；错误域及 snapshot/事件序列拒绝把 facade 失败当 Policy 失败。

internal 同翻译单元调用真实 NoExecutions identity/find/scan，后两者必须 UnsupportedCapability；直接调用 HostControl::cleanup_error 验证 max-1→max 尚未饱和，下一次保持 max 并标饱和，有限错误存储不增长。另持真实 PolicyAssembly/既有 SessionAuthority 调用真实 HostControl::finish 正常关闭路径；模块 stop 内必须观察原 session 的 StoreClosed，证明 Policy 先关。该项是内部顺序控制，不能冒充公开 Host 启动流程或通过 Host NotReady 间接推断 Policy 状态。

可检验反例包括禁用单次分配失败（实际命中判定拒绝）、错误 random 返回全零未被 Host 拒绝、facade 失败遗漏 backend.close、Policy 与模块正常关闭颠倒。生产现有行为没有为测试改写；实际 red/green 由本次独立构建及后续真实负控制报告记录，不预填运行结果。

实际依据：host-diagnosis-71e02b8f56确认Debug ordinal2与孤立vector代理分别为(2,16,2)/(1,16,1)终止86；Release全扫1–9实际恢复。原da08退出3不改写。独立适用性见host-fault-applicability-review.md；ASan配置仍须实际执行，不能沿用Debug结果。

包装严格性增量：终止86须stderr仅含精确单行标记且stdout无成功标记；成功0须stderr为空、stdout末行仅有一次成功标记；真实observed_exit_code必须与预期一致。七种污染/不一致反例原包装均误收，修复后拒绝。三配置九个实际包装在host-wrapper-check-6cf27412d0通过，包含ASan原生故障模式；旧失败轮次保留。

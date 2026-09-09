# ADR：B3 Named Pipe 认证与授权发送起点

日期：2026-09-09。状态：实施决定；须经 B3 Critical 审核与真实 IPC 反例验证，不代表 Passed。

## 约束与决定

LocalIPC 只依赖 ControlProtocol，采集 OS SID/PID，不创建 PolicyStore、不接收客户端自报角色。服务端读取首批字节后 impersonate 采集 TokenUser，立即 RevertToSelf，再将已验证身份交给可信宿主装配。对端 SID 必须在显式允许集中；JSON 数据没有认证能力。客户端连接受控实例名，并核对该管道服务端 PID 所属令牌 SID。实例首句柄使用 FIRST_PIPE_INSTANCE，全部实例显式保护 DACL 和 REJECT_REMOTE_CLIENTS。

普通控制帧与通知帧使用独立有界配额；只有当前帧完整结束后才能切换，不声称优先级能撤回同流已开始字节。每连接异步循环隔离，持续慢写受期限约束并关闭；关闭只清理连接/观察 owner，不取消业务执行。

Policy 的 start_now 不能简单包装 Asio 入队。采用预分配帧票据和预创建 OVERLAPPED/event：在既有授权短仲裁内，对空闲流提交且仅提交一个真实前缀字节。只有 WriteFile 当场完成且确认 1 字节写入才报 Started。明确零字节才报 NotStarted；ERROR_IO_PENDING 或不能确定则报 Unknown，隔离连接并在仲裁外取消/等待该 OVERLAPPED 完成，禁止重试原帧。首字节已开始后的余帧由 Asio 接续，不能混入下一帧。起点代码不等待 I/O、不分配、不调用业务/Policy；争用用 try_lock 返回 NotStarted。

首字节 OVERLAPPED 的 event 低位置 1，避免把非 Asio 的完成记录交给 Asio IOCP。读/剩余写仍使用常规 overlapped Asio，不以 PIPE_NOWAIT 轮询取代异步模型。此决定不改变 policy-api 的 Started 定义；Unknown 可能牺牲连接可用性，但不能把待完成操作谎报为首字节事实。

依据：[WriteFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile)、[Overlapped Named Pipe](https://learn.microsoft.com/en-us/windows/win32/ipc/synchronous-and-overlapped-input-and-output)、[IOCP event 抑制完成通知](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-getqueuedcompletionstatus)。Windows 调度和内核调用耗时不提供硬实时上界；验证记录实际起点耗时与慢对端行为。

## 单 Host 的目录装配

B2 的 catalog_methods 需要原始 BindingPort 和同一 SessionAuthority；NativeHost 当前未公开这两个只读目录输入。B3 增加 `HostSession::catalog_context()`，经 Host Ready 准入返回同一已发布 registry 的 const BindingPort 与该会话 const SessionAuthority，不发布 Registrar、不创建第二 PolicyStore/Host，不引入 Runtime→Dynamic/Control 依赖。目录读取仍使用 VerifiedCaller 并按当前 Policy 检查；此上下文不授予执行权限。变更触及 Host 公共头，纳入本批直接 Host/SDK 影响验证。

## B3 意图文件的能力边界

当前 NativeHost 仅提供同步 Read/PureCompute，没有修改/Submit/Dedup provider，hello 的 dedup_epoch 仍为 null。B3 的只读调用允许保存 `VolatileHost` 本地请求意图：epoch 明确使用实际 hello 的 host_incarnation，文件记录 `deduplication:"not_provided"`，用于原子保存 nonce/逻辑请求指纹及拒绝同文件改参/宿主重启；它不声称服务端去重，也不为只读 RPC 塞入不存在的 IntentKey 字段。若将来实际 hello 提供 dedup_epoch，文件记录实际 epoch；修改/Submit 入口仍须按其协议传递原键，B3 不实现或暴露这些入口。握手未成功时不得建立/覆盖意图文件。此范围满足本批 Volatile 文件消费者验证，真实变更准入与去重继续由后续包承担。

## 验证责任

D2.05：真实不同进程、身份/角色拒绝、部分读写/断线、首字节与撤权两种顺序、Unknown 清理、慢流与独立控制连接；保留客户端实际收到的字节证明，不能用状态布尔替代。D2.06：薄客户端图和 Shell 编码/退出/意图文件。D2.07/G2：同来源批次审核与集成证据，真实任务观察仍留给 D3.07。

## B3 SDK 构建与安装决定

本批 SDK 升为 `0.1.0-dev.4 / B3Subset`，将 ControlClient 与 Adapter::LocalIPC 标记为真实静态组件，CLI 为已实现可执行入口。标准生产选择改为 B3Subset；Runtime 选择仍只取得 Foundation 依赖并安装原生闭包。B2Subset 作为开发构建选择不再保留，旧构建目录必须显式重新配置，防止独立开关组合使安装声明与实际目标分离。未实施组件保持 ContractBaseline。已实施 B2 组件保留其 B2Subset 目标阶段，Runtime 保留 NativeSubset；SDK 总阶段只标识同版实现基线，不自动宣告包 Passed。

Asio、CLI11 仅用于实现编译，不泄漏安装接口；Windows bcrypt/advapi32 以实际静态链接依赖投影。安装清单使用源清单按实际安装组件裁剪，公开新增头与 Host catalog_context 变动必须更新字节摘要并直接验证原生消费者。CLI 安装消费者须验证真实静态链接闭包不含 Runtime/Control/Dynamic，不通过制造第二份 Host 实现通过检查。

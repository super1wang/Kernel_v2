# B3 LocalIPC 与 ControlClient 合同

状态：已实现，正式放行以 progress 为准。依据 A17、A19、D2.05–D2.07；重大决定见 [ADR](../adr/ADR-b3-pipe-start.md)。只覆盖内核与无状态验证消费者。

## 传输与身份

`OCK::Adapter::LocalIPC` 只依赖 ControlProtocol，公开头为 `ock/local_ipc/pipe.hpp`。实例名限 1–64 个 ASCII 字母、数字、横线或下划线，映射为本机 `\\.\pipe\ock.NAME`。首实例独占创建；显式 protected DACL、本机限制、OS PID/SID 验证均启用。身份由首次读取后的 impersonation token 得出；对端提交的 JSON 角色和 SID 无认证作用。客户端必须核对预期服务端 SID。不同连接分别认证。

`PipeOptions` 的帧及队列字节数包含 12 字节帧头，默认控制帧上限 4 MiB、通知帧上限 16 KiB；Protocol/hello 的 body budget 对应扣除帧头。控制队列与通知队列分别计费，预留票据也占用配额，未消费析构归还。每个连接独立 I/O 循环；当前帧结束后优先控制帧，无法抢回已经开始的字节。持续慢写和不完整输入由 I/O 期限关闭。

`reserve_frame` 在授权仲裁外分配，票据绑定具体连接且只消费一次。`start_frame` 只尝试真实一个前缀字节：当场完成 1 字节为 Started；无写入为 NotStarted；pending/不确定为 Unknown 并隔离连接。Unknown 的取消和完成等待在 Policy 仲裁外进行。`ProtocolObservationTransport` 将同一票据接入 Policy，不把入队当成发送开始；ACK 使用控制队列。已经开始的事件可在退订后完成余帧，尚未开始的事件不得越过撤权。

`close` 发出非阻塞关闭信号，`wait_closed` 等待 I/O 和断线回调结束；`close_after_flush` 停止接收新请求并排空已入队响应后关闭。关闭仅清理连接和观察 owner，不发送业务 cancel。启动失败执行关闭回调并标记排空；OS 句柄由 RAII 管理。

## 薄客户端与 CLI

`OCK::ControlClient` 只依赖 ControlProtocol。ExchangePort 由前端装配；Client 是单调用者会话，先校验 hello 的版本、身份、方法及预算，再接受业务调用。发送前检查已协商能力和帧预算；响应校验 JSON-RPC 版本、id、方向及 result/error 排他性。

`ock` 支持 capabilities search/describe、invoke、execution list/watch 语法。输入为互斥的 `--args`、`--file`、`--stdin`，处理 UTF-8 与 BOM；stdout 输出机器 JSON，诊断写 stderr。Ctrl+C/Break 仅中断等待或输入，默认不取消业务；关闭客户端不销毁独立 Host。交互命令行参数通过 Windows Unicode argv 转为 UTF-8，空参数保持为空。

| 退出码 | 含义 |
|---|---|
| 0 | 查询成功、同步成功或 Accepted；Accepted 不等于后台完成 |
| 2 | 用法/输入错误或意图文件冲突 |
| 3 | 拒绝或所需能力不存在 |
| 4 | FailedBeforeApply、PartialCompletion 或已解析失败 |
| 5 | 等待超时 |
| 6 | 传输/协议/客户端失败 |
| 7 | Indeterminate |
| 8 | 本地等待中断 |
| 9 | 远端事实为 CancelledBeforeApply |

意图文件在首次业务请求之前原子写入；存储宿主/epoch、随机 nonce、逻辑请求指纹与 `VolatileHost`，不保存凭据或原始参数。同文件改参、换 Host/epoch 均冲突。当前只读 Host 的 dedup_epoch 为 null，记录实际 host_incarnation 并显式 `deduplication:not_provided`；没有服务器去重或跨重启保证。

watch 预备算法要求 managed provider，先 subscribe 再 get；按世代、sequence、observation_version 处理提示，缺口、终态/事实提示及周期超时触发重新 get。关闭发送 unsubscribe 并关闭观察连接，默认无 cancel。当前样例真实 Provider 为 absent，CLI list/watch 明确拒绝；内存脚本仅证明客户端算法，真实任务观察留 D3.07。

## 验证消费者

`examples/stateless_service/control_main.cpp` 启动同一 NativeHost，发布 `sample.increment`。CLI 与 Native 走相同 SharedTypeContract/HostBound；amount 为 0–100，输出 amount+1，100 触发输出验证失败。负数及超长 UTF-8 输入拒绝。样例不用第二 Registry/Host 或独立业务分派器。

独立安装消费者仍由同目录原 `main.cpp` 提供，Runtime-only 路径保持纯原生闭包。迁移安装验证同时检查新增头独立编译、薄客户端静态链接映射和已安装 CLI；私有依赖不泄漏。

成本样本分别记录 20 次 Native 与 20 次 Dynamic 的单次纳秒数、分配次数和字节数。预热、Host 初始化、JSON 文本解析、IPC、CLI 启动不计入；Dynamic 包含结构解码、验证与同一 HostBound 调用。分配探针必须先通过正控制，不据此宣称产品吞吐量或硬实时性能。

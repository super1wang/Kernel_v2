# 受管理执行验证消费者

Release 成本采样入口为 `tests/integration/cli/managed_cost.py <Release服务exe> <输出json>`。off/on/on/off 使用相同两个连接与每次 120 个 20 ms 任务，正常读取由客户端独立线程推进；另两次 slow 明确停止读取。服务进程 CPU、执行期与通知额外排空成本、PrivateUsage/WorkingSet、请求墙钟及 cursor 有效/MAC/超长路径分列。墙钟包含客户端 5 ms 轮询，CPU 存在系统计时粒度与波动，排空成本含诊断轮询；不将本样本用作稳定开销比例、硬时延或无 IPC Embedded 占用结论。

本消费者仅验证内核的真实 Control/CLI 路径，不包含 GUI、CAD、CAM 或设备模块。`sample.compute` 通过同一个注册的 typed 实现执行；输入为 amount、delay_ms 和 text，结果为 amount+1、原文本和 stop_observed。delay_ms 最多 10000，处理期间协作观察 stop，不产生外部业务效果。

在启用 B3 与 CpuPool 的开发组合构建 `ock_managed_service` 和 `ock`。启动：

```powershell
.\build\b4-debug\examples\managed_service\Debug\ock_managed_service.exe --instance managed-demo
```

在另一终端提交；原 CLI 退出后执行继续由 Host 持有：

```powershell
$managedCli = '.\build\b4-debug\apps\ock\Debug\ock.exe'
& $managedCli --instance managed-demo submit sample.compute --args '{"amount":4,"delay_ms":3000,"text":"跨连接输入"}' --json
& $managedCli --instance managed-demo execution get <execution-id> --json
& $managedCli --instance managed-demo execution wait <execution-id> --wait-timeout-ms 5000 --timeout-ms 7000 --json
& $managedCli --instance managed-demo result read <execution-id> --json
& $managedCli --instance managed-demo execution cancel <execution-id> --json
& $managedCli --instance managed-demo execution watch <execution-id> --jsonl
```

将 `<execution-id>` 替换为 Accepted 回执中的真实身份。wait 超时只结束等待；cancel 回执不等于执行已终止，继续通过 get/wait 确认。输入 `stop` 或关闭服务 stdin，服务先关闭连接，再由 Host 取消并排空执行。

认证依据由 OS 验证的管道 SID。连接关闭释放会话 wrapper，不显式关闭执行所持的 SessionAuthority；调用方显式授权关闭与 Host 关闭仍保留原撤权规则。Control 响应按当前发送授权进入独立 Control 队列，由同一连接 I/O 线程的可选 10 ms Tick 有限推进；默认 LocalIPC 客户端未启用 Tick。

样例配置固定 2 个 CPU workers、8 个 active 执行、128 条记录、16 个 waiters、输入/结果/终态分别 1 MiB 额度，最多 8 个连接和 16 个 Policy 会话。观察变更环为 1024 条，原始观察 lease 最多 64 个；Policy 每会话 8、每主体 32、全局 64 个订阅，Control 每订阅最多 128 条待发提示。Runtime 自身沿用单控制线程；AutomationHost 的管道 I/O 线程成本需另行计量。此配置不是 G3 的正式 footprint 或线程峰值验收。

终态缓存上限为 32；每次输入/结果预留各最多 16 KiB，在既定 1 MiB 字节预算内给活动执行留出空间。缓存淘汰导致待发提示目标不可用时，Policy 零字节丢弃，订阅继续并在后续可发送提示中报告 gap。

已接线的方法为 capabilities.search/describe、operation.submit、execution.get/wait/cancel/list、result.read 和 notifications.subscribe/unsubscribe；真实执行变化经 notifications.event 发送给 watch。`T20.cli.managed_roundtrip` 使用实际 CLI 子进程验证跨连接结果、并发等待/取消、真实列表、watch 终态确认和会话回收。独立安装、完整观察组合及 G3 仍需后续验收。

## 列表开发增量

`ock --instance <实例名> --json execution list --phase terminal --page-size 200` 查询同一真实执行表；`--phase nonterminal` 查询未终结执行。分页返回当前连接绑定的游标。`--pages 10 --jsonl` 在同一连接内最多连续读取 10 页；到末页提前结束，每页立即输出一行 JSON。`--pages` 范围为 1..128，默认单页；多页必须显式使用 JSONL。list 与 subscribe/event 使用当前 Policy 和发送前检查。

观察源在执行线程只追加固定大小的身份/版本提示；各连接 I/O Tick 有限消费，不增加后台观察线程。环覆盖或摘要合并标记 gap，通知不保留输入/结果 owner，不能阻断可靠完成。watch 在 subscribe 后 get，并在 phase/fact/gap 提示后再次 get；不把通知当作完整跃迁史。

Host Draining 拒绝新观察注册，已有观察可继续有限发送；最终关闭移除监听并等待在途回调及其 owner 释放，再报告 quiescent。可信组合根持有原始观察源，普通调用者仍只能通过既有 Policy/Control 查询与订阅。

watch 首个快照立即输出。开发验证还覆盖：观察进程取得非终态快照后被关闭，执行继续；另一 watch 重连后准确确认终态，结果未观察到 stop。

Policy 显式配置 48 个发送协调器，供最多 8 个连接分别装配六个端口；协调器数量与 16 个会话额度独立。排队总限额为 256 帧/1 MiB，其中 16 帧/64 KiB 保留给可靠控制响应，通知不能占用；同一协调器优先发送可靠响应，首字节前仍检查当前授权。

`T20.cli.managed_interrupt` 向本测试创建的隐藏控制台发送真实 Ctrl+C：默认仅停止 watch，结果 `stop_observed=false`；显式 `--cancel-on-interrupt` 请求协作取消，结果 `stop_observed=true`，两种观察中断均返回退出码 8。`T20.cli.managed_wire` 验证八连接、订阅 ACK/配额/重连世代、游标绑定拒绝、分页 upper，以及不修改时钟或配置的真实 120 s 游标过期。G3 正式验收仍需完成。

`T20.cli.managed_slow` 停止读取实际观察管道，在确认未发队列非空时通过另一 CLI 连接 get/cancel，并核对可靠完成；恢复读取后验证 gap 与 get 终态。再次形成队列后，通过本进程 stdin 收紧现有会话 Subscribe 授权，确认真实首字节次数不再增加、队列归零。stdin 的 `observation-diagnostics` 仅返回最多八会话的排队字节与饱和计数；`restrict-observers` 只收缩现有会话权限，均不是公开 RPC 或权限提升入口。G3 压力组合与占用正式验收仍继续完成。

`T20.cli.managed_slow_timeout` 在最多 32 个任务内确认真实管道积压且首字节数停止增长，保持控制连接活跃，验证未修改的默认 30 s 超时会清理慢观察连接和队列。丢弃不可用提示不能重置仍有积压的慢读计时。

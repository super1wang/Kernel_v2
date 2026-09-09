# 受管理执行验证消费者

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
```

将 `<execution-id>` 替换为 Accepted 回执中的真实身份。wait 超时只结束等待；cancel 回执不等于执行已终止，继续通过 get/wait 确认。输入 `stop` 或关闭服务 stdin，服务先关闭连接，再由 Host 取消并排空执行。

认证依据由 OS 验证的管道 SID。连接关闭释放会话 wrapper，不显式关闭执行所持的 SessionAuthority；调用方显式授权关闭与 Host 关闭仍保留原撤权规则。Control 响应按当前发送授权进入独立 Control 队列，由同一连接 I/O 线程的可选 10 ms Tick 有限推进；默认 LocalIPC 客户端未启用 Tick。

样例配置固定 2 个 CPU workers、8 个 active 执行、128 条记录、16 个 waiters、输入/结果/终态分别 1 MiB 额度，最多 8 个连接和 16 个 Policy 会话。Runtime 自身沿用单控制线程；AutomationHost 的管道 I/O 线程成本需另行计量。此配置不是 G3 的正式 footprint 或线程峰值验收。

已接线的方法为 capabilities.search/describe、operation.submit、execution.get/wait/cancel/list 和 result.read；subscribe/watch 在真实观察源接入后再发布。`T20.cli.managed_roundtrip` 使用实际 CLI 子进程验证跨连接结果、并发等待/取消、真实列表和会话回收。独立安装、完整观察组合及 G3 仍需后续验收。

## 列表开发增量

`ock --instance <实例名> --json execution list --phase terminal --page-size 200` 查询同一真实执行表；`--phase nonterminal` 查询未终结执行。分页返回当前连接绑定的游标，现有 CLI 尚无游标续页参数。list 使用当前 Policy 和发送前检查；subscribe/watch 仍未接线。

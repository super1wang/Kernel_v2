# B6 F5 等待过期补充反例

32c38ad 最终候选遗漏了 Host 已 Accepted、等待资源时过期的分类。使用独立构建副本探测，未改写该候选的源码或原机器报告。

- `v2-direct-expiry-probe-9840fafb`：隔离反例失败，普通过期被归为 CancelWon。
- `v2-direct-expiry-fixed-db38737c`：只修 complete_before_start 仍失败，进一步定位 ExecutionService timer 调用 cancel。
- `v2-direct-expiry-route-fixed-a1ec2819`：区分内部 expire 路由后通过。
- `v2-direct-expiry-all-fixed-0120dbc6`：增加 handler 已进入过期用例时，旧 Policy 测试假时钟已被前例快进，导致预期业务进入事实不成立。
- `v2-direct-expiry-real-clock-49288e0c`：Host 改用与 Scheduler 一致的 steady_clock，等待中和已进入过期均为 FailedBeforeApply，原真实取消用例仍为 CancelWon；1/1 通过。

以上均为开发直接验证，不代替新源码的正式三配置与 footprint。所有失败原文保留。附带探测脚本以仓库根目录运行时的 build 路径为准，不是新的正式验收入口。

# B6 最终线性化收口 footprint 来源刷新批准

AI 技术批准时间：2026-09-15T09:58:58.216935+00:00。生产来源 `5fd2310aec5f8a24ac14b948677930e2a7ced7e3`。

F6 收口修复（ExecutionService/ManagedInvocation 两处 Runtime 私有文件 + State 测试）触发同一最终来源刷新，旧 912cb755 证据保持历史。仅刷新来源方法摘要、批准时间及本批准文档绑定；沿用既有全部原数值预算、pilot 标识、Native 7 模式和 Embedded 3 配置（Release 含 startup），每项六组 ABBA。方法脚本、分配零窗口、线程等式与结构上界不变。所有方法输入逐字节匹配生产来源。

该批准只允许按原预算测量，不代表机器 Passed；出现失败保留原文，禁止提高阈值。全部通过并归档机器 acceptance 前 B7 HOLD。

- native/win-msvc-debug/occupancy: `d98f4aa2c13e08da57cfd1e83d6e35879911fd312cc02312b4e4cb4d2908b3a1`
- native/win-msvc-debug/allocation: `5c4b4307a282a976e0213aa95572b247072458ba17f936399f19b17d82df3b02`
- native/win-msvc-release/occupancy: `530d7edf769ceed200c9c1198428b224b5eda8753df0677d34bf0fe1343cf51c`
- native/win-msvc-release/allocation: `2757f1b54dd32d842135d6dc5e8495b250c768c260914a55d6aca5f06c12687a`
- native/win-msvc-release/latency: `b545fe9de7cc5d36cfd32c9ed3a30696363e5fe18b803297f05b263eb6d9afbf`
- native/win-msvc-asan/occupancy: `babd57c1729de19351f565d36c3eec7512996bdaacfd81004d4f82dd58e666a0`
- native/win-msvc-asan/allocation: `633789dd98f15117a2e4fd6fdd0ca57d4eeec3312e32a8afeb2c35042ffef6fc`
- embedded/win-msvc-debug/allocation: `18a61b138854625722545601cb3122726dd90b6085f73f3e756876aba17f9814`
- embedded/win-msvc-release/occupancy: `71c193be91674f753a9496cf8a5bf2124012eee572e00f0f03688d6da1f744f9`
- embedded/win-msvc-asan/allocation: `de146d115da7324ab5a6120eb48ef0372035395d9f1b8e040c1ff92932b22646`
- embedded/win-msvc-release/startup: `71c193be91674f753a9496cf8a5bf2124012eee572e00f0f03688d6da1f744f9`

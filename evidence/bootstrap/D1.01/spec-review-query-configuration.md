# D1.01 MSBuild查询配置增量规格复核

复核者：Codex独立规格复核代理（actor_type=AI）。时间：2026-09-07T09:52:02.008442+00:00。
结论：Approved，仅为查询配置增量的规格结论，不替代新三配置正式矩阵或包级验收。

已核对verify_foundation.py：CompilerId/ABI查询显式选择Debug/x64，实际消费者选择args.config/x64，与各自生成/编译配置对应。只通过/p选择Configuration和Platform，没有覆盖被验证的VcpkgEnabled或UserRootDir；两项隔离属性仍从真实工程求值并检查。因此避免CompilerId默认Win32/旧工具集查询造成误判，同时保留三类项目验证、180秒上限、安装及运行正反例。

已读取configured-query-verification.json及三份configured-query原始stdout：三命令均实际exit0且无遗留进程，隔离属性关闭vcpkg，MSVC14.44.35207包含路径和x64库路径与本次配置吻合。该机制probe仍使用实验目录，不冒充最终Release消费者或完整矩阵结果。此前四项SDK集成是配置选择改动前的运行，新增Release headers与完整矩阵由主集成另行验证。

本记录追加于spec-review-build-isolation.md（SHA-256 `41a18dd1051194ca506e29db1d51e77604543b919b28329b1686f80c413a9f71`），原文未覆盖。逐项核对其10文件快照，本轮仅下列文件变动；未改产品API、expected或自动验收政策，无人工流程要求。

| 审核文件 | SHA-256 |
|---|---|
| tests/install_consumer/verify_foundation.py | `8f2f5fee45b156f2226bab86f3cb5829ed331dc2547def8b6206c7a56cce1ecb` |

本代理只新增本文，无实现、review JSON或Git写入。

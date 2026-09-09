# B3 批次 SPEC / CODE 技术复核

日期：2026-09-09。审核主体：Codex AI，自我复核；不是人工 Approved。精确输入摘要由同名 spec/code JSON 绑定，机器事实以 E03 三配置报告及自动验收为准。本文件先于正式运行冻结，不预写测试通过。

## D2.05 独立结论

SPEC：同意按 A17 和包卡实现真实 Named Pipe、OS 对端身份、受控端点、分队列配额及慢流关闭。Policy 首字节定义保留；Unknown 不是 NotStarted，也不允许重试。客户端 OS 用户拒绝、SMB 远端正控制/拒绝、伪角色和消息方向均须是真实进程反例。真正 Task/取消调度不在本包，跨连接控制可用由实际独立往返证明。

CODE：已复核 `packages/adapters/local_ipc`、Protocol FrameSink、ProtocolObservationTransport 和同 Host 装配。protected DACL 使用数据访问掩码；服务端 SID 来自 impersonation token，立即 Revert 后才进入 Host。客户端核对 OS 服务端 SID；反向消息和自报角色不能进入授权。首实例排他、本机限定、每连接有界队列与 I/O 期限均有生产实现。票据归属/配额/单次消费、ACK 优先、真实一个字节的同步完成判据、低位 event 的 IOCP 隔离及仲裁外取消等待已逐项检查。

修正了传输含头预算与 Protocol body budget 的差异；普通唤醒改用预创建 event，避免已入账之后再分配唤醒任务。线程启动失败关闭并通知排空；连接、SID 转换结果与构造失败句柄由 RAII 管理。真实部分读写、撤权前后起点、Unknown 满缓冲、慢流控制配额和跨连接往返均已直接验证。技术结论：SPEC Approved、CODE Approved；正式 Passed 仍依赖固定三配置事实。

## D2.06 独立结论

SPEC：同意薄 CLI、UTF-8、stdout/stderr、固定退出码、能力探测、VolatileHost 意图文件与 watch 预备算法。当前 Host 没有 dedup/Task provider，文件明确记录 not_provided，不能伪称服务器去重。CLI list/watch 对 absent provider 拒绝；真正任务整体观察留 D3.07。

CODE：已复核 ControlClient/intent/watch、CLI11 命令解析和 PipeExchange。Client 验证 hello、请求预算、方法存在性、响应 id/方向及 result/error；调用缺失能力不传输。意图文件按排序字段与数值规范化计算逻辑指纹，原子创建/flush/rename，已有文件不覆盖，不记录凭据。Ctrl+C 默认仅中断等待；显式恢复 Windows 继承的忽略位，管道 stdin 不再永久阻塞。watch 先 subscribe 后 get，提示缺口和周期超时重取，退出只退订；显式 cancel-on-interrupt 需另行能力且使用独立控制连接。

安装 SDK 用 B3Subset 统一选择，Runtime-only 仍仅取得 expected。ControlClient、LocalIPC 无 Runtime/Control/Dynamic 链接；迁移消费者和真实 CLI 的链接映射都检查服务端库缺席，Asio/CLI11 不泄漏导出。技术结论：SPEC Approved、CODE Approved；正式 Passed 要求 D2.05 正式前置及本批机器事实。

## D2.07 独立结论

SPEC：同意按同一 SharedTypeContract 操作对比 Native、JSON 和 CLI 的成功/输入拒绝/输出失败，真实 Shell 解析单份机器结果。单次成本须记录原始样本与分配正控制；不把绑定调用测量解释为 IPC 吞吐或完整内核性能。

CODE：已复核无状态消费者的同 HostBound 路径、OS 身份至 Host.open 的可信装配、只读 CatalogContext 和原生安装消费者保留。生产 Runtime 没有反向 Dynamic 依赖。样例自身只含 Read/PureCompute，amount=100 的输出违规保持 FailedBeforeApply，不绕过验证器。20 次 Native/20 次 Dynamic 测量分别保留纳秒和分配渠道，Dynamic 的结构解码/验证范围与 Native 明确区分。真实 cmd/Windows PowerShell 的 file/stdin 与 UTF-8 文件名/中文/非 ASCII 字符均有子进程原文。

技术结论：SPEC Approved、CODE Approved；正式 Passed 要求 D2.06 前置与固定报告，不宣称 Task/State/Plan/Durable/GUI/设备能力。

## G2 集成与证据约束

集成 expected 来自包卡和 G2 要求：Debug 28、Release/ASan 各 26；包含 13 项新增完成条件，以及绑定/目录/帧/Outcome/观察协议、只读 Host 上下文和 SDK 的必要集成。不是从 discovery 反推清单。完整三配置是本批正式收口所需，不重跑历史 G1/B2 累计测试。

三份 E03 物理报告使用 D2.07 集成 task_id，四个逻辑矩阵引用同份结果，分包独立技术结论见上文；验收按 DAG 顺序。正式来源先提交，review JSON 绑定其输入摘要；失败原文保留，修改输入则使用新的完整来源运行，不拼接不同来源成功。

首轮来源 fe0893e 的 Debug/Release 用例虽全部通过，JUnit 成本 JSON 被 CTest 默认 1024 字节上限截断，因此不用于本次最终验收。成本样本输出增加 CTest 官方支持的 `CTEST_FULL_OUTPUT` 标记字段，保持完整合法 JSON，不改变测量窗口或计数器。新来源必须重新完成三配置，且核对每份归档 JSON 都有 Native/Dynamic 各 20 个样本；旧原始报告不覆盖、不补写成功样本。

来源 f0cd96a 的 ASan 成本正控制发现测量消费者条件错误：既有 allocation_cases 合同明确 ASan 接管 malloc 时 CRT 通道为 0，消费者却同时要求 CRT 和 ASan 为正。仅修正 B3 测量消费者，要求 C++ 恰好一次、有效字节不少于请求、启用的 ASan 通道恰好一次或非 ASan Debug 的 CRT 通道为正，并把实际正控制计数写入 JSON。既有探针和生产代码不变；ASan 专项已验证，最终仍按新来源完整执行三配置，不能拼接 f0cd96a 的其他成功用例。

遗留边界：同流开始字节无法撤回；Windows 调度不承诺硬实时；当前没有真实 Task/持久化去重/完整任务结果，真实任务观察由 D3.07 承接。以上是明确范围边界，不是豁免本批失败。

# SDK 公开表面与组件依赖基线

工作包：D0.02；依据 A02、A19、A20。开发 SDK 版本为 `0.1.0-dev.1`，独立于架构文档 v3.3。

## 已存在与尚未实现

当前实际创建 21 个 `INTERFACE_LIBRARY` 合同 target，并由 CMake 的 install(EXPORT) 导出依赖。只编译一个可独立包含的版本元数据头 `ock/foundation/sdk_version.hpp`，没有 Operation、Runtime、线程池、数据库、IPC 或 CLI 实现。

`OCK::Foundation` 等目标的 `OCK_IMPLEMENTATION_STAGE=ContractBaseline` 随安装导出；`OCK_RUNTIME_AVAILABLE=FALSE`。`find_package(OCK CONFIG REQUIRED)` 可读取版本与候选目标元数据，任何 `COMPONENTS Runtime` 等运行能力请求均明确拒绝。不能把合同 target 的存在当成模块实现通过。

`ock` 可执行程序由 D2.06 实现，当前只冻结其依赖 ControlClient、Adapter::LocalIPC 与 CLI11；不提供返回成功的假 CLI。没有新的 Observation target，Control 的 observation 仅为内部目录。

## 公开表面

机器清单：[sdk_api_manifest.json](../sdk/sdk_api_manifest.json)。实际头集合、SHA-256、导出 target、命名空间、编译要求及 experimental 分类分别登记。所有 `candidate_headers` 都是 Planned：实现前审查声明，转入真实公开头集合；候选文件名不是接口签名或稳定 ABI。

唯一已安装头提供 SDK 数字/文本版本和 `runtime_available=false` 元数据。公开编译要求为 C++20，并对 MSVC 公开导出 `/utf-8`，不导出本机绝对路径。MSVC Debug 使用动态 Debug CRT，Release 使用动态 Release CRT；实际工具链和依赖锁由 D0.06 定案，本包的测量不能代替完整支持矩阵。

公开头新增/删除、同名头内容变化、导出 target/编译条件变化均必须触发审查；哈希只定位变更，不等于 C++ 兼容证明。声明、所有权、异常/noexcept、线程语义须单独审查，并在有已发布版本后运行冻结消费者。当前只有 0.x 安装元数据消费者，不能虚报旧正式 SDK 兼容成功。

## 组件闭包

| 目标 | 直接内部依赖 | 公开包含前缀 |
|---|---|---|
| OCK::Foundation | 无 | `ock/foundation/` |
| OCK::CoreContracts | Foundation | `ock/contracts/` |
| OCK::Runtime | CoreContracts | `ock/runtime/` |
| OCK::Data | Foundation | `ock/data/` |
| OCK::Dynamic | Runtime、Data | `ock/dynamic/` |
| OCK::Automation | Runtime、Dynamic | `ock/automation/` |
| OCK::State | CoreContracts | `ock/state/` |
| OCK::Workspace | State | `ock/workspace/` |
| OCK::Durable | CoreContracts | `ock/durable/` |
| OCK::StateDurable | State、Durable | `ock/state_durable/` |
| OCK::DurablePlan | Automation、Durable | `ock/durable_plan/` |
| OCK::ControlProtocol | CoreContracts、Data | `ock/control_protocol/` |
| OCK::Control | Runtime、Dynamic、ControlProtocol | `ock/control/` |
| OCK::ControlClient | ControlProtocol | `ock/control_client/` |
| OCK::Adapter::CpuPool | CoreContracts | `ock/adapters/cpu_pool/` |
| OCK::Adapter::Logging | CoreContracts | `ock/adapters/logging/` |
| OCK::Adapter::Config | Dynamic | `ock/adapters/config/` |
| OCK::Adapter::SQLite | Durable | `ock/adapters/sqlite/` |
| OCK::Adapter::Storage | CoreContracts | `ock/adapters/storage/` |
| OCK::Adapter::LocalIPC | ControlProtocol | `ock/adapters/local_ipc/` |
| OCK::Adapter::MCP | Control | `ock/adapters/mcp/` |

Runtime 的传递闭包只能为 CoreContracts、Foundation；State 只经 CoreContracts 接入。ControlClient 经 ControlProtocol→CoreContracts/Data→Foundation，不含 Runtime、Control 或 Workspace。测试程序及证据工具不进入产品 DAG。

Foundation 将来公开导出固定 tl::expected 后端；jsoncons、immer、SQLite、Asio 等不泄漏到核心公开类型。D0.02 尚未取得或链接这些生产依赖；D0.06 的 probe/许可/hash/选项锁定通过后才实现对应包，不用 PRIVATE 隐藏最终静态链接要求。

## 检查边界

`tools/architecture/check.py --graph <构建目录>/ock-target-graph.json` 对比原始 A02、受审查 SDK 清单与实际 CMake 配置产物，并扫描当前 packages 内的 include 所有权/公开头集合。架构反例覆盖 Runtime→State/Data、客户端→服务端、Workspace→Runtime 私有头、未知头、测试目标入产品 DAG、UI 写旁路、通用 state RPC、DSL 必需化等。

UI/Workspace 写入口和 RPC 方法目前是架构合同夹具；当前不存在产品 UI 或 Control 路由，测试不证明真实授权/调用路径已实现。D1/D4/D6 与 D2/D7 必须把同一规则落到真实注册和调用链，分别复验。

安装测试将安装树复制到不同根，独立配置/编译/运行版本元数据消费者；另验证 Runtime 和未知 Observation 组件请求失败。当前范围不包括运行时安装消费者，后者从 D1 起交付。

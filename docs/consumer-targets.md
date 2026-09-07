# 三个内核验证消费者目标

任务：D0.01；来源：A02.2、A23。以下为目标清单，当前尚未创建应用或宣称可构建。所有消费者复用同一 Runtime、Plan 引擎和 Outcome。

| 消费者 | 计划 CMake 目标 / 目录 | 必需组件 | 可选组件 | 禁止组件 | 首次及后续验收 |
|---|---|---|---|---|---|
| C-A 无状态自动化服务 | `ock_stateless_service` / `apps/stateless_service` | Runtime | Dynamic、Automation、Control、ControlClient、Adapter::LocalIPC | State、Workspace、Durable、Adapter::SQLite | D1.06、D2.07、D3.07、D4.08、D6.08 |
| C-B 无文档事务配置服务 | `ock_settings_service` / `apps/settings_service` | Runtime、State | Dynamic、Automation、Durable、StateDurable、Adapter::SQLite | Workspace | D4.08、D5.09、D6.08 |
| C-C Workspace 对象/资产/后台算法验证程序 | `ock_workspace_app` / `apps/workspace_app` | Runtime、State、Workspace | Dynamic、Automation、Durable、StateDurable、DurablePlan、Adapter::SQLite、Adapter::Storage | 无额外组件禁令，受产品 DAG 约束 | D6.03、D6.08、D8.01 |

## C-A 行为与边界

从 Native 起步，验证动态/真实 CLI/任务/内存 Plan；不创建文档或数据库。

## C-B 行为与边界

类型化不可变配置根支持单域 Atomic；内存组合不取数据库，耐久组合显式选择桥接。

## C-C 行为与边界

仅验证内核 Workspace、对象/资产/后台执行及安装合同；不扩展 CAD/CAM、几何算法、设备控制或 GUI 产品。

C-A/C-B 的 requires_document=false 是机器约束；禁止为了连接 Atomic 或事务而补一个伪 Document。C-B 的内存验证不选择 SQLite；耐久验证才显式装配 Durable/StateDurable。C-C 可使用真实 Project/Document 来验证内核 Workspace，但不开发桌面产品、业务几何或设备动作。

NativeSubset 是 D1.06 的受限测量集合，完整 Embedded 在 D3.07 实测。它们不能互换名称或占用证据。独立安装消费者的实际构建/运行由相应工作包交付，当前清单只固定职责。

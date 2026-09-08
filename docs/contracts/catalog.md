# B2 能力目录与命令卡

本合同细化 v3.3 A04/A18 与 D2.03，当前为开发实现，包级验收仍待集中完成。

`Catalog` 消费生产 Registry 发布的 `BindingPort` 快照与类型 Schema。常规类型使用 `TypeSchema::generated<T>()`，从 Native `TypeContract<T>::FieldSpec` 生成，目录不会为帮助再编译一次参数校验器。调用校验仍由 RegisteredRecord/HostBound 执行。

需要进入动态目录的定义在现有 `DefinitionInput.docs` 中提供 `ock.command-docs/1` JSON：purpose、counterexamples、coordinates、position_mode、impact_scope、id_sources、cancellation、durability、result_phases、error_repair，以及 retry 中分别声明 natural_idempotence、framework_deduplication、device_deduplication。非适用内容必须明确说明；缺项或空内容不能生成看似完整的命令卡。纯 Native 定义继续可以使用普通 Docs 文本，本要求不改变 Native 注册合同。

目录初始化只解析一次 Docs，随后通过共享 owner 保留冷数据。`ock.command-card/1` 的精确操作版本、合同摘要、原子模式、所需权限、参数/结果 Schema 来自实际定义；单位来自字段 Schema 的 x-unit。JSON 输出受 PayloadBuilder 预算限制，公开形状见 `schemas/catalog/command-card.schema.json`。CLI help 使用同一 Docs 和字段元数据，无第二套 AI 参数说明。真实 ID 说明只能告诉调用者查询来源，不能作为固定有效 ID。

`capabilities.search` 接受可选 prefix、offset、page_size（默认 50，上限 200），只返回轻量名称/精确版本/installed/visible/eligible、下一页位置和缓存指纹；`capabilities.describe` 要求 name/version，返回完整命令卡。指纹覆盖定义 Docs、合同摘要、参数/结果 Schema、原子模式及页内当前 eligible；只作缓存提示，不是调用许可。目录上限 4096 项，Schema 数量上限 8192。

两类查询都使用当前 Session 的政策交集。隐藏操作与未安装操作对外统一 NotAvailable；非法参数由路由返回 Invalid params。搜索不复制长 Docs 到响应，也不创建执行许可。真实业务调用仍必须经过原来的治理入口。订阅/真实任务和 IPC 不属于目录证明范围。

# 不提供通用 state RPC

任务 D0.02，来源 A23.1；状态：沿用已批准架构决定，当前落地产物待 D0.02 评审。

## 决定

外部领域状态通过带 TypeContract 的 Read Operation 暴露并进入 Catalog。不得注册任意 path/dictionary 的 state.inspect/query 或用改名方式建立同类无类型入口。

## 验证与影响

D0.02 方法声明反例拒绝 state.* 通用 RPC；D2/D7 真实目录/路由与 D8.06 资料复验。未来统一查询语言须独立版本与 ADR。

业务、权限和事务仍由内核既有 Operation/Outcome/Plan 合同负责；不增加新产品模块、版本门禁或第二 Runtime。

# UI 业务写统一 Operation

任务 D0.02，来源 A23.1；状态：沿用已批准架构决定，当前落地产物待 D0.02 评审。

## 决定

StateEdit、ExternalEffect、Lifecycle 一律走同一 Operation；UI 不持有可直接提交的业务写服务。只读渲染、视图计算与已授权不可变 Snapshot 可直接消费。

## 验证与影响

验证合同夹具拒绝 UI/Workspace Direct 写入口；D1/D4/D6 再对真实调用链验证。不以用户体验、撤销方便或内部可信为绕治理理由。

业务、权限和事务仍由内核既有 Operation/Outcome/Plan 合同负责；不增加新产品模块、版本门禁或第二 Runtime。

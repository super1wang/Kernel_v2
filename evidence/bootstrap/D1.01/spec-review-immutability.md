# D1.01 拥有型错误详情不可变性增量规格复核

复核者：Codex 独立规格复核代理（actor_type=AI）。
时间：2026-09-07T08:51:44.256375+00:00。
增量规格结论：Approved。ErrorInfo公开特殊成员造成的可变别名缺口已在受检公共API边界关闭；该结论仅覆盖下列四文件字节，不替代修复后的实际测试、三配置正式矩阵或独立代码审核。

本记录追加于spec-review.md，其原始23文件快照保持不变，原文SHA-256为 `1e5fe19a50ddae92ffa2005cf0aac47ba37857ffee0943e07bea3bee8bcc63d6`。本次逐项对照确认只有本记录所列四文件发生变化。前次审核未识别的可变别名问题经独立代码复核发现，本记录修正并补充该处结论；其余审核边界保持。

已读取独立POC：independent-alias-probe-9027715f95/probe.cpp通过公开copy构造创建shared_ptr<ErrorInfo>可变别名，发布为Error的只读详情后再赋值，原始输出显示详情由original变成changed。已读取immutable-details-red-e1f8a2b816的实际ownership命令记录：退出1，首项不可复制CHECK失败。该失败证明新增合同检查在修复前能识别缺口；不把它记录为通过。

修复显式删除ErrorInfo的copy/move构造与copy/move赋值；文本构造仍为私有，工厂仍先校验UTF-8和4096字节预算，并只返回shared_ptr<const ErrorInfo>。正常调用者不能再从合法详情复制出可变ErrorInfo，已发布详情没有公开修改入口。Error自身的复制/移动继续共享只读拥有权，Result后端、错误传播、空详情不分配及OOM传播语义不变，没有扩展到Payload或额外容器。

ownership用例新增四种类型能力CHECK，Release中同样执行；保留来源销毁/修改后文本拥有性、Error共享复制/移动以及空详情无分配正例。合同明确ErrorInfo自身不可复制/移动/赋值与只读创建边界；公开manifest的头摘要已核对为实际修复字节。固定用例名称和数量没有削减。

本代理仅只读补审并创建本文；未改实现、测试、旧审查记录或Git。用户已生效政策采用AI审核，无人工批准要求。修复后的实际测试及最终包级验收由主集成另行记录，本文不宣称D1.01 Passed。

## 增量审核输入 SHA-256

| 文件 | SHA-256 |
|---|---|
| packages/foundation/include/ock/foundation/foundation.hpp | `1e3325f6077dd42d2f3ec8460d1eb4eeeca169c668975c41fb90556773940ae0` |
| tests/unit/foundation/foundation_tests.cpp | `f103640fd05d488b97562dde33df7f04f3c32fb017e6cd8951adceac6ed1d9cc` |
| docs/contracts/foundation.md | `333024870b90eacb9be98036e7ca7861e8bcb738530e5427b08d7fa4a9e26f9b` |
| sdk/sdk_api_manifest.json | `2a81b8bf6f8a5a856511f349026058394d007b82ff6879a7a9718d74ac6df70c` |

# D1.04 冻结候选完整独立 AI 规格/核心/覆盖复核

- actor_type：AI；复核者：独立代理review_contracts。
- 总结论：ChangesRequested。规格修订5本身保持Approved；当前核心实现及35项完整覆盖存在下列必改项。
- 本轮重新读取implementation-freeze-36315fd680b0/review.md和sources.json、start-revision5.json、API/计划修订5、核心所有权/授权/观察发送路径及固定测试，未仅汇总既往切片批准。
- API SHA256：`b7c73b6401cca3efb3c43f0e59ac8ce1cb5f597bb9710f0c5908faff5e054002`；计划：`71283f43812a3be88f6e8d457185d15dc494cbac73a5035430b21e97f328b07e`。
- 冻结核心cpp：`6b81a3d524c98a625276804b143f12307d392216972c3abf3db5e7aebc875a58`；hpp：`bcd5fcb28468b6f7f519c3e99671a1f0a6554871da1be043bced89c2b1ee4fee`。
- policy_tests.cpp：`c3b34cebb00b11fd6a41af1d33953867be61ba0b7e572566f1c6b2295cd8b604`；fixtures：`cea9f58db8a47311faadba06469587e13e26a0292e4b8902b2f6d18901453db1`。
- action/authority/session/send测试分别：`6b7764d720644e10d33716177ca7419e40d729c847e15d2f718475c8ba61d4d6`、`94b378ccea15926a3fff2daeba93725d7c07c4755e9c532f506d68a23082375f`、`fa093665543df391c79f1879d618671846de72a43a7acce54f3d641edc95e528`、`6a32a9798598ebbd5a8e41d1325464aab324887fa37c44e21ac10731b65516c6`。
- 实际核对冻结清单12份文件与implementation-8e072f1ca34a/source逐项相符；该轮commands共37条全部Exited/0、owned Job active_after=0，确含build/list及固定35主体。此为原生Debug事实，不等于根wrapper或正式三配置验收。

## P2-1：编码返回缓冲外部别名可改变已授权帧（独立真实red）

冻结policy.cpp:728–731、1879附近及Watch enqueue的相同helper，将encoder返回vector直接move进PreparedTransmission。编码器保留原元素指针后，锁外reserve回调仍能修改被私有对象接管的活缓冲。私有只读getter并没有令该缓冲独立拥有。

独立证据encoder-alias-3caf9bd610bd：源码绑定上述冻结cpp，configure/build/control均0；alias退出1，stdout `mutation=1 bytes=2 first=126`，stderr `encoded bytes remain independently owned`。原编码首字节81（0x51），实际sink收到126（0x7e）。探针先验证小块delete观察通道，只在未释放时修改原元素，未解引用悬空地址；其源码及commands/raw位于独立证据目录。

修复应在限长后、锁外、提交前取得独立字节副本，再将自有副本移入PreparedTransmission。原API“移入”描述不排除先复制；这正落实输入冻结与私有状态无外部可变别名合同，不改变可信编码能力、不新增生产后端、不宣称防恶意native内存写。修改const vector引用helper并保持局部返回vector活到reserve之后，可使同一别名探针在修后安全运行。父任务已开始修复；本报告保留修复前ChangesRequested，后续另出增量结论。

## P2-2：观察投影新增key文本漏计（独立真实red）

冻结policy.cpp:1635附近保存Entry.operation副本，1677附近ExecutionSummary::create(projected)再保存一份新投影OperationKey；entry_usage:1682仅对e.operation计一次selector文本。源已存在且不可变共享的Summary不作为本反例新增副本计费，但新Entry与projection两份独立key必须计费。

独立证据projection-budget-1efdbfdef52a：使用随后只修编码复制的cpp `c2e746bf6c11a5d1954e39e5e53888956e1f26b6535b9e3e29b1517e1d9ce449`，上述预算逻辑未变。configure/build及两份key足额控制退出0；紧预算退出1，stdout `base=6885 key=14 budget=6899 get=1`，stderr `one key budget cannot retain entry key and projection key`。仅剩14字节而实际新增28字节仍成功，证明Store总text计费少14字节；足额预算6913成功，非全拒绝控制。

应按实际新增拥有材料修正get/list/Watch投影及帧的共用计费路径，避免相同问题遗漏其他shape，也避免给共享不可变源任意重复收费。配额及释放正控制需保留。该独立probe未修改产品，只在evidence/build副本运行。

## P2-3：coverage-preflight仍未闭合的计划子断言

不是增加新功能或以缺测试直接判漏洞；以下是冻结计划已有条目，在当前35主体中仍未真实验证：

- query_field_projection：已有GetSummary不能替代ReadResult、Cancel不能泄露摘要、字段交叉控制；尚无实际改变UsePolicyInput.required_permissions使非Invoke拒绝、再四方补齐成功的来源控制。仅清空主体ReadResult规则不能证明实现读取了UsePolicyInput所需权限。
- page_binding_isolation：仍仅跨连接、page_size变更及expiry；缺新的合法caller下委托变更、owner/phase过滤变更的绑定拒绝正反例。
- generation_exhaustion/queued_revoke_drop：已有新StoreId不同、get响应关闭/host失约/owner矛盾/progress正常变化、list撤权零字节；缺先close旧Store→合法换源→新Store成功且实际拒旧发行对象，以及原Watch/list在此组合中零字节的控制。单独比较StoreId不等于旧对象拒绝。
- exception_atomicity：现有digest/find/scan/encoder/reserve bad_alloc及合法重试、管理更新分配扫描和析构重入；缺认证端异常后会话容量完整恢复、subscribe途中源异常后以小Watch配额证明没有半安装。此前混合filter失败配额测试不能代替异常中途路径。

上述补齐不改变35个名字。主要实现缺陷以P2-1/2真实red为准；这些覆盖项应同步补，而不是只接受新的全绿名称列表。

## 固定35项逐项核对结果

“已有”表示对应主要反例/正控制真实可执行，不表示超出本项的完整包批准；“待补”对应上面的具体问题。

|主体后缀|结论与覆盖|
|---|---|
|authentication_source|已有错身份/标签、假authority/grant、真凭据；认证异常见待补|
|session_isolation|已有同Principal双向caller/target/action隔离及A关闭B继续|
|service_principal|已有独立服务及1999/2000ms边界|
|scope_four_way|已有四来源同规则target/permission与owner/field反拼接|
|delegation_shrink|已有收缩、扩张/延时拒绝、新caller合法控制|
|recursive_delegation_denied|已有请求和认证ceiling再委托拒绝|
|owner_scope|已有source真实其他owner授权与拒绝|
|target_issued_identity|已有自制Target、跨来源候选/真目标控制|
|target_lifecycle|已有严格世代及删除重建旧视图拒绝|
|target_frozen_set|已有输入突变不漂移、多目标非法项拒绝|
|operation_exact_contract|已有修订5同Key异Digest原子失败，旧permit/response有效、新Key及未知source Key控制|
|group_members_complete|已有四来源缺成员权限拒绝|
|group_substitution|已有正常contract摘要变化与强制碰撞原始身份拒绝|
|permit_origin_binding|已有实际CheckedEffect原permit消费及0→1记录|
|permit_once|已有重复issue同owner、消费一次|
|permit_concurrent|已有真实双线程barrier一次消费|
|revoke_before_consume|已有受控先撤权零尝试及重授不复活|
|consume_before_revoke|已有先消费保留获准尝试|
|cancel_arbitration|已有两顺序与未交付外部stop不冒充取消|
|expiry_boundary|已有request已到期、配置30秒截止、负epoch/溢出及service边界|
|generation_exhaustion|已有实际小上限与ID不复用；旧对象跨Store待补|
|owned_inputs_budget|已有真元素别名、安全Auth释放探针、空控制块、租约类型和活跃owner容量；观察文本待修|
|exception_atomicity|多个实际端口/分配/析构控制已有；auth/中途subscribe待补|
|query_field_projection|已有字段/用途拒绝；UsePolicyInput所需权限来源待补|
|page_owner_authorization|已有扫描前owner拒绝及隐藏空页相关控制|
|page_binding_isolation|连接/大小/期限已有；委托/owner/phase待补|
|page_revoke_live_keyset|已有隐藏项空页前进、upper排除新行、页间generation拒绝|
|subscription_scope_atomic|已有小配额混合filter失败后恢复、实际合法发送；异常原子性待补|
|subscription_connection_cleanup|已有A关闭返还队列容量且B发送/执行不取消|
|queued_revoke_drop|get多模式/list撤权已有；换Store及Watch组合待补|
|transmission_start_arbitration|已有真实字节两顺序；编码元素别名实际red待修|
|failed_start_not_started|已有双票据容量、明确零字节重试及重验|
|unknown_start_no_retry|已有真实已发送不确定、同会话其他队列清理与B不受影响|
|unsubscribe_inflight|已有排队删除/配额恢复、Started不撤回|
|internal_component_boundary|原生类型/消费者已有；安装及依赖闭包由父任务实际wrapper另审|

## 核心结构及撤回的候选

具体Caller/Target/Permit类型与原对象地址/固定Session核验、每连接端口组合、完整组私有材料、相同短mutex仲裁消费/取消/撤权/发送符合已冻结设计。StoreClosed不可逆，快照更新先构造后仲裁比对世代；固定Key契约替换失败不提交。Hold把仍在途owner继续计费，原permit持有后新发行容量拒绝有实际控制。queue预留、drain链接摘除及锁外销毁、reservation owner保活避免任意析构进入短授权段；无新增生产Runtime/Host/索引依赖。

本轮曾提出“共享ExecutionSummary可变别名漂移”的候选，核对CoreContracts后撤回：ExecutionSummary final、私有构造、所有赋值删除、create深拥有且value只读，合法调用者不能改变同对象。source换成新Summary会与原对象比较，不需为反例引入const_cast。曾考虑巨大facts先复制的候选也未升级：CoreContracts已有facts<=8和ErrorInfo拒绝约束。以上不是已确认缺陷，不运行恶意内存写探针。

本报告仅对冻结实现及新增独立反例给出ChangesRequested。三配置正式矩阵尚未在本审核时完成，根wrapper由父任务负责；既有37条native成功不能替代这些验收证据。修复后应保留本报告并另绑定新SHA增量复核。

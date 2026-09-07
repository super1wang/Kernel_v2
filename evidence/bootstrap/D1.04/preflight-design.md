# D1.04 只读预研：授权实现设计约束

本文件为AI review_contracts只读预研建议，不是已批准API、不表示D1.04开工或Passed。读取时HEAD为fb08a1955ebfc8b8d97062962b61cac901880953，D1.03正式验收由主任务处理。仅新增本evidence文件，不改当前source_patterns覆盖的文件，不提交实现。

依据：执行卡D1.04；架构A06.1、A10、A17.4–A17.6；现有CoreContracts context.hpp/identity.hpp/observation.hpp。正式进入D1.04前须独立核对其实际前置D1.02和D0.05的Passed与精确证据，不能用D1.03的运行状态代替。

## 1. 本包应交付的最小真实能力

内存策略存储、认证来源接入、会话受限委托、真实目标解析、权限世代和撤权、一次性permit消费，以及查询/分页/订阅/出队发送的授权接入。沿既有CallerAuthorityPort、TargetAuthorityPort、PermitAuthorityPort实现实际发行集合，不另建第二套公共Caller/permit协议。

仅做授权侧的真实短仲裁及确定性接收消费者。生产Named Pipe、完整订阅帧/游标MAC、执行索引、Host启动、真实设备发送、Durable批准及存储留既有后续包。不能因为授权检查成功就声称提交/效果已发生，也不将ActivityLease、ResourceLease或生命周期存活当成ACL授权。

## 2. 建议具体内部接口

以下为待正式规格审核的接口方向，不能直接视为冻结签名。

### 认证与连接独立性

- `PolicyStore::create(PolicyBudget, ClockPort)`：组合根唯一装配的内存政策owner；私有记录，数量/文本/期限有界。
- 可信认证适配器产生内部 `AuthenticatedSession` owner：包含已验证Principal、唯一Connection/Session身份、有效期限、委托上限。构造不对请求DTO开放。不能靠调用方填写role/trusted/approved产生该对象。
- 为每个会话创建固定的CallerAuthority实现，authenticate(request)只核对请求是否与该实例可信会话一致。不同连接即使同Principal也有不同authority或不可互换的会话绑定。共享ACL Store可以相同，但禁止使用可被并发覆盖的全局 `current_principal`。
- 内部 `VerifiedCaller` 保有真实CallerGrant及CallerView的配对owner，用于既有issue(CallerGrant, binding)与resolve(CallerView, target)衔接。不要为方便新增公开grant可写记录或万能“构造已验证caller”工厂。

CallerView只公开description与belongs_to/revalidate，没有公开grant句柄或连接身份。这是重要设计约束：建议采用每连接固定CallerAuthority，真实接收端明确持有该连接的expected authority，避免同一全局issuer下只凭Principal把A连接的委托视图当B连接使用。服务自治使用单独受审ServicePrincipal入口，不能复用已断线用户会话。

### 委托与实际权限

- `DelegationScope` 为深拥有/冻结材料：被允许操作/权限、目标范围、允许读取字段、允许代表的owner、期限、是否允许再委托。无隐式再委托；如首版不支持递归委托，应明确拒绝相关请求。
- 有效集合严格求交：主体ACL ∩ 会话委托 ∩ 已安装模块政策 ∩ 实际目标规则；不是把各来源grant求并集。管理员身份也需显式政策，而不是通过owner参数触发特殊放行。
- 政策配置/撤销入口只给组合根或可信管理端，调用方对OperationKey/ContractDigest的声明不是政策来源。注册绑定及策略使用精确版本/摘要；D1.04可先用受信安装的窄操作政策目录，D1.05再接实际Registry，不为本包强加D1.03依赖或增加动态服务定位。
- 组请求必须保存每成员精确操作、实际目标和所需权限的规范化集合；组摘要仅标识已核验集合。`batch.execute`权限不能替代成员权限，换成员、换目标或换摘要必须重新授权。

### 目标解析

- `TargetAuthority::resolve` 从可信、受界限约束的目标目录解析ObjectId；保存目标实际对象身份/生命周期世代及会话或caller绑定，返回私有发行TargetView。
- 不从TargetView自己声称的valid()或请求ObjectId“看起来非零”推导授权。同issuer下A/B两主体、同主体不同连接、已销毁后复用目标标识均须校验真实记录。
- 请求中选中对象在准入阶段冻结成明确目标；后续发送/消费不能再读取漂移UI选择。目标集合须预算、去重与规范化；多目标不因第一个授权成功就放行整组。

### permit消费及撤权

- 复用既有PermitBinding；真实接收端根据当前精确操作或组、解析目标、ACL世代、生命周期世代和服务端期限构建expected，不能取 `permit.binding()`当expected。
- issue验证真实CallerGrant发行记录、当前委托/政策和目标规则。私有记录同时绑定会话身份、相关权限/委托版本和原始permit对象身份，不能仅比较DTO字段。
- consume与revoke/会话失效/委托收缩进入同一个短仲裁：校验全部当前事实，然后一次状态转换Issued→Consumed。两线程消费只有一个成功；撤权先赢则不消费，消费先赢则已获准尝试不被迟到撤权改成“未发生”。消费成功仍不等于设备已经应用。
- 生命周期当前事实可由可信目标目录/协调者提供，但不要把ACL世代和生命周期世代合成一个锁或一个计数。许可中记录相关世代，消费按各自当前值重验。现有Context拒绝零permission/lifecycle世代，首版初始化建议从1开始，不通过删检查引入另一种缺省语义。
- 所有单调身份/世代耗尽明确拒绝并封锁对应新发行，不回绕；过期用steady_clock，截止相等即过期。限额检查先于大容器复制，异常不留下半发行/半消费记录。

## 3. 观察和发送的真正安全边界

建议明确 `authorize_owner_filter`、`authorize_execution_fields`、`authorize_subscription_filter` 和 `begin_send` 四类窄用途，而不是一个永远返回allow的通用predicate。

- owner只提出过滤条件，self由可信会话解析。get/list/wait/cancel/结果等用途分别授权；能读摘要不自动能取消或读完整结果。
- list逐页逐目标复验当前授权，可见计数和错误不能包含隐藏对象。cursor绑定会话委托视图/过滤/宿主及期限，cursor本身不赋权。D1.04验证窄分页授权绑定，真实无状态MAC编码留D2.04；不能偷引入有状态cursor池。
- subscribe逐一验证整个过滤范围，失败无部分监听；缓存可保存静态规则，不能永久缓存“已允许”。queued内容保存拥有型最小投影和原始目标/会话绑定，不在队列里复用已过期的allow布尔值。
- 发送前按当前权限世代核验目标及最小字段。撤权与发送开始必须有可测的线性化边界：Queued→真正已授权交给发送端开始，不能实现成“先check再释放锁，稍后无条件send”。已经进入传输的字节不可撤回；尚未开始的无权帧丢弃。不要持锁执行任意用户回调或阻塞I/O，可由固定可信发送协调端串行完成授权与非阻塞交接。
- begin_send若返回票据，必须解释为何票据无法被无限期排队后绕过后续撤权；仅创建一个“曾经允许”的token不满足A17.5。D1.04用确定性发送端展示实际状态转换和两种先后，不能只检查bool。
- unsubscribe查找范围限当前连接，同Principal另连接也不能猜ID退订。不存在、旧世代、其他连接统一不可枚举响应；撤权/断线只清该连接资源，不取消业务执行。

## 4. 必须先行的真实反例

正式测试名称/总数需在API审核后冻结；下列为所需行为，不是已运行集合。

| 组 | 反例与控制 |
|---|---|
| 认证来源 | 自填Principal/role/trusted、假authority、同字段伪grant拒绝；真实适配器会话成功 |
| 会话隔离 | 同Principal A/B连接不同委托，互换CallerView/目标/permit均不能扩大授权；A断线不误撤B，也不能继续用A发送 |
| 权限交集 | 主体允许而委托/模块/目标任一拒绝即拒绝；不能靠其中一个宽集合覆盖其他限制 |
| 组许可 | 有batch权限但缺一成员权限拒绝；换成员/精确版本/目标/摘要拒绝；完整成员控制成功 |
| 目标真实性 | 伪TargetView、跨issuer、同issuer A/B互换、目标删除重建/生命周期变化拒绝；漂移选择不改变准入冻结目标 |
| 一次消费 | 双线程消费同permit只一成功；同字段另一permit不能借真实permit消费；异常不产生第二次成功 |
| 仲裁 | revoke→consume拒绝、consume→revoke保留已获准尝试；期限边界、委托收缩、会话失效均进入同一当前校验 |
| owner/分页 | 用户填他人owner、跨主体cursor/委托视图、页间撤权、隐藏总数/错误泄漏拒绝；有限扫描空页仍推进 |
| 订阅/发送 | subscribe允许后撤权、合并队列残留、初检后出队前撤权都不新发敏感数据；begin_send先赢时不声称撤回已发字节 |
| 退订 | 猜其他连接订阅、重复退订、旧世代不泄漏存在性；断线清监听而执行寿命不变 |
| 冻结/预算 | move输入留元素别名、共享可变委托集合、空owner、issued公开注入、超文本/数量/总计/身份耗尽均有控制；不靠普通copy假装深冻结 |

交错测试使用可控barrier/状态步进和真实仲裁实现，至少一个实际并发双消费控制；不依靠sleep猜竞态，也不能用测试端事后写入“revoke先赢”自证。每个拒绝核对消费/发送/发行数量，成功控制核对真实记录变化。失败与重跑证据独立追加。

## 5. 容易遗漏的接口组合问题

1. 原始permit必须和上下文/本次操作绑定保存；不得验证一个Context后消费接收者成员中另一个同字段真permit。D1.02已出现并修复过同类夹具缺陷。
2. CallerDescription.delegated_by不是委托证明；授权链应来自会话记录，公开description只能投影事实。
3. 只增加permission_generation不能自动撤销所有旧grant：consume、Target重验、缓存、分页、出队发送都必须核对当前相关世代。撤销后重新授予也不能使旧permit因字段重合复活。
4. Resolver成功不是发送许可；查询返回的目标/结果也不是后续操作的扩权票据。资源lease保活同样不授予访问。
5. 私有发行对象不提供可变集合/可赋值记录别名；同样应防空控制块aliasing shared_ptr冒充实际owner，但不承诺验证任意恶意C++指针图。
6. 不为本包实现审批UI或AI批准broker；只保留后续所需的受限政策边界，任何approved=true都只是请求数据。OS同用户权限问题不能靠应用会话虚称已隔离。

后续建议先冻结认证适配器信任输入、每连接authority布局、组成员绑定和send线性化点，再确定具体C++声明与固定反例。未冻结这些入口前，不宜先写一个可自填“已认证会话”的方便测试工厂。

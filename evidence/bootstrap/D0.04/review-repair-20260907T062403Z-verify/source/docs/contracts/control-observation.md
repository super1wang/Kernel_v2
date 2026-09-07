# D0.04-b：Control 观察与枚举合同

依据 A03/A05/A09.5/A10/A15/A17/A19/A21。独立 wire 为 `ock.notifications/1` 与 `ock.execution.list/1`；订阅不进入 Plan 节点。JSON Schema 2020-12、显式字段 pattern/范围、未知字段拒绝；format annotation 不能代替安全格式校验。仅从注册的本地 Schema 资源解析引用，不允许网络加载。D0 为确定性模型；D2.04 实现帧与连接生命周期，D3.04/D3.07 才验证真实执行和多进程观察。

## 三种请求与一种推送

| 方向 | 方法 | 字段/结果 |
|---|---|---|
| 客户端请求，有字符串 id | notifications.subscribe | filter、topics、可选 min_interval_ms；返回 subscription/stream/host、规范化过滤及topics、first_sequence="1"、replay_supported=false、有效间隔/容量 |
| 客户端请求，有字符串 id | notifications.unsubscribe | subscription_id、stream_generation；返回 removed:true/false |
| 客户端请求，有字符串 id | execution.list | owner（默认 self）、phase_set（默认 nonterminal）、page_size（默认50）、可选cursor；返回 items、可选next_cursor、consistency、host、retention_scope |
| 服务端推送，无 id | notifications.event | subscription/stream/host、sequence、topic、ExecutionRef、observation_version、gap、data |

客户端 event 方向错误不执行业务，不对无 id notification 发送响应；按协议关闭或记录有界诊断。订阅/退订不创建 Task、不要求业务 IntentKey。首版不支持顶层 batch。能力宣告只列实际装配方法；这些 Schema 不是声明 Host 已实现方法。

## 身份与有界字段

`common.schema.json` 是共享 DTO 的唯一定义：ExecutionRef=`{execution_id:"32位规范小写hex"}`；PrincipalRef=`{principal_id:"32位规范小写hex"}`。Foundation Tagged128 各身份域以独立字段标签区分；host_incarnation、subscription_id、stream_generation 同样为规范小写32位hex，但不是业务执行身份的组成部分。业务身份不随 Host 重启强制变化；订阅、stream 每次建立均新分配且不复用。测试标签由测试适配器映射到Tagged128，仅为确定性见证，不规定生产ID生成算法。

所有64位序号/观察版本用无前导零十进制字符串，并在语义层确认不超过2^64−1；序号/有效观察版本从1开始。计数耗尽拒绝或关闭订阅，不回绕。序号不是 observation_version，更不是业务 revision/CommitId。

filter 二选一：1–32个去重 ExecutionRef；或 owner=self/PrincipalRef。topics 为非空去重集合，只允许 execution.progress、execution.phase、execution.fact。其他owner只有显式管理/委托权限才可查询；模型仅装配self授权政策，跨owner请求统一NotAvailable，未来实现不得靠该简化跳过委托测试。

进度 data 为 `{completed,total,published:false}`，数字仍为64位字符串；phase 为 `{phase}`；fact 为 `{kind,reference,published}` 的最小摘要，StateCommitted 必须 published=true。完整 Args、Outcome、资产正文不进通知。list条目仅允许 ExecutionRef、operation、可公开owner/parent、phase、version、小进度/最多8项事实摘要；不加载大结果。Unknown/无权目标统一不可枚举存在性的NotAvailable；owner过滤、总数、cursor或退订均不提供隐式权限。

## 建立、查询与寿命

短注册屏障：验证当前权限和配额 → 安装pending-ack监听 → 先将成功响应加入连接发送序列 → 允许事件发送。屏障中变化可合并/丢弃，必须记gap；失败不留监听。确认响应丢失时客户端关闭连接释放未知订阅，再重新认证连接，不能无界重复subscribe。

客户端固定 `subscribe成功 → get（owner订阅则list）→ 合并通知`。已Terminal的保留执行可以订阅，但不补历史；建立期间Terminal由随后快照/提示覆盖。订阅确认之前绝不能发送该订阅的event。客户端可用有界缓存；若本地丢弃或无法缓存，必须置重新查询标记。模型选择丢弃未知快照提示并要求get，不承诺重放。

同ExecutionRef/host只接受严格更新的observation_version；旧get不能覆盖新通知，重复/旧event不能倒退投影。以subscribe回复记住stream_generation，旧stream不生效。gap、序号缺口、断线重连、Host世代变化、本地丢弃及准确终态需求均重新get/wait。Finalizing仍非Terminal；最后一条提示可能永远丢失，因此不能仅凭gap等待终态。

退订只在当前连接活动订阅查找。重复、旧世代、其他连接、未知id都返回false，不泄漏另一个订阅；成功撤销未开始发送的事件，已开始的帧可能在途。断线、会话失效、Host停止最终清理移除订阅，不取消执行；重连不复用stream、不补历史。订阅没有持久ACK/replay、结果pin或无限历史。

## 授权仲裁、队列及限速

验证过滤时必须整项通过，不部分安装。每次发送开始前按当前有效主体/委托世代及实际目标重新授权；撤权后丢弃尚未开始发送的无权队列数据。发送开始与撤权采用短仲裁模型，已经发出的字节不能撤回。模型连接只由测试认证适配器建立，RPC参数不能构造VerifiedCaller或trusted事实。真实安全适配器仍由D1.04/D2.05验证。

sequence 在初步授权/过滤后、合并/丢弃前递增，不因隐藏全局事件泄露计数。进度按订阅/执行/topic合并，保留新序号并移到正确排序位置。自上次发送有丢弃/合并则下一条gap=true；无下一条时不保证能观察gap。外部队列不吞内部Await/完成信号，不阻塞执行Terminal、提交、get或cancel。

预算开发初值：连接/主体/Host订阅8/32/256；每订阅pending128；通知编码16KiB；拥有队列连接/主体/Host为1/4/16MiB，首个超限拒绝或drop。模型按单一拥有事件编码加固定128字节元数据见证的逻辑预算核对，不把其冒充生产allocator实测。progress默认100ms、服务器最小50ms；更低请求提高到50，调用者只能降低频率。phase/fact可立即排队；若旧progress限速挡住后序phase，允许丢该提示并置gap，不能让进度间隔强制拖延phase。所有剩余帧保持sequence有序。

慢读超过受控transport_timeout时关闭连接并清理。实际字节流已开始的大帧仍有队头阻塞，写调度优先控制应答并限制通知帧。可靠及时控制默认独立认证的观察与控制连接；不要求第二Host。D0显式模型close_slow表示期限已判定，不声称测过真实Named Pipe超时或公平调度。

## 列表与cursor

顺序固定listing_ordinal降序，身份在当前host唯一递增不复用。第一页固定upper_ordinal；后续严格小于上一扫描position。新执行只出现在重开的列表；状态/权限每页重新判断，删除可消失，页间变化可能需重开列表才看到。consistency固定live_keyset，retention_scope固定managed_active_and_retained_terminal，不返回全局一致快照或精确总数。nonterminal包含Suspended/Finalizing等全部非Terminal阶段，短Invoke不列举。

默认页50、最大200，每页默认候选扫描2000。空items仍可携带推进next_cursor；next_cursor仅表示候选未结束，不保证下一页非空。按owner/状态有界索引扫描的生产性能在D3验证；D0小模型用排序候选见证分页语义，不能宣称无限历史扫描性能通过。

cursor只能是无状态认证opaque token，编码≤2048ASCII字节、寿命≤120秒、每cursor服务器句柄/pin/对象=0。详细字段、canonical、HMAC输入、secret生命周期和固定golden见 [cursor-v1.md](cursor-v1.md)。不得添加“也可以服务端cursor句柄”的首版分支。

独立审查补充：同一执行在同一Host下已观察Terminal，后续更高版本也不能重新投影为非Terminal；拒绝该投影并要求重新查询。subscribe确认同时固定当前Host，旧Host在途get响应不能清空或覆盖新Host视图；新Host确认时清理旧stream/sequence。两个反例分别覆盖snapshot/event和跨Host响应。


独立审查补充：客户端区分“最近完整快照版本”与“部分通知合并版本”。例如丢失phase v2、收到progress v3后，本地phase仍旧，此时同版本完整get(v3)必须能够补全；仅订阅progress时也适用，不能仅用单个observation_version把完整get当重复。真正更旧的get仍拒绝，完整投影同版重复可忽略，gap后同版完整get可重新确认。已有Terminal不可重开及subscribe确认host绑定约束继续优先生效。

客户端仅接受属于已确认活动 `(subscription_id,stream_generation)` 的通知；活动集合为空表示没有任何获准stream，不能变成通配。`Client.retire(id,generation)` 是本地退订步骤，先移除精确活动stream并丢弃其后到达的在途帧，未知/旧世代退役返回false。`Client.disconnect()` 清理全部本地stream、观察缓存与host关联并要求重建快照；重连按新subscribe ack建立成员资格。服务器退订不能撤回已发字节，因此本地退役是必须独立验证的客户端寿命步骤。

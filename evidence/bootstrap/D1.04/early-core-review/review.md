# D1.04 已写核心辅助逻辑早期 AI 检查

- actor_type：AI；review_contracts；结论为限定候选意见，不是整包Approved。
- 快照：本目录 `policy-initial.cpp`，SHA256 `973bbb0aa0ce01c8aa8d8d1db3a7d0c86e4810ccf40eaaaea284afd98fdbd754d`。
- 源仍由实施者追加；行号绑定快照。未编译/运行反例，以下静态路径必须由实际red和合法控制验证。未评价尚未写出的函数。

## P2候选1：委托缩减能增加拥有材料但未更新全局计费

快照59–60、182–185：narrower逐个新规则查找任意一条旧规则包含它，没有要求一对一。将一条规则复制成N条仍判为合法缩减；restrict_delegation只用独立Meter检查candidate，然后替换scope，没有更新session.hold.usage或Store.used。各会话均可在原始少量账目上累积更多规则文本，绕过Store整体declarations/text预算；即便简单去除完全重复规则，多条不同子集仍可拥有比原规则更多总材料。

应补反例：全局预算只剩少量空间，开会话时scope仅一条；构造N条相同规则（以及多个不同合法子集）使新实际总拥有材料超过全局限制、但单份Meter仍在限制内，restrict必须拒绝并保持旧scope/世代可用。足额预算下合法缩减成功；释放材料后预算可再次使用。修复应按真实新旧拥有量在同仲裁原子调整，避免只拒绝重复掩盖一般子集展开。

## P2候选2：TTL溢出检查本身可能溢出

快照23：`TimePoint::max()-now`先做有符号duration减法。now为合法负epoch时间（例如duration(-1)或min）时，该减法越过rep最大值；小正TTL的最终结果本可表示，却已经触发有符号溢出。ClockPort的steady_clock语义不保证epoch非负。

应补直接checked算术反例/合法控制：负now加1ms可表示时成功、接近max且空间不足时拒绝、恰好到max的边界按合同处理、TTL转换过大拒绝且不执行溢出表达式。采用rep/period安全检查，勿以固定负now拒绝替代对合法输入的checked计算。

## P2候选3：已写resolve仅判断存在，无法统一无权/未知响应

快照187–193：固定caller/current通过后仅调用stamp查目标，无权限规则检查；因此会话完全没有目标B权限，只要目录存在B即可获得成功TargetView，不存在C却得到TargetUnavailable。API第3节明确无权/不存在同响应。此处并不是prepare还没写完造成的缺方法判断；当前resolve已有完整返回路径。

应补反例：同一个可信会话的规则仅授权A，目标目录同时有A/B；resolve A正控制成功，resolve B与不存在C返回相同拒绝，跨连接caller仍拒绝。接口未携带use/operation，实施时须明确目标可解析资格的受控判据，不能把任一目录项成功当成已有动作授权；动作的精确tuple仍由prepare检查。

## 已查方向与实现提醒

- match要求所需permissions整体包含在同一条规则，同时匹配operation/target/owner/field；当前函数本身没有把不同规则权限拼接。保留A/read+B/write不能授予A/write的实际反例。owner_candidate只准扫描候选，不能把其成功直接当成每个结果已授权。
- source_current和close_store以同一Store mutex设置永久closed；未来所有最终发行/消费/发送均须检查closed。已有Hold析构重新取该mutex，因此最后一个Hold owner的释放必须保持在锁外；需在实际完整调用链检查，不能只看辅助函数宣称无死锁。
- Grant、Verified、Target等当前每次返回均新分配而未具有独立Hold。建议小预算下保留大量返回owner的计费/回收探针，核对最终采用的有界缓存或账目方案；这里只列资源生命周期核查点，不凭未完成总体设计另判必改。
- 未把枚举负值检查、尚未追加的发送/观察方法或当前不可完整链接列作整包缺陷。无并发大矩阵运行，无受审源码/测试修改。

# D0.04：execution.list v1 cursor

唯一方法版本 `ock.execution.list/1`，与一致快照的result.read cursor不同。token不提供机密性，也不是授权票据；每页继续验证当前权限，payload不含秘密、会话凭据、隐藏目标或其他调用者无权获知的信息。

## 编码与MAC输入

外部形式为 `v1.<base64url(payload_bytes)>.<base64url(mac_32_bytes)>`，不带 `=` padding，base64url必须规范（解码再编码字节相同），整串最多2048 ASCII字节。禁止额外段、未知envelope版本、未知算法及非规范表示。

payload固定字段集，未知/缺失字段拒绝：

| 字段 | 含义 |
|---|---|
| protocol | 固定ock.execution.list/1 |
| alg | 固定HS256 |
| host | 当前host_incarnation，Tagged128小写hex |
| store / restore | 有存储时StoreId与RestoreGeneration；无存储时二者都为null |
| caller | 当前VerifiedCaller的PrincipalRef |
| view | 受约束的授权/委托视图版本标识；模型用无前导零十进制世代 |
| filter | owner解析到PrincipalRef、phase_set取明确枚举；不保留self别名 |
| sort | 固定listing_ordinal_desc |
| upper | 第一页固定上界，uint64十进制字符串 |
| position | 上一页实际已扫描位置，uint64十进制字符串；后页严格小于它 |
| issued / expires | Host签发与到期UTC秒，uint64十进制字符串 |

canonical profile只用于该固定字段payload：JSON对象键按ASCII字典序递归排列；无空格/换行；字符串JSON转义；ASCII输出；不允许浮点、重复键、非有限数或任意扩展类型。十进制计数无前导零；所有身份/枚举为Schema及上表限制的规范ASCII。读取后重新canonical编码必须与已认证payload原字节完全相同。此局部格式不是A12业务canonical CBOR profile。

`mac = HMAC-SHA-256(host_secret, ASCII("ock.execution.list/1") || 0x00 || payload_bytes)`。前缀做用途分离；固定32字节mac，验证使用常量时间比较。先检查最大token长度、分段和base64url，再核对MAC，才解释字段。未知算法不能选择非认证fallback。

每个Host incarnation由加密随机源新建256位secret，仅宿主私有内存持有；不得写入cursor、日志、回执或持久恢复记录。Host重启/停止销毁旧secret，新incarnation重建，不接受旧token。明确安全重置secret会使现有cursor失效，不改变ExecutionRef业务身份。测试golden注入固定00..1f的key，只是公开已知测试向量，不能作为生产secret。

## 验证与失效

绑定host、可用时store/restore、caller、当前有效授权/委托view、规范化filter、sort及协议。过滤/身份/世代/授权视图变化、MAC错误、未知版本/算法、长度超限或不合法扫描位置返回CursorInvalid；到期返回CursorExpired。两者不自动从第一页重启，不向越权owner泄漏计数或对象存在性。

必须 `1 <= position <= upper <= 2^64-1`；首次内部起点为upper+1，仅内部宽整数计算，不编码为uint64。签发时间不能在当前Host时钟未来，`0 < expires-issued <= 120` 且 `now < expires`。后续页沿用最初issued/expires，不通过翻页续命。真实Host还需单调时钟/回拨策略确保TTL不因时钟变化无限延长；D2.04将此处UTC到期限制换算受控单调剩余预算，不持久化steady_clock原值。

每页最多扫描2000候选、返回最多200条；服务器收紧可更少。若有候选未结束则最后实际扫描ordinal必须严格变小，空匹配页也推进；没有继续候选则省略next_cursor。Signed但position为0、超过upper或输出不推进仍拒绝，不能把有MAC视为任意状态合法。

服务器只保存每Host一个secret及既有执行索引/授权世代，**每cursor句柄、对象、pin=0**。客户端保存token，没有cursor release接口。将来有状态游标只能新方法/协议版本并定义数量、内存/pin及清理政策，禁止在v1静默加入。

`tests/control/golden/cursor.json` 固定payload、context、canonical ASCII、MAC用途前缀、完整mac_hex与token；模型测试逐字节对照。篡改、未知算法、调用者/过滤变化、撤权、过期、跨Host/Store/Restore、超长、非规范base64url、无进展与零句柄都有独立合同测试。这个MAC测试不证明真实认证边界或secret操作系统保护已实现。

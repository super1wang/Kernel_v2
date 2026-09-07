# D1.04 固定35主体覆盖预审

- actor_type：AI；review_contracts。
- 结论：ChangesRequested（计划既定子断言尚未全被实际测试）；不作整包批准，也不把以下缺测试直接判为产品漏洞。
- 已读测试原字节：`coverage-preflight-source/`，五份SHA见其source.json。
- policy_tests.cpp：`5824d08b7938be6a7a8a29eaaf80f31e439a5f63d368468bb6238fd024a4558e`。
- action_cases.hpp：`6b7764d720644e10d33716177ca7419e40d729c847e15d2f718475c8ba61d4d6`。
- session_cases.hpp：`8fc2058117e601d6d244faaa2810a2f9a5bf63f5c186d56e6a13cd07e634037b`。
- send_cases.hpp：`b8b78d9242c4a46f9aff1f752cb9788d84abfb155bc2b58aa7da5cfec3496200`。
- fixtures.hpp：`a377312b852e6f0139c8ccd29521a33d65403cca7520a7576f37e94ad64e9740`。

对照API修订4及计划35行。action/session已审范围沿用action-tests-fix-review.md、session-tests-review.md；本轮重点检查剩余子断言。所有行号指快照policy_tests.cpp，非持续变动的工作区。未运行新测试，未改产品/测试。已知noexcept、Sink、queue清理及Grant/Target计费在修，本报告不重复列其实现缺陷。

## 必补覆盖（均P2，归入原主体，不增加35名称）

1. **scope_four_way，47行：缺同tuple不拼接反例。** 当前仅将四方某一方全部清空。若实现先合并规则targets/permissions再匹配，现断言仍通过。分别构造同源(A,read)/(B,write)请求A/write；再构造owner/field与target错配的规则，仅各自完整tuple应成功、拼出来的tuple必须拒绝。不能用权限全空替代。

2. **authentication_source/target_issued_identity，40–46行：缺真实伪造对象和标签路径。** 当前认证仅错误credential和错误Principal；目标仅存在性及不同用途候选。补tags自报被拒、固定authority验证同description的自制CallerGrant、相同ObjectId自制TargetView被拒并保留真对象正控制。action的ForgedGrant只验证issue入口，不能代替CallerAuthority::validate；D1.02通用fixture也不证明D1.04具体authority实现。目标候选还应以仅GetSummary合法控制及解析后实际owner/field仍无权的查询拒绝，证明投影预检不升格为数据授权。

3. **owned_inputs_budget，15–30行：缺输入拥有边界及嵌套材料。** 当前确有action/permit文本、委托展开、Grant/Target活跃owner计费；但没有move前保留vector元素引用、共享可变源材料在发行后突变、空控制块别名shared_ptr的入口拒绝。补配置/委托移动元素别名突变仍不改变授权、source已返回的Summary别名不能改变response原投影、非空get但use_count==0的端口或lifetime owner明确拒绝；并让深层规则owner/field/target数量及长版本/权限文本分别触碰声明/文本预算，有足额正控制。现字节预算算式只覆盖默认短fixture，不能代表全部嵌套输入。

4. **query_field_projection/owner_scope，51/55行：用途隔离与实际owner联合字段不足。** 当前只有GetSummary字段删减及显式另一owner允许/拒绝。补GetSummary允许但ReadResult/ReadLog/ReadAsset/CancelExecution未授权拒绝，非Invoke权限确实来自UsePolicyInput（可信更改required_permissions后拒绝、四方补齐后成功），CancelExecution允许但GetSummary字段不足不能泄露摘要。用source中真实owner而非request过滤字段做错配控制；字段仅允许时保留progress/parent正控制，不只看删后为空。

5. **source寿命组合缺整条路径。** page_binding_isolation与query_field_projection没有source.restore=Present/未知enum创建拒绝，没有host失约检测导致StoreClosed并令旧响应零字节，也没有合法先close_store再更换host/建新Store的控制。按计划分别对get/list原Response及Watch排队：旧Store关闭后零字节，新Store成功且拒旧发行owner；普通progress/observation_version变更仍可授权输出。失约测试只证明检测，不虚称消除了任意恶意source的TOCTOU。无需生产Host或索引。

6. **分页，48/56/57行：隐藏项、空可见页及绑定维度不足。** 当前两行都可见，新增行被upper挡住，撤权用全局generation变动；未产生“扫描了隐藏项但可见页为空且continuation仍前进”。补scan_limit=1先扫隐藏owner/目标项，空页不能泄露ID/总数且下一页合法项成功；隐藏与不存在get错误按规定相同。续页补委托缩减、owner/phase过滤变化的拒绝；保持正常同绑定续页成功。不能只改变page_size代替所有绑定检查。

7. **subscription_scope_atomic，49行：失败不代表无部分安装。** 混合合法/不存在execution仅断言Result失败，未观察其是否泄漏一个Watch配额或留下监听。用active_watches/watches_per_session等小限额，先失败混合filter，再合法完整filter仍能占用全部预期容量；失败路径对应hint不能发送、合法watch可发送。明确覆盖存在但无权的第二项，不能只有不存在项。永久cache allow由后续撤权后的Watch发送零字节控制验证。

8. **expiry/generation，31–39/53行：遗漏不同最小期限来源与旧对象跨Store。** service主体已经覆盖认证期限的1999/2000ms，delegation覆盖收缩期限；clock_boundaries只检查算术，尚缺request先到、配置action_ttl先到时的到期前成功/等于截止拒绝。generation_exhaustion只比较两个StoreId不同，没有把旧Store已发行对象实际交给新Store的观察/发送端拒绝正反例。现递增/耗尽重试及撤权重授控制保留，不要求重写。

9. **exception_atomicity，54行：只扫描政策replace的前20次分配。** 该路径和析构重入测试有价值，但没有实际使认证、digest、source.find/scan、encoder、reserve这些允许抛出的适配器抛出；也没有针对open/prepare/subscribe/enqueue后的半发行/半Watch/预算泄漏控制。应在各可抛端口确定注入一次异常，确认异常或Result错误按合同传播，之后合法重试及原未消费许可/容量状态一致。不能让固定20点只命中准备前复制就声称覆盖全部分配；已知noexcept签名修复本身不在此重复报告。start_now noexcept失约不应按普通可抛端口吞掉/伪NotStarted。

## 其余主体和结论边界

已审action八项、session六项继续沿用各自限定结论。target_lifecycle已有真实替换/旧视图拒绝；owner_scope已有另一真实owner允许及拒绝。send_cases可见双顺序真实字节、NotStarted容量与重试重验、Unknown不重试、退订在途和跨连接清理控制；它们不补足上述source/隐藏分页/异常缺口。queued_revoke_drop当前只覆盖get Response的Store关闭，缺list/Watch/source替换及计划所述旧内容合并撤权组合，归第5/7项补齐，避免重复任务。

internal_component_boundary本地主体只有类型断言和prepare，所称SDK边界必须依赖实际配对wrapper；主任务正核对该工具证据，本报告不将一句stdout认作已完成安装/依赖检查。

本快照有35个名字不等于35项计划子断言已完整。以上仅要求计划/API已有范围，以独立反例、合法控制及实际失败/修复证据完成，不增添生产模块或新授权用途。

# D1.04 许可测试三项修复独立 AI 复核

- actor_type：AI；review_contracts。
- 结论：Approved（仅关闭action-tests-review.md列明三项）；不是整包或全部22项实现批准。
- 已审green：`implementation-5fb646b0f98d`。实际核对source.json十份源SHA全部一致；commands.json共24命令全部Exited/0，包含build/list及list列出的22个实际主体，八项action均在其中。
- 核心cpp：`049368aeceee711acb34815718add3dac631d1435f70166896aa1d7a0996d90f`。
- hpp：`a6fca851ce64d6985a77b5d543be1b73683b8797f58299c46584f272fff3764a`。
- action_cases.hpp：`6b7764d720644e10d33716177ca7419e40d729c847e15d2f718475c8ba61d4d6`。
- fixtures.hpp：`5103aef52a44794de78db9f2b611b5169202c1672b122f12f184738e8b9f8879`。
- policy_tests.cpp：`a31256d0bbf6d3d6534a227a7041b9ef08afd363442bd761489468755e6fbd18`。

本轮只读冻结快照及原始命令/错误记录，未另运行构建，未修改产品或测试。原ChangesRequested保留。

## 1. 实际context接收路径：关闭

CheckedEffect在create时将通过check的context与当时同一个permit owner私有配对保存；无公开替换字段。receive实际重验该context，交由固定action消费保存的原permit，仅成功后调用该context的record_attempt。测试确实传入同binding另一action许可组成的context，失败且记录0；随后原context成功记录1，再次接收仍拒绝且记录不增加。原action已消费而另一action仍可消费的控制同时保留。它验证真实窄消费者路径，不再只是创建context后丢弃不用，也不声称业务效果完成。

## 2. digest精确contract材料：关闭

实际red `action-tests-e53a4f20d9d9`：五份源SHA均核对一致，configure/build及其他主体退出0，group_substitution退出1；原始stderr为 `contract_fingerprint(1) != baseline_digest`。不是编译失败。

正常Digest现逐selector读取name、version和全部contract.bytes，并对字符串/成员/目标计数分界；包括envelope和每成员、anchor和有序目标。新增contract_fingerprint通过同步改可信目录/委托与请求构造合法材料，正常envelope相关及第二成员contract变化均有摘要差异断言；显式collision模式仍检查原始发行对象不能替换。green中此主体实际退出0。第一个变化同时涉及同精确操作的envelope及第一成员，并不是只隔离envelope一个字段；本轮结合具体selector调用检查确认两处都已覆盖，不夸称该断言单独证明envelope分支。

## 3. Action重复拥有文本预算：关闭

实际red `implementation-d93490630a16`：十份源SHA均核对一致，build/list等成功，owned_inputs_budget退出1，原始stderr为 `!probe.prepare()`，证明低预算仍成功准备。

修复后Action.request是指向其持有GroupSnapshot内部冻结request的const引用，不再另持第二份请求；Action.group shared owner保证引用寿命。prepare额外计入binding的envelope文本；Permit有独立Hold，计入自身binding文本，只有首次实际发行acquire，原始owner仍活着时仍占账目。

budget_contract的低预算控制只留两份selector文本，现prepare拒绝；四份容量下prepare、issue、消费实际成功。释放Action但保留Permit后，下一Action可prepare却暂时不能issue；释放旧Permit后新issue成功。这验证实际存活材料的计费及回收，而非仅增大测试预算或删除失败断言。对应green实际运行owned_inputs_budget退出0。

无剩余上述三项必改。此结论只绑定所列冻结SHA，持续开发中的其他源版本需重新核对；全包仍须其余固定主体、独立代码审核和正式矩阵。

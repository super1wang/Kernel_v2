# D1.04 观察测试补充实现说明（待独立 AI 审核）

仅新增 tests/contract/authorization/observation_cases.hpp，未改 dispatcher、固定expected、内核或规范；未增加CTest主体。实现者为AI子代理review_tools，本说明不是独立审核Approved。

调用映射建议：

| 既有主体 | 新增内部函数 |
| --- | --- |
| T19.policy.query_field_projection | observation_cases::noninvoke_permissions |
| T19.policy.page_binding_isolation | observation_cases::page_binding_dimensions |
| T20.policy.queued_revoke_drop | observation_cases::source_store_lifetime |

noninvoke_permissions先确认GetSummary可用，再真实replace_use_policy将required_permissions由allow改observe；逐步补齐主体ACL、operation/module、target、use/module仍拒绝，证明原会话委托及认证ceiling不自动扩权；新认证ceiling和新会话委托均授予后成功，任何一项仍缺则拒绝。目标沿用初始配置owner，没有制造目标身份替换。

page_binding_dimensions用真实第一页绑定验证同参数续页成功、owner/phase改变拒绝。合法缩减无关第二operation的委托后，重新verify取得新caller，旧绑定仍被拒绝；新第一页及其绑定续页成功，避免把旧caller失效当作cursor检查证据。

source_store_lifetime真实排入get/list Response及Watch三帧，先close_store并检查零字节/容量返还，再更换source host与summary.host，最后用同source新建Store。旧Response、Watch、caller和续页绑定在新Store被拒绝，旧发送器始终零字节；新get/list/watch则写入六个真实字节并检查三种投影标识。

实际独立来源 observation-tests-27ebadeedd13：configure/build与三个函数均Exited/0，全部WindowsJobObject先归属后启动、active_after=0；raw SHA及构建ZIP逐项核验。该轮只执行三个函数，不称35主体或整包Passed。源码快照固定，未追随同期内核预算改动。首轮observation-tests-d9a9ac03c7d6保留：分页与source函数通过，权限函数因测试另造configuration得到不同目标owner而在replace_target_policy失败；修正为初始配置共享owner后重跑。这是测试夹具错误，不能称内核行为red。

| 材料 | SHA256 |
| --- | --- |
| packages/runtime/policy/policy.hpp | bcd5fcb28468b6f7f519c3e99671a1f0a6554871da1be043bced89c2b1ee4fee |
| packages/runtime/policy/policy.cpp | c2e746bf6c11a5d1954e39e5e53888956e1f26b6535b9e3e29b1517e1d9ce449 |
| tests/contract/authorization/fixtures.hpp | cea9f58db8a47311faadba06469587e13e26a0292e4b8902b2f6d18901453db1 |
| tests/contract/authorization/observation_cases.hpp | 0d96ac513686f35c722936c7637d49c1645acea2ea3580e6667890158aa87416 |
| tests/compile/contracts/test_support.hpp | 86e94e83c42d0121e787bc7dae6b2f02785570b5f8c3b38d033f620aad66363a |
| commands.json | 822b911f4eb8299288460a9b8f72184b1864f22d316a6b2d7e49ad7791eda984 |
| build-artifacts.zip（61项） | 2d39c7514013e48889a81de3a49cd6025e54b1b828a41c61dbfdd22f7a95b6e2 |

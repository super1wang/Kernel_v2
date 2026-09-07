# D1.04 最终候选工具与集成证据 AI 复核

审核者：独立 AI 子代理 review_tools。**工具、固定清单及本轮 bootstrap 集成证据范围 Approved；D1.04 整包仍 Pending。** 当前编码器返回 vector 的别名候选由 review_contracts 独立验证，本文不关闭该候选、不代替完整内核规格/代码审核，也不声明正式 Debug/Release/ASan 矩阵已运行。

## 冻结来源与范围

核对 implementation-freeze-36315fd680b0 的12项源码 SHA 与工作树一致；implementation-8e072f1ca34a 的 build、实际 --list 与35个固定单名执行共37条命令均 Exited/0、active_after=0。所审范围是 QueueState/Frame/Sink/Unknown 清理、发送消费者、根工程授权接线、discover/verify_children/develop 和集成开发驱动、三份正式清单及归档。核心权限与完整预算证明仍归独立 contracts 审核。

三个 profile 的 common.inputs 均为同一223项，摘要 `08ecf883a2a7dd9c14848ad88485917fa7ac23ff405835284fb32cf5eda9c204`。该摘要也是本轮 integration 源码ZIP的摘要；结果 source_changed 的 added/removed/modified 三项均空。下列结论仅绑定这些字节，后续任何修复须更新摘要及受影响验证。

## Queue/Sink 和旧问题关闭

Unknown 现在于同一仲裁关闭会话，先终结当前帧、单次扣除队列预算并移除，再 drain 全部同会话 QueueState；局部 retired 于解锁后释放。因此此前 queue-lifetime-probe-5bd109a08c13 的剩余 reservation 清理缺口在源码上得到修复。当前 unknown 主体实际检查 pending==0，并以另一会话再次排入两帧和真实开始证明共享队列预算返还；不要求外部持有的终态 owner 立即释放。该主体在本轮35项和根集成JUnit均实际通过，历史 red 与 ChangesRequested 报告保留。

Store/Coordinator 共有独立 QueueState，注销和索引遍历受同一 Store mutex 保护；Coordinator 自身仍持 queues，注销不在锁内终结它。Frame 单独保有 reservation_owner，成员逆序保证 reservation 先析构、端口 owner 后析构；start 的局部 Frame 保活借用材料。close/退订先摘队时 charged=false 阻止旧 start 再次开始或错误删除新队头；反向首字节先发生则不回滚。退订、关闭、Unknown 的 retired 链和正常队列成员终结在仲裁外；charged 的扣款均受同一锁保护。

Sink 预分配32768字节接收区，reserve 计算已有 size 与 pending；未使用 reservation 的RAII析构返还容量，Started/Unknown消费、NotStarted保留。近满双票据、NotStarted撤权重验、Unknown禁止重试、退订和close清理均有真实消费者断言。两种撤权/start次序的 semaphore 位于 API 调用外，实际数组长度/字节作证。此确定性夹具的 size/pending 非原子，不宣称支持任意并发访问同一个端口；当前受控测试不产生该并行交错。

## 驱动、固定集合与清单

D1.03全部 expected case 对象和三CHECK原样保留；仅追加35主体。预期数量为 Debug251、Release251、ASan253，各3CHECK；regex覆盖各自完整固定集合，正式配置没有减项。三个source集合相同并包含D1.04 matrix。D1.03全部输出、runtime模式与最低约束在对应build目录映射后均被保留。

实际 runner --list 产生注册，discover不从expected制造实际测试；5个固定wrapper保持在原35主体内部。verify_children 先执行真实native，再进行四组正反编译或SDK安装边界。每条子命令单独owned Job及原始SHA，成功必须Exited/0/active_after0；负编译要求非0并匹配对应源码和MSVC诊断，Runtime拒绝要求精确组件不可用诊断。

build/d1.04-integration.py 开始冻结expected与完整source，结束重新运行实际 inputs，检测新增/删除/字节变化；此前新增漏检已用独立AST反例关闭。唯一输出目录保留失败，源码与构建ZIP读回核验。该驱动只验证35 policy主体的bootstrap集成，不代替正式完整矩阵。

## 实际集成证据

integration-e90b09d64c8b 为 bootstrap-integration-only Passed；JUnit35个名字精确等于固定新增集合。根configure/build/list/CTest原始命令正常退出，源ZIP223项核验一致。build-artifacts.zip 的470个条目逐项核验大小和SHA，且不存在重名。

5个wrapper共24条实际子命令、48份raw日志、12个正反cpp、4个positive.obj，5份commands与5份structure齐全。四组各native/configure/positive成功、两negative预期失败；8个负例逐条匹配正确源码及诊断。安装组native/install/CoreContracts配置成功，Runtime配置因规定的不可用错误失败；安装包未含policy头/库，目标元数据为内部库仅链接OCK::CoreContracts、其闭包为OCK::Foundation。

四个真实CMakeCXXCompiler.cmake均指向 `build/msvc-validation/67414627e4e76f7e/bin/cl.exe`；CompilerId/ABI材料包含在子构建归档和声明模式中。authorization runtime模式实际收集254个文件，逐个以validator同一Path.match规则核查未匹配0；全部10项最低约束均满足且收集文件均在归档中。原D1.03与SDK清单继承核对属于声明层，本轮只跑policy集合，未将继承旧测试称为本轮运行。

结果JSON SHA：`4bc0c3be94f9d42bef25b2df84904017ec0b917de76df40fcf49e6a4ea383176`。
构建ZIP SHA：`865421e244e07c6dc4682fc9527541fb8934fd7eae82a256cf1316f2afaa2992`。

## 精确绑定

| 材料 | SHA256 |
| --- | --- |
| CMakeLists.txt | 7ab27457dbaaaacfc19be411cd4c244812d41c3f53e3b9944e08f75ecce98e03 |
| build/d1.04-integration.py | 7de37ff3aab400966f3cffffc7110dc65d3b21e932431938110105018a88b9f3 |
| tests/manifests/d1.04.expected.json | a4d410e2f75610e61300559d3e73f0eb1bf6baacf24f74d070ed5a686195774f |
| tests/runs/d1.04-matrix.json | 3c58c8bcc67999054374c06bd03e89a759759cd0b458df4210d4113b872cc506 |
| tools/development/msvc_isolation.py | eb2fe6bb59efeb8d1c8279d4bb72719a4ff8bea3c83bb39a321a09f9b2dbf239 |
| cmake/LockedMSVC.cmake | ed0e029ec8e77653b2db779059b8b3483e600e8b27027c660ba01076ce94b3a3 |
| cmake/msvc-validation-tools.json | 67414627e4e76f7ea66817f14525c16f0d2e80ac30d0ef125724852f29339ed8 |
| packages/runtime/policy/policy.cpp | 6b81a3d524c98a625276804b143f12307d392216972c3abf3db5e7aebc875a58 |
| packages/runtime/policy/policy.hpp | bcd5fcb28468b6f7f519c3e99671a1f0a6554871da1be043bced89c2b1ee4fee |
| tests/contract/authorization/action_cases.hpp | 6b7764d720644e10d33716177ca7419e40d729c847e15d2f718475c8ba61d4d6 |
| tests/contract/authorization/authority_cases.hpp | 94b378ccea15926a3fff2daeba93725d7c07c4755e9c532f506d68a23082375f |
| tests/contract/authorization/CMakeLists.txt | 02b088132c84abd1fb90258ca7a918f64a237e9b54fcf161f37b2c8e3976af30 |
| tests/contract/authorization/develop.py | 3132328ddfbf2598764f7c27f0ae40e6a23938ad973922d7e07669a507c67224 |
| tests/contract/authorization/discover.py | 5a4a594fa6654cf7e3dbcd7670aa88c91d062d0b73c61b10bb3736734b7cb2f8 |
| tests/contract/authorization/fixtures.hpp | cea9f58db8a47311faadba06469587e13e26a0292e4b8902b2f6d18901453db1 |
| tests/contract/authorization/policy_tests.cpp | c3b34cebb00b11fd6a41af1d33953867be61ba0b7e572566f1c6b2295cd8b604 |
| tests/contract/authorization/send_cases.hpp | 6a32a9798598ebbd5a8e41d1325464aab324887fa37c44e21ac10731b65516c6 |
| tests/contract/authorization/session_cases.hpp | fa093665543df391c79f1879d618671846de72a43a7acce54f3d641edc95e528 |
| tests/contract/authorization/verify_children.py | 6817cc0f140fa4cea8920314e2248b3b66e0b27b275eadd54609a8a96764caff |
| tests/runs/d1.04-win-msvc-debug.json | 084f14e7d767a34b0323163fdf939ae3e331670da19aadbbcd0b0230e1c8a05a |
| tests/runs/d1.04-win-msvc-release.json | 13d16de429137c7ba7c75e6fef168494e25e35c82eef43ae1cfe915a98cba138 |
| tests/runs/d1.04-win-msvc-asan.json | 381ddaf5b086a732f04859d828b41991de0018f75f4f0adbc45761925dcadf5f |
| final-tools-source-audit.json | 68a50262c0ced64bb815ded24d7f5cee90ed6bb2645ef636e1b77df7e2535c9f |
| final-tools-integration-audit.json | a04d4fbe6bed010655670cab152ccd4f20e7231b0f7114f1d744d8eca06967b9 |

本次仅读源码和既有真实归档，新增两份独立核验JSON与本报告；未改源码、规范、历史证据或提交。若encoder候选导致修改，本文继续作为旧来源审核记录，不自动覆盖新来源。

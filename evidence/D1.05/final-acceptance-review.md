# D1.05 最终独立 AI 验收

结论：**AI Approved；D1.05 当前固定矩阵自动验收 Passed**。没有剩余必须修复的问题或未完成的本包验收条件。这不是 human Approved，不批准G1、SDK Runtime、Host或GUI/CAD/CAM/设备等产品范围。历史失败与原始报告均未改写。

验收精确实现为 `a8049df35d7428e79d89df9b0f2f223fa7002bda`，255项输入摘要为 `028ca1c8afab6dfb2b5c3c6758ce4b71f13311957cc78bdc2463e6993a265560`。实际重新读取全部255个Git blob、核对来源清单及当前字节，全部一致；三报告原dirty=true保留，不以该标志改写真实来源关系。

## 正式矩阵与当前门禁

实际执行当前 `tools/evidence/gate.py`，使用固定 `tests/runs/d1.05-matrix.json`、三份正式报告及当前workspace，命令退出0，输出 `D1.05: automated=Passed, gate=Passed`。独立输出在 `evidence/D1.05/independent-final-20260908-8d31/gate-summary.json`；未使用历史accept_gate候选入口，也没有复制主集成者的gate结果。

该入口对三报告分别执行current audit，重新核对当前输入/collector/实际命令程序/构建产物与归档、原始流、固定expected/manifest、精确发现与执行集合、CHECK、复核政策及附件。结果errors=[]、missing=[]，三项自动及单运行包状态均Passed。matrix SHA为 `e8873b724523aa1f9ceedb719793160600d98160882f5d4aacdcea853aec5259`，gate规则SHA为 `8e1526662d21c341baf6ad35cf139b039e6df65dd0e0fb774c7f8d3e3856013f`。

| 配置及原始run_id | CTest | CHECK | 报告SHA-256 |
|---|---:|---:|---|
| Debug / 20260907T235925Z-a07bf46d5760 | 283 | 3 | cd162ff112b27110569381db14bc223748df0ba3b613831c9302afa85cd2acf1 |
| Release / 20260908T001937Z-799b5f40252d | 283 | 3 | e70a92bc34d039e8e85eb75b3d63680406dc4c2bfead934106c9310a2baa7ab4 |
| ASan / 20260908T004118Z-0cdd375d90c8 | 285 | 3 | 8cc0d7e5ec2f3ebbc4b00bc57df033d475e39e6c780be353a10a61a95c01f9e0 |

三个报告均位于 `evidence/a8049df35d74-028ca1c8afab/<profile>/D1.05/<run_id>/report.json`。共851项CTest、9项CHECK，独立读取JUnit确认零失败、禁用和跳过；全部27条根命令实际Exited/0，Job恢复前已被拥有、结束后active_after=0，未依靠强制清理冒充正常退出。三配置实现/依赖身份相同。

## ASan专项及八包装

Debug与Release分别已有独立原始材料报告。本轮直接读取ASan runtime归档：8包装、28条子命令、69个分配样本；逐条核对原始stdout/stderr长度和SHA、精确退出码、预期负编译诊断、Job恢复前拥有及正常排空。机器明细见asan-controls.json。

private_dispatch的正编译成功，五项负编译仅出现对应源码C2248/C2039；运输probe固定标记及退出86真实存在。no_self_wait正编译成功、四项负编译C2039。安装边界中CoreContracts成功，Runtime以退出1和精确未实现标记拒绝；独立消费者四项compute/read/invalid_input/provider_unavailable为true。预期失败没有被算作业务成功。

ASan12个分配入口各观察到恰好一次asan_allocations；原CRT12个入口均0，完整保留missing列表且effective=unobserved_asan_intercepted，未把CRT盲区写成覆盖证明。前8项C++ new为正，malloc家族4项C++为0。独立无分配负窗口所有有效计数为0。真实单次分配注入C++=1、ASan=1、退出1且verified=false；真实hook注册失败以退出87和native_asan_hook_registration_failed标记结束，Job正常排空，不能在注册失败后继续声称计数有效。

40次完整稳态invoke的C++/ASan分配及释放为0，C++驻留与峰值不变。非空Borrowed独立上下文窗口有效通道为0。首用/可变结果实际成功，可变结果正分配；最后Bound释放有实际释放和驻留下降，weak控制块释放至少2块。ASan子命令比另外两配置多1条，来源于真实注册失败控制；三配置共24包装、82条包装子命令、207样本。

## 审核来源、前置与范围

对三份归档的reviewPolicy实际执行evaluate，均errors=[]、approved=True；当前spec/code记录与归档一致，每份记录九个技术证据的实际SHA也匹配。基础SPEC和独立CODE有各自正式来源，Policy/管线/分配/包装/消费者与新增控制按各自范围聚合，没有用历史SPEC替代CODE。43ad局部集成与028ca当前正式运行仍为不同来源，文档末尾LF增量不改变原始执行记录。

D1.03、D1.04前置记录所引用的原gate及各正式report SHA再次匹配；两个原gate均自动/包级Passed、errors/missing为空。该核对保持历史证据，不用当前源码重写历史包结果。

本包仅覆盖内部Native现行运输约束下的真实Read/Compute和candidate普通Read分支、逐调用治理、有界同步观测，以及规划验证消费者。非空资源声明及未支持Provider路径继续按合同拒绝。零分配只覆盖各配置已校准通道、当前线程、参与模块、有限预热与固定小值场景；Release CRT及未启用ASan保持null，非空Borrowed零窗不等于支持Native非空资源执行。上述是明确合同边界，不是未完成项。

本轮没有修改源码、冻结输入、原报告或历史失败。包级技术与自动验收已闭合；进度登记及证据提交由主集成任务继续完成，不是追加技术批准条件。

## 机器核对与既有分配置材料SHA-256

| 文件 | SHA-256 |
|---|---|
| evidence/D1.05/independent-formal-debug-review-20260908.md | 006ef95d0e9ee709663e7b0dc38aaca1dc95a3c6555b7c43931a6d773ee17f84 |
| evidence/D1.05/independent-formal-release-review-20260908.md | 8f984609cb4eb7ddfd0995e9f43171fb51b89203a9b4cdc48278e24013d6f1ea |
| evidence/D1.05/independent-final-20260908-8d31/asan-controls.json | 103ffb793f6298b0f2eff1660af70c8a30330bc58640145bc85abe3f632d3cc5 |
| evidence/D1.05/independent-final-20260908-8d31/decision-binding.json | 4de678b3739b0251f00cb440267c71c84b4837de806031c01cc0b79fab9e93cb |
| evidence/D1.05/independent-final-20260908-8d31/gate-summary.json | f7e77e3bb3895cec8e0567d11b56c0918c4e7e77a477c78e9577db56542155f7 |
| evidence/D1.05/independent-final-20260908-8d31/source-reviews-runs.json | ceff3cc09fabe9f26155fccef4f01ee52ea0a481abc17521c0c4962a75e8a989 |

# D1.05 最终验收准备独立 AI 复核

结论：**准备材料 Approved；实际包级验收 Pending**。当前受审材料没有发现需要修改冻结源码、固定预期或审核记录的缺口。正式 Debug 矩阵正在运行，Release/ASan 尚未提供最终报告；本轮未运行构建或矩阵，不声明 D1.05 Passed。

## 来源与审核聚合

实际读取 Git 对象，逐项重新计算 `a8049df35d7428e79d89df9b0f2f223fa7002bda` 中 255 个文件的 SHA-256，并与 implementation-source.json 的 expected/git_blob_sha256 及当前字节核对，全部一致。当前 HEAD 与本地 upstream 跟踪引用均为该提交；这不代替新的联网远端核验。

三份正式 run manifest 分别经 tools/evidence/common.inputs/digest 重新计算，均得到 255 项输入摘要 `028ca1c8afab6dfb2b5c3c6758ce4b71f13311957cc78bdc2463e6993a265560`。implementation-source.json 的 Passed 只表示 Git 来源核对通过，不是测试或工作包通过。

D1.05-spec.json 与 D1.05-code.json 均绑定该摘要并使用 actor_type=AI，review_kind 分离，明确保留包级 InProgress 和覆盖边界。各自九个 evidence 引用的实际字节 SHA 全部匹配。聚合来源覆盖基础 SPEC、基础独立 CODE、Policy SPEC/CODE、管线 SPEC/CODE、分配/包装/消费者 SPEC/CODE，以及新增借用/Unknown 控制的审核和实测收尾；没有把原基础 SPEC 冒称 CODE。历史管线与分配报告的较早来源通过未变生产字节及后续增量报告衔接，范围排除没有被删除。

独立比较最后43ad局部运行归档和当前255文件，仅 native-invocation-transport.md 相差末尾一个 LF，严格满足 old == current + b'\n'。对应独立格式复核已经聚合。故局部96次执行仍绑定43ad，未被重新描述为028ca上的正式矩阵结果。

## 固定矩阵与审核合同

D1.04的全部case对象和三个CHECK对象原样继承，新增长度为32，名称唯一。三个正式profile的固定预期分别为283、283、285；regex逐项精确匹配各自预期且排除其他profile专用项。矩阵恰有Debug/Release/ASan三项，没有重复或缩减。

当前expected/matrix/run manifest及前置材料摘要全部匹配 D1.05-start-lf.json 的 approved_inputs。旧expected审核中的CRLF摘要经保留的换行归一报告衔接，不需要改写历史SHA。

实际调用 review_policy.evaluate，使用已核验提交的政策Git字节及各manifest输入清单，三份均返回 errors=[]、approved=True。该结果只表示spec/code审核合同齐全。另读accept_gate，最终仍须校验三份原始report、固定矩阵、统一实现身份、Git blob、审核证据及自动测试结果，不能以本轮合同返回true取代门禁。

## 最终验收仍须补齐

- 同一提交、同一028ca输入、同一依赖锁的正式三profile原始report和源码/构建/命令/CTest归档；Debug283、Release283、ASan285及各三CHECK实际通过。
- 对三份完成报告执行独立audit、固定矩阵汇总及自动验收，核对原始流、重复/缺失/跳过、八包装子控制、分配有效通道、进程树清理和review附件身份；不使用局部32项替代正式集合。
- 基于真实结果追加最终AI验收记录及进度。既有前置批准及历史失败保持原始字节，本轮不重新授予历史包批准。

上述项目是等待运行完成的证据，不是已发现的源码缺陷。当前准备结论可以用于继续完成正式矩阵，不能用于提前标记工作包Passed。

## 本轮材料 SHA-256

| 文件 | SHA-256 |
|---|---|
| docs/reviews/D1.05-spec.json | cfa929876813854255207650c17d57c55c7cea3d40864d6b40a0a00821758d15 |
| docs/reviews/D1.05-code.json | 157ad758c2b3dc7aaf834ec5f2fe48bd9e3108da2d92a74a68121c265a65a14a |
| evidence/D1.05/implementation-source.json | b3dda151a1077dc8deed26eb187b23f0998d1c3f6a68e031d92674a059a889d2 |
| tests/manifests/d1.05.expected.json | 37fee8ea9da96b3a553714f49426c85dd88181de96c8df313df57cbb0ba699a1 |
| tests/runs/d1.05-matrix.json | e8873b724523aa1f9ceedb719793160600d98160882f5d4aacdcea853aec5259 |
| tools/evidence/review_policy.py | 35f3628a58d464d27978fbe9eaa58d2944f32eca766c31215e729f9048bbdd40 |
| tools/evidence/accept_gate.py | a33a4efe90b080a64ca6579922eb0ec55816076a018faef1f358096203016d38 |

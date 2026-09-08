# P0 Evidence 快读摘要实施者自审

日期：2026-09-08。此为实施者自审，不是独立 AI Approved，不替代完整 audit 或工作包验收。

仅修改 tools/evidence/run.py 的完成后接入，新增 summary.py、独立 evidence-summary-v1 schema 和 test_summary.py。没有修改正式 evidence-v1 schema、validate.audit、门禁 Passed 语义或 D1.05 既有证据。旧 Logging 候选任务已停止，本次没有继续写 Logging API。

## 行为及边界

build_summary(report_path) 从既有 report、独立 expected 快照、discovered、逐轮实际 JUnit 和原始命令引用提取计数与失败/跳过/缺项/多余；set_digests 是排序后唯一名称集合的 canonical JSON SHA-256，executed 为各轮名称并集，执行次数另行累计，不把并集去重误当调用次数。字段不存在时为 null，显式空集合才为 0。

command_costs 限 top-level-only，记录总条数、configure/build/install 分类次数及耗时、原 role 次数/耗时。cmake --build/--install 参数用于识别嵌在 check role 的直接命令。没有解析包装器内部执行，nested_command_count 恒为 null，不能声称已统计全进程树构建成本。duration_seconds 来自实际开始/结束时间差，缺任一端为 null，不从零推测耗时。

write_summary 在同目录原子替换 summary.json；写盘故障归一 ValueError。run 在原 report 经过既有 audit、保存最终状态之后才生成导航，失败保存 summary-error.json 绑定原报告 hash，CLI 退出非零，原 report 的状态和字节不因导航失败改变。若连错误文件也无法保存，异常使入口非零，不能因此成功。单元夹具实际验证了原报告 Passed 与独立导航失败并存。

read_summary 检查专用 schema、自身内容摘要、每个来源引用的存在/长度/SHA，并与确定性提取结果比较；即使篡改者重算摘要自己的 digest，改计数仍被检出。引用包括 report、manifest、expected、discovery、各轮 JUnit、命令 raw、源/构建/运行/自测/审核归档。不会返回原始 raw 内容，也不执行完整 audit。不解压整个归档；manifest/expected 的已记录原 SHA 与快照字节不同时，仅读取 source.zip 对应两个指定原成员，核对原 SHA 与 JSON 语义，兼容 run.py 的 JSON 重排。这个分支拒绝缺原成员、重复成员、路径逃逸和坏 zip。摘要与同目录引用的哈希提供完整性/陈旧检查，不是对能重写整套来源材料者的认证签名；完整源码、构建及审计仍是正式依据。

摘要没有自己的 automated_status/package_status 判定；report_claims 明确只是原报告声明。JUnit、命令、检查、缺轮次或原报告错误与 Passed 矛盾时拒绝生成，不将失败改成 Passed。缺必需来源拒绝，不能用空值冒充完整摘要。

## 实际验证与保留记录

- p0-summary-red-0a8de36e53d6：最初 8 项因模块不存在，实际退出 1，属于 API 缺失 red。
- p0-summary-red-boundary-7c3b6403a2a8：实际 11 项中 2 failure + 1 error，发现缺 raw、缺必需轮次和损坏 XML 异常边界；修复后 11 项通过。
- p0-summary-red-costs-e5f9cd9317bf：集合摘要/命令成本及写盘故障新增要求，实际 2 error，后修复。
- p0-summary-red-snapshots-3f45ad31b12f：原 manifest/expected 声明 SHA 被忽略的 2 个子断言实际失败，后修复。review.archive 分支原本正确，只加真实结构回归，没有为它虚构 red。
- p0-summary-final-f83a6158266f：13 项摘要测试及受影响既有 34 项 test_runner 工具回归分别实际退出 0。该轮发生在最后的原 SHA 校验增量之前，不冒充最终版本 34 项全量重跑。
- p0-summary-final-snapshots-c2fbfcc0c56e：最终 15 项摘要测试实际退出 0，包含真实 CMake/CTest 采集器、JSON 重排与导航失败不改报告。最终源码四文件快照及摘要保存在该目录。
- p0-summary-compat-98a4a8e0b5f2：对现有 D1.05 Debug/Release/ASan 正式 report 仅调用 build_summary，实际退出 0。提取既有 283/283/285 条执行、正确审核归档引用；各原目录所有文件的 SHA 集合前后相同。没有 write_summary 到旧目录，没有重新执行 D1.05 正式测试。

所有 red/green 均有独立 Windows Job 命令记录、原始 stdout/stderr 长度与 SHA。新增测试中的合成 XML/JSON 明确是单元夹具，不冒称正式运行证据。git diff --check 和四文件逐行尾空白检查通过。

最终范围输入摘要：606e06c66100807ed5a96d31d3bbc9f64976685ef4d82fd483edd06d7d865594。待主任务独立交叉审核；不提交。

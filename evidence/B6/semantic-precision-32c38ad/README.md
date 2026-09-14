# B6 semantic precision v2 证据导航

最终生产来源 `32c38ad54e875a2812a875685421a46074c292ff`。`acceptance.json` 组合现有格式的三配置正式验收与 footprint 完整性结果。`formal-acceptance-02.json` 为最终有效三配置决定，`matrix-reports-02.json` 是其原始报告清单；`reports.json` 为最终十份成本报告。

`formal-acceptance.json` 首次失败原因仅为 AI 审核附件缺少 sha256 字段，原始错误保持；首轮三配置机器本身各 31/31 Passed。补齐附件绑定后，以相同生产来源和固定 31 项重新执行三配置并生成第二份验收，不覆盖首轮报告或拼接来源。所有 footprint 均首次按原数值预算通过，没有提高阈值。

`source-binding.json` 绑定 190 个测量输入，正式矩阵另绑定 648 个输入和源树 SHA；最终校验原始文件与 Git 字节。`pruning/` 保存六个投影的目标图和依赖获取事实。Debug / ASan Embedded 各 480 个完整 Invoke 零分配窗口，Native 新线程为 0，Embedded 结构上界为 3。

这里的 b6v2-*.py 是位于 build 时实际运行的脚本存档，保留当时路径假设。verify-footprint.py / finalize.py 仅组合原始证据，已有输出拒绝覆盖。原始报告中的本机绝对路径与系统 DLL 指纹用于来源追溯，不表示跨机器可执行路径。

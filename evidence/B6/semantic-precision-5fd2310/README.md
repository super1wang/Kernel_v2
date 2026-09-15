# B6 最终线性化收口 5fd2310 正式证据

生产来源：`5fd2310aec5f8a24ac14b948677930e2a7ced7e3`（唯一生产提交 `B6: close pre-start expiry cancellation race`）。最终决定见 acceptance.json。

- matrix-01 为同源 Debug/Release/ASan 各 31/31 的干净会话；matrix-reports.json 指向三份通过报告。
- 正式矩阵首轮曾在 bb7ca66 因 run manifest 构建产物路径截断失败（build_outputs 丢子路径），原文保留在 `evidence/bb7ca66a18e9-cff754095a6f/`；修正 manifest 后 amend 冻结为 5fd2310 并完整重跑，无拼接。
- footprint 首次会话两次瞬时线程观测噪声失败（`native-release-allocation-failed-01.log` 基线退出边界 +1、`native-release-latency-failed-01.log` native 首边界 -1），失败 G1 报告目录与 `commands-attempts-with-failures.json` 原文保留；最终仅选用同源干净单会话 10 项 footprint（commands.json 全零）。
- reports.json 指向全部 Native 7 / Embedded 3 新采样报告；commands.json 保留原方法入口和退出码。sdk 公共摘要与 912cb755 完全一致（git diff sdk/ 为空）。
- source-binding.json 的 190 个方法输入逐字节绑定生产来源。footprint-integrity.json 验证原 11 项预算、24 样本/六组 ABBA、零分配窗口、线程与六个依赖投影。
- formal-acceptance.json 使用既有工具生成，机器错误与 AI review 错误均为零；六份 spec/code review 记录绑定同源 inputs_sha256 `9cd7734e…`。技术审核不代替机器事实。
- 附带 b6f6-*.py 为本次 build 下实际编排脚本副本；原执行位置为仓库 build/，正式工具仍是 tools/evidence 与 tools/footprint。verify-footprint.py 与 finalize.py 在本目录执行，原始失败和旧候选记录未覆盖。

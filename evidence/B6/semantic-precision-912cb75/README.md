# B6 v2 最终来源 912cb75 正式证据

生产来源：912cb7554cd4c78ea9bf6db5615a2875d215e5ed。最终决定见 acceptance.json。

- matrix-02 保留首次 Debug SDK 隔离安装重命名 WinError 5 失败；原报告在对应 source/profile 目录中。
- matrix-03 为新的隔离目录，同源 Debug/Release/ASan 各 31/31。matrix-reports-02.json 与 formal-acceptance-02.json 的文件后缀沿用编排约定，引用的是 matrix-03 三份通过报告；不是拼接旧来源。
- reports.json 指向全部 Native 7 / Embedded 3 新采样报告；commands.json 保留原方法入口和退出码。
- source-binding.json 的 190 个方法输入逐字节绑定生产来源。footprint-integrity.json 验证原 11 项预算、24 样本/六组 ABBA、零分配窗口、线程与六个依赖投影。
- formal-acceptance-02.json 使用既有工具生成，机器错误与 AI review 错误均为零。技术审核不代替机器事实。

附带 b6v2-wait-*.py 是本次 build 下实际编排脚本副本；原执行位置为仓库 build/，正式工具仍是 tools/evidence 与 tools/footprint。verify-footprint.py 和 finalize.py 在本目录执行，原始失败和旧候选记录未覆盖。

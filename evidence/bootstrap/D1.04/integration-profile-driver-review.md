# D1.04 profile 开发驱动增量 AI 审核

审核者review_tools，独立AI。结论：限定diff Approved；未执行整包或宣称三配置结果。

before SHA256 `7de37ff3aab400966f3cffffc7110dc65d3b21e932431938110105018a88b9f3`；after SHA256 `e3a9aaf78855eb1cb197d618d9f864a2a4324fd4e442923025df869f954388d6`，当前build/d1.04-integration.py与after原字节一致，语法解析通过。

逐行diff只有argparse固定debug/release/asan选择、唯一输出目录加入profile、选择对应固定run manifest、binary路径/CTest -C/toolchain文件名取spec.configuration，以及结果记录profile/configuration。configure/build已经来自同一spec，ASan的实际开关继续由既有asan manifest约束；Release/ASan不再误用Debug binary。无自由manifest路径参数。

独立owned进程、原始流、35名字与JUnit精确核对、开始冻结expected、结束完整inputs重算（added/removed/modified）、源码/构建ZIP及失败独立归档均未变。此驱动依旧只执行policy新增35主体，不替代正式251/251/253与三CHECK；实际三配置结果须由后续运行记录另行核对。

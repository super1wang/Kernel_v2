# D0.06-a 独立集成审查

审查者：主集成者（未实施此范围）。范围：dependencies.lock、CMakePresets、cmake/Dependencies与DependencyProbes、prepare.py、probe源文件、THIRD_PARTY_NOTICES、toolchain.md；开发Python锁及verify_python.py。

已核对固定MSVC/C++20/x64/SDK/CRT配置，按组件明确依赖集合、所有上游归档/源码树/许可摘要，SQLite固定含WAL-reset修复版本；未使用浮动latest。归档解包预算、路径/链接边界、重复或篡改缓存拒绝进入实际锁测试。依赖均只创建开发probe目标，不写入SDK导出DAG。Default Embedded仅四项，完整十二项仅All显式取得。

实跑证据含两个独立空目录配置/Debug、Release构建与各15项CTest；ASan Embedded健康及真实堆越界父验证8项。77项构建/报告/故障raw材料的摘要清单已保存。正式全工程仍必须在最终集成源码下重新配置与执行，不拼接前期inputs_changed轮次作为全绿。

Python开发锁独立于生产候选依赖；六个指定wheel摘要逐一匹配已归档官方PyPI元数据，安装字节与wheel逐项比较，并核实真实导入位置。源码检查确认不全局安装、不采纳缺包或额外分发、不信任只报版本的环境。八项锁反例和真实wheel/安装材料篡改拒绝证据存在，最终固定安装通过；正式清单还会重新运行。

审查发现并已修正集成清单中的离线选项拼写：实际选项为OCK_DEPENDENCIES_OFFLINE。此修正不改变a实现；最终configure必须使用真实选项。本次未发现a范围必须修改的实现问题。后续专项仍明确归属D1/D3/D5/D6，当前不声明真实后端Conformance、canonical CBOR或产品footprint通过。

技术结论：a合同/实现可纳入最终G0集成复验；最终自动结果以正式报告为准，人工审批Pending。审查不改写历史退出码或替用户签批准。

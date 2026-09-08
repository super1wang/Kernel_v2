# 开发快跑与当前导航

这些工具只提供L0/L1开发事实，不签发工作包Passed。正式验收继续使用受审run manifest和现有完整机器gate。

```powershell
python -X utf8 tools/dev/verify.py D1.06 --changed --plan
python -X utf8 tools/dev/verify.py D1.06 --paths tools/dev/verify.py tests/tools/dev/test_verify.py
python -X utf8 tools/dev/verify.py D1.06 --changed --risk asan
python -X utf8 tools/dev/verify.py D1.06 --full --reason "具体的公共合同/构建或未知影响风险"
```

默认比较HEAD与暂存/未暂存文件并包含未跟踪文件；用`--base <ref>`包含指定基线后的已提交改动。重命名同时处理旧、新路径。`--paths`适用于已经明确限定的微批次，选择是显式声明，不声称验证了其他工作树修改。`--plan`只输出集合，不执行。

首版映射直接写在verify.py中，使用实际仓库路径和固定历史用例family。已实现的Native、Registry、Policy等受影响消费者可选择其固定集合；D1.06新Host尚未冻结expected，遇到它或其他未知影响时要求当前包全量，而集合缺失将明确失败，不以旧Native测试代替。新增行为时同步审核显式映射及本包expected。

Python工具测试仅运行对应目录，必须非空、零失败、零跳过；C++先成功配置/构建锁定Profile，再逐完整名称执行CTest，核对本轮JUnit完整集合。源码前后变化使结果无效。记录为独立`evidence/bootstrap/D1.06/dev-fast-*`目录，保留实际owned Job命令/退出/流摘要；输出`DevelopmentChecksPassed`明确不是包级Passed。

公共头改动自动补Release/ASan，其他具体寿命、分配或优化风险由`--risk`明确增加。纯Python组没有ASan含义，不将其一次执行复制为多个Profile结果。全量升级原因记录在selection中；构建树按Profile隔离，可增量重建，但测试每次真实执行，不复用旧结论。

Runtime模板/测试头改动至少补Release；共享CoreContracts夹具同时选择Registry、Policy、Native下游运行。每次C++验证先重新configure再build；独立锁定MSVC控制证实该顺序会更新POST_BUILD注册，无须额外强制clean-first。实际cache/toolchain通过既有build_identity核对，并记录构建exe的SHA，之后才执行CTest。该结论限定当前生成器；更换生成器须重验失效规则。

## AI导航

```powershell
python -X utf8 tools/dev/current.py check
python -X utf8 tools/dev/current.py refresh
```

`docs/ai-current.json`是本地生成、被gitignore排除的导航文件；生成器受版本控制。这样导航可在提交后绑定实际HEAD，避免把自身尚未产生的提交SHA写入同一提交。克隆后先按AGENTS读取规范/进度，再生成；不能把缺失文件当作已有摘要。

check重新核对HEAD、分支、当前包状态、规范及进度/合同SHA与固定导航结构，任何变化返回非零及完整规范回退路径。不能仅相信JSON的stale=false。refresh只生成当前引用，不代表已阅读或批准规范；发现stale须完整读取规范及最新进度、复核映射后再刷新。当前生成器只支持D1.06，阶段推进必须更新映射，不能沿用过期的Host未实现描述。

历史Evidence和审核不删除。Evidence summary完成后只改变默认阅读入口；异常、缺项、摘要不符或审核疑点仍展开原始材料，完整机器audit保持执行。

# D1.06 v2 身份入口修复自审

实施者自审；原 v2 自审和失败材料不改写。独立 CODE 发现 require_complete 对缺失或未知 method 跳过 v2 校验，属于真实验证缺口。

本轮最窄强制只接受 `ock.native-footprint/2`，继续校验完整 v2 身份及查询事实。不增加旧 v1 兼容路径，不重算或改写 v1 历史。原基础护栏的合格合成记录补全 v2 身份与十二边界；错误树/缺样反例原职责保留。

- `footprint-v2-identity-red-263478fef16d`：缺失、null、/1、/999 反例实际失败，子进程 exit 1。
- `footprint-v2-identity-green-552bff69828a`：18 项永久护栏实际全通过，owned Exited 0、排空、未终止 Job。
- 同一新实现只读重新校验此前 held 的 none/thread 和 Native 单对的 baseline/native 共四条真实 v2 原始记录；均能生成原口径摘要，来源 commands SHA 记录在 raw-v2-revalidation.json。没有新增 OS 控制或重跑消费者。

三个改动文件的精确 SHA 见 `footprint-v2-identity-fix.json`。本轮不改变采样、生产库、消费者、预算或线程未知结论。三个文件现已冻结，交独立增量复核后继续已批准 allocation 实施。

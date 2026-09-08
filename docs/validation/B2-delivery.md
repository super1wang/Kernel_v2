# B2 交付与自动验收

实现来源：`621d8ea2fe6091b7cb0f76fb7f76bae7793ffaca`；输入摘要：`7ea3e0685e7d0ad40c8f877e2233df02592f2839ea16eb9c088853d7fb88fda2`。

D2.01→D2.02→D2.03/D2.04 已按实际 Passed 前置顺序自动验收。SPEC/CODE 为两类 AI 结论，不是人工批准。正式运行复用每配置根构建，四包各自 expected 和三配置证据独立，未重跑历史 Passed 包。

| 包 | Debug | Release | ASan | 自动验收 |
|---|---:|---:|---:|---|
| D2.01 | 13 | 13 | 14 | [Passed](../../evidence/bootstrap/B2/acceptance-78d39114a1/D2.01.json) |
| D2.02 | 3 | 3 | 3 | [Passed](../../evidence/bootstrap/B2/acceptance-78d39114a1/D2.02.json) |
| D2.03 | 1 | 1 | 1 | [Passed](../../evidence/bootstrap/B2/acceptance-78d39114a1/D2.03.json) |
| D2.04 | 13 | 11 | 11 | [Passed](../../evidence/bootstrap/B2/acceptance-78d39114a1/D2.04.json) |

合计 Debug 30、Release 28、ASan 29。ASan 逃逸样例要求实际 heap-use-after-free 报告；普通进程失败不能算检测成功。安装验证迁移后的 SDK、逐公开头独立编译和 Native 实际链接闭包。

实现包括单 DOM/预算、共享 TypeContract 与 Schema、精确命令卡、OCK1/JSON-RPC、真实 HostBound invoke、mock 观察订阅/get/list 与无状态 cursor。get/list 外部 reserve 不持连接锁，并覆盖等待另一线程关闭的回调。void Plan exports 采用空对象。

边界：复杂 Schema 明确 DynamicOnly，当前 Registrar 仍拒绝任意复杂 Schema 操作注册；不自动生成弱化的 Native 入口。get 仅摘要并声明 full_result_available=false；真实 Task/完整结果属于 D3，真实 IPC 属 B3。Plan endpoint 未安装。G2 未开始。

开发与失败证据保留在 evidence/bootstrap/B2；此前不同来源的成功结果未拼接进入本次验收。源码审核依据和冻结集合见 [B2 正式范围](../reviews/B2-formal-scope.md)。

收口时保留两类失败：Native 安装临时目录移动曾返回 Windows 拒绝访问，同来源重跑通过；首次提交字节核验发现 25 个工作区 CRLF 与 Git LF 差异，确认仅换行后归一，重新执行最终十二份正式运行。未覆盖旧报告，也未将旧来源配置拼入最终验收。

## 正式运行

- [D2.01 / win-msvc-debug](../../evidence/621d8ea2fe60-7ea3e0685e7d/win-msvc-debug/D2.01/20260908T151625Z-505e39f96059/report.json)
- [D2.02 / win-msvc-debug](../../evidence/621d8ea2fe60-7ea3e0685e7d/win-msvc-debug/D2.02/20260908T151656Z-b8767f0421e3/report.json)
- [D2.03 / win-msvc-debug](../../evidence/621d8ea2fe60-7ea3e0685e7d/win-msvc-debug/D2.03/20260908T151703Z-b9e10f00b474/report.json)
- [D2.04 / win-msvc-debug](../../evidence/621d8ea2fe60-7ea3e0685e7d/win-msvc-debug/D2.04/20260908T151709Z-a8ae6712e22b/report.json)
- [D2.01 / win-msvc-release](../../evidence/621d8ea2fe60-7ea3e0685e7d/win-msvc-release/D2.01/20260908T151759Z-f692c317bcf3/report.json)
- [D2.02 / win-msvc-release](../../evidence/621d8ea2fe60-7ea3e0685e7d/win-msvc-release/D2.02/20260908T151830Z-21388ca88096/report.json)
- [D2.03 / win-msvc-release](../../evidence/621d8ea2fe60-7ea3e0685e7d/win-msvc-release/D2.03/20260908T151836Z-4fce23a1a770/report.json)
- [D2.04 / win-msvc-release](../../evidence/621d8ea2fe60-7ea3e0685e7d/win-msvc-release/D2.04/20260908T151842Z-f5327a447398/report.json)
- [D2.01 / win-msvc-asan](../../evidence/621d8ea2fe60-7ea3e0685e7d/win-msvc-asan/D2.01/20260908T151921Z-c36ded9f1747/report.json)
- [D2.02 / win-msvc-asan](../../evidence/621d8ea2fe60-7ea3e0685e7d/win-msvc-asan/D2.02/20260908T151947Z-f1cbe9ef590b/report.json)
- [D2.03 / win-msvc-asan](../../evidence/621d8ea2fe60-7ea3e0685e7d/win-msvc-asan/D2.03/20260908T151954Z-d61eb446a019/report.json)
- [D2.04 / win-msvc-asan](../../evidence/621d8ea2fe60-7ea3e0685e7d/win-msvc-asan/D2.04/20260908T152001Z-dd270ea8b334/report.json)

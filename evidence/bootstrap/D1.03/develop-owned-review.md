# D1.03 develop.py owned进程判定增量独立 AI 代码复核

actor_type：AI；审核者：review_tools。结论：Approved。本增量只批准开发辅助脚本run的成功判定修复；注册器预算行为仍由review_contracts负责，正式矩阵未在本报告中声明运行Passed。

当前develop.py SHA-256：3ef3f80410242e09a1af9824013fec60e805a9ec7a429d7f8e8ff09aff7af0ed。受审源与全部red/green原始材料、未修改process/run/validate工具SHA见develop-owned-review-sha256.json。

## 实际修改与反例

与develop-owned-red/fixture/develop-source.py逐行比较，唯一行为修改是return条件由exit_code==0变为status==Exited且exit_code==0且process_tree.active_after==0。记录命令、复制来源、构建/执行流程均未改动，正式采集器和validator规则未放宽。

red外层命令实际退出1，stderr记录AssertionError ('alive', True, False)。旧run将主进程退出0但后代未自行排空的DescendantsAlive错误判成功，红轮是真实故障，不是手填失败。

green/check.py从当前develop.py提取实际run的AST执行；仅把真实process.execute超时缩短为5秒，没有替换其返回状态为伪造值。alive创建真实owned子进程；clean正常退出；failure实际退出7。三份green存档develop-source.py均与当前源逐字节一致。

| 控制 | 实际内层结果 | run判定 | 断言 |
|---|---|---|---|
| alive | DescendantsAlive，主进程exit_code0，owned Job清理后active_after0，terminated_owned_job=true | false | 成功 |
| clean | Exited，exit_code0，active_after0，未终止Job | true | 成功 |
| failure | Exited，exit_code7，active_after0 | false | 成功 |

alive的active_after0是工具清理残留后的事实，不能据此称后代正常排空；新status条件正确拒绝。三个外层实际断言进程均Exited0且active_after0。没有按名称杀外部进程，也没有把DescendantsAlive改写成Exited。

## 来源冻结

独立调用tools.evidence.common.inputs及digest重新计算：Debug/Release/ASan均204项，共同最终输入摘要75090b8927158e4476b812c504be76a83144327ad4aaee5f20acfc63b3aa496b。此前final-tools-review.md中的旧摘要仅为当时快照，不改写；本增量报告记录修复后的新摘要。

当前没有剩余开发工具成功判定必改项。既有实际Exited0且active_after0的绿色原生记录不因这项收紧失效；此前工具/归档结论在原范围内继续成立。主任务可结合注册器独立代码审核冻结spec/code、提交真实来源，再运行固定216/216/218及每配置三CHECK。所有旧报告和失败轮保持原字节，本轮不修改实现、不提交。

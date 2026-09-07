# D1.04 待运行集成驱动独立 AI 静态复核

actor_type：AI；review_tools。结论：ChangesRequested，1项来源完整性检查需修正。本次仅静态预审，未启动驱动、配置或构建；不声明35主体或整包Passed。

受审SHA-256：

- build/d1.04-integration.py：569b4c3cc5ef398e15e41fcfe8048f42119f0f8a95a6ed56c5def129f40badbb。
- authorization/discover.py：5a4a594fa6654cf7e3dbcd7670aa88c91d062d0b73c61b10bb3736734b7cb2f8。
- authorization/verify_children.py：6817cc0f140fa4cea8920314e2248b3b66e0b27b275eadd54609a8a96764caff。

## P2：来源稳定检查漏新增输入

驱动末尾changed仅遍历初始rows，检查已有文件是否仍存在/散列是否相同。若运行期间packages/**或tests/**下新增会被编译或被扫描的文件，初始清单没有该路径，这段逻辑完全看不到变化，仍可能result.status=Passed；source-inputs.zip却未包含新增来源。当前工程仍在实现时尤其需要拒绝这种不一致。

修改建议：结束时再次执行inputs(ROOT,spec,spec_path)，与初始rows完整集合及每项size/SHA比较；至少记录added/removed/changed并设置SourceChanged。现有初始文件字节核验继续保留。固定35项也宜从开始已冻结expected内容读取，避免中途重新取当前expected作为预期；整集合稳定检查仍不可省略。无需放宽任何原始来源检查。

## 已可接受的部分

- 输出目录integration-UUID用exist_ok=False，构建目录独立UUID；初始来源JSON、ZIP及实际driver.py副本保留，不覆盖历史回合。source ZIP写入前逐项核对size/SHA。
- 每命令使用既有owned execute，记录完整两原始流SHA；需status=Exited、exit_code0、active_after0。DescendantsAlive/Timeout不能因主进程exit0被误判成功。
- 真实native --list和固定policy expected排序后精确比较，要求35项；CTest -R仅policy组且no-tests=error；junit_cases拒绝重复/无效/计数矛盾，之后再次精确核对全部35名字和Passed。不是只看CTest退出0。
- try/finally无论configure/build/native/CTest哪个失败都尝试归档authorization子树，result默认Failed。该子树包含五wrapper的命令、源、安装、编译产物及发现配置；root cache/toolchain/targetgraph/CTest文件另行补入。ZIP写入后逐项重读size/SHA，支持后续原始子命令审核。
- result在归档前计算，但最终JSON在archive_materials成功之后才保存；归档异常不会把未完成归档伪装成已保存的成功报告。若归档异常需另保留外层命令失败及原目录，不覆盖重跑。

## 仍须实际集成后核对

该bootstrap驱动不自行判定五wrapper24子命令或minimum，只完整归档子树；这足以提供后续审查材料，不能取代正式manifest的minimum审核。实际回合需检查commands raw引用完整、8编译反例与4positive诊断、安装CoreContracts/Runtime实际结果，以及生成产物和ZIP集合一一相符。

API修订4仅澄清target候选可见性必须存在共同完整授权tuple，未改变当前四组compile controls所用签名；discover固定5wrapper映射无需改名或增主体。修订4行为须由target_issued_identity/scope_four_way等对应native控制证明，当前仅接口编译不能替代该行为。仍在改动的core必须先稳定后运行，来源P2修复后再做增量预审。

本轮只新增此报告，未修改源码或启动完整集成。

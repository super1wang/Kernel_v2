# D1.04 最终自动验收证据独立 AI 复核

审核者：review_tools，actor_type=AI。结论：**Approved；D1.04 固定三配置自动验收实际 Passed。** 本结论限D1.04内存授权Policy及规划验证消费者；G1仍InProgress，不延伸至后续Invocation、Host、生产传输或其他产品模块。

## 实际矩阵与完整性

本次再次调用当前未修改tools.evidence.validate.audit，对三份正式报告逐一执行当前工作树完整核验，均package=Passed、errors=[]。实际JUnit及CHECK记录独立解析，结果如下：

| 正式配置 | 实际CTest | 实际CHECK | 报告SHA256 |
| --- | --- | --- | --- |
| win-msvc-debug | 251/251 | 3/3 | 44e16d1f26dbf890769f4cd83197b5b53a45cc0c0d504a21c4a5493491c50029 |
| win-msvc-release | 251/251 | 3/3 | 8667c58e316f049ff0ec1e260abae1c820fa7dafd32c306dc80ab12e7491c886 |
| win-msvc-asan | 253/253 | 3/3 | 23d1a7fd0f5293618f7dd8b533a286f1f931bb7c8ad08582c75a3da31edf9085 |

合计755次CTest、9项CHECK全部Passed，固定矩阵没有减少或替换profile。每轮包含35个policy主体、5个policy wrapper、24条真实子命令和48份原始流；三轮共105次policy主体、15个wrapper实例、72条子命令、144份raw。预期负编译与安装Runtime拒绝保留真实非0，不能混作普通成功退出。

完整audit覆盖固定expected、实际发现和JUnit、重复/遗漏、原始命令状态、来源与build/runtime ZIP、最低归档约束、当前工作树来源和审核政策绑定。runtime归档Debug1583项、Release1583项、ASan1586项；另对policy子命令逐条验证WindowsJobObject在启动前归属、active_after=0和raw SHA。没有重跑SDK、CTest或编译，没有修改实现或旧报告。

实际工具链均MSVC19.44.35228.0、v143 version14.44.35207、SDK10.0.26100.0。Release配置/CRT相符；ASan正式报告配置Debug、asan_requested=ON，并完成253个固定实际测试。先前提交前ASan运行库搜索路径失败保留在integration-asan-bc3cce35dc7a，配对控制及独立修复审核已归档；不抹去失败，也不让旧开发驱动失败替代当前正式Passed事实。

## 来源与Git字节

三份报告的commit均为 `8978b277850beb98ab9da16838052d6f32a7a4c4`，全部224个input对象逐项一致，统一摘要 `1a044ad0c5b50f1efa53a82125430a7002a61a1fbbd989f8f1ca2e1e50ac8352`。当前完整audit再次核对工作树与归档来源，不仅比较一个自报摘要。

source-provenance-8978b27.json及gate目录副本记录224个Git blob。Debug独立审核已实际重新执行只读git cat-file --batch，从指定commit读取全部224个blob并逐个核对原始长度、SHA256和blob ID。最终三报告的source集合精确相同，因此该逐字节证明适用于三轮。报告dirty=true保持原样，不冒称工作树clean；受审输入与Git提交一致由直接字节证明支持。

## 审核身份与gate

spec/code技术审核记录均actor_type=AI、Approved，绑定同一最终输入摘要；review archive及所引证据SHA通过完整audit。新增observation测试头由本代理实施，独立审核由review_contracts完成；本报告不把自身实现说明包装为独立代码复核，也不称human Approved。政策automatic-acceptance-policy.json规定AI自我复核及自动验收，无人工逐节点放行。

主任务gate目录current-20260907T203550Z的命令实际Exited/0、active_after=0。gate-summary.json SHA为 `4f1e6b7131e75fbc4a78d7b97a8ff26c23c7e9cc08af7b8bc1bdbe01c0d77e13`，automated_status和gate_status均Passed，missing/errors均空。

本代理独立调用当前未修改gate.summarize，对固定tests/runs/d1.04-matrix.json和同三份正式报告重新执行完整audit汇总；结果Passed，除generated_at外所有字段与主任务gate逐项一致，保存final-independent-gate-audit.json。未修改门禁规则、减少矩阵或借历史批准跳过检查。gate旧固定note含“人工放行”措辞不是本次审核身份；实际review记录和UTF-8 acceptance-context.json明确AI模式、human_review_required=false。控制台中文编码显示问题不改变已核验UTF-8 JSON字节，原日志保持不变。

## 证据绑定

| 材料 | SHA256 |
| --- | --- |
| evidence/D1.04/final-three-report-audit.json | ab77125fe2dd15eba8a63e14e5e8e5c815a344506296a4bb40b657d213c38fb3 |
| evidence/D1.04/final-independent-gate-audit.json | 7022f553357ad1c4c9fce6fe82881a34f781fafe2eef2883ba5e776d74c5a436 |
| evidence/D1.04/source-provenance-8978b27.json | 0a82877111be717e6d1f8a18da814ade4065bc72b4532f10764c14cfb9ecaaaa |
| evidence/D1.04/current-20260907T203550Z/source-provenance.json | 0a82877111be717e6d1f8a18da814ade4065bc72b4532f10764c14cfb9ecaaaa |
| evidence/D1.04/current-20260907T203550Z/command.json | 9745b75a047c0b51b12906c6a4fe032175e4250183ca48e5c7ce348bf18bdd41 |
| evidence/D1.04/current-20260907T203550Z/acceptance-context.json | f9abde57b3862c6f6bf1dc178527c825390632c443a4af920cb551e772a83c94 |
| tests/runs/d1.04-matrix.json | 3c58c8bcc67999054374c06bd03e89a759759cd0b458df4210d4113b872cc506 |
| tools/evidence/gate.py | 8e1526662d21c341baf6ad35cf139b039e6df65dd0e0fb774c7f8d3e3856013f |
| docs/reviews/automatic-acceptance-policy.json | 712ad1e661300f538cacac5af1d5111fdf6418624369f02a900e992610786d80 |
| evidence/bootstrap/D1.04/final-contracts-fixes-review.md | d95aa99302c2426b99edfe2e0b1179c8ac4a0500f051eb0407036b5016ea7a41 |
| evidence/bootstrap/D1.04/final-tools-fixes-review.md | 5541e2d55654d57472ae3c85ba4dbbcd1bbe38f9f6da3be29e362a0f232f59f6 |

本报告新增最终独立意见，不覆写Debug/Release单配置结论或任何历史失败。D1.04可按已授权流程同步进度并提交验收证据；不把本报告扩展为后续工作包准入已经完成。

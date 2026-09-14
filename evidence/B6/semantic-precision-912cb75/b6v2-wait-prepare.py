from pathlib import Path
import json,sys,subprocess,hashlib
from datetime import datetime,timezone
R=Path(__file__).resolve().parents[1];sys.path.insert(0,str(R))
from tools.evidence.common import inputs,digest,sha_file
source=subprocess.check_output(['git','rev-parse','HEAD'],cwd=R,text=True).strip();assert source.startswith('912cb75')
O=R/'evidence/B6/semantic-precision-912cb75';O.mkdir(parents=True,exist_ok=True)
review=R/'docs/reviews/B6-semantic-v2-wait-review.md'
base=(R/'docs/reviews/B6-semantic-v2-review.md').read_text(encoding='utf-8')
base=base.replace('32c38ad54e875a2812a875685421a46074c292ff',source)
base=base.replace('冻结前 30 项 Debug 直接集通过；','前候选 30 项 Debug 直接集通过；本候选新增两条 Host expiry 直接反例通过；')
base+='\n## F5 资源等待补充复核\n\n32c38ad 遗漏的 Host 未启动 expiry 反例已确认失败。已检查 ExecutionService deadline → ManagedControl → ManagedInvocation → InvocationRecord 全链路：State expiry 通过 Scheduler retire(ExpiredBeforeDispatch) 只退役未启动工作，不设置 owning cancel 请求、不请求 stop。已进入 State 保留现有 Action deadline 和 commit gate；Host 实钟测试证明 member one 已进入后过期，member two 不进入且 revision 不增。Read 仍按既有 cancel 路径。真正 Host cancel、原 stop callback、父取消均继续调用真实 cancel 入口。\n\ncomplete_before_start 的 State 分类只接受 owner cancel_requested 在互斥锁下读取的真实请求，移除 State ErrorCode 兜底。Scheduler retire 与 start 原互斥仲裁保持；不直接释放已交给业务的 lease。仅增加私有控制方法，未修改公开 CoreContracts/Outcome/State provider 合同，未改变 verifier 或预算。\n\n真实时钟与 Scheduler 一致，避免前序 Policy 假时钟快进导致 handler 未进入。等待中 expiry 与已进入 expiry 均 Passed，业务前/后分别 false/true，Accepted 均 true、NotReached、no_application_proven。原取消用例仍 CancelWon。新候选统一重验原 31 项三配置与全部原 footprint，不复用旧来源机器结果。\n'
review.write_text(base,encoding='utf-8')
identities=[]
for p in ('debug','release','asan'):
 path=f'tests/runs/b6-semantic-v2-win-msvc-{p}.json';spec=json.loads((R/path).read_text());rows=inputs(R,spec,path);d=digest(rows);identities.append(d)
 for kind in ('spec','code'):
  prior=json.loads((R/f'docs/reviews/B6-semantic-v2-win-msvc-{p}-{kind}.json').read_text())
  prior.update(reviewed_commit=source,reviewed_inputs_sha256=d,reviewed_at=datetime.now(timezone.utc).isoformat(),reviewer='Codex AI B6 semantic precision v2 expiry closure',approval_text='已复核 F1-F5 及 State Host 等待和已进入 expiry 完整完成路由；规格与代码 Approved。机器验收独立，新来源全部门禁通过前 B7 HOLD。',evidence=[{'path':review.relative_to(R).as_posix(),'sha256':sha_file(review)}])
  (R/f'docs/reviews/B6-semantic-v2-wait-win-msvc-{p}-{kind}.json').write_text(json.dumps(prior,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
assert len(set(identities))==1
(O/'formal-source.json').write_text(json.dumps({'source_commit':source,'input_count':len(rows),'inputs_sha256':d},indent=2)+'\n')
for name in ('formal-02','footprint-prepare','footprint-run'):
 s=(R/f'build/b6v2-{name}.py').read_text(encoding='utf-8').replace('semantic-precision-32c38ad','semantic-precision-912cb75').replace('32c38ad54e875a2812a875685421a46074c292ff',source).replace('B6-semantic-v2-footprint-budget.md','B6-semantic-v2-wait-footprint-budget.md')
 (R/f'build/b6v2-wait-{name}.py').write_text(s,encoding='utf-8')
print(source,len(rows),d)

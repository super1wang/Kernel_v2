"""D0.04-a 先行反例；确定性合同模型，不执行业务操作。"""
from copy import deepcopy
import json
from pathlib import Path
import unittest
from plan_model import ContractError, PlanChecker, check_schema, pointer, apply_bindings, Meter, evaluate
ROOT=Path(__file__).resolve().parents[2]
def branch(**kw):
    kw['else']=kw.pop('else_');return kw
def base(steps=None,exports=None):
    return dict(format='ock.plan/1',durability='volatile',on_error='stop',inputs={},steps=steps or [],exports=exports or {})
def let(out='v',value=1,id='l'):
    return dict(kind='let',id=id,value={'literal':value},out=out)
def ref(slot,path=''):return dict(slot=slot,path=path)
def call(delivery='complete',out='r',name='math.compute',id='c'):
    return dict(kind='call',id=id,op=dict(name=name,version='1.0.0'),args={'value':2},delivery=delivery,out=out)
class PlanContracts(unittest.TestCase):
    def test_T04_plan_golden_nodes(self):
        rows=json.loads((ROOT/'tests/plan/golden/nodes.json').read_text())
        self.assertEqual({r['node'] for r in rows},{'call','atomic','await','let','assert','if','foreach','parallel'})
        for row in rows:
            with self.subTest(row=row['name']):
                if row['valid']:PlanChecker().check(row['plan'])
                else:
                    with self.assertRaises(ContractError):PlanChecker().check(row['plan'])
    def test_T04_plan_wire_rejects_return_and_unknown(self):
        for p in [base([dict(kind='return',id='bad',value=1)]),dict(base(),budget=99999),base([dict(let(),surprise=True)])]:
            with self.assertRaises(ContractError):check_schema(p)
    def test_T04_plan_pointer_missing_null_and_escapes(self):
        obj={'a/b':{'~key':[None,7]}}
        self.assertIsNone(pointer(obj,'/a~1b/~0key/0'))
        self.assertEqual(pointer(obj,''),obj)
        for path in ['/a~2b','/a~1b/~0key/01','/a~1b/~0key/-','/a~1b/~0key/-1','/a~1b/~0key/2','#/a']:
            with self.assertRaises(ContractError):pointer(obj,path)
        with self.assertRaises(ContractError):pointer({'x':None},'/absent')
    def test_T04_plan_binding_overlap_and_constants(self):
        node=call();node['args']={'box':{}}
        for bindings in [[{'to':'/args/box','value':ref('a')},{'to':'/args/box/v','value':ref('b')}],[{'to':'/args/x','value':ref('a')},{'to':'/args/x','value':ref('b')}],[{'to':'/op/version','value':ref('a')}]]:
            with self.assertRaises(ContractError):apply_bindings(node,bindings,lambda r:1)
        with self.assertRaises(ContractError):apply_bindings(call(),[{'to':'/args/value','value':ref('a')}],lambda r:3)
        node['args']['box']['v']=None
        with self.assertRaises(ContractError):apply_bindings(node,[{'to':'/args/box/v','value':ref('a')}],lambda r:3)
    def test_T04_plan_binding_requires_parent_and_validates_whole(self):
        node=call();node['args']={}
        bound=apply_bindings(node,[{'to':'/args/value','value':ref('a')}],lambda r:4)
        self.assertEqual(bound['args'],{'value':4})
        with self.assertRaises(ContractError):apply_bindings(node,[{'to':'/args/missing/value','value':ref('a')}],lambda r:4)
        p=base([let('v','wrong'),dict(node,bindings=[{'to':'/args/value','value':ref('v')}])])
        with self.assertRaises(ContractError):PlanChecker().check(p)
    def test_T11_plan_scope_single_assignment_and_forward(self):
        for p in [base([let(),let(id='other')]),base([let(id='same'),let(out='other',id='same')]),base([dict(let(),value=ref('later')),let('later',2,'later')]),base([let('$input')])]:
            with self.assertRaises(ContractError):PlanChecker().check(p)
        shadow=branch(kind='if',id='i',predicate={'literal':True},then={'steps':[let()], 'exports':{}},else_={'steps':[],'exports':{}},out='choice')
        with self.assertRaises(ContractError):PlanChecker().check(base([let(),shadow]))
    def test_T11_plan_exports_and_branch_types(self):
        with self.assertRaises(ContractError):PlanChecker().check(base([let()],{'missing':ref('no')}))
        node=branch(kind='if',id='i',predicate={'literal':True},then={'steps':[let()], 'exports':{'x':ref('v')}},else_={'steps':[let(value='text')],'exports':{'x':ref('v')}},out='r')
        with self.assertRaises(ContractError):PlanChecker().check(base([node]))
        node['else']['steps']=[let()];PlanChecker().check(base([node]))
        node['else']['exports']={}
        with self.assertRaises(ContractError):PlanChecker().check(base([node]))
    def test_T11_plan_ticket_is_typed_and_consumed(self):
        await_node=dict(kind='await',id='a',task=ref('task'),out='r')
        PlanChecker().check(base([call('ticket','task'),await_node]))
        for p in [base([let('task',{'execution_id':'same'}),await_node]),base([call('ticket','task')]),base([call('ticket','task')],{'ticket':ref('task')})]:
            with self.assertRaises(ContractError):PlanChecker().check(p)
        checker=PlanChecker(allow_detached_exports=True)
        self.assertEqual(checker.check(base([call('ticket','task')],{'ticket':ref('task')}))['exports']['ticket']['kind'],'ExecutionRef')
    def test_T10_plan_atomic_result_and_dynamic_check(self):
        p=json.loads((ROOT/'examples/plans/settings-atomic.plan.json').read_text())
        result=PlanChecker().check(p)
        self.assertEqual(result['exports']['edit']['kind'],'AtomicResult')
        dynamic=deepcopy(p);dynamic['inputs']={};dynamic['steps'].insert(0,dict(kind='call',id='domain',op={'name':'settings.locate','version':'1.0.0'},args={},delivery='complete',out='location'))
        dynamic['steps'][1]['bindings']=[{'to':'/domain','value':ref('location','/domain')},{'to':'/preconditions/revision','value':ref('location','/revision')}]
        self.assertIn('edit:DynamicCheckRequired',PlanChecker().check(dynamic)['pending'])
    def test_T10_plan_atomic_rejects_effect_and_ticket(self):
        p=json.loads((ROOT/'examples/plans/settings-atomic.plan.json').read_text())
        for name,delivery in [('effect.send','complete'),('settings.set_limit','ticket')]:
            bad=deepcopy(p);bad['steps'][0]['steps'][0]['op']['name']=name;bad['steps'][0]['steps'][0]['delivery']=delivery
            with self.assertRaises(ContractError):PlanChecker().check(bad)
        bad=deepcopy(p);bad['steps'][0]['bindings']=bad['steps'][0]['bindings'][:1]
        with self.assertRaises(ContractError):PlanChecker().check(bad)
    def test_T11_plan_expression_types_and_shortcircuit(self):
        self.assertFalse(evaluate({'operator':'and','arguments':[{'literal':False},{'operator':'exists','arguments':[ref('declared','/missing')]}]}, {'declared':{}}))
        self.assertFalse(evaluate({'operator':'exists','arguments':[ref('declared','/missing')]},{'declared':{}}))
        for expr in [{'operator':'eq','arguments':[{'literal':1},{'literal':True}]},{'operator':'lt','arguments':[{'literal':1},{'literal':1.0}]},{'operator':'not','arguments':[{'literal':3}]}]:
            with self.assertRaises(ContractError):evaluate(expr,{})
        with self.assertRaises(ContractError):evaluate({'operator':'exists','arguments':[ref('undeclared')]},{})
    def test_T11_plan_foreach_fixed_limit(self):
        node=dict(kind='foreach',id='f',collection=ref('$input','/values'),item_slot='item',index_slot='idx',max_iterations=2,body={'steps':[],'exports':{'x':ref('item')}},out='r')
        p=dict(base([node]),inputs={'values':[1,2]});PlanChecker().check(p)
        p['inputs']['values'].append(3)
        with self.assertRaises(ContractError):PlanChecker().check(p)
    def test_T11_plan_parallel_order_and_policy(self):
        node=dict(kind='parallel',id='p',branches=[{'steps':[let(value=i)],'exports':{'x':ref('v')}} for i in (1,2)],max_concurrency=2,on_error='stop_launch_and_drain',out='r')
        result=PlanChecker().check(base([node],{'r':ref('r')}));self.assertEqual(len(result['exports']['r']['branches']),2)
        for change in [dict(on_error='continue'),dict(max_concurrency=17),dict(branches=node['branches']*17)]:
            with self.assertRaises(ContractError):PlanChecker().check(base([dict(node,**change)]))
    def test_T22_plan_static_and_total_budget(self):
        with self.assertRaises(ContractError):PlanChecker().check(base([let('v'+str(i),i,'l'+str(i)) for i in range(1001)]))
        m=Meter();m.consume(10000)
        with self.assertRaises(ContractError):m.consume(1)
        for charge in ['slot_bytes','result_bytes','control_depth','deadline_ms']:
            meter=Meter(**{charge:2});meter.consume(0,**{charge:2})
            with self.assertRaises(ContractError):meter.consume(0,**{charge:1})
        inner={'steps':[let('v'+str(i),i,'l'+str(i)) for i in range(100)],'exports':{}}
        p=dict(base([dict(kind='foreach',id='f',collection=ref('$input','/items'),item_slot='item',index_slot='ix',max_iterations=101,body=inner,out='rows')]),inputs={'items':[1]*101})
        with self.assertRaises(ContractError):PlanChecker().check(p)
    def test_T04_plan_examples_and_exact_operation(self):
        for name in ['settings-atomic.plan.json','compute-apply.plan.json']:
            PlanChecker().check(json.loads((ROOT/'examples/plans'/name).read_text()))
        bad=call();bad['op']['version']='latest'
        with self.assertRaises(ContractError):PlanChecker().check(base([bad]))
        bad['op']['version']='9.0.0'
        with self.assertRaises(ContractError):PlanChecker().check(base([bad]))

    def test_T10_plan_atomic_subset_and_partial_domain_bindings(self):
        p=json.loads((ROOT/'examples/plans/settings-atomic.plan.json').read_text())
        a=p['steps'][0];a['domain']={'provider':'settings'};a['bindings'][0]={'to':'/domain/id','value':ref('$input','/domain/id')}
        PlanChecker().check(p)
        for kind in ['await','if','foreach','parallel','atomic']:
            bad=deepcopy(p);bad['steps'][0]['steps']=[{'kind':kind,'id':'forbidden'}]
            with self.assertRaises(ContractError):PlanChecker().check(bad)
        bad=deepcopy(p);bad['steps'][0]['steps'][0]['target']={'provider':'settings','id':'other'}
        with self.assertRaises(ContractError):PlanChecker().check(bad)
    def test_T11_plan_dateref_and_branch_ticket_lifetime(self):
        p=base([dict(kind='call',id='d',op={'name':'asset.preview','version':'1.0.0'},args={},delivery='complete',out='data')],{'data':ref('data')})
        self.assertEqual(PlanChecker().check(p)['exports']['data']['kind'],'DataRef')
        p['steps'].append(dict(kind='await',id='a',task=ref('data'),out='r'))
        with self.assertRaises(ContractError):PlanChecker().check(p)
        ticket=call('ticket','task')
        conditional=branch(kind='if',id='i',predicate={'literal':True},then={'steps':[dict(kind='await',id='a',task=ref('task'),out='r')],'exports':{}},else_={'steps':[],'exports':{}},out='choice')
        with self.assertRaises(ContractError):PlanChecker().check(base([ticket,conditional]))
    def test_T04_plan_expression_static_types_not_hidden_by_shortcircuit(self):
        p=base([dict(kind='let',id='l',out='v',value={'operator':'and','arguments':[{'literal':False},{'literal':7}]})])
        with self.assertRaises(ContractError):PlanChecker().check(p)
        p['steps'][0]['value']={'operator':'and','arguments':[{'literal':False},ref('$input','/missing')]}
        with self.assertRaises(ContractError):PlanChecker().check(p)
    def test_T22_plan_payload_and_control_depth(self):
        p=base([let(value=float('nan'))])
        with self.assertRaises(ContractError):PlanChecker().check(p)
        node=let()
        for i in range(17):node=branch(kind='if',id='i'+str(i),predicate={'literal':True},then={'steps':[node],'exports':{}},else_={'steps':[],'exports':{}},out='o'+str(i))
        with self.assertRaises(ContractError):PlanChecker().check(base([node]))


    def test_T22_plan_loop_control_cost(self):
        node=dict(kind='foreach',id='f',collection=ref('$input','/items'),item_slot='item',index_slot='index',max_iterations=10000,body={'steps':[],'exports':{}},out='rows')
        with self.assertRaises(ContractError):PlanChecker().check(dict(base([node]),inputs={'items':[1]*10000}))


    def test_T22_plan_payload_component_budgets(self):
        for payload in [{'text':'x'*(2*1024*1024+1)},{'values':[None]*100001}]:
            with self.assertRaises(ContractError):check_schema(dict(base(),inputs=payload))

if __name__=='__main__':unittest.main()



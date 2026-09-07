"""D0.04 Plan 合同参考检查器；静态样本类型/预算，不是真实 PlanCompiler 或执行器。"""
from copy import deepcopy
from dataclasses import dataclass
import json
import math
from pathlib import Path
import re
import sys
from jsonschema import Draft202012Validator
ROOT=Path(__file__).resolve().parents[2]
SCHEMA=json.loads((ROOT/'schemas/plan-v1.schema.json').read_text(encoding='utf-8'))
Draft202012Validator.check_schema(SCHEMA)
VALIDATOR=Draft202012Validator(SCHEMA)
MAX_U64=2**64-1
class ContractError(ValueError):pass
def require(ok,reason):
    if not ok:raise ContractError(reason)
def check_schema(document):
    node_count=0;text_bytes=0;owned_bytes=0;seen=set()
    def bounded(value,depth=0):
        nonlocal node_count,text_bytes,owned_bytes
        node_count+=1;require(node_count<=100000,'PayloadNodesExceeded')
        require(depth<=64,'PayloadDepthExceeded')
        if id(value) not in seen:seen.add(id(value));owned_bytes+=sys.getsizeof(value)
        require(owned_bytes<=32*1024*1024,'PayloadAllocationExceeded')
        if isinstance(value,str):text_bytes+=len(value.encode('utf-8'));require(text_bytes<=2*1024*1024,'PayloadTextExceeded')
        if isinstance(value,float):require(math.isfinite(value),'NonFinitePayload')
        if isinstance(value,dict):
            for k,v in value.items():
                require(type(k) is str,'PayloadKeyType');bounded(k,depth+1);bounded(v,depth+1)
        elif isinstance(value,list):
            for v in value:bounded(v,depth+1)
    bounded(document)
    encoded=json.dumps(document,allow_nan=False,ensure_ascii=False).encode('utf-8')
    require(len(encoded)<=4*1024*1024,'PayloadFrameExceeded')
    error=next(VALIDATOR.iter_errors(document),None)
    require(error is None,'Schema: '+(error.message[:200] if error else ''))
def tokens(path):
    require(type(path) is str and (path=='' or path.startswith('/')),'PointerSyntax')
    result=[]
    for part in path.split('/')[1:]:
        require(re.search(r'~(?![01])',part) is None,'PointerEscape')
        result.append(part.replace('~1','/').replace('~0','~'))
    return result
def pointer(value,path):
    for part in tokens(path):
        if isinstance(value,dict):require(part in value,'Missing: '+path);value=value[part]
        elif isinstance(value,list):
            require(re.fullmatch(r'0|[1-9][0-9]*',part) is not None,'ArrayIndex: '+path)
            require(int(part)<len(value),'Missing: '+path);value=value[int(part)]
        else:raise ContractError('PathType: '+path)
    return value
def apply_bindings(node,bindings,resolve):
    result=deepcopy(node);paths=[]
    for binding in bindings:
        parts=tokens(binding['to']);require(parts,'BindingRoot')
        if node['kind']=='call':require((parts[0]=='args' and len(parts)>1) or parts[0]=='target','ForbiddenBinding')
        else:require(parts[0]=='domain' or parts==['preconditions','revision'],'ForbiddenBinding')
        for old in paths:require(not (parts[:len(old)]==old or old[:len(parts)]==parts),'BindingOverlap')
        paths.append(parts)
    for binding,parts in zip(bindings,paths):
        parent=result
        for part in parts[:-1]:
            require(isinstance(parent,dict) and part in parent,'BindingParentMissing')
            parent=parent[part]
        require(isinstance(parent,dict),'BindingParentType')
        require(parts[-1] not in parent,'BindingConstantConflict')
        parent[parts[-1]]=deepcopy(resolve(binding['value']))
    return result
@dataclass
class Value:
    data:object
    kind:str='BusinessResult'
    dynamic:bool=False
    detail:object=None

def type_shape(value):
    if isinstance(value,Value):
        if value.kind=='ExecutionRef':return {'kind':value.kind,'result':type_shape(value.detail)}
        if value.kind=='AtomicResult':return {'kind':value.kind,'values':{k:type_shape(v) for k,v in value.detail.items()},'commit':'CommitReceipt'}
        if value.kind=='BlockExports':return {'kind':value.kind,'fields':{k:type_shape(v) for k,v in value.detail.items()}}
        if value.kind=='BranchResults':return {'kind':value.kind,'branches':[type_shape(v) for v in value.detail]}
        if value.kind=='LoopResults':return {'kind':value.kind,'items':type_shape(value.detail)}
        return {'kind':value.kind,'type':type_shape(value.data)}
    if isinstance(value,dict):return {k:type_shape(v) for k,v in value.items()}
    if isinstance(value,list):
        kinds=[type_shape(v) for v in value]
        require(not kinds or all(x==kinds[0] for x in kinds),'HeterogeneousCollection')
        return ['empty' if not kinds else kinds[0]]
    return {type(None):'null',bool:'boolean',int:'integer',float:'number',str:'string'}[type(value)]
def ref_value(ref,slots):
    require(ref['slot'] in slots,'UndeclaredSlot: '+ref['slot']);value=slots[ref['slot']]
    if ref['path']=='':return value
    if value.kind=='ExecutionRef':raise ContractError('ExecutionRefPathIsOpaque')
    if value.kind=='AtomicResult':
        parts=tokens(ref['path'])
        if parts==['commit']:return Value({'commit_id':'0'*32},'CommitReceipt',True)
        require(parts and parts[0]=='values','AtomicResultPath')
        nested=Value({k:v.data for k,v in value.detail.items()},'BlockExports',value.dynamic,value.detail)
        if len(parts)==1:return nested
        return ref_value({'slot':'v','path':'/'+ '/'.join(p.replace('~','~0').replace('/','~1') for p in parts[1:])},{'v':nested})
    if value.kind=='BlockExports':
        parts=tokens(ref['path']);require(parts[0] in value.detail,'Missing: '+ref['path']);head=value.detail[parts[0]]
        if len(parts)==1:return head
        return ref_value({'slot':'v','path':'/'+ '/'.join(p.replace('~','~0').replace('/','~1') for p in parts[1:])},{'v':head})
    return Value(pointer(value.data,ref['path']),dynamic=value.dynamic)
def evaluate(expr,slots):
    wrapped={k:v if isinstance(v,Value) else Value(v) for k,v in slots.items()}
    if 'literal' in expr:return deepcopy(expr['literal'])
    if 'slot' in expr:return ref_value(expr,wrapped).data
    op=expr['operator'];args=expr['arguments']
    if op=='exists':
        require(args[0]['slot'] in wrapped,'UndeclaredSlot')
        try:ref_value(args[0],wrapped);return True
        except ContractError as e:
            if str(e).startswith('Missing:'):return False
            raise
    if op in ('and','or'):
        for arg in args:
            v=evaluate(arg,wrapped);require(type(v) is bool,'ExpectedBoolean')
            if (op=='and' and not v) or (op=='or' and v):return v
        return op=='and'
    values=[evaluate(a,wrapped) for a in args]
    if op=='not':require(type(values[0]) is bool,'ExpectedBoolean');return not values[0]
    a,b=values;require(type(a) is type(b),'ExpressionTypeMismatch')
    if op in ('eq','ne'):
        require(type(a) in (type(None),bool,int,float,str),'ComparableScalarRequired')
        return a==b if op=='eq' else a!=b
    require(type(a) in (int,float) and math.isfinite(a) and math.isfinite(b),'FiniteNumericRequired')
    return {'lt':lambda:a<b,'le':lambda:a<=b,'gt':lambda:a>b,'ge':lambda:a>=b}[op]()
class Meter:
    def __init__(self,**limits):
        self.limits={'instructions':10000,'slot_bytes':64*1024*1024,'result_bytes':4*1024*1024,'control_depth':16,'deadline_ms':300000,**limits};self.used={k:0 for k in self.limits}
    def consume(self,instructions=1,**charges):
        amounts={'instructions':instructions,**charges}
        for k,v in amounts.items():require(type(v) is int and v>=0 and k in self.limits and self.used[k]+v<=self.limits[k],'BudgetExceeded: '+k)
        for k,v in amounts.items():self.used[k]+=v
# 最小测试目录。输出值仅作为类型见证，不能被称为实际业务执行结果。
def record(properties):return {'type':'object','properties':properties,'required':list(properties),'additionalProperties':False}
CATALOG={
 ('math.compute','1.0.0'):('pure_compute',record({'value':{'type':'integer'}}),Value({'value':4},dynamic=True)),
 ('settings.locate','1.0.0'):('pure_compute',record({}),Value({'domain':{'provider':'settings','id':'settings.machine-a'},'revision':'7'},dynamic=True)),
 ('settings.set_limit','1.0.0'):('state_edit',record({'axis':{'enum':['x','y','z']},'limit':{'type':'integer','minimum':0}}),Value({'applied':True},dynamic=True)),
 ('settings.apply','1.0.0'):('state_edit',record({'candidate':record({'value':{'type':'integer'}})}),Value({'applied':True},dynamic=True)),
 ('asset.preview','1.0.0'):('pure_compute',record({}),Value({'asset_id':'0'*32},'DataRef',True)),
 ('effect.send','1.0.0'):('external_effect',record({'value':{'type':'integer'}}),Value({'sent':True},dynamic=True))}
class PlanChecker:
    def __init__(self,allow_detached_exports=False):self.allow_detached_exports=allow_detached_exports
    def check(self,plan):
        check_schema(plan);self.pending=[];self.meter=Meter();self.static_count=0
        def count(block,depth=0):
            require(depth<=16,'ControlDepthExceeded')
            for node in block['steps']:
                self.static_count+=1;require(self.static_count<=1000,'StaticNodesExceeded')
                if node['kind']=='atomic':count(node,depth+1)
                elif node['kind']=='if':count(node['then'],depth+1);count(node['else'],depth+1)
                elif node['kind']=='foreach':count(node['body'],depth+1)
                elif node['kind']=='parallel':
                    for branch in node['branches']:count(branch,depth+1)
        count(plan)
        exports,_,cost=self.block(plan,{'$input':Value(plan['inputs'])})
        self.meter.consume(cost)
        return {'exports':{k:type_shape(v) for k,v in exports.items()},'pending':list(dict.fromkeys(self.pending)),'instructions':cost,'runtime_authorization':'Required; not proved by Schema or model'}
    def expression(self,expr,slots):
        def static_type(e):
            if 'literal' in e:return type(e['literal'])
            if 'slot' in e:return type(ref_value(e,slots).data)
            op=e['operator'];args=e['arguments']
            if op=='exists':
                require(args[0]['slot'] in slots,'UndeclaredSlot')
                try:ref_value(args[0],slots)
                except ContractError as error:
                    if not str(error).startswith('Missing:'):raise
                return bool
            types=[static_type(a) for a in args]
            if op in ('and','or','not'):require(all(t is bool for t in types),'ExpectedBoolean')
            elif op in ('eq','ne'):require(types[0] is types[1] and types[0] in (type(None),bool,int,float,str),'ExpressionTypeMismatch')
            else:require(types[0] is types[1] and types[0] in (int,float),'FiniteNumericRequired')
            return bool
        static_type(expr)
        if 'slot' in expr:return ref_value(expr,slots)
        dynamic=any(v.dynamic for v in slots.values()) and 'literal' not in expr
        return Value(evaluate(expr,slots),dynamic=dynamic)
    def block(self,block,parent,atomic_domain=None):
        slots=dict(parent);ids=set();tickets=[];consumed=set();cost=0
        for raw in block['steps']:
            step=raw['id'];require(step not in ids,'DuplicateStepId: '+step);ids.add(step);cost+=1
            kind=raw['kind'];out=raw.get('out');value=None
            if out is not None:require(out not in slots,'SlotShadow: '+out)
            if atomic_domain is not None:require(kind in ('call','let','assert'),'AtomicNodeForbidden')
            if kind=='call':
                key=(raw['op']['name'],raw['op']['version']);require(key in CATALOG,'UnsupportedDefinition')
                shape,args_schema,sample=CATALOG[key]
                if atomic_domain is not None:require(raw['delivery']=='complete' and shape in ('state_edit','candidate_read','pure_compute'),'AtomicShapeForbidden')
                node=apply_bindings(raw,raw.get('bindings',[]),lambda r:ref_value(r,slots).data)
                error=next(Draft202012Validator(args_schema).iter_errors(node['args']),None);require(error is None,'BoundArgsType: '+step)
                if shape=='state_edit':
                    target=node.get('target',atomic_domain);require(target is not None,'TargetRequired')
                    require(isinstance(target,dict) and set(target)=={'provider','id'} and all(type(v) is str and v for v in target.values()),'TargetType')
                    if atomic_domain is not None:require(target==atomic_domain,'AtomicDomainMismatch')
                value=deepcopy(sample)
                if raw['delivery']=='ticket':
                    require(out is not None,'TicketOutRequired');value=Value({'execution_id':'0'*32},'ExecutionRef',True,value);tickets.append(value)
            elif kind=='let':value=self.expression(raw['value'],slots)
            elif kind=='assert':
                pred=self.expression(raw['predicate'],slots);require(type(pred.data) is bool,'AssertBoolean')
                # Check 校验表达式，不把业务 assert(false) 冒充执行；执行时必须停止当前块。
            elif kind=='await':
                ticket=ref_value(raw['task'],slots);require(ticket.kind=='ExecutionRef','AwaitRequiresTypedExecutionRef');consumed.add(id(ticket));value=deepcopy(ticket.detail)
            elif kind=='atomic':
                bound=apply_bindings(raw,raw.get('bindings',[]),lambda r:ref_value(r,slots).data)
                require('domain' in bound and set(bound['domain'])=={'provider','id'},'AtomicDomainRequired')
                rev=bound['preconditions'].get('revision');require(type(rev) is str and re.fullmatch('0|[1-9][0-9]*',rev) and int(rev)<=MAX_U64,'AtomicRevisionRequired')
                if any(ref_value(b['value'],slots).dynamic for b in raw.get('bindings',[])):self.pending.append(step+':DynamicCheckRequired')
                exports,child_consumed,child_cost=self.block(raw,slots,bound['domain']);consumed|=child_consumed;cost+=child_cost
                value=Value({'values':{k:v.data for k,v in exports.items()},'commit':{'commit_id':'0'*32}},'AtomicResult',True,exports)
            elif kind=='if':
                pred=self.expression(raw['predicate'],slots);require(type(pred.data) is bool,'IfBoolean')
                left,lc,la=self.block(raw['then'],slots);right,rc,ra=self.block(raw['else'],slots)
                require({k:type_shape(v) for k,v in left.items()}=={k:type_shape(v) for k,v in right.items()},'BranchExportsMismatch')
                chosen=left if pred.data else right;consumed|=lc&rc;cost+=max(la,ra) if pred.dynamic else (la if pred.data else ra)
                value=Value({k:v.data for k,v in chosen.items()},'BlockExports',pred.dynamic or any(v.dynamic for v in chosen.values()),chosen)
            elif kind=='foreach':
                collection=ref_value(raw['collection'],slots);require(isinstance(collection.data,list),'CollectionArrayRequired');require(len(collection.data)<=raw['max_iterations'],'MaxIterationsExceeded')
                require(raw['item_slot']!=raw['index_slot'] and raw['item_slot'] not in slots and raw['index_slot'] not in slots,'LoopSlotShadow')
                type_shape(collection.data)
                prototype=None;rows=[]
                # 空集也编译 body，需显式元素类型；模型无见证时拒绝未定类型。
                require(collection.data or not raw['body']['steps'] and not raw['body']['exports'],'EmptyCollectionElementTypeRequired')
                for index,item in enumerate(deepcopy(collection.data)):
                    local={**slots,raw['item_slot']:Value(item,dynamic=collection.dynamic),raw['index_slot']:Value(index)}
                    ex,cc,spent=self.block(raw['body'],local);consumed|=cc;cost+=1+spent;prototype=Value({k:v.data for k,v in ex.items()},'BlockExports',collection.dynamic,ex);rows.append(prototype.data)
                    require(cost<=10000,'BudgetExceeded: instructions')
                if collection.dynamic:self.pending.append(step+':DynamicLengthCheckRequired')
                value=Value(rows,'LoopResults',collection.dynamic,prototype or Value({},'BlockExports',False,{}))
            elif kind=='parallel':
                results=[]
                for branch in raw['branches']:
                    ex,cc,spent=self.block(branch,slots);consumed|=cc;cost+=spent;results.append(Value({k:v.data for k,v in ex.items()},'BlockExports',any(v.dynamic for v in ex.values()),ex))
                value=Value([r.data for r in results],'BranchResults',any(r.dynamic for r in results),results)
            if out is not None:slots[out]=value
        exports={k:ref_value(r,slots) for k,r in block['exports'].items()}
        exported={id(v) for v in exports.values() if v.kind=='ExecutionRef'}
        for ticket in tickets:require(id(ticket) in consumed or self.allow_detached_exports and id(ticket) in exported,'TicketMustAwaitOrAuthorizedExport')
        return exports,consumed,cost

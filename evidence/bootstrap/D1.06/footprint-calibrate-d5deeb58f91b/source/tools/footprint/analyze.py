"""只从完整原始材料计算；缺样、缺预算不会生成 Passed。"""
import math
from datetime import datetime


def finite(value):
    return type(value) in (int,float) and math.isfinite(value)


def nearest_rank(values, probability):
    if not values or not finite(probability) or not 0 < probability <= 1 or not all(finite(v) for v in values):
        raise ValueError('finite nonempty samples and probability required')
    return sorted(values)[math.ceil(probability*len(values))-1]


def statistics(values):
    return {'count':len(values),'min':min(values),'median':nearest_rank(values,.5),
            'p95':nearest_rank(values,.95),'max':max(values)}


def paired_differences(rows, blocks):
    if type(blocks) is not int or blocks<=0 or len(rows)!=blocks*4:
        raise ValueError('complete fixed ABBA blocks required')
    ids=[r['run_id'] for r in rows]
    if len(set(ids))!=len(ids) or len({r['identity'] for r in rows})!=1:
        raise ValueError('duplicate runs or mismatched method identity')
    differences=[]
    for block in range(blocks):
        group=rows[block*4:block*4+4]
        if ''.join(r['kind'] for r in group)!='ABBA' or any(r['block']!=block or not finite(r['value']) for r in group):
            raise ValueError('ABBA order/block/value mismatch')
        differences.extend([group[1]['value']-group[0]['value'],group[2]['value']-group[3]['value']])
    return differences


def require_modules(required,recorded):
    def index(rows):
        mapping={}
        for row in rows:
            key=row['path'].casefold()
            if key in mapping or type(row.get('bytes')) is not int or row['bytes']<=0 or len(row.get('sha256',''))!=64:
                raise ValueError('duplicate/incomplete module identity')
            mapping[key]=row
        return mapping
    actual=index(recorded)
    for key,row in index(required).items():
        if key not in actual or any(actual[key][name]!=row[name] for name in ('sha256','bytes')):
            raise ValueError('required distribution module omitted or changed')


def owned_success(result):
    tree=result.get('process_tree',{})
    return (isinstance(tree,dict) and result.get('status')=='Exited' and result.get('exit_code')==0
            and tree.get('assigned_before_resume') is True and tree.get('active_after')==0
            and tree.get('terminated_owned_job') is False)


def require_complete(result):
    from .windows_process import PHASES
    observation=result.get('observation')
    if not owned_success(result) or not isinstance(observation,dict):
        raise ValueError('root did not exit successfully with observation')
    tree=result.get('process_tree',{})
    if tree.get('active_after')!=0 or tree.get('terminated_owned_job') is not False:
        raise ValueError('owned Job was not normally drained')
    if observation.get('status')!='Complete' or [p.get('phase') for p in observation.get('phases',[])]!=list(PHASES):
        raise ValueError('observation/stage sequence incomplete')
    samples=observation.get('samples')
    if not samples or any(s.get('ok') is not True or not finite(s.get('private_bytes')) or not finite(s.get('working_set_bytes')) for s in samples):
        raise ValueError('missing or invalid samples; never substitute zero')
    if observation.get('method')!='ock.native-footprint/2':raise ValueError('unsupported or missing observation method')
    require_sampling_v2(observation)


def require_sampling_v2(observation):
    from .windows_process import ObservationConfig,PHASES
    configuration=ObservationConfig(**observation['configuration'])
    if (observation.get('method_digest')!=configuration.identity() or observation.get('memory_sampling')!=configuration.memory_sampling or
        observation.get('thread_sampling')!='phase_boundaries'):
        raise ValueError('v2 sampling identity mismatch')
    boundaries=[]
    def valid_query(query):
        return (isinstance(query,dict) and query.get('ok') is True and query.get('win32_error')==0 and
                type(query.get('structure_bytes')) is int and query['structure_bytes']>0 and
                type(query.get('started_ticks')) is int and type(query.get('completed_ticks')) is int and
                0<=query['started_ticks']<=query['completed_ticks'])
    for sample in observation['samples']:
        if not valid_query(sample.get('memory_query')):raise ValueError('missing memory query facts')
        reason=sample.get('reason');thread=sample.get('thread_query');ids=sample.get('thread_ids')
        if reason=='periodic':
            if configuration.mode=='latency':raise ValueError('periodic query in latency mode')
            if thread is not None or ids is not None:raise ValueError('periodic point copied or queried thread state')
        else:
            if reason not in ('assigned_suspended',*PHASES):raise ValueError('unknown thread boundary')
            boundaries.append(reason)
            if (not valid_query(thread) or not isinstance(ids,list) or not ids or
                any(type(value) is not int or value<=0 for value in ids) or len(set(ids))!=len(ids) or
                thread['started_ticks']<sample['memory_query']['completed_ticks']):
                raise ValueError('missing boundary thread query facts')
    if boundaries!=['assigned_suspended',*PHASES]:raise ValueError('missing or repeated thread boundaries')


def validate_budget(budget,*,method_digest,report_created,run_ids):
    if not isinstance(budget,dict) or budget.get('status')!='Approved' or budget.get('method_digest')!=method_digest:
        raise ValueError('matching approved budget required')
    allowed={'status','method_digest','approved_at','pilot_run_ids','limits'}
    if set(budget)-allowed or not budget.get('pilot_run_ids') or set(run_ids)&set(budget['pilot_run_ids']):
        raise ValueError('unknown budget transformation or reused pilot runs')
    try:
        approved=datetime.fromisoformat(budget['approved_at'].replace('Z','+00:00'))
        report=datetime.fromisoformat(report_created.replace('Z','+00:00'))
    except (KeyError,TypeError,ValueError) as exc:
        raise ValueError('real approval and report timestamps required') from exc
    if approved.tzinfo is None or report.tzinfo is None or approved>=report:
        raise ValueError('budget approval must precede the new report')
    limits=budget.get('limits')
    if not isinstance(limits,dict) or not limits or any(not finite(v) or v<=0 for v in limits.values()):
        raise ValueError('finite positive approved limits required')


def summarize_observation(result):
    require_complete(result)
    observation=result['observation']
    threads=[s for s in observation['samples'] if s.get('thread_ids') is not None]
    return {'measurement_status':'Complete','budget_status':'NotApproved',
            'private_bytes':statistics([s['private_bytes'] for s in observation['samples']]),
            'working_set_bytes':statistics([s['working_set_bytes'] for s in observation['samples']]),
            'observed_thread_peak':max(len(s['thread_ids']) for s in threads),
            'thread_sample_count':len(threads),
            'thread_peak_scope':observation.get('thread_sampling','v1_sampled'),
            'sample_count':len(observation['samples']),
            'missed_intervals':sum(s.get('missed_intervals',0) for s in observation['samples']),
            'ready':observation.get('ready'),
            'process_exited_memory':'NotMeasured',
            'cold_os':'NotMeasured'}

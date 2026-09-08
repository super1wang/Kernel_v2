"""对已实际执行的同轮Logging共同项作机器资格判定，不运行替代后端。"""
import copy
from tests.conformance.support.identity import descriptor_errors,complete_common
VERSION='ock.logging.conformance/1'
COMMON=('accept_reject_drop_accounting','format_redaction','error_isolation','flush_accepted_range','shutdown_callback_lifetime')
CAPS={'async':False,'file':False,'callbacks':False,'volatile_retention':True}
FACTORIES={'memory':('production','ock::runtime::observability::make_memory_logging','packages/runtime/observability/logging.cpp'),
           'test':('test','logging_test::Fixture::bundle(false)','tests/conformance/logging/reference_backend.hpp')}
CASES=tuple('T22.logging.'+backend+'.'+case for backend in FACTORIES for case in COMMON)+('T19.logging.memory_pages','T23.logging.fixed_write_allocation')

def descriptors(binding):
    sources={row['path']:row['sha256'] for row in binding['sources']}
    return {name:{'name':name,'factory':factory,'kind':kind,'port_contract_version':VERSION,
                  'implementation_sha256':sources[path],'capabilities':dict(CAPS)}
            for name,(kind,factory,path) in FACTORIES.items()}

def qualify(binding,rows,run_id,binary_sha256,claimed=None):
    actual=descriptors(binding);claimed=actual if claimed is None else claimed
    if set(claimed)!=set(actual):raise ValueError('actual two-factory set differs')
    names=[row.get('case') for row in rows]
    if len(names)!=len(set(names)) or set(names)!=set(CASES):raise ValueError('missing/duplicate Logging case')
    for row in rows:
        if row.get('run_id')!=run_id:raise ValueError('cross-run result reuse')
        if row.get('binary_sha256')!=binary_sha256 or row.get('binding_sha256')!=binding['sha256']:raise ValueError('binary/source binding differs')
        if row.get('status')!='Passed':raise ValueError('actual Logging case failed')
    results=[]
    for backend in actual:
        errors=descriptor_errors(claimed[backend],actual[backend],CAPS,('production','test'),exact_capabilities=True)
        if claimed[backend].get('kind')!=actual[backend]['kind'] or claimed[backend].get('name')!=backend:errors.append('actual factory kind/name differs')
        common=[{'id':r['case'].rsplit('.',1)[1],'status':r['status']} for r in rows if r['case'].startswith('T22.logging.'+backend+'.')]
        qualified=not errors and complete_common(common,COMMON)
        results.append({'descriptor':copy.deepcopy(claimed[backend]),'cases':common,'errors':errors,'qualified':qualified,
          'optional':[{'id':name,'status':'NotApplicable','reason':'contract expression capabilities.'+cap+' == false'} for name,cap in [('queue_full','async'),('rotation','file'),('disk_faults','file')]]})
    return {'format':'ock.logging-qualification/1','contract_version':VERSION,'run_id':run_id,'binary_sha256':binary_sha256,
      'binding_sha256':binding['sha256'],'results':results,'qualified':all(x['qualified'] for x in results)}

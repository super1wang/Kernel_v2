"""分配逐窗原始报告严格校验；不将未测通道解释为零。"""
def validate_allocation(record,kind,configuration,asan,*,injected=False):
    if (not isinstance(record,dict) or record.get('format')!='ock.footprint-allocation/1' or
        record.get('kind')!=kind or configuration not in ('Debug','Release') or type(asan) is not bool):
        raise ValueError('allocation report identity mismatch')
    coverage=record.get('coverage',{})
    crt=configuration=='Debug'
    if any(coverage.get(k) is not v for k,v in [('cpp',True),('crt',crt),('asan',asan)]):
        raise ValueError('allocation channel applicability mismatch')
    labels=('construct_register_start','log_proof_open_verify_bind','first_compute','first_read','invalid_input','shutdown','bound_release','session_release','last_owner_release')
    expected={('probe',i) for i in range(12)}|{('probe_negative',0)}|{(label,0) for label in labels}|{('warmup',i) for i in range(4)}|{('invoke',i) for i in range(40)}
    rows=record.get('samples');seen={}
    if not isinstance(rows,list):raise ValueError('missing allocation samples')
    for row in rows:
        key=(row.get('window'),row.get('index'))
        if type(key[1]) is not int or key not in expected or key in seen or row.get('success') is not True:
            raise ValueError('missing/duplicate/failed allocation window')
        for name,available in [('cpp',True),('crt',crt),('asan_allocations',asan),('asan_frees',asan)]:
            value=row.get(name)
            if (available and (type(value) is not int or value<0)) or (not available and value is not None):
                raise ValueError('allocation channel value missing or fabricated')
        for name in ('cpp_frees','allocated_bytes','released_bytes','live_before','live_after','peak_live'):
            if type(row.get(name)) is not int or row[name]<0:raise ValueError('allocation ledger field missing or invalid')
        # 固定单线程消费者的TLS增减必须与全局账本吻合；并发污染使窗口无效。
        if (row['live_after']!=row['live_before']+row['allocated_bytes']-row['released_bytes'] or
            row['peak_live']<max(row['live_before'],row['live_after']) or
            row['allocated_bytes']<row['cpp'] or row['released_bytes']<row['cpp_frees'] or
            (row['cpp']==0 and row['allocated_bytes']!=0) or (row['cpp_frees']==0 and row['released_bytes']!=0)):
            raise ValueError('allocation ledger balance or peak mismatch')
        seen[key]=row
    if set(seen)!=expected:raise ValueError('allocation windows incomplete')
    for i in range(12):
        row=seen['probe',i]
        if row['cpp']!=(1 if i<8 else 0):raise ValueError('C++ probe did not count exact entry')
        if i<8:
            requested=64 if i in (2,3,6,7) else 37
            if (row['cpp_frees']!=1 or row['live_after']!=row['live_before'] or
                row['allocated_bytes']!=row['released_bytes'] or row['allocated_bytes']<requested):
                raise ValueError('C++ probe allocation/free pairing invalid')
        elif row['cpp_frees']!=0:raise ValueError('CRT-only probe falsely counted C++ free')
        if asan:
            if row['asan_allocations']!=1 or (crt and row['crt']!=0):raise ValueError('ASan probe channel mismatch')
        elif crt and row['crt']<=0:raise ValueError('CRT probe entry not observed')
    zero_names=['cpp']+(['crt'] if crt else [])+(['asan_allocations'] if asan else [])
    def zero(row):return all(row[name]==0 for name in zero_names)
    if not zero(seen['probe_negative',0]):raise ValueError('negative probe counted allocation')
    for i in range(40):
        row=seen['invoke',i]
        if injected and i==0:
            if row['cpp']<=0 or (crt and not asan and row['crt']<=0) or (asan and row['asan_allocations']<=0):
                raise ValueError('injection not proven in specific window')
        elif not zero(row):raise ValueError('steady window allocated')
    if record.get('verified') is not (not injected):raise ValueError('allocation verification outcome mismatch')

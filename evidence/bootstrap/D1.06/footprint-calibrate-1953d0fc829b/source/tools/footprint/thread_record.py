"""线程快照只证明实际入口归属；不据相同计数批准内核线程预算。"""
def validate_thread_record(record,pid,held):
    def require(value):
        if not value:raise ValueError('invalid/incomplete thread-origin diagnostic')
    require(record.get('format')=='ock.thread-origin/1' and record.get('pid')==pid)
    require(all(type(record.get(k)) is int and record[k]==v for k,v in
                [('capture',0),('walk_end',259),('free_marker',0),('free_snapshot',0)]))
    require(record.get('held_joined') is True)
    rows=record.get('threads');require(isinstance(rows,list) and 1<=len(rows)<=64)
    ids=[]
    for row in rows:
        require(all(type(row.get(k)) is int for k in ('pid','tid','flags','start','module_base','module_bytes','rva','created')))
        require(row['pid']==pid and row['tid']>0 and row['flags']==0 and row['created']>0)
        require(0<row['module_base']<=row['start']<row['module_base']+row['module_bytes']
                and row['rva']==row['start']-row['module_base'])
        require(isinstance(row.get('module'),str) and len(row['module'])>3 and '\x00' not in row['module'])
        ids.append(row['tid'])
    require(len(set(ids))==len(ids) and record.get('main_tid') in ids)
    require(type(record.get('held_tid')) is int and
            (record['held_tid'] in ids and record['held_tid']!=record['main_tid'] if held else record['held_tid']==0))

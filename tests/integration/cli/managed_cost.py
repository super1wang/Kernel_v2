"""G3 AutomationHost 成本样本；真实进程/管道，非计数器或峰值预算替身。"""
import ctypes
from ctypes import wintypes as w
import json
import hashlib
from pathlib import Path
import sys
import time
import threading
from managed_wire_support import Wire,start,ref,listed,servers,connections

class Memory(ctypes.Structure):
    _fields_=[('cb',w.DWORD),('faults',w.DWORD),('peak_ws',ctypes.c_size_t),('ws',ctypes.c_size_t),
      ('peak_paged',ctypes.c_size_t),('paged',ctypes.c_size_t),('peak_nonpaged',ctypes.c_size_t),
      ('nonpaged',ctypes.c_size_t),('pagefile',ctypes.c_size_t),('peak_pagefile',ctypes.c_size_t),('private',ctypes.c_size_t)]
k=ctypes.WinDLL('kernel32',use_last_error=True);p=ctypes.WinDLL('psapi',use_last_error=True)
k.GetProcessTimes.argtypes=[w.HANDLE,*([ctypes.POINTER(w.FILETIME)]*4)]
p.GetProcessMemoryInfo.argtypes=[w.HANDLE,ctypes.POINTER(Memory),w.DWORD]
def snapshot(server):
    assert server.poll() is None
    created,exited,kernel,user=(w.FILETIME() for _ in range(4));handle=w.HANDLE(int(server._handle))
    assert k.GetProcessTimes(handle,ctypes.byref(created),ctypes.byref(exited),ctypes.byref(kernel),ctypes.byref(user)),ctypes.get_last_error()
    value=Memory();value.cb=ctypes.sizeof(value)
    assert p.GetProcessMemoryInfo(handle,ctypes.byref(value),value.cb),ctypes.get_last_error()
    ticks=lambda t:(t.dwHighDateTime<<32)|t.dwLowDateTime
    return {'kernel_100ns':ticks(kernel),'user_100ns':ticks(user),'private_bytes':value.private,
      'working_set_bytes':value.ws,'peak_working_set_bytes':value.peak_ws,'peak_commit_bytes':value.peak_pagefile}
def cpu(before,after):return sum(after[key]-before[key] for key in ('kernel_100ns','user_100ns'))/10000
def diagnostic(server):
    server.stdin.write(b'observation-diagnostics\n');server.stdin.flush()
    return json.loads(server.stdout.readline())
def measured(wire,method,params,rows):
    begin=time.perf_counter_ns();value=wire.call(method,params)
    rows.append((time.perf_counter_ns()-begin)/1000000);return value

output=Path(sys.argv[2]);results=[];readers=[]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
binary=Path(sys.argv[1]).resolve()
inputs={'binary':str(binary),'binary_sha256':digest(binary),'binary_bytes':binary.stat().st_size,
  'source_sha256':digest(Path(__file__)),'wire_source_sha256':digest(Path(__file__).with_name('managed_wire_support.py'))}
try:
    for mode in ('off','on','on','off','slow','slow'):
        endpoint=start();server=endpoint[1];fast,observer=Wire(endpoint),Wire(endpoint)
        card=fast.call('capabilities.describe',{'name':'sample.compute','version':'1.0.0'})['result']
        if mode!='off':
            for _ in range(8):assert 'result' in observer.call('notifications.subscribe',{'filter':{'owner':'self'},'topics':['execution.phase']})
        stop=threading.Event();received={'events':0,'errors':[]}
        def read_events():
            try:
                while not stop.is_set():
                    if mode!='slow' and observer.available():
                        frame=observer.receive();assert frame.get('method')=='notifications.event';received['events']+=1
                    else:stop.wait(0.001)
            except Exception as error:received['errors'].append(repr(error))
        reader=threading.Thread(target=read_events);readers.append((stop,reader));reader.start()
        initial=snapshot(server);memory=[initial];latency={'submit':[],'wait':[],'get':[]}
        begin=time.perf_counter_ns()
        for index in range(120):
            submitted=measured(fast,'operation.submit',{'operation':{'name':'sample.compute','version':'1.0.0'},
              'contract_digest':card['contract_digest'],'args':{'amount':8,'delay_ms':20,'text':'cost sample'},
              'execution_timeout_ms':10000},latency['submit'])
            assert submitted['result']['kind']=='Accepted',submitted
            identity=submitted['result']['execution_ref']['execution_id']
            done=measured(fast,'execution.wait',{**ref(identity),'wait_timeout_ms':5000},latency['wait'])
            assert done['result']['wait_state']=='Terminal',done
            assert measured(fast,'execution.get',ref(identity),latency['get'])['result']['phase']=='Terminal'
            if index%8==7:memory.append(snapshot(server))
        elapsed=(time.perf_counter_ns()-begin)/1000000;work_end=snapshot(server);state=diagnostic(server)
        drain_begin=time.perf_counter_ns()
        if mode=='on':
            until=time.monotonic()+10
            while state['queued_bytes']:
                assert time.monotonic()<until,'notification drain deadline'
                time.sleep(0.01);state=diagnostic(server)
            assert received['events']>0
        drained_ms=(time.perf_counter_ns()-drain_begin)/1000000;final=snapshot(server)
        stop.set();reader.join(12);assert not reader.is_alive() and not received['errors'],received
        if mode=='slow':assert 0<state['queued_bytes']<=1024*1024 and state['started']>0,state
        if mode=='off':assert state['queued_bytes']==0 and state['started']==0,state
        row={'mode':mode,'pid':server.pid,'tasks':120,'task_delay_ms':20,'server_cpu_ms':cpu(initial,final),
          'active_cpu_ms':cpu(initial,work_end),'notification_drain_cpu_ms':cpu(work_end,final),'notification_drain_ms':drained_ms,
          'client_elapsed_ms':elapsed,'client_request_ms':latency,'memory':memory+[final],
          'notification_frames_read':received['events'],'diagnostics':state,'cursor':[]}
        if mode=='off':
            cursor=listed(fast)['result']['next_cursor'];assert len(cursor)<=2048
            offset=cursor.rfind('.')+1
            altered=cursor[:offset]+('A' if cursor[offset]!='A' else 'B')+cursor[offset+1:]
            for label,value,code in [('valid',cursor,None),('mac',altered,-32011),('oversized','v1.'+'a'*2049,-32602)]:
                samples=[];before=snapshot(server)
                for _ in range(100):
                    response=measured(fast,'execution.list',{'owner':'self','phase_set':'terminal','page_size':1,'cursor':value},samples)
                    if code is None:assert 'result' in response
                    else:assert response['error']['code']==code,response
                after=snapshot(server)
                row['cursor'].append({'case':label,'input_chars':len(value),'calls':100,'server_cpu_ms':cpu(before,after),
                  'client_request_ms':samples,'before':before,'after':after,'expected_error':code})
        fast.close();observer.close()
        _,error=server.communicate(b'stop\n',timeout=10);assert server.returncode==0,(server.returncode,error)
        row['exit_code']=server.returncode;results.append(row)
        assert digest(binary)==inputs['binary_sha256']
        output.write_text(json.dumps({'inputs':inputs,'scope':'AutomationHost Release samples; client 5 ms polling; not Embedded or hard latency limits',
          'runs':results},indent=2)+'\n',encoding='utf-8')
        print('COST',mode,round(row['server_cpu_ms'],3),'ms CPU',state,flush=True)
finally:
    for stop,reader in readers:stop.set();reader.join(12)
    for connection in connections:connection.close()
    for server in servers:
        if server.poll() is None:
            try:server.communicate(b'stop\n',timeout=10)
            except Exception:server.kill();server.communicate()

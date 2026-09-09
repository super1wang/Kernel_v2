"""保持真实慢观察管道，验证默认 30 s 超时及独立控制连接存活。"""
import json
import sys
import time
from managed_wire_support import Wire,start,ref,servers,connections

endpoint=start();server=endpoint[1]
def diagnostics():
    server.stdin.write(b'observation-diagnostics\n');server.stdin.flush()
    return json.loads(server.stdout.readline())
try:
    fast,slow=Wire(endpoint),Wire(endpoint)
    card=fast.call('capabilities.describe',{'name':'sample.compute','version':'1.0.0'})['result']
    for _ in range(8):
        assert 'result' in slow.call('notifications.subscribe',{'filter':{'owner':'self'},'topics':['execution.phase']})
    for count in range(32):
        value=fast.call('operation.submit',{'operation':{'name':'sample.compute','version':'1.0.0'},
            'contract_digest':card['contract_digest'],'args':{'amount':1,'delay_ms':20,'text':'timeout'},'execution_timeout_ms':10000})
        assert value['result']['kind']=='Accepted',value
        last=value['result']['execution_ref']['execution_id']
        assert fast.call('execution.wait',{**ref(last),'wait_timeout_ms':5000})['result']['wait_state']=='Terminal'
        before=diagnostics()
        if before['queued_bytes']>=16384:
            time.sleep(0.25)
            stable=diagnostics()
            if stable['queued_bytes']>=16384 and stable['started']==before['started']:
                before=stable;break
    else:raise AssertionError('no stable real pipe backpressure within bounded workload')
    assert before['sessions']==2 and before['queued_bytes']>=16384,before
    started=time.monotonic();print('real slow queue established',count+1,before,flush=True)
    while True:
        time.sleep(1)
        assert fast.call('execution.get',ref(last))['result']['phase']=='Terminal'
        state=diagnostics();elapsed=time.monotonic()-started
        if int(elapsed)%5==0 or state['sessions']==1:print('slow timeout seconds',round(elapsed,2),state,flush=True)
        assert elapsed<35,'default slow connection did not close'
        if state['sessions']==1:
            assert elapsed>=25,('unexpected early close',elapsed,state)
            assert state['queued_bytes']==0 and state['started']==before['started'],(before,state)
            break
    print('real default 30 s slow connection timeout released queue; authenticated control remained usable',flush=True)
finally:
    for connection in connections:connection.close()
    for process in servers:
        if process.poll() is None:
            try:
                _,error=process.communicate(b'stop\n',timeout=10)
                if error:print(error.decode(errors='replace'),file=sys.stderr)
            except Exception:
                process.kill();process.communicate();raise
        assert process.returncode==0,process.returncode

"""真实慢管道、可靠 CLI 控制、gap 与当前会话撤权；不替换发送结果。"""
import ctypes
import json
import subprocess
import sys
import time
from managed_wire_support import Wire,start,ref,servers,connections

endpoint=start();server=endpoint[1]
def diagnostics(command='observation-diagnostics'):
    server.stdin.write((command+'\n').encode());server.stdin.flush()
    value=json.loads(server.stdout.readline());print('DIAGNOSTICS',value,flush=True);return value

def cli(arguments):
    result=subprocess.run([sys.argv[2],'--instance',endpoint[0],'--json',*arguments],capture_output=True,timeout=10)
    print('CLI',arguments[:2],result.returncode,flush=True)
    assert result.returncode==0,(result.stdout,result.stderr)
    return json.loads(result.stdout)['result']

try:
    fast,slow=Wire(endpoint),Wire(endpoint)
    card=fast.call('capabilities.describe',{'name':'sample.compute','version':'1.0.0'})['result']
    for _ in range(8):
        assert 'result' in slow.call('notifications.subscribe',{'filter':{'owner':'self'},'topics':['execution.phase']})
    def submit(delay=20):
        value=fast.call('operation.submit',{'operation':{'name':'sample.compute','version':'1.0.0'},
            'contract_digest':card['contract_digest'],'args':{'amount':8,'delay_ms':delay,'text':'slow observer'},
            'execution_timeout_ms':10000})
        assert value['result']['kind']=='Accepted',value
        return value['result']['execution_ref']['execution_id']
    def wait(value):
        result=fast.call('execution.wait',{**ref(value),'wait_timeout_ms':5000})
        assert result['result']['wait_state']=='Terminal',result
    def pressure():
        for _ in range(120):
            value=submit();wait(value)
        state=diagnostics()
        assert 0<state['queued_bytes']<=1024*1024,state
        assert state['started']>0,state
        return value,state
    last,blocked=pressure()
    assert cli(['execution','get',last])['phase']=='Terminal'
    running=submit(5000)
    until=time.monotonic()+3
    while fast.call('execution.get',ref(running))['result']['phase']!='Running':
        assert time.monotonic()<until
        time.sleep(0.01)
    assert cli(['execution','cancel',running])['disposition']=='AlreadyClaimed'
    wait(running)
    assert cli(['result','read',running])['reply']['outcome']['result']['stop_observed'] is True
    print('real slow observer did not block CLI get/cancel or reliable completion',flush=True)

    seen_gap=False;received=0
    def drain():
        global received,seen_gap
        until=time.monotonic()+10;idle=0
        while time.monotonic()<until:
            if slow.available():
                frame=slow.receive();assert frame.get('method')=='notifications.event',frame
                received+=1;assert received<=4096
                seen_gap|=frame['params']['gap'];idle=0
            else:
                idle+=1
                if idle>=100:
                    if diagnostics()['queued_bytes']==0:return
                    idle=0
                time.sleep(0.01)
        raise AssertionError('real notification queue did not drain')
    drain()
    changed=submit();wait(changed);drain()
    assert seen_gap,'overflow/coalescing did not report a gap'
    assert fast.call('execution.get',ref(changed))['result']['phase']=='Terminal'
    print('resumed real pipe delivered gap; authorized get confirmed terminal',received,flush=True)

    last,before=pressure()
    revoked=diagnostics('restrict-observers');assert revoked['restricted']>=2
    # 已开始的帧可以仍在 OS 管道内；恢复读取后，未开始队列不能再增加 Started。
    drained_bytes=0;until=time.monotonic()+3
    while time.monotonic()<until:
        try: available=slow.available()
        except AssertionError:
            assert ctypes.get_last_error() in (109,232,233)
            break
        if available:
            drained_bytes+=len(slow.pipe.read(available));assert drained_bytes<=4*1024*1024
        else: time.sleep(0.01)
    after=diagnostics()
    assert after['started']==revoked['started'],(before,revoked,after)
    assert after['queued_bytes']==0,after
    assert cli(['execution','get',last])['phase']=='Terminal'
    print('revoke with queued real events: no later first byte, queue released, buffered bytes',drained_bytes,flush=True)
finally:
    for connection in connections:connection.close()
    for process in servers:
        if process.poll() is None:
            try:
                _,error=process.communicate(b'stop\n',timeout=10)
                if error:print(error.decode(errors='replace'),file=sys.stderr)
            except subprocess.TimeoutExpired:
                process.kill();process.communicate()
        assert process.returncode==0,process.returncode

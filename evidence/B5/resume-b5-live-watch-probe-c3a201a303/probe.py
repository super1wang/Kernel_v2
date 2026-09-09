
import sys,json,subprocess,threading,queue,traceback
from pathlib import Path
sys.argv=['probe',str(Path.cwd()/'build/b4-debug/examples/managed_service/Debug/ock_managed_service.exe')]
exec(Path('tests/integration/cli/managed_wire.py').read_text(encoding='utf-8').split('\ntry:\n    endpoint = start()',1)[0])
watcher=None
try:
 endpoint=start(); initial=Wire(endpoint);print('initial wire hello',flush=True)
 card=initial.call('capabilities.describe',{'name':'sample.compute','version':'1.0.0'})['result']
 submit=initial.call('operation.submit',{'operation':{'name':'sample.compute','version':'1.0.0'},'contract_digest':card['contract_digest'],'args':{'amount':1,'delay_ms':10000,'text':'probe'},'execution_timeout_ms':10000});print('submit',submit,flush=True)
 refid=submit['result']['execution_ref']['execution_id']
 cli=str(Path.cwd()/'build/b4-debug/apps/ock/Debug/ock.exe')
 watcher=subprocess.Popen([cli,'--instance',endpoint[0],'execution','watch',refid,'--jsonl'],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 print('watcher',watcher.pid,watcher.stdout.readline(),flush=True)
 fresh=Wire(endpoint);print('fresh wire hello',fresh.hello,flush=True)
 print('fresh get',fresh.call('execution.get',ref(refid)),flush=True)
 print('initial get',initial.call('execution.get',ref(refid)),flush=True)
except Exception:traceback.print_exc()
finally:
 if watcher and watcher.poll() is None:watcher.kill();print('watch tail',watcher.communicate(),flush=True)
 for connection in connections:connection.close()
 for server in servers:
  print('server final',server.communicate(b'stop\n',timeout=15),server.returncode,flush=True)

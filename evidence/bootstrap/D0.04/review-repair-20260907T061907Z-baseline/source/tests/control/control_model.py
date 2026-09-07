"""D0.04 Control 原子步参考模型；没有真实连接、线程、安全适配器或 Runtime。"""
from copy import deepcopy
import base64
import hashlib
import hmac
import json
from pathlib import Path
import re
import secrets
from jsonschema import Draft202012Validator
from referencing import Registry, Resource
ROOT=Path(__file__).resolve().parents[2]
MAX_U64=2**64-1
EVENT_METADATA_BYTES=128
TOPICS={'execution.progress','execution.phase','execution.fact'}
BUDGETS={'pending_messages':128,'frame_bytes':16384,'connection_bytes':1048576,'principal_bytes':4194304,'host_bytes':16777216}
SCHEMAS={name:json.loads((ROOT/'schemas/rpc-v1'/name).read_text(encoding='utf-8')) for name in ['common.schema.json','notifications.schema.json','execution-list.schema.json']}
REGISTRY=Registry().with_resources([(s['$id'],Resource.from_contents(s)) for s in SCHEMAS.values()])
for schema in SCHEMAS.values():Draft202012Validator.check_schema(schema)
class ContractError(ValueError):pass
def require(ok,reason):
 if not ok:raise ContractError(reason)
def canonical(value):return json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=True,allow_nan=False).encode('ascii')
def tagged(label):return hashlib.sha256(label.encode('utf-8')).hexdigest()[:32]
def validate_message(message,direction):
 require(direction in ('client_to_server','server_to_client'),'Direction')
 require(type(message) is dict,'Schema: SingleObjectRequired')
 method=message.get('method')
 if direction=='client_to_server':
  require(method in ('notifications.subscribe','notifications.unsubscribe','execution.list') and 'id' in message,'ClientMethodDirection')
 else:require(method=='notifications.event' and 'id' not in message or method is None and 'id' in message,'ServerMethodDirection')
 name='execution-list.schema.json' if method=='execution.list' or 'items' in message.get('result',{}) else 'notifications.schema.json'
 error=next(Draft202012Validator(SCHEMAS[name],registry=REGISTRY).iter_errors(message),None)
 require(error is None,'Schema: '+(error.message[:160] if error else ''))
 require(len(canonical(message))<=(16384 if method=='notifications.event' else 4*1024*1024),'FrameTooLarge')
 def counters(value):
  if isinstance(value,dict):
   for k,v in value.items():
    if k in ('sequence','observation_version','completed','total'):require(type(v) is str and re.fullmatch('0|[1-9][0-9]{0,19}',v) and int(v)<=MAX_U64,'CounterOverflow')
    counters(v)
  elif isinstance(value,list):
   for v in value:counters(v)
 counters(message)
 if method=='notifications.event':
  params=message['params'];require(int(params['sequence'])>=1 and int(params['observation_version'])>=1,'CounterZero')
  if params['topic']=='execution.progress':require(int(params['data']['completed'])<=int(params['data']['total']),'ProgressBounds')
  if params['topic']=='execution.fact' and params['data']['kind']=='StateCommitted':require(params['data']['published'],'UnpublishedCommit')
class CursorCodec:
 """每 Host 一个 256 位秘密；无逐cursor对象、pin或句柄。测试可以注入固定key。"""
 def __init__(self,secret=None):
  self.secret=secrets.token_bytes(32) if secret is None else secret
  require(type(self.secret) is bytes and len(self.secret)==32,'CursorSecretLength')
 @property
 def cursor_handles(self):return 0
 @staticmethod
 def b64(raw):return base64.urlsafe_b64encode(raw).decode('ascii').rstrip('=')
 @classmethod
 def unb64(cls,text):
  require(re.fullmatch('[A-Za-z0-9_-]+',text) is not None,'CursorInvalid')
  try:raw=base64.b64decode(text+'='*((-len(text))%4),altchars=b'-_',validate=True)
  except (ValueError,base64.binascii.Error):raise ContractError('CursorInvalid') from None
  require(cls.b64(raw)==text,'CursorInvalid');return raw
 def encode(self,payload):
  raw=canonical(payload);mac=hmac.new(self.secret,b'ock.execution.list/1\x00'+raw,hashlib.sha256).digest()
  token='v1.'+self.b64(raw)+'.'+self.b64(mac);require(len(token.encode('ascii'))<=2048,'CursorInvalid');return token
 def decode(self,token,context,now):
  require(type(token) is str and len(token)<=2048 and token.isascii(),'CursorInvalid')
  parts=token.split('.');require(len(parts)==3 and parts[0]=='v1','CursorInvalid');raw=self.unb64(parts[1]);signature=self.unb64(parts[2])
  require(hmac.compare_digest(signature,hmac.new(self.secret,b'ock.execution.list/1\x00'+raw,hashlib.sha256).digest()),'CursorInvalid')
  try:payload=json.loads(raw.decode('ascii'))
  except (ValueError,UnicodeError):raise ContractError('CursorInvalid') from None
  require(type(payload) is dict and raw==canonical(payload),'CursorInvalid')
  expected={'protocol','alg','host','store','restore','caller','view','filter','sort','upper','position','issued','expires'}
  require(set(payload)==expected and payload['protocol']=='ock.execution.list/1' and payload['alg']=='HS256' and payload['sort']=='listing_ordinal_desc','CursorInvalid')
  for key,value in context.items():require(payload.get(key)==value,'CursorInvalid')
  for key in ('upper','position','issued','expires'):
   value=payload[key];require(type(value) is str and re.fullmatch('0|[1-9][0-9]{0,19}',value) and int(value)<=MAX_U64,'CursorInvalid')
  require(1<=int(payload['position'])<=int(payload['upper']) and 0<int(payload['expires'])-int(payload['issued'])<=120 and int(payload['issued'])<=now,'CursorInvalid')
  require(now<int(payload['expires']),'CursorExpired')
  return payload
class Control:
 def __init__(self,**limits):
  self.limits={'connection_limit':8,'principal_limit':32,'host_limit':256,'pending_limit':128,'connection_bytes':1048576,'principal_bytes':4194304,'host_bytes':16777216,'scan_budget':2000,**limits}
  require(all(type(v) is int and v>0 for v in self.limits.values()),'InvalidPolicy')
  self.host=tagged('host-incarnation-1');self.store=tagged('store-1');self.restore='1';self.now=1000;self.codec=CursorCodec()
  self.connections={};self.subscriptions={};self.executions={};self.ordinals=0;self.serial=0;self.auth_versions={};self.denied=set();self.sent_order=[]
 def connect(self,connection,principal):
  require(connection not in self.connections,'ConnectionExists');self.connections[connection]=principal;self.auth_versions.setdefault(principal,1)
 def principal(self,connection):require(connection in self.connections,'SessionInvalid');return self.connections[connection]
 def add(self,execution,owner,phase='Running'):
  require(re.fullmatch('[0-9a-f]{32}',execution) is not None and execution not in self.executions,'ExecutionIdentity')
  require(self.ordinals<MAX_U64,'OrdinalExhausted');self.ordinals+=1
  self.executions[execution]={'execution_ref':{'execution_id':execution},'operation':{'name':'math.compute','version':'1.0.0'},'owner_label':owner,'owner':{'principal_id':tagged(owner)},'phase':phase,'observation_version':'1','ordinal':self.ordinals,'progress':{'completed':'0','total':'100','published':False}}
 def allowed(self,principal,execution):return execution in self.executions and self.executions[execution]['owner_label']==principal and (principal,execution) not in self.denied
 def revoke(self,principal,execution):self.denied.add((principal,execution));self.auth_versions[principal]+=1
 def grant(self,principal,execution):self.denied.discard((principal,execution));self.auth_versions[principal]+=1
 def normalize_owner(self,principal,owner):
  if owner=='self':return {'principal_id':tagged(principal)}
  if isinstance(owner,str):return {'principal_id':tagged(owner)}
  return deepcopy(owner)
 def authorize_owner(self,principal,owner):require(self.normalize_owner(principal,owner)=={'principal_id':tagged(principal)},'NotAvailable')
 def subscribe(self,connection,filter,topics,min_interval_ms=100):
  principal=self.principal(connection);filter=deepcopy(filter)
  if 'owner' in filter:filter['owner']=self.normalize_owner(principal,filter['owner'])
  message={'jsonrpc':'2.0','id':'subscribe','method':'notifications.subscribe','params':{'filter':filter,'topics':topics,'min_interval_ms':min_interval_ms}}
  validate_message(message,'client_to_server')
  if 'executions' in filter:
   require(all(self.allowed(principal,x['execution_id']) for x in filter['executions']),'NotAvailable')
   filter['executions'].sort(key=lambda r:r['execution_id'])
  else:self.authorize_owner(principal,filter['owner'])
  require(sum(s['connection']==connection for s in self.subscriptions.values())<self.limits['connection_limit'],'QuotaExceeded')
  require(sum(s['principal']==principal for s in self.subscriptions.values())<self.limits['principal_limit'] and len(self.subscriptions)<self.limits['host_limit'],'QuotaExceeded')
  self.serial+=1;sid=tagged('subscription-'+str(self.serial));generation=tagged('stream-'+str(self.serial))
  self.subscriptions[sid]={'connection':connection,'principal':principal,'filter':filter,'topics':sorted(topics),'generation':generation,'interval':max(50,min_interval_ms),'acked':False,'queue':[],'sequence':0,'gap':False,'last_progress':{}}
  return sid
 def ack(self,connection,sid):
  s=self.subscriptions[sid];require(s['connection']==connection and not s['acked'],'AckState')
  result={'subscription_id':sid,'stream_generation':s['generation'],'host_incarnation':self.host,'filter':s['filter'],'topics':s['topics'],'effective_min_interval_ms':s['interval'],'first_sequence':'1','replay_supported':False,'budgets':{**BUDGETS,'pending_messages':self.limits['pending_limit'],**{k:self.limits[k] for k in ('connection_bytes','principal_bytes','host_bytes')}}}
  validate_message({'jsonrpc':'2.0','id':'subscribe','result':result},'server_to_client');self.sent_order.append('ack:'+sid);s['acked']=True;return deepcopy(result)
 def unsubscribe(self,connection,sid,generation):
  self.principal(connection);s=self.subscriptions.get(sid)
  if s is None or s['connection']!=connection or s['generation']!=generation:return False
  del self.subscriptions[sid];return True
 def disconnect(self,connection):
  for sid in [sid for sid,s in self.subscriptions.items() if s['connection']==connection]:del self.subscriptions[sid]
  self.connections.pop(connection,None)
 def close_slow(self,connection):self.disconnect(connection)
 def drop_pending(self,sid):self.subscriptions[sid]['queue'].clear();self.subscriptions[sid]['gap']=True
 def project(self,execution):return deepcopy({k:v for k,v in self.executions[execution].items() if k not in ('ordinal','owner_label')})
 def get(self,connection,execution):
  principal=self.principal(connection);require(self.allowed(principal,execution),'NotAvailable');return {'host_incarnation':self.host,**self.project(execution)}
 def update(self,execution,phase=None,progress=None,fact=None):
  e=self.executions[execution]
  require(sum(x is not None for x in (phase,progress,fact))==1,'OneObservationChangeRequired')
  if phase is not None:require(phase in SCHEMAS['common.schema.json']['$defs']['phase']['enum'],'PhaseInvalid')
  if progress is not None:require(type(progress) is int and 0<=progress<=100,'ProgressBounds')
  if fact is not None:
   require(type(fact) is dict and set(fact)=={'kind','reference','published'} and fact['kind'] in ('StateCommitted','EffectResolved','LifecycleResolved') and type(fact['published']) is bool and re.fullmatch('[0-9a-f]{32}',fact['reference']) is not None,'FactInvalid')
   require(fact['kind']!='StateCommitted' or fact['published'],'UnpublishedCommit')
  require(int(e['observation_version'])<MAX_U64,'ObservationVersionExhausted')
  require(phase is None or e['phase']!='Terminal' or phase=='Terminal','TerminalCannotReopen')
  e['observation_version']=str(int(e['observation_version'])+1)
  if phase is not None:e['phase']=phase;topic='execution.phase';data={'phase':phase}
  elif progress is not None:e['progress']={'completed':str(progress),'total':'100','published':False};topic='execution.progress';data=e['progress']
  else:topic='execution.fact';data=fact
  for sid,s in list(self.subscriptions.items()):
   if topic not in s['topics'] or not self.allowed(s['principal'],execution):continue
   if 'executions' in s['filter'] and {'execution_id':execution} not in s['filter']['executions']:continue
   if s['sequence']==MAX_U64:del self.subscriptions[sid];continue
   s['sequence']+=1
   event={'jsonrpc':'2.0','method':'notifications.event','params':{'subscription_id':sid,'stream_generation':s['generation'],'host_incarnation':self.host,'sequence':str(s['sequence']),'topic':topic,'execution_ref':{'execution_id':execution},'observation_version':e['observation_version'],'gap':False,'data':deepcopy(data)}}
   validate_message(event,'server_to_client')
   key=(execution,topic)
   if topic=='execution.progress':
    old=[v for v in s['queue'] if (v['params']['execution_ref']['execution_id'],v['params']['topic'])==key]
    if old:s['queue']=[v for v in s['queue'] if v not in old];s['gap']=True
   if len(s['queue'])>=self.limits['pending_limit']:s['gap']=True;continue
   size=len(canonical(event))+EVENT_METADATA_BYTES
   def memory(predicate):return sum(len(canonical(v))+EVENT_METADATA_BYTES for sub in self.subscriptions.values() if predicate(sub) for v in sub['queue'])
   if any(memory(predicate)+size>self.limits[budget] for budget,predicate in [('connection_bytes',lambda x:x['connection']==s['connection']),('principal_bytes',lambda x:x['principal']==s['principal']),('host_bytes',lambda x:True)]):s['gap']=True;continue
   s['queue'].append(event)
 def drain(self,connection):
  self.principal(connection);sent=[]
  for sid,s in list(self.subscriptions.items()):
   if s['connection']!=connection or not s['acked']:continue
   remaining=[]
   for event in s['queue']:
    params=event['params'];execution=params['execution_ref']['execution_id']
    if not self.allowed(s['principal'],execution):s['gap']=True;continue
    # 单模型步代表发送授权/开始发送的短仲裁；已开始发送的字节不能撤回。
    if params['topic']=='execution.progress':
     last=s['last_progress'].get(execution)
     if last is not None and (self.now-last)*1000<s['interval']:
      # 后续 phase/fact 不受 progress interval 强制延迟；可丢低优先级提示并标 gap。
      later_control=any(int(v['params']['sequence'])>int(params['sequence']) and v['params']['topic']!='execution.progress' for v in s['queue'])
      if later_control:s['gap']=True
      else:remaining.append(event)
      continue
     s['last_progress'][execution]=self.now
    # 有被限速的早序号时不能让后序号先越过；phase/fact仍可立即排队。
    if remaining:remaining.append(event);continue
    if s['gap']:params['gap']=True;s['gap']=False
    sent.append(deepcopy(event));self.sent_order.append('event:'+sid)
   s['queue']=remaining
  return sent
 def cursor_context(self,connection,owner,phase_set):
  principal=self.principal(connection)
  return {'host':self.host,'store':self.store,'restore':self.restore,'caller':{'principal_id':tagged(principal)},'view':str(self.auth_versions[principal]),'filter':{'owner':self.normalize_owner(principal,owner),'phase_set':phase_set}}
 def list(self,connection,owner='self',phase_set='nonterminal',page_size=50,cursor=None):
  principal=self.principal(connection);wire={'owner':self.normalize_owner(principal,owner),'phase_set':phase_set,'page_size':page_size}
  if cursor is not None:wire['cursor']=cursor
  try:validate_message({'jsonrpc':'2.0','id':'list','method':'execution.list','params':wire},'client_to_server')
  except ContractError:
   if cursor is not None:raise ContractError('CursorInvalid') from None
   raise
  context=self.cursor_context(connection,owner,phase_set)
  if cursor is None:
   upper=self.ordinals;position=upper+1;issued=self.now;expires=self.now+120
  else:
   payload=self.codec.decode(cursor,context,self.now);upper=int(payload['upper']);position=int(payload['position']);issued=int(payload['issued']);expires=int(payload['expires'])
  self.authorize_owner(principal,owner)
  candidates=sorted([e for e in self.executions.values() if e['owner_label']==principal and e['ordinal']<=upper and e['ordinal']<position],key=lambda e:e['ordinal'],reverse=True)
  items=[];scanned=0;last=position
  for e in candidates:
   if scanned>=self.limits['scan_budget'] or len(items)>=page_size:break
   scanned+=1;last=e['ordinal'];execution=e['execution_ref']['execution_id']
   if self.allowed(principal,execution) and (phase_set=='all' or (e['phase']=='Terminal')==(phase_set=='terminal')):items.append(self.project(execution))
  result={'items':items,'consistency':'live_keyset','host_incarnation':self.host,'retention_scope':'managed_active_and_retained_terminal'}
  if scanned<len(candidates):
   require(last<position,'CursorInvalid')
   payload={'protocol':'ock.execution.list/1','alg':'HS256','sort':'listing_ordinal_desc',**context,'upper':str(upper),'position':str(last),'issued':str(issued),'expires':str(expires)}
   result['next_cursor']=self.codec.encode(payload)
  validate_message({'jsonrpc':'2.0','id':'list','result':result},'server_to_client');return result
class Client:
 """subscribe 成功后的客户端合并策略；需要准确终态始终重新 get/wait。"""
 def __init__(self):self.views={};self.host=None;self.needs_snapshot=True;self.sequences={};self.streams={};self.ack_host=None
 def establish(self,ack):
  host=ack['host_incarnation']
  if self.host is not None and self.host!=host:self.views.clear();self.sequences.clear();self.streams.clear()
  self.ack_host=host;self.host=host
  self.streams[ack['subscription_id']]=ack['stream_generation'];self.needs_snapshot=True
 def local_drop(self):self.needs_snapshot=True
 def accept_snapshot(self,snapshot):
  host=snapshot['host_incarnation'];execution=snapshot['execution_ref']['execution_id']
  if self.ack_host is not None and host!=self.ack_host:self.needs_snapshot=True;return False
  if self.host is not None and host!=self.host:self.views.clear();self.sequences.clear()
  self.host=host;old=self.views.get(execution)
  if old and old['phase']=='Terminal' and snapshot['phase']!='Terminal':self.needs_snapshot=True;return False
  if old and int(old['observation_version'])>=int(snapshot['observation_version']):return False
  self.views[execution]=deepcopy(snapshot);self.needs_snapshot=False;return True
 def accept_event(self,event):
  p=event['params']
  if p['host_incarnation']!=self.host:self.needs_snapshot=True;return False
  if self.streams and self.streams.get(p['subscription_id'])!=p['stream_generation']:return False
  key=(p['subscription_id'],p['stream_generation']);sequence=int(p['sequence']);previous=self.sequences.get(key,0)
  if sequence<=previous:return False
  if p['gap'] or sequence!=previous+1:self.needs_snapshot=True
  self.sequences[key]=sequence;execution=p['execution_ref']['execution_id'];old=self.views.get(execution)
  if old is None:self.needs_snapshot=True;return False
  if int(p['observation_version'])<=int(old['observation_version']):return False
  if old['phase']=='Terminal' and p['topic']=='execution.phase' and p['data']['phase']!='Terminal':self.needs_snapshot=True;return False
  old['observation_version']=p['observation_version']
  if p['topic']=='execution.phase':old['phase']=p['data']['phase']
  elif p['topic']=='execution.progress':old['progress']=deepcopy(p['data'])
  else:self.needs_snapshot=True
  return True

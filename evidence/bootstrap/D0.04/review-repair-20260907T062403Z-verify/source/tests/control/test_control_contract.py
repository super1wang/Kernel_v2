"""D0.04-b 确定性原子模型步骤，不声明真实RPC/并发授权通过。"""
from copy import deepcopy
import json
import hashlib
from pathlib import Path
import unittest
from control_model import ContractError, Control, Client, CursorCodec, validate_message, MAX_U64
ROOT=Path(__file__).resolve().parents[2]
def eid(label):return hashlib.sha256(label.encode()).hexdigest()[:32]
def setup(**limits):
 c=Control(**limits);c.connect('c1','alice');c.connect('c2','alice');c.connect('bob','bob');c.add(eid('e1'),'alice');return c
def sub(c,conn='c1',**kwargs):return c.subscribe(conn,{'executions':[{'execution_id':eid('e1')}]},['execution.phase','execution.progress','execution.fact'],**kwargs)
class ControlContracts(unittest.TestCase):
 def test_T04_control_message_golden_and_directions(self):
  rows=json.loads((ROOT/'tests/control/golden/messages.json').read_text())
  for r in rows:
   with self.subTest(r=r['name']):
    if r['valid']:validate_message(r['message'],r['direction'])
    else:
     with self.assertRaises(ContractError):validate_message(r['message'],r['direction'])
 def test_T19_control_terminal_before_subscribe(self):
  c=setup();c.update(eid('e1'),phase='Terminal');s=sub(c);c.ack('c1',s)
  self.assertEqual(c.get('c1',eid('e1'))['phase'],'Terminal');self.assertEqual(c.drain('c1'),[])
 def test_T19_control_terminal_during_establishment(self):
  c=setup();s=sub(c);c.update(eid('e1'),phase='Terminal');self.assertEqual(c.drain('c1'),[])
  c.ack('c1',s);frames=c.drain('c1');self.assertEqual(frames[0]['params']['data']['phase'],'Terminal')
  self.assertEqual(c.sent_order[:2],['ack:'+s,'event:'+s])
 def test_T19_control_ack_before_events(self):
  c=setup();s=sub(c)
  for i in range(3):c.update(eid('e1'),progress=i)
  self.assertEqual(c.drain('c1'),[]);c.ack('c1',s)
  events=c.drain('c1');self.assertTrue(events[0]['params']['gap']);self.assertEqual(events[0]['params']['sequence'],'3')
 def test_T19_control_client_stale_snapshot_and_event(self):
  c=setup();s=sub(c);ack=c.ack('c1',s);client=Client();client.establish(ack);client.accept_snapshot(c.get('c1',eid('e1')));c.update(eid('e1'),phase='Finalizing')
  event=c.drain('c1')[0];self.assertTrue(client.accept_event(event));self.assertFalse(client.accept_snapshot(dict(c.get('c1',eid('e1')),observation_version='1')))
  self.assertFalse(client.accept_event(event));self.assertEqual(client.views[eid('e1')]['phase'],'Finalizing')
 def test_T19_control_drop_without_later_notification(self):
  c=setup(pending_limit=1);s=sub(c);c.ack('c1',s);c.update(eid('e1'),phase='Finalizing');c.update(eid('e1'),phase='Terminal');c.drop_pending(s)
  self.assertEqual(c.drain('c1'),[]);self.assertEqual(c.get('c1',eid('e1'))['phase'],'Terminal')
  client=Client();client.local_drop();self.assertTrue(client.needs_snapshot)
 def test_T19_control_coalescing_preserves_sequence(self):
  c=setup();s=sub(c);c.ack('c1',s);c.update(eid('e1'),progress=1);c.update(eid('e1'),phase='Finalizing');c.update(eid('e1'),progress=2)
  events=c.drain('c1');self.assertEqual([e['params']['sequence'] for e in events],['2','3']);self.assertTrue(events[0]['params']['gap'])
 def test_T19_control_unsubscribe_connection_generation_inflight(self):
  c=setup();s=sub(c);generation=c.subscriptions[s]['generation'];c.ack('c1',s);c.update(eid('e1'),phase='Terminal')
  self.assertFalse(c.unsubscribe('c2',s,generation));self.assertFalse(c.unsubscribe('c1',s,'old'))
  self.assertTrue(c.unsubscribe('c1',s,generation));self.assertFalse(c.unsubscribe('c1',s,generation));self.assertEqual(c.drain('c1'),[])
  self.assertEqual(c.get('c1',eid('e1'))['phase'],'Terminal')
 def test_T19_control_disconnect_and_reconnect(self):
  c=setup();s=sub(c);old=c.subscriptions[s]['generation'];c.disconnect('c1');self.assertNotIn(s,c.subscriptions)
  c.connect('c1','alice');new=sub(c);self.assertNotEqual(c.subscriptions[new]['generation'],old);self.assertEqual(c.get('c1',eid('e1'))['phase'],'Running')
  client=Client();client.establish(c.ack('c1',new));client.accept_snapshot(c.get('c1',eid('e1')));self.assertFalse(client.accept_event({'params':{'host_incarnation':'new'}}));self.assertTrue(client.needs_snapshot)
 def test_T20_control_authorization_and_no_enumeration(self):
  c=setup();c.add(eid('hidden'),'bob')
  for identity in [eid('hidden'),eid('absent')]:
   with self.assertRaisesRegex(ContractError,'NotAvailable'):c.subscribe('c1',{'executions':[{'execution_id':identity}]},['execution.phase'])
  self.assertEqual(len(c.subscriptions),0)
  with self.assertRaisesRegex(ContractError,'NotAvailable'):c.subscribe('c1',{'owner':'bob'},['execution.phase'])
  with self.assertRaisesRegex(ContractError,'NotAvailable'):c.list('c1',owner='bob')
 def test_T20_control_revoke_queued_and_send_arbitration(self):
  c=setup();s=sub(c);c.ack('c1',s);c.update(eid('e1'),phase='Finalizing');c.revoke('alice',eid('e1'));self.assertEqual(c.drain('c1'),[])
  c.grant('alice',eid('e1'));c.update(eid('e1'),phase='Terminal');sent=c.drain('c1');self.assertEqual(len(sent),1);c.revoke('alice',eid('e1'));self.assertEqual(sent[0]['params']['data']['phase'],'Terminal')
 def test_T22_control_quotas_interval_and_slow_reader(self):
  c=setup(connection_limit=1);s=sub(c,min_interval_ms=1);self.assertEqual(c.subscriptions[s]['interval'],50)
  with self.assertRaisesRegex(ContractError,'QuotaExceeded'):sub(c)
  c.ack('c1',s);c.update(eid('e1'),phase='Terminal');c.close_slow('c1');self.assertNotIn(s,c.subscriptions);self.assertEqual(c.get('c2',eid('e1'))['phase'],'Terminal')
  for kwargs in [dict(principal_limit=1),dict(host_limit=1)]:
   host=setup(**kwargs);sub(host)
   with self.assertRaisesRegex(ContractError,'QuotaExceeded'):sub(host,'c2')
 def test_T22_control_queue_memory_and_counter_limits(self):
  for budget in ['connection_bytes','principal_bytes','host_bytes']:
   c=setup(**{budget:1});s=sub(c);c.ack('c1',s);c.update(eid('e1'),phase='Terminal');self.assertEqual(c.drain('c1'),[]);self.assertEqual(c.get('c1',eid('e1'))['phase'],'Terminal')
  c=setup();s=sub(c);c.subscriptions[s]['sequence']=MAX_U64;c.update(eid('e1'),phase='Terminal');self.assertNotIn(s,c.subscriptions)
 def test_T19_control_finalizing_and_projection(self):
  c=setup();c.update(eid('e1'),phase='Finalizing');items=c.list('c1')['items'];self.assertEqual(items[0]['phase'],'Finalizing');self.assertNotIn('result',items[0]);self.assertNotIn('args',items[0])
  c.update(eid('e1'),phase='Terminal');self.assertEqual(c.list('c1')['items'],[]);self.assertEqual(len(c.list('c1',phase_set='terminal')['items']),1)
 def test_T19_control_cursor_golden_mac(self):
  row=json.loads((ROOT/'tests/control/golden/cursor.json').read_text());codec=CursorCodec(bytes.fromhex(row['secret_hex']))
  self.assertEqual(codec.encode(row['payload']),row['token']);self.assertEqual(codec.decode(row['token'],row['context'],now=1000),row['payload']);self.assertEqual(codec.cursor_handles,0)
 def test_T20_control_cursor_tamper_unknown_and_oversize(self):
  c=setup();[c.add(eid('e'+str(i)),'alice') for i in range(2,5)];token=c.list('c1',page_size=1)['next_cursor']
  for bad in [token[:-1]+('A' if token[-1]!='A' else 'B'),'x'*2049,'v2.'+token.split('.',1)[1],token+'=']:
   with self.assertRaisesRegex(ContractError,'CursorInvalid'):c.list('c1',page_size=1,cursor=bad)
 def test_T20_control_cursor_identity_filter_and_revocation(self):
  c=setup();c.add(eid('e2'),'alice');token=c.list('c1',page_size=1)['next_cursor']
  for conn,kwargs in [('bob',{}),('c1',{'phase_set':'all'})]:
   with self.assertRaisesRegex(ContractError,'CursorInvalid'):c.list(conn,page_size=1,cursor=token,**kwargs)
  c.revoke('alice',eid('e1'))
  with self.assertRaisesRegex(ContractError,'CursorInvalid'):c.list('c1',page_size=1,cursor=token)
 def test_T19_control_cursor_expiry_host_restore_and_no_restart(self):
  for mutation in ['expiry','host','restore','store']:
   c=setup();c.add(eid('e2'),'alice');token=c.list('c1',page_size=1)['next_cursor']
   if mutation=='expiry':c.now+=120
   elif mutation=='host':c.host='host-new'
   elif mutation=='restore':c.restore='2'
   else:c.store='other-store'
   with self.assertRaisesRegex(ContractError,'CursorExpired|CursorInvalid'):c.list('c1',page_size=1,cursor=token)
 def test_T19_control_list_live_keyset_add_delete_change(self):
  c=setup();[c.add(eid('e'+str(i)),'alice') for i in range(2,6)];first=c.list('c1',page_size=2);self.assertEqual([x['execution_ref']['execution_id'] for x in first['items']],[eid('e5'),eid('e4')])
  c.add(eid('e6'),'alice');del c.executions[eid('e3')];c.update(eid('e2'),phase='Terminal');second=c.list('c1',page_size=2,cursor=first['next_cursor'])
  self.assertEqual([x['execution_ref']['execution_id'] for x in second['items']],[eid('e1')]);self.assertEqual(c.list('c1')['items'][0]['execution_ref']['execution_id'],eid('e6'))
 def test_T22_control_list_empty_page_advances_and_no_progress_rejected(self):
  c=setup(scan_budget=1);c.add(eid('e2'),'alice');c.update(eid('e2'),phase='Terminal');first=c.list('c1',page_size=1);self.assertEqual(first['items'],[])
  second=c.list('c1',page_size=1,cursor=first['next_cursor']);self.assertEqual(second['items'][0]['execution_ref']['execution_id'],eid('e1'))
  payload=c.codec.decode(first['next_cursor'],c.cursor_context('c1','self','nonterminal'),now=c.now);payload['position']=str(int(payload['upper'])+1)
  with self.assertRaisesRegex(ContractError,'CursorInvalid'):c.list('c1',cursor=c.codec.encode(payload))
 def test_T22_control_list_cursor_zero_handles_and_bounded_frame(self):
  c=setup();c.add(eid('e2'),'alice');tokens=[c.list('c1',page_size=1)['next_cursor'] for _ in range(20)];self.assertTrue(all(len(x)<=2048 for x in tokens));self.assertEqual(c.codec.cursor_handles,0)
  with self.assertRaises(ContractError):c.list('c1',page_size=201)
  with self.assertRaises(ContractError):c.list('c1',page_size=0)

 def test_T19_control_phase_not_delayed_by_progress_interval(self):
  c=setup();s=sub(c);c.ack('c1',s);c.update(eid('e1'),progress=1);self.assertEqual(len(c.drain('c1')),1)
  c.update(eid('e1'),progress=2);c.update(eid('e1'),phase='Terminal');events=c.drain('c1')
  self.assertEqual(events[-1]['params']['data']['phase'],'Terminal');self.assertTrue(events[-1]['params']['gap'])
 def test_T20_control_failed_subscribe_and_hidden_sequence(self):
  c=setup();c.add(eid('hidden'),'bob')
  with self.assertRaisesRegex(ContractError,'NotAvailable'):c.subscribe('c1',{'executions':[{'execution_id':eid('e1')},{'execution_id':eid('hidden')}]},['execution.phase'])
  self.assertEqual(c.subscriptions,{})
  s=sub(c);c.ack('c1',s);c.update(eid('hidden'),phase='Terminal');c.update(eid('e1'),phase='Terminal');self.assertEqual(c.drain('c1')[0]['params']['sequence'],'1')
 def test_T20_control_cursor_algorithm_and_authenticated_nonprogress(self):
  c=setup();c.add(eid('e2'),'alice');token=c.list('c1',page_size=1)['next_cursor'];context=c.cursor_context('c1','self','nonterminal');payload=c.codec.decode(token,context,c.now)
  for mutation in [dict(alg='none'),dict(protocol='ock.execution.list/2'),dict(position='0'),dict(position=str(int(payload['upper'])+1)),dict(expires=str(c.now+121)),dict(issued=str(c.now+1))]:
   with self.assertRaisesRegex(ContractError,'CursorInvalid'):c.codec.decode(c.codec.encode(dict(payload,**mutation)),context,c.now)
 def test_T22_control_effective_budgets_and_session_invalidation(self):
  c=setup(pending_limit=1,connection_bytes=500);s=sub(c);ack=c.ack('c1',s);self.assertEqual(ack['budgets']['pending_messages'],1);self.assertEqual(ack['budgets']['connection_bytes'],500)
  c.disconnect('c1')
  with self.assertRaisesRegex(ContractError,'SessionInvalid'):c.get('c1',eid('e1'))
  self.assertEqual(c.get('c2',eid('e1'))['phase'],'Running')
 def test_T19_control_client_old_stream_rejected(self):
  c=setup();s=sub(c);ack=c.ack('c1',s);client=Client();client.establish(ack);client.accept_snapshot(c.get('c1',eid('e1')))
  c.update(eid('e1'),phase='Finalizing');event=c.drain('c1')[0];bad=deepcopy(event);bad['params']['stream_generation']='0'*32
  self.assertFalse(client.accept_event(bad));self.assertTrue(client.accept_event(event))


 def test_T19_control_invalid_projection_preserves_state(self):
  c=setup();before=c.get('c1',eid('e1'))
  for change in [dict(phase='InvalidPhase'),dict(progress=101)]:
   with self.assertRaises(ContractError):c.update(eid('e1'),**change)
   self.assertEqual(c.get('c1',eid('e1')),before)

 def test_T19_control_client_terminal_cannot_reopen(self):
  c=setup();s=sub(c);ack=c.ack('c1',s);client=Client();client.establish(ack)
  c.update(eid('e1'),phase='Terminal');snapshot=c.get('c1',eid('e1'));client.accept_snapshot(snapshot)
  invalid=deepcopy(snapshot);invalid.update(phase='Running',observation_version='99')
  self.assertFalse(client.accept_snapshot(invalid));self.assertEqual(client.views[eid('e1')]['phase'],'Terminal')
  event=c.drain('c1')[0];event['params'].update(observation_version='100',data={'phase':'Running'})
  self.assertFalse(client.accept_event(event));self.assertEqual(client.views[eid('e1')]['phase'],'Terminal')
 def test_T19_control_client_old_host_snapshot_rejected(self):
  c=setup();s=sub(c);ack=c.ack('c1',s);client=Client();client.establish(ack)
  old=c.get('c1',eid('e1'));client.accept_snapshot(old)
  new_ack=deepcopy(ack);new_ack['host_incarnation']=eid('host-2');new_ack['stream_generation']=eid('stream-2');client.establish(new_ack)
  self.assertFalse(client.accept_snapshot(old));self.assertTrue(client.needs_snapshot)
  fresh=deepcopy(old);fresh['host_incarnation']=eid('host-2');self.assertTrue(client.accept_snapshot(fresh))


 def test_T19_control_full_snapshot_completes_partial_same_version(self):
  for progress_only in (False,True):
   c=setup();s=c.subscribe('c1',{'executions':[{'execution_id':eid('e1')}]},['execution.progress']) if progress_only else sub(c)
   client=Client();client.establish(c.ack('c1',s));client.accept_snapshot(c.get('c1',eid('e1')))
   c.update(eid('e1'),phase='Finalizing')
   if not progress_only:c.drop_pending(s)
   c.update(eid('e1'),progress=20);event=c.drain('c1')[0];self.assertEqual(event['params']['gap'],not progress_only)
   self.assertTrue(client.accept_event(event));self.assertEqual(client.views[eid('e1')]['phase'],'Running')
   fresh=c.get('c1',eid('e1'));self.assertEqual(fresh['observation_version'],client.views[eid('e1')]['observation_version'])
   self.assertTrue(client.accept_snapshot(fresh));self.assertEqual(client.views[eid('e1')]['phase'],'Finalizing');self.assertFalse(client.needs_snapshot)
   stale=deepcopy(fresh);stale.update(observation_version='2',phase='Running');self.assertFalse(client.accept_snapshot(stale))
   self.assertFalse(client.accept_snapshot(fresh))


 def test_T19_control_client_retirement_rejects_inflight_streams(self):
  c=setup();s=sub(c);ack=c.ack('c1',s);initial=c.get('c1',eid('e1'));client=Client();client.establish(ack);client.accept_snapshot(initial)
  c.update(eid('e1'),phase='Finalizing');inflight=c.drain('c1')[0]
  unconfirmed=Client();unconfirmed.accept_snapshot(initial);self.assertFalse(unconfirmed.accept_event(inflight))
  self.assertFalse(client.retire(s,'0'*32));self.assertIn(s,client.streams)
  self.assertTrue(client.retire(s,ack['stream_generation']));self.assertFalse(client.retire(s,ack['stream_generation']))
  self.assertTrue(c.unsubscribe('c1',s,ack['stream_generation']));self.assertFalse(client.accept_event(inflight));self.assertEqual(client.views[eid('e1')]['phase'],'Running')
  fresh=sub(c);client.establish(c.ack('c1',fresh));self.assertFalse(client.accept_event(inflight))
  c.update(eid('e1'),phase='Terminal');current=c.drain('c1')[0];self.assertTrue(client.accept_event(current));self.assertEqual(client.views[eid('e1')]['phase'],'Terminal')
  client.disconnect();self.assertEqual(client.streams,{});self.assertFalse(client.accept_event(current));self.assertTrue(client.needs_snapshot)

if __name__=='__main__':unittest.main()


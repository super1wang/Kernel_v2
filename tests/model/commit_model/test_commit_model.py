"""D0.05 固定提交/许可反例；模型步是确定性串行化，不模拟真实线程。"""
from copy import deepcopy
from dataclasses import replace
import itertools
import unittest

from commit_model.model import (ContractError, Binding, Policy, PublishedState,
                                CommitDomain, Effect, Device, LockDiscipline)


class CommitContracts(unittest.TestCase):
    def ready(self, durable=True, capacity=1):
        domain = CommitDomain(durable=durable, capacity=capacity)
        policy = Policy()
        binding = Binding('alice', 'edit@1', 'domain-1', 1, 1, 100)
        permit = policy.issue(binding)
        attempt = domain.prepare('commit-1', 7, binding)
        return domain, policy, binding, permit, attempt

    def claimed(self, durable=True):
        d, p, b, permit, a = self.ready(durable)
        d.claim(a, p, permit, now=10)
        return d, p, b, permit, a

    def test_T07_permit_cancel_claim_orders(self):
        for order in itertools.permutations(('cancel', 'claim')):
            d, p, b, permit, a = self.ready()
            for action in order:
                if action == 'cancel': d.cancel(a)
                elif order[0] == 'cancel':
                    with self.assertRaises(ContractError): d.claim(a, p, permit, now=10)
                else: d.claim(a, p, permit, now=10)
            self.assertEqual(a.phase, 'Cancelled' if order[0] == 'cancel' else 'CommitClaimed')
            self.assertEqual(p.is_consumed(permit), order[0] == 'claim')
            self.assertIsNone(d.outcome(a) if order[0] == 'claim' else None)

    def test_T07_permit_revoke_claim_orders(self):
        for order in itertools.permutations(('revoke', 'claim')):
            d, p, b, permit, a = self.ready()
            for action in order:
                if action == 'revoke': p.revoke('alice')
                elif order[0] == 'revoke':
                    with self.assertRaises(ContractError): d.claim(a, p, permit, now=10)
                else: d.claim(a, p, permit, now=10)
            self.assertEqual(p.is_consumed(permit), order[0] == 'claim')
            if order[0] == 'claim':
                d.submit(a); d.writer_step(); d.deliver(); d.publish(a)
                self.assertEqual(d.outcome(a)['kind'], 'StateCommitted')

    def test_T07_permit_binding_expiry_single_use(self):
        for field, value in [('caller', 'mallory'), ('operation', 'edit@2'),
                             ('target', 'domain-2'), ('permission_generation', 2),
                             ('lifecycle_generation', 2), ('deadline', 101)]:
            d, p, b, permit, a = self.ready()
            a.binding = replace(b, **{field: value})
            with self.assertRaises(ContractError): d.claim(a, p, permit, now=10)
            self.assertFalse(p.is_consumed(permit))
        d, p, b, permit, a = self.ready()
        with self.assertRaises(ContractError): d.claim(a, p, permit, now=100)
        self.assertFalse(p.is_consumed(permit))
        d.claim(a, p, permit, now=99)
        with self.assertRaises(ContractError): p.consume(permit, b, now=99)
        with self.assertRaises(ContractError): p.consume('forged', b, now=99)

    def test_T07_permit_leases_do_not_authorize(self):
        for activity, resource in [(False, True), (True, False)]:
            d, p, b, permit, a = self.ready()
            with self.assertRaises(ContractError):
                d.claim(a, p, permit, now=10, activity_lease=activity, resource_lease=resource)
            self.assertFalse(p.is_consumed(permit))
        d, p, b, permit, a = self.ready()
        with self.assertRaises(ContractError): d.claim(a, p, 'forged', now=10)
        self.assertIsNone(d.reservation)

    def test_T07_commit_capacity_reserved_before_claim(self):
        d, p, b, permit, a = self.ready(capacity=0)
        with self.assertRaises(ContractError): d.claim(a, p, permit, now=10)
        self.assertFalse(p.is_consumed(permit))
        self.assertIsNone(d.reservation)

    def test_T07_commit_lifecycle_revision_orders(self):
        d, p, b, permit, a = self.ready()
        d.close()
        with self.assertRaises(ContractError): d.claim(a, p, permit, now=10)
        self.assertFalse(p.is_consumed(permit))
        d, p, b, permit, a = self.claimed()
        with self.assertRaises(ContractError): d.close()
        self.assertEqual(a.phase, 'CommitClaimed')
        d.submit(a); d.writer_step(); d.deliver(); d.publish(a)
        d.close()
        self.assertEqual(d.outcome(a)['kind'], 'StateCommitted')

    def test_T14_commit_reservation_serializes_and_rejects_stale_base(self):
        d, p, b, permit, a = self.ready()
        other = d.prepare('commit-2', 9, b)
        other_permit = p.issue(b)
        d.claim(a, p, permit, now=10)
        with self.assertRaises(ContractError): d.claim(other, p, other_permit, now=10)
        d.submit(a); d.writer_step(); d.deliver()
        with self.assertRaises(ContractError): d.claim(other, p, other_permit, now=10)
        d.publish(a)
        with self.assertRaises(ContractError): d.claim(other, p, other_permit, now=10)
        self.assertFalse(p.is_consumed(other_permit))
        self.assertEqual(d.snapshot().value, 7)

    def test_T14_commit_wait_and_lock_order(self):
        locks = LockDiscipline()
        with locks.hold('domain'):
            with locks.hold('policy'): pass
            for action in (locks.wait_db, locks.business, locks.notify, locks.destroy_old):
                with self.assertRaises(ContractError): action()
        with locks.hold('policy'):
            with self.assertRaises(ContractError):
                with locks.hold('domain'): pass
        with locks.hold('writer'):
            with self.assertRaises(ContractError): locks.sync_domain()
        d, p, b, permit, a = self.claimed()
        d.submit(a)
        self.assertEqual(d.locks.held, [])
        self.assertFalse(a.worker_held)
        self.assertIsNotNone(d.reservation)

    def test_T14_commit_all_transaction_crash_windows(self):
        for cut in CommitDomain.TX_STEPS:
            d, p, b, permit, a = self.claimed()
            original = deepcopy(d.disk)
            d.submit(a); d.writer_step(crash_at=cut)
            self.assertEqual(d.disk, original, cut)
            d.deliver()
            self.assertEqual(d.outcome(a)['kind'], 'FailedBeforeApply')
            self.assertEqual(d.outcome(a)['failure_phase'], 'Running')
            self.assertEqual(a.phase, 'KnownNotCommitted')
            self.assertIsNone(d.reservation)
            self.assertEqual(d.snapshot().revision, 0)
        d, p, b, permit, a = self.claimed()
        d.submit(a); d.writer_step(); d.deliver()
        d.check_disk()
        self.assertEqual(set(d.disk), {'state', 'ledger', 'receipts', 'audit', 'outbox'})
        self.assertEqual(d.disk['ledger']['commit-1']['revision'], 1)
        self.assertEqual(d.disk['receipts']['commit-1']['kind'], 'StateCommitted')

    def test_T14_commit_permit_and_durable_are_not_published(self):
        d, p, b, permit, a = self.claimed()
        self.assertIsNone(d.outcome(a))
        self.assertEqual(d.snapshot().revision, 0)
        d.submit(a); d.writer_step(); d.deliver()
        self.assertEqual(a.phase, 'DurableCommitted')
        self.assertIsNone(d.outcome(a))
        self.assertEqual(d.dispatch_outbox(), [])
        self.assertEqual(d.snapshot().revision, 0)
        d.publish(a)
        self.assertEqual(d.outcome(a)['kind'], 'StateCommitted')
        self.assertEqual(d.dispatch_outbox(), ['commit-1'])
        self.assertEqual(d.dispatch_outbox(), [])

    def test_T14_commit_publication_is_one_owned_record(self):
        d, p, b, permit, a = self.claimed()
        old = d.snapshot()
        d.submit(a); d.writer_step(); d.deliver(); d.publish(a)
        new = d.snapshot()
        self.assertEqual((old.value, old.revision, old.history), (0, 0, ()))
        self.assertEqual((new.value, new.revision, new.history), (7, 1, ('commit-1',)))
        with self.assertRaises(ContractError):
            d.validate_publication(replace(new, history=old.history))
        self.assertEqual(d.retired, [old])
        self.assertEqual(d.notifications, ['commit-1'])

    def test_T14_commit_duplicate_and_late_callback(self):
        d, p, b, permit, a = self.claimed()
        token = d.submit(a); d.writer_step(); callback = d.deliver(); d.publish(a)
        newer = d.prepare('commit-2', 8, b)
        d.claim(newer, p, p.issue(b), now=10); d.submit(newer)
        with self.assertRaises(ContractError): d.deliver(callback)
        with self.assertRaises(ContractError): d.deliver(replace(callback, reservation_id=token + 900))
        self.assertEqual(d.reservation.attempt, 'commit-2')
        self.assertEqual(d.snapshot().value, 7)
        d.writer_step(); d.deliver(); d.publish(newer)
        self.assertEqual(d.snapshot().value, 8)

    def test_T16_commit_unknown_result_reconciles_actual_storage(self):
        for cut, applied in [('before_commit_unconfirmed', False), ('after_commit', True)]:
            d, p, b, permit, a = self.claimed()
            d.submit(a); d.writer_step(crash_at=cut); d.deliver()
            self.assertEqual(d.outcome(a)['kind'], 'Indeterminate')
            self.assertTrue(d.isolated)
            self.assertEqual(d.snapshot().revision, 0)
            with self.assertRaises(ContractError): d.prepare('next', 8, b)
            d.recover()
            self.assertEqual(d.snapshot().revision, int(applied))
            self.assertEqual(d.outcome(a)['kind'], 'StateCommitted' if applied else 'FailedBeforeApply')
            self.assertEqual(d.handler_runs, 1)
            self.assertEqual(len([f for f in a.facts if f['kind'] == 'UnknownFact']), 1)
            self.assertEqual(len([f for f in a.facts if f['kind'] == 'ResolutionRecord']), 1)

    def test_T16_commit_publish_failure_preserves_ledger(self):
        d, p, b, permit, a = self.claimed()
        d.submit(a); d.writer_step(); d.deliver(); d.publish(a, fail=True)
        self.assertTrue(d.isolated)
        self.assertEqual(a.phase, 'DurableCommitted')
        self.assertIsNone(d.outcome(a))
        self.assertIn('commit-1', d.disk['ledger'])
        self.assertEqual(d.dispatch_outbox(), [])
        d.recover()
        self.assertEqual(d.outcome(a)['kind'], 'StateCommitted')
        self.assertEqual(d.dispatch_outbox(), ['commit-1'])
        self.assertEqual(d.handler_runs, 1)

    def test_T16_commit_recovery_validates_before_opening(self):
        for field in ['ledger', 'receipts', 'audit', 'outbox']:
            d, p, b, permit, a = self.claimed()
            d.submit(a); d.writer_step(); d.deliver()
            d.disk[field].clear()
            with self.assertRaises(ContractError): d.recover()
            self.assertTrue(d.isolated)
            self.assertFalse(d.business_open)
            self.assertEqual(d.snapshot().revision, 0)
            self.assertEqual(d.dispatch_outbox(), [])

    def test_T07_commit_memory_commit_and_empty_delta(self):
        d, p, b, permit, a = self.ready(durable=False)
        a = d.prepare('empty', 0, b)
        d.claim(a, p, permit, now=10)
        self.assertIsNone(d.outcome(a))
        d.publish(a)
        self.assertEqual(d.snapshot().revision, 1)
        self.assertEqual(d.snapshot().history, ('empty',))
        self.assertEqual(d.outcome(a)['evidence'], 'Volatile')
        self.assertEqual(d.disk['ledger'], {})
        self.assertEqual(d.cancel(a), 'AlreadyClaimed')
        self.assertEqual(d.outcome(a)['kind'], 'StateCommitted')

    def test_T07_commit_materials_prepared_before_consumption(self):
        d, p, b, permit, a = self.ready()
        a.result_valid = False
        with self.assertRaises(ContractError): d.claim(a, p, permit, now=10)
        self.assertFalse(p.is_consumed(permit))
        a.result_valid = True
        d.claim(a, p, permit, now=10)
        with self.assertRaises(ContractError): d.run_validator(a)
        d.submit(a); d.writer_step(); d.deliver(); d.publish(a)
        d.finalize(a)
        self.assertEqual(a.phase, 'Finalized')
        self.assertEqual(a.completion_count, 1)
        with self.assertRaises(ContractError): d.finalize(a)

    def test_T16_effect_claim_permit_send_are_separate(self):
        device, policy = Device(), Policy()
        binding = Binding('alice', 'send@1', 'device-1', 1, 1, 100)
        effect = Effect('effect-1', binding, device)
        with self.assertRaises(ContractError): effect.authorize(policy, policy.issue(binding), 10)
        effect.claim()
        effect.authorize(policy, policy.issue(binding), 10)
        self.assertIsNone(effect.outcome())
        self.assertEqual(device.calls, [])
        effect.send(application='Applied', status='Succeeded')
        self.assertIsNone(effect.outcome())
        effect.record()
        self.assertEqual(effect.outcome()['kind'], 'EffectResolved')
        self.assertEqual(effect.outcome()['effect_id'], 'effect-1')
        self.assertEqual(device.calls, ['effect-1'])

    def test_T07_effect_cancel_revoke_send_orders(self):
        for action in ('cancel', 'revoke'):
            for early in (True, False):
                device, policy = Device(), Policy()
                b = Binding('alice', 'send@1', 'device-1', 1, 1, 100)
                effect = Effect('effect-1', b, device); effect.claim(); permit = policy.issue(b)
                def stop():
                    if action == 'cancel': effect.cancel()
                    else: policy.revoke('alice')
                if early:
                    stop()
                    with self.assertRaises(ContractError): effect.authorize(policy, permit, 10)
                    self.assertEqual(device.calls, [])
                else:
                    effect.authorize(policy, permit, 10); stop()
                    effect.send(application='Applied', status='Succeeded'); effect.record()
                    self.assertEqual(effect.outcome()['application'], 'Applied')

    def test_T16_effect_crash_windows_never_blind_resend(self):
        for cut in ('before_claim', 'after_claim', 'after_permit', 'after_send', 'after_outcome'):
            device, policy = Device(), Policy()
            b = Binding('alice', 'send@1', 'device-1', 1, 1, 100)
            effect = Effect('effect-1', b, device)
            if cut != 'before_claim': effect.claim()
            if cut in ('after_permit', 'after_send', 'after_outcome'):
                effect.authorize(policy, policy.issue(b), 10)
            if cut in ('after_send', 'after_outcome'): effect.send(application='Applied', status='Succeeded')
            if cut == 'after_outcome': effect.record()
            calls = list(device.calls)
            effect.restart()
            self.assertEqual(device.calls, calls)
            if cut == 'before_claim': self.assertIsNone(effect.outcome())
            elif cut == 'after_outcome': self.assertEqual(effect.outcome()['kind'], 'EffectResolved')
            else:
                self.assertEqual(effect.outcome()['kind'], 'Indeterminate')
                with self.assertRaises(ContractError): effect.send(application='Applied', status='Succeeded')

    def test_T16_effect_reconcile_and_partial_facts(self):
        for application, status in [('Applied', 'Succeeded'), ('PartiallyApplied', 'Failed'), ('NotApplied', 'Failed')]:
            device, policy = Device(), Policy()
            b = Binding('alice', 'send@1', 'device-1', 1, 1, 100)
            effect = Effect('effect-1', b, device); effect.claim()
            effect.authorize(policy, policy.issue(b), 10)
            effect.send(application=application, status=status); effect.restart()
            effect.reconcile()
            result = effect.outcome()
            self.assertEqual((result['kind'], result['application'], result['status']), ('EffectResolved', application, status))
            self.assertEqual(len(device.calls), 1)
            self.assertEqual(len([f for f in result['known_facts'] if f['kind'] == 'ResolutionRecord']), 1)
            with self.assertRaises(ContractError): effect.reconcile()

    def test_T16_effect_missing_evidence_stays_unknown(self):
        effect = Effect('effect-1', Binding('alice', 'send@1', 'device-1', 1, 1, 100), Device())
        effect.claim(); effect.restart()
        with self.assertRaises(ContractError): effect.reconcile()
        self.assertEqual(effect.outcome()['kind'], 'Indeterminate')
        self.assertEqual(effect.device.calls, [])

    def test_T16_effect_result_encoding_does_not_erase_effect(self):
        device, policy = Device(), Policy()
        b = Binding('alice', 'send@1', 'device-1', 1, 1, 100)
        effect = Effect('effect-1', b, device); effect.claim()
        effect.authorize(policy, policy.issue(b), 10)
        effect.send(application='Applied', status='Succeeded'); effect.record(result_valid=False)
        self.assertEqual(effect.outcome()['application'], 'Applied')
        self.assertIn('result_error', effect.outcome())
        self.assertEqual(effect.outcome()['kind'], 'EffectResolved')


    def test_T14_commit_materials_frozen_after_claim(self):
        for durable in (True, False):
            d, p, b, permit, a = self.claimed(durable)
            a.prepared = replace(a.prepared, value=999)
            with self.assertRaises(ContractError):
                if durable: d.submit(a)
                else: d.publish(a)
            self.assertEqual(d.snapshot().value, 0)
            self.assertEqual(d.disk['ledger'], {})

    def test_T16_effect_outcome_evidence_shape_matches_contract(self):
        device, policy = Device(), Policy()
        b = Binding('alice', 'send@1', 'device-1', 1, 1, 100)
        effect = Effect('effect-1', b, device); effect.claim()
        effect.authorize(policy, policy.issue(b), 10)
        effect.send(application='Applied', status='Succeeded'); effect.restart(); effect.reconcile()
        result = effect.outcome()
        self.assertIsInstance(result['external_evidence'], list)
        self.assertTrue(result['external_evidence'])
        self.assertTrue(all(isinstance(item, str) and item for item in result['external_evidence']))
        resolution = next(fact for fact in result['known_facts'] if fact['kind'] == 'ResolutionRecord')
        self.assertIsInstance(resolution['evidence_ref'], str)

    def test_T16_effect_before_claim_crash_allows_same_identity_retry(self):
        device, policy = Device(), Policy()
        b = Binding('alice', 'send@1', 'device-1', 1, 1, 100)
        effect = Effect('effect-1', b, device); effect.restart()
        effect.claim(); effect.authorize(policy, policy.issue(b), 10)
        effect.send(application='Applied', status='Succeeded'); effect.record()
        self.assertEqual(effect.outcome()['effect_id'], 'effect-1')
        self.assertEqual(device.calls, ['effect-1'])

    def test_T07_effect_cancel_before_permit_records_no_apply(self):
        device, policy = Device(), Policy()
        b = Binding('alice', 'send@1', 'device-1', 1, 1, 100)
        effect = Effect('effect-1', b, device); effect.claim(); effect.cancel()
        self.assertEqual(effect.outcome()['kind'], 'CancelledBeforeApply')
        self.assertEqual(effect.outcome()['known_facts'], [])
        effect.restart()
        self.assertEqual(effect.outcome()['kind'], 'CancelledBeforeApply')
        self.assertEqual(device.calls, [])


    def test_T14_commit_intent_and_state_share_transaction(self):
        key = ('store-1', 'restore-1', 'alice', 'default', 1, 'nonce-1')
        for cut in CommitDomain.TX_STEPS + (None,):
            d = CommitDomain(); policy = Policy()
            binding = Binding('alice', 'edit@1', 'domain-1', 1, 1, 100)
            attempt = d.prepare('commit-1', 7, binding, intent_key=key)
            d.claim(attempt, policy, policy.issue(binding), 10)
            d.submit(attempt); d.writer_step(crash_at=cut); d.deliver()
            self.assertEqual(key in d.disk['receipts'], cut is None)
            self.assertEqual('commit-1' in d.disk['ledger'], cut is None)
            if cut is None:
                self.assertEqual(d.disk['ledger']['commit-1']['intent_key'], key)
                self.assertEqual(d.disk['receipts'][key]['result'], {'value': 7})
                d.publish(attempt)
                duplicate = d.prepare('commit-2', 8, binding, intent_key=key)
                permit = policy.issue(binding)
                with self.assertRaises(ContractError): d.claim(duplicate, policy, permit, 10)
                self.assertFalse(policy.is_consumed(permit))
            d.check_disk()



    def test_T16_commit_memory_recovery_rejected_before_mutation(self):
        for published in (False, True):
            d, p, b, permit, a = self.claimed(durable=False)
            if published: d.publish(a)
            before = (d.snapshot(), deepcopy(a.facts), d.outcome(a), d.business_open, d.isolated, d.reservation)
            with self.assertRaises(ContractError): d.recover()
            self.assertEqual((d.snapshot(), a.facts, d.outcome(a), d.business_open, d.isolated, d.reservation), before)

    def test_T14_commit_candidate_chain_validated_before_claim(self):
        for change in [dict(revision=2, history=('ghost','commit-1')),dict(history=('other',)),dict(lifecycle_generation=99),dict(value=999)]:
            d, p, b, permit, a = self.ready()
            a.prepared = replace(a.prepared, **change)
            before = deepcopy(d.disk)
            with self.assertRaises(ContractError): d.claim(a, p, permit, now=10)
            self.assertFalse(p.is_consumed(permit));self.assertEqual(d.disk,before);self.assertEqual(d.snapshot().revision,0)

        for value in ({'mutable':1}, [1], True):
            d,p,b,permit,a=self.ready()
            with self.assertRaises(ContractError):d.prepare('bad-type',value,b)
            self.assertNotIn('bad-type',d.attempts)

    def test_T14_commit_published_outcome_uses_frozen_material(self):
        for durable in (True,False):
            d,p,b,permit,a=self.claimed(durable)
            if durable:d.submit(a);d.writer_step();d.deliver()
            d.publish(a);expected=d.outcome(a)
            a.prepared=replace(a.prepared,value=999,revision=42,history=('rewritten',));a.commit_id='forged';a.facts.clear()
            self.assertEqual(d.outcome(a),expected)
            self.assertEqual(d.snapshot().value,7)

    def test_T07_effect_claim_material_binding_is_frozen(self):
        for cut in ('before_permit','after_permit'):
            for field in ('effect_id','binding','device'):
                device,policy=Device(),Policy();b=Binding('alice','send@1','device-1',1,1,100);e=Effect('effect-1',b,device);e.claim();permit=policy.issue(b)
                if cut=='after_permit':e.authorize(policy,permit,10)
                setattr(e,field,{'effect_id':'effect-2','binding':replace(b,target='device-2'),'device':Device()}[field])
                with self.assertRaises(ContractError):
                    if cut=='before_permit':e.authorize(policy,permit,10)
                    else:e.send('Applied','Succeeded')
                self.assertEqual(device.calls,[])
                if cut=='before_permit':self.assertFalse(policy.is_consumed(permit))

    def test_T16_effect_sent_facts_use_frozen_identity(self):
        device,policy=Device(),Policy();b=Binding('alice','send@1','device-1',1,1,100);e=Effect('effect-1',b,device);e.claim();e.authorize(policy,policy.issue(b),10);e.send('Applied','Succeeded')
        e.effect_id='forged';e.binding=replace(b,target='forged');e.device=Device();e.report={'application':'NotApplied','status':'Failed','external_evidence':['forged']}
        e.record();outcome=e.outcome();self.assertEqual(outcome['effect_id'],'effect-1');self.assertEqual(outcome['application'],'Applied');self.assertEqual(outcome['external_evidence'],['device-ledger:effect-1'])
        e.restart();self.assertEqual(e.outcome(),outcome);self.assertEqual(device.calls,['effect-1'])
        unknown=Effect('effect-2',b,device);unknown.claim();unknown.authorize(policy,policy.issue(b),10);unknown.send('Applied','Succeeded');unknown.effect_id='other';unknown.device=Device();unknown.restart();unknown.reconcile();self.assertEqual(unknown.outcome()['effect_id'],'effect-2')

    def test_T16_effect_send_attempt_is_once_before_external_call(self):
        class FailingDevice(Device):
            def __init__(self, after_apply):
                super().__init__()
                self.after_apply, self.attempts = after_apply, 0

            def apply(self, effect_id, application, status):
                self.attempts += 1
                if self.after_apply:
                    super().apply(effect_id, application, status)
                raise RuntimeError('外部调用结果丢失')

        for recorded in (False, True):
            with self.subTest(recorded=recorded):
                device, policy = Device(), Policy()
                binding = Binding('alice', 'send@1', 'device-1', 1, 1, 100)
                effect = Effect('effect-1', binding, device)
                effect.claim(); effect.authorize(policy, policy.issue(binding), 10)
                effect.send('Applied', 'Succeeded')
                if recorded: effect.record()
                effect.report = None
                with self.assertRaises(ContractError): effect.send('Applied', 'Succeeded')
                self.assertEqual(device.calls, ['effect-1'])
                if not recorded: effect.record()
                self.assertEqual(effect.outcome()['application'], 'Applied')

        for after_apply in (False, True):
            with self.subTest(after_apply=after_apply):
                device, policy = FailingDevice(after_apply), Policy()
                binding = Binding('alice', 'send@1', 'device-1', 1, 1, 100)
                effect = Effect('effect-1', binding, device)
                effect.claim(); effect.authorize(policy, policy.issue(binding), 10)
                with self.assertRaisesRegex(RuntimeError, '外部调用结果丢失'):
                    effect.send('Applied', 'Succeeded')
                self.assertEqual(effect.outcome()['kind'], 'Indeterminate')
                effect.report = None
                with self.assertRaises(ContractError): effect.send('Applied', 'Succeeded')
                with self.assertRaises(ContractError): effect.record()
                self.assertEqual(device.attempts, 1)
                effect.restart()
                with self.assertRaises(ContractError): effect.send('Applied', 'Succeeded')
                if after_apply:
                    effect.reconcile()
                    result = effect.outcome()
                    self.assertEqual(result['kind'], 'EffectResolved')
                    self.assertEqual(result['application'], 'Applied')
                    self.assertEqual(result['effect_id'], 'effect-1')
                    self.assertTrue(any(f['kind'] == 'ResolutionRecord' for f in result['known_facts']))
                else:
                    with self.assertRaises(ContractError): effect.reconcile()
                    self.assertEqual(effect.outcome()['kind'], 'Indeterminate')
                with self.assertRaises(ContractError): effect.send('Applied', 'Succeeded')
                self.assertEqual(device.attempts, 1)
                self.assertEqual(device.calls, ['effect-1'] if after_apply else [])

if __name__ == '__main__': unittest.main()

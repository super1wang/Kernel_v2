"""D0.05 去重、子执行、恢复世代的固定合同反例。"""
from copy import deepcopy
from dataclasses import replace
import itertools
import json
from pathlib import Path
import unittest

from dedup_model.model import (ContractError, Request, IntentStore, StepKey, ClientIntent)


class DedupContracts(unittest.TestCase):
    def ready(self, volatile=False, capacity=100):
        store = IntentStore(volatile=volatile, capacity=capacity)
        session = store.connect('alice')
        request = Request('edit@1', 'contract-1', (('value', 7),), 'target-1', (('revision', 0),), 'plan-1')
        return store, session, request

    def test_T15_intent_same_key_returns_existing_identity(self):
        s, session, req = self.ready()
        first = s.claim(session, 'default', 1, 'nonce-1', req)
        again = s.claim(s.connect('alice'), 'default', 1, 'nonce-1', replace(req, request_id='other', trace_id='new', retry_attempt=9))
        self.assertEqual(first, again)
        self.assertEqual(len(s.image['records']), 1)
        self.assertEqual(len(s.image['executions']), 1)
        s.resolve(first['key'], 'StateCommitted')
        final = s.claim(session, 'default', 1, 'nonce-1', req)
        self.assertEqual(final['outcome'], 'StateCommitted')
        self.assertEqual(final['execution_ref'], first['execution_ref'])

    def test_T15_intent_fingerprint_covers_all_semantic_fields(self):
        s, session, req = self.ready()
        s.claim(session, 'default', 1, 'nonce-1', req)
        for field, value in [('operation', 'edit@2'), ('contract', 'contract-2'),
                             ('args', (('value', 8),)), ('target', 'target-2'),
                             ('preconditions', (('revision', 1),)), ('plan_digest', 'plan-2')]:
            before = deepcopy(s.image)
            with self.assertRaises(ContractError): s.claim(session, 'default', 1, 'nonce-1', replace(req, **{field: value}))
            self.assertEqual(s.image, before)

    def test_T15_intent_stable_principal_scope_and_permissions(self):
        s, alice, req = self.ready()
        record = s.claim(alice, 'default', 1, 'nonce-1', req)
        bob_record = s.claim(s.connect('bob'), 'default', 1, 'nonce-1', req)
        self.assertNotEqual(record['key'], bob_record['key'])
        self.assertEqual(record['key'].scope.store_id, s.store_id)
        self.assertEqual(record['key'].scope.restore_generation, s.restore_generation)
        s.revoke('alice')
        for action in (lambda: s.claim(alice, 'default', 1, 'nonce-1', req), lambda: s.query(alice, record['key'])):
            with self.assertRaises(ContractError): action()
        with self.assertRaises(ContractError): s.query(s.connect('bob'), record['key'])
        with self.assertRaises(ContractError): s.claim(replace(alice, principal='bob'), 'default', 1, 'nonce-2', req)

    def test_T15_intent_server_scope_and_namespace_reject_forgery(self):
        s, session, req = self.ready()
        for field, value in [('store_id', 'other'), ('restore_generation', 'other'), ('token', 'forged')]:
            with self.assertRaises(ContractError): s.claim(replace(session, **{field: value}), 'default', 1, 'n', req)
        with self.assertRaises(ContractError): s.claim(session, 'unvalidated-namespace', 1, 'n', req)
        self.assertEqual(s.image['records'], {})

    def test_T15_intent_future_expired_and_existing_before_floor(self):
        s, session, req = self.ready()
        retained = s.claim(session, 'default', 1, 'retained', req)
        s.set_status(retained['key'], 'Unknown')
        s.advance_epoch(session, 'default', 2); s.gc(session, 'default', 2)
        self.assertEqual(s.claim(session, 'default', 1, 'retained', req)['execution_ref'], retained['execution_ref'])
        for epoch, nonce in [(1, 'missing'), (3, 'future'), (0, 'zero'), (-1, 'negative')]:
            with self.assertRaises(ContractError): s.claim(session, 'default', epoch, nonce, req)
        s.claim(session, 'default', 2, 'current', req)

    def test_T14_intent_acceptance_transaction_crash_windows(self):
        for cut in IntentStore.ACCEPT_STEPS:
            s, session, req = self.ready()
            before = deepcopy(s.image)
            with self.assertRaises(ContractError): s.claim(session, 'default', 1, 'n', req, crash_at=cut)
            self.assertEqual(s.image, before, cut)
            self.assertEqual(s.scheduled, [])
            record = s.claim(session, 'default', 1, 'n', req)
            self.assertIn(record['execution_ref'], s.image['executions'])
        s, session, req = self.ready()
        with self.assertRaises(ContractError): s.claim(session, 'default', 1, 'n', req, crash_at='after_commit')
        self.assertEqual(s.scheduled, [])
        s.restart()
        self.assertEqual(s.scheduled, [])
        record = s.claim(s.connect('alice'), 'default', 1, 'n', req)
        self.assertEqual(record['status'], 'Suspended')
        self.assertEqual(len(s.image['records']), 1)

    def test_T15_intent_claim_gc_both_orders(self):
        for order in itertools.permutations(('claim', 'gc')):
            s, session, req = self.ready()
            s.advance_epoch(session, 'default', 2)
            record = None
            for action in order:
                if action == 'gc': s.gc(session, 'default', 2)
                elif order[0] == 'gc':
                    with self.assertRaises(ContractError): s.claim(session, 'default', 1, 'n', req)
                else: record = s.claim(session, 'default', 1, 'n', req)
            if record:
                self.assertEqual(s.claim(session, 'default', 1, 'n', req)['execution_ref'], record['execution_ref'])
            self.assertEqual(len(s.image['records']), int(record is not None))
            s.check_invariants()

    def test_T15_intent_writer_transaction_prevents_interleaving(self):
        s, session, req = self.ready()
        with s.writer():
            for action in (lambda: s.claim(session, 'default', 1, 'n', req),
                           lambda: s.gc(session, 'default', 1),
                           lambda: s.advance_epoch(session, 'default', 2)):
                with self.assertRaises(ContractError): action()
        self.assertEqual(s.image['records'], {})

    def test_T15_gc_watermark_and_delete_crash_windows(self):
        for cut in ('before_floor', 'after_floor', 'after_delete', None):
            s, session, req = self.ready()
            record = s.claim(session, 'default', 1, 'n', req); s.resolve(record['key'], 'StateCommitted')
            s.advance_epoch(session, 'default', 2)
            if cut:
                with self.assertRaises(ContractError): s.gc(session, 'default', 2, crash_at=cut)
            else: s.gc(session, 'default', 2)
            s.check_invariants()
            if record['key'] in s.image['records']:
                self.assertEqual(s.claim(session, 'default', 1, 'n', req)['execution_ref'], record['execution_ref'])
            else:
                with self.assertRaises(ContractError): s.claim(session, 'default', 1, 'n', req)

    def test_T15_gc_delete_before_watermark_is_detected(self):
        s, session, req = self.ready()
        record = s.claim(session, 'default', 1, 'n', req); s.resolve(record['key'], 'StateCommitted')
        s.advance_epoch(session, 'default', 2)
        # 直接构造错误写序列在删除后崩溃的镜像；模型必须识别复活窗口。
        del s.image['records'][record['key']]
        with self.assertRaises(ContractError): s.check_invariants()

    def test_T15_gc_pins_inflight_unknown_and_references(self):
        s, session, req = self.ready()
        records = {name: s.claim(session, 'default', 1, name, req) for name in ('pin', 'inflight', 'unknown', 'ref', 'done')}
        for name in ('pin', 'ref', 'done'): s.resolve(records[name]['key'], 'StateCommitted')
        s.pin(records['pin']['key'], 'parent-1')
        s.reference(records['ref']['key'], 'checkpoint-1')
        s.set_status(records['unknown']['key'], 'Unknown')
        s.advance_epoch(session, 'default', 2); s.gc(session, 'default', 2)
        for name in ('pin', 'inflight', 'unknown', 'ref'): self.assertIn(records[name]['key'], s.image['records'])
        self.assertNotIn(records['done']['key'], s.image['records'])
        s.unpin(records['pin']['key'], 'parent-1'); s.unreference(records['ref']['key'], 'checkpoint-1')
        s.gc(session, 'default', 2)
        self.assertNotIn(records['pin']['key'], s.image['records'])
        self.assertNotIn(records['ref']['key'], s.image['records'])
        s.check_invariants()

    def test_T15_gc_scope_watermarks_are_independent_monotonic(self):
        s, alice, req = self.ready(); bob = s.connect('bob')
        s.advance_epoch(alice, 'default', 2); s.gc(alice, 'default', 2)
        s.claim(bob, 'default', 1, 'n', req)
        s.claim(alice, 'batch', 1, 'n', req)
        for action in (lambda: s.gc(alice, 'default', 1), lambda: s.gc(alice, 'default', 3),
                       lambda: s.advance_epoch(alice, 'default', 1)):
            with self.assertRaises(ContractError): action()

    def test_T15_intent_volatile_capacity_preserves_window(self):
        s, session, req = self.ready(volatile=True, capacity=1)
        record = s.claim(session, 'default', 1, 'n', req); s.resolve(record['key'], 'StateCommitted')
        with self.assertRaises(ContractError): s.claim(session, 'default', 1, 'new', req)
        self.assertEqual(s.claim(session, 'default', 1, 'n', req)['execution_ref'], record['execution_ref'])
        s.advance_epoch(session, 'default', 2); s.gc(session, 'default', 2)
        s.claim(session, 'default', 2, 'new', req)
        with self.assertRaises(ContractError): s.claim(session, 'default', 1, 'n', req)

    def test_T15_intent_client_retry_never_changes_identity(self):
        s, session, req = self.ready()
        client = ClientIntent(s, session, 1, 'n')
        self.assertEqual(client.retry(s, session), (1, 'n'))
        s.advance_epoch(session, 'default', 2); s.gc(session, 'default', 2)
        self.assertEqual(client.retry(s, session), (1, 'n'))
        with self.assertRaises(ContractError): s.claim(session, 'default', *client.retry(s, session), req)
        s.restart()
        self.assertEqual(client.retry(s, s.connect('alice')), (1, 'n'))
        volatile, v_session, req = self.ready(volatile=True)
        old = ClientIntent(volatile, v_session, 1, 'n'); volatile.restart()
        with self.assertRaises(ContractError): old.retry(volatile, volatile.connect('alice'))

    def test_T15_step_internal_key_and_parameter_binding(self):
        s, session, req = self.ready()
        parent = s.register_parent(session, 'parent-1', 'fixed-plan', {'/node': ('edit@1', 'contract-1', 'target-1')})
        admission = s.admit_child(parent, '/node', (), 0)
        first = s.claim_child(session, admission, req)
        self.assertIsInstance(first['key'], StepKey)
        self.assertEqual(first['key'], StepKey('parent-1', '/node', (), 0))
        self.assertEqual(s.claim_child(session, admission, replace(req, retry_attempt=3)), first)
        with self.assertRaises(ContractError): s.claim_child(session, admission, replace(req, args=(('value', 8),)))
        self.assertEqual(len(s.image['steps']), 1)
        s.advance_epoch(session, 'default', 3); s.gc(session, 'default', 3)
        self.assertEqual(s.claim_child(session, admission, req)['execution_ref'], first['execution_ref'])
        self.assertIn('parent-1', s.image['steps'][first['key']]['pins'])

    def test_T15_step_branch_iteration_and_occurrence_are_identity(self):
        s, session, req = self.ready()
        parent = s.register_parent(session, 'parent-1', 'fixed-plan', {'/node': ('edit@1', 'contract-1', 'target-1')})
        keys = []
        for path, occurrence in [(('then', 0), 0), (('else', 0), 0), (('then', 1), 0), (('then', 0), 1)]:
            admission = s.admit_child(parent, '/node', path, occurrence)
            keys.append(s.claim_child(session, admission, req)['key'])
        self.assertEqual(len(set(keys)), 4)

    def test_T15_step_admission_lifetime_definition_and_authorization(self):
        s, session, req = self.ready()
        with self.assertRaises(ContractError): s.admit_child('missing', '/node', (), 0)
        parent = s.register_parent(session, 'parent-1', 'fixed-plan', {'/node': ('edit@1', 'contract-1', 'target-1')})
        with self.assertRaises(ContractError): s.admit_child(parent, '/not-in-plan', (), 0)
        admission = s.admit_child(parent, '/node', (), 0)
        for field, value in [('operation', 'edit@2'), ('contract', 'contract-2'), ('target', 'target-2')]:
            with self.assertRaises(ContractError): s.claim_child(session, admission, replace(req, **{field: value}))
        with self.assertRaises(ContractError): s.claim_child(session, replace(admission, token='forged'), req)
        with self.assertRaises(ContractError): s.claim_child(s.connect('bob'), admission, req)
        s.revoke('alice')
        with self.assertRaises(ContractError): s.claim_child(session, admission, req)
        s.grant('alice'); s.finish_parent(parent)
        with self.assertRaises(ContractError): s.claim_child(session, admission, req)

    def test_T15_step_external_interface_has_no_epoch_bypass(self):
        s, session, req = self.ready()
        with self.assertRaises(TypeError): s.claim(session, 'default', 0, 'n', req, skip_epoch=True)
        with self.assertRaises(ContractError): s.claim(session, 'default', StepKey('p', '/n', (), 0), 'n', req)
        self.assertEqual(s.image['records'], {})

    def test_T18_restore_restart_preserves_store_and_generation(self):
        s, session, req = self.ready()
        record = s.claim(session, 'default', 1, 'n', req); s.resolve(record['key'], 'StateCommitted')
        before = (s.store_id, s.restore_generation, s.incarnation)
        s.restart()
        self.assertEqual((s.store_id, s.restore_generation), before[:2])
        self.assertNotEqual(s.incarnation, before[2])
        with self.assertRaises(ContractError): s.query(session, record['key'])
        self.assertEqual(s.query(s.connect('alice'), record['key'])['outcome'], 'StateCommitted')

    def test_T18_restore_old_backup_changes_generation_and_seals_effects(self):
        s, session, req = self.ready()
        before = s.claim(session, 'default', 1, 'before', req); s.resolve(before['key'], 'StateCommitted')
        backup = s.backup()
        after = s.claim(session, 'default', 1, 'after', req); s.resolve(after['key'], 'EffectResolved')
        client = ClientIntent(s, session, 1, 'after')
        generation, store_id = s.restore_generation, s.store_id
        s.restore_backup(backup)
        self.assertEqual(s.store_id, store_id)
        self.assertNotEqual(s.restore_generation, generation)
        self.assertTrue(s.effects_sealed)
        with self.assertRaises(ContractError): s.claim(session, 'default', 1, 'after', req)
        fresh = s.connect('alice')
        with self.assertRaises(ContractError): client.retry(s, fresh)
        self.assertEqual(s.lookup_historical(after['key'])['status'], 'UnknownAfterBackup')
        self.assertEqual(s.lookup_historical(before['key'])['outcome'], 'StateCommitted')
        with self.assertRaises(ContractError): s.authorize_effect_send(s.connect('alice'), after['key'])
        self.assertEqual(s.effect_dispatches, [])

    def test_T18_restore_reconciliation_does_not_replay_history(self):
        s, session, req = self.ready(); backup = s.backup()
        after = s.claim(session, 'default', 1, 'after', req)
        s.restore_backup(backup)
        with self.assertRaises(ContractError): s.finish_restore_reconciliation('')
        s.finish_restore_reconciliation('operator-device-ledger-42')
        self.assertFalse(s.effects_sealed)
        with self.assertRaises(ContractError): s.authorize_effect_send(s.connect('alice'), after['key'])
        self.assertEqual(s.effect_dispatches, [])
        self.assertEqual(s.handler_runs, 0)

    def test_T18_backup_manifest_pins_and_validation(self):
        s, session, req = self.ready()
        s.add_asset('asset-1', b'contents'); s.reference_asset('asset-1')
        backup = s.begin_backup()
        s.live_assets.clear(); s.gc_assets()
        self.assertIn('asset-1', s.assets)
        s.finish_backup(backup)
        s.gc_assets()
        self.assertIn('asset-1', s.assets)
        bad = deepcopy(backup); bad['assets']['asset-1'] = b'broken'
        before = s.restore_generation
        with self.assertRaises(ContractError): s.restore_backup(bad)
        self.assertEqual(s.restore_generation, before)
        bad = deepcopy(backup); bad['image']['records']['corrupt'] = {}
        with self.assertRaises(ContractError): s.restore_backup(bad)
        s.restore_backup(backup)
        self.assertEqual(s.assets['asset-1'], b'contents')

    def test_T18_restore_invalidates_child_admission(self):
        s, session, req = self.ready()
        parent = s.register_parent(session, 'parent-1', 'fixed-plan', {'/node': ('edit@1', 'contract-1', 'target-1')})
        admission = s.admit_child(parent, '/node', (), 0)
        backup = s.backup(); s.restore_backup(backup)
        with self.assertRaises(ContractError): s.claim_child(s.connect('alice'), admission, req)

    def test_T15_intent_expected_manifest_matches_fixed_cases(self):
        import ast
        root = Path(__file__).resolve().parents[3]
        found = []
        for folder in ('commit_model', 'dedup_model'):
            source = root / 'tests/model' / folder / ('test_' + folder + '.py')
            for cls in ast.parse(source.read_text(encoding='utf-8')).body:
                if isinstance(cls, ast.ClassDef):
                    found.extend(f'{folder}.{source.stem}.{cls.name}.{node.name}' for node in cls.body
                                 if isinstance(node, ast.FunctionDef) and node.name.startswith('test_'))
        manifest = json.loads((root / 'tests/manifests/d0.05.expected.json').read_text(encoding='utf-8'))
        expected = [case['argv'][-1] for case in manifest['cases']]
        self.assertEqual(len(expected), len(set(expected)))
        self.assertCountEqual(found, expected)


    def test_T18_restore_structure_checked_before_generation_change(self):
        from dedup_model.model import digest
        s, session, req = self.ready()
        record = s.claim(session, 'default', 1, 'n', req)
        backup = s.backup()
        # 一致 hash 不代表合法结构：这是来自错误 writer 的真实镜像模型。
        bad = deepcopy(backup); bad['image']['receipts'].clear()
        bad['image_sha256'] = digest(bad['image'])
        before = (s.restore_generation, s.incarnation, deepcopy(s.image))
        with self.assertRaises(ContractError): s.restore_backup(bad)
        self.assertEqual((s.restore_generation, s.incarnation, s.image), before)
        self.assertEqual(s.query(session, record['key'])['execution_ref'], record['execution_ref'])


    def test_T18_restore_new_effect_requires_reconciliation_and_new_intent(self):
        s, session, req = self.ready(); backup = s.backup()
        old = s.claim(session, 'default', 1, 'old', req)
        s.restore_backup(backup)
        fresh = s.connect('alice')
        new = s.claim(fresh, 'default', 1, 'new-explicit-intent', replace(req, operation='send@1'))
        with self.assertRaises(ContractError): s.authorize_effect_send(fresh, new['key'])
        self.assertEqual(s.effect_dispatches, [])
        s.finish_restore_reconciliation('external-ledger-42')
        with self.assertRaises(ContractError): s.authorize_effect_send(fresh, old['key'])
        s.authorize_effect_send(fresh, new['key'])
        self.assertEqual(s.effect_dispatches, [new['key']])
        with self.assertRaises(ContractError): s.authorize_effect_send(fresh, new['key'])
        s.restart()
        self.assertEqual(s.image['records'][new['key']]['status'], 'Unknown')
        with self.assertRaises(ContractError): s.authorize_effect_send(s.connect('alice'), new['key'])


if __name__ == '__main__': unittest.main()

"""先行合同/反例；测试名稳定映射到 D0.03 expected manifest。"""
import copy
import itertools
import json
from pathlib import Path
import unittest

from model import (ContractError, Execution, Observer, OUTCOMES, PHASE_EDGES,
                   validate_document, validate_outcome, validate_snapshot, validate_schema)

HERE = Path(__file__).resolve().parent


def read(candidate=False):
    return dict(kind="ReadCompleted", evidence="Volatile", known_facts=[],
                result={"value": 7}, result_scope="Candidate" if candidate else "ReadOnly")


def failed(reason="Error"):
    return dict(kind="FailedBeforeApply", evidence="Volatile", known_facts=[],
                failure_phase="Running", reason=reason, proof="NoAppliedStateOrEffect")


def effect(application="PartiallyApplied", status="Failed"):
    return dict(kind="EffectResolved", evidence="Volatile", known_facts=[
        dict(kind="EffectFact", fact_id="f-effect", effect_id="effect-1", application=application)],
        effect_id="effect-1", application=application, status=status,
        external_evidence=["device-receipt-1"], result={"sent": 2})


def lifecycle():
    return dict(kind="LifecycleResolved", evidence="Volatile", known_facts=[dict(
        kind="LifecycleFact", fact_id="f-life", transition_id="transition-1",
        before="Ready", after="Failed", generation="8")], transition_id="transition-1",
        before="Ready", after="Failed", generation="8", status="Failed")


def state(facts, evidence="Durable"):
    return dict(kind="StateCommitted", evidence=evidence, known_facts=copy.deepcopy(facts),
                commit_id="commit-1", domain="settings", revision="41", published_version="41", result={})


def unknown():
    return dict(kind="Indeterminate", evidence="PersistenceUncertain", known_facts=[dict(
        kind="UnknownFact", fact_id="u1", boundary="ExternalEffect", reference_id="effect-1",
        reconcile="query-device-receipt")], unknown_ids=["u1"])


def plan(facts=None, statuses=("Succeeded", "Succeeded")):
    return dict(kind="PlanCompleted", evidence="Volatile", known_facts=facts or [],
                exports={"value": 3}, necessary_children_drained=True,
                steps=[dict(step_id=f"step-{i}", status=s) for i, s in enumerate(statuses)])


def running(**kwargs):
    e = Execution(**kwargs)
    e.publish_record()
    e.dispatch()
    return e


class ExecutionContracts(unittest.TestCase):
    def test_T02_outcome_nine_variants(self):
        self.assertEqual(set(OUTCOMES), {"ReadCompleted", "StateCommitted", "EffectResolved",
            "LifecycleResolved", "PlanCompleted", "FailedBeforeApply", "CancelledBeforeApply",
            "PartialCompletion", "Indeterminate"})
        for kind in ("Success", "CancelledAfterApply", "DurableCommitted"):
            with self.assertRaises(ContractError): validate_outcome(dict(read(), kind=kind))

    def test_T02_outcome_read_candidate(self):
        validate_outcome(read(True))
        with self.assertRaises(ContractError): validate_outcome(dict(read(), commit_id="fake"))
        with self.assertRaises(ContractError): validate_outcome(dict(read(), known_facts=effect()["known_facts"]))

    def test_T02_outcome_effect_minimal_receipt(self):
        value = effect()
        validate_outcome(value)
        value.pop("result")
        value["result_error"] = "ResultSerializationFailed"
        validate_outcome(value)
        for replacement in (failed(), dict(value, application="NotApplied")):
            e = running()
            e.record_facts(effect()["known_facts"])
            with self.assertRaises(ContractError): e.complete(replacement)

    def test_T02_outcome_lifecycle_failed_transition(self):
        validate_outcome(lifecycle())
        e = running()
        e.complete(lifecycle())
        e.finalize()
        self.assertEqual(e.get()["outcome"]["after"], "Failed")
        with self.assertRaises(ContractError): validate_outcome(dict(lifecycle(), after="Ready"))

    def test_T02_outcome_plan_all_success(self):
        validate_outcome(plan())
        for bad in (dict(plan(), commit_id="global"), plan(statuses=("Succeeded", "Failed")),
                    dict(plan(), necessary_children_drained=False)):
            with self.assertRaises(ContractError): validate_outcome(bad)
        validate_outcome(failed("ReadOnlyPlanStepFailed"))

    def test_T02_outcome_partial_and_unknown(self):
        p = dict(kind="PartialCompletion", evidence="Volatile", known_facts=effect()["known_facts"],
                 steps=[dict(step_id="a", status="Succeeded"), dict(step_id="b", status="Failed")])
        validate_outcome(p)
        with self.assertRaises(ContractError): validate_outcome(dict(p, known_facts=[]))
        mixed = p["known_facts"] + unknown()["known_facts"]
        with self.assertRaises(ContractError): validate_outcome(dict(p, known_facts=mixed))
        validate_outcome(dict(unknown(), known_facts=mixed))
        with self.assertRaises(ContractError): validate_outcome(dict(plan(), known_facts=mixed))

    def test_T12_outcome_evidence_matrix(self):
        for evidence in ("Volatile", "Durable", "RequiredRecordFailed"):
            validate_outcome(dict(effect(), evidence=evidence))
        validate_outcome(unknown())
        for evidence in ("PersistenceUncertain", "durable", True):
            with self.assertRaises(ContractError): validate_outcome(dict(read(), evidence=evidence))
        with self.assertRaises(ContractError): validate_outcome(dict(read(), durable=True))

    def test_T19_outcome_reply_layers(self):
        validate_document(dict(kind="Accepted", execution_ref="exec-1", acceptance_guarantee="Volatile"))
        validate_document(dict(kind="Completed", outcome=read()))
        validate_document(dict(kind="Rejected", reason="QuotaExceeded"))
        for bad in (dict(kind="Accepted", outcome=read()),
                    dict(kind="Accepted", execution_ref="exec-1", acceptance_guarantee="Volatile", outcome=read()),
                    dict(kind="Completed", outcome=read(), execution_ref="exec-1")):
            with self.assertRaises(ContractError): validate_document(bad)

    def test_T06_execution_durable_acceptance(self):
        e = Execution(acceptance="DurableAccepted")
        with self.assertRaises(ContractError): e.accepted_reply()
        e.publish_record()
        with self.assertRaises(ContractError): e.accepted_reply()
        e.persist_acceptance()
        self.assertEqual(e.accepted_reply()["acceptance_guarantee"], "DurableAccepted")

    def test_T06_execution_inline_completion(self):
        e = Execution()
        with self.assertRaises(ContractError): e.dispatch(inline=read())
        e.publish_record()
        e.dispatch(inline=read())
        self.assertEqual(e.get()["phase"], "Finalizing")
        e.finalize()
        self.assertEqual(e.accepted_reply()["kind"], "Accepted")
        self.assertEqual((e.callback_count, e.release_count, e.completion_signals), (1, 1, 1))

    def test_T06_execution_executor_rejection(self):
        for disposition in ("Rejected", "Exception"):
            e = Execution()
            e.publish_record()
            reply = e.accepted_reply()
            e.dispatch(disposition=disposition)
            self.assertEqual(e.get()["outcome"]["kind"], "FailedBeforeApply")
            self.assertEqual(e.work_count, 0)
            self.assertEqual(reply["execution_ref"], e.get()["execution_ref"])
            with self.assertRaises(ContractError): e.complete(read())
            e.finalize()

    def test_T06_execution_duplicate_callback(self):
        e = running()
        e.complete(read())
        before = e.get()
        with self.assertRaises(ContractError): e.complete(read())
        self.assertEqual(e.get(), before)
        e.finalize()
        with self.assertRaises(ContractError): e.complete(read())
        with self.assertRaises(ContractError): e.finalize()
        self.assertEqual((e.callback_count, e.release_count, e.completion_signals), (1, 1, 1))

    def test_T06_execution_phase_edges(self):
        expected = {"Queued": {"WaitingResources", "Running", "Finalizing", "Suspended"},
            "WaitingResources": {"Running", "Finalizing", "Suspended"},
            "Running": {"WaitingChild", "Finalizing", "Suspended"},
            "WaitingChild": {"Running", "Finalizing", "Suspended"},
            "Finalizing": {"Terminal"}, "Suspended": set(), "Terminal": set()}
        self.assertEqual(PHASE_EDGES, expected)
        for phase, target in itertools.product(expected, repeat=2):
            if target not in expected[phase]:
                with self.subTest(phase=phase, target=target):
                    e = Execution()
                    e.publish_record()
                    e._phase = phase  # 仅故障注入：枚举非法边，不构造成功证据。
                    with self.assertRaises(ContractError): e.transition(target)

    def test_T06_execution_suspended_owner(self):
        e = running(recoverable=True)
        with self.assertRaises(ContractError): e.suspend(durable_pins=False)
        e.suspend(durable_pins=True)
        self.assertFalse(e.wait_terminal())
        self.assertTrue(e.get()["durable_pins"])
        with self.assertRaises(ContractError): e.transition("Running")
        e.resume()
        self.assertEqual(e.get()["phase"], "Running")
        with self.assertRaises(ContractError): running().suspend(durable_pins=True)
        queued = Execution(recoverable=True)
        queued.publish_record()
        queued.transition("WaitingResources")
        queued.suspend(durable_pins=True)
        queued.resume()
        self.assertEqual(queued.get()["phase"], "WaitingResources")
        queued.dispatch()
        queued.complete(read())
        queued.finalize()

    def test_T12_execution_required_record_failure(self):
        e = running(required_record=True, max_record_attempts=2)
        e.complete(effect())
        with self.assertRaises(ContractError): e.finalize()
        e.record_attempt(success=False)
        self.assertEqual(e.get()["phase"], "Finalizing")
        e.record_attempt(success=False)
        s = e.get()
        self.assertTrue(s["writes_blocked"])
        self.assertEqual(s["outcome"]["evidence"], "RequiredRecordFailed")
        self.assertEqual(s["known_facts"], effect()["known_facts"])
        self.assertEqual(s["fault"], "RequiredRecordFailed")
        e.finalize()
        self.assertTrue(e.wait_terminal())
        with self.assertRaises(ContractError): e.record_attempt(success=True)

    def test_T12_execution_isolation_lifetime(self):
        e = running(required_record=True, max_record_attempts=1)
        e.local_started("late-worker")
        e.complete(effect())
        e.record_attempt(success=False)
        with self.assertRaises(ContractError): e.finalize()
        with self.assertRaises(ContractError): e.isolate("")
        e.isolate("IsolationOwner-1")
        with self.assertRaises(ContractError): e.finalize()
        self.assertEqual(e.release_count, 0)
        e.local_drained("late-worker")
        e.finalize()
        self.assertEqual(e.release_count, 1)

    def test_T06_execution_necessary_children(self):
        parent, child = running(), running(execution_ref="child-1")
        parent.attach_child(child)
        parent.complete(failed())
        with self.assertRaises(ContractError): parent.finalize()
        with self.assertRaises(ContractError): parent.transfer_child("child-1", "")
        parent.transfer_child("child-1", "IndependentOwner-1")
        parent.finalize()
        self.assertFalse(child.wait_terminal())
        self.assertEqual(child.owner, "IndependentOwner-1")

    def test_T06_execution_plan_children_drained(self):
        parent, child = running(), running(execution_ref="child-1")
        parent.attach_child(child)
        with self.assertRaises(ContractError): parent.complete(plan())
        child.complete(read())
        child.finalize()
        parent.complete(plan())
        parent.finalize()

    def test_T06_execution_cancel_keeps_facts(self):
        e = running()
        e.complete(effect())
        old = e.get()["outcome"]
        self.assertEqual(e.cancel(), "AlreadyClaimed")
        self.assertEqual(e.get()["outcome"], old)
        e.finalize()
        self.assertEqual(e.cancel(), "AlreadyTerminal")
        self.assertEqual(e.get()["outcome"], old)
        cancelled = dict(kind="CancelledBeforeApply", evidence="Volatile", known_facts=[],
                         reason="Cancelled", proof="NoAppliedStateOrEffect")
        e = running()
        with self.assertRaises(ContractError): e.complete(cancelled)
        e.cancel()
        e.cancel_before_apply()
        self.assertEqual(e.get()["outcome"]["kind"], "CancelledBeforeApply")

    def test_T20_execution_atomic_candidate_progress(self):
        e = running()
        e.progress(20, scope="Candidate")
        s = e.get()
        self.assertEqual(s["progress"], {"percent": 20, "scope": "Candidate"})
        self.assertEqual(s["known_facts"], [])
        self.assertIsNone(s["outcome"])
        with self.assertRaises(ContractError): e.progress(30, scope="Published")
        forged = copy.deepcopy(s)
        forged["progress"]["scope"] = "Published"
        with self.assertRaises(ContractError): validate_snapshot(forged)

    def test_T12_execution_commit_publication_gate(self):
        e = running()
        e.record_commit("commit-1", "settings", "41", durability="DurableCommitted")
        s = e.get()
        self.assertIsNone(s["outcome"])
        self.assertEqual(s["known_facts"][0]["durability"], "DurableCommitted")
        with self.assertRaises(ContractError): e.complete(state(e.get()["known_facts"]))
        e.publish_commit("commit-1")
        e.complete(state(e.get()["known_facts"]))
        self.assertEqual(e.get()["phase"], "Finalizing")
        self.assertFalse(e.wait_terminal())
        with self.assertRaises(ContractError): e.publish_commit("commit-1")
        e.finalize()
        self.assertEqual(e.publication_count, 1)

    def test_T20_execution_observation_revision_independent(self):
        e = running()
        initial = int(e.get()["observation_version"])
        e.progress(10)
        e.record_commit("commit-1", "settings", "41", durability="DurableCommitted")
        e.publish_commit("commit-1")
        e.complete(state(e.get()["known_facts"]))
        s = e.get()
        self.assertGreater(int(s["observation_version"]), initial)
        self.assertNotEqual(s["observation_version"], s["outcome"]["revision"])
        self.assertEqual(s["known_facts"], s["outcome"]["known_facts"])
        validate_snapshot(s)

    def test_T20_execution_snapshot_consistency(self):
        e = running()
        first = e.get()
        first["phase"] = "Terminal"
        first["known_facts"].append({"fake": True})
        self.assertEqual(e.get()["phase"], "Running")
        self.assertEqual(e.get()["known_facts"], [])
        e.complete(read())
        s = e.get()
        s["outcome"]["known_facts"] = effect()["known_facts"]
        with self.assertRaises(ContractError): validate_snapshot(s)
        s = e.get()
        s["phase"] = "Terminal"
        with self.assertRaises(ContractError): validate_snapshot(s)

    def test_T20_execution_version_exhaustion(self):
        e = running(version_limit=4)
        while int(e.get()["observation_version"]) < 4: e.progress(1)
        before = e.get()
        with self.assertRaises(ContractError): e.progress(2)
        self.assertEqual(e.get(), before)
        self.assertEqual(e.get()["observation_version"], "4")

    def test_T12_execution_append_only_resolution(self):
        e = running()
        e.complete(unknown())
        e.finalize()
        original = e.get()
        resolved = effect("Applied", "Succeeded")
        resolved["known_facts"] = original["known_facts"] + resolved["known_facts"] + [dict(
            kind="ResolutionRecord", fact_id="r1", unknown_id="u1", determination="Applied",
            evidence_ref="device-receipt-1")]
        e.reconcile(resolved)
        self.assertEqual(e.get()["phase"], "Terminal")
        self.assertEqual(e.get()["known_facts"][:1], original["known_facts"])
        self.assertEqual(e.completion_signals, 1)
        with self.assertRaises(ContractError): e.reconcile(effect("NotApplied", "Failed"))
        with self.assertRaises(ContractError): e.transition("Running")

    def test_T20_observer_late_notification(self):
        e = running()
        older = e.get()
        e.complete(read())
        e.finalize()
        observer = Observer()
        observer.snapshot(e.get())
        self.assertFalse(observer.notification(older))
        self.assertTrue(observer.current["quiescent"])
        self.assertFalse(observer.notification(e.get()))
        other = dict(older, host_incarnation="host-2", observation_version="1")
        self.assertFalse(observer.notification(other))
        self.assertTrue(observer.needs_resync)
        observer.snapshot(other)
        self.assertEqual(observer.current["host_incarnation"], "host-2")

    def test_T20_observer_lost_terminal_notification(self):
        e = running()
        observer = Observer()
        observer.snapshot(e.get())
        e.complete(read())
        e.finalize()  # 外部提示全部丢弃，不影响可靠本地完成。
        self.assertTrue(e.wait_terminal())
        self.assertEqual(e.completion_signals, 1)
        observer.notification(None, gap=True)
        self.assertTrue(observer.needs_resync)
        observer.snapshot(e.get())
        self.assertFalse(observer.needs_resync)
        self.assertEqual(observer.current["phase"], "Terminal")

    def test_T12_execution_reject_forged_publication(self):
        donor = running()
        donor.record_commit("commit-1", "settings", "41", durability="DurableCommitted")
        donor.publish_commit("commit-1")
        forged = state(donor.get()["known_facts"])
        e = running()
        with self.assertRaises(ContractError): e.complete(forged)
        with self.assertRaises(ContractError): e.record_facts(forged["known_facts"])
        self.assertEqual(e.publication_count, 0)

    def test_T06_execution_child_unknown_aggregation(self):
        parent, child = running(), running(execution_ref="child-1")
        parent.attach_child(child)
        parent.complete(failed())
        child.complete(unknown())
        child.finalize()
        with self.assertRaises(ContractError): parent.finalize()
        parent.collect_children(unknown())
        parent.finalize()
        self.assertEqual(parent.get()["outcome"]["kind"], "Indeterminate")
        self.assertEqual(parent.get()["known_facts"], child.get()["known_facts"])
        self.assertEqual(parent.callback_count, 1)

    def test_T20_execution_child_projection_version(self):
        parent, child = running(), running(execution_ref="child-1")
        parent.attach_child(child)
        before = parent.get()
        child.complete(read())
        child.finalize()
        after = parent.get()
        self.assertTrue(before == after or int(after["observation_version"]) > int(before["observation_version"]))

    def test_T20_observer_out_of_order_snapshot(self):
        e = running()
        old = e.get()
        e.complete(read())
        e.finalize()
        observer = Observer()
        observer.snapshot(e.get())
        observer.snapshot(old)
        self.assertEqual(observer.current["phase"], "Terminal")
        self.assertEqual(observer.current["observation_version"], e.get()["observation_version"])

    def test_T06_execution_queued_cancel(self):
        e = Execution()
        e.publish_record()
        e.cancel()
        e.cancel_before_apply()
        e.finalize()
        self.assertEqual(e.get()["outcome"]["kind"], "CancelledBeforeApply")
        self.assertEqual((e.callback_count, e.work_count, e.completion_signals), (0, 0, 1))
        with self.assertRaises(ContractError): e.dispatch()

    def test_T12_execution_post_observation_error(self):
        e = running()
        e.complete(read())
        original = e.get()["outcome"]
        e.observation_failed("ExporterUnavailable")
        e.finalize()
        self.assertEqual(e.get()["outcome"], original)
        self.assertEqual(e.get()["post_observation_errors"], ["ExporterUnavailable"])
        self.assertFalse(e.get()["writes_blocked"])

    def test_T12_execution_commit_ledger_repair(self):
        e = running(required_record=True, max_record_attempts=1)
        e.record_commit("commit-1", "settings", "41", durability="DurableCommitted")
        e.publish_commit("commit-1")
        e.complete(state(e.get()["known_facts"]))
        facts = e.get()["known_facts"]
        e.record_attempt(success=False)
        self.assertEqual(e.get()["repair_strategy"], "CommitLedger")
        self.assertEqual(e.get()["known_facts"], facts)
        self.assertEqual(e.get()["outcome"]["kind"], "StateCommitted")
        e.finalize()

    def test_T06_execution_normal_phase_path(self):
        e = Execution()
        e.publish_record()
        e.transition("WaitingResources")
        e.dispatch()
        e.transition("WaitingChild")
        e.transition("Running")
        e.complete(read())
        e.finalize()
        self.assertTrue(e.wait_terminal())

    def test_T12_outcome_failed_record_projection(self):
        e = running()
        e.complete(effect())
        s = e.get()
        s["outcome"]["evidence"] = "RequiredRecordFailed"
        with self.assertRaises(ContractError): validate_snapshot(s)
        e = running(required_record=True, max_record_attempts=1)
        e.complete(unknown())
        e.record_attempt(success=False)
        e.finalize()
        resolved = effect("Applied", "Succeeded")
        resolved["known_facts"] = e.get()["known_facts"] + resolved["known_facts"] + [dict(
            kind="ResolutionRecord", fact_id="r1", unknown_id="u1", determination="Applied", evidence_ref="receipt-1")]
        e.reconcile(resolved)
        validate_snapshot(e.get())
        self.assertEqual(e.get()["outcome"]["evidence"], "RequiredRecordFailed")

    def test_T06_execution_plan_child_actual_success(self):
        cancelled = dict(kind="CancelledBeforeApply", evidence="Volatile", known_facts=[],
                         reason="Cancelled", proof="NoAppliedStateOrEffect")
        for route, child_result in itertools.product(("complete", "collect"), (failed(), cancelled, effect(), lifecycle())):
            with self.subTest(route=route, kind=child_result["kind"]):
                parent, child = running(execution_ref="parent"), running(execution_ref="child")
                parent.attach_child(child)
                if route == "collect": parent.complete(failed("ParentStepFailed"))
                if child_result["kind"] == "CancelledBeforeApply": child.cancel()
                child.complete(child_result)
                child.finalize()
                before = parent.get()
                with self.assertRaises(ContractError):
                    if route == "complete": parent.complete(plan(child_result["known_facts"]))
                    else: parent.collect_children(plan(child_result["known_facts"]))
                self.assertEqual(parent.get(), before)
                self.assertEqual(parent.release_count, 0)
        parent, child = running(execution_ref="parent"), running(execution_ref="child")
        parent.attach_child(child)
        child.complete(effect("Applied", "Succeeded"))
        child.finalize()
        parent.complete(plan(child.get()["known_facts"]))
        parent.finalize()
        parent, child = running(execution_ref="parent"), running(execution_ref="child")
        parent.attach_child(child)
        parent.complete(failed("ParentStepFailed"))
        child.complete(read())
        child.finalize()
        with self.assertRaises(ContractError): parent.collect_children(plan())
        parent.collect_children(failed("ParentStepFailed"))
        parent.finalize()

    def test_T12_outcome_resolution_final_consistency(self):
        u = dict(kind="UnknownFact", fact_id="unknown-commit", boundary="StorageCommit",
                 reference_id="commit-1", reconcile="query-ledger")
        r = dict(kind="ResolutionRecord", fact_id="resolution-commit", unknown_id="unknown-commit",
                 determination="NotApplied", evidence_ref="ledger-absence-proof")
        c = dict(kind="CommitFact", fact_id="commit:commit-1", commit_id="commit-1", domain="settings",
                 revision="41", durability="DurableCommitted")
        publication = dict(kind="PublishedFact", fact_id="published:commit-1", commit_id="commit-1", published_version="41")
        validate_outcome(dict(failed(), known_facts=[u, r]))
        with self.assertRaises(ContractError): validate_outcome(state([u, r, c, publication]))
        e = running()
        e.record_facts([u, r])
        before = e.get()
        with self.assertRaises(ContractError): e.record_commit("commit-1", "settings", "41", durability="DurableCommitted")
        self.assertEqual(e.get(), before)
        applied = dict(r, determination="Applied", evidence_ref="ledger-commit-proof")
        validate_outcome(state([u, c, applied, publication]))

    def test_T06_execution_owner_acyclic(self):
        a, b, c = (running(execution_ref=name) for name in ("a", "b", "c"))
        a.attach_child(b)
        b.attach_child(c)
        for descendant in (b, c):
            before = descendant.get()
            with self.assertRaises(ContractError): descendant.attach_child(a)
            self.assertEqual(descendant.get(), before)
        parent, other, child = (running(execution_ref=name) for name in ("ExecutionOwner", "other", "child"))
        parent.attach_child(child)
        with self.assertRaises(ContractError): other.attach_child(child)
        parent.transfer_child("child", "IndependentOwner")
        with self.assertRaises(ContractError): other.attach_child(child)
        normal, transferred = running(execution_ref="normal"), running(execution_ref="transferred")
        normal.attach_child(transferred)
        normal.transfer_child("transferred", "ExecutionOwner")
        with self.assertRaises(ContractError): other.attach_child(transferred)

    def test_T06_execution_cancel_propagates_owned_children(self):
        p, c, grandchild, independent = (running(execution_ref=name) for name in ("p", "c", "g", "independent"))
        p.attach_child(c)
        c.attach_child(grandchild)
        p.attach_child(independent)
        p.transfer_child("independent", "IndependentOwner")
        self.assertEqual(p.cancel(), "Requested")
        c.cancel_before_apply()
        grandchild.cancel_before_apply()
        with self.assertRaises(ContractError): independent.cancel_before_apply()
        late = running(execution_ref="late")
        p.attach_child(late)
        late.cancel_before_apply()
        claimed, active = running(execution_ref="claimed"), running(execution_ref="active")
        claimed.attach_child(active)
        claimed.record_facts(effect()["known_facts"])
        before = claimed.get()["known_facts"]
        self.assertEqual(claimed.cancel(), "AlreadyClaimed")
        active.cancel_before_apply()
        self.assertEqual(claimed.get()["known_facts"], before)

    def test_T12_execution_collected_outcome_frozen(self):
        parent, child = running(required_record=True, execution_ref="p"), running(execution_ref="c")
        parent.attach_child(child)
        child.complete(read())
        child.finalize()
        parent.complete(read())
        parent.record_attempt(success=True)
        recorded = parent.get()
        with self.assertRaises(ContractError): parent.collect_children(plan())
        self.assertEqual(parent.get(), recorded)
        parent.finalize()
        self.assertEqual(parent.get()["outcome"]["kind"], "ReadCompleted")
        parent, child = running(execution_ref="p"), running(execution_ref="c")
        parent.attach_child(child)
        parent.complete(read())
        child.complete(read())
        child.finalize()
        parent.collect_children(plan())
        with self.assertRaises(ContractError): parent.collect_children(read())
        parent.finalize()

    def test_T19_schema_golden(self):
        cases = json.loads((HERE / "golden" / "outcomes.json").read_text(encoding="utf-8"))
        self.assertGreaterEqual(len(cases), 24)
        for case in cases:
            with self.subTest(case=case["name"]):
                if case["schema_valid"]: validate_schema(case["document"])
                else:
                    with self.assertRaises(ContractError): validate_schema(case["document"])
                if case["valid"]: validate_document(case["document"])
                else:
                    with self.assertRaises(ContractError): validate_document(case["document"])

    def test_T19_schema_expected_manifest(self):
        manifest = json.loads((HERE.parents[1] / "manifests" / "d0.03.expected.json").read_text(encoding="utf-8"))
        actual = sorted(name.removeprefix("test_").replace("_", ".", 2)
                        for name in dir(type(self)) if name.startswith("test_T"))
        self.assertEqual(sorted(case["id"] for case in manifest["cases"]), actual)


if __name__ == "__main__":
    unittest.main(verbosity=2)

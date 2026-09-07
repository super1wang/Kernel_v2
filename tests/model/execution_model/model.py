"""D0.03 单线程状态机。每个方法是一个原子模型步，不证明真实锁/线程实现。"""
from copy import deepcopy
import json
from pathlib import Path
from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parents[3]
SCHEMA = json.loads((ROOT / "schemas/outcome-v1.schema.json").read_text(encoding="utf-8"))
Draft202012Validator.check_schema(SCHEMA)
VALIDATOR = Draft202012Validator(SCHEMA)
OUTCOMES = ("ReadCompleted", "StateCommitted", "EffectResolved", "LifecycleResolved", "PlanCompleted",
            "FailedBeforeApply", "CancelledBeforeApply", "PartialCompletion", "Indeterminate")
MAX_VERSION = 2**64 - 1
PHASE_EDGES = {"Queued": {"WaitingResources", "Running", "Finalizing", "Suspended"},
    "WaitingResources": {"Running", "Finalizing", "Suspended"},
    "Running": {"WaitingChild", "Finalizing", "Suspended"},
    "WaitingChild": {"Running", "Finalizing", "Suspended"},
    "Finalizing": {"Terminal"}, "Suspended": set(), "Terminal": set()}


class ContractError(ValueError):
    """预期拒绝，且调用者的可见投影不得部分更新。"""


def require(condition, reason):
    if not condition: raise ContractError(reason)


def validate_schema(document):
    errors = list(VALIDATOR.iter_errors(document))
    require(not errors, "Schema: " + (errors[0].message[:400] if errors else ""))


def facts_info(facts):
    ids, commits, published, effects, lifecycle, unknown, resolutions = {}, {}, {}, {}, {}, {}, {}
    for f in facts:
        fid, kind = f["fact_id"], f["kind"]
        require(fid not in ids, "重复 fact_id")
        ids[fid] = f
        for key in ("revision", "published_version", "generation"):
            if key in f: require(int(f[key]) <= MAX_VERSION, "64 位计数溢出")
        if kind == "CommitFact":
            require(f["commit_id"] not in commits, "提交事实不能改写或重复")
            commits[f["commit_id"]] = f
        elif kind == "PublishedFact":
            cid = f["commit_id"]
            require(cid in commits and cid not in published, "发布必须引用唯一已知提交")
            require(f["published_version"] == commits[cid]["revision"], "发布版本不匹配")
            published[cid] = f
        elif kind == "EffectFact":
            require(f["effect_id"] not in effects, "效果事实不能改写")
            effects[f["effect_id"]] = f
        elif kind == "LifecycleFact":
            require(f["transition_id"] not in lifecycle, "转换事实不能改写")
            lifecycle[f["transition_id"]] = f
        elif kind == "UnknownFact": unknown[fid] = f
        elif kind == "ResolutionRecord":
            uid = f["unknown_id"]
            require(uid in unknown and uid not in resolutions, "对账只解决先前且未解决的未知")
            u = unknown[uid]
            if u["boundary"] == "ExternalEffect":
                fact = effects.get(u["reference_id"])
                require(fact is not None and fact["application"] == f["determination"], "对账与已知效果不匹配")
            else:
                present = u["reference_id"] in commits
                require(f["determination"] in ("NotApplied", "Applied"), "提交不存在部分应用")
                require(present == (f["determination"] == "Applied"), "对账与提交事实不匹配")
            resolutions[uid] = f
    # 所有确定对账在最终追加集合上仍须成立，不能由后续同身份事实推翻。
    for uid, resolution in resolutions.items():
        boundary = unknown[uid]
        if boundary["boundary"] == "StorageCommit":
            require((boundary["reference_id"] in commits) == (resolution["determination"] == "Applied"),
                    "追加提交不能推翻先前确定对账")
        else:
            require(effects[boundary["reference_id"]]["application"] == resolution["determination"],
                    "追加效果不能推翻先前确定对账")
    applied = bool(commits) or any(f["application"] != "NotApplied" for f in effects.values()) or any(
        f["before"] != f["after"] for f in lifecycle.values())
    return dict(commits=commits, published=published, effects=effects, lifecycle=lifecycle,
                unresolved=set(unknown) - set(resolutions), applied=applied)


def validate_outcome(value):
    validate_schema(value)
    require(value.get("kind") in OUTCOMES, "不是 Outcome")
    info = facts_info(value["known_facts"])
    kind = value["kind"]
    if kind == "Indeterminate":
        require(set(value["unknown_ids"]) == info["unresolved"] and bool(info["unresolved"]), "未知边界清单不完整")
        return
    require(not info["unresolved"], "确定 Outcome 不能掩盖未解决未知")
    if kind in ("ReadCompleted", "FailedBeforeApply", "CancelledBeforeApply"):
        require(not info["applied"], "无应用结果不能覆盖已发生效果")
    if kind == "StateCommitted":
        c = info["commits"].get(value["commit_id"])
        p = info["published"].get(value["commit_id"])
        require(c is not None and p is not None, "StateCommitted 成功必须经过 publication gate")
        require(c["domain"] == value["domain"] and c["revision"] == value["revision"] and
                p["published_version"] == value["published_version"], "提交投影与事实不一致")
        require(len(info["commits"]) == 1 and not info["effects"] and not info["lifecycle"], "单次状态成功恰好一个提交")
        if c["durability"] == "DurableCommitted": require(value["evidence"] != "Volatile", "耐久事实不能降为易失证据")
    elif kind == "EffectResolved":
        fact = info["effects"].get(value["effect_id"])
        require(fact is not None and fact["application"] == value["application"], "效果结果必须匹配发送事实")
    elif kind == "LifecycleResolved":
        fact = info["lifecycle"].get(value["transition_id"])
        require(fact is not None and all(fact[k] == value[k] for k in ("before", "after", "generation")), "生命周期前后事实不一致")
    elif kind in ("PlanCompleted", "PartialCompletion"):
        steps = value["steps"]
        require(len({s["step_id"] for s in steps}) == len(steps), "步骤身份重复")
        statuses = {s["status"] for s in steps}
        if kind == "PlanCompleted":
            require(statuses <= {"Succeeded"}, "完整成功要求所有必要节点成功")
            require(set(info["commits"]) <= set(info["published"]), "计划成功不能暴露未发布提交")
        else:
            require(info["applied"] and bool(statuses & {"Failed", "Cancelled"}) and "Unknown" not in statuses,
                    "PartialCompletion 必须有确定效果与未成功步骤且无未知")


def validate_snapshot(value):
    validate_schema(value)
    require(value.get("kind") == "ExecutionObservation", "不是执行观察")
    require(0 < int(value["observation_version"]) <= MAX_VERSION, "观察版本不允许零或回绕")
    info = facts_info(value["known_facts"])
    if value["outcome"] is not None:
        validate_outcome(value["outcome"])
        if value["outcome"]["evidence"] == "RequiredRecordFailed":
            require(value["required_record_state"] == "Failed", "证据故障不能伪装无必要记录失败")
        require(value["outcome"]["known_facts"] == value["known_facts"], "get 拼接了不一致事实")
    if value["progress"] and value["progress"]["scope"] == "Published":
        require(bool(info["published"]), "候选进度不能伪装发布")
    if value["required_record_state"] == "Failed":
        require(value["outcome"] is not None and value["outcome"]["evidence"] == "RequiredRecordFailed", "记录失败必须明确保留证据故障")
    if value["phase"] == "Terminal": require(not value["durable_pins"], "终结必须释放当前执行的 durable pins")


def validate_document(value):
    validate_schema(value)
    kind = value["kind"]
    if kind in OUTCOMES: validate_outcome(value)
    elif kind == "Completed": validate_outcome(value["outcome"])
    elif kind == "ExecutionObservation": validate_snapshot(value)


def outcome_succeeded(outcome):
    """完整计划的必要节点成功：业务成功且必要记录没有未修复故障。"""
    if outcome is None or outcome["evidence"] == "RequiredRecordFailed": return False
    if outcome["kind"] in ("ReadCompleted", "StateCommitted", "PlanCompleted"): return True
    return outcome["kind"] in ("EffectResolved", "LifecycleResolved") and outcome["status"] == "Succeeded"


class Execution:
    """身份发布、Executor 回调、必要收尾的一个受管理执行模型。"""
    def __init__(self, execution_ref="exec-1", host_incarnation="host-1", acceptance="Volatile",
                 required_record=False, max_record_attempts=3, recoverable=False, version_limit=MAX_VERSION):
        require(bool(execution_ref) and bool(host_incarnation), "身份不能为空")
        require(acceptance in ("Volatile", "DurableAccepted"), "非法接受保证")
        require(0 < version_limit <= MAX_VERSION and max_record_attempts > 0, "非法预算")
        self.execution_ref, self.host_incarnation = execution_ref, host_incarnation
        self.acceptance, self.owner = acceptance, "ExecutionOwner"
        self._parent, self._independent_owner = None, None
        self._phase, self._version, self._version_limit = "Queued", 0, version_limit
        self._record_published, self._acceptance_persisted = False, False
        self._facts, self._outcome, self._progress = [], None, None
        self._pending_outcome, self._transferred_facts = None, {}
        self._post_errors, self._repair_strategy = [], None
        self._pending_callback, self._dispatched, self._cancel_requested = False, False, False
        self._locals, self._children, self._transferred = set(), {}, set()
        self._record_state = "Pending" if required_record else "NotRequired"
        self._attempts, self._max_attempts = 0, max_record_attempts
        self._writes_blocked, self._fault, self._isolation_owner = False, None, None
        self._recoverable, self._durable_pins = recoverable, False
        self._resume_phase = None
        self.callback_count = self.release_count = self.publication_count = self.completion_signals = self.work_count = 0

    def _before_change(self):
        require(self._version < self._version_limit, "observation_version 耗尽，拒绝变更，禁止回绕")

    def _changed(self): self._version += 1

    def publish_record(self):
        require(not self._record_published, "执行记录只能发布一次")
        self._before_change()
        self._record_published = True
        self._changed()

    def persist_acceptance(self):
        require(self._record_published and not self._dispatched, "接受事务必须在派发之前完成")
        self._acceptance_persisted = True

    def accepted_reply(self):
        require(self._record_published, "Accepted 前必须有可由完成器引用的记录")
        require(self.acceptance != "DurableAccepted" or self._acceptance_persisted, "未持久接受不可返回 DurableAccepted")
        return dict(kind="Accepted", execution_ref=self.execution_ref, acceptance_guarantee=self.acceptance)

    def dispatch(self, disposition="Accepted", inline=None):
        self.accepted_reply()
        require(not self._dispatched and self._phase in ("Queued", "WaitingResources"), "不能重复派发")
        require(disposition in ("Accepted", "Rejected", "Exception"), "非法 Executor 返回")
        require(inline is None or disposition == "Accepted", "拒绝/异常不得持有或调用 work")
        if inline is not None: validate_outcome(inline)
        self._before_change()
        self._dispatched = True
        if disposition == "Accepted":
            self._phase, self._pending_callback = "Running", True
            self.work_count += 1
        else:
            self._phase = "Finalizing"
            self._outcome = dict(kind="FailedBeforeApply", evidence="Volatile", known_facts=[],
                failure_phase="Queued", reason="Executor" + disposition, proof="NoAppliedStateOrEffect")
        self._changed()
        if inline is not None: self.complete(inline)

    def transition(self, target):
        require(target in PHASE_EDGES[self._phase], "非法 phase 转换")
        if target == "Terminal": return self.finalize()
        require(target not in ("Suspended", "Finalizing"), "挂起/完成必须使用有合同检查的专用操作")
        require(target != "Running" or self._pending_callback, "运行必须由已接受的 Executor 持有")
        self._before_change()
        self._phase = target
        self._changed()

    def record_facts(self, facts):
        require(all(f.get("kind") not in ("CommitFact", "PublishedFact") for f in facts), "状态事实只允许协调器记录/发布端口产生")
        self._append_facts(facts)

    def _append_facts(self, facts):
        require(self._phase in ("Running", "WaitingChild") and self._outcome is None, "事实追加阶段非法")
        trial = deepcopy(self._facts) + deepcopy(facts)
        for f in trial:
            errors = list(Draft202012Validator(dict(SCHEMA, **{"oneOf": [{"$ref": "#/$defs/known_fact"}]})).iter_errors(f))
            require(not errors, "非法 KnownFact")
        facts_info(trial)
        self._before_change()
        self._facts = trial
        self._changed()

    def record_commit(self, commit_id, domain, revision, durability="Memory"):
        self._append_facts([dict(kind="CommitFact", fact_id="commit:" + commit_id, commit_id=commit_id,
                               domain=domain, revision=revision, durability=durability)])

    def publish_commit(self, commit_id):
        info = facts_info(self._facts)
        require(commit_id in info["commits"] and commit_id not in info["published"], "缺少提交或重复发布")
        c = info["commits"][commit_id]
        self._append_facts([dict(kind="PublishedFact", fact_id="published:" + commit_id,
                               commit_id=commit_id, published_version=c["revision"])])
        self.publication_count += 1

    def complete(self, outcome):
        require(self._pending_callback and self._phase in ("Running", "WaitingChild"), "重复/迟到/未接受的 callback")
        validate_outcome(outcome)
        require(outcome["evidence"] != "RequiredRecordFailed", "必要记录失败必须经过有界收尾分流")
        require(outcome["known_facts"][:len(self._facts)] == self._facts, "KnownFacts 只能追加")
        if outcome["kind"] == "StateCommitted":
            require(outcome["known_facts"] == self._facts, "callback 不能伪造协调器 publication gate")
        if outcome["kind"] == "CancelledBeforeApply": require(self._cancel_requested, "取消尚未赢得决定点")
        if outcome["kind"] == "PlanCompleted":
            self._check_plan_children()
        if self._children_pending() == 0: self._check_child_facts(outcome)
        self._before_change()
        self._facts = deepcopy(outcome["known_facts"])
        if self._children_pending(): self._pending_outcome = deepcopy(outcome)
        else: self._outcome = deepcopy(outcome)
        self._pending_callback, self._phase = False, "Finalizing"
        self.callback_count += 1
        self._changed()

    def local_started(self, token):
        require(self._phase in ("Running", "WaitingChild") and token and token not in self._locals, "非法本地代码寿命")
        self._before_change()
        self._locals.add(token)
        self._changed()

    def local_drained(self, token):
        require(token in self._locals, "未知或重复排空")
        self._before_change()
        self._locals.remove(token)
        self._changed()

    def record_attempt(self, success):
        require(type(success) is bool and self._phase == "Finalizing" and self._record_state == "Pending" and self._outcome is not None, "记录尝试阶段非法或预算已耗尽")
        self._before_change()
        self._attempts += 1
        if success: self._record_state = "Recorded"
        elif self._attempts >= self._max_attempts:
            self._record_state, self._writes_blocked, self._fault = "Failed", True, "RequiredRecordFailed"
            self._outcome["evidence"] = "RequiredRecordFailed"
            info = facts_info(self._facts)
            self._repair_strategy = "CommitLedger" if any(c["durability"] == "DurableCommitted" for c in info["commits"].values()) else "ReceiptReconcile" if info["effects"] or info["unresolved"] else "ManualReview"
        self._changed()

    def isolate(self, owner):
        require(self._phase == "Finalizing" and self._record_state == "Failed" and owner and self._locals,
                "IsolationOwner 必须明确接管尚未排空代码的寿命")
        self._before_change()
        self._isolation_owner = self.owner = owner
        self._changed()

    def attach_child(self, child):
        require(self._phase in ("Running", "WaitingChild") and child is not self and
                child.execution_ref != self.execution_ref and child.execution_ref not in self._children,
                "子执行必须有独立身份与唯一父 owner")
        require(child._parent is None and child._independent_owner is None, "子执行已有 owner")
        ancestor = self
        while ancestor is not None:
            require(ancestor is not child and ancestor.execution_ref != child.execution_ref, "父子 owner 必须无环且祖先身份唯一")
            ancestor = ancestor._parent
        self._before_change()
        self._children[child.execution_ref] = child
        child.owner, child._parent = self.execution_ref, self
        self._changed()
        if self._cancel_requested: child.cancel()

    def transfer_child(self, child_id, trusted_owner):
        require(child_id in self._children and child_id not in self._transferred and trusted_owner and
                trusted_owner != self.execution_ref, "必须由显式可信独立 owner 接管")
        require(self._phase != "Terminal", "父终态不能再交接")
        self._before_change()
        self._children[child_id].owner = trusted_owner
        self._children[child_id]._parent = None
        self._children[child_id]._independent_owner = trusted_owner
        self._transferred.add(child_id)
        self._transferred_facts[child_id] = deepcopy(self._children[child_id]._facts)
        self._changed()

    def _children_pending(self):
        return sum(not c.wait_terminal() for cid, c in self._children.items() if cid not in self._transferred)

    def _check_plan_children(self):
        require(all(c.wait_terminal() and outcome_succeeded(c._outcome) for c in self._children.values()),
                "PlanCompleted 要求每个必要 child 实际成功并完成收尾")

    def _check_child_facts(self, outcome):
        for cid, child in self._children.items():
            facts = self._transferred_facts[cid] if cid in self._transferred else child._facts
            require(all(f in outcome["known_facts"] for f in facts), "父结果不能删除子回执或掩盖子未知")

    def collect_children(self, outcome):
        require(self._phase == "Finalizing" and self._children and self._children_pending() == 0 and
                self._pending_outcome is not None,
                "先收集并收尾必要 child，再确定父 Outcome")
        validate_outcome(outcome)
        require(outcome["known_facts"][:len(self._facts)] == self._facts, "父收尾事实只追加")
        self._check_child_facts(outcome)
        if outcome["kind"] == "PlanCompleted":
            self._check_plan_children()
            require(outcome_succeeded(self._pending_outcome), "父自身已知失败不能在子收尾时升级为完整成功")
        self._before_change()
        self._facts, self._outcome = deepcopy(outcome["known_facts"]), deepcopy(outcome)
        if self._record_state == "Failed": self._outcome["evidence"] = "RequiredRecordFailed"
        self._pending_outcome = None
        self._changed()

    def observation_failed(self, reason):
        require(bool(reason) and self._record_published, "PostObservationError 必须有原因和记录")
        self._before_change()
        self._post_errors.append(reason)
        self._changed()

    def finalize(self):
        require(self._phase == "Finalizing", "只有必要收尾可以完成执行")
        outcome = self._outcome if self._outcome is not None else self._pending_outcome
        require(outcome is not None, "父执行尚无确定收尾材料")
        self._check_child_facts(outcome)
        if outcome["kind"] == "PlanCompleted": self._check_plan_children()
        require(not self._pending_callback and not self._locals and self._children_pending() == 0, "本地 callback/child 未排空，禁止释放依赖")
        require(self._record_state != "Pending", "必要记录仍未收尾")
        self._before_change()
        self._outcome, self._pending_outcome = deepcopy(outcome), None
        self._phase, self._durable_pins = "Terminal", False
        self.release_count += 1
        self.completion_signals += 1
        self._changed()

    def cancel(self):
        if self._phase == "Terminal": return "AlreadyTerminal"
        self._cancel_requested = True
        for cid, child in self._children.items():
            if cid not in self._transferred: child.cancel()
        if self._outcome is not None or facts_info(self._facts)["applied"] or facts_info(self._facts)["unresolved"]:
            return "AlreadyClaimed"
        return "Requested"

    def cancel_before_apply(self):
        require(self._cancel_requested, "必须先有取消意图")
        outcome = dict(kind="CancelledBeforeApply", evidence="Volatile", known_facts=deepcopy(self._facts),
                       reason="Cancelled", proof="NoAppliedStateOrEffect")
        if self._phase in ("Queued", "WaitingResources") and not self._dispatched:
            require(self._record_published, "未接受身份不能形成业务取消 Outcome")
            validate_outcome(outcome)
            self._before_change()
            self._outcome, self._phase = outcome, "Finalizing"
            self._changed()
        else: self.complete(outcome)

    def progress(self, percent, scope="Work"):
        require(self._phase in ("Running", "WaitingChild"), "终态/收尾不可被迟到进度更新")
        require(type(percent) is int and 0 <= percent <= 100 and scope in ("Work", "Candidate", "Published"), "非法进度")
        require(scope != "Published" or bool(facts_info(self._facts)["published"]), "候选不等于发布")
        self._before_change()
        self._progress = dict(percent=percent, scope=scope)
        self._changed()

    def suspend(self, durable_pins):
        require(self._recoverable and durable_pins and "Suspended" in PHASE_EDGES[self._phase] and not self._locals,
                "挂起必须可恢复、有 durable pins 且无本地代码继续运行")
        self._before_change()
        self._resume_phase = self._phase
        self._phase, self._durable_pins = "Suspended", True
        self._changed()

    def resume(self):
        require(self._phase == "Suspended" and self._durable_pins, "仅显式恢复同一挂起身份")
        self._before_change()
        self._phase, self._durable_pins = self._resume_phase, False
        self._resume_phase = None
        self._changed()

    def reconcile(self, resolved):
        require(self._phase == "Terminal" and self._outcome["kind"] == "Indeterminate", "只追加终结未知的对账")
        validate_outcome(resolved)
        require(resolved["kind"] != "Indeterminate" and len(resolved["known_facts"]) > len(self._facts) and
                resolved["known_facts"][:len(self._facts)] == self._facts, "对账不能覆盖未知原始事实")
        self._before_change()
        self._facts, self._outcome = deepcopy(resolved["known_facts"]), deepcopy(resolved)
        if self._record_state == "Failed": self._outcome["evidence"] = "RequiredRecordFailed"
        self._changed()

    def wait_terminal(self): return self._phase == "Terminal"

    def get(self):
        require(self._record_published, "没有可查询执行身份")
        # 方法原子执行并返回拥有型复制；真实实现需短同步/串行协议保证此同次投影。
        return deepcopy(dict(kind="ExecutionObservation", execution_ref=self.execution_ref,
            host_incarnation=self.host_incarnation, observation_version=str(self._version), phase=self._phase,
            progress=self._progress, known_facts=self._facts, outcome=self._outcome,
            quiescent=self._phase == "Terminal", durable_pins=self._durable_pins,
            writes_blocked=self._writes_blocked, fault=self._fault, isolation_owner=self._isolation_owner,
            local_pending=len(self._locals) + int(self._pending_callback and self._phase != "Suspended"),
            required_record_state=self._record_state, post_observation_errors=self._post_errors,
            repair_strategy=self._repair_strategy))


class Observer:
    """仅验证 A17 的版本合并语义，不实现订阅 wire/连接/权限。"""
    def __init__(self): self.current, self.needs_resync = None, True

    def snapshot(self, value):
        validate_snapshot(value)
        if self.current is not None:
            require(value["execution_ref"] == self.current["execution_ref"], "不能替换其他执行的快照")
            if value["host_incarnation"] == self.current["host_incarnation"]:
                if int(value["observation_version"]) < int(self.current["observation_version"]): return False
                if value["observation_version"] == self.current["observation_version"]:
                    require(value == self.current, "同观察版本必须对应同一投影")
        self.current, self.needs_resync = deepcopy(value), False
        return True

    def notification(self, value, gap=False):
        if gap:
            self.needs_resync = True
            return False
        require(value is not None, "空通知必须显式标记 gap")
        validate_snapshot(value)
        if self.current is None or value["host_incarnation"] != self.current["host_incarnation"]:
            self.needs_resync = True
            return False
        require(value["execution_ref"] == self.current["execution_ref"], "不能合并其他执行")
        if int(value["observation_version"]) <= int(self.current["observation_version"]): return False
        require(self.current["phase"] != "Terminal" or value["phase"] == "Terminal", "迟到提示不能重开终态")
        self.current = deepcopy(value)
        return True

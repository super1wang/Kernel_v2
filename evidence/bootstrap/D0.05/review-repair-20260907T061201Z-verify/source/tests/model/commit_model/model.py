"""D0.05 提交/许可/效果参考模型。每个方法是一个有界串行化语义步。

存储镜像通过副本提交模拟事务原子性；故障点在实际字段写入之间。
这不是 SQLite、Runtime、线程安全证明或生产授权实现。
"""
from contextlib import contextmanager
from copy import deepcopy
from dataclasses import dataclass, field


class ContractError(ValueError):
    pass


def require(condition, reason):
    if not condition:
        raise ContractError(reason)


@dataclass(frozen=True)
class Binding:
    caller: str
    operation: str
    target: str
    permission_generation: int
    lifecycle_generation: int
    deadline: int


class LockDiscipline:
    """参考锁序：domain 短锁→policy 短仲裁；DB writer 不反向等待。"""
    def __init__(self):
        self.held = []

    @contextmanager
    def hold(self, name):
        require(name in ('domain', 'policy', 'writer'), '未知锁')
        require(name not in self.held, '内部锁不可重入')
        require(not self.held or (self.held == ['domain'] and name == 'policy'), '锁序违规')
        self.held.append(name)
        try:
            yield
        finally:
            self.held.pop()

    def wait_db(self): require(not self.held, '等待 DB 不能持内部锁')
    def business(self): require(not self.held, '业务/可重入 validator 不能在锁内')
    def notify(self): require(not self.held, '普通观察者在锁外调用')
    def destroy_old(self): require(not self.held, '旧根析构在关键区外')
    def sync_domain(self): require('writer' not in self.held, 'DB writer 禁止反向同步等待域')


class Policy:
    def __init__(self):
        self.generations = {}
        self.permits = {}
        self.events = []

    def issue(self, binding):
        require(all((binding.caller, binding.operation, binding.target)), '许可身份为空')
        require(binding.permission_generation == self.generations.get(binding.caller, 1), '权限世代过期')
        token = 'permit-' + str(len(self.permits) + 1)
        self.permits[token] = [binding, False]
        return token

    def revoke(self, caller):
        self.generations[caller] = self.generations.get(caller, 1) + 1
        self.events.append(('revoke', caller))

    def consume(self, token, binding, now):
        require(token in self.permits, '许可必须由可信 Policy 创建')
        stored, consumed = self.permits[token]
        require(not consumed, '许可只能消费一次')
        require(stored == binding, '许可绑定不匹配')
        require(now < binding.deadline, '许可已过期')
        require(binding.permission_generation == self.generations.get(binding.caller, 1), '撤权先赢')
        self.permits[token][1] = True
        self.events.append(('consume', token))

    def is_consumed(self, token):
        return self.permits[token][1]


@dataclass(frozen=True)
class PublishedState:
    value: int
    revision: int
    history: tuple
    lifecycle_generation: int


@dataclass
class Attempt:
    commit_id: str
    binding: Binding
    base_revision: int
    prepared: PublishedState
    intent_key: object
    result_valid: bool = True
    phase: str = 'ReadyToCommit'
    cancel_requested: bool = False
    worker_held: bool = False
    facts: list = field(default_factory=list)
    completion_count: int = 0


@dataclass(frozen=True)
class Reservation:
    reservation_id: int
    attempt: str
    prepared: PublishedState
    binding: Binding
    intent_key: object


@dataclass(frozen=True)
class Completion:
    reservation_id: int
    commit_id: str
    status: str


class CommitDomain:
    TX_STEPS = ('before_state', 'before_ledger', 'before_receipts', 'before_audit', 'before_outbox', 'before_commit')

    def __init__(self, durable=True, capacity=1):
        self.durable, self.capacity = durable, capacity
        self.published = PublishedState(0, 0, (), 1)
        self.disk = {'state': self.published, 'ledger': {}, 'receipts': {}, 'audit': {}, 'outbox': {}}
        self.reservation = None
        self.next_reservation = 1
        self.attempts, self.queue, self.callbacks = {}, [], []
        self.delivered, self.outbox_delivered = set(), set()
        self.locks = LockDiscipline()
        self.isolated, self.business_open, self.closed = False, True, False
        self.lifecycle_generation = 1
        self.retired, self.notifications = [], []
        self.handler_runs = 0
        self._prepared_materials, self._published_outcomes = {}, {}

    def prepare(self, commit_id, value, binding, intent_key=None):
        require(self.business_open and not self.isolated and not self.closed, '域未开放写入')
        require(type(value) is int, '参考模型的根值必须是拥有型整数')
        require(commit_id and commit_id not in self.attempts and commit_id not in self.disk['ledger'], 'CommitId 不可复用')
        require(self.published.revision < 2**64 - 1, 'revision 耗尽')
        self.locks.business()
        self.handler_runs += 1
        state = PublishedState(value, self.published.revision + 1,
                               self.published.history + (commit_id,), self.lifecycle_generation)
        attempt = Attempt(commit_id, binding, self.published.revision, state, commit_id if intent_key is None else intent_key)
        self.attempts[commit_id] = attempt
        self._prepared_materials[id(attempt)] = (commit_id, state, binding, attempt.base_revision, deepcopy(attempt.intent_key))
        return attempt

    def run_validator(self, attempt):
        require(attempt.phase == 'ReadyToCommit', '许可消费后不得再运行长业务验证')
        self.locks.business()
        require(attempt.result_valid, '输出 R 必须在消费前验证')

    def cancel(self, attempt):
        attempt.cancel_requested = True
        if attempt.phase in ('ReadyToCommit', 'Preparing'):
            attempt.phase = 'Cancelled'
            return 'Requested'
        return 'AlreadyClaimed'

    def close(self):
        require(self.reservation is None, '关闭不得抢走已 claim 的域提交 reservation')
        self.closed = True
        self.lifecycle_generation += 1

    def claim(self, attempt, policy, permit, now, activity_lease=True, resource_lease=True):
        require(self.attempts.get(attempt.commit_id) is attempt, '陌生提交对象')
        require(attempt.phase == 'ReadyToCommit' and not attempt.cancel_requested, '取消或重复 claim')
        self.run_validator(attempt)
        with self.locks.hold('domain'):
            require(self.business_open and not self.isolated and not self.closed, '域未开放')
            require(self.reservation is None, '同域已有 reservation')
            require(attempt.intent_key not in self.disk['receipts'], '该外部 intent 已有确定提交回执')
            require(attempt.base_revision == self.published.revision, '候选 base revision 冲突')
            require((attempt.commit_id, attempt.prepared, attempt.binding, attempt.base_revision, attempt.intent_key)
                    == self._prepared_materials[id(attempt)], '候选材料改变后必须重新准备与验证')
            require(attempt.prepared.revision == attempt.base_revision + 1
                    and attempt.prepared.history == self.published.history + (attempt.commit_id,)
                    and attempt.prepared.lifecycle_generation == self.lifecycle_generation,
                    '候选 revision/history/lifecycle 必须续接当前发布记录')
            require(attempt.binding.target == 'domain-1', '提交目标必须绑定本域')
            require(attempt.binding.lifecycle_generation == self.lifecycle_generation, '生命周期世代失效')
            require(activity_lease and resource_lease, '寿命和资源租约必须独立存在')
            require(not self.durable or self.capacity > 0, 'DB 队列容量不足，在消费前拒绝')
            with self.locks.hold('policy'):
                policy.consume(permit, attempt.binding, now)
            self.reservation = Reservation(self.next_reservation, attempt.commit_id, attempt.prepared, attempt.binding, attempt.intent_key)
            self.next_reservation += 1
            if self.durable: self.capacity -= 1
            attempt.phase = 'CommitClaimed'

    def submit(self, attempt):
        require(self.durable and attempt.phase == 'CommitClaimed', '仅 durable claim 可入 writer')
        require(self.reservation is not None and self.reservation.attempt == attempt.commit_id, 'reservation 不匹配')
        require(attempt.prepared == self.reservation.prepared and attempt.binding == self.reservation.binding and attempt.intent_key == self.reservation.intent_key, '许可后提交材料不可改写')
        require(not self.queue and not self.callbacks, '同一提交不能重复入队')
        self.locks.wait_db()
        attempt.worker_held = False
        self.queue.append((self.reservation, attempt))
        return self.reservation.reservation_id

    def writer_step(self, crash_at=None):
        require(len(self.queue) == 1, '没有 writer 工作')
        require(crash_at is None or crash_at in self.TX_STEPS + ('before_commit_unconfirmed', 'after_commit'), '未知故障点')
        reservation, attempt = self.queue[0]
        require(attempt.prepared == reservation.prepared and attempt.binding == reservation.binding and attempt.intent_key == reservation.intent_key, 'writer 只能提交已冻结材料')
        self.queue.pop(0)
        staged = deepcopy(self.disk)
        cid = attempt.commit_id
        with self.locks.hold('writer'):
            for name in ('state', 'ledger', 'receipts', 'audit', 'outbox'):
                if crash_at == 'before_' + name:
                    self.callbacks.append(Completion(reservation.reservation_id, cid, 'RolledBack'))
                    return
                if name == 'state': staged[name] = attempt.prepared
                elif name == 'ledger': staged[name][cid] = {'revision': attempt.prepared.revision, 'state': attempt.prepared, 'intent_key': attempt.intent_key}
                elif name == 'receipts': staged[name][attempt.intent_key] = {'kind': 'StateCommitted', 'commit_id': cid, 'revision': attempt.prepared.revision,
                                                                    'intent_key': attempt.intent_key, 'result': {'value': attempt.prepared.value}}
                else: staged[name][cid] = {'commit_id': cid, 'revision': attempt.prepared.revision}
            if crash_at == 'before_commit':
                self.callbacks.append(Completion(reservation.reservation_id, cid, 'RolledBack'))
                return
            if crash_at != 'before_commit_unconfirmed': self.disk = staged
            status = 'Unknown' if crash_at in ('before_commit_unconfirmed', 'after_commit') else 'Committed'
            self.callbacks.append(Completion(reservation.reservation_id, cid, status))

    def _commit_fact(self, attempt):
        if any(f['kind'] == 'CommitFact' for f in attempt.facts): return
        attempt.facts.append({'kind': 'CommitFact', 'fact_id': 'commit:' + attempt.commit_id,
                              'commit_id': attempt.commit_id, 'domain': 'domain-1',
                              'revision': str(attempt.prepared.revision),
                              'durability': 'DurableCommitted' if self.durable else 'Memory'})

    def _unknown(self, attempt):
        if any(f['kind'] == 'UnknownFact' for f in attempt.facts): return
        attempt.facts.append({'kind': 'UnknownFact', 'fact_id': 'unknown:' + attempt.commit_id,
                              'boundary': 'StorageCommit', 'reference_id': attempt.commit_id,
                              'reconcile': 'InspectCommitLedger'})

    def deliver(self, callback=None):
        selected = self.callbacks[0] if callback is None and self.callbacks else callback
        require(selected is not None, '没有完成回调')
        require(self.reservation is not None and selected.reservation_id == self.reservation.reservation_id
                and selected.commit_id == self.reservation.attempt, '迟到回调 reservation_id 不匹配')
        require(selected not in self.delivered and selected in self.callbacks, '重复或非 writer 回调')
        attempt = self.attempts[selected.commit_id]
        require(attempt.phase == 'CommitClaimed', '完成阶段不匹配')
        self.callbacks.remove(selected)
        self.delivered.add(selected)
        if selected.status == 'Committed':
            self._commit_fact(attempt)
            attempt.phase = 'DurableCommitted'
        elif selected.status == 'RolledBack':
            attempt.phase = 'KnownNotCommitted'
            self._release()
        else:
            self._unknown(attempt)
            attempt.phase = 'Indeterminate'
            self.isolated, self.business_open = True, False
        return selected

    def _release(self):
        if self.reservation and self.durable: self.capacity += 1
        self.reservation = None

    def validate_publication(self, state):
        require(isinstance(state, PublishedState), '需要拥有型 PublishedState')
        require(state.revision == len(state.history), 'root/revision/history 撕裂')
        if self.durable and state.revision:
            require(state == self.disk['state'], '发布材料与耐久镜像不匹配')
        require(state.lifecycle_generation > 0, '生命周期世代失效')

    def publish(self, attempt, fail=False):
        require(self.reservation is not None and self.reservation.attempt == attempt.commit_id, '发布缺少 reservation')
        require(attempt.phase == ('DurableCommitted' if self.durable else 'CommitClaimed'), '尚不可发布')
        require(attempt.prepared == self.reservation.prepared and attempt.binding == self.reservation.binding and attempt.intent_key == self.reservation.intent_key, '发布只能使用已 claim 的不可变材料')
        if fail:
            self.isolated, self.business_open = True, False
            return
        self.validate_publication(attempt.prepared)
        with self.locks.hold('domain'):
            old = self.published
            self.published = attempt.prepared
            self._commit_fact(attempt)
            attempt.facts.append({'kind': 'PublishedFact', 'fact_id': 'publish:' + attempt.commit_id,
                                  'commit_id': attempt.commit_id, 'published_version': str(attempt.prepared.revision)})
            attempt.phase = 'Published'
            self._freeze_published_outcome(attempt, attempt.commit_id, attempt.prepared)
            self._release()
        self.locks.destroy_old(); self.retired.append(old)
        self.locks.notify(); self.notifications.append(attempt.commit_id)

    def finalize(self, attempt):
        require(attempt.phase == 'Published' and attempt.completion_count == 0, '收尾必须唯一且已发布')
        attempt.completion_count += 1
        attempt.phase = 'Finalized'

    def snapshot(self): return self.published

    def _freeze_published_outcome(self, attempt, commit_id, state):
        self._published_outcomes[id(attempt)] = {
            'evidence': 'Durable' if self.durable else 'Volatile', 'known_facts': deepcopy(attempt.facts),
            'kind': 'StateCommitted', 'commit_id': commit_id, 'domain': 'domain-1',
            'revision': str(state.revision), 'published_version': str(state.revision),
            'result': {'value': state.value}}

    def outcome(self, attempt):
        if id(attempt) in self._published_outcomes:
            return deepcopy(self._published_outcomes[id(attempt)])
        common = {'evidence': 'Durable' if self.durable else 'Volatile', 'known_facts': deepcopy(attempt.facts)}
        require(attempt.phase not in ('Published', 'Finalized'), '缺少已冻结的发布事实')
        if attempt.phase == 'Indeterminate':
            return dict(common, kind='Indeterminate', evidence='PersistenceUncertain', unknown_ids=['unknown:' + attempt.commit_id])
        if attempt.phase == 'KnownNotCommitted':
            return dict(common, kind='FailedBeforeApply', failure_phase='Running', reason='CommitConfirmedRollback', proof='NoAppliedStateOrEffect')
        if attempt.phase == 'Cancelled':
            return dict(common, kind='CancelledBeforeApply', reason='Cancelled', proof='NoAppliedStateOrEffect')
        return None

    def dispatch_outbox(self):
        if self.isolated or not self.business_open: return []
        ids = [cid for cid in self.disk['outbox'] if cid in self.published.history and cid not in self.outbox_delivered]
        self.outbox_delivered.update(ids)
        return ids

    def check_disk(self):
        state = self.disk['state']
        require(isinstance(state, PublishedState) and state.revision == len(state.history), '损坏的 state 镜像')
        ids = set(state.history)
        require(len(ids) == state.revision, '重复历史提交身份')
        require(all(set(self.disk[name]) == ids for name in ('ledger', 'audit', 'outbox')), '状态与 ledger/审计/Outbox 撕裂')
        intent_keys = [self.disk['ledger'][cid]['intent_key'] for cid in ids]
        require(len(set(intent_keys)) == len(ids) and set(self.disk['receipts']) == set(intent_keys), 'intent 回执与提交 ledger 撕裂或复用')
        for revision, cid in enumerate(state.history, 1):
            ledger = self.disk['ledger'][cid]
            require(ledger['revision'] == revision and ledger['state'].revision == revision, '损坏的版本链')
            require(ledger['state'].history == state.history[:revision], '历史链不连贯')
            receipt = self.disk['receipts'][ledger['intent_key']]
            require(receipt['revision'] == revision and receipt['commit_id'] == cid
                    and receipt['intent_key'] == ledger['intent_key']
                    and receipt['result'] == {'value': ledger['state'].value}, 'intent 最终事实与状态不匹配')
            for name in ('audit', 'outbox'):
                require(self.disk[name][cid]['revision'] == revision and self.disk[name][cid]['commit_id'] == cid, '提交批次身份不一致')
            require(receipt['kind'] == 'StateCommitted', '耐久回执不是最终状态事实')
        if ids: require(self.disk['ledger'][state.history[-1]]['state'] == state, '根与最终 ledger 不匹配')

    def recover(self):
        require(self.durable, '易失域没有可安装的耐久恢复镜像')
        self.isolated, self.business_open = True, False
        self.check_disk()
        # 验证后先安装可信根；恢复不运行历史 Handler，也不重新消费 permit。
        self.published = self.disk['state']
        for cid, attempt in self.attempts.items():
            applied = cid in self.disk['ledger']
            if applied:
                attempt.commit_id = cid
                if id(attempt) in self._published_outcomes:
                    attempt.facts = deepcopy(self._published_outcomes[id(attempt)]['known_facts'])
                attempt.prepared = self.disk['ledger'][cid]['state']
                self._commit_fact(attempt)
                if not any(f['kind'] == 'PublishedFact' for f in attempt.facts):
                    attempt.facts.append({'kind': 'PublishedFact', 'fact_id': 'publish:' + cid,
                                          'commit_id': cid, 'published_version': str(attempt.prepared.revision)})
                attempt.phase = 'Published'
            elif attempt.phase in ('CommitClaimed', 'Indeterminate'):
                attempt.phase = 'KnownNotCommitted'
            unknown = [f for f in attempt.facts if f['kind'] == 'UnknownFact']
            if unknown and not any(f['kind'] == 'ResolutionRecord' for f in attempt.facts):
                attempt.facts.append({'kind': 'ResolutionRecord', 'fact_id': 'resolve:' + cid,
                                      'unknown_id': unknown[0]['fact_id'], 'determination': 'Applied' if applied else 'NotApplied',
                                      'evidence_ref': 'validated-ledger:' + cid})
            if applied: self._freeze_published_outcome(attempt, cid, attempt.prepared)
        self._release(); self.queue.clear(); self.callbacks.clear()
        self.isolated, self.business_open = False, True


class Device:
    """独立于本机 durable 镜像的外部世界，只有它能提供确定效果证据。"""
    def __init__(self):
        self.calls, self.receipts = [], {}

    def apply(self, effect_id, application, status):
        require(application in ('NotApplied', 'Applied', 'PartiallyApplied'), '非法效果报告')
        require(status in ('Succeeded', 'Failed'), '非法效果状态')
        self.calls.append(effect_id)
        self.receipts[effect_id] = {'application': application, 'status': status, 'external_evidence': ['device-ledger:' + effect_id]}
        return deepcopy(self.receipts[effect_id])


class Effect:
    def __init__(self, effect_id, binding, device):
        require(effect_id, 'EffectId 不可为空')
        self.effect_id, self.binding, self.device = effect_id, binding, device
        self.disk = {'claim': None, 'outcome': None}
        self.permitted, self.cancel_requested, self.recovered = False, False, False
        self.report, self.facts = None, []
        self._claim_material, self._sent_report = None, None

    def claim(self):
        require(self.disk['claim'] is None, '稳定 EffectId 只能 claim 一次')
        self._claim_material = (self.effect_id, self.binding, self.device)
        self.disk = {'claim': {'effect_id': self.effect_id, 'binding': self.binding}, 'outcome': None}
        self.recovered = False  # 无既存 durable claim 是同身份安全重试的实际证据。

    def cancel(self):
        self.cancel_requested = True
        if not self.permitted and not self.recovered and self.disk['outcome'] is None:
            self.disk['outcome'] = {'kind': 'CancelledBeforeApply', 'evidence': 'Durable' if self.disk['claim'] else 'Volatile',
                                    'known_facts': [], 'reason': 'Cancelled', 'proof': 'NoAppliedStateOrEffect'}
        return 'AlreadyClaimed' if self.permitted else 'Requested'

    def _check_claim_material(self):
        require(self._claim_material is not None and self.disk['claim'] is not None, '缺少冻结的效果 claim')
        effect_id, binding, device = self._claim_material
        require(self.disk['claim'] == {'effect_id': effect_id, 'binding': binding}
                and self.effect_id == effect_id and self.binding == binding and self.device is device,
                '效果 claim 后身份、绑定或发送所有者不可改变')

    def _effect_identity(self):
        require(self._claim_material is not None, '缺少效果事实身份')
        return self._claim_material[0]

    def authorize(self, policy, permit, now):
        self._check_claim_material()
        require(self.disk['claim'] is not None and not self.recovered, '发送许可前必须 durable claim')
        require(not self.cancel_requested and not self.permitted and self.disk['outcome'] is None, '取消或重复许可')
        policy.consume(permit, self.binding, now)
        self.permitted = True

    def send(self, application, status):
        self._check_claim_material()
        require(self.permitted and not self.recovered and self.report is None, '不得无许可或在未知恢复后盲重发')
        # 外部操作发生在本机 SQL 事务之外；结果先在 Device 保存，再返回本机。
        self.report = self._claim_material[2].apply(self._effect_identity(), application, status)
        self._sent_report = deepcopy(self.report)

    def _resolved(self, report, result_valid=True):
        self.facts.append({'kind': 'EffectFact', 'fact_id': 'effect:' + self._effect_identity(),
                           'effect_id': self._effect_identity(), 'application': report['application']})
        result = {'kind': 'EffectResolved', 'evidence': 'Durable', 'effect_id': self._effect_identity(),
                  **report, 'known_facts': deepcopy(self.facts)}
        if result_valid: result['result'] = {'receipt': self._effect_identity()}
        else: result['result_error'] = 'ResultEncodingFailed'
        return result

    def record(self, result_valid=True):
        require(self._sent_report is not None and self.disk['outcome'] is None and not self.recovered, '没有可保存的实际效果报告')
        self.disk['outcome'] = self._resolved(self._sent_report, result_valid)

    def restart(self):
        self.permitted, self.report, self.recovered = False, None, True
        self._sent_report = None
        if self.disk['claim'] is not None and self.disk['outcome'] is None:
            self.facts = [{'kind': 'UnknownFact', 'fact_id': 'unknown:' + self._effect_identity(),
                           'boundary': 'ExternalEffect', 'reference_id': self._effect_identity(), 'reconcile': 'InspectDeviceLedger'}]
        elif self.disk['outcome'] is not None:
            self.facts = deepcopy(self.disk['outcome']['known_facts'])

    def reconcile(self):
        require(self.recovered and self.disk['outcome'] is None and self.facts, '只对未解决未知追加对账')
        report = self._claim_material[2].receipts.get(self._effect_identity())
        require(report is not None, '未找到 outcome 不能证明未发送；需要外部证据')
        result = self._resolved(report)
        self.facts.append({'kind': 'ResolutionRecord', 'fact_id': 'resolve:' + self._effect_identity(),
                           'unknown_id': 'unknown:' + self._effect_identity(), 'determination': report['application'],
                           'evidence_ref': report['external_evidence'][0]})
        result['known_facts'] = deepcopy(self.facts)
        self.disk['outcome'] = result

    def outcome(self):
        if self.disk['outcome'] is not None: return deepcopy(self.disk['outcome'])
        if self.recovered and self.disk['claim'] is not None:
            return {'kind': 'Indeterminate', 'evidence': 'Durable', 'known_facts': deepcopy(self.facts),
                    'unknown_ids': ['unknown:' + self._effect_identity()]}
        return None

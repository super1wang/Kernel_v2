"""D0.05 去重/恢复参考模型。

持久镜像在 writer 事务末尾整体替换；accepted_keys 是测试观察器的历史，
只用于识别错误 GC 的复活窗口，不是要求产品无限保存的业务表。
Request.fingerprint 是已归一化符号值的等价关系，不冒充 ock.canonical/1。
"""
from contextlib import contextmanager
from copy import deepcopy
from dataclasses import dataclass, fields, is_dataclass
import hashlib
import json
import math


class ContractError(ValueError):
    pass


def require(condition, reason):
    if not condition: raise ContractError(reason)


@dataclass(frozen=True)
class Scope:
    store_id: str
    restore_generation: str
    principal: str
    namespace: str


@dataclass(frozen=True)
class IntentKey:
    scope: Scope
    epoch: int
    nonce: str


@dataclass(frozen=True)
class Session:
    store_id: str
    restore_generation: str
    principal: str
    incarnation: str
    token: str


def symbol_value(value, depth=0):
    """拥有型、带类型的模型值；不编码字节，不冒充 canonical CBOR。"""
    require(depth <= 64, '模型符号值深度超限或含循环')
    kind = type(value)
    if value is None: return ('null',)
    if kind is bool: return ('boolean', value)
    if kind is int: return ('integer', value)
    if kind is float:
        require(math.isfinite(value), '模型符号值拒绝非有限数')
        return ('float', value.hex())
    if kind is str: return ('text', value)
    if kind is bytes: return ('bytes', value)
    if kind in (tuple, list):
        return ('tuple' if kind is tuple else 'array', tuple(symbol_value(item, depth + 1) for item in value))
    if kind is dict:
        require(all(type(key) is str for key in value), '模型map仅接受文本键')
        return ('map', tuple((key, symbol_value(value[key], depth + 1)) for key in sorted(value)))
    raise ContractError('未声明的模型符号值类型')


@dataclass(frozen=True)
class Request:
    operation: str
    contract: str
    args: tuple
    target: str
    preconditions: tuple
    plan_digest: str
    request_id: str = 'request-1'
    trace_id: str = 'trace-1'
    retry_attempt: int = 0

    def fingerprint(self):
        require(all((self.operation, self.contract, self.target)), '精确 Operation/契约/target 必须存在')
        return tuple(symbol_value(value) for value in (self.operation, self.contract, self.args, self.target, self.preconditions, self.plan_digest))


@dataclass(frozen=True)
class StepKey:
    parent_execution: str
    static_node_path: str
    branch_iteration_path: tuple
    logical_occurrence: int


@dataclass(frozen=True)
class ChildAdmission:
    token: str
    key: StepKey
    restore_generation: str
    plan_digest: str


def model_bytes(value):
    """仅备份模型完整性，不是 SDK canonical encoder 或安全签名。"""
    if is_dataclass(value): return ['record', type(value).__name__, [[f.name, model_bytes(getattr(value, f.name))] for f in fields(value)]]
    if isinstance(value, dict):
        return ['map', sorted([[model_bytes(k), model_bytes(v)] for k, v in value.items()], key=lambda pair: json.dumps(pair[0], sort_keys=True))]
    if isinstance(value, (set, frozenset)): return ['set', sorted([model_bytes(v) for v in value], key=lambda item: json.dumps(item, sort_keys=True))]
    if isinstance(value, tuple): return ['tuple', [model_bytes(v) for v in value]]
    if isinstance(value, list): return ['list', [model_bytes(v) for v in value]]
    if isinstance(value, bytes): return ['bytes', value.hex()]
    return value


def digest(value):
    return hashlib.sha256(json.dumps(model_bytes(value), sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode('utf-8')).hexdigest()


class IntentStore:
    ACCEPT_STEPS = ('before_claim', 'before_execution', 'before_input', 'before_receipt', 'before_commit')
    _store_counter = 0

    def __init__(self, volatile=False, capacity=100):
        type(self)._store_counter += 1
        self.store_id = 'store-' + str(type(self)._store_counter)
        self.restore_generation, self.incarnation = 'restore-1', 'host-1'
        self._restore_serial, self._host_serial, self._next_session = 1, 1, 1
        self.volatile, self.capacity = volatile, capacity
        self.image = self._empty_image()
        self.sessions, self.revoked, self.admissions = {}, set(), {}
        self.accepted_keys = set()
        self._writing = False
        self.scheduled, self.effect_dispatches = [], []
        self.effects_sealed, self.handler_runs = False, 0
        self.assets, self.live_assets, self.backups = {}, set(), []
        self.reconciliation_records = []

    @staticmethod
    def _empty_image():
        return {'scopes': {}, 'records': {}, 'executions': {}, 'inputs': {}, 'receipts': {}, 'parents': {}, 'steps': {}}

    @contextmanager
    def writer(self):
        require(not self._writing, 'claim/epoch/GC 必须使用同一有序 DB writer，不允许交错事务')
        self._writing = True
        try:
            yield
        finally:
            self._writing = False

    def connect(self, principal):
        require(bool(principal), 'Principal 必须由认证入口给出')
        token = 'session-' + str(self._next_session); self._next_session += 1
        session = Session(self.store_id, self.restore_generation, principal, self.incarnation, token)
        self.sessions[token] = session
        return session

    def _authorize(self, session):
        require(isinstance(session, Session) and self.sessions.get(session.token) == session, '伪造或已撤销 session')
        require((session.store_id, session.restore_generation, session.incarnation) ==
                (self.store_id, self.restore_generation, self.incarnation), '存储/恢复/宿主范围已失效')
        require(session.principal not in self.revoked, '当前权限不允许查询或提交')

    def _scope(self, session, namespace):
        self._authorize(session)
        require(namespace in ('default', 'batch'), 'namespace 未经服务器验证')
        generation = self.incarnation if self.volatile else self.restore_generation
        return Scope(self.store_id, generation, session.principal, namespace)

    def revoke(self, principal): self.revoked.add(principal)
    def grant(self, principal): self.revoked.discard(principal)

    @staticmethod
    def _window(image, scope):
        return image['scopes'].get(scope, {'floor': 1, 'current': 1})

    def claim(self, session, namespace, epoch, nonce, request, crash_at=None):
        require(crash_at is None or crash_at in self.ACCEPT_STEPS + ('after_commit',), '未知 accept 故障点')
        with self.writer():
            scope = self._scope(session, namespace)
            require(type(epoch) is int and epoch > 0 and isinstance(nonce, str) and bool(nonce), '非法外部 IntentKey')
            key = IntentKey(scope, epoch, nonce)
            fingerprint = request.fingerprint()
            existing = self.image['records'].get(key)
            if existing is not None:
                require(existing['fingerprint'] == fingerprint, 'IntentConflict：同 key 不同业务指纹')
                return deepcopy(existing)
            window = self._window(self.image, scope)
            require(epoch >= window['floor'], 'IntentExpired')
            require(epoch <= window['current'], 'EpochInvalid：未来 epoch')
            require(len(self.image['records']) < self.capacity, '容量不足，不能淘汰窗口内回执')
            staged = deepcopy(self.image)
            execution_ref = 'execution-' + str(len(staged['executions']) + 1)
            # 身份由 key 派生以避免 GC 后 len 变小造成复用。
            execution_ref += '-' + digest(key)[:16]
            record = {'key': key, 'fingerprint': fingerprint, 'execution_ref': execution_ref,
                      'status': 'Claimed', 'outcome': None, 'pins': set(), 'references': set(), 'effect_permitted': False}
            operations = [
                ('before_claim', lambda: staged['records'].__setitem__(key, record)),
                ('before_execution', lambda: staged['executions'].__setitem__(execution_ref, {'key': key, 'status': 'Accepted'})),
                ('before_input', lambda: staged['inputs'].__setitem__(execution_ref, deepcopy(request))),
                ('before_receipt', lambda: staged['receipts'].__setitem__(key, {'execution_ref': execution_ref, 'acceptance': 'Volatile' if self.volatile else 'DurableAccepted'})),
            ]
            for boundary, operation in operations:
                require(crash_at != boundary, '注入接受事务回滚：' + boundary)
                operation()
            require(crash_at != 'before_commit', '注入接受事务 commit 前回滚')
            self.image = staged
            self.accepted_keys.add(key)
            require(crash_at != 'after_commit', '接受已提交但响应/调度尚未发布时崩溃')
            self.scheduled.append(execution_ref)
            return deepcopy(record)

    def query(self, session, key):
        self._authorize(session)
        require(isinstance(key, IntentKey) and key.scope == self._scope(session, key.scope.namespace), '不可跨 Principal/store/restore 查询')
        return deepcopy(self.image['records'].get(key))

    def set_status(self, key, status):
        require(status in ('Claimed', 'Suspended', 'Unknown'), '非法非终结状态')
        with self.writer():
            record = self.image['records'][key]
            require(record['outcome'] is None, '已确定结果不能改为在途/未知')
            require(record['status'] not in ('Unknown', 'EffectPermitted') or status == 'Unknown',
                    '未知或已许可效果不能经普通状态接口重新准入')
            require(not record['effect_permitted'] or status == 'Unknown', '发送准入事实不能被状态投影抹掉')
            record['status'] = status

    def resolve(self, key, outcome):
        require(outcome in ('StateCommitted', 'EffectResolved', 'FailedBeforeApply', 'CancelledBeforeApply'), '非法确定事实')
        with self.writer():
            record = self.image['records'][key]
            require(record['outcome'] is None, '同一意图事实不可覆盖')
            record['outcome'], record['status'] = outcome, 'Terminal'

    def advance_epoch(self, session, namespace, current):
        with self.writer():
            scope = self._scope(session, namespace)
            window = self._window(self.image, scope)
            require(type(current) is int and current > window['current'], 'epoch 必须单调推进')
            staged = deepcopy(self.image)
            staged['scopes'][scope] = {'floor': window['floor'], 'current': current}
            self.image = staged

    def pin(self, key, owner):
        require(owner, 'pin 需要 owner')
        with self.writer(): self.image['records'][key]['pins'].add(owner)

    def unpin(self, key, owner):
        with self.writer(): self.image['records'][key]['pins'].remove(owner)

    def reference(self, key, owner):
        require(owner, '引用需要 owner')
        with self.writer(): self.image['records'][key]['references'].add(owner)

    def unreference(self, key, owner):
        with self.writer(): self.image['records'][key]['references'].remove(owner)

    def gc(self, session, namespace, floor, crash_at=None):
        require(crash_at in (None, 'before_floor', 'after_floor', 'after_delete'), '未知 GC 故障点')
        with self.writer():
            scope = self._scope(session, namespace)
            window = self._window(self.image, scope)
            require(type(floor) is int and window['floor'] <= floor <= window['current'], 'floor 不得回退或超过 current')
            staged = deepcopy(self.image)
            eligible = [key for key, record in staged['records'].items() if key.scope == scope and key.epoch < floor
                        and record['status'] == 'Terminal' and not record['pins'] and not record['references']]
            require(crash_at != 'before_floor', 'GC 在 floor 前崩溃')
            staged['scopes'][scope] = {'floor': floor, 'current': window['current']}
            # 先耐久 floor；独立有序清理事务允许多保留材料，永不低水位缺回执。
            self.image = staged
            require(crash_at != 'after_floor', 'GC 在持久 floor 后崩溃，仅多保留旧材料')
            cleaned = deepcopy(self.image)
            for key in eligible:
                record = cleaned['records'].pop(key)
                execution = record['execution_ref']
                cleaned['executions'].pop(execution)
                cleaned['inputs'].pop(execution)
                cleaned['receipts'].pop(key)
            self.image = cleaned
            require(crash_at != 'after_delete', 'GC 在删除事务提交后崩溃')

    def check_invariants(self, image=None, accepted_keys=None):
        image = self.image if image is None else image
        accepted_keys = self.accepted_keys if accepted_keys is None else accepted_keys
        for key in accepted_keys:
            if key not in image['records']:
                require(self._window(image, key.scope)['floor'] > key.epoch, '旧 floor 与缺失回执导致意图复活窗口')
        for key, record in image['records'].items():
            execution = record['execution_ref']
            require(record['key'] == key, '意图键不匹配')
            require(type(record['effect_permitted']) is bool, '缺少明确发送准入事实')
            require(not record['effect_permitted'] or record['status'] in ('EffectPermitted', 'Unknown', 'Terminal'),
                    '状态投影与发送准入事实冲突')
            require(execution in image['executions'] and execution in image['inputs'] and key in image['receipts'], '接受记录被撕裂')
            require(image['executions'][execution]['key'] == key and image['receipts'][key]['execution_ref'] == execution, '接受材料身份不一致')
            require(isinstance(image['inputs'][execution], Request) and record['fingerprint'] == image['inputs'][execution].fingerprint(),
                    '接受输入与不可变业务指纹不一致')
        for window in image['scopes'].values(): require(1 <= window['floor'] <= window['current'], '非法窗口')
        for key, record in image['steps'].items():
            require(key.parent_execution in image['parents'], 'child 缺少持久 parent')
            require(key.parent_execution in record['pins'], 'parent 恢复需要的 child receipt 缺 pin')

    def register_parent(self, session, parent_id, plan_digest, nodes):
        with self.writer():
            self._authorize(session)
            require(not self.volatile and parent_id and parent_id not in self.image['parents'] and plan_digest and nodes, 'ChildAdmission 需要持久且唯一的固定 parent')
            self.image['parents'][parent_id] = {'principal': session.principal, 'plan_digest': plan_digest,
                                              'nodes': deepcopy(nodes), 'active': True, 'restore_generation': self.restore_generation}
            return parent_id

    def admit_child(self, parent_id, node, path, occurrence):
        parent = self.image['parents'].get(parent_id)
        require(parent is not None and parent['active'] and parent['restore_generation'] == self.restore_generation, 'parent 不存在或寿命/世代失效')
        require(node in parent['nodes'] and isinstance(path, tuple) and type(occurrence) is int and occurrence >= 0, '步骤不属于固定计划或路径无效')
        key = StepKey(parent_id, node, path, occurrence)
        token = 'admission-' + str(len(self.admissions) + 1)
        admission = ChildAdmission(token, key, self.restore_generation, parent['plan_digest'])
        self.admissions[token] = admission
        return admission

    def claim_child(self, session, admission, request):
        with self.writer():
            self._authorize(session)
            require(isinstance(admission, ChildAdmission) and self.admissions.get(admission.token) == admission
                    and admission.restore_generation == self.restore_generation, '内部准入失效或伪造')
            key = admission.key; parent = self.image['parents'][key.parent_execution]
            require(parent['active'] and parent['principal'] == session.principal and parent['plan_digest'] == admission.plan_digest, 'parent 寿命/主体/固定计划不匹配')
            require(parent['nodes'][key.static_node_path] == (request.operation, request.contract, request.target), '子步骤精确版本/目标不匹配')
            fingerprint = request.fingerprint()
            existing = self.image['steps'].get(key)
            if existing is not None:
                require(existing['fingerprint'] == fingerprint, '同一步绑定参数改变')
                return deepcopy(existing)
            record = {'key': key, 'fingerprint': fingerprint, 'execution_ref': 'child-' + digest(key)[:16],
                      'pins': {key.parent_execution}, 'outcome': None}
            staged = deepcopy(self.image); staged['steps'][key] = record; self.image = staged
            return deepcopy(record)

    def finish_parent(self, parent_id):
        with self.writer(): self.image['parents'][parent_id]['active'] = False

    def restart(self):
        self._host_serial += 1; self.incarnation = 'host-' + str(self._host_serial)
        self.sessions.clear(); self.admissions.clear(); self.scheduled.clear()
        if self.volatile:
            self.image = self._empty_image(); self.accepted_keys.clear()
        else:
            self.check_invariants()
            for record in self.image['records'].values():
                if record['effect_permitted'] and record['outcome'] is None: record['status'] = 'Unknown'
                elif record['status'] == 'Claimed': record['status'] = 'Suspended'

    def add_asset(self, identity, data):
        require(identity not in self.assets and isinstance(data, bytes), '资产不可覆盖')
        self.assets[identity] = data

    def reference_asset(self, identity):
        require(identity in self.assets, '正式引用不得指向不存在资产')
        self.live_assets.add(identity)

    def gc_assets(self):
        pinned = set(self.live_assets)
        for backup in self.backups: pinned.update(backup['asset_manifest'])
        self.assets = {key: data for key, data in self.assets.items() if key in pinned}

    def begin_backup(self):
        require(not self.volatile and not self._writing, '备份需要受控一致窗口')
        self.check_invariants()
        image = deepcopy(self.image)
        assets = {key: self.assets[key] for key in self.live_assets}
        backup = {'store_id': self.store_id, 'restore_generation': self.restore_generation,
                  'image': image, 'image_sha256': digest(image), 'assets': assets,
                  'asset_manifest': {key: hashlib.sha256(data).hexdigest() for key, data in assets.items()},
                  'complete': False}
        self.backups.append(backup)
        return backup

    def finish_backup(self, backup):
        require(any(item is backup for item in self.backups) and not backup['complete'], '未知或重复备份完成')
        self._validate_backup(backup, require_complete=False)
        backup['complete'] = True

    def backup(self):
        value = self.begin_backup(); self.finish_backup(value); return value

    def _validate_backup(self, backup, require_complete=True):
        require(backup['store_id'] == self.store_id, '备份属于其他 StoreId')
        require(not require_complete or backup['complete'], '备份未完成')
        require(digest(backup['image']) == backup['image_sha256'], '备份镜像摘要不匹配')
        try:
            self.check_invariants(backup['image'], set(backup['image']['records']))
        except (KeyError, TypeError, AttributeError) as error:
            raise ContractError('备份材料结构不合法') from error
        require(set(backup['assets']) == set(backup['asset_manifest']), '备份缺少必要资产')
        for key, expected in backup['asset_manifest'].items():
            require(hashlib.sha256(backup['assets'][key]).hexdigest() == expected, '备份资产摘要不匹配')

    def restore_backup(self, backup):
        require(not self._writing, '恢复必须走离线显式入口')
        self._validate_backup(backup)
        self._restore_serial += 1
        self.restore_generation = 'restore-' + str(self._restore_serial)
        self._host_serial += 1; self.incarnation = 'host-' + str(self._host_serial)
        self.image = deepcopy(backup['image'])
        self.assets = deepcopy(backup['assets']); self.live_assets = set(self.assets)
        self.sessions.clear(); self.admissions.clear(); self.scheduled.clear()
        self.accepted_keys = set(self.image['records'])
        self.effects_sealed = True
        self.check_invariants()

    def lookup_historical(self, key):
        require(isinstance(key, IntentKey) and key.scope.store_id == self.store_id, '不是本存储历史身份')
        record = self.image['records'].get(key)
        if record is None:
            return {'status': 'UnknownAfterBackup', 'key': key, 'reason': '备份后缺失记录不能证明动作未发生'}
        return deepcopy(record)

    def authorize_effect_send(self, session, key):
        """恢复门只发放当前新意图的发送准入；ActionPermit 仲裁由 commit_model 单独验证。"""
        with self.writer():
            self._authorize(session)
            require(not self.effects_sealed, '旧备份恢复后封住自动效果重放')
            require(isinstance(key, IntentKey) and key.scope == self._scope(session, key.scope.namespace),
                    '旧世代/其他主体历史不能换范围重播')
            record = self.image['records'].get(key)
            require(record is not None and record['status'] == 'Claimed' and record['outcome'] is None and not record['effect_permitted'],
                    '只准入尚未发送的新意图；未知、已许可和既有结果先查询/对账')
            record['effect_permitted'] = True
            record['status'] = 'EffectPermitted'
            self.effect_dispatches.append(key)

    def finish_restore_reconciliation(self, evidence):
        require(self.effects_sealed and isinstance(evidence, str) and bool(evidence), '需显式外部历史对账材料')
        self.reconciliation_records.append({'generation': self.restore_generation, 'evidence': evidence})
        self.effects_sealed = False


class ClientIntent:
    """首次发送前保存的逻辑身份；自动重试只返回原 epoch/nonce。"""
    def __init__(self, store, session, epoch, nonce):
        store._authorize(session)
        self.scope = (store.store_id, store.incarnation if store.volatile else store.restore_generation, session.principal)
        self.epoch, self.nonce = epoch, nonce

    def retry(self, store, session):
        store._authorize(session)
        current = (store.store_id, store.incarnation if store.volatile else store.restore_generation, session.principal)
        require(current == self.scope, '不能自动变更存储/恢复/Volatile incarnation/Principal 后重试未知意图')
        return self.epoch, self.nonce

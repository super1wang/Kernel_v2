"""独立共同合同不由后端决定删项；NotApplicable 与 Passed 分开。"""
import hashlib
import inspect
from pathlib import Path
from .fixtures import MockExecutor, DuplicateCallbackExecutor, ContractError
from .identity import descriptor_errors

CONTRACT_VERSION = 'ock.executor.bootstrap/1'
COMMON_CASES = (
    'ownership_transfer', 'exactly_once_completion', 'rejected_work_cleanup',
    'exception_boundary', 'drain_shutdown_lifetime', 'worker_wait_rejection')


def descriptor_for(factory, name, kind='mock'):
    return {'name': name, 'factory': factory.__module__ + '.' + factory.__qualname__,
            'kind': kind, 'port_contract_version': CONTRACT_VERSION,
            'implementation_sha256': hashlib.sha256(Path(inspect.getsourcefile(factory)).read_bytes()).hexdigest(),
            'capabilities': {'inline': True, 'parallel': False}}


def descriptor(name):
    return descriptor_for(MockExecutor, name) if name == 'mock-inline' else descriptor_for(DuplicateCallbackExecutor, name, 'fault')


def require(condition, message):
    if not condition:
        raise ContractError(message)


def common_case(factory, case):
    backend = factory()
    calls = []
    if case == 'ownership_transfer':
        accepted = backend.submit(lambda: calls.append('work') or 41, lambda r: calls.append(r))
        require(accepted == 'Accepted' and calls == ['work', {'status': 'Completed', 'value': 41}], 'accepted owner/completion mismatch')
        require(backend.drain(), 'accepted work not drained')
    elif case == 'exactly_once_completion':
        for value in range(3):
            backend.submit(lambda value=value: value, lambda result: calls.append(result))
        require(calls == [{'status': 'Completed', 'value': v} for v in range(3)], 'completion count/order/value mismatch')
    elif case == 'rejected_work_cleanup':
        backend.shutdown()
        require(backend.submit(lambda: calls.append('work'), lambda _: calls.append('done')) == 'Rejected', 'closed executor accepted work')
        require(backend.drain() and not calls, 'rejected work/callback still owned')
    elif case == 'exception_boundary':
        def fail():
            raise RuntimeError('work failed')
        require(backend.submit(fail, lambda result: calls.append(result)) == 'Accepted', 'exception erased acceptance')
        require(len(calls) == 1 and calls[0]['status'] == 'Failed' and backend.drain(), 'work exception boundary failed')
        backend.submit(lambda: 0, lambda _: fail())
        require(backend.drain() and backend.callback_errors == 1, 'callback exception boundary failed')
    elif case == 'drain_shutdown_lifetime':
        backend.submit(lambda: 7, lambda result: calls.append(result))
        require(backend.drain() and backend.shutdown() and len(calls) == 1, 'shutdown before accepted completion')
        require(backend.submit(lambda: 8, calls.append) == 'Rejected' and len(calls) == 1, 'shutdown admitted new work')
    elif case == 'worker_wait_rejection':
        def work():
            backend.submit(lambda: 1, lambda result: None)
            for action in (backend.drain, backend.shutdown):
                try:
                    action()
                except ContractError:
                    calls.append('rejected')
                else:
                    calls.append('wrongly accepted')
        backend.submit(work, lambda _: None)
        require(calls == ['rejected', 'rejected'] and backend.drain(), 'self-wait/self-shutdown not rejected')
    else:
        raise ValueError('unknown fixed common case')


def run_contract(factory, manifest, required_capabilities=()):
    result = {'port_contract_version': CONTRACT_VERSION, 'backend': manifest.get('name'),
              'cases': [], 'errors': [], 'qualified': False, 'scope': 'D0.06-c harness mock only; not production Executor'}
    fields = {'name', 'factory', 'kind', 'port_contract_version', 'implementation_sha256', 'capabilities'}
    if set(manifest) != fields:
        result['errors'].append('descriptor cannot override or waive common cases')
        return result
    actual = descriptor_for(factory, manifest['name'], manifest['kind'])
    caps = manifest['capabilities']
    result['errors'].extend(descriptor_errors(manifest, actual, {'inline', 'parallel'}, ('mock', 'fault'), required_capabilities))
    if result['errors']:
        return result
    for case in COMMON_CASES:
        try:
            common_case(factory, case)
            row = {'id': case, 'status': 'Passed'}
        except Exception as error:
            row = {'id': case, 'status': 'Failed', 'reason': str(error)}
        result['cases'].append(row)
    if caps['parallel']:
        # G0 不提供真实并行实现；声明了该能力就必须失败，不能以 skip 伪装通过。
        result['cases'].append({'id': 'real_parallel', 'status': 'Failed', 'reason': 'bootstrap factory has no verified real-thread fixture'})
    else:
        result['cases'].append({'id': 'real_parallel', 'status': 'NotApplicable', 'reason': 'contract expression capabilities.parallel == false'})
    if not caps['inline']:
        result['errors'].append('this bootstrap factory contract requires inline semantics')
    result['qualified'] = manifest['kind'] == 'mock' and not result['errors'] and all(c['status'] != 'Failed' for c in result['cases'])
    return result

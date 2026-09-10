"""真实 Control 连接验证排队取消/过期保留 Accepted 结果语义。"""
import subprocess
import sys
import time
from managed_wire_support import Wire, start, ref, servers, connections

try:
    endpoint = start()
    writer, reader = Wire(endpoint), Wire(endpoint)
    card = writer.call('capabilities.describe', {'name': 'sample.compute', 'version': '1.0.0'})['result']

    def submit(delay=0, timeout=10000):
        submitter = Wire(endpoint)
        response = submitter.call('operation.submit', {
            'operation': {'name': 'sample.compute', 'version': '1.0.0'},
            'contract_digest': card['contract_digest'],
            'args': {'amount': 1, 'delay_ms': delay, 'text': 'post-closure'},
            'execution_timeout_ms': timeout})['result']
        assert response['kind'] == 'Accepted', response
        return response['execution_ref']['execution_id']

    blockers = [submit(10000) for _ in range(2)]
    deadline = time.monotonic() + 3
    while not all(reader.call('execution.get', ref(value))['result']['phase'] == 'Running'
                  for value in blockers):
        assert time.monotonic() < deadline, 'workers did not start'
        time.sleep(0.005)

    cancelled = submit()
    initial = reader.call('execution.get', ref(cancelled))['result']
    assert initial['phase'] in ('Queued', 'WaitingResources'), initial
    response = writer.call('execution.cancel', ref(cancelled))['result']
    assert response['disposition'] == 'Requested', response
    expired = submit(timeout=100)
    for value in (cancelled, expired):
        waited = reader.call('execution.wait', {**ref(value), 'wait_timeout_ms': 3000})['result']
        assert waited['wait_state'] == 'Terminal', waited
        current = reader.call('execution.get', ref(value))['result']
        assert current['phase'] == 'Terminal' and current['execution_ref']['execution_id'] == value
        assert int(current['observation_version']) > 1
        result = reader.call('result.read', ref(value))['result']
        assert result['execution_ref']['execution_id'] == value and result['projection'] == 'full'
        reply = result['reply']
        assert reply['kind'] == 'Completed', reply
        outcome = reply['outcome']
        assert outcome['kind'] == 'CancelledBeforeApply', outcome
        before = outcome['conditions']['before_apply']
        assert before['execution_accepted'] is True and before['business_entered'] is False
    page = reader.call('execution.list', {'owner': 'self', 'phase_set': 'terminal', 'page_size': 32})['result']
    assert {cancelled, expired} <= {item['execution_ref']['execution_id'] for item in page['items']}
    for value in blockers:
        writer.call('execution.cancel', ref(value))
    print('real cross-connection queued cancellation and expiry preserve Completed outcomes', flush=True)
finally:
    for connection in connections:
        connection.close()
    for server in servers:
        if server.poll() is None:
            try:
                _, error = server.communicate(b'stop\n', timeout=10)
                if error:
                    print(error.decode(errors='replace'), file=sys.stderr)
            except subprocess.TimeoutExpired:
                server.kill()
                server.communicate()
        assert server.returncode == 0, server.returncode

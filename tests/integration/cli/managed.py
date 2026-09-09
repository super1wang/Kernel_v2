import json
import subprocess
import sys
import uuid
import queue
import threading

service, cli = sys.argv[1:3]
instance = 'managed-' + uuid.uuid4().hex
server = subprocess.Popen([service, '--instance', instance], stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf-8')
children = []

def spawn(arguments):
    process = subprocess.Popen([cli, '--instance', instance, '--json', *arguments],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding='utf-8')
    children.append(process)
    print('CLI', process.pid, arguments[0:2], flush=True)
    return process

def finish(process, expected=0):
    output, error = process.communicate(timeout=15)
    assert process.returncode == expected, (process.pid, process.returncode, output, error)
    return json.loads(output)

def call(arguments, expected=0):
    return finish(spawn(arguments), expected)

def first_line(process):
    lines = queue.Queue(maxsize=1)
    threading.Thread(target=lambda: lines.put(process.stdout.readline()), daemon=True).start()
    line = lines.get(timeout=3)
    print('CLI first snapshot', process.pid, line.rstrip(), flush=True)
    return json.loads(line)

def submit(amount, delay, text):
    response = call(['submit', 'sample.compute', '--args', json.dumps(
        {'amount': amount, 'delay_ms': delay, 'text': text}, ensure_ascii=False)])['result']
    assert response['kind'] == 'Accepted' and response['acceptance_guarantee'] == 'Volatile'
    return response['execution_ref']['execution_id']

def result(ref):
    response = call(['result', 'read', ref])['result']
    assert response['projection'] == 'full' and response['execution_ref']['execution_id'] == ref
    assert response['reply']['kind'] == 'Completed'
    return response['reply']['outcome']['result']

try:
    assert json.loads(server.stdout.readline()) == {'ready': True}
    text = '跨连接拥有输入 ' * 100
    ref = submit(4, 3000, text)
    current = call(['execution', 'get', ref])['result']
    assert current['phase'] in ('Queued', 'WaitingResources', 'Running')
    assert current['full_result_available'] is False
    timed = call(['execution', 'wait', ref, '--wait-timeout-ms', '0'], 5)['result']
    assert timed['wait_state'] == 'Timeout'
    completed = call(['execution', 'wait', ref, '--wait-timeout-ms', '5000', '--timeout-ms', '7000'])['result']
    assert completed['wait_state'] == 'Terminal' and completed['phase'] == 'Terminal'
    assert result(ref) == {'amount': 5, 'text': text, 'stop_observed': False}
    assert call(['execution', 'get', ref])['result']['full_result_available'] is True
    ref = submit(8, 10000, 'two active CLI processes')
    for _ in range(10):
        if call(['execution', 'get', ref])['result']['phase'] == 'Running':
            break
    else:
        raise AssertionError('managed execution did not start')
    waiter = spawn(['execution', 'wait', ref, '--wait-timeout-ms', '10000', '--timeout-ms', '12000'])
    assert waiter.poll() is None
    cancel = call(['execution', 'cancel', ref])['result']
    assert cancel['disposition'] == 'AlreadyClaimed'
    assert finish(waiter)['result']['wait_state'] == 'Terminal'
    assert result(ref) == {'amount': 9, 'text': 'two active CLI processes', 'stop_observed': True}
    assert call(['execution', 'cancel', ref])['result']['disposition'] == 'AlreadyTerminal'
    # 超过服务的 16 个会话额度反复跨进程连接；缓存结果不占住原会话。
    retained = set()
    for n in range(20):
        ref = submit(n, 0, 'retained result')
        retained.add(ref)
        assert call(['execution', 'wait', ref, '--wait-timeout-ms', '1000'])['result']['wait_state'] == 'Terminal'
        assert result(ref)['amount'] == n + 1
    listed = call(['execution', 'list', '--phase', 'terminal', '--page-size', '200'])['result']
    assert listed['consistency'] == 'live_keyset'
    assert listed['retention_scope'] == 'managed_active_and_retained_terminal'
    assert retained <= {item['execution_ref']['execution_id'] for item in listed['items']}
    assert len(listed['items']) == 22 and all(item['phase'] == 'Terminal' for item in listed['items'])
    assert 'next_cursor' not in listed
    page = call(['execution', 'list', '--phase', 'terminal', '--page-size', '1'])['result']
    assert len(page['items']) == 1 and page['next_cursor'].startswith('v1.')
    assert call(['execution', 'list', '--phase', 'nonterminal'])['result']['items'] == []
    pager = spawn(['execution', 'list', '--phase', 'terminal', '--page-size', '5', '--pages', '10', '--jsonl'])
    output, error = pager.communicate(timeout=15)
    assert pager.returncode == 0, (pager.pid, pager.returncode, output, error)
    pages = [json.loads(line)['result'] for line in output.splitlines()]
    assert len(pages) == 5 and 'next_cursor' not in pages[-1]
    identities = [item['execution_ref']['execution_id'] for page in pages for item in page['items']]
    assert len(identities) == 22 and len(set(identities)) == 22 and retained <= set(identities)
    call(['execution', 'list', '--pages', '2'], 2)
    # 真实订阅在 10 s 无通知重读周期前收到完成提示并准确 get 终态。
    watched = submit(21, 5000, 'native notification source')
    abandoned = spawn(['execution', 'watch', watched, '--jsonl', '--timeout-ms', '10000'])
    initial = first_line(abandoned)
    assert initial['result']['phase'] != 'Terminal'
    assert initial['result']['execution_ref']['execution_id'] == watched
    abandoned.kill()  # 仅关闭本测试创建的观察客户端；不是 Ctrl+C/cancel 证明。
    abandoned.communicate(timeout=3)
    assert call(['execution', 'get', watched])['result']['phase'] != 'Terminal'
    watcher = spawn(['execution', 'watch', watched, '--jsonl', '--timeout-ms', '10000'])
    output, error = watcher.communicate(timeout=8)
    assert watcher.returncode == 0, (watcher.pid, watcher.returncode, output, error)
    snapshots = [json.loads(line) for line in output.splitlines()]
    assert len(snapshots) >= 2 and snapshots[0]['result']['phase'] != 'Terminal'
    assert snapshots[-1]['result']['phase'] == 'Terminal'
    assert all(item['result']['execution_ref']['execution_id'] == watched for item in snapshots)
    assert result(watched) == {'amount': 22, 'text': 'native notification source', 'stop_observed': False}
    call(['execution', 'get', 'f' * 32], 3)
    assert server.poll() is None
    print('Managed multi-process submit/get/wait/cancel/result/list/watch and session recycling passed', flush=True)
finally:
    for process in children:
        if process.poll() is None:
            process.kill()
            process.communicate()
    if server.poll() is None:
        try:
            _, error = server.communicate('stop\n', timeout=15)
            if error:
                print(error, file=sys.stderr)
        except subprocess.TimeoutExpired:
            server.kill()
            server.communicate()
    assert server.returncode == 0, server.returncode

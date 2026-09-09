"""真实 managed Host 的 OCK1 观察/游标边界；TTL 使用生产时钟和生产 120 s。"""
import ctypes
import json
import msvcrt
import os
import struct
import subprocess
import sys
import time
import uuid

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p,
                          ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p]
k32.CreateFileW.restype = ctypes.c_void_p
k32.GetNamedPipeServerProcessId.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
k32.PeekNamedPipe.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p,
                            ctypes.POINTER(ctypes.c_ulong), ctypes.c_void_p]
servers, connections = [], []

def start():
    instance = 'managed-wire-' + uuid.uuid4().hex
    server = subprocess.Popen([sys.argv[1], '--instance', instance], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              creationflags=subprocess.CREATE_NO_WINDOW)
    servers.append(server)
    assert json.loads(server.stdout.readline()) == {'ready': True}
    print('HOST', server.pid, instance, flush=True)
    return instance, server

class Wire:
    def __init__(self, endpoint):
        instance, server = endpoint
        self.handle = k32.CreateFileW('\\\\.\\pipe\\ock.' + instance, 0xC0000000, 0, None, 3, 0x00110000, None)
        assert self.handle != ctypes.c_void_p(-1).value, ctypes.get_last_error()
        pid = ctypes.c_ulong()
        assert k32.GetNamedPipeServerProcessId(self.handle, ctypes.byref(pid)) and pid.value == server.pid
        self.pipe = os.fdopen(msvcrt.open_osfhandle(self.handle, os.O_RDWR | os.O_BINARY), 'r+b', buffering=0)
        self.serial, self.events = 0, []
        connections.append(self)
        self.hello = self.call('host.hello', {'api_version': 'ock.control/1'})['result']
        assert self.hello['observation_backend'] == 'managed'
        assert self.hello['notification_protocol'] == 'ock.notifications/1'

    def available(self):
        size = ctypes.c_ulong()
        assert k32.PeekNamedPipe(self.handle, None, 0, None, ctypes.byref(size), None), ctypes.get_last_error()
        return size.value

    def exact(self, count, deadline):
        value = b''
        while len(value) < count:
            assert time.monotonic() < deadline, 'wire read deadline'
            available = self.available()
            if not available:
                time.sleep(0.005)
                continue
            chunk = self.pipe.read(min(count - len(value), available))
            assert chunk, 'unexpected EOF'
            value += chunk
        return value

    def receive(self):
        deadline = time.monotonic() + 10
        header = self.exact(12, deadline)
        assert header[:8] == b'OCK1\x00\x01\x00\x00', header
        size = struct.unpack('>I', header[8:])[0]
        assert 0 < size <= 65536
        return json.loads(self.exact(size, deadline))

    def event(self, frame):
        assert frame['method'] == 'notifications.event' and 'id' not in frame
        assert len(self.events) < 128
        self.events.append(frame['params'])

    def call(self, method, params):
        self.serial += 1
        identifier = str(self.serial)
        body = json.dumps({'jsonrpc': '2.0', 'id': identifier, 'method': method, 'params': params}).encode()
        self.pipe.write(b'OCK1\x00\x01\x00\x00' + struct.pack('>I', len(body)) + body)
        for _ in range(129):
            frame = self.receive()
            if frame.get('id') == identifier:
                return frame
            self.event(frame)
        raise AssertionError('unbounded events before response')

    def close(self):
        if not self.pipe.closed:
            self.pipe.close()

def listed(wire, cursor=None, phase='terminal'):
    params = {'owner': 'self', 'phase_set': phase, 'page_size': 1}
    if cursor is not None:
        params['cursor'] = cursor
    return wire.call('execution.list', params)

def subscribe(wire, identities):
    return wire.call('notifications.subscribe', {'filter': {'executions': [
        {'execution_id': ref} for ref in identities]}, 'topics': ['execution.phase']})

def token(response):
    return {key: response['result'][key] for key in ('subscription_id', 'stream_generation')}

def unsubscribe(wire, value):
    return wire.call('notifications.unsubscribe', value)['result']['removed']

def ref(value):
    return {'execution_ref': {'execution_id': value}}

try:
    endpoint = start()
    first, second = Wire(endpoint), Wire(endpoint)
    other = Wire(start())
    assert first.hello['host_incarnation'] != other.hello['host_incarnation']
    card = first.call('capabilities.describe', {'name': 'sample.compute', 'version': '1.0.0'})['result']

    def submit(delay=0):
        response = first.call('operation.submit', {'operation': {'name': 'sample.compute', 'version': '1.0.0'},
            'contract_digest': card['contract_digest'], 'args': {'amount': 1, 'delay_ms': delay, 'text': 'wire'},
            'execution_timeout_ms': 10000})['result']
        assert response['kind'] == 'Accepted'
        return response['execution_ref']['execution_id']

    def wait(value):
        result = first.call('execution.wait', {**ref(value), 'wait_timeout_ms': 5000})['result']
        assert result['wait_state'] == 'Terminal'

    original = [submit() for _ in range(3)]
    for value in original:
        wait(value)
    page = listed(first)['result']
    cursor = page['next_cursor']
    issued = time.monotonic()
    assert len(cursor) <= 2048
    later = submit()
    wait(later)
    observed = [page['items'][0]['execution_ref']['execution_id']]
    following = listed(first, cursor)['result']
    continuation = following['next_cursor']
    observed.append(following['items'][0]['execution_ref']['execution_id'])
    last = listed(first, continuation)['result']
    observed.append(last['items'][0]['execution_ref']['execution_id'])
    assert 'next_cursor' not in last and set(observed) == set(original) and later not in observed
    assert listed(first, cursor)['result']['items'] == following['items']  # stateless replay
    altered = cursor[:cursor.rfind('.') + 1] + ('A' if cursor[cursor.rfind('.') + 1] != 'A' else 'B') + cursor[cursor.rfind('.') + 2:]
    for label, wire, value, phase in [('MAC', first, altered, 'terminal'),
            ('connection', second, cursor, 'terminal'), ('Host', other, cursor, 'terminal'),
            ('phase', first, cursor, 'all')]:
        response = listed(wire, value, phase)
        assert response['error']['code'] == -32011, (label, response)
        print('cursor rejection', label, response['error'], flush=True)
    assert listed(first, 'v1.' + 'a' * 2049)['error']['code'] == -32602
    assert 'error' in first.call('execution.list', {'owner': '0' * 31 + '2'})

    assert 'error' in subscribe(first, [original[0], 'f' * 32])
    subscriptions = [token(subscribe(first, [original[0]])) for _ in range(8)]
    assert len({value['stream_generation'] for value in subscriptions}) == 8
    assert 'error' in subscribe(first, [original[0]])
    assert not unsubscribe(second, subscriptions[0])
    for value in subscriptions:
        assert unsubscribe(first, value)
        assert not unsubscribe(first, value)
    print('atomic filter rejection, 8-watch quota, cross-connection and repeated unsubscribe passed', flush=True)

    running = submit(1500)
    before = len(first.events)
    subscription = token(subscribe(first, [running]))
    assert len(first.events) == before, 'event overtook subscribe ACK'
    initial = first.call('execution.get', ref(running))['result']
    assert initial['phase'] != 'Terminal'
    wait(running)
    until = time.monotonic() + 2
    while len(first.events) == before and time.monotonic() < until:
        if first.available():
            first.event(first.receive())
        else:
            time.sleep(0.005)
    events = first.events[before:]
    assert events
    sequences = [int(value['sequence']) for value in events]
    assert sequences == sorted(set(sequences))
    for event in events:
        assert event['subscription_id'] == subscription['subscription_id']
        assert event['stream_generation'] == subscription['stream_generation']
        assert event['execution_ref']['execution_id'] == running
        assert int(event['observation_version']) >= int(initial['observation_version'])
    assert unsubscribe(first, subscription)
    old = token(subscribe(second, [running]))
    second.close()
    replacement = Wire(endpoint)
    fresh = token(subscribe(replacement, [running]))
    assert fresh['stream_generation'] != old['stream_generation']
    assert not unsubscribe(replacement, old) and unsubscribe(replacement, fresh)
    replacement.close()
    print('real subscribe ACK barrier, events, observation versions and reconnect generation passed', flush=True)
    # 验证样例声明的八连接均可装配六个发送协调器，不被会话索引误限为两连接。
    parallel = [Wire(endpoint) for _ in range(7)]
    for connection in parallel:
        assert connection.call('execution.get', ref(original[0]))['result']['phase'] == 'Terminal'
    for connection in parallel:
        connection.close()
    print('eight live connections with real authorized query coordinators passed', flush=True)

    # 真实生产 TTL；只读 keepalive 不替换原 cursor，也不改变系统时钟。
    report = 0
    while time.monotonic() - issued < 122:
        time.sleep(max(0, min(1, 122 - (time.monotonic() - issued))))
        elapsed = time.monotonic() - issued
        if int(elapsed) // 10 > report:
            report = int(elapsed) // 10
            assert first.call('execution.get', ref(original[0]))['result']['phase'] == 'Terminal'
            print('production TTL elapsed seconds', round(elapsed, 2), flush=True)
    for value in (cursor, continuation):
        expired = listed(first, value)
        assert expired['error']['code'] == -32012, expired
    assert 'result' in listed(first)
    print('original and continued cursors expired on unchanged real clock; fresh list succeeds', flush=True)
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

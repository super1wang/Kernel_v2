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


import json
import struct
import subprocess
import sys
import uuid
import ctypes
import msvcrt
import os

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
kernel32.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p]
kernel32.CreateFileW.restype = ctypes.c_void_p
def open_pipe(instance):
    handle = kernel32.CreateFileW("\\\\.\\pipe\\ock." + instance, 0xC0000000, 0, None, 3, 0x00110000, None)
    assert handle != ctypes.c_void_p(-1).value, ctypes.get_last_error()
    return os.fdopen(msvcrt.open_osfhandle(handle, os.O_RDWR | os.O_BINARY), "r+b", buffering=0)

def read_exact(pipe, count):
    result = b""
    while len(result) < count:
        chunk = pipe.read(min(7, count - len(result)))
        assert chunk, "truncated frame"
        result += chunk
    return result

for mode in ["revoked", "started"]:
    instance = "observe-" + uuid.uuid4().hex
    server = subprocess.Popen([sys.argv[1], mode, instance], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
    try:
        assert server.stdout.readline().strip() == "READY"
        with open_pipe(instance) as pipe:
            body = b'{"begin":true}'
            pipe.write(b"OCK1\x00\x01\x00\x00" + struct.pack(">I", len(body)) + body)
            messages = []
            while True:
                header = read_exact(pipe, 12)
                assert header[:8] == b"OCK1\x00\x01\x00\x00"
                message = json.loads(read_exact(pipe, struct.unpack(">I", header[8:])[0]))
                if message.get("done"):
                    break
                messages.append(message)
            assert messages[0]["id"] == "subscribe", "event preceded ACK"
            events = [message for message in messages if message.get("method") == "notifications.event"]
            assert len(events) == (0 if mode == "revoked" else 1), messages
            if events:
                assert events[0]["params"]["sequence"] == "1"
        stdout, stderr = server.communicate(timeout=10)
        assert server.returncode == 0, (stdout, stderr)
        print(mode, "actual received frame ordering passed")
    finally:
        if server.poll() is None:
            try:
                server.wait(timeout=2)
            except subprocess.TimeoutExpired:
                server.kill()
        output, errors = server.communicate()
        if errors:
            print(mode, output, errors, file=sys.stderr)

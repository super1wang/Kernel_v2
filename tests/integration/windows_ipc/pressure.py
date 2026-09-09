import ctypes
import json
import msvcrt
import os
import struct
import subprocess
import sys
import time
import uuid

k32=ctypes.WinDLL("kernel32",use_last_error=True)
k32.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_void_p]
k32.CreateFileW.restype=ctypes.c_void_p
k32.PeekNamedPipe.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_ulong,ctypes.c_void_p,ctypes.POINTER(ctypes.c_ulong),ctypes.c_void_p]
def connect(instance):
    handle=k32.CreateFileW("\\\\.\\pipe\\ock."+instance,0xC0000000,0,None,3,0x00110000,None)
    assert handle!=ctypes.c_void_p(-1).value,ctypes.get_last_error()
    return os.fdopen(msvcrt.open_osfhandle(handle,os.O_RDWR|os.O_BINARY),"r+b",buffering=0)
def send(pipe,value):
    body=json.dumps(value).encode()
    pipe.write(b"OCK1\0\1\0\0"+struct.pack(">I",len(body))+body)
def read(pipe):
    def exact(n):
        data=b""
        while len(data)<n:
            chunk=pipe.read(n-len(data));assert chunk,"truncated"
            data+=chunk
        return data
    header=exact(12);assert header[:8]==b"OCK1\0\1\0\0"
    return json.loads(exact(struct.unpack(">I",header[8:])[0]))
for mode in ["priority","slow","unknown"]:
    instance="pressure-"+uuid.uuid4().hex
    server=subprocess.Popen([sys.argv[1],mode,instance],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding="utf-8")
    try:
        assert server.stdout.readline().strip()=="READY"
        with connect(instance) as pipe:
            if mode=="priority":
                send(pipe,{"phase":"priority"}); assert read(pipe)=={"control":True}
            elif mode=="slow":
                send(pipe,{"phase":"slow"}); assert server.stdout.readline().strip()=="SLOW_QUEUED"
                start=time.monotonic()
                with connect(instance) as fast:
                    send(fast,{"phase":"fast"});assert read(fast)=={"fast":True}
                elapsed=time.monotonic()-start
                assert elapsed<1.5,elapsed
                # 慢连接始终不读；服务端必须按自身期限关闭并排空，不依赖本客户端 close。
                assert server.stdout.readline().strip()=="CLOSED"
                print("independent authenticated control latency",elapsed)
            else:
                send(pipe,{"phase":"fill"}); assert server.stdout.readline().strip()=="FILLED_QUEUED"
                available=ctypes.c_ulong()
                deadline=time.monotonic()+2
                while time.monotonic()<deadline:
                    assert k32.PeekNamedPipe(msvcrt.get_osfhandle(pipe.fileno()),None,0,None,ctypes.byref(available),None)
                    if available.value==65536:break
                assert available.value==65536,available.value
                while time.monotonic()<deadline:
                    send(pipe,{"phase":"prefix"})
                    outcome=server.stdout.readline().strip()
                    if outcome!="NOT_STARTED":break
                assert outcome=="UNKNOWN",outcome
                assert server.stdout.readline().strip()=="CLOSED"
        stdout,stderr=server.communicate(timeout=10)
        assert server.returncode==0,(stdout,stderr)
        print(mode,"passed")
    finally:
        if server.poll() is None:server.kill()
        server.communicate()

import ctypes
import json
import msvcrt
import os
import struct
import subprocess
import sys
import uuid

k32=ctypes.WinDLL('kernel32',use_last_error=True)
k32.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.c_ulong,ctypes.c_void_p]
k32.CreateFileW.restype=ctypes.c_void_p
service,cli=sys.argv[1:3]
instance='wire-security-'+uuid.uuid4().hex
server=subprocess.Popen([service,'--instance',instance],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
def frame(value):
    body=json.dumps(value).encode()
    return b'OCK1\x00\x01\x00\x00'+struct.pack('>I',len(body))+body
def exact(pipe,n):
    data=b''
    while len(data)<n:
        chunk=pipe.read(n-len(data))
        assert chunk,'unexpected EOF'
        data+=chunk
    return data
def receive(pipe):
    header=exact(pipe,12)
    return json.loads(exact(pipe,struct.unpack('>I',header[8:])[0]))
def request(method,params):return {'jsonrpc':'2.0','id':'probe','method':method,'params':params}
try:
    assert json.loads(server.stdout.readline())=={'ready':True}
    duplicate=subprocess.run([service,'--instance',instance],input=b'stop\n',capture_output=True,timeout=10)
    assert duplicate.returncode==6,(duplicate.returncode,duplicate.stdout,duplicate.stderr)
    for mode in ['role-in-hello','role-in-envelope','wrong-direction','before-hello','oversize-header']:
        handle=k32.CreateFileW('\\\\.\\pipe\\ock.'+instance,0xC0000000,0,None,3,0x00110000,None)
        assert handle!=ctypes.c_void_p(-1).value,ctypes.get_last_error()
        with os.fdopen(msvcrt.open_osfhandle(handle,os.O_RDWR|os.O_BINARY),'r+b',buffering=0) as pipe:
            hello=request('host.hello',{'api_version':'ock.control/1'})
            if mode=='role-in-hello':
                hello['params']['role']='admin';bad=frame(hello)
            elif mode=='role-in-envelope':
                hello['role']='admin';bad=frame(hello)
            elif mode=='before-hello':bad=frame(request('capabilities.search',{}))
            else:
                pipe.write(frame(hello));assert receive(pipe)['result']['observation_backend']=='absent'
                bad=frame({'jsonrpc':'2.0','method':'notifications.event','params':{}}) if mode=='wrong-direction' else b'OCK1\x00\x01\x00\x00'+struct.pack('>I',4*1024*1024)
            # A queued valid request after fatal input must never execute.
            pipe.write(bad+frame(request('capabilities.search',{})))
            if mode not in ['wrong-direction','oversize-header']:
                assert 'error' in receive(pipe)
            try: remaining=pipe.read(1)
            except OSError as error:
                assert error.winerror in (109,232) or error.errno==22,error
                remaining=b''
            assert remaining==b'',(mode,remaining)
        print(mode,'rejected and connection closed')
    result=subprocess.run([cli,'--instance',instance,'capabilities','search'],capture_output=True,timeout=10)
    assert result.returncode==0,(result.stdout,result.stderr)
finally:
    if server.poll() is None:
        try:server.communicate(b'stop\n',timeout=10)
        except subprocess.TimeoutExpired:server.kill();server.communicate()
    assert server.returncode==0,server.returncode

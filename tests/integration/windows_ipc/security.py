import csv
import ctypes
import io
import subprocess
import sys
import uuid
from ctypes import wintypes as w

k32=ctypes.WinDLL("kernel32",use_last_error=True)
advapi=ctypes.WinDLL("advapi32",use_last_error=True)
class Security(ctypes.Structure):
    _fields_=[("length",w.DWORD),("descriptor",ctypes.c_void_p),("inherit",w.BOOL)]
k32.CreateNamedPipeW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,w.DWORD,w.DWORD,w.DWORD,w.DWORD,ctypes.POINTER(Security)]
k32.CreateNamedPipeW.restype=w.HANDLE
k32.CreateFileW.argtypes=[w.LPCWSTR,w.DWORD,w.DWORD,ctypes.c_void_p,w.DWORD,w.DWORD,w.HANDLE]
k32.CreateFileW.restype=w.HANDLE
k32.CloseHandle.argtypes=[w.HANDLE]
k32.LocalFree.argtypes=[ctypes.c_void_p]
advapi.ConvertStringSecurityDescriptorToSecurityDescriptorW.argtypes=[w.LPCWSTR,w.DWORD,ctypes.POINTER(ctypes.c_void_p),ctypes.c_void_p]
invalid=ctypes.c_void_p(-1).value
sid=list(csv.reader(io.StringIO(subprocess.check_output(["whoami","/user","/fo","csv","/nh"],text=True))))[0][-1]
descriptor=ctypes.c_void_p()
assert advapi.ConvertStringSecurityDescriptorToSecurityDescriptorW("D:P(A;;GA;;;"+sid+")",1,ctypes.byref(descriptor),None)
security=Security(ctypes.sizeof(Security),descriptor,False)
positive_name="ock-remote-positive-"+uuid.uuid4().hex
positive=k32.CreateNamedPipeW("\\\\.\\pipe\\"+positive_name,3|0x40000000,0,1,4096,4096,0,ctypes.byref(security))
k32.LocalFree(descriptor)
assert positive!=invalid,ctypes.get_last_error()
instance="security-"+uuid.uuid4().hex
server=subprocess.Popen([sys.argv[1],"server",instance],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding="utf-8")
try:
    assert server.stdout.readline().strip()=="READY"
    # 同一 SMB 路径的正控制排除“本机网络服务不可用导致所有连接都失败”的伪拒绝。
    permitted=k32.CreateFileW("\\\\localhost\\pipe\\"+positive_name,0xC0000000,0,None,3,0x00110000,None)
    assert permitted!=invalid,("remote positive control unavailable",ctypes.get_last_error())
    k32.CloseHandle(permitted)
    denied=k32.CreateFileW("\\\\localhost\\pipe\\ock."+instance,0xC0000000,0,None,3,0x00110000,None)
    failure=ctypes.get_last_error()
    if denied!=invalid:k32.CloseHandle(denied)
    assert denied==invalid and failure==5,("remote client not denied by policy",failure)
    client=subprocess.run([sys.argv[1],"client",instance],capture_output=True,timeout=10)
    assert client.returncode==0,(client.returncode,client.stderr)
    output,errors=server.communicate(timeout=10)
    assert server.returncode==0,(output,errors)
    print("SMB positive control succeeded; production remote denied; local peer succeeded")
finally:
    k32.CloseHandle(positive)
    if server.poll() is None:server.kill()
    server.communicate()

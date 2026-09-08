"""固定生命周期故障消费者；只操作 execute 继承的三句柄，不进入 SDK。"""
import ctypes
from ctypes import wintypes as w
import os
from pathlib import Path
import struct
import subprocess
import sys
import time

RECORD=struct.Struct('<II16sIIIIQQQQQ')
k=ctypes.WinDLL('kernel32',use_last_error=True)
for name,args,result in [
    ('MapViewOfFile',[w.HANDLE,w.DWORD,w.DWORD,w.DWORD,ctypes.c_size_t],ctypes.c_void_p),
    ('SetEvent',[w.HANDLE],w.BOOL),('WaitForSingleObject',[w.HANDLE,w.DWORD],w.DWORD),
    ('QueryPerformanceCounter',[ctypes.POINTER(ctypes.c_longlong)],w.BOOL)]:
    fn=getattr(k,name);fn.argtypes=args;fn.restype=result


def main():
    mode=sys.argv[1]
    if mode=='descendant':
        time.sleep(60);return 0
    if mode=='sentinel':
        ready,release=map(Path,sys.argv[2:4]);ready.write_text(str(os.getpid()))
        deadline=time.monotonic()+30
        while time.monotonic()<deadline:
            if release.exists():print('sentinel released normally',flush=True);return 0
            time.sleep(.02)
        return 9
    index=sys.argv.index('--ock-observation')
    mapping,event,ack=map(int,sys.argv[index+1:index+4])
    nonce=bytes.fromhex(sys.argv[index+4]);generation=int(sys.argv[index+5])
    view=k.MapViewOfFile(mapping,0xF001F,0,0,RECORD.size)
    if not view:raise ctypes.WinError(ctypes.get_last_error())
    def stage(phase,wait=True):
        tick=ctypes.c_longlong()
        if not k.QueryPerformanceCounter(ctypes.byref(tick)):raise ctypes.WinError(ctypes.get_last_error())
        raw=RECORD.pack(1,80,nonce,os.getpid(),phase,phase+1,0,tick.value,1,0,0,generation)
        ctypes.memmove(view,raw,len(raw))
        if not k.SetEvent(event):raise ctypes.WinError(ctypes.get_last_error())
        if wait and k.WaitForSingleObject(ack,10000)!=0:raise RuntimeError('ACK timeout')
    if mode=='stage_timeout':
        stage(0);time.sleep(10);return 8
    if mode=='descendants_alive':
        child=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),'descendant'],close_fds=True)
        print('owned descendant '+str(child.pid),flush=True)
        stage(0);return 0
    if mode not in ('normal','extra_phase'):raise ValueError('fixed mode required')
    for phase in range(11):stage(phase)
    if mode=='extra_phase':
        stage(11,False)
        # 单个线程通知后立即退出，使 owner 的 root 退出优先分支接受检验。
        os._exit(0)
    return 0


if __name__=='__main__':sys.exit(main())

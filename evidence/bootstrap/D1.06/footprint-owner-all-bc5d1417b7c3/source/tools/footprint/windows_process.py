"""有限 Win32 观测；进程/Job 创建和终止仍仅由 evidence.process 拥有。"""
import ctypes
from ctypes import wintypes as w
from dataclasses import dataclass,asdict
import hashlib
import json
import os
import struct
import time
import uuid

PHASES=('ProcessMainEntered','HostConstructionBegin','HostReady','WarmupComplete','IdleSamplingBegin',
        'ShutdownBegin','ShutdownComplete','BoundReleased','SessionReleased','OwnersReleased','ExitPermitted')
# C++ shim 使用 pack(1)，固定 little-endian、80 字节，无地址或动态正文。
RECORD=struct.Struct('<II16sIIIIQQQQQ')
METHOD='ock.native-footprint/2'


class ObservationError(RuntimeError):
    pass


@dataclass(frozen=True)
class ObservationConfig:
    mode:str
    setup_timeout_ms:int
    stage_timeout_ms:int
    max_samples:int
    max_modules:int
    max_threads:int
    module_path_chars:int
    module_attempts:int
    calibration_bytes:int
    calibration_hold_ms:int
    ready_delay_ms:int

    def __post_init__(self):
        if self.mode not in ('occupancy','allocation','latency'):
            raise ValueError('unknown observation mode')
        limits={'setup_timeout_ms':60000,'stage_timeout_ms':60000,'max_samples':100000,
                'max_modules':4096,'max_threads':65536,'module_path_chars':32768,'module_attempts':8,
                'calibration_bytes':134217728,'calibration_hold_ms':5000,'ready_delay_ms':5000}
        for name,maximum in limits.items():
            value=getattr(self,name)
            if type(value) is not int or not 0<value<=maximum:
                raise ValueError(name+' must be finite positive bounded integer')
        if self.module_path_chars<260 or self.calibration_bytes%4096:
            raise ValueError('module path capacity or page-aligned calibration bytes invalid')
        if self.stage_timeout_ms<=1500 or self.calibration_hold_ms<500:
            raise ValueError('stage budget/hold cannot omit fixed windows')

    @property
    def sample_interval_ms(self):
        return None if self.mode=='latency' else 5

    @property
    def memory_sampling(self):return 'phase_boundaries' if self.mode=='latency' else 'due_5ms'

    @property
    def thread_sampling(self):return 'phase_boundaries'

    def identity(self):
        data={'method':METHOD,**asdict(self),'sample_interval_ms':self.sample_interval_ms,
              'memory_sampling':self.memory_sampling,'thread_sampling':self.thread_sampling}
        return hashlib.sha256(json.dumps(data,sort_keys=True,separators=(',',':')).encode()).hexdigest()


class ProtocolState:
    """纯值验证器；不会按 PID 查询、附着或终止任何进程。"""
    def __init__(self,nonce,generation,actual_pid,pre_create):
        self.nonce=nonce;self.generation=generation;self.actual_pid=actual_pid
        self.previous_ticks=pre_create;self.next_index=0

    @property
    def complete(self):return self.next_index==len(PHASES)

    def accept(self,raw,observed_ticks):
        if len(raw)!=RECORD.size or self.complete:raise ObservationError('InvalidRecordSizeOrExtraPhase')
        version,size,nonce,pid,phase,sequence,reserved,ticks,checks,owners,sentinels,generation=RECORD.unpack(raw)
        if (version!=1 or size!=RECORD.size or nonce!=self.nonce or pid!=self.actual_pid or
            phase!=self.next_index or sequence!=self.next_index+1 or reserved!=0 or generation!=self.generation):
            raise ObservationError('PhaseIdentityOrOrderMismatch')
        if ticks<self.previous_ticks or ticks>observed_ticks or checks!=1:
            raise ObservationError('InvalidTicksOrConsumerChecks')
        self.previous_ticks=ticks;self.next_index+=1
        return {'phase':PHASES[phase],'sequence':sequence,'child_ticks':ticks,'parent_observed_ticks':observed_ticks,
                'checks':checks,'owners':owners,'sentinels':sentinels}


class _Scope:
    """仅供 execute 内部创建。裸 HANDLE 不作为公共参数或返回值。"""
    def __init__(self,configuration):
        if type(configuration) is not ObservationConfig:raise ValueError('immutable ObservationConfig required; callbacks/PIDs prohibited')
        configuration.__post_init__()
        self.config=configuration;self._closed=False;self._bound=False
        self._owned=[];self._view=None;self._process=None;self._pid=None;self._pending=None
        self._k=ctypes.WinDLL('kernel32',use_last_error=True)
        self._p=ctypes.WinDLL('psapi',use_last_error=True)
        signatures={
          'QueryPerformanceCounter':([ctypes.POINTER(ctypes.c_longlong)],w.BOOL),
          'QueryPerformanceFrequency':([ctypes.POINTER(ctypes.c_longlong)],w.BOOL),
          'CreateFileMappingW':([w.HANDLE,ctypes.c_void_p,w.DWORD,w.DWORD,w.DWORD,w.LPCWSTR],w.HANDLE),
          'MapViewOfFile':([w.HANDLE,w.DWORD,w.DWORD,w.DWORD,ctypes.c_size_t],ctypes.c_void_p),
          'UnmapViewOfFile':([ctypes.c_void_p],w.BOOL),
          'CreateEventW':([ctypes.c_void_p,w.BOOL,w.BOOL,w.LPCWSTR],w.HANDLE),
          'SetEvent':([w.HANDLE],w.BOOL),
          'WaitForSingleObject':([w.HANDLE,w.DWORD],w.DWORD),
          'CloseHandle':([w.HANDLE],w.BOOL),
          'CreateToolhelp32Snapshot':([w.DWORD,w.DWORD],w.HANDLE),
          'Thread32First':([w.HANDLE,ctypes.c_void_p],w.BOOL),
          'Thread32Next':([w.HANDLE,ctypes.c_void_p],w.BOOL)}
        for name,(args,returns) in signatures.items():
            fn=getattr(self._k,name);fn.argtypes=args;fn.restype=returns
        for name,args,returns in [
          ('GetProcessMemoryInfo',[w.HANDLE,ctypes.c_void_p,w.DWORD],w.BOOL),
          ('EnumProcessModulesEx',[w.HANDLE,ctypes.c_void_p,w.DWORD,ctypes.POINTER(w.DWORD),w.DWORD],w.BOOL),
          ('GetModuleFileNameExW',[w.HANDLE,w.HANDLE,w.LPWSTR,w.DWORD],w.DWORD)]:
            fn=getattr(self._p,name);fn.argtypes=args;fn.restype=returns
        frequency=ctypes.c_longlong();self._check(self._k.QueryPerformanceFrequency(ctypes.byref(frequency)))
        if frequency.value<=0:raise ObservationError('InvalidQPCFrequency')
        self.frequency=frequency.value
        self.nonce=uuid.uuid4().bytes;self.generation=int.from_bytes(os.urandom(8),'little') or 1
        self.data={'status':'Incomplete','method':METHOD,'configuration':asdict(configuration),
                   'memory_sampling':configuration.memory_sampling,'thread_sampling':configuration.thread_sampling,
                   'method_digest':configuration.identity(),'nonce':self.nonce.hex(),'generation':self.generation,
                   'qpc_frequency':self.frequency,'samples':[],'phases':[],'modules':[],'events':[],
                   'errors':[],'cleanup_errors':[],'ready':None}
        try:
            mapping=self._check(self._k.CreateFileMappingW(w.HANDLE(-1),None,4,0,RECORD.size,None));self._owned.append(mapping)
            self._view=self._check(self._k.MapViewOfFile(mapping,0xF001F,0,0,RECORD.size))
            self._event=self._check(self._k.CreateEventW(None,False,False,None));self._owned.append(self._event)
            self._ack=self._check(self._k.CreateEventW(None,False,False,None));self._owned.append(self._ack)
        except BaseException:
            self.close();raise

    @staticmethod
    def _check(ok):
        if not ok:raise ctypes.WinError(ctypes.get_last_error())
        return ok

    def _live(self):
        if self._closed or not self._bound:raise ObservationError('ScopeClosed')

    def ticks(self):
        value=ctypes.c_longlong();self._check(self._k.QueryPerformanceCounter(ctypes.byref(value)));return value.value

    def _launch_material(self):
        # 仅 execute 的创建逻辑读取；从不向报告返回可复用 HANDLE。
        if self._closed:raise ObservationError('ScopeClosed')
        args=['--ock-observation',str(self._owned[0]),str(self._event),str(self._ack),self.nonce.hex(),str(self.generation)]
        return tuple(self._owned),args

    def pre_create(self):
        ticks=self.ticks();self.data['pre_create_ticks']=ticks;return ticks

    def bind_assigned(self,actual_pid,process_handle):
        if self._closed or self._bound:raise ObservationError('ScopeClosedOrAlreadyBound')
        self._bound=True;self._pid=actual_pid;self._process=process_handle
        self.protocol=ProtocolState(self.nonce,self.generation,actual_pid,self.data['pre_create_ticks'])
        self.data['actual_pid']=actual_pid;self.data['events'].append({'event':'assigned_suspended','ticks':self.ticks()})
        self._sample('assigned_suspended')

    def resumed(self,absolute_deadline):
        self._live();self._deadline=absolute_deadline;now=time.monotonic()
        self._stage_deadline=min(absolute_deadline,now+self.config.setup_timeout_ms/1000)
        self._next_sample=now+.005
        self.data['events'].append({'event':'resumed','ticks':self.ticks()})

    def _memory(self):
        self._live()
        class Memory(ctypes.Structure):
            _fields_=[('cb',w.DWORD),('PageFaultCount',w.DWORD)]+[(n,ctypes.c_size_t) for n in
               ('PeakWorkingSetSize','WorkingSetSize','QuotaPeakPagedPoolUsage','QuotaPagedPoolUsage',
                'QuotaPeakNonPagedPoolUsage','QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage','PrivateUsage')]
        value=Memory();value.cb=ctypes.sizeof(value)
        ok=bool(self._p.GetProcessMemoryInfo(self._process,ctypes.byref(value),value.cb))
        return {'ok':ok,'win32_error':0 if ok else ctypes.get_last_error(),'structure_bytes':ctypes.sizeof(value),
                'private_bytes':value.PrivateUsage if ok else None,'working_set_bytes':value.WorkingSetSize if ok else None,
                'peak_working_set_bytes':value.PeakWorkingSetSize if ok else None,
                'peak_commit_bytes':value.PeakPagefileUsage if ok else None}

    def _threads(self):
        self._live()
        class Thread(ctypes.Structure):
            _fields_=[(n,w.DWORD) for n in ('dwSize','cntUsage','th32ThreadID','th32OwnerProcessID')]+[('tpBasePri',w.LONG),('tpDeltaPri',w.LONG),('dwFlags',w.DWORD)]
        handle=self._k.CreateToolhelp32Snapshot(4,0)
        if handle in (None,ctypes.c_void_p(-1).value):raise ctypes.WinError(ctypes.get_last_error())
        ids=[]
        try:
            entry=Thread();entry.dwSize=ctypes.sizeof(entry)
            more=self._k.Thread32First(handle,ctypes.byref(entry))
            if not more and ctypes.get_last_error()!=18:raise ctypes.WinError(ctypes.get_last_error())
            while more:
                if entry.dwSize<16:raise ObservationError('TruncatedThreadEntry')
                if entry.th32OwnerProcessID==self._pid:
                    if len(ids)>=self.config.max_threads:raise ObservationError('ThreadCapacityExceeded')
                    ids.append(entry.th32ThreadID)
                entry.dwSize=ctypes.sizeof(entry)
                more=self._k.Thread32Next(handle,ctypes.byref(entry))
            if ctypes.get_last_error()!=18:raise ctypes.WinError(ctypes.get_last_error())
        finally:self._k.CloseHandle(handle)
        if not ids:raise ObservationError('NoRootThreadsObserved')
        return {'ids':sorted(ids),'structure_bytes':ctypes.sizeof(Thread),'win32_error':0}

    def _sample(self,reason,missed=0):
        self._live()
        if reason not in ('periodic','assigned_suspended',*PHASES):raise ObservationError('InvalidSampleBoundary')
        if len(self.data['samples'])>=self.config.max_samples:raise ObservationError('SampleCapacityExceeded')
        ticks=self.ticks()
        row={'ticks':ticks,'reason':reason,'missed_intervals':missed,'ok':False,
             'private_bytes':None,'working_set_bytes':None,'peak_working_set_bytes':None,'peak_commit_bytes':None,
             'thread_ids':None,'thread_query':None,
             'memory_query':{'started_ticks':ticks,'completed_ticks':None,'ok':False,'win32_error':None}}
        self.data['samples'].append(row)
        try:
            try:
                memory=self._memory();row.update(memory)
                row['memory_query'].update(ok=memory['ok'],win32_error=memory['win32_error'],structure_bytes=memory['structure_bytes'])
            except BaseException as exc:
                row['memory_query'].update(error=str(exc),win32_error=getattr(exc,'winerror',None));raise
            finally:row['memory_query']['completed_ticks']=self.ticks()
            if not row['ok']:raise ObservationError('MemoryQueryFailed')
            if reason!='periodic':
                query={'started_ticks':self.ticks(),'completed_ticks':None,'ok':False,'win32_error':None}
                row['thread_query']=query
                try:
                    threads=self._threads();row['thread_ids']=threads['ids']
                    query.update(ok=True,win32_error=threads['win32_error'],structure_bytes=threads['structure_bytes'])
                except BaseException as exc:
                    row['ok']=False;row['thread_error']=str(exc)
                    query.update(error=str(exc),win32_error=getattr(exc,'winerror',None));raise
                finally:query['completed_ticks']=self.ticks()
        finally:row['completed_ticks']=self.ticks()
        return row

    def _modules(self,phase):
        self._live();required=w.DWORD();capacity=self.config.max_modules
        for attempt in range(self.config.module_attempts):
            handles=(w.HMODULE*capacity)()
            self._check(self._p.EnumProcessModulesEx(self._process,handles,ctypes.sizeof(handles),ctypes.byref(required),3))
            if required.value%ctypes.sizeof(w.HMODULE):raise ObservationError('InvalidModuleArraySize')
            count=required.value//ctypes.sizeof(w.HMODULE)
            if count>capacity:raise ObservationError('ModuleCapacityExceeded')
            modules=[]
            for handle in list(handles)[:count]:
                path=ctypes.create_unicode_buffer(self.config.module_path_chars)
                length=self._p.GetModuleFileNameExW(self._process,handle,path,len(path))
                if not length:raise ctypes.WinError(ctypes.get_last_error())
                if length>=len(path)-1:raise ObservationError('ModulePathTruncated')
                modules.append(path.value)
            confirm=w.DWORD();again=(w.HMODULE*capacity)()
            self._check(self._p.EnumProcessModulesEx(self._process,again,ctypes.sizeof(again),ctypes.byref(confirm),3))
            if confirm.value==required.value and list(again)[:count]==list(handles)[:count]:
                self.data['modules'].append({'phase':phase,'ticks':self.ticks(),'ok':True,'attempts':attempt+1,'required_bytes':required.value,'paths':modules});return
        raise ObservationError('ModuleSnapshotUnstable')

    def poll(self):
        self._live();now=time.monotonic()
        if now>=self._deadline:raise ObservationError('RunDeadlineReached')
        if now>=self._stage_deadline and not self.protocol.complete:raise ObservationError('StageDeadlineReached')
        signaled=self._k.WaitForSingleObject(self._event,0)
        if signaled==0xffffffff:raise ctypes.WinError(ctypes.get_last_error())
        if signaled==0:
            if self._pending:raise ObservationError('PhaseBeforeAcknowledgement')
            row=self.protocol.accept(ctypes.string_at(self._view,RECORD.size),self.ticks())
            self.data['phases'].append(row)
            sample=self._sample(row['phase'])
            if row['phase']=='HostReady':
                construction=next(p for p in self.data['phases'] if p['phase']=='HostConstructionBegin')
                self.data['ready']={'child_ticks':row['child_ticks'],'observed_ticks':row['parent_observed_ticks'],
                   'sample_ticks':sample['ticks'],'internal_ticks':row['child_ticks']-construction['child_ticks'],
                   'create_to_ready_ticks':row['child_ticks']-self.data['pre_create_ticks'],
                   'parent_observed_ticks_delta':row['parent_observed_ticks']-self.data['pre_create_ticks'],
                   'notification_lag_ticks':row['parent_observed_ticks']-row['child_ticks'],
                   'peak_working_set_bytes':sample['peak_working_set_bytes'],'peak_commit_bytes':sample['peak_commit_bytes']}
            if row['phase'] in ('WarmupComplete','ShutdownComplete'):self._modules(row['phase'])
            # 查询也消耗原运行/阶段预算；不能在耗尽后重置阶段期限或放行消费者。
            now=time.monotonic()
            if now>=self._deadline:raise ObservationError('RunDeadlineReached')
            if now>=self._stage_deadline:raise ObservationError('StageDeadlineReached')
            hold=1.0 if row['phase']=='IdleSamplingBegin' else (.5 if row['phase'] in ('WarmupComplete','ShutdownComplete','BoundReleased','SessionReleased','OwnersReleased') else 0)
            self._pending=(time.monotonic()+hold,row)
            self._stage_deadline=min(self._deadline,time.monotonic()+self.config.stage_timeout_ms/1000)
        if self._pending and time.monotonic()>=self._pending[0]:
            now=time.monotonic()
            if now>=self._deadline:raise ObservationError('RunDeadlineReached')
            if now>=self._stage_deadline:raise ObservationError('StageDeadlineReached')
            self._check(self._k.SetEvent(self._ack));self._pending[1]['ack_ticks']=self.ticks();self._pending=None
        if self.protocol.complete:return # 最终确认后只等待 owner 检出 root 退出，不再查询。
        now=time.monotonic()
        if self.config.mode!='latency' and now>=self._next_sample:
            missed=max(0,int((now-self._next_sample)/.005));self._next_sample+=(missed+1)*.005
            self._sample('periodic',missed)

    def root_exited(self):
        self._live();self.data['events'].append({'event':'root_exited','ticks':self.ticks()})
        try:
            # root 的原始句柄已报告退出；只检查 owner 自己的通道事件，不再查询进程。
            # 最终 ACK 后消费者可能先通知额外阶段再退出，不能让退出优先分支吞掉它。
            signaled=self._k.WaitForSingleObject(self._event,0)
            if signaled==0xffffffff:raise ctypes.WinError(ctypes.get_last_error())
            if signaled==0:
                self.data['status']='ObserverFailed'
                self.data['errors'].append('UnconsumedPhaseEventAtRootExit')
            else:
                self.data['status']='Complete' if self.protocol.complete and self._pending is None else 'Incomplete'
                if self.data['status']!='Complete':self.data['errors'].append('RootExitedBeforeProtocolComplete')
        finally:
            self._bound=False # 即便 Job 中其他进程仍活，也永久禁止 root 查询。

    def failed(self,exc):
        self.data['status']='ObserverFailed';self.data['errors'].append(type(exc).__name__+': '+str(exc))

    def close(self):
        if self._closed:return
        self._closed=True;self._bound=False;self._process=None
        if self._view:
            if not self._k.UnmapViewOfFile(self._view):self.data['cleanup_errors'].append('UnmapViewOfFile:'+str(ctypes.get_last_error()))
            self._view=None
        for handle in reversed(self._owned):
            if not self._k.CloseHandle(handle):self.data['cleanup_errors'].append('CloseHandle:'+str(ctypes.get_last_error()))
        self._owned=[]
        if self.data['cleanup_errors']:self.data['status']='ObserverFailed'

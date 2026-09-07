"""Windows 开发验证子进程：先纳入独占 Job 再运行，原始流直写文件。"""
import math
import ctypes
from ctypes import wintypes as w
from datetime import datetime, timezone
import os
from pathlib import Path
import shutil
import subprocess
import time


def now():
    return datetime.now(timezone.utc).isoformat()


def execute(argv, cwd, stdout_path, stderr_path, timeout):
    """只处置本次创建的 Job。外部进程从不按名称或系统 PID 枚举终止。"""
    if os.name != 'nt':
        raise RuntimeError('D0 Windows profile only')
    if not argv or type(timeout) not in (int, float) or not math.isfinite(timeout) or not 0 < timeout <= 3600:
        raise ValueError('nonempty argv and finite positive timeout required')
    result = {'argv': list(argv), 'cwd': str(cwd), 'started_at': now(),
              'exit_code': None, 'status': 'LaunchFailed', 'process_tree': {
                  'mechanism': 'WindowsJobObject', 'assigned_before_resume': False,
                  'active_after': None, 'total_processes': 0, 'terminated_owned_job': False}}
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    size_t = ctypes.c_size_t

    class Startup(ctypes.Structure):
        _fields_ = [('cb', w.DWORD), ('lpReserved', w.LPWSTR), ('lpDesktop', w.LPWSTR),
                    ('lpTitle', w.LPWSTR), ('dwX', w.DWORD), ('dwY', w.DWORD),
                    ('dwXSize', w.DWORD), ('dwYSize', w.DWORD), ('dwXCountChars', w.DWORD),
                    ('dwYCountChars', w.DWORD), ('dwFillAttribute', w.DWORD),
                    ('dwFlags', w.DWORD), ('wShowWindow', w.WORD), ('cbReserved2', w.WORD),
                    ('lpReserved2', ctypes.POINTER(ctypes.c_byte)), ('hStdInput', w.HANDLE),
                    ('hStdOutput', w.HANDLE), ('hStdError', w.HANDLE)]

    class ProcessInfo(ctypes.Structure):
        _fields_ = [('hProcess', w.HANDLE), ('hThread', w.HANDLE),
                    ('dwProcessId', w.DWORD), ('dwThreadId', w.DWORD)]

    class BasicLimit(ctypes.Structure):
        _fields_ = [('PerProcessUserTimeLimit', ctypes.c_longlong),
                    ('PerJobUserTimeLimit', ctypes.c_longlong), ('LimitFlags', w.DWORD),
                    ('MinimumWorkingSetSize', size_t), ('MaximumWorkingSetSize', size_t),
                    ('ActiveProcessLimit', w.DWORD), ('Affinity', size_t),
                    ('PriorityClass', w.DWORD), ('SchedulingClass', w.DWORD)]

    class IOCounters(ctypes.Structure):
        _fields_ = [(name, ctypes.c_ulonglong) for name in (
            'ReadOperationCount', 'WriteOperationCount', 'OtherOperationCount',
            'ReadTransferCount', 'WriteTransferCount', 'OtherTransferCount')]

    class ExtendedLimit(ctypes.Structure):
        _fields_ = [('BasicLimitInformation', BasicLimit), ('IoInfo', IOCounters),
                    ('ProcessMemoryLimit', size_t), ('JobMemoryLimit', size_t),
                    ('PeakProcessMemoryUsed', size_t), ('PeakJobMemoryUsed', size_t)]

    class Accounting(ctypes.Structure):
        _fields_ = [(name, ctypes.c_longlong) for name in (
            'TotalUserTime', 'TotalKernelTime', 'ThisPeriodTotalUserTime', 'ThisPeriodTotalKernelTime')]
        _fields_ += [(name, w.DWORD) for name in (
            'TotalPageFaultCount', 'TotalProcesses', 'ActiveProcesses', 'TotalTerminatedProcesses')]

    signatures = {
        'CreateJobObjectW': ([ctypes.c_void_p, w.LPCWSTR], w.HANDLE),
        'SetInformationJobObject': ([w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD], w.BOOL),
        'AssignProcessToJobObject': ([w.HANDLE, w.HANDLE], w.BOOL),
        'QueryInformationJobObject': ([w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD, ctypes.c_void_p], w.BOOL),
        'TerminateJobObject': ([w.HANDLE, w.UINT], w.BOOL),
        'TerminateProcess': ([w.HANDLE, w.UINT], w.BOOL),
        'ResumeThread': ([w.HANDLE], w.DWORD),
        'WaitForSingleObject': ([w.HANDLE, w.DWORD], w.DWORD),
        'GetExitCodeProcess': ([w.HANDLE, ctypes.POINTER(w.DWORD)], w.BOOL),
        'CloseHandle': ([w.HANDLE], w.BOOL),
        'CreateProcessW': ([w.LPCWSTR, w.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, w.BOOL,
                            w.DWORD, ctypes.c_void_p, w.LPCWSTR, ctypes.POINTER(Startup),
                            ctypes.POINTER(ProcessInfo)], w.BOOL)}
    for name, (args, returns) in signatures.items():
        fn = getattr(k, name); fn.argtypes = args; fn.restype = returns

    def checked(value):
        if not value:
            raise ctypes.WinError(ctypes.get_last_error())
        return value

    job = None; pi = ProcessInfo(); assigned = False
    stdout_path = Path(stdout_path); stderr_path = Path(stderr_path)
    stdout_path.parent.mkdir(parents=True, exist_ok=True)
    stderr_path.parent.mkdir(parents=True, exist_ok=True)
    import msvcrt
    with stdout_path.open('xb') as out, stderr_path.open('xb') as err, open(os.devnull, 'rb') as inp:
        handles = [msvcrt.get_osfhandle(f.fileno()) for f in (inp, out, err)]
        try:
            resolved = shutil.which(argv[0])
            if not resolved:
                raise FileNotFoundError(argv[0])
            result['executable'] = str(Path(resolved).resolve())
            job = checked(k.CreateJobObjectW(None, None))
            limits = ExtendedLimit(); limits.BasicLimitInformation.LimitFlags = 0x2000
            checked(k.SetInformationJobObject(job, 9, ctypes.byref(limits), ctypes.sizeof(limits)))
            si = Startup(); si.cb = ctypes.sizeof(si); si.dwFlags = 0x100
            si.hStdInput, si.hStdOutput, si.hStdError = handles
            for handle in handles:
                os.set_handle_inheritable(handle, True)
            line = ctypes.create_unicode_buffer(subprocess.list2cmdline([resolved, *argv[1:]]))
            checked(k.CreateProcessW(resolved, line, None, None, True, 0x08000004,
                                     None, str(cwd), ctypes.byref(si), ctypes.byref(pi)))
            result['pid'] = pi.dwProcessId
            checked(k.AssignProcessToJobObject(job, pi.hProcess)); assigned = True
            result['process_tree']['assigned_before_resume'] = True
            if k.ResumeThread(pi.hThread) == 0xffffffff:
                raise ctypes.WinError(ctypes.get_last_error())
            k.CloseHandle(pi.hThread); pi.hThread = None
            deadline = time.monotonic() + timeout
            root_done = False
            stats = Accounting()
            while True:
                wait = k.WaitForSingleObject(pi.hProcess, 0)
                if wait == 0xffffffff:
                    raise ctypes.WinError(ctypes.get_last_error())
                root_done = wait == 0
                checked(k.QueryInformationJobObject(job, 1, ctypes.byref(stats), ctypes.sizeof(stats), None))
                if root_done and stats.ActiveProcesses == 0:
                    result['status'] = 'Exited'
                    break
                if time.monotonic() >= deadline:
                    result['status'] = 'DescendantsAlive' if root_done else 'Timeout'
                    checked(k.TerminateJobObject(job, 124))
                    result['process_tree']['terminated_owned_job'] = True
                    drain_deadline = time.monotonic() + 5
                    while stats.ActiveProcesses and time.monotonic() < drain_deadline:
                        time.sleep(0.02)
                        checked(k.QueryInformationJobObject(job, 1, ctypes.byref(stats), ctypes.sizeof(stats), None))
                    break
                time.sleep(0.01)
            code = w.DWORD(); checked(k.GetExitCodeProcess(pi.hProcess, ctypes.byref(code)))
            result['observed_exit_code'] = code.value
            if result['status'] in ('Exited', 'DescendantsAlive'):
                result['exit_code'] = code.value
                if code.value >= 0xC0000000 and result['status'] == 'Exited':
                    result['status'] = 'Crashed'
            result['process_tree'].update(active_after=stats.ActiveProcesses,
                                           total_processes=stats.TotalProcesses)
            if stats.ActiveProcesses:
                result['status'] = 'UnreapedChildren'
        except (OSError, ValueError) as exc:
            result['error'] = str(exc)
            err.write((str(exc) + '\n').encode('utf-8')); err.flush()
            if pi.hProcess:
                if assigned:
                    k.TerminateJobObject(job, 125)
                    result['process_tree']['terminated_owned_job'] = True
                else:
                    k.TerminateProcess(pi.hProcess, 125)
                k.WaitForSingleObject(pi.hProcess, 5000)
            result['status'] = 'LaunchFailed'
        finally:
            for handle in handles:
                os.set_handle_inheritable(handle, False)
            if pi.hThread:
                k.CloseHandle(pi.hThread)
            if pi.hProcess:
                k.CloseHandle(pi.hProcess)
            if job:
                k.CloseHandle(job)
    result['finished_at'] = now()
    return result

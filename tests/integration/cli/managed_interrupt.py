"""真实 Console CTRL_C_EVENT，分别验证观察中断与显式取消。"""
import ctypes
import json
import queue
import subprocess
import sys
import threading
import time
import uuid

service, cli = sys.argv[1:3]
instance = 'managed-interrupt-' + uuid.uuid4().hex
server = subprocess.Popen([service, '--instance', instance], stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=subprocess.CREATE_NO_WINDOW)
children = []
k32 = ctypes.WinDLL('kernel32', use_last_error=True)

def call(arguments):
    process = subprocess.Popen([cli, '--instance', instance, '--json', *arguments], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    children.append(process)
    output, error = process.communicate(timeout=15)
    print('CLI', process.pid, arguments[:2], process.returncode, flush=True)
    assert process.returncode == 0, (process.pid, output, error)
    return json.loads(output)['result']

try:
    assert json.loads(server.stdout.readline()) == {'ready': True}
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    for cancel in (False, True):
        accepted = call(['submit', 'sample.compute', '--args', json.dumps(
            {'amount': 7, 'delay_ms': 10000 if cancel else 5000, 'text': 'console interrupt'})])
        ref = accepted['execution_ref']['execution_id']
        arguments = [cli, '--instance', instance, '--timeout-ms', '10000', 'execution', 'watch', ref, '--jsonl']
        if cancel:
            arguments.append('--cancel-on-interrupt')
        watcher = subprocess.Popen(arguments, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup)
        children.append(watcher)
        ready = queue.Queue(maxsize=1)
        threading.Thread(target=lambda: ready.put(watcher.stdout.readline()), daemon=True).start()
        first = json.loads(ready.get(timeout=3))
        assert first['result']['phase'] != 'Terminal'
        print('WATCH first snapshot', watcher.pid, first, 'cancel_on_interrupt', cancel, flush=True)
        for _ in range(10):
            running = call(['execution', 'get', ref])
            if running['phase'] == 'Running':
                break
            time.sleep(0.02)
        assert running['phase'] == 'Running'
        k32.FreeConsole()
        assert k32.AttachConsole(watcher.pid), ctypes.get_last_error()
        assert k32.SetConsoleCtrlHandler(None, True)
        assert k32.GenerateConsoleCtrlEvent(0, 0), ctypes.get_last_error()
        watcher.wait(timeout=5)
        k32.FreeConsole()
        output, error = watcher.communicate(timeout=3)
        print('WATCH interrupted', watcher.pid, watcher.returncode, output.decode(), error.decode(), flush=True)
        assert watcher.returncode == 8, (watcher.returncode, output, error)
        rows = [json.loads(line) for line in output.splitlines()]
        assert rows[-1]['error']['kind'] == 'Interrupted'
        dispositions = [row['result']['disposition'] for row in rows if 'disposition' in row.get('result', {})]
        assert dispositions == (['AlreadyClaimed'] if cancel else [])
        waited = call(['execution', 'wait', ref, '--wait-timeout-ms', '7000', '--timeout-ms', '9000'])
        assert waited['wait_state'] == 'Terminal'
        final = call(['result', 'read', ref])['reply']['outcome']['result']
        assert final == {'amount': 8, 'text': 'console interrupt', 'stop_observed': cancel}
    print('real managed CTRL_C_EVENT: default preserves execution; explicit flag requests cooperative cancellation', flush=True)
finally:
    k32.FreeConsole()
    for process in children:
        if process.poll() is None:
            process.kill()
            process.communicate()
    if server.poll() is None:
        try:
            _, error = server.communicate(b'stop\n', timeout=10)
            if error:
                print(error.decode(errors='replace'), file=sys.stderr)
        except subprocess.TimeoutExpired:
            server.kill()
            server.communicate()
    assert server.returncode == 0, server.returncode

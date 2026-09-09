import ctypes
import json
import subprocess
import sys
import time
import uuid

service, cli = sys.argv[1:3]
instance = 'interrupt-' + uuid.uuid4().hex
server = subprocess.Popen([service, '--instance', instance], stdin=subprocess.PIPE,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          creationflags=subprocess.CREATE_NO_WINDOW)
client = None
waiting = None
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
try:
    assert json.loads(server.stdout.readline()) == {'ready': True}
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    client = subprocess.Popen([cli, '--instance', instance, 'invoke', 'sample.increment', '--stdin'],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              creationflags=subprocess.CREATE_NEW_CONSOLE, startupinfo=startup)
    # Keep stdin open without EOF: interrupt must also release a waiting input reader.
    time.sleep(0.7)
    assert client.poll() is None
    k32.FreeConsole()
    assert k32.AttachConsole(client.pid), ctypes.get_last_error()
    assert k32.SetConsoleCtrlHandler(None, True)
    assert k32.GenerateConsoleCtrlEvent(0, 0), ctypes.get_last_error()
    client.wait(timeout=5)
    k32.FreeConsole()
    out, err = client.communicate()
    assert client.returncode == 8, (client.returncode, out, err)
    assert json.loads(out)['error']['kind'] == 'Interrupted'
    assert server.poll() is None
    check = subprocess.run([cli, '--instance', instance, 'capabilities', 'search', '--prefix', ''],
                           capture_output=True, timeout=10)
    assert check.returncode == 0, (check.stdout, check.stderr)
    assert json.loads(check.stdout)['result']['items'][0]['name'] == 'sample.increment'
    print('real CTRL_C_EVENT interrupts open stdin; Host remains usable; empty argv preserved')
    waiting_instance='wait-'+uuid.uuid4().hex
    waiting=subprocess.Popen([sys.argv[3],'wait-server',waiting_instance],stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                             creationflags=subprocess.CREATE_NO_WINDOW)
    assert waiting.stdout.readline().strip()==b'READY'
    client=subprocess.Popen([cli,'--instance',waiting_instance,'--timeout-ms','20000','capabilities','search'],
                            stdout=subprocess.PIPE,stderr=subprocess.PIPE,creationflags=subprocess.CREATE_NEW_CONSOLE,startupinfo=startup)
    assert waiting.stdout.readline().strip()==b'REQUEST'
    assert k32.AttachConsole(client.pid),ctypes.get_last_error()
    assert k32.SetConsoleCtrlHandler(None,True)
    assert k32.GenerateConsoleCtrlEvent(0,0),ctypes.get_last_error()
    client.wait(timeout=5)
    k32.FreeConsole()
    out,err=client.communicate()
    assert client.returncode==8,(client.returncode,out,err)
    assert json.loads(out)['error']['kind']=='Interrupted'
    waiting.wait(timeout=5)
    assert waiting.returncode==0,waiting.communicate()
    print('real CTRL_C_EVENT interrupts pending reply; peer received only hello, no implicit cancel')
finally:
    k32.FreeConsole()
    if client and client.poll() is None:
        client.kill()
        client.communicate()
    if waiting and waiting.poll() is None:
        waiting.kill()
        waiting.communicate()
    if server.poll() is None:
        try:
            server.communicate(b'stop\n', timeout=10)
        except subprocess.TimeoutExpired:
            server.kill()
            server.communicate()
    assert server.returncode == 0, server.returncode

import subprocess
import sys
import uuid

binary = sys.argv[1]
for server_mode, client_mode, expected in [
    ("server", "client", 0),
    ("server", "partial-client", 0),
    ("first-byte-server", "partial-client", 0),
    ("server", "wrong-server", 0),
    ("deny-server", "client", 8),
]:
    instance = "test-" + uuid.uuid4().hex
    server = subprocess.Popen([binary, server_mode, instance], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
    try:
        assert server.stdout.readline().strip() == "READY", "server did not bind"
        client = subprocess.run([binary, client_mode, instance], capture_output=True, timeout=15)
        assert client.returncode == expected, (server_mode, client_mode, client.returncode, client.stdout, client.stderr)
        stdout, stderr = server.communicate(timeout=15)
        assert server.returncode == 0, (server.returncode, stdout, stderr)
        print(server_mode, client_mode, "passed")
    finally:
        if server.poll() is None:
            server.kill()
            server.communicate()

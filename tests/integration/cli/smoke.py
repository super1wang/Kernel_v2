import json
import pathlib
import subprocess
import sys
import tempfile
import uuid

service, cli = sys.argv[1:3]
instance = "cli-" + uuid.uuid4().hex
server = subprocess.Popen([service, "--instance", instance], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
def call(arguments, expected=0, stdin=None):
    result = subprocess.run([cli, "--instance", instance, "--json", *arguments], input=stdin, capture_output=True, text=True, encoding="utf-8", timeout=15)
    assert result.returncode == expected, (arguments, result.returncode, result.stdout, result.stderr)
    value = json.loads(result.stdout)
    print(arguments[0], "exit", result.returncode, "passed")
    return value
try:
    assert json.loads(server.stdout.readline()) == {"ready": True}
    with tempfile.TemporaryDirectory(prefix="ock-cli-") as directory:
        directory = pathlib.Path(directory)
        source = directory / "中文输入.json"
        intent = directory / "request.intent.json"
        args = {"amount": 4, "text": "中文 UTF-8"}
        source.write_text(json.dumps(args, ensure_ascii=False), encoding="utf-8")
        search = call(["capabilities", "search"])
        assert search["result"]["items"][0]["name"] == "sample.increment"
        card = call(["capabilities", "describe", "sample.increment"])
        assert card["result"]["eligible"] is True
        result = call(["invoke", "sample.increment", "--file", str(source), "--intent-file", str(intent)])
        assert result["result"]["outcome"]["result"] == {"amount": 5, "text": args["text"]}
        original = intent.read_bytes()
        call(["invoke", "sample.increment", "--stdin", "--intent-file", str(intent)], stdin=json.dumps(args))
        assert intent.read_bytes() == original
        args["amount"] = 5
        call(["invoke", "sample.increment", "--args", json.dumps(args), "--intent-file", str(intent)], 2)
        assert intent.read_bytes() == original
        call(["invoke", "sample.increment", "--args", '{"amount":-1,"text":"bad"}'], 3)
        call(["invoke", "sample.increment", "--args", '{"amount":100,"text":"bad output"}'], 4)
        call(["execution", "list"], 3)
        for command in ("get", "wait", "cancel"):
            call(["execution", command, "1" * 32], 3)
        call(["result", "read", "1" * 32], 3)
        call(["submit", "sample.increment", "--args", json.dumps(args)], 3)
        call(["execution", "watch", "unused", "--jsonl"], 3)
        call(["invoke", "sample.increment", "--stdin"], 2, stdin="{broken")
        assert server.poll() is None, "CLI exit destroyed Host"
        call(["capabilities", "search"])
finally:
    if server.poll() is None:
        try:
            server_output, server_error = server.communicate("stop\n", timeout=10)
            if server_error:
                print(server_error, file=sys.stderr)
        except subprocess.TimeoutExpired:
            server.kill()
            server.communicate()
    assert server.returncode == 0, server.returncode

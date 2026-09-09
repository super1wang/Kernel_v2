import json
import pathlib
import subprocess
import sys
import tempfile
import uuid

service,cli=sys.argv[1:3]
instance="parity-"+uuid.uuid4().hex
server=subprocess.Popen([service,"--instance",instance],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding="utf-8")
try:
    assert json.loads(server.stdout.readline())=={"ready":True}
    with tempfile.TemporaryDirectory(prefix="ock-parity-") as directory:
        source=pathlib.Path(directory)/"input.json"
        for amount,text,expected in [(4,"中文 exact result",0),(-1,"invalid input",3),(100,"invalid output",4),(1,"界"*257,3)]:
            source.write_text(json.dumps({"amount":amount,"text":text},ensure_ascii=False),encoding="utf-8")
            native=subprocess.run([service,"--native",str(source)],capture_output=True,text=True,encoding="utf-8",timeout=15)
            remote=subprocess.run([cli,"--instance",instance,"invoke","sample.increment","--file",str(source),"--json"],capture_output=True,text=True,encoding="utf-8",timeout=15)
            assert native.returncode==remote.returncode==expected,(native.returncode,remote.returncode,native.stderr,remote.stderr)
            left=json.loads(native.stdout);right=json.loads(remote.stdout)["result"]
            assert left==right,(left,right)
            print("Native / JSON / CLI exact reply parity",amount,len(text),expected)
finally:
    if server.poll() is None:
        try:server.communicate("stop\n",timeout=10)
        except subprocess.TimeoutExpired:server.kill();server.communicate()
    assert server.returncode==0,server.returncode

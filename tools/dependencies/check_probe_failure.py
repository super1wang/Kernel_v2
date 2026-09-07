"""实跑故障程序，只有指定诊断及真实非零退出同时出现才证明探针有效。"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import uuid
p=argparse.ArgumentParser();p.add_argument("--mode",choices=["catch2","asan"],required=True);p.add_argument("argv",nargs=argparse.REMAINDER);a=p.parse_args();argv=a.argv[1:] if a.argv[:1]==["--"] else a.argv
run=Path.cwd()/("fault-"+uuid.uuid4().hex);run.mkdir(exist_ok=False)
start=datetime.now(timezone.utc).isoformat();result=subprocess.run(argv,capture_output=True,timeout=30)
for name,data in (("stdout",result.stdout),("stderr",result.stderr)):(run/(name+".log")).write_bytes(data)
report=dict(argv=argv,cwd=str(Path.cwd()),started_at=start,finished_at=datetime.now(timezone.utc).isoformat(),exit_code=result.returncode,raw=[dict(path=n+".log",size=len(d),sha256=hashlib.sha256(d).hexdigest()) for n,d in (("stdout",result.stdout),("stderr",result.stderr))]);(run/"process.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
sys.stdout.buffer.write(result.stdout);sys.stderr.buffer.write(result.stderr);print("fault_probe_actual_exit_code=",result.returncode,"raw=",run)
text=(result.stdout+result.stderr).decode("utf-8",errors="replace")
matched=("AddressSanitizer: heap-buffer-overflow" in text) if a.mode=="asan" else ("test cases:" in text and "1 failed" in text and "REQUIRE( false )" in text)
raise SystemExit(0 if result.returncode!=0 and matched else 1)

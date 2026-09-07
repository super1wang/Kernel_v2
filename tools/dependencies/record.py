"""D0.06-a 候选探针的原始进程记录；不是 D0.06-b 正式采集器。"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import uuid
ROOT = Path(__file__).resolve().parents[2]
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def now(): return datetime.now(timezone.utc).isoformat()
def main():
    parser=argparse.ArgumentParser();parser.add_argument("--label",required=True);parser.add_argument("--timeout",type=int,default=900);parser.add_argument("argv",nargs=argparse.REMAINDER);a=parser.parse_args()
    argv=a.argv[1:] if a.argv[:1]==["--"] else a.argv
    if not argv: parser.error("需要真实 argv")
    if argv[0]=="python":argv=[sys.executable,"-X","utf8",*argv[1:]]
    run=datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")+"-"+uuid.uuid4().hex[:8]
    out=ROOT/"evidence/bootstrap/D0.06-a"/run;out.mkdir(parents=True,exist_ok=False)
    paths=[ROOT/p for p in ("CMakePresets.json","dependencies.lock","THIRD_PARTY_NOTICES","cmake/Dependencies.cmake","cmake/DependencyProbes.cmake","docs/toolchain.md")]
    for p in ("tools/dependencies","tests/dependencies"):paths.extend(f for f in (ROOT/p).rglob("*") if f.is_file() and "__pycache__" not in f.parts)
    inputs=[dict(path=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for p in sorted(paths) if p.is_file()]
    meta=dict(task_id="D0.06-a",run_id=run,label=a.label,argv=argv,cwd=str(ROOT),started_at=now(),source_commit=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip(),source_dirty=bool(subprocess.check_output(["git","status","--porcelain"],cwd=ROOT)),inputs=inputs,python=platform.python_version(),limitations=["仅本 checkpoint 原始进程事实，不产包级 Passed；正式源/二进制/CTest关联由D0.06-b采集。"])
    try:
        p=subprocess.run(argv,cwd=ROOT,capture_output=True,timeout=a.timeout)
        stdout,stderr=p.stdout,p.stderr;meta.update(exit_code=p.returncode,process_status="Exited")
    except subprocess.TimeoutExpired as e:stdout,stderr=e.stdout or b"",e.stderr or b"";meta.update(exit_code=None,process_status="Timeout",timeout_seconds=a.timeout)
    except OSError as e:stdout,stderr=b"",str(e).encode();meta.update(exit_code=None,process_status="LaunchFailed")
    meta["finished_at"]=now();meta["raw"]=[]
    for name,data in (("stdout",stdout),("stderr",stderr)):
        file=out/(name+".log");file.write_bytes(data);meta["raw"].append(dict(path=file.name,size=len(data),sha256=sha(file),truncated=False))
    meta["inputs_changed_during_run"]=[i["path"] for i in inputs if not (ROOT/i["path"]).is_file() or sha(ROOT/i["path"])!=i["sha256"]]
    (out/"commands.json").write_text(json.dumps(meta,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(out.relative_to(ROOT).as_posix());print("actual_exit_code="+str(meta["exit_code"]));print(stdout.decode("utf-8",errors="replace"));print(stderr.decode("utf-8",errors="replace"),file=sys.stderr)
    return meta["exit_code"] if meta["exit_code"] is not None else 125
if __name__=="__main__":raise SystemExit(main())

"""G0 前原始命令记录器，不是 D0.06 正式证据采集器或门禁生成器。"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[2]


def now():
    return datetime.now(timezone.utc).isoformat()


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True, encoding="utf-8").strip()


def main():
    source_commit = git("rev-parse", "HEAD")
    source_dirty = bool(git("status", "--porcelain"))
    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ") + "-" + uuid.uuid4().hex[:12]
    out = ROOT / "evidence" / "bootstrap" / "D0.01" / run_id
    out.mkdir(parents=True, exist_ok=False)
    paths = []
    for name in ("docs", "tests", "tools"):
        paths.extend(p for p in (ROOT / name).rglob("*") if p.is_file()
                     and "__pycache__" not in p.parts and p.suffix not in (".pyc", ".pyo"))
    paths.extend(ROOT / p for p in ("README.md", "AGENTS.md", ".gitignore", ".gitattributes"))
    inputs = [{"path": p.relative_to(ROOT).as_posix(), "sha256": digest(p)}
              for p in sorted(paths) if p.is_file()]
    canonical = json.dumps(inputs, sort_keys=True, separators=(",", ":")).encode("utf-8")
    (out / "source-inputs.json").write_text(json.dumps(inputs, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    commands = []
    checks = json.loads((ROOT / "tests/manifests/expected.json").read_text(encoding="utf-8"))["checks"]
    result = {"format": "ock.bootstrap-commands/1", "task_id": "D0.01", "run_id": run_id,
              "started_at": now(), "source": {"commit": source_commit,
              "dirty": source_dirty, "inputs_sha256": hashlib.sha256(canonical).hexdigest()},
              "environment": {"python": platform.python_version(), "python_executable": sys.executable,
                              "platform": platform.platform()},
              "profile": "bootstrap-python", "build_status": "NotRun",
              "build_reason": "D0.01 无 C++ 工程；MSVC、依赖 probe 和正式采集在 D0.02/D0.06 交付。",
              "formal_evidence_status": "Incomplete", "commands": commands,
              "limitations": ["仅保存本轮原始命令事实，不提供 CTest/JUnit/二进制验证或包级状态。",
                              "G0 前必须由 D0.06 正式采集器核对导入，不能将此记录视为 D0.06 Passed。",
                              "人工评审独立保留，未签核不得将 D0.01 标记 Passed。"]}
    for index, check in enumerate(checks, 1):
        if check["kind"] != "command" or check["argv"][0] != "python":
            raise ValueError("bootstrap 只执行清单中的 Python 命令检查")
        argv = [sys.executable, "-X", "utf8", *check["argv"][1:]]
        command = {"check_id": check["id"], "argv": argv, "cwd": str(ROOT), "started_at": now()}
        try:
            completed = subprocess.run(argv, cwd=ROOT, capture_output=True, timeout=120)
            stdout, stderr = completed.stdout, completed.stderr
            command.update(exit_code=completed.returncode, process_status="Exited")
        except subprocess.TimeoutExpired as exc:
            stdout, stderr = exc.stdout or b"", exc.stderr or b""
            command.update(exit_code=None, process_status="Timeout", timeout_seconds=120,
                           process_disposition="subprocess.run 已 kill 并 wait 直接子进程；没有核验后代进程，记录不作通过")
        except OSError as exc:
            stdout, stderr = b"", str(exc).encode("utf-8")
            command.update(exit_code=None, process_status="LaunchFailed")
        command["finished_at"] = now()
        command["raw"] = []
        for stream, data in (("stdout", stdout), ("stderr", stderr)):
            file = out / f"{index:02}-{stream}.log"
            file.write_bytes(data)
            command["raw"].append({"path": file.name, "sha256": digest(file), "size": len(data), "truncated": False})
        commands.append(command)
    changed_inputs = [entry["path"] for entry in inputs if not (ROOT / entry["path"]).is_file()
                      or digest(ROOT / entry["path"]) != entry["sha256"]]
    result["inputs_changed_during_run"] = changed_inputs
    result["finished_at"] = now()
    all_ok = not changed_inputs and all(c.get("exit_code") == 0 and c["process_status"] == "Exited" for c in commands)
    result["collection_status"] = "Captured" if all_ok else "CommandFailedOrInputsChanged"
    (out / "commands.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(out.relative_to(ROOT).as_posix())
    for command in commands:
        print(f"{command['check_id']}: {command['process_status']}, exit_code={command['exit_code']}")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())

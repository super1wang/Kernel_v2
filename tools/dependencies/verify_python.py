"""D0.06-a 开发 wheel 锁与实际导入材料核验；不安装、不进入 Runtime。"""
import argparse
from copy import deepcopy
from email.parser import BytesParser
import hashlib
import importlib
import importlib.metadata
import io
import json
from pathlib import Path, PurePosixPath
import platform
import re
import sys
import tempfile
import unittest
import zipfile

ROOT=Path(__file__).resolve().parents[2]
VERSIONS={"jsonschema":"4.23.0","attrs":"26.1.0","jsonschema-specifications":"2025.9.1","referencing":"0.37.0","rpds-py":"2026.6.3","typing-extensions":"4.16.0"}
IMPORTS={"jsonschema":["jsonschema"],"attrs":["attr","attrs"],"jsonschema-specifications":["jsonschema_specifications"],"referencing":["referencing"],"rpds-py":["rpds"],"typing-extensions":["typing_extensions"]}
class LockError(ValueError):pass

def digest(data): return hashlib.sha256(data).hexdigest()
def require(value,reason):
    if not value: raise LockError(reason)
def normalize(name): return re.sub(r"[-_.]+","-",name).lower()

def verified_blob(blob,expected):
    require(re.fullmatch(r"[0-9a-f]{64}",expected) and digest(blob)==expected,"wheel SHA-256 differs from lock")

def wheel_payload(blob,name,version):
    require(len(blob)<=20*1024*1024,"wheel size budget exceeded")
    result={};seen=set();total=0
    with zipfile.ZipFile(io.BytesIO(blob)) as wheel:
        for item in wheel.infolist():
            if item.is_dir():continue
            parts=PurePosixPath(item.filename).parts
            require(parts and not item.filename.startswith(("/","\\")) and ".." not in parts and not any(":" in p or "\\" in p for p in parts),"unsafe wheel path")
            require(item.filename.casefold() not in seen,"duplicate or case-colliding wheel path");seen.add(item.filename.casefold())
            require((item.external_attr>>16)&0o170000!=0o120000,"wheel symlink forbidden")
            total+=item.file_size;require(total<=64*1024*1024,"expanded wheel budget exceeded")
            require(not any(part.endswith(".data") for part in parts),"new wheel relocation layout needs review")
            result[item.filename]=wheel.read(item)
    metadata=[p for p in result if p.endswith(".dist-info/METADATA")]
    require(len(metadata)==1,"wheel must have exactly one distribution METADATA")
    message=BytesParser().parsebytes(result[metadata[0]])
    require(normalize(message.get("Name",""))==normalize(name) and message.get("Version")==version,"wheel name/version differs from lock")
    # pip rewrites RECORD and adds INSTALLER/REQUESTED/console launchers; those are inventoried separately.
    return {p:b for p,b in result.items() if not p.endswith(".dist-info/RECORD")}

def verify_installed_files(payload,installed):
    installed=installed.resolve();rows=[]
    for name,data in sorted(payload.items()):
        path=(installed/name).resolve()
        require(path.is_relative_to(installed) and path.is_file(),"missing or outside installed file: "+name)
        actual=path.read_bytes();require(actual==data,"installed distribution file differs from wheel: "+name)
        rows.append({"path":name,"size":len(actual),"sha256":digest(actual)})
    return rows

def validate_requirements(lock,requirements):
    require(digest(requirements)==lock.get("requirements_sha256"),"requirements file fingerprint changed")
    parsed={}
    for line in requirements.decode("utf-8").splitlines():
        line=line.strip()
        if not line or line.startswith("#"):continue
        match=re.fullmatch(r"([A-Za-z0-9_.-]+)==([0-9.]+)",line)
        require(match is not None,"requirements must contain only exact reviewed versions")
        name=normalize(match[1]);require(name not in parsed,"duplicate requirement");parsed[name]=match[2]
    require(parsed==VERSIONS,"validation requirement versions changed")
    require({n:d.get("version") for n,d in lock.get("packages",{}).items()}==VERSIONS,"wheel lock package versions changed")

def verify(lock_path,wheels,installed):
    lock=json.loads(lock_path.read_text(encoding="utf-8"));require(lock.get("format")=="ock.python-development-lock/1","invalid development lock")
    require(platform.python_implementation()=="CPython" and platform.python_version()=="3.11.9" and sys.maxsize>2**32 and sys.platform=="win32","unreviewed Python platform")
    validate_requirements(lock,(ROOT/"tests/model/requirements-validation.txt").read_bytes())
    require({p.name for p in wheels.glob("*.whl")}=={d["wheel_filename"] for d in lock["packages"].values()},"wheel cache contains missing or unexpected versions")
    distributions={}
    for dist in importlib.metadata.distributions(path=[str(installed)]):
        name=normalize(dist.metadata["Name"]);require(name not in distributions,"duplicate installed distribution");distributions[name]=dist
    require(set(distributions)==set(VERSIONS),"isolated installation has missing or extra distributions")
    report={"scope":"D0.06 development validation only; no Runtime dependency", "python":platform.python_version(),"python_executable":sys.executable,"lock_sha256":digest(lock_path.read_bytes()),"packages":{}}
    sys.dont_write_bytecode=True
    sys.path.insert(0,str(installed.resolve()))
    for name,row in lock["packages"].items():
        require(row["pypi_json_url"]==f"https://pypi.org/pypi/{name}/{row['version']}/json","metadata source is not fixed official PyPI")
        metadata_path=(ROOT/row["pypi_json_file"]).resolve();require(metadata_path.is_relative_to((ROOT/"evidence/bootstrap/D0.06-a/python-lock").resolve()),"metadata evidence outside checkpoint")
        metadata_blob=metadata_path.read_bytes();require(digest(metadata_blob)==row["pypi_json_sha256"],"official metadata evidence changed")
        metadata=json.loads(metadata_blob);matches=[f for f in metadata["urls"] if f["filename"]==row["wheel_filename"]]
        require(len(matches)==1 and not matches[0]["yanked"] and matches[0]["packagetype"]=="bdist_wheel","wheel absent or withdrawn in captured PyPI metadata")
        require(matches[0]["digests"]["sha256"]==row["wheel_sha256"] and matches[0]["url"]==row["source_url"] and row["source_url"].startswith("https://files.pythonhosted.org/packages/"),"PyPI wheel URL/hash differs from lock")
        require(Path(row["wheel_filename"]).name==row["wheel_filename"],"unsafe wheel filename")
        blob=(wheels/row["wheel_filename"]).read_bytes();verified_blob(blob,row["wheel_sha256"])
        payload=wheel_payload(blob,name,row["version"]);files=verify_installed_files(payload,installed)
        require(distributions[name].version==row["version"],"installed distribution version differs")
        for license in row["license_files"]:require(license["path"] in payload and digest(payload[license["path"]])==license["sha256"],"license file differs")
        modules=[]
        for module_name in IMPORTS[name]:
            module=importlib.import_module(module_name);source=Path(module.__file__).resolve()
            require(source.is_relative_to(installed.resolve()),"module imported from outside isolated development installation")
            relative=source.relative_to(installed.resolve()).as_posix();require(relative in payload,"imported file is outside locked wheel payload")
            modules.append({"module":module_name,"path":relative,"sha256":digest(source.read_bytes())})
        report["packages"][name]={"version":row["version"],"wheel_sha256":row["wheel_sha256"],"wheel_payload_files":files,"imported_modules":modules}
    # Inventory pip-created files too; wheel byte equality above is the trust check for shipped payload.
    generated=[]
    for path in sorted(installed.rglob("*")):
        if path.is_file() and "__pycache__" not in path.parts and path.suffix!=".pyc":
            require(path.resolve().is_relative_to(installed.resolve()),"installation contains outside link")
            generated.append({"path":path.relative_to(installed).as_posix(),"sha256":digest(path.read_bytes())})
    report["complete_installed_inventory"]=generated
    return report

class Contracts(unittest.TestCase):
    def wheel(self,entries=None):
        buffer=io.BytesIO()
        with zipfile.ZipFile(buffer,"w") as z:
            for name,data in (entries or [("demo/__init__.py",b"value=1\n"),("demo-1.dist-info/METADATA",b"Name: demo\nVersion: 1\n")]): z.writestr(name,data)
        return buffer.getvalue()
    def test_valid_wheel_and_install(self):
        blob=self.wheel();verified_blob(blob,digest(blob));payload=wheel_payload(blob,"demo","1")
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            for n,b in payload.items():p=root/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
            self.assertEqual(len(verify_installed_files(payload,root)),2)
    def test_wheel_hash_mismatch(self):
        with self.assertRaises(LockError):verified_blob(self.wheel(),"0"*64)
    def test_wheel_metadata_version_mismatch(self):
        with self.assertRaises(LockError):wheel_payload(self.wheel(),"demo","2")
    def test_wheel_path_escape(self):
        with self.assertRaises(LockError):wheel_payload(self.wheel([("../escape",b"x")]),"demo","1")
    def test_wheel_duplicate_path(self):
        with self.assertRaises(LockError):wheel_payload(self.wheel([("demo/x",b"a"),("demo/X",b"b")]),"demo","1")
    def test_installed_tamper(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);(root/"x.py").write_bytes(b"changed")
            with self.assertRaises(LockError):verify_installed_files({"x.py":b"locked"},root)
    def test_installed_missing(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(LockError):verify_installed_files({"x.py":b"locked"},Path(temp))
    def test_requirements_version_mismatch(self):
        requirement="\n".join(n+"=="+v for n,v in VERSIONS.items()).encode()
        lock={"requirements_sha256":digest(requirement),"packages":{n:{"version":v} for n,v in VERSIONS.items()}}
        validate_requirements(lock,requirement);lock["packages"]["jsonschema"]["version"]="4.26.0"
        with self.assertRaises(LockError):validate_requirements(lock,requirement)

def main():
    parser=argparse.ArgumentParser();parser.add_argument("--lock",type=Path,default=ROOT/"tools/dependencies/python-lock.json");parser.add_argument("--wheels",type=Path,default=ROOT/"build/python-wheel-cache");parser.add_argument("--installed",type=Path,default=ROOT/"build/python-deps");parser.add_argument("--output",type=Path);args=parser.parse_args()
    result=verify(args.lock,args.wheels,args.installed)
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        with args.output.open("x",encoding="utf-8",newline="\n") as file:json.dump(result,file,ensure_ascii=False,indent=2);file.write("\n")
    print(json.dumps({"python":result["python"],"packages":{n:{"version":d["version"],"verified_wheel_files":len(d["wheel_payload_files"]),"imported_modules":d["imported_modules"]} for n,d in result["packages"].items()},"complete_installed_files":len(result["complete_installed_inventory"])},ensure_ascii=False,indent=2));return 0

if __name__=="__main__":
    if "--self-test" in sys.argv:unittest.main(argv=[sys.argv[0]],verbosity=2)
    else:
        try:raise SystemExit(main())
        except (LockError,OSError,ValueError,zipfile.BadZipFile) as error:print(str(error),file=sys.stderr);raise SystemExit(1)

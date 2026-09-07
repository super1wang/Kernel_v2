"""按组件获取已锁定归档；离线缓存和源码均校验，禁止隐式 latest 或任意分支。"""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import re
import posixpath
import tarfile
from urllib.request import Request, urlopen
import zipfile

NAMES={"expected","jsoncons","thread_pool","asio","immer","spdlog","fmt","sqlite","toml11","cli11","catch2","benchmark"}
COMPONENTS={"Foundation":{"expected"},"Embedded":{"expected","thread_pool","spdlog","fmt"},
 "Data":{"expected","jsoncons"},"CpuPool":{"expected","thread_pool"},"LocalIPC":{"expected","jsoncons","asio"},
 "State":{"expected","immer"},"Logging":{"expected","spdlog","fmt"},"SQLite":{"expected","sqlite"},
 "Config":{"expected","jsoncons","toml11"},"CLI":{"expected","jsoncons","asio","cli11"},
 "Tests":{"catch2"},"Benchmarks":{"benchmark"},"All":NAMES}
SQLITE_DEFINITIONS={"SQLITE_THREADSAFE":"1","SQLITE_DEFAULT_WAL_SYNCHRONOUS":"2","SQLITE_DEFAULT_SYNCHRONOUS":"2","SQLITE_DQS":"0","SQLITE_OMIT_LOAD_EXTENSION":"1","SQLITE_ENABLE_API_ARMOR":"1"}
MAX_ARCHIVE=64*1024*1024
MAX_EXTRACTED=256*1024*1024
class LockError(ValueError):pass
def require(value,reason):
    if not value:raise LockError(reason)
def digest(data):return hashlib.sha256(data).hexdigest()
def reject_duplicates(items):
    result={}
    for key,value in items:
        require(key not in result,"duplicate JSON field: "+key);result[key]=value
    return result
def load_lock(path):
    lock=json.loads(Path(path).read_text(encoding="utf-8"),object_pairs_hook=reject_duplicates);validate_lock(lock);return lock
def validate_lock(lock):
    require(lock.get("format")=="ock.dependencies/1","invalid lock format")
    toolchain=lock.get("toolchain",{})
    for key,value in {"cxx_standard":20,"architecture":"x64","generator":"Visual Studio 17 2022","toolset":"v143,version=14.44.35207","msvc_runtime":"MultiThreaded$<$<CONFIG:Debug>:Debug>DLL","compiler":"MSVC","compiler_version":"19.44.35228.0","cmake_version":"3.31.6-msvc6","windows_sdk":"10.0.26100.0","exceptions":True}.items():
        require(toolchain.get(key)==value,"unreviewed toolchain: "+key)
    deps=lock.get("dependencies",{})
    require(set(deps)==NAMES,"default dependency table is incomplete or contains unknown entries")
    for name,d in deps.items():
        for field in ("version","commit","source_url","archive_sha256","tree_sha256","license","compile_definitions","cmake_options","archive_format"):
            require(field in d,"missing dependency field: "+name+"."+field)
        require(re.fullmatch(r"[0-9a-f]{64}",d["archive_sha256"]) is not None,"invalid archive SHA-256")
        require(re.fullmatch(r"[0-9a-f]{64}",d["tree_sha256"]) is not None,"invalid tree SHA-256")
        require(d["archive_format"] in ("tar.gz","zip"),"unsupported archive format")
        require(isinstance(d["cmake_options"],dict) and isinstance(d["compile_definitions"],dict),"compile options must be explicit objects")
        require(d["license"].get("spdx") and d["license"].get("path") and re.fullmatch(r"[0-9a-f]{64}",d["license"].get("sha256","")),"missing license identity/hash")
        if name=="sqlite":
            require(d["version"]=="3.51.3" and d["source_url"]=="https://www.sqlite.org/2026/sqlite-amalgamation-3510300.zip","unapproved SQLite version or source; WAL-reset floor and withdrawn 3.52.0 must not be bypassed")
            require(d["compile_definitions"]==SQLITE_DEFINITIONS,"SQLite compile options changed without reviewed lock policy")
            require(d["commit"]=="737ae4a34738ffa0c3ff7f9bb18df914dd1cad163f28fd6b6e114a344fe6d618","SQLite SOURCE_ID differs from fixed release")
        else:
            require(re.fullmatch(r"[0-9a-f]{40}",d["commit"]) is not None,"floating Git revision")
            require(d["source_url"]=="https://codeload.github.com/"+d.get("repository","")+"/tar.gz/"+d["commit"],"archive URL must bind the locked upstream commit")
    require(deps["asio"]["compile_definitions"].get("ASIO_STANDALONE")=="1","Asio must be standalone")
    require(deps["spdlog"]["cmake_options"].get("SPDLOG_FMT_EXTERNAL")=="ON","spdlog must use the single locked fmt")

def select_dependencies(lock,components):
    validate_lock(lock);result=set()
    for component in components:
        require(component in COMPONENTS,"unknown component: "+component);result.update(COMPONENTS[component])
    return result

def archive_tree(data,format):
    require(len(data)<=MAX_ARCHIVE,"archive budget exceeded")
    rows=[];links=[]
    if format=="tar.gz":
        with tarfile.open(fileobj=io.BytesIO(data),mode="r:gz") as archive:
            for item in archive.getmembers():
                require(item.isfile() or item.isdir() or item.issym(),"hard links and special archive entries are forbidden")
                if item.issym(): links.append((item.name,item.linkname))
                if item.isfile():
                    require(item.size<=MAX_EXTRACTED,"expanded member budget exceeded")
                    rows.append((item.name,archive.extractfile(item).read()))
    elif format=="zip":
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            for item in archive.infolist():
                require((item.external_attr>>16)&0o170000 != 0o120000,"symlink archive entry forbidden")
                if not item.is_dir():
                    require(item.file_size<=MAX_EXTRACTED,"expanded member budget exceeded")
                    rows.append((item.filename,archive.read(item)))
    else:raise LockError("unsupported archive format")
    # 上游 Immer 两个文档符号链接只物化为普通文件；不创建任何 OS 链接。
    regular=dict(rows)
    for name,target in links:
        resolved=posixpath.normpath(posixpath.join(posixpath.dirname(name),target))
        require(not target.startswith(("/","\\")) and resolved.split("/")[0]==name.split("/")[0] and resolved in regular,
                "symbolic link must resolve to an existing regular file inside the same archive root")
        rows.append((name,regular[resolved]))
    tree={};roots=set();size=0
    for name,content in rows:
        parts=PurePosixPath(name).parts
        require(len(parts)>=2 and not name.startswith(("/","\\")) and ".." not in parts and not any(":" in p or "\\" in p for p in parts),"unsafe archive path")
        roots.add(parts[0]);relative="/".join(parts[1:])
        require(relative.lower() not in {p.lower() for p in tree},"duplicate/case-colliding archive path")
        size+=len(content);require(size<=MAX_EXTRACTED,"expanded archive budget exceeded");tree[relative]=content
    require(len(roots)==1 and bool(tree),"archive must contain one nonempty source root")
    return tree

def tree_digest(tree):
    manifest=[{"path":name,"sha256":digest(data)} for name,data in sorted(tree.items())]
    return digest(json.dumps(manifest,sort_keys=True,separators=(",",":")).encode())

def download(url):
    with urlopen(Request(url,headers={"User-Agent":"OCK-D0.06-dependency-probe"}),timeout=60) as response:
        data=response.read(MAX_ARCHIVE+1)
    require(len(data)<=MAX_ARCHIVE,"download budget exceeded");return data

def materialize(lock,components,cache,offline=False):
    selected=select_dependencies(lock,components);report=dict(selected=sorted(selected),dependencies={})
    for name in sorted(selected):
        d=lock["dependencies"][name];archive=cache/"archives"/(d["archive_sha256"]+"."+d["archive_format"])
        source=cache/"sources"/(name+"-"+d["archive_sha256"][:12]);fetched=False
        if archive.is_file():data=archive.read_bytes()
        else:
            require(not offline,"offline archive missing: "+name)
            data=download(d["source_url"]);require(digest(data)==d["archive_sha256"],"download hash mismatch: "+name)
            archive.parent.mkdir(parents=True,exist_ok=True);archive.write_bytes(data);fetched=True
        require(digest(data)==d["archive_sha256"],"cached archive hash mismatch: "+name)
        tree=archive_tree(data,d["archive_format"]);require(tree_digest(tree)==d["tree_sha256"],"source tree does not match lock: "+name)
        require(d["license"]["path"] in tree and digest(tree[d["license"]["path"]])==d["license"]["sha256"],"license hash mismatch: "+name)
        if source.exists():
            actual={p.relative_to(source).as_posix():p.read_bytes() for p in source.rglob("*") if p.is_file()}
            require(tree_digest(actual)==d["tree_sha256"],"cached source tree modified: "+name)
        else:
            source.mkdir(parents=True,exist_ok=False)
            for path,content in tree.items():
                target=source/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(content)
        report["dependencies"][name]=dict(source=source.resolve().as_posix(),archive=archive.resolve().as_posix(),archive_sha256=d["archive_sha256"],tree_sha256=d["tree_sha256"],license_sha256=d["license"]["sha256"],downloaded=fetched)
    return report

def main():
    parser=argparse.ArgumentParser();parser.add_argument("--lock",required=True,type=Path);parser.add_argument("--components",default="Foundation");parser.add_argument("--cache",required=True,type=Path);parser.add_argument("--offline",action="store_true");parser.add_argument("--output",required=True,type=Path);args=parser.parse_args()
    report=materialize(load_lock(args.lock),[x for x in args.components.split(";") if x],args.cache,args.offline)
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    cmake=args.output.with_suffix(".cmake")
    lines=['set(OCK_SELECTED_DEPENDENCIES "'+";".join(report["selected"])+'")']
    for name,row in report["dependencies"].items():
        require('"' not in row["source"] and ";" not in row["source"],"unsupported CMake path delimiter")
        lines.append('set(OCK_DEP_'+name+'_SOURCE "'+row["source"]+'")')
    cmake.write_text("\n".join(lines)+"\n",encoding="utf-8")
    print(json.dumps(report,indent=2));return 0
if __name__=="__main__":
    try:raise SystemExit(main())
    except LockError as error:print(str(error));raise SystemExit(1)

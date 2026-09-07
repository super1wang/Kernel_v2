"""字节锁定的本地MSVC验证副本；损坏缓存拒绝使用，不静默修复。"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import uuid
from xml.sax.saxutils import escape
ROOT=Path(__file__).resolve().parents[2]


def resolved_path(path):
    """解析真实路径后统一Windows长路径表示；不改变symlink/UNC目标边界。"""
    resolved = path.resolve()
    if os.name == 'nt':
        text = str(resolved)
        if text.casefold().startswith('\\\\?\\unc\\'):
            return Path('\\\\' + text[8:])
        if text.startswith('\\\\?\\') and len(text) > 6 and text[4].isalpha() and text[5:7] == ':\\':
            return Path(text[4:])
    return resolved


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def records(rows):
    result={}
    for row in rows:
        name=row['path']; parts=PurePosixPath(name)
        if (not name or parts.is_absolute() or parts.as_posix()!=name or
            '..' in parts.parts or ':' in name or '\\' in name or name in result or
            not re.fullmatch('[0-9a-f]{64}',row['sha256'])):
            raise ValueError('invalid locked tool record')
        result[name]=row['sha256']
    return result


def verify_tree(directory, expected):
    directory=resolved_path(directory)
    actual={p.relative_to(directory).as_posix():p for p in directory.rglob('*') if p.is_file()}
    if set(actual)!=set(expected):raise ValueError('tool file set differs from lock')
    for name,p in actual.items():
        if not resolved_path(p).is_relative_to(directory) or sha(p)!=expected[name]:
            raise ValueError('tool bytes/path differ from lock: '+name)


def prepare(lock_path, workspace=ROOT, source_override=None):
    workspace=resolved_path(workspace); lock_path=resolved_path(lock_path)
    data=json.loads(lock_path.read_text(encoding='utf-8'))
    if data['format']!='ock.msvc-validation-tools/1':raise ValueError('wrong tool lock format')
    files=records(data['files']); excluded=records(data['excluded'])
    if set(excluded)!={'vctip.exe'} or set(files)&set(excluded):raise ValueError('unexpected excluded tool')
    if not {'cl.exe','c1.dll','c1xx.dll','c2.dll','link.exe','lib.exe'}<=set(files):
        raise ValueError('required compiler tools absent')
    source=resolved_path(Path(source_override or data['source_hint']))
    verify_tree(source, files|excluded)
    parent=workspace/'build/msvc-validation'
    if not resolved_path(parent).is_relative_to(workspace):raise ValueError('cache escaped workspace')
    parent.mkdir(parents=True,exist_ok=True)
    target=parent/sha(lock_path)[:16]
    binary=escape((target/'bin').as_posix()+'/')
    props='<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003"><PropertyGroup>\n'
    props+=''.join('<'+key+'>'+binary+'</'+key+'>\n' for key in ['CLToolPath','LinkToolPath','LIBToolPath'])
    props+='<ExecutablePath>'+binary+';$(ExecutablePath)</ExecutablePath>\n'
    props+='<NativeExecutablePath Condition="\'$(NativeExecutablePath)\' != \'\'">'+binary+';$(NativeExecutablePath)</NativeExecutablePath>\n'
    props+='</PropertyGroup></Project>\n'
    inputs={'format':'ock.msvc-validation-inputs/1','lock_sha256':sha(lock_path),'source':str(source),
            'copy':str(target/'bin'),'files':[{'path':name,'source_sha256':files[name],'copy_sha256':files[name]} for name in sorted(files)],
            'excluded':data['excluded'],'properties_sha256':hashlib.sha256(props.encode('utf-8')).hexdigest()}
    metadata={'local-tools.props':props.encode('utf-8'),
              'inputs.json':(json.dumps(inputs,ensure_ascii=False,indent=2)+'\n').encode('utf-8')}

    def verify_cache():
        if not resolved_path(target).is_relative_to(resolved_path(parent)):raise ValueError('cache target escaped workspace')
        expected_names={'bin/'+name for name in files}|set(metadata)
        actual_names=set()
        for cached in target.rglob('*'):
            if not resolved_path(cached).is_relative_to(resolved_path(target)):raise ValueError('cache member escaped target')
            if cached.is_file():actual_names.add(cached.relative_to(target).as_posix())
        if actual_names!=expected_names:raise ValueError('cache package file set differs from lock')
        verify_tree(target/'bin',files)
        for name,content in metadata.items():
            if (target/name).read_bytes()!=content:raise ValueError('cached metadata differs: '+name)

    if target.exists():
        verify_cache()
    else:
        stage=parent/('.partial-'+uuid.uuid4().hex);stage.mkdir()
        for name in files:
            dest=stage/'bin'/name;dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(source/name,dest)
        verify_tree(stage/'bin',files)
        for name,content in metadata.items():(stage/name).write_bytes(content)
        # bin与元数据一次发布；已有缓存全程只读，避免同卷hardlink外写。
        try:stage.rename(target)
        except FileExistsError:
            verify_cache()
            assert resolved_path(stage).is_relative_to(resolved_path(parent))
            shutil.rmtree(stage)
        verify_cache()
    return target


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--lock',type=Path,required=True);parser.add_argument('--source',type=Path)
    args=parser.parse_args();ready=prepare(args.lock,ROOT,args.source)
    print(json.dumps({'properties':(ready/'local-tools.props').as_posix(),'inputs':(ready/'inputs.json').as_posix()},ensure_ascii=False))

if __name__=='__main__':main()

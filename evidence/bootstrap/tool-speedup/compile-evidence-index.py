"""只读汇总与精选原始归档；不移动、不删除、不改写历史运行。"""
import json
from pathlib import Path
import sys
import zipfile
import xml.etree.ElementTree as ET

ROOT=Path(__file__).resolve().parents[3]
BASE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT))
from tools.evidence.common import save_json,sha_file,digest

FINAL='compile-ctest-a54cabb9c181'
BASELINE='compile-baseline-ee5f1a463a23'
FIXTURE='compile-fixture-60c7d4cd0af4'
CONTROLS='compile-controls-76275046d617'
RELEASE='compile-risk-release-off-cca18e07b8'
ASAN_RAW='compile-risk-debug-on-aca361896c'
ASAN='compile-risk-debug-on-63a76f56a0'
SOURCES=['tests/compile/contracts/'+x for x in ('CMakeLists.txt','discover.py','fixture.py','verify_children.py')]
SOURCES+=['tests/tools/compile/'+x for x in ('test_fixture.py','measure.py','controls.py')]


def load(path):return json.loads(path.read_text(encoding='utf-8'))


def command_rows(directory):
    result=[]
    for path in directory.rglob('commands.json'):
        row=load(path)
        result.extend(row if isinstance(row,list) else row['commands'])
    return result


def count(rows):
    configs=[x for x in rows if '-S' in x['argv'] and Path(x['argv'][0]).stem.lower()=='cmake']
    builds=[x for x in rows if '--build' in x['argv'] and Path(x['argv'][0]).stem.lower()=='cmake']
    return {'configure':len(configs),'positive_builds':sum(x['exit_code']==0 for x in builds),
            'negative_builds':sum(x['exit_code']!=0 for x in builds),
            'owned_commands':len(rows),'all_drained':all(x['process_tree']['active_after']==0 for x in rows),
            'none_required_job_kill':all(not x['process_tree']['terminated_owned_job'] for x in rows)}


def main():
    files=set(ROOT/x for x in SOURCES)
    # 包含所有本次compile轮次原始文本；排除编译对象、DLL、PDB、缓存等重复二进制。
    for directory in BASE.glob('compile-*'):
        if not directory.is_dir():continue
        for path in directory.rglob('*'):
            if path.is_file() and (path.suffix in ('.log','.json','.xml','.py','.cpp','.vcxproj')
                                  or path.name in ('CMakeLists.txt','CMakeCache.txt','CMakeCXXCompiler.cmake')
                                  or path.name.startswith('compile-profile-')):
                files.add(path)
    selected_child_dirs=[]
    fixtures=[]
    for run in (FINAL,RELEASE,ASAN_RAW):
        suite=ET.parse(BASE/run/'junit.xml').getroot()
        for case in suite.findall('testcase'):
            raw=case.findtext('system-out','').strip()
            if not raw.startswith('{'):continue
            row=json.loads(raw)
            if 'evidence' in row:
                directory=Path(row['evidence'])
                selected_child_dirs.append(directory)
                files.update(x for x in directory.iterdir() if x.is_file())
            if 'fixture' in row:
                directory=Path(row['fixture']);fixtures.append(directory)
                files.update(x for x in directory.iterdir() if x.is_file())
                files.update((directory/'build').glob('*.vcxproj'))
                files.add(directory/'build/CMakeCache.txt')
                files.update((directory/'build/CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
                # 原始编译命令行tlog为UTF-16，按原字节保留，只选实际执行target的command文件。
                files.update((directory/'build').glob('*.dir/*/*.tlog/CL.command.1.tlog'))
                contracts=directory.parents[2]
                files.update(contracts.glob('compile-profile-*.txt'))
                files.update(contracts.glob('contracts-tests*.cmake'))
                last=contracts/'Testing/Temporary/LastTest.log'
                if last.exists():files.add(last)
    ready=load(fixtures[0]/'ready.json')
    for row in ready['identity']['inputs']:
        path=Path(row['path'])
        if sha_file(path)!=row['sha256']:raise ValueError('最终输入在归档前漂移：'+str(path))
        if path.suffix.lower() not in ('.exe','.dll','.pdb','.lib'):
            files.add(path)
    final_rows=[]
    suite=ET.parse(BASE/FINAL/'junit.xml').getroot()
    for case in suite.findall('testcase'):
        raw=case.findtext('system-out','').strip()
        if raw.startswith('{'):
            row=json.loads(raw)
            if 'evidence' in row:final_rows+=command_rows(Path(row['evidence']))
    final_counts=count(final_rows)
    if (final_counts['configure'],final_counts['positive_builds'],final_counts['negative_builds'])!=(1,10,23):
        raise ValueError('最终实际编译数不符')
    baseline=load(BASE/BASELINE/'result.json');fixture=load(BASE/FIXTURE/'result.json')
    comparison={'baseline':{'path':BASELINE,'elapsed_seconds':baseline['elapsed_seconds'],**count(command_rows(BASE/BASELINE/'children'))},
                'fixture':{'path':FIXTURE,'elapsed_seconds':fixture['elapsed_seconds'],**count(command_rows(BASE/FIXTURE/'children'))},
                'wall_change_percent':100*(fixture['elapsed_seconds']/baseline['elapsed_seconds']-1),
                'verdict':'配置次数减少已证实；最终单次墙钟更慢，稳定提速未证实。'}
    summary={'scope':'P0 CompileContracts开发支持验证；不代表D1.06包Passed',
             'sources':[{'path':x,'sha256':sha_file(ROOT/x)} for x in SOURCES],
             'debug':{'path':FINAL,'ctest_passed':13,'ctest_expected':13,'counts':final_counts},
             'release':{'path':RELEASE,'ctest_passed':2,'profile':load(BASE/RELEASE/'result.json')['profile']},
             'asan':{'raw_path':ASAN_RAW,'verification_path':ASAN,'ctest_passed':2,
                     'profile':load(BASE/ASAN/'result.json')['profile'],
                     'reader_correction':'原XML读器误用EnableASAN，实际为EnableAsan；原测试2/2，后续只读核验不重跑编译。'},
             'controls':{'path':CONTROLS,'checks':load(BASE/CONTROLS/'checks.json')},
             'comparison':comparison,
             'limitations':['只做单次最终串行双包装性能对照，非全Profile统计或完整矩阵收益。',
                            '初次预览71.82→61.63秒，最终58.21→65.69秒，方向相反；不选择有利轮次。',
                            '没有修改历史expected、生产Host/Logging或包状态；独立AI结论由主任务维护。']}
    summary['source_digest']=digest(summary['sources'])
    save_json(BASE/'compile-summary.json',summary)
    files.add(BASE/'compile-summary.json')
    files.update([Path(__file__),BASE/'compile-risk-driver.py'])
    entries=[]
    archive=BASE/'compile-raw-text.zip'
    with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as stream:
        for path in sorted(files):
            relative=path.relative_to(ROOT).as_posix()
            entries.append({'path':relative,'sha256':sha_file(path),'size':path.stat().st_size})
            stream.write(path,relative)
    with zipfile.ZipFile(archive) as stream:
        from tools.evidence.common import sha_bytes
        for row in entries:
            data=stream.read(row['path'])
            if len(data)!=row['size'] or sha_bytes(data)!=row['sha256']:raise ValueError('归档读回不符')
    save_json(BASE/'compile-archive-index.json',{'archive':archive.name,'sha256':sha_file(archive),
                'size':archive.stat().st_size,'entries':entries,'policy':'精选原始文本逐字节保存；未移动/删除旧目录，排除重复obj/DLL/PDB/cache。'})
    print(json.dumps({'summary':str(BASE/'compile-summary.json'),'archive_entries':len(entries),
                      'archive_size':archive.stat().st_size,'source_digest':summary['source_digest']},ensure_ascii=False))


if __name__=='__main__':main()

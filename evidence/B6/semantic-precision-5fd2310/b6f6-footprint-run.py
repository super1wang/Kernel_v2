import os, sys, json, subprocess
from pathlib import Path
from datetime import datetime, timezone

R = Path(__file__).resolve().parents[1]
os.chdir(R)
O = R / 'evidence/B6/semantic-precision-5fd2310'
os.environ['PATH'] = str(R / 'build/runtime/python311') + os.pathsep + \
    'E:/vs2022IDE/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin' + os.pathsep + os.environ['PATH']
os.environ['PYTHONPATH'] = str(R / 'build/python-deps') + os.pathsep + str(R)
os.environ['MSBUILDDISABLENODEREUSE'] = '1'


def save(p, v):
    p.write_text(json.dumps(v, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


old = json.loads((O / 'previous-budgets.json').read_text())
new = json.loads((R / 'footprint-budgets.json').read_text())
for section in (None, 'embedded'):
    a = old if section is None else old[section]
    b = new if section is None else new[section]
    assert a['budgets'].keys() == b['budgets'].keys()
    for k in a['budgets']:
        assert {x: v for x, v in a['budgets'][k].items() if x not in ('method_digest', 'approved_at')} == \
               {x: v for x, v in b['budgets'][k].items() if x not in ('method_digest', 'approved_at')}
jobs = []
for kind in ('native', 'embedded'):
    for profile in ('debug', 'release', 'asan'):
        for mode in (['occupancy', 'allocation'] + (['latency'] if profile == 'release' else [])) if kind == 'native' else ['combined']:
            jobs.append({'kind': kind, 'profile': 'win-msvc-' + profile, 'mode': mode})
assert len(jobs) == 10
save(O / 'expected.json', {'production_commit': '5fd2310aec5f8a24ac14b948677930e2a7ced7e3', 'blocks': 6, 'jobs': jobs,
                           'budget_limits_and_pilots_unchanged': True})
commands = []
reports = []
if (O / 'commands.json').is_file():
    commands = json.loads((O / 'commands.json').read_text(encoding='utf-8'))
    reports = json.loads((O / 'reports.json').read_text(encoding='utf-8'))
done = {(r['kind'], r['profile'], r['mode']) for r in reports}


def run(label, argv):
    print('START ' + label, flush=True)
    start = datetime.now(timezone.utc).isoformat()
    with (O / (label + '.log')).open('wb') as f:
        p = subprocess.run(argv, stdout=f, stderr=subprocess.STDOUT, timeout=1800)
    row = {'label': label, 'argv': argv, 'started_at': start,
           'completed_at': datetime.now(timezone.utc).isoformat(),
           'exit_code': p.returncode, 'log': str(O / (label + '.log'))}
    commands.append(row)
    save(O / 'commands.json', commands)
    print('DONE ' + label + ' exit=' + str(p.returncode), flush=True)
    if p.returncode:
        print((O / (label + '.log')).read_text(encoding='utf-8', errors='replace')[-5000:], flush=True)
        sys.exit(p.returncode)
    return (O / (label + '.log')).read_text(encoding='utf-8', errors='replace')


prepared = set()
for job in jobs:
    if (job['kind'], job['profile'], job['mode']) in done:
        print('SKIP ' + job['kind'] + '-' + job['profile'] + '-' + job['mode'], flush=True)
        continue
    kind = job['kind']
    profile = job['profile']
    short = profile.removeprefix('win-msvc-')
    mode = job['mode']
    producer = R / ('build/b6-' + kind + '-' + short)
    if (kind, profile) not in prepared:
        run(kind + '-' + short + '-configure', ['cmake', '-S', str(R), '-B', str(producer)])
        if kind == 'native':
            run(kind + '-' + short + '-build', ['cmake', '--build', str(producer),
                '--config', 'Release' if short == 'release' else 'Debug', '--parallel', '4', '--', '/nr:false'])
        prepared.add((kind, profile))
    argv = [sys.executable, '-X', 'utf8', 'tools/footprint/' + ('formal.py' if kind == 'native' else 'embedded_formal.py'),
            '--profile', profile]
    argv += ['--mode', mode, '--build', str(producer)] if kind == 'native' else ['--producer', str(producer)]
    output = run(kind + '-' + short + '-' + mode, argv)
    paths = [Path(line.strip()) / 'report.json' for line in output.splitlines()
             if line.strip().startswith(str(R / 'evidence'))]
    assert len(paths) == 1, output
    report = json.loads(paths[0].read_text())
    assert report['status'] == 'Passed', report
    reports.append({**job, 'report': str(paths[0])})
    save(O / 'reports.json', reports)
print('ALL 10 FORMAL REPORTS PASSED', flush=True)

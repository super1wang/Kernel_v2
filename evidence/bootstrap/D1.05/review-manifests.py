import hashlib, json, pathlib, re
root = pathlib.Path(__file__).resolve().parents[3]
def read(p): return json.loads((root / p).read_text(encoding='utf-8-sig'))
def sha(p): return hashlib.sha256((root / p).read_bytes()).hexdigest()
old = read('tests/manifests/d1.04.expected.json')
new = read('tests/manifests/d1.05.expected.json')
assert new['cases'][:len(old['cases'])] == old['cases']
assert new['checks'] == old['checks']
added = new['cases'][len(old['cases']):]
plan_names = re.findall(r'^\| (T\d+\.native\.\w+) \|', (root / 'docs/plans/D1.05.md').read_text(encoding='utf-8-sig'), re.M)
assert len(added) == len(plan_names) == 32
assert [c['id'] for c in added] == plan_names
assert len({c['id'] for c in new['cases']}) == len(new['cases'])
out = {'kind':'independent AI static manifest verification', 'profiles':[], 'sha256':{}}
for profile, expected in [('debug',283),('release',283),('asan',285)]:
    p = f'tests/runs/d1.05-win-msvc-{profile}.json'
    m = read(p)
    previous = read(p.replace('d1.05-', 'd1.04-'))
    def norm(v): return json.dumps(v,ensure_ascii=False,sort_keys=True).replace('d1.04-', 'd1.05-')
    for key in ['source_patterns','required_artifacts','build_outputs','runtime_artifact_patterns','required_runtime_artifacts']:
        current = {norm(x) for x in m[key]}
        assert all(norm(x) in current for x in previous[key]), (profile,key)
    for key in ['checks','repeat','timeout_seconds','configuration','preset','asan','review_policy','review_required','conformance_manifest']:
        assert norm(previous[key]) == norm(m[key]), (profile,key)
    assert 'examples/native_service/**' in m['source_patterns']
    assert 'docs/plans/D1.05.md' in m['source_patterns']
    assert '-DOCK_BUILD_NATIVE_TESTS=ON' in m['configure']
    assert len(m['checks']) == 3
    count = sum(not c.get('profiles') or m['profile'] in c['profiles'] for c in new['cases'])
    assert count == expected
    minima = [x for x in m['required_runtime_artifacts'] if '/contract/native/' in x['pattern']]
    out['profiles'].append({'profile':m['profile'],'expected_ctest':count,'checks':len(m['checks']),'native_minima':minima})
    out['sha256'][p]=sha(p)
matrix='tests/runs/d1.05-matrix.json'
assert read(matrix)['required_runs'] == [{'task_id':'D1.05','profile':f'win-msvc-{p}'} for p in ['debug','release','asan']]
for p in [matrix,'tests/manifests/d1.05.expected.json']:
    out['sha256'][p]=sha(p)
out['status']='Passed static configuration assertions; no behavior run'
(pathlib.Path(__file__).parent/'review-manifests.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'status':out['status'],'counts':[p['expected_ctest'] for p in out['profiles']],'sha256':out['sha256']},indent=2))

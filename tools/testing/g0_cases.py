"""从实际函数发现 D0.05/D0.06 用例；固定预期另存 manifest。"""
import argparse
import ast
from pathlib import Path
import re
import sys
import unittest
ROOT = Path(__file__).resolve().parents[2]
GROUPS = {
    'D0.05': [('tests/model/commit_model/test_commit_model.py', 'commit_model.test_commit_model'), ('tests/model/dedup_model/test_dedup_model.py', 'dedup_model.test_dedup_model')],
    'D0.06': [('tests/tools/evidence/test_process.py', 'test_process'), ('tests/tools/evidence/test_runner.py', 'test_runner'), ('tests/conformance/test_bootstrap.py', 'test_bootstrap')],
}

def cases():
    found = []
    for task, sources in GROUPS.items():
        for file, module in sources:
            for cls in ast.parse((ROOT / file).read_text(encoding='utf-8')).body:
                if not isinstance(cls, ast.ClassDef):
                    continue
                for method in cls.body:
                    if not isinstance(method, ast.FunctionDef) or not method.name.startswith('test_'):
                        continue
                    match = re.fullmatch(r'test_(T[0-9]{2})_([a-z0-9]+)_([a-z0-9_]+)', method.name)
                    if not match:
                        raise ValueError('unexpected case name: ' + method.name)
                    found.append((task, '.'.join(match.groups()), module + '.' + cls.name + '.' + method.name))
    if not found or len(found) != len({row[1] for row in found}):
        raise ValueError('empty or duplicate discovery')
    return found

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--emit-cmake', type=Path)
    parser.add_argument('--case')
    args = parser.parse_args()
    found = cases()
    if args.emit_cmake:
        rows = []
        for task, name, method in found:
            rows.append(f'add_test(NAME {name} COMMAND "{Path(sys.executable).as_posix()}" -X utf8 "{Path(__file__).resolve().as_posix()}" --case "{method}")')
            rows.append(f'set_tests_properties({name} PROPERTIES LABELS "{task}" TIMEOUT 240 WORKING_DIRECTORY "{ROOT.as_posix()}")')
        args.emit_cmake.write_text('\n'.join(rows) + '\n', encoding='utf-8', newline='\n')
        return 0
    if args.case not in {row[2] for row in found}:
        raise ValueError('case not found in actual source')
    for path in ('', 'build/python-deps', 'tests/model', 'tests/tools/evidence', 'tests/conformance'):
        sys.path.insert(0, str(ROOT / path))
    suite = unittest.defaultTestLoader.loadTestsFromName(args.case)
    if suite.countTestCases() != 1:
        raise ValueError('exactly one concrete test is required')
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return int(not (result.testsRun == 1 and result.wasSuccessful() and not result.skipped and not result.expectedFailures))

if __name__ == '__main__':
    sys.exit(main())

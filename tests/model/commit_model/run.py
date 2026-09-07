"""D0.05 严格模型入口；单例必须存在且实际执行一次，跳过/预期失败均拒绝。"""
import argparse
import ast
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tests/model'))


def discovered():
    found = []
    for folder in ('commit_model', 'dedup_model'):
        path = ROOT / 'tests/model' / folder / ('test_' + folder + '.py')
        for cls in ast.parse(path.read_text(encoding='utf-8')).body:
            if isinstance(cls, ast.ClassDef):
                found.extend(f'{folder}.{path.stem}.{cls.name}.{node.name}' for node in cls.body
                             if isinstance(node, ast.FunctionDef) and node.name.startswith('test_'))
    if not found or len(found) != len(set(found)): raise ValueError('empty or duplicate model cases')
    return found


def main():
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--all', action='store_true'); group.add_argument('--case'); group.add_argument('--list', action='store_true')
    args = parser.parse_args(); cases = discovered()
    if args.list:
        print('\n'.join(cases)); return 0
    selected = cases if args.all else [args.case]
    if any(case not in cases for case in selected): raise ValueError('case is absent from source')
    suite = unittest.defaultTestLoader.loadTestsFromNames(selected)
    if suite.countTestCases() != len(selected): raise ValueError('test count differs from selection')
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return int(not (result.testsRun == len(selected) and result.wasSuccessful() and not result.skipped and not result.expectedFailures))


if __name__ == '__main__': sys.exit(main())

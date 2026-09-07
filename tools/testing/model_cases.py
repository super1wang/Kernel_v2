"""从真实测试函数发现 CTest 名称，不读取 expected 清单。"""
import argparse
import ast
from pathlib import Path
import re
import sys
import unittest

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'tests/model/execution_model/test_execution_model.py'

def cases():
    found=[]
    for cls in ast.parse(SOURCE.read_text(encoding='utf-8')).body:
        if not isinstance(cls,ast.ClassDef):continue
        for method in cls.body:
            if isinstance(method,ast.FunctionDef) and method.name.startswith('test_'):
                match=re.fullmatch(r'test_(T\d{2})_([a-z0-9]+)_([a-z0-9_]+)',method.name)
                if not match:raise ValueError('unexpected test naming: '+method.name)
                found.append(('.'.join(match.groups()),f'{SOURCE.stem}.{cls.name}.{method.name}'))
    if not found or len(found)!=len({name for name,_ in found}):raise ValueError('missing or duplicate model cases')
    return found

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--emit-cmake',type=Path);parser.add_argument('--case')
    args=parser.parse_args();discovered=cases()
    if args.emit_cmake:
        rows=[]
        for name,test in discovered:
            rows.append(f'add_test(NAME {name} COMMAND "{Path(sys.executable).as_posix()}" -X utf8 "{Path(__file__).resolve().as_posix()}" --case "{test}")')
            rows.append(f'set_tests_properties({name} PROPERTIES LABELS "D0.03" TIMEOUT 30)')
        args.emit_cmake.write_text('\n'.join(rows)+'\n',encoding='utf-8',newline='\n')
        return 0
    if args.case not in {test for _,test in discovered}:raise ValueError('test does not exist in actual source')
    sys.path.insert(0,str(ROOT/'build/python-deps'));sys.path.insert(0,str(SOURCE.parent))
    suite=unittest.defaultTestLoader.loadTestsFromName(args.case)
    if suite.countTestCases()!=1:raise ValueError('expected exactly one concrete test')
    result=unittest.TextTestRunner(verbosity=2).run(suite)
    return not (result.testsRun==1 and result.wasSuccessful() and not result.skipped and not result.expectedFailures)

if __name__=='__main__':sys.exit(main())

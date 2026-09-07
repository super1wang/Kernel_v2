"""D0.04 独立逐项入口；从源码发现，不读取 expected 清单。"""
import argparse
import ast
from pathlib import Path
import re
import sys
import unittest
ROOT=Path(__file__).resolve().parents[2]
SOURCES=[ROOT/'tests/plan/test_plan_contract.py',ROOT/'tests/control/test_control_contract.py']
def cases():
    found=[]
    for source in SOURCES:
        for cls in ast.parse(source.read_text(encoding='utf-8')).body:
            if not isinstance(cls,ast.ClassDef):continue
            for method in cls.body:
                if not isinstance(method,ast.FunctionDef) or not method.name.startswith('test_'):continue
                match=re.fullmatch(r'test_(T\d{2})_([a-z0-9]+)_([a-z0-9_]+)',method.name)
                if not match:raise ValueError('Unexpected test name: '+method.name)
                found.append(('.'.join(match.groups()),f'{source.stem}.{cls.name}.{method.name}'))
    if not found or len(found)!=len({name for name,_ in found}):raise ValueError('Missing or duplicate cases')
    return found
def main():
    parser=argparse.ArgumentParser();group=parser.add_mutually_exclusive_group(required=True);group.add_argument('--case');group.add_argument('--emit-cmake',type=Path);group.add_argument('--list',action='store_true');args=parser.parse_args();found=cases()
    if args.list:
        for name,test in found:print(name,test)
        return 0
    if args.emit_cmake:
        rows=[]
        for name,test in found:
            rows.append(f'add_test(NAME {name} COMMAND "{Path(sys.executable).as_posix()}" -X utf8 "{Path(__file__).resolve().as_posix()}" --case "{test}")')
            rows.append(f'set_tests_properties({name} PROPERTIES LABELS "D0.04" TIMEOUT 30)')
        args.emit_cmake.write_text('\n'.join(rows)+'\n',encoding='utf-8',newline='\n');return 0
    if args.case not in {test for _,test in found}:raise ValueError('Case absent from actual source')
    for path in [ROOT/'build/python-deps',*(source.parent for source in SOURCES)]:sys.path.insert(0,str(path))
    suite=unittest.defaultTestLoader.loadTestsFromName(args.case)
    if suite.countTestCases()!=1:raise ValueError('Expected one concrete test')
    result=unittest.TextTestRunner(verbosity=2).run(suite)
    return not(result.testsRun==1 and result.wasSuccessful() and not result.skipped and not result.expectedFailures)
if __name__=='__main__':sys.exit(main())

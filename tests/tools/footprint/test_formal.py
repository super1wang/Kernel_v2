"""正式聚合不得挑样、复用 pilot、隐去超限或放过输入变动。"""
import unittest
from tools.footprint.formal import bounded_metrics, verify_inventory
from pathlib import Path
import tempfile
import json
import subprocess
import sys


class FormalTests(unittest.TestCase):
    def test_ctest_profile_mapping(self):
        root=Path(__file__).resolve().parents[3]
        template=(root/'tests/tools/footprint/formal-tests.cmake.in').read_text(encoding='utf-8')
        for asan,config,profile,count in [('OFF','Debug','win-msvc-debug',10),
                                          ('OFF','Release','win-msvc-release',3),
                                          ('ON','Debug','win-msvc-asan',2)]:
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as d:
                folder=Path(d)
                rendered=template
                for key,value in {'OCK_ENABLE_ASAN':asan,'Python3_EXECUTABLE':Path(sys.executable).as_posix(),
                                  'PROJECT_SOURCE_DIR':root.as_posix(),'CMAKE_BINARY_DIR':folder.as_posix()}.items():
                    rendered=rendered.replace('@'+key+'@',value)
                (folder/'CTestTestfile.cmake').write_text(rendered,encoding='utf-8')
                run=subprocess.run(['ctest','--test-dir',d,'-C',config,'--show-only=json-v1'],capture_output=True,text=True,check=True)
                tests=json.loads(run.stdout)['tests']
                self.assertEqual(len(tests),count)
                for test in tests:
                    command=test['command']
                    if '--profile' in command:
                        self.assertEqual(command[command.index('--profile')+1],profile)

    def test_exact_finite_metrics(self):
        bounded_metrics({'bytes': 10}, {'bytes': 10})
        for metrics, limits in [({'bytes': 11}, {'bytes': 10}),
                                ({}, {'bytes': 10}),
                                ({'bytes': float('nan')}, {'bytes': 10}),
                                ({'bytes': 0}, {'bytes': float('inf')}),
                                ({'bytes': 0}, {'bytes': True})]:
            with self.assertRaises(ValueError): bounded_metrics(metrics, limits)

    def test_missing_or_changed_raw_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'raw'; p.write_bytes(b'actual')
            with self.assertRaises(ValueError):
                verify_inventory([{'path':str(p),'sha256':'0'*64,'bytes':6}])
            with self.assertRaises(ValueError):
                verify_inventory([{'path':str(p)+'missing','sha256':'0'*64,'bytes':6}])

if __name__=='__main__': unittest.main()

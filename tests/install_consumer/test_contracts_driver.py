"""防止 SDK ASan 验证把普通 Debug 或冲突插桩当作成功。"""
from pathlib import Path
import tempfile
import unittest
from tests.install_consumer.verify_contracts import ROOT, read_profile, verify_compile_trace

class ContractsDriverTests(unittest.TestCase):
    def test_actual_cache_must_explicitly_match_locked_profile(self):
        rows = ['CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022', 'CMAKE_GENERATOR_PLATFORM:INTERNAL=x64',
                'CMAKE_GENERATOR_TOOLSET:INTERNAL=v143,version=14.44.35207', 'CMAKE_SYSTEM_VERSION:STRING=10.0.26100.0']
        with tempfile.TemporaryDirectory(prefix='contracts-profile-', dir=ROOT/'build') as directory:
            build = Path(directory)
            for value, expected in [('ON', True), ('OFF', False)]:
                (build/'CMakeCache.txt').write_text('\n'.join(rows+['OCK_ENABLE_ASAN:BOOL='+value]), encoding='utf-8')
                self.assertEqual(read_profile(build), expected)
            for mutated in [rows, rows+['OCK_ENABLE_ASAN:BOOL=garbage'],
                            [s.replace('x64','Win32') for s in rows]+['OCK_ENABLE_ASAN:BOOL=ON']]:
                (build/'CMakeCache.txt').write_text('\n'.join(mutated), encoding='utf-8')
                with self.assertRaises(ValueError): read_profile(build)
    def test_actual_trace_rejects_missing_or_conflicting_instrumentation(self):
        verify_compile_trace('/c /Z7 /fsanitize=address /MDd', True)
        verify_compile_trace('/c /Z7 /MDd /RTC1', False)
        for trace, asan in [('/c /Z7 /MDd', True), ('/c /Zi /fsanitize=address', True),
                            ('/c /Z7 /RTC1 /fsanitize=address', True),
                            ('/c /Z7 /fsanitize=address', False)]:
            with self.assertRaises(AssertionError): verify_compile_trace(trace, asan)

if __name__ == '__main__': unittest.main()

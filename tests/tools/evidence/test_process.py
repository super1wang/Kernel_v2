"""D0.06-b 真实进程退出、字节输出及受控进程树反例。"""
from pathlib import Path
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
from tools.evidence.common import save_json

class ProcessTests(unittest.TestCase):
    def test_T23_process_creation_clock_matches_child(self):
        r=self.run_python("import ctypes;v=ctypes.c_longlong();f=ctypes.c_longlong();k=ctypes.windll.kernel32;assert k.QueryPerformanceCounter(ctypes.byref(v));assert k.QueryPerformanceFrequency(ctypes.byref(f));print(v.value,f.value)")
        self.assertEqual(r['exit_code'],0)
        ticks,frequency=map(int,(self.folder/'stdout.log').read_text().split())
        clock=r['creation_clock']
        self.assertEqual(frequency,clock['qpc_frequency'])
        self.assertGreater(frequency,0)
        self.assertGreaterEqual(ticks,clock['qpc_ticks'])
        self.assertLess((ticks-clock['qpc_ticks'])/frequency,30)
    def setUp(self):
        fixture_root=ROOT/'build/evidence-fixtures'
        fixture_root.mkdir(parents=True,exist_ok=True)
        self.folder=Path(tempfile.mkdtemp(prefix='ock-process-',dir=fixture_root))/'evidence'
        self.folder.mkdir()
    def run_python(self,code,timeout=30):
        result = execute([sys.executable,'-X','utf8','-c',code],ROOT,self.folder/'stdout.log',self.folder/'stderr.log',timeout)
        save_json(self.folder/'command.json',result)
        return result
    def test_T23_process_nonfinite_timeout_rejected(self):
        for value in (float('nan'),float('inf'),-1,0,True,3601):
            with self.assertRaises(ValueError): self.run_python('pass',value)
    def test_T23_process_raw_bytes(self):
        r=self.run_python("import os;os.write(1,b'raw\\r\\n');os.write(2,b'err\\n')")
        self.assertEqual(r['exit_code'],0)
        self.assertEqual((self.folder/'stdout.log').read_bytes(),b'raw\r\n')
        self.assertEqual((self.folder/'stderr.log').read_bytes(),b'err\n')
        self.assertEqual(r['process_tree']['active_after'],0)
    def test_T23_process_printed_success_nonzero(self):
        r=self.run_python("print('Passed');raise SystemExit(7)")
        self.assertEqual(r['exit_code'],7)
        self.assertNotEqual(r['exit_code'],0)
    def test_T23_process_launch_failure(self):
        r=execute([str(self.folder/'missing.exe')],ROOT,self.folder/'stdout.log',self.folder/'stderr.log',1)
        save_json(self.folder/'command.json',r)
        self.assertEqual(r['status'],'LaunchFailed')
        self.assertIsNone(r['exit_code'])
    def test_T23_process_crashed_exit(self):
        r=self.run_python("import ctypes;ctypes.windll.kernel32.ExitProcess(0xC0000409)")
        self.assertEqual(r['status'],'Crashed')
        self.assertEqual(r['exit_code'],0xC0000409)
    def test_T23_process_timeout_tree(self):
        r=self.run_python("import subprocess,sys,time;p=subprocess.Popen([sys.executable,'-c','import time;time.sleep(120)']);print(p.pid,flush=True);time.sleep(120)",5)
        self.assertEqual(r['status'],'Timeout')
        self.assertEqual(r['process_tree']['active_after'],0)
        self.assertGreaterEqual(r['process_tree']['total_processes'],2)
        self.assertTrue(r['process_tree']['terminated_owned_job'])
    def test_T23_process_leaked_child_rejected(self):
        r=self.run_python("import subprocess,sys;p=subprocess.Popen([sys.executable,'-c','import time;time.sleep(120)']);print(p.pid,flush=True)",5)
        self.assertEqual(r['status'],'DescendantsAlive')
        self.assertEqual(r['process_tree']['active_after'],0)
        self.assertTrue(r['process_tree']['terminated_owned_job'])

if __name__=='__main__':unittest.main()

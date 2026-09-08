"""故障包装拒绝混合诊断、错误流及不一致退出事实。"""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec=importlib.util.spec_from_file_location('host_wrapper',Path(__file__).with_name('verify_runtime.py'))
wrapper=importlib.util.module_from_spec(spec);spec.loader.exec_module(wrapper)


class FaultWrapperTests(unittest.TestCase):
    def invoke(self, corruption=None):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);binary=root/'test.exe';binary.write_bytes(b'fixture')
            def execute(argv,cwd,stdout,stderr,timeout):
                mode=argv[1];code=86 if mode in ('create-ordinal-2','stl-default-vector') else 0
                marker=(b'host_fault_terminate ordinal=2 size=16 calls=2' if mode=='create-ordinal-2'
                        else b'host_fault_terminate ordinal=1 size=16 calls=1')
                out=b'' if code else b'host_fault_controls_checked\n'
                err=marker+b'\r\n' if code else b''
                row={'status':'Exited','exit_code':code,'observed_exit_code':code,
                     'process_tree':{'assigned_before_resume':True,'active_after':0,'terminated_owned_job':False}}
                if mode=='create-ordinal-2' and corruption:
                    if corruption=='extra':err+=b'other error\n'
                    elif corruption=='stdout':out,err=err,b''
                    elif corruption=='success':out=b'host_fault_controls_checked\n'
                    elif corruption=='duplicate':err+=err
                    elif corruption=='observed':row['observed_exit_code']=0
                    elif corruption=='missing':del row['observed_exit_code']
                if mode=='create-recoverable' and corruption=='success_stderr':err=b'error\n'
                stdout.write_bytes(out);stderr.write_bytes(err);return row
            args=['verify_runtime','--binary',str(binary),'--fault-binary',str(binary),
                  '--case','T03.host.configuration','--work',str(root/'out'),
                  '--runtime-dir',directory,'--configuration','Debug']
            with patch.object(wrapper,'execute',execute),patch('sys.argv',args):
                return wrapper.main()

    def test_valid(self):self.assertEqual(self.invoke(),0)

    def test_reject_corruptions(self):
        for kind in ('extra','stdout','success','duplicate','observed','missing','success_stderr'):
            with self.subTest(kind=kind),self.assertRaises(ValueError):self.invoke(kind)


if __name__=='__main__':unittest.main()

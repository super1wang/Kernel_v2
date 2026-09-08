"""复用旧原生包装时，优化Python和被清理的子进程不得产生绿色结果。"""
import importlib.util
import json
from pathlib import Path
import sys
import unittest
import uuid
from tools.evidence.process import execute
from tools.evidence.common import save_json, sha_file

ROOT=Path(__file__).resolve().parents[2]


class WrapperGuards(unittest.TestCase):
    def test_optimized_wrapper_modes_rejected(self):
        out=ROOT/'evidence/bootstrap/D1.06'/('sdk-wrapper-guard-'+uuid.uuid4().hex[:10])
        out.mkdir(parents=True)
        failures=[]
        for folder in ('registration','authorization','native'):
            script=ROOT/'tests/contract'/folder/'verify_children.py'
            raw=[out/(folder+'-'+kind+'.log') for kind in ('stdout','stderr')]
            row=execute([sys.executable,'-O',str(script),'--help'],ROOT,*raw,60)
            row['source_sha256']=sha_file(script)
            save_json(out/(folder+'.json'),row)
            if row['status']!='Exited' or row['exit_code']==0 or b'optimized_python_not_supported' not in b''.join(p.read_bytes() for p in raw):
                failures.append(folder)
        self.assertEqual(failures,[],str(out))

    def test_terminated_job_cannot_pass(self):
        for folder,method in [('authorization','succeeded'),('native','exited')]:
            with self.subTest(folder=folder):
                path=ROOT/'tests/contract'/folder/'verify_children.py'
                spec=importlib.util.spec_from_file_location('guard_'+folder,path)
                module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
                row={'status':'Exited','exit_code':0,'process_tree':{'mechanism':'WindowsJobObject',
                    'assigned_before_resume':True,'active_after':0,'terminated_owned_job':False}}
                check=getattr(module,method)
                self.assertTrue(check(row))
                row['process_tree']['terminated_owned_job']=True
                self.assertFalse(check(row))
                row['process_tree']['terminated_owned_job']=False
                row['process_tree']['assigned_before_resume']=False
                self.assertFalse(check(row))


if __name__=='__main__':unittest.main()

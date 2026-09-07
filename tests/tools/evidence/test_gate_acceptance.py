"""自动验收：矩阵及Git来源不能被政策变更豁免。"""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from tools.evidence.accept_gate import matrix_errors, candidate_errors

class GateAcceptanceTests(unittest.TestCase):
    def test_matrix_requires_every_profile(self):
        required=[('D0.06','debug'),('D0.06','release')]
        self.assertTrue(matrix_errors(required,[required[0]]))
        self.assertEqual(matrix_errors(required,required),[])
    def test_duplicate_or_extra_is_rejected(self):
        self.assertTrue(matrix_errors([('D0.06','debug')],[('D0.06','debug')]*2))
        self.assertTrue(matrix_errors([('D0.06','debug')],[('D0.06','release')]))
        self.assertTrue(matrix_errors([],[]))
        self.assertTrue(matrix_errors([('D0.06','debug')]*2,[('D0.06','debug')]))
    def test_actual_git_bytes_and_clean_claim(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            def git(*args): return subprocess.check_output(['git','-C',d,*args],stderr=subprocess.STDOUT)
            git('init','-q');git('config','user.name','isolated fixture');git('config','user.email','fixture@example.invalid')
            (root/'source.cpp').write_bytes(b'original\n');git('add','.');git('commit','-qm','fixture')
            commit=git('rev-parse','HEAD').decode().strip()
            rows=[{'path':'source.cpp','size':9,'sha256':hashlib.sha256(b'original\n').hexdigest()}]
            self.assertEqual(candidate_errors(root,commit,rows),[])
            bad=[dict(rows[0],sha256='0'*64)]
            self.assertTrue(candidate_errors(root,commit,bad))
            self.assertTrue(candidate_errors(root,commit,[dict(rows[0],path='../source.cpp')]))
            self.assertTrue(candidate_errors(root,commit,[]))
            self.assertTrue(candidate_errors(root,'a'*40,rows))
            self.assertTrue(candidate_errors(root,commit,rows+rows))
            # 当前工作区已变化仍只验证声明的历史候选，不冒充当前源码验证。
            (root/'source.cpp').write_bytes(b'new version\n')
            self.assertEqual(candidate_errors(root,commit,rows),[])

if __name__=='__main__': unittest.main()

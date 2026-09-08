import copy
import unittest
from tools.footprint.thread_record import validate_thread_record


class ThreadOriginTests(unittest.TestCase):
    def record(self):
        return {'format':'ock.thread-origin/1','pid':10,'main_tid':11,'held_tid':12,'held_joined':True,
                'capture':0,'walk_end':259,'free_marker':0,'free_snapshot':0,
                'threads':[{'pid':10,'tid':tid,'flags':0,'start':101,'module_base':100,'module_bytes':20,
                            'rva':1,'created':1000,'module':'C:\\test.dll'} for tid in (11,12)]}

    def test_valid(self):validate_thread_record(self.record(),10,True)

    def test_reject(self):
        actions=[lambda r:r.update(capture=5),lambda r:r.update(free_snapshot=5),
                 lambda r:r.update(held_joined=False),lambda r:r.update(pid=99),
                 lambda r:r.update(main_tid=99),lambda r:r.update(held_tid=0),
                 lambda r:r['threads'].append(copy.deepcopy(r['threads'][0])),
                 lambda r:r['threads'][0].update(pid=99),lambda r:r['threads'][0].update(flags=1),
                 lambda r:r['threads'][0].update(start=121),lambda r:r['threads'][0].update(rva=2),
                 lambda r:r['threads'][0].update(module=''),lambda r:r.update(threads=r['threads']*33)]
        for i,mutate in enumerate(actions):
            with self.subTest(i=i):
                r=self.record();mutate(r)
                with self.assertRaises(ValueError):validate_thread_record(r,10,True)

if __name__=='__main__':unittest.main()

"""B4 的新增 Adapter/Profile 边界，沿用唯一清单校验器。"""
from copy import deepcopy
import unittest
from tools.architecture import check

class B4SurfaceTests(unittest.TestCase):
    def graph(self):
        manifest=check.load_manifest()
        graph={name:{'kind':t['kind'],'dependencies':t['dependencies'],'system_dependencies':t.get('system_dependencies',[]),'implementation':t['implementation'],'external_dependencies':t.get('external_dependencies',[]),'compile_features':t['public_compile_features'],'compile_options':t['public_compile_options'],'compile_definitions':[]} for name,t in manifest['targets'].items()}
        return manifest,graph

    def test_cpu_is_real_and_private(self):
        m,g=self.graph();self.assertEqual([],check.validate_manifest(m,g,'B4Subset'))
        self.assertEqual(g['Adapter::CpuPool']['dependencies'],['CoreContracts'])
        g['Adapter::CpuPool']['external_dependencies']=['thread_pool']
        self.assertTrue(check.validate_manifest(m,g,'B4Subset'))

    def test_b3_projection_must_not_claim_cpu(self):
        m,g=self.graph();self.assertTrue(check.validate_manifest(m,g,'B3Subset'))
        g['Adapter::CpuPool'].update(kind='INTERFACE_LIBRARY',implementation='ContractBaseline')
        self.assertEqual([],check.validate_manifest(m,g,'B3Subset'))
        self.assertTrue(check.validate_manifest(m,g,'B4Subset'))

    def test_unknown_profile_rejected(self):
        m,g=self.graph();self.assertTrue(check.validate_manifest(m,g,'CpuPoolOnly'))

    def test_embedded_has_only_runtime_and_cpu(self):
        m,g=self.graph();selected={'Foundation','CoreContracts','Runtime','Adapter::CpuPool'}
        embedded={name:value for name,value in g.items() if name in selected}
        self.assertEqual([],check.validate_manifest(m,embedded,'Embedded'))
        embedded['Data']=g['Data']
        self.assertTrue(check.validate_manifest(m,embedded,'Embedded'))
        del embedded['Data'];embedded['Adapter::CpuPool']['kind']='INTERFACE_LIBRARY'
        self.assertTrue(check.validate_manifest(m,embedded,'Embedded'))

    def test_runtime_cannot_include_cpu(self):
        m=check.load_manifest()
        self.assertTrue(check.validate_include('Runtime','#include <ock/adapters/cpu_pool/cpu_pool.hpp>',m))

    def test_cpu_cannot_regress_to_contract(self):
        m=deepcopy(check.load_manifest());m['targets']['Adapter::CpuPool']['kind']='INTERFACE_LIBRARY'
        self.assertTrue(check.validate_manifest(m))

if __name__=='__main__':unittest.main()

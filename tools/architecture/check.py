"""D0.02 静态依赖、公开表面和入口合同守卫；不执行产品授权。"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT=Path(__file__).resolve().parents[2]

def load_manifest():
    return json.loads((ROOT/'sdk/sdk_api_manifest.json').read_text(encoding='utf-8'))

def normative_graph():
    text=(ROOT/'docs/01_Architecture_v3.3.md').read_text(encoding='utf-8')
    text=text.split('### A02.1',1)[1].split('### A02.2',1)[0]
    return {name:[] if deps.strip()=='无' else deps.strip().split('、')
            for name,deps in re.findall(r'^\| `OCK::([^`]+)` \| ([^|]+) \|',text,re.M)}

def transitive_dependencies(component,targets):
    seen=set();pending=list(targets[component]['dependencies'])
    while pending:
        name=pending.pop()
        if name in seen:continue
        seen.add(name)
        if name in targets:pending.extend(targets[name]['dependencies'])
    return seen

def preprocessing_view(source):
    """屏蔽注释但保留换行，并记录字符串位置，避免把字符串中的#当指令。"""
    source=re.sub(r'\\\r?\n','',source)
    chars=list(source);literal=[False]*len(source);i=0
    while i<len(source):
        if source.startswith('//',i):
            end=source.find('\n',i)
            if end<0:end=len(source)
            chars[i:end]=' '*(end-i);i=end
        elif source.startswith('/*',i):
            end=source.find('*/',i+2)
            if end<0:raise ValueError('未结束的块注释')
            end+=2
            for n in range(i,end):
                if chars[n] not in '\r\n':chars[n]=' '
            i=end
        elif source.startswith('R"',i):
            raw=re.match(r'R"([^\s()\\]{0,16})\(',source[i:])
            if raw is None:raise ValueError('无法解析的原始字符串')
            closing=')'+raw.group(1)+'"';end=source.find(closing,i+raw.end())
            if end<0:raise ValueError('未结束的原始字符串')
            end+=len(closing);literal[i:end]=[True]*(end-i);i=end
        elif source[i] in ('"',"'"):
            quote=source[i];literal[i]=True;i+=1
            while i<len(source):
                literal[i]=True
                if source[i]=='\\':
                    if i+1<len(source):literal[i+1]=True
                    i+=2;continue
                if source[i]==quote:i+=1;break
                i+=1
        else:i+=1
    return ''.join(chars),literal

def validate_include(component,source,manifest,public=False,source_path=None):
    errors=[];targets=manifest['targets']
    allowed=transitive_dependencies(component,targets)|{component}
    try:text,literals=preprocessing_view(source)
    except ValueError as exc:return [str(exc)]
    if public and component == 'CoreContracts':
        # 小型声明守卫；完整模板/能力边界仍由真实编译消费者和声明审核证明。
        declarations=''.join(' ' if literals[i] else ch for i,ch in enumerate(text))
        # LogComponent::Host 是固定分类枚举值，不是通用 Host 类型。
        declarations=re.sub(r'enum\s+class\s+LogComponent\s*:\s*std::uint8_t\s*\{\s*Host\s*=\s*0\s*,\s*Registry\s*=\s*1\s*,\s*Policy\s*=\s*2\s*,\s*Invocation\s*=\s*3\s*\}\s*;', '', declarations)
        if re.search(r'\b(?:Host|Document|Payload|ServiceLocator)\b|\bstd\s*::\s*any\b|\bvoid\s*\*',declarations):
            errors.append('CoreContracts公开声明不得暴露通用Host/状态/动态值或无类型服务指针')
    for match in re.finditer(r'^[ \t]*#[ \t]*include\b([^\n]*)',text,re.M):
        hash_position=text.index('#',match.start(),match.end())
        if literals[hash_position]:continue
        directive=match.group(1)
        parsed=re.fullmatch(r'\s*(?:<([^>\n]+)>|"([^"\n]+)")\s*',directive)
        if not parsed:
            errors.append('无法解析的 include 需要显式审查：'+directive);continue
        include=parsed.group(1) or parsed.group(2)
        normalized=include.replace('\\','/')
        if any(part in ('','.','..') for part in normalized.split('/')):
            errors.append(f'包含路径必须规范且不可跨目录逃逸：{include}');continue
        if normalized.startswith('ock/'):
            owners=[name for name,t in targets.items() if normalized.startswith(t['namespace_prefix'])]
            if not owners:errors.append(f'未知公开头所属组件：{include}');continue
            owner=max(owners,key=lambda name:len(targets[name]['namespace_prefix']))
            if owner not in allowed:errors.append(f'{component} 越层包含 {owner}: {include}')
            if '/detail/' in normalized or '/internal/' in normalized:
                approved = normalized in manifest.get('implementation_includes', {}).get(source_path, [])
                installed_source = source_path is not None and source_path.startswith(targets[component]['include_root']+'/')
                if owner != component or ((public or installed_source) and not approved):errors.append(f'私有头泄漏：{include}')
        elif public and any(normalized.startswith(prefix) for prefix in manifest['private_third_party_prefixes']):
            errors.append(f'第三方实现泄漏到公开头：{include}')
        elif '..' in normalized.split('/'):
            errors.append(f'包含路径不可跨目录逃逸：{include}')
    return errors

def validate_frontend_access(owner,kind,route,authorized,immutable):
    if owner not in ('UI','Workspace') or kind not in ('Read','StateEdit','ExternalEffect','Lifecycle'):
        return ['未知前端或业务形态']
    if route=='Operation':return []
    if route=='Direct' and kind=='Read' and authorized and immutable:return []
    return ['业务写必须经 Operation；直接读取仅限已授权不可变快照']

def validate_rpc_methods(methods):
    return [f'禁止通用状态 RPC：{method}' for method in methods if method.startswith('state.')]

FOUNDATION_PUBLIC_OPTIONS=['$<$<CXX_COMPILER_ID:MSVC>:/utf-8>', '$<$<CXX_COMPILER_ID:MSVC>:/EHsc>', '$<$<CXX_COMPILER_ID:MSVC>:/Zc:__cplusplus>', '$<$<CXX_COMPILER_ID:MSVC>:/permissive->']


def validate_implementation_stage(manifest):
    """只允许已实施组件前进；公开第三方依赖不能混入产品DAG。"""
    stages={'ContractBaseline':set(),'Foundation':{'Foundation'},'CoreContracts':{'Foundation','CoreContracts'},'NativeSubset':{'Foundation','CoreContracts','Runtime'},'B2Subset':{'Foundation','CoreContracts','Runtime','Data','Dynamic','ControlProtocol','Control'}}
    stage=manifest.get('stage')
    stages['B3Subset']=stages['B2Subset']|{'ControlClient','Adapter::LocalIPC'}
    stages['B4Subset']=stages['B3Subset']|{'Adapter::CpuPool'}
    stages['B5Subset']=stages['B4Subset']
    if stage not in stages:return ['未知SDK实施阶段']
    errors=[]
    for name,target in manifest['targets'].items():
        implemented=name in stages[stage]
        expected_stage = 'NativeSubset' if stage in ('NativeSubset','B2Subset','B3Subset','B4Subset','B5Subset') and name == 'Runtime' else ('B2Subset' if stage in ('B2Subset','B3Subset','B4Subset','B5Subset') and name in ('Data','Dynamic','ControlProtocol','Control') else ('B3Subset' if stage in ('B3Subset','B4Subset','B5Subset') and name in ('ControlClient','Adapter::LocalIPC') else ('Implemented' if implemented else 'ContractBaseline')))
        if stage=='B5Subset' and name in ('Runtime','Control'):expected_stage='B5Subset'
        if target.get('implementation') != expected_stage:
            errors.append(name+' 实施状态与当前阶段不符')
        expected=['expected'] if implemented and name=='Foundation' else []
        if target.get('external_dependencies',[])!=expected:
            errors.append(name+' 公开第三方依赖不符')
    return errors


CORE_CONTRACTS_HEADERS = {
    'packages/contracts/include/ock/contracts/'+name+'.hpp'
    for name in ('identity','context','outcome','ports','observation','operation')}

def validate_contracts_surface(manifest):
    """D1.02受审六头集合；字节、存在性和实际包含关系由完整校验继续检查。"""
    if manifest.get('stage') not in ('CoreContracts','NativeSubset','B2Subset','B3Subset','B4Subset','B5Subset'):
        return []
    expected=CORE_CONTRACTS_HEADERS | ({'packages/contracts/include/ock/contracts/logging.hpp'} if manifest['stage'] in ('NativeSubset','B2Subset','B3Subset','B4Subset','B5Subset') else set())
    if manifest['stage'] in ('B4Subset','B5Subset'):expected |= {'packages/contracts/include/ock/contracts/executor.hpp'}
    listed=[h['path'] for h in manifest['headers'] if h['target']=='CoreContracts']
    if set(listed) != expected or len(listed) != len(expected):
        return ['CoreContracts公开头集合与受审阶段不符']
    return []


NATIVE_IMPLEMENTATION_INCLUDES = {
    'packages/runtime/include/ock/runtime/host.hpp': ['ock/runtime/detail/host.hpp'],
    'packages/runtime/include/ock/runtime/detail/host.hpp': ['ock/runtime/detail/invocation.hpp'],
    'packages/runtime/include/ock/runtime/detail/invocation.hpp': ['ock/runtime/detail/private_bridge.hpp'],
}
NATIVE_RUNTIME_HEADERS = {
    'packages/runtime/include/ock/runtime/'+name+'.hpp'
    for name in ('registry','policy','native_types','host','logging',
                 'detail/host','detail/invocation','detail/private_bridge')}


def validate_manifest(manifest,actual_graph=None,build_components='B3Subset'):
    errors=[]
    def require(value,message):
        if not value:errors.append(message)
    targets=manifest['targets'];norm=normative_graph()
    require(build_components in ('Runtime','Embedded','B3Subset','B4Subset','B5Subset'),'未知生产组件选择')
    selected={'Foundation','CoreContracts','Runtime'} if build_components in ('Runtime','Embedded') else set(norm)
    if build_components=='Embedded':selected.add('Adapter::CpuPool')
    require(manifest['format']=='ock.sdk-api/1','公开清单格式不符')
    require(manifest['sdk_version']==({'B5Subset':'0.1.0-dev.6','B4Subset':'0.1.0-dev.5','B3Subset':'0.1.0-dev.4','B2Subset':'0.1.0-dev.3','NativeSubset':'0.1.0-dev.2'}.get(manifest['stage'],'0.1.0-dev.1')),'开发 SDK 版本必须独立于文档 v3.3')
    errors.extend(validate_implementation_stage(manifest))
    errors.extend(validate_contracts_surface(manifest))
    require(set(targets)==set(norm),'产品 target 集合与 A02 不一致')
    for name,target in targets.items():
        require(target['dependencies']==norm.get(name),f'{name} 直接依赖与 A02 不一致')
        expected_roots = ['packages/control/server','packages/control/observation'] if name=='Control' and manifest['stage'] in ('B2Subset','B3Subset','B4Subset','B5Subset') else []
        require(target.get('source_roots',[])==expected_roots,f'{name} 源码归属目录漂移')
        closure=transitive_dependencies(name,targets)
        require(name not in closure,f'{name} 依赖成环')
        require(set(target['dependencies'])<=targets.keys(),f'{name} 存在未知/测试目标依赖')
        require(target['export']=='OCK::'+name,f'{name} 导出名错误')
        require(target['kind']==('STATIC_LIBRARY' if (name=='Runtime' and manifest['stage'] in ('NativeSubset','B2Subset','B3Subset','B4Subset','B5Subset')) or (manifest['stage'] in ('B2Subset','B3Subset','B4Subset','B5Subset') and name in ('Data','Dynamic','ControlProtocol','Control')) or (manifest['stage'] in ('B3Subset','B4Subset','B5Subset') and name in ('ControlClient','Adapter::LocalIPC')) or (manifest['stage'] in ('B4Subset','B5Subset') and name=='Adapter::CpuPool') else 'INTERFACE_LIBRARY'),f'{name} 实际 target 类型错误')
        system = [{'name':'bcrypt','platform':'Windows','link_only':True}] if (name=='Runtime' and manifest['stage'] in ('NativeSubset','B2Subset','B3Subset','B4Subset','B5Subset')) or (manifest['stage'] in ('B2Subset','B3Subset','B4Subset','B5Subset') and name in ('Dynamic','Control')) or (manifest['stage'] in ('B3Subset','B4Subset','B5Subset') and name=='ControlClient') else ([{'name':'advapi32','platform':'Windows','link_only':True}] if manifest['stage'] in ('B3Subset','B4Subset','B5Subset') and name=='Adapter::LocalIPC' else [])
        require(target.get('system_dependencies',[])==system,f'{name} 系统链接依赖不符')
        require(target['public_compile_features']==['cxx_std_20'],f'{name} 公开编译条件漂移')
        expected_options=FOUNDATION_PUBLIC_OPTIONS if name=='Foundation' and manifest['targets']['Foundation']['implementation']=='Implemented' else ['$<$<CXX_COMPILER_ID:MSVC>:/utf-8>']
        require(target['public_compile_options']==expected_options,f'{name} 公开编码/异常选项漂移')
        definitions=target.get('public_compile_definitions', [] if manifest['stage']!='CoreContracts' else None)
        require(definitions==[],f'{name} 公开宏定义漂移')
        require(target['api_classification'] in ('experimental','stable','detail'),f'{name} 分类无效')
        if target['api_classification']=='stable':
            require(not any(targets[d]['api_classification']!='stable' for d in closure if d in targets),
                    f'{name} stable 依赖非 stable')
        if actual_graph is not None and name in selected:
            projected_cpu = manifest['stage'] in ('B4Subset','B5Subset') and build_components=='B3Subset' and name=='Adapter::CpuPool'
            require(actual_graph.get(name,{}).get('kind')==('INTERFACE_LIBRARY' if projected_cpu else target['kind']),f'{name} 实际 CMake 类型不符')
            require(actual_graph.get(name,{}).get('system_dependencies',[])==system,f'{name} 实际系统链接依赖不符')
            require(actual_graph.get(name,{}).get('dependencies')==target['dependencies'],f'{name} 实际 CMake 依赖不符')
            require(actual_graph.get(name,{}).get('implementation')==('ContractBaseline' if projected_cpu else target['implementation']),f'{name} 实际目标能力声明不符')
            require(actual_graph.get(name,{}).get('external_dependencies',[])==target.get('external_dependencies',[]),f'{name} 实际第三方公开依赖不符')
            require(actual_graph.get(name,{}).get('compile_features')==target['public_compile_features'],f'{name} 实际公开编译要求漂移')
            require(actual_graph.get(name,{}).get('compile_options')==target['public_compile_options'],f'{name} 实际公开选项漂移')
            actual_definitions=actual_graph.get(name,{}).get('compile_definitions', [] if manifest['stage']!='CoreContracts' else None)
            require(actual_definitions==definitions and actual_definitions is not None,f'{name} 实际公开宏定义漂移')
    if actual_graph is not None:require(set(actual_graph)==selected,'实际 CMake 目标集合漂移')
    if 'Runtime' in targets:
        require(transitive_dependencies('Runtime',targets)=={'CoreContracts','Foundation'},'Runtime 必须保持最小闭包')
    if 'ControlClient' in targets:
        require(not (transitive_dependencies('ControlClient',targets)&{'Runtime','Control','Workspace'}),'薄客户端不能依赖服务端')
    require(manifest['required_frontends']==['CLI','Plan','ClientSDK'],'首发必需前端不能扩展到 DSL/REPL')
    require(manifest['optional_frontends']==['DSL','REPL'],'DSL/REPL 必须保持可选')
    require(manifest['frozen_previous_release']['exists'] is False,'没有上一正式 SDK，不得伪造兼容结果')
    require(manifest['planned_executables']=={'ock':{'dependencies':['ControlClient','Adapter::LocalIPC'],'external_dependencies':['CLI11'],'owner_task':'D2.06','implementation':('B3Subset' if manifest['stage'] in ('B3Subset','B4Subset','B5Subset') else 'Planned')}},'CLI 仅登记 D2.06 依赖，不能提前构建假 Host')
    if manifest['stage'] in ('NativeSubset','B2Subset','B3Subset','B4Subset','B5Subset'):
        require(manifest.get('implementation_includes') == NATIVE_IMPLEMENTATION_INCLUDES, '模板实现包含边漂移')
        runtime_headers=[h['path'] for h in manifest['headers'] if h['target']=='Runtime']
        expected_runtime=NATIVE_RUNTIME_HEADERS | ({'packages/runtime/include/ock/runtime/scheduler.hpp','packages/runtime/include/ock/runtime/resources.hpp'} if manifest['stage'] in ('B4Subset','B5Subset') else set())
        require(set(runtime_headers)==expected_runtime and len(runtime_headers)==len(expected_runtime), 'Runtime安装头集合与受审阶段不符')
    listed=[]
    for header in manifest['headers']:
        path=ROOT/header['path'];listed.append(path.resolve())
        require(path.is_file(),f'公开头缺失：{path}')
        require(header['target'] in targets,'公开头所有者错误')
        classification = 'detail' if '/detail/' in header['path'] and header['target']=='Runtime' else 'experimental'
        require(header['classification']==classification,'头分类不符或提前承诺 Stable')
        if path.is_file():
            require(hashlib.sha256(path.read_bytes()).hexdigest()==header['sha256'],f'公开头声明/字节变化需要审查：{path}')
            errors.extend(validate_include(header['target'],path.read_text(encoding='utf-8'),manifest,header['classification']!='detail',header['path']))
    header_suffixes={'.h','.hpp','.hh','.hxx','.inl','.ipp'}
    installed={p.resolve() for p in (ROOT/'packages').rglob('*') if p.suffix in header_suffixes and 'include' in p.parts}
    require(set(listed)==installed and len(listed)==len(set(listed)),'公开头增加/删除/重复未登记')
    for candidate in manifest['candidate_headers']:
        require(candidate['target'] in targets and candidate['status']=='Planned','候选头状态不正确')
        require(not (ROOT/candidate['path']).exists(),'候选头已实现时必须转入受审查公开集合')
    for path in (ROOT/'packages').rglob('*'):
        if path.suffix not in header_suffixes|{'.cpp','.cc','.cxx'}:continue
        relative=path.relative_to(ROOT).as_posix()
        owners=[name for name,t in targets.items() if any(relative.startswith(root+'/') for root in t.get('source_roots',[t['include_root'].split('/include')[0]]))]
        if not owners:errors.append(f'源码没有组件所有者：{relative}');continue
        owner=max(owners,key=lambda name:len(targets[name]['include_root']))
        errors.extend(validate_include(owner,path.read_text(encoding='utf-8'),manifest,'/include/' in relative and '/detail/' not in relative,relative))
    return errors

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--graph',type=Path)
    args=parser.parse_args()
    try:
        graph=None if args.graph is None else json.loads(args.graph.read_text(encoding='utf-8'))
        manifest=load_manifest();errors=validate_manifest(manifest,None if graph is None else graph['targets'],
            'B3Subset' if graph is None else graph.get('build_components','B3Subset'))
    except (OSError,ValueError,KeyError,TypeError) as exc:
        errors=[str(exc)]
    print(json.dumps({'check_id':'CHECK.D0.02.architecture','errors':errors,'scope':'合同目标/公开表面静态检查；无Runtime运行或授权实现'},ensure_ascii=False,indent=2))
    return bool(errors)

if __name__=='__main__':sys.exit(main())

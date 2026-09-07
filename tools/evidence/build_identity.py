"""从实际 CMakeCache 与生成的工具链产物核对构建配置，而非复述 manifest。"""
import json
import re
from pathlib import Path

def option(argv, name):
    values=[]
    for i,part in enumerate(argv):
        if part == name and i+1 < len(argv): values.append(argv[i+1])
        elif part.startswith(name+'='): values.append(part.split('=',1)[1])
    return values[-1] if values else None

def observe(spec, source_root, read_build, read_source):
    errors=[]
    def need(ok,message):
        if not ok: errors.append('build identity: '+message)
    cache_path=spec['build_dir']+'/CMakeCache.txt'
    text=read_build(cache_path).decode('utf-8-sig')
    cache={}
    for line in text.splitlines():
        match=re.fullmatch(r'([^#/][^:]*):[^=]+=(.*)',line)
        if match: cache[match[1]]=match[2]
    keys=('CMAKE_GENERATOR','CMAKE_GENERATOR_PLATFORM','CMAKE_GENERATOR_TOOLSET','CMAKE_BUILD_TYPE','CMAKE_CONFIGURATION_TYPES','CMAKE_HOME_DIRECTORY','CMAKE_CXX_COMPILER','CMAKE_C_COMPILER')
    observed={'cache':{key:cache[key] for key in keys if key in cache},'configuration':spec['configuration']}
    need(Path(cache.get('CMAKE_HOME_DIRECTORY','')).resolve()==Path(source_root).resolve(),'source directory differs from configured source')
    configurations=cache.get('CMAKE_CONFIGURATION_TYPES','').split(';')
    if any(configurations):
        need(spec['configuration'] in configurations,'requested configuration not generated')
        actual=option(spec['build'],'--config')
        preset=option(spec['build'],'--preset')
        if preset:
            presets=json.loads(read_source('CMakePresets.json'))
            matches=[p for p in presets['buildPresets'] if p['name']==preset]
            need(len(matches)==1,'build preset absent or duplicate')
            if len(matches)==1:
                actual=actual or matches[0].get('configuration')
                need(matches[0].get('configurePreset')==spec['preset'],'build preset uses another configure preset')
        need(actual==spec['configuration'],'actual build configuration differs from reported configuration')
    else:
        need(cache.get('CMAKE_BUILD_TYPE')==spec['configuration'],'single-config CMake mode differs from reported configuration')
    if spec['preset'] is not None:
        need(option(spec['configure'],'--preset')==spec['preset'],'configure preset differs from reported preset')
    if spec.get('toolchain_artifact'):
        actual=json.loads(read_build(spec['toolchain_artifact']))
        lock=json.loads(read_source(spec['dependency_lock']))['toolchain']
        for key in ('compiler','compiler_version','generator','toolset','windows_sdk','cmake_version','cxx_standard'):
            need(actual.get(key)==lock[key],'actual toolchain differs from lock: '+key)
        need(actual['configuration']==spec['configuration'],'toolchain artifact configuration differs')
        need(actual['crt']==('MultiThreadedDebugDLL' if spec['configuration']=='Debug' else 'MultiThreadedDLL'),'CRT configuration differs')
        need(actual['asan_requested']==('ON' if spec.get('asan',False) else 'OFF'),'sanitizer mode differs')
        observed['toolchain']=actual
    elif cache.get('CMAKE_CXX_COMPILER') or cache.get('CMAKE_C_COMPILER'):
        errors.append('build identity: compiled project requires actual toolchain artifact')
    return observed,errors

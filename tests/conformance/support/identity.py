"""共同资格规则；描述符不能删共同项，能力也不能替代实际行为。"""
def descriptor_errors(descriptor, actual, capabilities, kinds, required=(), exact_capabilities=False):
    if set(descriptor) != set(actual):
        return ['descriptor cannot override or waive common cases']
    errors=[]
    for key in ('factory','implementation_sha256','port_contract_version'):
        if descriptor[key]!=actual[key]:errors.append(key+' mismatch')
    caps=descriptor['capabilities']
    if not isinstance(caps,dict) or set(caps)!=set(capabilities) or any(type(v) is not bool for v in caps.values()):
        errors.append('capability descriptor incomplete')
    elif exact_capabilities and caps!=actual['capabilities']:
        errors.append('actual factory capabilities differ')
    if descriptor['kind'] not in kinds:errors.append('unsupported fixture kind')
    for capability in required:
        if not isinstance(caps,dict) or caps.get(capability) is not True:errors.append('profile requires capability: '+capability)
    return errors

def complete_common(rows, required):
    names=[row.get('id') for row in rows]
    return len(names)==len(set(names))==len(required) and set(names)==set(required) and all(row.get('status')=='Passed' for row in rows)

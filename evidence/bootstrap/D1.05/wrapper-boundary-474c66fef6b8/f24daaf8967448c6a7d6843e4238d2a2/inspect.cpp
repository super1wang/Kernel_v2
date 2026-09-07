#include "examples/native_service/value.hpp"
#include "packages/runtime/invocation/invocation.hpp"
using namespace ock::contracts;
using namespace ock::runtime;
using namespace invocation;
using Value = native_service::Value;
void rejected(const registry::Catalog& c, const OperationKey& k) { (void)NativeAccess::inspect(c,k,{},Shape::Read,CppTypeToken::of<Value>(),CppTypeToken::of<Value>()); }

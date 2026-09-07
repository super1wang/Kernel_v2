#include "examples/native_service/value.hpp"
#include "packages/runtime/invocation/invocation.hpp"
using namespace ock::contracts;
using namespace ock::runtime;
using namespace invocation;
using Value = native_service::Value;
void accepted(NativeBound<Value,Value>& b, const Value& v, const InvokeOptions& o) { (void)b.invoke(v,o); }

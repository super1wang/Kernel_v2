#include "examples/native_service/value.hpp"
#include "packages/runtime/invocation/invocation.hpp"
using namespace ock::contracts;
using namespace ock::runtime;
using namespace invocation;
using Value = native_service::Value;
void rejected(const registry::Catalog& c, const NativeEntry& e, WorkContext& w) { NativeAccess::dispatch(c,e,nullptr,w,nullptr); }
#include "test_support.hpp"
#include "packages/runtime/registry/registry.hpp"
using namespace ock::runtime::registry;
class Executor final : public ExecutorPort {
public: Result<void> submit(std::unique_ptr<ReadyWork>) override { return {}; }
};
ModuleInput module(const char* n="module") {
  ModuleManifest m{name(n),ver()};
  m.executors.push_back(name("test"));
  return {m, {}, {}, {}, {}, {{name("test"),name("app"),false,false,std::make_shared<Executor>()}}, {}};
}
OperationOptions options(const char* n="module") { return {{},{},{name(n),name("test")},{},false}; }
std::unique_ptr<RegistrationBatch> batch(BatchBudget budget={}) { auto b=RegistrationBatch::create(budget); CHECK(b); return std::move(*b); }
void typed_binding_success() {
  auto b=batch(); auto m=module(); m.manifest.operations.push_back(key());
  m.register_operations=[](Registrar& r){ CHECK(r.compute(compute_handler,input(AtomicMode::PureCompute),options())); };
  CHECK(b->add(m)); auto p=b->publish(); CHECK(p);
  auto bound=bind<int,int>(*p,key(),{},Shape::Read); CHECK(bound); CHECK(validate_inline_args(*bound,2)); CHECK(!validate_inline_args(*bound,-1));
}
void ignored_errors_sticky() {
  auto b=batch(); auto m=module(); m.manifest.operations.push_back(key());
  m.register_operations=[](Registrar& r){ (void)r.compute(compute_handler,input(),options()); (void)r.compute(compute_handler,input(AtomicMode::PureCompute),options()); };
  CHECK(b->add(m)); CHECK(!b->publish()); CHECK(b->failed()); CHECK(!b->errors().empty());
}
void module_cycles_rejected() {
  auto b=batch(); auto m=module(); bool called=false;
  m.manifest.dependencies.push_back({m.manifest.name,ver()}); m.register_operations=[&](Registrar&){called=true;};
  CHECK(b->add(m)); CHECK(!b->publish()); CHECK(!called);
}
void publish_once_freeze() {
  auto b=batch(); CHECK(b->add(module())); auto p=b->publish(); CHECK(p); CHECK(!b->publish()); CHECK(!b->add(module("later"))); CHECK((*p)->size()==0);
}
int main(int argc,char** argv) {
  std::map<std::string,void(*)()> cases{{"T05.registration.typed_binding_success",typed_binding_success},{"T02.registration.ignored_errors_sticky",ignored_errors_sticky},{"T02.registration.module_cycles_rejected",module_cycles_rejected},{"T05.registration.publish_once_freeze",publish_once_freeze}};
  if(argc==2 && std::string(argv[1])=="--list") {for(auto& [n,f]:cases)std::cout<<n<<'\n';return 0;}
  try {CHECK(argc==2); auto i=cases.find(argv[1]); CHECK(i!=cases.end()); i->second(); return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

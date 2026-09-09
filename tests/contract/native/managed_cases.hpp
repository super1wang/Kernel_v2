#pragma once
#include "fixtures.hpp"
#include "packages/runtime/executions/invocation_access.hpp"
#include <ock/runtime/resources.hpp>

namespace native_test {
using ManagedAccess=executions::detail::InvocationAccess;
inline void managed_admission() {
  Env e(false,compute,{},[](registry::ModuleInput& m) {
    m.register_operations=[](registry::Registrar& r) {
      auto definition=native_definition();definition.execution.inline_safe=false;
      CHECK(r.compute(compute,definition,native_options()));
    };
  });
  auto bound=e.bind();CHECK(bound);entered=0;
  CHECK(!ManagedAccess::prepare(*bound,-1,e.options()));CHECK(entered==0);
  CHECK(ManagedAccess::prepare(*bound,2,e.options()));CHECK(entered==0);
  // Submit 的准备不需要在 worker；真正开始只允许声明 affinity 的 worker。
  CHECK(std::holds_alternative<Rejected>(ManagedAccess::run(*bound,2,e.options())));
  CHECK(std::holds_alternative<Rejected>(bound->invoke(2,e.options())));
  e.threads->role=ThreadRole::Worker;e.threads->allow=false;e.threads->any_thread=true;
  bool passed=false;
  std::thread worker([&] {passed=result(ManagedAccess::run(*bound,2,e.options()))==4;});
  worker.join();CHECK(passed);CHECK(entered==1);
  CHECK(std::holds_alternative<Rejected>(bound->invoke(2,e.options())));
  e.threads->affinity=name("other");
  CHECK(std::holds_alternative<Rejected>(ManagedAccess::run(*bound,2,e.options())));
  e.threads->affinity=name("app");
  CHECK(ManagedAccess::prepare(*bound,2,e.options()));
  auto denied=policy_test::configuration().principals[0];denied.rules.clear();
  CHECK(e.policy.assembly.administration->replace_principal_policy(denied));
  CHECK(std::holds_alternative<Rejected>(ManagedAccess::run(*bound,2,e.options())));
  CHECK(entered==1); // 入队前的成功许可不替代开始时当前许可。
}
inline Result<int> managed_resource_handler(const int& args,WorkContext& context) {
  ++entered;
  if(context.granted_resources().size()!=1 || !context.granted_resources()[0])
    return make_unexpected(invocation_error(InvocationErrc::ResourceUnavailable));
  return args+2;
}
inline void managed_resources() {
  Env e(false,managed_resource_handler,{},[](registry::ModuleInput& m) {
    m.manifest.resources.push_back(name("declared"));
    m.manifest.required_resources.push_back({name("native"),name("declared")});
    m.resources.push_back({name("declared"),std::make_shared<ResourceLease>()});
    m.register_operations=[](registry::Registrar& r) {
      auto options=native_options();options.resources.push_back({name("native"),name("declared")});
      CHECK(r.compute(managed_resource_handler,native_definition(),options));
      options.resources.clear(); // 发布材料拥有声明，与注册调用栈独立。
    };
  });
  auto bound=e.bind();CHECK(bound);auto material=ManagedAccess::material(*bound);
  CHECK(material->caller==e.policy.caller);
  CHECK(material->entry->resources.size()==1);
  CHECK((material->entry->resources[0]==registry::ResourceRef{name("native"),name("declared")}));
  CHECK(ManagedAccess::prepare(*bound,2,e.options()));entered=0;
  CHECK(std::holds_alternative<Rejected>(bound->invoke(2,e.options())));
  e.threads->role=ThreadRole::Worker;
  CHECK(std::holds_alternative<Rejected>(ManagedAccess::run(*bound,2,e.options())));
  auto manager=resources::ResourceManager::create({{"actual.slot",1,false}});CHECK(manager);
  std::array claims{resources::Claim{"actual.slot",resources::Mode::Exclusive,1}};
  auto acquired=(*manager)->acquire(claims,resources::Phase::Compute);CHECK(acquired&&acquired->lease);
  const ResourceLease* view=acquired->lease.get();
  CHECK(result(ManagedAccess::run(*bound,2,e.options(),std::span(&view,1)))==4);
  CHECK(entered==1&&(*manager)->used("actual.slot")==1);
  std::stop_source stop;auto options=e.options();options.stop=stop.get_token();stop.request_stop();
  CHECK(std::holds_alternative<Rejected>(ManagedAccess::run(*bound,2,options,std::span(&view,1))));
  CHECK(entered==1&&(*manager)->used("actual.slot")==1);
  acquired->lease.reset();CHECK((*manager)->used("actual.slot")==0);
}
}

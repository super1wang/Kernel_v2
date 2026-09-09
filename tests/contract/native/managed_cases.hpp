#pragma once
#include "fixtures.hpp"
#include "packages/runtime/executions/invocation_access.hpp"
#include "packages/runtime/executions/invocation_record.hpp"
#include <ock/runtime/resources.hpp>

namespace native_test {
struct OwnedRecordInput {std::shared_ptr<const std::vector<int>> values;};
}
namespace ock::contracts {
template<> struct TypeContract<native_test::OwnedRecordInput> {
  static TypeIdentity identity() {return {*Name::parse("owned.record.input"),*OperationVersion::parse("1.0.0",5),{}};}
  static constexpr auto async_ownership=AsyncOwnership::Owning;
  static Result<void> validate(const native_test::OwnedRecordInput& input) {
    return input.values ? Result<void>{} : reject(ContractsErrc::InvalidFact);
  }
};
}
namespace native_test {
using ManagedAccess=executions::detail::InvocationAccess;
inline void managed_record() {
  using Record=executions::detail::InvocationRecord<int,int>;
  Record::Policy policy{sizeof(int),sizeof(InvokeReply<int>),
      [](const int&)->Result<std::size_t>{return sizeof(int);},
      [](const InvokeReply<int>&)->Result<std::size_t>{return sizeof(InvokeReply<int>);}};
  Env e;auto bound=e.bind();CHECK(bound);entered=0;
  auto first=Record::create(*bound,2,e.options(),policy);CHECK(first);
  CHECK(!(*first)->reply());CHECK(entered==0);
  // 调用槽在接受前预留，第二次明确拒绝；开始时复用本槽，不再次 Busy。
  CHECK(!Record::create(*bound,3,e.options(),policy));
  CHECK(std::holds_alternative<Rejected>(bound->invoke(3,e.options())));
  e.threads->role=ThreadRole::Worker;
  CHECK((*first)->run_once({}));CHECK((*first)->reply());
  CHECK(result(*(*first)->reply())==4&&entered==1);
  CHECK(!(*first)->run_once({}));
  CHECK(!(*first)->reject_before_start(invocation_error(InvocationErrc::Cancelled)));
  auto second=Record::create(*bound,3,e.options(),policy);CHECK(second);
  CHECK((*second)->reject_before_start(invocation_error(InvocationErrc::Cancelled)));
  CHECK(!(*second)->run_once({}));CHECK(entered==1);
  CHECK(std::holds_alternative<Rejected>(*(*second)->reply()));
  policy.input_limit=1;CHECK(!Record::create(*bound,3,e.options(),policy));
  policy.input_limit=sizeof(int);policy.reply_limit=1;
  auto limited=Record::create(*bound,3,e.options(),policy);CHECK(limited);
  CHECK((*limited)->run_once({}));CHECK(std::holds_alternative<Rejected>(*(*limited)->reply()));
  CHECK(entered==2);
  std::stop_source original;auto options=e.options();options.stop=original.get_token();
  auto cancelled=Record::create(*bound,3,options,policy);CHECK(cancelled);original.request_stop();
  CHECK((*cancelled)->run_once({}));CHECK(entered==2);
}
inline Result<int> owned_record_handler(const OwnedRecordInput& input,WorkContext&) {
  return static_cast<int>(input.values->size());
}
inline void managed_record_ownership() {
  using Record=executions::detail::InvocationRecord<OwnedRecordInput,int>;
  Env e(false,compute,{},[](registry::ModuleInput& m) {
    m.register_operations=[](registry::Registrar& r){CHECK(r.compute(owned_record_handler,native_definition(),native_options()));};
  });
  auto projection=+[](const OwnedRecordInput&,std::span<foundation::ObjectId> out) noexcept -> Result<std::size_t> {
    return targets(0,out);
  };
  auto bound=e.engine->bind<OwnedRecordInput,int>(key(),{},Shape::Read,e.policy.caller,
      std::array{policy_test::target()},projection,name("owned.record"));CHECK(bound);
  Record::Policy policy{1024,sizeof(InvokeReply<int>),
      [](const OwnedRecordInput& input)->Result<std::size_t>{return sizeof(input)+input.values->capacity()*sizeof(int);},
      [](const InvokeReply<int>&)->Result<std::size_t>{return sizeof(InvokeReply<int>);}};
  CHECK(!Record::create(*bound,OwnedRecordInput{},e.options(),policy));
  auto input=std::make_shared<const std::vector<int>>(std::initializer_list<int>{1,2,3});
  std::weak_ptr<const std::vector<int>> weak=input;
  auto record=Record::create(*bound,OwnedRecordInput{input},e.options(),policy);CHECK(record);
  input.reset();CHECK(!weak.expired());CHECK((*record)->input_bytes()>sizeof(OwnedRecordInput));
  e.threads->role=ThreadRole::Worker;e.threads->any_thread=true;
  std::thread worker([&]{CHECK((*record)->run_once({}));});worker.join();
  CHECK(weak.expired());CHECK(result(*(*record)->reply())==3);
  auto second=std::make_shared<const std::vector<int>>(8,1);weak=second;
  auto cancelled=Record::create(*bound,OwnedRecordInput{second},e.options(),policy);CHECK(cancelled);
  second.reset();CHECK(!weak.expired());
  CHECK((*cancelled)->reject_before_start(invocation_error(InvocationErrc::Cancelled)));
  CHECK(weak.expired());
}
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

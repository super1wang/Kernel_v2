#pragma once
#include "fixtures.hpp"
#include <barrier>
#include <latch>
namespace native_test {
struct Args { int value=0; foundation::ObjectId target=policy_test::target(); int mode=0; };
struct Value { int value; int mode; };
inline std::atomic<unsigned> input_checks=0,output_checks=0;
inline bool different_identity=false;
}
namespace ock::contracts {
template<> struct TypeContract<native_test::Args> {
  static TypeIdentity identity() { return {name("native.args"),ver(native_test::different_identity?"2.0.0":"1.0.0"),{}}; }
  static Result<void> validate(const native_test::Args& a) {
    ++native_test::input_checks;
    if(a.mode==1) throw std::runtime_error("input validator");
    if(a.mode==2) throw std::bad_alloc{};
    return a.value>=0 && a.value<=100 && a.mode>=0 && a.mode<=4 && !a.target.empty()
        ?Result<void>{}:reject(ContractsErrc::InvalidContract);
  }
};
template<> struct TypeContract<native_test::Value> {
  static TypeIdentity identity() { return {name("native.value"),ver(),{}}; }
  static Result<void> validate(const native_test::Value& r) {
    ++native_test::output_checks;
    if(r.mode==3) throw std::runtime_error("output validator");
    if(r.mode==4) throw std::bad_alloc{};
    return r.value>=0?Result<void>{}:reject(ContractsErrc::InvalidContract);
  }
};
}
namespace native_test {
inline Result<std::size_t> value_targets(const Args& a,std::span<foundation::ObjectId> out) noexcept {
  if(out.empty())return make_unexpected(invocation_error(InvocationErrc::InvalidInput));
  out[0]=a.target;return 1;
}
inline Result<Value> value_compute(const Args& a,WorkContext&) { ++entered;return Value{a.value==99?-1:a.value+2,a.mode}; }
inline DefinitionInput native_definition(AtomicMode atomic=AtomicMode::PureCompute) {
  return {key(),{},{true,false,false,name("test"),name("app")},atomic,{name("allow")},"native"};
}
inline registry::OperationOptions native_options() { return {{},{},{name("native"),name("test")},{},false}; }
inline void value_registration(registry::ModuleInput& module) {
  module.register_operations=[](registry::Registrar& registrar){CHECK(registrar.compute(value_compute,native_definition(),native_options()));};
}
inline auto bind_value(Env& e, foundation::ObjectId t=policy_test::target()) {
  return e.engine->bind<Args,Value>(key(),{},Shape::Read,e.policy.caller,std::array{t},value_targets,name("value.call"));
}
inline void value_invalid_input() {
  Env e(false,compute,{},value_registration);auto b=bind_value(e);CHECK(b);entered=0;
  for(auto a:{Args{-1},Args{101},Args{1,{},0},Args{1,policy_test::target(),-1}})
    CHECK(std::holds_alternative<Rejected>(b->invoke(a,e.options())));
  CHECK(entered==0);
  for(int n:{0,100})CHECK(std::holds_alternative<Completed<Value>>(b->invoke(Args{n},e.options())));
  CHECK(entered==2);
}
inline void validation_exception() {
  Env e(false,compute,{},value_registration);auto b=bind_value(e);CHECK(b);entered=0;
  for(int mode:{1,2}) {
    auto r=b->invoke({1,policy_test::target(),mode},e.options());CHECK(std::holds_alternative<Rejected>(r));
  }
  CHECK(entered==0);CHECK(std::holds_alternative<Completed<Value>>(b->invoke({1},e.options())));
}
inline void invalid_output() {
  Env e(false,compute,{},value_registration);auto b=bind_value(e);CHECK(b);entered=0;
  for(auto a:{Args{99},Args{1,policy_test::target(),3},Args{1,policy_test::target(),4}}) {
    auto r=b->invoke(a,e.options());CHECK(std::holds_alternative<Completed<Value>>(r));
    CHECK(std::holds_alternative<FailedBeforeApply>(std::get<Completed<Value>>(r).outcome.value()));
  }
  CHECK(entered==3);CHECK(std::holds_alternative<ReadCompleted<Value>>(
    std::get<Completed<Value>>(b->invoke({1},e.options())).outcome.value()));
}
inline void exact_binding() {
  Env e(false,compute,{},value_registration);auto b=bind_value(e);CHECK(b);
  auto wrong=key();wrong.version=ver("2.0.0");
  CHECK(!e.engine->bind<Args,Value>(wrong,{},Shape::Read,e.policy.caller,std::array{policy_test::target()},value_targets,name("exact")));
  auto digest=ContractDigest{};digest.bytes[0]=1;
  CHECK(!e.engine->bind<Args,Value>(key(),digest,Shape::Read,e.policy.caller,std::array{policy_test::target()},value_targets,name("exact")));
  CHECK(!e.engine->bind<Args,Value>(key(),{},Shape::Lifecycle,e.policy.caller,std::array{policy_test::target()},value_targets,name("exact")));
  CHECK(!e.bind()); // 同一快照的 A/R C++ token 不匹配。
  different_identity=true;CHECK(!bind_value(e));different_identity=false;
  CHECK(bind_value(e));
  auto moved=std::move(*b);CHECK(std::holds_alternative<Rejected>(b->invoke({1},e.options())));
  CHECK(std::holds_alternative<Completed<Value>>(moved.invoke({1},e.options())));
}
inline void target_binding() {
  Env e(false,compute,{},value_registration);auto b=bind_value(e);CHECK(b);entered=0;
  CHECK(std::holds_alternative<Rejected>(b->invoke({1,policy_test::target(2)},e.options())));CHECK(entered==0);
  CHECK(!bind_value(e,policy_test::target(99)));
  CHECK(std::holds_alternative<Completed<Value>>(b->invoke({1},e.options())));
}
inline void thread_origin() {
  Env e;auto b=e.bind();CHECK(b);entered=0;bool rejected=false;
  std::thread worker([&]{rejected=std::holds_alternative<Rejected>(b->invoke(1,e.options()));});worker.join();
  CHECK(rejected&&entered==0);CHECK(result(b->invoke(1,e.options()))==3);
}
inline void thread_modes() {
  Env e;auto b=e.bind();CHECK(b);
  for(auto role:{ThreadRole::Worker,ThreadRole::Control,ThreadRole::Domain,ThreadRole::Database}) {
    e.threads->role=role;e.threads->allow=false;
    CHECK(std::holds_alternative<Rejected>(b->invoke(1,e.options())));
  }
  e.threads->allow=true;e.threads->role=static_cast<ThreadRole>(100);
  CHECK(std::holds_alternative<Rejected>(b->invoke(1,e.options())));
  e.threads->role=ThreadRole::Application;e.threads->affinity=name("wrong");
  CHECK(std::holds_alternative<Rejected>(b->invoke(1,e.options())));
  e.threads->affinity=name("app");CHECK(result(b->invoke(1,e.options()))==3);
  for(int mode:{0,1,2}) {
    Env restricted(false,compute,{},[mode](registry::ModuleInput& m){
      m.executors[0].async_dispatch=true;m.executors[0].external_wait=true;
      m.register_operations=[mode](registry::Registrar& r){auto d=native_definition();
        d.execution.inline_safe=mode!=0;d.execution.requires_async_dispatch=mode==1;d.execution.requires_external_wait=mode==2;
        CHECK(r.compute(compute,d,native_options()));};});
    auto blocked=restricted.bind();CHECK(blocked);CHECK(std::holds_alternative<Rejected>(blocked->invoke(1,restricted.options())));
  }
}
inline Result<int> budget_compute(const int&,WorkContext& work) {
  ++entered;CHECK(work.trace_name()==name("native.call"));CHECK(work.granted_resources().empty());
  CHECK(work.charge(60));CHECK(!work.charge(60));return 1;
}
inline void cancel_and_budget() {
  Env e(false,budget_compute);auto b=e.bind();CHECK(b);entered=0;
  auto o=e.options();std::stop_source cancelled;cancelled.request_stop();o.stop=cancelled.get_token();
  CHECK(std::holds_alternative<Rejected>(b->invoke(0,o)));
  for(auto limit:{0ULL,1025ULL}){o=e.options();o.work_limit=limit;CHECK(std::holds_alternative<Rejected>(b->invoke(0,o)));}
  CHECK(entered==0);CHECK(result(b->invoke(0,e.options()))==1);CHECK(result(b->invoke(0,e.options()))==1);
}
inline std::function<void()> entered_hook;
inline Result<int> callback_compute(const int&,WorkContext&) {++entered;if(entered_hook)entered_hook();return 1;}
inline void reentrant_and_concurrent() {
  binding_capacity();
  Env e(false,callback_compute);auto b=e.bind();CHECK(b);
  entered_hook=[&]{CHECK(std::holds_alternative<Rejected>(b->invoke(0,e.options())));};
  CHECK(result(b->invoke(0,e.options()))==1);entered_hook={};
  e.threads->any_thread=true;
  std::latch in_handler(1),release(1);
  entered_hook=[&]{in_handler.count_down();release.wait();};
  std::thread first([&]{CHECK(result(b->invoke(0,e.options()))==1);});
  in_handler.wait();CHECK(std::holds_alternative<Rejected>(b->invoke(0,e.options())));release.count_down();first.join();entered_hook={};
  CHECK(result(b->invoke(0,e.options()))==1);
}
}

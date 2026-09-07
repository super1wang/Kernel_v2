#pragma once
#include "shape_cases.hpp"
namespace native_test {
inline int move_number=0,throw_on_move=0;
struct Moving {
  int value;
  explicit Moving(int n):value(n){}
  Moving(const Moving&)=delete;
  Moving& operator=(const Moving&)=delete;
  Moving(Moving&& other):value(other.value){if(++move_number==throw_on_move)throw std::runtime_error("result move");}
  Moving& operator=(Moving&& other){value=other.value;if(++move_number==throw_on_move)throw std::runtime_error("result move");return *this;}
};
}
namespace ock::contracts {
template<> struct TypeContract<native_test::Moving> {
  static TypeIdentity identity(){return {name("native.moving"),ver(),{}};}
  static Result<void> validate(const native_test::Moving& r){return r.value>=0?Result<void>{}:reject(ContractsErrc::InvalidContract);}
};
}
namespace native_test {
inline Result<Moving> moving_handler(const int& n,WorkContext&){++entered;return Moving{n};}
inline void return_move_observation() {
    throw_on_move=0;entered=0;
    Env e(false,compute,{},[](registry::ModuleInput& m){m.register_operations=[](registry::Registrar& r){CHECK(r.compute(moving_handler,native_definition(),native_options()));};});
    auto b=e.engine->bind<int,Moving>(key(),{},Shape::Read,e.policy.caller,std::array{policy_test::target()},targets,name("moving"));CHECK(!b);
    CHECK(b.error().code()==invocation_error(InvocationErrc::InvalidBinding).code());CHECK(entered==0);
    static_assert(!invocation::detail::transport_safe<Moving>);
    static_assert(invocation::detail::transport_safe<int>);
    Env good;auto control=good.bind();CHECK(control);CHECK(result(control->invoke(1,good.options()))==3);
}
inline int throwing_transport_probe(){
  std::set_terminate([]{std::fputs("locked_result_noexcept_transport_terminated\n",stderr);std::fflush(stderr);std::_Exit(86);});
  throw_on_move=0;Result<Moving> value{Moving{1}};
  move_number=0;throw_on_move=1;
  auto moved=std::move(value);(void)moved;
  return 0;
}
inline policy::TimePoint observed_deadline;
inline std::stop_source* stop_during_handler=nullptr;
inline Result<int> observe_context(const int&,WorkContext& work){
  ++entered;observed_deadline=work.deadline();CHECK(!work.stop_requested());
  if(stop_during_handler){stop_during_handler->request_stop();CHECK(work.stop_requested());}
  return 1;
}
inline void deadline_and_stop_context(){
  Env e(false,observe_context);auto b=e.bind();CHECK(b);
  auto options=e.options();options.deadline=e.policy.clock->now()+std::chrono::hours(2);
  CHECK(result(b->invoke(0,options))==1);CHECK(observed_deadline==e.policy.auth->identity.deadline);
  options.deadline=e.policy.clock->now()+std::chrono::milliseconds(20);CHECK(result(b->invoke(0,options))==1);CHECK(observed_deadline==options.deadline);
  options.deadline=e.policy.clock->now();entered=0;CHECK(std::holds_alternative<Rejected>(b->invoke(0,options)));CHECK(entered==0);
  std::stop_source source;options=e.options();options.stop=source.get_token();stop_during_handler=&source;
  CHECK(result(b->invoke(0,options))==1);stop_during_handler=nullptr;
}
inline void actual_role_threads(){
  for(auto role:{ThreadRole::Worker,ThreadRole::Control,ThreadRole::Domain,ThreadRole::Database}){
    Env e;auto b=e.bind();CHECK(b);bool allowed=false,denied=false;
    std::thread real([&]{e.threads->owner=std::this_thread::get_id();e.threads->role=role;e.threads->allow=false;
      denied=std::holds_alternative<Rejected>(b->invoke(1,e.options()));e.threads->allow=true;
      allowed=result(b->invoke(1,e.options()))==3;});real.join();CHECK(allowed&&denied);
    CHECK(std::holds_alternative<Rejected>(b->invoke(1,e.options())));
  }
}
inline void multi_slot_isolation(){
  NativeBudget budget;budget.concurrent_calls_per_binding=2;
  Env e(false,budget_compute,budget);e.threads->any_thread=true;auto b=e.bind();CHECK(b);
  std::barrier launch(3);bool one=false,two=false;
  std::thread first([&]{launch.arrive_and_wait();one=result(b->invoke(0,e.options()))==1;});
  std::thread second([&]{launch.arrive_and_wait();two=result(b->invoke(0,e.options()))==1;});
  launch.arrive_and_wait();first.join();second.join();CHECK(one&&two);
}
inline Result<std::size_t> empty_projection(const Args&,std::span<foundation::ObjectId>) noexcept {return 0;}
inline void replace_bound_in_validator(){
  Env e(false,compute,{},value_registration);auto b=bind_value(e);CHECK(b);
  auto other=e.engine->bind<Args,Value>(key(),{},Shape::Read,e.policy.caller,std::array{policy_test::target()},empty_projection,name("replacement"));CHECK(other);
  input_hook=[&]{*b=std::move(*other);};
  auto reply=b->invoke({1},e.options());input_hook={};
  CHECK(std::holds_alternative<Completed<Value>>(reply));
  CHECK(std::holds_alternative<Rejected>(b->invoke({1},e.options())));
}
}

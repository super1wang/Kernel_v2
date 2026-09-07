#pragma once
#include "fixtures.hpp"
namespace native_test {
inline Result<int> business_error(const int& a, WorkContext&) {
  ++entered;
  if (a == 1) throw std::runtime_error("business exception");
  if (a == 2) throw std::bad_alloc{};
  if (a == 3) return 8;
  return make_unexpected(invocation_error(InvocationErrc::InvalidOutput));
}
inline void business_failure() {
  Env e(false,business_error); auto b=e.bind(); CHECK(b); entered=0;
  for(int i=0;i<3;++i) {
    auto reply=b->invoke(i,e.options());
    CHECK(std::holds_alternative<Completed<int>>(reply));
    const auto& outcome=std::get<Completed<int>>(reply).outcome;
    CHECK(std::holds_alternative<FailedBeforeApply>(outcome.value()));
    const auto& failure=std::get<FailedBeforeApply>(outcome.value());
    CHECK(failure.reason.code()==invocation_error(i==0?InvocationErrc::InvalidOutput:
         i==1?InvocationErrc::HandlerException:InvocationErrc::BudgetExceeded).code());
  }
  CHECK(entered==3); CHECK(result(b->invoke(3,e.options()))==8);
}
inline void no_task_path() {
  NativeBudget budget; budget.observation_capacity=2;
  Env e(false,compute,budget); auto b=e.bind(); CHECK(b);
  CHECK(result(b->invoke(1,e.options()))==3);
  CHECK(std::holds_alternative<Rejected>(b->invoke(-1,e.options())));
  CHECK(e.engine->snapshot({}).dropped==0);
  CHECK(result(b->invoke(2,e.options()))==4);
  CHECK(e.engine->snapshot({}).dropped==1);
}
inline void binding_capacity() {
  NativeBudget budget; budget.bindings=1;
  Env e(false,compute,budget);
  {auto first=e.bind();CHECK(first);CHECK(!e.bind());}
  auto replacement=e.bind(); CHECK(replacement);
  CHECK(result(replacement->invoke(1,e.options()))==3);
}
}

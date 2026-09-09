#pragma once
#include "fixtures.hpp"
#include "packages/runtime/executions/execution_table.hpp"
#include "packages/runtime/executions/managed_execution.hpp"
namespace native_test {
struct ManagedInlineExecutor final : ExecutorPort {
  Result<void> submit(std::unique_ptr<ReadyWork> work) override {work->execute();return {};}
};
inline void managed_execution_resource_wait() {
  using Managed=executions::detail::ManagedExecution<int,int>;
  Env env(false,managed_resource_handler,{},[](registry::ModuleInput& m) {
    m.manifest.resources.push_back(name("declared"));
    m.manifest.required_resources.push_back({name("native"),name("declared")});
    m.resources.push_back({name("declared"),std::make_shared<ResourceLease>()});
    m.register_operations=[](registry::Registrar& r) {
      auto options=native_options();options.resources.push_back({name("native"),name("declared")});
      CHECK(r.compute(managed_resource_handler,native_definition(),options));
    };
  });
  env.threads->role=ThreadRole::Worker;auto bound=env.bind();CHECK(bound);entered=0;
  auto executor=std::make_shared<ManagedInlineExecutor>();
  auto s=scheduler::Scheduler::create(executor,{{env.policy.caller->view().description().principal.principal_id}});CHECK(s);
  std::shared_ptr<scheduler::Scheduler> schedule=std::move(*s);
  auto r=resources::ResourceManager::create({{"actual.slot",1,false}});CHECK(r);
  std::shared_ptr<resources::ResourceManager> manager=std::move(*r);
  auto t=executions::detail::ExecutionTable::create({});CHECK(t);
  auto resolver=[](std::span<const registry::ResourceRef> refs)->Result<std::vector<resources::Claim>> {
    CHECK(refs.size()==1&&refs[0].name==name("declared"));
    return std::vector<resources::Claim>{{"actual.slot",resources::Mode::Exclusive,1}};
  };
  std::array claims{resources::Claim{"actual.slot",resources::Mode::Exclusive,1}};
  auto held=manager->acquire(claims,resources::Phase::Compute);CHECK(held&&held->lease);
  ExecutionRef id;id.execution_id.bytes[0]=81;HostIncarnation host;host.bytes[0]=82;
  Managed::Record::Policy policy{4,sizeof(InvokeReply<int>),
      [](const int&)->Result<std::size_t>{return 4;},
      [](const InvokeReply<int>&)->Result<std::size_t>{return sizeof(InvokeReply<int>);}};
  auto execution=Managed::create(*t,schedule,manager,id,host,*bound,9,env.options(),policy,resolver,[]{});CHECK(execution);
  (*execution)->drive();CHECK(schedule->pump()==0&&entered==0);CHECK(manager->snapshot().waiters==1);
  auto summary=(*t)->summary((*execution)->entry());CHECK(summary&&(*summary)->value().phase==ExecutionPhase::WaitingResources);
  held->lease.reset();CHECK((*execution)->pending());(*execution)->drive();CHECK(schedule->pump()==1&&entered==1);
  CHECK(manager->used("actual.slot")==0&&manager->snapshot().waiters==0);
  auto record=std::static_pointer_cast<const Managed::Record>((*execution)->entry()->payload());
  CHECK(record->reply()&&result(*record->reply())==11);
  // 资源等待期间取消必须移除 waiter，释放后的迟到 wake 不再调用业务。
  held=manager->acquire(claims,resources::Phase::Compute);CHECK(held&&held->lease);
  id.execution_id.bytes[0]=83;
  auto cancelled=Managed::create(*t,schedule,manager,id,host,*bound,10,env.options(),policy,resolver,[]{});CHECK(cancelled);
  (*cancelled)->drive();CHECK(manager->snapshot().waiters==1);(*cancelled)->cancel();
  CHECK(manager->snapshot().waiters==0);held->lease.reset();(*cancelled)->drive();schedule->pump();CHECK(entered==1);
  schedule->close();
}
inline void managed_execution_path() {
  using Managed=executions::detail::ManagedExecution<int,int>;
  using Table=executions::detail::ExecutionTable;
  Env env;auto bound=env.bind();CHECK(bound);env.threads->role=ThreadRole::Worker;entered=0;
  auto executor=std::make_shared<ManagedInlineExecutor>();
  auto scheduler_result=scheduler::Scheduler::create(executor,
      {{env.policy.caller->view().description().principal.principal_id}});CHECK(scheduler_result);
  std::shared_ptr<scheduler::Scheduler> schedule=std::move(*scheduler_result);
  auto resources_result=resources::ResourceManager::create({{"test.slot",1,false}});CHECK(resources_result);
  std::shared_ptr<resources::ResourceManager> resources=std::move(*resources_result);
  auto table=Table::create({});CHECK(table);
  Managed::Record::Policy policy{4,sizeof(InvokeReply<int>),
      [](const int&)->Result<std::size_t>{return 4;},
      [](const InvokeReply<int>&)->Result<std::size_t>{return sizeof(InvokeReply<int>);}};
  ExecutionRef id;id.execution_id.bytes[0]=71;HostIncarnation host;host.bytes[0]=72;
  auto resolver=[](std::span<const registry::ResourceRef> refs)->Result<std::vector<resources::Claim>> {
    CHECK(refs.empty());return std::vector<resources::Claim>{};
  };
  auto execution=Managed::create(*table,schedule,resources,id,host,*bound,5,env.options(),policy,resolver,[]{});
  CHECK(execution);CHECK((*execution)->accepted().execution==id);CHECK(entered==0);
  CHECK((*table)->find(id));CHECK(schedule->pump()==0);
  CHECK((*execution)->pending());(*execution)->drive();CHECK(schedule->pump()==1);CHECK(entered==1);
  auto summary=(*table)->summary((*execution)->entry());CHECK(summary);
  CHECK((*summary)->value().phase==ExecutionPhase::Terminal);
  auto record=std::static_pointer_cast<const Managed::Record>((*execution)->entry()->payload());
  CHECK(record->reply()&&result(*record->reply())==7);
  (*execution)->cancel();CHECK(result(*record->reply())==7);
  id.execution_id.bytes[0]=73;
  auto cancelled=Managed::create(*table,schedule,resources,id,host,*bound,8,env.options(),policy,resolver,[]{});
  CHECK(cancelled);(*cancelled)->cancel();(*cancelled)->drive();schedule->pump();CHECK(entered==1);
  summary=(*table)->summary((*cancelled)->entry());CHECK(summary&&(*summary)->value().phase==ExecutionPhase::Terminal);
  CHECK((*summary)->value().fault.has_value());
  schedule->close();id.execution_id.bytes[0]=74;
  CHECK(!Managed::create(*table,schedule,resources,id,host,*bound,8,env.options(),policy,resolver,[]{}));
  CHECK(!(*table)->find(id));CHECK(entered==1);
}
inline void execution_table_ownership() {
  using Table=executions::detail::ExecutionTable;
  auto made=Table::create({3,12,48,1,48});CHECK(made);auto table=*made;
  Env env;
  auto input=[&](unsigned char id) {
    ExecutionRef ref;ref.execution_id.bytes[0]=id;HostIncarnation host;host.bytes[0]=42;
    return SummaryInput{ref,key(),env.policy.caller->view().description().principal,{},
        ExecutionPhase::Queued,host,*ObservationVersion::create(1),{}, {}};
  };
  auto a=table->prepare(input(1),std::make_shared<int>(11),CppTypeToken::of<int>(),4,16);CHECK(a);
  CHECK(!table->find(input(1).execution));CHECK(!table->summary(*a));
  CHECK(table->usage().records==1);
  CHECK(!table->prepare(input(1),std::make_shared<int>(12),CppTypeToken::of<int>(),4,16));
  CHECK(table->usage().records==1);
  CHECK(table->publish(*a));CHECK(!table->publish(*a));CHECK(!table->abandon(*a));
  PhaseConditions conditions{{RequiredRecordState::NotRequired,0,0},{},{},false,false};
  CHECK(!table->transition(*a,ExecutionPhase::Terminal,conditions));
  CHECK(table->transition(*a,ExecutionPhase::Finalizing,conditions));
  CHECK(table->transition(*a,ExecutionPhase::Terminal,conditions));
  CHECK(!table->transition(*a,ExecutionPhase::Running,conditions));
  auto summary=table->summary(*a);CHECK(summary&&(*summary)->value().version.value()==3);
  auto b=table->prepare(input(2),std::make_shared<int>(22),CppTypeToken::of<int>(),4,16);CHECK(b);
  // enqueue 中同步退役先积存事实，发布后仍能按同一身份观察。
  CHECK(table->transition(*b,ExecutionPhase::Finalizing,conditions));
  CHECK(table->transition(*b,ExecutionPhase::Terminal,conditions));
  CHECK(!table->find(input(2).execution));CHECK(table->publish(*b));
  CHECK(table->trim(8)==0); // 两条都被读取者 pin。
  b->reset();CHECK(table->trim(8)==1);CHECK(table->usage().records==1);
  auto c=table->prepare(input(3),std::make_shared<int>(33),CppTypeToken::of<int>(),8,32);CHECK(c);
  CHECK(!table->prepare(input(4),std::make_shared<int>(44),CppTypeToken::of<int>(),1,1));
  CHECK(table->abandon(*c));CHECK(table->usage().records==2); // 摘除索引仍有真实 owner。
  c->reset();CHECK(table->usage().records==1);
  // 表销毁时外部 entry/result 保活；payload 与计费状态独立于表寿命。
  auto payload=(*a)->payload();a->reset();
  CHECK(table->usage().records==1);
  table.reset();made->reset();CHECK(*std::static_pointer_cast<const int>(payload)==11);
}
}

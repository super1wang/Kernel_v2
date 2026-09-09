#pragma once
#include "fixtures.hpp"
#include "packages/runtime/executions/execution_table.hpp"
#include "packages/runtime/executions/managed_execution.hpp"
#include "packages/runtime/executions/execution_queries.hpp"
#include <future>
namespace native_test {
inline void execution_source_observation() {
  using Table=executions::detail::ExecutionTable;
  HostIncarnation host;host.bytes[0]=91;
  Table::Limits limits;limits.waiters=1;
  auto made=Table::create(limits,host);CHECK(made);auto table=*made;
  auto source=std::make_shared<executions::detail::ExecutionSource>(table);
  auto make=[&](unsigned n,PrincipalRef owner,bool publish,bool terminal) {
    ExecutionRef ref;ref.execution_id.bytes[0]=static_cast<unsigned char>(n);
    SummaryInput summary{ref,key(),owner,{},ExecutionPhase::Queued,host,*ObservationVersion::create(1),{}, {}};
    auto payload=std::make_shared<InvokeReply<int>>(Rejected{error(ContractsErrc::Rejected)});
    auto entry=table->prepare(std::move(summary),payload,CppTypeToken::of<int>(),4,sizeof(InvokeReply<int>),{policy_test::target()},
        +[](const void* p) noexcept -> const void* {return p;});CHECK(entry);
    if(publish)CHECK(table->publish(*entry));
    if(terminal) {
      PhaseConditions conditions{{RequiredRecordState::NotRequired,0,0},{},{},false,false};
      CHECK(table->transition(*entry,ExecutionPhase::Finalizing,conditions));
      CHECK(table->transition(*entry,ExecutionPhase::Terminal,conditions));
    }
    return *entry;
  };
  auto one=make(1,policy_test::principal(),true,false);
  auto two=make(2,policy_test::principal(),true,true);
  auto other=make(3,policy_test::principal(2),true,true);
  auto hidden=make(4,policy_test::principal(),false,false);
  auto five=make(5,policy_test::principal(),true,false);
  CHECK(!source->find(hidden->execution()));
  auto found=source->find(one->execution());CHECK(found&&found->actual_targets==std::vector{policy_test::target()});
  ListRequest request{policy_test::principal(),PhaseSet::All,{1,1},{}};
  auto first=source->scan({request});CHECK(first&&first->candidates.size()==1&&first->candidates[0].first==five->ordinal());
  CHECK(first->next_scan);request.position=first->next_scan;
  auto later=make(6,policy_test::principal(),true,false); // 不进入已取得的 upper。
  auto second=source->scan({request});CHECK(second&&second->candidates.empty()&&second->next_scan);
  CHECK(second->next_scan->before_ordinal<request.position->before_ordinal);
  request.position=second->next_scan;
  auto third=source->scan({request});CHECK(third&&third->candidates.size()==1&&third->candidates[0].first==two->ordinal());
  request.position=third->next_scan;
  auto fourth=source->scan({request});CHECK(fourth&&fourth->candidates.size()==1&&fourth->candidates[0].first==one->ordinal()&&!fourth->next_scan);
  request={policy_test::principal(),PhaseSet::Terminal,{10,10},{}};
  auto terminals=source->scan({request});CHECK(terminals&&terminals->candidates.size()==1);
  CHECK(terminals->candidates[0].first==two->ordinal());
  auto wrong=request;wrong.position=KeysetPosition{HostIncarnation{},10,9};CHECK(!source->scan({wrong}));
  CHECK(table->abandon(hidden));
  // 同一个真实 source 交给既有当前授权层，不以表中 owner 相等代替权限检查。
  auto clock=std::make_shared<policy_test::Clock>();
  auto auth=std::make_shared<policy_test::Auth>(clock->now()+std::chrono::hours(1));
  auto assembly=policy::PolicyStore::create({},policy_test::configuration(),auth,clock,
      std::make_shared<policy_test::Digest>(),source);CHECK(assembly);
  auto session=assembly->store->open({{std::byte{7}}},{policy_test::rules(),auth->identity.deadline,false});CHECK(session);
  auto caller=(*session)->verify({policy_test::principal(),{}, {}});CHECK(caller);
  auto threads=std::make_shared<Threads>();threads->any_thread=true;
  executions::detail::ExecutionQueries queries(table,*session,threads);
  auto read=queries.result<int>(**caller,two->execution());CHECK(read&&read->value&&read->response);
  CHECK(!queries.result<void>(**caller,two->execution()));
  CHECK(!queries.result<int>(**caller,one->execution()));
  auto timeout=queries.wait(**caller,one->execution(),std::chrono::steady_clock::now());
  CHECK(timeout&&timeout->state==Table::WaitState::Timeout&&timeout->observed.summary->value().phase==ExecutionPhase::Queued);
  threads->role=ThreadRole::Worker;CHECK(!queries.wait(**caller,one->execution(),std::chrono::steady_clock::now()));
  threads->role=ThreadRole::Application;
  std::stop_source waiting_stop;
  auto waiting=std::async(std::launch::async,[&]{return queries.wait(**caller,one->execution(),std::chrono::steady_clock::now()+std::chrono::seconds(5),waiting_stop.get_token());});
  auto admitted_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!table->usage().waiters&&std::chrono::steady_clock::now()<admitted_deadline)std::this_thread::yield();
  CHECK(table->usage().waiters==1);
  CHECK(!table->wait_terminal(one->execution(),std::chrono::steady_clock::now()));
  CHECK(!table->prepare_wait(one->execution()));
  waiting_stop.request_stop();auto stopped=waiting.get();CHECK(stopped&&stopped->state==Table::WaitState::Cancelled);
  CHECK(table->usage().waiters==0&&source->find(one->execution())->summary->value().phase==ExecutionPhase::Queued);
  for(auto use:{policy::AccessUse::GetSummary,policy::AccessUse::Wait,policy::AccessUse::CancelExecution,policy::AccessUse::ReadResult})
    CHECK((*session)->observations()->get(**caller,one->execution(),use));
  auto authorized_page=(*session)->observations()->list(**caller,request,{});CHECK(authorized_page);
  CHECK(authorized_page->page.items.size()==1&&authorized_page->page.items[0].listing_ordinal==two->ordinal());
  CHECK(validate_list_page(request,authorized_page->page,{200,2000}));
  // 非终态含 Finalizing；终态索引转移后，Nonterminal 不再扫描该记录。
  auto ticket=table->prepare_wait(one->execution());CHECK(ticket);
  auto pending=table->poll_wait(**ticket,std::chrono::steady_clock::now()+std::chrono::seconds(2));CHECK(pending&&!*pending);
  PhaseConditions conditions{{RequiredRecordState::NotRequired,0,0},{},{},false,false};
  CHECK(table->transition(one,ExecutionPhase::Finalizing,conditions));
  ListRequest active{policy_test::principal(),PhaseSet::Nonterminal,{10,10},{}};
  auto active_page=source->scan({active});CHECK(active_page&&active_page->candidates.size()==3);
  CHECK(active_page->candidates.back().second.summary->value().phase==ExecutionPhase::Finalizing);
  CHECK(table->transition(one,ExecutionPhase::Terminal,conditions));
  pending=table->poll_wait(**ticket,std::chrono::steady_clock::now()+std::chrono::seconds(2));
  CHECK(pending&&*pending&&**pending==Table::WaitState::Terminal);
  ticket->reset();CHECK(table->usage().waiters==0);
  active_page=source->scan({active});CHECK(active_page&&active_page->candidates.size()==2);
  auto revoked_wait=std::async(std::launch::async,[&]{return queries.wait(**caller,five->execution(),std::chrono::steady_clock::now()+std::chrono::seconds(5));});
  admitted_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!table->usage().waiters&&std::chrono::steady_clock::now()<admitted_deadline)std::this_thread::yield();
  CHECK(table->usage().waiters==1);
  CHECK(assembly->administration->replace_principal_policy({policy_test::principal(),{}}));
  CHECK(table->transition(five,ExecutionPhase::Finalizing,conditions));
  CHECK(table->transition(five,ExecutionPhase::Terminal,conditions));
  CHECK(!revoked_wait.get());CHECK(table->usage().waiters==0);
  CHECK(!queries.result<int>(**caller,two->execution()));
  CHECK(!(*session)->observations()->get(**caller,one->execution(),policy::AccessUse::GetSummary));
  CHECK(!(*session)->observations()->list(**caller,request,{}));
  CHECK(source->find(one->execution())); // 事实保留，访问仍拒绝。
}
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
  HostIncarnation host;host.bytes[0]=82;
  auto t=executions::detail::ExecutionTable::create({},host);CHECK(t);
  auto resolver=[](std::span<const registry::ResourceRef> refs)->Result<std::vector<resources::Claim>> {
    CHECK(refs.size()==1&&refs[0].name==name("declared"));
    return std::vector<resources::Claim>{{"actual.slot",resources::Mode::Exclusive,1}};
  };
  std::array claims{resources::Claim{"actual.slot",resources::Mode::Exclusive,1}};
  auto held=manager->acquire(claims,resources::Phase::Compute);CHECK(held&&held->lease);
  ExecutionRef id;id.execution_id.bytes[0]=81;
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
  std::stop_source original;auto options=env.options();options.stop=original.get_token();
  auto cancelled=Managed::create(*t,schedule,manager,id,host,*bound,10,options,policy,resolver,[]{});CHECK(cancelled);
  (*cancelled)->drive();CHECK(manager->snapshot().waiters==1);original.request_stop();CHECK((*cancelled)->finished());
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
  HostIncarnation host;host.bytes[0]=72;
  auto table=Table::create({},host);CHECK(table);
  Managed::Record::Policy policy{4,sizeof(InvokeReply<int>),
      [](const int&)->Result<std::size_t>{return 4;},
      [](const InvokeReply<int>&)->Result<std::size_t>{return sizeof(InvokeReply<int>);}};
  ExecutionRef id;id.execution_id.bytes[0]=71;
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
  auto typed=(*table)->result<int>(id);CHECK(typed&&result(**typed)==7);
  auto terminal_cancel=(*execution)->cancel();CHECK(terminal_cancel&&*terminal_cancel==CancelDisposition::AlreadyTerminal);CHECK(result(*record->reply())==7);
  id.execution_id.bytes[0]=73;
  auto cancelled=Managed::create(*table,schedule,resources,id,host,*bound,8,env.options(),policy,resolver,[]{});
  CHECK(cancelled);auto queued_cancel=(*cancelled)->cancel();CHECK(queued_cancel&&*queued_cancel==CancelDisposition::Requested);(*cancelled)->drive();schedule->pump();CHECK(entered==1);
  summary=(*table)->summary((*cancelled)->entry());CHECK(summary&&(*summary)->value().phase==ExecutionPhase::Terminal);
  CHECK((*summary)->value().fault.has_value());
  // 原始 stop 在排队阶段直接赢得退役，不必等到 worker 首次进入。
  std::stop_source original;auto options=env.options();options.stop=original.get_token();
  id.execution_id.bytes[0]=75;
  auto stopped=Managed::create(*table,schedule,resources,id,host,*bound,9,options,policy,resolver,[]{});CHECK(stopped);
  CHECK(!(*stopped)->finished());original.request_stop();CHECK((*stopped)->finished());
  (*stopped)->drive();schedule->pump();CHECK(entered==1);
  auto stopped_result=(*table)->result<int>(id);CHECK(stopped_result&&std::holds_alternative<Rejected>(**stopped_result));
  schedule->close();id.execution_id.bytes[0]=74;
  CHECK(!Managed::create(*table,schedule,resources,id,host,*bound,8,env.options(),policy,resolver,[]{}));
  CHECK(!(*table)->find(id));CHECK(entered==1);
  // Scheduler 的同步 wake 在 enqueue 返回前关闭：早到完成只能积存，
  // 表项仍不可见；成功发布后仍返回同一 Accepted，不能丢失失败事实。
  for(bool reject_publication:{false,true}) {
    auto early_table=Table::create({},host);CHECK(early_table);
    std::shared_ptr<scheduler::Scheduler> early_schedule;
    bool fired=false;
    id.execution_id.bytes[0]=reject_publication?77:76;
    auto early=scheduler::Scheduler::create(executor,
        {{env.policy.caller->view().description().principal.principal_id}}, {},[&] {
          if(std::exchange(fired,true))return;
          CHECK(!(*early_table)->find(id));
          early_schedule->close();
          CHECK(!(*early_table)->find(id));
          if(reject_publication)(*early_table)->close_admission();
        });CHECK(early);early_schedule=std::move(*early);
    auto value=Managed::create(*early_table,early_schedule,resources,id,host,
        *bound,11,env.options(),policy,resolver,[]{});
    CHECK(fired);CHECK(entered==1);
    if(reject_publication) {
      CHECK(!value);CHECK(!(*early_table)->find(id));
      CHECK((*early_table)->usage().records==0);
    } else {
      CHECK(value);CHECK((*value)->accepted().execution==id);CHECK((*value)->finished());
      auto visible=(*early_table)->summary((*value)->entry());CHECK(visible);
      CHECK((*visible)->value().phase==ExecutionPhase::Terminal);
      CHECK((*visible)->value().fault.has_value());
      auto reply=(*early_table)->result<int>(id);
      CHECK(reply&&std::holds_alternative<Rejected>(**reply));
    }
    CHECK(early_schedule->snapshot().active==0);
    early_schedule.reset();
  }
}
inline void execution_table_ownership() {
  using Table=executions::detail::ExecutionTable;
  HostIncarnation host;host.bytes[0]=42;
  auto made=Table::create({3,12,48,1,48},host);CHECK(made);auto table=*made;
  Env env;
  auto input=[&](unsigned char id) {
    ExecutionRef ref;ref.execution_id.bytes[0]=id;
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

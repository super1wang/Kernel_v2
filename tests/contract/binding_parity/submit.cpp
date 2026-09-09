#include "tests/contract/native/fixtures.hpp"
#include <ock/control/invoke_method.hpp>
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
#include <iostream>
using namespace ock;
struct Input {std::int64_t amount=0;std::string text;};
struct Fields {
  static contracts::TypeIdentity identity(){return {name("submit.input"),ver(),{}};}
  static auto fields(){
    binding::Field<&Input::amount> amount;amount.name="amount";amount.minimum=0;amount.maximum=10;
    binding::Field<&Input::text> text;text.name="text";
    return std::tuple(amount,text);
  }
};
namespace ock::contracts {template<>struct TypeContract<Input>:binding::TypeContract<Input,Fields>{};}
namespace {
std::atomic<bool> entered=false,release=false,stopped=false;
Result<Input> work(const Input& input,contracts::WorkContext& context) {
  entered=true;
  auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  while(!release&&std::chrono::steady_clock::now()<until){if(context.stop_requested())stopped=true;std::this_thread::yield();}
  if(context.stop_requested())stopped=true;
  return Input{input.amount+1,input.text};
}
template<class P>void until(P predicate){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);while(!predicate()&&std::chrono::steady_clock::now()<end)std::this_thread::yield();CHECK(predicate());}
struct Threads:runtime::invocation::TrustedThreadPort {
  std::thread::id main=std::this_thread::get_id();
  Result<runtime::invocation::ThreadObservation> current()const noexcept override {
    using namespace runtime::invocation;
    return ThreadObservation{std::this_thread::get_id()==main?ThreadRole::Application:ThreadRole::Worker,name("app"),true};
  }
};
struct Factory:runtime::host::HostExecutionFactoryPort {
  Result<std::shared_ptr<runtime::host::HostExecutionPort>> create(contracts::HostIncarnation id)override{
    auto made=cpu_pool::Executor::create({2,2});CHECK(made);
    std::shared_ptr<contracts::ExecutorControlPort> pool=std::move(*made);
    runtime::host::ExecutionOptions options;options.active=4;options.subjects={{policy_test::principal().principal_id}};
    auto result=runtime::host::make_executions(id,pool,options);
    if(!result)(void)pool->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2));
    return result;
  }
};
struct Lifecycle:runtime::host::ModuleLifecyclePort {
  Result<void> start(const runtime::host::ModuleContext&)override{return {};}
  runtime::host::ModuleStopResult stop()override{return {true,{}};}
};
Result<std::size_t> targets(const Input&,std::span<foundation::ObjectId> out)noexcept{
  if(out.empty())return foundation::make_unexpected(contracts::error(contracts::ContractsErrc::BudgetExceeded));out[0]=policy_test::target();return 1;
}
}
int main()try{
  using namespace runtime;using namespace contracts;
  auto clock=std::make_shared<policy_test::Clock>();auto auth=std::make_shared<policy_test::Auth>(clock->now()+std::chrono::hours(1));
  auto made=host::NativeHost::create({},policy_test::configuration(),{auth,clock,std::make_shared<policy_test::Digest>(),std::make_shared<Threads>(),{},std::make_shared<Factory>()});CHECK(made);
  auto& host=*made;
  struct Close{host::NativeHost& host;~Close(){release=true;host.shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(3));}}close{*host};
  registry::ModuleManifest manifest{name("native"),ver()};manifest.operations.push_back(key());manifest.executors.push_back(name("test"));
  registry::ModuleInput module{manifest,{},{},{},{},{{name("test"),name("app"),false,false,std::make_shared<native_test::Executor>()}},{}};
  module.register_operations=[](registry::Registrar& registrar){
    registry::SubmissionStorage<Input,Input> storage{4096,4096,
        [](const Input& input)->Result<std::size_t>{return sizeof(Input)+input.text.capacity();},
        [](const InvokeReply<Input>& value)->Result<std::size_t>{
          auto c=std::get_if<Completed<Input>>(&value);if(!c)return sizeof(value);
          auto r=std::get_if<ReadCompleted<Input>>(&c->outcome.value());
          return sizeof(value)+(r&&r->result?r->result->text.capacity():0);
        }};
    CHECK(registrar.compute(work,{key(),{},{false,false,false,name("test"),name("app")},AtomicMode::PureCompute,{name("allow")},"submit"},{{},{},{name("native"),name("test")},{},false},storage));
  };
  CHECK(host->add({module,std::make_shared<Lifecycle>()}));CHECK(host->start());
  auto session=host->open({{std::byte{7}}},{policy_test::rules(),auth->identity.deadline,false});CHECK(session);
  auto caller=session->verify({policy_test::principal(),{},{}});CHECK(caller);
  auto registered=control::RegisteredInvocation<Input,Input>::create();CHECK(registered);
  auto binding=control::InvocationBinding::bind(*session,*registered,key(),{},Shape::Read,*caller,std::array{policy_test::target()},targets,name("submit.rpc"));CHECK(binding);
  auto method=control::InvokeMethod::create_submit({*binding},clock,std::chrono::seconds(2));CHECK(method);
  const auto id=control::wire_text(host->incarnation());
  auto router=control::Router::create({"submit.test",id,id},*caller,{*method});CHECK(router);
  auto send=[&](std::string_view text){auto payload=data::Payload::parse(text);CHECK(payload);return router->dispatch(payload->view());};
  CHECK(send(R"({"jsonrpc":"2.0","id":"hello","method":"host.hello","params":{"api_version":"ock.control/1"}})"));
  std::string request=R"({"jsonrpc":"2.0","id":"submit","method":"operation.submit","params":{"operation":{"name":"test.read","version":"1.0.0"},"contract_digest":")"+std::string(64,'0')+R"(","execution_timeout_ms":2000,"args":{"amount":4,"text":")"+std::string(1024,'x')+R"("}}})";
  for(unsigned kind=0;kind<3;++kind) {
    auto bad=request;
    if(kind==0)bad.replace(bad.find("2000"),4,"3000");
    if(kind==1)bad.replace(bad.find("\"amount\":4"),10,"\"amount\":-1");
    if(kind==2)bad.replace(bad.find(std::string(64,'0')),64,std::string(64,'1'));
    auto refused=send(bad);CHECK(refused);auto wire=data::Payload::parse(refused->json);CHECK(wire);
    CHECK(wire->view().at("result").at("kind").string()=="Rejected");CHECK(!entered);
  }
  auto response=send(request);CHECK(response);auto payload=data::Payload::parse(response->json);CHECK(payload);
  auto result=payload->view().at("result");CHECK(result.at("kind").string()=="Accepted");CHECK(result.at("acceptance_guarantee").string()=="Volatile");
  auto identity=result.at("execution_ref").at("execution_id").string();CHECK(identity);
  auto execution=control::wire_id<foundation::TaskId>(result.at("execution_ref").at("execution_id"));CHECK(execution);ExecutionRef ref{*execution};
  request.clear();request.shrink_to_fit();until([]{return entered.load();});
  router->close();method->port->disconnect(); // 连接意图不能取消已经接受的工作。
  auto empty=data::Payload::parse("{}");CHECK(empty);
  auto closed=method->port->call(**caller,empty->view());CHECK(closed&&closed->view().at("kind").string()=="Rejected");
  release=true;
  auto waited=session->wait(**caller,ref,std::chrono::steady_clock::now()+std::chrono::seconds(2));CHECK(waited&&waited->state==host::ExecutionWaitState::Terminal);
  CHECK(!stopped);
  auto value=session->result<Input>(**caller,ref);CHECK(value);
  const auto& output=std::get<ReadCompleted<Input>>(std::get<Completed<Input>>(*value->value).outcome.value());
  CHECK(output.result&&output.result->amount==5&&output.result->text==std::string(1024,'x'));
  std::cout<<"Owned Control Submit retained input and survived RPC disconnect\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

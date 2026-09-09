#include "tests/contract/native/fixtures.hpp"
#include <ock/control/invoke_method.hpp>
#include <ock/control/execution_method.hpp>
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
#include <iostream>
#include <filesystem>
#include <fstream>
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
struct Transport final:control::ObservationTransport {
  struct Ticket final:runtime::policy::TransmissionReservation {
    std::size_t bytes;explicit Ticket(std::size_t n):bytes(n){}std::size_t capacity()const noexcept override{return bytes;}
  };
  std::vector<data::Payload> sent;bool closed=false;
  void record(std::span<const std::byte> bytes) {
    control::FrameDecoder decoder;auto decoded=decoder.consume(bytes);CHECK(decoded&&decoded->message);sent.push_back(std::move(*decoded->message));
  }
  Result<void> queue_ack(std::span<const std::byte> bytes)override {
    if(closed)return foundation::make_unexpected(control::error(control::ProtocolErrc::Closed));record(bytes);return {};
  }
  Result<std::unique_ptr<runtime::policy::TransmissionReservation>> reserve(std::size_t n)override {
    return std::unique_ptr<runtime::policy::TransmissionReservation>(new Ticket(n));
  }
  runtime::policy::StartResult start_now(const runtime::policy::PreparedTransmission& value,runtime::policy::TransmissionReservation&)noexcept override {
    if(closed)return runtime::policy::StartResult::NotStarted;
    try{record(value.bytes());return runtime::policy::StartResult::Started;}catch(...){return runtime::policy::StartResult::Unknown;}
  }
  void close()noexcept override{closed=true;}
};
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
  auto opened=host->open({{std::byte{7}}},{policy_test::rules(),auth->identity.deadline,false});CHECK(opened);
  auto session=std::make_shared<host::HostSession>(std::move(*opened));
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
  using Query=control::ExecutionMethod;
  std::vector<control::ExecutionResultBinding> results{control::ExecutionResultBinding::create<Input>(key(),registered->output())};
  auto query_args=data::Payload::parse("{\"execution_ref\":{\"execution_id\":\""+std::string(*identity)+"\"}}");CHECK(query_args);
  std::array<std::shared_ptr<Transport>,4> transports;
  std::array<std::shared_ptr<Query>,4> queries;
  for(unsigned i=0;i<4;++i){
    transports[i]=std::make_shared<Transport>();auto kind=static_cast<Query::Kind>(i);
    auto schema=Query::parameters(kind);CHECK(schema&&schema->validate(query_args->view()));
    auto q=Query::create(kind,session,*caller,transports[i],results,clock,std::chrono::seconds(2));CHECK(q);queries[i]=*q;
  }
  std::vector<control::Method> query_methods;
  for(unsigned i=0;i<4;++i){auto kind=static_cast<Query::Kind>(i);query_methods.push_back({std::string(Query::name(kind)),*Query::parameters(kind),queries[i]});}
  auto query_router=control::Router::create({"submit.test",id,id,{}, {},"managed"},*caller,std::move(query_methods));CHECK(query_router);
  auto query_send=[&](std::string_view method,std::string_view call_id,std::string_view parameters){
    auto body=data::Payload::parse("{\"jsonrpc\":\"2.0\",\"id\":\""+std::string(call_id)+"\",\"method\":\""+std::string(method)+"\",\"params\":"+std::string(parameters)+"}");CHECK(body);return query_router->dispatch(body->view());
  };
  CHECK(query_send("host.hello","hello",R"({"api_version":"ock.control/1"})"));
  auto query_text=query_args->encode();CHECK(query_text);
  auto queued_get=query_send("execution.get","get",*query_text);CHECK(queued_get&&queued_get->queued&&queued_get->json.empty());CHECK(queries[0]->pump());
  CHECK(transports[0]->sent.back().view().at("result").at("full_result_available").boolean()==false);
  auto pending=queries[1]->dispatch(**caller,"wait",query_args->view());CHECK(pending&&!*pending);
  CHECK(!queries[1]->dispatch(**caller,"second-wait",query_args->view()));CHECK(queries[1]->pump());CHECK(transports[1]->sent.empty());
  CHECK(!queries[3]->dispatch(**caller,"early-result",query_args->view()));
  auto unavailable=query_send("result.read","early-result",*query_text);CHECK(unavailable);
  auto failure=data::Payload::parse(unavailable->json);CHECK(failure&&failure->view().at("error").at("message").string()=="NotAvailable");
  router->close();method->port->disconnect(); // 连接意图不能取消已经接受的工作。
  auto empty=data::Payload::parse("{}");CHECK(empty);
  auto closed=method->port->call(**caller,empty->view());CHECK(closed&&closed->view().at("kind").string()=="Rejected");
  release=true;
  auto waited=session->wait(**caller,ref,std::chrono::steady_clock::now()+std::chrono::seconds(2));CHECK(waited&&waited->state==host::ExecutionWaitState::Terminal);
  CHECK(!stopped);
  auto value=session->result<Input>(**caller,ref);CHECK(value);
  const auto& output=std::get<ReadCompleted<Input>>(std::get<Completed<Input>>(*value->value).outcome.value());
  CHECK(output.result&&output.result->amount==5&&output.result->text==std::string(1024,'x'));
  CHECK(queries[1]->pump());CHECK(transports[1]->sent.size()==1);
  CHECK(transports[1]->sent.back().view().at("result").at("wait_state").string()=="Terminal");
  CHECK(queries[0]->dispatch(**caller,"get-done",query_args->view()));CHECK(queries[0]->pump());
  CHECK(transports[0]->sent.back().view().at("result").at("full_result_available").boolean()==true);
  CHECK(queries[3]->dispatch(**caller,"result",query_args->view()));CHECK(queries[3]->pump());
  auto full=transports[3]->sent.back().view().at("result");CHECK(full.at("projection").string()=="full");
  CHECK(full.at("reply").at("kind").string()=="Completed");
  CHECK(full.at("reply").at("outcome").at("result").at("amount").int64()==5);
  CHECK(full.at("reply").at("outcome").at("result").at("text").string()==std::string(1024,'x'));
  CHECK(queries[2]->dispatch(**caller,"cancel-terminal",query_args->view()));CHECK(queries[2]->pump());
  CHECK(transports[2]->sent.back().view().at("result").at("disposition").string()=="AlreadyTerminal");
  auto observer_open=host->open({{std::byte{7}}},{policy_test::rules(),auth->identity.deadline,false});CHECK(observer_open);
  auto observer=std::make_shared<host::HostSession>(std::move(*observer_open));auto observer_caller=observer->verify({policy_test::principal(),{}, {}});CHECK(observer_caller);
  auto protected_transport=std::make_shared<Transport>();
  auto protected_result=Query::create(Query::Kind::Result,observer,*observer_caller,protected_transport,results,clock);CHECK(protected_result);
  CHECK((*protected_result)->dispatch(**observer_caller,"revoked",query_args->view()));CHECK(observer->close());
  (void)(*protected_result)->pump();CHECK(protected_transport->sent.empty());
  entered=false;release=false;stopped=false;
  auto native=session->bind<Input,Input>(key(),{},Shape::Read,*caller,std::array{policy_test::target()},targets,name("query.cancel"));CHECK(native);
  auto second=native->submit(Input{8,"cancel"},{{},clock->now()+std::chrono::seconds(2),100});CHECK(std::holds_alternative<Accepted>(second));
  auto second_ref=std::get<Accepted>(second).execution;until([]{return entered.load();});
  auto second_args=data::Payload::parse("{\"execution_ref\":{\"execution_id\":\""+control::wire_text(second_ref.execution_id)+"\"}}");CHECK(second_args);
  auto timeout_args=data::Payload::parse("{\"execution_ref\":{\"execution_id\":\""+control::wire_text(second_ref.execution_id)+"\"},\"wait_timeout_ms\":0}");CHECK(timeout_args);
  CHECK(queries[1]->dispatch(**caller,"timeout",timeout_args->view()));CHECK(queries[1]->pump());
  CHECK(transports[1]->sent.back().view().at("result").at("wait_state").string()=="Timeout");CHECK(!stopped);
  auto disconnected_transport=std::make_shared<Transport>();
  auto disconnected_wait=Query::create(Query::Kind::Wait,session,*caller,disconnected_transport,results,clock);CHECK(disconnected_wait);
  CHECK((*disconnected_wait)->dispatch(**caller,"disconnect",second_args->view()));(*disconnected_wait)->disconnect();
  CHECK(!(*disconnected_wait)->pump());CHECK(disconnected_transport->sent.empty());CHECK(!stopped);
  CHECK(queries[2]->dispatch(**caller,"cancel-running",second_args->view()));CHECK(queries[2]->pump());
  CHECK(transports[2]->sent.back().view().at("result").at("disposition").string()=="AlreadyClaimed");until([]{return stopped.load();});
  release=true;auto cancelled_wait=session->wait(**caller,second_ref,clock->now()+std::chrono::seconds(2));CHECK(cancelled_wait&&cancelled_wait->state==host::ExecutionWaitState::Terminal);
  auto cancelled_value=session->result<Input>(**caller,second_ref);CHECK(cancelled_value);
  const auto& cancelled_output=std::get<ReadCompleted<Input>>(std::get<Completed<Input>>(*cancelled_value->value).outcome.value());
  CHECK(cancelled_output.result&&cancelled_output.result->amount==9);
  auto schemas=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path()/"schemas/rpc-v1";
  auto schema_text=[&](std::string name){std::ifstream f(schemas/name,std::ios::binary);CHECK(f);std::string text(std::istreambuf_iterator<char>(f),{});std::erase(text,'\r');return text;};
  for(auto [index,file]:std::vector<std::pair<unsigned,std::string>>{{0,"execution-get-summary.schema.json"},{1,"execution-wait.schema.json"},{2,"execution-cancel.schema.json"},{3,"result-read.schema.json"}}) {
    auto text=schema_text(file);
    if(index==3){const std::string marker="{\n      \"$ref\": \"outcome.schema.json\"\n    }";auto pos=text.find(marker);CHECK(pos!=text.npos);text.replace(pos,marker.size(),schema_text("outcome.schema.json"));}
    auto schema=binding::CompiledSchema::compile(text);CHECK(schema);
    for(const auto& sent:transports[index]->sent)CHECK(schema->validate(sent.view().at("result")));
  }
  std::cout<<"Owned Control Submit retained input and survived RPC disconnect\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

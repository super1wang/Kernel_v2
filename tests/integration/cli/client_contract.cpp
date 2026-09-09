#include <ock/control_client/watch.hpp>
#include <ock/control_protocol/observation_wire.hpp>
#include <deque>
#include <iostream>
#include <stdexcept>
using namespace ock;
void check(bool good) { if(!good) throw std::runtime_error("client contract failed"); }
struct Script final : control_client::ExchangePort,control_client::NotificationPort {
  std::vector<std::string> methods;
  std::deque<control_client::Notification> events;
  std::string host=std::string(32,'2'),execution=std::string(32,'1'),subscription=std::string(32,'3'),stream=std::string(32,'4');
  unsigned version=2;
  bool closed=false,bad_id=false;
  foundation::Result<data::Payload> exchange(const data::Payload &request,std::chrono::milliseconds,std::stop_token) override {
    auto method=*request.view().at("method").string(); methods.emplace_back(method);
    auto id=*request.view().at("id").string();
    std::string result;
    if(method=="host.hello") result="{\"application_id\":\"script.test\",\"instance_id\":\""+host+"\",\"host_incarnation\":\""+host+"\",\"api_version\":\"ock.control/1\",\"dedup_epoch\":null,\"observation_backend\":\"managed\",\"budgets\":{\"frame_bytes\":16384},\"supported_methods\":[\"host.hello\",\"notifications.subscribe\",\"notifications.unsubscribe\",\"execution.get\"]}";
    else if(method=="notifications.subscribe") {
      contracts::PrincipalId principal{}; principal.bytes[15]=1;
      check(bool(control::parse_subscribe(request.view().at("params"),{principal})));
      result="{\"subscription_id\":\""+subscription+"\",\"stream_generation\":\""+stream+"\",\"host_incarnation\":\""+host+"\",\"first_sequence\":\"1\",\"replay_supported\":false}";
    } else if(method=="execution.get") {
      check(methods.size()>1 && methods[1]=="notifications.subscribe");
      result="{\"host_incarnation\":\""+host+"\",\"execution_ref\":{\"execution_id\":\""+execution+"\"},\"observation_version\":\""+std::to_string(version)+"\",\"phase\":\"Running\"}";
    } else if(method=="notifications.unsubscribe") {
      check(bool(control::parse_unsubscribe(request.view().at("params")))); result="{\"removed\":true}";
    } else throw std::runtime_error("unexpected method");
    return data::Payload::parse("{\"jsonrpc\":\"2.0\",\"id\":\""+std::string(bad_id ? "wrong" : id)+"\",\"result\":"+result+"}");
  }
  void enqueue(unsigned sequence,unsigned observation,bool gap=false) {
    auto payload=data::Payload::parse("{\"jsonrpc\":\"2.0\",\"method\":\"notifications.event\",\"params\":{\"subscription_id\":\""+subscription+"\",\"stream_generation\":\""+stream+"\",\"host_incarnation\":\""+host+"\",\"execution_ref\":{\"execution_id\":\""+execution+"\"},\"sequence\":\""+std::to_string(sequence)+"\",\"observation_version\":\""+std::to_string(observation)+"\",\"topic\":\"execution.progress\",\"gap\":"+(gap ? "true" : "false")+",\"data\":{}}}");
    check(bool(payload)); events.push_back({std::move(*payload),false});
  }
  foundation::Result<control_client::Notification> next_notification(std::chrono::milliseconds,std::stop_token) override {
    if(events.empty()) return control_client::Notification{{},false};
    auto event=std::move(events.front()); events.pop_front(); return event;
  }
  void close_notifications() noexcept override { closed=true; }
};
int main(int argc,char **argv) try {
  check(argc==2); const std::string mode=argv[1];
  if(mode=="exit_codes") {
    for(auto [kind,expected] : std::vector<std::pair<std::string,int>>{{"ReadCompleted",0},{"FailedBeforeApply",4},{"CancelledBeforeApply",9},{"PartialCompletion",4},{"Indeterminate",7}}) {
      auto p=data::Payload::parse("{\"result\":{\"kind\":\"Completed\",\"outcome\":{\"kind\":\""+kind+"\"}}}"); check(bool(p));
      check(control_client::exit_code(p->view())==expected);
    }
    auto accepted=data::Payload::parse(R"({"result":{"kind":"Accepted"}})"); check(control_client::exit_code(accepted->view())==0);
    auto denied=data::Payload::parse(R"({"result":{"kind":"Rejected"}})"); check(control_client::exit_code(denied->view())==3);
    return 0;
  }
  auto script=std::make_shared<Script>();
  if(mode=="protocol") {
    script->bad_id=true; check(!control_client::Client::open(script));
    script->bad_id=false; auto client=control_client::Client::open(script); check(bool(client));
    const auto before=script->methods.size();
    auto empty=data::Payload::parse("{}");
    auto missing=client->call("execution.cancel",*empty);
    check(!missing && missing.error().code()==control_client::error(control_client::ClientErrc::MissingCapability).code());
    check(script->methods.size()==before);
    auto large=data::Payload::parse("{\"text\":\""+std::string(client->hello().frame_bytes,'x')+"\"}");
    check(bool(large));
    check(!client->call("execution.get",*large));
    check(script->methods.size()==before); return 0;
  }
  check(mode=="watch");
  auto client=control_client::Client::open(script); check(bool(client));
  script->enqueue(1,1); // subscribe→get 之间到达的旧版本必须丢弃。
  auto watch=control_client::Watch::open(*client,*script,script->execution); check(bool(watch));
  check(script->methods==std::vector<std::string>{"host.hello","notifications.subscribe","execution.get"});
  auto old=(*watch)->next(std::chrono::milliseconds(1)); check(old && !*old);
  script->enqueue(2,3); auto next=(*watch)->next(std::chrono::milliseconds(1)); check(next && *next);
  script->version=5; script->enqueue(4,5,true);
  auto gap=(*watch)->next(std::chrono::milliseconds(1)); check(gap && *gap && script->methods.back()=="execution.get");
  script->version=6; script->events.push_back({{},true});
  auto lost=(*watch)->next(std::chrono::milliseconds(1)); check(lost && *lost);
  script->host=std::string(32,'5'); script->enqueue(5,7);
  check(!(*watch)->next(std::chrono::milliseconds(1)));
  (*watch)->close(); check(script->closed && script->methods.back()=="notifications.unsubscribe");
  check(std::find(script->methods.begin(),script->methods.end(),"execution.cancel")==script->methods.end());
  std::cout << "client subscribe/get/version/gap/close contract passed\n";
  return 0;
} catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }

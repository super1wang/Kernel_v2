#include "pipe_exchange.hpp"
#include <ock/control_client/intent.hpp>
#include <CLI/CLI.hpp>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <fstream>
#include <iostream>
namespace {
using namespace ock;
using foundation::Result;
std::stop_source interruption;
BOOL WINAPI console_control(DWORD event) {
  if(event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) { interruption.request_stop(); return TRUE; }
  return FALSE;
}
Result<std::string> utf8(std::wstring_view text) {
  if(text.empty()) return std::string{};
  int size = WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
  if(size<=0) return foundation::make_unexpected(control_client::error(control_client::ClientErrc::InvalidInput));
  std::string bytes(size,'\0');
  if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),bytes.data(),size,nullptr,nullptr))
    return foundation::make_unexpected(control_client::error(control_client::ClientErrc::InvalidInput));
  return bytes;
}
int failure(int code,std::string_view name) {
  auto quoted = control_client::quote(name);
  std::cout << "{\"error\":{\"kind\":" << (quoted ? *quoted : "\"Failure\"") << ",\"exit_code\":" << code << "}}\n";
  std::cerr << name << '\n'; return code;
}
int failed(const foundation::Error &error) {
  using control_client::ClientErrc;
  if(error.code() == control_client::error(ClientErrc::Interrupted).code()) return failure(8,"Interrupted");
  if(error.code() == control_client::error(ClientErrc::Timeout).code()) return failure(5,"Timeout");
  if(error.code() == control_client::error(ClientErrc::IntentConflict).code()) return failure(2,"IntentConflict");
  if(error.code() == control_client::error(ClientErrc::MissingCapability).code()) return failure(3,"MissingCapability");
  if(error.code() == control_client::error(ClientErrc::InvalidInput).code()) return failure(2,"InvalidInput");
  return failure(6,"TransportOrProtocolFailure");
}
int output(Result<data::Payload> response) {
  if(!response) return failed(response.error());
  auto encoded = response->encode(); if(!encoded) return failed(encoded.error());
  std::cout << *encoded << '\n'; return control_client::exit_code(response->view());
}
Result<std::string> input_bytes(const std::string &file,bool use_stdin,const std::string &inline_args) {
  constexpr std::size_t limit = 4*1024*1024;
  if(file.empty() && !use_stdin) return inline_args.empty() ? std::string("{}") : inline_args;
  std::ifstream input;
  if(!file.empty()) { input.open(std::filesystem::u8path(file),std::ios::binary); if(!input) return foundation::make_unexpected(control_client::error(control_client::ClientErrc::InvalidInput)); }
  std::istream &stream = use_stdin ? std::cin : input;
  std::string bytes; std::array<char,4096> chunk;
  if(use_stdin && GetFileType(GetStdHandle(STD_INPUT_HANDLE)) == FILE_TYPE_PIPE) {
    const auto handle=GetStdHandle(STD_INPUT_HANDLE);
    for(;;) {
      if(interruption.stop_requested()) return foundation::make_unexpected(control_client::error(control_client::ClientErrc::Interrupted));
      DWORD available=0;
      if(!PeekNamedPipe(handle,nullptr,0,nullptr,&available,nullptr)) {
        if(GetLastError()==ERROR_BROKEN_PIPE) break;
        return foundation::make_unexpected(control_client::error(control_client::ClientErrc::InvalidInput));
      }
      if(!available) { Sleep(10); continue; }
      DWORD count=0;
      if(!ReadFile(handle,chunk.data(),(std::min)(available,static_cast<DWORD>(chunk.size())),&count,nullptr))
        return foundation::make_unexpected(control_client::error(control_client::ClientErrc::InvalidInput));
      if(count>limit-bytes.size()) return foundation::make_unexpected(control_client::error(control_client::ClientErrc::InvalidInput));
      bytes.append(chunk.data(),count);
    }
    if(bytes.starts_with("\xef\xbb\xbf")) bytes.erase(0,3);
    return bytes;
  }
  while(stream) {
    stream.read(chunk.data(),chunk.size()); const auto n=static_cast<std::size_t>(stream.gcount());
    if(n>limit-bytes.size()) return foundation::make_unexpected(control_client::error(control_client::ClientErrc::InvalidInput));
    bytes.append(chunk.data(),n);
  }
  if(stream.bad()) return foundation::make_unexpected(control_client::error(control_client::ClientErrc::InvalidInput));
  if(bytes.starts_with("\xef\xbb\xbf")) bytes.erase(0,3);
  return bytes;
}
}
int wmain(int argc,wchar_t **wide_argv) try {
  _setmode(_fileno(stdin),_O_BINARY); _setmode(_fileno(stdout),_O_BINARY); _setmode(_fileno(stderr),_O_BINARY);
  SetConsoleOutputCP(CP_UTF8);
  // 新控制台子进程也可能继承父进程的忽略位；显式恢复本 CLI 的中断语义。
  SetConsoleCtrlHandler(nullptr,FALSE); SetConsoleCtrlHandler(console_control,TRUE);
  std::vector<std::string> arguments; std::vector<char*> argv;
  for(int i=0;i<argc;++i) { auto text=utf8(wide_argv[i]); if(!text) return failure(2,"InvalidUTF8"); arguments.push_back(std::move(*text)); }
  for(auto &argument:arguments) argv.push_back(argument.data());
  CLI::App app{"OCK local control client"}; app.require_subcommand(1);
  std::string instance="default",server_sid,operation,version="1.0.0",args,file,intent_file,digest,prefix,owner="self",phase="nonterminal",execution_ref;
  bool json=false,jsonl=false,stdin_input=false,cancel_on_interrupt=false;
  int timeout=5000,page_size=50;
  app.add_option("--instance",instance,"Controlled local instance name");
  app.add_option("--server-sid",server_sid,"Expected OS server SID; defaults to current user");
  app.add_option("--timeout-ms",timeout)->check(CLI::Range(1,300000));
  app.add_flag("--json",json);
  auto capabilities=app.add_subcommand("capabilities"); capabilities->require_subcommand(1); capabilities->fallthrough();
  auto search=capabilities->add_subcommand("search"); search->fallthrough(); search->add_option("--prefix",prefix);
  auto describe=capabilities->add_subcommand("describe"); describe->fallthrough();
  describe->add_option("operation",operation)->required(); describe->add_option("--version",version);
  auto invoke=app.add_subcommand("invoke"); invoke->fallthrough(); invoke->add_option("operation",operation)->required();
  invoke->add_option("--version",version); invoke->add_option("--contract-digest",digest);
  auto args_option=invoke->add_option("--args",args); auto file_option=invoke->add_option("--file",file);
  auto stdin_option=invoke->add_flag("--stdin",stdin_input);
  args_option->excludes(file_option)->excludes(stdin_option); file_option->excludes(stdin_option);
  invoke->add_option("--intent-file",intent_file);
  auto execution=app.add_subcommand("execution"); execution->fallthrough(); execution->require_subcommand(1);
  auto list=execution->add_subcommand("list"); list->fallthrough();
  list->add_option("--owner",owner); list->add_option("--phase",phase)->check(CLI::IsMember({"nonterminal","terminal","all"}));
  list->add_option("--page-size",page_size)->check(CLI::Range(1,200));
  auto watch=execution->add_subcommand("watch"); watch->fallthrough(); watch->add_option("execution_ref",execution_ref)->required();
  watch->add_flag("--jsonl",jsonl); watch->add_flag("--cancel-on-interrupt",cancel_on_interrupt);
  try { app.parse(argc,argv.data()); }
  catch(const CLI::CallForHelp &help) { return app.exit(help); }
  catch(const CLI::ParseError &parse) { std::cerr << parse.what() << '\n'; return failure(2,"Usage"); }
  if(server_sid.empty()) { auto sid=local_ipc::current_user_sid(); if(!sid) return failed(sid.error()); server_sid=std::move(*sid); }
  local_ipc::PipeOptions options; options.instance=instance; options.expected_server_sid=server_sid;
  auto transport=cli::PipeExchange::open(options); if(!transport) return failed(transport.error());
  auto client=control_client::Client::open(*transport,std::chrono::milliseconds(timeout),interruption.get_token());
  if(!client) return failed(client.error());
  auto quoted=[](std::string_view text) { auto result=control_client::quote(text); if(!result) throw std::runtime_error("Invalid text"); return *result; };
  if(*search) return output(client->call("capabilities.search",*data::Payload::parse("{\"prefix\":"+quoted(prefix)+"}"),interruption.get_token()));
  if(*describe) return output(client->call("capabilities.describe",*data::Payload::parse("{\"name\":"+quoted(operation)+",\"version\":"+quoted(version)+"}"),interruption.get_token()));
  if(*list) {
    if(client->hello().observation_backend!="managed") return failure(3,"ExecutionProviderUnavailable");
    auto params=data::Payload::parse("{\"owner\":"+quoted(owner)+",\"phase_set\":"+quoted(phase)+",\"page_size\":"+std::to_string(page_size)+"}");
    return output(client->call("execution.list",*params,interruption.get_token()));
  }
  if(*watch) {
    if(client->hello().observation_backend!="managed" || !client->supports("notifications.subscribe") || !client->supports("execution.get"))
      return failure(3,"ExecutionProviderUnavailable");
    if(!jsonl) return failure(2,"WatchRequiresJSONL");
    if(cancel_on_interrupt && !client->supports("execution.cancel")) return failure(3,"CancelCapabilityUnavailable");
    auto observer_transport=cli::PipeExchange::open(options); if(!observer_transport) return failed(observer_transport.error());
    auto observer=control_client::Client::open(*observer_transport,std::chrono::milliseconds(timeout),interruption.get_token());
    if(!observer) return failed(observer.error());
    if(observer->hello().instance_id!=client->hello().instance_id || observer->hello().host_incarnation!=client->hello().host_incarnation)
      return failure(6,"HostChangedDuringWatch");
    auto subscription=control_client::Watch::open(*observer,**observer_transport,execution_ref,interruption.get_token());
    if(!subscription) return failed(subscription.error());
    auto initial=(*subscription)->snapshot(); if(!initial) return failed(initial.error());
    bool terminal=initial->view().at("result").at("phase").string()=="Terminal";
    if(output(std::move(initial))!=0) return failure(6,"InvalidSnapshot");
    while(!terminal && !interruption.stop_requested()) {
      auto next=(*subscription)->next(std::chrono::milliseconds(timeout),interruption.get_token());
      if(!next) {
        if(interruption.stop_requested()) break;
        return failed(next.error());
      }
      if(*next) {
        terminal=(**next).view().at("result").at("phase").string()=="Terminal";
        auto encoded=(**next).encode(); if(!encoded) return failed(encoded.error());
        std::cout<<*encoded<<'\n'<<std::flush;
      }
    }
    (*subscription)->close();
    if(interruption.stop_requested()) {
      if(cancel_on_interrupt) {
        auto params=data::Payload::parse("{\"execution_ref\":{\"execution_id\":"+quoted(execution_ref)+"}}");
        (void)output(client->call("execution.cancel",*params));
      }
      return failure(8,"Interrupted");
    }
    return 0; // 观察成功完成；summary 不承诺完整业务 Outcome。
  }
  auto bytes=input_bytes(file,stdin_input,args); if(!bytes) return failed(bytes.error());
  auto parameters=data::Payload::parse(*bytes); if(!parameters) return failure(2,"InvalidJSONOrUTF8");
  if(digest.empty()) {
    auto description=client->call("capabilities.describe",*data::Payload::parse("{\"name\":"+quoted(operation)+",\"version\":"+quoted(version)+"}"),interruption.get_token());
    if(!description) return failed(description.error());
    if(control_client::exit_code(description->view())!=0) return output(std::move(description));
    auto card=description->view().at("result");
    if(card.at("eligible").boolean()!=true) return failure(3,"OperationNotEligible");
    auto hash=card.at("contract_digest").string(); if(!hash) return failure(6,"InvalidCatalog"); digest=*hash;
  }
  auto encoded=parameters->encode(); if(!encoded) return failed(encoded.error());
  auto request=data::Payload::parse("{\"operation\":{\"name\":"+quoted(operation)+",\"version\":"+quoted(version)+"},\"contract_digest\":"+quoted(digest)+",\"args\":"+*encoded+"}");
  if(!request) return failure(2,"InvalidRequest");
  if(!intent_file.empty()) {
    auto saved=control_client::ensure_intent(std::filesystem::u8path(intent_file),client->hello(),*request);
    if(!saved) return failed(saved.error());
  }
  return output(client->call("operation.invoke",*request,interruption.get_token()));
} catch(const std::exception &e) { std::cerr << e.what() << '\n'; return failure(6,"ClientFailure"); }

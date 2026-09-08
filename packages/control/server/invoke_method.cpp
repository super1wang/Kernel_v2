#include <ock/control/invoke_method.hpp>
namespace ock::control {
namespace {
Result<data::Payload> rejected(foundation::Error reason){
  return encode_invoke<void>(contracts::Rejected{reason},[](auto const &){return data::Payload::parse("null");});
}
Result<contracts::ContractDigest> digest(data::ValueView view){
  auto text=view.string();if(!text || text->size()!=64)return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
  constexpr std::string_view hex="0123456789abcdef";contracts::ContractDigest value{};
  for(std::size_t i=0;i<32;++i){auto a=hex.find((*text)[2*i]),b=hex.find((*text)[2*i+1]);
    if(a==hex.npos || b==hex.npos)return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
    value.bytes[i]=std::byte((a<<4)|b);
  }return value;
}
}
Result<Method> InvokeMethod::create(std::vector<InvocationBinding> bindings,
    std::shared_ptr<runtime::policy::ClockPort> clock,std::chrono::milliseconds timeout,std::uint64_t work){
  if(!clock || bindings.empty() || bindings.size()>128 || timeout.count()<=0 || timeout.count()>30000 || !work || work>1024)
    return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
  for(std::size_t i=0;i<bindings.size();++i)for(std::size_t j=0;j<i;++j)
    if(bindings[i].selector_.operation==bindings[j].selector_.operation)
      return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
  auto schema=binding::CompiledSchema::compile(R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","properties":{"operation":{"type":"object","properties":{"name":{"type":"string","minLength":1,"maxLength":96},"version":{"type":"string","minLength":1,"maxLength":128}},"required":["name","version"],"additionalProperties":false},"contract_digest":{"type":"string","pattern":"^[0-9a-f]{64}$"},"args":{}},"required":["operation","contract_digest","args"],"additionalProperties":false})");
  if(!schema)return foundation::make_unexpected(schema.error());
  return Method{"operation.invoke",*schema,std::shared_ptr<InvokeMethod>(new InvokeMethod(std::move(bindings),std::move(clock),timeout,work))};
}
Result<data::Payload> InvokeMethod::call(const runtime::policy::VerifiedCaller &caller,data::ValueView params){
  if(stop_.stop_requested())return rejected(error(ProtocolErrc::Closed));
  auto operation=params.at("operation");auto name=operation.at("name").string(),version=operation.at("version").string();
  auto hash=digest(params.at("contract_digest"));
  if(!name || !version || !hash || params.at("args").missing())return rejected(error(ProtocolErrc::InvalidRequest));
  auto parsed_name=foundation::Name::parse(*name);auto parsed_version=contracts::OperationVersion::parse(*version,128);
  if(!parsed_name || !parsed_version)return rejected(error(ProtocolErrc::InvalidRequest));
  contracts::OperationKey key{*parsed_name,*parsed_version};
  for(auto &binding:bindings_)if(binding.selector_.operation==key){
    if(binding.selector_.contract!=*hash)return rejected(runtime::invocation::invocation_error(runtime::invocation::InvocationErrc::ContractMismatch));
    auto now=clock_->now();
    if(now>runtime::policy::TimePoint::max()-timeout_)return rejected(error(ProtocolErrc::BudgetExceeded));
    return binding.invoker_->run(caller,params.at("args"),{stop_.get_token(),now+timeout_,work_});
  }
  return rejected(runtime::invocation::invocation_error(runtime::invocation::InvocationErrc::InvalidBinding));
}
} // namespace ock::control

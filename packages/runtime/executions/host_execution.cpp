#include "host_execution.hpp"

namespace ock::runtime::host {
Result<std::shared_ptr<HostExecutionPort>> make_executions(HostIncarnation id,
    std::shared_ptr<ExecutorControlPort> executor,const ExecutionOptions& options) {
  if(id.empty()||!executor||executor.use_count()==0)
    return make_unexpected(host_error(HostErrc::InvalidOwner));
  // 组合配置也有明确上限；复制之前拒绝超限，不交给客户端控制。
  if(options.resources.size()>256||options.slots.size()>256||options.aliases.size()>1024||
     options.subjects.empty()||options.subjects.size()>256||!options.active||!options.control_batch)
    return make_unexpected(host_error(HostErrc::InvalidInput));
  try {
    std::shared_ptr<resources::ResourceManager> manager;
    if(!options.slots.empty()) {
      auto made=resources::ResourceManager::create(options.slots,options.aliases,options.resource_options);
      if(!made)return make_unexpected(made.error());
      manager=std::move(*made);
    } else if(!options.resources.empty()||!options.aliases.empty())
      return make_unexpected(host_error(HostErrc::InvalidInput));
    for(std::size_t i=0;i<options.resources.size();++i) {
      const auto& resource=options.resources[i];
      if(resource.ref.module.view().empty()||resource.ref.name.view().empty()||resource.claims.empty()||
         resource.claims.size()>options.resource_options.max_claims)
        return make_unexpected(host_error(HostErrc::InvalidInput));
      for(std::size_t j=0;j<i;++j)if(resource.ref==options.resources[j].ref)
        return make_unexpected(host_error(HostErrc::InvalidInput));
      // 此时是未发布的新 manager；完整复用它的 key/alias/数量/阶段验证。
      auto validated=manager->acquire(resource.claims,resources::Phase::Compute);
      if(!validated)return make_unexpected(validated.error());
      foundation::invariant(bool(validated->lease)&&!validated->waiter);
    }
    auto frozen=std::make_shared<const std::vector<ExecutionResource>>(options.resources);
    auto resolve=[frozen,limit=options.resource_options.max_claims](std::span<const registry::ResourceRef> refs)
        -> Result<std::vector<resources::Claim>> {
      std::vector<resources::Claim> claims;
      for(const auto& ref:refs) {
        auto found=std::find_if(frozen->begin(),frozen->end(),[&](const auto& value){return value.ref==ref;});
        if(found==frozen->end())return make_unexpected(resources::error(resources::Errc::UnknownKey));
        if(found->claims.size()>limit-claims.size())return make_unexpected(resources::error(resources::Errc::Full));
        claims.insert(claims.end(),found->claims.begin(),found->claims.end());
      }
      return claims;
    };
    auto made=executions::detail::HostedExecutions::create(id,std::move(executor),std::move(manager),
        options.subjects,options.limits,options.scheduling,
        {options.active,options.control_batch,options.children_per_execution,options.max_depth,options.required_record},std::move(resolve));
    if(!made)return make_unexpected(made.error());
    return std::shared_ptr<HostExecutionPort>(std::move(*made));
  } catch(const std::bad_alloc&) {return make_unexpected(host_error(HostErrc::BudgetExceeded));}
    catch(...) {return make_unexpected(host_error(HostErrc::CallbackException));}
}
}

#pragma once
#include "allocation_probe.hpp"
#include "shape_cases.hpp"
#include <cstdlib>
#include <malloc.h>
namespace native_test {
struct AllocationReport {
  struct Sample {const char* name;allocation::Counts counts;bool success;};
  std::array<Sample,128> rows{};std::size_t size=0;
  void add(const char* name,allocation::Counts counts,bool success=true){CHECK(size<rows.size());rows[size++]={name,counts,success};}
  void emit(const char* test,bool checked) const {
    std::cout<<"{\"format\":\"ock.native-allocation/1\",\"case\":\""<<test<<"\",\"coverage\":{";
    std::cout<<"\"cpp_new\":true,\"debug_crt\":"<<(allocation::crt_available()?"true":"false")
      <<",\"debug_crt_effective\":\""<<(!allocation::crt_available()?"unavailable":allocation::asan_available()?"unobserved_asan_intercepted":"full")<<'"'
      <<",\"asan_allocator_hooks\":"<<(allocation::asan_available()?"true":"false")
      <<",\"iterator_debug_level\":"<<_ITERATOR_DEBUG_LEVEL
      <<",\"modules\":[\"consumer\",\"Invocation\",\"Policy\",\"Registry\",\"CoreContracts\",\"Foundation\"],"
      <<"\"window\":\"current thread complete synchronous invoke including outcome observation and destruction\","
      <<"\"blind_spots\":[\"Release CRT malloc family\",\"DLL private allocators\",\"custom heaps\",\"background threads unsupported\"],"
      <<"\"scope\":\"fixed small Args/Value zero resource declaration prewarmed bounded slots; channels not added together\","
      <<"\"retained_channel\":\"executable replacement C++ new/delete CRT usable bytes; allocator metadata and DLL/private heaps excluded; no concurrent background activity in these samples\","
      <<"\"asan_hook_scope\":\"post-registration current-thread allocator events; not startup, process total, DLL/private heap or background coverage\","
      <<"\"crt_missing_positive_entries\":";
    if(allocation::crt_available()&&std::string_view(test)=="T23.native.allocation_probe"){
      std::cout<<'[';bool comma=false;for(std::size_t i=0;i<12;++i)if(!rows[i].counts.crt){
        if(comma)std::cout<<',';std::cout<<'"'<<rows[i].name<<'"';comma=true;}
      std::cout<<']';
    }else std::cout<<"null";
    std::cout<<"},\"samples\":[";
    for(std::size_t i=0;i<size;++i){if(i)std::cout<<',';const auto& r=rows[i];std::cout<<"{\"name\":\""<<r.name<<"\",\"cpp\":"<<r.counts.cpp
      <<",\"crt\":";if(allocation::crt_available())std::cout<<r.counts.crt;else std::cout<<"null";
      std::cout<<",\"asan_allocations\":";if(allocation::asan_available())std::cout<<r.counts.asan_allocations;else std::cout<<"null";
      std::cout<<",\"asan_frees\":";if(allocation::asan_available())std::cout<<r.counts.asan_frees;else std::cout<<"null";
      std::cout<<",\"cpp_frees\":"<<r.counts.frees<<",\"cpp_allocated_bytes\":"<<r.counts.allocated_bytes
        <<",\"cpp_released_bytes\":"<<r.counts.released_bytes
        <<",\"cpp_live_bytes_before\":"<<r.counts.live_bytes_before
        <<",\"cpp_live_bytes_after\":"<<r.counts.live_bytes_after
        <<",\"cpp_peak_live_bytes\":"<<r.counts.peak_live_bytes
        <<",\"cpp_live_blocks_before\":"<<r.counts.live_blocks_before
        <<",\"cpp_live_blocks_after\":"<<r.counts.live_blocks_after
        <<",\"success\":"<<(r.success?"true":"false")<<'}';}
    std::cout<<"],\"checks\":{\"verified\":"<<(checked?"true":"false")<<"}}\n";
  }
};
inline bool zero(allocation::Counts c){return c.cpp==0&&(!allocation::crt_available()||c.crt==0)
    &&(!allocation::asan_available()||c.asan_allocations==0);}
inline void allocation_probe() {
  allocation::initialize();allocation::initialize();AllocationReport report;
  for(int kind=0;kind<12;++kind){
    // realloc 的前置块在窗口外取得，避免 malloc/calloc 的事件掩盖漏计。
    void* seed=kind==10?std::malloc(37):nullptr;
    if(kind==10)CHECK(seed);
    allocation::start();void* p=nullptr;
    switch(kind){
      case 0:p=::operator new(37);::operator delete(p);break;
      case 1:p=::operator new[](37);::operator delete[](p);break;
      case 2:p=::operator new(64,std::align_val_t{64});::operator delete(p,std::align_val_t{64});break;
      case 3:p=::operator new[](64,std::align_val_t{64});::operator delete[](p,std::align_val_t{64});break;
      case 4:p=::operator new(37,std::nothrow);::operator delete(p,std::nothrow);break;
      case 5:p=::operator new[](37,std::nothrow);::operator delete[](p,std::nothrow);break;
      case 6:p=::operator new(64,std::align_val_t{64},std::nothrow);::operator delete(p,std::align_val_t{64},std::nothrow);break;
      case 7:p=::operator new[](64,std::align_val_t{64},std::nothrow);::operator delete[](p,std::align_val_t{64},std::nothrow);break;
      case 8:p=std::malloc(37);std::free(p);break;
      case 9:p=std::calloc(7,9);std::free(p);break;
      case 10:p=std::realloc(seed,128);std::free(p?p:seed);break;
      case 11:p=_aligned_malloc(64,64);_aligned_free(p);break;
    }
    const char* names[]={"new","new_array","aligned_new","aligned_array","nothrow_new","nothrow_array","nothrow_aligned","nothrow_aligned_array","malloc","calloc","realloc","aligned_malloc"};
    auto c=allocation::stop();CHECK(p);if(kind<8){CHECK(c.cpp>0);CHECK(c.frees==c.cpp);
      CHECK(c.allocated_bytes>=37&&c.released_bytes==c.allocated_bytes);
      CHECK(c.live_bytes_after==c.live_bytes_before&&c.live_blocks_after==c.live_blocks_before);
      CHECK(c.peak_live_bytes>=c.live_bytes_before+37);}
    else {CHECK(c.cpp==0&&c.frees==0&&c.allocated_bytes==0&&c.released_bytes==0);
      CHECK(c.live_bytes_after==c.live_bytes_before);}
    if(allocation::asan_available()){CHECK(c.asan_allocations==1);CHECK(c.crt==0);}
    else if(allocation::crt_available())CHECK(c.crt>0);
    report.add(names[kind],c);
  }
  allocation::start();volatile int n=7;n=n+2;auto c=allocation::stop();CHECK(zero(c)&&c.asan_frees==0);report.add("negative_no_allocation",c);
  report.emit("T23.native.allocation_probe",true);
}
inline void allocation_steady() {
  allocation::initialize();AllocationReport report;
  NativeBudget budget;budget.observation_capacity=2;
  Env e(false,compute,budget,value_registration);auto b=bind_value(e);CHECK(b);
  for(int n=0;n<4;++n)CHECK(std::holds_alternative<Completed<Value>>(b->invoke({n},e.options())));
  entered=0;input_checks=0;output_checks=0;bool all=true;
  for(int n=0;n<40;++n){auto options=e.options();bool success=false;
    allocation::start();{auto r=b->invoke({n},options);if(auto* c=std::get_if<Completed<Value>>(&r))
      if(auto* read=std::get_if<ReadCompleted<Value>>(&c->outcome.value()))success=bool(read->result)&&read->result->value==n+2;}
    auto c=allocation::stop();report.add("complete_steady_invoke",c,success);all=all&&success&&zero(c)
      &&c.frees==0&&c.asan_frees==0&&c.live_bytes_before==c.live_bytes_after&&c.peak_live_bytes==c.live_bytes_before;
  }
  all=all&&entered==40&&input_checks==40&&output_checks>=40&&e.engine->snapshot({}).dropped==42;
  report.emit("T23.native.allocation_steady",all);CHECK(all);
}
inline void allocation_governance() {
  allocation::initialize();AllocationReport report;
  for(int mode=0;mode<3;++mode){Env e(false,compute,{},value_registration);auto b=bind_value(e);CHECK(b);
    CHECK(std::holds_alternative<Completed<Value>>(b->invoke({1},e.options())));entered=0;
    if(mode==1)CHECK(e.policy.assembly.administration->replace_principal_policy({policy_test::principal(),{}}));
    if(mode==2)CHECK(e.policy.assembly.administration->set_lifecycle(policy_test::target(),2));
    auto options=e.options();bool rejected=false;allocation::start();{auto r=b->invoke({mode==0?-1:2},options);rejected=std::holds_alternative<Rejected>(r);}
    auto counts=allocation::stop();report.add(mode==0?"invalid_input":mode==1?"revoked":"lifecycle_changed",counts,rejected);CHECK(rejected&&entered==0);
  }
  report.emit("T23.native.allocation_governance",true);
}
inline Result<std::size_t> own_targets(const Own&,std::span<foundation::ObjectId> out) noexcept {out[0]=policy_test::target();return 1;}
inline void allocation_other_costs() {
  allocation::initialize();AllocationReport report;
  ResourceLease borrowed_resource;const ResourceLease* borrowed_view=&borrowed_resource;
  auto borrowed_budget=*foundation::CheckedCount<std::uint64_t>::create(0,5);
  const auto borrowed_label=name("borrowed.count");bool borrowed_valid=false;
  allocation::start();{
    WorkContext context({},policy::TimePoint{},borrowed_budget,borrowed_label,BorrowedResourceViews{std::span(&borrowed_view,1)});
    borrowed_valid=context.granted_resources().size()==1&&context.granted_resources()[0]==&borrowed_resource;
  }
  auto borrowed_count=allocation::stop();report.add("borrowed_nonempty_context_only",borrowed_count,borrowed_valid);
  CHECK(borrowed_valid&&zero(borrowed_count)); // 独立栈上下文，不声称非空资源 Native 可执行。
  allocation::start();auto e=std::make_unique<Env>();auto count=allocation::stop();report.add("engine_registration_session",count);CHECK(count.cpp>0);
  allocation::start();auto bound=e->bind();count=allocation::stop();CHECK(bound);report.add("bind",count);CHECK(count.cpp>0);
  std::optional<NativeBound<int,int>> b{std::move(*bound)};
  std::weak_ptr<NativeEngine> engine=e->engine;
  std::weak_ptr<const registry::Catalog> catalog=e->catalog;
  auto options=e->options();bool first_success=false;allocation::start();
  {auto reply=b->invoke(1,options);if(auto* completed=std::get_if<Completed<int>>(&reply))
    if(auto* read=std::get_if<ReadCompleted<int>>(&completed->outcome.value()))
      first_success=bool(read->result)&&*read->result==3;}
  count=allocation::stop();report.add("first_invoke",count,first_success);CHECK(first_success);
  for(int n=0;n<4;++n){bool success=false;allocation::start();{auto reply=b->invoke(n,options);success=result(reply)==n+2;}
    count=allocation::stop();report.add("finite_warmup",count,success);CHECK(success);}
  allocation::start();{auto error=Error::with_details(invocation_error(InvocationErrc::InvalidOutput).code(),std::string(400,'x'));}count=allocation::stop();report.add("error_details",count);CHECK(count.cpp>0);
  Env variable(false,compute,{},[](registry::ModuleInput& m){m.register_operations=[](registry::Registrar& r){CHECK(r.compute(own_handler,native_definition(),native_options()));};});
  auto own=variable.engine->bind<Own,Own>(key(),{},Shape::Read,variable.policy.caller,std::array{policy_test::target()},own_targets,name("variable"));CHECK(own);
  Own input{std::string(4096,'v')};options=variable.options();bool success=false;allocation::start();
  {auto reply=own->invoke(input,options);if(auto* completed=std::get_if<Completed<Own>>(&reply))
    if(auto* read=std::get_if<ReadCompleted<Own>>(&completed->outcome.value()))
      success=bool(read->result)&&read->result->text==input.text;}
  count=allocation::stop();report.add("variable_result",count,success);CHECK(success&&count.cpp>0);
  allocation::start();e.reset();count=allocation::stop();report.add("release_external_engine_wrapper",count);
  // NativeEngine 外壳已销毁，Bound 继续持有内部状态和观察表，仍可调用。
  CHECK(result(b->invoke(1,options))==3);
  CHECK(engine.expired()&&!catalog.expired());
  allocation::start();b.reset();count=allocation::stop();
  const bool reclaimed=engine.expired()&&catalog.expired()&&count.frees>0&&count.released_bytes>0
      &&count.live_bytes_after<count.live_bytes_before&&count.live_blocks_after<count.live_blocks_before;
  report.add("final_bound_engine_state_catalog_observation_reclaimed",count,reclaimed);CHECK(reclaimed);
  allocation::start();engine.reset();catalog.reset();count=allocation::stop();
  report.add("final_weak_control_blocks_reclaimed",count);CHECK(count.frees>=2&&count.released_bytes>0);
  report.emit("T23.native.allocation_other_costs",true);
}
inline int allocation_injected_red() {
  allocation::initialize();AllocationReport report;allocation::start();
  auto* p=::operator new(71);::operator delete(p);auto count=allocation::stop();
  CHECK(count.cpp>0&&(!allocation::asan_available()||count.asan_allocations==1));
  report.add("intentional_single_allocation",count);report.emit("--allocation-injected-red",zero(count));
  return zero(count)?0:1;
}
inline int allocation_registration_failure(){return allocation::registration_failure_probe();}
}

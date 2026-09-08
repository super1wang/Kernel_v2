#include "channel.hpp"
#include "allocation_trace.hpp"
#ifdef OCK_FOOTPRINT_THREAD_ORIGIN
#include "thread_origin.hpp"
#endif
#ifdef OCK_FOOTPRINT_NATIVE
#include "native_fixture.hpp"
#else
#include "baseline_fixture.hpp"
#endif
#include <cstdio>
#include <optional>
int main(int argc,char** argv){
  footprint::Trace trace;
  try{
    footprint::Channel channel(argc,argv);channel.stage(0,0,0);trace.initialize();channel.stage(1,0,0);
    std::optional<native_service::Scenario> scenario;
    trace.measure("construct_register_start",0,false,[&]{scenario.emplace();});channel.stage(2,scenario->owner_mask(),0);
    trace.measure("log_proof_open_verify_bind",0,false,[&]{scenario->prepare();});
    trace.measure("first_compute",0,false,[&]{scenario->call(0);});
    trace.measure("first_read",0,false,[&]{scenario->call(1);});
    trace.measure("invalid_input",0,false,[&]{scenario->prove_invalid();});
    for(unsigned i=0;i<4;++i)trace.measure("warmup",i,false,[&]{scenario->call(i);});
#ifdef OCK_FOOTPRINT_THREAD_ORIGIN
    footprint::thread_origin_diagnostic();
#endif
    channel.stage(3,scenario->owner_mask(),0);channel.stage(4,scenario->owner_mask(),0);
    for(unsigned i=0;i<40;++i)trace.measure("invoke",i,true,[&]{footprint::inject(i);scenario->call(i);});
    channel.stage(5,scenario->owner_mask(),0);trace.measure("shutdown",0,false,[&]{scenario->shutdown();});channel.stage(6,scenario->owner_mask(),scenario->sentinels());
    trace.measure("bound_release",0,false,[&]{scenario->release_bound();});channel.stage(7,scenario->owner_mask(),scenario->sentinels());
    trace.measure("session_release",0,false,[&]{scenario->release_session();});channel.stage(8,scenario->owner_mask(),scenario->sentinels());
    unsigned sentinels=0;trace.measure("last_owner_release",0,false,[&]{scenario->release_owners();sentinels=scenario->sentinels();scenario.reset();});
    channel.stage(9,0,sentinels);channel.stage(10,0,sentinels);
#ifdef OCK_FOOTPRINT_NATIVE
    std::printf(R"({"kind":"native","warmup_calls":4,"steady_calls":40,"handler_entries":46,"counter_mode":"%s","logging_mode":"default-memory","runtime_workers":0,"checks":{"ready_gate":true,"read":true,"compute":true,"invalid_input":true,"async_unavailable":true,"start_log_flush":true,"public_log_page":true,"ordinary_invoke_no_log":true,"shutdown":true,"stopped_bound":true,"bound_release":true,"session_release":true,"last_owner_release":true}})" "\n",footprint::counter_mode);
#else
    std::printf(R"({"kind":"baseline","warmup_calls":4,"steady_calls":40,"local_calls":46,"counter_mode":"%s","logging_mode":"none","runtime_workers":0,"checks":{"local_values":true,"shutdown":true,"last_owner_release":true}})" "\n",footprint::counter_mode);
#endif
    trace.emit(native_service::Scenario::kind,true);return trace.verified()?0:1;
  }catch(const std::exception& error){trace.emit(native_service::Scenario::kind,false);std::fprintf(stderr,"%s\n",error.what());return 1;}
}

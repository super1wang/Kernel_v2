#include "../consumer/channel.hpp"
#include "../../../examples/embedded_service/fixture.hpp"
#include <cstdio>
#include <optional>

using namespace ock;
using namespace ock::contracts;
using namespace ock::runtime::observability;

// 独立消费者的操作级诊断；Host 本身的默认日志与执行语义不变。
struct Trace {
  unsigned mode;
  const char* path;
  std::optional<MemoryLoggingBundle> memory;
  std::optional<SafeLogger> logger;
  std::jthread writer;
  HANDLE file=INVALID_HANDLE_VALUE;
  std::atomic<bool> failed=false;
  std::uint64_t attempted=0,accepted=0,rejected=0,written=0,gaps=0,evicted=0;
  LogPosition position{embedded::id<HostIncarnation>(),2,0};
  Trace(unsigned selected,const char* output):mode(selected),path(output){}
  ~Trace(){stop();if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
  void stop() noexcept {if(writer.joinable()){writer.request_stop();writer.join();}}
  bool drain() {
    std::array<PublicLogRecord,16> records;
    auto page=memory->diagnostics->copy_records(position,records);
    if(!page){
      if(page.error().code()==log_error(LogErrc::Busy).code())return false;
      failed=true;return false;
    }
    if(page->gap)++gaps;
    position=page->next;
    for(unsigned i=0;i<page->copied;++i){
      DWORD count=0;
      if(!WriteFile(file,records[i].text.data(),records[i].text_size,&count,nullptr)||count!=records[i].text_size){failed=true;return false;}
      if(!WriteFile(file,"\n",1,&count,nullptr)||count!=1){failed=true;return false;}
      ++written;
    }
    return position.accepted_sequence>=page->accepted_upper.accepted_sequence;
  }
  void begin(){
    if(!mode)return;
    auto made=make_memory_logging(position.host,2,{128,LogOverflow::DropOldest,LogLevel::Trace});
    embedded::require(bool(made),"trace memory");memory.emplace(std::move(*made));
    auto safe=SafeLogger::create(memory->writer);embedded::require(bool(safe),"trace facade");logger.emplace(std::move(*safe));
    if(mode<2)return;
    file=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    embedded::require(file!=INVALID_HANDLE_VALUE,"new trace file");
    writer=std::jthread([this](std::stop_token stop){
      try {
        while(!stop.stop_requested()&&!failed){drain();std::this_thread::sleep_for(std::chrono::milliseconds(mode==3?10:1));}
        const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!failed&&!drain()){
          if(std::chrono::steady_clock::now()>=end){failed=true;break;}
          std::this_thread::yield();
        }
      }catch(...){failed=true;}
    });
  }
  void record(unsigned category,unsigned index,bool end){
    if(!mode)return;
    const std::array fields{LogField{LogKey::Status,LogValueClass::PublicCode,category*2u+unsigned(end)},
      LogField{LogKey::Count,LogValueClass::PublicCount,index}};
    ++attempted;
    const auto result=logger->try_write({LogLevel::Trace,LogComponent::Invocation,LogEvent::Diagnostic,fields});
    if(result.decision==LogDecision::Accepted)++accepted;else ++rejected;
  }
  void finish(){
    if(!mode)return;
    stop();embedded::require(!failed,"trace file drain");
    auto snapshot=logger->snapshot();embedded::require(bool(snapshot),"trace snapshot");
    evicted=snapshot->evicted_through;
    embedded::require(snapshot->counters.accepted==accepted&&accepted+rejected==attempted,"trace accounting");
    embedded::require(bool(logger->close()),"trace close");
    if(mode>=2){embedded::require(FlushFileBuffers(file)!=0,"trace flush");CloseHandle(file);file=INVALID_HANDLE_VALUE;}
  }
};
struct Hooks {
  footprint::Channel& channel;
  Trace& trace;
  std::array<std::uint64_t,85> elapsed{};
  unsigned calls=0;
  void begin_construction(){trace.begin();}
  void ready(){}
  void stage(unsigned phase,unsigned owners,unsigned sentinels){
    if(phase==6)trace.finish();
    if(phase==9){trace.logger.reset();trace.memory.reset();}
    channel.stage(phase,owners,sentinels);
  }
  template<class F> void measure(const char* label,unsigned index,bool,F&& work){
    unsigned category=std::string_view(label)=="warmup"?0:std::string_view(label)=="invoke"?1:
      std::string_view(label)=="submit_wait_result"?2:3;
    embedded::require(calls<elapsed.size(),"finite operation trace");
    auto start=footprint::ticks();trace.record(category,index,false);work();trace.record(category,index,true);
    elapsed[calls++]=footprint::ticks()-start;
  }
};
int main(int argc,char** argv)try{
  const auto mode=footprint::argument(argc,argv,"--mode",0);embedded::require(mode<=3,"trace mode");
  const char* path=nullptr;
  for(int i=1;i+1<argc;++i)if(std::string_view(argv[i])=="--log")path=argv[i+1];
  embedded::require(mode<2||path,"file trace path");
  footprint::Channel channel(argc,argv);channel.stage(0,0,0);
  Trace trace(mode,path);Hooks hooks{channel,trace};
  FILETIME created,exited,kernel0,user0,kernel1,user1;
  embedded::require(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel0,&user0)!=0,"CPU start");
  embedded::run(hooks);
  embedded::require(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel1,&user1)!=0,"CPU end");
  auto ticks=[](FILETIME time){return (std::uint64_t(time.dwHighDateTime)<<32)|time.dwLowDateTime;};
  LARGE_INTEGER frequency{};embedded::require(QueryPerformanceFrequency(&frequency)!=0,"QPC frequency");
  embedded::require(hooks.calls==85&&trace.attempted==(mode?170:0),"complete operation trace");
  std::printf("{\"format\":\"ock.observed-embedded/1\",\"mode\":%u,\"calls\":%u,\"attempted\":%llu,\"accepted\":%llu,\"rejected\":%llu,\"evicted\":%llu,\"file_records\":%llu,\"read_gaps\":%llu,\"cpu_100ns\":%llu,\"qpc_frequency\":%llu,\"released\":true,\"operation_ticks\":[",
    mode,hooks.calls,trace.attempted,trace.accepted,trace.rejected,trace.evicted,trace.written,trace.gaps,
    ticks(kernel1)+ticks(user1)-ticks(kernel0)-ticks(user0),static_cast<std::uint64_t>(frequency.QuadPart));
  for(unsigned i=0;i<hooks.calls;++i)std::printf("%s%llu",i?",":"",hooks.elapsed[i]);
  std::puts("]}");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"%s\n",error.what());return 1;}

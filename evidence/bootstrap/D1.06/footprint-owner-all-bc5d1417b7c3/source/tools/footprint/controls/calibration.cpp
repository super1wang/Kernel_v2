#include "../consumer/channel.hpp"
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <thread>
extern "C" __declspec(dllimport) int footprint_required_value();
int main(int argc,char** argv){
  using namespace footprint;
  void* region=nullptr;HANDLE thread_stop=nullptr;std::thread worker;
  try{
    Channel channel(argc,argv);auto mode=control(argc,argv);
    require(mode=="none"||mode=="memory"||mode=="thread"||mode=="delay"||mode=="bad_nonce"||mode=="early_exit"||mode=="raw");
    if(mode=="early_exit")return 0;
    channel.stage(0,15,0,mode=="bad_nonce");channel.stage(1);
    if(mode=="memory"){
      auto bytes=argument(argc,argv,"--bytes",16777216);require(bytes>0&&bytes%4096==0);
      region=VirtualAlloc(nullptr,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);require(region!=nullptr);
      for(unsigned i=0;i<bytes;i+=4096)static_cast<volatile unsigned char*>(region)[i]=static_cast<unsigned char>(i/4096);
    }
    if(mode=="thread"){
      thread_stop=CreateEventW(nullptr,TRUE,FALSE,nullptr);require(thread_stop!=nullptr);
      worker=std::thread([&]{WaitForSingleObject(thread_stop,15000);});
    }
    if(mode=="delay"){
      auto delay=argument(argc,argv,"--delay-ms",100);require(delay>0&&delay<=5000);
      LARGE_INTEGER frequency{};require(QueryPerformanceFrequency(&frequency)!=0);
      auto until=ticks()+static_cast<std::uint64_t>(frequency.QuadPart)*delay/1000;
      while(ticks()<until)Sleep(1);
    }
    require(17*3+2==footprint_required_value());channel.stage(2);
    for(unsigned i=0;i<4;++i)require(i+2==2+i);
    channel.stage(3);channel.stage(4);channel.stage(5);
    if(worker.joinable()){SetEvent(thread_stop);worker.join();CloseHandle(thread_stop);thread_stop=nullptr;}
    if(region){require(VirtualFree(region,0,MEM_RELEASE)!=0);region=nullptr;}
    channel.stage(6,15,1);channel.stage(7,14,1);channel.stage(8,12,1);channel.stage(9,0,3);channel.stage(10,0,3);
    if(mode=="raw"){
      _setmode(_fileno(stdout),_O_BINARY);_setmode(_fileno(stderr),_O_BINARY);
      const unsigned char out[]{0,0xff,'o','u','t',13,10};const unsigned char err[]{0xfe,0,'e','r','r',10};
      std::fwrite(out,1,sizeof(out),stdout);std::fwrite(err,1,sizeof(err),stderr);
    }
    return 0;
  }catch(...){if(worker.joinable()){SetEvent(thread_stop);worker.join();}if(thread_stop)CloseHandle(thread_stop);if(region)VirtualFree(region,0,MEM_RELEASE);return 1;}
}

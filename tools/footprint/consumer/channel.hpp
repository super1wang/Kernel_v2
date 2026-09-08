#pragma once
// 与 Python 固定80字节协议一致；只用于测量消费者，不进入产品 SDK。
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace footprint {
inline void require(bool value){if(!value)throw std::runtime_error("footprint consumer check failed");}
inline std::uint64_t ticks(){LARGE_INTEGER value{};require(QueryPerformanceCounter(&value)!=0);return static_cast<std::uint64_t>(value.QuadPart);}
#pragma pack(push,1)
struct Record {
  std::uint32_t version,size;
  std::array<unsigned char,16> nonce;
  std::uint32_t pid,phase,sequence,reserved;
  std::uint64_t ticks,checks,owners,sentinels,generation;
};
#pragma pack(pop)
static_assert(sizeof(Record)==80);
class Channel final {
public:
  Channel(int argc,char** argv){
    int index=0;for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--ock-observation")index=i;
    require(index>0&&index+5<argc);
    mapping_=reinterpret_cast<HANDLE>(std::strtoull(argv[index+1],nullptr,10));
    event_=reinterpret_cast<HANDLE>(std::strtoull(argv[index+2],nullptr,10));
    ack_=reinterpret_cast<HANDLE>(std::strtoull(argv[index+3],nullptr,10));
    auto nonce=std::string_view(argv[index+4]);require(nonce.size()==32);
    auto digit=[](char c)->unsigned{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("bad nonce");};
    for(unsigned i=0;i<16;++i)nonce_[i]=static_cast<unsigned char>(digit(nonce[2*i])*16+digit(nonce[2*i+1]));
    generation_=std::strtoull(argv[index+5],nullptr,10);require(generation_!=0);
    view_=static_cast<Record*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(Record)));require(view_!=nullptr);
  }
  ~Channel(){if(view_)UnmapViewOfFile(view_);if(ack_)CloseHandle(ack_);if(event_)CloseHandle(event_);if(mapping_)CloseHandle(mapping_);}
  Channel(const Channel&)=delete;
  void stage(unsigned phase,std::uint64_t owners=15,std::uint64_t sentinels=0,bool wrong_nonce=false){
    Record value{1,sizeof(Record),nonce_,GetCurrentProcessId(),phase,phase+1,0,ticks(),1,owners,sentinels,generation_};
    if(wrong_nonce)value.nonce[0]^=1;
    *view_=value;MemoryBarrier();require(SetEvent(event_)!=0);
    require(WaitForSingleObject(ack_,10000)==WAIT_OBJECT_0);
  }
private:
  HANDLE mapping_{},event_{},ack_{};
  Record* view_{};
  std::array<unsigned char,16> nonce_{};
  std::uint64_t generation_{};
};
inline unsigned argument(int argc,char** argv,std::string_view name,unsigned fallback){
  for(int i=1;i+1<argc;++i)if(argv[i]==name){char* end{};auto n=std::strtoul(argv[i+1],&end,10);require(end&&!*end&&n<=134217728);return static_cast<unsigned>(n);}return fallback;
}
inline std::string_view control(int argc,char** argv){for(int i=1;i+1<argc;++i)if(std::string_view(argv[i])=="--control")return argv[i+1];return "none";}
}

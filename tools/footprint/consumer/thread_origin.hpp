#pragma once
// 独立来源诊断；正常测量目标不包含此头，不计入任何占用或时延样本。
#include "channel.hpp"
#include <processsnapshot.h>
#include <psapi.h>
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace footprint {
inline void thread_origin_diagnostic() {
  struct Held {
    std::atomic<bool> release{false};
    std::atomic<DWORD> id{0};
    std::thread worker;
    ~Held(){release.store(true);if(worker.joinable())worker.join();}
  } held;
#ifdef OCK_FOOTPRINT_HELD_THREAD
  held.worker=std::thread([&]{held.id.store(GetCurrentThreadId());while(!held.release.load())Sleep(1);});
  const auto deadline=GetTickCount64()+5000;
  while(held.id.load()==0){require(GetTickCount64()<deadline);Sleep(1);}
#endif
  struct Snapshot {
    HPSS value{};HPSSWALK marker{};
    ~Snapshot(){
      if(marker){auto code=PssWalkMarkerFree(marker);if(code)std::fprintf(stderr,"thread_origin_cleanup_marker_error=%lu\n",code);}
      if(value){auto code=PssFreeSnapshot(GetCurrentProcess(),value);if(code)std::fprintf(stderr,"thread_origin_cleanup_snapshot_error=%lu\n",code);}
    }
  } snapshot;
  const DWORD capture=PssCaptureSnapshot(GetCurrentProcess(),PSS_CAPTURE_THREADS,0,&snapshot.value);
  if(capture!=ERROR_SUCCESS){std::fprintf(stderr,"thread_origin_capture_error=%lu\n",capture);require(false);}
  const DWORD marker=PssWalkMarkerCreate(nullptr,&snapshot.marker);
  if(marker!=ERROR_SUCCESS){std::fprintf(stderr,"thread_origin_marker_error=%lu\n",marker);require(false);}
  struct Row {DWORD pid,id,flags;std::uintptr_t address,base;DWORD image_bytes;FILETIME created;std::wstring path;};
  std::vector<Row> rows;rows.reserve(64);
  DWORD walk=ERROR_SUCCESS;
  for(;;){
    PSS_THREAD_ENTRY entry{};
    walk=PssWalkSnapshot(snapshot.value,PSS_WALK_THREADS,snapshot.marker,&entry,sizeof(entry));
    if(walk==ERROR_NO_MORE_ITEMS)break;
    if(walk!=ERROR_SUCCESS){std::fprintf(stderr,"thread_origin_walk_error=%lu\n",walk);require(false);}
    if(rows.size()>=64){std::fprintf(stderr,"thread_origin_capacity_exceeded limit=64 next_tid=%lu\n",entry.ThreadId);require(false);}
    if(entry.ProcessId!=GetCurrentProcessId()||entry.ThreadId==0){
      std::fprintf(stderr,"thread_origin_identity_error pid=%lu expected=%lu tid=%lu\n",entry.ProcessId,GetCurrentProcessId(),entry.ThreadId);require(false);
    }
    HMODULE module{};MODULEINFO info{};
    if(!entry.Win32StartAddress){std::fprintf(stderr,"thread_origin_null_start tid=%lu start=0\n",entry.ThreadId);require(false);}
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(entry.Win32StartAddress),&module)){
      const auto error=GetLastError();
      std::fprintf(stderr,"thread_origin_unresolved tid=%lu start=%llu error=%lu\n",entry.ThreadId,
        static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(entry.Win32StartAddress)),error);require(false);
    }
    if(!GetModuleInformation(GetCurrentProcess(),module,&info,sizeof(info))){
      const auto error=GetLastError();std::fprintf(stderr,"thread_origin_module_information_error tid=%lu error=%lu\n",entry.ThreadId,error);require(false);
    }
    auto path=std::make_unique<wchar_t[]>(32768);
    const DWORD size=GetModuleFileNameW(module,path.get(),32768);
    if(size==0||size>=32768){
      const auto error=GetLastError();std::fprintf(stderr,"thread_origin_module_path_error tid=%lu length=%lu capacity=32768 error=%lu\n",entry.ThreadId,size,error);require(false);
    }
    const auto address=reinterpret_cast<std::uintptr_t>(entry.Win32StartAddress);
    const auto base=reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
    if(address<base||address-base>=info.SizeOfImage){
      std::fprintf(stderr,"thread_origin_address_outside_module tid=%lu start=%llu base=%llu image_bytes=%lu\n",entry.ThreadId,
        static_cast<unsigned long long>(address),static_cast<unsigned long long>(base),info.SizeOfImage);require(false);
    }
    rows.push_back({entry.ProcessId,entry.ThreadId,static_cast<DWORD>(entry.Flags),address,base,info.SizeOfImage,entry.CreateTime,std::wstring(path.get(),size)});
  }
  const DWORD free_marker=PssWalkMarkerFree(snapshot.marker);snapshot.marker=nullptr;
  const DWORD free_snapshot=PssFreeSnapshot(GetCurrentProcess(),snapshot.value);snapshot.value=nullptr;
  if(free_marker||free_snapshot)std::fprintf(stderr,"thread_origin_free_errors=%lu,%lu\n",free_marker,free_snapshot);
  require(free_marker==ERROR_SUCCESS&&free_snapshot==ERROR_SUCCESS);
  unsigned held_found=0,main_found=0;
  for(const auto& row:rows){held_found+=row.id==held.id.load();main_found+=row.id==GetCurrentThreadId();}
  require(main_found==1&&held_found==(held.id.load()?1u:0u));
  held.release.store(true);if(held.worker.joinable())held.worker.join();
  // ASCII JSON，包括UTF-16路径转义；格式化在快照释放之后。
  std::fprintf(stderr,"{\"format\":\"ock.thread-origin/1\",\"pid\":%lu,\"main_tid\":%lu,\"held_tid\":%lu,\"held_joined\":true,\"capture\":%lu,\"walk_end\":%lu,\"free_marker\":%lu,\"free_snapshot\":%lu,\"threads\":[",
    GetCurrentProcessId(),GetCurrentThreadId(),held.id.load(),capture,walk,free_marker,free_snapshot);
  bool first=true;
  for(const auto& row:rows){
    std::fprintf(stderr,"%s{\"pid\":%lu,\"tid\":%lu,\"flags\":%lu,\"start\":%llu,\"module_base\":%llu,\"module_bytes\":%lu,\"rva\":%llu,\"created\":%llu,\"module\":\"",
      first?"":",",row.pid,row.id,row.flags,static_cast<unsigned long long>(row.address),static_cast<unsigned long long>(row.base),row.image_bytes,
      static_cast<unsigned long long>(row.address-row.base),(static_cast<unsigned long long>(row.created.dwHighDateTime)<<32)|row.created.dwLowDateTime);
    first=false;for(wchar_t value:row.path)std::fprintf(stderr,"\\u%04x",static_cast<unsigned>(value));std::fputs("\"}",stderr);
  }
  std::fputs("]}\n",stderr);
}
}

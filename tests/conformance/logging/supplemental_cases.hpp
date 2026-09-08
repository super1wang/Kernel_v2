#pragma once
#include "reference_backend.hpp"
#include "../../contract/native/allocation_probe.hpp"
#include <cstdlib>
#include <iostream>
#include <malloc.h>
namespace logging_test {
namespace allocation=native_test::allocation;

inline void memory_pages(){
  Fixture f(true,2);
  for(unsigned i=1;i<=3;++i)accepted(f.logger.try_write(input()),i);
  std::array<PublicLogRecord,64> output{};for(auto& r:output)r.text.fill('Q');
  auto first=f.diagnostics->copy_records(position(0),std::span(output).first(1));REQUIRE(first);
  REQUIRE(first->copied==1&&first->next==position(2)&&first->accepted_upper==position(3));
  REQUIRE(first->evicted_through==1&&first->gap&&output[0].position==position(2));REQUIRE(output[1].text[0]=='Q');
  for(unsigned i=4;i<=6;++i)accepted(f.logger.try_write(input()),i);
  auto second=f.diagnostics->copy_records(first->next,std::span(output).first(1));REQUIRE(second);
  REQUIRE(second->copied==1&&second->next==position(5)&&second->accepted_upper==position(6));
  REQUIRE(second->evicted_through==4&&second->gap&&output[0].position==position(5));
  auto third=f.diagnostics->copy_records(second->next,output);REQUIRE(third);
  REQUIRE(third->copied==1&&third->next==position(6)&&!third->gap&&output[0].position==position(6));REQUIRE(output[1].text[0]=='Q');
  auto empty=f.diagnostics->copy_records(third->next,output);REQUIRE(empty&&empty->copied==0&&empty->next==position(6)&&!empty->gap);
  auto before=f.logger.snapshot();REQUIRE(before);
  output[0].text[0]='Z';auto other=position(0);other.host=host(2);
  REQUIRE(is_error(f.diagnostics->copy_records(other,output),LogErrc::InvalidPosition));
  other=position(0);other.stream=2;REQUIRE(is_error(f.diagnostics->copy_records(other,output),LogErrc::InvalidPosition));
  REQUIRE(is_error(f.diagnostics->copy_records(position(7),output),LogErrc::InvalidPosition));
  REQUIRE(is_error(f.diagnostics->copy_records(position(0),{}),LogErrc::InvalidReadPage));
  std::array<PublicLogRecord,65> large{};large[0].text[0]='L';
  REQUIRE(is_error(f.diagnostics->copy_records(position(0),large),LogErrc::InvalidReadPage));
  REQUIRE(output[0].text[0]=='Z'&&large[0].text[0]=='L');
  auto after=f.logger.snapshot();REQUIRE(after);
  REQUIRE(after->accepted_through==before->accepted_through&&after->evicted_through==before->evicted_through&&after->retained_count==2);
  REQUIRE(f.logger.close());REQUIRE(f.diagnostics->copy_records(position(0),output));
  std::cout<<"assertion real_memory_page_formula_gap_intervening_eviction_unchanged_errors_closed_read\n";
}

struct AllocationRows {
  struct Row{const char* backend;const char* window;allocation::Counts counts;bool success;};
  std::array<Row,256> rows{};std::size_t count{};
  void add(const char* backend,const char* window,allocation::Counts counts,bool success){REQUIRE(count<rows.size());rows[count++]={backend,window,counts,success};}
  void emit(bool verified) const {
    std::cout<<"{\"format\":\"ock.logging-allocation/1\",\"case\":\"T23.logging.fixed_write_allocation\",\"coverage\":{\"cpp_new\":true,\"debug_crt\":"<<(allocation::crt_available()?"true":"false")
      <<",\"asan_allocator_hooks\":"<<(allocation::asan_available()?"true":"false")
      <<",\"debug_crt_effective\":\""<<(!allocation::crt_available()?"unavailable":allocation::asan_available()?"unobserved_asan_intercepted":"full")
      <<"\",\"window\":\"current-thread fixed LogInput through SafeLogger try_write and complete LogWriteResult destruction\",\"channels_added\":false,\"retained_scope\":\"replacement executable C++ new/delete usable bytes, not process PrivateUsage\",\"blind_spots\":[\"Release CRT malloc family\",\"DLL/private/custom heaps\",\"other threads\"]},\"samples\":[";
    for(std::size_t i=0;i<count;++i){const auto& r=rows[i];if(i)std::cout<<',';
      std::cout<<"{\"backend\":\""<<r.backend<<"\",\"window\":\""<<r.window<<"\",\"success\":"<<(r.success?"true":"false")
        <<",\"cpp\":"<<r.counts.cpp<<",\"crt\":";if(allocation::crt_available())std::cout<<r.counts.crt;else std::cout<<"null";
      std::cout<<",\"asan_allocations\":";if(allocation::asan_available())std::cout<<r.counts.asan_allocations;else std::cout<<"null";
      std::cout<<",\"asan_frees\":";if(allocation::asan_available())std::cout<<r.counts.asan_frees;else std::cout<<"null";
      std::cout<<",\"cpp_frees\":"<<r.counts.frees<<",\"allocated_bytes\":"<<r.counts.allocated_bytes<<",\"released_bytes\":"<<r.counts.released_bytes
        <<",\"live_before\":"<<r.counts.live_bytes_before<<",\"live_after\":"<<r.counts.live_bytes_after<<",\"peak_live\":"<<r.counts.peak_live_bytes<<'}';
    }
    std::cout<<"],\"verified\":"<<(verified?"true":"false")<<"}\n";
  }
};
inline bool allocation_zero(allocation::Counts c){return c.cpp==0&&(!allocation::crt_available()||c.crt==0)&&(!allocation::asan_available()||c.asan_allocations==0);}
inline void allocation_validity(AllocationRows& report){
  allocation::initialize();allocation::initialize();
  const char* names[]{"new","new_array","aligned_new","aligned_array","nothrow_new","nothrow_array","nothrow_aligned","nothrow_aligned_array","malloc","calloc","realloc","aligned_malloc"};
  for(unsigned kind=0;kind<12;++kind){
    void* seed=kind==10?std::malloc(37):nullptr;if(kind==10)REQUIRE(seed);
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
    auto c=allocation::stop();REQUIRE(p);
    if(kind<8)REQUIRE(c.cpp==1&&c.frees==1&&c.allocated_bytes>=37&&c.allocated_bytes==c.released_bytes&&c.live_bytes_before==c.live_bytes_after);
    else REQUIRE(c.cpp==0&&c.frees==0);
    if(allocation::asan_available())REQUIRE(c.asan_allocations==1&&c.crt==0);
    else if(allocation::crt_available())REQUIRE(c.crt>0);
    report.add("probe",names[kind],c,true);
  }
  allocation::start();volatile unsigned value=7;value=value+2;auto c=allocation::stop();REQUIRE(value==9&&allocation_zero(c));report.add("probe","negative_no_allocation",c,true);
}
inline void fixed_write_allocation(){
  AllocationRows report;allocation_validity(report);bool all=true;
  std::array<LogField,4> fields{{{LogKey::Detail,LogValueClass::Redacted,0x534543524554ULL},{LogKey::Count,LogValueClass::PublicCount,UINT64_MAX},{LogKey::Status,LogValueClass::PublicCode,65535},{LogKey::Module,LogValueClass::PublicCode,7}}};
  auto in=input();in.fields=fields;
  for(bool memory:{true,false}){
    const char* label=memory?"memory":"test";
    std::optional<Fixture> fixture;
    allocation::start();fixture.emplace(memory,64);auto initial=allocation::stop();report.add(label,"initialize",initial,true);all=all&&initial.cpp>0;
    auto write=[&](const char* window,std::uint64_t sequence,bool accepted_expected,bool require_zero){
      bool success=false;allocation::start();{
        auto result=fixture->logger.try_write(in);
        success=accepted_expected?(result.decision==LogDecision::Accepted&&result.reason==LogReason::None&&result.accepted==position(sequence)):
          (result.decision==LogDecision::Rejected&&result.reason==LogReason::Full&&!result.accepted);
      }auto c=allocation::stop();report.add(label,window,c,success);
      all=all&&success&&(!require_zero||(allocation_zero(c)&&c.frees==0&&c.asan_frees==0&&c.live_bytes_before==c.live_bytes_after));
    };
    write("first_write",1,true,false);
    for(unsigned i=2;i<=5;++i)write("finite_warmup",i,true,false);
    for(unsigned i=6;i<=45;++i)write("fixed_accepted_write",i,true,true);
    // 有限填满成本单列，之后40次真实持续满载；不假装空槽是满载。
    bool filled=true;allocation::start();for(unsigned i=46;i<=64;++i){auto r=fixture->logger.try_write(in);filled=filled&&r.decision==LogDecision::Accepted&&r.accepted==position(i);}auto filling=allocation::stop();report.add(label,"fill_to_capacity",filling,filled);all=all&&filled;
    for(unsigned i=65;i<=104;++i)write("sustained_full_write",i,memory,true);
    auto snapshot=fixture->logger.snapshot();all=all&&bool(snapshot)&&snapshot->retained_count==64&&snapshot->accepted_through==position(memory?104:64)&&snapshot->evicted_through==(memory?40:0);
    allocation::start();fixture.reset();auto released=allocation::stop();report.add(label,"last_owner_release",released,true);
    all=all&&released.cpp==0&&released.frees>0&&released.live_bytes_after==initial.live_bytes_before;
  }
  report.emit(all);REQUIRE(all);
}
}

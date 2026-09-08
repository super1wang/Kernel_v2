#include "reference_backend.hpp"
#include "supplemental_cases.hpp"
#include <iostream>
#include <map>
#include <thread>
#include <type_traits>
using namespace logging_test;

void accounting(bool memory) {
  REQUIRE(is_error(make_memory_logging(host(),1,{0}),LogErrc::InvalidConfiguration));
  REQUIRE(is_error(make_memory_logging(host(),1,{4097}),LogErrc::BudgetExceeded));
  REQUIRE(is_error(make_memory_logging({},1),LogErrc::InvalidConfiguration));
  REQUIRE(is_error(make_memory_logging(host(),0),LogErrc::InvalidConfiguration));
  REQUIRE(is_error(make_memory_logging(host(),1,{2,static_cast<LogOverflow>(9),LogLevel::Info}),LogErrc::InvalidConfiguration));
  REQUIRE(is_error(make_memory_logging(host(),1,{2,LogOverflow::DropOldest,static_cast<LogLevel>(9)}),LogErrc::InvalidConfiguration));
  auto defaults=make_memory_logging(host(),1);REQUIRE(defaults);REQUIRE(defaults->writer->snapshot()->limits.record_capacity==128);
  auto maximum=make_memory_logging(host(),1,{4096});REQUIRE(maximum);REQUIRE(maximum->writer->snapshot()->limits.record_capacity==4096);
  Fixture f(memory);
  auto dropped=f.logger.try_write(input(LogLevel::Debug));REQUIRE(dropped.decision==LogDecision::Dropped);REQUIRE(dropped.reason==LogReason::Filtered);REQUIRE(!dropped.accepted);
  auto bad=input();bad.event=static_cast<LogEvent>(99);rejected(f.logger.try_write(bad),LogReason::InvalidRecord);
  accepted(f.logger.try_write(input()),1);accepted(f.logger.try_write(input()),2);
  if(memory)accepted(f.logger.try_write(input()),3);else rejected(f.logger.try_write(input()),LogReason::Full);
  auto s=f.logger.snapshot();REQUIRE(s);REQUIRE(s->retained_count==2);REQUIRE(s->counters.accepted==(memory?3u:2u));REQUIRE(s->counters.evicted_after_accept==(memory?1u:0u));
  REQUIRE(s->counters.rejected==(memory?1u:2u));REQUIRE(s->counters.dropped_before_accept==1);REQUIRE(s->counters.rejected_invalid==1);
  auto c=f.logger.counters();REQUIRE(c);REQUIRE(c->write_attempts==5);REQUIRE(c->accepted+c->rejected+c->dropped==5);
  REQUIRE(f.logger.close());rejected(f.logger.try_write(input()),LogReason::Closed);
  REQUIRE(f.logger.snapshot()->counters.rejected_closed==1);
  std::cout<<"assertion quota_full_filter_closed_accounting\n";
  Fixture concurrent(memory,256);
  auto write=[&]{for(int i=0;i<30;++i)concurrent.logger.try_write(input());};
  std::thread t1(write),t2(write);t1.join();t2.join();
  auto q=concurrent.logger.snapshot();REQUIRE(q);REQUIRE(q->counters.accepted+q->counters.rejected==60);REQUIRE(q->counters.rejected==q->counters.rejected_busy);REQUIRE(q->accepted_through.accepted_sequence==q->counters.accepted);
  auto controller=std::make_shared<TestBackend>();auto guarded=SafeLogger::create(controller);controller->busy_write=true;rejected(guarded->try_write(input()),LogReason::Busy);controller->busy_write=false;REQUIRE(guarded->snapshot()->counters.rejected_busy==1);accepted(guarded->try_write(input()),1);
  std::cout<<"assertion finite_concurrency_busy_positive_control\n";
}
void format_redaction(bool memory) {
  Fixture f(memory,16);auto in=input();in.event=LogEvent::Ready;
  accepted(f.logger.try_write(in),1);
  std::array fields{LogField{LogKey::Detail,LogValueClass::Redacted,0x534543524554ULL},LogField{LogKey::Module,LogValueClass::PublicCode,7},LogField{LogKey::Count,LogValueClass::PublicCount,42},LogField{LogKey::Status,LogValueClass::PublicCode,0}};
  in.fields=fields;accepted(f.logger.try_write(in),2);fields[1].value=42;
  std::array<PublicLogRecord,4> out{};auto page=f.diagnostics->copy_records(position(0),out);REQUIRE(page);REQUIRE(page->copied==2);
  REQUIRE(std::string_view(out[0].text.data(),out[0].text_size)=="ock.log/1 level=info component=host event=ready");
  REQUIRE(std::string_view(out[1].text.data(),out[1].text_size)=="ock.log/1 level=info component=host event=ready status=code:0 count=count:42 module=code:7 detail=<redacted>");
  for(auto& r:out)for(auto i=r.text_size;i<256;++i)REQUIRE(r.text[i]==0);
  fields[1].key=LogKey::Detail;rejected(f.logger.try_write(in),LogReason::InvalidRecord);
  fields[1].key=static_cast<LogKey>(100);rejected(f.logger.try_write(in),LogReason::InvalidRecord);
  fields[1]={LogKey::Module,LogValueClass::PublicCode,0};rejected(f.logger.try_write(in),LogReason::InvalidRecord);
  fields[1]={LogKey::Module,LogValueClass::PublicCount,3};rejected(f.logger.try_write(in),LogReason::InvalidRecord);
  fields[1]={LogKey::Module,LogValueClass::Redacted,UINT64_MAX};accepted(f.logger.try_write(in),3);
  fields[0].visibility=LogValueClass::PublicCode;rejected(f.logger.try_write(in),LogReason::InvalidRecord);
  std::array<LogField,5> too_many{};in.fields=too_many;rejected(f.logger.try_write(in),LogReason::InvalidRecord);
  LogField detail{LogKey::Detail};in={LogLevel::Debug,LogComponent::Policy,LogEvent::Diagnostic,std::span(&detail,1)};
  // 单独启用 Debug 后端，第三 golden 的真实存储不能用 Filtered 冒充。
  Fixture debug(memory,2,LogLevel::Debug);accepted(debug.logger.try_write(in),1);REQUIRE(debug.diagnostics->copy_records(position(0),out));
  REQUIRE(std::string_view(out[0].text.data(),out[0].text_size)=="ock.log/1 level=debug component=policy event=diagnostic detail=<redacted>");
  in={LogLevel::Error,LogComponent::Host,LogEvent::Ready,{}};rejected(f.logger.try_write(in),LogReason::InvalidRecord);
  REQUIRE(is_error(f.diagnostics->copy_records(position(0),{}),LogErrc::InvalidReadPage));
  out[0].text[0]='X';REQUIRE(is_error(f.diagnostics->copy_records(position(99),out),LogErrc::InvalidPosition));REQUIRE(out[0].text[0]=='X');
  std::array<PublicLogRecord,65> excessive{};REQUIRE(is_error(f.diagnostics->copy_records(position(0),excessive),LogErrc::InvalidReadPage));
  std::cout<<"assertion golden_redaction_borrowing_invalid_read_unchanged\n";
}
void error_isolation(bool memory) {
  Fixture f(memory);int business_result=17;auto bad=input();bad.level=static_cast<LogLevel>(99);
  rejected(f.logger.try_write(bad),LogReason::InvalidRecord);REQUIRE(business_result==17);
  auto backend=std::make_shared<TestBackend>();auto r=SafeLogger::create(backend);REQUIRE(r);auto& logger=*r;
  backend->throw_write=true;rejected(logger.try_write(input()),LogReason::BackendFailure);backend->throw_write=false;
  REQUIRE(logger.counters()->write_backend_exceptions==1);REQUIRE(logger.snapshot()->counters.accepted==0);REQUIRE(logger.snapshot()->counters.rejected_backend==1);
  backend->bad_write=true;rejected(logger.try_write(input()),LogReason::BackendProtocolFailure);backend->bad_write=false;
  REQUIRE(logger.counters()->acceptance_unknown==1);REQUIRE(logger.counters()->write_protocol_failures==1);
  auto foreign=position(1);foreign.host=host(2);
  for(auto invalid:std::array{LogWriteResult{LogDecision::Accepted,LogReason::None,foreign},LogWriteResult{LogDecision::Dropped,LogReason::Full,{}},LogWriteResult{LogDecision::Rejected,LogReason::Reentrant,{}},LogWriteResult{static_cast<LogDecision>(99),LogReason::None,{}}}){
    backend->forced_write=invalid;rejected(logger.try_write(input()),LogReason::BackendProtocolFailure);
  }
  backend->forced_write.reset();REQUIRE(logger.counters()->acceptance_unknown==5);
  backend->throw_control=true;REQUIRE(is_error(logger.snapshot(),LogErrc::BackendFailure));REQUIRE(is_error(logger.flush(position(0)),LogErrc::BackendFailure));REQUIRE(is_error(logger.close(),LogErrc::BackendFailure));backend->throw_control=false;
  backend->bad_snapshot=true;REQUIRE(is_error(logger.snapshot(),LogErrc::BackendProtocolFailure));backend->bad_snapshot=false;
  backend->bad_flush=true;REQUIRE(is_error(logger.flush(position(0)),LogErrc::BackendProtocolFailure));backend->bad_flush=false;
  auto c=logger.counters();REQUIRE(c->snapshot_backend_exceptions==1);REQUIRE(c->snapshot_protocol_failures==1);REQUIRE(c->flush_backend_exceptions==1);REQUIRE(c->flush_protocol_failures==1);REQUIRE(c->close_backend_exceptions==1);
  backend->snapshot_error=ock::foundation::make_error(ock::foundation::FoundationErrc::invalid_name);REQUIRE(is_error(logger.snapshot(),LogErrc::BackendProtocolFailure));backend->snapshot_error.reset();
  auto detailed=ock::foundation::Error::with_details(log_error(LogErrc::BackendFailure).code(),"secret-details");REQUIRE(detailed);backend->snapshot_error=*detailed;REQUIRE(is_error(logger.snapshot(),LogErrc::BackendProtocolFailure));backend->snapshot_error.reset();
  backend->accept_then_throw=true;rejected(logger.try_write(input()),LogReason::BackendFailure);backend->accept_then_throw=false;
  // 非合格故障控制：真实接受与 facade 观察不一致，不能凭 catch 恢复接受事实。
  REQUIRE(logger.snapshot()->counters.accepted==1);REQUIRE(logger.counters()->accepted==0);REQUIRE(logger.counters()->acceptance_unknown==5);
  auto broken=std::make_shared<TestBackend>();broken->bad_snapshot=true;REQUIRE(SafeLogger::create(broken));broken->bad_snapshot=false;
  broken->inconsistent_snapshot=true;REQUIRE(is_error(SafeLogger::create(broken),LogErrc::BackendProtocolFailure));broken->inconsistent_snapshot=false;broken->throw_control=true;REQUIRE(is_error(SafeLogger::create(broken),LogErrc::BackendFailure));
  REQUIRE(is_error(SafeLogger::create({}),LogErrc::InvalidLogger));
  auto alias_owner=std::make_shared<TestBackend>();
  std::shared_ptr<LogPort> borrowed_alias(std::shared_ptr<LogPort>{},alias_owner.get());
  REQUIRE(borrowed_alias&&borrowed_alias.use_count()==0);
  const auto calls_before=alias_owner->calls.load();
  REQUIRE(is_error(SafeLogger::create(borrowed_alias),LogErrc::InvalidLogger));
  REQUIRE(alias_owner->calls==calls_before); // 必须在任何 backend 回调之前拒绝。
  std::shared_ptr<LogPort> owned_alias(alias_owner,static_cast<LogPort*>(alias_owner.get()));
  auto owned_facade=SafeLogger::create(owned_alias);REQUIRE(owned_facade);
  owned_alias.reset();alias_owner.reset();REQUIRE(owned_facade->snapshot());
  auto moved=std::move(logger);rejected(logger.try_write(input()),LogReason::InvalidLogger);REQUIRE(is_error(logger.snapshot(),LogErrc::InvalidLogger));REQUIRE(moved.snapshot());
  std::cout<<"assertion facade_backend_separate_counts_and_nonqualified_acceptance\n";
}
void flush_range(bool memory) {
  Fixture f(memory);auto zero=f.logger.flush(position(0));REQUIRE(zero);REQUIRE(zero->covered_through==position(0));REQUIRE(zero->evicted_through==0);REQUIRE(zero->volatile_only);
  accepted(f.logger.try_write(input()),1);auto first=f.logger.flush(position(1));REQUIRE(first&&first->evicted_through==0);
  accepted(f.logger.try_write(input()),2);
  for(int i=3;i<=5;++i){if(memory)accepted(f.logger.try_write(input()),i);else rejected(f.logger.try_write(input()),LogReason::Full);}
  for(auto h:{0u,1u,2u}){auto v=f.logger.flush(position(h));REQUIRE(v);REQUIRE(v->covered_through==position(h));REQUIRE(v->evicted_through==(memory?h:0u));}
  if(memory){auto equal=f.logger.flush(position(3));REQUIRE(equal&&equal->evicted_through==3);auto greater=f.logger.flush(position(5));REQUIRE(greater&&greater->evicted_through==3);}
  REQUIRE(is_error(f.logger.flush(position(6)),LogErrc::InvalidPosition));auto other=position(0);other.stream=2;REQUIRE(is_error(f.logger.flush(other),LogErrc::InvalidPosition));
  std::array<PublicLogRecord,1> page{};auto p=f.diagnostics->copy_records(position(0),page);REQUIRE(p);REQUIRE(p->gap==memory);REQUIRE(p->next==position(memory?4:1));REQUIRE(p->accepted_upper==position(memory?5:2));REQUIRE(page[0].position==p->next);
  auto snap=f.logger.snapshot();auto a=f.logger.close();auto b=f.logger.close();REQUIRE(a&&b);REQUIRE(a->covered_through==b->covered_through);REQUIRE(a->covered_through==snap->accepted_through);REQUIRE(f.logger.flush(position(1)));REQUIRE(f.logger.snapshot()->retained_count==2);
  auto tail=f.diagnostics->copy_records(snap->accepted_through,page);REQUIRE(tail);REQUIRE(tail->copied==0);REQUIRE(tail->next==snap->accepted_through);REQUIRE(!tail->gap);
  auto backend=std::make_shared<TestBackend>();auto guarded=SafeLogger::create(backend);backend->fail_flush=true;REQUIRE(is_error(guarded->flush(position(0)),LogErrc::BackendFailure));backend->fail_flush=false;REQUIRE(guarded->flush(position(0)));REQUIRE(guarded->snapshot()->counters.flush_failures==1);
  backend->bad_close=true;REQUIRE(is_error(guarded->close(),LogErrc::BackendProtocolFailure));backend->bad_close=false;REQUIRE(guarded->close());
  std::cout<<"assertion flush_clipped_eviction_page_gap_idempotence\n";
}
void lifetime(bool memory) {
  Fixture f(memory);accepted(f.logger.try_write(input()),1);auto keep=f.diagnostics;auto weak=std::weak_ptr<MemoryDiagnostics>(keep);REQUIRE(f.logger.close());REQUIRE(f.logger.close());
  std::array<PublicLogRecord,1> out{};REQUIRE(keep->copy_records(position(0),out));
  auto backend=std::make_shared<TestBackend>();auto logger=SafeLogger::create(backend);REQUIRE(logger);
  backend->fail_close=true;REQUIRE(is_error(logger->close(),LogErrc::BackendFailure));REQUIRE(backend->close_actions==1);rejected(logger->try_write(input()),LogReason::Closed);REQUIRE(logger->snapshot()->state==LogState::Closing);
  backend->fail_close=false;REQUIRE(logger->close());REQUIRE(logger->close());REQUIRE(backend->close_actions==1);
  // 4×4 跨方法保护；测试函数在后端锁外调用，禁止无限递归。
  for(int outer=0;outer<4;++outer)for(int inner=0;inner<4;++inner){
    auto port=std::make_shared<TestBackend>();auto one=SafeLogger::create(port);auto two=SafeLogger::create(port);REQUIRE(one&&two);
    unsigned entered=0;port->before=[&]{++entered;if(inner==0)rejected(two->try_write(input()),LogReason::Reentrant);if(inner==1)REQUIRE(is_error(two->snapshot(),LogErrc::Reentrant));if(inner==2)REQUIRE(is_error(two->flush(position(0)),LogErrc::Reentrant));if(inner==3)REQUIRE(is_error(two->close(),LogErrc::Reentrant));};
    if(outer==0)accepted(one->try_write(input()),1);if(outer==1)REQUIRE(one->snapshot());if(outer==2)REQUIRE(one->flush(position(0)));if(outer==3)REQUIRE(one->close());REQUIRE(entered==1);port->before={};
    auto counts=two->counters();REQUIRE(counts->write_reentrant+counts->snapshot_reentrant+counts->flush_reentrant+counts->close_reentrant==1);
  }
  // A→独立 B→A，B 允许，A 只拒绝最内层。
  auto a=std::make_shared<TestBackend>(),b=std::make_shared<TestBackend>();auto la=SafeLogger::create(a),lb=SafeLogger::create(b);
  a->before=[&]{REQUIRE(lb->snapshot());};b->before=[&]{rejected(la->try_write(input()),LogReason::Reentrant);};REQUIRE(la->snapshot());a->before={};b->before={};
  // 同步释放 facade 外壳；之后计数必须仍使用在途共享 State，包括异常回程。
  for(bool throws:{false,true}){auto port=std::make_shared<TestBackend>();auto shell=std::make_unique<SafeLogger>(*SafeLogger::create(port));auto observer=*shell;port->before=[&]{shell.reset();};port->throw_write=throws;auto raw=shell.get();auto result=raw->try_write(input());REQUIRE(!shell);if(throws)rejected(result,LogReason::BackendFailure);else accepted(result,1);REQUIRE(observer.counters()->write_attempts==1);port->before={};}
  for(int method=1;method<4;++method)for(bool throws:{false,true}){
    auto port=std::make_shared<TestBackend>();auto shell=std::make_unique<SafeLogger>(*SafeLogger::create(port));auto observer=*shell;port->before=[&]{shell.reset();};port->throw_control=throws;auto raw=shell.get();
    if(method==1){auto r=raw->snapshot();if(throws)REQUIRE(is_error(r,LogErrc::BackendFailure));else REQUIRE(r);}
    if(method==2){auto r=raw->flush(position(0));if(throws)REQUIRE(is_error(r,LogErrc::BackendFailure));else REQUIRE(r);}
    if(method==3){auto r=raw->close();if(throws)REQUIRE(is_error(r,LogErrc::BackendFailure));else REQUIRE(r);}
    REQUIRE(!shell);auto counters=observer.counters();REQUIRE(counters->snapshot_attempts+counters->flush_attempts+counters->close_attempts==1);port->before={};
  }
  // 最后一份外壳与外部 backend owner 都在回调中释放；在途 State 延迟最终销毁。
  auto in_flight_death=std::make_shared<std::atomic<unsigned>>(0);auto port=std::make_shared<TestBackend>();port->destroyed=in_flight_death;auto shell=std::make_unique<SafeLogger>(*SafeLogger::create(port));
  port->before=[&]{shell.reset();port.reset();REQUIRE(*in_flight_death==0);};auto raw=shell.get();accepted(raw->try_write(input()),1);REQUIRE(*in_flight_death==1);
  std::vector<std::shared_ptr<TestBackend>> ports;std::vector<SafeLogger> facades;
  for(int i=0;i<17;++i){ports.push_back(std::make_shared<TestBackend>());facades.push_back(*SafeLogger::create(ports.back()));}
  for(int i=0;i<16;++i)ports[i]->before=[&,i]{auto result=facades[i+1].snapshot();if(i==15)REQUIRE(is_error(result,LogErrc::Reentrant));else REQUIRE(result);};
  REQUIRE(facades[0].snapshot());REQUIRE(facades[16].counters()->snapshot_reentrant==1);for(auto& item:ports)item->before={};
  auto create_port=std::make_shared<TestBackend>();create_port->before=[&]{REQUIRE(is_error(SafeLogger::create(create_port),LogErrc::Reentrant));};REQUIRE(SafeLogger::create(create_port));create_port->before={};
  auto death=std::make_shared<std::atomic<unsigned>>(0);{auto port=std::make_shared<TestBackend>();port->destroyed=death;auto safe=SafeLogger::create(port);port.reset();REQUIRE(*death==0);REQUIRE(safe->close());REQUIRE(*death==0);}REQUIRE(*death==1);
  REQUIRE(!weak.expired());
  // 同一套件分别证明默认 memory 和独立 test 的最后内部状态释放。
  auto release_bundle=Fixture::bundle(memory,2,LogLevel::Info);
  auto release_writer=release_bundle.writer;auto release_diagnostics=release_bundle.diagnostics;
  auto writer_weak=std::weak_ptr<LogPort>(release_writer);
  auto diagnostics_weak=std::weak_ptr<MemoryDiagnostics>(release_diagnostics);
  std::optional<SafeLogger> release_logger(*SafeLogger::create(release_writer));
  release_bundle={};REQUIRE(release_logger->close());
  release_writer.reset();REQUIRE(!writer_weak.expired());REQUIRE(!diagnostics_weak.expired());
  release_logger.reset();REQUIRE(!writer_weak.expired());REQUIRE(!diagnostics_weak.expired());
  REQUIRE(release_diagnostics->copy_records(position(0),out));
  release_diagnostics.reset();REQUIRE(writer_weak.expired());REQUIRE(diagnostics_weak.expired());
  std::cout<<"assertion close_pending_cross_method_tls_self_destruction_final_owner\n";
}
#ifndef OCK_LOGGING_BINDING_SHA
#define OCK_LOGGING_BINDING_SHA "development-unbound"
#endif
int main(int argc,char**argv) {
  if(argc==2&&std::string_view(argv[1])!="--list")std::cout<<"logging_binding "<<OCK_LOGGING_BINDING_SHA<<" case "<<argv[1]<<'\n';
  static_assert(noexcept(std::declval<SafeLogger&>().try_write(input())));
  const std::map<std::string,void(*)(bool)> names{{"accept_reject_drop_accounting",accounting},{"format_redaction",format_redaction},{"error_isolation",error_isolation},{"flush_accepted_range",flush_range},{"shutdown_callback_lifetime",lifetime}};
  try{if(argc==2&&std::string_view(argv[1])=="--list"){std::cout<<"T19.logging.memory_pages\n";for(auto backend:{"memory","test"})for(auto&[name,fn]:names)std::cout<<"T22.logging."<<backend<<'.'<<name<<'\n';std::cout<<"T23.logging.fixed_write_allocation\n";return 0;}
    if(argc==2&&std::string_view(argv[1])=="T19.logging.memory_pages"){memory_pages();std::cout<<"assertions_completed "<<argv[1]<<'\n';return 0;}
    if(argc==2&&std::string_view(argv[1])=="T23.logging.fixed_write_allocation"){fixed_write_allocation();std::cout<<"assertions_completed "<<argv[1]<<'\n';return 0;}
    if(argc!=2)return 2;std::string value=argv[1];for(auto backend:{"memory","test"})for(auto&[name,fn]:names)if(value==std::string("T22.logging.")+backend+'.'+name){fn(std::string_view(backend)=="memory");std::cout<<"assertions_completed "<<value<<'\n';return 0;}return 2;
  }catch(const std::exception& e){allocation::stop();std::cerr<<e.what()<<'\n';return 1;}
}

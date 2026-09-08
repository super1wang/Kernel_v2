#pragma once
// 独立测试后端：RejectNewest；不复用生产格式器、环或 facade 实现。
#include <ock/runtime/logging.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace logging_test {
using namespace ock::contracts;
using namespace ock::runtime::observability;
using ock::foundation::Result;
using ock::foundation::make_unexpected;
inline HostIncarnation host(unsigned n=1) { HostIncarnation h{}; h.bytes[0]=static_cast<std::uint8_t>(n); return h; }
inline LogInput input(LogLevel level=LogLevel::Info) noexcept { return {level,LogComponent::Host,LogEvent::Diagnostic,{}}; }
inline LogPosition position(std::uint64_t n) { return {host(),1,n}; }
inline void require(bool ok,const char* expression,int line) {
  if(!ok) throw std::runtime_error(std::to_string(line)+": "+expression);
}
#define REQUIRE(...) ::logging_test::require(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)
inline bool is_error(const auto& result,LogErrc code) {
  return !result && result.error().code()==log_error(code).code() && !result.error().info();
}
inline void accepted(const LogWriteResult& r,std::uint64_t seq) {
  REQUIRE(r.decision==LogDecision::Accepted); REQUIRE(r.reason==LogReason::None);
  REQUIRE(r.accepted==position(seq));
}
inline void rejected(const LogWriteResult& r,LogReason why) {
  REQUIRE(r.decision==LogDecision::Rejected); REQUIRE(r.reason==why); REQUIRE(!r.accepted);
}

// 独立饱和原子计数，Busy/非法记录路径也不依赖存储锁。
struct Counter {
  std::atomic<std::uint64_t> value{};
  std::atomic<bool> overflow{};
  operator std::uint64_t() const noexcept { return value.load(); }
  std::uint64_t operator++() noexcept {
    auto old=value.load();
    for(;;){if(old==UINT64_MAX){overflow=true;return old;}if(value.compare_exchange_weak(old,old+1))return old+1;}
  }
};
struct TestCounts {
#define TEST_COUNTERS(X) X(accepted) X(rejected) X(dropped_before_accept) X(evicted_after_accept) X(rejected_invalid) X(rejected_full) X(rejected_busy) X(rejected_closed) X(rejected_backend) X(rejected_sequence) X(dropped_filtered) X(snapshot_failures) X(flush_failures) X(close_failures)
#define TEST_DECLARE(n) Counter n;
  TEST_COUNTERS(TEST_DECLARE)
#undef TEST_DECLARE
  operator LogCounters() const noexcept {
    LogCounters result{};
#define TEST_READ(n) result.n=n;result.saturated=result.saturated||n.overflow.load();
    TEST_COUNTERS(TEST_READ)
#undef TEST_READ
    return result;
  }
#undef TEST_COUNTERS
};

class TestBackend final : public LogPort, public MemoryDiagnostics {
public:
  explicit TestBackend(std::uint32_t capacity=2,LogLevel minimum=LogLevel::Info): limits{capacity,LogOverflow::RejectNewest,minimum} { rows.reserve(capacity); }
  // 以下控制器仅属于测试对象。所有用户函数均在本测试后端锁外执行。
  std::function<void()> before;
  bool throw_write{}, bad_write{}, accept_then_throw{}, fail_close{}, fail_flush{}, throw_control{};
  bool bad_snapshot{}, bad_flush{}, busy_write{};
  bool inconsistent_snapshot{};
  bool bad_close{};
  std::optional<LogWriteResult> forced_write;
  std::optional<ock::foundation::Error> snapshot_error;
  unsigned close_actions{};
  std::atomic<unsigned> calls{};
  std::shared_ptr<std::atomic<unsigned>> destroyed;
  ~TestBackend() override { if(destroyed)++*destroyed; }
  LogWriteResult try_write(const LogInput& in) override {
    ++calls; if(before)before();
    if(forced_write)return *forced_write;
    if(throw_write) { ++counts.rejected; ++counts.rejected_backend; throw std::runtime_error("secret-exception"); }
    if(bad_write)return {LogDecision::Accepted,LogReason::None,{}};
    std::array<char,256> formatted{};
    if(!format(in,formatted))return reject(LogReason::InvalidRecord);
    if(busy_write)return reject(LogReason::Busy);
    std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock())return reject(LogReason::Busy);
    if(state!=LogState::Open)return reject(LogReason::Closed);
    if(in.level<limits.minimum_level){++counts.dropped_before_accept;++counts.dropped_filtered;return {LogDecision::Dropped,LogReason::Filtered,{}};}
    if(rows.size()==limits.record_capacity)return reject(LogReason::Full);
    auto p=position(++counts.accepted);
    rows.push_back({p,in.level,static_cast<std::uint16_t>(std::char_traits<char>::length(formatted.data())),formatted});
    if(accept_then_throw)throw std::runtime_error("nonqualified post-accept");
    return {LogDecision::Accepted,LogReason::None,p};
  }
  Result<LogSnapshot> snapshot() override {
    ++calls; if(before)before(); if(throw_control)throw std::runtime_error("control");
    if(snapshot_error)return make_unexpected(*snapshot_error);
    std::lock_guard lock(mutex);
    auto p=position(counts.accepted); if(bad_snapshot)p.stream=2;
    return LogSnapshot{p,0,rows.size()+(inconsistent_snapshot?1:0),limits,state,counts};
  }
  Result<LogFlushResult> flush(LogPosition p) override {
    ++calls; if(before)before(); if(throw_control)throw std::runtime_error("control");
    std::lock_guard lock(mutex);
    if(p.host!=host()||p.stream!=1||p.accepted_sequence>counts.accepted){++counts.flush_failures;return make_unexpected(log_error(LogErrc::InvalidPosition));}
    if(fail_flush){++counts.flush_failures;return make_unexpected(log_error(LogErrc::BackendFailure));}
    if(bad_flush)++p.accepted_sequence;
    return LogFlushResult{p,0,true};
  }
  Result<LogFlushResult> close() override {
    ++calls; if(before)before(); if(throw_control)throw std::runtime_error("control");
    std::lock_guard lock(mutex);
    if(state==LogState::Open){state=LogState::Closing;++close_actions;}
    if(fail_close){++counts.close_failures;return make_unexpected(log_error(LogErrc::BackendFailure));}
    state=LogState::Closed;auto p=position(counts.accepted);if(bad_close)p.stream=2;return LogFlushResult{p,0,true};
  }
  Result<LogReadPage> copy_records(LogPosition after,std::span<PublicLogRecord> out) override {
    std::lock_guard lock(mutex);
    if(out.empty()||out.size()>64)return make_unexpected(log_error(LogErrc::InvalidReadPage));
    if(after.host!=host()||after.stream!=1||after.accepted_sequence>counts.accepted)return make_unexpected(log_error(LogErrc::InvalidPosition));
    auto n=std::min<std::size_t>(out.size(),counts.accepted-after.accepted_sequence);
    for(std::size_t i=0;i<n;++i)out[i]=rows[static_cast<std::size_t>(after.accepted_sequence)+i];
    return LogReadPage{static_cast<std::uint32_t>(n),position(after.accepted_sequence+n),position(counts.accepted),0,false};
  }
private:
  LogLimits limits;
  LogState state{LogState::Open};
  TestCounts counts{};
  std::vector<PublicLogRecord> rows;
  std::mutex mutex;
  LogWriteResult reject(LogReason reason) {
    ++counts.rejected;
    switch(reason){case LogReason::InvalidRecord:++counts.rejected_invalid;break;case LogReason::Busy:++counts.rejected_busy;break;case LogReason::Closed:++counts.rejected_closed;break;default:++counts.rejected_full;break;}
    return {LogDecision::Rejected,reason,{}};
  }
  static bool format(const LogInput& in,std::array<char,256>& out) {
    static constexpr const char* levels[]{"trace","debug","info","warning","error"};
    static constexpr const char* components[]{"host","registry","policy","invocation"};
    static constexpr const char* events[]{"", "configured","starting","module_started","ready","start_failed","stop_accepting","module_stopped","log_closing","diagnostic"};
    static constexpr const char* keys[]{"","status","count","module","detail"};
    auto l=static_cast<unsigned>(in.level), c=static_cast<unsigned>(in.component), e=static_cast<unsigned>(in.event);
    if(l>4||c>3||e<1||e>9||in.fields.size()>4)return false;
    if(e!=9&&(c!=0||l!=(e==5?4u:2u)))return false;
    std::array<const LogField*,5> fields{};
    for(auto& f:in.fields){auto k=static_cast<unsigned>(f.key),v=static_cast<unsigned>(f.visibility);
      if(k<1||k>4||fields[k]||v>2)return false;
      fields[k]=&f;
      if(v==0)continue;
      if(k==4||(k==2?v!=2:v!=1)||((k==1||k==3)&&f.value>65535)||(k==3&&f.value==0))return false;
    }
    // 测试后端的独立有界格式器：逐字符写入，十进制手动反向展开。
    // 不转调生产 to_chars/格式助手，也不使用动态 string。
    std::size_t used=0;
    auto character=[&](char value){if(used+1>=out.size())return false;out[used++]=value;return true;};
    auto literal=[&](const char* value){for(;*value;++value)if(!character(*value))return false;return true;};
    auto decimal=[&](std::uint64_t value){
      char reversed[20];unsigned size=0;
      do{reversed[size++]=static_cast<char>('0'+value%10);value/=10;}while(value);
      while(size)if(!character(reversed[--size]))return false;
      return true;
    };
    if(!literal("ock.log/1 level=")||!literal(levels[l])||!literal(" component=")||!literal(components[c])||!literal(" event=")||!literal(events[e]))return false;
    for(unsigned k=1;k<=4;++k)if(auto f=fields[k]){
      if(!character(' ')||!literal(keys[k])||!character('='))return false;
      if(f->visibility==LogValueClass::Redacted){if(!literal("<redacted>"))return false;}
      else if(!literal(f->visibility==LogValueClass::PublicCode?"code:":"count:")||!decimal(f->value))return false;
    }
    return true;
  }
};

struct Fixture {
  bool memory;
  std::shared_ptr<LogPort> writer;
  std::shared_ptr<MemoryDiagnostics> diagnostics;
  SafeLogger logger;
  static MemoryLoggingBundle bundle(bool memory,std::uint32_t capacity,LogLevel minimum) {
    if(memory){auto b=make_memory_logging(host(),1,{capacity,LogOverflow::DropOldest,minimum});REQUIRE(b);return *b;}
    auto b=std::make_shared<TestBackend>(capacity,minimum);return {b,b};
  }
  Fixture(bool m,MemoryLoggingBundle b):memory(m),writer(b.writer),diagnostics(b.diagnostics),logger(*SafeLogger::create(writer)){}
  explicit Fixture(bool m,std::uint32_t capacity=2,LogLevel minimum=LogLevel::Info):Fixture(m,bundle(m,capacity,minimum)){}
};
}

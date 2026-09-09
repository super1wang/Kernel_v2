#include <ock/control_client/intent.hpp>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <cmath>
namespace ock::control_client {
namespace {
using foundation::Result;
struct File {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~File() { if(value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
Result<void> ordered(data::PayloadBuilder &builder,data::ValueView value) {
  using data::Kind;
  switch(value.kind()) {
  case Kind::Object: {
    std::vector<std::string_view> keys;
    for(std::size_t i=0;i<value.size();++i) keys.push_back(*value.key_at(i));
    std::sort(keys.begin(),keys.end());
    auto ok = builder.begin_object(); if(!ok) return ok;
    for(auto key : keys) {
      ok = builder.key(key); if(!ok) return ok;
      ok = ordered(builder,value.at(key)); if(!ok) return ok;
    }
    return builder.end_object();
  }
  case Kind::Array: {
    auto ok = builder.begin_array(); if(!ok) return ok;
    for(std::size_t i=0;i<value.size();++i) { ok = ordered(builder,value.at(i)); if(!ok) return ok; }
    return builder.end_array();
  }
  case Kind::Null: return builder.null();
  case Kind::Boolean: return builder.boolean(*value.boolean());
  case Kind::String: return builder.string(*value.string());
  case Kind::Int64: return builder.int64(*value.int64());
  case Kind::UInt64: return builder.uint64(*value.uint64());
  case Kind::Number: {
    const auto n = *value.number();
    if(std::abs(n) <= 9007199254740991.0 && std::trunc(n) == n) return builder.int64(static_cast<std::int64_t>(n));
    return builder.number(n);
  }
  default: return foundation::make_unexpected(error(ClientErrc::InvalidInput));
  }
}
std::string hex(std::span<const unsigned char> bytes) {
  std::string result; result.reserve(bytes.size()*2);
  for(auto byte : bytes) { result += "0123456789abcdef"[byte>>4]; result += "0123456789abcdef"[byte&15]; }
  return result;
}
Result<std::string> fingerprint(const data::Payload &request) {
  data::PayloadBuilder builder;
  auto ok = ordered(builder,request.view()); if(!ok) return foundation::make_unexpected(ok.error());
  auto payload = builder.freeze(); if(!payload) return foundation::make_unexpected(payload.error());
  auto encoded = payload->encode(); if(!encoded) return foundation::make_unexpected(encoded.error());
  std::string input("ock.cli.logical-request/1"); input.push_back('\0'); input += *encoded;
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return foundation::make_unexpected(error(ClientErrc::File));
  std::array<unsigned char,32> bytes{};
  const auto status = BCryptHash(algorithm,nullptr,0,reinterpret_cast<PUCHAR>(input.data()),static_cast<ULONG>(input.size()),bytes.data(),static_cast<ULONG>(bytes.size()));
  BCryptCloseAlgorithmProvider(algorithm,0);
  if(status<0) return foundation::make_unexpected(error(ClientErrc::File));
  return hex(bytes);
}
Result<data::Payload> read_existing(const std::filesystem::path &path) {
  File file{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
  if(file.value == INVALID_HANDLE_VALUE) return foundation::make_unexpected(error(ClientErrc::File));
  BY_HANDLE_FILE_INFORMATION information{};
  if(!GetFileInformationByHandle(file.value,&information) || (information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) ||
      information.nFileSizeHigh || !information.nFileSizeLow || information.nFileSizeLow > 8192)
    return foundation::make_unexpected(error(ClientErrc::File));
  std::string bytes(information.nFileSizeLow,'\0'); DWORD read = 0;
  if(!ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr) || read != bytes.size())
    return foundation::make_unexpected(error(ClientErrc::File));
  return data::Payload::parse(bytes);
}
bool matches(data::ValueView root,const Hello &hello,std::string_view hash) {
  const auto nonce = root.at("nonce").string();
  return root.kind()==data::Kind::Object && root.size()==10 && root.at("format").string()=="ock.cli.intent/1" &&
      root.at("scope").string()=="VolatileHost" && root.at("application_id").string()==hello.application_id &&
      root.at("instance_id").string()==hello.instance_id && root.at("host_incarnation").string()==hello.host_incarnation &&
      root.at("epoch").string()==hello.dedup_epoch.value_or(hello.host_incarnation) && root.at("fingerprint").string()==hash &&
      root.at("fingerprint_format").string()=="ock.cli.logical-request/1" &&
      root.at("deduplication").string()==(hello.dedup_epoch ? "advertised" : "not_provided") &&
      nonce && nonce->size()==32 && nonce->find_first_not_of("0123456789abcdef")==nonce->npos && nonce->find_first_not_of('0')!=nonce->npos;
}
}
Result<data::Payload> ensure_intent(const std::filesystem::path &path,const Hello &hello,const data::Payload &request) {
  if(path.empty() || hello.application_id.empty() || hello.host_incarnation.empty()) return foundation::make_unexpected(error(ClientErrc::InvalidInput));
  auto hash = fingerprint(request); if(!hash) return foundation::make_unexpected(hash.error());
  auto existing = [&]() -> Result<data::Payload> {
    auto payload = read_existing(path);
    if(!payload || !matches(payload->view(),hello,*hash)) return foundation::make_unexpected(error(ClientErrc::IntentConflict));
    return payload;
  };
  const auto attributes = GetFileAttributesW(path.c_str());
  if(attributes != INVALID_FILE_ATTRIBUTES) return existing();
  if(GetLastError()!=ERROR_FILE_NOT_FOUND) return foundation::make_unexpected(error(ClientErrc::File));
  std::array<unsigned char,16> random{};
  if(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)
    return foundation::make_unexpected(error(ClientErrc::File));
  const auto nonce = hex(random);
  data::PayloadBuilder builder;
  (void)builder.begin_object();
  for(const auto &[key,value] : std::vector<std::pair<std::string,std::string>>{
      {"format","ock.cli.intent/1"},{"scope","VolatileHost"},{"application_id",hello.application_id},
      {"instance_id",hello.instance_id},{"host_incarnation",hello.host_incarnation},
      {"epoch",hello.dedup_epoch.value_or(hello.host_incarnation)},{"nonce",nonce},{"fingerprint",*hash},
      {"deduplication",hello.dedup_epoch ? "advertised" : "not_provided"},{"fingerprint_format","ock.cli.logical-request/1"}}) {
    (void)builder.key(key); (void)builder.string(value);
  }
  (void)builder.end_object();
  auto payload = builder.freeze(); if(!payload) return foundation::make_unexpected(payload.error());
  auto bytes = payload->encode(8192); if(!bytes) return foundation::make_unexpected(bytes.error());
  auto temporary = path; temporary += std::filesystem::path("."+nonce+".tmp");
  File file{CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
  if(file.value==INVALID_HANDLE_VALUE) return foundation::make_unexpected(error(ClientErrc::File));
  DWORD written=0;
  const bool saved = WriteFile(file.value,bytes->data(),static_cast<DWORD>(bytes->size()),&written,nullptr) && written==bytes->size() && FlushFileBuffers(file.value);
  CloseHandle(file.value); file.value=INVALID_HANDLE_VALUE;
  if(!saved) { DeleteFileW(temporary.c_str()); return foundation::make_unexpected(error(ClientErrc::File)); }
  if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_WRITE_THROUGH)) {
    const DWORD failure = GetLastError(); DeleteFileW(temporary.c_str());
    if(failure==ERROR_ALREADY_EXISTS || failure==ERROR_FILE_EXISTS) return existing();
    return foundation::make_unexpected(error(ClientErrc::File));
  }
  return payload;
}
}

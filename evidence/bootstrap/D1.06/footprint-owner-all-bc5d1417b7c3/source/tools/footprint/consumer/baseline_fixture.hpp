#pragma once
#include "channel.hpp"
namespace native_service {
class Scenario {
  unsigned calls_=0;
  bool stopped_=false,bound_=true,session_=true,owners_=true;
public:
  static constexpr const char* kind="baseline";
  void prepare(){}
  void prove_invalid(){footprint::require(calls_==2);}
  void call(unsigned index){footprint::require(!stopped_);volatile int input=5;const int value=input+(index%2?7:2);footprint::require(value==(index%2?12:7));++calls_;}
  void shutdown(){footprint::require(calls_==46);stopped_=true;}
  void release_bound(){footprint::require(stopped_);bound_=false;}
  void release_session(){footprint::require(!bound_);session_=false;}
  void release_owners(){footprint::require(!session_);owners_=false;}
  unsigned sentinels()const{return 0;}
  unsigned owner_mask()const{return 0;}
};
}

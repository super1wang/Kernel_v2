#include "service.hpp"
#include <iostream>
int main(int argc,char** argv)try {
  if(argc!=3||std::string_view(argv[1])!="--instance")return 2;
  auto sid=ock::local_ipc::current_user_sid();if(!sid)return 3;
  auto service=managed::Service::create(*sid);if(!service){std::cerr<<"Managed Host setup rejected\n";return 3;}
  auto server=(*service)->listen(argv[2]);if(!server)return 6;
  std::cout<<"{\"ready\":true}"<<std::endl;
  std::string command;
  while(std::getline(std::cin,command)) {
    if(command=="observation-diagnostics") {
      auto value=(*service)->observation_diagnostics();
      std::cout<<"{\"sessions\":"<<value.sessions<<",\"queued_bytes\":"<<value.queued_bytes<<",\"started\":"<<value.started<<'}'<<std::endl;
    } else if(command=="restrict-observers") {
      auto result=(*service)->restrict_observers();if(!result)return 6;
      auto value=(*service)->observation_diagnostics();
      std::cout<<"{\"restricted\":"<<*result<<",\"queued_bytes\":"<<value.queued_bytes<<",\"started\":"<<value.started<<'}'<<std::endl;
    } else break;
  }
  (*server)->close();
  return (*service)->shutdown()?0:6;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 6;}

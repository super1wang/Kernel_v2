#include "service.hpp"
#include <fstream>
#include <iostream>
int main(int argc,char **argv) try {
  if(argc != 3) { std::cerr << "usage: ock_stateless_service --instance NAME | --native JSON_FILE\n"; return 2; }
  auto sid = ock::local_ipc::current_user_sid(); if(!sid) return 3;
  auto service = stateless::Service::create(*sid); if(!service) { std::cerr << "Host setup rejected\n"; return 3; }
  if(std::string_view(argv[1]) == "--native") {
    std::ifstream input(argv[2],std::ios::binary); if(!input) return 2;
    std::string json((std::istreambuf_iterator<char>(input)),{});
    auto payload = ock::data::Payload::parse(json); if(!payload) return 2;
    auto amount=payload->view().at("amount").int64(); auto text=payload->view().at("text").string();
    if(!amount || !text || payload->view().size()!=2) return 2;
    auto result = (*service)->native({*amount,std::string(*text)}); if(!result) return 3;
    auto encoded = result->encode(); if(!encoded) return 6;
    std::cout << *encoded << '\n';
    if(result->view().at("kind").string()=="Rejected") return 3;
    return result->view().at("outcome").at("kind").string()=="ReadCompleted" ? 0 : 4;
  }
  if(std::string_view(argv[1]) != "--instance") return 2;
  auto server = (*service)->listen(argv[2]); if(!server) { std::cerr << "Pipe bind rejected\n"; return 6; }
  std::cout << "{\"ready\":true}" << std::endl;
  std::string command; std::getline(std::cin,command);
  (*server)->close(); (*service)->close_connections();
  return 0;
} catch(const std::exception &e) { std::cerr << e.what() << '\n'; return 6; }

#include "tests/contract/native/policy_inline_cases.hpp"
int main(int argc,char**argv) {
try {
if(argc==2 && std::string_view(argv[1])=="ownership_and_identity") { policy_inline_test::ownership_and_identity(); return 0; }
if(argc==2 && std::string_view(argv[1])=="invalidation") { policy_inline_test::invalidation(); return 0; }
if(argc==2 && std::string_view(argv[1])=="deadlines_slots_and_cancellation") { policy_inline_test::deadlines_slots_and_cancellation(); return 0; }
if(argc==2 && std::string_view(argv[1])=="required_permissions_and_tuple") { policy_inline_test::required_permissions_and_tuple(); return 0; }
if(argc==2 && std::string_view(argv[1])=="global_quota_and_move_assignment") { policy_inline_test::global_quota_and_move_assignment(); return 0; }
if(argc==2 && std::string_view(argv[1])=="close_and_original_retention") { policy_inline_test::close_and_original_retention(); return 0; }
if(argc==2 && std::string_view(argv[1])=="metadata_budget") { policy_inline_test::metadata_budget(); return 0; }
return 2; } catch(const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }}

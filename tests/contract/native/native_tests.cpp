#include "fixtures.hpp"
#include "pipeline_cases.hpp"
#include "value_cases.hpp"
#include "policy_inline_cases.hpp"
#include "shape_cases.hpp"
#include "allocation_cases.hpp"
#include "lifetime_cases.hpp"
#include "managed_cases.hpp"
#include "execution_table_cases.hpp"
#include "execution_service_cases.hpp"
using namespace native_test;
void registered_compute() { Env e;auto b=e.bind();CHECK(b);entered=0;CHECK(result(b->invoke(2,e.options()))==4);CHECK(result(b->invoke(5,e.options()))==7);CHECK(entered==2); }
void registered_read() { Env e(true);auto b=e.bind();CHECK(b);e.reader.reset();CHECK(result(b->invoke(3,e.options()))==10); }
void invalid_input() { Env e;auto b=e.bind();CHECK(b);entered=0;CHECK(std::holds_alternative<Rejected>(b->invoke(-1,e.options())));CHECK(entered==0);CHECK(result(b->invoke(0,e.options()))==2); value_invalid_input(); }
void authority_origin() { Env e;policy_test::Env other;CHECK(!e.engine->bind<int,int>(key(),{},Shape::Read,other.caller,std::array{policy_test::target()},targets,name("native.call")));CHECK(e.bind());policy_inline_test::ownership_and_identity(); }
void revoke_and_regrant(){native_invalidation();policy_inline_test::invalidation();}
void session_and_deadline(){policy_inline_test::deadlines_slots_and_cancellation();policy_inline_test::global_quota_and_move_assignment();policy_inline_test::metadata_budget();}
void policy_contract_alignment(){policy_inline_test::required_permissions_and_tuple();}
void private_dispatch(){Env e;auto b=e.bind();CHECK(b);CHECK(result(b->invoke(1,e.options()))==3);}
void no_self_wait(){thread_origin();thread_modes();actual_role_threads();}
void component_boundary(){private_dispatch();}
void example_consumer(){registered_compute();registered_read();provider_unavailable();}
int main(int argc,char**argv) {
  if(argc==2&&std::string_view(argv[1])=="--throwing-transport-probe")return throwing_transport_probe();
  const std::map<std::string,void(*)()> cases{{"T02.native.registered_compute",registered_compute},{"T02.native.registered_read",registered_read},{"T02.native.invalid_input",invalid_input},{"T06.native.authority_origin",authority_origin},
    {"T06.native.managed_admission",managed_admission},{"T03.native.managed_resources",managed_resources},
    {"T03.native.managed_record",managed_record},
    {"T03.native.managed_record_ownership",managed_record_ownership},
    {"T03.native.execution_table_ownership",execution_table_ownership},
    {"T06.native.execution_source_observation",execution_source_observation},
#ifdef OCK_NATIVE_EXECUTION_SERVICE_TESTS
    {"T03.native.execution_service_control",execution_service_control},
    {"T03.native.host_execution_lifecycle",host_execution_lifecycle},
    {"T03.native.host_execution_resources",host_execution_resources},
#endif
    {"T03.native.managed_execution_path",managed_execution_path},
    {"T03.native.managed_execution_resource_wait",managed_execution_resource_wait},
    {"T06.native.business_failure",business_failure},{"T03.native.no_task_path",no_task_path},
    {"T03.native.reentrant_and_concurrent",[]{reentrant_and_concurrent();multi_slot_isolation();replace_bound_in_validator();}},
    {"T02.native.exact_binding",exact_binding},{"T02.native.validation_exception",validation_exception},
    {"T06.native.invalid_output",[]{invalid_output();return_move_observation();}},{"T06.native.target_binding",[]{target_binding();target_read_association();}},
    {"T03.native.thread_origin",thread_origin},{"T03.native.thread_modes",thread_modes},
    {"T03.native.cancel_and_budget",[]{cancel_and_budget();aggregate_budget();deadline_and_stop_context();}},
    {"T06.native.permission_intersection",policy_inline_test::required_permissions_and_tuple},
    {"T06.native.revoke_and_regrant",revoke_and_regrant},
    {"T06.native.session_and_deadline",session_and_deadline},
    {"T06.native.policy_contract_alignment",policy_contract_alignment},
    {"T06.native.lifecycle_replacement",lifecycle_replacement},
    {"T06.native.admission_order",admission_order},
    {"T02.native.candidate_read_dispatch",candidate_read_dispatch},
    {"T03.native.provider_unavailable",provider_unavailable},
    {"T03.native.resource_lifetime",resource_lifetime},
    {"T06.native.outcome_consistency",outcome_consistency},
    {"T02.native.private_dispatch",private_dispatch},{"T03.native.no_self_wait",no_self_wait},
    {"T03.native.component_boundary",component_boundary},{"T03.native.example_consumer",example_consumer},
    {"T23.native.allocation_probe",allocation_probe},{"T23.native.allocation_steady",allocation_steady},
    {"T23.native.allocation_governance",allocation_governance},{"T23.native.allocation_other_costs",allocation_other_costs}};
  try {if(argc==2&&std::string_view(argv[1])=="--asan-hook-registration-failure")return allocation_registration_failure();if(argc==2&&std::string_view(argv[1])=="--allocation-injected-red")return allocation_injected_red();if(argc==2&&std::string_view(argv[1])=="--list"){for(auto&[n,f]:cases)std::cout<<n<<'\n';return 0;}if(argc!=2)return 2;auto i=cases.find(argv[1]);if(i==cases.end())return 2;i->second();return 0;}catch(const std::exception& e){allocation::stop();std::cerr<<e.what()<<'\n';return 1;}
}

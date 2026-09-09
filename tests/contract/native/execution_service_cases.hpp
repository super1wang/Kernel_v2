#pragma once
#ifdef OCK_NATIVE_EXECUTION_SERVICE_TESTS
namespace native_test {void execution_service_control();void host_execution_lifecycle();void host_execution_resources();}
namespace native_test {void host_structured_lifetime();void host_structured_admission();}
namespace native_test {void host_async_completion();void host_async_drain();void host_async_deadline();}
namespace native_test {void host_required_record();void host_required_record_drain();}
namespace native_test {void host_cached_result_sessions();}
#endif

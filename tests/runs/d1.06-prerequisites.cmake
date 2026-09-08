# 正式入口消费前置状态及先行预算；不重跑历史 D1.05。
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../../footprint-budgets.json")
  message(FATAL_ERROR "D1.06 requires approved footprint budgets before formal configuration")
endif()
execute_process(COMMAND python -X utf8 -c
  "from tools.footprint.formal import load,identity,now; from tools.footprint.analyze import validate_budget; b=load('footprint-budgets.json')['budgets']; expected=[(p,m) for p in ('win-msvc-debug','win-msvc-release','win-msvc-asan') for m in ('occupancy','allocation')]+[('win-msvc-release','latency')]; assert set(b)=={p+'/'+m for p,m in expected}; [validate_budget(b[p+'/'+m],method_digest=identity(p,m)[0],report_created=now(),run_ids=[]) for p,m in expected]"
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
  RESULT_VARIABLE _d106_budget_result ERROR_VARIABLE _d106_budget_error)
if(NOT _d106_budget_result EQUAL 0)
  message(FATAL_ERROR "D1.06 budget identity/approval incomplete: ${_d106_budget_error}")
endif()

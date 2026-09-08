"""严格校验单对消费者实际输出；不产生性能 Passed 或预算。"""
def validate_record(record,kind,counter_mode='disabled'):
    expected={'native':('default-memory','handler_entries',('ready_gate','read','compute','invalid_input','async_unavailable','start_log_flush','public_log_page','ordinary_invoke_no_log','shutdown','stopped_bound','bound_release','session_release','last_owner_release')),
              'baseline':('none','local_calls',('local_values','shutdown','last_owner_release'))}
    if kind not in expected or not isinstance(record,dict):raise ValueError('invalid consumer record')
    logging,count,keys=expected[kind]
    if (record.get('kind')!=kind or record.get('warmup_calls')!=4 or record.get('steady_calls')!=40 or
        record.get(count)!=46 or record.get('counter_mode')!=counter_mode or record.get('logging_mode')!=logging or record.get('runtime_workers')!=0):
        raise ValueError('consumer mode/count mismatch')
    checks=record.get('checks')
    if not isinstance(checks,dict) or set(checks)!=set(keys) or any(checks[key] is not True for key in keys):
        raise ValueError('consumer capability check missing or failed')

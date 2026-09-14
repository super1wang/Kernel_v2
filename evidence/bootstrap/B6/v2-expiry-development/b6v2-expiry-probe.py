from pathlib import Path
import subprocess,sys,os
R=Path(__file__).resolve().parents[1];os.chdir(R)
os.environ['PATH']='E:/vs2022IDE/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin;'+os.environ['PATH']
s=(R/'tests/unit/state_roots/runtime_state.cpp').read_text()
extra='''  // Isolated follow-up probe: accepted State expires while its resource is held.
  entered_step=std::make_shared<std::binary_semaphore>(0);release_step=std::make_shared<std::binary_semaphore>(0);
  before_return=[entered_step,release_step](WorkContext&){entered_step->release();CHECK(release_step->try_acquire_for(std::chrono::seconds(3)));};
  auto expiry_hold=host_group_bound->submit(*host_group,submit_options);CHECK(std::holds_alternative<Accepted>(expiry_hold));
  auto expiry_hold_ref=std::get<Accepted>(expiry_hold).execution;CHECK(entered_step->try_acquire_for(std::chrono::seconds(2)));
  auto short_options=submit_options;short_options.deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(100);
  auto expiring=host_group_bound->submit(*host_group,short_options);CHECK(std::holds_alternative<Accepted>(expiring));
  auto expiring_ref=std::get<Accepted>(expiring).execution;
  auto expiry_wait=session->wait(**caller,expiring_ref,std::chrono::steady_clock::now()+std::chrono::seconds(2));
  auto expiry_result=session->result<atomic::Results>(**caller,expiring_ref);
  CHECK(session->cancel(**caller,expiry_hold_ref));release_step->release();
  auto hold_wait=session->wait(**caller,expiry_hold_ref,std::chrono::steady_clock::now()+std::chrono::seconds(3));
  before_return={};CHECK(hold_wait&&hold_wait->state==host::ExecutionWaitState::Terminal);
  CHECK(expiry_wait&&expiry_wait->state==host::ExecutionWaitState::Terminal&&expiry_result);
  exact_failure(*expiry_result->value,true,false);
'''
assert '  recorder->hold=true;' in s
probe=R/'build/b6v2-expiry-probe.cpp';probe.write_text(s.replace('  recorder->hold=true;',extra+'  recorder->hold=true;'),encoding='utf-8')
project=R/'build/b6-formal-debug/tests/unit/state_roots/ock_state_runtime.vcxproj';old=project.read_bytes()
original=str(R/'tests/unit/state_roots/runtime_state.cpp').replace('\\','/')
text=old.decode('utf-8');assert original in text or original.replace('/','\\') in text
text=text.replace(original,probe.as_posix()).replace(original.replace('/','\\'),str(probe))
try:
 project.write_bytes(text.encode('utf-8'))
 result=subprocess.run([sys.executable,'-u','-X','utf8','build/b6v2-direct.py','expiry-probe'])
finally:project.write_bytes(old)
raise SystemExit(result.returncode)

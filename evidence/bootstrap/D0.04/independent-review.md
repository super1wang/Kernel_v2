# D0.04 独立只读技术复核

本轮独立技术复核通过，未发现尚未解决的 must-fix。

本次审查者未实现 D0.04；审查期间没有修改实现、Schema、golden、expected 或 Git，只写本文件。依据为唯一架构 A08/A17（必要引用 A03/A05/A19/A21）及 D0.04 执行卡。技术复核不代替人工签核，不把 Python 确定性模型当成真实 Runtime、授权适配器、多线程或多进程观察完成。

## 发现及整改复核

| 项目 | 修复前事实与依据 | 修复后核验 |
|---|---|---|
| 已 Terminal 被高版本重新打开 | 父审查提出；A05/A17要求同执行同Host终结不可回退 | snapshot/event 两种较高版本 Running 均被拒，Terminal保留 |
| 新 Host 确认后的旧快照 | 父审查提出；A17要求世代变化重新建立视图，旧在途响应不得覆盖新世代 | subscribe确认绑定Host，旧Host get拒绝，新Host合法快照接受，旧stream/sequence清理 |
| gap 后完整快照不能修补同版本部分投影 | 本审查实际复现：get v1 Running→丢 Finalizing v2→progress v3→get v3 被拒；客户端仍 Running v3。违反A17.5重新get恢复规则 | 分开记录完整snapshot版本与部分通知版本；同版本完整get可补齐，旧版本仍拒绝。无gap、仅订阅progress也验证 |
| Atomic绑定后的Domain漏完整校验 | 本审查实际复现：`inputs.domain={provider:1,id:[]}`绑定Atomic/domain，内部纯compute，Check成功且pending=[]。违反A08.4绑定后整体校验 | Atomic及测试目录StateEdit target复用完整Domain字段Schema；错误类型/空字段/非ASCII/超长/额外字段/null/数组拒绝；合法domain接受 |
| 本地最后一个订阅退订后旧帧放行 | 本审查实际复现：先drain取得在途frame，再移除server订阅及模拟本地streams移除，空集合触发旧fallback，旧帧被接受。A17.5要求丢弃已退订世代迟到帧；原inflight测试未覆盖Client | 新增显式retire/disconnect模型步，始终要求已确认活动stream精确匹配；未确认、最后退订、重复/错误世代、旧stream在途及断线均验证 |

三项本审查反例由原实施者增加先行失败测试后修复，原始 red/green 索引见 [review-repair-status.md](review-repair-status.md)。父提出两项的原始记录由主集成保存；本文件逐项重新执行五项修复，没有把此前成功结果代替最终验证。

## 其余合同复核

核对了八类Plan节点的必需/可选字段、无外部return、exports映射、精确Operation版本、槽单赋值/禁止遮蔽/前向引用、Typed ExecutionRef与DataRef/AtomicResult区分、Await/授权导出票据寿命、RFC6901转义/Missing/null、绑定重叠和常量冲突、Atomic子集与动态检查、分支类型、固定ForEach、Parallel定义序、静态/累计指令及独立预算模型。16条node golden涵盖每节点正反例，两个正式示例可通过合同检查。

Control部分核对三种请求/一种服务端无id推送方向、共享身份DTO、有界字段/uint64语义、ack前屏障、终态建立竞态、授权过滤/发送仲裁、撤权残留、连接/订阅/队列配额、progress合并顺序与gap、phase/fact不受进度间隔强制延迟、Finalizing非Terminal、实时keyset分页与空页推进。24条消息golden及cursor固定向量覆盖Schema和语义边界。

cursor仅使用固定v1 canonical payload及域分离HMAC-SHA256，核MAC后解释字段；绑定caller/授权view/filter/Host/Store/Restore/排序位置/期限，拒绝篡改、未知算法、过期、跨世代及非法位置。模型逐cursor句柄为0，文档没有stateful v1退路。真实索引性能、OS认证、secret保护、帧/连接超时与Runtime子执行仍明确留在后续责任包，不以本轮模型冒充完成。

## 独立实际运行

第一次读取父修复版本后，在单Python进程从两个真实测试模块加载全部用例：50项、0失败、0错误，exit 0；随后额外probe独立发现上表三项遗漏。修复冻结后重复同一实际模块加载方式，输出为：

```text
Ran 53 tests in 2.270s
OK
actual_tests=53 failures=0 errors=0 skipped=0
expected=53 discovered=53 missing=[] unexpected=[]
```

命令为 `python -X utf8 -B -`，输入脚本从 `tests/plan/test_plan_contract.py` 与 `tests/control/test_control_contract.py` 使用unittest真实加载，另外从AST独立发现并对照固定expected；实际进程exit 0。该53项聚合运行是独立技术复核，不声称等同CTest逐项证据。实施者最终逐项记录位于 `review-repair-20260907T062403Z-verify/verification.json`；主集成的正式采集另行绑定全部源码/工具链/二进制。

下方五项通过现有 `run_cases.py --case` 各开独立进程执行。为满足本次只写一个review文件的范围，每个stdout/stderr原始字节以base64保留，并附长度/SHA-256；不是手工填写Passed。

```json
{
  "review_finished_at": "2026-09-07T06:28:19.695621+00:00",
  "source_commit": "4e96e99e157877feb20caf9a8351d909c8e7f71e",
  "source_dirty": true,
  "inputs_changed_during_targeted_run": [],
  "actual_processes": [
    {
      "case": "T19.control.client_terminal_cannot_reopen",
      "argv": [
        "C:\\Users\\xrx10\\AppData\\Local\\Programs\\Python\\Python311\\python.exe",
        "-X",
        "utf8",
        "-B",
        "tests/plan/run_cases.py",
        "--case",
        "test_control_contract.ControlContracts.test_T19_control_client_terminal_cannot_reopen"
      ],
      "cwd": "E:\\VS2019Qt5.15\\3D",
      "started_at": "2026-09-07T06:27:51.488449+00:00",
      "finished_at": "2026-09-07T06:27:54.333445+00:00",
      "exit_code": 0,
      "stdout_size": 0,
      "stdout_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "stdout_base64": "",
      "stderr_size": 245,
      "stderr_sha256": "68fbf3920e9915b45468d4c4bf78fed44448caefc1f48ce079fc4a7c7359464c",
      "stderr_base64": "dGVzdF9UMTlfY29udHJvbF9jbGllbnRfdGVybWluYWxfY2Fubm90X3Jlb3BlbiAodGVzdF9jb250cm9sX2NvbnRyYWN0LkNvbnRyb2xDb250cmFjdHMudGVzdF9UMTlfY29udHJvbF9jbGllbnRfdGVybWluYWxfY2Fubm90X3Jlb3BlbikgLi4uIG9rDQoNCi0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0NClJhbiAxIHRlc3QgaW4gMC4wMjJzDQoNCk9LDQo="
    },
    {
      "case": "T19.control.client_old_host_snapshot_rejected",
      "argv": [
        "C:\\Users\\xrx10\\AppData\\Local\\Programs\\Python\\Python311\\python.exe",
        "-X",
        "utf8",
        "-B",
        "tests/plan/run_cases.py",
        "--case",
        "test_control_contract.ControlContracts.test_T19_control_client_old_host_snapshot_rejected"
      ],
      "cwd": "E:\\VS2019Qt5.15\\3D",
      "started_at": "2026-09-07T06:27:54.336415+00:00",
      "finished_at": "2026-09-07T06:27:57.217272+00:00",
      "exit_code": 0,
      "stdout_size": 0,
      "stdout_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "stdout_base64": "",
      "stderr_size": 253,
      "stderr_sha256": "5161e9e0906534100483c21f338693a9e6351a22f6a66e10c1a90f8b8e4fe472",
      "stderr_base64": "dGVzdF9UMTlfY29udHJvbF9jbGllbnRfb2xkX2hvc3Rfc25hcHNob3RfcmVqZWN0ZWQgKHRlc3RfY29udHJvbF9jb250cmFjdC5Db250cm9sQ29udHJhY3RzLnRlc3RfVDE5X2NvbnRyb2xfY2xpZW50X29sZF9ob3N0X3NuYXBzaG90X3JlamVjdGVkKSAuLi4gb2sNCg0KLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLQ0KUmFuIDEgdGVzdCBpbiAwLjAxNHMNCg0KT0sNCg=="
    },
    {
      "case": "T19.control.full_snapshot_completes_partial_same_version",
      "argv": [
        "C:\\Users\\xrx10\\AppData\\Local\\Programs\\Python\\Python311\\python.exe",
        "-X",
        "utf8",
        "-B",
        "tests/plan/run_cases.py",
        "--case",
        "test_control_contract.ControlContracts.test_T19_control_full_snapshot_completes_partial_same_version"
      ],
      "cwd": "E:\\VS2019Qt5.15\\3D",
      "started_at": "2026-09-07T06:27:57.218275+00:00",
      "finished_at": "2026-09-07T06:27:58.895914+00:00",
      "exit_code": 0,
      "stdout_size": 0,
      "stdout_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "stdout_base64": "",
      "stderr_size": 275,
      "stderr_sha256": "d2e983e48a49b1ab3db9c20184f0d0ae10e1a90d5c77a518b2d84c041f93939d",
      "stderr_base64": "dGVzdF9UMTlfY29udHJvbF9mdWxsX3NuYXBzaG90X2NvbXBsZXRlc19wYXJ0aWFsX3NhbWVfdmVyc2lvbiAodGVzdF9jb250cm9sX2NvbnRyYWN0LkNvbnRyb2xDb250cmFjdHMudGVzdF9UMTlfY29udHJvbF9mdWxsX3NuYXBzaG90X2NvbXBsZXRlc19wYXJ0aWFsX3NhbWVfdmVyc2lvbikgLi4uIG9rDQoNCi0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0NClJhbiAxIHRlc3QgaW4gMC4wMzNzDQoNCk9LDQo="
    },
    {
      "case": "T04.plan.bound_domain_and_target_validate_full_schema",
      "argv": [
        "C:\\Users\\xrx10\\AppData\\Local\\Programs\\Python\\Python311\\python.exe",
        "-X",
        "utf8",
        "-B",
        "tests/plan/run_cases.py",
        "--case",
        "test_plan_contract.PlanContracts.test_T04_plan_bound_domain_and_target_validate_full_schema"
      ],
      "cwd": "E:\\VS2019Qt5.15\\3D",
      "started_at": "2026-09-07T06:27:58.896882+00:00",
      "finished_at": "2026-09-07T06:28:02.118785+00:00",
      "exit_code": 0,
      "stdout_size": 0,
      "stdout_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "stdout_base64": "",
      "stderr_size": 263,
      "stderr_sha256": "ad9221a6c76a436b0fa1b304861e14fbeb181700fd5b9bedd8abe0c68e1c6629",
      "stderr_base64": "dGVzdF9UMDRfcGxhbl9ib3VuZF9kb21haW5fYW5kX3RhcmdldF92YWxpZGF0ZV9mdWxsX3NjaGVtYSAodGVzdF9wbGFuX2NvbnRyYWN0LlBsYW5Db250cmFjdHMudGVzdF9UMDRfcGxhbl9ib3VuZF9kb21haW5fYW5kX3RhcmdldF92YWxpZGF0ZV9mdWxsX3NjaGVtYSkgLi4uIG9rDQoNCi0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0NClJhbiAxIHRlc3QgaW4gMC4xMDNzDQoNCk9LDQo="
    },
    {
      "case": "T19.control.client_retirement_rejects_inflight_streams",
      "argv": [
        "C:\\Users\\xrx10\\AppData\\Local\\Programs\\Python\\Python311\\python.exe",
        "-X",
        "utf8",
        "-B",
        "tests/plan/run_cases.py",
        "--case",
        "test_control_contract.ControlContracts.test_T19_control_client_retirement_rejects_inflight_streams"
      ],
      "cwd": "E:\\VS2019Qt5.15\\3D",
      "started_at": "2026-09-07T06:28:02.119786+00:00",
      "finished_at": "2026-09-07T06:28:19.684010+00:00",
      "exit_code": 0,
      "stdout_size": 0,
      "stdout_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "stdout_base64": "",
      "stderr_size": 271,
      "stderr_sha256": "f4833e25429c51494ced0e6b2415bb78606ed6dc6d0d07b73450eea18cfd4305",
      "stderr_base64": "dGVzdF9UMTlfY29udHJvbF9jbGllbnRfcmV0aXJlbWVudF9yZWplY3RzX2luZmxpZ2h0X3N0cmVhbXMgKHRlc3RfY29udHJvbF9jb250cmFjdC5Db250cm9sQ29udHJhY3RzLnRlc3RfVDE5X2NvbnRyb2xfY2xpZW50X3JldGlyZW1lbnRfcmVqZWN0c19pbmZsaWdodF9zdHJlYW1zKSAuLi4gb2sNCg0KLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLQ0KUmFuIDEgdGVzdCBpbiAwLjAzMnMNCg0KT0sNCg=="
    }
  ]
}
```

## 审查输入指纹

以下为最后定向复核前读取、完成后再次核对未变化的文件。原始规范指纹仅作为所依照文字身份，不修改唯一规范。

```json
{
  "docs/01_Architecture_v3.3.md": "f20644428ed8c307109e26fe690e951f7e191659903bb496ba8dee51125bca2d",
  "docs/02_Execution_Plan_v3.3.md": "7daf3c8a7e68462a97cd593d2ab9a35c1888b720428c72bc9f915049050df511",
  "docs/contracts/control-observation.md": "830fe3ff6c0eaf300aa35f332f4838c9462e5c1edc5fca6e577852c0bcc3b1fe",
  "docs/contracts/cursor-v1.md": "c39b4356ccf74f29bcf67281c735424710a569f307b20c061ce9f5e134f0224a",
  "docs/contracts/plan.md": "3dd3a15b20a68433fde32f7542fa7fa239cf55c0a810c76fa08f5ce4e82ab2a2",
  "examples/plans/compute-apply.plan.json": "6ad1eba9d3c637a91b813263b1fe905c281be03a4c5e41d1a879bdc671796d87",
  "examples/plans/settings-atomic.plan.json": "a9e6e50e91204b8f119ce1e1d615da0877b6e80a4f163804bfd45011d3fb57b4",
  "schemas/plan-v1.schema.json": "3a07cf8a1a4de201f25756ce780c747c2c12659e1f62ad406725c90b812805f3",
  "schemas/rpc-v1/common.schema.json": "27292f7f18010c155ce07864398a4e6d06930ccbcb973fcbf98910685bfdb2c0",
  "schemas/rpc-v1/execution-list.schema.json": "53d3e3d7f316e17c70afe6fc4b9074162cb33f4582a1d274deecb38dcb6bb8aa",
  "schemas/rpc-v1/notifications.schema.json": "f88e11256e03d9d667bf40d7bcb49eff8f8204c5259bc020222f7976f3be8644",
  "tests/control/control_model.py": "e939c80f6c4062e44f77517583f0b52c2277187ad9daa06cb21b2eaf796980f0",
  "tests/control/golden/cursor.json": "9d0ff462f7bfadea3e7eefb0e0fd6badb2676877dd4b3ec48ed55dfcfd04be52",
  "tests/control/golden/messages.json": "cc3d609ab4b5df116a3363220aa34143104605419bf3d4331ffeeb5e8ce043ad",
  "tests/control/test_control_contract.py": "f46b7f679da233d4a36f676d4c0a7deb114439c220116b172a6f374f24aa5dec",
  "tests/manifests/d0.04.expected.json": "704e26d58216fcfa32255fc9f3115ca767816bc4d453db84848ab0a195aec2cd",
  "tests/plan/golden/nodes.json": "fcdf6d97ad133057fe941b3610237535aadd11d15e3d6f3b7e88da57ee283466",
  "tests/plan/plan_model.py": "fdea7794b72549818fc758b346e8b521efac6045c33c412cc63488986048e124",
  "tests/plan/run_cases.py": "b480ab6e6a58b06b2e3d25f085dc6c8acc77cd7356f87a1d48e81a7b432d14d8",
  "tests/plan/test_plan_contract.py": "af674ee2e915ad6e9c4c2362d7131af341e040269c9897d3b41b25cabb81bbdc"
}
```

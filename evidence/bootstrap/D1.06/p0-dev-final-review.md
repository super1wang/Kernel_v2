# P0 dev 工具最终独立增量复核

AI SPEC：**Approved（限定范围）**。AI CODE：**Approved（限定范围）**。仅覆盖下列七项最终输入；不批准 D1.06 Host 行为、整体 P0 夹具/Summary 实现或工作包验收。

## SPEC

原 S1 共享夹具传递漏测已关闭：test_support.hpp 与 conformance/core_contracts 材料都选择 contracts、Registry、Policy、Native，头文件同时选择 Release；实际选择结果已核对。未知路径、缺失 D1.06 full 集合、空/跳过执行仍按原范围拒绝或不声明通过。两个新增工具检查保持独立开发支持 manifest，不修改正式 expected。完整集合缺失现于 Python 子命令前预检，避免无效运行。

## CODE

强制 clean-first 及临时策略测试已移除；原 C1 的撤回事实见 [独立实测报告](p0-dev-postbuild-retraction.md)，原报告和所有失败记录保留。现有每次 configure/build、执行后精确 JUnit、源前后摘要与 Job 排空检查继续生效。

新增 capture_build 在成功 build 后、CTest 前调用既有 observe，核对实际 cache/toolchain；身份错误及无任何实际 exe 均拒绝，并记录实际存在 exe 的 SHA/字节数。此记录不是所有声明产物齐全的断言，最终用例完整性仍由本轮精确 JUnit 核对。未发现本次增量仍需修复的问题。导航生成器未变，保持非规范、完整来源/HEAD/分支/结构重算和 stale 回退；阶段推进仍须更新固定映射。

## 实测与输入

独立 owned Job 执行 **18/18** 工具单测，exit 0、active_after=0、未终止残余进程，七项输入前后稳定。指定的 compile-configure-9aacc1ce3157 缺 toolchain-Debug.json，被正确拒绝。只读已有 d1.05-integration-e349937e 的真实 cache/toolchain 成功，并记录 10 个 exe；该正控制只验证 helper，未重新 configure/build/CTest，不继承旧树为本轮开发通过。机器结果、真实流摘要、两个共享路径选择和完整身份见 [checks.json](p0-dev-final-review-20260908-e247/checks.json)。

| 文件 | SHA-256 |
|---|---|
| `tools/dev/verify.py` | `a21597bb92e65d21978f59f516880f3296f3c9eb3e687f54a8adf980c794cb9e` |
| `tools/dev/current.py` | `ee05f2457c3fbf3a80ff1133292bc51ca9a39e5df2e43d6bc2b8d7bfa501b24f` |
| `tools/dev/README.md` | `dec4a695bbf79dc792a7b33cc7a4d63f8390554411f054b8dd9069c7fa92af43` |
| `tests/tools/dev/test_verify.py` | `5f6f8acaf7010837578ec4e8f6557add37705c8f677b28057b98ca93d68ae5b6` |
| `tests/tools/dev/test_current.py` | `74dfa48192c0201a97b95a23d0950b82d252f07e539981f9173331bdebb9e944` |
| `tests/manifests/d1.06-tools.expected.json` | `fb6e6b27f9661ff6679e9381fc95b964cc4c64f6bf5b8837352e6577a5069784` |
| `.gitignore` | `e3783c2643d6deca546765ae8bcede905290f007e1608ebcaea01a6fde0d72f7` |

本范围无未关闭阻断项。后续全体 P0 交付结论仍由根聚合夹具、Summary 和实际代表性运行证据；不因此重跑已验收 D1.05 矩阵。

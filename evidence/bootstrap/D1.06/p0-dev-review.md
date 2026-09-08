# P0 开发入口独立复核

AI 结论：**ChangesRequested**。范围仅 `tools/dev/verify.py`、`current.py`、README、两份 `tests/tools/dev/test_*.py`、开发支持 manifest 及 `.gitignore` 导航增量；不批准 D1.06 行为或工作包，不替代夹具和 Evidence summary 的独立审核。精确七项 SHA、真实 Python 运行及输入前后稳定核对见 [机器材料](p0-dev-review-20260908-a4d1/checks.json)。

## SPEC

未知路径升级当前包、缺失 D1.06 正式 expected 时拒绝 full、固定历史 family 只表达已实现消费者影响集、空集合不声明 Passed、开发支持两项不修改正式 expected、显式风险配置、导航不产生审核批准，均符合当前 P0 薄入口范围。

**S1 / P1：共享测试材料的传递影响缺失。** `verify.py:41` 将 `tests/compile/contracts/test_support.hpp` 限为 contracts/compile-tools。`tests/contract/registration/fixtures.hpp:2` 和 `tests/contract/authorization/fixtures.hpp:3` 直接包含该文件；`tests/contract/native/fixtures.hpp:3` 再包含授权夹具。该材料定义 TypeContract、Provider、handler 的运行行为，成功重编译无法替代下游断言执行。实际选择结果已保存，确实缺 registry、policy、native。应为该精确共享路径补下游集合并增加选择反例。

## CODE

逐项检查了 configure/build 失败中止、非空 Python 计数和跳过拒绝、完整名称正则、JUnit 缺项/重复/非 Passed 拒绝、源摘要前后比较和 owned Job 排空条件。current 的完整结构/来源/HEAD/分支重算能拒绝伪造静态 stale=false；本地 ignored 导航避免自身 HEAD 递归。

**C1 / P1：增量构建可能沿用旧测试发现结果。** `verify.py:198` 删除 `--fresh` 后复用 profile 构建树，而合同、Registry、Policy、Native 的 `discover.py` 仅挂于 executable 的 POST_BUILD（对应 CMakeLists 分别第 42、12、12、18 行）。仅修改 discover.py 不必触发 executable 重链接，configure/build 成功后仍可能使用旧的 `*-tests-<Config>.cmake` 命令或属性；当用例名未变时 JUnit 精确集合仍可通过。源前后摘要也不能证明注册文件来自新发现脚本。应在此情形显式重新发现或窄失效重建，并补旧树上仅改发现脚本的控制。此项为代码路径证据；本审核未运行共享 C++ 构建，不冒称已获得实际 C++ red。

## 实测与边界

独立 owned Job 执行当前 dev 工具单测 **15/15**，exit 0，active_after=0，未终止残余进程；七项输入稳定。现有绿测未覆盖上述两项，不能据此 Approved。保持历史证据不变；修复后复核相应差异与控制即可。导航当前只描述 P0，后续阶段推进必须同步更新其固定 phase/review 映射，不能以 refresh 代替语义更新。

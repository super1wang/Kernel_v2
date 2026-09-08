# 内核开发提速与 Token 精简决策方案

日期：2026-09-08  
版本：v2（增加微批次开发、集中测试与默认最小影响集规则）  
交付对象：后续开发 AI、主集成者  
当前分支：`work/d0-kernel-baseline`  
当前基线：`9cd8439a1fd9d95bb4062e04f3f6ab7bb4d781b9`  
当前状态：D1.01–D1.05 Passed；D1.06 尚未开始；G1 InProgress  
文档性质：开发效率与验证流程决策；**不新增架构、不新增工作包编号、不改变 v3.3 唯一规范地位**

---

## 1. 决策摘要

当前开发提速不再以“继续减少测试数量”为主要方向，而改为同时解决五类重复成本：

1. **重复构建成本**：Compile Contracts 与 Install Consumer 中大量重复 `configure/build`。
2. **重复验证成本**：每个小修改、小任务或小工作包都机械执行大范围 CTest / 三 Profile。
3. **重复节点切换成本**：高度相关的小任务被拆成多个独立开发—审核—验收循环。
4. **重复上下文成本**：AI 每轮重复读取两份大型主规范、大量历史审核和完整 Evidence。
5. **重复审核搬运成本**：SPEC 与 CODE 审核重复加载同一批规范、代码、测试和证据。

新的执行原则为：

> **相关小任务尽量微批次开发，批次内只跑必要影响集，稳定后集中正式验证；正式验收保持严格；复用准备，不复用结论；AI 默认读摘要，异常时再展开完整证据。**

本轮只实施能够直接降低开发等待和 Token 消耗、且不改变生产合同与正式门禁语义的优化。

---

## 2. 当前事实与前提

### 2.1 当前开发节点

当前 `work/d0-kernel-baseline` 已推进至：

```text
9cd8439a1fd9d95bb4062e04f3f6ab7bb4d781b9
```

该提交完成 D1.05 的正式验收，当前状态：

```text
D1.01 Passed
D1.02 Passed
D1.03 Passed
D1.04 Passed
D1.05 Passed
D1.06 NotStarted
G1    InProgress
```

因此：

- 不重开 D1.05；
- 不重新执行 D1.05 已完成正式矩阵；
- 本次优化完成后直接进入 D1.06；
- P1/P2 未完成优化不得成为 G1 的新增前置。

### 2.2 已确认的主要运行瓶颈

上一版决策中已确认：

- D1.04 Debug 共 251 项 CTest，约 952.1 秒；
- 其中 31 项编译/SDK 包装约 903.5 秒，占约 94.9%；
- 包装内部约 41 次 configure、75 次 build；
- install 本身仅约 1.4 秒；
- Python 模型、CHECK、主 gate 不是当前近一小时矩阵的主要瓶颈。

因此继续优先优化：

```text
重复 configure
    ↓
重复 build preparation
    ↓
重复 consumer project setup
```

而不是优先删测试、并行化所有 CTest 或压缩 install 动作。

---

## 3. 总体目标

本轮优化有六个目标，按优先级排序：

1. 将“代码修改 → 有效反馈”的开发循环显著缩短。
2. 将同一工作包内高度相关的小任务，以及满足并行条件的小工作包，组成微批次，减少节点切换和重复验收。
3. 消除同一 Profile 内重复建立 CMake 子工程的主要成本。
4. 默认禁止与当前改动无关的完整 CTest；开发期只执行必要影响集与风险专项。
5. 降低 AI/Codex 每个工作包读取历史材料和 Evidence 的 Token 成本。
6. 获得可核验收益后立即回到 D1.06，不把测试工具继续平台化。

成功标准不是“测试数量减少”，而是：

```text
保持行为覆盖
+
减少重复准备
+
减少重复读取
+
减少无效全量运行
```

---

# 4. 新的验证分层

以后统一采用四层验证，而不是把正式验收入口用于所有开发动作。

## 4.1 L0：开发快跑 `dev-fast`

### 目的

用于每次有意义修改后的快速反馈。

### 默认执行

```text
changed files
    ↓
显式影响映射
    ↓
Debug build
    ↓
本包直接受影响测试
    ↓
新增反例 / 正控制
    ↓
必要的风险专项
```

默认：

- Debug；
- fail-fast；
- 只执行新增测试、直接影响集、必要传递影响集和风险专项；
- **不执行与当前改动无关的完整 CTest**；
- 不执行完整三 Profile；
- 不生成正式包级 Passed；
- 不要求完整正式 Evidence；
- 失败立即回开发，不继续跑无关测试。

### `full CTest` 升级条件

开发期 `ctest all` / 当前阶段完整历史回归属于**例外动作**，只有出现以下任一条件才升级：

1. 修改 CMake、toolchain、SDK export、dependency lock、公共编译选项或测试注册基础设施；
2. 修改 Foundation / CoreContracts 等广泛公共合同、公共头、ABI/noexcept/模板边界，无法用窄影响集覆盖；
3. 修改 discovery、fixture、expected、manifest、gate、Evidence 采集/结果映射；
4. 影响范围不明确、显式影响映射缺失或曾被证明漏测；
5. 当前执行本身就是 L2 正式验收或 L3 阶段 Gate；
6. SPEC/CODE 审核发现跨包风险，明确要求扩大回归；
7. 出现只能通过跨 Profile / 全阶段组合才能复现的问题。

除此之外，开发 AI 不得为了“更保险”默认执行完整 CTest。扩大测试范围必须能说明触发原因。

建议增加薄入口：

```text
tools/dev/verify.py
```

示例：

```powershell
python -X utf8 tools/dev/verify.py D1.06 --changed
python -X utf8 tools/dev/verify.py D1.06 --changed --risk asan
python -X utf8 tools/dev/verify.py D1.06 --full
```

### 影响映射原则

首版只允许**简单、显式、可审查映射**，例如：

```text
packages/Foundation/**        -> foundation, contracts, native
packages/CoreContracts/**     -> contracts, registry, native
packages/Host/**              -> host, native, install-consumer
packages/Logging/**           -> logging, host
cmake/**                      -> build, install, compile-contracts
sdk/**                        -> install-consumer
tools/evidence/**             -> evidence-selftests
tests/compile/contracts/**    -> compile-contracts
tests/install_consumer/**     -> install-consumer
unknown                       -> full current package
```

规则：

- 不建设自动依赖推理系统；
- 不通过当轮 discovered 反向生成影响集；
- 映射不明确时扩大验证；
- 不允许因为 `dev-fast` 通过而声明包 Passed。

---

## 4.2 L1：包级开发验证

实现趋于稳定后执行。

最低内容：

- 当前包 Debug 全影响集；
- 新增合同反例；
- 公开头 / ABI / noexcept / 模板等相关消费者；
- 按风险补 Release 或 ASan；
- 构建系统变化补干净 configure 和安装消费者。

L1 仍属于开发事实，不是正式验收。

---

## 4.3 L2：包级正式验收

仅在以下输入稳定后执行：

- 实现稳定；
- 测试稳定；
- expected 固定；
- 文档与审核输入稳定；
- SPEC/CODE 审核目标明确。

执行受审正式 Profile 矩阵。

原则：

> **正式矩阵只要求对最终输入完整成立，不要求开发阶段预先机械跑一遍同样的完整矩阵。**

正式矩阵失败：

- 保留失败；
- 修复后追加 run；
- 不能覆盖旧失败；
- 改变正式输入后按新输入重新绑定；
- 不能拼接不同源码版本的成功配置冒充全绿。

---

## 4.4 L3：G 阶段集成门禁

只验证阶段级真实闭环：

- 必需消费者；
- 安装边界；
- 配置组合；
- 阶段关键回归；
- 性能 / 占用预算；
- 真实 IPC / 恢复 / 持久化等所属阶段能力。

G0→G8 顺序保持不变。

---

# 5. P0-0：相关小任务微批次开发与集中测试

这是本版新增的开发效率规则，并立即生效。

## 5.1 目标

避免把同一能力附近的多个小修改机械拆成：

```text
小任务 A
→ 完整测试
→ 审核
→ Evidence
→ 提交

小任务 B
→ 再完整测试
→ 再审核
→ 再 Evidence

小任务 C
→ 再重复一次
```

默认改成：

```text
相关小任务 A
  ↓ dev-fast(A)
相关小任务 B
  ↓ dev-fast(A+B影响集)
相关小任务 C
  ↓ dev-fast(A+B+C影响集)
批次输入稳定
  ↓
一次适用的正式矩阵 / 集中审核
```

批次化减少的是：

- 重复上下文加载；
- 重复 configure/build；
- 重复无关 CTest；
- 重复审核材料生成；
- 频繁提交/恢复任务造成的 Token 消耗。

## 5.2 允许合并的第一类：同一工作包内的小任务

同一工作包内部的子任务、实现步骤、反例修复和消费者补齐，**默认视为一个 Development Batch**。

例如 D1.06 内部：

```text
Host 最小实现
Logging 共同合同
NativeSubset 占用基线
安装消费者
D1.06 专项反例
```

不要求每完成一个小项就跑一轮完整矩阵。

推荐：

```text
实现一个逻辑小项
    ↓
dev-fast --changed
    ↓
继续下一相关小项
    ↓
批次稳定
    ↓
D1.06 一次正式受审矩阵
```

只有高风险变化才按第 4.1 节升级测试范围。

## 5.3 允许合并的第二类：彼此无严格 Passed 前置关系的并行小工作包

多个独立工作包可以组成跨包 Development Batch，但必须同时满足：

1. 各成员在批次开始前，其**批次外前置包均已 Passed**；
2. 批次成员之间不存在“必须先 Passed 才能开始后一个”的严格前置关系，或 v3.3 已明确允许并行；
3. 修改区域、构建 Profile、消费者或测试准备高度重合；
4. 不跨独立 G Gate、外部 ABI 冻结、持久化格式冻结、安全边界冻结等必须独立验收的边界；
5. 每个工作包的 expected、需求映射、审核结论和最终状态仍可独立判定。

允许一个物理正式 run 覆盖多个批次成员，但必须产生可机器分区的结果：

```text
one source snapshot
one Profile preparation
one physical CTest run
        │
        ├─ Dn.xx expected / executed / result
        ├─ Dn.yy expected / executed / result
        └─ Dn.zz expected / executed / result
```

**共享运行不等于共享 Passed。** 任一包缺少自己的完成条件，只能该包保持 InProgress/Failed，不能被同批其他包带过。

## 5.4 严格前后置工作包不得伪合并

如果：

```text
B requires A = Passed
```

则不得为了减少测试而：

```text
A 未 Passed
→ 直接实现 B
→ 最后一起宣布 A/B Passed
```

这会破坏现有工作包 DAG。

对于严格顺序包，采用：

```text
A 开发
→ A 必要正式适用集
→ A 独立 Passed
→ B 开发
→ B 必要正式适用集
→ B 独立 Passed
→ 阶段 Gate 再做一次集中跨包回归
```

关键变化在于：

> **A/B 的包级正式验收只跑各自适用的正式集合，不默认累计执行全部历史 CTest；跨包完整集成回归集中放到 G Gate。**

因此即使不能合并工作包，也仍可避免“每个小包都全仓 full CTest”。

## 5.5 集中测试的默认规则

测试范围采用三层集合：

```text
S_changed   = 当前改动直接影响测试
S_required  = 当前工作包正式完成条件要求的测试
S_gate      = 当前 G 阶段跨包集成回归
```

执行规则：

```text
开发期     → S_changed
包级正式   → S_required
阶段 Gate  → S_gate
```

不再默认：

```text
开发期 / 每个包
→ S_changed + S_required + 全部历史测试
```

若 `S_required` 已包含某项历史测试，不重复注册/执行第二次。

## 5.6 微批次审核与 Token 规则

同一 Development Batch 允许：

- 一次加载相关 v3.3 章节；
- 一次加载当前批次 diff；
- 一次加载 Evidence summaries；
- 在同一上下文中输出多个独立 SPEC/CODE 结论。

但必须保持：

- 工作包结论独立；
- source digest 精确；
- review kind 独立；
- 失败不能被批次平均或隐藏；
- 不复制同一结论冒充多个审核。

## 5.7 拆批条件

出现以下任一情况必须拆出独立验证：

- 新增或改变公共基础合同，影响范围显著扩大；
- 工具链 / CMake / SDK / dependency lock 变化；
- 安全、权限、持久化、恢复、ABI 等高风险冻结边界；
- 一个成员需要独立 Gate；
- 影响映射不可靠；
- 成员之间出现严格 Passed 前置关系；
- 批次过大导致审核无法清晰区分各包责任。

批次大小以“减少重复且仍能清晰审计”为准，不追求一次合并尽可能多的工作包。

---

# 6. P0-1：Compile Contracts 改为 Profile 级构建夹具

这是本轮最优先的机器时间优化。

## 6.1 当前问题

当前编译合同测试按 Case 建立独立工作目录，每个 Case 可能重复：

```text
生成 CMakeLists
cmake configure
build positive
build negative A
build negative B
...
```

CTest 的“测试隔离”实际上被实现成了“CMake 工程隔离”。

二者没有必要绑定。

## 6.2 新结构

改为：

```text
Profile Debug
    │
    └─ compile-contract fixture
         │
         ├─ configure child-superproject     ← 一次
         │
         ├─ positive target
         ├─ missing_contract target
         ├─ nested_borrow target
         ├─ bad_validator target
         ├─ wrong_context target
         ├─ host_lookup target
         └─ ...
```

然后每个 CTest 仍然实际执行自己的 target：

```text
cmake --build <shared-build> --target <negative-case>
```

并分别检查：

- 真实非零返回；
- 目标 C++ 诊断类别；
- 正控制可编译；
- 子进程排空；
- 目标测试独立命令记录。

## 6.3 推荐机制

优先使用 CTest：

```text
FIXTURES_SETUP
FIXTURES_REQUIRED
```

共享 build tree 时：

- 初版保持串行；
- 如需并发，使用 `RESOURCE_LOCK`；
- 同一 build 目录不允许未受控并发 MSBuild。

## 6.4 Fixture 身份

至少包含：

```text
source digest
compiler
toolset
SDK
architecture
configuration
CRT
ASan
public compile options
dependency lock
test-support / factory inputs
```

身份不一致：

```text
拒绝复用
或
重新准备
```

不得直接 Passed。

## 6.5 必须保留的反例

至少验证：

1. fixture 身份不匹配不能复用；
2. fixture 准备失败后旧对象不能让后续 Case 通过；
3. 工具链失效不能误计为合同拒绝；
4. 两个负例必须分别真实执行；
5. 正控制必须可编译；
6. expected/discovered/executed 对照仍阻止少跑和漏报。

---

# 7. P0-2：Install Consumer 改为只读安装根复用

本项在 Compile Contracts 优化完成并测得收益后实施。

## 7.1 目标结构

```text
Profile Fixture
    │
    ├─ normal-install/
    ├─ no-tests-install/
    └─ consumer-project/
         ├─ public_header_identity
         ├─ public_header_context
         ├─ public_header_outcome
         ├─ macro_pollution_consumer
         └─ normal_contract_consumer
```

正常消费者共享：

- 一次 producer install；
- 一次 consumer configure；
- 只读安装树。

只有需要改变安装内容的 Case 才创建独立副本：

```text
missing header
missing dependency header
pruned component
relocated install root
```

## 7.2 边界

不得：

- 将独立安装消费者改成源码树 include；
- 让负例污染正常安装树；
- 用同一损坏安装树覆盖多个职责；
- 因 install 很快而过度重构安装系统。

本项目标主要是减少 consumer configure/build，而不是优化 `cmake --install` 本身。

---

# 8. P0-3：增加 AI 当前工作索引

增加一个**非规范性导航文件**：

```text
docs/ai-current.json
```

它不是第三份规范，只用于告诉 AI 当前工作包需要读取哪些已有规范区段。

## 8.1 建议结构

```json
{
  "format": "ock.ai-current/1",
  "normative": false,
  "current_package": "D1.06",
  "branch": "work/d0-kernel-baseline",
  "head": "<commit>",
  "last_passed": "D1.05",
  "gate": "G1",
  "prerequisites": ["D1.05", "D0.06"],
  "architecture_refs": [
    {
      "path": "docs/01_Architecture_v3.3.md",
      "sections": ["<D1.06相关章节>"],
      "sha256": "<sha>"
    }
  ],
  "execution_plan_refs": [
    {
      "path": "docs/02_Execution_Plan_v3.3.md",
      "sections": ["D1.06", "G1"],
      "sha256": "<sha>"
    }
  ],
  "contracts": [
    "<D1.06直接合同>"
  ],
  "active_reviews": [],
  "stale": false
}
```

## 8.2 使用规则

新 AI/Codex 开始任务时默认读取：

```text
AGENTS.md
docs/progress.md
docs/ai-current.json
当前包相关规范章节
当前包相关合同
当前 diff
```

不再默认读取：

- 两份完整 100KB+ 主规范；
- 全部历史 review；
- 全部旧工作包 validation；
- 完整 Evidence 大文件。

### 失效规则

若：

- 规范文件 SHA 不一致；
- 当前 HEAD 不匹配；
- current_package 与 progress 不一致；

则：

```text
ai-current stale
    ↓
禁止依赖摘要
    ↓
回退读取完整规范 / 最新 progress
    ↓
刷新索引
```

因此 AI 索引只减少读取，不改变规范真实性。

---

# 9. P0-4：增加 Evidence 快读摘要层

正式 Evidence 保持不变。

新增每个正式 run 的：

```text
summary.json
```

供 AI 和人工快速定位。

## 9.1 示例结构

```json
{
  "format": "ock.evidence-summary/1",
  "task": "D1.06",
  "profile": "win-msvc-debug",
  "source_digest": "<sha256>",

  "expected": {
    "count": 0,
    "digest": "<sha256>"
  },
  "discovered": {
    "count": 0,
    "digest": "<sha256>"
  },
  "executed": {
    "count": 0,
    "digest": "<sha256>"
  },

  "failed": [],
  "skipped": [],
  "missing": [],
  "unexpected": [],

  "commands": {
    "configure_count": 0,
    "build_count": 0,
    "install_count": 0
  },

  "timing_seconds": {
    "configure": 0.0,
    "build": 0.0,
    "test": 0.0,
    "total": 0.0
  },

  "result": "Passed",
  "full_report": "report.json",
  "junit": "round-001-junit.xml"
}
```

## 9.2 AI 默认读取顺序

以后审核：

```text
summary.json
    ↓
manifest / expected digest
    ↓
当前实现 diff
    ↓
相关合同
```

只有出现：

```text
failed != []
skipped != []
missing != []
unexpected != []
digest mismatch
异常退出
工具错误
审核疑点
```

才展开：

```text
report.json
discovered.json
JUnit
stdout/stderr
runtime-artifacts
source-inputs
```

## 9.3 目的

减少 AI 每轮重复读取：

- 约万行级 `report.json`；
- 万行级 `discovered.json`；
- discover stdout；
- 完整 source input 展开；
- 大量正常通过 JUnit。

完整原始 Evidence 必须继续存在，因此该层**只改变默认阅读路径，不改变正式验收事实**。

---

# 10. P1：SPEC / CODE 共用一次上下文加载

自动验收仍要求两个独立审核：

```text
SPEC
CODE
```

但不再要求两次独立地重新读取全部材料。

推荐：

```text
prepare review context once
        │
        ├─ SPEC review
        │    └─ D1.xx-spec.json
        │
        └─ CODE review
             └─ D1.xx-code.json
```

两份审核必须：

- 结论独立；
- 检查职责不同；
- 绑定同一精确 implementation/source digest；
- 不能复制相同内容伪装成两个审核；
- 任何一份不通过都不能自动 Passed。

共享的仅是：

- 规范输入；
- diff；
- Evidence summary；
- build identity；
- source digest。

该优化属于 Token / 上下文复用，不降低审核数量。

---

# 11. 审核材料收敛规则

以后每个工作包建议只维护一个主索引：

```text
docs/reviews/D1.xx-index.md
```

或等价 JSON。

索引仅指向：

```text
SPEC final
CODE final
expected
formal run summaries
gate / acceptance
历史 amendment
```

历史 revision 继续保留，但默认不进入 AI 当前上下文。

例如：

```text
D1.04-api-initial.md
D1.04-api-revision2.md
D1.04-api-revision3.md
D1.04-api-revision4.md
D1.04-api-revision5.md
```

都保留作为历史，但新 AI 默认只读取“最终有效版本 + 必要 amendment”。

原则：

> **历史完整，当前上下文精简。**

---

# 12. 不实施或暂缓实施

本轮明确不做：

- 分布式编译缓存；
- 远程 Evidence 平台；
- 通用测试调度服务；
- 自动测试相关性推理系统；
- Git 历史重写；
- 删除旧失败证据；
- 大规模并行 CTest；
- 同时切换 CMake generator；
- 升级工具链 / 三方依赖；
- 重构生产 Runtime；
- 为测试优化新增独立工作包编号；
- 把 P1/P2 变成 G1 新前置。

原因：

这些方案会增加实现和验证复杂度，并可能让“提速工作”本身成为新的长期工程。

---

# 13. 实施顺序

当前从 `9cd8439` 开始：

```text
D1.05 Passed
    ↓
P0-0 微批次 / 最小影响集规则立即生效
    ↓
P0-A Compile Contracts Profile Fixture
    ↓
最小工具反例
    ↓
一次真实前后测量
    ↓
P0-B dev-fast
    ↓
P0-C ai-current
    ↓
P0-D evidence summary
    ↓
停止测试基础设施优化
    ↓
D1.06 Host + Logging
    ↓
D1.06 正式验收
    ↓
G1
```

Install Consumer 深度复用：

```text
若 Compile Fixture 已获得明显收益
且仍是剩余主要热点
    ↓
继续实施

否则
    ↓
登记 P1
进入 D1.06
```

---

# 14. 本轮最低验收标准

P0 优化交付只需要满足以下条件：

## 14.1 正确性

- 原有关键行为覆盖保持；
- 微批次不改变工作包编号、前置 DAG、独立 expected 或 Passed 判定；
- 开发期默认只执行必要影响集，full CTest 仅在明确升级条件触发；
- 严格前后置工作包不会因批次化越过 `Passed` 前置；
- 编译反例仍逐项实际执行；
- 正控制仍实际编译；
- install consumer 仍为独立安装消费；
- expected/discovered/executed 对照仍有效；
- 失败历史仍追加保留；
- 正式包 Passed 语义不改变。

## 14.2 性能

记录优化前后：

```text
configure 次数
build 次数
install 次数
CTest 时间
完整正式 run 时间
```

优先要求证明：

```text
configure/build 次数显著下降
```

不要求为了证明性能额外跑多轮完整矩阵。

## 14.3 AI / Token

至少交付：

```text
docs/ai-current.json
summary.json schema / generator
AI 默认读取规则
stale fallback 规则
```

并证明：

- 完整 Evidence 仍可追溯；
- AI 摘要损坏不能使失败变 Passed；
- digest 不匹配会强制展开 / 失败；
- summary 不是正式证据替代品。

---

# 15. 停止条件

满足以下条件即停止优化：

1. 微批次 / 集中测试规则已作为默认开发策略生效；
2. `dev-fast` 可用于当前包快速验证，且不会默认跑无关 full CTest；
3. Compile Contracts 已从 Case 级重复 configure 转为 Profile 级共享准备；
4. AI 已有当前包导航索引；
5. Evidence 已有快读摘要；
6. 原工作包 DAG、独立 Passed 与正式门禁语义未减弱；
7. 已记录一次真实前后数据；
8. 剩余优化不再明显高于 D1.06 的开发收益。

即使未达到“完整矩阵 30 分钟以内”，只要取得明确、可复核的结构性收益，也应结束优化并回主线。

---

# 16. D1.06 的默认开发方式

完成上述 P0 后立即进入 D1.06。

D1.06 本身作为一个 Development Batch 处理。开发阶段默认：

```text
Host 最小实现
    ↓
dev-fast --changed
    ↓
Logging 共同合同 / 反例
    ↓
dev-fast --changed
    ↓
NativeSubset / 安装消费者
    ↓
dev-fast --changed
    ↓
按风险补 Release / ASan
    ↓
D1.06 输入整体稳定
    ↓
一次 D1.06 正式受审矩阵
    ↓
SPEC + CODE
    ↓
自动验收
```

不得重新回到：

```text
每次修改
    ↓
Debug full
    ↓
Release full
    ↓
ASan full
    ↓
再继续修改
```

除非影响范围不明确或当前改动属于工具链、公开 ABI、CMake、内存安全等高风险类别。

---

# 17. 可直接交给开发 AI 的执行指令

> 请在 `Kernel_v2 / work/d0-kernel-baseline` 从当前最新 HEAD 开始执行本决策。先核对 `docs/progress.md` 与 Git 实际状态；当前已知 D1.05 Passed，D1.06 尚未开始，不重跑 D1.05。
>
> 第一，从现在开始采用“微批次 + 最小影响集”规则。同一工作包内高度相关的小任务集中开发，每个小项只执行新增测试、直接/必要传递影响集和风险专项；不得默认执行与当前改动无关的 full CTest。只有本决策第 4.1 节列出的升级条件成立时才扩大为 full CTest，并记录原因。
>
> 第二，跨工作包批次只允许用于批次外前置均已 Passed、成员之间不存在严格 `Passed` 前置、且共享构建/消费者上下文的并行小包。严格前后置包仍分别 Passed，不得跨越 DAG；它们的包级正式测试只执行各自适用集合，跨包完整回归集中到 G Gate。
>
> 第三，优先把 `tests/compile/contracts/verify_children.py` 的 Case 级重复 CMake 工程改造成同一 Profile 内共享的编译夹具。优先使用 CTest Fixture；每个编译反例仍单独实际 build、单独检查返回码与目标诊断，正控制仍必须实际编译。不得把多个负例合并成一个“整体失败即成功”的 target。
>
> 第四，增加最薄的 `dev-fast` 开发验证入口。首版只实现显式影响映射；未知影响退回当前包全量。`dev-fast` 只能提供开发反馈，不能产生包级 Passed。
>
> 第五，增加 `docs/ai-current.json` 作为非规范性导航文件，只指向当前包所需的 v3.3 规范章节、合同和最新审核。规范 SHA 或 HEAD 不匹配时必须判 stale 并回退完整读取，不得把该文件变成第三份规范。
>
> 第六，为正式 run 增加 `summary.json` 快读层。完整 `report.json`、JUnit、raw、source-inputs 与历史失败全部继续保留。AI 默认读取摘要；只有失败、digest mismatch、缺项或疑点时才展开完整 Evidence。
>
> 第七，SPEC 与 CODE 仍分别真实审核并生成独立结论，但允许同一 Development Batch 共享一次规范、diff 和 Evidence 摘要上下文加载；不同工作包的审核结论和 source digest 仍独立，不能复制一份结论冒充多个审核。
>
> 第八，只补与上述工具变化直接相关的最小反例。记录优化前后的 configure/build/install 次数与总时间；不要为了“证明提速”额外运行多轮完整正式矩阵。
>
> 第九，P0 获得可核验收益后立即停止测试基础设施优化并把 D1.06 的 Host、Logging、NativeSubset、安装消费者视为一个内部开发批次推进，稳定后集中执行一次 D1.06 正式矩阵。P1/P2 未实施项如实登记，不得变成 G1 新前置。
>
> 执行完成时报告：批次范围、修改内容、实际执行的最小测试集合、为何没有/为何需要 full CTest、保留覆盖、configure/build 次数变化、真实耗时变化、Token/上下文读取路径变化、当前包状态、下一节点。未执行项明确写未执行，不得把方案或局部验证写成正式 Passed。

---

# 18. 最终决策

本轮正式采用以下五项作为新的 P0 开发效率基线：

```text
1. 相关小任务微批次开发，稳定后集中正式测试
2. dev-fast 最小影响集快速反馈，开发期默认不跑无关 full CTest
3. Profile 级 Compile Contracts 构建夹具
4. AI 当前包快读索引
5. Evidence 快读摘要层
```

并采用以下三个辅助原则：

```text
6. 严格前后置工作包不跨越 Passed DAG；包级只跑适用正式集，跨包回归集中到 G Gate
7. SPEC / CODE 保持独立结论，但同一批次共享一次上下文准备
8. 历史材料完整保留，当前 AI 上下文只读最终有效索引
```

核心边界不变：

> **合并相关小任务，不合并责任；减少无关测试，不减少必要覆盖；减少读取，不删除证据；集中验收，不跨越前置 DAG。**

该方案完成后，测试基础设施不再继续扩张，立即恢复 D1.06 与 G1 主线开发。

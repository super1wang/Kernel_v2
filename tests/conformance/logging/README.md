# D1.06 LoggingConformance 消费者

同一组五个 C++ 函数分别注入默认 MemoryLogging/DropOldest 和独立 TestBackend/RejectNewest，形成十个固定 `T22.logging` 主名。`cases.json` 记录共同子断言和可选能力表达式，不代替正式 expected、证据审核或 qualified_backends 登记。

两个后端的满载行为确实不同；测试后端独立格式化和存储，不转调生产内存日志。异常、强制 Busy、畸形报告、接受后抛和同步回调控制全部仅在测试对象内。普通合法模式与非合格故障模式分开；后者真实接受事实和 facade 报告不一致时，断言明确指出它不能 qualified，不模拟回滚或自动重试。

`develop.py` 默认只运行本目录 Debug 十二项（共同十项及固定两项补充），源码冻结到独立 `evidence/bootstrap/D1.06/logging-debug-*`，每个真实子进程使用既有 owned Job。命令、raw、发现集合、JUnit 和退出状态全部保留；不会修改旧 D1.05 证据。`--case` 只选择已知固定主名，发现仍必须等于完整十二项。`--mutation unclipped-flush` 只修改该轮隔离快照，去掉生产 flush 的水位裁剪，用单例负控制证明断言能捕获错误；该轮仍正常保留 Failed/非零，不能重新命名为测试通过。

初始 red 是 API 缺失编译失败；后续测试 helper 的 noexcept 表达式修正与初始 stream 合法性预期修正，均在独立失败轮次保留。它们不能被描述为不存在的生产行为修复。最终生产语义以固定合同、当前源码与实际成功轮次共同核对。

`T19.logging.memory_pages` 验证默认实现实际页公式、跨页淘汰及失败输出不变。`T23.logging.fixed_write_allocation` 在本测试 EXE 链接未修改的 D1.05 allocation_probe.cpp，执行十二入口正探针和无分配负探针，然后对两个合法后端各测四十次 Accepted 固定写入、四十次真实持续满载；窗口覆盖 SafeLogger、完整结果判定与析构。TestBackend 的满载是正确 Rejected/Full，不能当作 Accepted；memory 满载接受并淘汰旧记录。每次窗口单独判零，三个通道不相加，Release CRT 用 null、ASan 原 CRT 盲区保留。

原独立后端的动态 string 格式化已被真实 red 检出，现改为独立逐字符和手动十进制有界格式器，不调用生产格式助手。初始化、首次写、四次有限预热、填满、最后 owner 释放及驻留/峰值成本单列。开发驱动保存 verbose 原始完整计数报告并派生 allocation-report.json，不以 JUnit 截断输出替代计数材料。

此目录的局部 Debug 不替代全三配置、安装消费、Host 生命周期或 footprint 正式验收；固定日志写入不冒充完整 Host invoke，测试控制/初始化分配也不混入这两个稳态窗口。

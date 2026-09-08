# D1.06 Logging 实施者自审

这是实施者自审与局部运行交付，不是独立 SPEC/CODE Approved 或 D1.06 包级 Passed。当前九个源码/消费者文件已停止修改；完整逐文件 SHA、局部来源集合算法及原始结果摘要见同名 JSON。集合 SHA-256：`5d72a029b7295ca7431130950765bb0b4c72a5cf6c3c88b311fda8383e69369a`。已逐字节确认九文件与最终冻结源码快照相同。

## 实施范围

- 公共固定数据和 LogPort 位于 CoreContracts 第七头；`log_error` 是头内 inline 定义，不依赖 Runtime 链接。
- Runtime 单一生产单元为 `packages/runtime/observability/logging.cpp`。固定有限槽、静态公共格式、默认 DropOldest、短锁同步存储、分页游标及 `min(E,H)`；无线程、任意正文或回调注册接口。
- SafeLogger 只持有共享 State；每次虚调用前复制局部 owner，后续计数/校验不解引用外壳。跨方法 TLS 链按 backend 身份保护，含 create 初始 snapshot、同端口多 facade、A→B→A 与最大十六层。无 facade 锁内虚调用或最后 owner 销毁。
- 独立 facade 与后端计数不互相补写；固定错误为空 ErrorInfo，捕获不调用 what()。返回材料的 nothrow 移动/复制已作静态检查，测试禁用 NRVO 的 Debug 编译通过。合格接受前异常与非合格接受后抛严格分开；没有凭 throw 恢复未知接受事实。
- 同一五函数分别注入默认内存后端和独立 RejectNewest 测试后端；异常、畸形报告与回调控制仅在测试对象内。根 CMake、Host、tools/process 和旧 expected 未修改。本目录 CMake 消费 `OCK::Runtime` 与 `OCK::CoreContracts`，无额外生产 Logging 静态库。

## 实际运行

所有轮次位于 `evidence/bootstrap/D1.06`，均独立冻结相关源并保留 owned Job 命令、stdout/stderr 与退出。原失败没有覆盖：

| 目录 | 实际结果与含义 |
|---|---|
| logging-debug-568023d5dc21 | configure 0、build 1；缺失 `ock/runtime/logging.hpp` 的真实 API 编译 red，不能称行为断言失败 |
| logging-debug-328e19f4f9ac | build 1；测试把未声明 noexcept 的纯值 helper 带入 noexcept 表达式，修正测试声明 |
| logging-debug-d25eb8886ebd | 初始共同十项 Debug 通过；不代替后补子断言 |
| logging-debug-b93fb434e40c | 补断言后 8/10；错误预期把初次包装 stream=2 当作非法。合同允许任意非零流，Host 另限 stream=1；修正测试并增加真实水位矛盾拒绝，未收紧生产 API |
| logging-debug-7379ab83f0d3 | 隔离快照移除 flush 裁剪的单例负控制：编译成功，CTest 8/Failed，`flush_accepted_range` 的 `v` 断言实际捕获。只改该快照，生产未改；保留 Failed 原状态 |
| logging-debug-32c863fedb48 | 当前稳定输入最终 Debug：configure/build/list/CTest 全部 0；expected/discovered/executed 十项精确一致，JUnit 十项 Passed |

最终轮四命令全部 Exited、exit_code=0、active_after=0、未终止 owned Job；raw 摘要已读回核对。最终 JUnit SHA-256：`375fc194336eae5b2a85091221ea733f7d891086ed1b65b35d46773328a33839`。所有共同项子断言映射见 `tests/conformance/logging/cases.json`，成功 stdout 记录各子组完成标记。

## 自审重点与边界

已核对静态格式不读取 Redacted.value、只在公开类型范围内数值格式化、输入借用不保留、槽文本尾零；格式长度取保守编译期上界。固定容量在分配前校验，持有记录不扩容，接受序号耗尽拒绝且计数饱和不回绕。完整 `2^64` 序号循环没有实际运行，也没有为其新增生产可变状态开关。

已实际覆盖四方法分别同步销毁 facade 外壳和异常回程、最后外部 backend/facade owner 在回调中释放时在途 State 仍活、控制关闭失败保留 Closing、显式重试及关闭动作只发生一次；独立线程与 4×4 跨方法、十六层和 create 重入均有真实断言。

普通日志失败对业务事实的隔离在本消费者中验证为不替换调用者结果；真实 Host 与 Native 组合边界由主集成验证，本局部测试不冒充 Host/Native/安装消费。未运行全三配置，未宣称日志预热后的分配、Private/WorkingSet 或最终释放字节达标；这些由同批 footprint 消费者实测。可选 async/file 项因能力为 false 而 NA，共同五项以及 callbacks=false 时的寿命项不能跳过。后端正式 qualified 登记仍需主集成的真实共同结果与独立审核。

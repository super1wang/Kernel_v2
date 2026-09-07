# D1.05 分配驻留与回收补充复核

范围仅为 `allocation_probe.hpp/.cpp`、`allocation_cases.hpp` 与 private_dispatch 包装器的异常传输子进程控制。未改变公共分配策略或内核行为。

分配计量以本可执行程序替换的 C++ new/delete 配对为边界，使用 `_msize` / `_aligned_msize` 的 CRT 可用块大小。固定原子状态记录当前驻留字节/块数，线程局部窗口记录分配、释放与峰值，无动态索引分配。它不包括 CRT 堆元数据、DLL 私有分配器、自定义堆或无覆盖的后台活动。Debug CRT 次数与 C++ 通道不相加；Release CRT 字段保持 null。

实际验证：

- Debug：`pipeline-a5d3e6c67ff6` 四项 allocation 用例全部退出 0。
- Release：`pipeline-d45d2ff7b11b` 四项 allocation 用例全部退出 0。
- 每轮均保存冻结输入、工具链配置、编译与独占 Job 原始输出；四份本次修改源码 SHA 均与两份成功快照一致。
- 8 种 C++ new 形式证明分配/释放配对、峰值上升、驻留恢复；3 种直接 CRT 分配没有被混入 C++ 通道。40 次完整稳态调用维持零分配、零释放及驻留不变。
- other_costs 明列 4 次有限预热。释放外部 NativeEngine 外壳后，Bound 仍可调用；最终 Bound 释放内部 EngineState、Catalog 与观察表，再释放弱引用控制块。
- 最终 Bound 回收窗口：Debug 释放 3394 块、269041 字节；Release 释放 1532 块、223873 字节。每轮另释放两个弱引用控制块、48 字节。这里只报告替换 new 通道的实测可用块大小，不声称整个进程归零。
- 所有样本已核对字节守恒、块数守恒和峰值上下界；非法输入、撤权、生命周期变更治理用例仍实际拒绝。
- 初轮 `pipeline-891aa521507c` / `pipeline-2e632b786ff5` 的 other_costs 失败保留：错误假设 Bound 保留 NativeEngine 外壳。根据实际所有权修正测试，区分外壳与内部状态，未改内核掩盖失败。
- `wrapper-transport-ff1c9bc50710` 实测异常传输子进程在独占 Job 中退出 86，stderr 包含 `locked_result_noexcept_transport_terminated`。正式 private_dispatch 包装器已检查这一协议并保存命令 JSON。

以上为子任务实际结果，D1.05 包级状态及最终完整矩阵由主任务审核。

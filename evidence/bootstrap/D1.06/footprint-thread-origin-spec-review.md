# 线程来源诊断有限 SPEC 复核

actor_type: AI；结论：Approved，仅批准独立诊断实施，不批准线程归因完成、正式采样变化、预算或包级验收。

候选保持同消费者/安装 Runtime，由消费者主线程只捕获自己进程的 PSS_CAPTURE_THREADS，不取 context/VA clone/外部进程，随后逐项读取线程起点并映射模块/RVA，边界合理。锁定 SDK processsnapshot.h 的 PSS_THREAD_ENTRY 实际包含 ProcessId/ThreadId、CreateTime、Win32StartAddress、CaptureTime/Flags；PSS 返回值应按 ERROR_SUCCESS/原错误码判断，不能用陈旧 GetLastError 代替。官方依据：[PssCaptureSnapshot](https://learn.microsoft.com/en-us/windows/win32/api/processsnapshot/nf-processsnapshot-psscapturesnapshot)、[PSS_THREAD_ENTRY](https://learn.microsoft.com/en-us/windows/win32/api/processsnapshot/ns-processsnapshot-pss_thread_entry)。

实现应保持候选的固定 64 上限并确认枚举实际结束；达到容量且仍有记录即截断/未知，不能静默丢弃。快照和 walk marker 全路径释放，回执错误保留。主线程用本进程实际 ID 标识；held-thread 正控制需记录其真实 TID/创建时间并观察已知起点，不能仅用“总数加一”验证地址归属。模块范围/RVA 用实际已加载边界核验，保留路径/摘要和所有未解析项，不解引用起点或 context 指针。

这只是当前快照中线程入口的来源说明。系统模块起点（包括 ntdll）不能证明谁触发创建，更不能证明两个边界间没有短命线程。独立诊断本身的工作可能改变内存、时延和运行库状态，不能混入 occupancy/allocation/pilot；同构 baseline/Native 对照和原能力/释放断言继续有效。候选已明确未知不通过、不能放宽新增线程 0，符合已审 v2 方法与 A21 硬约束。本复核不声称方案必然能消除四线程的因果未知；若入口仍不足，继续 Unresolved。

未运行诊断或修改实现。精确输入：

- 候选：`9b37c2bd1b4e5ecbcc41fcd3e71663e71de85c155727db90cdd541c4c1e91ba7`
- 锁定 Windows SDK processsnapshot.h：`93c20675d17f18b1945a5b05d44fa9bfbcf091eeb27687fbccbcda072d5c32cc`

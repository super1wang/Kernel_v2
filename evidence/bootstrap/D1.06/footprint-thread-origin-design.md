# D1.06 线程来源有限诊断候选

目的：解释已观测 baseline/Native 各四线程，保留新增线程零硬约束。该诊断不替代逐轮线程集合，也不把相同数量单独当作归因。

独立诊断构建沿当前同消费者与安装 Runtime，仅在编译期开关下由消费者主线程于能力完成后调用 Windows SDK 公开 PssCaptureSnapshot(GetCurrentProcess(), PSS_CAPTURE_THREADS, 0)。不捕获上下文、VA clone、句柄或外部进程，不启动观察线程。固定最大64条记录，PssWalkSnapshot读取实际PID/TID、Win32StartAddress、创建时间、flags；使用本进程 GetModuleHandleExW(FROM_ADDRESS|UNCHANGED_REFCOUNT)和GetModuleInformation/文件路径将起点绑定模块基址/RVA，保留完整错误及未解析地址。全部快照/marker按公开API释放，记录返回码。SDK processsnapshot.h声明作为结构布局来源。

两个诊断消费者仍由既有execute拥有Job；不改process.py、不按名称附着外部进程。该额外工作只属于独立诊断模式，其内存和时延不能成为occupancy/allocation/pilot数据；原消费能力与释放断言仍真实执行。固定对照为同构baseline与Native各一次，以及已存在held-thread控制的一条新增线程（如复用需明确接口与独立来源），不反复调整线程数量直到相等。

报告分别列主线程、起点所属实际模块/摘要/RVA及未知项。模块来源只证明入口归属，不能推断全部调用因果；若无法结合无线程创建的Host/Logging/Native结构证据和每轮集合得出可靠归因，继续Unresolved，不因此放宽硬约束。未知/截断/查询失败均不能通过。此候选待有限审核后实施，不改已批准测量方法或生产接口。

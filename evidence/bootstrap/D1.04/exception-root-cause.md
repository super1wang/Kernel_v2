# D1.04 异常切片挂起的根因定位

actor_type：AI，主集成。仅根因诊断，尚未声明实现修复或整包Passed。

初次原生异常切片 `implementation-89e584485e5d` 出现挂起，不能凭此认定为仲裁死锁。后续实现者细分输出到allocation-point 0、env-ready、issued，update未返回。

主任务三个独立源码快照的实际观察：

1. `exception-diagnostic-5ea0cae54045`：配置/编译退出0，异常主体20秒owned Job Timeout；stdout停在allocation-point 0。
2. `exception-diagnostic-5cf74b482e71`：仅快照内添加不依赖C++动态分配的fputs/fflush边界日志；配置/编译退出0，异常主体20秒Timeout。stderr为update-before-vector、new-throw，没有update-before-reserve或update-caught，故停点是局部空vector构造。
3. `exception-diagnostic-738efa531cea`：同类快照额外安装诊断terminate observer，记录后调用_Exit(99)。配置/编译退出0，异常主体实际Exited99，stderr为update-before-vector、new-throw、terminate-observed；active_after为0。这是终止事件的实际证明，不是将超时假标成功。

读取锁定安装的 `F:/VS2022/VS2022/VC/Tools/MSVC/14.44.35207/include/vector`：670附近默认vector构造的noexcept由allocator默认构造决定，内部调用_Alloc_proxy；`include/xmemory` 的_Alloc_proxy在Debug模式为容器代理分配内存。故障注入在默认空vector构造时抛出bad_alloc，越过noexcept边界触发terminate，外层catch不会执行。size构造重载不具有该默认构造noexcept边界，可作为后续候选修复；本报告不声称已验证所有构造路径。

诊断修改只位于独立source快照，原policy/test字节另存original-policy.cpp、original-tests.cpp，实际编译字节及SHA登记于source.json。主测试与生产源码没有安装该诊断terminate observer。每轮保留原始stdout/stderr、命令及全构建ZIP；未杀其他会话的进程，未改全局调试或CRT设置。

后续必须先修可捕获异常的内部构造路径，再真实重跑分配失败、原子回滚及成功正控制；API外由调用者自身构造的输入不被本实现catch覆盖。历史Timeout与Exited99均保持失败/诊断事实，不能改写为Passed。

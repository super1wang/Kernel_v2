# D1.06 首个真实 Native/baseline 单对自审

这是实施者自审，不是独立 AI 批准。首个单对只证明当前消费者可编译、安装消费及能力断言连通，保留原始占用；不充当 pilot、正式 ABBA、预算或 G1 结论。

## 来源

独立运行目录为 `footprint-native-pair-03d6fbd84f62`。24 项实际输入当前逐字相同，集合摘要 `040176792287f2cfd2534fa2704ca60b5de629bb84d37f7867c641d23da039a6`（排序 path/TAB/SHA/LF）。逐文件及命令、安装清单、工程、原始结果摘要在 `footprint-native-pair-self-review.json`，该索引 SHA256 `2a83e4adc38274469f1fc7e63a16045a1c3a39f56d5af395c18f5649df206ef6`。

真实 SDK 来自 `host-install-5fb4d2956f/result.json` 所绑定安装，生产者为 `sdk-stage-da08b48728`。驱动校验全部安装文件字节/SHA，然后复制到本轮独立 prefix 并再次逐项校验；只使用该 prefix 中公开头及实际静态 `OCK::Runtime`。没有重编生产库或修改 Host/Logging、冻结观察器三文件、旧入口 run.py、示例目录。

新增 `consumer/pair_main.cpp`、Native fixture/值、baseline fixture、本目录 CMake、pair_develop.py、pair_record.py 及其局部测试。原有 channel.hpp 按原字节复用。采样增量仅单独候选 `footprint-sampling-amendment-candidate.md`，SHA `242dc6ca8073cfaef67b1e7d2b21a2a1c982d700879c77a2acc62c7b86bf60c2`，尚未实施。

## 能力与寿命

两 EXE 编译同一主文件，使用同样十一阶段、固定记录/事件、父 ACK、四次预热、四十次调用及固定停留。baseline 的唯一工作是有结果检查的本地计算，无 SDK include/Runtime 链接，不伪造 Host、Session 或管理 owner；其阶段 owner 掩码及销毁哨兵均为 0。

Native 通过公共 API 创建真实 Host/模块/服务，Ready 前 open 拒绝且 handler 未进入。模块 start 的有效借用内接受一条固定 Diagnostic 并立即 flush，保存的只是位置与成功值；随后 Ready 页面验证同 Host/stream 的全部五条记录，第三条是已 flush 记录，实际 E=0、gap=false。Read/Compute 各验证一次，非法参数拒绝且 handler 不增；四次 warmup 和四十次调用都走真实 HostBound，交替 Read/Compute，逐个结果验证和析构，总 handler=46。

普通调用后页面水位仍为 5；实际生命周期日志保持默认启用。shutdown 成功、模块停止一次、旧 Bound 拒绝且 handler 仍为 46。ShutdownComplete 时仍保留真实 Bound/Session/Host/外部装配；依次释放后阶段掩码为 15→14→12→0。最后 weak Lifecycle/Reader/Target 全部 expired，生命周期析构哨兵由 0 变 1。没有解引用释放后的借用 logger，也没有把退出当作 shutdown。

baseline 的释放检查只是本地状态顺序，stdout 的 last_owner_release 表示 baseline 自有状态已放弃，不代表它拥有内核对象。Native 的同名字段由上述真实 weak/析构断言支持。

## 验证与原始事实

新增记录校验器先保存 `footprint-pair-red-152c22cde371` 的缺失实现 import red；这是局部校验器 red，不是 Native 业务失败。校验器随后验证缺项、False 能力及错误 warmup/调用次数不能通过。真实单对中该项、配置、编译、两次 imports 和两个消费者均 owned Exited 0、active_after 0、未终止 Job。

实际编译为 x64/C++20、Debug /MDd、/Od、/RTC1，未使用 ASan/GL/LTCG，零编译警告/错误。生成工程和实际 build stdout 中 baseline 无 Runtime/SDK include；Native 仅复制 prefix 的 Runtime.lib、bcrypt 及平台默认库，没引入 Data/State/SQLite/Asio。两个 dumpbin 原始导入报告保留。

| 原始事实 | baseline | Native |
|---|---:|---:|
| EXE bytes | 69,120 | 2,575,360 |
| 内存/线程样本数 | 61 | 66 |
| 原始 missed intervals | 1,160 | 1,048 |
| Ready 实际线程数 | 4 | 4 |
| Ready PrivateUsage | 675,840 | 909,312 |
| OwnersReleased PrivateUsage | 675,840 | 843,776 |

各阶段原始 PrivateUsage、线程 ID、QPC、Ready 高水位及完整加载模块路径/SHA 位于 commands.json/pair.json。两个 Job 的实际总进程均为 2，如实保存，未把它推断为 worker。Native 多加载 MSVCP140D、bcrypt、bcryptprimitives；前者是本轮 Debug CRT 差异，后两者是 OS 环境模块，不能直接合成 Release 发布占用。

## 未完成边界

高频观测仍执行旧方法，缺样明显；线程仅说明本次实际观察的数目相同，未归因其余三个线程，不宣称已完成线程硬约束或实时覆盖。释放后 PrivateUsage 没归零不等于泄漏，弱 owner 事实与 OS/CRT 占用分别记录。当前计数模式 disabled、分配 NotMeasured，40 次调用不能被写成零分配 Passed；同源码的 Release 轻量时延和分配探针尚未接入。

这是顺序 A/B 单对而非 ABBA，未作分位/预算判断，也未自动给测量值加余量。retained_management 专项、线程归因、方法增量审核、规定 pilot、预算决定及预算后新样本仍留在 D1.06 主线；不扩展其他工作包，本包提交后暂停。

# P0 POST_BUILD 独立实测与原 C1 撤回

AI 增量结论：原 `p0-dev-review.md` 的 C1 阻断结论**撤回**，不改写原报告。共享 test_support.hpp 的 SPEC 漏测结论仍成立，当前下游映射修复已静态核对。最终 dev 工具 CODE 结论待根去除强制清理策略并完成实际构建身份小增量后，另以最终 SHA 复核。

原 C1 推断认为仅改 discover.py 会在 dev-fast 的 configure/build 后保留旧注册。真实独立小 CXX 工程不支持这个推断：使用 LockedMSVC、VS 2022 v143 14.44.35207、SDK 10.0.26100.0，首次生成 old-registration，单改发现脚本后重新 configure + 普通 build 得到 new-registration，所有四个命令 exit 0 且独占 Job 自然排空。

不是改写 C++ 源码造成的混杂：小工程 main.cpp/CMakeLists 只在最初写入一次；CMake 中唯一 file(WRITE) 产生未参与 target 的 identity.txt；c892 的 main.obj 最后修改 02:41:40 UTC，早于第二次 configure 的 02:41:41 UTC，而注册已在第二次 build 更新。日志显示 CMake Custom Rule 执行，无 main.cpp 编译行。精确 SHA、时间及原始命令见 [混杂排除材料](p0-dev-rereview-20260908-d936/c1-retraction-observations.json)。此结论只针对已实测的锁定生成器及每轮重新 configure 的流程，未泛化其他生成器。

全部历史轮保留：b721 因原进程环境 PATH/Path 重复键而配置失败，属于环境失败；c892 仅以 subprocess 的 env=dict(os.environ) 规范化测量进程环境，未改全局环境，实测反驳旧注册假设，故其旧值预期断言失败；d936 经授权提权运行，四个普通配置/构建命令再次得到相同结果。

另 d936 实际用 verify.build_command 生成含 --clean-first 的命令，确实重编译并写出第三版 clean-registration，但根 exit 0 后出现 DescendantsAlive，180 秒后只清理自身 Job，最终 active_after=0、terminated_owned_job=true；因此不能称该轮通过。未推断残余进程具体来源。源码七项摘要在结束时仍稳定；见 [结束材料](p0-dev-rereview-20260908-d936/control-completion.json)和同目录 checks.json。没有改动共享构建树、生产源码或历史证据。

不应为这项未被实测证实的假设增加强制 clean-first 成本；保留现有每次成功 configure/build 后再 CTest 的最小机制。

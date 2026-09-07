# 内核验证的 MSBuild 用户属性目录

`LockedMSVC.cmake` 将本项目生成工程的 `UserRootDir` 指向此目录。这里不放置 `Microsoft.Cpp.*.user.props`，使编译器探测、内部 ABI 探测和消费者工程不导入机器用户属性。仓库锁定依赖仍通过 CMake 显式提供。

这只作用于采用该工具链的工程，不修改用户全局 MSBuild、vcpkg 或其他工程。它不宣称屏蔽所有环境变量或系统扩展。

# D1.05 Native 包装器与独立消费者实施复核

本记录只报告子任务实际验证，不代替 D1.05 包级自动验收。正式矩阵仍由主任务运行。

## 修改范围

- `examples/native_service/main.cpp`、`value.hpp`、`README.md`：删除测试夹具依赖，自带固定可信认证、时钟、空观察源及有限整数合同。真实注册并调用 Compute/Read；非法输入和不可执行 StateEdit 必须明确拒绝，业务进入计数保持不变。成功输出四项 JSON 检查。
- `tests/contract/native/verify_children.py`：固定八个子进程包装器；命令全部进入独占 Windows Job，记录原始流、SHA-256、退出码与清理结果。两组编译控制核对目标源文件和唯一预期 MSVC 诊断；四组分配报告包含实测通道与盲区，注入分配必须失败；还包括真实示例进程和安装边界控制。

## 实际证据

- `wrapper-compile-10af6f2accf7`：初次 configure 失败，MSBuild 环境 PATH/Path 重复；没有声称 C++ 失败或通过。
- `wrapper-compile-d77d56d17463`：实验进程内 PATH 规范化后两组编译控制通过，但示例 ProviderBinding 形参不匹配。保留失败。该实验未保留在产品包装器中，没有改动全局环境。
- `wrapper-compile-34ebf944f4be`：修正为明确拒绝的 AtomicProviderPort 后，在授权宿主环境重新独立编译，2 个正例、8 个反例及示例对象实际通过。
- `wrapper-boundary-474c66fef6b8`：最终两个实际包装器通过，共 2 个正例、9 个反例（追加 bound.force_inline 方法反例）。分别运行真实 Native 主体并产生规定名称的 positive.obj、配置、原始日志和结构记录。
- `wrapper-allocation-a5fbf161636d`：四个实际分配包装器通过；注入单次 new 实测 cpp>0、verified=false、exit=1。独占 Job 归属、进程树归零及原始日志 SHA/长度已复核。

包装器试运行使用父任务已构建的 `build/d1.05-stage-5256e562f4da/Debug/native_stage.exe`；不能替代后续源码的完整矩阵。消费者在本子任务仅已实际编译对象，完整链接、运行及安装边界留待主任务根工程构建验证，未虚构 Passed。

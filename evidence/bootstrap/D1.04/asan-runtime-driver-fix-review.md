# D1.04 ASan 开发列表运行库修复独立 AI 复核

审核者review_tools，独立AI。结论：限定驱动修复 Approved；ASan完整35项及wrapper须等待独立新目录运行，不将配对控制冒充完整通过。

真实控制 asan-runtime-control-07a51e79705a 对同一binary和--list参数运行：直接调用Crashed/3221225781；cmake -E env仅前置已注册runtime PATH后Exited/0，实际35个名字逐项等于固定expected。两条命令均先归入WindowsJobObject、active_after=0。binary、已归档registry和两份ASan DLL已重算SHA并与inputs一致；registry所有PATH修改条目解析后指向同一个目录。该记录支持缺失运行库搜索路径的诊断，不是C++行为失败。

before→after diff仅新增os/re及替换额外native-list步骤：从本次build、实际配置生成的policy-tests文件读取已注册PATH目录，缺失即拒绝，使用cmake -E env给本次子进程临时前置目录；执行仍经过原run/ownedJob和原始流。未修改全局环境、正式源文件或清单；configure/build/CTest与source稳定、唯一目录、ZIP核验机制不变。没有通过忽略退出状态或跳过list绕过检查。

修复适用于debug/release/asan相同工具步骤；前两轮已实际Passed的源码与测试未改，无需因新增该列表环境重复它们的编译/CTest。三轮最终报告应分别绑定实际使用的开发driver字节，不能声称三轮用了同一driver。首轮失败integration-asan-bc3cce35dc7a保持原样。

- evidence/bootstrap/D1.04/asan-runtime-driver-fix/before.py：`e3a9aaf78855eb1cb197d618d9f864a2a4324fd4e442923025df869f954388d6`
- evidence/bootstrap/D1.04/asan-runtime-driver-fix/after.py：`c4a0c23a9232fc65f748abc93a44addaf0afa16ec2ebf8d97a306c4a9ddbb337`
- evidence/bootstrap/D1.04/asan-runtime-control-07a51e79705a/commands.json：`b485607cc34f48038abe17f31828d243e556a3062d6edf0a3f9f9ca6ca0b7492`
- evidence/bootstrap/D1.04/asan-runtime-control-07a51e79705a/inputs.json：`776247807eabf0891c913311ab722fda36323a31009a4a20e6e1ad9593da54ec`
- evidence/bootstrap/D1.04/asan-runtime-control-07a51e79705a/with-runtime-stdout.log：`468de333e14f18c2d126e10a14df5bbd89f5226b780a6cc647c1edfdbc0f45a7`
- evidence/bootstrap/D1.04/asan-runtime-control-07a51e79705a/without-runtime-stdout.log：`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`

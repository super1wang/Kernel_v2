# 独立反例实际验证

AI review_contracts；HEAD 基线 8cd018c737b7418802b34dd4740c8897ef9825eb。本轮追加验证先前静态报告两项 P2，不覆盖历史报告。

使用 LockedMSVC CMake 工具链、原 Foundation/expected include 闭包及 Windows 独占 Job。配置、控制消费者编译和合法运行均实际 Exited 0。完整命令、工具路径、进程退出与排空记录见 commands.json；源码散列和 probe 原文件一起归档。

| 反例 | 实际结果 |
|---|---|
| EffectContext 携带伪原始许可，接收夹具成员换回真许可 | Exited 17；accepted=1 consumed=1，期望拒绝失败 |
| TransitionView 同样替换许可 | Exited 17；accepted=1 consumed=1，期望拒绝失败 |
| 未认证主体直接写 Authority::issued | Exited 19；accepted=1 |
| 未发布证明直接写 Publication::issued | Exited 19；accepted=1 |
| 断言两种 issuance 不公开 | Exited 1；精确 C2338，两个 REVIEW_*_ISSUANCE_MUST_BE_PRIVATE 均失败 |

控制消费者验证合法两种 shape 通过，并证明实际伪许可直接交给真实 PermitIssuer::consume 会拒绝，因此上述上下文错误通过来自接收夹具换用真许可，不是许可校验本身始终成功。注入反例分别在插入前确认 authenticate/validate 拒绝，插入后才认可。

构建结束后再次核对三个受审文件，散列与 source-inputs.json 一致：authority_factories.hpp 1784a0e6fb99663770e85f7b26e369a5f21c99622393b37a34aaa5add60b9bd5；factories.hpp 63c746338c0b76afac74ebed6487d3fda6d31d9f81c42f0360a57302bf4bbee4；test_support.hpp 974e58cf8f62ceb59a50fe0a02ae62ea537da8652f8178633951c6cb0164beb0。

这是真实 red 证据，不是包级失败/通过决策。未修改受审产品与工厂，未减少零世代限制。修复后应新增 green 轮次，不能覆盖本目录。

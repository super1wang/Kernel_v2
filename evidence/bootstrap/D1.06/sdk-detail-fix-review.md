# SDK 安装 detail 守卫增量自审

原审核 P2 可复现：安装 detail 来源以 public=False 检查时，未登记的同组件 detail 边被放行。两条反例分别为 detail/host.hpp→detail/private_bridge.hpp、detail/invocation.hpp→detail/unknown.hpp。sdk-detail-red-51f1abd3f9 保留真实失败命令及原始流。

修复仅在来源位于该组件安装 include_root 时，要求 detail 边出现在精确登记表；非安装生产内部来源沿用原同组件规则。三条已登记边及普通生产内部边有正控制。sdk-detail-green-ed30fb0b71 实际运行49项通过，包含既有架构和包装守卫；未重跑无关安装/构建矩阵。

增量字节、命令、原始流及zip逐成员SHA见 sdk-detail-fix-index.json。原 sdk-source-identity.json、sdk-raw-text.zip 和原失败证据保持不变。本自审不是独立 CODE Approved，也不是 D1.06 包级 Passed。

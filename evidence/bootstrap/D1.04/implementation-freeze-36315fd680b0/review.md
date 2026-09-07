# D1.04 实施者冻结候选

状态：实现/原生合同已完成本地验证，等待独立完整规格、代码、工具审核及根工程正式矩阵；不写整包Passed。

实际来源：`implementation-8e072f1ca34a`，Debug build、实际--list及35个固定单名执行，共37条命令全部Exited/0、active_after=0。本文件生成时再次逐项核对该轮source.json与工作区SHA全部一致。源码/测试已clang-format且LF；实现只有policy.hpp/policy.cpp，未拆其他cpp。

范围：内部Store/Session/tuple权限、真实原始身份一次性许可、固定source寿命与不可重绑定合同、拥有型投影/分页/Watch、真实预留sink字节Start仲裁、关闭/撤权/退订队列摘除和锁外释放、活跃owner预算及饱和发行。内部库不安装，未实现公开SDK Runtime、生产Host索引、生产认证/摘要/传输或D2协议。四个独立cases头由父任务实现并接入固定dispatcher。

## 主要真实反例和修复链

|问题|修复前证据|修复后证据|
|---|---|---|
|重复restrict绕Store总预算、负epoch期限算术|implementation-636ddfd23169|implementation-93640fc9eb51|
|无权目标可解析；跨来源候选拼接|implementation-240bdc694b43 / implementation-42d1ef4b2089|implementation-dbaeac2b6c14|
|动作重复文本/permit保有文本少计|implementation-d93490630a16|implementation-5fb646b0f98d|
|活跃Grant与Target owner未占预算|implementation-928e811455e5 / implementation-3a117f682685|implementation-143f7ea6042d|
|MSVC Debug noexcept默认vector分配触发终止|父任务exception-diagnostic-738efa531cea；早期Timeout保留|implementation-143f7ea6042d|
|start源bad_alloc未转为Result|implementation-537775f5d057|implementation-5b1db71a2302|
|Watch跨主体最终释放不恢复槽、summary.host失约未关闭|implementation-72a76ea82306|implementation-2a806a6748ca对应主体；最终35完整通过|
|超预算管理输入先复制大容器|implementation-8988f09b6f5e|implementation-0107ced34983|
|负枚举Kind/Topic被接受|implementation-a61f10fe85f0|implementation-8e072f1ca34a|

父任务还独立保存并复核：完整Digest材料、退订/会话close队列清理、Unknown清理同会话其他队列、Sink双票据容量、同精确Key合同稳定与Auth移动返回拥有材料的red/green。此处不重复替代其独立报告。

## 保留的控制失败

缺头/缺定义链接失败仅作bootstrap接口缺实现red，不算行为red。`implementation-3f227958c7a2`的终态文本预算控制原预期过严，后改为旧permit保有时新issue失败而新action仍可创建。`implementation-705e431945c4`、`2693c466f43b`、`b545c995cf48`的预算Timeout定位为测试自身使用vector元素引用扩容assign；先复制MemberRequest后恢复，未改运行时锁。大块MSVC对齐导致Auth data地址与delete地址不同的观察通道失败由父任务改小块合法控制，未读私有布局。相关历史不覆盖。

自动审批曾拒绝枚举所有owner的目标可见性补丁，原命令未执行，详见implementation-target-approval-rejection/rejection.txt。最终采用独立Approved修订4同四维候选规则；没有绕过被拒行为。

## 后续

冻结后不再主动修改源码。父任务负责根工程5个wrapper、正式Debug/Release重复矩阵、中文进度、提交/推送和AI最终验收。本实施者未提交或推送，未改根CMake/固定expected/已批准合同。

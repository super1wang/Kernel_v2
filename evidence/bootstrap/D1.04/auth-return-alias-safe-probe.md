# Auth返回拥有材料的安全限定探针

独立AI补充：review_contracts。撤回authority-tests-review.md中“open返回后直接修改已move返回payload元素”的无条件建议：正确实现复制ceiling后会析构临时返回DTO，原元素可能已经释放，解引用会造成UAF。不能使用该方案取得所谓green。

建议采用**只观察释放，不解引用旧地址**的实现回归探针，复用测试现有global new/delete拦截。认证端固定可信凭据，payload.ceiling.rules设置为一个非空规则，返回前记录其vector分配的地址数值，然后将payload显式move进Result。观察器用thread_local预存整数和bool，不分配、不调用Policy。

```cpp
struct ReleaseProbe {
  std::uintptr_t allocation = 0;
  bool active = false;
  bool released = false;
};
thread_local ReleaseProbe auth_return_probe;

// 在现有所有delete重载最终进入free之前调用，不能新增第二套重复释放。
void observe_free(void* p) noexcept {
  if (auth_return_probe.active &&
      reinterpret_cast<std::uintptr_t>(p) == auth_return_probe.allocation)
    auth_return_probe.released = true;
}

// 固定认证器的authenticate内部，凭据检查成功之后：
auth_return_probe = {
  reinterpret_cast<std::uintptr_t>(payload.ceiling.rules.data()), true, false};
return std::move(payload);
```

测试先构造全部装配材料，再调用open；无论返回或抛异常都通过栈上guard关闭active。open成功后只检查released及真实session.prepare正控制，不读取payload元素或保存的地址。正确的当前实现复制返回ceiling，然后局部authenticated销毁，原vector缓冲释放，released为true；直接move接管返回buffer的回归变体仍随Session持有，released为false。失败时不尝试写旧元素，不因地址是否释放分支去解引用。

使用单元素vector，核对锁定MSVC下其data对应实际delete收到的分配地址；应先运行独立正控制：一个同类型单元素vector离开scope时观察器确实收到释放事件。该控制失败应记探针不适用，不能误报产品失败。delete观察必须发生在free之前；地址只作为uintptr_t比较，避免在释放后运算悬空指针。大块对齐分配或自定义allocator若导致base不同，应观察实际分配身份，不能猜指针偏移。

此探针验证当前实现明确采用的“复制返回DTO、释放临时buffer”路线，能安全区分旧move接管回归。它不是要求所有合法API实现都必须在这个时刻释放原DTO：未来若合法保留额外返回DTO但授权仍用独立快照，应按实际实现重新设计测试，不能以本探针否定公共合同允许的其他路线。

scope输入的控制可直接保存scope.rules[0].permissions成员引用并在open之后修改，因为调用者scope仍持有源元素，正确实现不会销毁调用者容器；错误move接管时缓冲由活跃Session持有，测试结束前也不得销毁该Session。认证返回临时DTO的寿命与调用者输入不同，不能照搬后者的引用使用方式。

本文件仅给安全探针方案，未执行修改、没有red/green或关闭结论。后续必须保存实际运行证据，并确认观察器自身的正控制。

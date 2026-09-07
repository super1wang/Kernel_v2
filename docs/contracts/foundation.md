# D1.01 Foundation 最小原语合同

依据：[架构 v3.3 A03.1](../01_Architecture_v3.3.md#a03)、[执行计划 D1.01](../02_Execution_Plan_v3.3.md#d101) 与 [本包实施计划](../plans/D1.01.md)。公开入口为 `ock/foundation/foundation.hpp`，命名空间为 `ock::foundation`，链接 `OCK::Foundation`。本文件描述合同，不代表工作包已验收。

## 后端与工具链

`Result<T, E = Error>` 直接别名 `tl::expected<T, E>`；`Unexpected<E>` 与 `make_unexpected` 来自同一后端，不增加另一层状态容器。锁定 tl::expected 1.1.0、C++20、启用 C++ 异常；公开头拒绝其他后端版本、关闭后端异常或 MSVC 未启用 `_CPPUNWIND` 的模式。MSVC须启用 `/Zc:__cplusplus`；由公开目标传递 C++20、`/EHsc`、`/utf-8` 等固定选项。Debug与Release消费者使用对应CRT配置，不承诺跨工具链二进制ABI兼容。

普通业务失败使用 Result。错误 `.value()` 保留 `tl::bad_expected_access<E>`；用户类型异常按后端与类型本身的保证传播。详情创建的 `std::bad_alloc` 不捕获或转换成业务失败。`invariant(false)` 调用 `fail_fast()` 并终止进程，禁止继续执行或制造可重试错误。

## 错误域、错误码与拥有型详情

`ErrorDomain` 在常量求值时复制1–63字节可打印ASCII名称，字段不公开修改。使用 `inline constexpr ErrorDomain domain{"component"};` 声明进程寿命域；不得卸载域所在代码。`ErrorCode::make<domain>(numeric_code)` 是唯一自定义域构造入口。引用非类型模板参数排除栈域和临时域，任意域指针构造不公开。域相等按同一个域对象的身份判断；相同名称的两个域对象仍属于不同域。

`ErrorCode` 保存域身份与32位数值；构造、复制、比较不分配。默认码为Foundation域的0值，不代替Result的成功/失败状态。`Error` 保存码与可空 `shared_ptr<const ErrorInfo>`，纯码错误不分配。ErrorInfo自身禁止复制、移动和赋值，合法创建入口仅返回只读共享拥有权，避免复制出可变别名后修改已发布详情。复制带详情错误共享拥有权；返回的详情只读，调用者读取视图必须保证所属Error或共享指针存活。

`Error::with_details(code, text)` 返回 `Result<Error>`，`ErrorInfo::create(text)` 返回拥有型只读详情。两者先检查4096 UTF-8字节的上限与编码合法性，再分配。超限返回 `detail_too_large`，无效编码返回 `invalid_utf8`，均不预先分配超额文本、不截断字符。空文本是合法显示文本，规范为“无详情”：`ErrorInfo::create(空串)` 返回成功但空的shared_ptr，`Error::with_details(code, 空串)` 返回无info的纯码Error，均不分配。调用方须区分Result成功与其中shared_ptr非空；也可直接构造 `Error{code}`。更严格的调用方预算必须在调用前检查。详情输入在调用返回后可销毁或修改；详情不引用Payload，不借用原始文本。

UTF-8检查只涵盖编码合法性：拒绝截断序列、错误续字节、overlong、surrogate以及超过U+10FFFF的编码。不执行Unicode归一化、字符分类或语言规则。

## 名称与稳定身份

`Name::parse(string_view)` 接受1–96字节、每字节均为0x21–0x7e的名称；内部拥有96字节固定缓冲和长度。空名称、空格、NUL、控制字节、DEL与中文UTF-8字节均拒绝。比较按字节且大小写敏感，标点遵循同一范围规则；不作转小写或Unicode归一化。`view()`借用Name对象自身的缓冲。

`Tagged128<Tag>` 恰为16字节，公开16字节值数组，无跨Tag隐式转换或比较。`TaskId`、`ObjectId`、`RegistryId`、`RegistryGeneration`分别采用不同Tag。`parse_id<Tag>`只接受32个小写hex字符；`encode_id`返回32字节数组，无末尾NUL。全零和全1的稳定ID编码均可往返。解析与值比较不授权任何操作；本包没有生产随机ID生成器。

## 句柄和epoch

`BoundHandle<Tag>` 包含RegistryId、RegistryGeneration及32位slot，缺省为全零身份/世代与UINT32_MAX槽位。`resolve_slot`严格先验证非零registry身份，再验证非零generation，最后验证slot不为UINT32_MAX且小于capacity。分别返回 `wrong_registry`、`stale_generation`、`invalid_slot`，失败不索引调用者容器。调用者仅可在成功后索引，同时须由后续生命周期同步保证核验和访问之间registry不被替换；此原语不提供线程锁或实际registry管理器。

不同registry即使generation相同也拒绝；旧世代、空句柄与越界槽位拒绝。创建方必须保证RegistryId与RegistryGeneration的适当唯一性，不能以复用的局部递增数冒充跨registry唯一身份。

`RuntimeEpoch` 是独立的uint64强类型，不能转换为稳定业务ID，也不参与稳定ID外部编码。`advance()` 在UINT64_MAX处返回溢出错误并保留原值，不回绕。

## 受检算术与计数

`UnsignedInteger`限于无符号整数并排除bool。`checked_add/sub/mul` 在计算前检查边界；`checked_narrow<To>` 检查数值可由目标类型表示，不静默截断。失败只返回错误码，无分配，不执行已知溢出计算。

`CheckedCount<T>::create(initial, limit)`拒绝超过预算的初值，默认预算为T的最大值。`try_add`先验证类型上限和预算再更新，`try_sub`先验证不下溢再更新；所有失败保持原值。名称中的“更新”不表示CPU原子操作，同一计数对象跨线程读写需要调用方同步。独立值对象可独立使用，拥有型详情发布后不可变。

## 验证边界

本目录的原生runner在代码中注册23项T03/T04/T05用例，`--list`直接输出真实注册集合；构建后按配置生成CTest条目，不读取expected manifest；公共wrapper按CTest的显式配置选择对应注册文件，未指定配置或尚未构建该配置时明确失败。发现、CTest和原生子进程仅将CMake实际MSVC编译器目录加到PATH前面以找到ASan运行库，其余环境保留。Release使用显式CHECK而非会被NDEBUG关闭的assert。分配窗口独立于诊断输出，普通new、new[]和aligned new各有一次真实控制分配；纯码与值操作窗口必须为零。布局输出记录本次工具链的sizeof/alignof，只对16字节Tagged128等实际合同作断言，不把全部观察值冻结成ABI。

静态域负例先用相同工具链编译合法消费者，再分别实际编译栈域、临时域、任意指针构造并要求MSVC编译诊断。fail-fast在独立受控Job运行，先运行会实际返回的no-op控制进程并确认拒绝；真实不变量路径要求前置标记、MSVC abort退出码3、无意外返回后置标记及无遗留子进程。若不变量意外返回则打印后置标记并退出0，不能以普通非零退出或DLL缺失冒充通过。每轮子进程的stdout/stderr、命令、退出与摘要保存在独立 `child-runs/<uuid>` 中。T24公开头/搬迁安装/缺依赖/后端模式消费者由主集成验证，不能以原生测试替代。

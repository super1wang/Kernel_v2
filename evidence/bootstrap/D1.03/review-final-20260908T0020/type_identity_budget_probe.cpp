// 独立审查反例；未在本审查进程中编译，复用真实测试控制构造。
#define main registration_original_main
#include "tests/contract/registration/registration_tests.cpp"
#undef main
struct LongContractValue { int value; };
namespace ock::contracts {
template<> struct TypeContract<LongContractValue> {
  static TypeIdentity identity() {
    const std::string version = std::string(1024, '1') + ".0.0";
    return {name("long.contract"), *OperationVersion::parse(version, 2048), {}};
  }
  static Result<void> validate(const LongContractValue&) { return {}; }
};
}
Result<int> long_contract_handler(const LongContractValue& input, WorkContext&) {
  return input.value;
}
int main() {
  auto b = batch({128, 4096, 16384, 512, 128});
  auto m = operation_module();
  m.register_operations = [](Registrar& r) {
    auto d = input(AtomicMode::PureCompute);
    d.docs.clear();
    (void)r.compute(long_contract_handler, d, options());
  };
  CHECK(b->add(m));
  auto c = b->publish();
  std::cout << "Expected long TypeIdentity to exceed 512-byte batch budget; published=" << bool(c) << '\n';
  if (c) {
    const auto definition = *(*c)->describe(0);
    std::cout << "Stored argument contract version bytes=" << definition->args_contract().version.text().size() << '\n';
    return 17;
  }
  return 0;
}

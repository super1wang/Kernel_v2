// D1.05 验证消费者：在独立进程中实际创建目录、认证会话与 Native 调用。
#include "tests/contract/native/fixtures.hpp"
#include <iostream>

int main() {
  try {
    native_test::Env environment;
    auto bound = environment.bind();
    if (!bound)
      return 2;
    auto reply = bound->invoke(5, environment.options());
    if (native_test::result(reply) != 7)
      return 3;
    std::cout << "native-service: 7\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

#include <ock/foundation/sdk_version.hpp>
#include <iostream>
static_assert(ock::sdk::major_version() == 0);
static_assert(ock::sdk::runtime_available);
static_assert(ock::sdk::version == "0.1.0-dev.2");
int main() { std::cout << ock::sdk::version << std::endl; return 0; }

#include "probe_common.hpp"
#include <toml.hpp>
#include <cstdint>
int main() {
  const auto value = toml::parse_str("limit = 9223372036854775807\nname = '配置'\n");
  PROBE_CHECK(toml::find<std::int64_t>(value, "limit") == INT64_MAX);
  PROBE_CHECK(toml::find<std::string>(value, "name") == "配置");
  bool bad = false; try { (void)toml::parse_str("limit = 9223372036854775808\n"); } catch (const toml::syntax_error&) { bad = true; }
  PROBE_CHECK(bad); std::cout << "toml11 exact integer and overflow rejection smoke\n";
}

#include "probe_common.hpp"
#include <CLI/CLI.hpp>
int main() {
  CLI::App app{"dependency probe"}; int number = 0; std::string name;
  app.add_option("--number", number)->required(); app.add_option("--name", name);
  const char* args[]{"probe", "--number", "7", "--name", "中文 空格"}; app.parse(5, args);
  PROBE_CHECK(number == 7 && name == "中文 空格");
  CLI::App bad{"negative"}; const char* unknown[]{"probe", "--unknown"}; bool rejected=false;
  try { bad.parse(2, unknown); } catch (const CLI::ParseError& e) { rejected = e.get_exit_code() != 0; }
  PROBE_CHECK(rejected); std::cout << "CLI11 UTF-8/argument/error-code smoke\n";
}

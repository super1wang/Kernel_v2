#include "probe_common.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>
int main() {
  std::ostringstream stream; auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(stream);
  spdlog::logger logger("probe", sink); logger.set_pattern("%v"); logger.info("number={}", 7); logger.flush();
  PROBE_CHECK(stream.str().find("number=7") != std::string::npos);
  spdlog::shutdown(); std::cout << "spdlog external locked fmt/flush smoke\n";
}

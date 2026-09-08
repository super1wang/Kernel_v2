#include <filesystem>
#include <fstream>
#include <iostream>
#include <ock/data/payload.hpp>

int main(int argc, char **argv) {
  if (argc != 2) return 2;
  std::size_t count = 0;
  for (auto folder : {"accepted", "rejected"}) {
    const bool expected = std::string_view(folder) == "accepted";
    for (auto const &entry : std::filesystem::directory_iterator(std::filesystem::path(argv[1])/folder)) {
      std::ifstream input(entry.path(), std::ios::binary);
      std::string bytes((std::istreambuf_iterator<char>(input)), {});
      auto result = ock::data::Payload::parse(bytes);
      if (bool(result) != expected) {
        std::cerr << "seed expectation failed: " << entry.path() << '\n';
        return 1;
      }
      if (result) {
        auto encoded = result->encode();
        if (!encoded || !ock::data::Payload::parse(*encoded)) return 1;
      }
      // A bounded deterministic mutation pass; this is a seed regression,
      // not a claim of a long-running coverage-guided fuzz campaign.
      for (std::size_t i = 0; i < bytes.size(); ++i) {
        auto mutated = bytes;
        mutated[i] = static_cast<char>(static_cast<unsigned char>(mutated[i]) ^ 0x80);
        auto parsed = ock::data::Payload::parse(mutated);
        if (parsed) {
          auto encoded = parsed->encode();
          if (!encoded || !ock::data::Payload::parse(*encoded)) return 1;
        }
      }
      ++count;
    }
  }
  if (count < 12) return 1;
  std::cout << count << " parser seeds and bounded byte mutations passed\n";
}

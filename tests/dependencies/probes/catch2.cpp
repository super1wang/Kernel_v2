#include <catch_amalgamated.hpp>
TEST_CASE("real assertion succeeds", "[smoke]") { REQUIRE(2 + 2 == 4); }
TEST_CASE("deliberate assertion failure", "[.guard-fault]") { REQUIRE(false); }

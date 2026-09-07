#pragma once
#include <iostream>
#include <string>
#define PROBE_CHECK(condition) do { if (!(condition)) { std::cerr << "probe check failed: " << #condition << '\n'; return 1; } } while(false)

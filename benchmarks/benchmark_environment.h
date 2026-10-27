// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <benchmark/benchmark.h>

#include <fstream>
#include <string>

namespace BenchmarkEnvironment
{
inline void addCpuFrequencyContext()
{
#ifdef __linux__
  // Google Benchmark's MHz field may describe the hardware maximum, even
  // when the OS caps execution well below it. Label these as CPU 0 values:
  // other CPUs can have different policies on heterogeneous systems.
  constexpr const char* CPU_FREQUENCY_PATH =
      "/sys/devices/system/cpu/cpu0/cpufreq/";
  for (const char* field : {"scaling_governor", "scaling_max_freq"})
  {
    std::ifstream input(std::string(CPU_FREQUENCY_PATH) + field);
    if (std::string value; std::getline(input, value) && !value.empty())
    {
      const std::string key =
          std::string("cpu0_") + field +
          (std::string(field) == "scaling_max_freq" ? "_khz" : "");
      benchmark::AddCustomContext(key, value);
    }
  }
#endif
}

inline int run(int argc, char** argv)
{
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
  {
    return 1;
  }
  addCpuFrequencyContext();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
}  // namespace BenchmarkEnvironment

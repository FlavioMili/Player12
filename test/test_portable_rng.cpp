// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <random>
#include <vector>

#include "model/world_rng.h"

// Golden values: the same seed must give these exact bits with every
// compiler and standard library (they were taken with GCC and Clang, -O0 and
// -O2, with and without FMA instructions). A change here means saves and
// shared seeds no longer reproduce across platforms.

namespace
{
/** Distance in units in the last place between two finite doubles. */
std::int64_t ulpDistance(double a, double b)
{
  std::int64_t x = std::bit_cast<std::int64_t>(a);
  std::int64_t y = std::bit_cast<std::int64_t>(b);
  if (x < 0) x = std::numeric_limits<std::int64_t>::min() - x;
  if (y < 0) y = std::numeric_limits<std::int64_t>::min() - y;
  return x > y ? x - y : y - x;
}
}  // namespace

TEST(PortableRng, WorldRngGoldenValues)
{
  WorldRng rng(7);
  const double first = rng.uniform01();
  const double second = rng.uniform01();
  EXPECT_EQ(first, 0x1.66b1f5ee9df2ep-1);
  EXPECT_EQ(second, 0x1.1d70f6593d20ap-2);
  const int die_a = rng.uniformInt(1, 6);
  const int die_b = rng.uniformInt(1, 6);
  EXPECT_EQ(die_a, 1);
  EXPECT_EQ(die_b, 5);
  EXPECT_EQ(rng.uniform(2.0f, 5.0f), 0x1.3e3ec4p+2f);
  const float normal_a = rng.normal(60.0f, 10.0f);
  const float normal_b = rng.normal(60.0f, 10.0f);
  const float normal_c = rng.normal(60.0f, 10.0f);
  EXPECT_EQ(normal_a, 0x1.035d96p+6f);
  EXPECT_EQ(normal_b, 0x1.542296p+5f);
  EXPECT_EQ(normal_c, 0x1.49e1dap+5f);
  EXPECT_EQ(rng.lognormal(1e6f, 0.5f), 0x1.602c52p+20f);
}

TEST(PortableRng, PortableMathGoldenValues)
{
  EXPECT_EQ(PortableMath::log(0.3), -0x1.34378fcbda721p+0);
  EXPECT_EQ(PortableMath::log(1e-300), -0x1.5963447f87fb5p+9);
  EXPECT_EQ(PortableMath::log(12345.678), 0x1.2d79559791e31p+3);
  EXPECT_EQ(PortableMath::exp(-0.7), 0x1.fc80db9dd5542p-2);
  EXPECT_EQ(PortableMath::exp(10.5), 0x1.1bb7015e84d3bp+15);
  EXPECT_EQ(PortableMath::exp(-700.0), 0x1.14f2b0fb9307fp-1010);
  EXPECT_EQ(PortableMath::cosTwoPi(0.1), 0x1.9e3779b97f4a8p-1);
  EXPECT_EQ(PortableMath::cosTwoPi(0.375), -0x1.6a09e667f3bccp-1);
  EXPECT_EQ(PortableMath::cosTwoPi(0.9), 0x1.9e3779b97f4a8p-1);
  // Exact points.
  EXPECT_EQ(PortableMath::log(1.0), 0.0);
  EXPECT_EQ(PortableMath::exp(0.0), 1.0);
  EXPECT_EQ(PortableMath::cosTwoPi(0.0), 1.0);
  EXPECT_EQ(PortableMath::cosTwoPi(0.5), -1.0);
  EXPECT_EQ(PortableMath::log(0.0), -std::numeric_limits<double>::infinity());
  EXPECT_EQ(PortableMath::exp(-800.0), 0.0);
  EXPECT_TRUE(std::isinf(PortableMath::exp(800.0)));
  EXPECT_TRUE(
      std::isnan(PortableMath::exp(std::numeric_limits<double>::quiet_NaN())));
}

TEST(PortableRng, PortableMathIsAccurate)
{
  // Within a few units in the last place of the C library (itself within
  // one), over the ranges the generators use.
  std::mt19937_64 engine(11);
  std::int64_t worst_log = 0;
  std::int64_t worst_exp = 0;
  double worst_cos = 0.0;
  for (int i = 0; i < 200'000; ++i)
  {
    const double unit = PortableRandom::unitDouble(engine());
    if (unit <= 0.0) continue;
    const double x = std::ldexp(unit, static_cast<int>(engine() % 120U) - 60);
    worst_log =
        std::max(worst_log, ulpDistance(PortableMath::log(x), std::log(x)));
    if (const double power = (unit - 0.5) * 1400.0; std::exp(power) > 1e-300)
      worst_exp = std::max(
          worst_exp, ulpDistance(PortableMath::exp(power), std::exp(power)));
    worst_cos =
        std::max(worst_cos, std::abs(PortableMath::cosTwoPi(unit) -
                                     std::cos(2.0 * std::numbers::pi * unit)));
  }
  EXPECT_LE(worst_log, 4);
  EXPECT_LE(worst_exp, 4);
  EXPECT_LT(worst_cos, 2e-15);
}

TEST(PortableRng, StandardEngineDistributionGoldenValues)
{
  std::mt19937 engine(5489U);
  const float unit = PortableRandom::uniformReal(engine, 0.0f, 1.0f);
  const float signed_unit = PortableRandom::uniformReal(engine, -1.0f, 1.0f);
  EXPECT_EQ(unit, 0x1.a12376p-1f);
  EXPECT_EQ(signed_unit, -0x1.754588p-1f);
  EXPECT_EQ(PortableRandom::uniformReal(engine, 10.0, 20.0),
            0x1.30ed3cd54599fp+4);
  const int digit = PortableRandom::uniformInt(engine, 0, 9);
  const int offset = PortableRandom::uniformInt(engine, -5, 5);
  const int large = PortableRandom::uniformInt(engine, 100, 1'000'000);
  EXPECT_EQ(digit, 5);
  EXPECT_EQ(offset, 1);
  EXPECT_EQ(large, 848714);
  const double standard = PortableRandom::normal(engine, 0.0, 1.0);
  const double shifted = PortableRandom::normal(engine, 3.0, 2.0);
  EXPECT_EQ(standard, -0x1.4a837acad3c9cp-4);
  EXPECT_EQ(shifted, 0x1.5b599f2448652p+2);
  const std::vector expected_goals = {3, 0, 3, 3, 1, 2, 0, 1};
  std::vector<int> goals;
  for ([[maybe_unused]] const int expected_goal : expected_goals)
    goals.push_back(PortableRandom::poisson(engine, 0.9));
  EXPECT_EQ(goals, expected_goals);
  EXPECT_EQ(PortableRandom::poisson(engine, 150.0), 176);

  std::mt19937_64 wide(42);
  const std::uint64_t small = PortableRandom::below(wide, 10);
  const std::uint64_t prime = PortableRandom::below(wide, 1'000'003);
  EXPECT_EQ(small, 6U);
  EXPECT_EQ(prime, 854435U);
}

TEST(PortableRng, DistributionsHaveTheRightShape)
{
  std::mt19937 engine(2026);
  constexpr int DRAWS = 200'000;
  double poisson_sum = 0.0;
  double poisson_squares = 0.0;
  double normal_sum = 0.0;
  double normal_squares = 0.0;
  std::vector faces(6, 0);
  for (int i = 0; i < DRAWS; ++i)
  {
    const int goals = PortableRandom::poisson(engine, 0.8);
    ASSERT_GE(goals, 0);
    poisson_sum += goals;
    poisson_squares += goals * goals;
    const double z = PortableRandom::normal(engine, 0.0, 1.0);
    normal_sum += z;
    normal_squares += z * z;
    const int face = PortableRandom::uniformInt(engine, 1, 6);
    ASSERT_GE(face, 1);
    ASSERT_LE(face, 6);
    ++faces[static_cast<std::size_t>(face - 1)];
    const float unit = PortableRandom::uniformReal(engine, 0.0f, 1.0f);
    ASSERT_GE(unit, 0.0f);
    ASSERT_LT(unit, 1.0f);
  }
  const double poisson_mean = poisson_sum / DRAWS;
  EXPECT_NEAR(poisson_mean, 0.8, 0.01);
  EXPECT_NEAR(poisson_squares / DRAWS - poisson_mean * poisson_mean, 0.8, 0.02);
  const double normal_mean = normal_sum / DRAWS;
  EXPECT_NEAR(normal_mean, 0.0, 0.01);
  EXPECT_NEAR(normal_squares / DRAWS - normal_mean * normal_mean, 1.0, 0.02);
  for (const int count : faces) EXPECT_NEAR(count, DRAWS / 6, DRAWS / 100);
  // A mean of 0 never scores; large means are split but keep their mean.
  EXPECT_EQ(PortableRandom::poisson(engine, 0.0), 0);
  double large_sum = 0.0;
  for (int i = 0; i < 2'000; ++i)
    large_sum += PortableRandom::poisson(engine, 200.0);
  EXPECT_NEAR(large_sum / 2'000, 200.0, 1.5);
}

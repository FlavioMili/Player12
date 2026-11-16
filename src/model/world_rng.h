// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

#include "model/gamedate.h"

/**
 * @enum RngDomain
 * @brief Independent random streams of the world simulation.
 *
 * Every stream is derived from the save's world seed plus a domain tag and
 * stable keys (day, player id, team id). Consuming numbers in one domain never
 * perturbs another, and results do not depend on container iteration order,
 * so a reloaded save continues exactly like an uninterrupted one.
 */
enum class RngDomain : std::uint64_t
{
  Generation = 1,
  Migration,
  TrainingInjury,
  MatchInjury,
  MatchRating,
  Development,
  YouthIntake,
  Retirement,
  Transfers,
  Scouting,
  Staff,
  Training,
  Interactions,
  Stories,
  Managers,
};

/**
 * @class WorldRng
 * @brief Deterministic, portable pseudo random generator (xoshiro256**).
 *
 * All distributions are implemented here instead of using <random>
 * distributions, whose algorithms differ between standard libraries. The same
 * seed therefore produces the same world on every platform.
 */
class WorldRng
{
 public:
  using result_type = std::uint64_t;

  /** Seeds the four state words from @p seed through splitmix64. */
  explicit WorldRng(std::uint64_t seed);

  /** Creates the stream for @p domain keyed by up to two stable values. */
  static WorldRng stream(std::uint64_t world_seed, RngDomain domain,
                         std::uint64_t key_a = 0, std::uint64_t key_b = 0);

  /** Single uniform draw in [0, 1) without constructing a stream. */
  static double hashUniform(std::uint64_t world_seed, RngDomain domain,
                            std::uint64_t key_a, std::uint64_t key_b = 0);

  static constexpr result_type min() { return 0; }
  static constexpr result_type max()
  {
    return std::numeric_limits<result_type>::max();
  }
  result_type operator()() { return next(); }

  /** Next raw 64-bit value. */
  std::uint64_t next();

  /** Uniform double in [0, 1). */
  double uniform01();

  /** Uniform float in [lo, hi). */
  float uniform(float lo, float hi);

  /** Uniform integer in [lo, hi] (inclusive, unbiased). */
  int uniformInt(int lo, int hi);

  /** Normally distributed value (Box-Muller over PortableMath). */
  float normal(float mean, float stddev);

  /** Lognormal value with the given median and log-space sigma. */
  float lognormal(float median, float sigma);

  /** Bernoulli trial with probability @p p. */
  bool chance(double p);

  /** Index drawn proportionally to non-negative @p weights. */
  std::size_t weightedIndex(std::span<const float> weights);

  /** Fisher-Yates shuffle with this generator. */
  template <typename T>
  void shuffle(std::span<T> values)
  {
    for (std::size_t i = values.size(); i > 1; --i)
    {
      const auto j =
          static_cast<std::size_t>(uniformInt(0, static_cast<int>(i - 1)));
      std::swap(values[i - 1], values[j]);
    }
  }

 private:
  std::array<std::uint64_t, 4> state{};
};

/**
 * Elementary functions built from IEEE-754 basic operations only (+, -, *,
 * /, sqrt, frexp, ldexp, floor), which every platform rounds the same way.
 * The C library's log, exp and cos may differ in the last bit between
 * libstdc++/glibc, libc++/Apple and MSVC, which would make seeded draws
 * platform dependent. Accurate to a few units in the last place.
 */
namespace PortableMath
{
/** Natural logarithm of @p x > 0. */
double log(double x);
/** e to the power @p x (0 below -745, infinity above 709.78). */
double exp(double x);
/** cos(2 pi @p turns) for @p turns in [0, 1). */
double cosTwoPi(double turns);
}  // namespace PortableMath

/**
 * Distributions over any standard 32- or 64-bit generator (std::mt19937,
 * std::mt19937_64), with the same results on every platform. Use these
 * instead of the <random> distributions, whose algorithms are left to the
 * standard library. The arithmetic lives out of line in world_rng.cpp.
 */
namespace PortableRandom
{
template <typename Engine>
concept FullRangeEngine =
    Engine::min() == 0 &&
    (Engine::max() == std::numeric_limits<std::uint32_t>::max() ||
     Engine::max() == std::numeric_limits<std::uint64_t>::max());

/** 32 random bits (the high half of a 64-bit engine's output). */
template <FullRangeEngine Engine>
std::uint32_t bits32(Engine& engine)
{
  if constexpr (Engine::max() == std::numeric_limits<std::uint32_t>::max())
    return static_cast<std::uint32_t>(engine());
  else
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(engine()) >>
                                      32U);
}

/** 64 random bits (two draws of a 32-bit engine, the first one high). */
template <FullRangeEngine Engine>
std::uint64_t bits64(Engine& engine)
{
  if constexpr (Engine::max() == std::numeric_limits<std::uint32_t>::max())
  {
    const auto high = static_cast<std::uint64_t>(engine());
    return (high << 32U) | static_cast<std::uint64_t>(engine());
  }
  else
  {
    return static_cast<std::uint64_t>(engine());
  }
}

/** Float in [0, 1) from the top 24 of @p bits. */
float unitFloat(std::uint32_t bits);
/** Double in [0, 1) from the top 53 of @p bits. */
double unitDouble(std::uint64_t bits);
/** @p lo + @p unit * (@p hi - @p lo), never contracted to a fused op. */
float lerp(float lo, float hi, float unit);
double lerp(double lo, double hi, double unit);
/** @p offset + @p value * @p scale, never contracted to a fused op. */
double affine(double offset, double scale, double value);
/** Standard normal value from two uniforms (Box-Muller); @p u1 in (0, 1]. */
double standardNormal(double u1, double u2);
/** Poisson value of mean @p mean (<= 64) by inversion of @p unit. */
int poissonFromUnit(double unit, double mean);

/** Uniform float in [lo, hi). */
template <FullRangeEngine Engine>
float uniformReal(Engine& engine, float lo, float hi)
{
  return lerp(lo, hi, unitFloat(bits32(engine)));
}

/** Uniform double in [lo, hi). */
template <FullRangeEngine Engine>
double uniformReal(Engine& engine, double lo, double hi)
{
  return lerp(lo, hi, unitDouble(bits64(engine)));
}

/** Uniform integer in [0, @p count) without modulo bias; 0 if count is 0. */
template <FullRangeEngine Engine>
std::uint64_t below(Engine& engine, std::uint64_t count)
{
  if (count <= 1) return 0;
  // Rejection sampling: values under the threshold would favour the low
  // residues.
  const std::uint64_t threshold = (0U - count) % count;
  std::uint64_t value = bits64(engine);
  while (value < threshold) value = bits64(engine);
  return value % count;
}

/** Uniform integer in [lo, hi] (inclusive). */
template <FullRangeEngine Engine>
int uniformInt(Engine& engine, int lo, int hi)
{
  if (hi <= lo) return lo;
  const auto count =
      static_cast<std::uint64_t>(static_cast<std::int64_t>(hi) - lo) + 1U;
  return static_cast<int>(static_cast<std::int64_t>(lo) +
                          static_cast<std::int64_t>(below(engine, count)));
}

/** Normally distributed value. */
template <FullRangeEngine Engine>
double normal(Engine& engine, double mean, double stddev)
{
  // 1 - [0, 1) is in (0, 1], so the logarithm is always finite.
  const double u1 = 1.0 - unitDouble(bits64(engine));
  const double u2 = unitDouble(bits64(engine));
  return affine(mean, stddev, standardNormal(u1, u2));
}

/** Poisson distributed count of mean @p mean (>= 0). */
template <FullRangeEngine Engine>
int poisson(Engine& engine, double mean)
{
  // A sum of independent Poisson counts is a Poisson count of the summed
  // means, so large means are split into parts inversion handles well.
  constexpr double PART = 64.0;
  int count = 0;
  while (mean > PART)
  {
    count += poissonFromUnit(unitDouble(bits64(engine)), PART);
    mean -= PART;
  }
  return count + poissonFromUnit(unitDouble(bits64(engine)), mean);
}
}  // namespace PortableRandom

/** Mixes two 64-bit values into a well distributed hash (splitmix64). */
std::uint64_t mixHash(std::uint64_t a, std::uint64_t b);

/** Days since 1970-01-01 for a proleptic Gregorian date. */
std::int32_t dayOrdinal(const GameDateValue& date);

/** Converts a date to the compact integer form YYYYMMDD. */
std::int32_t dateToInt(const GameDateValue& date);

/** Converts the compact integer form YYYYMMDD back to a date. */
GameDateValue dateFromInt(std::int32_t value);

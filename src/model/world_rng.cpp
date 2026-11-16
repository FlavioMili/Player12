// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/world_rng.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>

// Seeded draws must round identically everywhere: no fused multiply-add
// (the build also passes -ffp-contract=off to GCC and Clang).
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

namespace
{
std::uint64_t splitmix64(std::uint64_t& x)
{
  std::uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31U);
}

double toUnit(std::uint64_t value)
{
  return static_cast<double>(value >> 11U) * 0x1.0p-53;
}

// ln 2 split so that k * LN2_HI is exact for |k| < 2^20 (fdlibm).
constexpr double LN2_HI = 6.93147180369123816490e-01;
constexpr double LN2_LO = 1.90821492927058770002e-10;
constexpr double INV_LN2 = 1.44269504088896338700e+00;
constexpr double HALF_PI = 1.57079632679489661923;
constexpr double SQRT_HALF = 0.70710678118654752440;

/** Horner evaluation of sum c[i] * x^i with the highest term first. */
template <std::size_t N>
double horner(const std::array<double, N>& coefficients, double x)
{
  double sum = coefficients[N - 1];
  for (std::size_t i = N - 1; i > 0; --i)
  {
    const double product = sum * x;
    sum = product + coefficients[i - 1];
  }
  return sum;
}

// 1 / (2k + 1) for atanh(s) / s = sum s^(2k) / (2k + 1); |s| <= 0.1716.
constexpr std::array<double, 13> ATANH_SERIES = {
    1.0,        1.0 / 3.0,  1.0 / 5.0,  1.0 / 7.0,  1.0 / 9.0,
    1.0 / 11.0, 1.0 / 13.0, 1.0 / 15.0, 1.0 / 17.0, 1.0 / 19.0,
    1.0 / 21.0, 1.0 / 23.0, 1.0 / 25.0};

constexpr double factorial(int n)
{
  return n <= 1 ? 1.0 : n * factorial(n - 1);
}

// Taylor series of e^r for |r| <= ln 2 / 2.
constexpr std::array<double, 15> EXP_SERIES = {1.0,
                                               1.0,
                                               1.0 / factorial(2),
                                               1.0 / factorial(3),
                                               1.0 / factorial(4),
                                               1.0 / factorial(5),
                                               1.0 / factorial(6),
                                               1.0 / factorial(7),
                                               1.0 / factorial(8),
                                               1.0 / factorial(9),
                                               1.0 / factorial(10),
                                               1.0 / factorial(11),
                                               1.0 / factorial(12),
                                               1.0 / factorial(13),
                                               1.0 / factorial(14)};

// cos y = sum (-1)^k y^(2k) / (2k)!, in powers of y^2; |y| <= pi / 4.
constexpr std::array<double, 10> COS_SERIES = {1.0,
                                               -1.0 / factorial(2),
                                               1.0 / factorial(4),
                                               -1.0 / factorial(6),
                                               1.0 / factorial(8),
                                               -1.0 / factorial(10),
                                               1.0 / factorial(12),
                                               -1.0 / factorial(14),
                                               1.0 / factorial(16),
                                               -1.0 / factorial(18)};

// sin y / y = sum (-1)^k y^(2k) / (2k + 1)!, in powers of y^2.
constexpr std::array<double, 10> SIN_SERIES = {1.0,
                                               -1.0 / factorial(3),
                                               1.0 / factorial(5),
                                               -1.0 / factorial(7),
                                               1.0 / factorial(9),
                                               -1.0 / factorial(11),
                                               1.0 / factorial(13),
                                               -1.0 / factorial(15),
                                               1.0 / factorial(17),
                                               -1.0 / factorial(19)};

double cosSmall(double y)
{
  const double y2 = y * y;
  return horner(COS_SERIES, y2);
}

double sinSmall(double y)
{
  const double y2 = y * y;
  return y * horner(SIN_SERIES, y2);
}
}  // namespace

double PortableMath::log(double x)
{
  if (!(x > 0.0))
    return x == 0.0 ? -std::numeric_limits<double>::infinity()
                    : std::numeric_limits<double>::quiet_NaN();
  if (x == std::numeric_limits<double>::infinity()) return x;
  // x = m * 2^e with m in [sqrt(1/2), sqrt(2)); frexp is exact.
  int exponent = 0;
  double mantissa = std::frexp(x, &exponent);
  if (mantissa < SQRT_HALF)
  {
    mantissa *= 2.0;
    --exponent;
  }
  // log m = 2 atanh(s) with s = (m - 1) / (m + 1); m - 1 is exact.
  const double f = mantissa - 1.0;
  const double s = f / (2.0 + f);
  const double s2 = s * s;
  const double log_mantissa = 2.0 * s * horner(ATANH_SERIES, s2);
  const auto k = static_cast<double>(exponent);
  const double low = k * LN2_LO + log_mantissa;
  return k * LN2_HI + low;
}

double PortableMath::exp(double x)
{
  if (std::isnan(x)) return x;
  if (x > 709.78) return std::numeric_limits<double>::infinity();
  if (x < -745.2) return 0.0;
  // x = k ln 2 + r with |r| <= ln 2 / 2; k * LN2_HI is exact.
  const double k = std::floor(x * INV_LN2 + 0.5);
  const double hi = x - k * LN2_HI;
  const double r = hi - k * LN2_LO;
  return std::ldexp(horner(EXP_SERIES, r), static_cast<int>(k));
}

double PortableMath::cosTwoPi(double turns)
{
  // Quarter turns: 4t and its fraction are exact.
  const double quarters = turns * 4.0;
  const double whole = std::floor(quarters);
  const double fraction = quarters - whole;
  const auto quadrant = static_cast<int>(static_cast<long long>(whole) & 3LL);
  // cos and sin of the angle within the quadrant, from series on [0, pi/4].
  double cos_part = 0.0;
  double sin_part = 0.0;
  if (fraction <= 0.5)
  {
    const double y = fraction * HALF_PI;
    cos_part = cosSmall(y);
    sin_part = sinSmall(y);
  }
  else
  {
    const double y = (1.0 - fraction) * HALF_PI;
    cos_part = sinSmall(y);
    sin_part = cosSmall(y);
  }
  switch (quadrant)
  {
    case 0:
      return cos_part;
    case 1:
      return -sin_part;
    case 2:
      return -cos_part;
    default:
      return sin_part;
  }
}

float PortableRandom::unitFloat(std::uint32_t bits)
{
  return static_cast<float>(bits >> 8U) * 0x1p-24f;
}

double PortableRandom::unitDouble(std::uint64_t bits) { return toUnit(bits); }

float PortableRandom::lerp(float lo, float hi, float unit)
{
  const float span = hi - lo;
  const float offset = unit * span;
  return lo + offset;
}

double PortableRandom::lerp(double lo, double hi, double unit)
{
  const double span = hi - lo;
  const double offset = unit * span;
  return lo + offset;
}

double PortableRandom::affine(double offset, double scale, double value)
{
  const double scaled = value * scale;
  return offset + scaled;
}

double PortableRandom::standardNormal(double u1, double u2)
{
  const double radius = std::sqrt(-2.0 * PortableMath::log(u1));
  return radius * PortableMath::cosTwoPi(u2);
}

int PortableRandom::poissonFromUnit(double unit, double mean)
{
  if (!(mean > 0.0)) return 0;
  // Inversion: the smallest k whose cumulative probability exceeds unit.
  constexpr int LIMIT = 1000;
  double probability = PortableMath::exp(-mean);
  double cumulative = probability;
  int k = 0;
  while (unit >= cumulative && k < LIMIT)
  {
    ++k;
    probability = probability * mean / k;
    cumulative += probability;
    // Rounding can leave the sum just below a unit close to 1.
    if (probability == 0.0 && k > mean) break;
  }
  return k;
}

std::uint64_t mixHash(std::uint64_t a, std::uint64_t b)
{
  std::uint64_t x = a ^ std::rotl(b, 23) ^ 0xD1B54A32D192ED03ULL;
  splitmix64(x);
  return splitmix64(x) ^ b;
}

WorldRng::WorldRng(std::uint64_t seed)
{
  for (auto& word : state) word = splitmix64(seed);
}

WorldRng WorldRng::stream(std::uint64_t world_seed, RngDomain domain,
                          std::uint64_t key_a, std::uint64_t key_b)
{
  const std::uint64_t domain_hash =
      mixHash(world_seed, static_cast<std::uint64_t>(domain));
  return WorldRng(mixHash(mixHash(domain_hash, key_a), key_b));
}

double WorldRng::hashUniform(std::uint64_t world_seed, RngDomain domain,
                             std::uint64_t key_a, std::uint64_t key_b)
{
  const std::uint64_t domain_hash =
      mixHash(world_seed, static_cast<std::uint64_t>(domain));
  return toUnit(mixHash(mixHash(domain_hash, key_a), key_b));
}

std::uint64_t WorldRng::next()
{
  const std::uint64_t result = std::rotl(state[1] * 5, 7) * 9;
  const std::uint64_t t = state[1] << 17U;
  state[2] ^= state[0];
  state[3] ^= state[1];
  state[1] ^= state[2];
  state[0] ^= state[3];
  state[2] ^= t;
  state[3] = std::rotl(state[3], 45);
  return result;
}

double WorldRng::uniform01() { return toUnit(next()); }

float WorldRng::uniform(float lo, float hi)
{
  return lo + static_cast<float>(uniform01()) * (hi - lo);
}

int WorldRng::uniformInt(int lo, int hi)
{
  if (hi <= lo) return lo;
  const auto range =
      static_cast<std::uint64_t>(static_cast<std::int64_t>(hi) - lo) + 1U;
  // Rejection sampling removes the modulo bias for exact uniformity.
  const std::uint64_t threshold = (0U - range) % range;
  std::uint64_t value = next();
  while (value < threshold) value = next();
  return static_cast<int>(static_cast<std::int64_t>(lo) +
                          static_cast<std::int64_t>(value % range));
}

float WorldRng::normal(float mean, float stddev)
{
  double u1 = uniform01();
  while (u1 <= 0.0) u1 = uniform01();
  const double u2 = uniform01();
  const double z = PortableRandom::standardNormal(u1, u2);
  const float scaled = static_cast<float>(z) * stddev;
  return mean + scaled;
}

float WorldRng::lognormal(float median, float sigma)
{
  return median * static_cast<float>(PortableMath::exp(
                      static_cast<double>(normal(0.0f, sigma))));
}

bool WorldRng::chance(double p) { return uniform01() < p; }

std::size_t WorldRng::weightedIndex(std::span<const float> weights)
{
  double total = 0.0;
  for (const float weight : weights)
    total += static_cast<double>(std::max(0.0f, weight));
  if (weights.empty() || total <= 0.0) return 0;
  double pick = uniform01() * total;
  for (std::size_t i = 0; i < weights.size(); ++i)
  {
    pick -= static_cast<double>(std::max(0.0f, weights[i]));
    if (pick < 0.0) return i;
  }
  return weights.size() - 1;
}

std::int32_t dayOrdinal(const GameDateValue& date)
{
  // Howard Hinnant's days_from_civil.
  const int month = date.month;
  const int year = static_cast<int>(date.year) - (month <= 2 ? 1 : 0);
  const int era = (year >= 0 ? year : year - 399) / 400;
  const int year_of_era = year - era * 400;
  const int day_of_year =
      (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + date.day - 1;
  const int day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return era * 146097 + day_of_era - 719468;
}

std::int32_t dateToInt(const GameDateValue& date)
{
  return static_cast<std::int32_t>(date.year) * 10000 + date.month * 100 +
         date.day;
}

GameDateValue dateFromInt(std::int32_t value)
{
  return GameDateValue(static_cast<std::uint16_t>(value / 10000),
                       static_cast<std::uint8_t>((value / 100) % 100),
                       static_cast<std::uint8_t>(value % 100));
}

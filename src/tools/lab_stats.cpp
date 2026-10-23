// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/lab_stats.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <random>
#include <vector>

#include "model/world_rng.h"

namespace Lab
{
namespace
{
constexpr double Z_95 = 1.959963984540054;
constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

bool sameNumber(double left, double right)
{
  return (std::isnan(left) && std::isnan(right)) || left == right;
}
}  // namespace

bool Estimate::valid() const { return n > 0 && std::isfinite(value); }

bool Estimate::operator==(const Estimate& other) const
{
  return n == other.n && sameNumber(value, other.value) &&
         sameNumber(low, other.low) && sameNumber(high, other.high);
}

double tQuantile975(std::size_t degrees_of_freedom)
{
  // Student t 0.975 quantiles for 1-30 degrees of freedom.
  static constexpr std::array<double, 30> TABLE = {
      12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228,
      2.201,  2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086,
      2.080,  2.074, 2.069, 2.064, 2.060, 2.056, 2.052, 2.048, 2.045, 2.042};
  if (degrees_of_freedom == 0) return NaN;
  if (degrees_of_freedom <= TABLE.size()) return TABLE[degrees_of_freedom - 1];
  if (degrees_of_freedom <= 60) return 2.000;
  if (degrees_of_freedom <= 120) return 1.980;
  return Z_95;
}

Estimate meanEstimate(std::span<const double> values)
{
  Estimate estimate;
  estimate.n = values.size();
  if (values.empty()) return estimate;
  const auto n = static_cast<double>(values.size());
  const double mean = std::accumulate(values.begin(), values.end(), 0.0) / n;
  estimate.value = mean;
  if (values.size() < 2)
  {
    estimate.low = mean;
    estimate.high = mean;
    return estimate;
  }
  double squares = 0.0;
  for (const double value : values) squares += (value - mean) * (value - mean);
  const double half = tQuantile975(values.size() - 1) *
                      std::sqrt(squares / (n - 1.0)) / std::sqrt(n);
  estimate.low = mean - half;
  estimate.high = mean + half;
  // Counts and durations cannot be negative: keep the interval meaningful.
  if (std::ranges::all_of(values, [](double value) { return value >= 0.0; }))
    estimate.low = std::max(estimate.low, 0.0);
  return estimate;
}

Estimate proportionEstimate(std::size_t successes, std::size_t trials)
{
  Estimate estimate;
  estimate.n = trials;
  if (trials == 0) return estimate;
  const auto n = static_cast<double>(trials);
  const double p = static_cast<double>(std::min(successes, trials)) / n;
  const double z2 = Z_95 * Z_95;
  const double centre = (p + z2 / (2.0 * n)) / (1.0 + z2 / n);
  const double half =
      Z_95 * std::sqrt(p * (1.0 - p) / n + z2 / (4.0 * n * n)) / (1.0 + z2 / n);
  estimate.value = p;
  estimate.low = std::max(0.0, centre - half);
  estimate.high = std::min(1.0, centre + half);
  return estimate;
}

Estimate ratioEstimate(std::span<const double> numerators,
                       std::span<const double> denominators)
{
  Estimate estimate;
  const auto units = std::min(numerators.size(), denominators.size());
  estimate.n = units;
  if (units == 0) return estimate;
  const double num = std::accumulate(
      numerators.begin(),
      numerators.begin() + static_cast<std::ptrdiff_t>(units), 0.0);
  const double den = std::accumulate(
      denominators.begin(),
      denominators.begin() + static_cast<std::ptrdiff_t>(units), 0.0);
  if (den <= 0.0)
  {
    estimate.n = 0;
    return estimate;
  }
  const double ratio = num / den;
  estimate.value = ratio;
  if (units < 2)
  {
    estimate.low = ratio;
    estimate.high = ratio;
    return estimate;
  }
  const auto n = static_cast<double>(units);
  const double meanDen = den / n;
  double residuals = 0.0;
  for (std::size_t i = 0; i < units; ++i)
  {
    const double residual = numerators[i] - ratio * denominators[i];
    residuals += residual * residual;
  }
  const double se = std::sqrt(residuals / (n * (n - 1.0))) / meanDen;
  const double half = tQuantile975(units - 1) * se;
  estimate.low = ratio - half;
  estimate.high = ratio + half;
  if (const auto nonNegative = [](double value) { return value >= 0.0; };
      std::all_of(numerators.begin(),
                  numerators.begin() + static_cast<std::ptrdiff_t>(units),
                  nonNegative) &&
      std::all_of(denominators.begin(),
                  denominators.begin() + static_cast<std::ptrdiff_t>(units),
                  nonNegative))
    estimate.low = std::max(estimate.low, 0.0);
  return estimate;
}

Estimate medianEstimate(std::span<const double> values)
{
  Estimate estimate;
  estimate.n = values.size();
  if (values.empty()) return estimate;
  std::vector<double> sorted(values.begin(), values.end());
  std::ranges::sort(sorted);
  const auto n = sorted.size();
  estimate.value =
      n % 2 == 1 ? sorted[n / 2] : 0.5 * (sorted[n / 2 - 1] + sorted[n / 2]);
  // Ranks n/2 -+ z sqrt(n)/2 bound the median with ~95% coverage.
  const double spread = Z_95 * std::sqrt(static_cast<double>(n)) / 2.0;
  const double half = static_cast<double>(n) / 2.0;
  const auto lowRank = static_cast<std::ptrdiff_t>(std::floor(half - spread));
  const auto highRank = static_cast<std::ptrdiff_t>(std::ceil(half + spread));
  const auto last = static_cast<std::ptrdiff_t>(n) - 1;
  estimate.low = sorted[static_cast<std::size_t>(
      std::clamp<std::ptrdiff_t>(lowRank - 1, 0, last))];
  estimate.high = sorted[static_cast<std::size_t>(
      std::clamp<std::ptrdiff_t>(highRank - 1, 0, last))];
  return estimate;
}

Estimate bootstrapEstimate(
    std::size_t units,
    const std::function<double(std::span<const std::size_t>)>& statistic,
    std::uint64_t seed, int resamples)
{
  Estimate estimate;
  estimate.n = units;
  if (units == 0) return estimate;
  std::vector<std::size_t> indices(units);
  std::iota(indices.begin(), indices.end(), std::size_t{0});
  estimate.value = statistic(indices);
  if (!std::isfinite(estimate.value))
  {
    estimate.n = 0;
    return estimate;
  }
  std::mt19937_64 rng(seed);
  std::vector<double> draws;
  draws.reserve(static_cast<std::size_t>(std::max(resamples, 0)));
  for (int r = 0; r < resamples; ++r)
  {
    for (std::size_t& index : indices)
      index = PortableRandom::below(rng, units);
    const double value = statistic(indices);
    if (std::isfinite(value)) draws.push_back(value);
  }
  if (draws.size() < 20)
  {
    estimate.low = estimate.value;
    estimate.high = estimate.value;
    return estimate;
  }
  std::ranges::sort(draws);
  const auto at = [&](double q)
  {
    const auto index = static_cast<std::size_t>(
        std::lround(q * static_cast<double>(draws.size() - 1)));
    return draws[index];
  };
  estimate.low = at(0.025);
  estimate.high = at(0.975);
  return estimate;
}

Verdict classify(const Estimate& estimate, const std::optional<Band>& band)
{
  using enum Verdict;
  if (!estimate.valid()) return NotMeasured;
  if (!band) return Info;
  if (estimate.value >= band->low && estimate.value <= band->high) return Pass;
  const double low =
      std::isfinite(estimate.low) ? estimate.low : estimate.value;
  if (const double high =
          std::isfinite(estimate.high) ? estimate.high : estimate.value;
      high >= band->low && low <= band->high)
    return Warn;
  return Fail;
}
}  // namespace Lab

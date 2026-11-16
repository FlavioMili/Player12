// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Multi-season soak of the whole AI-only world (every league, every club):
// after three seasons, saved and reloaded at every rollover, the population,
// player ability, club finances and competitions must stay inside
// believable bands. A full world season takes minutes, so the test carries
// the "slow" label; `fm_lab season --seasons 10 --reload` runs the long
// version with a report.

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

#include "global/logger.h"
#include "tools/lab_season.h"

namespace
{
constexpr int SEASONS = 3;
constexpr std::uint64_t SOAK_SEED = 1;

unsigned simulationThreads()
{
  if (const char* env = std::getenv("FM_SIM_THREADS"); env && *env)
    return static_cast<unsigned>(std::max(1, std::atoi(env)));
  return 2;
}

double relativeChange(double from, double to)
{
  return from == 0.0 ? 0.0 : to / from - 1.0;
}

/** Saves of a whole world reach 150 MB after three seasons (plus backups):
 * they go to the test's working directory, not to /tmp. */
class SoakRuntime
{
 public:
  SoakRuntime()
  {
    std::filesystem::create_directories(path);
    setRoot(path.string());
  }
  ~SoakRuntime()
  {
    setRoot("");
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
  SoakRuntime(const SoakRuntime&) = delete;
  SoakRuntime& operator=(const SoakRuntime&) = delete;

 private:
  static void setRoot(const std::string& root)
  {
#if !defined(_WIN32)
    if (root.empty())
      unsetenv("FM_RUNTIME_ROOT");
    else
      setenv("FM_RUNTIME_ROOT", root.c_str(), 1);
#else
    _putenv_s("FM_RUNTIME_ROOT", root.c_str());
#endif
  }

  std::filesystem::path path = std::filesystem::current_path() / "soak-runtime";
};
}  // namespace

TEST(WorldSoakTest, ThreeSeasonsStayBelievable)
{
  Logger::init();
  if (const auto logger = spdlog::get("main_logger"))
    logger->set_level(spdlog::level::warn);

  const SoakRuntime runtime;
  std::vector<Lab::SeasonVitals> vitals;
  Lab::SeasonOptions options;
  options.seed = SOAK_SEED;
  options.seasons = SEASONS;
  options.threads = simulationThreads();
  options.reload = true;
  options.vitals = &vitals;
  ASSERT_NO_THROW(Lab::runSeasons(options));
  ASSERT_EQ(vitals.size(), static_cast<std::size_t>(SEASONS));
  const Lab::SeasonVitals& first = vitals.front();
  const Lab::SeasonVitals& last = vitals.back();

  // Every competition finishes every season, and the save stays loadable.
  for (const Lab::SeasonVitals& season : vitals)
  {
    SCOPED_TRACE(std::format("season {}", season.season));
    EXPECT_GT(season.leagues_completed, 0u);
    EXPECT_EQ(season.leagues_completed, first.leagues_completed);
    EXPECT_GT(season.promoted, 0u);
    EXPECT_EQ(season.promoted, season.relegated);
    EXPECT_EQ(season.cups_completed, first.cups_completed);
    EXPECT_GT(season.continental_completed, 0u);
    EXPECT_GT(season.load_ms, 0.0) << "the save after the season must load";
  }
  // Saves grow with the history (about 47 MB a season with seed 1); the
  // growth must not accelerate.
  EXPECT_LT(last.save_mb, 4.0 * first.save_mb);

  // Population: senior squads and the players on the market keep their
  // size. The academies fill up over the first seasons (a new world starts
  // with its teenagers only), so the whole population may only grow within
  // a bound. The unsigned do not pile up: players nobody wants drop below
  // the simulated divisions or retire (seed 1: about 1% of the players on
  // 30 June; 4% and growing without the clearance).
  EXPECT_LT(std::abs(relativeChange(static_cast<double>(first.club_seniors),
                                    static_cast<double>(last.club_seniors))),
            0.12);
  EXPECT_LT(std::abs(relativeChange(
                static_cast<double>(first.club_seniors + first.free_agents),
                static_cast<double>(last.club_seniors + last.free_agents))),
            0.10);
  EXPECT_LT(static_cast<double>(last.players),
            1.35 * static_cast<double>(first.players));
  for (const Lab::SeasonVitals& season : vitals)
  {
    SCOPED_TRACE(std::format("season {}", season.season));
    EXPECT_LT(static_cast<double>(season.free_agents),
              0.025 * static_cast<double>(season.players))
        << "free-agent pool on 30 June";
  }

  // Ability: bounded drift. Seed 1 measures +1.7 mean overall and +0.7 for
  // the best hundred over two seasons, a known inflation these bands only
  // keep from getting worse.
  EXPECT_LT(std::abs(last.mean_overall - first.mean_overall), 2.5);
  EXPECT_LT(std::abs(last.top100_overall - first.top100_overall), 1.5);

  // Finances: almost no club deep in debt, wages a believable share of
  // revenue, and second tiers that do not slide into the red season after
  // season (seed 1, worst month: tier 2 26% in season 3; 39% with 7%
  // insolvent and wages at 85% of revenue without parachutes and
  // relegation clauses).
  constexpr std::array<double, 2> PEAK_NEGATIVE = {0.10, 0.35};
  for (const Lab::SeasonVitals& season : vitals)
  {
    SCOPED_TRACE(std::format("season {}", season.season));
    for (std::size_t tier = 0; tier < season.tiers.size(); ++tier)
    {
      SCOPED_TRACE(std::format("tier {}", tier + 1));
      const Lab::TierVitals& row = season.tiers[tier];
      EXPECT_GT(row.clubs, 0u);
      EXPECT_LT(row.peak_negative_share, PEAK_NEGATIVE[tier]);
      EXPECT_LT(row.insolvent_share, 0.03);
      EXPECT_GT(row.wage_revenue, 0.40);
      EXPECT_LT(row.wage_revenue, 0.80);
    }
  }

  // Money buys success: wage bills explain league positions (CT-W28 band
  // 0.35-0.55 for top divisions).
  double r2 = 0.0;
  double r2_top = 0.0;
  for (const Lab::SeasonVitals& season : vitals)
  {
    r2 += season.wage_position_r2;
    r2_top += season.wage_position_r2_top;
  }
  EXPECT_GT(r2 / static_cast<double>(vitals.size()), 0.25);
  EXPECT_GT(r2_top / static_cast<double>(vitals.size()), 0.35);
}

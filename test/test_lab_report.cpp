// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <cmath>
#include <format>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "model/match_report.h"
#include "tools/lab_fixtures.h"
#include "tools/lab_report.h"
#include "tools/lab_runner.h"
#include "tools/lab_stats.h"

namespace
{
constexpr std::array MATCH_SCOPES = {Lab::Scope::Match, Lab::Scope::Engine};

Lab::Estimate exact(double value, std::size_t n = 1000)
{
  Lab::Estimate estimate;
  estimate.value = value;
  estimate.low = value;
  estimate.high = value;
  estimate.n = n;
  return estimate;
}

const Lab::ReportRow& rowById(const std::vector<Lab::ReportRow>& rows,
                              const std::string& id)
{
  for (const Lab::ReportRow& row : rows)
    if (row.id == id) return row;
  throw std::out_of_range(id);
}
}  // namespace

TEST(LabStats, WilsonIntervalMatchesReferenceValues)
{
  const Lab::Estimate half = Lab::proportionEstimate(50, 100);
  EXPECT_DOUBLE_EQ(half.value, 0.5);
  EXPECT_NEAR(half.low, 0.4038, 1e-4);
  EXPECT_NEAR(half.high, 0.5962, 1e-4);

  const Lab::Estimate none = Lab::proportionEstimate(0, 10);
  EXPECT_DOUBLE_EQ(none.value, 0.0);
  EXPECT_DOUBLE_EQ(none.low, 0.0);
  EXPECT_NEAR(none.high, 0.2775, 1e-4);

  EXPECT_FALSE(Lab::proportionEstimate(0, 0).valid());
}

TEST(LabStats, MeanUsesStudentTInterval)
{
  const std::vector values = {1.0, 2.0, 3.0, 4.0, 5.0};
  const Lab::Estimate mean = Lab::meanEstimate(values);
  EXPECT_DOUBLE_EQ(mean.value, 3.0);
  // sd = 1.5811, t(4) = 2.776 -> half width 1.963.
  EXPECT_NEAR(mean.low, 1.037, 1e-3);
  EXPECT_NEAR(mean.high, 4.963, 1e-3);
  EXPECT_EQ(mean.n, 5U);
  EXPECT_NEAR(Lab::tQuantile975(1000), 1.95996, 1e-4);
  EXPECT_NEAR(Lab::tQuantile975(1), 12.706, 1e-3);
}

TEST(LabStats, RatioAndMedianIntervals)
{
  const std::vector num = {1.0, 1.0, 1.0, 1.0};
  const std::vector den = {2.0, 2.0, 2.0, 2.0};
  const Lab::Estimate constant = Lab::ratioEstimate(num, den);
  EXPECT_DOUBLE_EQ(constant.value, 0.5);
  EXPECT_DOUBLE_EQ(constant.low, 0.5);
  EXPECT_DOUBLE_EQ(constant.high, 0.5);

  const std::vector goals = {0.0, 2.0, 1.0, 3.0};
  const std::vector shots = {10.0, 10.0, 10.0, 10.0};
  const Lab::Estimate conversion = Lab::ratioEstimate(goals, shots);
  EXPECT_DOUBLE_EQ(conversion.value, 0.15);
  EXPECT_LT(conversion.low, 0.15);
  EXPECT_GT(conversion.high, 0.15);

  const std::vector zeros = {0.0, 0.0};
  EXPECT_FALSE(Lab::ratioEstimate(zeros, zeros).valid())
      << "a zero denominator has no ratio";

  const std::vector sample = {5.0, 1.0, 4.0, 2.0, 3.0};
  const Lab::Estimate median = Lab::medianEstimate(sample);
  EXPECT_DOUBLE_EQ(median.value, 3.0);
  EXPECT_LE(median.low, 3.0);
  EXPECT_GE(median.high, 3.0);
}

TEST(LabStats, BootstrapIsDeterministic)
{
  const std::vector<double> values = {0, 1, 1, 2, 2, 2, 3, 3, 4, 6};
  const auto mean = [&values](std::span<const std::size_t> indices)
  {
    double sum = 0.0;
    for (const std::size_t i : indices) sum += values[i];
    return sum / static_cast<double>(indices.size());
  };
  const Lab::Estimate first = Lab::bootstrapEstimate(values.size(), mean, 7);
  const Lab::Estimate again = Lab::bootstrapEstimate(values.size(), mean, 7);
  EXPECT_EQ(first, again);
  EXPECT_DOUBLE_EQ(first.value, 2.4);
  EXPECT_LT(first.low, 2.4);
  EXPECT_GT(first.high, 2.4);
}

TEST(LabStats, BandClassification)
{
  const Lab::Band band{2.5, 3.3};
  Lab::Estimate inside = exact(2.8);
  EXPECT_EQ(Lab::classify(inside, band), Lab::Verdict::Pass);

  Lab::Estimate nearMiss = exact(2.45);
  nearMiss.low = 2.3;
  nearMiss.high = 2.6;
  EXPECT_EQ(Lab::classify(nearMiss, band), Lab::Verdict::Warn);

  Lab::Estimate far = exact(1.9);
  far.low = 1.8;
  far.high = 2.0;
  EXPECT_EQ(Lab::classify(far, band), Lab::Verdict::Fail);

  EXPECT_EQ(Lab::classify(inside, std::nullopt), Lab::Verdict::Info);
  EXPECT_EQ(Lab::classify(Lab::Estimate{}, band), Lab::Verdict::NotMeasured);
}

TEST(LabReport, TargetTableCoversEverySpecRow)
{
  std::set<std::string> ids;
  for (const Lab::TargetSpec& spec : Lab::targetTable())
  {
    EXPECT_TRUE(ids.insert(std::string(spec.id)).second)
        << "duplicate id " << spec.id;
    if (spec.band) EXPECT_LE(spec.band->low, spec.band->high) << spec.id;
    if (spec.metric.empty())
      EXPECT_FALSE(spec.note.empty())
          << spec.id << " has no extractor and no reason";
  }
  // Keep in sync with spec.md "Realism Requirements and Calibration Targets".
  for (int i = 1; i <= 43; ++i)
    EXPECT_TRUE(ids.contains(std::format("CT-M{:02}", i))) << "CT-M" << i;
  for (int i = 1; i <= 32; ++i)
    EXPECT_TRUE(ids.contains(std::format("CT-W{:02}", i))) << "CT-W" << i;
}

TEST(LabReport, PlantedMiscalibrationFailsTheGoalsGate)
{
  std::map<std::string, Lab::Estimate> metrics;
  metrics["goals_per_match"] = exact(2.8);
  Lab::LabReport good;
  good.rows = Lab::evaluateTargets(metrics, MATCH_SCOPES);
  EXPECT_EQ(rowById(good.rows, "CT-M13").verdict, Lab::Verdict::Pass);
  EXPECT_EQ(good.exitCode(), Lab::ExitCode::OK);

  metrics["goals_per_match"] = exact(1.4);
  Lab::LabReport bad;
  bad.rows = Lab::evaluateTargets(metrics, MATCH_SCOPES);
  EXPECT_EQ(rowById(bad.rows, "CT-M13").verdict, Lab::Verdict::Fail);
  EXPECT_EQ(bad.summary().gate_fail, 1);
  EXPECT_EQ(bad.exitCode(), Lab::ExitCode::GATE_FAILED);

  // Missing extractors are listed, never silently dropped or failed.
  EXPECT_EQ(rowById(bad.rows, "CT-M01").verdict, Lab::Verdict::NotMeasured);
  EXPECT_THROW(rowById(bad.rows, "CT-W05"), std::out_of_range)
      << "world targets are not part of a match report";
}

TEST(LabReport, MonitorPassRateDrivesExitCode)
{
  std::map<std::string, Lab::Estimate> metrics;
  metrics["goals_per_match"] = exact(2.8);
  metrics["offsides_per_team"] = exact(5.0);  // CT-M39 Monitor, far off.
  Lab::LabReport report;
  report.rows = Lab::evaluateTargets(metrics, MATCH_SCOPES);
  EXPECT_EQ(report.summary().monitor_evaluated, 1);
  EXPECT_EQ(report.exitCode(), Lab::ExitCode::MONITOR_BELOW_THRESHOLD);
}

TEST(LabReport, JsonRoundTripPreservesTheReport)
{
  std::map<std::string, Lab::Estimate> metrics;
  metrics["goals_per_match"] = Lab::meanEstimate(std::vector{1.0, 3.0, 2.0});
  metrics["draw_share"] = Lab::proportionEstimate(26, 100);
  Lab::LabReport report;
  report.mode = "matches";
  report.seed = 42;
  report.samples = 100;
  report.threads = 2;
  report.wall_seconds = 1.5;
  report.ms_per_match = 15.0;
  report.parameters["pairing"] = "fixed | 65 v 65";
  report.rows = Lab::evaluateTargets(metrics, MATCH_SCOPES);
  report.tables.emplace_back("Top scorelines",
                             std::vector<std::string>{"Score", "Share"},
                             std::vector<std::vector<std::string>>{
                                 {"1|1", "12.0%"}, {"1-0", "10.0%"}});
  report.notes.push_back("note");

  const std::string text = Lab::toJson(report).dump();
  const Lab::LabReport parsed =
      Lab::reportFromJson(nlohmann::json::parse(text));
  EXPECT_EQ(parsed, report);
  EXPECT_EQ(Lab::toJson(parsed).dump(), text);

  const nlohmann::json json = nlohmann::json::parse(text);
  EXPECT_EQ(json.at("schema_version").get<int>(), 1);
  EXPECT_EQ(json.at("summary").at("exit_code").get<int>(), report.exitCode());

  const std::string markdown = Lab::toMarkdown(report);
  EXPECT_NE(markdown.find("## Gate targets"), std::string::npos);
  EXPECT_NE(markdown.find("pairing: fixed | 65 v 65"), std::string::npos);
  EXPECT_NE(markdown.find("| 1\\|1 |"), std::string::npos)
      << "pipes in table cells are escaped";

  nlohmann::json broken = json;
  broken["rows"][0]["verdict"] = "MAYBE";
  EXPECT_THROW(Lab::reportFromJson(broken), nlohmann::json::exception);
}

TEST(LabRunner, ReportSampleCountsLateGoalsAndTiming)
{
  MatchReport report;
  report.home_goals = 2;
  report.away_goals = 1;
  report.home_stats.shots = 10;
  report.away_stats.shots = 8;
  report.events = {{.minute = 10, .kind = MatchEventKind::GOAL, .home = true},
                   {.minute = 47,
                    .added_minute = 2,
                    .kind = MatchEventKind::GOAL,
                    .home = false},
                   {.minute = 92,
                    .added_minute = 2,
                    .kind = MatchEventKind::OWN_GOAL,
                    .home = false}};
  const Lab::MatchSample sample = Lab::sampleFromReport(report);
  EXPECT_FALSE(sample.detailed);
  EXPECT_EQ(sample.totalGoals(), 3);
  EXPECT_EQ(sample.own_goals, 1);
  EXPECT_EQ(sample.late_goals, 1);
  EXPECT_EQ(sample.goal_bins[0], 1);
  EXPECT_EQ(sample.goal_bins[2], 1) << "45+2 is first-half added time";
  EXPECT_EQ(sample.goal_bins[5], 1);

  const auto metrics = Lab::matchMetrics(std::vector{sample});
  EXPECT_DOUBLE_EQ(metrics.at("goals_per_match").value, 3.0);
  EXPECT_DOUBLE_EQ(metrics.at("goals_per_shot").value, 2.0 / 18.0);
  EXPECT_FALSE(metrics.contains("penalties_per_match"))
      << "engine-only metrics need detailed samples";
}

TEST(LabRunner, TacticJobsPairEverySeedWithSwappedVenues)
{
  constexpr int SEEDS = 3;
  const std::vector<Lab::MatchJob> jobs = Lab::tacticJobs(9, SEEDS, 65.0f);
  const std::size_t presets = Lab::TACTIC_PRESETS.size();
  ASSERT_EQ(jobs.size(), presets * (presets - 1) / 2 * SEEDS * 2);
  for (std::size_t i = 0; i < jobs.size(); i += 2)
  {
    EXPECT_EQ(jobs[i].seed, jobs[i + 1].seed);
    const auto first = Lab::tacticPresetsOf(i, SEEDS);
    const auto second = Lab::tacticPresetsOf(i + 1, SEEDS);
    EXPECT_EQ(first[0], second[1]);
    EXPECT_EQ(first[1], second[0]);
    EXPECT_NE(first[0], first[1]);
    EXPECT_FLOAT_EQ(jobs[i].home.sliders.pressing,
                    Lab::TACTIC_PRESETS[first[0]].sliders.pressing);
  }
}

// Tiny real run: every match finishes, metrics exist and the output does not
// depend on the thread count.
TEST(LabRunner, SmokeRunIsThreadCountInvariant)
{
  constexpr std::size_t MATCHES = 20;
  std::vector<Lab::MatchJob> jobs(MATCHES);
  for (std::size_t i = 0; i < MATCHES; ++i)
  {
    jobs[i].seed = Lab::matchSeed(2026, i);
    jobs[i].home.rating = 60.0f + static_cast<float>(i % 5) * 4.0f;
  }
  const StatsConfig config = Lab::loadStatsConfig();
  const auto serial = Lab::simulateMatches(jobs, config, 1);
  const auto parallel = Lab::simulateMatches(jobs, config, 3);
  ASSERT_EQ(serial.size(), MATCHES);

  const auto metrics = Lab::matchMetrics(serial);
  ASSERT_TRUE(metrics.contains("goals_per_match"));
  EXPECT_TRUE(metrics.at("goals_per_match").valid());
  EXPECT_TRUE(metrics.contains("match_length_minutes"));
  EXPECT_GT(metrics.at("match_length_minutes").value, 89.0);

  Lab::LabReport a;
  a.rows = Lab::evaluateTargets(metrics, MATCH_SCOPES);
  a.tables = Lab::matchTables(serial);
  Lab::LabReport b;
  b.rows = Lab::evaluateTargets(Lab::matchMetrics(parallel), MATCH_SCOPES);
  b.tables = Lab::matchTables(parallel);
  EXPECT_EQ(Lab::toJson(a).dump(), Lab::toJson(b).dump());
}

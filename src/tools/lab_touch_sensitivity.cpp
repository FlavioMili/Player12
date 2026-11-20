// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/lab_touch_sensitivity.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "global/stats_config.h"
#include "model/match_body.h"
#include "model/match_brain.h"
#include "model/match_first_touch_knobs.h"
#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_setup.h"

namespace
{
constexpr const char* DRILL_NAME = "First touch";
constexpr int INVALID_OPTIONS = 3;

/** The outcomes compared, in report order: the first two rank. */
struct Outcome
{
  const char* metric;
  const char* label;
  /** A share of runs (0-1), shown as a percentage. */
  bool rate;
};
constexpr std::array<Outcome, 5> OUTCOMES{{
    {.metric = "Lost", .label = "lost", .rate = true},
    {.metric = "Heavy", .label = "heavy", .rate = true},
    {.metric = "Abs weight error (m/s)",
     .label = "abs weight error m/s",
     .rate = false},
    {.metric = "Abs angle error (deg)",
     .label = "abs angle error deg",
     .rate = false},
    {.metric = "Time to collect (s)", .label = "collect s", .rate = false},
}};
constexpr std::size_t OUTCOME_COUNT = OUTCOMES.size();

/** Means of the outcomes over one cell's runs. */
struct Cell
{
  std::array<double, OUTCOME_COUNT> sum{};
  std::array<int, OUTCOME_COUNT> count{};
  int runs = 0;

  [[nodiscard]] double mean(std::size_t outcome) const
  {
    return (count[outcome] != 0) ? sum[outcome] / count[outcome] : 0.0;
  }
};

/** One variable: its two values (absolute, or around each rating for First
 * Touch). */
struct Variable
{
  std::string name;
  std::string group;
  float low = 0.0F;
  float high = 0.0F;
  /** First Touch: low and high are offsets from each cell's rating. */
  bool aroundRating = false;
};

/** Its largest effect on one outcome, and where. */
struct Effect
{
  double delta = 0.0;
  std::string where;
};

std::vector<std::string> splitList(std::string_view text, char separator)
{
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= text.size())
  {
    const std::size_t end = std::min(text.find(separator, start), text.size());
    if (end > start)
    {
      parts.emplace_back(text.substr(start, end - start));
    }
    start = end + 1;
  }
  return parts;
}

std::optional<float> toFloat(std::string_view text)
{
  float value = 0.0F;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size())
  {
    return std::nullopt;
  }
  return value;
}

std::filesystem::path freshFolder()
{
  const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  return std::filesystem::temp_directory_path() /
         std::format("fm-lab-touch-{}", stamp);
}

struct Options
{
  int repeats = 100;
  std::vector<float> ratings{30.0F, 50.0F, 70.0F, 90.0F};
  std::vector<std::string> situations;
  std::vector<std::string> only;
  float step = 0.5F;
  std::map<std::string, std::pair<float, float>> ranges;
  std::uint32_t seed = 1;
  std::optional<std::filesystem::path> outDir;
  /** The brains and bodies both sides play with (the new first touch is
   * the Binturong body's, so Binturong unless told otherwise). */
  std::array<BrainId, 2> brains{BrainId::BINTURONG, BrainId::BINTURONG};
  std::array<BodyId, 2> bodies{BodyId::BINTURONG, BodyId::BINTURONG};
};

std::optional<Options> parseOptions(const std::vector<std::string>& arguments)
{
  Options options;
  for (std::size_t index = 0; index < arguments.size(); ++index)
  {
    const std::string& option = arguments[index];
    if (index + 1 >= arguments.size())
    {
      std::cerr << std::format("touch-sensitivity: '{}' needs a value\n",
                               option);
      return std::nullopt;
    }
    const std::string& value = arguments[++index];
    const auto number = toFloat(value);
    if (option == "--repeats" && number && *number >= 1.0F)
    {
      options.repeats = static_cast<int>(*number);
    }
    else if (option == "--step" && number && *number > 0.0F)
    {
      options.step = *number;
    }
    else if (option == "--seed" && number && *number >= 0.0F)
    {
      options.seed = static_cast<std::uint32_t>(*number);
    }
    else if (option == "--out-dir")
    {
      options.outDir = value;
    }
    else if (option == "--brains" && parseBrainPair(value))
    {
      options.brains = *parseBrainPair(value);
    }
    else if (option == "--bodies" && parseBodyPair(value))
    {
      options.bodies = *parseBodyPair(value);
    }
    else if (option == "--situations")
    {
      options.situations =
          value == "all" ? std::vector<std::string>{} : splitList(value, ';');
    }
    else if (option == "--only")
    {
      options.only = splitList(value, ';');
    }
    else if (option == "--ratings")
    {
      options.ratings.clear();
      for (const std::string& part : splitList(value, ','))
      {
        const auto rating = toFloat(part);
        if (!rating)
        {
          return std::nullopt;
        }
        options.ratings.push_back(*rating);
      }
    }
    else if (option == "--range")
    {
      const std::size_t equals = value.find('=');
      const std::size_t colon = value.find(':', equals);
      const auto low = equals != std::string::npos && colon != std::string::npos
                           ? toFloat(std::string_view(value).substr(
                                 equals + 1, colon - equals - 1))
                           : std::nullopt;
      const auto high = colon != std::string::npos
                            ? toFloat(std::string_view(value).substr(colon + 1))
                            : std::nullopt;
      if (!low || !high)
      {
        std::cerr << std::format(
            "touch-sensitivity: --range \"{}\": expected Name=low:high\n",
            value);
        return std::nullopt;
      }
      options.ranges[value.substr(0, equals)] = {*low, *high};
    }
    else
    {
      std::cerr << std::format(
          "touch-sensitivity: unknown or invalid '{} {}'\n", option, value);
      return std::nullopt;
    }
  }
  if (options.ratings.empty())
  {
    return std::nullopt;
  }
  return options;
}

/** The variables: the receiver's own, then every knob, low and high. */
std::vector<Variable> makeVariables(const Options& options)
{
  std::vector<Variable> list{
      {.name = "First Touch",
       .group = "Player",
       .low = -20.0F,
       .high = 20.0F,
       .aroundRating = true},
      {.name = "Pace", .group = "Player", .low = 40.0F, .high = 90.0F},
      {.name = "Condition", .group = "Player", .low = 60.0F, .high = 100.0F},
      {.name = "Foot", .group = "Player", .low = 0.0F, .high = 1.0F},
  };
  for (const TouchKnobInfo& info : TOUCH_KNOBS)
  {
    Variable variable{.name = std::string(info.name),
                      .group = std::string(info.group)};
    const float value = info.defaultValue;
    if (value != 0.0F)
    {
      variable.low =
          std::clamp(value * (1.0F - options.step), info.min, info.max);
      variable.high =
          std::clamp(value * (1.0F + options.step), info.min, info.max);
    }
    else
    {
      // Nothing to scale: from nothing up to the step's share of its range.
      variable.low = value;
      variable.high = std::clamp(value + (options.step * (info.max - info.min)),
                                 info.min, info.max);
    }
    list.push_back(variable);
  }
  for (Variable& variable : list)
  {
    if (const auto found = options.ranges.find(variable.name);
        found != options.ranges.end())
    {
      variable.low = found->second.first;
      variable.high = found->second.second;
      variable.aroundRating = false;
    }
  }
  if (!options.only.empty())
  {
    std::erase_if(list,
                  [&](const Variable& variable)
                  {
                    return std::ranges::find(options.only, variable.name) ==
                           options.only.end();
                  });
  }
  return list;
}

std::string percent(double share)
{
  return std::format("{:.1f}", share * 100.0);
}

std::string shown(const Outcome& outcome, double value)
{
  return outcome.rate ? percent(value) : std::format("{:.2f}", value);
}
}  // namespace

int runTouchSensitivity(const StatsConfig& config,
                        const std::vector<std::string>& arguments)
{
  const auto parsed = parseOptions(arguments);
  if (!parsed)
  {
    return INVALID_OPTIONS;
  }
  const Options& options = *parsed;

  // The drill plays with the sandbox's brains and bodies.
  sandboxBrains() = options.brains;
  sandboxBodies() = options.bodies;

  auto drills = makeDrills();
  Drill* drill = nullptr;
  for (const auto& candidate : drills)
  {
    if (std::string_view(candidate->name()) == DRILL_NAME)
    {
      drill = candidate.get();
    }
  }
  if (drill == nullptr)
  {
    std::cerr << "touch-sensitivity: no First touch drill\n";
    return INVALID_OPTIONS;
  }
  // Every setting as the drill starts, put back before each run.
  std::map<std::string, float*> settings;
  std::map<std::string, float> defaults;
  for (const DrillParameter& parameter : drill->parameters())
  {
    if (parameter.value != nullptr)
    {
      settings[parameter.name] = parameter.value;
      defaults[parameter.name] = *parameter.value;
    }
  }

  std::vector<DrillPreset> situations = drill->presets();
  if (!options.situations.empty())
  {
    std::erase_if(situations,
                  [&](const DrillPreset& preset)
                  {
                    return std::ranges::find(options.situations, preset.name) ==
                           options.situations.end();
                  });
    if (situations.size() != options.situations.size())
    {
      std::cerr << "touch-sensitivity: unknown situation; situations:";
      for (const DrillPreset& preset : drill->presets())
      {
        std::cerr << " \"" << preset.name << '"';
      }
      std::cerr << '\n';
      return INVALID_OPTIONS;
    }
  }
  const std::vector<Variable> variables = makeVariables(options);
  for (const Variable& variable : variables)
  {
    if (!settings.contains(variable.name))
    {
      std::cerr << std::format("touch-sensitivity: no setting \"{}\"\n",
                               variable.name);
      return INVALID_OPTIONS;
    }
  }
  if (!options.only.empty() && variables.size() != options.only.size())
  {
    std::cerr
        << "touch-sensitivity: --only names a variable that does not exist\n";
    return INVALID_OPTIONS;
  }

  const std::size_t cellsPerSet = situations.size() * options.ratings.size();
  const std::size_t totalRuns = cellsPerSet * (1 + 2 * variables.size()) *
                                static_cast<std::size_t>(options.repeats);
  std::cout << std::format(
      "touch-sensitivity: {} situations x {} ratings, {} variables low and "
      "high, {} runs per cell: {} runs\n",
      situations.size(), options.ratings.size(), variables.size(),
      options.repeats, totalRuns);
  const auto started = std::chrono::steady_clock::now();
  std::size_t done = 0;

  // One cell: a situation at a rating, with some settings changed.
  const auto runCell = [&](const DrillPreset& situation, float rating,
                           const std::map<std::string, float>& changes)
  {
    Cell cell;
    for (int repeat = 0; repeat < options.repeats; ++repeat)
    {
      for (const auto& [name, value] : defaults)
      {
        *settings.at(name) = value;
      }
      (void)drill->applyPreset(situation.name);
      *settings.at("First Touch") = rating;
      for (const auto& [name, value] : changes)
      {
        *settings.at(name) = value;
      }
      drill->seed = options.seed + static_cast<std::uint32_t>(repeat);
      {
        DrillRun run(*drill, config, false);
        while (run.step())
        {
        }
      }
      ++cell.runs;
      for (const DrillMetric& metric : drill->metrics())
      {
        for (std::size_t outcome = 0; outcome < OUTCOME_COUNT; ++outcome)
        {
          if (metric.name == OUTCOMES[outcome].metric)
          {
            cell.sum[outcome] += metric.value;
            ++cell.count[outcome];
          }
        }
      }
      if (++done % 20000 == 0)
      {
        std::cerr << std::format("  {} / {}\r", done, totalRuns);
      }
    }
    return cell;
  };

  // [situation][rating]
  std::vector<std::vector<Cell>> baseline(situations.size());
  // [variable][low/high][situation][rating]
  std::vector<std::array<std::vector<std::vector<Cell>>, 2>> varied(
      variables.size());
  for (std::size_t s = 0; s < situations.size(); ++s)
  {
    for (const float rating : options.ratings)
    {
      baseline[s].push_back(runCell(situations[s], rating, {}));
    }
  }
  for (std::size_t v = 0; v < variables.size(); ++v)
  {
    for (int level = 0; level < 2; ++level)
    {
      auto& table = varied[v][static_cast<std::size_t>(level)];
      table.resize(situations.size());
      for (std::size_t s = 0; s < situations.size(); ++s)
      {
        for (const float rating : options.ratings)
        {
          const Variable& variable = variables[v];
          float value = level == 0 ? variable.low : variable.high;
          if (variable.aroundRating)
          {
            value = std::clamp(rating + value, 1.0F, 100.0F);
          }
          table[s].push_back(
              runCell(situations[s], rating, {{variable.name, value}}));
        }
      }
    }
  }
  for (const auto& [name, value] : defaults)
  {
    *settings.at(name) = value;
  }
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();

  // Each variable's largest effect on each outcome, and where.
  struct Ranked
  {
    std::size_t variable = 0;
    std::array<Effect, OUTCOME_COUNT> effects;
    /** Whether its largest lost / heavy effects clear the noise (two
     * standard errors of a difference at the baseline's rate). */
    std::array<bool, 2> clear{};
  };
  std::vector<Ranked> ranked;
  for (std::size_t v = 0; v < variables.size(); ++v)
  {
    Ranked entry;
    entry.variable = v;
    for (std::size_t s = 0; s < situations.size(); ++s)
    {
      for (std::size_t r = 0; r < options.ratings.size(); ++r)
      {
        for (int level = 0; level < 2; ++level)
        {
          const Cell& base = baseline[s][r];
          const Cell& cell = varied[v][static_cast<std::size_t>(level)][s][r];
          for (std::size_t outcome = 0; outcome < OUTCOME_COUNT; ++outcome)
          {
            const double delta = cell.mean(outcome) - base.mean(outcome);
            Effect& effect = entry.effects[outcome];
            if (std::abs(delta) <= std::abs(effect.delta))
            {
              continue;
            }
            effect.delta = delta;
            effect.where =
                std::format("{} @{:.0f} ({})", situations[s].name,
                            options.ratings[r], level == 0 ? "low" : "high");
            if (outcome < 2)
            {
              const double rate =
                  std::max({base.mean(outcome), cell.mean(outcome),
                            1.0 / std::max(base.runs, 1)});
              const double error =
                  std::sqrt(2.0 * rate * (1.0 - rate) / std::max(base.runs, 1));
              entry.clear[outcome] = std::abs(delta) > 2.0 * error;
            }
          }
        }
      }
    }
    ranked.push_back(entry);
  }
  // Ranked by the larger of the two (lost, heavy), then by the larger of
  // the two errors' changes, each as a share of its baseline average (so a
  // degree and a percentage weigh alike).
  std::array<double, 2> errorScale{};
  for (std::size_t e = 0; e < 2; ++e)
  {
    double sum = 0.0;
    int count = 0;
    for (const auto& row : baseline)
    {
      for (const Cell& cell : row)
      {
        sum += cell.mean(2 + e);
        ++count;
      }
    }
    errorScale[e] = (count != 0) && sum > 0.0 ? sum / count : 1.0;
  }
  const auto errorChange = [&](const Ranked& entry)
  {
    return std::max(std::abs(entry.effects[2].delta) / errorScale[0],
                    std::abs(entry.effects[3].delta) / errorScale[1]);
  };
  std::ranges::sort(
      ranked,
      [&](const Ranked& a, const Ranked& b)
      {
        const double first = std::max(std::abs(a.effects[0].delta),
                                      std::abs(a.effects[1].delta));
        const double second = std::max(std::abs(b.effects[0].delta),
                                       std::abs(b.effects[1].delta));
        if (first != second)
        {
          return first > second;
        }
        return errorChange(a) > errorChange(b);
      });
  // Each variable's place by lost alone and by heavy alone.
  std::vector<std::array<int, 2>> places(variables.size());
  for (std::size_t outcome = 0; outcome < 2; ++outcome)
  {
    std::vector<std::size_t> order(ranked.size());
    for (std::size_t i = 0; i < order.size(); ++i)
    {
      order[i] = i;
    }
    std::ranges::stable_sort(
        order,
        [&](std::size_t a, std::size_t b)
        {
          return std::abs(ranked[a].effects[outcome].delta) >
                 std::abs(ranked[b].effects[outcome].delta);
        });
    for (std::size_t place = 0; place < order.size(); ++place)
    {
      places[ranked[order[place]].variable][outcome] =
          static_cast<int>(place) + 1;
    }
  }

  // --- Output -------------------------------------------------------------
  const std::filesystem::path folder = options.outDir.value_or(freshFolder());
  std::filesystem::create_directories(folder);
  {
    std::ofstream cells(folder / "cells.csv");
    cells << "variable,level,value,situation,rating,runs";
    for (const Outcome& outcome : OUTCOMES)
    {
      cells << ',' << outcome.metric;
    }
    cells << '\n';
    const auto row = [&](std::string_view variable, std::string_view level,
                         float value, std::size_t s, std::size_t r,
                         const Cell& cell)
    {
      cells << std::format("\"{}\",{},{},\"{}\",{},{}", variable, level, value,
                           situations[s].name, options.ratings[r], cell.runs);
      for (std::size_t outcome = 0; outcome < OUTCOME_COUNT; ++outcome)
      {
        cells << std::format(",{:.5f}", cell.mean(outcome));
      }
      cells << '\n';
    };
    for (std::size_t s = 0; s < situations.size(); ++s)
    {
      for (std::size_t r = 0; r < options.ratings.size(); ++r)
      {
        row("(baseline)", "base", options.ratings[r], s, r, baseline[s][r]);
      }
    }
    for (std::size_t v = 0; v < variables.size(); ++v)
    {
      for (int level = 0; level < 2; ++level)
      {
        for (std::size_t s = 0; s < situations.size(); ++s)
        {
          for (std::size_t r = 0; r < options.ratings.size(); ++r)
          {
            const Variable& variable = variables[v];
            float value = level == 0 ? variable.low : variable.high;
            if (variable.aroundRating)
            {
              value = std::clamp(options.ratings[r] + value, 1.0F, 100.0F);
            }
            row(variable.name, level == 0 ? "low" : "high", value, s, r,
                varied[v][static_cast<std::size_t>(level)][s][r]);
          }
        }
      }
    }
  }
  const auto rangeText = [&](const Variable& variable)
  {
    return variable.aroundRating
               ? std::format("rating {:+.0f} / {:+.0f}", variable.low,
                             variable.high)
               : std::format("{:g} / {:g}", variable.low, variable.high);
  };
  {
    std::ofstream csv(folder / "ranking.csv");
    csv << "variable,group,low,high,place by lost,place by heavy";
    for (const Outcome& outcome : OUTCOMES)
    {
      csv << ",largest change in " << outcome.metric << ",where";
    }
    csv << '\n';
    for (const Ranked& entry : ranked)
    {
      const Variable& variable = variables[entry.variable];
      csv << std::format("\"{}\",{},{},{},{},{}", variable.name, variable.group,
                         variable.low, variable.high, places[entry.variable][0],
                         places[entry.variable][1]);
      for (const Effect& effect : entry.effects)
      {
        csv << std::format(",{:.5f},\"{}\"", effect.delta, effect.where);
      }
      csv << '\n';
    }
  }
  std::string report = std::format(
      "# First touch sensitivity\n\n{} situations x {} First Touch ratings "
      "({}), "
      "{} variables each set low and high, {} runs per cell on seeds {}-{}: "
      "{} runs in {:.1f} s. Knob steps: x(1 -/+ {:g}), within each knob's "
      "range unless --range set it.\n\n",
      situations.size(), options.ratings.size(),
      [&]
      {
        std::string text;
        for (const float rating : options.ratings)
        {
          text += (text.empty() ? "" : ", ") + std::format("{:.0f}", rating);
        }
        return text;
      }(),
      variables.size(), options.repeats, options.seed,
      options.seed + static_cast<std::uint32_t>(options.repeats) - 1, totalRuns,
      seconds, options.step);
  report += "## Baseline (as tuned)\n\n| Situation | First Touch |";
  for (const Outcome& outcome : OUTCOMES)
  {
    report += std::format(" {} |", outcome.label);
  }
  report += "\n|---|---|";
  for (std::size_t i = 0; i < OUTCOME_COUNT; ++i)
  {
    report += "---|";
  }
  report += '\n';
  for (std::size_t s = 0; s < situations.size(); ++s)
  {
    for (std::size_t r = 0; r < options.ratings.size(); ++r)
    {
      report += std::format("| {} | {:.0f} |", situations[s].name,
                            options.ratings[r]);
      for (std::size_t outcome = 0; outcome < OUTCOME_COUNT; ++outcome)
      {
        report += std::format(
            " {} |", shown(OUTCOMES[outcome], baseline[s][r].mean(outcome)));
      }
      report += '\n';
    }
  }
  report +=
      "\n## Ranking\n\nEach variable's largest change from the baseline in any "
      "situation and rating, ordered by the larger of its lost and heavy "
      "changes, then by its larger error change (each error relative to its "
      "baseline average). Places by lost alone and by heavy "
      "alone are beside it. Lost and heavy are percentage points; ~ marks a "
      "change within two standard errors of the noise.\n\n"
      "| # | Variable | Group | Low / high | Place: lost | Place: heavy | "
      "Lost | Heavy | Abs weight error m/s | Abs angle error deg | Collect s | "
      "Largest "
      "lost change at | Largest heavy change at |\n"
      "|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
  for (std::size_t place = 0; place < ranked.size(); ++place)
  {
    const Ranked& entry = ranked[place];
    const Variable& variable = variables[entry.variable];
    const auto rate = [&](std::size_t outcome)
    {
      return std::format("{:+.1f}{}", entry.effects[outcome].delta * 100.0,
                         entry.clear[outcome] ? "" : " ~");
    };
    report += std::format(
        "| {} | {} | {} | {} | {} | {} | {} | {} | {:+.2f} | {:+.2f} | {:+.2f} "
        "| "
        "{} | {} |\n",
        place + 1, variable.name, variable.group, rangeText(variable),
        places[entry.variable][0], places[entry.variable][1], rate(0), rate(1),
        entry.effects[2].delta, entry.effects[3].delta, entry.effects[4].delta,
        entry.effects[0].where.empty() ? "-" : entry.effects[0].where,
        entry.effects[1].where.empty() ? "-" : entry.effects[1].where);
  }
  std::ofstream(folder / "report.md") << report;

  std::cout << std::format("Done in {:.1f} s.\n\n", seconds);
  std::cout << std::format("{:>3}  {:<26} {:>8} {:>8} {:>10} {:>10}\n", "#",
                           "Variable", "lost pp", "heavy pp", "|weight|m/s",
                           "|angle|deg");
  for (std::size_t place = 0; place < ranked.size() && place < 25; ++place)
  {
    const Ranked& entry = ranked[place];
    std::cout << std::format(
        "{:>3}  {:<26} {:>+8.1f} {:>+8.1f} {:>+10.2f} {:>+10.2f}\n", place + 1,
        variables[entry.variable].name.substr(0, 26),
        entry.effects[0].delta * 100.0, entry.effects[1].delta * 100.0,
        entry.effects[2].delta, entry.effects[3].delta);
  }
  std::cout << std::format("\nReport: {}\n", (folder / "report.md").string());
  return 0;
}

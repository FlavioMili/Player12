// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_measure.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <ctime>
#include <format>
#include <fstream>
#include <iostream>
#include <string_view>

#include "model/match_tuning.h"
#include "tools/lab_trace.h"

namespace
{
using Clock = std::chrono::steady_clock;

constexpr std::size_t TIMESTAMP_BYTES = 32;
constexpr float DEFAULT_TRACE_SECONDS = 10.0F;
/** Table columns: the swept parameter and the "by" parameter. */
constexpr std::size_t SWEEP_COLUMN = 14;
constexpr std::size_t BY_COLUMN = 12;
/** A spread is shown once it reaches 0.01 at two decimals. */
constexpr double SHOWN_SPREAD = 0.005;

std::optional<DrillParameter> findParameter(Drill& drill, std::string_view name)
{
  for (const DrillParameter& parameter : drill.parameters())
  {
    if (name == parameter.name)
    {
      return parameter;
    }
  }
  return std::nullopt;
}

bool sameName(std::string_view a, std::string_view b)
{
  if (a.size() != b.size())
  {
    return false;
  }
  for (std::size_t index = 0; index < a.size(); ++index)
  {
    if (std::tolower(static_cast<unsigned char>(a[index])) !=
        std::tolower(static_cast<unsigned char>(b[index])))
    {
      return false;
    }
  }
  return true;
}

/** The parameter whose name matches, ignoring case (its exact name). */
std::optional<DrillParameter> lookUp(Drill& drill, std::string_view name)
{
  for (const DrillParameter& parameter : drill.parameters())
  {
    if (sameName(name, parameter.name))
    {
      return parameter;
    }
  }
  return std::nullopt;
}

std::optional<float> toFloat(std::string_view text)
{
  while (!text.empty() && text.front() == ' ')
  {
    text.remove_prefix(1);
  }
  while (!text.empty() && text.back() == ' ')
  {
    text.remove_suffix(1);
  }
  float value = 0.0F;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc() || end != text.data() + text.size())
  {
    return std::nullopt;
  }
  return value;
}

std::string csvField(const std::string& text)
{
  if (text.find_first_of(",\"\n") == std::string::npos)
  {
    return text;
  }
  std::string quoted = "\"";
  for (const char c : text)
  {
    if (c == '"')
    {
      quoted += '"';
    }
    quoted += c;
  }
  return quoted + "\"";
}

std::string timestamp()
{
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  std::array<char, TIMESTAMP_BYTES> text{};
  std::strftime(text.data(), text.size(), "%Y-%m-%d %H%M%S", &local);
  return text.data();
}
}  // namespace

std::vector<float> headlessChoices(const DrillParameter& parameter)
{
  std::vector<float> values;
  for (std::size_t index = 0; index < parameter.choices.size(); ++index)
  {
    if (std::string_view(parameter.choices[index]) != "You")
    {
      values.push_back(static_cast<float>(index));
    }
  }
  return values;
}

std::vector<float> spreadValues(float from, float to, int count, bool integer)
{
  std::vector<float> values;
  count = std::max(1, count);
  for (int index = 0; index < count; ++index)
  {
    float value = count == 1 ? from
                             : from + ((to - from) * static_cast<float>(index) /
                                       static_cast<float>(count - 1));
    if (integer)
    {
      value = std::round(value);
    }
    if (values.empty() || values.back() != value)
    {
      values.push_back(value);
    }
  }
  return values;
}

std::optional<MeasureAxis> parseMeasureAxis(Drill& drill,
                                            const std::string& text,
                                            std::string& error)
{
  const std::size_t equals = text.find('=');
  const std::string name = text.substr(0, equals);
  const auto parameter = lookUp(drill, name);
  if (!parameter)
  {
    error = std::format("{} has no setting \"{}\"", drill.name(), name);
    return std::nullopt;
  }
  MeasureAxis axis{.parameter = parameter->name, .values = {}};
  if (equals == std::string::npos)
  {
    if (parameter->choices.empty())
    {
      error = std::format(R"(give values for "{}", e.g. "{}=40:90:6")",
                          parameter->name, parameter->name);
      return std::nullopt;
    }
    axis.values = headlessChoices(*parameter);
    return axis;
  }
  const std::string values = text.substr(equals + 1);
  if (values.find(':') != std::string::npos)
  {
    // from:to:count
    std::vector<float> parts;
    std::size_t start = 0;
    while (start <= values.size())
    {
      const std::size_t end = std::min(values.find(':', start), values.size());
      const auto value =
          toFloat(std::string_view(values).substr(start, end - start));
      if (!value)
      {
        break;
      }
      parts.push_back(*value);
      start = end + 1;
    }
    if (parts.size() != 3 || parts[2] < 1.0F)
    {
      error = std::format("\"{}\": expected from:to:count", values);
      return std::nullopt;
    }
    axis.values =
        spreadValues(parts[0], parts[1], static_cast<int>(parts[2]),
                     parameter->integer || !parameter->choices.empty());
    return axis;
  }
  std::size_t start = 0;
  while (start <= values.size())
  {
    const std::size_t end = std::min(values.find(',', start), values.size());
    const auto value =
        toFloat(std::string_view(values).substr(start, end - start));
    if (!value)
    {
      error = std::format("\"{}\": expected numbers", values);
      return std::nullopt;
    }
    axis.values.push_back(*value);
    start = end + 1;
  }
  return axis;
}

std::filesystem::path measureFolder()
{
  std::filesystem::path base = std::filesystem::current_path();
  if (const char* documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS))
  {
    base = std::filesystem::path(documents);
  }
  return base / "Player12 drill results";
}

std::filesystem::path measurePath(const std::string& drill,
                                  const std::string& sweep)
{
  // Names may hold characters a file name cannot ("Least turn (°)" is fine,
  // a slash is not).
  std::string name = std::format("{} by {} {}.csv", drill, sweep, timestamp());
  std::ranges::replace_if(
      name,
      [](char c)
      {
        return std::string_view("/\\:*?\"<>|").find(c) !=
               std::string_view::npos;
      },
      '-');
  return measureFolder() / name;
}

// --- DrillMeasure ------------------------------------------------------------

DrillMeasure::DrillMeasure(Drill& drill_ref, const StatsConfig& config_ref,
                           MeasureSpec spec)
    : drill(drill_ref),
      config(config_ref),
      setup(std::move(spec)),
      savedSeed(drill.seed)
{
  for (const DrillParameter& parameter : drill.parameters())
  {
    if (parameter.value == nullptr)
    {
      continue;
    }
    saved[parameter.name] = *parameter.value;
    if (!parameter.choices.empty())
    {
      choiceNames[parameter.name].assign(parameter.choices.begin(),
                                         parameter.choices.end());
    }
  }
  const auto known = [&](const std::string& name)
  { return saved.contains(name); };
  if (!known(setup.sweep.parameter))
  {
    notes.push_back(
        std::format("Unknown setting \"{}\".", setup.sweep.parameter));
  }
  if (setup.by && !known(setup.by->parameter))
  {
    notes.push_back(
        std::format("Unknown setting \"{}\".", setup.by->parameter));
  }
  // Nobody plays a headless run: "You" becomes the AI.
  if (const auto who = findParameter(drill, "Who runs"))
  {
    const std::vector<float> allowed = headlessChoices(*who);
    const auto headless = [&](float value)
    { return std::ranges::find(allowed, value) != allowed.end(); };
    const bool onAxis = setup.sweep.parameter == "Who runs" ||
                        (setup.by && setup.by->parameter == "Who runs");
    const float path = setup.fixed.contains("Who runs")
                           ? setup.fixed.at("Who runs")
                           : saved.at("Who runs");
    if (!onAxis && !allowed.empty() && !headless(path))
    {
      setup.fixed["Who runs"] = allowed.front();
      notes.emplace_back("\"You\" can't run headless: measured with the AI.");
    }
    for (MeasureAxis* axis : {&setup.sweep, setup.by ? &*setup.by : nullptr})
    {
      if ((axis != nullptr) && axis->parameter == "Who runs")
      {
        std::erase_if(axis->values,
                      [&](float value) { return !headless(value); });
      }
    }
  }
  setup.repeats = std::max(1, setup.repeats);
  const std::size_t groups = setup.by ? setup.by->values.size() : 1;
  const bool unknown =
      std::ranges::any_of(notes, [](const std::string& note)
                          { return note.starts_with("Unknown"); });
  total = unknown ? 0
                  : groups * setup.sweep.values.size() *
                        static_cast<std::size_t>(setup.repeats);
}

DrillMeasure::~DrillMeasure() { restore(); }

void DrillMeasure::restore()
{
  if (restored)
  {
    return;
  }
  restored = true;
  for (const DrillParameter& parameter : drill.parameters())
  {
    if ((parameter.value != nullptr) && saved.contains(parameter.name))
    {
      *parameter.value = saved.at(parameter.name);
    }
  }
  drill.seed = savedSeed;
}

void DrillMeasure::apply(std::size_t index)
{
  const auto repeats = static_cast<std::size_t>(setup.repeats);
  const std::size_t xs = setup.sweep.values.size();
  const std::size_t repeat = index % repeats;
  const std::size_t x = index / repeats % xs;
  const std::size_t group = index / repeats / xs;
  std::map<std::string, float> values = saved;
  for (const auto& [name, value] : setup.fixed)
  {
    values[name] = value;
  }
  values[setup.sweep.parameter] = setup.sweep.values[x];
  if (setup.by)
  {
    values[setup.by->parameter] = setup.by->values[group];
  }
  for (const DrillParameter& parameter : drill.parameters())
  {
    if ((parameter.value != nullptr) && values.contains(parameter.name))
    {
      *parameter.value = values.at(parameter.name);
    }
  }
  drill.seed = setup.firstSeed + static_cast<std::uint32_t>(repeat);
  MeasureRun run;
  run.x = setup.sweep.values[x];
  run.by = setup.by ? setup.by->values[group] : 0.0F;
  run.seed = drill.seed;
  results.push_back(run);
}

bool DrillMeasure::work(double budgetSeconds)
{
  const auto start = Clock::now();
  while (next < total)
  {
    restored = false;
    apply(next);
    {
      DrillRun run(drill, config, false);
      while (run.step())
      {
      }
      results.back().metrics = drill.metrics();
    }
    for (const DrillMetric& metric : results.back().metrics)
    {
      if (std::ranges::find(names, metric.name) == names.end())
      {
        names.push_back(metric.name);
      }
    }
    ++next;
    if (std::chrono::duration<double>(Clock::now() - start).count() >=
        budgetSeconds)
    {
      break;
    }
  }
  elapsed += std::chrono::duration<double>(Clock::now() - start).count();
  if (next >= total)
  {
    restore();
  }
  return next < total;
}

void DrillMeasure::stop()
{
  total = next;
  restore();
}

std::vector<MeasurePoint> DrillMeasure::points() const
{
  std::vector<MeasurePoint> out;
  std::map<std::pair<float, float>, std::size_t> index;
  std::map<std::size_t, std::map<std::string, std::vector<double>>> samples;
  for (const MeasureRun& run : results)
  {
    const auto key = std::pair{run.by, run.x};
    auto found = index.find(key);
    if (found == index.end())
    {
      found = index.emplace(key, out.size()).first;
      out.push_back(
          {.x = run.x, .by = run.by, .runs = 0, .completed = 0, .stats = {}});
    }
    MeasurePoint& point = out[found->second];
    ++point.runs;
    if (!run.metrics.empty())
    {
      ++point.completed;
    }
    for (const DrillMetric& metric : run.metrics)
    {
      samples[found->second][metric.name].push_back(metric.value);
    }
  }
  for (auto& [at, metrics] : samples)
  {
    for (auto& [name, values] : metrics)
    {
      MeasureStat stat;
      stat.count = static_cast<int>(values.size());
      double sum = 0.0;
      for (const double value : values)
      {
        sum += value;
      }
      stat.mean = sum / static_cast<double>(values.size());
      double squares = 0.0;
      for (const double value : values)
      {
        squares += (value - stat.mean) * (value - stat.mean);
      }
      stat.sd =
          values.size() > 1
              ? std::sqrt(squares / static_cast<double>(values.size() - 1))
              : 0.0;
      stat.min = *std::ranges::min_element(values);
      stat.max = *std::ranges::max_element(values);
      out[at].stats[name] = stat;
    }
  }
  return out;
}

std::string DrillMeasure::valueText(const std::string& parameter,
                                    float value) const
{
  if (const auto names_ = choiceNames.find(parameter);
      names_ != choiceNames.end())
  {
    const auto choice = static_cast<std::size_t>(value);
    if (choice < names_->second.size())
    {
      return names_->second[choice];
    }
  }
  return std::format("{:g}", value);
}

std::string DrillMeasure::csv() const
{
  std::string text = csvField(setup.sweep.parameter);
  if (setup.by)
  {
    text += "," + csvField(setup.by->parameter);
  }
  text += ",Seed,Completed";
  for (const std::string& name : names)
  {
    text += "," + csvField(name);
  }
  text += "\n";
  for (const MeasureRun& run : results)
  {
    text += csvField(valueText(setup.sweep.parameter, run.x));
    if (setup.by)
    {
      text += "," + csvField(valueText(setup.by->parameter, run.by));
    }
    text += std::format(",{},{}", run.seed, run.metrics.empty() ? 0 : 1);
    for (const std::string& name : names)
    {
      text += ",";
      for (const DrillMetric& metric : run.metrics)
      {
        if (metric.name == name)
        {
          text += std::format("{:.4f}", metric.value);
        }
      }
    }
    text += "\n";
  }
  return text;
}

// --- Command line ------------------------------------------------------------

int runMeasureCommand(const StatsConfig& config, const std::string& drillName,
                      const std::vector<std::string>& arguments)
{
  auto drills = makeDrills();
  Drill* drill = nullptr;
  for (const auto& candidate : drills)
  {
    if (sameName(candidate->name(), drillName))
    {
      drill = candidate.get();
    }
  }
  if (drill == nullptr)
  {
    std::cerr << "No drill \"" << drillName << "\". Drills:";
    for (const auto& candidate : drills)
    {
      std::cerr << " \"" << candidate->name() << '"';
    }
    std::cerr << '\n';
    return 2;
  }
  MeasureSpec spec;
  std::optional<std::filesystem::path> csvPath;
  bool csvAuto = false;
  std::string error;
  std::optional<std::string> presetName;
  std::optional<std::filesystem::path> tracePath;
  float traceSeconds = DEFAULT_TRACE_SECONDS;
  for (std::size_t index = 0; index < arguments.size(); ++index)
  {
    const std::string& argument = arguments[index];
    const bool hasValue = index + 1 < arguments.size();
    const std::string value = hasValue ? arguments[index + 1] : std::string();
    if (argument == "--sweep" && hasValue)
    {
      const auto axis = parseMeasureAxis(*drill, value, error);
      if (!axis)
      {
        break;
      }
      spec.sweep = *axis;
    }
    else if (argument == "--by" && hasValue)
    {
      spec.by = parseMeasureAxis(*drill, value, error);
      if (!spec.by)
      {
        break;
      }
    }
    else if (argument == "--set" && hasValue)
    {
      const std::size_t equals = value.find('=');
      const auto parameter = lookUp(*drill, value.substr(0, equals));
      const auto number =
          equals == std::string::npos
              ? std::nullopt
              : toFloat(std::string_view(value).substr(equals + 1));
      if (!parameter || !number)
      {
        error = std::format("--set \"{}\": expected Setting=value", value);
        break;
      }
      spec.fixed[parameter->name] = *number;
    }
    else if (argument == "--repeats" && hasValue)
    {
      spec.repeats = static_cast<int>(toFloat(value).value_or(1.0F));
    }
    else if (argument == "--preset" && hasValue)
    {
      presetName = value;
    }
    else if (argument == "--seed" && hasValue)
    {
      spec.firstSeed =
          static_cast<std::uint32_t>(toFloat(value).value_or(1.0F));
    }
    else if (argument == "--trace" && hasValue)
    {
      tracePath = value;
    }
    else if (argument == "--seconds" && hasValue)
    {
      traceSeconds = toFloat(value).value_or(traceSeconds);
    }
    else if (argument == "--csv")
    {
      if (hasValue && !arguments[index + 1].starts_with("--"))
      {
        csvPath = value;
      }
      else
      {
        csvAuto = true;
        continue;
      }
    }
    else
    {
      error = std::format("unknown option \"{}\"", argument);
      break;
    }
    ++index;
  }
  // A preset fills in settings; any --set (whatever its place) wins over it.
  if (error.empty() && presetName)
  {
    const auto list = drill->presets();
    const auto found = std::ranges::find(list, *presetName, &DrillPreset::name);
    if (found == list.end())
    {
      error = std::format("no preset \"{}\"; presets:", *presetName);
      for (const DrillPreset& preset : list)
      {
        error += " \"" + preset.name + '"';
      }
    }
    else
    {
      for (const auto& [name, value] : found->settings)
      {
        spec.fixed.try_emplace(name, value);
      }
    }
  }
  // --trace: one run (the preset, --set, --seed), every player and the ball
  // every fixed step for --seconds, instead of a sweep (lab_trace.h).
  if (error.empty() && tracePath)
  {
    for (const DrillParameter& parameter : drill->parameters())
    {
      if ((parameter.value != nullptr) && spec.fixed.contains(parameter.name))
      {
        *parameter.value = spec.fixed.at(parameter.name);
      }
    }
    drill->seed = spec.firstSeed;
    const int ticks = static_cast<int>(
        std::lround(traceSeconds / MatchTuning::Timing::FIXED_STEP_SECONDS));
    std::ofstream out(*tracePath, std::ios::binary);
    if (!out)
    {
      std::cerr << "Measure: cannot write " << tracePath->string() << '\n';
      return 2;
    }
    Lab::writeTraceHeader(out);
    DrillRun run(*drill, config, false);
    Lab::writeTraceRows(out, "d", 0, run.engine());
    for (int tick = 1; tick <= ticks; ++tick)
    {
      const bool going = run.step();
      Lab::writeTraceRows(out, "d", tick, run.engine());
      if (!going)
      {
        break;
      }
    }
    std::cout << std::format("{}: trace written to {}\n", drill->name(),
                             tracePath->string());
    // The run's report, as the sandbox shows it.
    for (const std::string& line : drill->report())
    {
      std::cout << "  " << line << '\n';
    }
    return out ? 0 : 2;
  }
  if (error.empty() && spec.sweep.values.empty())
  {
    error = "--sweep is required, e.g. --sweep \"Pace=40:95:12\"";
  }
  if (!error.empty())
  {
    std::cerr << "Measure: " << error << "\nSettings of " << drill->name()
              << ":";
    for (const DrillParameter& parameter : drill->parameters())
    {
      std::cerr << " \"" << parameter.name << '"';
    }
    std::cerr << '\n';
    return 2;
  }

  DrillMeasure measure(*drill, config, spec);
  for (const std::string& note : measure.warnings())
  {
    std::cout << note << '\n';
  }
  if (measure.runsTotal() == 0)
  {
    return 2;
  }
  std::cout << std::format(
      "{}: {} runs ({} by {}{}, {} repeat{})\n", drill->name(),
      measure.runsTotal(), spec.sweep.parameter,
      measure.spec().sweep.values.size(),
      spec.by ? std::format(", for each {}", spec.by->parameter)
              : std::string(),
      spec.repeats, spec.repeats == 1 ? "" : "s");
  while (measure.work(1.0))
  {
    std::cerr << std::format("  {} / {}\r", measure.runsDone(),
                             measure.runsTotal());
  }
  std::cout << std::format("Done in {:.2f} s.\n\n", measure.seconds());

  // Table: one row per point, mean (± spread) of every metric.
  const auto& names = measure.metricNames();
  std::string header = std::format(
      "{:>{}}", spec.sweep.parameter.substr(0, SWEEP_COLUMN), SWEEP_COLUMN);
  if (spec.by)
  {
    header += std::format("  {:>{}}", spec.by->parameter.substr(0, BY_COLUMN),
                          BY_COLUMN);
  }
  header += "   done";
  for (const std::string& name : names)
  {
    header += std::format("  {}", name);
  }
  std::cout << header << '\n';
  for (const MeasurePoint& point : measure.points())
  {
    std::string row =
        std::format("{:>{}}", measure.valueText(spec.sweep.parameter, point.x),
                    SWEEP_COLUMN);
    if (spec.by)
    {
      row += std::format("  {:>{}}",
                         measure.valueText(spec.by->parameter, point.by),
                         BY_COLUMN);
    }
    row += std::format("  {:>2}/{:<2}", point.completed, point.runs);
    for (const std::string& name : names)
    {
      const auto found = point.stats.find(name);
      std::string cell =
          found == point.stats.end() ? "-"
          : found->second.count > 1 && found->second.sd >= SHOWN_SPREAD
              ? std::format("{:.2f}±{:.2f}", found->second.mean,
                            found->second.sd)
              : std::format("{:.2f}", found->second.mean);
      row += std::format("  {:>{}}", cell, name.size());
    }
    std::cout << row << '\n';
  }
  if (csvAuto)
  {
    csvPath = measurePath(drill->name(), spec.sweep.parameter);
  }
  if (csvPath)
  {
    std::error_code ignored;
    if (csvPath->has_parent_path())
    {
      std::filesystem::create_directories(csvPath->parent_path(), ignored);
    }
    std::ofstream file(*csvPath, std::ios::binary);
    file << measure.csv();
    if (!file)
    {
      std::cerr << "Could not write " << csvPath->string() << '\n';
      return 1;
    }
    std::cout << "\nRuns written to " << csvPath->string() << '\n';
  }
  return 0;
}

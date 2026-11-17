// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_bench.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <ctime>
#include <format>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string_view>

#include "global/runtime_paths.h"
#include "global/thread_pool.h"
#include "tools/match_sandbox_measure.h"

namespace
{
using Clock = std::chrono::steady_clock;

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

std::optional<double> metricOf(const std::vector<DrillMetric>& metrics,
                               std::string_view name)
{
  for (const DrillMetric& metric : metrics)
  {
    if (metric.name == name)
    {
      return metric.value;
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

/** One CSV line's fields (quotes as csvField writes them). */
std::vector<std::string> csvFields(const std::string& line)
{
  std::vector<std::string> fields(1);
  bool quoted = false;
  for (std::size_t index = 0; index < line.size(); ++index)
  {
    const char c = line[index];
    if (quoted)
    {
      if (c == '"' && index + 1 < line.size() && line[index + 1] == '"')
      {
        fields.back() += '"';
        ++index;
      }
      else if (c == '"')
      {
        quoted = false;
      }
      else
      {
        fields.back() += c;
      }
    }
    else if (c == '"')
    {
      quoted = true;
    }
    else if (c == ',')
    {
      fields.emplace_back();
    }
    else if (c != '\r')
    {
      fields.back() += c;
    }
  }
  return fields;
}

std::string tagsText(const std::map<std::string, std::string>& tags)
{
  std::string text;
  for (const auto& [key, value] : tags)
  {
    text += std::format("{}{}={}", text.empty() ? "" : ";", key, value);
  }
  return text;
}

std::map<std::string, std::string> tagsFrom(const std::string& text)
{
  std::map<std::string, std::string> tags;
  std::size_t start = 0;
  while (start < text.size())
  {
    const std::size_t end = std::min(text.find(';', start), text.size());
    const std::string pair = text.substr(start, end - start);
    if (const std::size_t equals = pair.find('='); equals != std::string::npos)
    {
      tags[pair.substr(0, equals)] = pair.substr(equals + 1);
    }
    start = end + 1;
  }
  return tags;
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
  char text[32];
  std::strftime(text, sizeof(text), "%Y-%m-%d %H%M%S", &local);
  return text;
}

/** `pattern` with * (any run) and ? (any one), ignoring case. */
bool wildcard(std::string_view pattern, std::string_view text)
{
  if (pattern.empty())
  {
    return text.empty();
  }
  if (pattern.front() == '*')
  {
    for (std::size_t skip = 0; skip <= text.size(); ++skip)
    {
      if (wildcard(pattern.substr(1), text.substr(skip)))
      {
        return true;
      }
    }
    return false;
  }
  if (text.empty())
  {
    return false;
  }
  if (pattern.front() != '?' &&
      std::tolower(static_cast<unsigned char>(pattern.front())) !=
          std::tolower(static_cast<unsigned char>(text.front())))
  {
    return false;
  }
  return wildcard(pattern.substr(1), text.substr(1));
}

const DrillParameter* parameterOf(const std::vector<DrillParameter>& parameters,
                                  std::string_view name)
{
  for (const DrillParameter& parameter : parameters)
  {
    if (sameName(parameter.name, name))
    {
      return &parameter;
    }
  }
  return nullptr;
}

/** A setting's value from the file: a number, or a choice by name. */
std::optional<float> settingValue(const DrillParameter& parameter,
                                  const nlohmann::json& value,
                                  std::string& error)
{
  if (value.is_number())
  {
    return value.get<float>();
  }
  if (value.is_boolean())
  {
    return value.get<bool>() ? 1.0F : 0.0F;
  }
  if (value.is_string())
  {
    const std::string text = value.get<std::string>();
    for (std::size_t index = 0; index < parameter.choices.size(); ++index)
    {
      if (sameName(parameter.choices[index], text))
      {
        return static_cast<float>(index);
      }
    }
    error =
        std::format("\"{}\" is not a choice of \"{}\"", text, parameter.name);
    return std::nullopt;
  }
  error = std::format("\"{}\": expected a number or a choice", parameter.name);
  return std::nullopt;
}
}  // namespace

// --- Success -----------------------------------------------------------------

float passTolerance(float distance)
{
  const float share = std::clamp(
      (distance - BENCH_NEAR_METRES) / (BENCH_FAR_METRES - BENCH_NEAR_METRES),
      0.0F, 1.0F);
  return BENCH_NEAR_TOLERANCE +
         ((BENCH_FAR_TOLERANCE - BENCH_NEAR_TOLERANCE) * share);
}

bool BenchSuccess::judge(const std::vector<DrillMetric>& metrics,
                         float* tolerance) const
{
  switch (kind)
  {
    case Kind::FLAG:
      return metricOf(metrics, metric).value_or(0.0) >= 0.5;
    case Kind::AT_MOST:
    {
      const auto value = metricOf(metrics, metric);
      return value && *value <= threshold;
    }
    case Kind::AT_LEAST:
    {
      const auto value = metricOf(metrics, metric);
      return value && *value >= threshold;
    }
    case Kind::STRIKE:
    {
      const auto distance = metricOf(metrics, "Strike to target (m)");
      const float within =
          passTolerance(static_cast<float>(distance.value_or(0.0)));
      if (tolerance != nullptr)
      {
        *tolerance = within;
      }
      // No miss: a pass meant to fly never came down.
      const auto miss = metricOf(metrics, "Strike miss (m)");
      return miss && *miss <= within;
    }
  }
  return false;
}

std::string BenchSuccess::describe(std::optional<float> range) const
{
  switch (kind)
  {
    case Kind::FLAG:
      return metric;
    case Kind::AT_MOST:
      return std::format("{} <= {:g}", metric, threshold);
    case Kind::AT_LEAST:
      return std::format("{} >= {:g}", metric, threshold);
    case Kind::STRIKE:
      return range ? std::format(
                         "lands or runs within {:.1f} m of the target "
                         "({:g} m pass)",
                         passTolerance(*range), *range)
                   : std::string("lands or runs within the strike tolerance");
  }
  return {};
}

std::string BenchTest::tag(const std::string& key) const
{
  const auto found = tags.find(key);
  return found == tags.end() ? std::string() : found->second;
}

// --- Catalogue
// -----------------------------------------------------------------

std::filesystem::path benchCataloguePath()
{
  return RuntimePaths::assetRoot() / "assets" / "tools" /
         "technique_tests.json";
}

std::vector<float> defaultBenchSkills()
{
  std::vector<float> skills{1.0F};
  for (int value = 10; value <= 100; value += 10)
  {
    skills.push_back(static_cast<float>(value));
  }
  return skills;
}

BenchCatalogue loadBenchCatalogue(const std::filesystem::path& path)
{
  BenchCatalogue catalogue;
  catalogue.path = path;
  catalogue.skills = defaultBenchSkills();
  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    catalogue.problems.push_back("Could not open " + path.string());
    return catalogue;
  }
  nlohmann::json root;
  try
  {
    root = nlohmann::json::parse(file);
  }
  catch (const std::exception& e)
  {
    catalogue.problems.push_back(std::string("Could not read it: ") + e.what());
    return catalogue;
  }
  if (root.contains("skills") && root["skills"].is_array())
  {
    catalogue.skills.clear();
    for (const auto& value : root["skills"])
    {
      if (value.is_number())
      {
        catalogue.skills.push_back(value.get<float>());
      }
    }
  }

  // One set of drills to check names against.
  const auto drills = makeDrills();
  const auto drillNamed = [&](std::string_view name) -> Drill*
  {
    for (const auto& drill : drills)
    {
      if (sameName(drill->name(), name))
      {
        return drill.get();
      }
    }
    return nullptr;
  };
  const nlohmann::json defaults =
      root.value("drillDefaults", nlohmann::json::object());

  for (const auto& entry : root.value("tests", nlohmann::json::array()))
  {
    BenchTest test;
    test.id = entry.value("id", std::string());
    test.name = entry.value("name", test.id);
    test.section = entry.value("section", std::string("Tests"));
    const std::string label = test.id.empty() ? test.name : test.id;
    const auto problem = [&](const std::string& text)
    { catalogue.problems.push_back(std::format("{}: {}", label, text)); };
    if (test.id.empty())
    {
      problem("no id");
      continue;
    }
    Drill* drill = drillNamed(entry.value("drill", std::string()));
    if (drill == nullptr)
    {
      problem(
          std::format("no drill \"{}\"", entry.value("drill", std::string())));
      continue;
    }
    test.drill = drill->name();
    const auto parameters = drill->parameters();
    bool ok = true;
    const auto read = [&](const nlohmann::json& settings)
    {
      if (!settings.is_object())
      {
        return;
      }
      for (const auto& [name, value] : settings.items())
      {
        const DrillParameter* parameter = parameterOf(parameters, name);
        if (!parameter)
        {
          problem(std::format("{} has no setting \"{}\"", test.drill, name));
          ok = false;
          continue;
        }
        std::string error;
        const auto number = settingValue(*parameter, value, error);
        if (!number)
        {
          problem(error);
          ok = false;
          continue;
        }
        test.settings[parameter->name] = *number;
      }
    };
    if (defaults.contains(test.drill))
    {
      read(defaults[test.drill]);
    }
    read(entry.value("settings", nlohmann::json::object()));
    const std::string skill = entry.value("skill", std::string());
    if (const DrillParameter* parameter = parameterOf(parameters, skill))
    {
      test.skill = parameter->name;
    }
    else
    {
      problem(std::format("{} has no skill setting \"{}\"", test.drill, skill));
      ok = false;
    }
    for (const auto& [key, value] :
         entry.value("tags", nlohmann::json::object()).items())
    {
      if (value.is_string())
      {
        test.tags[key] = value.get<std::string>();
      }
    }

    const nlohmann::json success =
        entry.value("success", nlohmann::json("strike"));
    if (success.is_string() && success.get<std::string>() == "strike")
    {
      test.success.kind = BenchSuccess::Kind::STRIKE;
    }
    else if (success.is_object() && success.contains("flag"))
    {
      test.success.kind = BenchSuccess::Kind::FLAG;
      test.success.metric = success["flag"].get<std::string>();
    }
    else if (success.is_object() && success.contains("metric") &&
             (success.contains("atMost") || success.contains("atLeast")))
    {
      test.success.metric = success["metric"].get<std::string>();
      test.success.kind = success.contains("atMost")
                              ? BenchSuccess::Kind::AT_MOST
                              : BenchSuccess::Kind::AT_LEAST;
      test.success.threshold = success.contains("atMost")
                                   ? success["atMost"].get<float>()
                                   : success["atLeast"].get<float>();
    }
    else
    {
      problem(
          "success: expected \"strike\", {\"flag\": NAME} or {\"metric\": "
          "NAME, \"atMost\" or \"atLeast\": VALUE}");
      ok = false;
    }
    if (entry.contains("range") && entry["range"].is_number())
    {
      test.range = entry["range"].get<float>();
    }
    else if (const auto length = test.settings.find("Length (m)");
             length != test.settings.end())
    {
      test.range = length->second;
    }
    if (std::ranges::any_of(catalogue.tests, [&](const BenchTest& other)
                            { return other.id == test.id; }))
    {
      problem("the id is used twice");
      ok = false;
    }
    if (ok)
    {
      catalogue.tests.push_back(std::move(test));
    }
  }
  return catalogue;
}

// --- Results
// -------------------------------------------------------------------

double BenchStat::sd() const
{
  if (count < 2)
  {
    return 0.0;
  }
  const double average = mean();
  return std::sqrt(
      std::max(0.0, (squares - count * average * average) / (count - 1)));
}

std::optional<double> BenchCell::rate() const
{
  if (completed == 0)
  {
    return std::nullopt;
  }
  return static_cast<double>(successes) / completed;
}

void BenchResults::add(BenchRow row)
{
  if (row.test >= tests.size())
  {
    return;
  }
  BenchCell& slot = cells[tests[row.test].id][row.skill];
  ++slot.runs;
  if (row.completed)
  {
    ++slot.completed;
    if (row.success)
    {
      ++slot.successes;
    }
    if (tests[row.test].success.kind == BenchSuccess::Kind::STRIKE)
    {
      slot.tolerance.add(row.tolerance);
    }
    for (const DrillMetric& metric : row.metrics)
    {
      slot.metrics[metric.name].add(metric.value);
    }
  }
  for (const DrillMetric& metric : row.metrics)
  {
    if (std::ranges::find(names, metric.name) == names.end())
    {
      names.push_back(metric.name);
    }
  }
  all.push_back(std::move(row));
}

const BenchCell* BenchResults::cell(const std::string& testId,
                                    float skill) const
{
  const auto test = cells.find(testId);
  if (test == cells.end())
  {
    return nullptr;
  }
  const auto found = test->second.find(skill);
  return found == test->second.end() ? nullptr : &found->second;
}

std::string BenchResults::csv() const
{
  std::string text =
      "Test,Name,Section,Drill,Tags,Success rule,Skill setting,Skill,Seed,"
      "Completed,Success,Tolerance (m)";
  for (const std::string& name : names)
  {
    text += "," + csvField(name);
  }
  text += "\n";
  for (const BenchRow& row : all)
  {
    const BenchTest& test = tests[row.test];
    text += std::format(
        "{},{},{},{},{},{},{},{:g},{},{},{},", csvField(test.id),
        csvField(test.name), csvField(test.section), csvField(test.drill),
        csvField(tagsText(test.tags)),
        csvField(test.success.describe(test.range)), csvField(test.skill),
        row.skill, row.seed, row.completed ? 1 : 0, row.success ? 1 : 0);
    if (row.completed && test.success.kind == BenchSuccess::Kind::STRIKE)
    {
      text += std::format("{:.3f}", row.tolerance);
    }
    for (const std::string& name : names)
    {
      text += ",";
      if (const auto value = metricOf(row.metrics, name))
      {
        text += std::format("{:.4f}", *value);
      }
    }
    text += "\n";
  }
  return text;
}

bool BenchResults::save(const std::filesystem::path& path) const
{
  std::error_code ignored;
  std::filesystem::create_directories(path.parent_path(), ignored);
  {
    std::ofstream file(path, std::ios::binary);
    file << csv();
    if (!file)
    {
      return false;
    }
  }
  std::filesystem::path notes = path;
  notes.replace_extension();
  notes += " tuning.txt";
  std::ofstream file(notes, std::ios::binary);
  file << label << "\n" << repeats << " runs per point\n\nTuning:\n";
  if (tuningChanges.empty())
  {
    file << "The game's values (nothing changed).\n";
  }
  for (const std::string& line : tuningChanges)
  {
    file << line << "\n";
  }
  return static_cast<bool>(file);
}

std::optional<BenchResults> BenchResults::load(
    const std::filesystem::path& path, std::string& error)
{
  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    error = "Could not open " + path.string();
    return std::nullopt;
  }
  std::string line;
  if (!std::getline(file, line))
  {
    error = "Empty file";
    return std::nullopt;
  }
  const std::vector<std::string> header = csvFields(line);
  const auto column = [&](std::string_view name) -> int
  {
    for (std::size_t index = 0; index < header.size(); ++index)
    {
      if (header[index] == name)
      {
        return static_cast<int>(index);
      }
    }
    return -1;
  };
  const int testColumn = column("Test");
  const int skillColumn = column("Skill");
  const int successColumn = column("Success");
  const int toleranceColumn = column("Tolerance (m)");
  if (testColumn < 0 || skillColumn < 0 || successColumn < 0 ||
      toleranceColumn < 0)
  {
    error = "Not a bench results file (no Test, Skill or Success column)";
    return std::nullopt;
  }
  BenchResults results;
  results.label = path.stem().string();
  std::map<std::string, std::size_t> testIndex;
  const auto field = [](const std::vector<std::string>& fields, int index)
  {
    return index >= 0 && index < static_cast<int>(fields.size())
               ? fields[static_cast<std::size_t>(index)]
               : std::string();
  };
  while (std::getline(file, line))
  {
    if (line.empty() || line == "\r")
    {
      continue;
    }
    const std::vector<std::string> fields = csvFields(line);
    const std::string id = field(fields, testColumn);
    auto found = testIndex.find(id);
    if (found == testIndex.end())
    {
      BenchTest test;
      test.id = id;
      test.name = field(fields, column("Name"));
      test.section = field(fields, column("Section"));
      test.drill = field(fields, column("Drill"));
      test.tags = tagsFrom(field(fields, column("Tags")));
      test.skill = field(fields, column("Skill setting"));
      // The rule is kept as text only; a strike rule is recognisable.
      test.success.kind =
          field(fields, column("Success rule")).starts_with("lands")
              ? BenchSuccess::Kind::STRIKE
              : BenchSuccess::Kind::FLAG;
      test.success.metric = field(fields, column("Success rule"));
      found = testIndex.emplace(id, results.tests.size()).first;
      results.tests.push_back(std::move(test));
    }
    BenchRow row;
    row.test = found->second;
    row.skill = toFloat(field(fields, skillColumn)).value_or(0.0F);
    row.seed = static_cast<std::uint32_t>(
        toFloat(field(fields, column("Seed"))).value_or(0.0F));
    row.completed = field(fields, column("Completed")) == "1";
    row.success = field(fields, successColumn) == "1";
    row.tolerance = toFloat(field(fields, toleranceColumn)).value_or(0.0F);
    for (std::size_t index = static_cast<std::size_t>(toleranceColumn) + 1;
         index < header.size() && index < fields.size(); ++index)
    {
      if (const auto value = toFloat(fields[index]))
      {
        row.metrics.push_back({.name = header[index], .value = *value});
      }
    }
    if (std::ranges::find(results.skills, row.skill) == results.skills.end())
    {
      results.skills.push_back(row.skill);
    }
    results.add(std::move(row));
  }
  std::ranges::sort(results.skills);
  std::filesystem::path notes = path;
  notes.replace_extension();
  notes += " tuning.txt";
  if (std::ifstream tuning(notes, std::ios::binary); tuning)
  {
    bool inTuning = false;
    while (std::getline(tuning, line))
    {
      if (!line.empty() && line.back() == '\r')
      {
        line.pop_back();
      }
      if (line == "Tuning:")
      {
        inTuning = true;
      }
      else if (inTuning && !line.empty() &&
               !line.starts_with("The game's values"))
      {
        results.tuningChanges.push_back(line);
      }
    }
  }
  return results;
}

std::filesystem::path benchResultsPath()
{
  return measureFolder() / "bench" / (timestamp() + ".csv");
}

// --- Runner
// --------------------------------------------------------------------

BenchRunner::BenchRunner(const BenchCatalogue& catalogue,
                         const StatsConfig& config_ref, BenchSpec spec)
    : config(config_ref), setup(std::move(spec))
{
  setup.repeats = std::max(1, setup.repeats);
  if (setup.skills.empty())
  {
    setup.skills = defaultBenchSkills();
  }
  // The tests, with the skill overridden where asked (and possible).
  const auto drills = makeDrills();
  for (const std::size_t index : setup.tests)
  {
    if (index >= catalogue.tests.size())
    {
      continue;
    }
    BenchTest test = catalogue.tests[index];
    if (!setup.skillOverride.empty())
    {
      const Drill* drill = nullptr;
      for (const auto& candidate : drills)
      {
        if (candidate->name() == test.drill)
        {
          drill = candidate.get();
        }
      }
      const DrillParameter* parameter =
          (drill != nullptr)
              ? parameterOf(const_cast<Drill*>(drill)->parameters(),
                            setup.skillOverride)
              : nullptr;
      if (parameter == nullptr)
      {
        notes.push_back(std::format("{}: {} has no \"{}\", left out.", test.id,
                                    test.drill, setup.skillOverride));
        continue;
      }
      test.skill = parameter->name;
    }
    tests.push_back(std::move(test));
  }

  // Every test at the middle skill first, then the ends, then the rest.
  std::vector<float> order = setup.skills;
  std::ranges::sort(order);
  std::vector<float> priority;
  if (!order.empty())
  {
    const auto middle = std::ranges::min_element(
        order, {}, [](float skill) { return std::abs(skill - 50.0F); });
    priority.push_back(*middle);
    for (const float end : {order.front(), order.back()})
    {
      if (std::ranges::find(priority, end) == priority.end())
      {
        priority.push_back(end);
      }
    }
    for (const float skill : order)
    {
      if (std::ranges::find(priority, skill) == priority.end())
      {
        priority.push_back(skill);
      }
    }
  }
  for (const float skill : priority)
  {
    for (std::size_t test = 0; test < tests.size(); ++test)
    {
      for (int repeat = 0; repeat < setup.repeats; ++repeat)
      {
        jobs.push_back(
            {.test = test,
             .skill = skill,
             .seed = setup.firstSeed + static_cast<std::uint32_t>(repeat)});
      }
    }
  }

  started = Clock::now();
  const unsigned threads = std::clamp(
      setup.threads > 0 ? setup.threads : ThreadPool::defaultThreadCount(), 1U,
      ThreadPool::MAX_THREADS);
  const auto count = std::min<std::size_t>(threads, jobs.size());
  for (std::size_t index = 0; index < count; ++index)
  {
    workers.emplace_back([this] { work(); });
  }
  if (jobs.empty())
  {
    finishedAfter = 0.0;
  }
}

BenchRunner::~BenchRunner() { stop(); }

void BenchRunner::stop()
{
  stopping = true;
  for (std::jthread& worker : workers)
  {
    if (worker.joinable())
    {
      worker.join();
    }
  }
  workers.clear();
  if (!done())
  {
    finishedAfter =
        std::chrono::duration<double>(Clock::now() - started).count();
  }
}

double BenchRunner::seconds() const
{
  if (done() || (stopping && workers.empty()))
  {
    return finishedAfter.load();
  }
  return std::chrono::duration<double>(Clock::now() - started).count();
}

void BenchRunner::work()
{
  // Each worker has its own drills (they hold a run's state), each starting
  // every run from its own default settings.
  const auto drills = makeDrills();
  std::map<std::string, Drill*> byName;
  std::map<Drill*, std::map<std::string, float>> defaults;
  for (const auto& drill : drills)
  {
    byName[drill->name()] = drill.get();
    for (const DrillParameter& parameter : drill->parameters())
    {
      if (parameter.value != nullptr)
      {
        defaults[drill.get()][parameter.name] = *parameter.value;
      }
    }
  }
  const TechniqueTuning& tuning = setup.tuning;
  const auto prepare = [&tuning](MatchEngine& engine)
  { tuning.applyTo(engine); };
  while (!stopping)
  {
    const std::size_t index = nextJob.fetch_add(1);
    if (index >= jobs.size())
    {
      break;
    }
    const Job& job = jobs[index];
    const BenchTest& test = tests[job.test];
    BenchRow row;
    row.test = job.test;
    row.skill = job.skill;
    row.seed = job.seed;
    if (const auto found = byName.find(test.drill); found != byName.end())
    {
      Drill& drill = *found->second;
      std::map<std::string, float> values = defaults[&drill];
      for (const auto& [name, value] : test.settings)
      {
        values[name] = value;
      }
      values[test.skill] = job.skill;
      for (const DrillParameter& parameter : drill.parameters())
      {
        if ((parameter.value != nullptr) && values.contains(parameter.name))
        {
          *parameter.value = values.at(parameter.name);
        }
      }
      drill.seed = job.seed;
      {
        DrillRun run(drill, config, false, prepare);
        while (run.step())
        {
        }
      }
      row.metrics = drill.metrics();
      row.completed = !row.metrics.empty();
      if (row.completed)
      {
        row.success = test.success.judge(row.metrics, &row.tolerance);
      }
    }
    {
      const std::scoped_lock lock(mutex);
      pending.push_back(std::move(row));
    }
    if (finished.fetch_add(1) + 1 == jobs.size())
    {
      finishedAfter =
          std::chrono::duration<double>(Clock::now() - started).count();
    }
  }
}

void BenchRunner::collect(BenchResults& results)
{
  if (!initialised)
  {
    initialised = true;
    results = BenchResults{};
    results.label = timestamp();
    results.tests = tests;
    results.skills = setup.skills;
    std::ranges::sort(results.skills);
    results.repeats = setup.repeats;
    results.tuningChanges = setup.tuning.changes();
  }
  std::vector<BenchRow> rows;
  {
    const std::scoped_lock lock(mutex);
    rows.swap(pending);
  }
  for (BenchRow& row : rows)
  {
    results.add(std::move(row));
  }
}

// --- Command line
// ----------------------------------------------------------------

int runBenchCommand(const StatsConfig& config,
                    const std::vector<std::string>& arguments)
{
  std::filesystem::path cataloguePath = benchCataloguePath();
  std::vector<std::string> patterns;
  std::vector<std::pair<std::string, std::string>> tagFilters;
  BenchSpec spec;
  std::optional<std::filesystem::path> csvPath;
  bool skillsGiven = false;
  for (std::size_t index = 0; index < arguments.size(); ++index)
  {
    const std::string& argument = arguments[index];
    const bool hasValue = index + 1 < arguments.size();
    const std::string value = hasValue ? arguments[index + 1] : std::string();
    const auto list = [](const std::string& text)
    {
      std::vector<std::string> parts;
      std::stringstream stream(text);
      for (std::string part; std::getline(stream, part, ',');)
      {
        if (!part.empty())
        {
          parts.push_back(part);
        }
      }
      return parts;
    };
    if (argument == "--catalogue" && hasValue)
    {
      cataloguePath = value;
    }
    else if (argument == "--tests" && hasValue)
    {
      patterns = list(value);
    }
    else if (argument == "--tag" && hasValue &&
             value.find('=') != std::string::npos)
    {
      tagFilters.emplace_back(value.substr(0, value.find('=')),
                              value.substr(value.find('=') + 1));
    }
    else if (argument == "--skills" && hasValue)
    {
      skillsGiven = true;
      const std::vector<std::string> parts = list(value);
      if (parts.size() == 1 && value.find(':') != std::string::npos)
      {
        std::vector<float> range;
        for (const std::string& part :
             [&]
             {
               std::vector<std::string> pieces;
               std::stringstream stream(value);
               for (std::string piece; std::getline(stream, piece, ':');)
               {
                 pieces.push_back(piece);
               }
               return pieces;
             }())
        {
          if (const auto number = toFloat(part))
          {
            range.push_back(*number);
          }
        }
        if (range.size() == 3)
        {
          spec.skills = spreadValues(range[0], range[1],
                                     static_cast<int>(range[2]), true);
        }
      }
      else
      {
        for (const std::string& part : parts)
        {
          if (const auto number = toFloat(part))
          {
            spec.skills.push_back(*number);
          }
        }
      }
    }
    else if (argument == "--repeats" && hasValue)
    {
      spec.repeats =
          std::max(1, static_cast<int>(toFloat(value).value_or(50.0F)));
    }
    else if (argument == "--seed" && hasValue)
    {
      spec.firstSeed =
          static_cast<std::uint32_t>(toFloat(value).value_or(1.0F));
    }
    else if (argument == "--threads" && hasValue)
    {
      spec.threads = static_cast<unsigned>(toFloat(value).value_or(0.0F));
    }
    else if (argument == "--skill" && hasValue)
    {
      spec.skillOverride = value;
    }
    else if (argument == "--csv" && hasValue)
    {
      csvPath = value;
    }
    else
    {
      std::cerr << "Bench: unknown or incomplete option \"" << argument
                << "\".\nUsage: --bench [--tests P0*,S1*] [--tag KEY=VALUE]... "
                   "[--skills 1,10,...|A:B:N] [--repeats N] [--seed N] "
                   "[--threads N] [--skill SETTING] [--catalogue PATH] "
                   "[--csv PATH]\n";
      return 2;
    }
    ++index;
  }

  const BenchCatalogue catalogue = loadBenchCatalogue(cataloguePath);
  for (const std::string& problem : catalogue.problems)
  {
    std::cerr << "Catalogue: " << problem << '\n';
  }
  if (!skillsGiven)
  {
    spec.skills = catalogue.skills;
  }
  for (std::size_t index = 0; index < catalogue.tests.size(); ++index)
  {
    const BenchTest& test = catalogue.tests[index];
    const bool named =
        patterns.empty() ||
        std::ranges::any_of(patterns, [&](const std::string& pattern)
                            { return wildcard(pattern, test.id); });
    const bool tagged = std::ranges::all_of(
        tagFilters, [&](const auto& filter)
        { return sameName(test.tag(filter.first), filter.second); });
    if (named && tagged)
    {
      spec.tests.push_back(index);
    }
  }
  if (spec.tests.empty())
  {
    std::cerr << "Bench: no tests chosen (" << catalogue.tests.size() << " in "
              << cataloguePath.string() << ").\n";
    return 2;
  }

  std::cout << std::format("Bench: {} tests x {} skills x {} repeats\n",
                           spec.tests.size(), spec.skills.size(), spec.repeats);
  BenchRunner runner(catalogue, config, spec);
  for (const std::string& note : runner.warnings())
  {
    std::cerr << note << '\n';
  }
  BenchResults results;
  while (!runner.done())
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    runner.collect(results);
  }
  runner.collect(results);
  std::cout << std::format("Done in {:.2f} s ({} runs).\n\n", runner.seconds(),
                           runner.runsDone());

  // Success rate by test and skill.
  std::cout << std::format("{:<5} {:<44}", "Test", "Success %");
  for (const float skill : results.skills)
  {
    std::cout << std::format("{:>6g}", skill);
  }
  std::cout << "   rule\n";
  for (const BenchTest& test : results.tests)
  {
    std::cout << std::format("{:<5} {:<44}", test.id, test.name.substr(0, 44));
    for (const float skill : results.skills)
    {
      const BenchCell* cell = results.cell(test.id, skill);
      const auto rate = (cell != nullptr) ? cell->rate() : std::nullopt;
      std::cout << (rate ? std::format("{:>6.0f}", *rate * 100.0)
                         : std::format("{:>6}", "-"));
    }
    std::cout << "   " << test.success.describe(test.range) << '\n';
  }

  const std::filesystem::path path = csvPath ? *csvPath : benchResultsPath();
  if (results.save(path))
  {
    std::cout << "\nSaved " << path.string() << '\n';
  }
  else
  {
    std::cerr << "\nCould not write " << path.string() << '\n';
  }
  return 0;
}

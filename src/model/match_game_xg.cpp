// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/match_game_xg.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "global/paths.h"

namespace
{
/** Smoothed values are stored to this many places. */
constexpr double STORED_SCALE = 10000.0;
constexpr int JSON_INDENT = 1;
constexpr int FORMAT_VERSION = 1;

/** Where `value` falls on `axis`: the lower index and the share past it,
 * clamped to the axis. */
std::pair<int, float> place(const GameXgTable::Axis& axis, float value)
{
  if (axis.count <= 1 || axis.step <= 0.0F)
  {
    return {0, 0.0F};
  }
  const float position = std::clamp((value - axis.from) / axis.step, 0.0F,
                                    static_cast<float>(axis.count - 1));
  const int low = std::min(static_cast<int>(position), axis.count - 2);
  return {low, position - static_cast<float>(low)};
}

nlohmann::json axisJson(const GameXgTable::Axis& axis)
{
  return {{"from", axis.from}, {"step", axis.step}, {"count", axis.count}};
}

GameXgTable::Axis axisOf(const nlohmann::json& json)
{
  return {.from = json.at("from").get<float>(),
          .step = json.at("step").get<float>(),
          .count = json.at("count").get<int>()};
}
}  // namespace

float GameXgTable::at(std::size_t level, int lineIndex, int sideIndex) const
{
  const std::size_t index = (((level * static_cast<std::size_t>(line.count)) +
                              static_cast<std::size_t>(lineIndex)) *
                             static_cast<std::size_t>(side.count)) +
                            static_cast<std::size_t>(sideIndex);
  return index < values.size() ? values[index] : 0.0F;
}

float GameXgTable::plane(std::size_t level, float lineMetres,
                         float sideMetres) const
{
  const auto [l, ls] = place(line, lineMetres);
  const auto [s, ss] = place(side, sideMetres);
  const int l1 = std::min(l + 1, line.count - 1);
  const int s1 = std::min(s + 1, side.count - 1);
  const float near =
      at(level, l, s) + ((at(level, l, s1) - at(level, l, s)) * ss);
  const float far =
      at(level, l1, s) + ((at(level, l1, s1) - at(level, l1, s)) * ss);
  return near + ((far - near) * ls);
}

float GameXgTable::value(float lineMetres, float sideMetres,
                         float shootingRating) const
{
  if (empty() || shooting.empty())
  {
    return 0.0F;
  }
  const float across = mirrored ? std::abs(sideMetres) : sideMetres;
  if (shooting.size() == 1 || shootingRating <= shooting.front())
  {
    return plane(0, lineMetres, across);
  }
  if (shootingRating >= shooting.back())
  {
    return plane(shooting.size() - 1, lineMetres, across);
  }
  std::size_t upper = 1;
  while (upper + 1 < shooting.size() && shooting[upper] < shootingRating)
  {
    ++upper;
  }
  const float share = (shootingRating - shooting[upper - 1]) /
                      (shooting[upper] - shooting[upper - 1]);
  const float low = plane(upper - 1, lineMetres, across);
  return low + ((plane(upper, lineMetres, across) - low) * share);
}

long long GameXgTable::shots() const
{
  long long total = 0;
  for (const GameXgSample& sample : samples)
  {
    total += sample.shots;
  }
  return total;
}

std::string GameXgTable::toJson() const
{
  nlohmann::json json;
  json["version"] = FORMAT_VERSION;
  json["layer"] = layer;
  json["goal"] = goalKindName(goal);
  json["measured"] = measured;
  json["repeats"] = repeats;
  json["first_seed"] = firstSeed;
  json["foot"] = foot;
  json["mirrored"] = mirrored;
  json["shooting"] = shooting;
  json["line"] = axisJson(line);
  json["side"] = axisJson(side);
  nlohmann::json grid = nlohmann::json::array();
  for (const float value : values)
  {
    grid.push_back(std::round(value * STORED_SCALE) / STORED_SCALE);
  }
  json["values"] = std::move(grid);
  nlohmann::json raw = nlohmann::json::array();
  for (const GameXgSample& sample : samples)
  {
    raw.push_back({sample.line, sample.side, sample.shooting, sample.shots,
                   sample.goals});
  }
  json["samples_line_side_shooting_shots_goals"] = std::move(raw);
  return json.dump(JSON_INDENT);
}

std::optional<GameXgTable> GameXgTable::fromJson(const std::string& text,
                                                 std::string& error)
{
  try
  {
    const nlohmann::json json = nlohmann::json::parse(text);
    GameXgTable table;
    table.layer = json.at("layer").get<std::string>();
    const std::string goal = json.at("goal").get<std::string>();
    bool known = false;
    for (int index = 0; index <= static_cast<int>(GoalKind::POP_UP); ++index)
    {
      if (goal == goalKindName(static_cast<GoalKind>(index)))
      {
        table.goal = static_cast<GoalKind>(index);
        known = true;
      }
    }
    if (!known)
    {
      error = "unknown goal \"" + goal + "\"";
      return std::nullopt;
    }
    table.measured = json.value("measured", std::string());
    table.repeats = json.value("repeats", 0);
    table.firstSeed = json.value("first_seed", 1U);
    table.foot = json.value("foot", std::string("right"));
    table.mirrored = json.value("mirrored", true);
    table.shooting = json.at("shooting").get<std::vector<float>>();
    table.line = axisOf(json.at("line"));
    table.side = axisOf(json.at("side"));
    table.values = json.at("values").get<std::vector<float>>();
    for (const auto& raw : json.value("samples_line_side_shooting_shots_goals",
                                      nlohmann::json::array()))
    {
      table.samples.push_back({.line = raw.at(0).get<float>(),
                               .side = raw.at(1).get<float>(),
                               .shooting = raw.at(2).get<float>(),
                               .shots = raw.at(3).get<int>(),
                               .goals = raw.at(4).get<int>()});
    }
    const std::size_t expected = table.shooting.size() *
                                 static_cast<std::size_t>(table.line.count) *
                                 static_cast<std::size_t>(table.side.count);
    if (table.values.size() != expected || table.shooting.empty())
    {
      error = "the grid's size doesn't match its axes";
      return std::nullopt;
    }
    return table;
  }
  catch (const std::exception& exception)
  {
    error = exception.what();
    return std::nullopt;
  }
}

bool GameXgTable::save(const std::filesystem::path& path,
                       std::string& error) const
{
  std::error_code code;
  std::filesystem::create_directories(path.parent_path(), code);
  std::ofstream out(path, std::ios::binary);
  if (!out)
  {
    error = "can't write " + path.string();
    return false;
  }
  out << toJson() << '\n';
  return static_cast<bool>(out);
}

std::optional<GameXgTable> GameXgTable::load(const std::filesystem::path& path,
                                             std::string& error)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    error = "no file " + path.string();
    return std::nullopt;
  }
  std::ostringstream text;
  text << in.rdbuf();
  return fromJson(text.str(), error);
}

namespace GameXg
{
std::filesystem::path folder()
{
  return {AssetPaths::file("assets/config/game_xg")};
}

std::filesystem::path file(std::string_view layer, GoalKind goal)
{
  std::string name(layer);
  std::ranges::replace(name, ' ', '_');
  return folder() / (name + "_" + goalKindName(goal) + ".json");
}
}  // namespace GameXg

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
#include <map>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <numbers>
#include <sstream>

#include "global/paths.h"

namespace
{
/** Smoothed values are stored to this many places. */
constexpr double STORED_SCALE = 10000.0;
constexpr int JSON_INDENT = 1;
constexpr int FORMAT_VERSION = 1;
/** A stored sample's columns, without the Goalkeeping one (it follows
 * Shooting where present). */
constexpr std::size_t SAMPLE_LINE = 0;
constexpr std::size_t SAMPLE_SIDE = 1;
constexpr std::size_t SAMPLE_SHOOTING = 2;
constexpr std::size_t SAMPLE_SHOTS = 3;
constexpr std::size_t SAMPLE_GOALS = 4;

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

namespace
{
/** Where `rating` falls among ascending `levels`: the lower level's index
 * and the share past it (clamped to the ends). */
std::pair<std::size_t, float> bracket(const std::vector<float>& levels,
                                      float rating)
{
  if (levels.size() <= 1 || rating <= levels.front())
  {
    return {0, 0.0F};
  }
  if (rating >= levels.back())
  {
    return {levels.size() - 2, 1.0F};
  }
  std::size_t upper = 1;
  while (upper + 1 < levels.size() && levels[upper] < rating)
  {
    ++upper;
  }
  return {upper - 1,
          (rating - levels[upper - 1]) / (levels[upper] - levels[upper - 1])};
}
}  // namespace

float GameXgTable::overShooting(std::size_t block, float lineMetres,
                                float sideMetres, float shootingRating) const
{
  const std::size_t first = block * shooting.size();
  const auto [low, share] = bracket(shooting, shootingRating);
  const float at = plane(first + low, lineMetres, sideMetres);
  if (shooting.size() == 1)
  {
    return at;
  }
  return at + ((plane(first + low + 1, lineMetres, sideMetres) - at) * share);
}

float GameXgTable::value(float lineMetres, float sideMetres,
                         float shootingRating, float goalkeepingRating) const
{
  if (empty() || shooting.empty())
  {
    return 0.0F;
  }
  const float across = mirrored ? std::abs(sideMetres) : sideMetres;
  if (goalkeeping.size() <= 1)
  {
    return overShooting(0, lineMetres, across, shootingRating);
  }
  const auto [low, share] = bracket(goalkeeping, goalkeepingRating);
  const float at = overShooting(low, lineMetres, across, shootingRating);
  return at +
         ((overShooting(low + 1, lineMetres, across, shootingRating) - at) *
          share);
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
  if (!goalkeeping.empty())
  {
    json["goalkeeping"] = goalkeeping;
  }
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
    raw.push_back({sample.line, sample.side, sample.shooting,
                   sample.goalkeeping, sample.shots, sample.goals});
  }
  json["samples_line_side_shooting_goalkeeping_shots_goals"] = std::move(raw);
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
    table.goalkeeping = json.value("goalkeeping", std::vector<float>{});
    table.line = axisOf(json.at("line"));
    table.side = axisOf(json.at("side"));
    table.values = json.at("values").get<std::vector<float>>();
    // Samples: with a Goalkeeping column, or (the first maps) without; the
    // columns after Shooting shift by one.
    const auto addSamples = [&](const char* key, bool withGoalkeeping)
    {
      const std::size_t shift = withGoalkeeping ? 1 : 0;
      for (const auto& raw : json.value(key, nlohmann::json::array()))
      {
        table.samples.push_back(
            {.line = raw.at(SAMPLE_LINE).get<float>(),
             .side = raw.at(SAMPLE_SIDE).get<float>(),
             .shooting = raw.at(SAMPLE_SHOOTING).get<float>(),
             .goalkeeping = withGoalkeeping
                                ? raw.at(SAMPLE_SHOOTING + 1).get<float>()
                                : 0.0F,
             .shots = raw.at(SAMPLE_SHOTS + shift).get<int>(),
             .goals = raw.at(SAMPLE_GOALS + shift).get<int>()});
      }
    };
    addSamples("samples_line_side_shooting_goalkeeping_shots_goals", true);
    addSamples("samples_line_side_shooting_shots_goals", false);
    const std::size_t expected =
        std::max<std::size_t>(1, table.goalkeeping.size()) *
        table.shooting.size() * static_cast<std::size_t>(table.line.count) *
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

const char* GameXgLegTable::kindName(Kind kind)
{
  switch (kind)
  {
    case Kind::OPPONENT_STANDING:
      return "opponent standing";
    case Kind::OPPONENT_SIDESTEPPING:
      return "opponent sidestepping";
    case Kind::OPPONENT_SPRINTING:
      return "opponent sprinting";
    case Kind::TEAMMATE_STANDING:
      return "team-mate standing";
    case Kind::TEAMMATE_SIDESTEPPING:
      return "team-mate sidestepping";
    case Kind::TEAMMATE_SPRINTING:
      return "team-mate sprinting";
    case Kind::COUNT:
      break;
  }
  return "";
}

std::size_t GameXgLegTable::index(std::size_t kind, std::size_t rating,
                                  std::size_t distance,
                                  std::size_t offset) const
{
  return (((((kind * ratings.size()) + rating) * distances.size()) + distance) *
          offsets.size()) +
         offset;
}

float GameXgLegTable::value(Kind kind, float rating, float distanceMetres,
                            float offsetMetres) const
{
  if (empty() || ratings.empty() || distances.empty() || offsets.empty() ||
      offsetMetres > offsets.back())
  {
    return 0.0F;
  }
  const auto k = static_cast<std::size_t>(kind);
  const std::pair<std::size_t, float> ratingAt = bracket(ratings, rating);
  const std::pair<std::size_t, float> distanceAt =
      bracket(distances, distanceMetres);
  const std::pair<std::size_t, float> offsetAt = bracket(offsets, offsetMetres);
  const std::size_t d = distanceAt.first;
  const std::size_t o = offsetAt.first;
  const std::size_t d1 = std::min(d + 1, distances.size() - 1);
  const std::size_t o1 = std::min(o + 1, offsets.size() - 1);
  const float ds = distanceAt.second;
  const float os = offsetAt.second;
  const auto plane = [&](std::size_t ri)
  {
    const float a = values[index(k, ri, d, o)];
    const float b = values[index(k, ri, d, o1)];
    const float c = values[index(k, ri, d1, o)];
    const float e = values[index(k, ri, d1, o1)];
    const float near = a + ((b - a) * os);
    const float far = c + ((e - c) * os);
    return near + ((far - near) * ds);
  };
  const std::size_t r = ratingAt.first;
  const float low = plane(r);
  return low + ((plane(std::min(r + 1, ratings.size() - 1)) - low) *
                ratingAt.second);
}

std::string GameXgLegTable::toJson() const
{
  nlohmann::json json;
  json["version"] = FORMAT_VERSION;
  json["layer"] = "legs";
  json["measured"] = measured;
  json["repeats"] = repeats;
  json["first_seed"] = firstSeed;
  nlohmann::json kinds = nlohmann::json::array();
  for (std::size_t kind = 0; kind < KINDS; ++kind)
  {
    kinds.push_back(kindName(static_cast<Kind>(kind)));
  }
  json["kinds"] = std::move(kinds);
  json["ratings"] = ratings;
  json["distances"] = distances;
  json["offsets"] = offsets;
  nlohmann::json grid = nlohmann::json::array();
  for (const float value : values)
  {
    grid.push_back(std::round(value * STORED_SCALE) / STORED_SCALE);
  }
  json["values"] = std::move(grid);
  json["shots"] = shots;
  json["blocks"] = blocks;
  json["scatter_shooting"] = scatterShooting;
  json["scatter_across"] = scatterAcross;
  json["scatter_up"] = scatterUp;
  return json.dump(JSON_INDENT);
}

namespace
{
/** Points for a Shooting from rows of `count` per Shooting, interpolated
 * between the rows measured (the edge row outside them). */
template <std::size_t COUNT>
std::array<float, COUNT> scatterRow(const std::vector<float>& shooting,
                                    const std::vector<float>& rows, float at)
{
  std::array<float, COUNT> out{};
  if (shooting.empty() || rows.size() != shooting.size() * COUNT)
  {
    return out;
  }
  std::size_t low = 0;
  while (low + 1 < shooting.size() && shooting[low + 1] <= at)
  {
    ++low;
  }
  const std::size_t high = std::min(low + 1, shooting.size() - 1);
  const float span = shooting[high] - shooting[low];
  const float share =
      span > 0.0F ? std::clamp((at - shooting[low]) / span, 0.0F, 1.0F) : 0.0F;
  for (std::size_t point = 0; point < COUNT; ++point)
  {
    const float from = rows[(low * COUNT) + point];
    const float to = rows[(high * COUNT) + point];
    out.at(point) = from + ((to - from) * share);
  }
  return out;
}
}  // namespace

std::array<float, GameXgLegTable::SCATTER_ACROSS_POINTS>
GameXgLegTable::acrossPoints(float shooting) const
{
  return scatterRow<SCATTER_ACROSS_POINTS>(scatterShooting, scatterAcross,
                                           shooting);
}

std::array<float, GameXgLegTable::SCATTER_UP_POINTS> GameXgLegTable::upPoints(
    float shooting) const
{
  return scatterRow<SCATTER_UP_POINTS>(scatterShooting, scatterUp, shooting);
}

std::optional<GameXgLegTable> GameXgLegTable::fromJson(const std::string& text,
                                                       std::string& error)
{
  try
  {
    const nlohmann::json json = nlohmann::json::parse(text);
    GameXgLegTable table;
    table.measured = json.value("measured", std::string());
    table.repeats = json.value("repeats", 0);
    table.firstSeed = json.value("first_seed", 1U);
    table.ratings = json.at("ratings").get<std::vector<float>>();
    table.distances = json.at("distances").get<std::vector<float>>();
    table.offsets = json.at("offsets").get<std::vector<float>>();
    table.values = json.at("values").get<std::vector<float>>();
    table.shots = json.value("shots", std::vector<int>{});
    table.blocks = json.value("blocks", std::vector<int>{});
    table.scatterShooting =
        json.value("scatter_shooting", std::vector<float>{});
    table.scatterAcross = json.value("scatter_across", std::vector<float>{});
    table.scatterUp = json.value("scatter_up", std::vector<float>{});
    if (table.values.size() != KINDS * table.ratings.size() *
                                   table.distances.size() *
                                   table.offsets.size())
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

bool GameXgLegTable::save(const std::filesystem::path& path,
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

std::optional<GameXgLegTable> GameXgLegTable::load(
    const std::filesystem::path& path, std::string& error)
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

namespace
{
struct Cache
{
  std::mutex lock;
  /** Loaded (or found missing: null) per layer and goal. */
  std::map<std::pair<std::string, GoalKind>, std::unique_ptr<GameXgTable>>
      tables;
  /** The leg-chance table: looked for once (null inside if missing). */
  std::optional<std::unique_ptr<GameXgLegTable>> legs;
};

Cache& cache()
{
  static Cache shared;
  return shared;
}
}  // namespace

const GameXgTable* table(std::string_view layer, GoalKind goal)
{
  if (goal == GoalKind::NONE)
  {
    return nullptr;
  }
  Cache& shared = cache();
  const std::scoped_lock guard(shared.lock);
  const auto key = std::pair{std::string(layer), goal};
  auto found = shared.tables.find(key);
  if (found == shared.tables.end())
  {
    std::string problem;
    auto loaded = GameXgTable::load(file(layer, goal), problem);
    found =
        shared.tables
            .emplace(key,
                     loaded ? std::make_unique<GameXgTable>(std::move(*loaded))
                            : nullptr)
            .first;
  }
  return found->second.get();
}

void reload()
{
  Cache& shared = cache();
  const std::scoped_lock guard(shared.lock);
  shared.tables.clear();
  shared.legs.reset();
}

std::filesystem::path legFile() { return folder() / "legs.json"; }

const GameXgLegTable* legs()
{
  Cache& shared = cache();
  const std::scoped_lock guard(shared.lock);
  if (!shared.legs)
  {
    std::string problem;
    auto loaded = GameXgLegTable::load(legFile(), problem);
    shared.legs =
        loaded ? std::make_unique<GameXgLegTable>(std::move(*loaded)) : nullptr;
  }
  return shared.legs->get();
}

float openShare(PitchPoint from, PitchPoint keeper, float goalLine,
                float goalMiddle, float halfWidth, float coverMetres)
{
  // Angles as the shooter sees them, measured from his line to the goal's
  // middle (so nothing wraps round): the mouth between the posts, and the
  // keeper's cover either side of him.
  const float toGoalX = goalLine - from.x;
  const float toGoalY = goalMiddle - from.y;
  const float base = std::atan2(toGoalY, toGoalX);
  const auto bearing = [&](float x, float y)
  {
    float angle = std::atan2(y - from.y, x - from.x) - base;
    angle = std::atan2(std::sin(angle), std::cos(angle));
    return angle;
  };
  const float postA = bearing(goalLine, goalMiddle - halfWidth);
  const float postB = bearing(goalLine, goalMiddle + halfWidth);
  const float mouthLow = std::min(postA, postB);
  const float mouthHigh = std::max(postA, postB);
  const float mouth = mouthHigh - mouthLow;
  if (mouth <= 0.0F)
  {
    return 0.0F;
  }
  const float away = std::hypot(keeper.x - from.x, keeper.y - from.y);
  const float centre = bearing(keeper.x, keeper.y);
  const float halfCover = away > coverMetres ? std::asin(coverMetres / away)
                                             : std::numbers::pi_v<float> / 2.0F;
  const float covered =
      std::max(0.0F, std::min(mouthHigh, centre + halfCover) -
                         std::max(mouthLow, centre - halfCover));
  return std::clamp(1.0F - (covered / mouth), 0.0F, 1.0F);
}
}  // namespace GameXg

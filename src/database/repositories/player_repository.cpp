// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "player_repository.h"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <iterator>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "database/SQLLoader.h"
#include "database/repositories/text_encoding.h"
#include "model/role_utils.h"
#include "model/squad_numbers.h"

namespace
{
class StatsSaxParser final : public nlohmann::json_sax<nlohmann::json>
{
 public:
  bool null() override { return false; }
  bool boolean(bool /*value*/) override { return false; }

  bool number_integer(number_integer_t value) override
  {
    return addValue(static_cast<float>(value));
  }

  bool number_unsigned(number_unsigned_t value) override
  {
    return addValue(static_cast<float>(value));
  }

  bool number_float(number_float_t value, const string_t& /*token*/) override
  {
    return addValue(static_cast<float>(value));
  }

  bool string(string_t& /*value*/) override { return false; }
  bool binary(binary_t& /*value*/) override { return false; }

  bool start_object(std::size_t /*elements*/) override
  {
    ++depth;
    return depth == ROOT_OBJECT_DEPTH;
  }

  bool key(string_t& value) override
  {
    currentKey = std::move(value);
    return depth == ROOT_OBJECT_DEPTH;
  }

  bool end_object() override
  {
    if (depth != ROOT_OBJECT_DEPTH) return false;
    --depth;
    return true;
  }

  bool start_array(std::size_t /*elements*/) override { return false; }
  bool end_array() override { return false; }

  bool parse_error(std::size_t /*position*/, const std::string& /*lastToken*/,
                   const nlohmann::detail::exception& /*exception*/) override
  {
    return false;
  }

  std::map<std::string, float> takeStats() { return std::move(stats); }

 private:
  bool addValue(float value)
  {
    if (depth != ROOT_OBJECT_DEPTH || currentKey.empty()) return false;
    stats.try_emplace(std::move(currentKey), value);
    currentKey.clear();
    return true;
  }

  static constexpr std::size_t ROOT_OBJECT_DEPTH = 1;
  std::size_t depth = 0;
  std::string currentKey;
  std::map<std::string, float> stats;
};

/** Parses a comma separated list of numbers; missing values stay at 0. */
template <std::size_t N>
std::array<double, N> parseNumbers(std::string_view text, std::size_t& count)
{
  std::array<double, N> values{};
  count = 0;
  while (!text.empty() && count < N)
  {
    const std::size_t comma = text.find(',');
    const std::string_view token = text.substr(0, comma);
    double value = 0.0;
    if (std::from_chars(token.data(), token.data() + token.size(), value).ec !=
        std::errc{})
      break;
    values[count++] = value;
    if (comma == std::string_view::npos) break;
    text.remove_prefix(comma + 1);
  }
  return values;
}

void appendTraits(std::string& out, const PlayerTraits& traits)
{
  using TextEncoding::appendInt;
  appendInt(out, int{traits.professionalism});
  out.push_back(',');
  appendInt(out, int{traits.ambition});
  out.push_back(',');
  appendInt(out, int{traits.temperament});
  out.push_back(',');
  appendInt(out, int{traits.loyalty});
  out.push_back(',');
  appendInt(out, int{traits.injury_proneness});
}

PlayerTraits decodeTraits(std::string_view text)
{
  std::size_t count = 0;
  const auto values = parseNumbers<5>(text, count);
  PlayerTraits traits;
  if (count < values.size()) return traits;
  const auto trait = [](double value)
  { return static_cast<std::uint8_t>(std::clamp(value, 1.0, 100.0)); };
  traits.professionalism = trait(values[0]);
  traits.ambition = trait(values[1]);
  traits.temperament = trait(values[2]);
  traits.loyalty = trait(values[3]);
  traits.injury_proneness = trait(values[4]);
  return traits;
}

constexpr std::size_t DYNAMICS_FIELDS = 14 + PlayerDynamics::FORM_WINDOW;

// CSV: condition, sharpness, morale (2 decimals), playing share (3), the
// integer fields, then the recent ratings (1 decimal).
void appendDynamics(std::string& out, const PlayerDynamics& dynamics)
{
  using TextEncoding::appendFixed;
  using TextEncoding::appendInt;
  appendFixed(out, dynamics.condition, 2);
  out.push_back(',');
  appendFixed(out, dynamics.sharpness, 2);
  out.push_back(',');
  appendFixed(out, dynamics.morale, 2);
  out.push_back(',');
  appendFixed(out, dynamics.playing_share, 3);
  for (const int value :
       {static_cast<int>(std::to_underlying(dynamics.injury)),
        int{dynamics.injury_days},
        static_cast<int>(std::to_underlying(dynamics.last_injury)),
        dynamics.last_injury_day, dynamics.last_match_day,
        int{dynamics.season_appearances}, int{dynamics.season_minutes},
        int{dynamics.week_minutes}, int{dynamics.transfer_interest_weeks},
        int{dynamics.rating_count}})
  {
    out.push_back(',');
    appendInt(out, value);
  }
  for (const float rating : dynamics.recent_ratings)
  {
    out.push_back(',');
    appendFixed(out, rating, 1);
  }
}

/** JSON object of the stats; floats in their shortest round-trip form. */
void appendStats(std::string& out, const std::map<std::string, float>& stats)
{
  out.push_back('{');
  bool first = true;
  for (const auto& [name, value] : stats)
  {
    if (!first) out.push_back(',');
    first = false;
    out.push_back('"');
    for (const char c : name)
    {
      if (c == '"' || c == '\\') out.push_back('\\');
      if (static_cast<unsigned char>(c) < 0x20)
        std::format_to(std::back_inserter(out), "\\u{:04x}",
                       static_cast<unsigned>(c));
      else
        out.push_back(c);
    }
    out += "\":";
    TextEncoding::appendShortest(out, value);
  }
  out.push_back('}');
}

/**
 * Encodes and binds the columns of a player row (team_id ... squad_number).
 * Buffers are reused across rows and must outlive the statement step.
 */
class PlayerRowBinder
{
 public:
  void bind(sqlite3_stmt* stmt, const Player& player, int index)
  {
    stats.clear();
    appendStats(stats, player.getStats());
    role = RoleUtils::toString(player.getRole());
    traits.clear();
    appendTraits(traits, player.getTraits());
    dynamics.clear();
    appendDynamics(dynamics, player.getDynamics());
    const auto nationality = languageToString.find(player.getNationality());
    const std::string_view nationality_text =
        nationality != languageToString.end()
            ? std::string_view(nationality->second)
            : std::string_view("English");
    const std::string_view foot =
        player.getFoot() == Foot::Left ? "Left" : "Right";

    sqlite3_bind_int(stmt, index++, static_cast<int>(player.getTeamId()));
    bindText(stmt, index++, player.getFirstName());
    bindText(stmt, index++, player.getLastName());
    sqlite3_bind_int(stmt, index++, player.getAge());
    bindText(stmt, index++, role);
    bindText(stmt, index++, nationality_text);
    sqlite3_bind_int(stmt, index++, static_cast<int>(player.getWage()));
    sqlite3_bind_int(stmt, index++, player.getContractYears());
    sqlite3_bind_int(stmt, index++, player.getHeight());
    bindText(stmt, index++, foot);
    bindText(stmt, index++, stats);
    sqlite3_bind_int(stmt, index++, static_cast<int>(player.getStatus()));
    sqlite3_bind_double(stmt, index++,
                        static_cast<double>(player.getPotential()));
    bindText(stmt, index++, traits);
    bindText(stmt, index++, dynamics);
    sqlite3_bind_int(stmt, index, player.getSquadNumber());
  }

 private:
  static void bindText(sqlite3_stmt* stmt, int index, std::string_view text)
  {
    sqlite3_bind_text(stmt, index, text.data(), static_cast<int>(text.size()),
                      SQLITE_STATIC);
  }

  std::string stats;
  std::string role;
  std::string traits;
  std::string dynamics;
};

PlayerDynamics decodeDynamics(std::string_view text)
{
  std::size_t count = 0;
  const auto values = parseNumbers<DYNAMICS_FIELDS>(text, count);
  PlayerDynamics dynamics;
  if (count < DYNAMICS_FIELDS) return dynamics;
  const auto injury = [](double value)
  {
    const auto raw = static_cast<int>(value);
    return raw > 0 &&
                   raw < static_cast<int>(std::to_underlying(InjuryType::COUNT))
               ? static_cast<InjuryType>(raw)
               : InjuryType::None;
  };
  dynamics.condition = static_cast<float>(std::clamp(values[0], 0.0, 100.0));
  dynamics.sharpness = static_cast<float>(std::clamp(values[1], 0.0, 100.0));
  dynamics.morale = static_cast<float>(std::clamp(values[2], 0.0, 100.0));
  dynamics.playing_share = static_cast<float>(std::clamp(values[3], 0.0, 1.0));
  dynamics.injury = injury(values[4]);
  dynamics.injury_days = static_cast<std::uint16_t>(values[5]);
  if (dynamics.injury == InjuryType::None) dynamics.injury_days = 0;
  dynamics.last_injury = injury(values[6]);
  dynamics.last_injury_day = static_cast<std::int32_t>(values[7]);
  dynamics.last_match_day = static_cast<std::int32_t>(values[8]);
  dynamics.season_appearances = static_cast<std::uint16_t>(values[9]);
  dynamics.season_minutes = static_cast<std::uint16_t>(values[10]);
  dynamics.week_minutes = static_cast<std::uint16_t>(values[11]);
  dynamics.transfer_interest_weeks = static_cast<std::uint8_t>(values[12]);
  dynamics.rating_count = static_cast<std::uint8_t>(std::min<double>(
      values[13], static_cast<double>(PlayerDynamics::FORM_WINDOW)));
  for (std::size_t i = 0; i < PlayerDynamics::FORM_WINDOW; ++i)
    dynamics.recent_ratings[i] = static_cast<float>(values[14 + i]);
  return dynamics;
}

const char* columnText(sqlite3_stmt* stmt, int column)
{
  const unsigned char* text = sqlite3_column_text(stmt, column);
  return text ? reinterpret_cast<const char*>(text) : "";
}

constexpr int PLAYER_PARAM_COUNT = 16;

class InvalidPlayerStatsError : public std::runtime_error
{
 public:
  using std::runtime_error::runtime_error;
};

std::map<std::string, float> parsePlayerStats(std::string_view encodedStats)
{
  StatsSaxParser parser;
  if (!nlohmann::json::sax_parse(encodedStats, &parser))
  {
    throw InvalidPlayerStatsError("Invalid player stats JSON in database");
  }
  return parser.takeStats();
}
}  // namespace

PlayerRepository::PlayerRepository(std::shared_ptr<DatabaseConnection> conn)
    : db_conn(conn)
{
}

std::vector<Player> PlayerRepository::loadAllPlayers() const
{
  sqlite3_stmt* stmt =
      db_conn->prepareStatement(SQLLoader::getQuery(Query::SELECT_ALL_PLAYERS));
  std::vector<Player> players;
  sqlite3_stmt* count_stmt =
      db_conn->prepareStatement("SELECT COUNT(*) FROM Players;");
  if (sqlite3_step(count_stmt) == SQLITE_ROW)
  {
    players.reserve(
        static_cast<std::size_t>(sqlite3_column_int64(count_stmt, 0)));
  }
  sqlite3_finalize(count_stmt);

  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    auto id = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
    auto team_id = static_cast<uint32_t>(sqlite3_column_int(stmt, 1));
    auto first_name =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    auto last_name =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    int age = sqlite3_column_int(stmt, 4);
    auto role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    auto nationality_str =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    auto wage = static_cast<uint32_t>(sqlite3_column_int(stmt, 7));
    int contract_years = sqlite3_column_int(stmt, 8);
    int height = sqlite3_column_int(stmt, 9);
    auto foot_str =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
    auto stats_str =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
    int status = sqlite3_column_int(stmt, 12);

    std::string nat_str(nationality_str);
    auto it = stringToLanguage.find(nat_str);
    Language nationality =
        (it != stringToLanguage.end()) ? it->second : Language::EN;
    Foot foot = (std::string(foot_str) == "Left") ? Foot::Left : Foot::Right;

    std::map<std::string, float> stats = parsePlayerStats(stats_str);

    PlayerRole playerRole = RoleUtils::fromString(role);

    Player& player = players.emplace_back(
        id, team_id, first_name, last_name, playerRole, nationality, wage,
        status, age, contract_years, height, foot, std::move(stats));
    if (const double potential = sqlite3_column_double(stmt, 13);
        potential > 0.0)
      player.setPotential(static_cast<float>(potential));
    player.setTraits(decodeTraits(columnText(stmt, 14)));
    player.mutableDynamics() = decodeDynamics(columnText(stmt, 15));
    const int squad_number = sqlite3_column_int(stmt, 16);
    if (SquadNumbers::isValid(squad_number))
      player.setSquadNumber(static_cast<std::uint8_t>(squad_number));
  }

  sqlite3_finalize(stmt);
  return players;
}

void PlayerRepository::insertPlayer(const Player& player) const
{
  sqlite3_stmt* stmt =
      db_conn->prepareStatement(SQLLoader::getQuery(Query::INSERT_PLAYER));
  PlayerRowBinder binder;
  binder.bind(stmt, player, 1);
  db_conn->executeStep(stmt);
  sqlite3_finalize(stmt);
}

void PlayerRepository::insertPlayers(
    const std::vector<std::reference_wrapper<const Player>>& players) const
{
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      SQLLoader::getQuery(Query::INSERT_PLAYER_WITH_ID));
  PlayerRowBinder binder;
  for (const auto& player_ref : players)
  {
    const Player& player = player_ref.get();
    sqlite3_bind_int(stmt, 1, static_cast<int>(player.getId()));
    binder.bind(stmt, player, 2);
    db_conn->executeStep(stmt);
    sqlite3_reset(stmt);
  }
  sqlite3_finalize(stmt);
}

void PlayerRepository::insertPlayerWithId(const Player& player) const
{
  insertPlayers({std::cref(player)});
}

void PlayerRepository::updatePlayer(const Player& player) const
{
  updatePlayers({std::cref(player)});
}

void PlayerRepository::updatePlayers(
    const std::vector<std::reference_wrapper<const Player>>& players) const
{
  // Every player changes daily (condition, training), so rows are rewritten;
  // the cost is kept down by reusing statements and encoding buffers. Players
  // created since the last save (youth intake) are inserted.
  sqlite3_stmt* update =
      db_conn->prepareStatement(SQLLoader::getQuery(Query::UPDATE_PLAYER));
  sqlite3_stmt* insert = nullptr;
  PlayerRowBinder binder;
  for (const auto& player_ref : players)
  {
    const Player& player = player_ref.get();
    binder.bind(update, player, 1);
    sqlite3_bind_int(update, PLAYER_PARAM_COUNT + 1,
                     static_cast<int>(player.getId()));
    db_conn->executeStep(update);
    sqlite3_reset(update);
    if (sqlite3_changes(db_conn->getRaw()) > 0) continue;
    if (!insert)
      insert = db_conn->prepareStatement(
          SQLLoader::getQuery(Query::INSERT_PLAYER_WITH_ID));
    sqlite3_bind_int(insert, 1, static_cast<int>(player.getId()));
    binder.bind(insert, player, 2);
    db_conn->executeStep(insert);
    sqlite3_reset(insert);
  }
  sqlite3_finalize(update);
  sqlite3_finalize(insert);
}

void PlayerRepository::deletePlayer(PlayerID player_id) const
{
  sqlite3_stmt* stmt =
      db_conn->prepareStatement(SQLLoader::getQuery(Query::DELETE_PLAYER));
  sqlite3_bind_int(stmt, 1, static_cast<int>(player_id));

  db_conn->executeStep(stmt);
  sqlite3_finalize(stmt);
}

void PlayerRepository::transferPlayer(PlayerID player_id,
                                      uint16_t new_team_id) const
{
  sqlite3_stmt* stmt =
      db_conn->prepareStatement(SQLLoader::getQuery(Query::TRANSFER_PLAYER));

  sqlite3_bind_int(stmt, 1, new_team_id);
  sqlite3_bind_int(stmt, 2, static_cast<int>(player_id));

  db_conn->executeStep(stmt);
  sqlite3_finalize(stmt);
}

// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/match_insights.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>

#include "model/match_engine.h"
#include "model/match_report.h"

namespace
{
namespace T = MatchTracking;
namespace I = MatchInsights;

constexpr std::uint8_t DETAIL_VERSION = 1;
constexpr float XA_SCALE = 100.0f;
constexpr float POSITION_SCALE = 255.0f;
/** Columns from which a touch is in the final third (4 of 12). */
constexpr std::size_t FINAL_THIRD_COLUMN = T::GRID_COLUMNS * 2 / 3;
constexpr std::size_t FLANK_ROWS = T::GRID_ROWS / 3;
/** Buckets of a spell (three buckets of five minutes). */
constexpr std::size_t SPELL_BUCKETS = 3;

// ---- Varint byte stream ----------------------------------------------------

void putVarint(std::vector<std::uint8_t>& out, std::uint64_t value)
{
  while (value >= 0x80)
  {
    out.push_back(static_cast<std::uint8_t>(value | 0x80));
    value >>= 7;
  }
  out.push_back(static_cast<std::uint8_t>(value));
}

class Reader
{
 public:
  explicit Reader(std::span<const std::uint8_t> data) : bytes(data) {}

  bool byte(std::uint8_t& out)
  {
    if (at >= bytes.size()) return fail();
    out = bytes[at++];
    return true;
  }

  bool varint(std::uint64_t& out)
  {
    out = 0;
    for (int shift = 0; shift < 64; shift += 7)
    {
      std::uint8_t raw = 0;
      if (!byte(raw)) return false;
      const std::byte next{raw};
      out |= static_cast<std::uint64_t>(
                 std::to_integer<std::uint8_t>(next & std::byte{0x7F}))
             << shift;
      if ((next & std::byte{0x80}) == std::byte{0}) return true;
    }
    return fail();
  }

  template <typename Int>
  bool small(Int& out, std::uint64_t limit)
  {
    std::uint64_t value = 0;
    if (!varint(value) || value > limit) return fail();
    out = static_cast<Int>(value);
    return true;
  }

  [[nodiscard]] bool ok() const { return good; }
  [[nodiscard]] bool done() const { return at == bytes.size(); }

 private:
  bool fail()
  {
    good = false;
    return false;
  }

  std::span<const std::uint8_t> bytes;
  std::size_t at = 0;
  bool good = true;
};

std::uint8_t toByte(int value)
{
  return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

std::string oneDecimal(float value) { return std::format("{:.1f}", value); }

std::string percent(float share)
{
  return std::format("{:.0f}%", std::round(share * 100.0f));
}

int regulationEnd(int period)
{
  const int clamped = std::clamp(period, 1, 4);
  return clamped <= 2 ? 45 * clamped : 90 + 15 * (clamped - 2);
}

bool isGoal(const ShotRecord& shot)
{
  return shot.outcome == ShotOutcome::Goal;
}

const char* flankKey(std::size_t flank)
{
  return flank == 0 ? "@SUM_FLANK_LEFT" : "@SUM_FLANK_RIGHT";
}
}  // namespace

// ---- MatchDetail -----------------------------------------------------------

std::optional<std::size_t> MatchDetail::indexOf(PlayerID player) const
{
  const auto found = std::ranges::find(players, player, &DetailPlayer::player);
  if (found == players.end()) return std::nullopt;
  return static_cast<std::size_t>(found - players.begin());
}

std::vector<std::uint8_t> MatchDetail::encode() const
{
  std::vector<std::uint8_t> out;
  out.reserve(64 + players.size() * 48 + links.size() * 3);
  out.push_back(DETAIL_VERSION);
  out.push_back(static_cast<std::uint8_t>(T::GRID_COLUMNS));
  out.push_back(static_cast<std::uint8_t>(T::GRID_ROWS));
  out.push_back(static_cast<std::uint8_t>(T::BUCKETS));
  putVarint(out, players.size());
  for (const DetailPlayer& line : players)
  {
    putVarint(out, line.player);
    out.push_back(line.home ? 1 : 0);
    putVarint(out, line.touches);
    out.push_back(toByte(static_cast<int>(
        std::lround(std::clamp(line.avg_x, 0.0f, 1.0f) * POSITION_SCALE))));
    out.push_back(toByte(static_cast<int>(
        std::lround(std::clamp(line.avg_y, 0.0f, 1.0f) * POSITION_SCALE))));
    putVarint(out, line.progressive_passes);
    putVarint(out, line.pressures);
    putVarint(out, static_cast<std::uint64_t>(std::lround(
                       std::max(line.expected_assists, 0.0f) * XA_SCALE)));
    // Sparse cells: count of filled cells, then (gap from the last, count).
    const auto filled = std::ranges::count_if(
        line.cells, [](std::uint8_t value) { return value > 0; });
    putVarint(out, static_cast<std::uint64_t>(filled));
    std::size_t previous = 0;
    for (std::size_t cell = 0; cell < line.cells.size(); ++cell)
    {
      if (line.cells[cell] == 0) continue;
      putVarint(out, cell - previous);
      out.push_back(line.cells[cell]);
      previous = cell;
    }
  }
  putVarint(out, links.size());
  for (const PassLink& link : links)
  {
    out.push_back(link.from);
    out.push_back(link.to);
    putVarint(out, link.count);
  }
  for (const auto& side : final_third)
    out.insert(out.end(), side.begin(), side.end());
  putVarint(out, substitutions.size());
  for (const DetailSubstitution& change : substitutions)
  {
    out.push_back(change.minute);
    out.push_back(change.home ? 1 : 0);
    putVarint(out, change.off);
    putVarint(out, change.on);
  }
  return out;
}

std::optional<MatchDetail> MatchDetail::decode(
    std::span<const std::uint8_t> data)
{
  Reader in(data);
  std::uint8_t version = 0;
  std::uint8_t columns = 0;
  std::uint8_t rows = 0;
  if (std::uint8_t buckets = 0;
      !in.byte(version) || version != DETAIL_VERSION || !in.byte(columns) ||
      !in.byte(rows) || !in.byte(buckets) || columns != T::GRID_COLUMNS ||
      rows != T::GRID_ROWS || buckets != T::BUCKETS)
    return std::nullopt;
  MatchDetail detail;
  std::size_t count = 0;
  if (!in.small(count, T::MAX_PLAYERS)) return std::nullopt;
  detail.players.resize(count);
  for (DetailPlayer& line : detail.players)
  {
    std::uint8_t home = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint64_t xa = 0;
    std::size_t filled = 0;
    if (!in.small(line.player, 0xFFFF'FFFFULL) || !in.byte(home) ||
        !in.small(line.touches, 0xFFFF) || !in.byte(x) || !in.byte(y) ||
        !in.small(line.progressive_passes, 0xFFFF) ||
        !in.small(line.pressures, 0xFFFF) || !in.varint(xa) ||
        !in.small(filled, T::CELLS))
      return std::nullopt;
    line.home = home != 0;
    line.avg_x = static_cast<float>(x) / POSITION_SCALE;
    line.avg_y = static_cast<float>(y) / POSITION_SCALE;
    line.expected_assists = static_cast<float>(xa) / XA_SCALE;
    std::size_t cell = 0;
    for (std::size_t index = 0; index < filled; ++index)
    {
      std::size_t gap = 0;
      std::uint8_t value = 0;
      if (!in.small(gap, T::CELLS) || !in.byte(value)) return std::nullopt;
      cell += gap;
      if (cell >= T::CELLS) return std::nullopt;
      line.cells[cell] = value;
    }
  }
  std::size_t links = 0;
  if (!in.small(links, T::MAX_PLAYERS * T::MAX_PLAYERS)) return std::nullopt;
  detail.links.resize(links);
  for (PassLink& link : detail.links)
  {
    if (!in.byte(link.from) || !in.byte(link.to) ||
        !in.small(link.count, 0xFFFF) || link.from >= count || link.to >= count)
      return std::nullopt;
  }
  for (auto& side : detail.final_third)
    for (std::uint8_t& value : side)
      if (!in.byte(value)) return std::nullopt;
  std::size_t changes = 0;
  if (!in.small(changes, 64)) return std::nullopt;
  detail.substitutions.resize(changes);
  for (DetailSubstitution& change : detail.substitutions)
  {
    std::uint8_t home = 0;
    if (!in.byte(change.minute) || !in.byte(home) ||
        !in.small(change.off, 0xFFFF'FFFFULL) ||
        !in.small(change.on, 0xFFFF'FFFFULL))
      return std::nullopt;
    change.home = home != 0;
  }
  if (!in.ok() || !in.done()) return std::nullopt;
  return detail;
}

MatchDetail captureDetail(const MatchEngine& engine)
{
  MatchDetail detail;
  const MatchTracker& tracker = engine.getTracker();
  const std::vector<PlayerMatchStats>& stats = engine.getPlayerStats();
  std::array<std::uint8_t, T::MAX_PLAYERS> index_of{};
  index_of.fill(0xFF);
  const std::size_t tracked = std::min(stats.size(), T::MAX_PLAYERS);
  for (std::size_t index = 0; index < tracked; ++index)
  {
    const PlayerMatchStats& entry = stats[index];
    const TrackedPlayer* numbers = tracker.player(index);
    if (const bool touched = numbers != nullptr && numbers->touch_count > 0;
        entry.minutesPlayed <= 0.0f && !touched)
      continue;
    index_of[index] = static_cast<std::uint8_t>(detail.players.size());
    DetailPlayer& line = detail.players.emplace_back();
    line.player = entry.playerId;
    line.home = entry.isHomeTeam;
    if (numbers == nullptr) continue;
    line.touches = numbers->touch_count;
    if (numbers->touch_count > 0)
    {
      const auto touches = static_cast<float>(numbers->touch_count);
      line.avg_x = numbers->sum_x / touches;
      line.avg_y = numbers->sum_y / touches;
    }
    line.progressive_passes = numbers->progressive_passes;
    line.pressures = numbers->pressures;
    line.expected_assists = numbers->expected_assists;
    for (std::size_t cell = 0; cell < T::CELLS; ++cell)
      line.cells[cell] = toByte(numbers->touches[cell]);
  }
  for (std::size_t from = 0; from < tracked; ++from)
  {
    if (index_of[from] == 0xFF) continue;
    for (std::size_t to = 0; to < tracked; ++to)
    {
      const std::uint16_t count = tracker.passes(from, to);
      if (count == 0 || index_of[to] == 0xFF || from == to ||
          stats[from].isHomeTeam != stats[to].isHomeTeam)
        continue;
      detail.links.push_back({index_of[from], index_of[to], count});
    }
  }
  const auto& touches = tracker.finalThirdTouches();
  for (std::size_t side = 0; side < 2; ++side)
    for (std::size_t bucket = 0; bucket < T::BUCKETS; ++bucket)
      detail.final_third[side][bucket] = toByte(touches[side][bucket]);
  for (const MatchSubstitution& change : engine.getSubstitutions())
  {
    detail.substitutions.push_back({toByte(static_cast<int>(change.timeMinute)),
                                    change.isHomeTeam, change.outgoingPlayerId,
                                    change.incomingPlayerId});
  }
  return detail;
}

std::array<int, 3> finalThirdByFlank(const MatchDetail& detail, bool home)
{
  std::array<int, 3> flanks{};
  for (const DetailPlayer& line : detail.players)
  {
    if (line.home != home) continue;
    for (std::size_t row = 0; row < T::GRID_ROWS; ++row)
      for (std::size_t column = FINAL_THIRD_COLUMN; column < T::GRID_COLUMNS;
           ++column)
        flanks[std::min<std::size_t>(row / FLANK_ROWS, 2)] +=
            line.cells[row * T::GRID_COLUMNS + column];
  }
  return flanks;
}

// ---- Key moments -----------------------------------------------------------

std::vector<KeyMoment> keyMoments(const InsightInput& input)
{
  std::vector<KeyMoment> moments;
  if (input.report == nullptr) return moments;
  for (const MatchReportEvent& event : input.report->events)
  {
    KeyMoment moment;
    moment.minute = event.minute;
    moment.added = event.added_minute;
    moment.player = event.player;
    moment.home = event.home;
    using enum MatchEventKind;
    using enum KeyMoment::Kind;
    switch (event.kind)
    {
      case GOAL:
        moment.kind = Goal;
        moment.other = event.assist;
        break;
      case OWN_GOAL:
        moment.kind = OwnGoal;
        moment.home = !event.home;
        break;
      case YELLOW_CARD:
        moment.kind = Yellow;
        break;
      case SECOND_YELLOW:
        moment.kind = SecondYellow;
        break;
      case RED_CARD:
        moment.kind = Red;
        break;
    }
    moments.push_back(moment);
  }
  // Goals are linked to their shot; missed big chances and woodwork are
  // moments of their own.
  for (std::size_t index = 0; index < input.shots.size(); ++index)
  {
    const ShotRecord& shot = input.shots[index];
    const auto whole = static_cast<int>(std::max(shot.minute, 0.0f));
    const int end = regulationEnd(shot.period);
    if (isGoal(shot))
    {
      KeyMoment* best = nullptr;
      int gap = 3;
      for (KeyMoment& moment : moments)
      {
        if (moment.kind != KeyMoment::Kind::Goal || moment.shot >= 0 ||
            moment.player != shot.player)
          continue;
        const int distance = std::abs(static_cast<int>(moment.minute) - whole);
        if (distance < gap)
        {
          gap = distance;
          best = &moment;
        }
      }
      if (best != nullptr)
      {
        best->shot = static_cast<int>(index);
        best->xg = shot.xg;
      }
      continue;
    }
    const bool woodwork = shot.outcome == ShotOutcome::Woodwork;
    if (!woodwork && shot.xg < I::BIG_CHANCE_XG) continue;
    KeyMoment moment;
    moment.kind =
        woodwork ? KeyMoment::Kind::Woodwork : KeyMoment::Kind::BigChance;
    moment.minute = toByte(whole);
    moment.added = toByte(whole >= end ? whole - end : 0);
    moment.home = shot.home;
    moment.player = shot.player;
    moment.xg = shot.xg;
    moment.shot = static_cast<int>(index);
    moments.push_back(moment);
  }
  if (input.detail != nullptr)
  {
    for (const DetailSubstitution& change : input.detail->substitutions)
    {
      KeyMoment moment;
      moment.kind = KeyMoment::Kind::Substitution;
      moment.minute = change.minute;
      moment.home = change.home;
      moment.player = change.on;
      moment.other = change.off;
      moments.push_back(moment);
    }
  }
  std::ranges::stable_sort(moments, {}, &KeyMoment::minute);
  return moments;
}

// ---- Summary ---------------------------------------------------------------

namespace
{
struct SideFacts
{
  int goals = 0;
  float xg = 0.0f;
  int shots = 0;
  int big_misses = 0;
  int set_piece_goals = 0;
  int saves = 0;
  int pressures = 0;
};

struct Candidate
{
  int priority = 0;
  AnalysisLine line;
};

/** The managed side's defender most exposed on one of its flanks. */
PlayerID exposedDefender(const MatchDetail& detail, bool home,
                         std::size_t own_flank)
{
  PlayerID best = 0;
  int most = 0;
  for (const DetailPlayer& line : detail.players)
  {
    if (line.home != home || line.touches < 10 || line.avg_x > 0.45f) continue;
    const bool on_flank =
        own_flank == 0 ? line.avg_y < 0.4f : line.avg_y > 0.6f;
    if (on_flank && line.touches > most)
    {
      most = line.touches;
      best = line.player;
    }
  }
  return best;
}

/** Strongest fifteen-minute spell of one side over the other. */
struct Spell
{
  std::size_t first = 0;
  int own = 0;
  int other = 0;
};

std::optional<Spell> strongestSpell(const MatchDetail& detail, bool home,
                                    std::size_t used_buckets)
{
  const auto& mine = detail.final_third[home ? 0 : 1];
  const auto& theirs = detail.final_third[home ? 1 : 0];
  std::optional<Spell> best;
  int best_margin = 0;
  for (std::size_t first = 0; first + SPELL_BUCKETS <= used_buckets; ++first)
  {
    Spell spell{first, 0, 0};
    for (std::size_t bucket = first; bucket < first + SPELL_BUCKETS; ++bucket)
    {
      spell.own += mine[bucket];
      spell.other += theirs[bucket];
    }
    const int margin = spell.own - spell.other;
    if (spell.own >= I::MIN_SPELL_TOUCHES && spell.own >= 2 * spell.other &&
        margin > best_margin)
    {
      best_margin = margin;
      best = spell;
    }
  }
  return best;
}
}  // namespace

std::vector<AnalysisLine> summariseMatch(const InsightInput& input)
{
  std::vector<AnalysisLine> lines;
  if (input.report == nullptr) return lines;
  const MatchReport& report = *input.report;
  const bool home = input.managed_home;
  const auto name = [&input](PlayerID id)
  { return input.name_of ? input.name_of(id) : std::string(); };

  SideFacts own;
  SideFacts opp;
  own.goals = home ? report.home_goals : report.away_goals;
  opp.goals = home ? report.away_goals : report.home_goals;
  const TeamMatchStats& own_stats =
      home ? report.home_stats : report.away_stats;
  const TeamMatchStats& opp_stats =
      home ? report.away_stats : report.home_stats;
  own.xg = own_stats.expected_goals;
  opp.xg = opp_stats.expected_goals;
  own.saves = own_stats.saves;
  for (const ShotRecord& shot : input.shots)
  {
    SideFacts& side = shot.home == home ? own : opp;
    ++side.shots;
    if (!isGoal(shot) && shot.xg >= I::BIG_CHANCE_XG) ++side.big_misses;
    if (isGoal(shot) && shot.set_piece) ++side.set_piece_goals;
  }
  if (own.shots + opp.shots == 0 && own.xg + opp.xg <= 0.0f) return lines;

  // The verdict: result against the balance of chances.
  const std::string own_xg = oneDecimal(own.xg);
  const std::string opp_xg = oneDecimal(opp.xg);
  const float gap = own.xg - opp.xg;
  const char* verdict = nullptr;
  if (own.goals > opp.goals)
  {
    if (gap >= I::CLEAR_XG_GAP)
      verdict = "SUM_WIN_DESERVED";
    else if (gap <= -I::CLEAR_XG_GAP)
      verdict = "SUM_WIN_AGAINST_RUN";
    else
      verdict = "SUM_WIN_EVEN";
  }
  else if (own.goals < opp.goals)
  {
    if (gap >= I::CLEAR_XG_GAP)
      verdict = "SUM_LOSS_UNLUCKY";
    else if (gap <= -I::CLEAR_XG_GAP)
      verdict = "SUM_LOSS_DESERVED";
    else
      verdict = "SUM_LOSS_EVEN";
  }
  else if (gap >= I::CLEAR_XG_GAP)
    verdict = "SUM_DRAW_SHOULD_WIN";
  else if (gap <= -I::CLEAR_XG_GAP)
    verdict = "SUM_DRAW_HELD";
  else
    verdict = "SUM_DRAW_EVEN";
  lines.push_back({verdict, {own_xg, opp_xg}});

  std::vector<Candidate> candidates;
  if (const float own_finish = static_cast<float>(own.goals) - own.xg;
      own_finish <= -I::FINISHING_GAP)
    candidates.push_back({90,
                          {"SUM_OWN_WASTEFUL",
                           {own_xg, std::to_string(own.goals),
                            std::to_string(own.big_misses)}}});
  else if (own_finish >= I::FINISHING_GAP)
    candidates.push_back(
        {70, {"SUM_OWN_CLINICAL", {own_xg, std::to_string(own.goals)}}});
  if (const float opp_finish = static_cast<float>(opp.goals) - opp.xg;
      opp_finish >= I::FINISHING_GAP)
    candidates.push_back(
        {85, {"SUM_OPP_CLINICAL", {opp_xg, std::to_string(opp.goals)}}});
  else if (opp_finish <= -I::FINISHING_GAP)
    candidates.push_back(
        {75,
         {"SUM_OPP_WASTEFUL",
          {opp_xg, std::to_string(opp.goals), std::to_string(own.saves)}}});

  if (own.shots >= I::MIN_SHOTS_FOR_QUALITY &&
      own.xg / static_cast<float>(own.shots) < I::LOW_SHOT_QUALITY)
    candidates.push_back(
        {55,
         {"SUM_LOW_QUALITY",
          {std::to_string(own.shots),
           std::format("{:.2f}", own.xg / static_cast<float>(own.shots))}}});

  if (own.set_piece_goals > 0)
    candidates.push_back(
        {65, {"SUM_SET_PIECE_FOR", {std::to_string(own.set_piece_goals)}}});
  if (opp.set_piece_goals > 0)
    candidates.push_back(
        {80, {"SUM_SET_PIECE_AGAINST", {std::to_string(opp.set_piece_goals)}}});

  if (input.detail != nullptr && !input.detail->empty())
  {
    const MatchDetail& detail = *input.detail;
    // Where the opponent's attacks came from: their left is our right.
    const std::array<int, 3> theirs = finalThirdByFlank(detail, !home);
    if (const int their_total = theirs[0] + theirs[1] + theirs[2];
        their_total >= I::MIN_FLANK_TOUCHES)
    {
      const std::size_t flank = theirs[0] >= theirs[2] ? 0 : 2;
      const float share =
          static_cast<float>(theirs[flank]) / static_cast<float>(their_total);
      if (share >= I::FLANK_SHARE)
      {
        const std::size_t ours = flank == 0 ? 2 : 0;
        const PlayerID defender = exposedDefender(detail, home, ours);
        if (defender != 0 && !name(defender).empty())
          candidates.push_back({78,
                                {"SUM_OVERLOAD_AGAINST_PLAYER",
                                 {flankKey(flank), flankKey(ours),
                                  percent(share), name(defender)}}});
        else
          candidates.push_back(
              {76,
               {"SUM_OVERLOAD_AGAINST",
                {flankKey(flank), flankKey(ours), percent(share)}}});
      }
    }
    const std::array<int, 3> mine = finalThirdByFlank(detail, home);
    if (const int my_total = mine[0] + mine[1] + mine[2];
        my_total >= I::MIN_FLANK_TOUCHES)
    {
      const std::size_t flank = mine[0] >= mine[2] ? 0 : 2;
      const float share =
          static_cast<float>(mine[flank]) / static_cast<float>(my_total);
      if (share >= I::FLANK_SHARE)
        candidates.push_back(
            {50, {"SUM_OWN_CHANNEL", {flankKey(flank), percent(share)}}});
    }

    const std::size_t used =
        report.extra_time ? T::BUCKETS : T::BUCKETS * 3 / 4;
    const std::optional<Spell> own_spell = strongestSpell(detail, home, used);
    const std::optional<Spell> opp_spell = strongestSpell(detail, !home, used);
    const auto spellLine = [](const char* key, const Spell& spell)
    {
      const std::size_t start = spell.first * T::BUCKET_MINUTES;
      const std::size_t end = start + SPELL_BUCKETS * T::BUCKET_MINUTES;
      return AnalysisLine{
          key,
          {std::to_string(start), std::to_string(end),
           std::to_string(spell.own), std::to_string(spell.other)}};
    };
    if (opp_spell && (!own_spell || opp_spell->own - opp_spell->other >=
                                        own_spell->own - own_spell->other))
      candidates.emplace_back(60, spellLine("SUM_SPELL_OPP", *opp_spell));
    else if (own_spell)
      candidates.emplace_back(58, spellLine("SUM_SPELL_OWN", *own_spell));

    const PassLink* busiest = nullptr;
    for (const PassLink& link : detail.links)
      if (detail.players[link.from].home == home &&
          (busiest == nullptr || link.count > busiest->count))
        busiest = &link;
    if (busiest != nullptr && busiest->count >= 8)
    {
      const std::string from = name(detail.players[busiest->from].player);
      const std::string to = name(detail.players[busiest->to].player);
      if (!from.empty() && !to.empty())
        candidates.push_back(
            {40,
             {"SUM_CONNECTION", {from, to, std::to_string(busiest->count)}}});
    }

    for (const DetailPlayer& line : detail.players)
      (line.home == home ? own : opp).pressures += line.pressures;
    if (own.pressures >= 40 && static_cast<float>(own.pressures) >=
                                   1.3f * static_cast<float>(opp.pressures))
      candidates.push_back(
          {45,
           {"SUM_PRESS_OWN",
            {std::to_string(own.pressures), std::to_string(opp.pressures)}}});
    else if (opp.pressures >= 40 &&
             static_cast<float>(opp.pressures) >=
                 1.3f * static_cast<float>(own.pressures))
      candidates.push_back(
          {45,
           {"SUM_PRESS_OPP",
            {std::to_string(own.pressures), std::to_string(opp.pressures)}}});
  }

  std::ranges::stable_sort(candidates, std::greater{}, &Candidate::priority);
  for (Candidate& candidate : candidates)
  {
    if (static_cast<int>(lines.size()) >= I::MAX_SUMMARY_LINES) break;
    lines.push_back(std::move(candidate.line));
  }
  return lines;
}

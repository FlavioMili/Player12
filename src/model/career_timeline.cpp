// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/career_timeline.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <system_error>
#include <tuple>
#include <utility>

#include "database/gamedata.h"
#include "global/language_manager.h"
#include "model/awards.h"
#include "model/calendar.h"
#include "model/competition.h"
#include "model/game.h"
#include "model/inbox.h"
#include "model/manager_career.h"
#include "model/records.h"
#include "model/season_history.h"
#include "model/transfer_market.h"

namespace
{
/** Season verdicts (titles, promotions, finishes) close on 30 June. */
GameDateValue seasonEnd(std::uint16_t start_year)
{
  return GameDateValue(static_cast<std::uint16_t>(start_year + 1U), 6, 30);
}

/** Monthly awards are given on the 1st of the next month, season awards
 * on 1 June. */
GameDateValue awardDate(const AwardRecord& record)
{
  if (record.month == 0)
    return GameDateValue(static_cast<std::uint16_t>(record.season_year + 1U), 6,
                         1);
  const std::uint16_t year =
      record.month >= 7 ? record.season_year
                        : static_cast<std::uint16_t>(record.season_year + 1U);
  return record.month == 12
             ? GameDateValue(static_cast<std::uint16_t>(year + 1U), 1, 1)
             : GameDateValue(year, static_cast<std::uint8_t>(record.month + 1U),
                             1);
}

/** The spell covers @p date (a current spell has no end). */
bool during(const ManagerStint& stint, const GameDateValue& date)
{
  return !(date < stint.start) &&
         (stint.reason == DepartureReason::Current || !(stint.end < date));
}

std::string leagueArg(const GameData* gamedata, LeagueID league_id)
{
  if (gamedata == nullptr) return {};
  const auto league = gamedata->getLeague(league_id);
  return league ? Competitions::leagueNameArg(league->get()) : std::string();
}

std::string playerName(const GameData* gamedata, PlayerID id)
{
  if (gamedata == nullptr) return {};
  const auto player = gamedata->getPlayer(id);
  return player ? player->get().getName() : std::string();
}

std::string teamName(const GameData* gamedata, TeamID id)
{
  if (gamedata == nullptr) return {};
  const auto team = gamedata->getTeam(id);
  return team ? team->get().getName() : std::string();
}

const char* departureKey(DepartureReason reason)
{
  using enum DepartureReason;
  switch (reason)
  {
    case Sacked:
      return "TIMELINE_SACKED";
    case Resigned:
      return "TIMELINE_RESIGNED";
    case Moved:
      return "TIMELINE_MOVED";
    case ContractExpired:
      return "TIMELINE_EXPIRED";
    case Current:
      break;
  }
  return nullptr;
}

/** Text key and the figure and detail arguments of a club record. */
std::tuple<const char*, std::string, std::string> recordText(
    const RecordEntry& record, const GameData* gamedata)
{
  using enum RecordKind;
  switch (record.kind)
  {
    case BiggestWin:
    case BiggestDefeat:
    case HighestScoringMatch:
      return {"TIMELINE_RECORD_MATCH",
              std::format("{}-{}", record.goals_for, record.goals_against),
              teamName(gamedata, record.opponent_id)};
    case RecordSigning:
    case RecordSale:
      return {"TIMELINE_RECORD_PLAYER", formatMoney(record.value), record.name};
    case TopScorerSeason:
      return {"TIMELINE_RECORD_PLAYER", std::to_string(record.value),
              record.name};
    case HighestAttendance:
    case MostGoalsSeason:
    case MostPointsSeason:
    case COUNT:
      break;
  }
  return {"TIMELINE_RECORD_VALUE", std::to_string(record.value), std::string()};
}

class Builder
{
 public:
  explicit Builder(const TimelineSources& sources) : in(sources) {}

  std::vector<TimelineEntry> run()
  {
    for (const ManagerStint& stint : in.stints) addStint(stint);
    addSeasons();
    addHonours();
    addAwards();
    std::ranges::sort(
        out,
        [](const TimelineEntry& left, const TimelineEntry& right)
        {
          return std::tuple{left.date,      left.kind,     left.team_id,
                            left.player_id, left.text_key, left.args} <
                 std::tuple{right.date,      right.kind,     right.team_id,
                            right.player_id, right.text_key, right.args};
        });
    return std::move(out);
  }

 private:
  void add(const GameDateValue& date, TimelineKind kind, TeamID team,
           std::string key, std::vector<std::string> args, PlayerID player = 0)
  {
    TimelineEntry entry;
    entry.date = date;
    entry.kind = kind;
    entry.season_year = SeasonCalendar::seasonStartYear(date);
    entry.team_id = team;
    entry.player_id = player;
    entry.text_key = std::move(key);
    entry.args = std::move(args);
    out.push_back(std::move(entry));
  }

  const ManagerStint* stintAt(TeamID team, const GameDateValue& date) const
  {
    for (const ManagerStint& stint : in.stints)
      if (stint.team_id == team && during(stint, date)) return &stint;
    return nullptr;
  }

  void addStint(const ManagerStint& stint)
  {
    add(stint.start, TimelineKind::Appointed, stint.team_id,
        "TIMELINE_APPOINTED", {stint.club_name});
    if (const char* key = departureKey(stint.reason))
      add(stint.end, TimelineKind::Departed, stint.team_id, key,
          {stint.club_name, std::to_string(stint.played),
           std::to_string(stint.won), std::to_string(stint.drawn),
           std::to_string(stint.lost)});
    addSignings(stint);
    addRecords(stint);
    addMovements(stint);
  }

  void addSignings(const ManagerStint& stint)
  {
    std::vector<const TransferRecord*> signings;
    for (const TransferRecord& record : in.transfers)
      if (record.to_team == stint.team_id && during(stint, record.date) &&
          (record.kind == TransferKind::Permanent ||
           record.kind == TransferKind::Free ||
           record.kind == TransferKind::Loan ||
           record.kind == TransferKind::PreContract))
        signings.push_back(&record);
    std::ranges::sort(
        signings,
        [](const TransferRecord* left, const TransferRecord* right)
        {
          return std::tuple{right->fee, left->date, left->player_id} <
                 std::tuple{left->fee, right->date, right->player_id};
        });
    std::size_t listed = 0;
    for (const TransferRecord* record : signings)
    {
      if (listed == CareerTimeline::KEY_SIGNINGS_PER_STINT) break;
      const std::string name = playerName(in.gamedata, record->player_id);
      if (name.empty()) continue;
      const char* key = "TIMELINE_SIGNING";
      if (record->fee == 0) key = "TIMELINE_SIGNING_FREE";
      if (record->kind == TransferKind::Loan) key = "TIMELINE_SIGNING_LOAN";
      add(record->date, TimelineKind::Signing, stint.team_id, key,
          {stint.club_name, name, formatMoney(record->fee),
           teamName(in.gamedata, record->from_team)},
          record->player_id);
      ++listed;
    }
  }

  void addRecords(const ManagerStint& stint)
  {
    for (const RecordEntry& record : in.records)
    {
      if (record.scope != RecordScope::Club ||
          record.scope_id != stint.team_id || !during(stint, record.date))
        continue;
      auto [key, figure, detail] = recordText(record, in.gamedata);
      add(record.date, TimelineKind::Record, stint.team_id, key,
          {stint.club_name, std::string("@") + recordKindKey(record.kind),
           std::move(figure), std::move(detail)},
          record.player_id);
    }
  }

  void addMovements(const ManagerStint& stint)
  {
    for (const SeasonHistoryEntry& entry : in.history)
    {
      if (entry.competition_type != MatchType::LEAGUE) continue;
      const GameDateValue date = seasonEnd(entry.start_year);
      if (!during(stint, date)) continue;
      const std::string league = leagueArg(in.gamedata, entry.competition_id);
      if (std::ranges::contains(entry.promoted, stint.team_id))
      {
        add(date, TimelineKind::Promotion, stint.team_id, "TIMELINE_PROMOTED",
            {stint.club_name, league});
        promoted.emplace_back(stint.team_id, entry.start_year);
      }
      if (std::ranges::contains(entry.relegated, stint.team_id))
        add(date, TimelineKind::Relegation, stint.team_id, "TIMELINE_RELEGATED",
            {stint.club_name, league});
    }
  }

  void addSeasons()
  {
    for (const ManagerSeasonLine& line : in.seasons)
      add(seasonEnd(line.start_year), TimelineKind::Season, line.team_id,
          "TIMELINE_SEASON",
          {line.club_name, std::to_string(line.position),
           std::to_string(line.league_size),
           leagueArg(in.gamedata, line.league_id),
           std::to_string(line.expected_position)});
  }

  void addHonours()
  {
    for (const ManagerAward& honour : in.honours)
    {
      const GameDateValue date = seasonEnd(honour.start_year);
      switch (honour.kind)
      {
        case ManagerAwardKind::LeagueTitle:
          add(date, TimelineKind::Trophy, honour.team_id,
              "TIMELINE_TROPHY_LEAGUE", {honour.club_name});
          break;
        case ManagerAwardKind::CupWin:
          add(date, TimelineKind::Trophy, honour.team_id, "TIMELINE_TROPHY_CUP",
              {honour.club_name});
          break;
        case ManagerAwardKind::Promotion:
          // Older saves without season history: the honour is the record.
          if (!std::ranges::contains(
                  promoted, std::pair{honour.team_id, honour.start_year}))
            add(date, TimelineKind::Promotion, honour.team_id,
                "TIMELINE_PROMOTED_SHORT", {honour.club_name});
          break;
        case ManagerAwardKind::ManagerOfTheSeason:
        case ManagerAwardKind::ManagerOfTheMonth:
          break;  // Dated by the league's award history below.
      }
    }
  }

  void addAwards()
  {
    for (const AwardRecord& record : in.awards)
    {
      if (record.player_id != 0 || record.name != in.manager_name ||
          (record.type != AwardType::ManagerOfMonth &&
           record.type != AwardType::ManagerOfSeason))
        continue;
      const GameDateValue date = awardDate(record);
      // Awards are given just after the period they reward.
      const ManagerStint* stint = stintAt(record.team_id, date - 1);
      if (stint == nullptr) continue;
      add(date, TimelineKind::Award, record.team_id, "TIMELINE_AWARD",
          {stint->club_name, std::string("@") + awardTypeKey(record.type),
           leagueArg(in.gamedata, record.league_id)});
    }
  }

  const TimelineSources& in;
  std::vector<TimelineEntry> out;
  std::vector<std::pair<TeamID, std::uint16_t>> promoted;
};

std::string seasonLabel(std::uint16_t year)
{
  return std::format("{}/{:02}", year, (year + 1U) % 100U);
}

std::string escapeCell(const std::string& text)
{
  std::string escaped;
  escaped.reserve(text.size());
  for (const char character : text)
  {
    if (character == '|') escaped += '\\';
    if (character == '\n')
    {
      escaped += ' ';
      continue;
    }
    escaped += character;
  }
  return escaped;
}
}  // namespace

const char* timelineKindKey(TimelineKind kind)
{
  using enum TimelineKind;
  switch (kind)
  {
    case Appointed:
      return "TIMELINE_KIND_APPOINTED";
    case Departed:
      return "TIMELINE_KIND_DEPARTED";
    case Trophy:
      return "TIMELINE_KIND_TROPHY";
    case Award:
      return "TIMELINE_KIND_AWARD";
    case Promotion:
      return "TIMELINE_KIND_PROMOTION";
    case Relegation:
      return "TIMELINE_KIND_RELEGATION";
    case Season:
      return "TIMELINE_KIND_SEASON";
    case Record:
      return "TIMELINE_KIND_RECORD";
    case Signing:
      return "TIMELINE_KIND_SIGNING";
    case COUNT:
      break;
  }
  return "TIMELINE_KIND_APPOINTED";
}

std::string TimelineEntry::text() const
{
  return formatLocalized(text_key, args);
}

std::vector<TimelineEntry> CareerTimeline::build(const TimelineSources& sources)
{
  return Builder(sources).run();
}

TimelineSources CareerTimeline::sourcesFor(const Game& game,
                                           const GameData& gamedata,
                                           std::vector<RecordEntry>& records)
{
  TimelineSources sources;
  sources.gamedata = &gamedata;
  const ManagerCareer& career = game.getCareer();
  if (career.hasProfile()) sources.manager_name = career.getProfile().name();
  sources.stints = career.getStints();
  sources.seasons = career.getSeasons();
  sources.honours = career.getAwards();
  sources.awards = game.getWorld().getAwards().history();
  sources.history = game.getCompetitions().getSeasonHistory();
  sources.transfers = game.getTransfers().history();
  records.clear();
  std::vector<TeamID> teams;
  for (const ManagerStint& stint : sources.stints)
  {
    if (std::ranges::contains(teams, stint.team_id)) continue;
    teams.push_back(stint.team_id);
    std::vector<RecordEntry> club =
        game.getWorld().getRecords().clubRecords(stint.team_id);
    records.insert(records.end(), club.begin(), club.end());
  }
  sources.records = records;
  return sources;
}

std::string CareerTimeline::toMarkdown(const TimelineSources& sources,
                                       std::span<const TimelineEntry> entries,
                                       const GameDateValue& today)
{
  std::string text;
  text += "# " +
          formatLocalized("TIMELINE_JOURNAL_TITLE", {sources.manager_name}) +
          "\n\n";
  text +=
      formatLocalized("TIMELINE_JOURNAL_WRITTEN", {today.toString()}) + "\n\n";

  text += "## " + std::string(LOC("TIMELINE_JOURNAL_CLUBS")) + "\n\n";
  text += std::format("| {} | {} | {} | {} | {} | {} | {} | {} |\n",
                      LOC("TIMELINE_COL_CLUB"), LOC("TIMELINE_COL_FROM"),
                      LOC("TIMELINE_COL_TO"), LOC("TIMELINE_COL_PLAYED"),
                      LOC("TIMELINE_COL_WON"), LOC("TIMELINE_COL_DRAWN"),
                      LOC("TIMELINE_COL_LOST"), LOC("TIMELINE_COL_TROPHIES"));
  text += "|---|---|---|---:|---:|---:|---:|---:|\n";
  for (const ManagerStint& stint : sources.stints)
  {
    const std::string until = stint.reason == DepartureReason::Current
                                  ? std::string(LOC("TIMELINE_PRESENT"))
                                  : localizedDate(stint.end.toString());
    text +=
        std::format("| {} | {} | {} | {} | {} | {} | {} | {} |\n",
                    escapeCell(stint.club_name),
                    localizedDate(stint.start.toString()), until, stint.played,
                    stint.won, stint.drawn, stint.lost, stint.trophies);
  }
  if (sources.stints.empty())
    text += std::string(LOC("TIMELINE_EMPTY_BODY")) + "\n";

  std::uint16_t season = 0;
  bool first = true;
  for (const TimelineEntry& entry : entries)
  {
    if (first || entry.season_year != season)
    {
      season = entry.season_year;
      first = false;
      text += "\n## " +
              formatLocalized("TIMELINE_SEASON_HEADER", {seasonLabel(season)}) +
              "\n\n";
    }
    text += std::format(
        "- **{}** · {} · {}\n", localizedDate(entry.date.toString()),
        LOC(timelineKindKey(entry.kind)), escapeCell(entry.text()));
  }
  return text;
}

std::string CareerTimeline::journalFileName(std::string_view manager_name,
                                            const GameDateValue& today)
{
  std::string slug;
  for (const char character : manager_name)
  {
    const auto byte = static_cast<unsigned char>(character);
    if (std::isalnum(byte) != 0 && byte < 0x80)
      slug += static_cast<char>(std::tolower(byte));
    else if (!slug.empty() && slug.back() != '-')
      slug += '-';
  }
  while (!slug.empty() && slug.back() == '-') slug.pop_back();
  if (slug.empty()) slug = "manager";
  return std::format("career-{}-{}.md", slug, today.toString());
}

std::optional<std::filesystem::path> CareerTimeline::writeJournal(
    const std::string& markdown, const std::filesystem::path& folder,
    const std::string& file_name)
{
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  if (error) return std::nullopt;
  const std::filesystem::path path = folder / file_name;
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) return std::nullopt;
  file << markdown;
  file.close();
  if (!file) return std::nullopt;
  return path;
}

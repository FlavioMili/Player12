// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/facility_projects.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "database/gamedata.h"
#include "database/sqlite_rows.h"
#include "model/calendar.h"
#include "model/club_economy.h"
#include "model/inbox.h"
#include "model/league.h"
#include "model/team.h"
#include "model/world_rng.h"

namespace
{
constexpr std::uint8_t STANDARD_MEDICAL = 50;
/** Share of the cost paid on approval. [P] */
constexpr double DEPOSIT_SHARE = 0.25;

double seasonIncome(const GameData& gamedata, const Team& team)
{
  std::vector<std::uint8_t> reputations;
  if (const auto league = gamedata.getLeague(team.getLeagueId()))
    for (const TeamID team_id : league->get().getTeamIDs())
      if (const auto other = gamedata.getTeam(team_id))
        reputations.push_back(other->get().getReputation());
  return ClubEconomy::expectedIncome(
      makeLeagueEconomy(team.getLeagueId(), reputations), team.getReputation());
}

std::uint8_t currentLevel(const Team& team, FacilityProjectType type,
                          std::uint8_t medical)
{
  using enum FacilityProjectType;
  switch (type)
  {
    case TrainingGround:
      return team.getProfile().training_facilities;
    case MedicalCentre:
      return medical;
    case StadiumExpansion:
    case COUNT:
      break;
  }
  return 0;
}
}  // namespace

float FacilityProject::progress(const GameDateValue& today) const
{
  if (completed) return 1.0f;
  const int total = dayOrdinal(end) - dayOrdinal(start);
  if (total <= 0) return 1.0f;
  return std::clamp(static_cast<float>(dayOrdinal(today) - dayOrdinal(start)) /
                        static_cast<float>(total),
                    0.0f, 1.0f);
}

ProjectQuote FacilityProjects::quote(const GameData& gamedata, TeamID team_id,
                                     FacilityProjectType type,
                                     std::uint32_t seats) const
{
  const auto team = gamedata.getTeam(team_id);
  if (!team || team_id == FREE_AGENTS_TEAM_ID) return {};
  const Team& club = team->get();
  return BoardModel::quoteProject(
      type, currentLevel(club, type, medicalLevel(team_id)),
      club.getProfile().stadium_capacity, club.getReputation(),
      seasonIncome(gamedata, club), seats);
}

ProjectVerdict FacilityProjects::request(
    GameData& gamedata, const BoardState& board, TeamID team_id,
    FacilityProjectType type, std::uint32_t seats, const GameDateValue& today)
{
  using enum ProjectVerdict;
  const auto team = gamedata.getTeam(team_id);
  if (!team || team_id == FREE_AGENTS_TEAM_ID) return AtMaximum;
  Team& club = team->get();
  const ProjectQuote offer = quote(gamedata, team_id, type, seats);

  ProjectRequestContext context;
  context.confidence = board.team_id == team_id ? board.confidence : 60.0f;
  context.balance = club.getFinances().getBalance();
  context.weekly_payroll =
      club.getFinances().getCurrentWageSpending(gamedata, club);
  context.reputation = club.getReputation();
  context.current_level = currentLevel(club, type, medicalLevel(team_id));
  for (const FacilityProject& project : projects)
  {
    if (project.team_id != team_id || project.completed) continue;
    ++context.running_projects;
    context.same_type_running =
        context.same_type_running || project.type == type;
  }
  context.cooling_down = cooldownUntil(team_id, type, today).has_value();

  const ProjectVerdict verdict = BoardModel::reviewProject(offer, context);
  if (verdict != Approved)
  {
    if (verdict != AlreadyRunning && verdict != TooManyProjects &&
        verdict != Cooldown)
      cooldowns[{team_id, std::to_underlying(type)}] =
          SeasonCalendar::addDays(today, BoardModel::PROJECT_COOLDOWN_DAYS);
    return verdict;
  }

  FacilityProject project;
  project.id = next_id++;
  project.team_id = team_id;
  project.type = type;
  project.start = today;
  project.end = SeasonCalendar::addDays(today, offer.days);
  project.cost = offer.cost;
  project.amount = offer.amount;
  project.disruption = offer.disruption;
  project.paid = static_cast<std::int64_t>(
      std::llround(static_cast<double>(offer.cost) * DEPOSIT_SHARE));
  club.getFinances().record(today, FinanceCategory::Facilities, -project.paid);
  if (project.disruption > 0)
  {
    ClubProfile profile = club.getProfile();
    profile.stadium_capacity -=
        std::min(profile.stadium_capacity, project.disruption);
    club.setProfile(profile);
  }
  projects.push_back(project);
  return verdict;
}

void FacilityProjects::onDay(GameData& gamedata, const GameDateValue& date,
                             TeamID managed_team_id, Inbox& inbox)
{
  for (FacilityProject& project : projects)
  {
    if (project.completed) continue;
    const auto team = gamedata.getTeam(project.team_id);
    if (!team) continue;
    if (date.day == 1 && date < project.end)
    {
      // The balance after the deposit is spread over the remaining months.
      const int days_left = dayOrdinal(project.end) - dayOrdinal(date);
      const int months_left = std::max(1, (days_left + 29) / 30);
      const std::int64_t instalment =
          (project.cost - project.paid) / months_left;
      if (instalment > 0)
      {
        team->get().getFinances().record(date, FinanceCategory::Facilities,
                                         -instalment);
        project.paid += instalment;
      }
    }
    if (!(date < project.end))
      complete(gamedata, project, date, managed_team_id, inbox);
  }
}

void FacilityProjects::complete(GameData& gamedata, FacilityProject& project,
                                const GameDateValue& date,
                                TeamID managed_team_id, Inbox& inbox)
{
  using enum FacilityProjectType;
  Team& club = gamedata.getTeam(project.team_id)->get();
  if (project.paid < project.cost)
  {
    club.getFinances().record(date, FinanceCategory::Facilities,
                              -(project.cost - project.paid));
    project.paid = project.cost;
  }
  project.completed = true;
  ClubProfile profile = club.getProfile();
  std::string detail;
  switch (project.type)
  {
    case TrainingGround:
      profile.training_facilities = static_cast<std::uint8_t>(std::min<int>(
          100, profile.training_facilities + static_cast<int>(project.amount)));
      detail = std::to_string(profile.training_facilities);
      break;
    case MedicalCentre:
    {
      std::uint8_t& level =
          medical.try_emplace(project.team_id, STANDARD_MEDICAL).first->second;
      level = static_cast<std::uint8_t>(
          std::min<int>(100, level + static_cast<int>(project.amount)));
      detail = std::to_string(level);
      break;
    }
    case StadiumExpansion:
    case COUNT:
      profile.stadium_capacity += project.amount + project.disruption;
      detail = std::to_string(profile.stadium_capacity);
      break;
  }
  club.setProfile(profile);
  if (project.team_id != managed_team_id) return;
  InboxMessage message;
  message.date = date;
  message.category = InboxCategory::Board;
  message.title_key = "INBOX_PROJECT_DONE_TITLE";
  message.body_key = "INBOX_PROJECT_DONE_BODY";
  message.args = {std::string("@") + BoardModel::projectTypeKey(project.type),
                  detail, formatMoney(project.cost)};
  message.team_id = managed_team_id;
  inbox.add(std::move(message));
}

std::uint8_t FacilityProjects::medicalLevel(TeamID team_id) const
{
  const auto found = medical.find(team_id);
  return found == medical.end() ? STANDARD_MEDICAL : found->second;
}

float FacilityProjects::layoffMultiplier(TeamID team_id) const
{
  return BoardModel::medicalLayoffMultiplier(medicalLevel(team_id));
}

std::vector<FacilityProject> FacilityProjects::projectsFor(TeamID team_id) const
{
  std::vector<FacilityProject> result;
  for (const FacilityProject& project : projects)
    if (project.team_id == team_id && !project.completed)
      result.push_back(project);
  for (auto it = projects.rbegin(); it != projects.rend(); ++it)
    if (it->team_id == team_id && it->completed) result.push_back(*it);
  return result;
}

std::optional<GameDateValue> FacilityProjects::cooldownUntil(
    TeamID team_id, FacilityProjectType type, const GameDateValue& today) const
{
  const auto found = cooldowns.find({team_id, std::to_underlying(type)});
  if (found == cooldowns.end() || !(today < found->second)) return std::nullopt;
  return found->second;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void FacilityProjects::clear()
{
  projects.clear();
  medical.clear();
  cooldowns.clear();
  next_id = 1;
}

void FacilityProjects::load(const std::shared_ptr<DatabaseConnection>& db_conn)
{
  using namespace SqliteRows;
  clear();
  const DatabaseConnection& db = *db_conn;
  forEach(db,
          "SELECT id, team_id, type, start_date, end_date, cost, paid, amount, "
          "disruption, completed FROM FacilityProjects ORDER BY id;",
          [this](sqlite3_stmt* stmt)
          {
            const auto type = column<std::uint8_t>(stmt, 2);
            if (type >= std::to_underlying(FacilityProjectType::COUNT)) return;
            FacilityProject project;
            project.id = column<std::uint32_t>(stmt, 0);
            project.team_id = column<TeamID>(stmt, 1);
            project.type = static_cast<FacilityProjectType>(type);
            project.start = GameDateValue::fromString(columnText(stmt, 3));
            project.end = GameDateValue::fromString(columnText(stmt, 4));
            project.cost = column<std::int64_t>(stmt, 5);
            project.paid = column<std::int64_t>(stmt, 6);
            project.amount = column<std::uint32_t>(stmt, 7);
            project.disruption = column<std::uint32_t>(stmt, 8);
            project.completed = column<int>(stmt, 9) != 0;
            next_id = std::max(next_id, project.id + 1);
            projects.push_back(project);
          });
  forEach(
      db, "SELECT team_id, medical FROM ClubFacilities;",
      [this](sqlite3_stmt* stmt)
      { medical[column<TeamID>(stmt, 0)] = column<std::uint8_t>(stmt, 1); });
  forEach(
      db, "SELECT team_id, type, until_date FROM ProjectCooldowns;",
      [this](sqlite3_stmt* stmt)
      {
        cooldowns[{column<TeamID>(stmt, 0), column<std::uint8_t>(stmt, 1)}] =
            GameDateValue::fromString(columnText(stmt, 2));
      });
}

void FacilityProjects::save(
    const std::shared_ptr<DatabaseConnection>& db_conn) const
{
  using namespace SqliteRows;
  const DatabaseConnection& db = *db_conn;
  for (const char* table :
       {"FacilityProjects", "ClubFacilities", "ProjectCooldowns"})
    clearTable(db, table);
  insertAll(db,
            "INSERT INTO FacilityProjects (id, team_id, type, start_date, "
            "end_date, cost, paid, amount, disruption, completed) VALUES (?, "
            "?, ?, ?, ?, ?, ?, ?, ?, ?);",
            projects,
            [](sqlite3_stmt* row, const FacilityProject& project)
            {
              sqlite3_bind_int64(row, 1, project.id);
              sqlite3_bind_int(row, 2, project.team_id);
              sqlite3_bind_int(
                  row, 3, static_cast<int>(std::to_underlying(project.type)));
              bindText(row, 4, project.start.toString());
              bindText(row, 5, project.end.toString());
              sqlite3_bind_int64(row, 6, project.cost);
              sqlite3_bind_int64(row, 7, project.paid);
              sqlite3_bind_int64(row, 8, project.amount);
              sqlite3_bind_int64(row, 9, project.disruption);
              sqlite3_bind_int(row, 10, project.completed ? 1 : 0);
            });
  insertAll(db, "INSERT INTO ClubFacilities (team_id, medical) VALUES (?, ?);",
            medical,
            [](sqlite3_stmt* row, const auto& entry)
            {
              sqlite3_bind_int(row, 1, entry.first);
              sqlite3_bind_int(row, 2, entry.second);
            });
  insertAll(db,
            "INSERT INTO ProjectCooldowns (team_id, type, until_date) VALUES "
            "(?, ?, ?);",
            cooldowns,
            [](sqlite3_stmt* row, const auto& entry)
            {
              sqlite3_bind_int(row, 1, entry.first.first);
              sqlite3_bind_int(row, 2, entry.first.second);
              bindText(row, 3, entry.second.toString());
            });
}

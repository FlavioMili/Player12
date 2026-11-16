// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Squad management: captain and set-piece takers, squad statuses, the squad
// planner, the medical centre and the season agenda of the managed club.

#include <algorithm>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "model/world_rng.h"

namespace
{
/** Rank of @p player_id by overall in his squad (0 = best; ties by id). */
std::size_t squadRank(const GameData& data, const Player& player)
{
  const StatsConfig& config = data.getStatsConfig();
  const double overall = player.getOverall(config);
  std::size_t rank = 0;
  for (const auto& other : data.getPlayersForTeam(player.getTeamId()))
  {
    const double other_overall = other.get().getOverall(config);
    if (other_overall > overall ||
        (other_overall == overall && other.get().getId() < player.getId()))
      ++rank;
  }
  return rank;
}
}  // namespace

PlayerID GameController::getSetPieceDesignation(SetPieceDuty duty) const
{
  const auto team = managedClub();
  return team ? team->get().getLineup().getDesignated(duty) : PlayerID{};
}

bool GameController::setSetPieceDesignation(SetPieceDuty duty,
                                            PlayerID player_id)
{
  const auto team = managedClub();
  if (!team || !gamedata || duty >= SetPieceDuty::COUNT) return false;
  if (player_id != PlayerID{})
  {
    const auto player = gamedata->getPlayer(player_id);
    if (!player || player->get().getTeamId() != team->get().getId())
      return false;
    if (!SetPieces::isLeadership(duty) &&
        SetPieces::score(duty, player->get()) <= 0.0f)
      return false;
  }
  team->get().getLineup().setDesignated(duty, player_id);
  return true;
}

PlayerID GameController::getEffectiveSetPieceTaker(SetPieceDuty duty) const
{
  const auto team = managedClub();
  if (!team || duty >= SetPieceDuty::COUNT) return PlayerID{};
  const Lineup& lineup = team->get().getLineup();
  if (!SetPieces::isLeadership(duty))
  {
    const Player* taker = lineup.effectiveTaker(duty);
    return taker ? taker->getId() : PlayerID{};
  }

  // Leaders: the designated players when they start, else the dressing
  // room's standing among the starters, then the most experienced ones.
  std::vector<PlayerID> order;
  for (const LeaderInfo& leader : getDressingRoom().leaders)
    if (lineup.isStarter(leader.player_id)) order.push_back(leader.player_id);
  std::vector<const Player*> rest = lineup.starters();
  std::ranges::sort(
      rest,
      [](const Player* a, const Player* b)
      {
        return a->getAge() > b->getAge() ||
               (a->getAge() == b->getAge() && a->getId() < b->getId());
      });
  for (const Player* player : rest)
    if (std::ranges::find(order, player->getId()) == order.end())
      order.push_back(player->getId());
  if (order.empty()) return PlayerID{};

  const Player* designated = lineup.effectiveTaker(SetPieceDuty::Captain);
  const PlayerID captain = designated ? designated->getId() : order.front();
  if (duty == SetPieceDuty::Captain) return captain;
  if (const Player* vice = lineup.effectiveTaker(SetPieceDuty::ViceCaptain);
      vice && vice->getId() != captain)
    return vice->getId();
  const auto next = std::ranges::find_if(
      order, [captain](PlayerID id) { return id != captain; });
  return next == order.end() ? PlayerID{} : *next;
}

void GameController::autoPickSetPieces()
{
  const auto team = managedClub();
  if (!team) return;
  Lineup& lineup = team->get().getLineup();
  lineup.setDesignations({});
  SetPieceDesignations picks{};
  for (std::size_t duty = 0; duty < SET_PIECE_DUTY_COUNT; ++duty)
    picks[duty] = getEffectiveSetPieceTaker(static_cast<SetPieceDuty>(duty));
  lineup.setDesignations(picks);
}

std::optional<SquadStatus> GameController::getSquadStatus(
    PlayerID player_id) const
{
  const auto team = managedClub();
  if (!team || !game) return std::nullopt;
  return game->getWorld().getSquadStatuses().get(player_id,
                                                 team->get().getId());
}

SquadStatus GameController::getDeservedSquadStatus(PlayerID player_id) const
{
  const auto player = gamedata ? gamedata->getPlayer(player_id) : std::nullopt;
  if (!player) return SquadStatus::Backup;
  return SquadStatusModel::deserved(squadRank(*gamedata, player->get()),
                                    player->get().getAge());
}

bool GameController::setSquadStatus(PlayerID player_id,
                                    std::optional<SquadStatus> status)
{
  const auto team = managedClub();
  if (!team || !game || !gamedata) return false;
  const auto player = gamedata->getPlayer(player_id);
  if (!player || player->get().getTeamId() != team->get().getId()) return false;
  if (status == SquadStatus::Prospect &&
      player->get().getAge() > SquadStatusModel::PROSPECT_MAX_AGE)
    return false;
  SquadStatusBook& book = game->getWorld().getSquadStatuses();
  book.set(player_id, team->get().getId(), status);
  // Statuses of players who have left (or of a former club) are dropped.
  const TeamID club = team->get().getId();
  book.prune(
      [this, club](PlayerID id)
      {
        const auto member = gamedata->getPlayer(id);
        return member && member->get().getTeamId() == club ? club : TeamID{0};
      });
  return true;
}

SquadPlan GameController::getSquadPlan(int season_offset) const
{
  const auto team = managedClub();
  if (!team || !gamedata) return {};
  const Lineup& lineup = team->get().getLineup();
  const StatsConfig& config = gamedata->getStatsConfig();
  std::array<int, PLANNER_GROUP_COUNT> starters{};
  for (const Player* starter : lineup.starters())
    ++starters[static_cast<std::size_t>(
        std::to_underlying(SquadPlanner::groupOf(starter->getRole())))];

  std::vector<PlannerPlayer> squad;
  for (const auto& reference : gamedata->getPlayersForTeam(team->get().getId()))
  {
    const Player& player = reference.get();
    PlannerPlayer entry;
    entry.id = player.getId();
    entry.role = player.getRole();
    entry.age = player.getAge();
    entry.overall = static_cast<float>(player.getOverall(config));
    const PotentialEstimate potential = getPotentialEstimate(player.getId());
    entry.potential = potential.high > 0.0f
                          ? 0.5f * (potential.low + potential.high)
                          : entry.overall;
    entry.contract_years = player.getContractYears();
    entry.in_xi = lineup.isStarter(player.getId());
    entry.injured = !player.isAvailable();
    entry.listed = isPlayerListed(player.getId());
    squad.push_back(entry);
  }
  return SquadPlanner::build(squad, season_offset, starters);
}

MedicalReport GameController::getMedicalReport() const
{
  MedicalReport report;
  const auto team = managedClub();
  if (!team || !gamedata || !game) return report;
  const TeamID club = team->get().getId();
  report.effects = getStaffEffects(club);
  for (const StaffMember* member : getStaff(club))
    if (member->role == StaffRole::Physio ||
        member->role == StaffRole::SportsScientist)
      report.medical_staff.push_back(member);
  const float quality = MedicalCentre::staffQuality(report.effects);
  const std::int32_t today = dayOrdinal(game->getCurrentDate());

  float condition_total = 0.0f;
  float sharpness_total = 0.0f;
  for (const auto& reference : gamedata->getPlayersForTeam(club))
  {
    const Player& player = reference.get();
    const PlayerDynamics& dynamics = player.getDynamics();
    if (!player.isAvailable())
    {
      MedicalInjuryRow row;
      row.player_id = player.getId();
      row.type = dynamics.injury;
      row.days_left = dynamics.injury_days;
      row.severity = InjuryModel::severity(dynamics.injury_days);
      row.window = MedicalCentre::returnWindow(dynamics.injury_days, quality);
      row.reinjury = MedicalCentre::reinjuryRisk(
          dynamics.injury, dynamics.injury_days, player.getAge());
      report.injured.push_back(row);
      continue;
    }
    const PlayerWorkload workload = getPlayerWorkload(player.getId());
    InjuryRiskInputs inputs;
    inputs.age = player.getAge();
    inputs.days_since_injury =
        dynamics.last_injury != InjuryType::None && dynamics.last_injury_day > 0
            ? today - dynamics.last_injury_day
            : -1;
    inputs.days_since_match =
        dynamics.last_match_day > 0 ? today - dynamics.last_match_day : -1;
    inputs.condition = dynamics.condition;
    inputs.workload_ratio = workload.ratio;
    inputs.staff_prevention = report.effects.injury_prevention;

    MedicalRiskRow row;
    row.player_id = player.getId();
    row.risk = MedicalCentre::assess(inputs);
    row.condition = dynamics.condition;
    row.sharpness = dynamics.sharpness;
    row.workload_ratio = workload.ratio;
    row.workload = workload.risk;
    row.acute = workload.acute;
    row.chronic = workload.chronic;
    row.returning =
        inputs.days_since_injury >= 0 &&
        inputs.days_since_injury <= MedicalCentre::RECURRENCE_WINDOW_DAYS;
    condition_total += row.condition;
    sharpness_total += row.sharpness;
    report.squad.push_back(row);
  }
  if (!report.squad.empty())
  {
    const auto count = static_cast<float>(report.squad.size());
    report.average_condition = condition_total / count;
    report.average_sharpness = sharpness_total / count;
  }
  std::ranges::sort(
      report.injured,
      [](const MedicalInjuryRow& a, const MedicalInjuryRow& b)
      {
        return a.days_left > b.days_left ||
               (a.days_left == b.days_left && a.player_id < b.player_id);
      });
  std::ranges::sort(report.squad,
                    [](const MedicalRiskRow& a, const MedicalRiskRow& b)
                    {
                      return a.risk.multiplier > b.risk.multiplier ||
                             (a.risk.multiplier == b.risk.multiplier &&
                              a.player_id < b.player_id);
                    });
  return report;
}

std::vector<AgendaEvent> GameController::getSeasonAgenda() const
{
  const auto team = managedClub();
  if (!team || !game) return {};
  const GameDateValue start = SeasonAgenda::seasonStart(game->getCurrentDate());
  return SeasonAgenda::build(
      start.year, game->getCalendar().getTeamFixtures(team->get().getId()),
      true, team->get().getLeagueId());
}

SquadNumbers::Change GameController::setSquadNumber(PlayerID player_id,
                                                    int number)
{
  if (const auto player = std::as_const(*gamedata).getPlayer(player_id);
      !game || !hasSelectedTeam() || !player ||
      player->get().getTeamId() != game->getManagedTeamId())
    return SquadNumbers::Change::Invalid;
  return gamedata->setSquadNumber(player_id, number);
}

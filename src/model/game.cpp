// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/game.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "database/database_connection.h"
#include "database/gamedata.h"
#include "database/repositories/fixture_repository.h"
#include "database/repositories/game_state_repository.h"
#include "database/repositories/league_repository.h"
#include "database/repositories/player_repository.h"
#include "database/repositories/team_repository.h"
#include "database/save_manager.h"
#include "global/global.h"
#include "global/logger.h"
#include "global/paths.h"
#include "model/competition.h"
#include "model/league.h"
#include "model/role_utils.h"
#include "model/team.h"
#include "model/world_rng.h"
#include "model/youth_academy.h"

namespace
{
/**
 * Days of injury a player may play through when the squad has nobody fit
 * for his place; longer injuries play only when nobody closer to fitness is
 * left.
 */
constexpr std::uint16_t PLAY_THROUGH_INJURY_DAYS = 7;
}  // namespace

Game::Game(std::shared_ptr<GameData> gd,
           std::shared_ptr<DatabaseConnection> conn)
    : db_conn(std::move(conn)),
      gamedata(std::move(gd)),
      competitions(gamedata, db_conn),
      world(gamedata),
      transfers(gamedata, world, competitions),
      career(gamedata),
      international(gamedata),
      national_job(gamedata),
      currentDate(START_DATE)
{
  (*gamedata).loadFromDB(db_conn);
  // Continental news reaches the inbox when it concerns the managed club;
  // results everybody talks about (winners) are filed as read.
  competitions.getContinental().setNewsSink(
      [this](InboxMessage message, const std::vector<TeamID>& clubs,
             bool headline)
      {
        const bool ours = managed_team_id != FREE_AGENTS_TEAM_ID &&
                          std::ranges::contains(clubs, managed_team_id);
        if (!ours && !headline) return;
        message.read = !ours;
        if (ours) message.team_id = managed_team_id;
        world.getInbox().add(std::move(message));
      });
  // Results and finals of the manager's national team reach his job.
  international.setResultSink(
      [this](const International::Fixture& fixture, double home_expected)
      { national_job.queueResult(fixture, home_expected); });
  international.setFinalsSink(
      [this](const NationalTeams::Finals& finals, bool decided)
      { national_job.queueFinals(finals, decided); });
  world.setInjuryRiskProvider(
      [this](PlayerID player_id, const GameDateValue& date)
      { return international.injuryRiskMultiplier(player_id, date); });
  world.setStandingsProvider(
      [this](LeagueID league_id)
      {
        std::vector<TeamID> order;
        for (const StandingRow& row :
             competitions.getStandings(calendar, league_id))
          order.push_back(row.team_id);
        return order;
      });
  world.setLoanCheck([this](PlayerID player_id)
                     { return transfers.findLoan(player_id) != nullptr; });
  world.setSeasonHistoryProvider(
      [this]() -> const std::vector<SeasonHistoryEntry>&
      { return competitions.getSeasonHistory(); });
  world.setFreeAgentHold(
      [this](PlayerID player_id)
      {
        return transfers.findNegotiation(player_id) != nullptr ||
               transfers.findPreContract(player_id) != nullptr;
      });
  world.setLineupGapFiller([this](Team& team, const Player& departed)
                           { fillLineupGap(team, departed); });
  world.getScouting().setBudgetProvider(
      [this](TeamID team_id)
      { return transfers.spendableBudget(team_id, currentDate); });
  world.setFixtureOutlookProvider(
      [this](const GameDateValue& date, TrainingSystem::FixtureOutlook& outlook)
      {
        const auto& fixtures = calendar.getFullCalendar();
        const GameDateValue horizon = SeasonCalendar::addDays(date, 7);
        for (auto day = fixtures.lower_bound(date);
             day != fixtures.end() && !(horizon < day->first); ++day)
        {
          const auto days = static_cast<std::uint8_t>(dayOrdinal(day->first) -
                                                      dayOrdinal(date));
          for (const Match& match : day->second)
          {
            if (match.isPlayed()) continue;
            outlook.try_emplace(match.getHomeTeamId(), days);
            outlook.try_emplace(match.getAwayTeamId(), days);
          }
        }
      });
  world.getStories().setCareerProvider(
      [this](PlayerID player_id)
      {
        CareerTotals totals;
        for (const PlayerSeasonStats& stats :
             competitions.getPlayerCareer(player_id))
        {
          totals.appearances += stats.appearances;
          totals.goals += stats.goals;
        }
        return totals;
      });
  loadGame();
  discoverExtensions();
}

void Game::discoverExtensions()
{
  auto canonical = SaveManager::createWorkingCopy();
  canonical->initialize();
  extensions = std::make_unique<RuntimeEditor::Extensions>(db_conn->getRaw(),
                                                           canonical->getRaw());
  extensions_version = RuntimeEditor::schemaVersion(db_conn->getRaw());
  // Learn opaque JSON members from this build's own serializers. The probe
  // stays in memory; restoring the database never touches the save slot.
  auto original = SaveManager::createWorkingCopy();
  RuntimeEditor::copy(db_conn->getRaw(), original->getRaw());
  try
  {
    serialize(true);
    payload_extensions =
        RuntimeEditor::Payloads(original->getRaw(), db_conn->getRaw());
    // Keep the normalized database consistent with the serializers' onSaved
    // flags. Restoring its old owned values would lose dirty-state writes.
    payload_extensions.restore(db_conn->getRaw());
  }
  catch (...)
  {
    RuntimeEditor::copy(original->getRaw(), db_conn->getRaw());
    throw;
  }
}

void Game::loadGame()
{
  GameStateRepository gameStateRepo(db_conn);
  FixtureRepository fixtureRepo(db_conn);
  if (std::string game_date_str; gameStateRepo.loadGameState(
          current_season, managed_team_id, game_date_str))
  {
    currentDate = GameDateValue::fromString(game_date_str);
    fixtureRepo.loadCalendar(calendar);
    competitions.load(calendar, current_season);
    world.load(db_conn);
    transfers.load(db_conn);
    career.load(db_conn);
    guidance.load(db_conn);
    medical.load(db_conn);
    supporters.load(db_conn);
    international.load(*db_conn);
    national_job.load(db_conn);
    season_archive.load(*db_conn);
    // Saves from before the continental competitions join them next
    // season unless the league phase can still be drawn.
    competitions.startContinentalSeason(currentDate);
    Logger::debug("Game loaded. Date: " + game_date_str +
                  ", Season: " + std::to_string(current_season));
  }
  else
  {
    // First run, initialize with defaults
    current_season = 1;
    managed_team_id = FREE_AGENTS_TEAM_ID;  // Or some other default
    currentDate = START_DATE;
    calendar.generate((*gamedata), currentDate);
    competitions.load(calendar, current_season);
    competitions.startContinentalSeason(currentDate);
    Logger::debug("First run, initializing game state.");
    saveGame();
  }
  // Ensure managed team is valid
  if ((*gamedata).getTeams().find(managed_team_id) ==
      (*gamedata).getTeams().end())
  {
    managed_team_id = FREE_AGENTS_TEAM_ID;
  }
  // Saves from before the board existed get it (and the day-one news) now.
  else if (managed_team_id != FREE_AGENTS_TEAM_ID &&
           world.getBoardState().team_id != managed_team_id)
  {
    world.onManagedTeamSelected(currentDate, managed_team_id,
                                upcomingFixtures(managed_team_id));
  }
  // Careers from before the manager market get a manager and AI rivals.
  if (managed_team_id != FREE_AGENTS_TEAM_ID)
    career.ensureProfile(managed_team_id, currentDate);
  career.ensureClubManagers(currentDate, managed_team_id);
  career.setInternationalDuty(national_job.hasJob());
}

void Game::saveGame() { serialize(false); }

void Game::serialize(bool probe)
{
  if (extensions &&
      extensions_version != RuntimeEditor::schemaVersion(db_conn->getRaw()))
  {
    auto canonical = SaveManager::createWorkingCopy();
    canonical->initialize();
    extensions = std::make_unique<RuntimeEditor::Extensions>(
        db_conn->getRaw(), canonical->getRaw());
    extensions_version = RuntimeEditor::schemaVersion(db_conn->getRaw());
  }
  if (extensions) extensions->capture(db_conn->getRaw());
  db_conn->beginTransaction();
  try
  {
    GameStateRepository gameStateRepo(db_conn);
    FixtureRepository fixtureRepo(db_conn);
    LeagueRepository leagueRepo(db_conn);
    PlayerRepository playerRepo(db_conn);
    TeamRepository teamRepo(db_conn);

    gameStateRepo.updateGameState(current_season, managed_team_id,
                                  currentDate.toString());
    fixtureRepo.saveCalendar(calendar);
    competitions.save();
    world.save(db_conn);
    if (!probe) SaveManager::fault(SaveManager::FaultPoint::MidFlush);
    transfers.save(db_conn);
    career.save(db_conn);
    guidance.save(db_conn);
    medical.save(db_conn);
    supporters.save(db_conn);
    international.save(*db_conn);
    national_job.save(db_conn);
    season_archive.save(*db_conn);

    for (const auto& [id, league] : (*gamedata).getLeagues())
    {
      leagueRepo.saveLeaguePoints(league);
    }
    playerRepo.updatePlayers(gamedata->getPlayersVector());
    teamRepo.updateTeamsState(gamedata->getTeamsVector());
    if (extensions) extensions->restore(db_conn->getRaw());
    payload_extensions.restore(db_conn->getRaw());
    db_conn->commitTransaction();
  }
  catch (const std::exception& e)
  {
    db_conn->rollbackTransaction();
    Logger::error("Failed to save game: " + std::string(e.what()));
    throw;
  }
  competitions.onSaved();
  world.onSaved();
  transfers.onSaved();
  guidance.onSaved();
  season_archive.onSaved();

  Logger::debug("Game saved.");
}

void Game::advanceDay()
{
  scheduler.resetProgress();
  // The closing day's loads are complete only now (a watched match is
  // applied after advanceDay()).
  medical.onDayEnd(*gamedata, currentDate, managed_team_id, world.getInbox());
  supporters.onDayEnd(*gamedata, currentDate, managed_team_id, calendar,
                      transfers, world);
  currentDate.nextDay();
  Logger::debug("Date changed to: " + currentDate.toString());
  world.onDayAdvanced(currentDate, managed_team_id);
  runCareerDay();
  // National-team matches run through the same batch scheduler; windows
  // never overlap competitive club fixtures.
  international.onDay(currentDate, scheduler, world, managed_team_id);
  last_national_events = national_job.onDay(
      currentDate, international, career,
      managed_team_id != FREE_AGENTS_TEAM_ID, world.getInbox());

  // A managed fixture left unplayed on its day is simulated so that the
  // competitions (tables, cup draws) never stall. On 1 July this happens
  // before the new season replaces the calendar, so a fixture left on
  // 30 June still counts for the season it belongs to.
  const bool season_ends = currentDate.month == 7 && currentDate.day == 1;
  const GameDateValue yesterday = SeasonCalendar::addDays(currentDate, -1);
  if (calendar.getFullCalendar().contains(yesterday))
  {
    simulateMatches(calendar.getMatchesForDateMutable(yesterday), true);
    if (season_ends) competitions.afterMatchday(calendar, yesterday);
  }

  if (season_ends)
  {
    handleSeasonTransition();
    // Pre-contracts complete once expired contracts have been released.
    transfers.onDayAdvanced(currentDate, managed_team_id);
    return;
  }

  auto& matches_today = calendar.getMatchesForDateMutable(currentDate);
  if (!matches_today.empty())
  {
    simulateMatches(matches_today);
  }
  competitions.afterMatchday(calendar, currentDate);
  transfers.onDayAdvanced(currentDate, managed_team_id);
  keepManagedSelectionEligible();
}

void Game::keepManagedSelectionEligible()
{
  if (!getAssistantFixesLineup() || managed_team_id == FREE_AGENTS_TEAM_ID)
    return;
  // Suspensions depend on the competition of the next managed fixture.
  const auto& schedule = calendar.getFullCalendar();
  for (auto day = schedule.lower_bound(currentDate); day != schedule.end();
       ++day)
  {
    for (const Match& match : day->second)
    {
      if (match.isPlayed() || (match.getHomeTeamId() != managed_team_id &&
                               match.getAwayTeamId() != managed_team_id))
        continue;
      fixMatchdaySquad(managed_team_id, match.getMatchType(), day->first);
      return;
    }
  }
}

void Game::simulateMatches(std::vector<Match>& matches, bool include_managed)
{
  // Inputs are captured in fixture order, simulated in parallel and applied
  // back in the same order. A club plays once per batch, so this equals
  // simulating and applying the matches one by one, which is what a single
  // simulation thread does.
  std::vector<Match*> batch;
  std::vector<MatchSimulationInput> inputs;
  std::vector<TeamID> batch_teams;
  const auto flush = [&]
  {
    if (inputs.empty()) return;
    std::vector<MatchSimulationResult> results =
        scheduler.run(inputs, gamedata->getStatsConfig());
    for (std::size_t i = 0; i < batch.size(); ++i)
    {
      Match& match = *batch[i];
      const std::vector<PlayerMatchConsequence> consequences =
          std::move(results[i].consequences);
      MatchReport report = match.applySimulation(std::move(results[i]));
      settleLevelTie(match, report);
      for (const PlayerMatchConsequence& consequence : consequences)
        if (world.applyMatchConsequences(match.getDate(), consequence,
                                         managed_team_id))
          medical.afterMatch(*gamedata, match.getDate(), consequence.player_id,
                             consequence.minutes_played, consequence.injured,
                             managed_team_id, world.getInbox());
      world.onMatchPlayed(match, report, managed_team_id);
      recordCareerMatch(match);
      // A first match played by the assistant (holiday, missed fixture)
      // still ticks the first-week checklist.
      if (match.getHomeTeamId() == managed_team_id ||
          match.getAwayTeamId() == managed_team_id)
        guidance.onboarding.complete(OnboardingTask::PlayFirstMatch);
      competitions.recordResult(match, std::move(report));
    }
    batch.clear();
    inputs.clear();
    batch_teams.clear();
  };
  const bool one_by_one = scheduler.getThreadCount() <= 1;

  for (auto& match : matches)
  {
    const bool managed = match.getHomeTeamId() == managed_team_id ||
                         match.getAwayTeamId() == managed_team_id;
    if (match.isPlayed() || (managed && !include_managed))
    {
      continue;
    }
    if (std::ranges::contains(batch_teams, match.getHomeTeamId()) ||
        std::ranges::contains(batch_teams, match.getAwayTeamId()))
      flush();

    // A missed managed fixture is played by the assistant with an eligible
    // squad; the manager's own selection is restored afterwards.
    std::optional<Lineup> managed_selection;
    if (managed)
    {
      if (auto team = gamedata->getTeam(managed_team_id))
      {
        managed_selection = team->get().getLineup();
        fixMatchdaySquad(managed_team_id, match.getMatchType());
      }
    }
    const auto swaps = competitions.benchSuspendedPlayers(match);
    std::optional<MatchSimulationInput> input =
        match.prepareSimulation(*gamedata);
    competitions.restoreLineups(swaps);
    if (managed_selection)
      gamedata->getTeam(managed_team_id)->get().getLineup() =
          *managed_selection;
    if (!input) continue;
    // Knockout ties are played to a winner (extra time, penalties), so form,
    // the manager's record and the season statistics see the final result.
    if (const auto rules = competitions.knockoutRules(calendar, match))
      input->knockout = *rules;
    // A team talk given before an unwatched match still counts.
    if (managed)
    {
      auto& talk = match.getHomeTeamId() == managed_team_id ? input->home_talk
                                                            : input->away_talk;
      for (int half = 1; half <= 2; ++half)
        talk[static_cast<std::size_t>(half - 1)] =
            world.getInteractions().teamTalkModifier(managed_team_id,
                                                     match.getDate(), half);
      // The medical staff's minute limits hold in unwatched matches too.
      input->medical_flags = MatchdaySquad::medicalFlags(
          medical, match.getHomeTeamId() == managed_team_id
                       ? input->home_lineup
                       : input->away_lineup);
    }
    batch.push_back(&match);
    inputs.push_back(std::move(*input));
    batch_teams.push_back(match.getHomeTeamId());
    batch_teams.push_back(match.getAwayTeamId());
    if (one_by_one) flush();
  }
  flush();
}

void Game::settleLevelTie(Match& match, MatchReport& report)
{
  if (match.wentToExtraTime() || match.wentToPenalties()) return;
  const auto rules = competitions.knockoutRules(calendar, match);
  if (!rules || match.getHomeScore() + rules->homeAggregate !=
                    match.getAwayScore() + rules->awayAggregate)
    return;
  const auto home_team = gamedata->getTeam(match.getHomeTeamId());
  const auto away_team = gamedata->getTeam(match.getAwayTeamId());
  if (!home_team || !away_team) return;
  const Competitions::KnockoutResolution resolution =
      Competitions::resolveDrawnKnockout(home_team->get(), away_team->get(),
                                         gamedata->getStatsConfig(),
                                         match.getSeed());
  std::optional<std::pair<uint8_t, uint8_t>> shootout;
  if (resolution.penalties)
    shootout.emplace(resolution.home_penalties, resolution.away_penalties);
  match.setKnockoutResult(
      static_cast<uint8_t>(match.getHomeScore() + resolution.home_extra_goals),
      static_cast<uint8_t>(match.getAwayScore() + resolution.away_extra_goals),
      true, shootout);
  match.writeResultTo(report);
  report.creditExtraTimeGoals(home_team->get().getLineup(),
                              match.getHomeTeamId(), true,
                              resolution.home_extra_goals, match.getSeed());
  report.creditExtraTimeGoals(away_team->get().getLineup(),
                              match.getAwayTeamId(), false,
                              resolution.away_extra_goals, match.getSeed());
}

bool Game::setMatchResult(const GameDateValue& date, TeamID home_id,
                          TeamID away_id, uint8_t home_score,
                          uint8_t away_score)
{
  MatchReport report;
  report.home_goals = home_score;
  report.away_goals = away_score;
  return setMatchResult(date, home_id, away_id, std::move(report));
}

bool Game::setMatchResult(const GameDateValue& date, TeamID home_id,
                          TeamID away_id, MatchReport report,
                          std::span<const PlayerMatchConsequence> consequences)
{
  Match* match = calendar.findMatch(date, home_id, away_id);
  if (!match || match->isPlayed())
  {
    return false;
  }
  const auto home_team = gamedata->getTeam(home_id);
  const auto away_team = gamedata->getTeam(away_id);

  // A knockout engine reports extra time and the shootout it played.
  std::optional<std::pair<uint8_t, uint8_t>> shootout;
  if (report.penalties)
    shootout.emplace(report.home_penalties, report.away_penalties);
  if (report.extra_time || shootout)
    match->setKnockoutResult(report.home_goals, report.away_goals,
                             report.extra_time, shootout);
  else
    match->setPlayedResult(report.home_goals, report.away_goals);
  match->writeResultTo(report);

  if (report.players.empty())
  {
    if (home_team)
      report.addLineupAppearances(home_team->get().getLineup(), home_id);
    if (away_team)
      report.addLineupAppearances(away_team->get().getLineup(), away_id);
  }
  settleLevelTie(*match, report);
  for (const PlayerMatchConsequence& consequence : consequences)
    if (world.applyMatchConsequences(date, consequence, managed_team_id))
      medical.afterMatch(*gamedata, date, consequence.player_id,
                         consequence.minutes_played, consequence.injured,
                         managed_team_id, world.getInbox());
  world.onMatchPlayed(*match, report, managed_team_id);
  recordCareerMatch(*match);
  competitions.recordResult(*match, std::move(report));
  competitions.afterMatchday(calendar, currentDate);
  return true;
}

bool Game::isEligible(const Player& player, MatchType type,
                      std::optional<GameDateValue> date) const
{
  return player.isAvailable() &&
         (type == MatchType::FRIENDLY ||
          !competitions.getDiscipline().isSuspended(player.getId(), type)) &&
         !international.isOnDuty(player.getId(), date.value_or(currentDate));
}

std::vector<PlayerID> Game::ineligibleSelections(TeamID team_id,
                                                 MatchType type) const
{
  const auto team = gamedata->getTeam(team_id);
  if (!team) return {};
  return MatchdaySquad::ineligible(team->get().getLineup(),
                                   [this, type](const Player& player)
                                   { return isEligible(player, type); });
}

std::size_t Game::fixMatchdaySquad(TeamID team_id, MatchType type,
                                   std::optional<GameDateValue> date)
{
  const auto team = gamedata->getTeam(team_id);
  if (!team) return 0;
  return fillMatchdaySquad(team->get().getLineup(), team_id, type,
                           date.value_or(currentDate), true);
}

std::vector<std::pair<PlayerID, PlayerID>> Game::previewMatchdaySquadFix(
    TeamID team_id, MatchType type) const
{
  const auto team = gamedata->getTeam(team_id);
  if (!team) return {};
  const Lineup& current = team->get().getLineup();
  Lineup fixed = current;
  fillMatchdaySquad(fixed, team_id, type, currentDate, true);
  return MatchdaySquad::replacements(current, fixed);
}

bool Game::canKickOff(TeamID team_id, MatchType type) const
{
  const auto team = gamedata->getTeam(team_id);
  if (!team) return false;
  Lineup fixed = team->get().getLineup();
  return fillMatchdaySquad(fixed, team_id, type, currentDate, false) == 0;
}

std::size_t Game::fillMatchdaySquad(Lineup& lineup, TeamID team_id,
                                    MatchType type, const GameDateValue& date,
                                    bool recall_regulars) const
{
  const auto team = gamedata->getTeam(team_id);
  if (!team) return 0;
  // Trialists of the academy intake are the last fit players to be asked.
  std::vector<const Player*> squad;
  std::vector<const Player*> everyone;
  for (const auto* ids :
       {&team->get().getPlayerIDs(), &team->get().getAcademyIDs()})
  {
    for (const PlayerID player_id : *ids)
    {
      const auto player = gamedata->getPlayer(player_id);
      if (!player) continue;
      everyone.push_back(&player->get());
      const YouthRecord* youth = world.getYouth().record(player_id);
      if (!youth || youth->status != YouthStatus::Candidate)
        squad.push_back(&player->get());
    }
  }
  const StatsConfig& config = gamedata->getStatsConfig();
  const Lineup before = lineup;
  // Suspensions and national-team duty are rules; an injury is a risk.
  const auto allowed = [&](const Player& player)
  {
    return (type == MatchType::FRIENDLY ||
            !competitions.getDiscipline().isSuspended(player.getId(), type)) &&
           !international.isOnDuty(player.getId(), date);
  };
  // Players the medical staff wants rested play only when nobody else can.
  const auto fit = [&](const Player& player)
  {
    return player.isAvailable() && allowed(player) &&
           !medical.isRested(player.getId());
  };
  const auto startersFit = [&]
  {
    return std::ranges::all_of(
        lineup.starters(), [&](const Player* player) { return fit(*player); });
  };

  if (recall_regulars)
    MatchdaySquad::recallRegulars(lineup, squad, fit, config);
  const Lineup recalled = lineup;
  MatchdaySquad::replaceIneligible(lineup, squad, fit, config);
  if (!startersFit())
    MatchdaySquad::replaceIneligible(lineup, everyone, fit, config);
  if (!startersFit())
  {
    // Nobody fit is left for every starting place: players close to fitness
    // play through their injury first, then anyone who may play at all.
    MatchdaySquad::replaceIneligible(
        lineup, everyone,
        [&](const Player& player)
        {
          return allowed(player) &&
                 player.getDynamics().injury_days <= PLAY_THROUGH_INJURY_DAYS;
        },
        config);
    MatchdaySquad::replaceIneligible(lineup, everyone, allowed, config);
    // Injured players start but never wait on the bench.
    MatchdaySquad::replaceIneligible(lineup, everyone, fit, config);
    // Whoever may not play at all and has no replacement leaves the XI.
    if (const Player* goalkeeper = lineup.getGoalkeeper();
        goalkeeper && !allowed(*goalkeeper))
      lineup.setGoalkeeper(nullptr);
    std::vector<PlayerID> barred;
    for (const Lineup::PositionedPlayer& slot : lineup.getOutfieldPlayers())
      if (slot.player && !allowed(*slot.player))
        barred.push_back(slot.player->getId());
    for (const PlayerID player_id : barred)
      lineup.removeOutfieldPlayer(player_id);
  }
  MatchdaySquad::recordStandIns(recalled, lineup);
  return MatchdaySquad::replacements(before, lineup).size();
}

void Game::endSeason()
{
  std::cout << "--- Season " << static_cast<int>(current_season)
            << " has concluded. ---"
            << "\n";
  lineup_repairs.clear();
  world.onSeasonEnd(currentDate, managed_team_id);
  // Final tables are read (and archived) before promotion and relegation
  // move the clubs.
  const std::uint16_t start_year =
      SeasonCalendar::seasonStartYear(SeasonCalendar::addDays(currentDate, -1));
  std::map<LeagueID, std::vector<StandingRow>> final_tables;
  std::unordered_map<TeamID, int> final_positions;
  for (const auto& [league_id, league] : gamedata->getLeagues())
  {
    const auto& rows = final_tables[league_id] =
        competitions.getStandings(calendar, league_id);
    int position = 0;
    for (const StandingRow& row : rows)
      final_positions[row.team_id] = ++position;
  }
  season_archive.recordTables(current_season, start_year, final_tables);
  beginSeasonReview(final_tables, start_year);
  competitions.closeSeason(calendar, current_season, start_year);
  std::vector<SeasonHistoryEntry> finished;
  for (const SeasonHistoryEntry& entry : competitions.getSeasonHistory())
    if (entry.season == current_season) finished.push_back(entry);
  // Title, promotion or relegation first, then the board's verdict.
  judgeSeason(finished);
  const BoardState board = world.getBoardState();
  const bool managing = managed_team_id != FREE_AGENTS_TEAM_ID &&
                        board.team_id == managed_team_id;
  const bool sacked = managing && closing_review &&
                      closing_review->result.verdict == SeasonVerdict::Sacked;
  // A board that sacks the manager does not renew his contract either.
  const bool contract_ran_out = career.onSeasonEnd(
      currentDate, managed_team_id, managing ? board.expected_position : 0,
      managing ? (sacked ? 0.0f : board.confidence) : 50.0f,
      [&final_positions](TeamID team_id)
      {
        const auto found = final_positions.find(team_id);
        return found == final_positions.end() ? 0 : found->second;
      },
      finished, world.getInbox());
  if (contract_ran_out)
    leaveManagedTeam(DepartureReason::ContractExpired);
  else if (sacked)
    leaveManagedTeam(DepartureReason::Sacked);
  (*gamedata).ageAllPlayers();
  // Expired contracts leave gaps in the manager's line-up, nothing more.
  const std::vector<PlayerID> released =
      gamedata->advanceContractsAndReleasePlayers(managed_team_id);
  if (const auto team = gamedata->getTeam(managed_team_id);
      team && managed_team_id != FREE_AGENTS_TEAM_ID)
    for (const PlayerID player_id : released)
      if (const auto player = gamedata->getPlayer(player_id))
        fillLineupGap(team->get(), player->get());
  postLineupRepairs();
  current_season++;
  competitions.setCurrentSeason(current_season);
}

void Game::handleSeasonTransition()
{
  endSeason();
  startNewSeason();
}

void Game::startNewSeason()
{
  for (auto& [id, league] : (*gamedata).getLeagues())
  {
    league.resetPoints();
  }
  calendar.generate((*gamedata), currentDate);
  competitions.startContinentalSeason(currentDate);
  world.onSeasonStart(currentDate, managed_team_id);
  finishSeasonReview();
}

const GameDateValue& Game::getCurrentDate() const { return currentDate; }

const Calendar& Game::getCalendar() const { return calendar; }

Calendar& Game::getCalendar() { return calendar; }

int Game::getCurrentSeason() const { return current_season; }

uint16_t Game::getManagedTeamId() const { return managed_team_id; }

void Game::setManagedTeamId(uint16_t id)
{
  managed_team_id = id;
  if (id != FREE_AGENTS_TEAM_ID) career.ensureProfile(id, currentDate);
  world.onManagedTeamSelected(currentDate, id, upcomingFixtures(id));
}

void Game::leaveManagedTeam(DepartureReason reason)
{
  if (managed_team_id == FREE_AGENTS_TEAM_ID) return;
  career.leaveJob(reason, currentDate, world.getInbox());
  world.onManagerLeft();
  // Talks and offers were the manager's business at that club.
  std::vector<std::uint32_t> offer_ids;
  for (const IncomingOffer& offer : transfers.incomingOffers())
    offer_ids.push_back(offer.id);
  for (const std::uint32_t offer_id : offer_ids)
    transfers.removeIncomingOffer(offer_id);
  std::vector<PlayerID> talks;
  for (const auto& [player_id, talk] : transfers.talks())
    talks.push_back(player_id);
  for (const PlayerID player_id : talks) transfers.removeNegotiation(player_id);
  // So were the instructions against that club's next opponents.
  guidance.opposition.restore({});
  if (const auto team = gamedata->getTeam(managed_team_id))
    team->get().getStrategy().setOppositionOrders({});
  managed_team_id = FREE_AGENTS_TEAM_ID;
}

void Game::takeJob(TeamID team_id, const ManagerContract& contract)
{
  if (team_id == FREE_AGENTS_TEAM_ID || !gamedata->getTeam(team_id)) return;
  if (managed_team_id != FREE_AGENTS_TEAM_ID)
    leaveManagedTeam(DepartureReason::Moved);
  // Only a famous manager may keep a national team alongside a club.
  if (national_job.hasJob() &&
      !NationalJobModel::canCombineWithClub(career.getProfile().reputation))
    national_job.leave(DepartureReason::Moved, currentDate, international,
                       career, world.getInbox());
  career.startJob(team_id, contract, currentDate);
  managed_team_id = team_id;
  world.onManagedTeamSelected(currentDate, team_id, upcomingFixtures(team_id));
}

void Game::runCareerDay()
{
  const BoardState& board = world.getBoardState();
  const bool managing = managed_team_id != FREE_AGENTS_TEAM_ID &&
                        board.team_id == managed_team_id;
  last_career_events = career.onDayAdvanced(
      currentDate, managed_team_id, managing ? board.confidence : 50.0f,
      managing ? board.expected_position : 0, [this](TeamID team_id)
      { return world.leaguePosition(team_id); }, world.getInbox());
  if (managing && board.dismissed) leaveManagedTeam(DepartureReason::Sacked);
}

void Game::recordCareerMatch(const Match& match)
{
  if (match.getMatchType() == MatchType::FRIENDLY) return;
  const TeamID home_id = match.getHomeTeamId();
  const TeamID away_id = match.getAwayTeamId();
  const float home_strength = world.lineupStrength(home_id);
  const float away_strength = world.lineupStrength(away_id);
  career.onMatchPlayed(
      home_id, away_id, match.getHomeScore(), match.getAwayScore(),
      match.getMatchType() == MatchType::LEAGUE,
      BoardModel::expectedPoints(home_strength, away_strength, true),
      BoardModel::expectedPoints(away_strength, home_strength, false),
      managed_team_id);
}

std::vector<UpcomingFixture> Game::upcomingFixtures(TeamID team_id) const
{
  std::vector<UpcomingFixture> fixtures;
  const auto& schedule = calendar.getFullCalendar();
  for (auto day = schedule.lower_bound(currentDate); day != schedule.end();
       ++day)
  {
    for (const Match& match : day->second)
    {
      const bool home = match.getHomeTeamId() == team_id;
      if (match.isPlayed() || (!home && match.getAwayTeamId() != team_id))
        continue;
      fixtures.push_back({day->first,
                          home ? match.getAwayTeamId() : match.getHomeTeamId(),
                          home, match.getMatchType()});
      if (match.getMatchType() != MatchType::FRIENDLY) return fixtures;
    }
  }
  return fixtures;
}

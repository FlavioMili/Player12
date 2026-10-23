// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "controller/game_controller.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <span>
#include <sstream>
#include <unordered_set>

#include "database/gamedata.h"
#include "database/migrations/migrations.h"
#include "database/repositories/player_repository.h"
#include "database/repositories/team_repository.h"
#include "global/global.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/club_economy.h"
#include "model/injury.h"
#include "model/transfer_tuning.h"
#include "model/transfer_windows.h"
#include "model/world_rng.h"

namespace
{
// The AI transfer market draws from a stream reseeded from the world seed
// every day, so a reloaded save makes the same moves.
// Key of the daily AI market stream (other transfer streams use player ids).
constexpr std::uint64_t AI_MARKET_STREAM = 0xA1AA'4E7ULL;

std::uint32_t transferSeed(const GameData& gamedata, const GameDateValue& date)
{
  return static_cast<std::uint32_t>(
      mixHash(mixHash(gamedata.getWorldSeed(),
                      static_cast<std::uint64_t>(RngDomain::Transfers)),
              static_cast<std::uint64_t>(dayOrdinal(date))));
}
}  // namespace

GameController::GameController() : game(nullptr), gamedata(nullptr) {}

void GameController::newGame(int slot, std::optional<std::uint64_t> world_seed)
{
  const std::scoped_lock runtime_lock(runtime_mutex);
  editor_session.reset();
  editor_backup.reset();
  const auto startedAt = std::chrono::steady_clock::now();
  // The slot is not touched until the new world is complete: the first save
  // replaces it atomically and keeps the previous file as a rotation backup,
  // so a failure while creating the world never loses the old career.
  // The new world is built aside, so a failure keeps the current career.
  auto new_data = std::make_shared<GameData>();
  if (world_seed) new_data->setWorldSeed(*world_seed);
  auto connection = SaveManager::createWorkingCopy();
  auto new_game = std::make_unique<Game>(new_data, connection);
  last_load_error.reset();
  gamedata = std::move(new_data);
  db_conn = std::move(connection);
  game = std::move(new_game);
  transfer_listings.clear();
  transfer_rng.seed(transferSeed(*gamedata, game->getCurrentDate()));

  // Seed the market: every club lists its surplus, bids for its needs and
  // offers prospects for loan; then the 30 June expiries leave some players
  // without a club and some contracts carry a release clause. Deals,
  // free-agent signings included, start once the first day is played.
  for (const auto& team : gamedata->getTeamsVector())
  {
    const TeamID team_id = team.get().getId();
    evaluateAndActForTeam(team_id);
    game->getTransfers().listLoanProspects(team_id);
  }
  game->getTransfers().seedSummerFreeAgents();
  game->getTransfers().seedReleaseClauses();
  purgeStaleListings();
  startSession(slot, 0);
  persistLocked(false);
  last_initialization_milliseconds =
      std::chrono::duration<float, std::milli>(
          std::chrono::steady_clock::now() - startedAt)
          .count();
  Logger::info(std::format("New game initialized in {:.2f} ms",
                           last_initialization_milliseconds));
}

bool GameController::loadGame(int slot)
{
  const std::scoped_lock runtime_lock(runtime_mutex);
  editor_session.reset();
  editor_backup.reset();
  const auto startedAt = std::chrono::steady_clock::now();
  const std::filesystem::path path = RuntimePaths::savePath(slot);
  last_load_error.reset();
  // Checked read-only first: a damaged, unfinished or newer save is
  // refused without being modified.
  const SaveInspection inspection = SaveManager::inspect(path, SaveCheck::Full);
  if (inspection.status != SaveStatus::Ok)
  {
    SaveError error;
    error.detail = inspection.detail;
    error.found_version = inspection.schema_version;
    error.supported_version = inspection.supported_version;
    switch (inspection.status)
    {
      case SaveStatus::Missing:
        error.kind = SaveErrorKind::Missing;
        break;
      case SaveStatus::Incomplete:
        error.kind = SaveErrorKind::Incomplete;
        break;
      case SaveStatus::FutureVersion:
        error.kind = SaveErrorKind::FutureVersion;
        break;
      case SaveStatus::Ok:
      case SaveStatus::Corrupt:
        error.kind = SaveErrorKind::Corrupt;
        break;
    }
    Logger::warn(std::format("Save slot {} refused: {}", slot, error.detail));
    last_load_error = std::move(error);
    return false;
  }
  if (inspection.foreign_key_issues > 0)
    Logger::warn(std::format("Save slot {} has {} dangling references", slot,
                             inspection.foreign_key_issues));
  if (inspection.schema_version < Migrations::currentSchemaVersion())
  {
    // The slot file itself changes only on the next save; this copy stays.
    try
    {
      SaveManager::preserveBeforeMigration(path, inspection.schema_version);
    }
    catch (const std::exception& error)
    {
      Logger::warn(std::string("Could not keep a pre-upgrade copy: ") +
                   error.what());
    }
  }
  try
  {
    auto loaded_data = std::make_shared<GameData>();
    auto connection = SaveManager::openWorkingCopy(path);
    auto loaded_game = std::make_unique<Game>(loaded_data, connection);
    gamedata = std::move(loaded_data);
    db_conn = std::move(connection);
    game = std::move(loaded_game);
  }
  catch (const std::exception& exception)
  {
    SaveError error;
    error.kind = SaveErrorKind::Corrupt;
    error.detail = exception.what();
    if (const auto* future =
            dynamic_cast<const Migrations::FutureVersionError*>(&exception))
    {
      error.kind = SaveErrorKind::FutureVersion;
      error.found_version = future->found;
      error.supported_version = future->supported;
    }
    else if (const auto* failure = dynamic_cast<const SaveFailure*>(&exception))
    {
      error = failure->error;
    }
    Logger::error(
        std::format("Save slot {} failed to load: {}", slot, error.detail));
    last_load_error = std::move(error);
    return false;
  }
  transfer_rng.seed(transferSeed(*gamedata, game->getCurrentDate()));

  // Load transfer listings
  transfer_listings.clear();
  auto loaded = gamedata->loadAllTransferListings();
  for (auto& [pid, listing] : loaded)
  {
    auto player_opt = gamedata->getPlayer(pid);
    if (player_opt.has_value())
    {
      listing.seller_team_id = player_opt->get().getTeamId();
      gamedata->getPlayers().at(pid).setTransferStatus(TransferStatus::Listed);
      listing.attention_score = calculateAttentionScore(pid);
      transfer_listings[pid] = listing;
    }
  }
  purgeStaleListings();
  absorbListingBids();
  if (game->getManagedTeamId() != FREE_AGENTS_TEAM_ID)
    game->getWorld().getScouting().setManagedTeam(game->getManagedTeamId());
  syncOppositionOrders();
  startSession(slot, inspection.metadata.playtime_seconds);

  last_initialization_milliseconds =
      std::chrono::duration<float, std::milli>(
          std::chrono::steady_clock::now() - startedAt)
          .count();
  Logger::info(std::format("Save loaded in {:.2f} ms",
                           last_initialization_milliseconds));

  return true;
}

bool GameController::isGameLoaded() const
{
  return game != nullptr && gamedata != nullptr;
}

int GameController::getCurrentSeason() const
{
  return game ? game->getCurrentSeason() : 0;
}

GameDateValue GameController::getCurrentDate() const
{
  return game ? game->getCurrentDate() : GameDateValue{};
}

bool GameController::hasSelectedTeam() const
{
  if (!game || !gamedata) return false;
  auto managed_id = game->getManagedTeamId();
  return managed_id != FREE_AGENTS_TEAM_ID &&
         (*gamedata).getTeam(managed_id).has_value();
}

std::optional<std::reference_wrapper<Team>> GameController::getManagedTeam()
{
  // The free-agent pool is not a club anyone manages.
  if (!game || !gamedata || game->getManagedTeamId() == FREE_AGENTS_TEAM_ID)
    return std::nullopt;
  return (*gamedata).getTeam(game->getManagedTeamId());
}

std::optional<std::reference_wrapper<const Team>>
GameController::getManagedTeam() const
{
  if (!game || !gamedata || game->getManagedTeamId() == FREE_AGENTS_TEAM_ID)
    return std::nullopt;
  return (*gamedata).getTeam(game->getManagedTeamId());
}

void GameController::selectManagedTeam(uint16_t team_id)
{
  if (game && gamedata && team_id != FREE_AGENTS_TEAM_ID &&
      gamedata->getTeam(team_id).has_value())
  {
    // Orders against the old club's opponents stay behind.
    if (const auto previous = managedClub())
      previous->get().getStrategy().setOppositionOrders({});
    game->setManagedTeamId(team_id);
    syncOppositionOrders();
    game->getWorld().getScouting().setManagedTeam(team_id);
    // The market was seeded before the club had a manager: whether its
    // players are transfer- or loan-listed is now the manager's call.
    TransferMarket& market = game->getTransfers();
    std::vector<PlayerID> squad;
    for (const auto& player : gamedata->getPlayersForTeam(team_id))
      squad.push_back(player.get().getId());
    for (const PlayerID player_id : squad)
    {
      market.setLoanListed(player_id, false);
      removePlayerFromTransfer(player_id);
    }
    // Nor does it buy anyone the manager did not bid for.
    clearBidsBy(team_id);
  }
}

const std::vector<std::reference_wrapper<const League>>&
GameController::getLeagues() const
{
  return (*gamedata).getLeaguesVector();
}

const std::vector<std::reference_wrapper<const Team>>&
GameController::getTeams() const
{
  return (*gamedata).getTeamsVector();
}

const std::vector<std::reference_wrapper<const Player>>&
GameController::getPlayersForTeam(uint16_t team_id) const
{
  return (*gamedata).getPlayersForTeam(team_id);
}

std::vector<std::reference_wrapper<const Team>>
GameController::getTeamsInLeague(uint8_t league_id) const
{
  std::vector<std::reference_wrapper<const Team>> teams;
  for (const auto& team : (*gamedata).getTeamsVector())
  {
    if (team.get().getLeagueId() == league_id)
    {
      teams.push_back(team);
    }
  }
  return teams;
}

std::optional<std::reference_wrapper<const League>>
GameController::getLeagueById(uint8_t league_id) const
{
  return (*gamedata).getLeague(league_id);
}

std::optional<std::reference_wrapper<const Team>> GameController::getTeamById(
    uint16_t team_id) const
{
  return (*gamedata).getTeam(team_id);
}

const ClubIdentity* GameController::getClubIdentity(TeamID team_id) const
{
  if (!club_identities) club_identities = DataGenerator::loadClubIdentities();
  const auto found = club_identities->find(team_id);
  return found != club_identities->end() ? &found->second : nullptr;
}

const StatsConfig& GameController::getStatsConfig() const
{
  return (*gamedata).getStatsConfig();
}

void GameController::advanceDay()
{
  const std::scoped_lock runtime_lock(runtime_mutex);
  if (!editor_session.expired()) return;
  if (!game) return;
  game->resetSimulationProgress();
  continue_days_started = 0;
  continue_days_total = 1;
  simulateDay();
}

void GameController::simulateDay()
{
  // Counted just before Game::advanceDay() resets the match progress.
  ++continue_days_started;
  game->advanceDay();
  transfer_rng.seed(transferSeed(*gamedata, game->getCurrentDate()));
  purgeStaleListings();
  processAITransferActivity();
  runDelegatedDuties();
  // The day's moves changed squads after the scouts' morning priors.
  game->getWorld().getScouting().invalidatePriors();
  maybeAutosave();
}

int GameController::advanceToNextManagedFixture(int max_days)
{
  const std::scoped_lock runtime_lock(runtime_mutex);
  if (!editor_session.expired()) return 0;
  last_continue_stop = ContinueStop::None;
  if (!game || max_days <= 0 || !hasSelectedTeam()) return 0;
  continue_stop_requested = false;

  std::optional<GameDateValue> targetDate;
  const TeamID managedTeamId = game->getManagedTeamId();
  for (const auto& [date, matches] : game->getCalendar().getFullCalendar())
  {
    if (date < game->getCurrentDate()) continue;
    const bool hasManagedFixture = std::ranges::any_of(
        matches,
        [managedTeamId](const Match& match)
        {
          return !match.isPlayed() && (match.getHomeTeamId() == managedTeamId ||
                                       match.getAwayTeamId() == managedTeamId);
        });
    if (hasManagedFixture)
    {
      targetDate = date;
      break;
    }
  }

  int advancedDays = 0;
  game->resetSimulationProgress();
  continue_days_started = 0;
  continue_days_total = targetDate
                            ? std::clamp(dayOrdinal(*targetDate) -
                                             dayOrdinal(game->getCurrentDate()),
                                         0, max_days)
                            : 0;
  ContinueStop stop = ContinueStop::Fixture;
  while (targetDate && game->getCurrentDate() < *targetDate)
  {
    if (advancedDays >= max_days)
    {
      stop = ContinueStop::DayLimit;
      break;
    }
    if (continue_stop_requested)
    {
      stop = ContinueStop::Requested;
      break;
    }
    simulateDay();
    ++advancedDays;
    // A sacking (or a contract running out) ends the run for a club the
    // manager no longer has.
    if (game->getManagedTeamId() != managedTeamId)
    {
      stop = ContinueStop::LostJob;
      break;
    }
    // The national team's call-up is the head coach's to pick.
    if (game->getLastNationalEvents().squad_to_pick)
    {
      stop = ContinueStop::Decision;
      break;
    }
  }
  last_continue_stop = targetDate ? stop : ContinueStop::None;
  return advancedDays;
}

float GameController::ContinueProgress::fraction() const
{
  if (days_total <= 0) return 0.0f;
  float days = static_cast<float>(days_done);
  if (matches_total > 0)
    days +=
        static_cast<float>(matches_done) / static_cast<float>(matches_total);
  return std::clamp(days / static_cast<float>(days_total), 0.0f, 1.0f);
}

GameController::ContinueProgress GameController::getContinueProgress() const
{
  ContinueProgress progress;
  progress.days_done = std::max(continue_days_started.load() - 1, 0);
  progress.days_total = continue_days_total.load();
  if (game)
  {
    const SimulationProgress matches = game->getSimulationProgress();
    progress.matches_done = matches.completed;
    progress.matches_total = matches.total;
  }
  return progress;
}

void GameController::setSimulationThreads(unsigned threads)
{
  if (game) game->setSimulationThreads(threads);
}

void GameController::clearBidsBy(TeamID team_id)
{
  for (auto& [player_id, listing] : transfer_listings)
  {
    if (listing.highest_bidder_id != team_id) continue;
    listing.highest_bid = 0;
    listing.highest_bidder_id = std::nullopt;
    gamedata->saveTransferListing(listing);
  }
}

void GameController::purgeStaleListings()
{
  const TransferMarket& market = game->getTransfers();
  std::vector<PlayerID> staleListings;
  for (const auto& [playerId, listing] : transfer_listings)
  {
    const auto player = gamedata->getPlayer(playerId);
    if (!player || player->get().getTeamId() == FREE_AGENTS_TEAM_ID ||
        player->get().getTeamId() != listing.seller_team_id ||
        !market.canBeTraded(playerId))
      staleListings.push_back(playerId);
  }
  for (PlayerID playerId : staleListings)
  {
    transfer_listings.erase(playerId);
    gamedata->deleteTransferListing(playerId);
    if (auto player = gamedata->getPlayers().find(playerId);
        player != gamedata->getPlayers().end())
      player->second.setTransferStatus(TransferStatus::NotListed);
  }
}

bool GameController::setMatchResult(GameDateValue date, uint16_t home_id,
                                    uint16_t away_id, uint8_t home_score,
                                    uint8_t away_score)
{
  return game &&
         game->setMatchResult(date, home_id, away_id, home_score, away_score);
}

bool GameController::setMatchResult(GameDateValue date, uint16_t home_id,
                                    uint16_t away_id, const MatchEngine& engine)
{
  if (!game) return false;
  MatchReport report;
  report.fillFromEngine(engine, home_id, away_id);
  if (!game->setMatchResult(date, home_id, away_id, std::move(report),
                            MatchdaySquad::consequences(engine)))
    return false;
  recordManagedMatch(date, home_id, away_id, engine);
  return true;
}

std::optional<MatchRules::Knockout> GameController::getKnockoutRules(
    GameDateValue date, TeamID home_id, TeamID away_id) const
{
  if (!game) return std::nullopt;
  const Match* match = game->getCalendar().findMatch(date, home_id, away_id);
  if (!match) return std::nullopt;
  return game->getCompetitions().knockoutRules(game->getCalendar(), *match);
}

std::vector<PlayerID> GameController::getIneligibleSelections(
    TeamID team_id, MatchType type) const
{
  return game ? game->ineligibleSelections(team_id, type)
              : std::vector<PlayerID>{};
}

size_t GameController::autoFixLineup(TeamID team_id, MatchType type)
{
  return game ? game->fixMatchdaySquad(team_id, type) : 0;
}

void GameController::setAssistantFixesLineup(bool enabled)
{
  if (game) game->setAssistantFixesLineup(enabled);
}

bool GameController::getAssistantFixesLineup() const
{
  return !game || game->getAssistantFixesLineup();
}

std::vector<std::pair<PlayerID, PlayerID>> GameController::previewLineupFix(
    TeamID team_id, MatchType type) const
{
  return game ? game->previewMatchdaySquadFix(team_id, type)
              : std::vector<std::pair<PlayerID, PlayerID>>{};
}

bool GameController::canKickOff(TeamID team_id, MatchType type) const
{
  return game && game->canKickOff(team_id, type);
}

std::vector<StandingRow> GameController::getStandings(LeagueID league_id) const
{
  if (!game) return {};
  return game->getCompetitions().getStandings(game->getCalendar(), league_id);
}

std::vector<LeagueID> GameController::getCupIds() const
{
  return game ? game->getCompetitions().getCupIds() : std::vector<LeagueID>{};
}

std::optional<Competitions::CupStatus> GameController::getCupStatus(
    LeagueID cup_id) const
{
  if (!game) return std::nullopt;
  return game->getCompetitions().getCupStatus(game->getCalendar(), cup_id);
}

std::string GameController::getCupName(LeagueID cup_id) const
{
  return gamedata ? Competitions::cupName(*gamedata, cup_id) : std::string();
}

uint8_t GameController::getLeagueTier(LeagueID league_id) const
{
  return gamedata ? Competitions::leagueTier(*gamedata, league_id) : 0;
}

std::vector<Match> GameController::getTeamFixtures(TeamID team_id) const
{
  return game ? game->getCalendar().getTeamFixtures(team_id)
              : std::vector<Match>{};
}

std::optional<MatchReport> GameController::getMatchReport(GameDateValue date,
                                                          TeamID home_id,
                                                          TeamID away_id) const
{
  if (!game) return std::nullopt;
  return game->getCompetitions().getMatchReport(date, home_id, away_id);
}

const std::vector<SeasonHistoryEntry>& GameController::getSeasonHistory() const
{
  static const std::vector<SeasonHistoryEntry> no_history;
  return game ? game->getCompetitions().getSeasonHistory() : no_history;
}

std::vector<PlayerSeasonStats> GameController::getTopScorers(
    MatchType competition_type, LeagueID competition_id, size_t limit) const
{
  if (!game) return {};
  return game->getCompetitions().getTopScorers(competition_type, competition_id,
                                               limit);
}

std::vector<PlayerSeasonStats> GameController::getPlayerSeasonStats(
    PlayerID player_id) const
{
  if (!game) return {};
  return game->getCompetitions().getPlayerSeasonStats(player_id);
}

std::vector<PlayerSeasonStats> GameController::getPlayerCareer(
    PlayerID player_id) const
{
  if (!game) return {};
  return game->getCompetitions().getPlayerCareer(player_id);
}

const ContinentalCompetitions* GameController::getContinental() const
{
  return game ? &game->getCompetitions().getContinental() : nullptr;
}

ContinentalCompetitions::TieScore GameController::getContinentalTieScore(
    LeagueID competition_id, const ContinentalCompetitions::Tie& tie) const
{
  if (!game) return {};
  const ContinentalCompetitions& continental =
      game->getCompetitions().getContinental();
  const auto* season = continental.getSeason(competition_id);
  if (!season) return {};
  return continental.tieScore(game->getCalendar(), *season, tie);
}

const NationalTeams* GameController::getNationalTeams() const
{
  return game ? &game->getNationalTeams() : nullptr;
}

const International::Record* GameController::getInternationalRecord(
    PlayerID player_id) const
{
  return game ? game->getNationalTeams().getRecord(player_id) : nullptr;
}

std::optional<Language> GameController::getInternationalDuty(
    PlayerID player_id) const
{
  if (!game) return std::nullopt;
  return game->getNationalTeams().dutyNation(player_id, game->getCurrentDate());
}

uint8_t GameController::getSuspensionMatches(PlayerID player_id,
                                             MatchType scope) const
{
  return game ? game->getCompetitions().getDiscipline().banMatches(player_id,
                                                                   scope)
              : 0;
}

std::vector<DisciplinaryRecord> GameController::getSuspendedPlayers(
    TeamID team_id) const
{
  if (!game) return {};
  return game->getCompetitions().getDiscipline().suspendedPlayers(team_id,
                                                                  *gamedata);
}

bool GameController::saveGame()
{
  const std::scoped_lock runtime_lock(runtime_mutex);
  return persistLocked(false);
}

bool GameController::persistLocked(bool autosave)
{
  if (!editor_session.expired()) return false;
  if (!game || current_slot < 0) return false;
  using Clock = std::chrono::steady_clock;
  const auto started = Clock::now();
  SaveStatusInfo status;
  status.autosave = autosave;
  status.game_date = game->getCurrentDate().toString();
  try
  {
    if (delegation_after_holiday)
    {
      // Autosaves on holiday keep the manager's own delegation.
      DelegationPolicy& delegation = game->getGuidance().delegation;
      const DelegationPolicy on_holiday = delegation;
      delegation = *delegation_after_holiday;
      try
      {
        game->saveGame();
      }
      catch (...)
      {
        delegation = on_holiday;
        throw;
      }
      delegation = on_holiday;
    }
    else
    {
      game->saveGame();
    }
    status.timings.flush_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - started)
            .count();
    const auto playtime = std::chrono::duration_cast<std::chrono::seconds>(
        Clock::now() - session_started);
    SaveManager::stampMetadata(*db_conn, gamedata->getWorldSeed(),
                               game->getCurrentDate(),
                               playtime_before_session + playtime.count());
    const SaveTimings file =
        SaveManager::persist(*db_conn, RuntimePaths::savePath(current_slot),
                             autosave_policy.backups);
    status.timings.snapshot_ms = file.snapshot_ms;
    status.timings.verify_ms = file.verify_ms;
    status.timings.sync_ms = file.sync_ms;
  }
  catch (const SaveFailure& failure)
  {
    status.ok = false;
    status.error = failure.error;
  }
  catch (const std::exception& exception)
  {
    status.ok = false;
    status.error.kind = SaveErrorKind::Io;
    status.error.detail = exception.what();
  }
  status.timings.total_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - started).count();
  if (status.ok)
  {
    // Any save restarts the autosave interval.
    last_autosave_date = game->getCurrentDate();
    last_autosave_season = game->getCurrentSeason();
    Logger::info(std::format(
        "{} slot {} in {:.1f} ms (flush {:.1f}, snapshot {:.1f}, verify "
        "{:.1f}, sync {:.1f})",
        autosave ? "Autosaved" : "Saved", current_slot, status.timings.total_ms,
        status.timings.flush_ms, status.timings.snapshot_ms,
        status.timings.verify_ms, status.timings.sync_ms));
  }
  else
  {
    Logger::error(std::format("Saving slot {} failed ({}): {}", current_slot,
                              status.error.langKey(), status.error.detail));
  }
  const std::scoped_lock lock(save_status_mutex);
  status.successful_saves = save_status.successful_saves + (status.ok ? 1 : 0);
  save_status = std::move(status);
  return save_status.ok;
}

GameController::SaveStatusInfo GameController::getSaveStatus() const
{
  const std::scoped_lock lock(save_status_mutex);
  return save_status;
}

void GameController::startSession(int slot, std::int64_t playtime_seconds)
{
  current_slot = slot;
  playtime_before_session = playtime_seconds;
  session_started = std::chrono::steady_clock::now();
  last_autosave_date = game->getCurrentDate();
  last_autosave_season = game->getCurrentSeason();
  const std::scoped_lock lock(save_status_mutex);
  save_status = {};
}

void GameController::maybeAutosave()
{
  const GameDateValue today = game->getCurrentDate();
  bool managed_match_yesterday = false;
  if (autosave_policy.frequency == AutosaveFrequency::Matchday)
  {
    const TeamID managed = game->getManagedTeamId();
    const auto& schedule = game->getCalendar().getFullCalendar();
    if (const auto day = schedule.find(SeasonCalendar::addDays(today, -1));
        day != schedule.end())
    {
      managed_match_yesterday = std::ranges::any_of(
          day->second,
          [managed](const Match& match)
          {
            return match.isPlayed() && (match.getHomeTeamId() == managed ||
                                        match.getAwayTeamId() == managed);
          });
    }
  }
  if (SaveManager::isAutosaveDue(
          autosave_policy.frequency, last_autosave_date, last_autosave_season,
          today, game->getCurrentSeason(), managed_match_yesterday))
    persistLocked(true);
}

void GameController::setAutosavePolicy(const AutosavePolicy& policy)
{
  autosave_policy = policy;
  autosave_policy.backups = std::clamp(policy.backups, 0, 9);
}

AutosavePolicy GameController::getAutosavePolicy() const
{
  return autosave_policy;
}

std::optional<int> GameController::getCurrentSlot() const
{
  if (!game || current_slot < 0) return std::nullopt;
  return current_slot;
}

std::vector<SaveBackup> GameController::getSaveBackups(int slot) const
{
  return SaveManager::listBackups(RuntimePaths::savePath(slot));
}

bool GameController::restoreBackup(int slot,
                                   const std::filesystem::path& backup)
{
  try
  {
    SaveManager::restoreBackup(RuntimePaths::savePath(slot), backup);
  }
  catch (const SaveFailure& failure)
  {
    Logger::error("Restoring a backup failed: " + failure.error.detail);
    last_load_error = failure.error;
    return false;
  }
  return loadGame(slot);
}

bool GameController::deleteSave(int slot)
{
  if (game && slot == current_slot) return false;
  SaveManager::deleteSave(RuntimePaths::savePath(slot));
  return true;
}

GameController::SaveSlotMetadata GameController::getSaveSlotMetadata(
    int slot) const
{
  SaveSlotMetadata metadata;
  const std::filesystem::path path = RuntimePaths::savePath(slot);
  const SaveInspection inspection =
      SaveManager::inspect(path, SaveCheck::Quick);
  metadata.status = inspection.status;
  metadata.supported_schema_version = inspection.supported_version;
  if (inspection.status == SaveStatus::Missing) return metadata;
  metadata.exists = true;

  try
  {
    auto ftime = std::filesystem::last_write_time(path);
    auto sct =
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
    std::time_t tt = std::chrono::system_clock::to_time_t(sct);
    std::tm tm_buf{};
#if defined(_WIN32)
    const std::tm* tm = localtime_s(&tm_buf, &tt) == 0 ? &tm_buf : nullptr;
#else
    const std::tm* tm = localtime_r(&tt, &tm_buf);
#endif
    if (tm != nullptr)
    {
      char buf[100];
      if (std::strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", tm))
      {
        metadata.real_date = buf;
      }
    }
  }
  catch (const std::exception&)
  {
    metadata.real_date = "";
  }

  switch (inspection.status)
  {
    case SaveStatus::Incomplete:
      metadata.status_key = "SAVE_ERROR_INCOMPLETE";
      break;
    case SaveStatus::Corrupt:
      metadata.status_key = "SAVE_ERROR_CORRUPT";
      break;
    case SaveStatus::FutureVersion:
      metadata.status_key = "SAVE_ERROR_FUTURE_VERSION";
      break;
    case SaveStatus::Ok:
    case SaveStatus::Missing:
      break;
  }
  metadata.team_name = inspection.club_name;
  metadata.game_date = inspection.game_date;
  metadata.season = inspection.season;
  metadata.schema_version = inspection.schema_version;
  metadata.playtime_seconds = inspection.metadata.playtime_seconds;
  metadata.last_saved_game_date = inspection.metadata.last_saved_game_date;
  metadata.last_saved_utc = inspection.metadata.updated_at_utc;
  metadata.game_version = inspection.metadata.game_version;
  metadata.backups = SaveManager::countBackups(path);
  return metadata;
}

// ========== Transfer Market: Listing ==========

void GameController::listPlayerForTransfer(PlayerID pid, uint32_t asking_price)
{
  // Listed players draw bids from clubs whose own window is open.
  if (!isGameLoaded() || !isTransferWindowOpenAnywhere() || asking_price == 0)
    return;
  auto player_opt = gamedata->getPlayer(pid);
  if (!player_opt.has_value()) return;

  // Don't list free agents, loanees or players committed elsewhere
  if (player_opt->get().getTeamId() == FREE_AGENTS_TEAM_ID ||
      !game->getTransfers().canBeTraded(pid))
    return;

  float attention = calculateAttentionScore(pid);
  TransferListing listing(pid, player_opt->get().getTeamId(), asking_price,
                          game->getCurrentDate(), attention);
  const auto existing_listing = transfer_listings.find(pid);
  const std::optional<TransferListing> old_listing =
      existing_listing == transfer_listings.end()
          ? std::nullopt
          : std::optional<TransferListing>(existing_listing->second);
  transfer_listings[pid] = listing;

  // Update player status bitmask
  Player& player = gamedata->getPlayers().at(pid);
  const TransferStatus old_status = player.getTransferStatus();
  player.setTransferStatus(TransferStatus::Listed);

  try
  {
    db_conn->beginTransaction();
    gamedata->saveTransferListing(listing);
    PlayerRepository(db_conn).updatePlayer(player);
    db_conn->commitTransaction();
  }
  catch (...)
  {
    db_conn->rollbackTransaction();
    player.setTransferStatus(old_status);
    if (old_listing)
      transfer_listings[pid] = *old_listing;
    else
      transfer_listings.erase(pid);
    throw;
  }
}

void GameController::removePlayerFromTransfer(PlayerID pid)
{
  auto it = transfer_listings.find(pid);
  if (it == transfer_listings.end()) return;

  const TransferListing removed_listing = it->second;
  transfer_listings.erase(it);

  // Update player status bitmask
  auto player_opt = gamedata->getPlayer(pid);
  if (!player_opt.has_value())
  {
    transfer_listings[pid] = removed_listing;
    return;
  }

  Player& player = gamedata->getPlayers().at(pid);
  const TransferStatus old_status = player.getTransferStatus();
  player.setTransferStatus(TransferStatus::NotListed);

  try
  {
    db_conn->beginTransaction();
    PlayerRepository(db_conn).updatePlayer(player);
    gamedata->deleteTransferListing(pid);
    db_conn->commitTransaction();
  }
  catch (...)
  {
    db_conn->rollbackTransaction();
    player.setTransferStatus(old_status);
    transfer_listings[pid] = removed_listing;
    throw;
  }
}

bool GameController::isPlayerListed(PlayerID pid) const
{
  return transfer_listings.contains(pid);
}

// ========== Transfer Market: Queries ==========

const std::unordered_map<PlayerID, TransferListing>&
GameController::getAllListings() const
{
  return transfer_listings;
}

std::vector<const TransferListing*> GameController::getListingsExcludingTeam(
    TeamID team_id) const
{
  std::vector<const TransferListing*> result;
  for (const auto& [pid, listing] : transfer_listings)
  {
    if (listing.seller_team_id != team_id)
    {
      result.push_back(&listing);
    }
  }
  return result;
}

std::vector<const TransferListing*> GameController::getListingsByRole(
    PlayerRole role) const
{
  std::vector<const TransferListing*> result;
  for (const auto& [pid, listing] : transfer_listings)
  {
    auto player_opt = gamedata->getPlayer(pid);
    if (player_opt.has_value() &&
        getRoleCategory(player_opt->get().getRole()) == getRoleCategory(role))
    {
      result.push_back(&listing);
    }
  }
  return result;
}

// ========== Budget Check ==========

bool GameController::canAffordPlayer(TeamID buyer_id, PlayerID pid,
                                     uint32_t target_price) const
{
  auto player_opt = gamedata->getPlayer(pid);
  if (!player_opt.has_value()) return false;
  return canAffordPlayer(buyer_id, pid, target_price,
                         player_opt->get().getWage());
}

bool GameController::canAffordPlayer(TeamID buyer_id, PlayerID pid,
                                     uint32_t target_price,
                                     uint32_t offered_weekly_wage) const
{
  auto buyer_opt = gamedata->getTeam(buyer_id);
  auto player_opt = gamedata->getPlayer(pid);
  if (!buyer_opt.has_value() || !player_opt.has_value()) return false;

  const Player& player = player_opt->get();
  const Finances& finances = buyer_opt->get().getFinances();

  if (buyer_id == player.getTeamId() ||
      transferBudgetForTeam(buyer_id) < target_price)
  {
    return false;
  }

  int64_t current_wages =
      finances.getCurrentWageSpending(*gamedata, buyer_opt->get());
  int64_t new_wage_total =
      current_wages + static_cast<int64_t>(offered_weekly_wage);

  return new_wage_total <= finances.getWageBudget();
}

GameController::ContractTerms GameController::getContractDemand(
    PlayerID pid, bool free_agent) const
{
  const auto player = gamedata->getPlayer(pid);
  if (!player) return {};

  const float wageRaise = free_agent
                              ? TransferTuning::Contract::FREE_AGENT_WAGE_RAISE
                              : TransferTuning::Contract::TRANSFER_WAGE_RAISE;
  const auto requestedWage = static_cast<uint32_t>(
      std::ceil(static_cast<float>(player->get().getWage()) * wageRaise));

  uint8_t requestedYears = TransferTuning::Contract::VETERAN_MINIMUM_YEARS;
  if (player->get().getAge() <=
      TransferTuning::Contract::YOUNG_PLAYER_MAXIMUM_AGE)
  {
    requestedYears = TransferTuning::Contract::YOUNG_PLAYER_MINIMUM_YEARS;
  }
  else if (player->get().getAge() <=
           TransferTuning::Contract::PRIME_PLAYER_MAXIMUM_AGE)
  {
    requestedYears = TransferTuning::Contract::PRIME_PLAYER_MINIMUM_YEARS;
  }

  return {
      std::max(requestedWage, TransferTuning::Contract::MINIMUM_WEEKLY_WAGE),
      requestedYears};
}

bool GameController::isContractOfferAcceptable(PlayerID pid, bool free_agent,
                                               ContractTerms terms) const
{
  if (terms.years < TransferTuning::Contract::MINIMUM_YEARS ||
      terms.years > TransferTuning::Contract::MAXIMUM_YEARS)
  {
    return false;
  }
  const ContractTerms demand = getContractDemand(pid, free_agent);
  return demand.weekly_wage > 0 && terms.weekly_wage >= demand.weekly_wage &&
         terms.years >= demand.years;
}

// ========== Core Transfer Execution ==========

bool GameController::executeTransfer(PlayerID pid, TeamID buyer_id,
                                     TeamID seller_id, uint32_t price,
                                     std::optional<ContractTerms> contract)
{
  auto buyer_opt = gamedata->getTeam(buyer_id);
  auto seller_opt = gamedata->getTeam(seller_id);
  auto player_it = gamedata->getPlayers().find(pid);
  if (!buyer_opt || !seller_opt || player_it == gamedata->getPlayers().end() ||
      buyer_id == seller_id || player_it->second.getTeamId() != seller_id ||
      !canAffordPlayer(
          buyer_id, pid, price,
          contract ? contract->weekly_wage : player_it->second.getWage()))
  {
    return false;
  }

  Team& buyer = buyer_opt->get();
  Team& seller = seller_opt->get();
  Player& player = player_it->second;
  const TransferStatus old_status = player.getTransferStatus();
  const uint32_t old_wage = player.getWage();
  const uint8_t old_contract_years = player.getContractYears();
  const bool moves_money = seller_id != FREE_AGENTS_TEAM_ID && price > 0;
  const GameDateValue today = game->getCurrentDate();

  db_conn->beginTransaction();
  try
  {
    if (moves_money)
    {
      seller.getFinances().record(today, FinanceCategory::TransferFeeIn,
                                  static_cast<int64_t>(price));
      buyer.getFinances().record(today, FinanceCategory::TransferFeeOut,
                                 -static_cast<int64_t>(price));
    }
    seller.removePlayerID(pid);
    buyer.addPlayerID(pid);
    gamedata->transferPlayer(pid, buyer_id);
    player.setTransferStatus(TransferStatus::NotListed);
    if (contract)
    {
      player.setWage(contract->weekly_wage);
      player.setContractYears(contract->years);
    }

    PlayerRepository playerRepo(db_conn);
    playerRepo.updatePlayer(player);
    TeamRepository team_repo(db_conn);
    team_repo.updateTeamState(buyer);
    if (moves_money) team_repo.updateTeamState(seller);
    gamedata->deleteTransferListing(pid);
    db_conn->commitTransaction();
  }
  catch (const std::exception& exception)
  {
    db_conn->rollbackTransaction();
    gamedata->transferPlayer(pid, seller_id);
    buyer.removePlayerID(pid);
    seller.addPlayerID(pid);
    if (moves_money)
    {
      buyer.getFinances().revertLastTransaction();
      seller.getFinances().revertLastTransaction();
    }
    player.setTransferStatus(old_status);
    player.setWage(old_wage);
    player.setContractYears(old_contract_years);
    Logger::error("Transfer failed: " + std::string(exception.what()));
    return false;
  }

  // The manager's line-up is repaired, never rebuilt.
  const TeamID managed = game->getManagedTeamId();
  if (buyer_id != managed)
    buyer.generateStartingXI(*gamedata, gamedata->getStatsConfig());
  if (seller_id == managed)
    game->getTransfers().removeFromLineup(seller, player);
  else if (seller_id != FREE_AGENTS_TEAM_ID)
    seller.generateStartingXI(*gamedata, gamedata->getStatsConfig());
  transfer_listings.erase(pid);
  game->getWorld().onTransferCompleted(today, pid, seller_id, buyer_id, price,
                                       game->getManagedTeamId());
  game->getTransfers().recordLegacyTransfer(pid, seller_id, buyer_id, price,
                                            today);
  return true;
}

// ========== Buy + Sign + Market Value ==========

bool GameController::buyPlayer(PlayerID pid, TeamID buyer_id, uint32_t price)
{
  return buyPlayerWithContract(pid, buyer_id, price,
                               getContractDemand(pid, false));
}

bool GameController::buyPlayerWithContract(PlayerID pid, TeamID buyer_id,
                                           uint32_t price, ContractTerms terms)
{
  auto player_opt = gamedata->getPlayer(pid);
  if (!player_opt.has_value()) return false;

  TeamID seller_id = player_opt->get().getTeamId();
  if (seller_id == buyer_id) return false;
  if (!isTransferWindowOpenFor(buyer_id) ||
      !game->getTransfers().canBeTraded(pid))
    return false;

  auto it = transfer_listings.find(pid);
  if (it == transfer_listings.end() || it->second.seller_team_id != seller_id)
  {
    return false;
  }
  else
  {
    const TransferListing& listing = it->second;
    const bool paysAskingPrice = price == listing.asking_price;
    const bool ownsHighestBid =
        listing.highest_bidder_id == buyer_id && price == listing.highest_bid;
    if (!paysAskingPrice && !ownsHighestBid)
    {
      return false;
    }
  }

  if (!isContractOfferAcceptable(pid, false, terms) ||
      !canAffordPlayer(buyer_id, pid, price, terms.weekly_wage))
  {
    return false;
  }

  return executeTransfer(pid, buyer_id, seller_id, price, terms);
}

bool GameController::signFreeAgent(PlayerID pid, TeamID buyer_id)
{
  return signFreeAgentWithContract(pid, buyer_id, getContractDemand(pid, true));
}

bool GameController::signFreeAgentWithContract(PlayerID pid, TeamID buyer_id,
                                               ContractTerms terms)
{
  auto player_opt = gamedata->getPlayer(pid);
  if (!player_opt.has_value()) return false;

  if (player_opt->get().getTeamId() != FREE_AGENTS_TEAM_ID ||
      !canSignFreeAgentFor(buyer_id))
    return false;
  if (!isContractOfferAcceptable(pid, true, terms) ||
      !canAffordPlayer(buyer_id, pid, 0, terms.weekly_wage))
  {
    return false;
  }

  return executeTransfer(pid, buyer_id, FREE_AGENTS_TEAM_ID, 0, terms);
}

uint32_t GameController::getPlayerMarketValue(PlayerID pid) const
{
  auto player_opt = gamedata->getPlayer(pid);
  if (!player_opt.has_value()) return 0;

  const Player& player = player_opt->get();
  player.updateMarketValue(gamedata->getStatsConfig());
  if (!game) return player.getMarketValue();
  // Recent honours raise the price (at most +25%).
  return static_cast<uint32_t>(std::lround(
      static_cast<double>(player.getMarketValue()) *
      static_cast<double>(game->getWorld().getAwards().valueMultiplier(
          pid, game->getCurrentDate()))));
}

// ========== Negotiation ==========

bool GameController::submitBid(PlayerID pid, TeamID bidder_id,
                               uint32_t bid_amount)
{
  auto it = transfer_listings.find(pid);
  if (it == transfer_listings.end()) return false;

  if (bidder_id == it->second.seller_team_id) return false;

  if (!isTransferWindowOpenFor(bidder_id) ||
      !game->getTransfers().canBeTraded(pid))
    return false;

  if (!canAffordPlayer(bidder_id, pid, bid_amount)) return false;

  // The managed club negotiates bids for its own players.
  if (hasSelectedTeam() &&
      it->second.seller_team_id == game->getManagedTeamId())
    return routeListingBid(pid, bidder_id, bid_amount);

  if (bid_amount > it->second.highest_bid)
  {
    it->second.highest_bid = bid_amount;
    it->second.highest_bidder_id = bidder_id;
    gamedata->saveTransferListing(it->second);
    game->getWorld().onTransferBid(game->getCurrentDate(), pid, bidder_id,
                                   bid_amount, game->getManagedTeamId());
    return true;
  }

  return false;
}

bool GameController::acceptBid(PlayerID pid)
{
  auto it = transfer_listings.find(pid);
  if (it == transfer_listings.end()) return false;
  if (!it->second.highest_bidder_id.has_value()) return false;
  const ContractTerms contract = getContractDemand(pid, false);
  if (!isTransferWindowOpenFor(*it->second.highest_bidder_id) ||
      !canAffordPlayer(*it->second.highest_bidder_id, pid,
                       it->second.highest_bid, contract.weekly_wage))
  {
    return false;
  }

  return executeTransfer(pid, *it->second.highest_bidder_id,
                         it->second.seller_team_id, it->second.highest_bid,
                         contract);
}

bool GameController::rejectBid(PlayerID pid)
{
  auto it = transfer_listings.find(pid);
  if (it == transfer_listings.end()) return false;

  it->second.highest_bid = 0;
  it->second.highest_bidder_id = std::nullopt;
  gamedata->saveTransferListing(it->second);
  return true;
}

bool GameController::counterOffer(PlayerID pid, uint32_t new_price)
{
  auto it = transfer_listings.find(pid);
  if (it == transfer_listings.end()) return false;
  if (new_price == 0) return false;

  it->second.asking_price = new_price;
  it->second.highest_bid = 0;
  it->second.highest_bidder_id = std::nullopt;

  gamedata->saveTransferListing(it->second);
  return true;
}

bool GameController::isTransferWindowOpen() const
{
  return game && hasSelectedTeam() &&
         isTransferWindowOpenFor(game->getManagedTeamId());
}

bool GameController::isTransferWindowOpenFor(TeamID club) const
{
  if (!game || club == FREE_AGENTS_TEAM_ID) return false;
  const auto team = std::as_const(*gamedata).getTeam(club);
  return team && TransferWindows::isOpen(team->get().getLeagueId(),
                                         game->getCurrentDate());
}

bool GameController::isTransferWindowOpenAnywhere() const
{
  return game && TransferWindows::isOpenAnywhere(game->getCurrentDate());
}

bool GameController::canSignFreeAgentFor(TeamID club) const
{
  if (!game || club == FREE_AGENTS_TEAM_ID) return false;
  const auto team = std::as_const(*gamedata).getTeam(club);
  return team && TransferWindows::canSignFreeAgent(team->get().getLeagueId(),
                                                   game->getCurrentDate());
}

// ========== AI Squad Evaluation ==========

GameController::SquadNeeds GameController::evaluateSquadNeeds(
    TeamID team_id) const
{
  SquadNeeds needs;
  const auto& team_players = gamedata->getPlayersForTeam(team_id);
  if (team_players.empty()) return needs;

  int gk = 0, cb = 0, lb = 0, rb = 0, mid = 0, wing = 0, st = 0;

  for (const auto& pref : team_players)
  {
    const Player& p = pref.get();
    // Skip players already listed for sale so we correctly identify
    // replacements
    if (isPlayerListed(p.getId())) continue;

    switch (p.getRole())
    {
      case PlayerRole::GK:
        gk++;
        break;
      case PlayerRole::CB:
        cb++;
        break;
      case PlayerRole::LB:
        lb++;
        break;
      case PlayerRole::RB:
        rb++;
        break;
      case PlayerRole::CDM:
      case PlayerRole::CM:
      case PlayerRole::CAM:
        mid++;
        break;
      case PlayerRole::LM:
      case PlayerRole::RM:
      case PlayerRole::LW:
      case PlayerRole::RW:
        wing++;
        break;
      case PlayerRole::ST:
        st++;
        break;
      default:
        break;
    }
  }

  // Target squad sizes per role group (shared with the squad planner)
  using SquadPlanner::targetDepth;
  const int TARGET_GK = targetDepth(PlannerGroup::Goalkeeper);
  const int TARGET_CB = targetDepth(PlannerGroup::CentreBack);
  const int TARGET_LB = targetDepth(PlannerGroup::LeftBack);
  const int TARGET_RB = targetDepth(PlannerGroup::RightBack);
  const int TARGET_MID = targetDepth(PlannerGroup::Midfield);
  const int TARGET_WING = targetDepth(PlannerGroup::Wide);
  const int TARGET_ST = targetDepth(PlannerGroup::Striker);

  auto calc = [](int current, int target, int& missing, int& surplus)
  {
    if (current < target)
    {
      missing = target - current;
      surplus = 0;
    }
    else
    {
      missing = 0;
      surplus = current - target;
    }
  };

  calc(gk, TARGET_GK, needs.missing_gk, needs.surplus_gk);
  calc(cb, TARGET_CB, needs.missing_cb, needs.surplus_cb);
  calc(lb, TARGET_LB, needs.missing_lb, needs.surplus_lb);
  calc(rb, TARGET_RB, needs.missing_rb, needs.surplus_rb);
  calc(mid, TARGET_MID, needs.missing_mid, needs.surplus_mid);
  calc(wing, TARGET_WING, needs.missing_wing, needs.surplus_wing);
  calc(st, TARGET_ST, needs.missing_st, needs.surplus_st);

  auto team_opt = gamedata->getTeam(team_id);
  if (team_opt.has_value() &&
      team_opt->get().getFinances().getBalance() > 10000000)
  {
    static constexpr std::array<PlayerRole, 7> CATEGORIES = {
        PlayerRole::GK, PlayerRole::CB, PlayerRole::LB, PlayerRole::RB,
        PlayerRole::CM, PlayerRole::LW, PlayerRole::ST};
    float weakestAverage = std::numeric_limits<float>::max();
    for (const PlayerRole category : CATEGORIES)
    {
      float total = 0.0f;
      int count = 0;
      for (const auto& reference : team_players)
      {
        const Player& player = reference.get();
        if (!isPlayerListed(player.getId()) &&
            getRoleCategory(player.getRole()) == category)
        {
          total +=
              static_cast<float>(player.getOverall(gamedata->getStatsConfig()));
          ++count;
        }
      }
      if (count > 0)
      {
        const float average = total / static_cast<float>(count);
        if (average < weakestAverage)
        {
          weakestAverage = average;
          needs.upgrade_target = category;
        }
      }
    }
  }

  return needs;
}

// ========== Attention Score ==========

float GameController::calculateAttentionScore(PlayerID pid) const
{
  auto player_opt = gamedata->getPlayer(pid);
  if (!player_opt.has_value()) return 0.0f;

  const Player& p = player_opt->get();
  const StatsConfig& config = gamedata->getStatsConfig();

  float score = static_cast<float>(p.getOverall(config)) / 100.0f;
  if (score <= 0.0f) return 0.0f;

  if (p.getAge() < 20)
    score *= 1.2f;
  else if (p.getAge() <= 28)
    score *= 1.0f;
  else if (p.getAge() <= 32)
    score *= 0.8f;
  else
    score *= 0.4f;

  if (p.getContractYears() <= 1)
    score *= 1.3f;
  else if (p.getContractYears() <= 2)
    score *= 1.1f;

  auto team_opt = gamedata->getTeam(p.getTeamId());
  if (team_opt.has_value())
  {
    score *= getLeagueAttentionMultiplier(team_opt->get().getLeagueId());
  }

  score *= 1.0f;

  return std::clamp(score, 0.0f, 1.5f);
}

float GameController::getLeagueAttentionMultiplier(LeagueID league_id) const
{
  switch (league_id)
  {
    case 2:
      return 1.3f;
    case 3:
      return 1.3f;
    case 1:
      return 1.2f;
    case 4:
      return 1.2f;
    case 5:
      return 1.1f;
    case 11:
      return 1.0f;
    case 10:
      return 1.0f;
    case 12:
      return 0.9f;
    default:
      return 0.8f;
  }
}

// ========== Max Price Calculation ==========

uint32_t GameController::calculateMaxPrice(PlayerID pid, TeamID buyer_id,
                                           const SquadNeeds& needs) const
{
  auto player_opt = gamedata->getPlayer(pid);
  auto buyer_opt = gamedata->getTeam(buyer_id);
  if (!player_opt.has_value() || !buyer_opt.has_value()) return 0;

  const Player& p = player_opt->get();
  uint32_t market_value = getPlayerMarketValue(pid);
  if (market_value == 0) return 0;

  auto getNeedMult = [&](PlayerRole role) -> float
  {
    switch (getRoleCategory(role))
    {
      case PlayerRole::GK:
        return needs.missing_gk >= 2 ? 2.2f
                                     : (needs.missing_gk == 1 ? 1.6f : 0.7f);
      case PlayerRole::CB:
        return needs.missing_cb >= 2 ? 2.0f
                                     : (needs.missing_cb == 1 ? 1.5f : 0.8f);
      case PlayerRole::LB:
        return needs.missing_lb >= 1 ? 1.8f : 0.7f;
      case PlayerRole::RB:
        return needs.missing_rb >= 1 ? 1.8f : 0.7f;
      case PlayerRole::CM:
        return needs.missing_mid >= 2 ? 2.0f
                                      : (needs.missing_mid == 1 ? 1.5f : 0.8f);
      case PlayerRole::LW:
        return needs.missing_wing >= 2
                   ? 2.0f
                   : (needs.missing_wing == 1 ? 1.5f : 0.8f);
      case PlayerRole::ST:
        return needs.missing_st >= 2 ? 2.2f
                                     : (needs.missing_st == 1 ? 1.6f : 0.8f);
      default:
        return 1.0f;
    }
  };
  float need_mult = getNeedMult(p.getRole());

  auto role_listings = getListingsByRole(getRoleCategory(p.getRole()));
  float scarcity_mult;
  if (role_listings.size() < 3)
    scarcity_mult = 1.5f;
  else if (role_listings.size() < 8)
    scarcity_mult = 1.1f;
  else
    scarcity_mult = 0.8f;

  const auto& team_players = gamedata->getPlayersForTeam(buyer_id);
  float team_avg = 0.0f;
  int count = 0;
  for (const auto& ref : team_players)
  {
    team_avg +=
        static_cast<float>(ref.get().getOverall(gamedata->getStatsConfig()));
    count++;
  }
  team_avg = count > 0 ? team_avg / static_cast<float>(count) : 50.0f;

  float player_ovr =
      static_cast<float>(p.getOverall(gamedata->getStatsConfig()));
  float diff = player_ovr - team_avg;

  float prestige_mult;
  if (diff > 15.0f)
    prestige_mult = 1.5f;
  else if (diff > 5.0f)
    prestige_mult = 1.2f;
  else if (diff > -5.0f)
    prestige_mult = 1.0f;
  else if (diff > -15.0f)
    prestige_mult = 0.8f;
  else
    prestige_mult = 0.6f;

  uint32_t budget = transferBudgetForTeam(buyer_id);
  const double valuation =
      static_cast<double>(market_value) * static_cast<double>(need_mult) *
      static_cast<double>(scarcity_mult) * static_cast<double>(prestige_mult);
  return static_cast<uint32_t>(
      std::min<double>(valuation, static_cast<double>(budget)));
}

// ========== Find Targets ==========

std::vector<PlayerID> GameController::findTargetsForRole(
    PlayerRole role, TeamID buyer_id, const SquadNeeds& /*needs*/) const
{
  std::vector<std::pair<PlayerID, float>> candidates;
  PlayerRole broad_cat = getRoleCategory(role);

  for (const auto& [pid, listing] : transfer_listings)
  {
    if (listing.seller_team_id == buyer_id ||
        !game->getTransfers().canBeTraded(pid))
      continue;

    auto player_opt = gamedata->getPlayer(pid);
    if (!player_opt.has_value()) continue;

    const Player& p = player_opt->get();

    if (getRoleCategory(p.getRole()) != broad_cat) continue;

    float score = listing.attention_score;

    if (p.getRole() == role) score *= 1.2f;

    if (canAffordPlayer(buyer_id, pid, listing.asking_price))
    {
      candidates.emplace_back(pid, score);
    }
  }

  const auto& free_agents = gamedata->getPlayersForTeam(FREE_AGENTS_TEAM_ID);
  for (const auto& ref : free_agents)
  {
    const Player& p = ref.get();
    if (getRoleCategory(p.getRole()) != broad_cat) continue;

    float score = calculateAttentionScore(p.getId());
    if (p.getRole() == role) score *= 1.2f;

    if (canAffordPlayer(buyer_id, p.getId(), 0))
    {
      candidates.emplace_back(p.getId(), score);
    }
  }

  // Ties by id: the listings are a hash map, whose order differs after a
  // reload.
  std::ranges::sort(candidates,
                    [](const auto& a, const auto& b)
                    {
                      return a.second != b.second ? a.second > b.second
                                                  : a.first < b.first;
                    });

  std::vector<PlayerID> result;
  for (const auto& [pid, score] : candidates)
  {
    result.push_back(pid);
  }
  return result;
}

// ========== AI Team Activity ==========

void GameController::evaluateAndActForTeam(TeamID team_id)
{
  SquadNeeds needs = evaluateSquadNeeds(team_id);
  const uint32_t budget = transferBudgetForTeam(team_id);

  auto tryBuy = [&](PlayerRole role, int priority) -> bool
  {
    if (priority <= 0 || budget == 0) return false;
    const auto targets = findTargetsForRole(role, team_id, needs);
    // The best few targets are sounded out; players who would refuse the
    // club (stature, playing time) are skipped.
    constexpr size_t SOUNDED_TARGETS = 5;
    const TransferMarket& market = game->getTransfers();
    const auto chosen = std::ranges::find_if(
        targets.begin(),
        targets.begin() + static_cast<std::ptrdiff_t>(
                              std::min(targets.size(), SOUNDED_TARGETS)),
        [&](PlayerID candidate)
        {
          const auto player = gamedata->getPlayer(candidate);
          return player &&
                 market.wouldJoin(
                     candidate, team_id,
                     player->get().getTeamId() == FREE_AGENTS_TEAM_ID
                         ? TransferNegotiation::ContractKind::FreeAgent
                         : TransferNegotiation::ContractKind::Transfer);
        });
    if (chosen == targets.end() ||
        chosen - targets.begin() >=
            static_cast<std::ptrdiff_t>(SOUNDED_TARGETS))
      return false;

    const PlayerID target = *chosen;
    auto player_opt = gamedata->getPlayer(target);
    if (!player_opt.has_value()) return false;

    if (player_opt->get().getTeamId() == FREE_AGENTS_TEAM_ID)
    {
      return signFreeAgent(target, team_id);
    }

    const uint32_t max_price = calculateMaxPrice(target, team_id, needs);

    const uint32_t marketValue = getPlayerMarketValue(target);
    const auto initialBid = static_cast<uint32_t>(
        static_cast<float>(marketValue) * randomFloat(0.75f, 1.15f));
    const uint32_t bid = std::min(initialBid, max_price);

    if (bid > 0 && bid <= budget)
    {
      return submitBid(target, team_id, bid);
    }
    return false;
  };

  const std::array<std::pair<PlayerRole, int>, 7> shortages = {{
      {PlayerRole::GK, needs.missing_gk},
      {PlayerRole::CB, needs.missing_cb},
      {PlayerRole::LB, needs.missing_lb},
      {PlayerRole::RB, needs.missing_rb},
      {PlayerRole::CM, needs.missing_mid},
      {PlayerRole::LW, needs.missing_wing},
      {PlayerRole::ST, needs.missing_st},
  }};
  for (const auto& [role, missing] : shortages)
  {
    if (tryBuy(role, missing)) return;
  }

  if (needs.upgrade_target && !transfer_listings.empty() &&
      randomFloat(0.0f, 1.0f) < 0.45f && tryBuy(*needs.upgrade_target, 1))
  {
    return;
  }

  auto trySell = [&](PlayerRole role, int surplus_count) -> bool
  {
    if (surplus_count <= 0) return false;

    const auto& team_players = gamedata->getPlayersForTeam(team_id);
    std::vector<PlayerID> candidates;
    for (const auto& ref : team_players)
    {
      const Player& p = ref.get();
      if (getRoleCategory(p.getRole()) == role && !isPlayerListed(p.getId()))
      {
        candidates.push_back(p.getId());
      }
    }

    if (candidates.empty()) return false;

    std::ranges::sort(
        candidates,
        [&](PlayerID a, PlayerID b)
        {
          auto pa = gamedata->getPlayer(a);
          auto pb = gamedata->getPlayer(b);
          if (!pa.has_value() || !pb.has_value()) return false;
          return pa->get().getOverall(gamedata->getStatsConfig()) <
                 pb->get().getOverall(gamedata->getStatsConfig());
        });

    const PlayerID toSell = candidates.front();
    const auto asking =
        static_cast<uint32_t>(static_cast<float>(getPlayerMarketValue(toSell)) *
                              randomFloat(0.8f, 1.3f));
    if (asking > 0)
    {
      listPlayerForTransfer(toSell, asking);
      return isPlayerListed(toSell);
    }
    return false;
  };

  const std::array<std::pair<PlayerRole, int>, 7> surpluses = {{
      {PlayerRole::GK, needs.surplus_gk},
      {PlayerRole::CB, needs.surplus_cb},
      {PlayerRole::LB, needs.surplus_lb},
      {PlayerRole::RB, needs.surplus_rb},
      {PlayerRole::CM, needs.surplus_mid},
      {PlayerRole::LW, needs.surplus_wing},
      {PlayerRole::ST, needs.surplus_st},
  }};
  for (const auto& [role, surplus] : surpluses)
  {
    if (trySell(role, surplus)) return;
  }

  if (needs.upgrade_target) tryBuy(*needs.upgrade_target, 1);
}

// ========== Helpers ==========

PlayerRole GameController::getRoleCategory(PlayerRole role) const
{
  using enum PlayerRole;
  switch (role)
  {
    case GK:
      return GK;
    case CB:
      return CB;
    case LB:
      return LB;
    case RB:
      return RB;
    case CDM:
    case CM:
    case CAM:
      return CM;
    case LM:
    case RM:
    case LW:
    case RW:
      return LW;
    case ST:
      return ST;
    default:
      return UNKNOWN;
  }
}

uint32_t GameController::transferBudgetForTeam(TeamID team_id) const
{
  auto team_opt = gamedata->getTeam(team_id);
  if (!team_opt.has_value()) return 0;

  // The board's allowance, never more than the cash left after a payroll
  // reserve and the instalments still due this season; nothing under an
  // embargo. The market applies the same rule to computer-managed clubs.
  const Finances& finances = team_opt->get().getFinances();
  // Bonuses and agent fees of agreed pre-contracts are already committed.
  const int64_t budget =
      game ? game->getTransfers().spendableBudget(team_id,
                                                  game->getCurrentDate()) -
                 game->getTransfers().committedPreContractCosts(team_id)
           : ClubEconomy::availableTransferBudget(
                 finances.getTransferBudget(), finances.getBalance(),
                 getWeeklyWageBill(team_id), 0);
  return static_cast<uint32_t>(
      std::clamp<int64_t>(budget, 0, std::numeric_limits<uint32_t>::max()));
}

void GameController::evaluateIncomingAIBids()
{
  auto managed_team_opt = game->getManagedTeamId();
  std::vector<PlayerID> to_accept;
  std::vector<PlayerID> to_reject;

  // In id order: a random draw is taken per listing, and the hash map's
  // order differs after a reload.
  std::vector<PlayerID> listed;
  listed.reserve(transfer_listings.size());
  for (const auto& [pid, listing] : transfer_listings) listed.push_back(pid);
  std::ranges::sort(listed);
  for (const PlayerID pid : listed)
  {
    const TransferListing& listing = transfer_listings.at(pid);
    if (listing.seller_team_id == FREE_AGENTS_TEAM_ID ||
        listing.seller_team_id == managed_team_opt)
    {
      continue;
    }

    if (listing.highest_bidder_id.has_value() && listing.highest_bid > 0)
    {
      if (listing.highest_bid >= listing.asking_price)
      {
        to_accept.push_back(pid);
      }
      else if (listing.highest_bid >=
               static_cast<uint32_t>(static_cast<float>(listing.asking_price) *
                                     0.85f))
      {
        if (randomFloat(0.0f, 1.0f) < 0.30f)
        {
          to_accept.push_back(pid);
        }
        else
        {
          to_reject.push_back(pid);
        }
      }
      else
      {
        to_reject.push_back(pid);
      }
    }
  }

  std::ranges::sort(to_accept);
  for (PlayerID pid : to_accept)
  {
    // A deal the buyer can no longer fund falls through.
    if (!completeAiSale(pid)) rejectBid(pid);
  }
  for (PlayerID pid : to_reject)
  {
    rejectBid(pid);
  }
}

bool GameController::completeAiSale(PlayerID pid)
{
  const auto it = transfer_listings.find(pid);
  const auto player = gamedata->getPlayer(pid);
  if (it == transfer_listings.end() || !it->second.highest_bidder_id ||
      !player || player->get().getTeamId() != it->second.seller_team_id ||
      !isTransferWindowOpenFor(*it->second.highest_bidder_id))
    return false;
  TransferMarket& market = game->getTransfers();
  const TeamID buyer_id = *it->second.highest_bidder_id;
  TransferMarket::Deal deal;
  deal.player_id = pid;
  deal.buyer_id = buyer_id;
  deal.kind = TransferKind::Permanent;
  deal.terms = TransferNegotiation::aiOfferTerms(it->second.highest_bid);
  deal.contract = TransferNegotiation::demandedOffer(
      TransferNegotiation::contractDemand(market.playerContext(
          pid, buyer_id, TransferNegotiation::ContractKind::Transfer)));
  if (!canPayDeal(deal) ||
      !market.completeTransfer(deal, game->getCurrentDate(),
                               game->getManagedTeamId()))
    return false;
  transfer_listings.erase(pid);
  return true;
}

uint32_t GameController::listingPrice(PlayerID pid) const
{
  const auto listing = transfer_listings.find(pid);
  const auto player = gamedata->getPlayer(pid);
  if (listing == transfer_listings.end() || !player ||
      listing->second.seller_team_id != player->get().getTeamId())
    return 0;
  return listing->second.asking_price;
}

bool GameController::canPayDeal(const TransferMarket::Deal& deal) const
{
  const auto buyer = gamedata->getTeam(deal.buyer_id);
  const auto player = gamedata->getPlayer(deal.player_id);
  if (!buyer || !player ||
      (game && game->getWorld().isTransferEmbargoed(deal.buyer_id)))
    return false;
  const Finances& finances = buyer->get().getFinances();
  const auto upfront =
      static_cast<int64_t>(TransferNegotiation::upfrontAmount(deal.terms));
  const auto agent = static_cast<int64_t>(TransferMarket::agentFee(deal));
  const auto bonus = static_cast<int64_t>(deal.contract.signing_bonus);
  const int64_t payroll = getWeeklyWageBill(deal.buyer_id) +
                          static_cast<int64_t>(deal.contract.weekly_wage);
  // Fees come out of the board's transfer allowance; a free signing only
  // needs the cash for the agent and the bonus.
  const bool cash_ok =
      deal.terms.fee > 0
          ? upfront + agent <= transferBudgetForTeam(deal.buyer_id) &&
                bonus <= finances.getBalance()
          : agent + bonus <= finances.getBalance();
  return cash_ok && payroll <= finances.getWageBudget();
}

void GameController::processAITransferActivity()
{
  using Market = TransferTuning::Market;
  const GameDateValue today = game->getCurrentDate();
  const TeamID managed = game->getManagedTeamId();
  TransferMarket& market = game->getTransfers();
  WorldRng rng = WorldRng::stream(
      gamedata->getWorldSeed(), RngDomain::Transfers,
      static_cast<std::uint64_t>(dayOrdinal(today)), AI_MARKET_STREAM);

  std::vector<TeamID> clubs;
  clubs.reserve(gamedata->getTeamsVector().size());
  for (const auto& team : gamedata->getTeamsVector())
  {
    const TeamID id = team.get().getId();
    if (id != FREE_AGENTS_TEAM_ID && id != managed) clubs.push_back(id);
  }
  std::ranges::sort(clubs);
  rng.shuffle(std::span<TeamID>(clubs));

  processOfferReplies();
  market.runAiPreContracts(
      today, managed, rng,
      Market::perDay(clubs.size(), Market::DAILY_PRE_CONTRACT_SHARE));

  // Every club follows its own country's window. The clubs of a country
  // share one, so the day's budget is set per country and rises towards
  // that country's deadline; clubs whose window is shut can only sign free
  // agents, from one shared budget.
  struct CountryBudget
  {
    const TransferWindows::CountryRules* rules = nullptr;
    float weight = 1.0f;
    std::size_t clubs = 0;
    int visits = 0;
    int moves = 0;
  };
  std::vector<CountryBudget> budgets;
  std::vector<LeagueID> leagues(clubs.size(), LeagueID{0});
  constexpr std::size_t SHUT = std::numeric_limits<std::size_t>::max();
  std::vector<std::size_t> budget_of(clubs.size(), SHUT);
  std::size_t shut = 0;
  for (std::size_t index = 0; index < clubs.size(); ++index)
  {
    if (const auto team = std::as_const(*gamedata).getTeam(clubs[index]))
      leagues[index] = team->get().getLeagueId();
    const TransferNegotiation::WindowInfo window =
        TransferNegotiation::windowInfo(leagues[index], today);
    if (!window.open)
    {
      ++shut;
      continue;
    }
    const TransferWindows::CountryRules* rules =
        &TransferWindows::rulesFor(leagues[index]);
    const auto found = std::ranges::find(budgets, rules, &CountryBudget::rules);
    budget_of[index] = static_cast<std::size_t>(found - budgets.begin());
    if (found == budgets.end())
      budgets.push_back(
          {rules, TransferNegotiation::activityWeight(window), 0, 0, 0});
    ++budgets[budget_of[index]].clubs;
  }
  for (CountryBudget& budget : budgets)
  {
    budget.visits = Market::perDay(budget.clubs, Market::DAILY_EVALUATION_SHARE,
                                   budget.weight);
    budget.moves =
        Market::perDay(budget.clubs, Market::DAILY_DEAL_SHARE, budget.weight);
  }
  // While some country trades, clubs whose window is shut but whose country
  // lets them register free agents any day (the Americas in July, right
  // after the 30 June expiries) sign them at the open-window rate; the
  // rest share the slow closed-window budget.
  const auto free_any_day = [&](std::size_t index)
  {
    return !budgets.empty() &&
           TransferWindows::rulesFor(leagues[index]).free_agent_days ==
               TransferWindows::FREE_AGENTS_ANY_TIME;
  };
  std::size_t free_clubs = 0;
  for (std::size_t index = 0; index < clubs.size(); ++index)
    if (budget_of[index] == SHUT && free_any_day(index)) ++free_clubs;
  shut -= free_clubs;
  constexpr int JULY = 7;
  const float free_weight =
      today.month == JULY ? Market::JULY_FREE_AGENT_WEIGHT : 1.0f;
  int free_visits =
      free_clubs > 0
          ? Market::perDay(free_clubs, Market::DAILY_EVALUATION_SHARE,
                           free_weight)
          : 0;
  int free_signings =
      free_clubs > 0
          ? Market::perDay(free_clubs, Market::DAILY_DEAL_SHARE, free_weight)
          : 0;
  int shut_visits =
      shut > 0 ? Market::perDay(shut, Market::DAILY_EVALUATION_SHARE) : 0;
  int shut_signings =
      shut > 0 ? Market::perDay(shut, Market::CLOSED_WINDOW_SIGNING_SHARE) : 0;

  if (!budgets.empty()) evaluateIncomingAIBids();
  for (std::size_t index = 0; index < clubs.size(); ++index)
  {
    const TeamID club = clubs[index];
    if (budget_of[index] == SHUT && free_any_day(index))
    {
      if (free_visits <= 0 || free_signings <= 0) continue;
      --free_visits;
      if (market.runAiClub(club, transfer_listings, today, managed, rng, true))
        --free_signings;
      continue;
    }
    if (budget_of[index] == SHUT)
    {
      // Out of its window a club can only register players without a club,
      // where its country allows that.
      if (shut_visits <= 0 || shut_signings <= 0) continue;
      --shut_visits;
      if (TransferWindows::canSignFreeAgent(leagues[index], today) &&
          market.runAiClub(club, transfer_listings, today, managed, rng, true))
        --shut_signings;
      continue;
    }
    CountryBudget& budget = budgets[budget_of[index]];
    if (budget.visits <= 0 || budget.moves <= 0) continue;
    --budget.visits;
    market.runAiApproach(club, today, managed, rng);
    if (market.runAiClub(club, transfer_listings, today, managed, rng, false))
      --budget.moves;
    else if (rng.chance(Market::LIST_ACTIVITY_CHANCE))
      evaluateAndActForTeam(club);
  }
}

// ========== Transfer Market: structured deals ==========

TransferNegotiation::WindowInfo GameController::getTransferWindow() const
{
  return game && hasSelectedTeam()
             ? getTransferWindowFor(game->getManagedTeamId())
             : TransferNegotiation::WindowInfo{};
}

TransferNegotiation::WindowInfo GameController::getTransferWindowFor(
    TeamID club) const
{
  if (!game) return {};
  const auto team = std::as_const(*gamedata).getTeam(club);
  if (!team || club == FREE_AGENTS_TEAM_ID)
  {
    TransferNegotiation::WindowInfo shut;
    shut.days_to_deadline = -1;
    return shut;
  }
  return TransferNegotiation::windowInfo(team->get().getLeagueId(),
                                         game->getCurrentDate());
}

TransferNegotiation::ClubResponse GameController::makeTransferOffer(
    PlayerID player_id, const TransferNegotiation::OfferTerms& terms)
{
  using TransferNegotiation::ClubResponse;
  using TransferNegotiation::Reason;
  ClubResponse refusal;
  if (!game || !hasSelectedTeam()) return refusal;
  const TeamID managed = game->getManagedTeamId();
  const auto player = gamedata->getPlayer(player_id);
  TransferMarket& market = game->getTransfers();
  if (!isTransferWindowOpen())
  {
    refusal.reasons.push_back(Reason::WindowClosed);
    return refusal;
  }
  if (game->getWorld().isTransferEmbargoed(managed))
  {
    refusal.reasons.push_back(Reason::Embargo);
    return refusal;
  }
  if (!hasSquadRoom())
  {
    refusal.reasons.push_back(Reason::SquadFull);
    return refusal;
  }
  if (!player || player->get().getTeamId() == managed ||
      player->get().getTeamId() == FREE_AGENTS_TEAM_ID ||
      !market.canBeTraded(player_id))
  {
    refusal.reasons.push_back(Reason::Unavailable);
    return refusal;
  }
  const TeamID seller = player->get().getTeamId();
  Negotiation talk;
  if (const Negotiation* existing = market.findNegotiation(player_id);
      existing && existing->seller == seller &&
      existing->kind == TransferNegotiation::ContractKind::Transfer)
    talk = *existing;
  talk.player_id = player_id;
  talk.seller = seller;
  talk.kind = TransferNegotiation::ContractKind::Transfer;

  TransferMarket::Deal probe;
  probe.player_id = player_id;
  probe.buyer_id = managed;
  probe.terms = terms;
  const int64_t signing_cash =
      static_cast<int64_t>(TransferNegotiation::upfrontAmount(terms)) +
      static_cast<int64_t>(TransferMarket::agentFee(probe));
  if (signing_cash > transferBudgetForTeam(managed))
  {
    refusal.reasons.push_back(Reason::OverBudget);
    return refusal;
  }

  const GameDateValue today = game->getCurrentDate();
  // A club does not sell a goalkeeper it needs, unless his release clause
  // is paid in full.
  const uint32_t clause = market.releaseClause(player_id);
  const bool meets_clause =
      clause > 0 && terms.fee >= clause && terms.instalment_years == 0;
  if (!meets_clause && !market.keepsAiKeepers(seller, player_id))
  {
    refusal.reasons.push_back(Reason::NotForSale);
    return refusal;
  }
  ClubResponse response = TransferNegotiation::evaluateOffer(
      market.saleContext(player_id, managed, today, listingPrice(player_id)),
      terms, talk.club_rounds);
  talk.expires =
      today + static_cast<size_t>(TransferTuning::Offer::AGREEMENT_VALID_DAYS);
  if (response.decision == ClubResponse::Decision::Accept)
  {
    talk.agreed = terms;
    talk.club_agreed = true;
  }
  else
  {
    ++talk.club_rounds;
  }
  market.setNegotiation(talk);
  game->getWorld().onTransferBid(today, player_id, managed, terms.fee, managed);
  return response;
}

std::optional<TransferNegotiation::ContractKind>
GameController::getContractTalkKind(PlayerID player_id) const
{
  if (!game || !hasSelectedTeam()) return std::nullopt;
  const auto player = gamedata->getPlayer(player_id);
  const TeamID managed = game->getManagedTeamId();
  if (!player) return std::nullopt;
  if (player->get().getTeamId() == managed)
  {
    // His own club renews; talks that broke down reopen after a few days.
    const PlayerActionBlock block = getRenewalBlock(player_id);
    if (block == PlayerActionBlock::None ||
        block == PlayerActionBlock::TalksEnded)
      return TransferNegotiation::ContractKind::Renewal;
    return std::nullopt;
  }
  const TransferMarket& market = game->getTransfers();
  if (player->get().getTeamId() == FREE_AGENTS_TEAM_ID)
  {
    if (!canSignFreeAgentFor(managed)) return std::nullopt;
    return TransferNegotiation::ContractKind::FreeAgent;
  }
  if (const Negotiation* talk = market.findNegotiation(player_id);
      talk && talk->club_agreed && isTransferWindowOpen() &&
      talk->seller == player->get().getTeamId())
    return TransferNegotiation::ContractKind::Transfer;
  const LoanDeal* loan = market.findLoan(player_id);
  if (!market.findPreContract(player_id) &&
      (!loan || loan->parent != managed) &&
      TransferNegotiation::canSignPreContract(player->get().getContractYears(),
                                              game->getCurrentDate()))
    return TransferNegotiation::ContractKind::PreContract;
  return std::nullopt;
}

TransferNegotiation::ContractDemand GameController::getPlayerDemand(
    PlayerID player_id, TransferNegotiation::ContractKind kind) const
{
  if (!game) return {};
  const TransferMarket& market = game->getTransfers();
  const auto context =
      market.playerContext(player_id, game->getManagedTeamId(), kind);
  TransferNegotiation::ContractDemand demand =
      TransferNegotiation::contractDemand(context);
  // The agent's ask softens with every proposal already turned down.
  if (const Negotiation* talk = market.findNegotiation(player_id))
    demand.asking_wage =
        TransferNegotiation::agentAsk(context, demand, talk->player_rounds);
  return demand;
}

std::uint8_t GameController::getContractRoundsLeft(PlayerID player_id) const
{
  constexpr std::uint8_t MAX_ROUNDS =
      TransferTuning::Negotiation::MAX_PLAYER_ROUNDS;
  if (!game) return 0;
  const Negotiation* talk = game->getTransfers().findNegotiation(player_id);
  const std::uint8_t used = talk ? talk->player_rounds : 0;
  return used >= MAX_ROUNDS ? 0 : static_cast<std::uint8_t>(MAX_ROUNDS - used);
}

GameController::ContractTalkResult GameController::proposeContract(
    PlayerID player_id, const TransferNegotiation::ContractOffer& offer)
{
  using TransferNegotiation::ContractKind;
  ContractTalkResult result;
  const auto kind = getContractTalkKind(player_id);
  if (!kind) return result;
  const TeamID managed = game->getManagedTeamId();
  const GameDateValue today = game->getCurrentDate();
  TransferMarket& market = game->getTransfers();
  const auto player = gamedata->getPlayer(player_id);
  const bool renewal = *kind == ContractKind::Renewal;

  Negotiation talk;
  if (const Negotiation* existing = market.findNegotiation(player_id))
    talk = *existing;
  talk.player_id = player_id;
  talk.seller = player->get().getTeamId();
  if (*kind != ContractKind::Transfer) talk.kind = *kind;
  if (getContractRoundsLeft(player_id) == 0)
  {
    result.block = PlayerActionBlock::TalksEnded;
    result.response.reasons.push_back(TransferNegotiation::Reason::TalksEnded);
    return result;
  }
  // A new signing needs room in the squad.
  if (!renewal && !hasSquadRoom())
  {
    result.block = PlayerActionBlock::SquadFull;
    result.response.reasons.push_back(TransferNegotiation::Reason::SquadFull);
    result.rounds_left = getContractRoundsLeft(player_id);
    return result;
  }
  // A renewal must run longer than the contract he has, within the rules.
  if (renewal && offer.years <= player->get().getContractYears())
  {
    result.block = PlayerActionBlock::NotLonger;
    result.rounds_left = getContractRoundsLeft(player_id);
    return result;
  }
  if (renewal && offer.years > TransferNegotiation::maxContractYears(
                                   player->get().getAge()))
  {
    result.block = PlayerActionBlock::TooLong;
    result.rounds_left = getContractRoundsLeft(player_id);
    return result;
  }

  const std::uint32_t fee =
      *kind == ContractKind::Transfer ? talk.agreed.fee : 0;
  const PlayerAgent::Reply reply =
      PlayerAgent::respond(market.playerContext(player_id, managed, *kind),
                           offer, talk.player_rounds, fee);
  result.response = reply.response;
  result.agent_line = reply.line_key;
  if (!result.response.accepted)
  {
    ++talk.player_rounds;
    talk.expires = std::max(
        talk.expires, today + static_cast<size_t>(
                                  TransferTuning::Offer::AGREEMENT_VALID_DAYS));
    market.setNegotiation(talk);
    result.rounds_left = getContractRoundsLeft(player_id);
    return result;
  }

  if (renewal)
  {
    // The new wage replaces his current one on the payroll; bonus and
    // agent fee are paid in cash today.
    const auto team = gamedata->getTeam(managed);
    const Finances& finances = team->get().getFinances();
    const int64_t payroll = getWeeklyWageBill(managed) -
                            static_cast<int64_t>(player->get().getWage()) +
                            static_cast<int64_t>(offer.weekly_wage);
    const int64_t cash = static_cast<int64_t>(offer.signing_bonus) +
                         static_cast<int64_t>(offer.agent_fee.value_or(
                             PlayerAgent::standardAgentFee(
                                 ContractKind::Renewal, 0, offer.weekly_wage)));
    if (payroll > finances.getWageBudget() || cash > finances.getBalance())
    {
      result.over_budget = true;
      result.rounds_left = getContractRoundsLeft(player_id);
      return result;
    }
    result.completed = market.renewContract(player_id, offer, today);
    if (result.completed) market.removeNegotiation(player_id);
    return result;
  }

  TransferMarket::Deal deal;
  deal.player_id = player_id;
  deal.buyer_id = managed;
  deal.contract = offer;
  deal.kind = *kind == ContractKind::Transfer ? TransferKind::Permanent
                                              : TransferKind::Free;
  if (*kind == ContractKind::Transfer)
  {
    deal.terms = talk.agreed;
    // A release clause paid in full cannot be refused by his club.
    const uint32_t clause = market.releaseClause(player_id);
    deal.binding = clause > 0 && talk.agreed.fee >= clause &&
                   talk.agreed.instalment_years == 0;
  }
  const bool affordable = *kind == ContractKind::PreContract
                              ? canPayPreContract(deal)
                              : canPayDeal(deal);
  if (!affordable)
  {
    result.over_budget = true;
    result.rounds_left = getContractRoundsLeft(player_id);
    return result;
  }
  result.completed =
      *kind == ContractKind::PreContract
          ? market.agreePreContract(player_id, managed, offer, today, managed)
          : market.completeTransfer(deal, today, managed);
  if (result.completed)
  {
    market.removeNegotiation(player_id);
    purgeStaleListings();
  }
  return result;
}

bool GameController::canPayPreContract(const TransferMarket::Deal& deal) const
{
  // He joins on 1 July: his wage must fit next season's wage room (with
  // the club's other agreed pre-contracts), and the bonus and agent fee due
  // then come out of the transfer money not already committed.
  const TransferMarket& market = game->getTransfers();
  const int64_t costs = static_cast<int64_t>(deal.contract.signing_bonus) +
                        static_cast<int64_t>(TransferMarket::agentFee(deal));
  return !game->getWorld().isTransferEmbargoed(deal.buyer_id) &&
         static_cast<int64_t>(deal.contract.weekly_wage) <=
             market.nextSeasonWageRoom(deal.buyer_id) &&
         costs <= static_cast<int64_t>(transferBudgetForTeam(deal.buyer_id));
}

TransferNegotiation::ClubResponse GameController::makeLoanOffer(
    PlayerID player_id, const TransferNegotiation::LoanTerms& terms)
{
  using TransferNegotiation::ClubResponse;
  using TransferNegotiation::Reason;
  ClubResponse response;
  if (!game || !hasSelectedTeam()) return response;
  if (!isTransferWindowOpen())
  {
    response.reasons.push_back(Reason::WindowClosed);
    return response;
  }
  const TeamID managed = game->getManagedTeamId();
  if (game->getWorld().isTransferEmbargoed(managed))
  {
    response.reasons.push_back(Reason::Embargo);
    return response;
  }
  if (!hasSquadRoom())
  {
    response.reasons.push_back(Reason::SquadFull);
    return response;
  }
  const auto player = gamedata->getPlayer(player_id);
  TransferMarket& market = game->getTransfers();
  if (!player || player->get().getTeamId() == managed ||
      player->get().getTeamId() == FREE_AGENTS_TEAM_ID ||
      !market.canBeTraded(player_id))
  {
    response.reasons.push_back(Reason::Unavailable);
    return response;
  }
  const Player& loanee = player->get();
  const TeamID parent = loanee.getTeamId();
  const GameDateValue today = game->getCurrentDate();
  loanee.updateMarketValue(gamedata->getStatsConfig());
  TransferNegotiation::LoanContext context;
  context.market_value = loanee.getMarketValue();
  context.weekly_wage = loanee.getWage();
  context.age = loanee.getAge();
  context.role = game->getWorld().squadRole(player_id);
  context.loan_listed = market.isLoanListed(player_id);
  context.weeks = TransferNegotiation::weeksBetween(
      today, TransferNegotiation::loanEndDate(today, terms.duration));
  context.within_limits = market.loanWithinLimits(player_id, parent, managed);
  response = TransferNegotiation::evaluateLoan(context, terms);
  if (response.decision != ClubResponse::Decision::Accept) return response;

  const auto parent_team = gamedata->getTeam(parent);
  const auto managed_team = getManagedTeam();
  const int gap =
      parent_team->get().getReputation() - managed_team->get().getReputation();
  if (!TransferNegotiation::playerAcceptsLoan(
          context.role, market.projectedRole(player_id, managed),
          loanee.getTraits().ambition, gap, response.reasons))
  {
    response.decision = ClubResponse::Decision::Reject;
    return response;
  }
  const int64_t borrower_wage =
      static_cast<int64_t>(loanee.getWage()) * terms.wage_share / 100;
  if (static_cast<int64_t>(terms.loan_fee) > transferBudgetForTeam(managed) ||
      getWeeklyWageBill(managed) + borrower_wage >
          managed_team->get().getFinances().getWageBudget())
  {
    response.decision = ClubResponse::Decision::Reject;
    response.reasons.push_back(Reason::OverBudget);
    return response;
  }
  if (!market.startLoan(player_id, managed, terms, today, managed))
  {
    response.decision = ClubResponse::Decision::Reject;
    response.reasons.push_back(Reason::LoanLimit);
    return response;
  }
  purgeStaleListings();
  return response;
}

bool GameController::setLoanListed(PlayerID player_id, bool listed)
{
  if (!game || !hasSelectedTeam()) return false;
  const auto player = gamedata->getPlayer(player_id);
  TransferMarket& market = game->getTransfers();
  if (!player || player->get().getTeamId() != game->getManagedTeamId() ||
      !market.canBeTraded(player_id))
    return false;
  market.setLoanListed(player_id, listed);
  return true;
}

bool GameController::canRecallLoan(PlayerID player_id) const
{
  if (!game || !isTransferWindowOpen()) return false;
  const LoanDeal* loan = game->getTransfers().findLoan(player_id);
  // Not at once: the borrower paid for a spell with him.
  return loan && loan->parent == game->getManagedTeamId() &&
         loan->recall_clause &&
         dayOrdinal(game->getCurrentDate()) - dayOrdinal(loan->start) >=
             TransferTuning::Loan::RECALL_MIN_DAYS;
}

bool GameController::recallLoan(PlayerID player_id)
{
  if (!canRecallLoan(player_id)) return false;
  return game->getTransfers().endLoan(player_id, game->getCurrentDate(),
                                      game->getManagedTeamId(), true);
}

bool GameController::hasSquadRoom() const
{
  return game &&
         game->getTransfers().seniorSquadSize(game->getManagedTeamId()) <
             TransferTuning::Market::MANAGED_MAX_SQUAD;
}

bool GameController::exerciseLoanOption(PlayerID player_id)
{
  if (!game) return false;
  TransferMarket& market = game->getTransfers();
  const LoanDeal* loan = market.findLoan(player_id);
  const TeamID managed = game->getManagedTeamId();
  if (!loan || loan->borrower != managed || loan->option_fee == 0) return false;
  TransferMarket::Deal probe;
  probe.player_id = player_id;
  probe.buyer_id = managed;
  probe.terms = TransferNegotiation::aiOfferTerms(loan->option_fee);
  const int64_t signing_cash =
      static_cast<int64_t>(TransferNegotiation::upfrontAmount(probe.terms)) +
      static_cast<int64_t>(TransferMarket::agentFee(probe));
  if (signing_cash > transferBudgetForTeam(managed)) return false;
  // His full wage replaces the share the club pays now.
  const auto player = gamedata->getPlayer(player_id);
  const auto club = gamedata->getTeam(managed);
  if (!player || !club ||
      getWeeklyWageBill(managed) -
              static_cast<int64_t>(player->get().getWage()) +
              static_cast<int64_t>(loan->full_wage) >
          club->get().getFinances().getWageBudget())
    return false;
  return market.exerciseLoanOption(player_id, game->getCurrentDate(), managed);
}

const std::vector<IncomingOffer>& GameController::getIncomingOffers() const
{
  static const std::vector<IncomingOffer> EMPTY;
  return game ? game->getTransfers().incomingOffers() : EMPTY;
}

int64_t GameController::getReleaseCost(PlayerID player_id) const
{
  if (!game) return 0;
  const auto player = gamedata->getPlayer(player_id);
  if (!player || player->get().getTeamId() != game->getManagedTeamId())
    return 0;
  return TransferNegotiation::severancePay(player->get().getWage(),
                                           player->get().getContractYears(),
                                           game->getCurrentDate());
}

bool GameController::releasePlayer(PlayerID player_id)
{
  if (!game || !hasSelectedTeam() ||
      getReleaseBlock(player_id) != PlayerActionBlock::None)
    return false;
  const auto player = gamedata->getPlayer(player_id);
  const TeamID managed = game->getManagedTeamId();
  if (!player || player->get().getTeamId() != managed) return false;
  const bool released = game->getTransfers().releasePlayer(
      player_id, game->getCurrentDate(), managed);
  if (released) purgeStaleListings();
  return released;
}

// ========== World simulation ==========

const std::vector<InboxMessage>& GameController::getInbox() const
{
  static const std::vector<InboxMessage> EMPTY;
  return game ? game->getWorld().getInbox().getMessages() : EMPTY;
}

bool GameController::markInboxMessageRead(uint32_t message_id)
{
  return game && game->getWorld().getInbox().markRead(message_id);
}

void GameController::markAllInboxMessagesRead()
{
  if (game) game->getWorld().getInbox().markAllRead();
}

size_t GameController::getUnreadInboxCount() const
{
  return game ? game->getWorld().getInbox().unreadCount() : 0;
}

bool GameController::isTransferEmbargoed() const
{
  return game && game->getWorld().isTransferEmbargoed(game->getManagedTeamId());
}

const BoardState& GameController::getBoardState() const
{
  static const BoardState EMPTY;
  return game ? game->getWorld().getBoardState() : EMPTY;
}

const std::vector<FinanceTransaction>& GameController::getFinanceLedger(
    TeamID team_id) const
{
  static const std::vector<FinanceTransaction> EMPTY;
  if (!gamedata) return EMPTY;
  const auto team = gamedata->getTeam(team_id);
  return team ? team->get().getFinances().getLedger() : EMPTY;
}

FinanceSummary GameController::getFinanceSummary(TeamID team_id,
                                                 GameDateValue from,
                                                 GameDateValue to) const
{
  if (!gamedata) return {};
  const auto team = gamedata->getTeam(team_id);
  return team ? team->get().getFinances().summarize(from, to)
              : FinanceSummary{};
}

int64_t GameController::getWeeklyWageBill(TeamID team_id) const
{
  if (!gamedata) return 0;
  const auto team = gamedata->getTeam(team_id);
  return team ? team->get().getFinances().getCurrentWageSpending(*gamedata,
                                                                 team->get())
              : 0;
}

bool GameController::moveTransferToWageBudget(int64_t transfer_amount)
{
  const auto team = getManagedTeam();
  if (!team) return false;
  return team->get().getFinances().moveTransferToWageBudget(
      transfer_amount, getWeeklyWageBill(team->get().getId()));
}

bool GameController::setTicketPrice(uint32_t price)
{
  constexpr uint32_t MAX_TICKET_PRICE = 1000;
  const auto team = getManagedTeam();
  if (!team || price == 0 || price > MAX_TICKET_PRICE) return false;
  ClubProfile profile = team->get().getProfile();
  profile.ticket_price = price;
  team->get().setProfile(profile);
  return true;
}

uint32_t GameController::getFairTicketPrice(TeamID team_id) const
{
  if (!gamedata) return 0;
  const auto team = gamedata->getTeam(team_id);
  if (!team) return 0;
  const auto economies = buildLeagueEconomies(*gamedata);
  const auto economy = economies.find(team->get().getLeagueId());
  if (economy == economies.end()) return 0;
  return static_cast<uint32_t>(std::lround(
      ClubEconomy::fairTicketPrice(economy->second, team->get().getProfile())));
}

uint32_t GameController::getLastHomeAttendance(TeamID team_id) const
{
  return game ? game->getWorld().lastHomeAttendance(team_id) : 0;
}

bool GameController::isPlayerAvailable(PlayerID player_id) const
{
  if (!gamedata) return false;
  const auto player = gamedata->getPlayer(player_id);
  return player && player->get().isAvailable();
}

std::vector<PlayerID> GameController::getInjuredPlayers(TeamID team_id) const
{
  std::vector<PlayerID> injured;
  if (!gamedata) return injured;
  for (const auto& player : gamedata->getPlayersForTeam(team_id))
  {
    if (!player.get().isAvailable()) injured.push_back(player.get().getId());
  }
  return injured;
}

std::vector<PlayerID> GameController::getUnavailableLineupPlayers(
    TeamID team_id) const
{
  std::vector<PlayerID> unavailable;
  if (!gamedata) return unavailable;
  const auto team = gamedata->getTeam(team_id);
  if (!team) return unavailable;
  const Lineup& lineup = team->get().getLineup();
  const auto check = [&](const Player* player)
  {
    if (player && !player->isAvailable())
      unavailable.push_back(player->getId());
  };
  check(lineup.getGoalkeeper());
  for (const auto& positioned : lineup.getOutfieldPlayers())
    check(positioned.player);
  for (const Player* reserve : lineup.getReserves()) check(reserve);
  return unavailable;
}

SquadRole GameController::getSquadRole(PlayerID player_id) const
{
  return game ? game->getWorld().squadRole(player_id) : SquadRole::Fringe;
}

GameController::PotentialEstimate GameController::getPotentialEstimate(
    PlayerID player_id) const
{
  const auto row = getScoutedRow(player_id);
  if (!row) return {};
  return {row->potential_low, row->potential_high};
}

bool GameController::applyMatchConsequences(PlayerID player_id,
                                            uint8_t minutes_played,
                                            float end_condition, bool injured)
{
  return game && game->getWorld().applyMatchConsequences(
                     game->getCurrentDate(),
                     {player_id, minutes_played, end_condition, injured},
                     game->getManagedTeamId());
}

bool GameController::renewContract(PlayerID player_id, ContractTerms terms)
{
  if (getContractTalkKind(player_id) !=
      TransferNegotiation::ContractKind::Renewal)
    return false;
  TransferNegotiation::ContractOffer offer;
  offer.weekly_wage = terms.weekly_wage;
  offer.years = terms.years;
  return offer.weekly_wage > 0 && proposeContract(player_id, offer).completed;
}

const char* GameController::playerActionBlockKey(PlayerActionBlock block)
{
  switch (block)
  {
    case PlayerActionBlock::None:
      return "";
    case PlayerActionBlock::NotYours:
      return "BLOCK_NOT_YOURS";
    case PlayerActionBlock::WindowClosed:
      return "BLOCK_WINDOW_CLOSED";
    case PlayerActionBlock::OnLoan:
      return "BLOCK_ON_LOAN";
    case PlayerActionBlock::Leaving:
      return "BLOCK_LEAVING";
    case PlayerActionBlock::SquadFloor:
      return "BLOCK_SQUAD_FLOOR";
    case PlayerActionBlock::LastGoalkeeper:
      return "BLOCK_LAST_GOALKEEPER";
    case PlayerActionBlock::MaxLength:
      return "BLOCK_MAX_LENGTH";
    case PlayerActionBlock::NotLonger:
      return "BLOCK_NOT_LONGER";
    case PlayerActionBlock::TooLong:
      return "BLOCK_TOO_LONG";
    case PlayerActionBlock::TalksEnded:
      return "BLOCK_TALKS_ENDED";
    case PlayerActionBlock::SquadFull:
      return "BLOCK_SQUAD_FULL";
  }
  return "";
}

namespace
{
/** Shared part of the blocks: a player of the managed club who is neither
 * borrowed nor committed to another club. */
GameController::PlayerActionBlock ownershipBlock(const GameData& data,
                                                 const Game& game,
                                                 PlayerID player_id)
{
  using Block = GameController::PlayerActionBlock;
  const auto player = data.getPlayer(player_id);
  if (!player || player->get().getTeamId() != game.getManagedTeamId())
    return Block::NotYours;
  const TransferMarket& market = game.getTransfers();
  if (const LoanDeal* loan = market.findLoan(player_id);
      loan && loan->borrower == game.getManagedTeamId())
    return Block::OnLoan;
  if (market.findPreContract(player_id)) return Block::Leaving;
  return Block::None;
}
}  // namespace

GameController::PlayerActionBlock GameController::getReleaseBlock(
    PlayerID player_id) const
{
  if (!game) return PlayerActionBlock::NotYours;
  if (const PlayerActionBlock block =
          ownershipBlock(*gamedata, *game, player_id);
      block != PlayerActionBlock::None)
    return block;
  bool goalkeeper = false;
  if (!game->getTransfers().keepsSquadFloor(game->getManagedTeamId(), player_id,
                                            &goalkeeper))
    return goalkeeper ? PlayerActionBlock::LastGoalkeeper
                      : PlayerActionBlock::SquadFloor;
  return PlayerActionBlock::None;
}

GameController::PlayerActionBlock GameController::getSaleBlock(
    PlayerID player_id) const
{
  if (!game) return PlayerActionBlock::NotYours;
  // A club abroad may still buy while its own window is open.
  if (!isTransferWindowOpenAnywhere()) return PlayerActionBlock::WindowClosed;
  return getReleaseBlock(player_id);
}

GameController::PlayerActionBlock GameController::getListingBlock(
    PlayerID player_id) const
{
  if (!game) return PlayerActionBlock::NotYours;
  if (const PlayerActionBlock block =
          ownershipBlock(*gamedata, *game, player_id);
      block != PlayerActionBlock::None)
    return block;
  if (!isTransferWindowOpenAnywhere()) return PlayerActionBlock::WindowClosed;
  return PlayerActionBlock::None;
}

GameController::PlayerActionBlock GameController::getRenewalBlock(
    PlayerID player_id) const
{
  if (!game) return PlayerActionBlock::NotYours;
  if (const PlayerActionBlock block =
          ownershipBlock(*gamedata, *game, player_id);
      block != PlayerActionBlock::None)
    return block;
  const Player& player = gamedata->getPlayer(player_id)->get();
  // Academy contracts are handled by the academy.
  if (player.isAcademyPlayer()) return PlayerActionBlock::NotYours;
  if (player.getContractYears() >=
      TransferNegotiation::maxContractYears(player.getAge()))
    return PlayerActionBlock::MaxLength;
  if (getContractRoundsLeft(player_id) == 0)
    return PlayerActionBlock::TalksEnded;
  return PlayerActionBlock::None;
}

// ========== Human side: conversations, team talks, dressing room ==========

float GameController::getManagerStanding() const
{
  const auto team = managedClub();
  if (!game || !team) return 50.0f;
  // Board confidence carries most weight; a big club lends some stature.
  return std::clamp(0.6f * game->getWorld().getBoardState().confidence +
                        0.4f * static_cast<float>(team->get().getReputation()),
                    0.0f, 100.0f);
}

std::vector<TalkOptionView> GameController::getTalkOptions(
    PlayerID player_id) const
{
  if (!game) return {};
  return game->getWorld().getInteractions().options(
      player_id, game->getCurrentDate(), game->getManagedTeamId(),
      getManagerStanding());
}

std::optional<TalkOutcome> GameController::talkToPlayer(PlayerID player_id,
                                                        TalkOption option)
{
  if (!game || !hasSelectedTeam()) return std::nullopt;
  WorldSimulation& world = game->getWorld();
  auto outcome = world.getInteractions().talk(
      player_id, option, game->getCurrentDate(), game->getManagedTeamId(),
      getManagerStanding());
  if (!outcome) return std::nullopt;
  world.getStories().resolveChoice(player_id);
  if (outcome->list_player && !isPlayerListed(player_id))
    listPlayerForTransfer(player_id, getPlayerMarketValue(player_id));
  return outcome;
}

const PlayerRelation* GameController::getPlayerRelation(
    PlayerID player_id) const
{
  return game ? game->getWorld().getInteractions().relation(player_id)
              : nullptr;
}

std::vector<Promise> GameController::getPlayerPromises(PlayerID player_id) const
{
  if (!game) return {};
  return game->getWorld().getInteractions().promisesFor(player_id);
}

std::vector<Promise> GameController::getActivePromises() const
{
  if (!game) return {};
  std::vector<Promise> active;
  for (const Promise& promise : game->getWorld().getInteractions().promises())
    if (promise.state == PromiseState::Active) active.push_back(promise);
  std::ranges::sort(active,
                    [](const Promise& a, const Promise& b)
                    {
                      return a.deadline_day < b.deadline_day ||
                             (a.deadline_day == b.deadline_day && a.id < b.id);
                    });
  return active;
}

std::optional<StoryChoice> GameController::getStoryChoice(
    PlayerID player_id) const
{
  if (!game) return std::nullopt;
  return game->getWorld().getStories().choiceFor(
      player_id, dayOrdinal(game->getCurrentDate()));
}

bool GameController::hasPendingTalk(PlayerID player_id) const
{
  const PlayerRelation* relation = getPlayerRelation(player_id);
  return (relation != nullptr && relation->request != TalkRequest::None) ||
         getStoryChoice(player_id).has_value();
}

TeamTalkContext GameController::getTeamTalkContext(TeamTalkMoment moment,
                                                   int own_goals,
                                                   int other_goals) const
{
  if (!game) return {};
  const WorldSimulation& world = game->getWorld();
  const TeamID managed = game->getManagedTeamId();
  const GameDateValue date = game->getCurrentDate();
  TeamTalkContext context =
      world.getInteractions().teamTalkContext(managed, moment, date);
  context.goal_difference = own_goals - other_goals;
  for (const Match& match : game->getCalendar().getMatchesForDate(date))
  {
    const bool home = match.getHomeTeamId() == managed;
    if (!home && match.getAwayTeamId() != managed) continue;
    const TeamID opponent =
        home ? match.getAwayTeamId() : match.getHomeTeamId();
    context.expected_points = BoardModel::expectedPoints(
        world.lineupStrength(managed), world.lineupStrength(opponent), home);
    context.derby = world.getStories().rivalOf(managed) == opponent;
    context.cup = match.getMatchType() == MatchType::CUP;
    if (context.cup)
    {
      const auto cup = getCupStatus(match.getCompetitionId());
      context.final =
          cup && cup->total_rounds > 0 && match.getStage() == cup->total_rounds;
    }
    break;
  }
  return context;
}

bool GameController::canGiveTeamTalk(TeamTalkMoment moment) const
{
  return game && hasSelectedTeam() &&
         game->getWorld().getInteractions().canGiveTeamTalk(
             game->getManagedTeamId(), moment, game->getCurrentDate());
}

std::optional<TeamTalkResult> GameController::giveTeamTalk(
    TeamTalkMoment moment, TeamTalkTone tone, int own_goals, int other_goals)
{
  if (!canGiveTeamTalk(moment)) return std::nullopt;
  return game->getWorld().getInteractions().giveTeamTalk(
      getTeamTalkContext(moment, own_goals, other_goals),
      game->getManagedTeamId(), tone);
}

float GameController::getTeamTalkModifier(TeamID team_id, int half) const
{
  if (!game) return 0.0f;
  return game->getWorld().getInteractions().teamTalkModifier(
      team_id, game->getCurrentDate(), half);
}

float GameController::getHumanFactorModifier(TeamID team_id, int half) const
{
  if (!game) return 0.0f;
  const float cohesion =
      team_id == game->getManagedTeamId()
          ? game->getWorld().getInteractions().cohesionModifier(
                team_id, game->getCurrentDate())
          : 0.0f;
  return std::clamp(getTeamTalkModifier(team_id, half) + cohesion,
                    -Interactions::TOTAL_CAP, Interactions::TOTAL_CAP);
}

DressingRoom GameController::getDressingRoom() const
{
  if (!game || !hasSelectedTeam()) return {};
  return game->getWorld().getInteractions().dressingRoom(
      game->getManagedTeamId(), game->getCurrentDate());
}

std::optional<ScoutedPlayerView> GameController::getScoutedView(
    PlayerID player_id) const
{
  if (!game) return std::nullopt;
  return game->getWorld().getScouting().view(player_id);
}

std::optional<ScoutedPlayerRow> GameController::getScoutedRow(
    PlayerID player_id) const
{
  if (!game) return std::nullopt;
  return game->getWorld().getScouting().row(player_id);
}

std::vector<ScoutedPlayerRow> GameController::searchScoutedPlayers(
    const ScoutSearchFilter& filter) const
{
  if (!game) return {};
  return game->getWorld().getScouting().search(filter);
}

float GameController::getScoutingKnowledge(PlayerID player_id) const
{
  return game ? game->getWorld().getScouting().knowledgeOf(player_id) : 0.0f;
}

const std::vector<ScoutProfile>& GameController::getScouts() const
{
  static const std::vector<ScoutProfile> EMPTY;
  return game ? game->getWorld().getScouting().getScouts() : EMPTY;
}

const std::vector<ScoutAssignment>& GameController::getScoutAssignments() const
{
  static const std::vector<ScoutAssignment> EMPTY;
  return game ? game->getWorld().getScouting().getAssignments() : EMPTY;
}

int64_t GameController::getScoutAssignmentCost(ScoutTargetKind kind,
                                               uint32_t target_id,
                                               uint16_t days) const
{
  return game ? game->getWorld().getScouting().assignmentCost(kind, target_id,
                                                              days)
              : 0;
}

ScoutAssignError GameController::startScoutAssignment(uint32_t scout_id,
                                                      ScoutTargetKind kind,
                                                      uint32_t target_id,
                                                      uint16_t days)
{
  if (!game) return ScoutAssignError::NoTeam;
  return game->getWorld().getScouting().startAssignment(
      game->getCurrentDate(), scout_id, kind, target_id, days);
}

bool GameController::cancelScoutAssignment(uint32_t assignment_id)
{
  if (!game || !game->getWorld().getScouting().cancelAssignment(assignment_id))
    return false;
  reclaimDuty(Duty::ScoutingAssignments);
  return true;
}

const std::vector<ScoutReport>& GameController::getScoutReports() const
{
  static const std::vector<ScoutReport> EMPTY;
  return game ? game->getWorld().getScouting().getReports() : EMPTY;
}

std::vector<ScoutSummary> GameController::getScoutSummaries() const
{
  if (!game) return {};
  return game->getWorld().getScouting().scoutSummaries();
}

ScoutExpertise GameController::getScoutExpertise(uint32_t scout_id) const
{
  return game ? game->getWorld().getScouting().expertiseOf(scout_id)
              : ScoutExpertise{};
}

ScoutEffectiveness GameController::getScoutEffectiveness(
    uint32_t scout_id, ScoutTargetKind kind, uint32_t target_id) const
{
  return game ? game->getWorld().getScouting().effectiveness(scout_id, kind,
                                                             target_id)
              : ScoutEffectiveness{};
}

size_t GameController::getUnreadScoutReportCount() const
{
  return game ? game->getWorld().getScouting().unreadReports() : 0;
}

bool GameController::markScoutReportsSeen(uint32_t scout_id)
{
  return game && game->getWorld().getScouting().markReportsSeen(scout_id);
}

const std::vector<RecruitmentFocus>& GameController::getRecruitmentFocuses()
    const
{
  static const std::vector<RecruitmentFocus> EMPTY;
  return game ? game->getWorld().getScouting().getFocuses() : EMPTY;
}

uint32_t GameController::saveRecruitmentFocus(const RecruitmentFocus& focus)
{
  return game ? game->getWorld().getScouting().upsertFocus(focus) : 0;
}

bool GameController::removeRecruitmentFocus(uint32_t focus_id)
{
  return game && game->getWorld().getScouting().removeFocus(focus_id);
}

const std::vector<ShortlistEntry>& GameController::getShortlist() const
{
  static const std::vector<ShortlistEntry> EMPTY;
  return game ? game->getWorld().getScouting().getShortlist() : EMPTY;
}

bool GameController::isShortlisted(PlayerID player_id) const
{
  return game && game->getWorld().getScouting().isShortlisted(player_id);
}

bool GameController::addToShortlist(PlayerID player_id)
{
  return game && game->getWorld().getScouting().addToShortlist(
                     game->getCurrentDate(), player_id);
}

bool GameController::removeFromShortlist(PlayerID player_id)
{
  return game && game->getWorld().getScouting().removeFromShortlist(player_id);
}

std::vector<SquadComparisonRow> GameController::compareShortlistWithSquad()
    const
{
  if (!game) return {};
  return game->getWorld().getScouting().compareWithSquad();
}

uint64_t GameController::getWorldSeed() const
{
  return gamedata ? gamedata->getWorldSeed() : 0;
}

// ========== Manager career ==========

bool GameController::hasCareer() const
{
  return game && game->getCareer().hasProfile();
}

bool GameController::isUnemployed() const
{
  return hasCareer() && !hasSelectedTeam();
}

const ManagerProfile* GameController::getManagerProfile() const
{
  return hasCareer() ? &game->getCareer().getProfile() : nullptr;
}

void GameController::createManager(const ManagerSetup& setup)
{
  if (game) game->getCareer().createProfile(setup, game->getCurrentDate());
}

const std::vector<ManagerStint>& GameController::getManagerStints() const
{
  static const std::vector<ManagerStint> none;
  return game ? game->getCareer().getStints() : none;
}

const std::vector<ManagerSeasonLine>& GameController::getManagerSeasons() const
{
  static const std::vector<ManagerSeasonLine> none;
  return game ? game->getCareer().getSeasons() : none;
}

const std::vector<ManagerAward>& GameController::getManagerAwards() const
{
  static const std::vector<ManagerAward> none;
  return game ? game->getCareer().getAwards() : none;
}

const AiManager* GameController::getClubManager(TeamID team_id) const
{
  return game ? game->getCareer().clubManager(team_id) : nullptr;
}

std::vector<GameController::VacancyView> GameController::getVacancies() const
{
  std::vector<VacancyView> views;
  if (!game) return views;
  const ManagerCareer& career = game->getCareer();
  for (const Vacancy& vacancy : career.getVacancies())
  {
    const auto team = gamedata->getTeam(vacancy.team_id);
    if (!team) continue;
    VacancyView view;
    view.team_id = vacancy.team_id;
    view.league_id = team->get().getLeagueId();
    view.tier = Competitions::leagueTier(*gamedata, view.league_id);
    view.reputation = team->get().getReputation();
    view.owner = career.visionOf(vacancy.team_id).owner;
    view.chance = career.applicationChance(vacancy.team_id);
    view.required_licence =
        ManagerMarketModel::requiredLicence(view.reputation, view.tier);
    view.opened = vacancy.opened;
    // The board's expectation follows the wage-bill rank, as for the
    // managed club's objective.
    const int64_t own_wages = team->get().getFinances().getCurrentWageSpending(
        *gamedata, team->get());
    int league_size = 0;
    int richer = 0;
    const auto league = gamedata->getLeague(view.league_id);
    for (const TeamID other_id :
         league ? league->get().getTeamIDs() : std::vector<TeamID>{})
    {
      const auto other = gamedata->getTeam(other_id);
      if (!other) continue;
      ++league_size;
      if (other_id != vacancy.team_id &&
          other->get().getFinances().getCurrentWageSpending(
              *gamedata, other->get()) > own_wages)
        ++richer;
    }
    view.expected_position = richer + 1;
    view.objective_key = BoardModel::objectiveKey(
        BoardModel::objectiveFor(view.expected_position, league_size));
    if (const JobApplication* application =
            career.findApplication(vacancy.team_id))
      view.stage = application->stage;
    views.push_back(view);
  }
  std::ranges::sort(views,
                    [](const VacancyView& a, const VacancyView& b)
                    {
                      return a.chance != b.chance ? a.chance > b.chance
                                                  : a.team_id < b.team_id;
                    });
  return views;
}

const std::vector<JobApplication>& GameController::getJobApplications() const
{
  static const std::vector<JobApplication> none;
  return game ? game->getCareer().getApplications() : none;
}

ApplyResult GameController::applyForJob(TeamID team_id)
{
  if (!game) return ApplyResult::NoProfile;
  return game->getCareer().apply(team_id, game->getCurrentDate());
}

std::optional<InterviewResult> GameController::attendInterview(
    TeamID team_id, std::span<const std::uint8_t> answers)
{
  if (!game) return std::nullopt;
  return game->getCareer().interview(team_id, answers, game->getCurrentDate(),
                                     game->getWorld().getInbox());
}

const std::vector<JobOffer>& GameController::getJobOffers() const
{
  static const std::vector<JobOffer> none;
  return game ? game->getCareer().getOffers() : none;
}

OfferReply GameController::negotiateJobOffer(std::uint32_t offer_id,
                                             int64_t weekly_wage,
                                             std::uint8_t years)
{
  if (!game) return OfferReply::Withdrawn;
  return game->getCareer().negotiate(offer_id, weekly_wage, years,
                                     game->getCurrentDate());
}

bool GameController::acceptJobOffer(std::uint32_t offer_id)
{
  if (!game) return false;
  ManagerCareer& career = game->getCareer();
  const GameDateValue today = game->getCurrentDate();
  const std::optional<JobOffer> offer = career.takeOffer(offer_id, today);
  if (!offer) return false;
  const TeamID former = game->getManagedTeamId();
  if (former != FREE_AGENTS_TEAM_ID)
    career.payCompensation(former, offer->team_id, offer->compensation, today);
  game->takeJob(offer->team_id, career.contractFor(*offer, today));
  if (game->getManagedTeamId() != offer->team_id) return false;
  clearBidsBy(offer->team_id);
  return true;
}

bool GameController::declineJobOffer(std::uint32_t offer_id)
{
  return game && game->getCareer().decline(offer_id);
}

bool GameController::resignFromClub()
{
  if (!game || !hasSelectedTeam()) return false;
  game->leaveManagedTeam(DepartureReason::Resigned);
  return true;
}

int GameController::advanceWhileUnemployed(int max_days)
{
  const std::scoped_lock runtime_lock(runtime_mutex);
  if (!editor_session.expired())
  {
    return 0;
  }
  last_continue_stop = ContinueStop::None;
  if (!game || max_days <= 0 || !isUnemployed()) return 0;
  continue_stop_requested = false;
  game->resetSimulationProgress();
  continue_days_started = 0;
  continue_days_total = max_days;
  int days = 0;
  ContinueStop stop = ContinueStop::DayLimit;
  while (days < max_days && isUnemployed())
  {
    if (continue_stop_requested)
    {
      stop = ContinueStop::Requested;
      break;
    }
    simulateDay();
    ++days;
    const CareerDayEvents& events = game->getLastCareerEvents();
    const NationalDayEvents& national = game->getLastNationalEvents();
    if (events.new_offer || events.interview_invitation || national.new_offer ||
        national.squad_to_pick)
    {
      stop = ContinueStop::Decision;
      break;
    }
  }
  last_continue_stop = stop;
  return days;
}

// ========== Training ==========

std::optional<std::reference_wrapper<Team>> GameController::managedClub()
{
  if (!hasSelectedTeam()) return std::nullopt;
  return getManagedTeam();
}

std::optional<std::reference_wrapper<const Team>> GameController::managedClub()
    const
{
  if (!hasSelectedTeam()) return std::nullopt;
  return getManagedTeam();
}

const TeamTrainingPlan* GameController::getTrainingPlan() const
{
  const auto team = managedClub();
  if (!team || !gamedata) return nullptr;
  return gamedata->getTraining().findPlan(team->get().getId());
}

bool GameController::setTrainingPreset(TrainingPreset preset)
{
  const auto team = managedClub();
  if (!team || !gamedata || preset == TrainingPreset::COUNT) return false;
  TeamTrainingPlan& plan = gamedata->getTraining().plan(team->get().getId());
  plan.preset = preset;
  if (preset != TrainingPreset::Custom)
    plan.slots = TrainingModel::presetMicrocycle(preset);
  reclaimDuty(Duty::TrainingSchedule);
  return true;
}

bool GameController::setTrainingSlot(MicrocycleDay day, TrainingSlot slot)
{
  const auto team = managedClub();
  if (!team || !gamedata || day == MicrocycleDay::COUNT ||
      slot.session == SessionType::COUNT ||
      slot.intensity == TrainingIntensity::COUNT)
    return false;
  TeamTrainingPlan& plan = gamedata->getTraining().plan(team->get().getId());
  TrainingSlot& current = plan.slots[static_cast<size_t>(day)];
  if (current == slot) return true;
  current = slot;
  plan.preset = TrainingPreset::Custom;
  reclaimDuty(Duty::TrainingSchedule);
  return true;
}

bool GameController::setTrainingIntensity(TrainingIntensity intensity)
{
  const auto team = managedClub();
  if (!team || !gamedata || intensity == TrainingIntensity::COUNT) return false;
  TrainingIntensity& current =
      gamedata->getTraining().plan(team->get().getId()).intensity;
  if (current == intensity) return true;
  current = intensity;
  reclaimDuty(Duty::TrainingSchedule);
  return true;
}

bool GameController::setCongestionAutoAdjust(bool enabled)
{
  const auto team = managedClub();
  if (!team || !gamedata) return false;
  bool& current =
      gamedata->getTraining().plan(team->get().getId()).auto_congestion;
  if (current == enabled) return true;
  current = enabled;
  reclaimDuty(Duty::TrainingSchedule);
  return true;
}

bool GameController::setPlayerTrainingFocus(PlayerID player_id,
                                            TrainingFocus focus)
{
  const auto team = managedClub();
  if (!team || !gamedata || focus == TrainingFocus::COUNT) return false;
  const auto player = gamedata->getPlayer(player_id);
  if (!player || player->get().getTeamId() != team->get().getId()) return false;
  gamedata->getTraining().player(player_id).focus = focus;
  return true;
}

GameController::PlayerWorkload GameController::getPlayerWorkload(
    PlayerID player_id) const
{
  PlayerWorkload workload;
  if (!gamedata) return workload;
  const auto player = gamedata->getPlayer(player_id);
  const PlayerTrainingState* state =
      gamedata->getTraining().findPlayer(player_id);
  if (!player || !state) return workload;
  workload.focus = state->focus;
  workload.acute = state->acute;
  workload.chronic = state->chronic;
  workload.ratio = TrainingModel::workloadRatio(*state);
  workload.risk = TrainingModel::workloadRisk(
      *state, player->get().getDynamics().condition);
  workload.trend = state->trend;
  return workload;
}

std::vector<GameController::TrainingDayPreview>
GameController::getTrainingWeekPreview() const
{
  std::vector<TrainingDayPreview> preview;
  const TeamTrainingPlan* plan = getTrainingPlan();
  if (!plan || !game) return preview;
  const TeamID team_id = game->getManagedTeamId();
  const auto& fixtures = game->getCalendar().getFullCalendar();
  const GameDateValue today = game->getCurrentDate();
  const auto matchOn = [&](const GameDateValue& date) -> const Match*
  {
    const auto found = fixtures.find(date);
    if (found == fixtures.end()) return nullptr;
    for (const Match& match : found->second)
    {
      if (match.getHomeTeamId() == team_id || match.getAwayTeamId() == team_id)
        return &match;
    }
    return nullptr;
  };

  constexpr int DAYS = 7;
  constexpr int LOOK_AHEAD = 14;
  std::array<const Match*, LOOK_AHEAD> matches{};
  for (int offset = 0; offset < LOOK_AHEAD; ++offset)
    matches[static_cast<size_t>(offset)] =
        matchOn(SeasonCalendar::addDays(today, offset));
  std::int32_t last_match_day = plan->last_match_day;
  for (int offset = 0; offset < DAYS; ++offset)
  {
    const GameDateValue date = SeasonCalendar::addDays(today, offset);
    const std::int32_t ordinal = dayOrdinal(date);
    int next = -1;
    for (int ahead = offset; ahead < LOOK_AHEAD && ahead <= offset + 7; ++ahead)
    {
      if (matches[static_cast<size_t>(ahead)])
      {
        next = ahead - offset;
        break;
      }
    }
    const int since = last_match_day > 0 ? ordinal - last_match_day : -1;
    TrainingDayPreview day;
    day.date = date;
    if (date.month == 6)
    {
      day.day.slot = {SessionType::Rest, TrainingIntensity::Low};
    }
    else
    {
      day.day = TrainingModel::scheduleDay(*plan, since, next, ordinal);
    }
    if (const Match* match = matches[static_cast<size_t>(offset)])
    {
      day.day.match_day = true;
      day.home = match->getHomeTeamId() == team_id;
      day.opponent = day.home ? match->getAwayTeamId() : match->getHomeTeamId();
      last_match_day = ordinal;
    }
    preview.push_back(day);
  }
  return preview;
}

float GameController::getTacticalFamiliarity(TeamID team_id) const
{
  return gamedata ? TrainingSystem::tacticalFamiliarity(*gamedata, team_id)
                  : 0.0f;
}

std::vector<TrainingAdvice> GameController::getTrainingAdvice() const
{
  const auto team = managedClub();
  if (!team || !gamedata) return {};
  int matches_next_week = 0;
  for (const TrainingDayPreview& day : getTrainingWeekPreview())
    matches_next_week += day.day.match_day ? 1 : 0;
  return TrainingModel::advise(TrainingSystem::adviceInputs(
      *gamedata, team->get().getId(), matches_next_week));
}

// ========== Staff ==========

std::vector<const StaffMember*> GameController::getStaff(TeamID team_id) const
{
  return gamedata ? gamedata->getStaff().clubStaff(team_id)
                  : std::vector<const StaffMember*>{};
}

std::vector<const StaffMember*> GameController::getStaffMarket() const
{
  return gamedata ? gamedata->getStaff().market()
                  : std::vector<const StaffMember*>{};
}

StaffEffects GameController::getStaffEffects(TeamID team_id) const
{
  return gamedata ? gamedata->getStaff().effects(team_id) : StaffEffects{};
}

float GameController::getScoutingAccuracy(TeamID team_id) const
{
  return gamedata ? gamedata->getStaff().effects(team_id).scouting_accuracy
                  : 0.0f;
}

uint32_t GameController::getStaffWageDemand(StaffID staff_id) const
{
  if (!gamedata) return 0;
  const StaffMember* member = gamedata->getStaff().find(staff_id);
  if (!member) return 0;
  return member->team_id == FREE_AGENTS_TEAM_ID
             ? StaffModel::marketWage(*member)
             : StaffModel::renewalWage(*member);
}

GameController::StaffActionResult GameController::hireStaff(StaffID staff_id,
                                                            uint8_t years)
{
  const auto team = managedClub();
  if (!team || !gamedata) return StaffActionResult::NoClub;
  StaffRoster& roster = gamedata->getStaff();
  StaffMember* member = roster.find(staff_id);
  if (!member) return StaffActionResult::UnknownStaff;
  if (member->team_id != FREE_AGENTS_TEAM_ID)
    return StaffActionResult::NotAvailable;
  if (years < 1 || years > 4) return StaffActionResult::InvalidTerms;
  const TeamID team_id = team->get().getId();
  if (roster.roleCount(team_id, member->role) >=
      StaffModel::ROLE_LIMITS[static_cast<size_t>(member->role)])
    return StaffActionResult::RoleFull;
  const uint32_t wage = StaffModel::marketWage(*member);
  if (team->get().getFinances().getBalance() < 12 * static_cast<int64_t>(wage))
    return StaffActionResult::CannotAfford;
  member->team_id = team_id;
  member->wage = wage;
  member->contract_years = years;
  roster.invalidate();
  return StaffActionResult::Ok;
}

GameController::StaffActionResult GameController::releaseStaff(StaffID staff_id)
{
  const auto team = managedClub();
  if (!team || !gamedata || !game) return StaffActionResult::NoClub;
  StaffRoster& roster = gamedata->getStaff();
  StaffMember* member = roster.find(staff_id);
  if (!member) return StaffActionResult::UnknownStaff;
  if (member->team_id != team->get().getId())
    return StaffActionResult::NotAvailable;
  const int64_t severance = StaffModel::severance(*member);
  Finances& finances = team->get().getFinances();
  if (finances.getBalance() < severance) return StaffActionResult::CannotAfford;
  finances.record(game->getCurrentDate(), FinanceCategory::Staff, -severance);
  member->team_id = FREE_AGENTS_TEAM_ID;
  member->contract_years = 0;
  member->wage = StaffModel::marketWage(*member);
  roster.invalidate();
  return StaffActionResult::Ok;
}

GameController::StaffActionResult GameController::extendStaffContract(
    StaffID staff_id, uint8_t years)
{
  const auto team = managedClub();
  if (!team || !gamedata) return StaffActionResult::NoClub;
  StaffRoster& roster = gamedata->getStaff();
  StaffMember* member = roster.find(staff_id);
  if (!member) return StaffActionResult::UnknownStaff;
  if (member->team_id != team->get().getId())
    return StaffActionResult::NotAvailable;
  if (years <= member->contract_years || years < 2 || years > 5)
    return StaffActionResult::InvalidTerms;
  member->wage = StaffModel::renewalWage(*member);
  member->contract_years = years;
  roster.invalidate();
  return StaffActionResult::Ok;
}

// ========== Youth academy ==========

namespace
{
GameDateValue intakeDateFor(std::uint16_t year)
{
  return GameDateValue(year, YouthModel::INTAKE_MONTH, YouthModel::INTAKE_DAY);
}
}  // namespace

std::vector<GameController::YouthPlayerView> GameController::getYouthPlayers(
    YouthStatus status) const
{
  std::vector<YouthPlayerView> views;
  const auto team = managedClub();
  if (!team || !game) return views;
  const TeamID team_id = team->get().getId();
  const YouthAcademy& academy = game->getWorld().getYouth();
  const int pro_age = YouthModel::firstProfessionalAge(
      leagueProfile(team->get().getLeagueId()).domestic_nationality);
  for (const YouthRecord* youth : academy.members(team_id, status))
  {
    const Player& player = gamedata->getPlayers().at(youth->player_id);
    YouthPlayerView view;
    view.id = player.getId();
    view.name = player.getName();
    view.role = player.getRole();
    view.age = player.getAge();
    view.nationality = player.getNationality();
    view.height = player.getHeight();
    view.status = youth->status;
    view.contract = youth->contract;
    view.contract_years = player.getContractYears();
    view.wage = player.getWage();
    if (youth->contract == YouthContract::None)
      view.offer = view.age >= pro_age ? YouthContract::Professional
                                       : YouthContract::Scholarship;
    else if (youth->contract == YouthContract::Scholarship &&
             view.age >= pro_age)
      view.offer = YouthContract::Professional;
    view.offer_wage = academy.contractWage(team_id, player, view.offer);
    view.estimate = academy.estimate(team_id, view.id);
    view.personality_key = YouthModel::personalityKey(player.getTraits());
    view.homegrown = academy.isHomegrown(view.id);
    view.loan_listed = game->getTransfers().isLoanListed(view.id);
    view.appearances = youth->appearances;
    view.goals = youth->goals;
    view.average_rating = youth->averageRating();
    view.progress = youth->progress;
    views.push_back(std::move(view));
  }
  std::ranges::stable_sort(
      views,
      [](const YouthPlayerView& a, const YouthPlayerView& b)
      {
        return a.estimate.potential_low + a.estimate.potential_high >
               b.estimate.potential_low + b.estimate.potential_high;
      });
  return views;
}

std::vector<GameController::YouthPlayerView>
GameController::getYouthEligibleFirstTeam() const
{
  std::vector<YouthPlayerView> views;
  const auto team = managedClub();
  if (!team || !game) return views;
  const YouthAcademy& academy = game->getWorld().getYouth();
  for (const auto& player_ref :
       gamedata->getPlayersForTeam(team->get().getId()))
  {
    const Player& player = player_ref.get();
    if (player.getAge() > YouthModel::U18_MAX_AGE ||
        academy.isAcademyPlayer(player.getId()))
      continue;
    YouthPlayerView view;
    view.id = player.getId();
    view.name = player.getName();
    view.role = player.getRole();
    view.age = player.getAge();
    view.nationality = player.getNationality();
    view.height = player.getHeight();
    view.status = YouthStatus::Graduated;
    view.contract = YouthContract::Professional;
    view.contract_years = player.getContractYears();
    view.wage = player.getWage();
    view.estimate = academy.estimate(team->get().getId(), view.id);
    view.personality_key = YouthModel::personalityKey(player.getTraits());
    view.homegrown = academy.isHomegrown(view.id);
    view.loan_listed = game->getTransfers().isLoanListed(view.id);
    views.push_back(std::move(view));
  }
  std::ranges::sort(views, {}, &YouthPlayerView::id);
  return views;
}

bool GameController::isAcademyPlayer(PlayerID player_id) const
{
  return game && game->getWorld().getYouth().isAcademyPlayer(player_id);
}

GameController::AcademyOverview GameController::getAcademyOverview() const
{
  AcademyOverview overview;
  const auto team = managedClub();
  if (!team || !game) return overview;
  const TeamID team_id = team->get().getId();
  const YouthAcademy& academy = game->getWorld().getYouth();
  const GameDateValue today = game->getCurrentDate();
  overview.ratings = academy.ratings(team_id);
  overview.head = academy.headOfYouth(team_id);
  overview.candidates = academy.members(team_id, YouthStatus::Candidate).size();
  overview.squad = academy.members(team_id, YouthStatus::Squad).size();

  // The intake cycle in progress: this year's until its decision deadline,
  // afterwards next year's.
  std::uint16_t year = today.year;
  if (dayOrdinal(today) >
      dayOrdinal(intakeDateFor(year)) + YouthModel::DECISION_DAYS)
    ++year;
  overview.intake_date = intakeDateFor(year);
  overview.preview_date =
      GameDateValue(year, YouthModel::PREVIEW_MONTH, YouthModel::PREVIEW_DAY);
  overview.decision_deadline =
      SeasonCalendar::addDays(overview.intake_date, YouthModel::DECISION_DAYS);
  overview.preview_ready = !(today < overview.preview_date);
  if (overview.preview_ready) overview.preview = academy.preview(team_id, year);

  const std::vector<YouthTableRow> rows =
      academy.table(team->get().getLeagueId());
  overview.league_size = static_cast<int>(rows.size());
  for (std::size_t index = 0; index < rows.size(); ++index)
  {
    if (rows[index].team_id != team_id) continue;
    overview.league_position = static_cast<int>(index) + 1;
    overview.table = rows[index];
  }
  return overview;
}

std::vector<YouthTableRow> GameController::getYouthTable() const
{
  const auto team = managedClub();
  if (!team || !game) return {};
  return game->getWorld().getYouth().table(team->get().getLeagueId());
}

const std::vector<YouthResult>& GameController::getYouthResults() const
{
  static const std::vector<YouthResult> EMPTY;
  return game ? game->getWorld().getYouth().results() : EMPTY;
}

UpgradeQuote GameController::getAcademyUpgradeQuote(AcademyUpgrade kind) const
{
  const auto team = managedClub();
  if (!team || !game) return {};
  return game->getWorld().getYouth().quote(
      team->get().getId(), kind, game->getCurrentDate(),
      game->getWorld().getBoardState().confidence, isTransferEmbargoed());
}

UpgradeRequestResult GameController::requestAcademyUpgrade(AcademyUpgrade kind)
{
  const auto team = managedClub();
  if (!team || !game) return UpgradeRequestResult::NoClub;
  WorldSimulation& world = game->getWorld();
  return world.getYouth().requestUpgrade(
      game->getCurrentDate(), team->get().getId(), kind,
      world.getBoardState().confidence, isTransferEmbargoed(),
      world.getInbox());
}

YouthActionResult GameController::signYouthCandidate(PlayerID player_id)
{
  const auto team = managedClub();
  if (!team || !game) return YouthActionResult::NoClub;
  return game->getWorld().getYouth().signCandidate(team->get().getId(),
                                                   player_id);
}

YouthActionResult GameController::releaseYouthCandidate(PlayerID player_id)
{
  const auto team = managedClub();
  if (!team || !game) return YouthActionResult::NoClub;
  return game->getWorld().getYouth().releaseCandidate(team->get().getId(),
                                                      player_id);
}

YouthActionResult GameController::offerYouthProfessionalContract(
    PlayerID player_id)
{
  const auto team = managedClub();
  if (!team || !game) return YouthActionResult::NoClub;
  return game->getWorld().getYouth().offerProfessional(team->get().getId(),
                                                       player_id);
}

YouthActionResult GameController::promoteYouthPlayer(PlayerID player_id)
{
  const auto team = managedClub();
  if (!team || !game) return YouthActionResult::NoClub;
  return game->getWorld().getYouth().promote(team->get().getId(), player_id);
}

YouthActionResult GameController::moveToYouthSquad(PlayerID player_id)
{
  const auto team = managedClub();
  if (!team || !game) return YouthActionResult::NoClub;
  return game->getWorld().getYouth().demote(team->get().getId(), player_id);
}

// ========== Honours: awards, records, hall of fame ==========

const std::vector<AwardRecord>& GameController::getAwardHistory() const
{
  static const std::vector<AwardRecord> NONE;
  return game ? game->getWorld().getAwards().history() : NONE;
}

std::vector<AwardRecord> GameController::getLeagueAwards(
    LeagueID league_id, uint16_t season_year) const
{
  if (!game) return {};
  return game->getWorld().getAwards().forLeague(league_id, season_year);
}

std::vector<AwardRecord> GameController::getPlayerHonours(
    PlayerID player_id) const
{
  if (!game) return {};
  return game->getWorld().getAwards().honoursFor(player_id);
}

std::vector<AwardPlayerTally> GameController::getAwardRace(LeagueID league_id,
                                                           bool young,
                                                           size_t limit) const
{
  if (!game) return {};
  std::vector<AwardCandidate> pool;
  const auto& tallies = game->getWorld().getAwards().seasonTallies();
  for (auto it = tallies.lower_bound({league_id, 0});
       it != tallies.end() && it->first.first == league_id; ++it)
  {
    const auto player = gamedata->getPlayer(it->second.player_id);
    if (!player || it->second.rated == 0 ||
        (young && player->get().getAge() > Awards::YOUNG_MAX_AGE))
      continue;
    pool.push_back(
        {player->get().getRole(), player->get().getAge(), it->second});
  }
  const std::uint16_t min_minutes = Awards::seasonMinMinutes(pool);
  const auto score = [](const AwardCandidate& candidate)
  {
    return Awards::playerScore(candidate.tally,
                               candidate.role == PlayerRole::GK);
  };
  std::ranges::sort(pool,
                    [&](const AwardCandidate& a, const AwardCandidate& b)
                    {
                      const bool qa = a.tally.minutes >= min_minutes;
                      const bool qb = b.tally.minutes >= min_minutes;
                      if (qa != qb) return qa;
                      const float sa = score(a);
                      const float sb = score(b);
                      return sa != sb ? sa > sb
                                      : a.tally.player_id < b.tally.player_id;
                    });
  std::vector<AwardPlayerTally> race;
  for (size_t index = 0; index < pool.size() && index < limit; ++index)
    race.push_back(pool[index].tally);
  return race;
}

std::vector<RecordEntry> GameController::getClubRecords(TeamID team_id) const
{
  if (!game) return {};
  return game->getWorld().getRecords().clubRecords(team_id);
}

std::vector<RecordEntry> GameController::getLeagueRecords(
    LeagueID league_id) const
{
  if (!game) return {};
  return game->getWorld().getRecords().leagueRecords(league_id);
}

std::vector<ClubPlayerTotal> GameController::getClubTopScorers(
    TeamID team_id, size_t limit) const
{
  if (!game) return {};
  return game->getWorld().getRecords().topScorers(team_id, limit);
}

std::vector<ClubPlayerTotal> GameController::getClubMostAppearances(
    TeamID team_id, size_t limit) const
{
  if (!game) return {};
  return game->getWorld().getRecords().mostAppearances(team_id, limit);
}

std::vector<AllTimeRow> GameController::getAllTimeTable(
    LeagueID league_id) const
{
  if (!game) return {};
  return game->getWorld().getRecords().allTimeTable(league_id);
}

std::vector<LegendEntry> GameController::getHallOfFame(TeamID team_id) const
{
  if (!game) return {};
  const AwardSystem& awards = game->getWorld().getAwards();
  return game->getWorld().getRecords().hallOfFame(
      team_id, [&awards](PlayerID player_id, TeamID club)
      { return awards.seasonHonours(player_id, club); });
}

// ========== Board: facility projects ==========

ProjectQuote GameController::getProjectQuote(FacilityProjectType type,
                                             uint32_t seats) const
{
  const auto team = managedClub();
  if (!team || !game) return {};
  return game->getWorld().getFacilityProjects().quote(
      *gamedata, team->get().getId(), type, seats);
}

ProjectVerdict GameController::requestFacilityProject(FacilityProjectType type,
                                                      uint32_t seats)
{
  const auto team = managedClub();
  if (!team || !game) return ProjectVerdict::AtMaximum;
  WorldSimulation& world = game->getWorld();
  return world.getFacilityProjects().request(*gamedata, world.getBoardState(),
                                             team->get().getId(), type, seats,
                                             game->getCurrentDate());
}

std::vector<FacilityProject> GameController::getFacilityProjects() const
{
  const auto team = managedClub();
  if (!team || !game) return {};
  return game->getWorld().getFacilityProjects().projectsFor(
      team->get().getId());
}

uint8_t GameController::getMedicalLevel(TeamID team_id) const
{
  if (!game) return 50;
  return game->getWorld().getFacilityProjects().medicalLevel(team_id);
}

std::optional<GameDateValue> GameController::getProjectCooldown(
    FacilityProjectType type) const
{
  const auto team = managedClub();
  if (!team || !game) return std::nullopt;
  return game->getWorld().getFacilityProjects().cooldownUntil(
      team->get().getId(), type, game->getCurrentDate());
}

// ========== Pre-season planner ==========

std::vector<FriendlySlot> GameController::getPreseasonFriendlies() const
{
  const auto team = managedClub();
  if (!team || !game) return {};
  return game->getWorld().getPreseason().friendlies(
      game->getCalendar(), team->get().getId(), game->getCurrentDate());
}

std::vector<OpponentOption> GameController::getFriendlyOpponents(
    GameDateValue date, OpponentLevel level, bool abroad) const
{
  const auto team = managedClub();
  if (!team || !game) return {};
  return game->getWorld().getPreseason().opponents(
      *gamedata, game->getCalendar(), team->get().getId(), date, level, abroad);
}

bool GameController::setPreseasonFriendly(GameDateValue date,
                                          TeamID opponent_id, bool home,
                                          bool tour)
{
  const auto team = managedClub();
  if (!team || !game ||
      !game->getWorld().getPreseason().setFriendly(
          game->getCalendar(), *gamedata, team->get().getId(), date,
          opponent_id, home, tour, game->getCurrentDate()))
    return false;
  reclaimDuty(Duty::Friendlies);
  return true;
}

std::vector<FriendlySuggestion> GameController::getPreseasonSuggestion() const
{
  const auto team = managedClub();
  if (!team || !game) return {};
  return game->getWorld().getPreseason().suggest(*gamedata, game->getCalendar(),
                                                 team->get().getId(),
                                                 game->getCurrentDate());
}

size_t GameController::applyPreseasonSuggestion()
{
  size_t applied = 0;
  for (const FriendlySuggestion& friendly : getPreseasonSuggestion())
    if (setPreseasonFriendly(friendly.date, friendly.opponent_id, friendly.home,
                             false))
      ++applied;
  return applied;
}

CampQuote GameController::getCampQuote(TrainingCamp camp) const
{
  const auto team = managedClub();
  if (!team || !game) return CampQuote();
  return game->getWorld().getPreseason().campQuote(
      *gamedata, game->getCalendar(), team->get().getId(), camp);
}

TrainingCamp GameController::getSuggestedCamp() const
{
  const auto team = managedClub();
  if (!team || !game) return TrainingCamp::None;
  return game->getWorld().getPreseason().suggestCamp(
      *gamedata, game->getCalendar(), team->get().getId(),
      game->getCurrentDate());
}

bool GameController::bookTrainingCamp(TrainingCamp camp)
{
  const auto team = managedClub();
  if (!team || !game) return false;
  return game->getWorld().getPreseason().bookCamp(
      *gamedata, game->getCalendar(), team->get().getId(), camp,
      game->getCurrentDate());
}

const PreseasonState& GameController::getPreseasonState() const
{
  static const PreseasonState NONE;
  return game ? game->getWorld().getPreseason().getState() : NONE;
}

int64_t GameController::getTourFee(TeamID opponent_id) const
{
  const auto team = managedClub();
  const auto opponent = gamedata->getTeam(opponent_id);
  if (!team || !opponent) return 0;
  std::vector<std::uint8_t> reputations;
  if (const auto league = gamedata->getLeague(team->get().getLeagueId()))
    for (const TeamID id : league->get().getTeamIDs())
      if (const auto club = gamedata->getTeam(id))
        reputations.push_back(club->get().getReputation());
  const double income = ClubEconomy::expectedIncome(
      makeLeagueEconomy(team->get().getLeagueId(), reputations),
      team->get().getReputation());
  return Preseason::tourFee(income, opponent->get().getReputation());
}

// ========== Mentoring groups ==========

std::vector<MentoringGroup> GameController::getMentoringGroups() const
{
  const auto team = managedClub();
  if (!team || !game) return {};
  return game->getWorld().getMentoring().groupsFor(team->get().getId());
}

MentoringError GameController::createMentoringGroup(PlayerID mentor_id,
                                                    uint32_t* group_id)
{
  const auto team = managedClub();
  if (!team || !game) return MentoringError::NotSameClub;
  return game->getWorld().getMentoring().createGroup(
      *gamedata, team->get().getId(), mentor_id, group_id);
}

MentoringError GameController::addMentee(uint32_t group_id, PlayerID mentee_id)
{
  if (!game) return MentoringError::UnknownGroup;
  return game->getWorld().getMentoring().addMentee(*gamedata, group_id,
                                                   mentee_id);
}

MentoringError GameController::removeMentee(uint32_t group_id,
                                            PlayerID mentee_id)
{
  if (!game) return MentoringError::UnknownGroup;
  return game->getWorld().getMentoring().removeMentee(group_id, mentee_id);
}

MentoringError GameController::dissolveMentoringGroup(uint32_t group_id)
{
  if (!game) return MentoringError::UnknownGroup;
  return game->getWorld().getMentoring().dissolve(group_id);
}

float GameController::getMentoringMultiplier(PlayerID player_id) const
{
  if (!game) return 1.0f;
  return game->getWorld().getMentoring().developmentMultiplier(*gamedata,
                                                               player_id);
}

// ========== Holiday / continue until ==========

HolidayPreferences GameController::getHolidayPreferences() const
{
  return game ? game->getWorld().getHolidayPreferences() : HolidayPreferences{};
}

void GameController::setHolidayPreferences(
    const HolidayPreferences& preferences)
{
  if (game) game->getWorld().getHolidayPreferences() = preferences;
}

std::optional<GameDateValue> GameController::getHolidayTarget(
    const HolidayPlan& plan) const
{
  if (!game || !hasSelectedTeam()) return std::nullopt;
  const TeamID managed = game->getManagedTeamId();
  // The next fixture after today: a match due today is left to the
  // assistant once the manager leaves.
  std::optional<GameDateValue> next_match;
  const auto& schedule = game->getCalendar().getFullCalendar();
  for (auto day = schedule.upper_bound(game->getCurrentDate());
       day != schedule.end() && !next_match; ++day)
    for (const Match& match : day->second)
      if (!match.isPlayed() && (match.getHomeTeamId() == managed ||
                                match.getAwayTeamId() == managed))
      {
        next_match = day->first;
        break;
      }
  const auto club = managedClub();
  return Holiday::targetDate(plan, game->getCurrentDate(), next_match,
                             club ? club->get().getLeagueId() : LeagueID{0});
}

int GameController::goOnHoliday(const HolidayPlan& plan)
{
  const std::scoped_lock runtime_lock(runtime_mutex);
  if (!editor_session.expired()) return 0;
  holiday_summary = HolidaySummary();
  last_continue_stop = ContinueStop::None;
  if (!game || !hasSelectedTeam()) return 0;
  continue_stop_requested = false;
  const TeamID managed = game->getManagedTeamId();
  WorldSimulation& world = game->getWorld();
  world.getHolidayPreferences() = plan.preferences;
  const HolidayPreferences& rules = plan.preferences;
  const std::optional<GameDateValue> target = getHolidayTarget(plan);
  // Open-ended holidays wait at most three months for a decision.
  constexpr int OPEN_ENDED_DAYS = 90;
  const int limit = std::clamp(
      target ? plan.max_days : std::min(plan.max_days, OPEN_ENDED_DAYS), 0,
      366);
  if (!target && plan.mode != HolidayMode::NextDecision) return 0;

  HolidaySummary summary;
  summary.valid = true;
  summary.start = game->getCurrentDate();
  const Team& club = gamedata->getTeam(managed)->get();
  const LeagueID league_id = club.getLeagueId();
  const auto tableSpot = [&](int& position, int& points, int* played)
  {
    for (const StandingRow& row : getStandings(league_id))
      if (row.team_id == managed)
      {
        position = row.position;
        points = row.points;
        if (played) *played = row.played;
      }
  };
  tableSpot(summary.position_before, summary.points_before,
            &summary.played_before);
  summary.balance_before = club.getFinances().getBalance();
  const size_t history_before = game->getTransfers().history().size();
  const auto& messages = world.getInbox().getMessages();
  const uint32_t first_new_message =
      messages.empty() ? 0 : messages.back().id + 1;
  uint32_t next_message = first_new_message;

  std::unordered_set<uint32_t> known_offers;
  for (const IncomingOffer& offer : getIncomingOffers())
    known_offers.insert(offer.id);
  std::unordered_map<PlayerID, uint16_t> injured;
  std::unordered_set<PlayerID> pending;
  for (const auto& player : gamedata->getPlayersForTeam(managed))
  {
    if (player.get().getDynamics().injury_days > 0)
      injured.emplace(player.get().getId(),
                      player.get().getDynamics().injury_days);
    if (hasPendingTalk(player.get().getId()))
      pending.insert(player.get().getId());
  }
  const int injured_at_start = static_cast<int>(injured.size());

  // While the manager is away the assistant also takes the duties handed
  // to him for the holiday; the manager's own policy returns afterwards.
  DelegationPolicy& delegation = game->getGuidance().delegation;
  const DelegationPolicy delegation_before = delegation;
  delegation_after_holiday = delegation_before;
  if (rules.assistant_lineup)
    delegation.set(Duty::LineupFixes, DutyOwner::Assistant);
  if (rules.assistant_training)
    delegation.set(Duty::TrainingSchedule, DutyOwner::Assistant);

  game->resetSimulationProgress();
  continue_days_started = 0;
  continue_days_total =
      target
          ? std::clamp(dayOrdinal(*target) - dayOrdinal(game->getCurrentDate()),
                       0, limit)
          : 0;
  int advanced = 0;
  while (!target || game->getCurrentDate() < *target)
  {
    if (advanced >= limit)
    {
      summary.reason = HolidayStop::DayLimit;
      break;
    }
    // The manager (or closing the game) calls him back after this day.
    if (continue_stop_requested)
    {
      summary.reason = HolidayStop::Interrupted;
      break;
    }
    simulateDay();
    ++advanced;

    HolidayDay day;
    // A sacking (or a contract running out) is applied within the same day
    // and resets the board, so the lost job itself is the signal.
    day.dismissed = game->getManagedTeamId() != managed;
    for (const InboxMessage& message : world.getInbox().getMessages())
    {
      if (message.id < next_message) continue;
      day.board_warning =
          day.board_warning || message.title_key == "INBOX_BOARD_WARNING_TITLE";
      day.new_decision =
          day.new_decision ||
          (!message.read && (message.category == InboxCategory::Board ||
                             message.category == InboxCategory::Contract));
    }
    if (!world.getInbox().getMessages().empty())
      next_message = world.getInbox().getMessages().back().id + 1;
    for (const IncomingOffer& offer : getIncomingOffers())
    {
      if (!known_offers.insert(offer.id).second) continue;
      day.new_offers.push_back(
          {offer.player_id,
           offer.loan ? offer.loan_terms.loan_fee : offer.terms.fee, offer.loan,
           getSquadRole(offer.player_id) == SquadRole::KeyPlayer});
      day.new_decision = true;
    }
    std::unordered_map<PlayerID, uint16_t> injured_now;
    for (const auto& player : gamedata->getPlayersForTeam(managed))
    {
      const PlayerID id = player.get().getId();
      const PlayerDynamics& dynamics = player.get().getDynamics();
      if (dynamics.injury_days > 0)
      {
        injured_now.emplace(id, dynamics.injury_days);
        if (!injured.contains(id))
        {
          HolidayInjury injury{
              id, player.get().getName(), InjuryModel::nameKey(dynamics.injury),
              dynamics.injury_days, getSquadRole(id) == SquadRole::KeyPlayer};
          day.new_injuries.push_back(injury);
          summary.injuries.push_back(std::move(injury));
        }
      }
      if (hasPendingTalk(id) && pending.insert(id).second)
        day.new_decision = true;
    }
    injured = std::move(injured_now);
    day.injured = static_cast<int>(injured.size());
    day.injured_at_start = injured_at_start;

    if (const auto stop = Holiday::checkStop(plan, day))
    {
      summary.reason = *stop;
      if (*stop == HolidayStop::BigBid)
        for (const HolidayOffer& offer : day.new_offers)
          if (!offer.loan)
          {
            if (const auto player = gamedata->getPlayer(offer.player_id))
              summary.stop_detail = player->get().getName();
            summary.stop_amount = offer.fee;
            break;
          }
      if (*stop == HolidayStop::KeyPlayerInjured)
        for (const HolidayInjury& injury : day.new_injuries)
          if (injury.key_player) summary.stop_detail = injury.name;
      break;
    }
  }
  delegation = delegation_before;
  delegation_after_holiday.reset();

  summary.end = game->getCurrentDate();
  summary.days = advanced;
  if (rules.assistant_inbox)
    for (const InboxMessage& message : world.getInbox().getMessages())
      if (message.id >= first_new_message && !message.read &&
          message.category != InboxCategory::Board &&
          message.category != InboxCategory::Contract &&
          world.getInbox().markRead(message.id))
        ++summary.messages_filed;

  for (const Match& match : getTeamFixtures(managed))
  {
    if (!match.isPlayed() || match.getDate() < summary.start ||
        summary.end < match.getDate())
      continue;
    const bool home = match.getHomeTeamId() == managed;
    summary.results.push_back(
        {match.getDate(), home ? match.getAwayTeamId() : match.getHomeTeamId(),
         home, match.getMatchType(),
         home ? match.getHomeScore() : match.getAwayScore(),
         home ? match.getAwayScore() : match.getHomeScore()});
  }
  const auto& history = game->getTransfers().history();
  for (size_t index = history_before; index < history.size(); ++index)
  {
    const TransferRecord& record = history[index];
    if (record.to_team != managed && record.from_team != managed) continue;
    const bool incoming = record.to_team == managed;
    const auto player = gamedata->getPlayer(record.player_id);
    summary.moves.push_back(
        {record.player_id, player ? player->get().getName() : std::string(),
         incoming ? record.from_team : record.to_team, record.fee, incoming});
  }
  tableSpot(summary.position_after, summary.points_after, nullptr);
  summary.balance_after = club.getFinances().getBalance();
  if (summary.start < summary.end)
  {
    const FinanceSummary finance =
        getFinanceSummary(managed, summary.start + 1, summary.end);
    summary.income = finance.income;
    summary.expenses = finance.expenses;
  }
  holiday_summary = std::move(summary);
  return advanced;
}

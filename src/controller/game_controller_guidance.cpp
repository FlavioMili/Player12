// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------
//
// Guidance features of GameController: first-week checklist, next steps,
// delegation (and the assistant's delegated daily jobs), opposition report,
// match snapshots and the data hub.

#include <algorithm>
#include <format>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "model/inbox.h"
#include "model/match_engine.h"
#include "model/world_rng.h"

namespace
{
/** Assistant's contract renewals run on the first of these months. */
constexpr int RENEWAL_FIRST_MONTH = 1;
constexpr int RENEWAL_LAST_MONTH = 5;
/** Oldest player the assistant renews. */
constexpr int RENEWAL_MAX_AGE = 32;
/** Scouting trips the assistant books. */
constexpr std::uint16_t SCOUT_PLAYER_DAYS = 14;
constexpr std::uint16_t SCOUT_LEAGUE_DAYS = 28;
/** Knowledge below which a shortlisted player is worth a trip. */
constexpr float SCOUT_KNOWLEDGE_TARGET = 60.0f;
/** A trip may cost at most this share of the cash balance. */
constexpr std::int64_t SCOUT_CASH_DIVISOR = 25;
/** Days before the youth decision deadline the assistant signs. */
constexpr int YOUTH_DECISION_LEAD_DAYS = 3;
/** Squad condition under which the assistant lightens training. */
constexpr float LIGHT_TRAINING_CONDITION = 80.0f;

const OnboardingState& emptyOnboarding()
{
  static const OnboardingState state;
  return state;
}

const DelegationPolicy& defaultDelegation()
{
  static const DelegationPolicy policy;
  return policy;
}

InboxMessage assistantNote(const GameDateValue& date, InboxCategory category,
                           const char* title, const char* body,
                           std::vector<std::string> args,
                           std::optional<PlayerID> player = std::nullopt)
{
  InboxMessage message;
  message.date = date;
  message.category = category;
  message.title_key = title;
  message.body_key = body;
  message.args = std::move(args);
  message.player_id = player;
  return message;
}
}  // namespace

// ========== Checklist ==========

const OnboardingState& GameController::getOnboarding() const
{
  return game ? game->getGuidance().onboarding : emptyOnboarding();
}

bool GameController::completeOnboardingTask(OnboardingTask task)
{
  return game && game->getGuidance().onboarding.complete(task);
}

void GameController::dismissOnboarding()
{
  if (game) game->getGuidance().onboarding.dismiss();
}

void GameController::showOnboarding()
{
  if (game) game->getGuidance().onboarding.undismiss();
}

// ========== Next steps ==========

std::vector<NextAction> GameController::getNextActions(size_t limit) const
{
  if (!game || !hasSelectedTeam()) return {};
  return rankNextActions(gatherNextActionFacts(*this), limit);
}

// ========== Delegation ==========

const DelegationPolicy& GameController::getDelegation() const
{
  return game ? game->getGuidance().delegation : defaultDelegation();
}

bool GameController::isDelegated(Duty duty) const
{
  return getDelegation().delegated(duty);
}

namespace
{
/** Marks changes made by the assistant (they never reclaim a duty). */
class AssistantActing
{
 public:
  explicit AssistantActing(bool& acting)
      : flag(acting), previous(std::exchange(acting, true))
  {
  }
  ~AssistantActing() { flag = previous; }
  AssistantActing(const AssistantActing&) = delete;
  AssistantActing& operator=(const AssistantActing&) = delete;

 private:
  bool& flag;
  bool previous;
};
}  // namespace

bool GameController::setDutyOwner(Duty duty, DutyOwner owner)
{
  if (!game || !game->getGuidance().delegation.set(duty, owner)) return false;
  if (reclaimed_duty == duty) reclaimed_duty.reset();
  if (duty == Duty::TrainingSchedule && owner == DutyOwner::Assistant)
  {
    const AssistantActing acting(assistant_acting);
    setCongestionAutoAdjust(true);
  }
  return true;
}

void GameController::applyDelegationPreset(DelegationPreset preset)
{
  if (!game) return;
  game->getGuidance().delegation.apply(preset);
  reclaimed_duty.reset();
  if (isDelegated(Duty::TrainingSchedule))
  {
    const AssistantActing acting(assistant_acting);
    setCongestionAutoAdjust(true);
  }
}

void GameController::reclaimDuty(Duty duty)
{
  if (assistant_acting || !game || !isDelegated(duty)) return;
  game->getGuidance().delegation.set(duty, DutyOwner::Manager);
  reclaimed_duty = duty;
}

void GameController::undoReclaimedDuty()
{
  if (!reclaimed_duty) return;
  const Duty duty = *reclaimed_duty;
  setDutyOwner(duty, DutyOwner::Assistant);
  reclaimed_duty.reset();
}

const StaffMember* GameController::getDelegate() const
{
  const auto club = getManagedTeam();
  if (!club) return nullptr;
  const std::vector<const StaffMember*> staff = getStaff(club->get().getId());
  if (const auto assistant = std::ranges::find_if(
          staff, [](const StaffMember* member)
          { return member->role == StaffRole::AssistantManager; });
      assistant != staff.end())
    return *assistant;
  return staff.empty() ? nullptr : staff.front();
}

void GameController::runDelegatedDuties()
{
  const auto club = managedClub();
  if (!club || !gamedata) return;
  const AssistantActing acting(assistant_acting);
  const TeamID club_id = club->get().getId();
  const GameDateValue today = game->getCurrentDate();
  Inbox& inbox = game->getWorld().getInbox();

  if (isDelegated(Duty::Friendlies))
  {
    // Open pre-season dates get the assistant's opponents once.
    const std::vector<FriendlySuggestion> suggestion = getPreseasonSuggestion();
    const std::vector<FriendlySlot> slots = getPreseasonFriendlies();
    const bool differs =
        std::ranges::any_of(suggestion,
                            [&slots](const FriendlySuggestion& pick)
                            {
                              return std::ranges::none_of(
                                  slots,
                                  [&pick](const FriendlySlot& slot)
                                  {
                                    return slot.date == pick.date &&
                                           slot.opponent_id == pick.opponent_id;
                                  });
                            });
    if (differs) applyPreseasonSuggestion();
  }

  if (isDelegated(Duty::TrainingSchedule))
  {
    if (const TeamTrainingPlan* plan = getTrainingPlan())
    {
      if (!plan->auto_congestion) setCongestionAutoAdjust(true);
      plan = getTrainingPlan();  // The plan may be rebuilt by the change.
      int match_days = 0;
      for (const TrainingDayPreview& day : getTrainingWeekPreview())
        if (day.opponent != 0) ++match_days;
      float condition = 0.0f;
      int fit = 0;
      for (const auto& player : getPlayersForTeam(club_id))
      {
        if (!isPlayerAvailable(player.get().getId())) continue;
        condition += player.get().getDynamics().condition;
        ++fit;
      }
      const bool tired = fit > 0 && condition / static_cast<float>(fit) <
                                        LIGHT_TRAINING_CONDITION;
      const TrainingIntensity wanted = match_days >= 2 || tired
                                           ? TrainingIntensity::Low
                                           : TrainingIntensity::Normal;
      if (plan->intensity != wanted) setTrainingIntensity(wanted);
    }
  }

  if (isDelegated(Duty::ContractRenewals) && today.day == 1 &&
      today.month >= RENEWAL_FIRST_MONTH && today.month <= RENEWAL_LAST_MONTH)
  {
    std::vector<PlayerID> expiring;
    for (const auto& player : getPlayersForTeam(club_id))
    {
      const Player& p = player.get();
      const SquadRole role = getSquadRole(p.getId());
      if (p.getContractYears() == 1 && p.getAge() <= RENEWAL_MAX_AGE &&
          role != SquadRole::KeyPlayer && role != SquadRole::Fringe &&
          !isAcademyPlayer(p.getId()))
        expiring.push_back(p.getId());
    }
    for (const PlayerID player_id : expiring)
    {
      // The assistant meets the player's demands for an extension of the
      // seasons he wants beyond this one, within the club's means.
      const auto player = std::as_const(*gamedata).getPlayer(player_id);
      if (!player || getContractTalkKind(player_id) !=
                         TransferNegotiation::ContractKind::Renewal)
        continue;
      const std::uint8_t before = player->get().getContractYears();
      const TransferNegotiation::ContractDemand demand = getPlayerDemand(
          player_id, TransferNegotiation::ContractKind::Renewal);
      TransferNegotiation::ContractOffer offer =
          TransferNegotiation::demandedOffer(demand);
      offer.years = static_cast<std::uint8_t>(std::min<int>(
          before + std::max<int>(demand.min_years, 1),
          TransferNegotiation::maxContractYears(player->get().getAge())));
      if (offer.years <= before || !proposeContract(player_id, offer).completed)
        continue;
      inbox.add(assistantNote(
          today, InboxCategory::Contract, "INBOX_DELEGATE_RENEWED_TITLE",
          "INBOX_DELEGATE_RENEWED_BODY",
          {player->get().getName(), std::to_string(offer.years - before),
           formatMoney(offer.weekly_wage)},
          player_id));
    }
  }

  if (isDelegated(Duty::ScoutingAssignments) && dayOrdinal(today) % 7 == 0)
  {
    std::vector<std::uint32_t> busy;
    for (const ScoutAssignment& assignment : getScoutAssignments())
      if (!assignment.finished) busy.push_back(assignment.scout_id);
    std::vector<PlayerID> targets;
    for (const ShortlistEntry& entry : getShortlist())
      if (getScoutingKnowledge(entry.player_id) < SCOUT_KNOWLEDGE_TARGET)
        targets.push_back(entry.player_id);
    const std::int64_t cash = club->get().getFinances().getBalance();
    // Starting an assignment may touch the scouting state: iterate a copy.
    const std::vector<ScoutProfile> scouts = getScouts();
    for (const ScoutProfile& scout : scouts)
    {
      if (std::ranges::contains(busy, scout.id)) continue;
      ScoutTargetKind kind = ScoutTargetKind::League;
      std::uint32_t target = club->get().getLeagueId();
      std::uint16_t days = SCOUT_LEAGUE_DAYS;
      if (!targets.empty())
      {
        kind = ScoutTargetKind::Player;
        target = targets.front();
        days = SCOUT_PLAYER_DAYS;
      }
      const std::int64_t cost = getScoutAssignmentCost(kind, target, days);
      if (cost <= 0 || cost * SCOUT_CASH_DIVISOR > cash) continue;
      if (startScoutAssignment(scout.id, kind, target, days) !=
          ScoutAssignError::None)
        continue;
      if (!targets.empty()) targets.erase(targets.begin());
      inbox.add(assistantNote(
          today, InboxCategory::Transfer, "INBOX_DELEGATE_SCOUT_TITLE",
          kind == ScoutTargetKind::Player ? "INBOX_DELEGATE_SCOUT_PLAYER_BODY"
                                          : "INBOX_DELEGATE_SCOUT_LEAGUE_BODY",
          {scout.name, std::to_string(days), formatMoney(cost)}));
    }
  }

  if (isDelegated(Duty::YouthContracts))
  {
    const AcademyOverview academy = getAcademyOverview();
    const int days_left =
        dayOrdinal(academy.decision_deadline) - dayOrdinal(today);
    if (academy.candidates > 0 && days_left >= 0 &&
        days_left <= YOUTH_DECISION_LEAD_DAYS)
    {
      // Trialists whose estimated ceiling reaches the squad's level.
      float squad_level = 0.0f;
      int counted = 0;
      for (const auto& player : getPlayersForTeam(club_id))
      {
        squad_level +=
            static_cast<float>(player.get().getOverall(getStatsConfig()));
        ++counted;
      }
      if (counted > 0) squad_level /= static_cast<float>(counted);
      std::vector<std::string> signed_names;
      for (const YouthPlayerView& trialist :
           getYouthPlayers(YouthStatus::Candidate))
      {
        const float ceiling = 0.5f * (trialist.estimate.potential_low +
                                      trialist.estimate.potential_high);
        if (ceiling >= squad_level &&
            signYouthCandidate(trialist.id) == YouthActionResult::Ok)
          signed_names.push_back(trialist.name);
      }
      if (!signed_names.empty())
      {
        std::string names;
        for (const std::string& name : signed_names)
          names += (names.empty() ? "" : ", ") + name;
        inbox.add(assistantNote(today, InboxCategory::Youth,
                                "INBOX_DELEGATE_YOUTH_TITLE",
                                "INBOX_DELEGATE_YOUTH_BODY",
                                {std::to_string(signed_names.size()), names}));
      }
    }
  }
}

// ========== Opposition ==========

std::optional<GameController::NextFixture>
GameController::getNextManagedFixture() const
{
  const auto club = getManagedTeam();
  if (!club || !game) return std::nullopt;
  const TeamID club_id = club->get().getId();
  const GameDateValue today = getCurrentDate();
  for (const Match& match : getTeamFixtures(club_id))
  {
    if (match.isPlayed() || match.getDate() < today) continue;
    const bool home = match.getHomeTeamId() == club_id;
    return NextFixture{match.getDate(),
                       home ? match.getAwayTeamId() : match.getHomeTeamId(),
                       home, match.getMatchType()};
  }
  return std::nullopt;
}

namespace
{
/** Played reports of a club this season. */
std::vector<MatchReport> playedReports(const GameController& controller,
                                       TeamID team_id)
{
  std::vector<MatchReport> reports;
  for (const Match& match : controller.getTeamFixtures(team_id))
  {
    if (!match.isPlayed()) continue;
    if (auto report = controller.getMatchReport(
            match.getDate(), match.getHomeTeamId(), match.getAwayTeamId()))
      reports.push_back(std::move(*report));
  }
  return reports;
}

/** Played league reports of a league this season. */
std::vector<MatchReport> leagueReports(const GameController& controller,
                                       LeagueID league_id)
{
  std::vector<MatchReport> reports;
  const Game* game = controller.getGame();
  if (game == nullptr) return reports;
  for (const auto& [date, matches] : game->getCalendar().getFullCalendar())
  {
    for (const Match& match : matches)
    {
      if (!match.isPlayed() || match.getMatchType() != MatchType::LEAGUE)
        continue;
      if (const auto home = controller.getTeamById(match.getHomeTeamId());
          !home || home->get().getLeagueId() != league_id)
        continue;
      if (auto report = controller.getMatchReport(date, match.getHomeTeamId(),
                                                  match.getAwayTeamId()))
        reports.push_back(std::move(*report));
    }
  }
  return reports;
}
}  // namespace

OppositionReport GameController::getOppositionReport(TeamID opponent) const
{
  OppositionReport empty;
  empty.opponent = opponent;
  const auto team = getTeamById(opponent);
  if (!team || !game) return empty;

  const std::vector<MatchReport> reports = playedReports(*this, opponent);
  const std::vector<MatchReport> league =
      leagueReports(*this, team->get().getLeagueId());
  OppositionInput input;
  input.opponent = opponent;
  input.reports = reports;
  input.league_reports = league;

  std::vector<PlayerID> starters;
  const Lineup& lineup = team->get().getLineup();
  if (const Player* keeper = lineup.getGoalkeeper())
    starters.push_back(keeper->getId());
  for (const auto& positioned : lineup.getOutfieldPlayers())
    if (positioned.player != nullptr)
      starters.push_back(positioned.player->getId());

  for (const auto& player_ref : getPlayersForTeam(opponent))
  {
    const Player& player = player_ref.get();
    OppositionPlayer entry;
    entry.player = player.getId();
    entry.name = player.getName();
    entry.role = player.getRole();
    entry.likely_starter = std::ranges::contains(starters, player.getId());
    entry.right_footed = player.getFoot() == Foot::Right;
    entry.knowledge = getScoutingKnowledge(player.getId());
    if (const auto row = getScoutedRow(player.getId());
        row && row->knowledge > 0)
      entry.estimate = row->overall;
    float rating_total = 0.0f;
    int rated = 0;
    for (const PlayerSeasonStats& stats : getPlayerSeasonStats(player.getId()))
    {
      if (stats.team_id != opponent) continue;
      entry.appearances += stats.appearances;
      entry.goals += stats.goals;
      entry.assists += stats.assists;
      rating_total += stats.rating_total;
      rated += stats.rated_matches;
    }
    if (rated > 0)
      entry.average_rating = rating_total / static_cast<float>(rated);
    input.squad.push_back(std::move(entry));
  }
  return buildOppositionReport(input);
}

void GameController::markOppositionReportViewed(TeamID opponent)
{
  if (!std::ranges::contains(viewed_opposition, opponent))
    viewed_opposition.push_back(opponent);
}

bool GameController::wasOppositionReportViewed(TeamID opponent) const
{
  return std::ranges::contains(viewed_opposition, opponent);
}

bool GameController::setOppositionInstruction(TeamID opponent, PlayerID player,
                                              OppositionInstruction instruction)
{
  if (!game || !gamedata) return false;
  if (const auto found = std::as_const(*gamedata).getPlayer(player);
      !found || found->get().getTeamId() != opponent)
    return false;
  game->getGuidance().opposition.set(opponent, player, instruction);
  syncOppositionOrders();
  return true;
}

void GameController::syncOppositionOrders()
{
  if (!game) return;
  const auto club = managedClub();
  if (!club) return;
  // Orders against every planned opponent: a player belongs to one club, so
  // only the ones against this match's opponent find their man.
  std::vector<PlayerInstruction> orders;
  for (const OppositionOrder& order : game->getGuidance().opposition.all())
    orders.push_back({order.player, order.instruction});
  club->get().getStrategy().setOppositionOrders(std::move(orders));
}

OppositionInstruction GameController::getOppositionInstruction(
    TeamID opponent, PlayerID player) const
{
  return game ? game->getGuidance().opposition.get(opponent, player)
              : OppositionInstruction::None;
}

std::vector<OppositionOrder> GameController::getOppositionInstructions(
    TeamID opponent) const
{
  return game ? game->getGuidance().opposition.forOpponent(opponent)
              : std::vector<OppositionOrder>{};
}

bool GameController::applyCounterTactic(const CounterTactic& counter)
{
  const auto club = managedClub();
  if (!club) return false;
  Team& team = club->get();
  team.setStrategy(applyCounter(team.getStrategy(), counter));
  if (counter.mark != 0)
  {
    if (const auto fixture = getNextManagedFixture())
      setOppositionInstruction(fixture->opponent, counter.mark,
                               OppositionInstruction::TightMark);
  }
  return true;
}

// ========== Match snapshots and data hub ==========

void GameController::recordManagedMatch(GameDateValue date, TeamID home_id,
                                        TeamID away_id,
                                        const MatchEngine& engine)
{
  if (!game) return;
  const TeamID managed = game->getManagedTeamId();
  if (managed != home_id && managed != away_id) return;
  CareerGuidance& guidance = game->getGuidance();
  guidance.addSnapshot(
      captureSnapshot(engine, date, home_id, away_id, managed == home_id));
  guidance.onboarding.complete(OnboardingTask::PlayFirstMatch);
  // Instructions were for this meeting.
  guidance.opposition.clear(managed == home_id ? away_id : home_id);
  syncOppositionOrders();
  std::erase(viewed_opposition, managed == home_id ? away_id : home_id);
}

GameController::DataHubView GameController::getDataHub() const
{
  DataHubView view;
  const auto club = getManagedTeam();
  if (!club || !game) return view;
  const TeamID club_id = club->get().getId();
  const std::vector<MatchReport> reports = playedReports(*this, club_id);
  const std::vector<MatchReport> league =
      leagueReports(*this, club->get().getLeagueId());
  // Snapshots of this season's played fixtures only.
  std::vector<ManagedMatchSnapshot> snapshots;
  for (const ManagedMatchSnapshot& snapshot :
       game->getGuidance().getSnapshots())
  {
    const bool this_season =
        std::ranges::any_of(reports,
                            [&snapshot](const MatchReport& report)
                            {
                              return report.date == snapshot.date &&
                                     report.home_team_id == snapshot.home_id &&
                                     report.away_team_id == snapshot.away_id;
                            });
    if (this_season) snapshots.push_back(snapshot);
  }
  DataHubInput input;
  input.team_id = club_id;
  input.team_reports = reports;
  input.league_reports = league;
  input.snapshots = snapshots;
  view.team = DataHub::buildTeamAnalytics(input);
  view.players = DataHub::buildPlayerAnalytics(input);
  return view;
}

std::optional<ManagedMatchSnapshot> GameController::getMatchSnapshot(
    GameDateValue date, TeamID home_id, TeamID away_id) const
{
  if (!game) return std::nullopt;
  for (const ManagedMatchSnapshot& snapshot :
       game->getGuidance().getSnapshots())
  {
    if (snapshot.date == date && snapshot.home_id == home_id &&
        snapshot.away_id == away_id)
      return snapshot;
  }
  return std::nullopt;
}

// ========== Inbox decisions ==========

bool GameController::isInboxDecisionPending(const InboxMessage& message) const
{
  switch (Inbox::actionFor(message.title_key))
  {
    case InboxAction::RespondOffer:
      // While a buyer considers the club's counter there is nothing to do.
      return message.player_id.has_value() &&
             std::ranges::any_of(
                 getIncomingOffers(),
                 [&message](const IncomingOffer& offer)
                 {
                   return offer.player_id == *message.player_id &&
                          offer.status == OfferStatus::AwaitingClub;
                 });
    case InboxAction::ReplyToPlayer:
      return message.player_id.has_value() &&
             hasPendingTalk(*message.player_id);
    case InboxAction::YouthTrialists:
      return hasSelectedTeam() && getAcademyOverview().candidates > 0;
    case InboxAction::Dilemma:
    {
      const auto open = getOpenDilemma();
      return open && open->day == dayOrdinal(message.date) &&
             Stories::dilemmaForTitle(message.title_key) == open->kind;
    }
    case InboxAction::Shortlist:
    case InboxAction::None:
      break;
  }
  return false;
}

InboxView GameController::getInboxView() const
{
  return game ? game->getWorld().getInbox().getView() : InboxView{};
}

void GameController::setInboxView(const InboxView& view)
{
  if (game) game->getWorld().getInbox().setView(view);
}

bool GameController::followPlayer(PlayerID player_id)
{
  return game && game->getWorld().getStories().follow(player_id,
                                                      game->getCurrentDate());
}

bool GameController::unfollowPlayer(PlayerID player_id)
{
  return game && game->getWorld().getStories().unfollow(player_id);
}

bool GameController::isFollowingPlayer(PlayerID player_id) const
{
  return game && game->getWorld().getStories().isFollowed(player_id);
}

std::vector<PlayerID> GameController::getFollowedPlayers() const
{
  std::vector<PlayerID> followed;
  if (!game) return followed;
  for (const FollowedPlayer& entry : game->getWorld().getStories().getFollows())
    followed.push_back(entry.player_id);
  return followed;
}

std::optional<Dilemma> GameController::getOpenDilemma() const
{
  if (!game || !hasSelectedTeam()) return std::nullopt;
  const Dilemma* open = game->getWorld().getStories().openDilemma();
  return open ? std::optional<Dilemma>(*open) : std::nullopt;
}

std::optional<DilemmaEffects> GameController::getDilemmaEffects(
    int option) const
{
  if (!game || !hasSelectedTeam()) return std::nullopt;
  return game->getWorld().getStories().previewDilemma(option,
                                                      game->getManagedTeamId());
}

bool GameController::resolveDilemma(int option)
{
  if (!game || !hasSelectedTeam()) return false;
  WorldSimulation& world = game->getWorld();
  return world.getStories().resolveDilemma(
      option, game->getCurrentDate(), game->getManagedTeamId(),
      world.getInteractions(), world.getInbox());
}

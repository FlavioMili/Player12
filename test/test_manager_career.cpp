// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Manager career: profile, market rules, the calibration of AI sackings, and
// the full dismissal -> unemployment -> new job flow with save and reload.

#include <gtest/gtest.h>
#include <sqlite3.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/board.h"
#include "model/competition.h"
#include "model/game.h"
#include "model/inbox.h"
#include "model/manager_career.h"
#include "model/season_history.h"
#include "model/world_rng.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 500'000 + (getpid() % 100'000) * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

std::unique_ptr<GameController> makeWorld(int slot)
{
  Logger::init();
  auto controller = std::make_unique<GameController>();
  controller->newGame(slot, WORLD_SEED);
  return controller;
}

ManagerSetup setupFor(ManagerBackground background)
{
  ManagerSetup setup;
  setup.first_name = "Ada";
  setup.last_name = "Rossetti";
  setup.nationality = Language::IT;
  setup.age = 44;
  setup.background = background;
  setup.style = ManagerStyle::Pressing;
  return setup;
}

/** Top-flight club of middling reputation (a typical first job). */
TeamID middleClub(const GameController& controller, LeagueID league_id = 1)
{
  std::vector<TeamID> clubs =
      controller.getLeagueById(league_id)->get().getTeamIDs();
  std::ranges::sort(clubs,
                    [&](TeamID a, TeamID b)
                    {
                      return controller.getTeamById(a)->get().getReputation() <
                             controller.getTeamById(b)->get().getReputation();
                    });
  return clubs[clubs.size() / 2];
}

/** Clubs of the world, least reputable first. */
std::vector<TeamID> clubsByReputation(const GameController& controller)
{
  std::vector<TeamID> clubs;
  for (const auto& team : controller.getTeams())
    if (team.get().getId() != FREE_AGENTS_TEAM_ID)
      clubs.push_back(team.get().getId());
  std::ranges::sort(
      clubs,
      [&](TeamID a, TeamID b)
      {
        const auto ra = controller.getTeamById(a)->get().getReputation();
        const auto rb = controller.getTeamById(b)->get().getReputation();
        return ra != rb ? ra < rb : a < b;
      });
  return clubs;
}

/** The interview answers that fit the board best. */
std::array<std::uint8_t, INTERVIEW_TOPICS> bestAnswers(const ClubVision& vision)
{
  std::array<std::uint8_t, INTERVIEW_TOPICS> answers{};
  for (std::size_t topic = 0; topic < INTERVIEW_TOPICS; ++topic)
  {
    int best = -10;
    for (std::size_t option = 0; option < INTERVIEW_OPTIONS; ++option)
    {
      const int fit = ManagerMarketModel::interviewAnswerFit(
          vision, static_cast<InterviewTopic>(topic), option);
      if (fit > best)
      {
        best = fit;
        answers[topic] = static_cast<std::uint8_t>(option);
      }
    }
  }
  return answers;
}

bool inboxHas(const Inbox& inbox, const std::string& title_key)
{
  return std::ranges::any_of(inbox.getMessages(),
                             [&](const InboxMessage& message)
                             { return message.title_key == title_key; });
}

std::int64_t staffLedger(const Team& team, const GameDateValue& date)
{
  std::int64_t total = 0;
  for (const FinanceTransaction& entry : team.getFinances().getLedger())
    if (entry.category == FinanceCategory::Staff && entry.date == date)
      total += entry.amount;
  return total;
}

/**
 * Gets the manager a job offer from one of @p candidates (vacancies are
 * opened by the test): apply, wait for the answer, interview with the
 * answers that fit. Returns the offer id (0 if every attempt failed).
 */
std::uint32_t earnOffer(Game& game, const std::vector<TeamID>& candidates)
{
  ManagerCareer& career = game.getCareer();
  for (const TeamID club : candidates)
  {
    career.dismissClubManager(club, game.getCurrentDate(),
                              game.getWorld().getInbox());
    if (career.apply(club, game.getCurrentDate()) != ApplyResult::Ok) continue;
    for (int day = 0; day < 8; ++day)
    {
      if (career.findApplication(club)->stage != ApplicationStage::Pending)
        break;
      game.advanceDay();
    }
    if (const JobApplication* application = career.findApplication(club);
        !application || application->stage != ApplicationStage::Interview)
      continue;
    const auto answers = bestAnswers(career.visionOf(club));
    if (const auto result = career.interview(
            club, answers, game.getCurrentDate(), game.getWorld().getInbox());
        !result || !result->offered)
      continue;
    for (const JobOffer& offer : career.getOffers())
      if (offer.team_id == club) return offer.id;
  }
  return 0;
}
}  // namespace

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

TEST(ManagerCareerModel, ChanceFollowsReputationAndLicence)
{
  using namespace ManagerMarketModel;
  const float modest = applicationChance(30.0f, CoachingLicence::Pro, 60, 1);
  const float known = applicationChance(55.0f, CoachingLicence::Pro, 60, 1);
  const float star = applicationChance(80.0f, CoachingLicence::Pro, 60, 1);
  EXPECT_LT(modest, known);
  EXPECT_LT(known, star);
  EXPECT_LT(modest, 0.1f) << "a modest name rarely gets a big job";
  EXPECT_GT(star, 0.9f);
  EXPECT_LT(applicationChance(55.0f, CoachingLicence::B, 60, 1), known)
      << "a missing licence costs chances at top-flight clubs";
  EXPECT_EQ(requiredLicence(80, 1), CoachingLicence::Pro);
  EXPECT_EQ(requiredLicence(50, 2), CoachingLicence::B);
  EXPECT_EQ(reputationTier(30.0f), ReputationTier::Local);
  EXPECT_EQ(reputationTier(50.0f), ReputationTier::National);
  EXPECT_EQ(reputationTier(70.0f), ReputationTier::Continental);
  EXPECT_EQ(reputationTier(90.0f), ReputationTier::World);
  EXPECT_LT(startingReputation(ManagerBackground::SundayLeague),
            startingReputation(ManagerBackground::FormerInternational));
}

TEST(ManagerCareerModel, WagesScaleWithDivisionAndStature)
{
  using namespace ManagerMarketModel;
  const std::int64_t elite = offerWage(95, 95.0f, 1);
  EXPECT_GT(elite, 250'000);
  EXPECT_LT(elite, 450'000);
  EXPECT_GT(offerWage(60, 50.0f, 1), offerWage(60, 50.0f, 2));
  EXPECT_GT(offerWage(40, 30.0f, 2), offerWage(40, 30.0f, 3));
  EXPECT_GT(offerWage(80, 50.0f, 1), offerWage(50, 50.0f, 1));
  EXPECT_GE(offerWage(1, 1.0f, 6), 500);
}

TEST(ManagerCareerModel, HazardFollowsConfidenceHoneymoonAndOwner)
{
  using namespace ManagerMarketModel;
  EXPECT_EQ(weeklyDismissalHazard(5.0f, 3, OwnerType::Ambitious), 0.0f)
      << "never after a handful of matches";
  EXPECT_GT(weeklyDismissalHazard(20.0f, 20, OwnerType::Ambitious),
            3.0f * weeklyDismissalHazard(50.0f, 20, OwnerType::Ambitious));
  EXPECT_LT(weeklyDismissalHazard(30.0f, 6, OwnerType::Ambitious),
            weeklyDismissalHazard(30.0f, 20, OwnerType::Ambitious));
  EXPECT_GT(weeklyDismissalHazard(30.0f, 20, OwnerType::ImpatientBenefactor),
            weeklyDismissalHazard(30.0f, 20, OwnerType::Patient));
  EXPECT_GT(monthFactor(11), monthFactor(8)) << "sacking season peaks late";
  EXPECT_EQ(monthFactor(6), 0.0f);
  EXPECT_GT(seasonEndDismissalChance(18, 6, 20, 30.0f, OwnerType::Ambitious),
            seasonEndDismissalChance(6, 6, 20, 60.0f, OwnerType::Ambitious));
  // Confidence reacts to results relative to expectation, not raw points.
  float form = 0.0f;
  const float after_draw = updateConfidence(50.0f, form, 1.0f, 0.4f);
  form = 0.0f;
  const float after_win = updateConfidence(50.0f, form, 3.0f, 2.8f);
  EXPECT_GT(after_draw, after_win);
}

TEST(ManagerCareerModel, InterviewAnswersMatchTheBoard)
{
  ClubVision frugal;
  frugal.tight_budget = 0.9f;
  frugal.youth_focus = 0.8f;
  frugal.ambition = 0.3f;
  frugal.owner = OwnerType::Patient;
  ClubVision rich;
  rich.tight_budget = 0.1f;
  rich.youth_focus = 0.2f;
  rich.ambition = 0.95f;
  rich.owner = OwnerType::ImpatientBenefactor;
  using ManagerMarketModel::interviewAnswerFit;
  EXPECT_GT(interviewAnswerFit(frugal, InterviewTopic::Budget, 2),
            interviewAnswerFit(frugal, InterviewTopic::Budget, 0));
  EXPECT_GT(interviewAnswerFit(rich, InterviewTopic::Budget, 0),
            interviewAnswerFit(frugal, InterviewTopic::Budget, 0));
  EXPECT_GT(interviewAnswerFit(frugal, InterviewTopic::Youth, 0),
            interviewAnswerFit(rich, InterviewTopic::Youth, 0));
  EXPECT_GT(interviewAnswerFit(rich, InterviewTopic::Ambition, 0),
            interviewAnswerFit(rich, InterviewTopic::Ambition, 2));
}

// ---------------------------------------------------------------------------
// Market calibration through the real career code
// ---------------------------------------------------------------------------

/**
 * Six seasons of synthetic league results (drawn from the same expected
 * points the board uses) through ManagerCareer: top-flight manager changes
 * per 20 clubs and season, and the tenure of dismissed managers, against
 * CT-W26/CT-W27 (about 9 of 20 per season; dismissed tenure under a year).
 * Also measures the market's daily cost.
 */
TEST(ManagerCareerMarket, TopFlightChangesMatchRealRates)
{
  const SlotCleanup slot{uniqueSlot(0)};
  auto controller = makeWorld(slot.slot);
  std::shared_ptr<GameData> data = controller->getGameData();
  ManagerCareer career(data);
  GameDateValue date(2025, 7, 1);
  career.ensureClubManagers(date, FREE_AGENTS_TEAM_ID);
  Inbox inbox;

  std::map<LeagueID, std::vector<TeamID>> leagues;
  for (const auto& team : controller->getTeams())
    if (team.get().getId() != FREE_AGENTS_TEAM_ID)
      leagues[team.get().getLeagueId()].push_back(team.get().getId());
  std::vector<TeamID> top_flight;
  for (const auto& [league_id, clubs] : leagues)
  {
    std::ranges::sort(leagues[league_id]);
    if (Competitions::leagueTier(*data, league_id) == 1)
      top_flight.insert(top_flight.end(), clubs.begin(), clubs.end());
  }
  ASSERT_GE(top_flight.size(), 20u);
  const auto strength = [&](TeamID team_id)
  {
    return 50.0f +
           0.3f * static_cast<float>(
                      controller->getTeamById(team_id)->get().getReputation());
  };

  std::unordered_map<TeamID, int> points;
  const auto position = [&](TeamID team_id)
  {
    const auto team = controller->getTeamById(team_id);
    if (!team) return 0;
    const auto& clubs = leagues[team->get().getLeagueId()];
    int rank = 1;
    for (const TeamID other : clubs)
      if (points[other] > points[team_id] ||
          (points[other] == points[team_id] && other < team_id))
        ++rank;
    return rank;
  };

  struct Holder
  {
    std::uint32_t id = 0;
    GameDateValue appointed = GameDateValue();
  };
  std::unordered_map<TeamID, Holder> holders;
  const auto snapshot = [&]
  {
    std::unordered_map<TeamID, Holder> now;
    for (const TeamID team_id : top_flight)
      if (const AiManager* manager = career.clubManager(team_id))
        now[team_id] = {manager->id, manager->appointed};
    return now;
  };
  holders = snapshot();

  constexpr int SEASONS = 8;
  constexpr int WARMUP_SEASONS = 3;
  int changes = 0;
  std::map<LeagueID, int> changes_by_league;
  std::vector<int> tenures;
  double career_seconds = 0.0;
  int days = 0;
  WorldRng rng(4242);
  for (int season = 0; season < SEASONS; ++season)
  {
    points.clear();
    for (int day = 0; day < 365; ++day)
    {
      date.nextDay();
      const auto started = std::chrono::steady_clock::now();
      if (date.month == 7 && date.day == 1)
        career.onSeasonEnd(date, FREE_AGENTS_TEAM_ID, 0, 50.0f, position, {},
                           inbox);
      career.onDayAdvanced(date, FREE_AGENTS_TEAM_ID, 50.0f, 0, position,
                           inbox);
      career_seconds += std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started)
                            .count();
      ++days;
      // One league round a week from mid-August to mid-May.
      if (const bool round =
              dayOrdinal(date) % 7 == 3 &&
              ManagerMarketModel::monthFactor(date.month) > 0.0f &&
              !(date.month == 5 && date.day > 20);
          round)
      {
        for (const auto& [league_id, clubs] : leagues)
        {
          std::vector<TeamID> order = clubs;
          rng.shuffle(std::span<TeamID>(order));
          for (std::size_t i = 0; i + 1 < order.size(); i += 2)
          {
            const TeamID home = order[i];
            const TeamID away = order[i + 1];
            const float diff = strength(home) - strength(away) + 2.5f;
            const float draw =
                0.27f * std::exp(-(diff / 10.0f) * (diff / 10.0f));
            const float win = (1.0f - draw) / (1.0f + std::exp(-diff / 4.5f));
            const double roll = rng.uniform01();
            const int home_goals = roll < static_cast<double>(win) ? 1 : 0;
            const int away_goals =
                roll >= static_cast<double>(win + draw) ? 1 : 0;
            int home_points = 0;
            if (home_goals > away_goals)
              home_points = 3;
            else if (home_goals == away_goals)
              home_points = 1;
            points[home] += home_points;
            int away_points = 0;
            if (away_goals > home_goals)
              away_points = 3;
            else if (home_goals == away_goals)
              away_points = 1;
            points[away] += away_points;
            career.onMatchPlayed({home, away, home_goals, away_goals, true,
                                  BoardModel::expectedPoints(
                                      strength(home), strength(away), true),
                                  BoardModel::expectedPoints(
                                      strength(away), strength(home), false),
                                  FREE_AGENTS_TEAM_ID});
          }
        }
      }
      const auto now = snapshot();
      for (const auto& [team_id, before] : holders)
      {
        if (const auto after = now.find(team_id);
            after != now.end() && after->second.id == before.id)
          continue;
        if (season >= WARMUP_SEASONS)
        {
          ++changes;
          ++changes_by_league
              [controller->getTeamById(team_id)->get().getLeagueId()];
          tenures.push_back(dayOrdinal(date) - dayOrdinal(before.appointed));
        }
      }
      holders = now;
    }
  }
  const double measured_seasons = SEASONS - WARMUP_SEASONS;
  const double per_twenty = static_cast<double>(changes) / measured_seasons /
                            static_cast<double>(top_flight.size()) * 20.0;
  ASSERT_FALSE(tenures.empty());
  std::ranges::sort(tenures);
  const double median_years =
      static_cast<double>(tenures[tenures.size() / 2]) / 365.0;
  const double micros_per_day = career_seconds * 1e6 / days;
  std::printf(
      "[calibration] top-flight changes per 20 clubs and season: %.2f "
      "(target ~9, band 6-12); median tenure of departed managers %.2f "
      "years; market cost %.1f us/day\n",
      per_twenty, median_years, micros_per_day);
  EXPECT_GE(per_twenty, 6.0);
  EXPECT_LE(per_twenty, 12.0);
  // Boards follow their league's culture: Brazilian clubs change coach
  // almost every season, MLS clubs about one season in three.
  constexpr LeagueID BRAZIL = 11;
  constexpr LeagueID USA = 7;
  std::printf(
      "[calibration] coach changes per club-season: Brazil %.2f, "
      "MLS %.2f\n",
      changes_by_league[BRAZIL] / measured_seasons / 20.0,
      changes_by_league[USA] / measured_seasons / 20.0);
  EXPECT_GT(changes_by_league[BRAZIL], 2 * changes_by_league[USA]);
  EXPECT_LT(median_years, 1.3);
  EXPECT_LT(micros_per_day, 2000.0) << "Continue must stay fast";
  // Every club keeps (or soon regains) a manager.
  std::size_t vacant = career.getVacancies().size();
  EXPECT_LT(vacant, top_flight.size() / 4);
}

// ---------------------------------------------------------------------------
// Career flows
// ---------------------------------------------------------------------------

TEST(ManagerCareerFlow, NewGameManagerStepAndFirstClub)
{
  const SlotCleanup slot{uniqueSlot(1)};
  auto controller = makeWorld(slot.slot);
  EXPECT_FALSE(controller->hasCareer()) << "the manager step comes first";
  EXPECT_FALSE(controller->isUnemployed());
  controller->createManager(setupFor(ManagerBackground::FormerInternational));
  ASSERT_TRUE(controller->hasCareer());
  EXPECT_TRUE(controller->isUnemployed()) << "no club chosen yet";
  const ManagerProfile* profile = controller->getManagerProfile();
  ASSERT_NE(profile, nullptr);
  EXPECT_EQ(profile->name(), "Ada Rossetti");
  EXPECT_EQ(profile->licence, CoachingLicence::A);
  EXPECT_FLOAT_EQ(profile->reputation, 65.0f);

  // Every club has a manager before the human arrives.
  for (const auto& team : controller->getTeams())
    if (team.get().getId() != FREE_AGENTS_TEAM_ID)
      EXPECT_NE(controller->getClubManager(team.get().getId()), nullptr)
          << team.get().getName();

  const TeamID club = middleClub(*controller);
  controller->selectManagedTeam(club);
  EXPECT_FALSE(controller->isUnemployed());
  EXPECT_EQ(controller->getClubManager(club), nullptr)
      << "the human replaces the club's manager";
  ASSERT_EQ(controller->getManagerStints().size(), 1u);
  EXPECT_EQ(controller->getManagerStints()[0].team_id, club);
  EXPECT_GT(controller->getManagerProfile()->contract.weekly_wage, 0);
}

TEST(ManagerCareerFlow, ResignedManagerLosesControlAndTimeRuns)
{
  const SlotCleanup slot{uniqueSlot(2)};
  auto controller = makeWorld(slot.slot);
  controller->createManager(setupFor(ManagerBackground::ProfessionalPlayer));
  const TeamID club = middleClub(*controller);
  controller->selectManagedTeam(club);
  const PlayerID player =
      controller->getPlayersForTeam(club).front().get().getId();
  const GameDateValue start = controller->getCurrentDate();

  ASSERT_TRUE(controller->resignFromClub());
  EXPECT_TRUE(controller->isUnemployed());
  EXPECT_FALSE(controller->hasSelectedTeam());
  EXPECT_FALSE(controller->getManagedTeam().has_value());
  EXPECT_EQ(controller->getManagerStints().back().reason,
            DepartureReason::Resigned);

  // No command reaches the former club any more.
  EXPECT_FALSE(controller->setTrainingPreset(TrainingPreset::Balanced));
  EXPECT_FALSE(controller->releasePlayer(player));
  EXPECT_FALSE(controller->setLoanListed(player, true));
  EXPECT_FALSE(controller->talkToPlayer(player, TalkOption::PraiseForm));
  EXPECT_EQ(controller->getTrainingPlan(), nullptr);
  if (const auto market = controller->getStaffMarket(); !market.empty())
    EXPECT_EQ(controller->hireStaff(market.front()->id, 2),
              GameController::StaffActionResult::NoClub);
  EXPECT_TRUE(controller->getIncomingOffers().empty());
  EXPECT_EQ(controller->applyForJob(club), ApplyResult::RecentlyLeft);

  // Continue works without a club.
  const int days = controller->advanceWhileUnemployed(7);
  EXPECT_GE(days, 1);
  EXPECT_LE(days, 7);
  EXPECT_EQ(dayOrdinal(controller->getCurrentDate()) - dayOrdinal(start), days);
  EXPECT_TRUE(controller->isUnemployed());

  // The former club is managed again within a few weeks.
  for (int day = 0; day < 30 && controller->getClubManager(club) == nullptr;
       ++day)
    controller->advanceDay();
  EXPECT_NE(controller->getClubManager(club), nullptr);

  // Unemployment survives save and reload.
  ASSERT_TRUE(controller->saveGame());
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  EXPECT_TRUE(reloaded.isUnemployed());
  EXPECT_EQ(reloaded.getManagerStints().size(), 1u);
  EXPECT_EQ(reloaded.getManagerProfile()->name(), "Ada Rossetti");
}

TEST(ManagerCareerFlow, SackedManagerIsPaidOffAndFindsANewClub)
{
  const SlotCleanup slot{uniqueSlot(3)};
  auto controller = makeWorld(slot.slot);
  controller->createManager(setupFor(ManagerBackground::FormerInternational));
  const TeamID first = middleClub(*controller);
  controller->selectManagedTeam(first);
  Game& game = *controller->getGame();
  const std::int64_t wage =
      controller->getManagerProfile()->contract.weekly_wage;
  const float reputation = controller->getManagerProfile()->reputation;

  // The board dismisses him: severance, unemployment, no game over.
  const GameDateValue sacked_on = game.getCurrentDate();
  const std::int64_t staff_before =
      staffLedger(controller->getTeamById(first)->get(), sacked_on);
  game.leaveManagedTeam(DepartureReason::Sacked);
  EXPECT_TRUE(controller->isUnemployed());
  EXPECT_TRUE(
      inboxHas(game.getWorld().getInbox(), "INBOX_MANAGER_SACKED_TITLE"));
  EXPECT_EQ(staffLedger(controller->getTeamById(first)->get(), sacked_on) -
                staff_before,
            -wage * 52)
      << "a year of wages on a three-year contract";
  EXPECT_EQ(controller->getManagerProfile()->career_earnings, wage * 52);
  EXPECT_LT(controller->getManagerProfile()->reputation, reputation);
  EXPECT_EQ(game.getWorld().getBoardState().team_id, FREE_AGENTS_TEAM_ID);
  EXPECT_NE(game.getCareer().findVacancy(first), nullptr);

  for (int day = 0; day < 7; ++day) game.advanceDay();
  EXPECT_TRUE(controller->isUnemployed());

  // A modest club takes him on after an interview.
  std::vector<TeamID> candidates = clubsByReputation(*controller);
  std::erase(candidates, first);
  candidates.resize(6);
  const std::uint32_t offer_id = earnOffer(game, candidates);
  ASSERT_NE(offer_id, 0u) << "no board offered a job";
  const JobOffer offer = *game.getCareer().findOffer(offer_id);
  EXPECT_EQ(offer.compensation, 0) << "nobody to compensate";
  const OfferReply reply = controller->negotiateJobOffer(
      offer_id, offer.weekly_wage + offer.weekly_wage / 10, offer.years);
  EXPECT_NE(reply, OfferReply::Withdrawn);
  ASSERT_TRUE(controller->acceptJobOffer(offer_id));

  // Every managed-club binding moved to the new club.
  const TeamID second = offer.team_id;
  EXPECT_EQ(game.getManagedTeamId(), second);
  EXPECT_EQ(controller->getManagedTeam()->get().getId(), second);
  EXPECT_EQ(game.getWorld().getBoardState().team_id, second);
  EXPECT_GT(game.getWorld().getBoardState().expected_position, 0);
  EXPECT_EQ(game.getWorld().getScouting().getManagedTeam(), second);
  EXPECT_TRUE(controller->getShortlist().empty());
  ASSERT_NE(controller->getTrainingPlan(), nullptr);
  EXPECT_EQ(controller->getClubManager(second), nullptr);
  EXPECT_TRUE(
      inboxHas(game.getWorld().getInbox(), "INBOX_MANAGER_SACKED_TITLE"))
      << "the inbox carries on";
  EXPECT_TRUE(
      inboxHas(game.getWorld().getInbox(), "INBOX_BOARD_WELCOME_TITLE"));
  ASSERT_EQ(controller->getManagerStints().size(), 2u);
  EXPECT_EQ(controller->getManagerStints()[0].reason, DepartureReason::Sacked);
  EXPECT_EQ(controller->getManagerStints()[1].reason, DepartureReason::Current);

  // Save and reload across the transition.
  ASSERT_TRUE(controller->saveGame());
  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  EXPECT_EQ(reloaded.getManagedTeam()->get().getId(), second);
  ASSERT_EQ(reloaded.getManagerStints().size(), 2u);
  EXPECT_EQ(reloaded.getManagerStints()[0].team_id, first);
  EXPECT_EQ(reloaded.getManagerProfile()->contract.weekly_wage,
            controller->getManagerProfile()->contract.weekly_wage);
  EXPECT_EQ(reloaded.getGame()->getWorld().getBoardState().team_id, second);
  EXPECT_EQ(reloaded.getGame()->getCareer().getAiManagers().size(),
            game.getCareer().getAiManagers().size());
}

TEST(ManagerCareerFlow, PoachingPaysTheFormerClub)
{
  const SlotCleanup slot{uniqueSlot(4)};
  auto controller = makeWorld(slot.slot);
  controller->createManager(setupFor(ManagerBackground::FormerInternational));
  const std::vector<TeamID> clubs = clubsByReputation(*controller);
  const TeamID first = clubs[clubs.size() / 3];
  controller->selectManagedTeam(first);
  Game& game = *controller->getGame();
  const std::int64_t release =
      controller->getManagerProfile()->contract.release_compensation;
  ASSERT_GT(release, 0);

  std::vector<TeamID> candidates(clubs.begin(), clubs.begin() + 8);
  std::erase(candidates, first);
  // Only clubs whose transfer window is open: the new club's board bids
  // below.
  std::erase_if(candidates, [&](TeamID club)
                { return !controller->isTransferWindowOpenFor(club); });
  ASSERT_FALSE(candidates.empty()) << "no candidate club has an open window";
  const std::uint32_t offer_id = earnOffer(game, candidates);
  ASSERT_NE(offer_id, 0u);
  const JobOffer offer = *game.getCareer().findOffer(offer_id);
  EXPECT_EQ(offer.compensation, release);
  const GameDateValue today = game.getCurrentDate();
  const std::int64_t before_old =
      staffLedger(controller->getTeamById(first)->get(), today);
  const std::int64_t before_new =
      staffLedger(controller->getTeamById(offer.team_id)->get(), today);
  // A bid the new club's board placed before he arrived is withdrawn.
  const TeamID seller = clubs.back();
  const PlayerID target =
      controller->getPlayersForTeam(seller).back().get().getId();
  controller->getGameData()
      ->getTeams()
      .at(offer.team_id)
      .getFinances()
      .addBalance(1'000'000'000LL);
  // Room for his wage too: a bid needs both budgets.
  Finances& bidder_money =
      controller->getGameData()->getTeams().at(offer.team_id).getFinances();
  bidder_money.setWageBudget(controller->getWeeklyWageBill(offer.team_id) +
                             1'000'000);
  bidder_money.setTransferBudget(10'000'000);
  controller->listPlayerForTransfer(target, 500'000);
  ASSERT_TRUE(controller->submitBid(target, offer.team_id, 600'000));
  ASSERT_TRUE(controller->acceptJobOffer(offer_id));
  for (const auto& [player_id, listing] : controller->getAllListings())
    EXPECT_NE(listing.highest_bidder_id, std::optional<TeamID>(offer.team_id));
  EXPECT_EQ(
      staffLedger(controller->getTeamById(first)->get(), today) - before_old,
      release);
  EXPECT_EQ(staffLedger(controller->getTeamById(offer.team_id)->get(), today) -
                before_new,
            -release);
  EXPECT_EQ(controller->getManagerStints().front().reason,
            DepartureReason::Moved);
  EXPECT_NE(game.getCareer().findVacancy(first), nullptr)
      << "the former club looks for a new manager";
}

TEST(ManagerCareerFlow, NegotiationHasLimits)
{
  const SlotCleanup slot{uniqueSlot(5)};
  auto controller = makeWorld(slot.slot);
  controller->createManager(setupFor(ManagerBackground::FormerInternational));
  Game& game = *controller->getGame();
  std::vector<TeamID> clubs = clubsByReputation(*controller);
  clubs.resize(8);
  const std::uint32_t offer_id = earnOffer(game, clubs);
  ASSERT_NE(offer_id, 0u);
  const JobOffer offer = *game.getCareer().findOffer(offer_id);
  EXPECT_EQ(
      controller->negotiateJobOffer(offer_id, offer.max_wage * 3, offer.years),
      OfferReply::Improved);
  EXPECT_EQ(
      controller->negotiateJobOffer(offer_id, offer.max_wage * 3, offer.years),
      OfferReply::Withdrawn)
      << "outrageous demands end the talks";
  EXPECT_EQ(game.getCareer().findOffer(offer_id), nullptr);
  EXPECT_FALSE(controller->acceptJobOffer(offer_id));
}

TEST(ManagerCareerFlow, LongUnemploymentFadesReputationAndBringsCalls)
{
  const SlotCleanup slot{uniqueSlot(6)};
  auto controller = makeWorld(slot.slot);
  std::shared_ptr<GameData> data = controller->getGameData();
  ManagerCareer career(data);
  GameDateValue date(2025, 9, 1);
  career.createProfile(setupFor(ManagerBackground::TopFlightPlayer), date);
  career.ensureClubManagers(date, FREE_AGENTS_TEAM_ID);
  Inbox inbox;
  const std::vector<TeamID> clubs = clubsByReputation(*controller);
  for (std::size_t i = 0; i < 4; ++i)
    career.dismissClubManager(clubs[i * 3], date, inbox);
  const float reputation = career.getProfile().reputation;
  bool offered = false;
  for (int day = 0; day < 150 && !offered; ++day)
  {
    date.nextDay();
    const CareerDayEvents events = career.onDayAdvanced(
        date, FREE_AGENTS_TEAM_ID, 50.0f, 0, [](TeamID) { return 0; }, inbox);
    offered = events.new_offer;
  }
  EXPECT_TRUE(offered) << "clubs call a manager out of work for long";
  EXPECT_LT(career.getProfile().reputation, reputation);
  EXPECT_GE(career.getProfile().reputation,
            0.7f * ManagerMarketModel::startingReputation(
                       ManagerBackground::TopFlightPlayer));
  ASSERT_FALSE(career.getOffers().empty());
  EXPECT_TRUE(career.getOffers().front().unsolicited);
}

TEST(ManagerCareerFlow, SeasonEndRecordsHonoursAndRenewsContracts)
{
  const SlotCleanup slot{uniqueSlot(7)};
  auto controller = makeWorld(slot.slot);
  std::shared_ptr<GameData> data = controller->getGameData();
  ManagerCareer career(data);
  GameDateValue date(2025, 7, 1);
  career.createProfile(setupFor(ManagerBackground::ProfessionalPlayer), date);
  const TeamID club = middleClub(*controller);
  ManagerContract contract = career.initialContract(club, date);
  contract.expires = GameDateValue(2026, 6, 30);
  career.startJob(club, contract, date);
  const float reputation = career.getProfile().reputation;

  SeasonHistoryEntry league;
  league.season = 1;
  league.competition_type = MatchType::LEAGUE;
  league.competition_id = controller->getTeamById(club)->get().getLeagueId();
  league.champion_id = club;
  SeasonHistoryEntry cup;
  cup.season = 1;
  cup.competition_type = MatchType::CUP;
  cup.competition_id = league.competition_id;
  cup.champion_id = club;
  const std::vector<SeasonHistoryEntry> finished = {league, cup};
  Inbox inbox;
  const bool ended = career.onSeasonEnd(
      GameDateValue(2026, 7, 1), club, 10, 80.0f, [club](TeamID team_id)
      { return team_id == club ? 1 : 10; }, finished, inbox);
  EXPECT_FALSE(ended) << "a content board renews";
  EXPECT_TRUE(inboxHas(inbox, "INBOX_MANAGER_RENEWED_TITLE"));
  EXPECT_EQ(career.getProfile().contract.expires, GameDateValue(2028, 6, 30));
  ASSERT_EQ(career.getSeasons().size(), 1u);
  EXPECT_EQ(career.getSeasons()[0].position, 1);
  EXPECT_EQ(career.getSeasons()[0].start_year, 2025);
  EXPECT_EQ(career.getStints().back().trophies, 2);
  const auto has = [&](ManagerAwardKind kind)
  {
    return std::ranges::any_of(career.getAwards(),
                               [kind](const ManagerAward& award)
                               { return award.kind == kind; });
  };
  EXPECT_TRUE(has(ManagerAwardKind::LeagueTitle));
  EXPECT_TRUE(has(ManagerAwardKind::CupWin));
  EXPECT_TRUE(has(ManagerAwardKind::ManagerOfTheSeason));
  EXPECT_GT(career.getProfile().reputation, reputation + 5.0f);

  // An unhappy board lets an expiring contract run out.
  career.startJob(club, contract, date);
  EXPECT_TRUE(career.onSeasonEnd(
      GameDateValue(2026, 7, 1), club, 5, 20.0f, [](TeamID) { return 15; }, {},
      inbox));
}

TEST(ManagerCareerPersistence, RoundTripAndOlderSaves)
{
  const SlotCleanup slot{uniqueSlot(8)};
  auto controller = makeWorld(slot.slot);
  controller->createManager(setupFor(ManagerBackground::SemiProfessional));
  const TeamID club = middleClub(*controller, 3);
  controller->selectManagedTeam(club);
  Game& game = *controller->getGame();
  const std::vector<TeamID> clubs = clubsByReputation(*controller);
  game.getCareer().dismissClubManager(clubs[0], game.getCurrentDate(),
                                      game.getWorld().getInbox());
  ASSERT_EQ(game.getCareer().apply(clubs[0], game.getCurrentDate()),
            ApplyResult::Ok);
  game.getCareer().recordAward(ManagerAwardKind::ManagerOfTheMonth, 2025, club);
  ASSERT_TRUE(controller->saveGame());

  GameController reloaded;
  ASSERT_TRUE(reloaded.loadGame(slot.slot));
  const ManagerCareer& before = game.getCareer();
  const ManagerCareer& after = reloaded.getGame()->getCareer();
  EXPECT_EQ(after.getProfile().name(), before.getProfile().name());
  EXPECT_EQ(after.getProfile().nationality, Language::IT);
  EXPECT_EQ(after.getProfile().background, ManagerBackground::SemiProfessional);
  EXPECT_FLOAT_EQ(after.getProfile().reputation,
                  before.getProfile().reputation);
  EXPECT_EQ(after.getProfile().licence, before.getProfile().licence);
  EXPECT_EQ(after.getProfile().club, club);
  EXPECT_EQ(after.getProfile().contract.expires,
            before.getProfile().contract.expires);
  EXPECT_EQ(after.getStints().size(), before.getStints().size());
  EXPECT_EQ(after.getAwards().size(), 1u);
  EXPECT_EQ(after.getAiManagers().size(), before.getAiManagers().size());
  EXPECT_EQ(after.getVacancies().size(), before.getVacancies().size());
  ASSERT_EQ(after.getApplications().size(), 1u);
  EXPECT_EQ(after.getApplications()[0].team_id, clubs[0]);
  for (std::size_t i = 0; i < before.getAiManagers().size(); ++i)
  {
    EXPECT_EQ(after.getAiManagers()[i].name(),
              before.getAiManagers()[i].name());
    EXPECT_EQ(after.getAiManagers()[i].team_id,
              before.getAiManagers()[i].team_id);
  }

  // A save from before the manager market: the career gets a manager, a
  // spell at the managed club and AI managers everywhere.
  sqlite3* db = nullptr;
  ASSERT_EQ(
      sqlite3_open(RuntimePaths::savePath(slot.slot).string().c_str(), &db),
      SQLITE_OK);
  for (const char* table :
       {"ManagerProfile", "ManagerStints", "AiManagers", "ManagerVacancies",
        "ManagerApplications", "ManagerAwards", "ManagerMarketState"})
    ASSERT_EQ(
        sqlite3_exec(db, (std::string("DROP TABLE ") + table + ";").c_str(),
                     nullptr, nullptr, nullptr),
        SQLITE_OK)
        << table;
  sqlite3_close(db);
  GameController old;
  ASSERT_TRUE(old.loadGame(slot.slot));
  ASSERT_TRUE(old.hasCareer());
  EXPECT_FALSE(old.isUnemployed());
  EXPECT_FALSE(old.getManagerProfile()->name().empty());
  ASSERT_EQ(old.getManagerStints().size(), 1u);
  EXPECT_EQ(old.getManagerStints()[0].team_id, club);
  for (const auto& team : old.getTeams())
    if (team.get().getId() != FREE_AGENTS_TEAM_ID && team.get().getId() != club)
      EXPECT_NE(old.getClubManager(team.get().getId()), nullptr);
}

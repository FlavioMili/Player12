// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Youth academies: intake planning and previews, the intake cycle of the
// managed and computer-managed clubs, U18 matches and development, contract
// rules, board-funded projects, persistence and the cost of all of it.

#include <gtest/gtest.h>
#include <sqlite3.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "controller/game_controller.h"
#include "database/database_connection.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/calendar.h"
#include "model/inbox.h"
#include "model/world_generation.h"
#include "model/world_rng.h"
#include "model/world_simulation.h"
#include "model/world_tuning.h"
#include "model/youth_academy.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 600'000 + getpid() % 100'000 * 10 + offset;
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

/** The most reputable club of a league (it has a head of youth). */
TeamID topClub(const GameController& controller, LeagueID league_id)
{
  TeamID best = 0;
  for (const TeamID id :
       controller.getLeagueById(league_id)->get().getTeamIDs())
  {
    if (best == 0 || controller.getTeamById(id)->get().getReputation() >
                         controller.getTeamById(best)->get().getReputation())
      best = id;
  }
  return best;
}

/** Runs the academy alone from @p from (exclusive) to @p to (inclusive). */
void runDays(YouthAcademy& academy, GameDateValue from, GameDateValue to,
             TeamID managed, Inbox& inbox)
{
  while (from < to)
  {
    from = SeasonCalendar::addDays(from, 1);
    academy.onDayAdvanced(from, managed, inbox);
  }
}

bool hasMessage(const Inbox& inbox, const std::string& title_key)
{
  return std::ranges::any_of(inbox.getMessages(),
                             [&](const InboxMessage& message)
                             { return message.title_key == title_key; });
}

IntakeInputs baseInputs()
{
  IntakeInputs inputs;
  inputs.reputation = 70;
  inputs.country = Language::IT;
  return inputs;
}

double meanPotential(const IntakeInputs& inputs, int years)
{
  double total = 0.0;
  int count = 0;
  for (int year = 0; year < years; ++year)
  {
    const IntakeClass intake = YouthModel::planIntake(
        inputs, WORLD_SEED, static_cast<std::uint16_t>(2030 + year), 7);
    for (const CandidateProfile& candidate : intake.candidates)
    {
      total += candidate.potential;
      ++count;
    }
  }
  return total / count;
}
}  // namespace

// ---------------------------------------------------------------------------
// Intake planning
// ---------------------------------------------------------------------------

TEST(YouthIntakeTest, PlansAreDeterministicAndSizedByTheAcademy)
{
  IntakeInputs top = baseInputs();
  top.facilities = 0.9f;
  top.recruitment = 0.9f;
  top.head_ability = 0.8f;
  IntakeInputs small = baseInputs();
  small.facilities = 0.2f;
  small.recruitment = 0.2f;

  const IntakeClass first = YouthModel::planIntake(top, WORLD_SEED, 2031, 12);
  const IntakeClass again = YouthModel::planIntake(top, WORLD_SEED, 2031, 12);
  ASSERT_EQ(first.candidates.size(), again.candidates.size());
  for (std::size_t i = 0; i < first.candidates.size(); ++i)
  {
    EXPECT_EQ(first.candidates[i].role, again.candidates[i].role);
    EXPECT_FLOAT_EQ(first.candidates[i].potential,
                    again.candidates[i].potential);
  }

  double top_size = 0.0;
  double small_size = 0.0;
  for (int year = 0; year < 200; ++year)
  {
    const auto y = static_cast<std::uint16_t>(2030 + year);
    const auto big = YouthModel::planIntake(top, WORLD_SEED, y, 3).candidates;
    const auto few = YouthModel::planIntake(small, WORLD_SEED, y, 3).candidates;
    ASSERT_GE(big.size(), 3u);
    ASSERT_LE(big.size(), 15u);
    ASSERT_GE(few.size(), 3u);
    top_size += static_cast<double>(big.size());
    small_size += static_cast<double>(few.size());
    for (const CandidateProfile& candidate : big)
    {
      ASSERT_GE(candidate.age, 15);
      ASSERT_LE(candidate.age, 17);
      ASSERT_GE(candidate.potential, candidate.current);
    }
  }
  // Top academies take in 8-15 youngsters, small ones a handful. [RR 4.3]
  EXPECT_GE(top_size / 200.0, 9.0);
  EXPECT_LE(top_size / 200.0, 13.0);
  EXPECT_LT(small_size / 200.0, 7.0);
}

TEST(YouthIntakeTest, AcademyGradeScalesComputerSigningsAndSquads)
{
  IntakeInputs top = baseInputs();
  top.facilities = 0.9f;
  top.recruitment = 0.9f;
  top.junior_coaching = 0.8f;
  top.head_ability = 0.8f;
  IntakeInputs small = baseInputs();
  small.facilities = 0.2f;
  small.recruitment = 0.2f;
  small.junior_coaching = 0.2f;
  small.head_ability = 0.2f;
  const float top_grade = YouthModel::academyGrade(top);
  const float small_grade = YouthModel::academyGrade(small);
  EXPECT_GT(top_grade, 0.8f);
  EXPECT_LT(small_grade, 0.25f);
  // Each part of the academy counts.
  for (float IntakeInputs::* part :
       {&IntakeInputs::facilities, &IntakeInputs::recruitment,
        &IntakeInputs::junior_coaching, &IntakeInputs::head_ability})
  {
    IntakeInputs better = small;
    better.*part = 0.9f;
    EXPECT_GT(YouthModel::academyGrade(better), small_grade);
  }
  EXPECT_GT(YouthModel::u18SquadLimit(top_grade),
            YouthModel::u18SquadLimit(small_grade));
  EXPECT_LE(YouthModel::u18SquadLimit(1.0f), 26u);
  EXPECT_GE(YouthModel::u18SquadLimit(0.0f), 16u);

  // Signed per year: 8-12 scholars at the best academies, a handful at
  // small ones. [RR 4.3, FW 5]
  double top_signed = 0.0;
  double small_signed = 0.0;
  for (int year = 0; year < 200; ++year)
  {
    const auto y = static_cast<std::uint16_t>(2030 + year);
    const std::size_t big =
        YouthModel::planIntake(top, WORLD_SEED, y, 3).candidates.size();
    const std::size_t few =
        YouthModel::planIntake(small, WORLD_SEED, y, 3).candidates.size();
    const int top_take = YouthModel::computerSignings(top_grade, big);
    const int small_take = YouthModel::computerSignings(small_grade, few);
    ASSERT_LE(top_take, static_cast<int>(big));
    ASSERT_LE(small_take, static_cast<int>(few));
    ASSERT_GE(small_take, YouthModel::SIGN_MIN);
    top_signed += top_take;
    small_signed += small_take;
  }
  EXPECT_GE(top_signed / 200.0, 8.0);
  EXPECT_LE(top_signed / 200.0, 12.0);
  EXPECT_GE(small_signed / 200.0, 2.0);
  EXPECT_LE(small_signed / 200.0, 4.0);
}

TEST(YouthIntakeTest, QualityFollowsFacilitiesRecruitmentStaffStatureAndRegion)
{
  const IntakeInputs base = baseInputs();
  const double reference = meanPotential(base, 150);

  IntakeInputs facilities = base;
  facilities.facilities = 0.95f;
  EXPECT_GT(meanPotential(facilities, 150), reference + 2.0);

  IntakeInputs recruitment = base;
  recruitment.recruitment = 0.95f;
  EXPECT_GT(meanPotential(recruitment, 150), reference + 1.0);

  IntakeInputs staff = base;
  staff.staff_bonus = 4.0f;
  EXPECT_GT(meanPotential(staff, 150), reference + 3.0);

  IntakeInputs famous = base;
  famous.reputation = 95;
  EXPECT_GT(meanPotential(famous, 150), reference + 3.0);

  IntakeInputs rich_region = base;
  rich_region.country = Language::BR;
  IntakeInputs thin_region = base;
  thin_region.country = Language::US;
  EXPECT_GT(meanPotential(rich_region, 150), meanPotential(thin_region, 150));
}

TEST(YouthIntakeTest, IntakePotentialSitsWellBelowTheClubLevel)
{
  // An ordinary academy's group averages about 20 points under the club's
  // first-team level (plus its country's talent pool).
  const IntakeInputs base = baseInputs();
  const double expected =
      static_cast<double>(WorldGeneration::teamLevel(base.reputation)) - 20.0 +
      static_cast<double>(YouthModel::regionTalent(base.country));
  EXPECT_NEAR(meanPotential(base, 300), expected, 1.5);

  // Even the best-equipped academy of a giant finds a world-class (85+)
  // prospect less than once a year on average.
  IntakeInputs giant = base;
  giant.reputation = 95;
  giant.facilities = 0.95f;
  giant.recruitment = 0.95f;
  giant.head_ability = 1.0f;
  giant.staff_bonus = 4.0f;
  int world_class = 0;
  int candidates = 0;
  constexpr int YEARS = 300;
  for (int year = 0; year < YEARS; ++year)
  {
    const IntakeClass intake = YouthModel::planIntake(
        giant, WORLD_SEED, static_cast<std::uint16_t>(2030 + year), 11);
    for (const CandidateProfile& candidate : intake.candidates)
    {
      ++candidates;
      if (candidate.potential >= 85.0f) ++world_class;
    }
  }
  std::cout << "[youth] giant intake: " << world_class << " of " << candidates
            << " candidates at 85+ over " << YEARS << " years\n";
  EXPECT_LT(static_cast<double>(world_class) / YEARS, 1.0);
  EXPECT_GT(world_class, 0) << "the very top still turns up now and then";
}

TEST(YouthIntakeTest, GoldenGenerationsAndWonderkidsAreRare)
{
  IntakeInputs inputs = baseInputs();
  inputs.facilities = 0.7f;
  inputs.recruitment = 0.7f;
  int golden = 0;
  int weak = 0;
  int wonderkids = 0;
  int candidates = 0;
  constexpr int YEARS = 1000;
  for (int year = 0; year < YEARS; ++year)
  {
    const IntakeClass intake = YouthModel::planIntake(
        inputs, WORLD_SEED, static_cast<std::uint16_t>(1500 + year), 21);
    golden += intake.quality == IntakeQuality::Golden ? 1 : 0;
    weak += intake.quality == IntakeQuality::Weak ? 1 : 0;
    for (const CandidateProfile& candidate : intake.candidates)
    {
      ++candidates;
      if (!candidate.wonderkid) continue;
      ++wonderkids;
      EXPECT_GE(candidate.potential, 78.0f);
    }
  }
  std::cout << "[youth] golden=" << golden << " weak=" << weak
            << " wonderkids=" << wonderkids << "/" << candidates << "\n";
  EXPECT_GE(golden, YEARS / 100);
  EXPECT_LE(golden, YEARS / 8);
  EXPECT_GT(weak, golden);
  EXPECT_GE(wonderkids, 5);
  EXPECT_LE(wonderkids, candidates / 50);
}

TEST(YouthIntakeTest, ProfessionalHeadsRaiseProfessionalIntakes)
{
  const auto mean_professionalism = [](float head_professionalism)
  {
    IntakeInputs inputs = baseInputs();
    inputs.head_ability = 0.7f;
    inputs.head_professionalism = head_professionalism;
    double total = 0.0;
    int count = 0;
    for (int year = 0; year < 200; ++year)
    {
      for (const CandidateProfile& candidate :
           YouthModel::planIntake(inputs, WORLD_SEED,
                                  static_cast<std::uint16_t>(2030 + year), 9)
               .candidates)
      {
        total += candidate.traits.professionalism;
        ++count;
      }
    }
    return total / count;
  };
  EXPECT_GT(mean_professionalism(0.95f), mean_professionalism(0.05f) + 8.0);
}

TEST(YouthIntakeTest, PreviewDescribesTheGroup)
{
  const IntakeClass intake =
      YouthModel::planIntake(baseInputs(), WORLD_SEED, 2032, 5);
  const IntakePreview preview = YouthModel::summarize(intake);
  EXPECT_EQ(preview.size, intake.candidates.size());
  EXPECT_EQ(preview.quality, intake.quality);
  const auto best = std::ranges::max_element(intake.candidates, {},
                                             &CandidateProfile::potential);
  EXPECT_EQ(preview.standout[0], best->role);
  EXPECT_NE(preview.standout[1], preview.standout[0]);
  EXPECT_STREQ(preview.personality_key,
               YouthModel::personalityKey(best->traits));
}

TEST(YouthRulesTest, ContractsAgesHomegrownAndUpgrades)
{
  EXPECT_EQ(YouthModel::firstProfessionalAge(Language::EN), 17);
  EXPECT_EQ(YouthModel::firstProfessionalAge(Language::IT), 16);
  EXPECT_EQ(YouthModel::youthContractYears(15), 3);
  EXPECT_EQ(YouthModel::youthContractYears(17), 2);
  // Three academy seasons between 15 and 21.
  EXPECT_TRUE(YouthModel::isHomegrown(15, 18));
  EXPECT_FALSE(YouthModel::isHomegrown(17, 19));
  EXPECT_FALSE(YouthModel::isHomegrown(19, 25));
  EXPECT_GT(YouthModel::scholarshipWage(3), YouthModel::scholarshipWage(6));
  EXPECT_GT(YouthModel::upgradeCost(AcademyUpgrade::Facilities, 70, 3),
            YouthModel::upgradeCost(AcademyUpgrade::Facilities, 70, 6));
  EXPECT_GT(YouthModel::upgradeCost(AcademyUpgrade::Facilities, 70, 1),
            YouthModel::upgradeCost(AcademyUpgrade::Recruitment, 70, 1));
  EXPECT_GT(YouthModel::upgradeDays(AcademyUpgrade::Facilities, 60),
            YouthModel::upgradeDays(AcademyUpgrade::Recruitment, 60));
  EXPECT_EQ(YouthModel::upgradeTarget(AcademyUpgrade::Facilities, 95), 100);

  // Stronger U18 sides win more often.
  WorldRng rng(5);
  int strong_wins = 0;
  int goals = 0;
  for (int match = 0; match < 2000; ++match)
  {
    const auto [home, away] = YouthModel::simulateMatch(50.0f, 38.0f, rng);
    strong_wins += home > away ? 1 : 0;
    goals += home + away;
  }
  EXPECT_GT(strong_wins, 1000);
  EXPECT_GT(goals / 2000.0, 2.3);
  EXPECT_LT(goals / 2000.0, 4.5);
}

// ---------------------------------------------------------------------------
// The intake cycle in a world
// ---------------------------------------------------------------------------

TEST(YouthAcademyTest, IntakeCycleForManagedAndOtherClubs)
{
  const SlotCleanup slot{uniqueSlot(0)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = topClub(*controller, 1);
  YouthAcademy academy(gamedata);
  academy.ensureReady();
  Inbox inbox;

  // Existing teenagers form the U18 squads.
  std::size_t teenagers = 0;
  for (const auto& team : controller->getTeams())
  {
    for (const YouthRecord* youth :
         academy.members(team.get().getId(), YouthStatus::Squad))
    {
      ++teenagers;
      const Player& player = gamedata->getPlayer(youth->player_id)->get();
      EXPECT_LE(player.getAge(), 17);
      EXPECT_TRUE(player.isAcademyPlayer());
    }
  }
  EXPECT_GT(teenagers, 0u);

  // Preview in February from the head of youth development.
  runDays(academy, GameDateValue(2026, 1, 20), GameDateValue(2026, 2, 1),
          managed, inbox);
  ASSERT_TRUE(hasMessage(inbox, "INBOX_YOUTH_PREVIEW_TITLE"));
  const IntakePreview preview = academy.preview(managed, 2026);

  // Intake day: trialists for the manager, signings elsewhere.
  const std::size_t before = gamedata->getPlayers().size();
  const PlayerID first_new = gamedata->peekNextPlayerId();
  runDays(academy, GameDateValue(2026, 2, 1), GameDateValue(2026, 3, 15),
          managed, inbox);
  ASSERT_TRUE(hasMessage(inbox, "INBOX_YOUTH_INTAKE_TITLE"));
  const auto candidates = academy.members(managed, YouthStatus::Candidate);
  ASSERT_EQ(candidates.size(), preview.size);
  std::set<PlayerRole> roles;
  for (const YouthRecord* youth : candidates)
  {
    const Player& player = gamedata->getPlayer(youth->player_id)->get();
    roles.insert(player.getRole());
    EXPECT_EQ(player.getWage(), 0u);
    EXPECT_EQ(player.getTeamId(), managed);
    EXPECT_GE(player.getAge(), 15);
    EXPECT_LE(player.getAge(), 17);
    EXPECT_TRUE(academy.isAcademyPlayer(youth->player_id));
    const YouthEstimate judged = academy.estimate(managed, youth->player_id);
    EXPECT_LE(judged.current_low, judged.current_high);
    EXPECT_LE(judged.potential_low, judged.potential_high);
    EXPECT_GE(judged.potential_high, judged.current_high);
  }
  EXPECT_TRUE(roles.contains(preview.standout[0]));

  std::size_t signed_elsewhere = 0;
  std::size_t local = 0;
  for (const auto& team : controller->getTeams())
  {
    const TeamID team_id = team.get().getId();
    if (team_id == managed) continue;
    EXPECT_TRUE(academy.members(team_id, YouthStatus::Candidate).empty());
    for (const YouthRecord* youth :
         academy.members(team_id, YouthStatus::Squad))
    {
      if (youth->player_id < first_new) continue;
      const Player& player = gamedata->getPlayer(youth->player_id)->get();
      ++signed_elsewhere;
      EXPECT_GT(player.getWage(), 0u);
      EXPECT_NE(youth->contract, YouthContract::None);
      local +=
          player.getNationality() ==
                  leagueProfile(team.get().getLeagueId()).domestic_nationality
              ? 1
              : 0;
    }
  }
  EXPECT_GE(signed_elsewhere, 3u * (controller->getTeams().size() - 1) / 2);
  EXPECT_GT(local * 10, signed_elsewhere * 7) << "academies recruit locally";
  EXPECT_GT(gamedata->getPlayers().size(), before + signed_elsewhere);

  // Sign the best, let one go; the rest leave at the deadline.
  const PlayerID signed_id = candidates.front()->player_id;
  const PlayerID released_id = candidates.back()->player_id;
  const PlayerID ignored_id = candidates[1]->player_id;
  ASSERT_NE(released_id, ignored_id);
  EXPECT_EQ(academy.signCandidate(managed, signed_id), YouthActionResult::Ok);
  EXPECT_EQ(academy.signCandidate(managed, signed_id),
            YouthActionResult::NotAllowed);
  EXPECT_GT(gamedata->getPlayer(signed_id)->get().getWage(), 0u);
  EXPECT_NE(academy.record(signed_id)->contract, YouthContract::None);
  // Trialists picked for the bench must not stay there once they leave:
  // the line-up holds raw pointers. The matchday bench is limited, so they
  // take the first two places and the current bench fills the rest.
  Lineup& lineup = gamedata->getTeam(managed)->get().getLineup();
  const Player* released_player = &gamedata->getPlayer(released_id)->get();
  const Player* ignored_player = &gamedata->getPlayer(ignored_id)->get();
  std::vector bench = {released_player, ignored_player};
  for (const Player* reserve : lineup.getReserves())
    if (bench.size() < Lineup::MAX_SUBSTITUTES) bench.push_back(reserve);
  lineup.setReserves(bench);
  ASSERT_TRUE(std::ranges::contains(lineup.getReserves(), released_player));
  ASSERT_TRUE(std::ranges::contains(lineup.getReserves(), ignored_player));
  EXPECT_EQ(academy.releaseCandidate(managed, released_id),
            YouthActionResult::Ok);
  EXPECT_FALSE(gamedata->getPlayer(released_id).has_value());
  EXPECT_FALSE(std::ranges::contains(lineup.getReserves(), released_player));

  runDays(academy, GameDateValue(2026, 3, 15), GameDateValue(2026, 4, 14),
          managed, inbox);
  EXPECT_TRUE(hasMessage(inbox, "INBOX_YOUTH_REMINDER_TITLE"));
  EXPECT_TRUE(hasMessage(inbox, "INBOX_YOUTH_DEADLINE_TITLE"));
  EXPECT_FALSE(gamedata->getPlayer(ignored_id).has_value());
  EXPECT_FALSE(std::ranges::contains(lineup.getReserves(), ignored_player));
  // Every reference left in the line-up is a live player, so the club still
  // saves and plays.
  for (const Player* reserve : lineup.getReserves())
    EXPECT_TRUE(gamedata->getPlayer(reserve->getId()).has_value());
  ASSERT_TRUE(controller->saveGame());
  const std::vector<Match> fixtures = controller->getTeamFixtures(managed);
  ASSERT_FALSE(fixtures.empty());
  Match rehearsal = fixtures.front();
  MatchReport rehearsal_report;
  rehearsal.simulate(*gamedata, &rehearsal_report);
  EXPECT_TRUE(rehearsal.isPlayed());
  EXPECT_FALSE(rehearsal_report.players.empty());
  EXPECT_TRUE(academy.members(managed, YouthStatus::Candidate).empty());
  ASSERT_NE(academy.record(signed_id), nullptr);
  EXPECT_EQ(academy.record(signed_id)->status, YouthStatus::Squad);

  // Graduates carry names nobody else has.
  std::map<std::string, PlayerID, std::less<>> names;
  for (const auto& [id, player] : gamedata->getPlayers())
  {
    const auto [clash, fresh] = names.try_emplace(player.getName(), id);
    EXPECT_TRUE(fresh || (clash->second < first_new && id < first_new))
        << player.getName() << " " << clash->second << " / " << id;
  }
}

TEST(YouthAcademyTest, U18LeagueGivesMinutesAndSpeedsUpDevelopment)
{
  const SlotCleanup slot{uniqueSlot(1)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = topClub(*controller, 1);
  YouthAcademy academy(gamedata);
  academy.ensureReady();
  Inbox inbox;
  // Intake day, every trialist signed, then U18 football to late November.
  runDays(academy, GameDateValue(2026, 3, 14), GameDateValue(2026, 3, 15),
          managed, inbox);
  for (const YouthRecord* youth :
       academy.members(managed, YouthStatus::Candidate))
    ASSERT_EQ(academy.signCandidate(managed, youth->player_id),
              YouthActionResult::Ok);
  runDays(academy, GameDateValue(2026, 3, 15), GameDateValue(2026, 11, 30),
          managed, inbox);

  const std::vector<YouthTableRow> table =
      academy.table(controller->getTeamById(managed)->get().getLeagueId());
  ASSERT_FALSE(table.empty());
  int total_played = 0;
  int goals_for = 0;
  int goals_against = 0;
  for (const YouthTableRow& row : table)
  {
    EXPECT_EQ(row.played, row.won + row.drawn + row.lost);
    total_played += row.played;
    goals_for += row.goals_for;
    goals_against += row.goals_against;
  }
  EXPECT_GT(table.front().played, 10);
  EXPECT_EQ(goals_for, goals_against);
  EXPECT_GE(table.front().points(), table.back().points());
  EXPECT_EQ(academy.results().size(),
            std::ranges::find(table, managed, &YouthTableRow::team_id)->played);

  // The development priority gives the U18 players minutes.
  int with_minutes = 0;
  for (const YouthRecord* youth : academy.members(managed, YouthStatus::Squad))
  {
    const Player& player = gamedata->getPlayer(youth->player_id)->get();
    const float multiplier = academy.developmentMultiplier(player);
    EXPECT_GE(multiplier, 0.8f);
    EXPECT_LE(multiplier, 1.6f);
    if (youth->minutes == 0) continue;
    ++with_minutes;
    EXPECT_GT(youth->averageRating(), 3.9f);
    EXPECT_GT(multiplier, 1.0f) << "regular U18 minutes help development";
  }
  EXPECT_GE(with_minutes, 5);
  // Senior players are untouched.
  for (const auto& player : controller->getPlayersForTeam(managed))
  {
    if (!academy.isAcademyPlayer(player.get().getId()))
    {
      EXPECT_FLOAT_EQ(academy.developmentMultiplier(player.get()), 1.0f);
      break;
    }
  }

  // Monthly progress points for the managed club's youngsters.
  const auto squad = academy.members(managed, YouthStatus::Squad);
  ASSERT_FALSE(squad.empty());
  EXPECT_GE(squad.front()->progress.size(), 3u);
}

TEST(YouthAcademyTest, U18VenuesAlternateOverASeason)
{
  const SlotCleanup slot{uniqueSlot(8)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = topClub(*controller, 1);
  YouthAcademy academy(gamedata);
  academy.ensureReady();
  Inbox inbox;
  runDays(academy, GameDateValue(2026, 6, 30), GameDateValue(2027, 6, 1),
          managed, inbox);

  // Every club hosts half its matches, and each meeting with an opponent is
  // played at the other ground from the previous one.
  int home = 0;
  int played = 0;
  std::map<TeamID, std::vector<bool>> venues;
  for (const YouthResult& result : academy.results())
  {
    if (result.date < GameDateValue(2026, 7, 1)) continue;
    ++played;
    home += result.home ? 1 : 0;
    venues[result.opponent_id].push_back(result.home);
  }
  ASSERT_GT(played, 20);
  EXPECT_LE(std::abs(2 * home - played), 2)
      << home << " home matches of " << played;
  for (const auto& [opponent, sides] : venues)
  {
    ASSERT_GE(sides.size(), 2u) << "v " << opponent;
    for (std::size_t i = 1; i < sides.size(); ++i)
      EXPECT_NE(sides[i], sides[i - 1]) << "v " << opponent << " #" << i;
  }
}

TEST(YouthAcademyTest, PromotionDemotionAndProfessionalContracts)
{
  const SlotCleanup slot{uniqueSlot(2)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  // An English club: first professional contracts at 17.
  const TeamID managed = topClub(*controller, 3);
  YouthAcademy academy(gamedata);
  academy.ensureReady();
  Inbox inbox;
  runDays(academy, GameDateValue(2026, 3, 14), GameDateValue(2026, 3, 15),
          managed, inbox);

  PlayerID young = 0;
  PlayerID old = 0;
  for (const YouthRecord* youth :
       academy.members(managed, YouthStatus::Candidate))
  {
    const int age = gamedata->getPlayer(youth->player_id)->get().getAge();
    if (age <= 16 && young == 0) young = youth->player_id;
    if (age == 17 && old == 0) old = youth->player_id;
  }
  ASSERT_NE(young, 0u);
  ASSERT_EQ(academy.signCandidate(managed, young), YouthActionResult::Ok);
  EXPECT_EQ(academy.record(young)->contract, YouthContract::Scholarship);
  EXPECT_EQ(gamedata->getPlayer(young)->get().getWage(),
            YouthModel::scholarshipWage(
                controller->getTeamById(managed)->get().getLeagueId()));
  EXPECT_EQ(academy.offerProfessional(managed, young),
            YouthActionResult::TooYoung);
  EXPECT_EQ(academy.promote(managed, young), YouthActionResult::TooYoung);
  if (old != 0)
  {
    ASSERT_EQ(academy.signCandidate(managed, old), YouthActionResult::Ok);
    EXPECT_EQ(academy.record(old)->contract, YouthContract::Professional);
    EXPECT_EQ(academy.promote(managed, old), YouthActionResult::Ok);
    EXPECT_EQ(academy.record(old)->status, YouthStatus::Graduated);
    EXPECT_FALSE(academy.isAcademyPlayer(old));
    EXPECT_EQ(academy.demote(managed, old), YouthActionResult::Ok);
    EXPECT_TRUE(academy.isAcademyPlayer(old));
  }

  // Over-age first-team players cannot join the U18s.
  for (const auto& player : controller->getPlayersForTeam(managed))
  {
    if (player.get().getAge() < 22) continue;
    EXPECT_EQ(academy.demote(managed, player.get().getId()),
              YouthActionResult::TooOld);
    break;
  }
  // Another club's player is not ours to move.
  const TeamID other =
      controller->getLeagueById(3)->get().getTeamIDs().front() == managed
          ? controller->getLeagueById(3)->get().getTeamIDs()[1]
          : controller->getLeagueById(3)->get().getTeamIDs()[0];
  EXPECT_EQ(
      academy.demote(
          managed, controller->getPlayersForTeam(other).front().get().getId()),
      YouthActionResult::NotAllowed);

  // Next season: the scholar is a year older, an 18-year-old moves up.
  academy.onSeasonEnd(GameDateValue(2026, 7, 1), managed);
  gamedata->ageAllPlayers();
  academy.onSeasonStart(GameDateValue(2026, 7, 1), managed, inbox);
  EXPECT_EQ(academy.record(young)->appearances, 0);
  for (const YouthRecord* youth : academy.members(managed, YouthStatus::Squad))
    EXPECT_LE(gamedata->getPlayer(youth->player_id)->get().getAge(),
              YouthModel::U18_MAX_AGE);
}

TEST(YouthAcademyTest, BoardFundsAcademyProjectsWithinReason)
{
  const SlotCleanup slot{uniqueSlot(3)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = topClub(*controller, 1);
  Team& team = gamedata->getTeams().at(managed);
  YouthAcademy academy(gamedata);
  academy.ensureReady();
  Inbox inbox;
  const GameDateValue today(2025, 9, 1);

  // A rich club with a trusting board gets its facilities improved.
  const std::int64_t reserve =
      12 * team.getFinances().getCurrentWageSpending(*gamedata, team);
  team.getFinances().record(today, FinanceCategory::Investment,
                            reserve + 50'000'000);
  const std::uint8_t facilities = team.getProfile().youth_facilities;
  UpgradeQuote offer =
      academy.quote(managed, AcademyUpgrade::Facilities, today, 60.0f, false);
  ASSERT_EQ(offer.verdict, facilities >= 100 ? UpgradeRequestResult::AtMaximum
                                             : UpgradeRequestResult::Approved);
  EXPECT_EQ(
      academy.quote(managed, AcademyUpgrade::Facilities, today, 20.0f, false)
          .verdict,
      UpgradeRequestResult::NoConfidence);
  EXPECT_EQ(
      academy.quote(managed, AcademyUpgrade::Facilities, today, 60.0f, true)
          .verdict,
      UpgradeRequestResult::CannotAfford);

  const std::int64_t balance = team.getFinances().getBalance();
  ASSERT_EQ(academy.requestUpgrade(today, managed, AcademyUpgrade::Facilities,
                                   60.0f, false, inbox),
            UpgradeRequestResult::Approved);
  EXPECT_EQ(team.getFinances().getBalance(), balance - offer.cost);
  EXPECT_TRUE(hasMessage(inbox, "INBOX_ACADEMY_APPROVED_TITLE"));
  // One project at a time.
  EXPECT_EQ(academy.requestUpgrade(today, managed, AcademyUpgrade::Recruitment,
                                   60.0f, false, inbox),
            UpgradeRequestResult::InProgress);
  offer =
      academy.quote(managed, AcademyUpgrade::Facilities, today, 60.0f, false);
  EXPECT_GT(offer.running_done_day, dayOrdinal(today));

  // Built after the announced time.
  runDays(academy, today,
          SeasonCalendar::addDays(today,
                                  offer.running_done_day - dayOrdinal(today)),
          managed, inbox);
  EXPECT_EQ(team.getProfile().youth_facilities, offer.target);
  EXPECT_TRUE(hasMessage(inbox, "INBOX_ACADEMY_DONE_TITLE"));

  // A refusal is remembered: no new request for a while.
  const GameDateValue later = SeasonCalendar::addDays(today, 400);
  EXPECT_EQ(academy.requestUpgrade(later, managed, AcademyUpgrade::Recruitment,
                                   10.0f, false, inbox),
            UpgradeRequestResult::NoConfidence);
  EXPECT_TRUE(hasMessage(inbox, "INBOX_ACADEMY_REFUSED_TITLE"));
  EXPECT_EQ(academy.requestUpgrade(later, managed, AcademyUpgrade::Recruitment,
                                   80.0f, false, inbox),
            UpgradeRequestResult::TooSoon);

  // A small club cannot build an elite academy.
  TeamID smallest = managed;
  for (const auto& other : controller->getTeams())
  {
    if (other.get().getReputation() <
        controller->getTeamById(smallest)->get().getReputation())
      smallest = other.get().getId();
  }
  Team& small = gamedata->getTeams().at(smallest);
  ClubProfile profile = small.getProfile();
  profile.youth_facilities =
      static_cast<std::uint8_t>(std::min(95, profile.reputation + 20));
  small.setProfile(profile);
  small.getFinances().record(today, FinanceCategory::Investment, 900'000'000);
  EXPECT_EQ(
      academy.quote(smallest, AcademyUpgrade::Facilities, today, 90.0f, false)
          .verdict,
      UpgradeRequestResult::BeyondStature);
}

TEST(YouthAcademyTest, TwoSeasonsKeepSeniorSquadsAndU18sApart)
{
  const SlotCleanup slot{uniqueSlot(7)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = topClub(*controller, 1);
  WorldSimulation world(gamedata);
  // The world between matches for two seasons, with the season rollover of
  // Game::endSeason (ageing and contract expiry).
  GameDateValue date = controller->getCurrentDate();
  for (int day = 0; day < 730; ++day)
  {
    date = SeasonCalendar::addDays(date, 1);
    world.onDayAdvanced(date, managed);
    if (date.month == 7 && date.day == 1)
    {
      world.onSeasonEnd(date, managed);
      gamedata->ageAllPlayers();
      gamedata->advanceContractsAndReleasePlayers();
      world.onSeasonStart(date, managed);
    }
  }

  const YouthAcademy& academy = world.getYouth();
  std::size_t clubs = 0;
  std::size_t in_band = 0;
  double seniors_total = 0.0;
  double u18_total = 0.0;
  std::size_t largest = 0;
  for (const auto& team_ref : controller->getTeams())
  {
    const Team& team = team_ref.get();
    if (team.getId() == managed) continue;
    ++clubs;
    const std::size_t seniors = academy.firstTeamSize(team.getId());
    const auto u18 = academy.members(team.getId(), YouthStatus::Squad);
    seniors_total += static_cast<double>(seniors);
    u18_total += static_cast<double>(u18.size());
    largest = std::max(largest, seniors);
    in_band += seniors >= 23 && seniors <= 32 ? 1 : 0;
    EXPECT_LE(u18.size(), YouthModel::u18SquadLimit(YouthModel::academyGrade(
                              academy.intakeInputs(team.getId()))))
        << team.getName();
    for (const YouthRecord* youth : u18)
    {
      const Player& player = gamedata->getPlayer(youth->player_id)->get();
      EXPECT_LE(player.getAge(), YouthModel::U18_MAX_AGE);
      EXPECT_TRUE(player.isAcademyPlayer());
    }
    // Automatic line-ups pick seniors while the senior squad is big enough.
    if (seniors >= 14)
    {
      for (const auto& positioned : team.getLineup().getOutfieldPlayers())
        if (positioned.player)
          EXPECT_FALSE(positioned.player->isAcademyPlayer()) << team.getName();
    }
  }
  const double mean_seniors = seniors_total / static_cast<double>(clubs);
  const double mean_u18 = u18_total / static_cast<double>(clubs);
  std::cout << "[youth] after two seasons: senior squads mean " << mean_seniors
            << " (largest " << largest << ", " << in_band << "/" << clubs
            << " within 23-32), U18 squads mean " << mean_u18 << "\n";
  // No transfer market runs here, so squads only lose players (retirements,
  // expiring contracts) and gain promotions: the upper bound is the check.
  EXPECT_GE(mean_seniors, 22.0);
  EXPECT_LE(mean_seniors, 32.0);
  EXPECT_LE(largest, WorldTuning::Youth::AI_SQUAD_TARGET)
      << "promotions never bloat a senior squad";
  EXPECT_GE(in_band * 10, clubs * 7);
  EXPECT_GE(mean_u18, 9.0) << "U18 squads hold two intakes";
}

TEST(YouthAcademyTest, MostU18sAreScholarsAndOnlyTheBestTurnProfessional)
{
  const SlotCleanup slot{uniqueSlot(8)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = topClub(*controller, 1);
  YouthAcademy academy(gamedata);
  academy.ensureReady();
  Inbox inbox;
  // A season of the cycle: intake, renewals, ageing, promotions, and the
  // next intake.
  runDays(academy, GameDateValue(2026, 3, 1), GameDateValue(2026, 3, 15),
          managed, inbox);
  academy.onSeasonEnd(GameDateValue(2026, 7, 1), managed);
  gamedata->ageAllPlayers();
  gamedata->advanceContractsAndReleasePlayers();
  academy.onSeasonStart(GameDateValue(2026, 7, 1), managed, inbox);
  runDays(academy, GameDateValue(2027, 3, 1), GameDateValue(2027, 3, 15),
          managed, inbox);

  std::size_t u18 = 0;
  std::size_t professional = 0;
  double pro_potential = 0.0;
  double scholar_potential = 0.0;
  const StatsConfig& config = gamedata->getStatsConfig();
  for (const auto& team : controller->getTeams())
  {
    const TeamID team_id = team.get().getId();
    if (team_id == managed) continue;
    const float level = WorldGeneration::teamLevel(team.get().getReputation());
    for (const YouthRecord* youth :
         academy.members(team_id, YouthStatus::Squad))
    {
      const Player& player = gamedata->getPlayer(youth->player_id)->get();
      ++u18;
      const double headroom =
          std::max<double>(player.getPotential(), player.getOverall(config)) -
          level;
      if (youth->contract == YouthContract::Professional)
      {
        ++professional;
        pro_potential += headroom;
        // First professional contracts from 17 at computer-managed clubs.
        EXPECT_GE(player.getAge(), YouthModel::PRO_POLICY_AGE)
            << player.getName();
        EXPECT_GT(player.getWage(),
                  YouthModel::scholarshipWage(team.get().getLeagueId()));
      }
      else
      {
        EXPECT_EQ(youth->contract, YouthContract::Scholarship);
        EXPECT_EQ(player.getWage(),
                  YouthModel::scholarshipWage(team.get().getLeagueId()));
        scholar_potential += headroom;
      }
    }
  }
  ASSERT_GT(u18, 1000u);
  ASSERT_GT(professional, 0u);
  const double share =
      static_cast<double>(professional) / static_cast<double>(u18);
  std::cout << "[youth] U18 professional share " << share << " of " << u18
            << "\n";
  // Most U18s are scholars; the best prospects sign first professional
  // contracts. [RR 4, FW 5]
  EXPECT_GE(share, 0.05);
  EXPECT_LE(share, 0.30);
  EXPECT_GT(pro_potential / static_cast<double>(professional),
            scholar_potential / static_cast<double>(u18 - professional) + 5.0)
      << "professional contracts go to the best prospects";
}

TEST(YouthAcademyTest, AcademyPlayersAreListedApartFromTheSeniorSquad)
{
  const SlotCleanup slot{uniqueSlot(9)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = topClub(*controller, 1);
  YouthAcademy academy(gamedata);
  academy.ensureReady();
  Inbox inbox;
  runDays(academy, GameDateValue(2026, 3, 1), GameDateValue(2026, 3, 15),
          managed, inbox);

  std::map<TeamID, std::size_t> club_players;
  std::map<TeamID, std::int64_t> club_wages;
  for (const auto& [player_id, player] : gamedata->getPlayers())
  {
    ++club_players[player.getTeamId()];
    club_wages[player.getTeamId()] += player.getWage();
  }
  for (const auto& team_ref : controller->getTeams())
  {
    const Team& team = team_ref.get();
    const TeamID team_id = team.getId();
    const auto& seniors = gamedata->getPlayersForTeam(team_id);
    const auto& youngsters = gamedata->getAcademyForTeam(team_id);
    for (const Player& player : seniors)
      EXPECT_FALSE(player.isAcademyPlayer()) << team.getName();
    for (const Player& player : youngsters)
    {
      EXPECT_TRUE(player.isAcademyPlayer()) << team.getName();
      EXPECT_TRUE(academy.isAcademyPlayer(player.getId()));
    }
    EXPECT_EQ(youngsters.size(),
              academy.members(team_id, YouthStatus::Squad).size() +
                  academy.members(team_id, YouthStatus::Candidate).size() +
                  academy.members(team_id, YouthStatus::Reserve).size());
    EXPECT_EQ(team.getPlayerIDs().size(), seniors.size());
    EXPECT_EQ(team.getAcademyIDs().size(), youngsters.size());
    EXPECT_EQ(academy.firstTeamSize(team_id), seniors.size());
    // Academy players are still club players: all of them are paid.
    EXPECT_EQ(seniors.size() + youngsters.size(), club_players[team_id]);
    EXPECT_EQ(team.getFinances().getCurrentWageSpending(*gamedata, team),
              club_wages[team_id]);
    EXPECT_LE(seniors.size(), 32u) << team.getName();
  }

  // Promotion and demotion move a player between the two lists.
  const auto candidates = academy.members(managed, YouthStatus::Candidate);
  ASSERT_FALSE(candidates.empty());
  PlayerID oldest = 0;
  for (const YouthRecord* youth : candidates)
  {
    if (oldest == 0 || gamedata->getPlayer(youth->player_id)->get().getAge() >
                           gamedata->getPlayer(oldest)->get().getAge())
      oldest = youth->player_id;
  }
  ASSERT_EQ(academy.signCandidate(managed, oldest), YouthActionResult::Ok);
  gamedata->getPlayers().at(oldest).setAge(17);
  const Team& club = controller->getTeamById(managed)->get();
  ASSERT_EQ(academy.promote(managed, oldest), YouthActionResult::Ok);
  EXPECT_TRUE(std::ranges::contains(club.getPlayerIDs(), oldest));
  EXPECT_FALSE(std::ranges::contains(club.getAcademyIDs(), oldest));
  EXPECT_FALSE(gamedata->getPlayer(oldest)->get().isAcademyPlayer());
  ASSERT_EQ(academy.demote(managed, oldest), YouthActionResult::Ok);
  EXPECT_FALSE(std::ranges::contains(club.getPlayerIDs(), oldest));
  EXPECT_TRUE(std::ranges::contains(club.getAcademyIDs(), oldest));
  EXPECT_TRUE(std::ranges::any_of(gamedata->getAcademyForTeam(managed),
                                  [oldest](const Player& player)
                                  { return player.getId() == oldest; }));

  // A sold academy player joins the buyer's senior squad.
  const TeamID buyer =
      controller->getLeagueById(1)->get().getTeamIDs().front() == managed
          ? controller->getLeagueById(1)->get().getTeamIDs()[1]
          : controller->getLeagueById(1)->get().getTeamIDs()[0];
  gamedata->getTeam(managed)->get().removePlayerID(oldest);
  gamedata->getTeam(buyer)->get().addPlayerID(oldest);
  gamedata->transferPlayer(oldest, buyer);
  EXPECT_FALSE(gamedata->getPlayer(oldest)->get().isAcademyPlayer());
  EXPECT_FALSE(std::ranges::contains(club.getAcademyIDs(), oldest));
  EXPECT_TRUE(std::ranges::any_of(gamedata->getPlayersForTeam(buyer),
                                  [oldest](const Player& player)
                                  { return player.getId() == oldest; }));
  EXPECT_TRUE(std::ranges::none_of(gamedata->getAcademyForTeam(managed),
                                   [oldest](const Player& player)
                                   { return player.getId() == oldest; }));
}

TEST(YouthSelectionTest, ExperiencedPlayersStartAtSimilarAbility)
{
  const SlotCleanup slot{uniqueSlot(10)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const StatsConfig& config = gamedata->getStatsConfig();
  // A modest club, so the attributes have room to grow.
  TeamID modest = 0;
  for (const auto& candidate : controller->getTeams())
  {
    if (candidate.get().getId() == FREE_AGENTS_TEAM_ID) continue;
    if (modest == 0 ||
        candidate.get().getReputation() <
            controller->getTeamById(modest)->get().getReputation())
      modest = candidate.get().getId();
  }
  Team& team = gamedata->getTeam(modest)->get();
  team.generateStartingXI(*gamedata, config);

  // An experienced outfield starter and a squad player of his position who
  // becomes his teenage double.
  const Player* veteran = nullptr;
  PlayerID double_id = 0;
  for (const auto& positioned : team.getLineup().getOutfieldPlayers())
  {
    if (!positioned.player || positioned.player->getAge() < 24) continue;
    for (const PlayerID player_id : team.getPlayerIDs())
    {
      const Player& other = gamedata->getPlayer(player_id)->get();
      if (!team.getLineup().isStarter(player_id) &&
          other.getRole() == positioned.player->getRole())
        double_id = player_id;
    }
    if (double_id != 0)
    {
      veteran = positioned.player;
      break;
    }
  }
  ASSERT_NE(veteran, nullptr);
  const PlayerID veteran_id = veteran->getId();
  Player& youngster = gamedata->getPlayers().at(double_id);
  const Player& senior = gamedata->getPlayer(veteran_id)->get();
  youngster.setStats(senior.getStats());
  youngster.setAge(19);
  youngster.mutableDynamics().injury_days = 0;
  ASSERT_NEAR(youngster.getOverall(config), senior.getOverall(config), 1e-6);

  // Same ability: the experienced player starts, the teenager is on the
  // bench.
  team.generateStartingXI(*gamedata, config);
  EXPECT_TRUE(team.getLineup().isStarter(veteran_id));
  EXPECT_FALSE(team.getLineup().isStarter(double_id));
  EXPECT_TRUE(
      std::ranges::contains(team.getLineup().getReserves(), &youngster));

  // Clearly better: the teenager starts.
  std::map<std::string, float> better = senior.getStats();
  for (auto& [name, value] : better) value = std::min(99.0f, value + 12.0f);
  youngster.setStats(better);
  ASSERT_GT(youngster.getOverall(config), senior.getOverall(config) + 5.0);
  team.generateStartingXI(*gamedata, config);
  EXPECT_TRUE(team.getLineup().isStarter(double_id));
}

// ---------------------------------------------------------------------------
// Persistence and cost
// ---------------------------------------------------------------------------

TEST(YouthPersistenceTest, AcademyStateRoundTripsAndOldSavesGetSquads)
{
  const SlotCleanup slot{uniqueSlot(4)};
  auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = topClub(*controller, 1);
  YouthAcademy academy(gamedata);
  academy.ensureReady();
  Inbox inbox;
  runDays(academy, GameDateValue(2026, 3, 10), GameDateValue(2026, 4, 1),
          managed, inbox);
  const auto candidates = academy.members(managed, YouthStatus::Candidate);
  ASSERT_GE(candidates.size(), 2u);
  const PlayerID signed_id = candidates.front()->player_id;
  ASSERT_EQ(academy.signCandidate(managed, signed_id), YouthActionResult::Ok);

  const auto db = controller->getDbConn();
  academy.save(db);
  YouthAcademy restored(gamedata);
  restored.load(db);
  ASSERT_NE(restored.record(signed_id), nullptr);
  EXPECT_EQ(restored.record(signed_id)->contract,
            academy.record(signed_id)->contract);
  EXPECT_EQ(restored.members(managed, YouthStatus::Candidate).size(),
            candidates.size() - 1);
  EXPECT_EQ(restored.members(managed, YouthStatus::Squad).size(),
            academy.members(managed, YouthStatus::Squad).size());
  EXPECT_EQ(restored.results().size(), academy.results().size());
  EXPECT_EQ(restored.club(managed)->recruitment,
            academy.club(managed)->recruitment);
  EXPECT_EQ(restored.club(managed)->table.points(),
            academy.club(managed)->table.points());
  for (const YouthRecord* youth : academy.members(managed, YouthStatus::Squad))
  {
    const YouthRecord* copy = restored.record(youth->player_id);
    ASSERT_NE(copy, nullptr);
    EXPECT_EQ(copy->appearances, youth->appearances);
    EXPECT_EQ(copy->goals, youth->goals);
    ASSERT_EQ(copy->progress.size(), youth->progress.size());
    for (std::size_t i = 0; i < copy->progress.size(); ++i)
      EXPECT_NEAR(copy->progress[i].overall, youth->progress[i].overall, 0.05f);
  }

  // A save without academy rows (older build) gets U18 squads on load.
  sqlite3_exec(db->getRaw(),
               "DELETE FROM YouthAcademies; DELETE FROM YouthPlayers; DELETE "
               "FROM YouthResults;",
               nullptr, nullptr, nullptr);
  YouthAcademy legacy(gamedata);
  legacy.load(db);
  EXPECT_FALSE(legacy.members(managed, YouthStatus::Squad).empty());
  EXPECT_NE(legacy.club(managed), nullptr);
  EXPECT_EQ(gamedata->getAcademyForTeam(managed).size(),
            legacy.members(managed, YouthStatus::Squad).size());
  for (const Player& player : gamedata->getPlayersForTeam(managed))
    EXPECT_FALSE(legacy.isAcademyPlayer(player.getId()));
}

TEST(YouthPersistenceTest, ControllerActionsSurviveSaveAndLoad)
{
  const SlotCleanup slot{uniqueSlot(5)};
  auto controller = makeWorld(slot.slot);
  // A club with an 18-year-old in the first-team squad.
  TeamID managed = 0;
  for (const TeamID id : controller->getLeagueById(1)->get().getTeamIDs())
  {
    for (const auto& player : controller->getPlayersForTeam(id))
      if (player.get().getAge() == 18 && managed == 0) managed = id;
  }
  ASSERT_NE(managed, 0);
  controller->selectManagedTeam(managed);
  const auto eligible = controller->getYouthEligibleFirstTeam();
  ASSERT_FALSE(eligible.empty());
  const PlayerID moved = eligible.front().id;
  ASSERT_EQ(controller->moveToYouthSquad(moved), YouthActionResult::Ok);
  EXPECT_TRUE(controller->isAcademyPlayer(moved));
  const auto squad = controller->getYouthPlayers(YouthStatus::Squad);
  for (const auto& view : squad)
  {
    EXPECT_LE(view.estimate.potential_low, view.estimate.potential_high);
    EXPECT_NE(view.contract, YouthContract::None);
  }
  const GameController::AcademyOverview overview =
      controller->getAcademyOverview();
  EXPECT_EQ(overview.squad, squad.size());
  EXPECT_EQ(overview.intake_date, GameDateValue(2026, 3, 15));
  EXPECT_FALSE(overview.preview_ready);
  EXPECT_GT(overview.league_size, 0);
  ASSERT_TRUE(controller->saveGame());

  const auto listed = [](const auto& players, PlayerID player_id)
  {
    return std::ranges::any_of(players, [player_id](const Player& player)
                               { return player.getId() == player_id; });
  };
  const std::size_t seniors = controller->getPlayersForTeam(managed).size();
  EXPECT_FALSE(listed(controller->getPlayersForTeam(managed), moved));

  controller = std::make_unique<GameController>();
  ASSERT_TRUE(controller->loadGame(slot.slot));
  EXPECT_TRUE(controller->isAcademyPlayer(moved));
  EXPECT_EQ(controller->getYouthPlayers(YouthStatus::Squad).size(),
            squad.size());
  // The academy stays apart from the senior squad after loading.
  EXPECT_EQ(controller->getPlayersForTeam(managed).size(), seniors);
  EXPECT_FALSE(listed(controller->getPlayersForTeam(managed), moved));
  EXPECT_TRUE(
      listed(controller->getGameData()->getAcademyForTeam(managed), moved));
  EXPECT_EQ(controller->getGameData()->getAcademyForTeam(managed).size(),
            squad.size());
}

TEST(YouthPerformanceTest, AYearOfAcademiesCostsLittle)
{
  const SlotCleanup slot{uniqueSlot(6)};
  const auto controller = makeWorld(slot.slot);
  auto gamedata = controller->getGameData();
  const TeamID managed = topClub(*controller, 1);
  YouthAcademy academy(gamedata);
  academy.ensureReady();
  Inbox inbox;
  const auto started = std::chrono::steady_clock::now();
  runDays(academy, GameDateValue(2025, 7, 1), GameDateValue(2026, 7, 1),
          managed, inbox);
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  std::size_t records = 0;
  for (const auto& team : controller->getTeams())
    records += academy.members(team.get().getId(), YouthStatus::Squad).size();
  std::cout << "[youth] 365 days of academies for "
            << controller->getTeams().size() << " clubs (" << records
            << " U18 players): " << seconds * 1000.0 << " ms\n";
  EXPECT_LT(seconds, 1.5);
}

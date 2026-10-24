// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Medical centre rules (return windows, recurrence and injury risk) and the
// season agenda behind the calendar screen.

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <set>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "model/calendar.h"
#include "model/match.h"
#include "model/medical_centre.h"
#include "model/season_agenda.h"

namespace
{
constexpr std::uint64_t WORLD_SEED = 20250702;

int uniqueSlot(int offset)
{
  return 700'000 + getpid() % 100'000 * 10 + offset;
}

struct SlotCleanup
{
  explicit SlotCleanup(int saveSlot) : slot(saveSlot) {}
  SlotCleanup(const SlotCleanup&) = delete;
  SlotCleanup& operator=(const SlotCleanup&) = delete;

  int slot;
  ~SlotCleanup() { RuntimePaths::removeSave(slot); }
};

std::size_t count(const std::vector<AgendaEvent>& events, AgendaKind kind)
{
  return static_cast<std::size_t>(
      std::ranges::count(events, kind, &AgendaEvent::kind));
}
}  // namespace

TEST(MedicalCentreTest, ReturnWindowContainsTheLayoffAndNarrowsWithStaff)
{
  for (const std::uint16_t days : {1, 5, 21, 90, 240})
  {
    for (const float quality : {0.0f, 0.5f, 1.0f})
    {
      const ReturnWindow window = MedicalCentre::returnWindow(days, quality);
      EXPECT_GE(window.earliest_days, 1);
      EXPECT_LE(window.earliest_days, days);
      EXPECT_GE(window.latest_days, days);
    }
  }
  const ReturnWindow poor = MedicalCentre::returnWindow(60, 0.0f);
  const ReturnWindow elite = MedicalCentre::returnWindow(60, 1.0f);
  EXPECT_LT(elite.latest_days - elite.earliest_days,
            poor.latest_days - poor.earliest_days);
  EXPECT_EQ(MedicalCentre::returnWindow(0, 0.5f).latest_days, 0);

  StaffEffects untrained;
  EXPECT_FLOAT_EQ(MedicalCentre::staffQuality(untrained), 0.0f);
  StaffEffects elite_staff;
  elite_staff.layoff_multiplier = 0.85f;
  EXPECT_FLOAT_EQ(MedicalCentre::staffQuality(elite_staff), 1.0f);
}

TEST(MedicalCentreTest, RecurrenceRiskByDiagnosisLayoffAndAge)
{
  using MedicalCentre::reinjuryRisk;
  EXPECT_EQ(reinjuryRisk(InjuryType::None, 0, 25), RiskBand::Low);
  EXPECT_EQ(reinjuryRisk(InjuryType::KneeAcl, 200, 22), RiskBand::High);
  EXPECT_EQ(reinjuryRisk(InjuryType::Concussion, 6, 24), RiskBand::Low);
  EXPECT_EQ(reinjuryRisk(InjuryType::HamstringStrain, 12, 24),
            RiskBand::Moderate);
  EXPECT_EQ(reinjuryRisk(InjuryType::HamstringStrain, 40, 24), RiskBand::High);
  EXPECT_EQ(reinjuryRisk(InjuryType::AnkleSprain, 10, 32), RiskBand::Moderate);
}

TEST(MedicalCentreTest, RiskRisesWithEachObservableFactor)
{
  InjuryRiskInputs fresh;
  const InjuryRiskAssessment base = MedicalCentre::assess(fresh);
  EXPECT_EQ(base.band, RiskBand::Low);
  EXPECT_EQ(base.reasons, RISK_REASON_NONE);

  InjuryRiskInputs loaded = fresh;
  loaded.age = 32;
  loaded.days_since_match = 3;
  loaded.days_since_injury = 30;
  loaded.condition = 50.0f;
  loaded.workload_ratio = 1.7f;
  const InjuryRiskAssessment high = MedicalCentre::assess(loaded);
  EXPECT_EQ(high.band, RiskBand::High);
  EXPECT_EQ(high.reasons, RISK_REASON_AGE | RISK_REASON_CONGESTION |
                              RISK_REASON_RECENT_INJURY | RISK_REASON_FATIGUE |
                              RISK_REASON_WORKLOAD);
  EXPECT_GT(high.multiplier, base.multiplier);

  // Good medical staff lowers the same player's risk.
  InjuryRiskInputs cared = loaded;
  cared.staff_prevention = 0.88f;
  EXPECT_LT(MedicalCentre::assess(cared).multiplier, high.multiplier);
  // An old injury no longer counts.
  InjuryRiskInputs healed = fresh;
  healed.days_since_injury = MedicalCentre::RECURRENCE_WINDOW_DAYS + 1;
  EXPECT_EQ(MedicalCentre::assess(healed).reasons, RISK_REASON_NONE);
}

TEST(MedicalCentreTest, ReportListsInjuredAndRanksTheRest)
{
  Logger::init();
  const SlotCleanup slot{uniqueSlot(7)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID club = controller.getTeams().front().get().getId();
  controller.selectManagedTeam(club);
  // Injure two players directly.
  auto& players = controller.getGameData()->getPlayers();
  const auto& squad = controller.getPlayersForTeam(club);
  ASSERT_GE(squad.size(), 3u);
  const PlayerID short_layoff = squad[0].get().getId();
  const PlayerID long_layoff = squad[1].get().getId();
  players.at(short_layoff).mutableDynamics().injury = InjuryType::AnkleSprain;
  players.at(short_layoff).mutableDynamics().injury_days = 6;
  players.at(long_layoff).mutableDynamics().injury = InjuryType::KneeAcl;
  players.at(long_layoff).mutableDynamics().injury_days = 180;

  const MedicalReport report = controller.getMedicalReport();
  ASSERT_EQ(report.injured.size(), 2u);
  EXPECT_EQ(report.injured[0].player_id, long_layoff);
  EXPECT_EQ(report.injured[0].severity, InjurySeverity::Major);
  EXPECT_EQ(report.injured[0].reinjury, RiskBand::High);
  EXPECT_EQ(report.injured[1].player_id, short_layoff);
  EXPECT_EQ(report.squad.size() + report.injured.size(), squad.size());
  EXPECT_TRUE(std::ranges::is_sorted(report.squad, std::greater<>(),
                                     [](const MedicalRiskRow& row)
                                     { return row.risk.multiplier; }));
  for (const StaffMember* member : report.medical_staff)
    EXPECT_TRUE(member->role == StaffRole::Physio ||
                member->role == StaffRole::SportsScientist);
  EXPECT_GT(report.average_condition, 0.0f);
}

TEST(SeasonAgendaTest, SeasonListsEveryKindOfEventInOrder)
{
  const std::vector<Match> none;
  // An Italian club (league 1).
  const std::vector<AgendaEvent> agenda =
      SeasonAgenda::build(2025, none, true, 1);
  ASSERT_FALSE(agenda.empty());
  EXPECT_EQ(agenda.front().kind, AgendaKind::SeasonStart);
  EXPECT_TRUE(agenda.front().date == GameDateValue(2025, 7, 1));
  EXPECT_TRUE(std::ranges::is_sorted(
      agenda, [](const AgendaEvent& a, const AgendaEvent& b)
      { return a.date < b.date; }));
  // The summer window closes on 1 September, the winter one runs from
  // 2 January to 2 February, and the next summer window opens in June.
  EXPECT_EQ(count(agenda, AgendaKind::TransferDeadline), 2u);
  EXPECT_EQ(count(agenda, AgendaKind::TransferWindowOpens), 2u);
  EXPECT_EQ(count(agenda, AgendaKind::InternationalBreak),
            SeasonCalendar::internationalWindows(2025).size());
  EXPECT_GE(count(agenda, AgendaKind::InternationalBreak), 4u);
  EXPECT_EQ(count(agenda, AgendaKind::WinterBreak), 1u);
  EXPECT_EQ(count(agenda, AgendaKind::BoardReview), 9u);
  EXPECT_EQ(count(agenda, AgendaKind::ContractReminder), 2u);
  EXPECT_EQ(count(agenda, AgendaKind::YouthPreview), 1u);
  EXPECT_EQ(count(agenda, AgendaKind::YouthIntake), 1u);
  EXPECT_EQ(count(agenda, AgendaKind::YouthDecisionDeadline), 1u);
  for (const AgendaEvent& event : agenda)
  {
    if (event.kind == AgendaKind::InternationalBreak ||
        event.kind == AgendaKind::WinterBreak)
      EXPECT_GT(event.span_days, 3);
    EXPECT_FALSE(event.date < GameDateValue(2025, 7, 1));
    EXPECT_TRUE(event.date < GameDateValue(2026, 7, 1));
  }
  EXPECT_EQ(
      count(SeasonAgenda::build(2025, none, false, 1), AgendaKind::BoardReview),
      0u);
}

TEST(SeasonAgendaTest, ControllerAgendaCarriesTheClubsFixtures)
{
  Logger::init();
  const SlotCleanup slot{uniqueSlot(8)};
  GameController controller;
  controller.newGame(slot.slot, WORLD_SEED);
  const TeamID club = controller.getTeams().front().get().getId();
  controller.selectManagedTeam(club);
  const std::vector<AgendaEvent> agenda = controller.getSeasonAgenda();
  std::set<MatchType> types;
  std::size_t fixtures = 0;
  for (const AgendaEvent& event : agenda)
  {
    if (event.kind != AgendaKind::Fixture) continue;
    ++fixtures;
    types.insert(event.match_type);
    EXPECT_TRUE(event.home_id == club || event.away_id == club);
  }
  EXPECT_EQ(fixtures, controller.getTeamFixtures(club).size());
  EXPECT_TRUE(types.contains(MatchType::LEAGUE));
  EXPECT_TRUE(types.contains(MatchType::FRIENDLY));
}

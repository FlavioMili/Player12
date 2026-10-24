// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/board.h"

#include <algorithm>
#include <cmath>

#include "model/world_tuning.h"

namespace
{
// Home advantage expressed in overall-rating points. [P]
constexpr float HOME_ADVANTAGE = 2.5f;
// Rating difference that moves the win probability by one logistic unit. [P]
constexpr float STRENGTH_SCALE = 4.5f;
// Draw probability for evenly matched sides. [P] ~27% of league matches.
constexpr float EVEN_DRAW_PROBABILITY = 0.27f;
// Standard deviation of (points - expected points) per match. [P]
constexpr float POINTS_STDDEV = 1.2f;

float blend(float confidence, float signal)
{
  return std::clamp(0.9f * confidence + 0.1f * signal, 0.0f, 100.0f);
}
}  // namespace

namespace BoardModel
{
BoardObjective objectiveFor(int expected_position, int league_size)
{
  using enum BoardObjective;
  if (expected_position <= 1) return WinLeague;
  if (expected_position <= 4) return TopFour;
  if (expected_position <= league_size / 2) return TopHalf;
  if (expected_position <= league_size - 5) return MidTable;
  return AvoidRelegation;
}

std::uint8_t targetPosition(BoardObjective objective, int league_size)
{
  using enum BoardObjective;
  int target = league_size;
  switch (objective)
  {
    case WinLeague:
      target = 2;  // Finishing second still keeps the board broadly happy.
      break;
    case TopFour:
      target = 4;
      break;
    case TopHalf:
      target = league_size / 2;
      break;
    case MidTable:
      target = league_size - 5;
      break;
    case AvoidRelegation:
      target = league_size - 3;
      break;
  }
  return static_cast<std::uint8_t>(std::clamp(target, 1, 255));
}

const char* objectiveKey(BoardObjective objective)
{
  using enum BoardObjective;
  switch (objective)
  {
    case WinLeague:
      return "BOARD_OBJ_WIN_LEAGUE";
    case TopFour:
      return "BOARD_OBJ_TOP_FOUR";
    case TopHalf:
      return "BOARD_OBJ_TOP_HALF";
    case MidTable:
      return "BOARD_OBJ_MID_TABLE";
    case AvoidRelegation:
      return "BOARD_OBJ_AVOID_RELEGATION";
  }
  return "BOARD_OBJ_MID_TABLE";
}

float expectedPoints(float own_strength, float opponent_strength, bool home)
{
  const float diff = own_strength - opponent_strength +
                     (home ? HOME_ADVANTAGE : -HOME_ADVANTAGE);
  const float draw =
      EVEN_DRAW_PROBABILITY * std::exp(-(diff / 10.0f) * (diff / 10.0f));
  const float win = (1.0f - draw) / (1.0f + std::exp(-diff / STRENGTH_SCALE));
  return 3.0f * win + draw;
}

void recordMatch(BoardState& state, float points, float expected_points)
{
  auto& deltas = state.recent_deltas;
  std::shift_right(deltas.begin(), deltas.end(), 1);
  deltas[0] = points - expected_points;
  if (state.result_count < deltas.size()) ++state.result_count;
  ++state.league_matches;

  float total = 0.0f;
  for (std::size_t i = 0; i < state.result_count; ++i) total += deltas[i];
  const float z = std::clamp(
      total /
          (POINTS_STDDEV * std::sqrt(static_cast<float>(state.result_count))),
      -2.0f, 2.0f);
  state.confidence = blend(state.confidence, 50.0f + 25.0f * z);
}

BoardReviewOutcome monthlyReview(BoardState& state, int position,
                                 int league_size, bool negative_balance,
                                 bool over_wage_budget, bool in_season)
{
  using Tuning = WorldTuning::Board;
  using enum BoardReviewOutcome;
  if (state.dismissed) return Dismissed;

  const float quarter = std::max(1.0f, static_cast<float>(league_size) / 4.0f);
  float signal = 50.0f;
  if (in_season && state.league_matches > 0)
  {
    signal += 25.0f *
              std::clamp(static_cast<float>(state.target_position - position) /
                             quarter,
                         -2.0f, 2.0f);
  }
  if (negative_balance) signal -= 25.0f;
  if (over_wage_budget) signal -= 12.0f;
  state.confidence = blend(state.confidence, signal);
  if (!in_season)
    return state.confidence < Tuning::WARNING_THRESHOLD ? Warning : Satisfied;

  if (state.confidence < Tuning::DISMISSAL_THRESHOLD)
    ++state.low_reviews;
  else
    state.low_reviews = 0;

  if (state.low_reviews >= Tuning::DISMISSAL_REVIEWS &&
      state.league_matches >= Tuning::MIN_MATCHES_FOR_DISMISSAL)
  {
    state.dismissed = true;
    return Dismissed;
  }
  return state.confidence < Tuning::WARNING_THRESHOLD ? Warning : Satisfied;
}

void seasonReview(BoardState& state, int final_position)
{
  auto margin = static_cast<float>(state.target_position - final_position);
  adjustConfidence(state, std::clamp(margin * 3.0f, -20.0f, 15.0f));
}

void adjustConfidence(BoardState& state, float delta)
{
  state.confidence = std::clamp(state.confidence + delta, 0.0f, 100.0f);
}

CupObjective cupObjectiveFor(BoardObjective objective, int tier)
{
  using enum BoardObjective;
  using enum CupObjective;
  // Only the top division's leading clubs are expected to go deep. [P]
  if (tier > 1) return None;
  switch (objective)
  {
    case WinLeague:
      return SemiFinal;
    case TopFour:
      return QuarterFinal;
    case TopHalf:
    case MidTable:
    case AvoidRelegation:
      break;
  }
  return None;
}

FinanceObjective financeObjectiveFor(float tight_budget, std::int64_t balance)
{
  return tight_budget >= 0.5f || balance < 0
             ? FinanceObjective::BreakEven
             : FinanceObjective::WithinWageBudget;
}

std::uint8_t youthTargetFor(float youth_focus)
{
  if (youth_focus >= 0.75f) return 3;
  if (youth_focus >= 0.6f) return 2;
  return 0;
}

const char* cupObjectiveKey(CupObjective objective)
{
  using enum CupObjective;
  switch (objective)
  {
    case None:
      return "BOARD_CUP_NONE";
    case QuarterFinal:
      return "BOARD_CUP_QUARTER_FINAL";
    case SemiFinal:
      return "BOARD_CUP_SEMI_FINAL";
    case Final:
      return "BOARD_CUP_FINAL";
    case Win:
      break;
  }
  return "BOARD_CUP_WIN";
}

const char* financeObjectiveKey(FinanceObjective objective)
{
  return objective == FinanceObjective::BreakEven ? "BOARD_FINANCE_BREAK_EVEN"
                                                  : "BOARD_FINANCE_WAGE_BUDGET";
}

const char* youthTargetKey(std::uint8_t target)
{
  if (target == 0) return "BOARD_YOUTH_NONE";
  return target >= 3 ? "BOARD_YOUTH_TARGET_3" : "BOARD_YOUTH_TARGET_2";
}

ObjectiveGrade gradeCup(CupObjective objective, std::optional<int> rounds_left,
                        bool won, bool still_in)
{
  using enum CupObjective;
  using enum ObjectiveGrade;
  if (won) return Exceeded;
  if (objective == None)
    return rounds_left && *rounds_left == 0 ? Exceeded : Met;
  // Rounds still to come after the one the target asks to reach.
  int required = 0;
  switch (objective)
  {
    case QuarterFinal:
      required = 2;
      break;
    case SemiFinal:
      required = 1;
      break;
    case None:
    case Final:
    case Win:
      break;
  }
  const bool reached = rounds_left && *rounds_left <= required;
  if (objective == Win)
  {
    if (still_in) return Met;
    return reached ? Missed : Failed;
  }
  if (reached) return *rounds_left < required ? Exceeded : Met;
  if (still_in) return Met;
  return rounds_left && *rounds_left == required + 1 ? Missed : Failed;
}

const char* targetStatusKey(ObjectiveGrade grade)
{
  using enum ObjectiveGrade;
  switch (grade)
  {
    case Exceeded:
      return "BOARD_STATUS_AHEAD";
    case Met:
      return "BOARD_STATUS_ON_TRACK";
    case Missed:
      return "BOARD_STATUS_BEHIND";
    case Failed:
      break;
  }
  return "BOARD_STATUS_OFF_TRACK";
}

ObjectiveGrade gradeLeague(int target_position, int position, int league_size)
{
  using enum ObjectiveGrade;
  const float quarter = std::max(1.0f, static_cast<float>(league_size) / 4.0f);
  const float margin = static_cast<float>(target_position - position) / quarter;
  if (margin >= 1.0f) return Exceeded;
  if (margin >= 0.0f) return Met;
  if (margin >= -1.0f) return Missed;
  return Failed;
}

ObjectiveGrade gradeFinances(FinanceObjective objective, std::int64_t balance,
                             std::int64_t start_balance, bool over_wage_budget)
{
  using enum ObjectiveGrade;
  if (balance < 0 &&
      !(objective == FinanceObjective::BreakEven && balance >= start_balance))
    return Failed;
  if (objective == FinanceObjective::BreakEven)
  {
    if (balance < start_balance) return Missed;
    // A clear profit on a tight budget is beyond what was asked. [P]
    const std::int64_t margin =
        std::max<std::int64_t>(1'000'000, std::abs(start_balance) / 5);
    return balance - start_balance >= margin && !over_wage_budget ? Exceeded
                                                                  : Met;
  }
  return over_wage_budget ? Missed : Met;
}

ObjectiveGrade gradeYouth(std::uint8_t target, int young_regulars)
{
  using enum ObjectiveGrade;
  if (target == 0) return young_regulars >= 3 ? Exceeded : Met;
  if (young_regulars >= target + 2) return Exceeded;
  if (young_regulars >= target) return Met;
  return young_regulars > 0 ? Missed : Failed;
}
}  // namespace BoardModel

// ---------------------------------------------------------------------------
// Facility projects
// ---------------------------------------------------------------------------

namespace BoardModel
{
ProjectQuote quoteProject(FacilityProjectType type, std::uint8_t current_level,
                          std::uint32_t capacity, std::uint8_t reputation,
                          double season_income, std::uint32_t seats)
{
  using enum FacilityProjectType;
  ProjectQuote quote;
  quote.type = type;
  const double level_factor = 1.0 + static_cast<double>(current_level) / 100.0;
  switch (type)
  {
    case TrainingGround:
    case MedicalCentre:
    {
      const bool training = type == TrainingGround;
      quote.amount = static_cast<std::uint32_t>(
          std::clamp(100 - static_cast<int>(current_level), 0, 10));
      // [P] A modern training centre costs a few percent of a season's
      // income per step; medical wings less.
      quote.cost = static_cast<std::int64_t>(std::llround(
          season_income * (training ? 0.06 : 0.035) * level_factor));
      quote.days = training ? 180 : 120;
      break;
    }
    case StadiumExpansion:
    case COUNT:
    {
      const std::uint32_t room = capacity >= MAX_STADIUM_CAPACITY
                                     ? 0
                                     : MAX_STADIUM_CAPACITY - capacity;
      const std::uint32_t limit = std::min(
          room, std::max<std::uint32_t>(MIN_EXPANSION_SEATS, capacity / 2));
      std::uint32_t added =
          std::min(std::max(seats, MIN_EXPANSION_SEATS), limit);
      added -= added % 500;
      quote.amount = added;
      // [P] Expansions cost EUR 2,500-8,500 per seat with the club's stature.
      const double per_seat = 2'500.0 + 60.0 * static_cast<double>(reputation);
      quote.cost = static_cast<std::int64_t>(
          std::llround(per_seat * static_cast<double>(added)));
      quote.days =
          static_cast<std::uint16_t>(240 + std::min(added, 9'000U) / 50);
      quote.disruption = std::min(added, capacity / 10);
      break;
    }
  }
  if (quote.amount == 0) quote.cost = 0;
  return quote;
}

ProjectVerdict reviewProject(const ProjectQuote& quote,
                             const ProjectRequestContext& context)
{
  using enum ProjectVerdict;
  if (context.same_type_running) return AlreadyRunning;
  if (context.running_projects >= MAX_RUNNING_PROJECTS) return TooManyProjects;
  if (context.cooling_down) return Cooldown;
  if (quote.amount == 0) return AtMaximum;
  // Facilities far above the club's stature do not pay back. [P]
  if (quote.type != FacilityProjectType::StadiumExpansion &&
      context.current_level >= context.reputation + 25)
    return NotNeeded;
  if (context.confidence < PROJECT_MIN_CONFIDENCE) return LowConfidence;
  if (context.balance - quote.cost <
      PROJECT_CASH_RESERVE_WEEKS * context.weekly_payroll)
    return CannotAfford;
  return Approved;
}

float medicalLayoffMultiplier(std::uint8_t medical_level)
{
  return 1.0f - 0.003f * static_cast<float>(
                             std::max(0, static_cast<int>(medical_level) - 50));
}

const char* projectTypeKey(FacilityProjectType type)
{
  using enum FacilityProjectType;
  switch (type)
  {
    case TrainingGround:
      return "PROJECT_TRAINING_GROUND";
    case MedicalCentre:
      return "PROJECT_MEDICAL_CENTRE";
    case StadiumExpansion:
    case COUNT:
      break;
  }
  return "PROJECT_STADIUM_EXPANSION";
}

const char* projectVerdictKey(ProjectVerdict verdict)
{
  using enum ProjectVerdict;
  switch (verdict)
  {
    case Approved:
      return "PROJECT_VERDICT_APPROVED";
    case AlreadyRunning:
      return "PROJECT_VERDICT_RUNNING";
    case TooManyProjects:
      return "PROJECT_VERDICT_TOO_MANY";
    case Cooldown:
      return "PROJECT_VERDICT_COOLDOWN";
    case AtMaximum:
      return "PROJECT_VERDICT_MAXIMUM";
    case NotNeeded:
      return "PROJECT_VERDICT_NOT_NEEDED";
    case LowConfidence:
      return "PROJECT_VERDICT_CONFIDENCE";
    case CannotAfford:
      break;
  }
  return "PROJECT_VERDICT_AFFORD";
}
}  // namespace BoardModel

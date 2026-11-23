// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/manager_career.h"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <utility>

#include "database/database_connection.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "model/competition.h"
#include "model/inbox.h"
#include "model/season_history.h"
#include "model/team.h"
#include "model/world_generation.h"
#include "model/world_rng.h"
#include "model/world_tuning.h"

namespace
{
// ---- Market tuning [P] ----------------------------------------------------

/** Weekly dismissal hazard at neutral confidence (B = 50). */
constexpr float BASE_WEEKLY_HAZARD = 0.0052f;
/** Log-hazard change per 25 points of confidence. */
constexpr float HAZARD_BETA = 1.9f;
/** Coach changes per club-season the hazards produce at a league factor of
 * one; each league scales them to its own turnover (LeagueProfile). */
constexpr float REFERENCE_COACH_CHANGES = 0.5f;
/** Matches before the honeymoon damping is gone. */
constexpr std::uint16_t HONEYMOON_MATCHES = 10;
/** League matches before a board sacks anyone mid-season. */
constexpr std::uint16_t MIN_MATCHES_FOR_DISMISSAL = 5;
/** Smoothing of the points-minus-expected form. */
constexpr float FORM_ALPHA = 0.25f;
/** Standard deviation of the smoothed form for i.i.d. results. */
constexpr float FORM_STDDEV = 0.45f;
/** Confidence of a newly appointed manager. */
constexpr float INITIAL_CONFIDENCE = 60.0f;

/** Days a board takes to appoint after a vacancy opens. */
constexpr int MIN_VACANCY_DAYS = 6;
constexpr int MAX_VACANCY_DAYS = 24;
/** An interview or open offer holds the appointment back this long. */
constexpr int HOLD_DAYS = 4;
constexpr int OFFER_VALID_DAYS = 10;
constexpr int MIN_RESPONSE_DAYS = 2;
constexpr int MAX_RESPONSE_DAYS = 5;
constexpr std::uint8_t NEGOTIATION_ROUNDS = 3;

/** Weekly reputation fade while out of work (about four points a year). */
constexpr float UNEMPLOYED_DECAY = 0.08f;
/** Out of work this long, clubs start calling. */
constexpr int UNSOLICITED_AFTER_DAYS = 21;
/** Out of work this long, a struggling lower-division club makes room. */
constexpr int DESPERATE_AFTER_DAYS = 90;

constexpr int AI_RETIREMENT_AGE = 70;
constexpr float SACKED_REPUTATION_HIT = 3.0f;
constexpr float RESIGNED_REPUTATION_HIT = 1.5f;

/** Licence ladder: days of head coaching needed for the next level. */
constexpr std::array<std::uint16_t, 4> LICENCE_DAYS = {60, 180, 365, 540};

/** Sacking season: in-season hazard multiplier by calendar month. [S] */
constexpr std::array<float, 13> MONTH_FACTOR = {0.0f, 1.2f, 1.0f, 1.0f, 0.9f,
                                                0.7f, 0.0f, 0.0f, 0.5f, 0.8f,
                                                1.1f, 1.5f, 1.5f};

/** Best weekly wage per division in whole euros. [S] (tier 5+ = last) */
constexpr std::array<double, 4> TIER_TOP_WAGE = {350'000.0, 25'000.0, 6'000.0,
                                                 2'500.0};

std::uint64_t seedOf(const GameData& gamedata)
{
  return gamedata.getWorldSeed();
}

double uniform(const GameData& gamedata, std::int64_t a, std::uint64_t b)
{
  return WorldRng::hashUniform(seedOf(gamedata), RngDomain::Managers,
                               static_cast<std::uint64_t>(a), b);
}

int daysBetween(const GameDateValue& from, const GameDateValue& to)
{
  return dayOrdinal(to) - dayOrdinal(from);
}

GameDateValue addDays(const GameDateValue& date, int days)
{
  return days >= 0 ? date + static_cast<std::size_t>(days)
                   : date - static_cast<std::size_t>(-days);
}

/** 30 June of the season that starts @p years - 1 years after @p date's. */
GameDateValue seasonEnd(const GameDateValue& date, int years)
{
  const int start_year = date.month >= 7 ? date.year : date.year - 1;
  return GameDateValue(static_cast<std::uint16_t>(start_year + years), 6, 30);
}

bool inSeason(const GameDateValue& date)
{
  return MONTH_FACTOR[date.month] > 0.0f;
}

/** Boards in Brazil or Serie B change coach almost every season, MLS
 * boards about one season in three. [S: LMA, CIES coach tenure reports] */
float leagueTurnover(const GameData& gamedata, TeamID team_id)
{
  const auto team = gamedata.getTeam(team_id);
  if (!team) return 1.0f;
  return leagueProfile(team->get().getLeagueId()).coach_changes /
         REFERENCE_COACH_CHANGES;
}

float ownerFactor(OwnerType owner)
{
  using enum OwnerType;
  switch (owner)
  {
    case Patient:
      return 0.7f;
    case Ambitious:
      return 1.15f;
    case ImpatientBenefactor:
      return 1.7f;
    case MemberOwned:
      return 0.85f;
  }
  return 1.0f;
}

struct InboxNotice
{
  InboxCategory category;
  const char* title_key;
  const char* body_key;
  std::vector<std::string> args;
  TeamID team_id;
  bool read = false;
};

void post(Inbox& inbox, const GameDateValue& date, InboxNotice notice)
{
  InboxMessage message;
  message.date = date;
  message.category = notice.category;
  message.title_key = notice.title_key;
  message.body_key = notice.body_key;
  message.args = std::move(notice.args);
  if (notice.team_id != FREE_AGENTS_TEAM_ID) message.team_id = notice.team_id;
  message.read = notice.read;
  inbox.add(std::move(message));
}

int expectedLeaguePosition(
    const GameData& gamedata,
    const std::map<LeagueID, std::vector<std::pair<std::uint8_t, TeamID>>>&
        league_ranks,
    TeamID team_id, int& league_size)
{
  const auto team = gamedata.getTeam(team_id);
  if (!team) return 0;
  const auto& clubs = league_ranks.at(team->get().getLeagueId());
  league_size = static_cast<int>(clubs.size());
  const auto found = std::ranges::find(
      clubs, team_id, &std::pair<std::uint8_t, TeamID>::second);
  return static_cast<int>(std::distance(clubs.begin(), found)) + 1;
}

// ---- SQLite ---------------------------------------------------------------

template <typename T>
T columnAs(sqlite3_stmt* stmt, int column)
{
  return static_cast<T>(sqlite3_column_int64(stmt, column));
}

float columnFloat(sqlite3_stmt* stmt, int column)
{
  return static_cast<float>(sqlite3_column_double(stmt, column));
}

std::string columnText(sqlite3_stmt* stmt, int column)
{
  const unsigned char* text = sqlite3_column_text(stmt, column);
  return text ? reinterpret_cast<const char*>(text) : "";
}

GameDateValue columnDate(sqlite3_stmt* stmt, int column)
{
  return dateFromInt(columnAs<std::int32_t>(stmt, column));
}

template <typename Read>
void forEachRow(const DatabaseConnection& db, const char* sql, Read read)
{
  sqlite3_stmt* stmt = db.prepareStatement(sql);
  while (sqlite3_step(stmt) == SQLITE_ROW) read(stmt);
  sqlite3_finalize(stmt);
}

template <typename Range, typename Bind>
void insertAll(const DatabaseConnection& db, const char* sql,
               const Range& items, Bind bind)
{
  sqlite3_stmt* stmt = db.prepareStatement(sql);
  for (const auto& item : items)
  {
    bind(stmt, item);
    db.executeStep(stmt);
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
  }
  sqlite3_finalize(stmt);
}

void bindText(sqlite3_stmt* stmt, int index, const std::string& text)
{
  sqlite3_bind_text(stmt, index, text.c_str(), -1, SQLITE_TRANSIENT);
}

void bindDate(sqlite3_stmt* stmt, int index, const GameDateValue& date)
{
  sqlite3_bind_int(stmt, index, dateToInt(date));
}

template <typename Enum>
Enum enumFrom(std::int64_t value, Enum count, Enum fallback)
{
  return value >= 0 && value < static_cast<std::int64_t>(count)
             ? static_cast<Enum>(value)
             : fallback;
}

AiManager readAiManager(sqlite3_stmt* stmt)
{
  AiManager manager;
  manager.id = columnAs<std::uint32_t>(stmt, 0);
  manager.first_name = columnText(stmt, 1);
  manager.last_name = columnText(stmt, 2);
  manager.nationality = static_cast<Language>(std::clamp<std::int64_t>(
      sqlite3_column_int64(stmt, 3), 0, std::to_underlying(Language::US)));
  manager.age = columnAs<std::uint8_t>(stmt, 4);
  manager.reputation = columnFloat(stmt, 5);
  manager.ability = columnFloat(stmt, 6);
  manager.style = enumFrom(sqlite3_column_int64(stmt, 7), ManagerStyle::COUNT,
                           ManagerStyle::Balanced);
  manager.team_id = columnAs<TeamID>(stmt, 8);
  manager.appointed = columnDate(stmt, 9);
  manager.matches = columnAs<std::uint16_t>(stmt, 10);
  manager.confidence = columnFloat(stmt, 11);
  manager.form = columnFloat(stmt, 12);
  return manager;
}

ManagerProfile readManagerProfile(sqlite3_stmt* stmt)
{
  ManagerProfile profile;
  profile.exists = true;
  profile.first_name = columnText(stmt, 0);
  profile.last_name = columnText(stmt, 1);
  profile.nationality = static_cast<Language>(std::clamp<std::int64_t>(
      sqlite3_column_int64(stmt, 2), 0, std::to_underlying(Language::US)));
  profile.age = columnAs<std::uint8_t>(stmt, 3);
  profile.reputation = columnFloat(stmt, 4);
  profile.licence = enumFrom(sqlite3_column_int64(stmt, 5),
                             CoachingLicence::COUNT, CoachingLicence::B);
  profile.licence_days = columnAs<std::uint16_t>(stmt, 6);
  profile.style = enumFrom(sqlite3_column_int64(stmt, 7), ManagerStyle::COUNT,
                           ManagerStyle::Balanced);
  profile.background =
      enumFrom(sqlite3_column_int64(stmt, 8), ManagerBackground::COUNT,
               ManagerBackground::ProfessionalPlayer);
  profile.club = columnAs<TeamID>(stmt, 9);
  profile.contract.weekly_wage = sqlite3_column_int64(stmt, 10);
  profile.contract.start = columnDate(stmt, 11);
  profile.contract.expires = columnDate(stmt, 12);
  profile.contract.release_compensation = sqlite3_column_int64(stmt, 13);
  profile.unemployed_since = columnDate(stmt, 14);
  profile.career_earnings = sqlite3_column_int64(stmt, 15);
  profile.seasons_managed = columnAs<std::uint16_t>(stmt, 16);
  return profile;
}

JobOffer readJobOffer(sqlite3_stmt* stmt)
{
  JobOffer offer;
  offer.id = columnAs<std::uint32_t>(stmt, 0);
  offer.team_id = columnAs<TeamID>(stmt, 1);
  offer.made = columnDate(stmt, 2);
  offer.expires = columnDate(stmt, 3);
  offer.weekly_wage = sqlite3_column_int64(stmt, 4);
  offer.years = columnAs<std::uint8_t>(stmt, 5);
  offer.release_compensation = sqlite3_column_int64(stmt, 6);
  offer.max_wage = sqlite3_column_int64(stmt, 7);
  offer.max_years = columnAs<std::uint8_t>(stmt, 8);
  offer.rounds_left = columnAs<std::uint8_t>(stmt, 9);
  offer.unsolicited = sqlite3_column_int(stmt, 10) != 0;
  offer.compensation = sqlite3_column_int64(stmt, 11);
  return offer;
}
}  // namespace

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

namespace ManagerMarketModel
{
ReputationTier reputationTier(float reputation)
{
  using enum ReputationTier;
  // Same scale as club reputation, where the smallest professional clubs
  // sit in the forties and the giants near 100.
  if (reputation >= 82.0f) return World;
  if (reputation >= 65.0f) return Continental;
  if (reputation >= 45.0f) return National;
  return Local;
}

const char* reputationTierKey(ReputationTier tier)
{
  using enum ReputationTier;
  switch (tier)
  {
    case Local:
      return "MANAGER_REP_LOCAL";
    case National:
      return "MANAGER_REP_NATIONAL";
    case Continental:
      return "MANAGER_REP_CONTINENTAL";
    case World:
      return "MANAGER_REP_WORLD";
  }
  return "MANAGER_REP_LOCAL";
}

const char* licenceKey(CoachingLicence licence)
{
  using enum CoachingLicence;
  switch (licence)
  {
    case None:
      return "MANAGER_LICENCE_NONE";
    case C:
      return "MANAGER_LICENCE_C";
    case B:
      return "MANAGER_LICENCE_B";
    case A:
      return "MANAGER_LICENCE_A";
    case Pro:
    case COUNT:
      break;
  }
  return "MANAGER_LICENCE_PRO";
}

const char* styleKey(ManagerStyle style)
{
  using enum ManagerStyle;
  switch (style)
  {
    case Possession:
      return "MANAGER_STYLE_POSSESSION";
    case Pressing:
      return "MANAGER_STYLE_PRESSING";
    case Counter:
      return "MANAGER_STYLE_COUNTER";
    case Direct:
      return "MANAGER_STYLE_DIRECT";
    case Balanced:
    case COUNT:
      break;
  }
  return "MANAGER_STYLE_BALANCED";
}

const char* backgroundKey(ManagerBackground background)
{
  using enum ManagerBackground;
  switch (background)
  {
    case SundayLeague:
      return "MANAGER_BG_SUNDAY_LEAGUE";
    case SemiProfessional:
      return "MANAGER_BG_SEMI_PRO";
    case TopFlightPlayer:
      return "MANAGER_BG_TOP_FLIGHT";
    case FormerInternational:
      return "MANAGER_BG_INTERNATIONAL";
    case ProfessionalPlayer:
    case COUNT:
      break;
  }
  return "MANAGER_BG_PROFESSIONAL";
}

const char* ownerKey(OwnerType owner)
{
  using enum OwnerType;
  switch (owner)
  {
    case Patient:
      return "MANAGER_OWNER_PATIENT";
    case Ambitious:
      return "MANAGER_OWNER_AMBITIOUS";
    case ImpatientBenefactor:
      return "MANAGER_OWNER_BENEFACTOR";
    case MemberOwned:
      return "MANAGER_OWNER_MEMBERS";
  }
  return "MANAGER_OWNER_PATIENT";
}

const char* departureKey(DepartureReason reason)
{
  using enum DepartureReason;
  switch (reason)
  {
    case Sacked:
      return "MANAGER_LEFT_SACKED";
    case Resigned:
      return "MANAGER_LEFT_RESIGNED";
    case Moved:
      return "MANAGER_LEFT_MOVED";
    case ContractExpired:
      return "MANAGER_LEFT_EXPIRED";
    case Current:
      break;
  }
  return "MANAGER_LEFT_CURRENT";
}

const char* awardKey(ManagerAwardKind kind)
{
  using enum ManagerAwardKind;
  switch (kind)
  {
    case LeagueTitle:
      return "MANAGER_AWARD_LEAGUE";
    case CupWin:
      return "MANAGER_AWARD_CUP";
    case Promotion:
      return "MANAGER_AWARD_PROMOTION";
    case ManagerOfTheSeason:
      return "MANAGER_AWARD_SEASON";
    case ManagerOfTheMonth:
      return "MANAGER_AWARD_MONTH";
  }
  return "MANAGER_AWARD_LEAGUE";
}

float startingReputation(ManagerBackground background)
{
  using enum ManagerBackground;
  switch (background)
  {
    case SundayLeague:
      return 30.0f;
    case SemiProfessional:
      return 38.0f;
    case TopFlightPlayer:
      return 55.0f;
    case FormerInternational:
      return 65.0f;
    case ProfessionalPlayer:
    case COUNT:
      break;
  }
  return 46.0f;
}

CoachingLicence startingLicence(ManagerBackground background)
{
  using enum ManagerBackground;
  switch (background)
  {
    case SundayLeague:
      return CoachingLicence::None;
    case SemiProfessional:
      return CoachingLicence::C;
    case ProfessionalPlayer:
      return CoachingLicence::B;
    case TopFlightPlayer:
    case FormerInternational:
    case COUNT:
      break;
  }
  return CoachingLicence::A;
}

CoachingLicence requiredLicence(std::uint8_t club_reputation, std::uint8_t tier)
{
  // Elite top-flight clubs need the highest licence, the rest of the top
  // flight the one below; lower divisions ask less. [S] (research 1.1)
  if (tier <= 1)
    return club_reputation >= 75 ? CoachingLicence::Pro : CoachingLicence::A;
  if (tier == 2) return CoachingLicence::B;
  return CoachingLicence::C;
}

float applicationChance(float manager_reputation, CoachingLicence licence,
                        std::uint8_t club_reputation, std::uint8_t tier,
                        float fit)
{
  const int missing =
      std::max(0, std::to_underlying(requiredLicence(club_reputation, tier)) -
                      std::to_underlying(licence));
  const float x =
      (manager_reputation - static_cast<float>(club_reputation) + 8.0f) / 7.0f -
      0.5f * static_cast<float>(missing) + fit;
  return std::clamp(1.0f / (1.0f + std::exp(-x)), 0.02f, 0.95f);
}

std::int64_t offerWage(std::uint8_t club_reputation, float manager_reputation,
                       std::uint8_t tier)
{
  const std::size_t row = std::min<std::size_t>(
      std::max<std::uint8_t>(tier, 1) - 1u, TIER_TOP_WAGE.size() - 1);
  const double stature = 0.7 * static_cast<double>(club_reputation) +
                         0.3 * static_cast<double>(manager_reputation);
  const double wage = TIER_TOP_WAGE[row] * std::exp((stature - 95.0) / 18.0);
  // Rounded to hundreds, never below a part-time coach's pay.
  return std::max<std::int64_t>(
      500, static_cast<std::int64_t>(std::llround(wage / 100.0)) * 100);
}

float updateConfidence(float confidence, float& form, float points,
                       float expected_points)
{
  form = (1.0f - FORM_ALPHA) * form + FORM_ALPHA * (points - expected_points);
  const float z = std::clamp(form / FORM_STDDEV, -2.0f, 2.0f);
  return std::clamp(0.9f * confidence + 0.1f * (50.0f + 25.0f * z), 0.0f,
                    100.0f);
}

float weeklyDismissalHazard(float confidence, std::uint16_t matches,
                            OwnerType owner)
{
  if (matches < MIN_MATCHES_FOR_DISMISSAL) return 0.0f;
  const float honeymoon =
      matches >= HONEYMOON_MATCHES
          ? 1.0f
          : 0.25f + 0.75f * static_cast<float>(matches) /
                        static_cast<float>(HONEYMOON_MATCHES);
  // Boards grow more patient with a manager who has lasted: most sacked
  // managers go within their first year or so. [S] (CT-W27)
  float standing = 0.5f;
  if (matches < 40)
    standing = 1.3f;
  else if (matches < 80)
    standing = 0.8f;
  return std::min(0.6f,
                  BASE_WEEKLY_HAZARD *
                      std::exp(-HAZARD_BETA * (confidence - 50.0f) / 25.0f) *
                      honeymoon * standing * ownerFactor(owner));
}

float seasonEndDismissalChance(int position, int expected_position,
                               int league_size, float confidence,
                               OwnerType owner)
{
  if (position <= 0 || league_size <= 0) return 0.0f;
  const float shortfall =
      static_cast<float>(position - expected_position) /
      std::max(1.0f, static_cast<float>(league_size) / 5.0f);
  const float base = 0.05f + 0.12f * std::max(0.0f, shortfall);
  return std::clamp(
      base * std::exp(-(confidence - 50.0f) / 20.0f) * ownerFactor(owner), 0.0f,
      0.8f);
}

float monthFactor(std::uint8_t month)
{
  return month < MONTH_FACTOR.size() ? MONTH_FACTOR[month] : 0.0f;
}

ClubVision clubVision(std::uint64_t world_seed, TeamID team_id,
                      std::uint8_t reputation, std::uint8_t youth_facilities,
                      std::int64_t balance, std::int64_t yearly_income)
{
  const auto draw = [world_seed, team_id](std::uint64_t key)
  {
    return static_cast<float>(
        WorldRng::hashUniform(world_seed, RngDomain::Managers, 0x5649'5349ULL,
                              mixHash(team_id, key)));
  };
  ClubVision vision;
  const float stature = static_cast<float>(reputation) / 100.0f;
  vision.ambition =
      std::clamp(0.2f + 0.7f * stature + 0.2f * (draw(1) - 0.5f), 0.0f, 1.0f);
  vision.youth_focus =
      std::clamp(0.25f + 0.5f * static_cast<float>(youth_facilities) / 100.0f +
                     0.3f * (draw(2) - 0.5f),
                 0.0f, 1.0f);
  const float cash =
      yearly_income > 0 ? static_cast<float>(static_cast<double>(balance) /
                                             static_cast<double>(yearly_income))
                        : 0.0f;
  vision.tight_budget = std::clamp(0.85f - 0.6f * stature -
                                       0.25f * std::clamp(cash, -1.0f, 1.0f) +
                                       0.1f * (draw(3) - 0.5f),
                                   0.0f, 1.0f);
  vision.preferred_style =
      static_cast<ManagerStyle>(std::min(4, static_cast<int>(draw(4) * 5.0f)));
  // Rich, ambitious clubs are more often run by impatient owners.
  if (const float owner_roll = draw(5);
      owner_roll < 0.15f + 0.25f * vision.ambition)
    vision.owner = vision.ambition > 0.6f ? OwnerType::ImpatientBenefactor
                                          : OwnerType::Ambitious;
  else if (owner_roll < 0.55f)
    vision.owner = OwnerType::Ambitious;
  else if (owner_roll < 0.75f)
    vision.owner = OwnerType::MemberOwned;
  else
    vision.owner = OwnerType::Patient;
  return vision;
}

const char* interviewQuestionKey(InterviewTopic topic)
{
  using enum InterviewTopic;
  switch (topic)
  {
    case Style:
      return "INTERVIEW_Q_STYLE";
    case Ambition:
      return "INTERVIEW_Q_AMBITION";
    case Youth:
      return "INTERVIEW_Q_YOUTH";
    case Budget:
    case COUNT:
      break;
  }
  return "INTERVIEW_Q_BUDGET";
}

const char* interviewOptionKey(InterviewTopic topic, std::size_t option)
{
  static constexpr std::array<std::array<const char*, INTERVIEW_OPTIONS>,
                              INTERVIEW_TOPICS>
      KEYS = {{
          {"INTERVIEW_A_STYLE_OWN", "INTERVIEW_A_STYLE_CLUB",
           "INTERVIEW_A_STYLE_ADAPT"},
          {"INTERVIEW_A_AMBITION_HIGH", "INTERVIEW_A_AMBITION_TARGET",
           "INTERVIEW_A_AMBITION_BUILD"},
          {"INTERVIEW_A_YOUTH_PATHWAY", "INTERVIEW_A_YOUTH_BLEND",
           "INTERVIEW_A_YOUTH_PROVEN"},
          {"INTERVIEW_A_BUDGET_FUNDS", "INTERVIEW_A_BUDGET_WITHIN",
           "INTERVIEW_A_BUDGET_SELL"},
      }};
  return KEYS[std::to_underlying(topic)]
             [std::min(option, INTERVIEW_OPTIONS - 1)];
}

namespace
{
int interviewBand(float value, float low, float high)
{
  if (value >= high) return 2;
  return value >= low ? 1 : -1;
}

int styleAnswerFit(const ClubVision& vision, std::size_t option)
{
  if (option == 1) return 2;
  if (option == 2) return 1;
  return vision.preferred_style == ManagerStyle::Balanced ? 1 : 0;
}

int ambitionAnswerFit(const ClubVision& vision, std::size_t option)
{
  if (option == 0) return interviewBand(vision.ambition, 0.5f, 0.75f);
  if (option == 1) return 1;
  if (vision.owner == OwnerType::Patient ||
      vision.owner == OwnerType::MemberOwned)
    return 2;
  return vision.ambition > 0.7f ? -1 : 0;
}

int youthAnswerFit(const ClubVision& vision, std::size_t option)
{
  if (option == 0) return interviewBand(vision.youth_focus, 0.45f, 0.65f);
  if (option == 1) return 1;
  return interviewBand(1.0f - vision.youth_focus, 0.5f, 0.7f);
}

int budgetAnswerFit(const ClubVision& vision, std::size_t option)
{
  if (option == 0) return vision.tight_budget < 0.3f ? 1 : -1;
  if (option == 1) return interviewBand(vision.tight_budget, 0.3f, 0.55f);
  return vision.tight_budget > 0.7f ? 2 : 0;
}
}  // namespace

int interviewAnswerFit(const ClubVision& vision, InterviewTopic topic,
                       std::size_t option)
{
  using enum InterviewTopic;
  switch (topic)
  {
    case Style:
      return styleAnswerFit(vision, option);
    case Ambition:
      return ambitionAnswerFit(vision, option);
    case Youth:
      return youthAnswerFit(vision, option);
    case Budget:
      return budgetAnswerFit(vision, option);
    case COUNT:
      break;
  }
  return 0;
}
}  // namespace ManagerMarketModel

// ---------------------------------------------------------------------------
// Career
// ---------------------------------------------------------------------------

using namespace ManagerMarketModel;

ManagerCareer::ManagerCareer(std::shared_ptr<GameData> gd)
    : gamedata(std::move(gd))
{
}

bool ManagerCareer::isEmployed() const
{
  return profile.exists && profile.club != FREE_AGENTS_TEAM_ID;
}

std::uint8_t ManagerCareer::leagueTier(TeamID team_id) const
{
  const auto team = gamedata->getTeam(team_id);
  return team ? Competitions::leagueTier(*gamedata, team->get().getLeagueId())
              : 1;
}

std::uint8_t ManagerCareer::clubReputation(TeamID team_id) const
{
  const auto team = gamedata->getTeam(team_id);
  return team ? team->get().getReputation() : 1;
}

std::string ManagerCareer::clubName(TeamID team_id) const
{
  const auto team = gamedata->getTeam(team_id);
  return team ? team->get().getName() : std::string();
}

ManagerStint* ManagerCareer::currentStint()
{
  if (stints.empty() || stints.back().reason != DepartureReason::Current)
    return nullptr;
  return &stints.back();
}

void ManagerCareer::createProfile(const ManagerSetup& setup,
                                  const GameDateValue& date)
{
  profile = ManagerProfile{};
  profile.exists = true;
  profile.first_name = setup.first_name;
  profile.last_name = setup.last_name;
  profile.nationality = setup.nationality;
  profile.age = std::clamp<std::uint8_t>(setup.age, 25, 75);
  profile.background = setup.background;
  profile.style = setup.style;
  profile.reputation = startingReputation(setup.background);
  profile.licence = startingLicence(setup.background);
  profile.club = FREE_AGENTS_TEAM_ID;
  profile.unemployed_since = date;
}

void ManagerCareer::ensureProfile(TeamID managed_team_id,
                                  const GameDateValue& date)
{
  if (!profile.exists)
  {
    // Careers begun before the manager step: a professional-player
    // background and a name from the club's country.
    ManagerSetup setup;
    if (const auto team = gamedata->getTeam(managed_team_id))
      setup.nationality =
          leagueProfile(team->get().getLeagueId()).domestic_nationality;
    WorldRng rng = WorldRng::stream(seedOf(*gamedata), RngDomain::Managers,
                                    0x4855'4D41ULL);
    const NamePool& names = NamePool::instance();
    const auto& firsts = names.firstNames(setup.nationality);
    if (const auto& lasts = names.lastNames(setup.nationality);
        !firsts.empty() && !lasts.empty())
    {
      setup.first_name = firsts[static_cast<std::size_t>(
          rng.uniformInt(0, static_cast<int>(firsts.size()) - 1))];
      setup.last_name = lasts[static_cast<std::size_t>(
          rng.uniformInt(0, static_cast<int>(lasts.size()) - 1))];
    }
    createProfile(setup, date);
  }
  if (managed_team_id != FREE_AGENTS_TEAM_ID && profile.club != managed_team_id)
  {
    if (isEmployed()) leaveJobQuietly(DepartureReason::Moved, date);
    startJob(managed_team_id, initialContract(managed_team_id, date), date);
  }
}

ManagerContract ManagerCareer::initialContract(TeamID team_id,
                                               const GameDateValue& date) const
{
  ManagerContract contract;
  contract.weekly_wage = offerWage(clubReputation(team_id), profile.reputation,
                                   leagueTier(team_id));
  contract.start = date;
  contract.expires = seasonEnd(date, 3);
  contract.release_compensation = contract.weekly_wage * 52 * 3 / 2;
  return contract;
}

ManagerContract ManagerCareer::contractFor(const JobOffer& offer,
                                           const GameDateValue& date) const
{
  ManagerContract contract;
  contract.weekly_wage = offer.weekly_wage;
  contract.start = date;
  contract.expires = seasonEnd(date, offer.years);
  contract.release_compensation = offer.release_compensation;
  return contract;
}

// ---- Managers of the world -------------------------------------------------

AiManager ManagerCareer::generateManager(TeamID team_id, float reputation,
                                         const GameDateValue& date)
{
  AiManager manager;
  manager.id = next_manager_id++;
  WorldRng rng =
      WorldRng::stream(seedOf(*gamedata), RngDomain::Managers, manager.id, 1);
  const auto team = gamedata->getTeam(team_id);
  manager.nationality =
      team && rng.chance(0.7)
          ? leagueProfile(team->get().getLeagueId()).domestic_nationality
          : static_cast<Language>(
                rng.uniformInt(0, std::to_underlying(Language::US)));
  const NamePool& names = NamePool::instance();
  const auto& firsts = names.firstNames(manager.nationality);
  if (const auto& lasts = names.lastNames(manager.nationality);
      !firsts.empty() && !lasts.empty())
  {
    // Reserved full names are never used.
    do
    {
      manager.first_name = firsts[static_cast<std::size_t>(
          rng.uniformInt(0, static_cast<int>(firsts.size()) - 1))];
      manager.last_name = lasts[static_cast<std::size_t>(
          rng.uniformInt(0, static_cast<int>(lasts.size()) - 1))];
    } while (names.isExcluded(manager.first_name + " " + manager.last_name));
  }
  manager.age = static_cast<std::uint8_t>(rng.uniformInt(36, 66));
  manager.reputation =
      std::clamp(reputation + rng.normal(0.0f, 7.0f), 3.0f, 95.0f);
  manager.ability =
      std::clamp(manager.reputation + rng.normal(0.0f, 8.0f), 5.0f, 99.0f);
  manager.style = static_cast<ManagerStyle>(
      rng.uniformInt(0, std::to_underlying(ManagerStyle::COUNT) - 1));
  manager.team_id = team_id;
  manager.confidence = INITIAL_CONFIDENCE;
  if (team_id != FREE_AGENTS_TEAM_ID)
  {
    // Existing managers have been in the job for a while already.
    const int tenure = rng.uniformInt(30, 900);
    manager.appointed = addDays(date, -tenure);
    manager.matches = static_cast<std::uint16_t>(tenure / 9);
    manager.confidence = std::clamp(rng.normal(58.0f, 8.0f), 30.0f, 85.0f);
  }
  else
  {
    manager.appointed = date;
  }
  return manager;
}

void ManagerCareer::ensureClubManagers(const GameDateValue& date,
                                       TeamID managed_team_id)
{
  std::vector<TeamID> clubs;
  for (const auto& [team_id, team] : gamedata->getTeams())
    if (team_id != FREE_AGENTS_TEAM_ID) clubs.push_back(team_id);
  std::ranges::sort(clubs);
  for (const TeamID team_id : clubs)
  {
    if (team_id == managed_team_id || team_id == profile.club ||
        clubManager(team_id) || findVacancy(team_id))
      continue;
    managers.push_back(generateManager(team_id, clubReputation(team_id), date));
  }
  topUpFreePool(date);
}

void ManagerCareer::topUpFreePool(const GameDateValue& date)
{
  const std::size_t clubs = gamedata->getTeams().size();
  const std::size_t target = std::max<std::size_t>(10, clubs / 6);
  auto free_count = static_cast<std::size_t>(std::ranges::count(
      managers, TeamID{FREE_AGENTS_TEAM_ID}, &AiManager::team_id));
  while (free_count < target)
  {
    // New coaches enter the market across the whole range of stature.
    WorldRng rng = WorldRng::stream(seedOf(*gamedata), RngDomain::Managers,
                                    next_manager_id, 2);
    managers.push_back(
        generateManager(FREE_AGENTS_TEAM_ID, rng.uniform(10.0f, 70.0f), date));
    ++free_count;
  }
}

const AiManager* ManagerCareer::clubManager(TeamID team_id) const
{
  if (team_id == FREE_AGENTS_TEAM_ID) return nullptr;
  const auto found = std::ranges::find(managers, team_id, &AiManager::team_id);
  return found == managers.end() ? nullptr : std::to_address(found);
}

// ---- Job market ------------------------------------------------------------

const Vacancy* ManagerCareer::findVacancy(TeamID team_id) const
{
  const auto found = std::ranges::find(vacancies, team_id, &Vacancy::team_id);
  return found == vacancies.end() ? nullptr : std::to_address(found);
}

const JobApplication* ManagerCareer::findApplication(TeamID team_id) const
{
  const auto found =
      std::ranges::find(applications, team_id, &JobApplication::team_id);
  return found == applications.end() ? nullptr : std::to_address(found);
}

const JobOffer* ManagerCareer::findOffer(std::uint32_t offer_id) const
{
  const auto found = std::ranges::find(offers, offer_id, &JobOffer::id);
  return found == offers.end() ? nullptr : std::to_address(found);
}

ClubVision ManagerCareer::visionOf(TeamID team_id) const
{
  const auto team = gamedata->getTeam(team_id);
  if (!team) return {};
  const Finances& finances = team->get().getFinances();
  return clubVision(seedOf(*gamedata), team_id, team->get().getReputation(),
                    team->get().getProfile().youth_facilities,
                    finances.getBalance(), finances.getWageBudget() * 52);
}

float ManagerCareer::applicationChance(TeamID team_id) const
{
  // Boards prefer a coach who plays their way, and many favour their own
  // country's coaches. [S] (research 1.2)
  float fit = 0.0f;
  if (visionOf(team_id).preferred_style == profile.style) fit += 0.6f;
  if (const auto team = gamedata->getTeam(team_id);
      team && leagueProfile(team->get().getLeagueId()).domestic_nationality ==
                  profile.nationality)
    fit += 0.3f;
  return ManagerMarketModel::applicationChance(
      profile.reputation, profile.licence, clubReputation(team_id),
      leagueTier(team_id), fit);
}

ApplyResult ManagerCareer::apply(TeamID team_id, const GameDateValue& date)
{
  using enum ApplyResult;
  if (!profile.exists) return NoProfile;
  if (team_id == profile.club) return OwnClub;
  if (!findVacancy(team_id)) return NoVacancy;
  if (findApplication(team_id)) return AlreadyApplied;
  // A board that just parted with him does not take him back so soon.
  for (const ManagerStint& stint : stints)
    if (stint.team_id == team_id &&
        (stint.reason == DepartureReason::Sacked ||
         stint.reason == DepartureReason::Resigned) &&
        daysBetween(stint.end, date) < 365)
      return RecentlyLeft;
  const int days =
      MIN_RESPONSE_DAYS +
      static_cast<int>(
          uniform(*gamedata, dayOrdinal(date), mixHash(team_id, 11)) *
          (MAX_RESPONSE_DAYS - MIN_RESPONSE_DAYS + 1));
  applications.emplace_back(team_id, date, addDays(date, days),
                            ApplicationStage::Pending);
  return Ok;
}

void ManagerCareer::processApplications(const GameDateValue& date, Inbox& inbox,
                                        CareerDayEvents& events)
{
  for (JobApplication& application : applications)
  {
    if (application.stage != ApplicationStage::Pending ||
        date < application.respond_date)
      continue;
    const std::string club = clubName(application.team_id);
    const float chance = applicationChance(application.team_id);
    if (uniform(*gamedata, dayOrdinal(date), mixHash(application.team_id, 12)) <
        static_cast<double>(chance))
    {
      application.stage = ApplicationStage::Interview;
      events.interview_invitation = true;
      post(inbox, date,
           {InboxCategory::General,
            "INBOX_JOB_INTERVIEW_TITLE",
            "INBOX_JOB_INTERVIEW_BODY",
            {club},
            application.team_id});
      // The board waits for the interview before appointing anyone.
      if (auto vacancy = std::ranges::find(vacancies, application.team_id,
                                           &Vacancy::team_id);
          vacancy != vacancies.end() &&
          daysBetween(date, vacancy->fill_date) < HOLD_DAYS * 2)
        vacancy->fill_date = addDays(date, HOLD_DAYS * 2);
    }
    else
    {
      application.stage = ApplicationStage::Rejected;
      post(inbox, date,
           {InboxCategory::General,
            "INBOX_JOB_REJECTED_TITLE",
            "INBOX_JOB_REJECTED_BODY",
            {club},
            application.team_id});
    }
  }
}

std::optional<InterviewResult> ManagerCareer::interview(
    TeamID team_id, std::span<const std::uint8_t> answers,
    const GameDateValue& date, Inbox& inbox)
{
  const auto application =
      std::ranges::find(applications, team_id, &JobApplication::team_id);
  if (application == applications.end() ||
      application->stage != ApplicationStage::Interview ||
      answers.size() != INTERVIEW_TOPICS || !findVacancy(team_id))
    return std::nullopt;
  const ClubVision vision = visionOf(team_id);
  InterviewResult result;
  for (std::size_t topic = 0; topic < INTERVIEW_TOPICS; ++topic)
  {
    result.answer_fit[topic] = interviewAnswerFit(
        vision, static_cast<InterviewTopic>(topic), answers[topic]);
    result.score += result.answer_fit[topic];
  }
  const double chance =
      std::clamp(0.35 + 0.1 * static_cast<double>(result.score), 0.05, 0.95);
  result.offered =
      uniform(*gamedata, dayOrdinal(date), mixHash(team_id, 13)) < chance;
  if (result.offered)
  {
    // A convincing interview earns better terms; asking for funds at a
    // club that has none costs some.
    const float wage_factor = 0.9f + 0.03f * static_cast<float>(result.score);
    makeOffer(team_id, date, false, wage_factor, inbox);
  }
  else
  {
    application->stage = ApplicationStage::Rejected;
    post(inbox, date,
         {InboxCategory::General,
          "INBOX_JOB_INTERVIEW_FAILED_TITLE",
          "INBOX_JOB_INTERVIEW_FAILED_BODY",
          {clubName(team_id)},
          team_id});
  }
  return result;
}

bool ManagerCareer::makeOffer(TeamID team_id, const GameDateValue& date,
                              bool unsolicited, float wage_factor, Inbox& inbox)
{
  if (!profile.exists || team_id == profile.club) return false;
  std::erase_if(offers, [team_id](const JobOffer& offer)
                { return offer.team_id == team_id; });
  const std::uint8_t reputation = clubReputation(team_id);
  const std::uint8_t tier = leagueTier(team_id);
  JobOffer offer;
  offer.id = next_offer_id++;
  offer.team_id = team_id;
  offer.made = date;
  offer.expires = addDays(date, OFFER_VALID_DAYS);
  // Long spells out of work lower what a manager can ask for.
  const bool desperate =
      !isEmployed() &&
      daysBetween(profile.unemployed_since, date) > DESPERATE_AFTER_DAYS * 2;
  offer.weekly_wage = std::max<std::int64_t>(
      500,
      static_cast<std::int64_t>(std::llround(
          static_cast<double>(offerWage(reputation, profile.reputation, tier)) *
          static_cast<double>(wage_factor) * (desperate ? 0.9 : 1.0) / 100.0)) *
          100);
  offer.max_wage = offer.weekly_wage * 5 / 4;
  offer.years = tier <= 1 && reputation >= 70 ? 3 : 2;
  offer.max_years = static_cast<std::uint8_t>(
      std::min<int>(static_cast<int>(MAX_CONTRACT_YEARS), offer.years + 1));
  offer.release_compensation = offer.weekly_wage * 52 * offer.years / 2;
  offer.rounds_left = NEGOTIATION_ROUNDS;
  offer.unsolicited = unsolicited;
  offer.compensation = isEmployed() ? profile.contract.release_compensation : 0;
  offers.push_back(offer);

  if (auto application =
          std::ranges::find(applications, team_id, &JobApplication::team_id);
      application != applications.end())
    application->stage = ApplicationStage::Offered;
  if (auto vacancy = std::ranges::find(vacancies, team_id, &Vacancy::team_id);
      vacancy != vacancies.end() && vacancy->fill_date < offer.expires)
    vacancy->fill_date = addDays(offer.expires, 1);
  post(inbox, date,
       {InboxCategory::General,
        unsolicited ? "INBOX_JOB_APPROACH_TITLE" : "INBOX_JOB_OFFER_TITLE",
        unsolicited ? "INBOX_JOB_APPROACH_BODY" : "INBOX_JOB_OFFER_BODY",
        {clubName(team_id), formatMoney(offer.weekly_wage),
         std::to_string(offer.years)},
        team_id});
  return true;
}

OfferReply ManagerCareer::negotiate(std::uint32_t offer_id,
                                    std::int64_t weekly_wage,
                                    std::uint8_t years,
                                    const GameDateValue& date)
{
  const auto offer = std::ranges::find(offers, offer_id, &JobOffer::id);
  if (offer == offers.end() || offer->expires < date)
    return OfferReply::Withdrawn;
  years = static_cast<std::uint8_t>(
      std::clamp<int>(years, 1, static_cast<int>(MAX_CONTRACT_YEARS)));
  weekly_wage = std::max<std::int64_t>(weekly_wage, 0);
  if (weekly_wage <= offer->max_wage && years <= offer->max_years)
  {
    offer->weekly_wage = std::max(weekly_wage, offer->weekly_wage / 2);
    offer->years = years;
    offer->release_compensation = offer->weekly_wage * 52 * years / 2;
    offer->rounds_left = 0;
    return OfferReply::Accepted;
  }
  // Outrageous demands end the talks at once.
  const int cost = weekly_wage > offer->max_wage * 8 / 5 ? 2 : 1;
  if (offer->rounds_left <= cost)
  {
    const TeamID team_id = offer->team_id;
    offers.erase(offer);
    if (auto application =
            std::ranges::find(applications, team_id, &JobApplication::team_id);
        application != applications.end())
      application->stage = ApplicationStage::Rejected;
    return OfferReply::Withdrawn;
  }
  offer->rounds_left = static_cast<std::uint8_t>(offer->rounds_left - cost);
  offer->weekly_wage =
      std::min(offer->max_wage, (offer->weekly_wage + weekly_wage) / 2);
  offer->weekly_wage = offer->weekly_wage / 100 * 100;
  offer->years = std::min(years, offer->max_years);
  offer->release_compensation = offer->weekly_wage * 52 * offer->years / 2;
  return OfferReply::Improved;
}

bool ManagerCareer::decline(std::uint32_t offer_id)
{
  const auto offer = std::ranges::find(offers, offer_id, &JobOffer::id);
  if (offer == offers.end()) return false;
  const TeamID team_id = offer->team_id;
  offers.erase(offer);
  std::erase_if(applications, [team_id](const JobApplication& application)
                { return application.team_id == team_id; });
  return true;
}

std::optional<JobOffer> ManagerCareer::takeOffer(std::uint32_t offer_id,
                                                 const GameDateValue& date)
{
  const auto offer = std::ranges::find(offers, offer_id, &JobOffer::id);
  if (offer == offers.end() || offer->expires < date ||
      offer->team_id == profile.club)
    return std::nullopt;
  JobOffer taken = *offer;
  offers.erase(offer);
  return taken;
}

// ---- Transitions -----------------------------------------------------------

void ManagerCareer::startJob(TeamID team_id, const ManagerContract& contract,
                             const GameDateValue& date)
{
  if (!profile.exists || team_id == FREE_AGENTS_TEAM_ID) return;
  // The club's manager (caretaker or incumbent) steps aside.
  for (AiManager& manager : managers)
  {
    if (manager.team_id != team_id) continue;
    manager.team_id = FREE_AGENTS_TEAM_ID;
    manager.matches = 0;
    manager.confidence = INITIAL_CONFIDENCE;
    manager.form = 0.0f;
  }
  std::erase_if(vacancies, [team_id](const Vacancy& vacancy)
                { return vacancy.team_id == team_id; });
  applications.clear();
  offers.clear();
  profile.club = team_id;
  profile.contract = contract;
  ManagerStint stint;
  stint.team_id = team_id;
  stint.club_name = clubName(team_id);
  if (const auto team = gamedata->getTeam(team_id))
    stint.league_id = team->get().getLeagueId();
  stint.start = date;
  stints.emplace_back(std::move(stint));
}

std::int64_t ManagerCareer::leaveJob(DepartureReason reason,
                                     const GameDateValue& date, Inbox& inbox)
{
  if (!isEmployed()) return 0;
  const TeamID club = profile.club;
  const std::string name = clubName(club);
  std::int64_t severance = 0;
  if (reason == DepartureReason::Sacked)
  {
    const int weeks =
        std::clamp(daysBetween(date, profile.contract.expires) / 7, 0, 52);
    severance = profile.contract.weekly_wage * weeks;
    if (severance > 0)
      if (const auto team = gamedata->getTeam(club))
        team->get().getFinances().record(date, FinanceCategory::Staff,
                                         -severance);
    profile.career_earnings += severance;
    post(inbox, date,
         {InboxCategory::Board,
          "INBOX_MANAGER_SACKED_TITLE",
          "INBOX_MANAGER_SACKED_BODY",
          {name, formatMoney(severance), std::to_string(weeks)},
          club});
  }
  else if (reason == DepartureReason::Resigned)
  {
    post(inbox, date,
         {InboxCategory::Board,
          "INBOX_MANAGER_RESIGNED_TITLE",
          "INBOX_MANAGER_RESIGNED_BODY",
          {name},
          club});
  }
  else if (reason == DepartureReason::ContractExpired)
  {
    post(inbox, date,
         {InboxCategory::Board,
          "INBOX_MANAGER_EXPIRED_TITLE",
          "INBOX_MANAGER_EXPIRED_BODY",
          {name},
          club});
  }
  leaveJobQuietly(reason, date);
  return severance;
}

void ManagerCareer::leaveJobQuietly(DepartureReason reason,
                                    const GameDateValue& date)
{
  const TeamID club = profile.club;
  if (ManagerStint* stint = currentStint())
  {
    stint->end = date;
    stint->reason = reason;
  }
  if (reason == DepartureReason::Sacked)
    profile.reputation -= SACKED_REPUTATION_HIT;
  else if (reason == DepartureReason::Resigned)
    profile.reputation -= RESIGNED_REPUTATION_HIT;
  profile.reputation = std::clamp(profile.reputation, 1.0f, 99.0f);
  profile.club = FREE_AGENTS_TEAM_ID;
  profile.contract = ManagerContract{};
  profile.unemployed_since = date;
  openVacancy(club, date);
}

void ManagerCareer::payCompensation(TeamID from_club, TeamID to_club,
                                    std::int64_t amount,
                                    const GameDateValue& date)
{
  if (amount <= 0 || from_club == FREE_AGENTS_TEAM_ID ||
      to_club == FREE_AGENTS_TEAM_ID)
    return;
  if (const auto payer = gamedata->getTeam(to_club))
    payer->get().getFinances().record(date, FinanceCategory::Staff, -amount);
  if (const auto payee = gamedata->getTeam(from_club))
    payee->get().getFinances().record(date, FinanceCategory::Staff, amount);
}

void ManagerCareer::recordAward(ManagerAwardKind kind, std::uint16_t start_year,
                                TeamID team_id)
{
  awards.emplace_back(start_year, kind, team_id, clubName(team_id));
  if (ManagerStint* stint = currentStint();
      stint && stint->team_id == team_id &&
      (kind == ManagerAwardKind::LeagueTitle ||
       kind == ManagerAwardKind::CupWin))
    ++stint->trophies;
}

void ManagerCareer::adjustReputation(float delta)
{
  if (!profile.exists) return;
  profile.reputation = std::clamp(profile.reputation + delta, 1.0f, 100.0f);
}

void ManagerCareer::addEarnings(std::int64_t amount)
{
  if (profile.exists) profile.career_earnings += amount;
}

bool ManagerCareer::dismissClubManager(TeamID team_id,
                                       const GameDateValue& date, Inbox& inbox)
{
  const auto manager =
      std::ranges::find(managers, team_id, &AiManager::team_id);
  if (team_id == FREE_AGENTS_TEAM_ID || manager == managers.end()) return false;
  dismissAi(*manager, date, inbox, profile.club);
  return true;
}

void ManagerCareer::openVacancy(TeamID team_id, const GameDateValue& date)
{
  if (team_id == FREE_AGENTS_TEAM_ID || findVacancy(team_id)) return;
  const int days = MIN_VACANCY_DAYS +
                   static_cast<int>(uniform(*gamedata, dayOrdinal(date),
                                            mixHash(team_id, 14)) *
                                    (MAX_VACANCY_DAYS - MIN_VACANCY_DAYS + 1));
  vacancies.emplace_back(team_id, date, addDays(date, days));
}

void ManagerCareer::dismissAi(AiManager& manager, const GameDateValue& date,
                              Inbox& inbox, TeamID managed_team_id)
{
  const TeamID club = manager.team_id;
  manager.team_id = FREE_AGENTS_TEAM_ID;
  manager.reputation = std::max(1.0f, manager.reputation - 2.0f);
  manager.matches = 0;
  manager.confidence = INITIAL_CONFIDENCE;
  manager.form = 0.0f;
  openVacancy(club, date);

  // News reaches the manager when it concerns his league or a job within
  // his reach; out of work, a reachable job is worth reading at once.
  const auto team = gamedata->getTeam(club);
  const auto own = gamedata->getTeam(managed_team_id);
  const bool same_league =
      team && own && team->get().getLeagueId() == own->get().getLeagueId();
  const bool reachable = profile.exists && applicationChance(club) >= 0.25f;
  if (!same_league && !reachable) return;
  post(
      inbox, date,
      {InboxCategory::General,
       "INBOX_MANAGER_NEWS_TITLE",
       reachable ? "INBOX_MANAGER_NEWS_RUMOUR_BODY" : "INBOX_MANAGER_NEWS_BODY",
       {manager.name(), clubName(club)},
       club,
       !(reachable && !isEmployed())});
}

void ManagerCareer::fillVacancies(const GameDateValue& date,
                                  TeamID managed_team_id, Inbox& inbox,
                                  CareerDayEvents& events)
{
  std::vector<TeamID> filled;
  for (Vacancy& vacancy : vacancies)
  {
    if (date < vacancy.fill_date) continue;
    // Never while the human still has a live interview or offer here.
    const JobApplication* application = findApplication(vacancy.team_id);
    if (const bool human_pending =
            (application &&
             (application->stage == ApplicationStage::Interview ||
              application->stage == ApplicationStage::Pending)) ||
            std::ranges::any_of(offers,
                                [&vacancy, date](const JobOffer& offer)
                                {
                                  return offer.team_id == vacancy.team_id &&
                                         !(offer.expires < date);
                                });
        human_pending)
    {
      vacancy.fill_date = addDays(date, HOLD_DAYS);
      continue;
    }
    const auto target = static_cast<float>(clubReputation(vacancy.team_id));
    AiManager* best = nullptr;
    float best_score = 0.0f;
    for (AiManager& manager : managers)
    {
      if (manager.team_id != FREE_AGENTS_TEAM_ID) continue;
      const auto noise = static_cast<float>(uniform(
          *gamedata, dayOrdinal(date), mixHash(vacancy.team_id, manager.id)));
      const float score = -std::abs(manager.reputation - target) +
                          0.2f * manager.ability + 6.0f * noise;
      if (!best || score > best_score)
      {
        best = &manager;
        best_score = score;
      }
    }
    if (!best) continue;
    best->team_id = vacancy.team_id;
    best->appointed = date;
    best->matches = 0;
    best->confidence = INITIAL_CONFIDENCE;
    best->form = 0.0f;
    ++events.ai_appointments;
    filled.emplace_back(vacancy.team_id);
    if (application)
      post(inbox, date,
           {InboxCategory::General,
            "INBOX_JOB_FILLED_TITLE",
            "INBOX_JOB_FILLED_BODY",
            {clubName(vacancy.team_id), best->name()},
            vacancy.team_id,
            false});
    else if (const auto team = gamedata->getTeam(vacancy.team_id),
             own = gamedata->getTeam(managed_team_id);
             team && own &&
             team->get().getLeagueId() == own->get().getLeagueId())
      post(inbox, date,
           {InboxCategory::General,
            "INBOX_MANAGER_APPOINTED_TITLE",
            "INBOX_MANAGER_APPOINTED_BODY",
            {best->name(), clubName(vacancy.team_id)},
            vacancy.team_id,
            true});
  }
  if (filled.empty()) return;
  std::erase_if(vacancies, [&filled](const Vacancy& vacancy)
                { return std::ranges::contains(filled, vacancy.team_id); });
  std::erase_if(applications, [&filled](const JobApplication& application)
                { return std::ranges::contains(filled, application.team_id); });
  std::erase_if(offers, [&filled](const JobOffer& offer)
                { return std::ranges::contains(filled, offer.team_id); });
  topUpFreePool(date);
}

// ---- Hooks -----------------------------------------------------------------

CareerDayEvents ManagerCareer::onDayAdvanced(const GameDateValue& date,
                                             TeamID managed_team_id,
                                             float board_confidence,
                                             int board_expected_position,
                                             const PositionProvider& positions,
                                             Inbox& inbox)
{
  CareerDayEvents events;
  std::erase_if(offers,
                [date](const JobOffer& offer) { return offer.expires < date; });
  std::erase_if(applications,
                [date](const JobApplication& application)
                {
                  return application.stage == ApplicationStage::Rejected &&
                         daysBetween(application.respond_date, date) > 30;
                });
  processApplications(date, inbox, events);
  fillVacancies(date, managed_team_id, inbox, events);

  if (isEmployed())
  {
    // Head coaching counts towards the next licence; a manager working
    // above his licence is enrolled on the course and progresses faster.
    const auto level = std::to_underlying(profile.licence);
    if (level < LICENCE_DAYS.size())
    {
      const bool on_course =
          profile.licence < requiredLicence(clubReputation(profile.club),
                                            leagueTier(profile.club));
      profile.licence_days = static_cast<std::uint16_t>(profile.licence_days +
                                                        (on_course ? 2 : 1));
      if (profile.licence_days >= LICENCE_DAYS[level])
      {
        profile.licence_days = 0;
        profile.licence = static_cast<CoachingLicence>(level + 1);
        post(inbox, date,
             {InboxCategory::General,
              "INBOX_LICENCE_TITLE",
              "INBOX_LICENCE_BODY",
              {std::string("@") + licenceKey(profile.licence)},
              FREE_AGENTS_TEAM_ID});
      }
    }
  }

  const std::size_t offers_before = offers.size();
  if (dayOrdinal(date) % 7 == 0)
    weeklyReviews(date, managed_team_id, inbox, events);
  if (date.day == 1)
    monthlyReviews(date, managed_team_id, board_confidence,
                   board_expected_position, positions, inbox, events);
  events.new_offer = offers.size() > offers_before;
  return events;
}

void ManagerCareer::weeklyReviews(const GameDateValue& date,
                                  TeamID managed_team_id, Inbox& inbox,
                                  CareerDayEvents& events)
{
  weeklyAiReviews(date, managed_team_id, inbox, events);
  weeklyManagerReview(date, managed_team_id, inbox, events);
}

void ManagerCareer::weeklyAiReviews(const GameDateValue& date,
                                    TeamID managed_team_id, Inbox& inbox,
                                    CareerDayEvents& events)
{
  const std::int32_t today = dayOrdinal(date);
  if (!inSeason(date)) return;
  const float month = MONTH_FACTOR[date.month];
  for (AiManager& manager : managers)
  {
    if (manager.team_id == FREE_AGENTS_TEAM_ID ||
        manager.team_id == managed_team_id)
      continue;
    const float hazard =
        month *
        weeklyDismissalHazard(manager.confidence, manager.matches,
                              visionOf(manager.team_id).owner) *
        leagueTurnover(*gamedata, manager.team_id);
    if (hazard > 0.0f && uniform(*gamedata, today, manager.team_id) <
                             static_cast<double>(hazard))
    {
      dismissAi(manager, date, inbox, managed_team_id);
      ++events.ai_dismissals;
    }
  }
}

void ManagerCareer::weeklyManagerReview(const GameDateValue& date,
                                        TeamID managed_team_id, Inbox& inbox,
                                        CareerDayEvents& events)
{
  const std::int32_t today = dayOrdinal(date);
  if (!profile.exists) return;
  if (isEmployed())
  {
    profile.career_earnings += profile.contract.weekly_wage;
    return;
  }

  // Out of work: the name fades, down to a floor set by the background,
  // unless he is in charge of a national team.
  const float floor = 0.7f * startingReputation(profile.background);
  if (!national_duty)
    profile.reputation = std::max(std::min(profile.reputation, floor),
                                  profile.reputation - UNEMPLOYED_DECAY);
  const int idle = daysBetween(profile.unemployed_since, date);
  if (idle < UNSOLICITED_AFTER_DAYS) return;
  // Each board with a vacancy he fits decides on its own whether to call,
  // more readily the longer he has been available; the most prestigious
  // caller wins.
  const double chance =
      std::min(0.45, 0.08 + 0.02 * static_cast<double>(idle / 7));
  TeamID pick = unsolicitedVacancyPick(today, chance);
  if (pick == FREE_AGENTS_TEAM_ID && idle >= DESPERATE_AFTER_DAYS)
    pick = desperateVacancyPick(date, managed_team_id, inbox, events, chance);
  if (pick != FREE_AGENTS_TEAM_ID) makeOffer(pick, date, true, 1.0f, inbox);
}

TeamID ManagerCareer::unsolicitedVacancyPick(std::int32_t today, double chance)
{
  TeamID pick = FREE_AGENTS_TEAM_ID;
  for (const Vacancy& vacancy : vacancies)
  {
    const std::uint8_t reputation = clubReputation(vacancy.team_id);
    // The smallest clubs always consider a coach who is available.
    if (static_cast<float>(reputation) >
            std::max(profile.reputation + 5.0f, 44.0f) ||
        std::ranges::any_of(
            offers, [vacancy_team_id = vacancy.team_id](const JobOffer& offer)
            { return offer.team_id == vacancy_team_id; }) ||
        uniform(*gamedata, today, mixHash(0x554E'454DULL, vacancy.team_id)) >=
            chance)
      continue;
    if (pick == FREE_AGENTS_TEAM_ID || reputation > clubReputation(pick))
      pick = vacancy.team_id;
  }
  return pick;
}

TeamID ManagerCareer::desperateVacancyPick(const GameDateValue& date,
                                           TeamID managed_team_id, Inbox& inbox,
                                           CareerDayEvents& events,
                                           double chance)
{
  if (uniform(*gamedata, dayOrdinal(date), 0x4445'5350ULL) >= chance)
    return FREE_AGENTS_TEAM_ID;
  // Long out of work: a struggling club further down makes a change.
  AiManager* weakest = nullptr;
  for (AiManager& manager : managers)
  {
    if (manager.team_id == FREE_AGENTS_TEAM_ID ||
        manager.team_id == managed_team_id ||
        static_cast<float>(clubReputation(manager.team_id)) >
            std::max(profile.reputation + 5.0f, 50.0f))
      continue;
    if (!weakest || manager.confidence < weakest->confidence)
      weakest = &manager;
  }
  if (!weakest || weakest->confidence >= INITIAL_CONFIDENCE)
    return FREE_AGENTS_TEAM_ID;
  const TeamID pick = weakest->team_id;
  dismissAi(*weakest, date, inbox, managed_team_id);
  ++events.ai_dismissals;
  return pick;
}

void ManagerCareer::monthlyReviews(const GameDateValue& date,
                                   TeamID managed_team_id,
                                   float board_confidence,
                                   int board_expected_position,
                                   const PositionProvider& positions,
                                   Inbox& inbox, CareerDayEvents& events)
{
  if (!inSeason(date) || !positions) return;
  // Expected finish of AI clubs: their reputation rank in the league.
  std::map<LeagueID, std::vector<std::pair<std::uint8_t, TeamID>>> ranks;
  for (const auto& [team_id, team] : gamedata->getTeams())
    if (team_id != FREE_AGENTS_TEAM_ID)
      ranks[team.getLeagueId()].emplace_back(team.getReputation(), team_id);
  for (auto& [league_id, clubs] : ranks)
    std::ranges::sort(clubs, std::greater<>{});
  monthlyAiReviews(ranks, positions);
  if (!isEmployed() || profile.club != managed_team_id) return;
  monthlyManagerReview(date, board_confidence, board_expected_position,
                       positions, ranks, inbox, events);
}

void ManagerCareer::monthlyAiReviews(
    const std::map<LeagueID, std::vector<std::pair<std::uint8_t, TeamID>>>&
        league_ranks,
    const PositionProvider& positions)
{
  for (AiManager& manager : managers)
  {
    if (manager.team_id == FREE_AGENTS_TEAM_ID || manager.matches < 4) continue;
    const int position = positions(manager.team_id);
    if (position <= 0) continue;
    int league_size = 20;
    const int target = expectedLeaguePosition(*gamedata, league_ranks,
                                              manager.team_id, league_size);
    const float quarter =
        std::max(1.0f, static_cast<float>(league_size) / 4.0f);
    const float signal =
        50.0f +
        25.0f * std::clamp(static_cast<float>(target - position) / quarter,
                           -2.0f, 2.0f);
    manager.confidence =
        std::clamp(0.9f * manager.confidence + 0.1f * signal, 0.0f, 100.0f);
  }
}

void ManagerCareer::monthlyManagerReview(
    const GameDateValue& date, float board_confidence,
    int board_expected_position, const PositionProvider& positions,
    const std::map<LeagueID, std::vector<std::pair<std::uint8_t, TeamID>>>&
        league_ranks,
    Inbox& inbox, CareerDayEvents& events)
{
  const TeamID managed_team_id = profile.club;
  const int position = positions(managed_team_id);
  if (position <= 0) return;
  int league_size = 20;
  expectedLeaguePosition(*gamedata, league_ranks, managed_team_id, league_size);
  const int target =
      board_expected_position > 0 ? board_expected_position : position;
  const float quarter = std::max(1.0f, static_cast<float>(league_size) / 4.0f);
  const float margin =
      std::clamp(static_cast<float>(target - position) / quarter, -2.0f, 2.0f);
  const float tier_weight =
      1.0f / static_cast<float>(leagueTier(managed_team_id));
  profile.reputation = std::clamp(
      profile.reputation + 0.25f * margin * tier_weight, 1.0f, 99.0f);

  // Overachieving: a bigger club asks permission to speak to him.
  if (board_confidence < 70.0f || position > target - 2) return;
  if (uniform(*gamedata, dayOrdinal(date), 0x504F'4143ULL) >= 0.35) return;
  const std::uint8_t own = clubReputation(managed_team_id);
  TeamID pick = FREE_AGENTS_TEAM_ID;
  const auto fits = [this, own](TeamID team_id)
  {
    const std::uint8_t reputation = clubReputation(team_id);
    return reputation > own + 3 &&
           static_cast<float>(reputation) <= profile.reputation + 20.0f;
  };
  for (const Vacancy& vacancy : vacancies)
    if (fits(vacancy.team_id) &&
        (pick == FREE_AGENTS_TEAM_ID ||
         clubReputation(vacancy.team_id) > clubReputation(pick)))
      pick = vacancy.team_id;
  if (pick == FREE_AGENTS_TEAM_ID)
  {
    // A club whose board has lost faith in its manager makes the change.
    for (AiManager& manager : managers)
    {
      if (manager.team_id == FREE_AGENTS_TEAM_ID ||
          manager.team_id == managed_team_id || manager.confidence >= 40.0f ||
          !fits(manager.team_id))
        continue;
      pick = manager.team_id;
      dismissAi(manager, date, inbox, managed_team_id);
      ++events.ai_dismissals;
      break;
    }
  }
  if (pick != FREE_AGENTS_TEAM_ID) makeOffer(pick, date, true, 1.1f, inbox);
}

void ManagerCareer::onMatchPlayed(const ManagerMatchEvent& match)
{
  const auto points = [](int own, int other)
  {
    if (own > other) return 3.0f;
    return own == other ? 1.0f : 0.0f;
  };
  const auto updateAiResult =
      [this, &match](TeamID team_id, float earned, float expected)
  {
    if (!match.league_match) return;
    for (AiManager& manager : managers)
    {
      if (manager.team_id != team_id) continue;
      manager.confidence =
          updateConfidence(manager.confidence, manager.form, earned, expected);
      manager.reputation = std::clamp(
          manager.reputation + 0.03f * (earned - expected), 1.0f, 99.0f);
      ++manager.matches;
      break;
    }
  };
  const auto apply = [this, &match, &points, &updateAiResult](
                         TeamID team_id, int own, int other, float expected)
  {
    const float earned = points(own, other);
    if (updateManagedMatchResult(match, team_id, own, other, earned, expected))
      return;
    updateAiResult(team_id, earned, expected);
  };
  apply(match.home_team, match.home_goals, match.away_goals,
        match.home_expected_points);
  apply(match.away_team, match.away_goals, match.home_goals,
        match.away_expected_points);
}

bool ManagerCareer::updateManagedMatchResult(const ManagerMatchEvent& match,
                                             TeamID team_id, int own, int other,
                                             float earned, float expected)
{
  if (team_id != match.managed_team || !isEmployed() || profile.club != team_id)
    return false;
  if (ManagerStint* stint = currentStint())
  {
    ++stint->played;
    if (own > other)
      ++stint->won;
    else if (own == other)
      ++stint->drawn;
    else
      ++stint->lost;
  }
  profile.reputation =
      std::clamp(profile.reputation + 0.06f * (earned - expected), 1.0f, 99.0f);
  return true;
}

void ManagerCareer::reviewAiManagersAtSeasonEnd(
    const GameDateValue& date, TeamID managed_team_id,
    const PositionProvider& positions, Inbox& inbox)
{
  if (!positions) return;
  for (AiManager& manager : managers)
  {
    if (manager.team_id == FREE_AGENTS_TEAM_ID ||
        manager.team_id == managed_team_id)
      continue;
    const auto team = gamedata->getTeam(manager.team_id);
    if (!team) continue;
    const auto league = gamedata->getLeague(team->get().getLeagueId());
    const int league_size =
        league ? static_cast<int>(league->get().getTeamIDs().size()) : 20;
    int expected = 1;
    for (const TeamID other :
         league ? league->get().getTeamIDs() : std::vector<TeamID>{})
      if (clubReputation(other) > team->get().getReputation()) ++expected;
    const float chance = std::min(
        0.9f, seasonEndDismissalChance(positions(manager.team_id), expected,
                                       league_size, manager.confidence,
                                       visionOf(manager.team_id).owner) *
                  leagueTurnover(*gamedata, manager.team_id));
    if (uniform(*gamedata, dayOrdinal(date), mixHash(manager.team_id, 15)) <
        static_cast<double>(chance))
      dismissAi(manager, date, inbox, managed_team_id);
  }
}

void ManagerCareer::ageManagersAtSeasonEnd(const GameDateValue& date)
{
  for (AiManager& manager : managers)
    manager.age = static_cast<std::uint8_t>(std::min(manager.age + 1, 99));
  for (const AiManager& manager : managers)
    if (manager.age >= AI_RETIREMENT_AGE &&
        manager.team_id != FREE_AGENTS_TEAM_ID)
      openVacancy(manager.team_id, date);
  std::erase_if(managers, [](const AiManager& manager)
                { return manager.age >= AI_RETIREMENT_AGE; });
  topUpFreePool(date);
}

float ManagerCareer::applySeasonHonours(
    const ManagerSeasonLine& line, std::span<const SeasonHistoryEntry> finished,
    std::uint16_t start_year)
{
  const TeamID club = line.team_id;
  const std::uint8_t tier = leagueTier(club);
  float delta = std::clamp(
      1.2f * static_cast<float>(line.expected_position - line.position), -8.0f,
      10.0f);
  const bool title =
      applyCompetitionHonours(line, finished, start_year, tier, delta);
  if (line.position > 0 && (line.position + 4 <= line.expected_position ||
                            (title && line.expected_position > 2)))
    recordAward(ManagerAwardKind::ManagerOfTheSeason, start_year, club);
  float tier_divisor = 2.5f;
  if (tier <= 1)
    tier_divisor = 1.0f;
  else if (tier == 2)
    tier_divisor = 1.6f;
  delta /= tier_divisor;
  if (delta > 0.0f &&
      profile.reputation > static_cast<float>(clubReputation(club)) + 10.0f)
    delta *= 0.5f;
  return delta;
}

bool ManagerCareer::applyCompetitionHonours(
    const ManagerSeasonLine& line, std::span<const SeasonHistoryEntry> finished,
    std::uint16_t start_year, std::uint8_t tier, float& delta)
{
  using enum ManagerAwardKind;
  const TeamID club = line.team_id;
  bool title = false;
  for (const SeasonHistoryEntry& entry : finished)
  {
    const bool cup = entry.competition_type == MatchType::CUP;
    if (!cup && entry.competition_id != line.league_id) continue;
    if (entry.champion_id == club)
    {
      recordAward(cup ? CupWin : LeagueTitle, start_year, club);
      float award_delta = 3.0f;
      if (!cup && tier <= 1) award_delta = 6.0f;
      delta += award_delta;
      title = title || !cup;
    }
    if (!cup && std::ranges::contains(entry.promoted, club))
    {
      recordAward(Promotion, start_year, club);
      delta += 3.0f;
    }
    if (!cup && std::ranges::contains(entry.relegated, club)) delta -= 4.0f;
  }
  return title;
}

bool ManagerCareer::onSeasonEnd(const GameDateValue& date,
                                TeamID managed_team_id, int expected_position,
                                float board_confidence,
                                const PositionProvider& positions,
                                std::span<const SeasonHistoryEntry> finished,
                                Inbox& inbox)
{
  const GameDateValue last_day = addDays(date, -1);
  const auto start_year = static_cast<std::uint16_t>(
      last_day.month >= 7 ? last_day.year : last_day.year - 1);

  reviewAiManagersAtSeasonEnd(date, managed_team_id, positions, inbox);
  ageManagersAtSeasonEnd(date);

  if (!profile.exists) return false;
  profile.age = static_cast<std::uint8_t>(std::min(profile.age + 1, 99));
  if (!isEmployed() || profile.club != managed_team_id) return false;

  const TeamID club = profile.club;
  const auto team = gamedata->getTeam(club);
  ManagerSeasonLine line;
  line.start_year = start_year;
  line.team_id = club;
  line.club_name = clubName(club);
  line.league_id = team ? team->get().getLeagueId() : 0;
  line.position = static_cast<std::uint8_t>(
      std::clamp(positions ? positions(club) : 0, 0, 255));
  if (const auto league = gamedata->getLeague(line.league_id))
    line.league_size = static_cast<std::uint8_t>(
        std::min<std::size_t>(league->get().getTeamIDs().size(), 255));
  line.expected_position = static_cast<std::uint8_t>(std::clamp(
      expected_position > 0 ? expected_position : line.position, 0, 255));
  seasons.emplace_back(line);
  ++profile.seasons_managed;

  const std::uint8_t tier = leagueTier(club);
  const float delta = applySeasonHonours(line, finished, start_year);
  profile.reputation = std::clamp(profile.reputation + delta, 1.0f, 99.0f);

  if (date < profile.contract.expires) return false;
  if (board_confidence < 35.0f) return true;
  // A content board renews for two more seasons at the going rate.
  profile.contract.weekly_wage =
      std::max(profile.contract.weekly_wage,
               offerWage(clubReputation(club), profile.reputation, tier));
  profile.contract.expires = seasonEnd(date, 2);
  profile.contract.release_compensation = profile.contract.weekly_wage * 52;
  post(inbox, date,
       {InboxCategory::Board,
        "INBOX_MANAGER_RENEWED_TITLE",
        "INBOX_MANAGER_RENEWED_BODY",
        {clubName(club), formatMoney(profile.contract.weekly_wage)},
        club});
  return false;
}

// ---- Persistence -----------------------------------------------------------

void ManagerCareer::load(const std::shared_ptr<DatabaseConnection>& db_conn)
{
  const DatabaseConnection& db = *db_conn;
  profile = ManagerProfile{};
  stints.clear();
  seasons.clear();
  awards.clear();
  managers.clear();
  vacancies.clear();
  applications.clear();
  offers.clear();
  next_manager_id = 1;
  next_offer_id = 1;
  forEachRow(db,
             "SELECT next_manager_id, next_offer_id FROM ManagerMarketState "
             "WHERE id = 1;",
             [this](sqlite3_stmt* stmt)
             {
               next_manager_id = columnAs<std::uint32_t>(stmt, 0);
               next_offer_id = columnAs<std::uint32_t>(stmt, 1);
             });
  forEachRow(
      db,
      "SELECT first_name, last_name, nationality, age, reputation, licence, "
      "licence_days, style, background, club_id, contract_wage, "
      "contract_start, contract_expires, release_compensation, "
      "unemployed_since, career_earnings, seasons_managed "
      "FROM ManagerProfile WHERE id = 1;",
      [this](sqlite3_stmt* stmt) { profile = readManagerProfile(stmt); });
  forEachRow(db,
             "SELECT team_id, club_name, league_id, start_date, end_date, "
             "reason, played, won, drawn, lost, trophies FROM ManagerStints "
             "ORDER BY seq;",
             [this](sqlite3_stmt* stmt)
             {
               ManagerStint stint;
               stint.team_id = columnAs<TeamID>(stmt, 0);
               stint.club_name = columnText(stmt, 1);
               stint.league_id = columnAs<LeagueID>(stmt, 2);
               stint.start = columnDate(stmt, 3);
               stint.end = columnDate(stmt, 4);
               stint.reason =
                   enumFrom(sqlite3_column_int64(stmt, 5), DepartureReason{5},
                            DepartureReason::Moved);
               stint.played = columnAs<std::uint16_t>(stmt, 6);
               stint.won = columnAs<std::uint16_t>(stmt, 7);
               stint.drawn = columnAs<std::uint16_t>(stmt, 8);
               stint.lost = columnAs<std::uint16_t>(stmt, 9);
               stint.trophies = columnAs<std::uint8_t>(stmt, 10);
               stints.emplace_back(std::move(stint));
             });
  forEachRow(db,
             "SELECT start_year, team_id, club_name, league_id, position, "
             "league_size, expected_position FROM ManagerSeasons ORDER BY seq;",
             [this](sqlite3_stmt* stmt)
             {
               ManagerSeasonLine line;
               line.start_year = columnAs<std::uint16_t>(stmt, 0);
               line.team_id = columnAs<TeamID>(stmt, 1);
               line.club_name = columnText(stmt, 2);
               line.league_id = columnAs<LeagueID>(stmt, 3);
               line.position = columnAs<std::uint8_t>(stmt, 4);
               line.league_size = columnAs<std::uint8_t>(stmt, 5);
               line.expected_position = columnAs<std::uint8_t>(stmt, 6);
               seasons.emplace_back(std::move(line));
             });
  forEachRow(db,
             "SELECT start_year, kind, team_id, club_name FROM ManagerAwards "
             "ORDER BY seq;",
             [this](sqlite3_stmt* stmt)
             {
               awards.emplace_back(
                   columnAs<std::uint16_t>(stmt, 0),
                   enumFrom(sqlite3_column_int64(stmt, 1), ManagerAwardKind{5},
                            ManagerAwardKind::ManagerOfTheSeason),
                   columnAs<TeamID>(stmt, 2), columnText(stmt, 3));
             });
  forEachRow(db,
             "SELECT id, first_name, last_name, nationality, age, reputation, "
             "ability, style, team_id, appointed, matches, confidence, form "
             "FROM AiManagers ORDER BY id;",
             [this](sqlite3_stmt* stmt)
             {
               AiManager manager = readAiManager(stmt);
               next_manager_id = std::max(next_manager_id, manager.id + 1);
               managers.emplace_back(std::move(manager));
             });
  forEachRow(db,
             "SELECT team_id, opened, fill_date FROM ManagerVacancies "
             "ORDER BY opened, team_id;",
             [this](sqlite3_stmt* stmt)
             {
               vacancies.emplace_back(columnAs<TeamID>(stmt, 0),
                                      columnDate(stmt, 1), columnDate(stmt, 2));
             });
  forEachRow(db,
             "SELECT team_id, applied, respond_date, stage "
             "FROM ManagerApplications ORDER BY applied, team_id;",
             [this](sqlite3_stmt* stmt)
             {
               applications.emplace_back(
                   columnAs<TeamID>(stmt, 0), columnDate(stmt, 1),
                   columnDate(stmt, 2),
                   enumFrom(sqlite3_column_int64(stmt, 3), ApplicationStage{4},
                            ApplicationStage::Rejected));
             });
  forEachRow(
      db,
      "SELECT id, team_id, made, expires, weekly_wage, years, "
      "release_compensation, max_wage, max_years, rounds_left, unsolicited, "
      "compensation FROM ManagerJobOffers ORDER BY id;",
      [this](sqlite3_stmt* stmt)
      {
        JobOffer offer = readJobOffer(stmt);
        next_offer_id = std::max(next_offer_id, offer.id + 1);
        offers.emplace_back(std::move(offer));
      });
}

void ManagerCareer::save(
    const std::shared_ptr<DatabaseConnection>& db_conn) const
{
  const DatabaseConnection& db = *db_conn;
  for (const char* table :
       {"ManagerMarketState", "ManagerProfile", "ManagerStints",
        "ManagerSeasons", "ManagerAwards", "AiManagers", "ManagerVacancies",
        "ManagerApplications", "ManagerJobOffers"})
  {
    sqlite3_exec(db.getRaw(),
                 (std::string("DELETE FROM ") + table + ";").c_str(), nullptr,
                 nullptr, nullptr);
  }
  sqlite3_stmt* stmt = db.prepareStatement(
      "INSERT INTO ManagerMarketState (id, next_manager_id, next_offer_id) "
      "VALUES (1, ?, ?);");
  sqlite3_bind_int64(stmt, 1, next_manager_id);
  sqlite3_bind_int64(stmt, 2, next_offer_id);
  db.executeStep(stmt);
  sqlite3_finalize(stmt);

  if (profile.exists)
  {
    stmt = db.prepareStatement(
        "INSERT INTO ManagerProfile (id, first_name, last_name, nationality, "
        "age, reputation, licence, licence_days, style, background, club_id, "
        "contract_wage, contract_start, contract_expires, "
        "release_compensation, unemployed_since, career_earnings, "
        "seasons_managed) VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?);");
    bindText(stmt, 1, profile.first_name);
    bindText(stmt, 2, profile.last_name);
    sqlite3_bind_int(stmt, 3, std::to_underlying(profile.nationality));
    sqlite3_bind_int(stmt, 4, profile.age);
    sqlite3_bind_double(stmt, 5, profile.reputation);
    sqlite3_bind_int(stmt, 6, std::to_underlying(profile.licence));
    sqlite3_bind_int(stmt, 7, profile.licence_days);
    sqlite3_bind_int(stmt, 8, std::to_underlying(profile.style));
    sqlite3_bind_int(stmt, 9, std::to_underlying(profile.background));
    sqlite3_bind_int(stmt, 10, profile.club);
    sqlite3_bind_int64(stmt, 11, profile.contract.weekly_wage);
    bindDate(stmt, 12, profile.contract.start);
    bindDate(stmt, 13, profile.contract.expires);
    sqlite3_bind_int64(stmt, 14, profile.contract.release_compensation);
    bindDate(stmt, 15, profile.unemployed_since);
    sqlite3_bind_int64(stmt, 16, profile.career_earnings);
    sqlite3_bind_int(stmt, 17, profile.seasons_managed);
    db.executeStep(stmt);
    sqlite3_finalize(stmt);
  }

  insertAll(db,
            "INSERT INTO ManagerStints (team_id, club_name, league_id, "
            "start_date, end_date, reason, played, won, drawn, lost, "
            "trophies) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            stints,
            [](sqlite3_stmt* row, const ManagerStint& stint)
            {
              sqlite3_bind_int(row, 1, stint.team_id);
              bindText(row, 2, stint.club_name);
              sqlite3_bind_int(row, 3, stint.league_id);
              bindDate(row, 4, stint.start);
              bindDate(row, 5, stint.end);
              sqlite3_bind_int(row, 6, std::to_underlying(stint.reason));
              sqlite3_bind_int(row, 7, stint.played);
              sqlite3_bind_int(row, 8, stint.won);
              sqlite3_bind_int(row, 9, stint.drawn);
              sqlite3_bind_int(row, 10, stint.lost);
              sqlite3_bind_int(row, 11, stint.trophies);
            });
  insertAll(db,
            "INSERT INTO ManagerSeasons (start_year, team_id, club_name, "
            "league_id, position, league_size, expected_position) "
            "VALUES (?, ?, ?, ?, ?, ?, ?);",
            seasons,
            [](sqlite3_stmt* row, const ManagerSeasonLine& line)
            {
              sqlite3_bind_int(row, 1, line.start_year);
              sqlite3_bind_int(row, 2, line.team_id);
              bindText(row, 3, line.club_name);
              sqlite3_bind_int(row, 4, line.league_id);
              sqlite3_bind_int(row, 5, line.position);
              sqlite3_bind_int(row, 6, line.league_size);
              sqlite3_bind_int(row, 7, line.expected_position);
            });
  insertAll(db,
            "INSERT INTO ManagerAwards (start_year, kind, team_id, club_name) "
            "VALUES (?, ?, ?, ?);",
            awards,
            [](sqlite3_stmt* row, const ManagerAward& award)
            {
              sqlite3_bind_int(row, 1, award.start_year);
              sqlite3_bind_int(row, 2, std::to_underlying(award.kind));
              sqlite3_bind_int(row, 3, award.team_id);
              bindText(row, 4, award.club_name);
            });
  insertAll(db,
            "INSERT INTO AiManagers (id, first_name, last_name, nationality, "
            "age, reputation, ability, style, team_id, appointed, matches, "
            "confidence, form) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            managers,
            [](sqlite3_stmt* row, const AiManager& manager)
            {
              sqlite3_bind_int64(row, 1, manager.id);
              bindText(row, 2, manager.first_name);
              bindText(row, 3, manager.last_name);
              sqlite3_bind_int(row, 4, std::to_underlying(manager.nationality));
              sqlite3_bind_int(row, 5, manager.age);
              sqlite3_bind_double(row, 6, manager.reputation);
              sqlite3_bind_double(row, 7, manager.ability);
              sqlite3_bind_int(row, 8, std::to_underlying(manager.style));
              sqlite3_bind_int(row, 9, manager.team_id);
              bindDate(row, 10, manager.appointed);
              sqlite3_bind_int(row, 11, manager.matches);
              sqlite3_bind_double(row, 12, manager.confidence);
              sqlite3_bind_double(row, 13, manager.form);
            });
  insertAll(db,
            "INSERT INTO ManagerVacancies (team_id, opened, fill_date) "
            "VALUES (?, ?, ?);",
            vacancies,
            [](sqlite3_stmt* row, const Vacancy& vacancy)
            {
              sqlite3_bind_int(row, 1, vacancy.team_id);
              bindDate(row, 2, vacancy.opened);
              bindDate(row, 3, vacancy.fill_date);
            });
  insertAll(db,
            "INSERT INTO ManagerApplications (team_id, applied, "
            "respond_date, stage) VALUES (?, ?, ?, ?);",
            applications,
            [](sqlite3_stmt* row, const JobApplication& application)
            {
              sqlite3_bind_int(row, 1, application.team_id);
              bindDate(row, 2, application.applied);
              bindDate(row, 3, application.respond_date);
              sqlite3_bind_int(row, 4, std::to_underlying(application.stage));
            });
  insertAll(db,
            "INSERT INTO ManagerJobOffers (id, team_id, made, expires, "
            "weekly_wage, years, release_compensation, max_wage, max_years, "
            "rounds_left, unsolicited, compensation) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            offers,
            [](sqlite3_stmt* row, const JobOffer& offer)
            {
              sqlite3_bind_int64(row, 1, offer.id);
              sqlite3_bind_int(row, 2, offer.team_id);
              bindDate(row, 3, offer.made);
              bindDate(row, 4, offer.expires);
              sqlite3_bind_int64(row, 5, offer.weekly_wage);
              sqlite3_bind_int(row, 6, offer.years);
              sqlite3_bind_int64(row, 7, offer.release_compensation);
              sqlite3_bind_int64(row, 8, offer.max_wage);
              sqlite3_bind_int(row, 9, offer.max_years);
              sqlite3_bind_int(row, 10, offer.rounds_left);
              sqlite3_bind_int(row, 11, offer.unsolicited ? 1 : 0);
              sqlite3_bind_int64(row, 12, offer.compensation);
            });
}

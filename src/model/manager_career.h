// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "global/languages.h"
#include "global/types.h"
#include "model/gamedate.h"

class DatabaseConnection;
class GameData;
class Inbox;
struct SeasonHistoryEntry;

/** @brief Where a new manager comes from (sets reputation and licence). */
enum class ManagerBackground : std::uint8_t
{
  SundayLeague = 0,
  SemiProfessional,
  ProfessionalPlayer,
  TopFlightPlayer,
  FormerInternational,
  COUNT
};

/** @brief Coaching qualification, lowest first (values are persisted). */
enum class CoachingLicence : std::uint8_t
{
  None = 0,
  C,
  B,
  A,
  Pro,
  COUNT
};

/** @brief Preferred way of playing (values are persisted). */
enum class ManagerStyle : std::uint8_t
{
  Balanced = 0,
  Possession,
  Pressing,
  Counter,
  Direct,
  COUNT
};

/** @brief How far a manager's name carries. */
enum class ReputationTier : std::uint8_t
{
  Local = 0,
  National,
  Continental,
  World
};

/** @brief Why a spell in charge ended (values are persisted). */
enum class DepartureReason : std::uint8_t
{
  Current = 0, /*!< Still in charge. */
  Sacked,
  Resigned,
  Moved, /*!< Left for another club. */
  ContractExpired
};

/** @brief Owner behaviour: scales the patience of the board. */
enum class OwnerType : std::uint8_t
{
  Patient = 0,
  Ambitious,
  ImpatientBenefactor,
  MemberOwned
};

/** @brief Honours of the manager (values are persisted). */
enum class ManagerAwardKind : std::uint8_t
{
  LeagueTitle = 0,
  CupWin,
  Promotion,
  ManagerOfTheSeason,
  ManagerOfTheMonth
};

/** @brief Terms of a manager's employment. */
struct ManagerContract
{
  std::int64_t weekly_wage = 0;
  GameDateValue start = GameDateValue();
  GameDateValue expires = GameDateValue(); /*!< 30 June of the final season. */
  std::int64_t release_compensation = 0;   /*!< Paid by a club poaching him. */
};

/** @brief The human manager. */
struct ManagerProfile
{
  bool exists = false;
  std::string first_name;
  std::string last_name;
  Language nationality = Language::EN;
  std::uint8_t age = 40;
  float reputation = 30.0f; /*!< 1-100, same scale as club reputation. */
  CoachingLicence licence = CoachingLicence::B;
  /** Days of coaching towards the next licence (doubled on a course). */
  std::uint16_t licence_days = 0;
  ManagerStyle style = ManagerStyle::Balanced;
  ManagerBackground background = ManagerBackground::ProfessionalPlayer;
  TeamID club = 0; /*!< FREE_AGENTS_TEAM_ID while unemployed. */
  ManagerContract contract;
  GameDateValue unemployed_since = GameDateValue();
  std::int64_t career_earnings = 0;
  std::uint16_t seasons_managed = 0; /*!< Completed seasons in a job. */

  std::string name() const { return first_name + " " + last_name; }
};

/** @brief What the new-game manager step collects. */
struct ManagerSetup
{
  std::string first_name;
  std::string last_name;
  Language nationality = Language::EN;
  std::uint8_t age = 40;
  ManagerBackground background = ManagerBackground::ProfessionalPlayer;
  ManagerStyle style = ManagerStyle::Balanced;
};

/** @brief One spell in charge of a club. */
struct ManagerStint
{
  TeamID team_id = 0;
  std::string club_name;
  LeagueID league_id = 0;
  GameDateValue start = GameDateValue();
  GameDateValue end =
      GameDateValue(); /*!< Meaningful once reason != Current. */
  DepartureReason reason = DepartureReason::Current;
  std::uint16_t played = 0; /*!< Competitive matches. */
  std::uint16_t won = 0;
  std::uint16_t drawn = 0;
  std::uint16_t lost = 0;
  std::uint8_t trophies = 0;

  float winRate() const
  {
    return played == 0 ? 0.0f
                       : static_cast<float>(won) / static_cast<float>(played);
  }
};

/** @brief The manager's club at the end of a season. */
struct ManagerSeasonLine
{
  std::uint16_t start_year = 0;
  TeamID team_id = 0;
  std::string club_name;
  LeagueID league_id = 0;
  std::uint8_t position = 0;
  std::uint8_t league_size = 0;
  std::uint8_t expected_position = 0;
};

struct ManagerAward
{
  std::uint16_t start_year = 0;
  ManagerAwardKind kind = ManagerAwardKind::LeagueTitle;
  TeamID team_id = 0;
  std::string club_name;
};

/** @brief A computer-controlled manager, employed or looking for work. */
struct AiManager
{
  std::uint32_t id = 0;
  std::string first_name;
  std::string last_name;
  Language nationality = Language::EN;
  std::uint8_t age = 50;
  float reputation = 40.0f;
  float ability = 50.0f; /*!< Coaching quality, 1-100. */
  ManagerStyle style = ManagerStyle::Balanced;
  TeamID team_id = 0; /*!< 0 = out of work. */
  GameDateValue appointed = GameDateValue();
  std::uint16_t matches = 0; /*!< League matches in the current job. */
  float confidence = 60.0f;  /*!< Board confidence, 0-100. */
  float form = 0.0f;         /*!< Smoothed points minus expected points. */

  std::string name() const { return first_name + " " + last_name; }
};

/** @brief A club without a permanent manager. */
struct Vacancy
{
  TeamID team_id = 0;
  GameDateValue opened = GameDateValue();
  GameDateValue fill_date =
      GameDateValue(); /*!< The board appoints someone by then. */
};

enum class ApplicationStage : std::uint8_t
{
  Pending = 0, /*!< Waiting for the board's answer. */
  Interview,   /*!< Invited to an interview. */
  Rejected,
  Offered
};

struct JobApplication
{
  TeamID team_id = 0;
  GameDateValue applied = GameDateValue();
  GameDateValue respond_date = GameDateValue();
  ApplicationStage stage = ApplicationStage::Pending;
};

/** @brief A club's contract offer to the human manager. */
struct JobOffer
{
  std::uint32_t id = 0;
  TeamID team_id = 0;
  GameDateValue made = GameDateValue();
  GameDateValue expires = GameDateValue();
  std::int64_t weekly_wage = 0;
  std::uint8_t years = 2;
  std::int64_t release_compensation = 0;
  std::int64_t max_wage = 0; /*!< Hidden ceiling of the board. */
  std::uint8_t max_years = 3;
  std::uint8_t rounds_left = 3;
  bool unsolicited = false;
  /** Paid by this club to the manager's current employer on acceptance. */
  std::int64_t compensation = 0;
};

/** @brief What a club's board looks for in a manager. */
struct ClubVision
{
  OwnerType owner = OwnerType::Patient;
  ManagerStyle preferred_style = ManagerStyle::Balanced;
  float ambition = 0.5f;     /*!< 0 = survive, 1 = win everything. */
  float youth_focus = 0.5f;  /*!< 0 = buy proven, 1 = academy first. */
  float tight_budget = 0.5f; /*!< 0 = rich, 1 = every euro counts. */
};

/** @brief Interview topics, asked in this order. */
enum class InterviewTopic : std::uint8_t
{
  Style = 0,
  Ambition,
  Youth,
  Budget,
  COUNT
};

inline constexpr std::size_t INTERVIEW_TOPICS =
    static_cast<std::size_t>(std::to_underlying(InterviewTopic::COUNT));
inline constexpr std::size_t INTERVIEW_OPTIONS = 3;

enum class ApplyResult : std::uint8_t
{
  Ok = 0,
  NoVacancy,
  AlreadyApplied,
  OwnClub,
  RecentlyLeft, /*!< Sacked or resigned there within the last year. */
  NoProfile
};

/** @brief Outcome of a completed interview. */
struct InterviewResult
{
  bool offered = false;
  int score = 0; /*!< Sum of the answers' fit, -4..8. */
  std::array<int, INTERVIEW_TOPICS> answer_fit{};
};

enum class OfferReply : std::uint8_t
{
  Accepted = 0, /*!< Terms agreed, the offer now carries them. */
  Improved,     /*!< The board moved towards the demand. */
  Withdrawn     /*!< The board walked away. */
};

/** @brief What changed during one simulated day. */
struct CareerDayEvents
{
  bool new_offer = false;
  bool interview_invitation = false;
  int ai_dismissals = 0;
  int ai_appointments = 0;
};

/** A competitive result and the board expectations used by career reviews. */
struct ManagerMatchEvent
{
  TeamID home_team = 0;
  TeamID away_team = 0;
  int home_goals = 0;
  int away_goals = 0;
  bool league_match = false;
  float home_expected_points = 0.0f;
  float away_expected_points = 0.0f;
  TeamID managed_team;
};

/**
 * Pure rules of the manager market, shared by the career and its tests.
 * [P] marks tunable estimates; see test_manager_career.cpp for the
 * calibration (top-flight manager changes per season, dismissed tenure).
 */
namespace ManagerMarketModel
{
inline constexpr std::size_t MAX_CONTRACT_YEARS = 4;

ReputationTier reputationTier(float reputation);
const char* reputationTierKey(ReputationTier tier);
const char* licenceKey(CoachingLicence licence);
const char* styleKey(ManagerStyle style);
const char* backgroundKey(ManagerBackground background);
const char* ownerKey(OwnerType owner);
const char* departureKey(DepartureReason reason);
const char* awardKey(ManagerAwardKind kind);

/** Starting reputation, licence and age range of a background. */
float startingReputation(ManagerBackground background);
CoachingLicence startingLicence(ManagerBackground background);

/** Licence a board expects for a club of this stature and division. */
CoachingLicence requiredLicence(std::uint8_t club_reputation,
                                std::uint8_t tier);

/**
 * Probability that a board invites the manager to an interview: logistic in
 * manager reputation minus club reputation, minus a penalty per missing
 * licence level, plus @p fit (style and nationality match with the board,
 * in logistic units); clamped to [0.02, 0.95].
 */
float applicationChance(float manager_reputation, CoachingLicence licence,
                        std::uint8_t club_reputation, std::uint8_t tier,
                        float fit = 0.0f);

/** Weekly wage a club offers (whole euros). */
std::int64_t offerWage(std::uint8_t club_reputation, float manager_reputation,
                       std::uint8_t tier);

/**
 * Board confidence after a league match: an exponentially smoothed
 * points-minus-expected form feeds B = 0.9 B + 0.1 (50 + 25 z).
 */
float updateConfidence(float confidence, float& form, float points,
                       float expected_points);

/**
 * Weekly dismissal hazard h0 * exp(-beta (B - 50) / 25) during the season,
 * damped over the first ten matches and scaled by owner patience. There is
 * no bounce term: a new manager starts from neutral confidence only.
 */
float weeklyDismissalHazard(float confidence, std::uint16_t matches,
                            OwnerType owner);

/** Probability of a season-end dismissal after finishing @p position. */
float seasonEndDismissalChance(int position, int expected_position,
                               int league_size, float confidence,
                               OwnerType owner);

/** Sacking season: weight of the in-season hazard by month (0 = none). */
float monthFactor(std::uint8_t month);

/** Deterministic board profile of a club. */
ClubVision clubVision(std::uint64_t world_seed, TeamID team_id,
                      std::uint8_t reputation, std::uint8_t youth_facilities,
                      std::int64_t balance, std::int64_t yearly_income);

/** Key of a topic's question and of one of its options. */
const char* interviewQuestionKey(InterviewTopic topic);
const char* interviewOptionKey(InterviewTopic topic, std::size_t option);

/** Fit of one answer with the board's vision, -1..2. */
int interviewAnswerFit(const ClubVision& vision, InterviewTopic topic,
                       std::size_t option);
}  // namespace ManagerMarketModel

/**
 * @class ManagerCareer
 * @brief The human manager's career and the market for managers.
 *
 * Owned by Game next to the world simulation. Every club has a manager: the
 * human at the managed club, an AI manager elsewhere (generated for worlds
 * that have none). AI managers lose their jobs when results stay below the
 * board's expectation (weekly hazard during the season plus a season-end
 * review); a vacancy stays open for a few weeks and is then filled by the
 * free AI manager whose reputation fits the club best. The human applies
 * for vacancies, is interviewed, receives offers (also unsolicited ones when
 * overachieving or out of work for long), negotiates the contract, resigns,
 * or is dismissed by the board. Out of work, time keeps running and the
 * reputation fades slowly. All randomness comes from the Managers stream.
 */
class ManagerCareer
{
 public:
  /** League position of a club in the current table (0 if unknown). */
  using PositionProvider = std::function<int(TeamID)>;

  explicit ManagerCareer(std::shared_ptr<GameData> gamedata);

  // ---- Profile ----

  bool hasProfile() const { return profile.exists; }
  const ManagerProfile& getProfile() const { return profile; }
  bool isEmployed() const;

  /** Creates the human manager from the new-game step. */
  void createProfile(const ManagerSetup& setup, const GameDateValue& date);

  /** A profile for careers started before the manager step existed. */
  void ensureProfile(TeamID managed_team_id, const GameDateValue& date);

  // ---- History ----

  const std::vector<ManagerStint>& getStints() const { return stints; }
  const std::vector<ManagerSeasonLine>& getSeasons() const { return seasons; }
  const std::vector<ManagerAward>& getAwards() const { return awards; }

  // ---- Managers of the world ----

  /** Gives every club without one an AI manager and fills the free pool. */
  void ensureClubManagers(const GameDateValue& date, TeamID managed_team_id);
  const AiManager* clubManager(TeamID team_id) const;
  const std::vector<AiManager>& getAiManagers() const { return managers; }

  // ---- Job market ----

  const std::vector<Vacancy>& getVacancies() const { return vacancies; }
  const Vacancy* findVacancy(TeamID team_id) const;
  const std::vector<JobApplication>& getApplications() const
  {
    return applications;
  }
  const JobApplication* findApplication(TeamID team_id) const;
  const std::vector<JobOffer>& getOffers() const { return offers; }
  const JobOffer* findOffer(std::uint32_t offer_id) const;

  ClubVision visionOf(TeamID team_id) const;
  /** Chance of an interview invitation from @p team_id's board. */
  float applicationChance(TeamID team_id) const;

  ApplyResult apply(TeamID team_id, const GameDateValue& date);

  /**
   * Answers the four interview questions (one option index per topic). The
   * board decides on the spot: an offer (terms shaped by the answers) or a
   * polite refusal. nullopt without an interview invitation.
   */
  std::optional<InterviewResult> interview(
      TeamID team_id, std::span<const std::uint8_t> answers,
      const GameDateValue& date, Inbox& inbox);

  /** Counter-proposal on wage and length. */
  OfferReply negotiate(std::uint32_t offer_id, std::int64_t weekly_wage,
                       std::uint8_t years, const GameDateValue& date);
  bool decline(std::uint32_t offer_id);
  /** Removes and returns an offer that is still valid on @p date. */
  std::optional<JobOffer> takeOffer(std::uint32_t offer_id,
                                    const GameDateValue& date);

  // ---- Transitions (Game moves the managed-club bindings) ----

  /**
   * The human takes charge of @p team_id: a new spell starts, the club's
   * vacancy closes and its AI manager (if any) becomes available.
   */
  void startJob(TeamID team_id, const ManagerContract& contract,
                const GameDateValue& date);

  /**
   * The human leaves the managed club: the spell closes and the club gets a
   * vacancy. A sacked manager receives the remaining wages (at most a year),
   * booked on the club's ledger and announced in the inbox. Returns them.
   */
  std::int64_t leaveJob(DepartureReason reason, const GameDateValue& date,
                        Inbox& inbox);

  /**
   * The club that hired the manager away pays his former club the release
   * compensation of his contract (both ledgers).
   */
  void payCompensation(TeamID from_club, TeamID to_club, std::int64_t amount,
                       const GameDateValue& date);

  /**
   * The board of @p team_id dismisses its AI manager (results, a takeover,
   * a better candidate): a vacancy opens. False without an AI manager.
   */
  bool dismissClubManager(TeamID team_id, const GameDateValue& date,
                          Inbox& inbox);

  /** Adds an honour decided elsewhere (e.g. a monthly award). */
  void recordAward(ManagerAwardKind kind, std::uint16_t start_year,
                   TeamID team_id);

  /** Results elsewhere (a national team) move the reputation (1-100). */
  void adjustReputation(float delta);
  /** Wages of a job outside the club game (a national team). */
  void addEarnings(std::int64_t amount);
  /** In charge of a national team: the name does not fade out of work. */
  void setInternationalDuty(bool in_charge) { national_duty = in_charge; }
  bool hasInternationalDuty() const { return national_duty; }

  /** Contract agreed through @p offer, starting on @p date. */
  ManagerContract contractFor(const JobOffer& offer,
                              const GameDateValue& date) const;

  /** Contract of a first appointment from the club choice. */
  ManagerContract initialContract(TeamID team_id,
                                  const GameDateValue& date) const;

  // ---- Hooks ----

  /** Offers, applications, vacancies, AI sackings and reputation. */
  CareerDayEvents onDayAdvanced(const GameDateValue& date,
                                TeamID managed_team_id, float board_confidence,
                                int board_expected_position,
                                const PositionProvider& positions,
                                Inbox& inbox);

  /** A competitive match: AI job security, the human's record. */
  void onMatchPlayed(const ManagerMatchEvent& match);

  /**
   * Season end: season line, honours, reputation, ageing and the boards'
   * season reviews of AI managers. @p finished holds the history rows of the
   * season that just ended, @p positions the final league positions
   * (captured before promotion and relegation). An expiring contract is
   * renewed while the board is content; returns true when it ran out.
   */
  bool onSeasonEnd(const GameDateValue& date, TeamID managed_team_id,
                   int expected_position, float board_confidence,
                   const PositionProvider& positions,
                   std::span<const SeasonHistoryEntry> finished, Inbox& inbox);

  void load(const std::shared_ptr<DatabaseConnection>& db_conn);
  void save(const std::shared_ptr<DatabaseConnection>& db_conn) const;

 private:
  void processApplications(const GameDateValue& date, Inbox& inbox,
                           CareerDayEvents& events);
  void fillVacancies(const GameDateValue& date, TeamID managed_team_id,
                     Inbox& inbox, CareerDayEvents& events);
  void weeklyReviews(const GameDateValue& date, TeamID managed_team_id,
                     Inbox& inbox, CareerDayEvents& events);
  void weeklyAiReviews(const GameDateValue& date, TeamID managed_team_id,
                       Inbox& inbox, CareerDayEvents& events);
  void weeklyManagerReview(const GameDateValue& date, TeamID managed_team_id,
                           Inbox& inbox, CareerDayEvents& events);
  TeamID unsolicitedVacancyPick(std::int32_t today, double chance);
  TeamID desperateVacancyPick(const GameDateValue& date, TeamID managed_team_id,
                              Inbox& inbox, CareerDayEvents& events,
                              double chance);
  void monthlyReviews(const GameDateValue& date, TeamID managed_team_id,
                      float board_confidence, int board_expected_position,
                      const PositionProvider& positions, Inbox& inbox,
                      CareerDayEvents& events);
  void monthlyAiReviews(
      const std::map<LeagueID, std::vector<std::pair<std::uint8_t, TeamID>>>&
          league_ranks,
      const PositionProvider& positions);
  void monthlyManagerReview(
      const GameDateValue& date, float board_confidence,
      int board_expected_position, const PositionProvider& positions,
      const std::map<LeagueID, std::vector<std::pair<std::uint8_t, TeamID>>>&
          league_ranks,
      Inbox& inbox, CareerDayEvents& events);
  void reviewAiManagersAtSeasonEnd(const GameDateValue& date,
                                   TeamID managed_team_id,
                                   const PositionProvider& positions,
                                   Inbox& inbox);
  void ageManagersAtSeasonEnd(const GameDateValue& date);
  bool updateManagedMatchResult(const ManagerMatchEvent& match, TeamID team_id,
                                int own, int other, float earned,
                                float expected);
  float applySeasonHonours(const ManagerSeasonLine& line,
                           std::span<const SeasonHistoryEntry> finished,
                           std::uint16_t start_year);
  bool applyCompetitionHonours(const ManagerSeasonLine& line,
                               std::span<const SeasonHistoryEntry> finished,
                               std::uint16_t start_year, std::uint8_t tier,
                               float& delta);
  void dismissAi(AiManager& manager, const GameDateValue& date, Inbox& inbox,
                 TeamID managed_team_id);
  void openVacancy(TeamID team_id, const GameDateValue& date);
  /** Closes the spell and opens the vacancy, without news or severance. */
  void leaveJobQuietly(DepartureReason reason, const GameDateValue& date);
  bool makeOffer(TeamID team_id, const GameDateValue& date, bool unsolicited,
                 float wage_factor, Inbox& inbox);
  void topUpFreePool(const GameDateValue& date);
  AiManager generateManager(TeamID team_id, float reputation,
                            const GameDateValue& date);
  std::uint8_t leagueTier(TeamID team_id) const;
  std::uint8_t clubReputation(TeamID team_id) const;
  std::string clubName(TeamID team_id) const;
  ManagerStint* currentStint();

  std::shared_ptr<GameData> gamedata;
  ManagerProfile profile;
  std::vector<ManagerStint> stints;
  std::vector<ManagerSeasonLine> seasons;
  std::vector<ManagerAward> awards;
  std::vector<AiManager> managers;
  std::vector<Vacancy> vacancies;
  std::vector<JobApplication> applications;
  std::vector<JobOffer> offers;
  std::uint32_t next_manager_id = 1;
  std::uint32_t next_offer_id = 1;
  bool national_duty = false;
};

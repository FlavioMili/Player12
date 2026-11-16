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
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "global/languages.h"
#include "global/types.h"
#include "model/gamedate.h"
#include "model/player.h"
#include "model/staff.h"

class DatabaseConnection;
class GameData;
class Inbox;
class NameRegistry;
class SquadSurnames;
struct StatsConfig;
class WorldRng;

/** @brief Where an academy player stands (values are persisted). */
enum class YouthStatus : std::uint8_t
{
  Candidate = 0, /*!< Intake trialist waiting for a contract offer. */
  Squad,         /*!< Member of the U18 squad. */
  Graduated,     /*!< Promoted academy product (kept until 21). */
  Reserve        /*!< Member of the U21 (reserve) squad. */
};

/** @brief Contract of an academy player (values are persisted). */
enum class YouthContract : std::uint8_t
{
  None = 0,    /*!< Trialist: nothing signed yet. */
  Scholarship, /*!< Scholarship with a fixed stipend. */
  Professional /*!< First professional contract. */
};

/** @brief Head of youth development's verdict on an intake. */
enum class IntakeQuality : std::uint8_t
{
  Weak = 0,
  Average,
  Promising,
  Golden
};

/** @brief Academy investments the board can approve (values are persisted). */
enum class AcademyUpgrade : std::uint8_t
{
  None = 0,
  Facilities,
  Recruitment
};

/** @brief Outcome of an action on an academy player. */
enum class YouthActionResult : std::uint8_t
{
  Ok,
  NoClub,
  UnknownPlayer,
  NotAllowed, /*!< Wrong status or club for this action. */
  TooYoung,   /*!< Below the first professional contract age. */
  TooOld,     /*!< Beyond the age limit of the squad. */
  OverageFull /*!< The U21 over-age places are taken. */
};

/** @brief Board answer to an academy investment request. */
enum class UpgradeRequestResult : std::uint8_t
{
  Approved,
  NoClub,
  InProgress,    /*!< A project is already being built. */
  TooSoon,       /*!< The board answered a request recently. */
  AtMaximum,     /*!< Nothing left to improve. */
  BeyondStature, /*!< Too ambitious for the club's reputation. */
  CannotAfford,  /*!< Cash (after a payroll reserve) does not cover it. */
  NoConfidence   /*!< The board does not trust the manager enough. */
};

/** @brief Monthly snapshot of an academy player's ability. */
struct YouthProgressPoint
{
  std::int32_t day = 0; /*!< Day ordinal. */
  float overall = 0.0f;
};

/**
 * @struct YouthRecord
 * @brief Academy registration of one player.
 *
 * Academy players remain club players (wages, contracts, training and
 * persistence work as for everyone else) but are listed apart from the
 * senior squad (Team::getAcademyIDs, GameData::getAcademyForTeam); the
 * record tells the U18 squad, trialists and graduates apart and carries
 * youth match statistics.
 */
struct YouthRecord
{
  PlayerID player_id = 0;
  TeamID team_id = 0;
  YouthStatus status = YouthStatus::Squad;
  YouthContract contract = YouthContract::None;
  std::uint8_t joined_age = 15;  /*!< Age when he joined the academy. */
  std::uint16_t appearances = 0; /*!< U18 matches this season. */
  std::uint16_t minutes = 0;
  std::uint16_t goals = 0;
  float rating_total = 0.0f;
  /** Managed club only: monthly ability, oldest first. */
  std::vector<YouthProgressPoint> progress;

  float averageRating() const
  {
    return appearances > 0 ? rating_total / static_cast<float>(appearances)
                           : 0.0f;
  }
};

/** @brief A club's row in its U18 league. */
struct YouthTableRow
{
  TeamID team_id = 0;
  std::uint16_t played = 0;
  std::uint16_t won = 0;
  std::uint16_t drawn = 0;
  std::uint16_t lost = 0;
  std::uint16_t goals_for = 0;
  std::uint16_t goals_against = 0;

  int points() const { return 3 * won + drawn; }
};

/** @brief A U18 or U21 match of the managed club, from its point of view. */
struct YouthResult
{
  GameDateValue date;
  TeamID opponent_id = 0;
  bool home = true;
  std::uint8_t goals_for = 0;
  std::uint8_t goals_against = 0;
};

/** @brief Over-age places used in a club's U21 squad. */
struct ReserveQuota
{
  std::size_t squad = 0;
  std::size_t overage_outfield = 0;
  std::size_t overage_goalkeepers = 0;
};

/**
 * @struct AcademyClub
 * @brief Academy state of one club.
 */
struct AcademyClub
{
  TeamID team_id = 0;
  std::uint8_t recruitment = 50; /*!< 1-100 reach of the scouting network. */
  AcademyUpgrade project = AcademyUpgrade::None;
  std::int32_t project_start_day = 0;
  std::int32_t project_done_day = 0;
  std::uint8_t project_target = 0;
  std::int32_t last_request_day = 0; /*!< Last board decision (0: none). */
  YouthTableRow table;
  YouthTableRow reserve_table; /*!< Row in the country's U21 league. */
  bool reserves_ready = false; /*!< The U21 squad has been set up. */
};

/** @brief Everything that shapes an intake. Qualities are 0-1. */
struct IntakeInputs
{
  float facilities = 0.5f;
  float recruitment = 0.5f;
  float junior_coaching = 0.25f;
  float head_ability = 0.0f; /*!< 0 without a head of youth development. */
  float head_professionalism = 0.5f;
  float staff_bonus = 0.0f; /*!< StaffEffects::youth_potential_bonus. */
  std::uint8_t reputation = 50;
  Language country = Language::EN;
};

/** @brief Hidden profile of one intake candidate. */
struct CandidateProfile
{
  PlayerRole role = PlayerRole::CM;
  std::uint8_t age = 16;
  float potential = 40.0f;
  float current = 25.0f;
  PlayerTraits traits;
  bool wonderkid = false;
  bool local = true;
};

/** @brief One club's intake of one year. */
struct IntakeClass
{
  IntakeQuality quality = IntakeQuality::Average;
  float class_offset = 0.0f; /*!< Potential shift of the whole group. */
  std::vector<CandidateProfile> candidates;
};

/** @brief What the head of youth development says before intake day. */
struct IntakePreview
{
  IntakeQuality quality = IntakeQuality::Average;
  std::array<PlayerRole, 2> standout{PlayerRole::UNKNOWN, PlayerRole::UNKNOWN};
  const char* personality_key = "";
  std::size_t size = 0;
};

/** @brief The club's head of youth development (id 0: vacant). */
struct HeadOfYouth
{
  StaffID id = 0;
  std::string name;
  std::uint8_t ability = 0; /*!< Youth development. */
  std::uint8_t judging = 0; /*!< Judging potential. */
  float professionalism = 0.5f;
  const char* style_key = "";
};

/** @brief Academy ratings shown on the overview (1-100). */
struct AcademyRatings
{
  std::uint8_t facilities = 0;
  std::uint8_t recruitment = 0;
  std::uint8_t junior_coaching = 0;
  std::uint8_t head = 0;
};

/** @brief Scouted ranges of an academy player (never exact values). */
struct YouthEstimate
{
  float current_low = 0.0f;
  float current_high = 0.0f;
  float potential_low = 0.0f;
  float potential_high = 0.0f;
};

/** @brief Price and schedule of the next step of an academy investment. */
struct UpgradeQuote
{
  AcademyUpgrade kind = AcademyUpgrade::None;
  std::uint8_t current = 0;
  std::uint8_t target = 0;
  std::int64_t cost = 0;
  int days = 0;
  /** Approved if requested now, otherwise the reason it would be refused. */
  UpgradeRequestResult verdict = UpgradeRequestResult::Approved;
  /** A running project of this kind: its start and completion days. */
  std::int32_t running_start_day = 0;
  std::int32_t running_done_day = 0;
};

/**
 * @namespace YouthModel
 * @brief Pure academy rules (tested without a world).
 */
namespace YouthModel
{
inline constexpr std::uint8_t PREVIEW_MONTH = 2;
inline constexpr std::uint8_t PREVIEW_DAY = 1;
inline constexpr std::uint8_t INTAKE_MONTH = 3;
inline constexpr std::uint8_t INTAKE_DAY = 15;
/** Days the manager has to offer contracts after intake day. */
inline constexpr int DECISION_DAYS = 30;
/** Oldest age allowed in the U18 squad (season age). */
inline constexpr int U18_MAX_AGE = 18;
/** Oldest age of a regular U21 player (season age). */
inline constexpr int U21_MAX_AGE = 21;
/** Over-age players a U21 squad may hold: three outfield players and a
 * goalkeeper, as in the professional development leagues. [RR] */
inline constexpr std::size_t U21_OVERAGE_OUTFIELD = 3;
inline constexpr std::size_t U21_OVERAGE_GOALKEEPERS = 1;
/** Largest U21 squad a computer-managed club keeps. [P] */
inline constexpr std::size_t U21_SQUAD_LIMIT = 16;
/** Club-trained status: seasons at the academy between 15 and 21. [RR] */
inline constexpr int HOMEGROWN_SEASONS = 3;
/** Contracts of minors are capped at three seasons. [RR] */
inline constexpr int MINOR_CONTRACT_YEARS = 3;
/** Computer-managed clubs sign between these numbers per intake: a handful
 * at small academies, 8-12 scholars at the best. [RR 4.3, FW 5] */
inline constexpr int SIGN_MIN = 2;
inline constexpr int SIGN_MAX = 12;
/** Computer-managed clubs offer first professional contracts from this age
 * (never below the country's legal minimum). [RR 4, FW 5] */
inline constexpr int PRO_POLICY_AGE = 17;

/**
 * The intake of @p team_id in @p year. Every candidate draws from its own
 * keyed stream, so the preview in February describes exactly the group that
 * arrives in March (unless the inputs changed in between).
 */
IntakeClass planIntake(const IntakeInputs& inputs, std::uint64_t world_seed,
                       std::uint16_t year, TeamID team_id);

/** The head of youth development's summary of an intake. */
IntakePreview summarize(const IntakeClass& intake);

/** Potential shift (overall points) of a country's talent pool. */
float regionTalent(Language country);

/** Age of the first professional contract in a country (16 or 17). [RR] */
int firstProfessionalAge(Language country);

/**
 * Academy grade in [0, 1]: youth facilities, recruitment reach, junior
 * coaching and the head of youth development, like an audited academy
 * category.
 */
float academyGrade(const IntakeInputs& inputs);

/** Largest U18 squad a computer-managed academy of @p grade keeps. */
std::size_t u18SquadLimit(float grade);

/** How many of @p candidates a computer-managed academy of @p grade signs
 * (before squad room). */
int computerSignings(float grade, std::size_t candidates);

/** Weekly scholarship stipend in a league. */
std::uint32_t scholarshipWage(LeagueID league_id);

/** Seasons of a youth contract signed at @p age. */
std::uint8_t youthContractYears(int age);

/** Club-trained after at least three academy seasons before turning 21. */
bool isHomegrown(int joined_age, int age);

/**
 * Whether a player of @p age and @p role may join a U21 squad whose
 * over-age places are used as in @p quota: Ok, or OverageFull when he is
 * over 21 and his kind of over-age place is taken.
 */
YouthActionResult reserveEligibility(int age, PlayerRole role,
                                     const ReserveQuota& quota);

/**
 * Hidden working style of a staff member in [0, 1] (0.7+ professional),
 * derived from the world seed and the staff id.
 */
float staffProfessionalism(std::uint64_t world_seed, StaffID staff_id);

/** Language key of a staff working style. */
const char* staffStyleKey(float professionalism);

/** Language key describing a personality in a word or two. */
const char* personalityKey(const PlayerTraits& traits);

/** Language key of an intake verdict (e.g. "YOUTH_QUALITY_GOLDEN"). */
const char* qualityKey(IntakeQuality quality);

/** Language key of a contract type. */
const char* contractKey(YouthContract contract);

/** Language key of a board answer. */
const char* requestResultKey(UpgradeRequestResult result);

/** Language key of an action result. */
const char* actionResultKey(YouthActionResult result);

/** Goals of a U18 match from the two teams' strengths (mean overall). */
std::pair<int, int> simulateMatch(float home_strength, float away_strength,
                                  WorldRng& rng);

/** Cost of the next step of an academy investment in a league. */
std::int64_t upgradeCost(AcademyUpgrade kind, int target, LeagueID league_id);

/** Days needed to complete the next step of an academy investment. */
int upgradeDays(AcademyUpgrade kind, int target);

/** Next level of an academy investment (capped at 100). */
std::uint8_t upgradeTarget(AcademyUpgrade kind, int current);
}  // namespace YouthModel

/**
 * @class YouthAcademy
 * @brief Academies of every club: yearly intake cycle, U18 and U21 squads
 * and their leagues, youth contracts and board-funded academy projects.
 *
 * Calendar (every club, same rules): the head of youth development previews
 * the intake on 1 February, candidates arrive on 15 March and the manager
 * has 30 days to offer contracts; computer-managed clubs sign their best
 * prospects on intake day. U18 teams play a cheap aggregate league every
 * week of the season; minutes and junior coaching speed up development.
 * U21 (reserve) squads hold players aged 21 or younger plus a few over-age
 * players; they are listed apart from the senior squad like the U18s and
 * play one cheap league per country midweek.
 */
class YouthAcademy
{
 public:
  explicit YouthAcademy(std::shared_ptr<GameData> gamedata);

  // ---- Hooks (called by WorldSimulation) ----

  /** Assigns existing young players to U18 squads once (new worlds). */
  void ensureReady();

  /** Players on loan stay in their senior squads (never sent to the U21s). */
  void setLoanCheck(std::function<bool(PlayerID)> is_on_loan)
  {
    loan_check = std::move(is_on_loan);
  }

  /** Daily: previews, intake day, decision deadline, U18 matchdays,
   * projects and monthly progress snapshots. */
  void onDayAdvanced(const GameDateValue& date, TeamID managed_team_id,
                     Inbox& inbox);

  /** Before ageing and contract expiry: computer-managed clubs renew the
   * academy players they want to keep. */
  void onSeasonEnd(const GameDateValue& date, TeamID managed_team_id);

  /** After ageing: new U18 and U21 leagues; over-age U18 players move up
   * to the U21s, U21 players beyond the over-age places to the first team. */
  void onSeasonStart(const GameDateValue& date, TeamID managed_team_id,
                     Inbox& inbox);

  /**
   * Development multiplier of a U18 or U21 player (1 for everyone else):
   * youth match minutes replace first-team minutes, junior coaching and
   * youth facilities replace the senior set-up. Bounded to [0.8, 1.6].
   */
  float developmentMultiplier(const Player& player) const;

  void load(const std::shared_ptr<DatabaseConnection>& db_conn);
  void save(const std::shared_ptr<DatabaseConnection>& db_conn) const;

  // ---- Queries ----

  const YouthRecord* record(PlayerID player_id) const;
  /** In a U18 or U21 squad, or a trialist of his current club. */
  bool isAcademyPlayer(PlayerID player_id) const;
  /** Records of a club with @p status, sorted by player id. */
  std::vector<const YouthRecord*> members(TeamID team_id,
                                          YouthStatus status) const;
  const AcademyClub* club(TeamID team_id) const;
  IntakeInputs intakeInputs(TeamID team_id) const;
  AcademyRatings ratings(TeamID team_id) const;
  HeadOfYouth headOfYouth(TeamID team_id) const;
  /** What the head of youth development expects from @p year's intake. */
  IntakePreview preview(TeamID team_id, std::uint16_t year) const;
  /** Scouted ranges of a player as judged by @p viewer's academy staff. */
  YouthEstimate estimate(TeamID viewer, PlayerID player_id) const;
  /** U18 league of @p league_id, leader first. */
  std::vector<YouthTableRow> table(LeagueID league_id) const;
  /** Managed club's U18 results this season, oldest first. */
  const std::vector<YouthResult>& results() const { return managed_results; }
  /** U21 league of @p team_id's country (every division), leader first. */
  std::vector<YouthTableRow> reserveTable(TeamID team_id) const;
  /** Managed club's U21 results this season, oldest first. */
  const std::vector<YouthResult>& reserveResults() const
  {
    return managed_reserve_results;
  }
  /** Size and over-age places of a club's U21 squad. */
  ReserveQuota reserveQuota(TeamID team_id) const;
  bool isHomegrown(PlayerID player_id) const;
  /** Players of a club's senior squad (academy players excluded). */
  std::size_t firstTeamSize(TeamID team_id) const;
  /** Weekly wage of an academy contract for @p player at @p team_id. */
  std::uint32_t contractWage(TeamID team_id, const Player& player,
                             YouthContract contract) const;
  /** Next step of an investment and the board's likely answer today. */
  UpgradeQuote quote(TeamID team_id, AcademyUpgrade kind,
                     const GameDateValue& date, float board_confidence,
                     bool embargoed) const;

  // ---- Actions of the managed club ----

  YouthActionResult signCandidate(TeamID team_id, PlayerID player_id);
  /** The trialist leaves football's professional ranks (removed). */
  YouthActionResult releaseCandidate(TeamID team_id, PlayerID player_id);
  YouthActionResult offerProfessional(TeamID team_id, PlayerID player_id);
  YouthActionResult promote(TeamID team_id, PlayerID player_id);
  YouthActionResult demote(TeamID team_id, PlayerID player_id);
  /** A first-team or U18 player joins the U21 squad (age rules and the
   * over-age places apply). */
  YouthActionResult moveToReserves(TeamID team_id, PlayerID player_id);
  /** A U21 player joins the first-team squad (professional contract). */
  YouthActionResult promoteReserve(TeamID team_id, PlayerID player_id);
  /** A U21 player young enough for the U18s goes back to them. */
  YouthActionResult reserveToU18(TeamID team_id, PlayerID player_id);
  UpgradeRequestResult requestUpgrade(const GameDateValue& date, TeamID team_id,
                                      AcademyUpgrade kind,
                                      float board_confidence, bool embargoed,
                                      Inbox& inbox);

 private:
  struct YouthRound
  {
    const GameDateValue& date;
    TeamID managed_team_id;
    YouthStatus status;
    const std::vector<std::vector<TeamID>>& groups;
    YouthTableRow AcademyClub::* row_of;
    std::vector<YouthResult>& results;
    std::uint64_t salt;
    float stand_in;
  };
  struct YouthMatchSheet
  {
    std::vector<std::pair<float, Player*>> players;
    float strength = 0.0f;
  };

  void bootstrap();
  /** Sets Player::isAcademyPlayer() from the records (after loading). */
  void syncFlags();
  /** Removes a youngster from the world: he drops out of professional
   * football (released trialists and scholars nobody keeps). */
  void leaveFootball(TeamID team_id, PlayerID player_id);
  void promoteComputerAcademies(TeamID managed_team_id);
  void processComputerAcademyPlayer(TeamID team_id, PlayerID player_id,
                                    float overall, float level,
                                    std::size_t& seniors,
                                    std::size_t& reserve_size,
                                    std::vector<PlayerID>& leaving);
  AcademyClub& clubState(TeamID team_id);
  void prune();
  void postPreview(const GameDateValue& date, TeamID managed_team_id,
                   Inbox& inbox) const;
  void runIntake(const GameDateValue& date, TeamID managed_team_id,
                 Inbox& inbox);
  void processIntakeTeam(const GameDateValue& date, TeamID team_id,
                         TeamID managed_team_id, std::uint64_t seed,
                         const StatsConfig& config, NameRegistry& names,
                         Inbox& inbox,
                         std::unordered_map<TeamID, int>& u18_sizes);
  PlayerID materializeIntakeCandidate(TeamID team_id, bool managed,
                                      Language country,
                                      const CandidateProfile& profile,
                                      const StatsConfig& config,
                                      NameRegistry& names,
                                      SquadSurnames& surnames, WorldRng& rng);
  void closeDecisions(const GameDateValue& date, TeamID managed_team_id,
                      Inbox& inbox);
  void remindDecisions(const GameDateValue& date, TeamID managed_team_id,
                       Inbox& inbox) const;
  void playMatchday(const GameDateValue& date, TeamID managed_team_id);
  void playReserveMatchday(const GameDateValue& date, TeamID managed_team_id);
  /**
   * One round of a youth league for every group of clubs: team sheets from
   * the players with @p status, stand-ins of @p stand_in ability for short
   * sheets, results in @p row_of and the managed club's @p results.
   */
  void playRound(const YouthRound& round);
  void playGroup(const YouthRound& round, const std::vector<TeamID>& group,
                 std::int32_t round_number, std::uint16_t season,
                 const StatsConfig& config,
                 std::unordered_map<TeamID, YouthMatchSheet>& sheets);
  void playMatch(const YouthRound& round, TeamID home, TeamID away,
                 const StatsConfig& config,
                 std::unordered_map<TeamID, YouthMatchSheet>& sheets);
  void recordMatchSide(const YouthRound& round, TeamID team_id,
                       TeamID opponent_id, bool home, YouthMatchSheet& sheet,
                       int scored, int conceded, WorldRng& rng,
                       const StatsConfig& config);
  /** Computer-managed clubs without a U21 squad yet (new worlds and older
   * saves) move young players outside their senior plans down. */
  void setUpReserves(TeamID managed_team_id);
  /** Young senior players outside a computer-managed club's plans join its
   * U21 squad while the senior squad stays at its target size. */
  void fillComputerReserves(TeamID team_id);
  /** A computer-managed senior squad below its target size takes its best
   * U21 players and professional-age U18 players up to that size. */
  void fillComputerSeniors(TeamID team_id);
  /** Season start: U21 players beyond the age rules move on. */
  void advanceReserves(TeamID managed_team_id,
                       std::vector<std::string>& to_first_team);
  void processReservePlayer(bool managed, float level, PlayerID player_id,
                            ReserveQuota& kept, std::size_t& seniors,
                            std::vector<PlayerID>& leaving,
                            std::vector<std::string>& to_first_team);
  /** Joins the U21 squad; resets the season's youth statistics. */
  void enterReserves(const Player& player, YouthRecord& youth);
  void completeProjects(const GameDateValue& date, TeamID managed_team_id,
                        Inbox& inbox);
  void snapshotProgress(const GameDateValue& date, TeamID managed_team_id);
  void applyContract(Player& player, YouthRecord& record,
                     YouthContract contract) const;
  /** Computer-managed clubs' first professional contracts: old enough and
   * judged among the club's best prospects. */
  bool deservesProfessional(TeamID team_id, const Player& player) const;
  double wageScale(TeamID team_id) const;
  float judgingSd(TeamID team_id) const;
  std::string headName(TeamID team_id) const;

  std::shared_ptr<GameData> gamedata;
  std::unordered_map<PlayerID, YouthRecord> records;
  std::unordered_map<TeamID, AcademyClub> clubs;
  std::vector<YouthResult> managed_results;
  std::vector<YouthResult> managed_reserve_results;
  std::function<bool(PlayerID)> loan_check;
  bool ready = false;
  bool reserves_checked = false;
};

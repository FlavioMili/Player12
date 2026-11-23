// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "global/languages.h"
#include "global/types.h"
#include "model/gamedate.h"
#include "model/manager_career.h"
#include "model/national_teams.h"

class DatabaseConnection;
class GameData;
class Inbox;

/** @brief The human manager in charge of a national team. */
struct NationalJob
{
  bool active = false;
  Language nation = Language::EN;
  GameDateValue start = GameDateValue();
  GameDateValue expires = GameDateValue(); /*!< 31 July after the finals. */
  std::int64_t weekly_wage = 0;
  std::uint16_t played = 0;
  std::uint16_t won = 0;
  std::uint16_t drawn = 0;
  std::uint16_t lost = 0;
  std::uint8_t trophies = 0;
  bool qualified = false; /*!< Reached the finals of the current cycle. */
};

/** @brief An earlier national-team job (values of reason are persisted). */
struct NationalStint
{
  Language nation = Language::EN;
  GameDateValue start = GameDateValue();
  GameDateValue end = GameDateValue();
  DepartureReason reason = DepartureReason::Resigned;
  std::uint16_t played = 0;
  std::uint16_t won = 0;
  std::uint16_t drawn = 0;
  std::uint16_t lost = 0;
  std::uint8_t trophies = 0;
};

/** @brief A national team looking for a head coach. */
struct NationalVacancy
{
  Language nation = Language::EN;
  GameDateValue opened = GameDateValue();
  GameDateValue fill_date = GameDateValue();
};

/** @brief Where an application stands (values are persisted). */
enum class NationalApplicationStage : std::uint8_t
{
  Pending = 0,
  Rejected,
  Offered
};

struct NationalApplication
{
  Language nation = Language::EN;
  GameDateValue applied = GameDateValue();
  GameDateValue respond_date = GameDateValue();
  NationalApplicationStage stage = NationalApplicationStage::Pending;
};

/** @brief A federation's offer to the human manager. */
struct NationalJobOffer
{
  std::uint32_t id = 0;
  Language nation = Language::EN;
  GameDateValue made = GameDateValue();
  GameDateValue expires = GameDateValue();
  std::int64_t weekly_wage = 0;
  std::uint8_t years = 2; /*!< Until the summer after the next finals. */
  bool unsolicited = false;
};

enum class NationalApplyResult : std::uint8_t
{
  Ok = 0,
  NoVacancy,
  AlreadyApplied,
  AlreadyInCharge, /*!< He already coaches a national team. */
  NeedsReputation, /*!< Federations only consider established managers. */
  ClubConflict,    /*!< Not famous enough to combine it with his club. */
  NoProfile,
  NoOffer,
  RecentlyLeft /*!< He left this national team less than a year ago. */
};

/** @brief What changed on the latest simulated day. */
struct NationalDayEvents
{
  bool new_offer = false;
  bool squad_to_pick = false; /*!< A call-up of his nation was announced. */
  bool job_ended = false;
};

/**
 * Pure rules of national-team jobs, shared with the tests. [P] marks
 * tunable estimates.
 */
namespace NationalJobModel
{
/** Federations consider managers from a national reputation. [P] */
inline constexpr float MIN_REPUTATION = 45.0f;
/** A club job and a national team together only for the very famous
 * (rare in real football). [P] */
inline constexpr float DUAL_ROLE_REPUTATION = 70.0f;

/** Stature of a national job (club reputation scale) from its world
 * ranking: the best nation about 92, the last about 42. */
float stature(std::size_t rank, std::size_t nations);
/** Licence a federation asks for. */
CoachingLicence requiredLicence(float stature);
/** Chance that a federation offers the job after an application. */
float applicationChance(float reputation, CoachingLicence licence,
                        float stature, bool compatriot);
/** Weekly wage of a national head coach (whole euros). */
std::int64_t weeklyWage(float stature);
bool canCombineWithClub(float reputation);
/** Contract end: 31 July of the first finals summer at least ten months
 * away. */
GameDateValue contractEnd(const GameDateValue& start);
/** Reputation change after a match: weight by competition times the
 * difference between the result (1, 0.5, 0) and the expected score. */
float resultReputation(International::Competition competition, double expected,
                       double actual);
const char* applyResultKey(NationalApplyResult result);
}  // namespace NationalJobModel

/**
 * @class NationalManagement
 * @brief National-team jobs of the human manager: vacancies, applications
 * and offers, the job itself (results, reputation, qualification, contract)
 * and the AI head coaches who fill the other vacancies.
 *
 * Owned by Game next to the manager career. The national teams' calendar is
 * unchanged: the manager's nation plays its qualifiers, nations league and
 * finals through NationalTeams, with the squad he picks during the week
 * before each window (the assistant's selection stands otherwise). Results
 * and finals reach this class through NationalTeams' sinks and are handled
 * on the same day by onDay().
 */
class NationalManagement
{
 public:
  explicit NationalManagement(std::shared_ptr<GameData> gamedata);

  bool hasJob() const { return job.active; }
  const NationalJob& getJob() const { return job; }
  const std::vector<NationalStint>& getHistory() const { return history; }
  const std::vector<NationalVacancy>& getVacancies() const { return vacancies; }
  const std::vector<NationalApplication>& getApplications() const
  {
    return applications;
  }
  const std::vector<NationalJobOffer>& getOffers() const { return offers; }
  const NationalApplication* findApplication(Language nation) const;
  const NationalJobOffer* findOffer(std::uint32_t offer_id) const;

  /** Stature of @p nation's job (0 when it has no national team). */
  float stature(const NationalTeams& teams, Language nation) const;
  /** Chance that @p nation's federation offers him the job. */
  float applicationChance(const NationalTeams& teams,
                          const ManagerProfile& profile, Language nation) const;

  NationalApplyResult apply(Language nation, const GameDateValue& date,
                            const ManagerProfile& profile, bool club_job);
  bool decline(std::uint32_t offer_id);
  /** Takes the job of a valid offer. */
  NationalApplyResult accept(std::uint32_t offer_id, const GameDateValue& date,
                             NationalTeams& teams, ManagerCareer& career,
                             bool club_job, Inbox& inbox);
  /** Leaves the national team; the federation appoints an AI coach later. */
  void leave(DepartureReason reason, const GameDateValue& date,
             NationalTeams& teams, ManagerCareer& career, Inbox& inbox);

  /** A federation parts with its coach and looks for a new one (no-op when
   * the nation already has a vacancy). */
  void openVacancy(Language nation, const GameDateValue& date);

  /** NationalTeams' sinks: queued and handled by onDay(). */
  void queueResult(const International::Fixture& fixture, double home_expected);
  void queueFinals(const NationalTeams::Finals& finals, bool decided);

  /** After the national teams' day: results, finals, call-up notices,
   * vacancies, applications, offers, wages and the contract. */
  NationalDayEvents onDay(const GameDateValue& date, NationalTeams& teams,
                          ManagerCareer& career, bool club_job, Inbox& inbox);

  void load(const std::shared_ptr<DatabaseConnection>& db_conn);
  void save(const std::shared_ptr<DatabaseConnection>& db_conn) const;

 private:
  struct QueuedResult
  {
    International::Fixture fixture;
    double home_expected = 0.5;
  };
  struct QueuedFinals
  {
    International::Competition competition =
        International::Competition::WorldFinals;
    International::Confederation confederation =
        International::Confederation::Europe;
    std::vector<Language> qualified;
    std::optional<Language> winner;
    std::optional<Language> runner_up;
    bool decided = false;
  };

  void handleResult(const QueuedResult& queued, const GameDateValue& date,
                    ManagerCareer& career, Inbox& inbox);
  void handleFinals(const QueuedFinals& queued, const GameDateValue& date,
                    NationalTeams& teams, ManagerCareer& career, Inbox& inbox,
                    NationalDayEvents& events);
  void handleQualificationFinals(const QueuedFinals& queued,
                                 const GameDateValue& date,
                                 const std::vector<Language>& ranking,
                                 NationalTeams& teams, ManagerCareer& career,
                                 Inbox& inbox, NationalDayEvents& events);
  void handleDecidedFinals(const QueuedFinals& queued,
                           const GameDateValue& date, ManagerCareer& career,
                           Inbox& inbox);
  void fillVacancies(const GameDateValue& date, NationalTeams& teams);
  void processApplications(const GameDateValue& date,
                           const NationalTeams& teams,
                           const ManagerProfile& profile, Inbox& inbox,
                           NationalDayEvents& events);
  void approach(const GameDateValue& date, const NationalTeams& teams,
                const ManagerProfile& profile, bool club_job, Inbox& inbox,
                NationalDayEvents& events);
  void makeOffer(Language nation, const GameDateValue& date,
                 const NationalTeams& teams, bool unsolicited, Inbox& inbox);
  void reviewContract(const GameDateValue& date, NationalTeams& teams,
                      ManagerCareer& career, Inbox& inbox,
                      NationalDayEvents& events);
  std::string aiCoachName(Language nation, const GameDateValue& date) const;
  bool recentlyLeft(Language nation, const GameDateValue& date) const;

  std::shared_ptr<GameData> gamedata;
  NationalJob job;
  std::vector<NationalStint> history;
  std::vector<NationalVacancy> vacancies;
  std::vector<NationalApplication> applications;
  std::vector<NationalJobOffer> offers;
  std::vector<QueuedResult> queued_results;
  std::vector<QueuedFinals> queued_finals;
  std::uint32_t next_offer_id = 1;
};

// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/national_job.h"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include "database/database_connection.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "model/calendar.h"
#include "model/inbox.h"
#include "model/world_generation.h"
#include "model/world_rng.h"

using International::Competition;

namespace
{
// ---- Market tuning [P] ----------------------------------------------------

/** Days a federation takes to appoint after a vacancy opens. */
constexpr int MIN_VACANCY_DAYS = 21;
constexpr int MAX_VACANCY_DAYS = 50;
constexpr int OFFER_VALID_DAYS = 10;
constexpr int MIN_RESPONSE_DAYS = 4;
constexpr int MAX_RESPONSE_DAYS = 9;
/** Monthly chance that a federation parts with its coach mid-cycle. */
constexpr double MONTHLY_VACANCY_CHANCE = 0.02;
/** After the qualifiers: chance that a nation that missed out changes. */
constexpr double MISSED_FINALS_VACANCY_CHANCE = 0.5;
/** After finals: chance that an eliminated nation changes. */
constexpr double ELIMINATED_VACANCY_CHANCE = 0.25;
/** Weekly chance (times the application chance) of an approach. */
constexpr double APPROACH_CHANCE = 0.12;
/** A federation calls only when the job is a realistic fit. */
constexpr float APPROACH_MIN_CHANCE = 0.3f;
/** Nations of this stature expect to reach every finals tournament. */
constexpr float QUALIFY_EXPECTED_STATURE = 62.0f;
constexpr float QUALIFIED_REPUTATION = 2.0f;
constexpr float MISSED_REPUTATION = 3.0f;
constexpr float SACKED_REPUTATION = 2.0f;
constexpr float WINNER_REPUTATION = 8.0f;
constexpr float RUNNER_UP_REPUTATION = 4.0f;
constexpr std::uint64_t VACANCY_SALT = 0x4E'4A'56;
constexpr std::uint64_t RESPONSE_SALT = 0x4E'4A'52;
constexpr std::uint64_t APPROACH_SALT = 0x4E'4A'41;
constexpr std::uint64_t COACH_SALT = 0x4E'4A'43;

double uniform(const GameData& gamedata, std::uint64_t a, std::uint64_t b)
{
  return WorldRng::hashUniform(gamedata.getWorldSeed(), RngDomain::Managers, a,
                               b);
}

int daysBetween(const GameDateValue& from, const GameDateValue& to)
{
  return dayOrdinal(to) - dayOrdinal(from);
}

std::string nationArg(Language nation)
{
  return "@" + International::teamNameKey(nation);
}

void post(Inbox& inbox, const GameDateValue& date, const char* title_key,
          const char* body_key, std::vector<std::string> args,
          InboxCategory category = InboxCategory::General)
{
  InboxMessage message;
  message.date = date;
  message.category = category;
  message.title_key = title_key;
  message.body_key = body_key;
  message.args = std::move(args);
  inbox.add(std::move(message));
}

bool concerns(const International::Fixture& fixture, Language nation)
{
  return fixture.home == nation || fixture.away == nation;
}

/** Finals the nation could have reached: world finals or its own
 * continent's (the nations-league finals are a bonus, not a target). */
bool targetFinals(Competition competition,
                  International::Confederation confederation, Language nation)
{
  if (competition == Competition::WorldFinals) return true;
  return competition == Competition::ContinentalFinals &&
         confederation == International::confederationOf(nation);
}

// ---- SQLite ---------------------------------------------------------------

template <typename T>
T columnAs(sqlite3_stmt* stmt, int column)
{
  return static_cast<T>(sqlite3_column_int64(stmt, column));
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

Language nationFrom(std::int64_t value)
{
  return value >= 0 && value <= std::to_underlying(Language::US)
             ? static_cast<Language>(value)
             : Language::EN;
}
}  // namespace

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

namespace NationalJobModel
{
float stature(std::size_t rank, std::size_t nations)
{
  if (nations <= 1) return 92.0f;
  return 92.0f - 50.0f * static_cast<float>(std::min(rank, nations - 1)) /
                     static_cast<float>(nations - 1);
}

CoachingLicence requiredLicence(float stature)
{
  return stature >= 75.0f ? CoachingLicence::Pro : CoachingLicence::A;
}

float applicationChance(float reputation, CoachingLicence licence,
                        float stature, bool compatriot)
{
  if (reputation < MIN_REPUTATION) return 0.0f;
  const int missing = std::max(0, std::to_underlying(requiredLicence(stature)) -
                                      std::to_underlying(licence));
  // Federations prefer their own coaches; a foreigner needs a bigger name.
  const float x = (reputation - stature + 6.0f) / 6.0f -
                  0.6f * static_cast<float>(missing) +
                  (compatriot ? 1.0f : -0.6f);
  return std::clamp(1.0f / (1.0f + std::exp(-x)), 0.02f, 0.9f);
}

std::int64_t weeklyWage(float stature)
{
  // About EUR 3m a year at the top, a part-time salary at the smallest
  // federations. [P]
  const double wage =
      1'500.0 * std::exp((static_cast<double>(stature) - 40.0) / 14.0);
  return std::max<std::int64_t>(
      1'000, static_cast<std::int64_t>(std::llround(wage / 100.0)) * 100);
}

bool canCombineWithClub(float reputation)
{
  return reputation >= DUAL_ROLE_REPUTATION;
}

GameDateValue contractEnd(const GameDateValue& start)
{
  // Finals are played every other summer (even years).
  int year = start.year + 1;
  if (year % 2 != 0) ++year;
  GameDateValue end(static_cast<std::uint16_t>(year), 7, 31);
  if (daysBetween(start, end) < 300)
    end = GameDateValue(static_cast<std::uint16_t>(year + 2), 7, 31);
  return end;
}

float resultReputation(Competition competition, double expected, double actual)
{
  using enum International::Competition;
  float weight = 0.5f;
  switch (competition)
  {
    case Friendly:
      weight = 0.5f;
      break;
    case NationsLeague:
    case NationsLeagueFinals:
      weight = 1.0f;
      break;
    case WorldQualifier:
    case ContinentalQualifier:
      weight = 1.5f;
      break;
    case WorldFinals:
    case ContinentalFinals:
      weight = 2.5f;
      break;
  }
  return weight * static_cast<float>(actual - expected);
}

const char* applyResultKey(NationalApplyResult result)
{
  using enum NationalApplyResult;
  switch (result)
  {
    case Ok:
      return "NT_APPLY_OK";
    case NoVacancy:
      return "NT_APPLY_NO_VACANCY";
    case AlreadyApplied:
      return "NT_APPLY_ALREADY";
    case AlreadyInCharge:
      return "NT_APPLY_IN_CHARGE";
    case NeedsReputation:
      return "NT_APPLY_REPUTATION";
    case ClubConflict:
      return "NT_APPLY_CLUB_CONFLICT";
    case NoOffer:
      return "NT_APPLY_NO_OFFER";
    case RecentlyLeft:
      return "NT_APPLY_RECENTLY_LEFT";
    case NoProfile:
      break;
  }
  return "NT_APPLY_NO_PROFILE";
}
}  // namespace NationalJobModel

// ---------------------------------------------------------------------------
// NationalManagement
// ---------------------------------------------------------------------------

NationalManagement::NationalManagement(std::shared_ptr<GameData> gd)
    : gamedata(std::move(gd))
{
}

const NationalApplication* NationalManagement::findApplication(
    Language nation) const
{
  const auto it =
      std::ranges::find(applications, nation, &NationalApplication::nation);
  return it == applications.end() ? nullptr : std::to_address(it);
}

const NationalJobOffer* NationalManagement::findOffer(
    std::uint32_t offer_id) const
{
  const auto it = std::ranges::find(offers, offer_id, &NationalJobOffer::id);
  return it == offers.end() ? nullptr : std::to_address(it);
}

float NationalManagement::stature(const NationalTeams& teams,
                                  Language nation) const
{
  const std::vector<Language> ranking = teams.ranking();
  const auto it = std::ranges::find(ranking, nation);
  if (it == ranking.end()) return 0.0f;
  return NationalJobModel::stature(
      static_cast<std::size_t>(it - ranking.begin()), ranking.size());
}

float NationalManagement::applicationChance(const NationalTeams& teams,
                                            const ManagerProfile& profile,
                                            Language nation) const
{
  const float size = stature(teams, nation);
  if (size <= 0.0f) return 0.0f;
  return NationalJobModel::applicationChance(
      profile.reputation, profile.licence, size, profile.nationality == nation);
}

NationalApplyResult NationalManagement::apply(Language nation,
                                              const GameDateValue& date,
                                              const ManagerProfile& profile,
                                              bool club_job)
{
  using enum NationalApplyResult;
  if (!profile.exists) return NoProfile;
  if (job.active) return AlreadyInCharge;
  if (std::ranges::find(vacancies, nation, &NationalVacancy::nation) ==
      vacancies.end())
    return NoVacancy;
  if (findApplication(nation)) return AlreadyApplied;
  if (recentlyLeft(nation, date)) return RecentlyLeft;
  if (profile.reputation < NationalJobModel::MIN_REPUTATION)
    return NeedsReputation;
  if (club_job && !NationalJobModel::canCombineWithClub(profile.reputation))
    return ClubConflict;
  const int days =
      MIN_RESPONSE_DAYS +
      static_cast<int>(
          uniform(*gamedata, static_cast<std::uint64_t>(dayOrdinal(date)),
                  mixHash(std::to_underlying(nation), RESPONSE_SALT)) *
          (MAX_RESPONSE_DAYS - MIN_RESPONSE_DAYS + 1));
  applications.push_back({nation, date, SeasonCalendar::addDays(date, days),
                          NationalApplicationStage::Pending});
  return Ok;
}

bool NationalManagement::decline(std::uint32_t offer_id)
{
  const auto erased =
      std::erase_if(offers, [offer_id](const NationalJobOffer& offer)
                    { return offer.id == offer_id; });
  return erased > 0;
}

NationalApplyResult NationalManagement::accept(std::uint32_t offer_id,
                                               const GameDateValue& date,
                                               NationalTeams& teams,
                                               ManagerCareer& career,
                                               bool club_job, Inbox& inbox)
{
  const ManagerProfile& profile = career.getProfile();
  if (!profile.exists) return NationalApplyResult::NoProfile;
  if (job.active) return NationalApplyResult::AlreadyInCharge;
  const NationalJobOffer* offer = findOffer(offer_id);
  if (!offer || offer->expires < date) return NationalApplyResult::NoOffer;
  if (club_job && !NationalJobModel::canCombineWithClub(profile.reputation))
    return NationalApplyResult::ClubConflict;
  job = NationalJob{};
  job.active = true;
  job.nation = offer->nation;
  job.start = date;
  job.expires = NationalJobModel::contractEnd(date);
  job.weekly_wage = offer->weekly_wage;
  const Language nation = offer->nation;
  // One national team at a time: every other approach lapses.
  offers.clear();
  applications.clear();
  std::erase_if(vacancies, [nation](const NationalVacancy& vacancy)
                { return vacancy.nation == nation; });
  teams.setCoach(nation, profile.name());
  career.setInternationalDuty(true);
  post(inbox, date, "INBOX_NT_APPOINTED_TITLE", "INBOX_NT_APPOINTED_BODY",
       {nationArg(nation), job.expires.toString(),
        formatMoney(job.weekly_wage)});
  return NationalApplyResult::Ok;
}

void NationalManagement::leave(DepartureReason reason,
                               const GameDateValue& date, NationalTeams& teams,
                               ManagerCareer& career, Inbox& inbox)
{
  if (!job.active) return;
  NationalStint stint;
  stint.nation = job.nation;
  stint.start = job.start;
  stint.end = date;
  stint.reason = reason;
  stint.played = job.played;
  stint.won = job.won;
  stint.drawn = job.drawn;
  stint.lost = job.lost;
  stint.trophies = job.trophies;
  history.push_back(stint);
  const Language nation = job.nation;
  job = NationalJob{};
  career.setInternationalDuty(false);
  if (reason == DepartureReason::Sacked)
    career.adjustReputation(-SACKED_REPUTATION);
  // The federation appoints an interim straight away and looks for a
  // permanent coach.
  teams.setCoach(nation, aiCoachName(nation, date));
  openVacancy(nation, date);
  const char* body = "INBOX_NT_LEFT_BODY";
  if (reason == DepartureReason::Sacked) body = "INBOX_NT_SACKED_BODY";
  if (reason == DepartureReason::ContractExpired)
    body = "INBOX_NT_EXPIRED_BODY";
  post(inbox, date, "INBOX_NT_LEFT_TITLE", body, {nationArg(nation)});
}

void NationalManagement::queueResult(const International::Fixture& fixture,
                                     double home_expected)
{
  if (job.active && concerns(fixture, job.nation))
    queued_results.emplace_back(fixture, home_expected);
}

void NationalManagement::queueFinals(const NationalTeams::Finals& finals,
                                     bool decided)
{
  QueuedFinals queued;
  queued.competition = finals.competition;
  queued.confederation = finals.confederation;
  queued.qualified = finals.qualified;
  queued.winner = finals.winner;
  queued.runner_up = finals.runner_up;
  queued.decided = decided;
  queued_finals.push_back(std::move(queued));
}

void NationalManagement::handleResult(const QueuedResult& queued,
                                      const GameDateValue& date,
                                      ManagerCareer& career, Inbox& inbox)
{
  const International::Fixture& fixture = queued.fixture;
  if (!job.active || !concerns(fixture, job.nation)) return;
  const bool home = fixture.home == job.nation;
  const int scored = home ? fixture.home_goals : fixture.away_goals;
  const int conceded = home ? fixture.away_goals : fixture.home_goals;
  double actual = 0.5;
  if (const auto winner = International::winnerOf(fixture))
    actual = *winner == job.nation ? 1.0 : 0.0;
  ++job.played;
  if (scored > conceded)
    ++job.won;
  else if (scored < conceded)
    ++job.lost;
  else
    ++job.drawn;
  const double expected =
      home ? queued.home_expected : 1.0 - queued.home_expected;
  career.adjustReputation(NationalJobModel::resultReputation(
      fixture.competition, expected, actual));
  post(inbox, date, "INBOX_NT_RESULT_TITLE", "INBOX_NT_RESULT_BODY",
       {nationArg(job.nation), std::to_string(scored), std::to_string(conceded),
        nationArg(home ? fixture.away : fixture.home),
        std::string("@") + International::competitionKey(fixture.competition)},
       InboxCategory::Match);
}

void NationalManagement::handleFinals(const QueuedFinals& queued,
                                      const GameDateValue& date,
                                      NationalTeams& teams,
                                      ManagerCareer& career, Inbox& inbox,
                                      NationalDayEvents& events)
{
  const std::vector<Language> ranking = teams.ranking();
  if (!queued.decided)
  {
    // The qualifiers are over: nations that missed out may change coach.
    for (const Language nation : ranking)
    {
      if (!targetFinals(queued.competition, queued.confederation, nation) ||
          std::ranges::contains(queued.qualified, nation) ||
          (job.active && job.nation == nation))
        continue;
      if (uniform(*gamedata, static_cast<std::uint64_t>(dayOrdinal(date)),
                  mixHash(std::to_underlying(nation), VACANCY_SALT)) <
          MISSED_FINALS_VACANCY_CHANCE)
        openVacancy(nation, date);
    }
    if (!job.active ||
        !targetFinals(queued.competition, queued.confederation, job.nation))
      return;
    if (std::ranges::contains(queued.qualified, job.nation))
    {
      job.qualified = true;
      career.adjustReputation(QUALIFIED_REPUTATION);
      post(inbox, date, "INBOX_NT_QUALIFIED_TITLE", "INBOX_NT_QUALIFIED_BODY",
           {nationArg(job.nation),
            std::string("@") +
                International::competitionKey(queued.competition)});
      return;
    }
    career.adjustReputation(-MISSED_REPUTATION);
    // Big nations expect to be there: the federation lets him go.
    if (stature(teams, job.nation) >= QUALIFY_EXPECTED_STATURE)
    {
      leave(DepartureReason::Sacked, date, teams, career, inbox);
      events.job_ended = true;
    }
    else
    {
      post(inbox, date, "INBOX_NT_MISSED_TITLE", "INBOX_NT_MISSED_BODY",
           {nationArg(job.nation),
            std::string("@") +
                International::competitionKey(queued.competition)});
    }
    return;
  }

  // Finals decided: honours for the finalists, changes among the others.
  for (const Language nation : queued.qualified)
  {
    if (nation == queued.winner || (job.active && job.nation == nation))
      continue;
    if (uniform(*gamedata, static_cast<std::uint64_t>(dayOrdinal(date)),
                mixHash(std::to_underlying(nation), VACANCY_SALT + 1)) <
        ELIMINATED_VACANCY_CHANCE)
      openVacancy(nation, date);
  }
  if (!job.active) return;
  if (queued.winner == job.nation)
  {
    ++job.trophies;
    career.adjustReputation(WINNER_REPUTATION);
    post(
        inbox, date, "INBOX_NT_TROPHY_TITLE", "INBOX_NT_TROPHY_BODY",
        {nationArg(job.nation),
         std::string("@") + International::competitionKey(queued.competition)});
  }
  else if (queued.runner_up == job.nation)
  {
    career.adjustReputation(RUNNER_UP_REPUTATION);
  }
}

void NationalManagement::openVacancy(Language nation, const GameDateValue& date)
{
  if ((job.active && job.nation == nation) ||
      std::ranges::find(vacancies, nation, &NationalVacancy::nation) !=
          vacancies.end())
    return;
  const int days =
      MIN_VACANCY_DAYS +
      static_cast<int>(
          uniform(*gamedata, static_cast<std::uint64_t>(dayOrdinal(date)),
                  mixHash(std::to_underlying(nation), VACANCY_SALT + 2)) *
          (MAX_VACANCY_DAYS - MIN_VACANCY_DAYS + 1));
  vacancies.push_back({nation, date, SeasonCalendar::addDays(date, days)});
}

bool NationalManagement::recentlyLeft(Language nation,
                                      const GameDateValue& date) const
{
  return std::ranges::any_of(
      history, [this, nation, date](const NationalStint& stint)
      { return stint.nation == nation && daysBetween(stint.end, date) < 365; });
}

std::string NationalManagement::aiCoachName(Language nation,
                                            const GameDateValue& date) const
{
  const NamePool& names = NamePool::instance();
  const auto& first = names.firstNames(nation);
  const auto& last = names.lastNames(nation);
  if (first.empty() || last.empty()) return {};
  WorldRng rng =
      WorldRng::stream(gamedata->getWorldSeed(), RngDomain::Managers,
                       mixHash(std::to_underlying(nation), COACH_SALT),
                       static_cast<std::uint64_t>(dayOrdinal(date)));
  return first[static_cast<std::size_t>(
             rng.uniformInt(0, static_cast<int>(first.size()) - 1))] +
         " " +
         last[static_cast<std::size_t>(
             rng.uniformInt(0, static_cast<int>(last.size()) - 1))];
}

void NationalManagement::fillVacancies(const GameDateValue& date,
                                       NationalTeams& teams)
{
  std::erase_if(
      vacancies,
      [this, date, &teams](const NationalVacancy& vacancy)
      {
        if (date < vacancy.fill_date) return false;
        // The federation waits for its answer to him.
        if (const NationalApplication* application =
                findApplication(vacancy.nation);
            (application &&
             application->stage == NationalApplicationStage::Pending) ||
            std::ranges::any_of(
                offers, [nation = vacancy.nation](const NationalJobOffer& offer)
                { return offer.nation == nation; }))
          return false;
        teams.setCoach(vacancy.nation, aiCoachName(vacancy.nation, date));
        return true;
      });
  std::erase_if(applications,
                [this](const NationalApplication& application)
                {
                  return std::ranges::find(vacancies, application.nation,
                                           &NationalVacancy::nation) ==
                         vacancies.end();
                });
}

void NationalManagement::makeOffer(Language nation, const GameDateValue& date,
                                   const NationalTeams& teams, bool unsolicited,
                                   Inbox& inbox)
{
  std::erase_if(offers, [nation](const NationalJobOffer& offer)
                { return offer.nation == nation; });
  NationalJobOffer offer;
  offer.id = next_offer_id++;
  offer.nation = nation;
  offer.made = date;
  offer.expires = SeasonCalendar::addDays(date, OFFER_VALID_DAYS);
  offer.weekly_wage = NationalJobModel::weeklyWage(stature(teams, nation));
  offer.years = static_cast<std::uint8_t>(
      NationalJobModel::contractEnd(date).year - date.year);
  offer.unsolicited = unsolicited;
  offers.push_back(offer);
  if (auto application =
          std::ranges::find(applications, nation, &NationalApplication::nation);
      application != applications.end())
    application->stage = NationalApplicationStage::Offered;
  post(inbox, date,
       unsolicited ? "INBOX_NT_APPROACH_TITLE" : "INBOX_NT_OFFER_TITLE",
       unsolicited ? "INBOX_NT_APPROACH_BODY" : "INBOX_NT_OFFER_BODY",
       {nationArg(nation), formatMoney(offer.weekly_wage),
        NationalJobModel::contractEnd(date).toString()});
}

void NationalManagement::processApplications(const GameDateValue& date,
                                             const NationalTeams& teams,
                                             const ManagerProfile& profile,
                                             Inbox& inbox,
                                             NationalDayEvents& events)
{
  for (NationalApplication& application : applications)
  {
    if (application.stage != NationalApplicationStage::Pending ||
        date < application.respond_date)
      continue;
    const float chance = applicationChance(teams, profile, application.nation);
    if (uniform(*gamedata, static_cast<std::uint64_t>(dayOrdinal(date)),
                mixHash(std::to_underlying(application.nation),
                        RESPONSE_SALT + 1)) < static_cast<double>(chance))
    {
      makeOffer(application.nation, date, teams, false, inbox);
      events.new_offer = true;
    }
    else
    {
      application.stage = NationalApplicationStage::Rejected;
      post(inbox, date, "INBOX_NT_REJECTED_TITLE", "INBOX_NT_REJECTED_BODY",
           {nationArg(application.nation)});
    }
  }
}

void NationalManagement::approach(const GameDateValue& date,
                                  const NationalTeams& teams,
                                  const ManagerProfile& profile, bool club_job,
                                  Inbox& inbox, NationalDayEvents& events)
{
  if (job.active || !profile.exists ||
      profile.reputation < NationalJobModel::MIN_REPUTATION ||
      (club_job && !NationalJobModel::canCombineWithClub(profile.reputation)))
    return;
  // The most prestigious federation that calls this week wins.
  std::optional<Language> pick;
  float best = 0.0f;
  for (const NationalVacancy& vacancy : vacancies)
  {
    if (findApplication(vacancy.nation) || recentlyLeft(vacancy.nation, date) ||
        std::ranges::any_of(
            offers, [nation = vacancy.nation](const NationalJobOffer& offer)
            { return offer.nation == nation; }))
      continue;
    if (const float chance = applicationChance(teams, profile, vacancy.nation);
        chance < APPROACH_MIN_CHANCE ||
        uniform(*gamedata, static_cast<std::uint64_t>(dayOrdinal(date)),
                mixHash(std::to_underlying(vacancy.nation), APPROACH_SALT)) >=
            APPROACH_CHANCE * static_cast<double>(chance))
      continue;
    const float size = stature(teams, vacancy.nation);
    if (!pick || size > best)
    {
      pick = vacancy.nation;
      best = size;
    }
  }
  if (!pick) return;
  makeOffer(*pick, date, teams, true, inbox);
  events.new_offer = true;
}

void NationalManagement::reviewContract(const GameDateValue& date,
                                        NationalTeams& teams,
                                        ManagerCareer& career, Inbox& inbox,
                                        NationalDayEvents& events)
{
  if (!job.active || !(job.expires < date)) return;
  // A coach who took his nation to the finals is kept for another cycle.
  if (job.qualified)
  {
    job.qualified = false;
    job.expires = NationalJobModel::contractEnd(date);
    post(inbox, date, "INBOX_NT_RENEWED_TITLE", "INBOX_NT_RENEWED_BODY",
         {nationArg(job.nation), job.expires.toString()});
    return;
  }
  leave(DepartureReason::ContractExpired, date, teams, career, inbox);
  events.job_ended = true;
}

NationalDayEvents NationalManagement::onDay(const GameDateValue& date,
                                            NationalTeams& teams,
                                            ManagerCareer& career,
                                            bool club_job, Inbox& inbox)
{
  NationalDayEvents events;
  for (const QueuedResult& queued : queued_results)
    handleResult(queued, date, career, inbox);
  queued_results.clear();
  for (const QueuedFinals& queued : queued_finals)
    handleFinals(queued, date, teams, career, inbox, events);
  queued_finals.clear();

  if (job.active)
  {
    // The squad of the coming window was announced today: his to change
    // until the players report.
    if (const NationalTeams::Squad* squad = teams.squadOf(job.nation, date);
        squad && squad->announced == date && date < squad->start)
    {
      events.squad_to_pick = true;
      post(inbox, date, "INBOX_NT_CALLUP_TITLE", "INBOX_NT_CALLUP_BODY",
           {nationArg(job.nation), squad->start.toString(),
            std::to_string(squad->players.size())},
           InboxCategory::Match);
    }
    if (dayOrdinal(date) % 7 == 0) career.addEarnings(job.weekly_wage);
    reviewContract(date, teams, career, inbox, events);
  }

  std::erase_if(offers, [date](const NationalJobOffer& offer)
                { return offer.expires < date; });
  std::erase_if(applications,
                [date](const NationalApplication& application)
                {
                  return application.stage ==
                             NationalApplicationStage::Rejected &&
                         daysBetween(application.respond_date, date) > 30;
                });
  if (date.day == 1)
  {
    for (const Language nation : teams.ranking())
    {
      if (job.active && job.nation == nation) continue;
      if (uniform(*gamedata, static_cast<std::uint64_t>(dayOrdinal(date)),
                  mixHash(std::to_underlying(nation), VACANCY_SALT + 3)) <
          MONTHLY_VACANCY_CHANCE)
        openVacancy(nation, date);
    }
  }
  const ManagerProfile& profile = career.getProfile();
  processApplications(date, teams, profile, inbox, events);
  if (dayOrdinal(date) % 7 == 3)
    approach(date, teams, profile, club_job, inbox, events);
  fillVacancies(date, teams);
  return events;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

void NationalManagement::load(
    const std::shared_ptr<DatabaseConnection>& db_conn)
{
  job = NationalJob{};
  history.clear();
  vacancies.clear();
  applications.clear();
  offers.clear();
  queued_results.clear();
  queued_finals.clear();
  next_offer_id = 1;
  const DatabaseConnection& db = *db_conn;
  forEachRow(db,
             "SELECT active, nation, start_date, expires, weekly_wage, played, "
             "won, drawn, lost, trophies, qualified, next_offer_id FROM "
             "NationalJobState WHERE id = 1;",
             [this](sqlite3_stmt* stmt)
             {
               job.active = columnAs<int>(stmt, 0) != 0;
               job.nation = nationFrom(columnAs<std::int64_t>(stmt, 1));
               job.start = columnDate(stmt, 2);
               job.expires = columnDate(stmt, 3);
               job.weekly_wage = columnAs<std::int64_t>(stmt, 4);
               job.played = columnAs<std::uint16_t>(stmt, 5);
               job.won = columnAs<std::uint16_t>(stmt, 6);
               job.drawn = columnAs<std::uint16_t>(stmt, 7);
               job.lost = columnAs<std::uint16_t>(stmt, 8);
               job.trophies = columnAs<std::uint8_t>(stmt, 9);
               job.qualified = columnAs<int>(stmt, 10) != 0;
               next_offer_id = std::max<std::uint32_t>(
                   1, columnAs<std::uint32_t>(stmt, 11));
             });
  forEachRow(db,
             "SELECT nation, start_date, end_date, reason, played, won, drawn, "
             "lost, trophies FROM NationalJobHistory ORDER BY seq;",
             [this](sqlite3_stmt* stmt)
             {
               NationalStint stint;
               stint.nation = nationFrom(columnAs<std::int64_t>(stmt, 0));
               stint.start = columnDate(stmt, 1);
               stint.end = columnDate(stmt, 2);
               const auto reason = columnAs<int>(stmt, 3);
               stint.reason =
                   reason >= 0 &&
                           reason <= std::to_underlying(
                                         DepartureReason::ContractExpired)
                       ? static_cast<DepartureReason>(reason)
                       : DepartureReason::Resigned;
               stint.played = columnAs<std::uint16_t>(stmt, 4);
               stint.won = columnAs<std::uint16_t>(stmt, 5);
               stint.drawn = columnAs<std::uint16_t>(stmt, 6);
               stint.lost = columnAs<std::uint16_t>(stmt, 7);
               stint.trophies = columnAs<std::uint8_t>(stmt, 8);
               history.push_back(stint);
             });
  forEachRow(db,
             "SELECT nation, opened, fill_date FROM NationalVacancies ORDER BY "
             "nation;",
             [this](sqlite3_stmt* stmt)
             {
               vacancies.push_back({nationFrom(columnAs<std::int64_t>(stmt, 0)),
                                    columnDate(stmt, 1), columnDate(stmt, 2)});
             });
  forEachRow(
      db,
      "SELECT nation, applied, respond_date, stage FROM "
      "NationalApplications ORDER BY nation;",
      [this](sqlite3_stmt* stmt)
      {
        const auto stage = columnAs<int>(stmt, 3);
        applications.push_back(
            {nationFrom(columnAs<std::int64_t>(stmt, 0)), columnDate(stmt, 1),
             columnDate(stmt, 2),
             stage >= 0 && stage <= std::to_underlying(
                                        NationalApplicationStage::Offered)
                 ? static_cast<NationalApplicationStage>(stage)
                 : NationalApplicationStage::Rejected});
      });
  forEachRow(
      db,
      "SELECT id, nation, made, expires, weekly_wage, years, unsolicited "
      "FROM NationalJobOffers ORDER BY id;",
      [this](sqlite3_stmt* stmt)
      {
        NationalJobOffer offer;
        offer.id = columnAs<std::uint32_t>(stmt, 0);
        offer.nation = nationFrom(columnAs<std::int64_t>(stmt, 1));
        offer.made = columnDate(stmt, 2);
        offer.expires = columnDate(stmt, 3);
        offer.weekly_wage = columnAs<std::int64_t>(stmt, 4);
        offer.years = columnAs<std::uint8_t>(stmt, 5);
        offer.unsolicited = columnAs<int>(stmt, 6) != 0;
        offers.push_back(offer);
        next_offer_id = std::max(next_offer_id, offer.id + 1);
      });
}

void NationalManagement::save(
    const std::shared_ptr<DatabaseConnection>& db_conn) const
{
  const DatabaseConnection& db = *db_conn;
  for (const char* table :
       {"NationalJobState", "NationalJobHistory", "NationalVacancies",
        "NationalApplications", "NationalJobOffers"})
    sqlite3_exec(db.getRaw(),
                 (std::string("DELETE FROM ") + table + ";").c_str(), nullptr,
                 nullptr, nullptr);
  sqlite3_stmt* stmt = db.prepareStatement(
      "INSERT INTO NationalJobState (id, active, nation, start_date, expires, "
      "weekly_wage, played, won, drawn, lost, trophies, qualified, "
      "next_offer_id) VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
  sqlite3_bind_int(stmt, 1, job.active ? 1 : 0);
  sqlite3_bind_int(stmt, 2, std::to_underlying(job.nation));
  sqlite3_bind_int(stmt, 3, dateToInt(job.start));
  sqlite3_bind_int(stmt, 4, dateToInt(job.expires));
  sqlite3_bind_int64(stmt, 5, job.weekly_wage);
  sqlite3_bind_int(stmt, 6, job.played);
  sqlite3_bind_int(stmt, 7, job.won);
  sqlite3_bind_int(stmt, 8, job.drawn);
  sqlite3_bind_int(stmt, 9, job.lost);
  sqlite3_bind_int(stmt, 10, job.trophies);
  sqlite3_bind_int(stmt, 11, job.qualified ? 1 : 0);
  sqlite3_bind_int64(stmt, 12, next_offer_id);
  db.executeStep(stmt);
  sqlite3_finalize(stmt);
  int seq = 0;
  insertAll(db,
            "INSERT INTO NationalJobHistory (seq, nation, start_date, "
            "end_date, reason, played, won, drawn, lost, trophies) VALUES (?, "
            "?, ?, ?, ?, ?, ?, ?, ?, ?);",
            history,
            [&seq](sqlite3_stmt* row, const NationalStint& stint)
            {
              sqlite3_bind_int(row, 1, seq++);
              sqlite3_bind_int(row, 2, std::to_underlying(stint.nation));
              sqlite3_bind_int(row, 3, dateToInt(stint.start));
              sqlite3_bind_int(row, 4, dateToInt(stint.end));
              sqlite3_bind_int(row, 5, std::to_underlying(stint.reason));
              sqlite3_bind_int(row, 6, stint.played);
              sqlite3_bind_int(row, 7, stint.won);
              sqlite3_bind_int(row, 8, stint.drawn);
              sqlite3_bind_int(row, 9, stint.lost);
              sqlite3_bind_int(row, 10, stint.trophies);
            });
  insertAll(db,
            "INSERT INTO NationalVacancies (nation, opened, fill_date) VALUES "
            "(?, ?, ?);",
            vacancies,
            [](sqlite3_stmt* row, const NationalVacancy& vacancy)
            {
              sqlite3_bind_int(row, 1, std::to_underlying(vacancy.nation));
              sqlite3_bind_int(row, 2, dateToInt(vacancy.opened));
              sqlite3_bind_int(row, 3, dateToInt(vacancy.fill_date));
            });
  insertAll(db,
            "INSERT INTO NationalApplications (nation, applied, respond_date, "
            "stage) VALUES (?, ?, ?, ?);",
            applications,
            [](sqlite3_stmt* row, const NationalApplication& application)
            {
              sqlite3_bind_int(row, 1, std::to_underlying(application.nation));
              sqlite3_bind_int(row, 2, dateToInt(application.applied));
              sqlite3_bind_int(row, 3, dateToInt(application.respond_date));
              sqlite3_bind_int(row, 4, std::to_underlying(application.stage));
            });
  insertAll(db,
            "INSERT INTO NationalJobOffers (id, nation, made, expires, "
            "weekly_wage, years, unsolicited) VALUES (?, ?, ?, ?, ?, ?, ?);",
            offers,
            [](sqlite3_stmt* row, const NationalJobOffer& offer)
            {
              sqlite3_bind_int64(row, 1, offer.id);
              sqlite3_bind_int(row, 2, std::to_underlying(offer.nation));
              sqlite3_bind_int(row, 3, dateToInt(offer.made));
              sqlite3_bind_int(row, 4, dateToInt(offer.expires));
              sqlite3_bind_int64(row, 5, offer.weekly_wage);
              sqlite3_bind_int(row, 6, offer.years);
              sqlite3_bind_int(row, 7, offer.unsolicited ? 1 : 0);
            });
}

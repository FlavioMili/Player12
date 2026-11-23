// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/stories.h"

#include <sqlite3.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "database/database_connection.h"
#include "database/gamedata.h"
#include "model/awards.h"
#include "model/inbox.h"
#include "model/injury.h"
#include "model/interactions.h"
#include "model/match_report.h"
#include "model/player.h"
#include "model/team.h"
#include "model/world_rng.h"

namespace
{
constexpr std::array<std::uint32_t, 5> APPEARANCE_MILESTONES = {100, 200, 300,
                                                                400, 500};
constexpr std::array<std::uint32_t, 4> GOAL_MILESTONES = {50, 100, 150, 200};
/** Milestone keys: appearances as is, goals offset by this. */
constexpr std::uint32_t GOAL_KEY_OFFSET = 10'000;
constexpr std::int32_t POOR_RUN_COOLDOWN_DAYS = 60;
constexpr std::int32_t DISPUTE_COOLDOWN_DAYS = 120;
constexpr std::int32_t SAGA_COOLDOWN_DAYS = 120;
constexpr std::int32_t RECORD_RETENTION_DAYS = 3 * 365;
/** A captain this unhappy, or this far below his minutes, disputes. [P] */
constexpr float DISPUTE_MORALE = 35.0f;
constexpr float DISPUTE_SHARE = 0.35f;
/** Rivals are clubs of the same league within this reputation gap. */
constexpr int RIVAL_REPUTATION_GAP = 6;
/** Salts of the decision-moment draws (RngDomain::Stories). */
constexpr std::uint64_t DILEMMA_ROLL_SALT = 0xD1;
constexpr std::uint64_t DILEMMA_PICK_SALT = 0xD2;
/** Age limits of the players a moment can be about. [P] */
constexpr int HOMESICK_MAX_AGE = 19;
constexpr int VETERAN_MIN_AGE = 31;
constexpr int SENIOR_MIN_AGE = 22;
/** Honours not counted yet (a player followed since the last check). */
constexpr std::uint16_t HONOURS_UNKNOWN = 0xFFFF;

struct DilemmaKeys
{
  StoryKind kind;
  const char* title;
  const char* body;
  std::array<const char*, 2> options;
  std::array<const char*, 2> done;
};

constexpr std::array<DilemmaKeys, 8> DILEMMA_KEYS = {{
    {StoryKind::CompassionateLeave,
     "DILEMMA_LEAVE_TITLE",
     "DILEMMA_LEAVE_BODY",
     {"DILEMMA_LEAVE_A", "DILEMMA_LEAVE_B"},
     {"DILEMMA_LEAVE_A_DONE", "DILEMMA_LEAVE_B_DONE"}},
    {StoryKind::HomesickYouth,
     "DILEMMA_HOMESICK_TITLE",
     "DILEMMA_HOMESICK_BODY",
     {"DILEMMA_HOMESICK_A", "DILEMMA_HOMESICK_B"},
     {"DILEMMA_HOMESICK_A_DONE", "DILEMMA_HOMESICK_B_DONE"}},
    {StoryKind::FineDispute,
     "DILEMMA_FINE_TITLE",
     "DILEMMA_FINE_BODY",
     {"DILEMMA_FINE_A", "DILEMMA_FINE_B"},
     {"DILEMMA_FINE_A_DONE", "DILEMMA_FINE_B_DONE"}},
    {StoryKind::RivalComments,
     "DILEMMA_RIVAL_TITLE",
     "DILEMMA_RIVAL_BODY",
     {"DILEMMA_RIVAL_A", "DILEMMA_RIVAL_B"},
     {"DILEMMA_RIVAL_A_DONE", "DILEMMA_RIVAL_B_DONE"}},
    {StoryKind::SponsorAppearance,
     "DILEMMA_SPONSOR_TITLE",
     "DILEMMA_SPONSOR_BODY",
     {"DILEMMA_SPONSOR_A", "DILEMMA_SPONSOR_B"},
     {"DILEMMA_SPONSOR_A_DONE", "DILEMMA_SPONSOR_B_DONE"}},
    {StoryKind::TrainingClash,
     "DILEMMA_CLASH_TITLE",
     "DILEMMA_CLASH_BODY",
     {"DILEMMA_CLASH_A", "DILEMMA_CLASH_B"},
     {"DILEMMA_CLASH_A_DONE", "DILEMMA_CLASH_B_DONE"}},
    {StoryKind::CoachingCourse,
     "DILEMMA_COURSE_TITLE",
     "DILEMMA_COURSE_BODY",
     {"DILEMMA_COURSE_A", "DILEMMA_COURSE_B"},
     {"DILEMMA_COURSE_A_DONE", "DILEMMA_COURSE_B_DONE"}},
    {StoryKind::TicketProtest,
     "DILEMMA_TICKETS_TITLE",
     "DILEMMA_TICKETS_BODY",
     {"DILEMMA_TICKETS_A", "DILEMMA_TICKETS_B"},
     {"DILEMMA_TICKETS_A_DONE", "DILEMMA_TICKETS_B_DONE"}},
}};

const DilemmaKeys* keysOf(StoryKind kind)
{
  const auto found = std::ranges::find(DILEMMA_KEYS, kind, &DilemmaKeys::kind);
  return found != DILEMMA_KEYS.end() ? std::to_address(found) : nullptr;
}

/** A cost or fee that grows with the club's stature, capped. */
std::int64_t scaledMoney(std::int64_t base, std::int64_t per_point,
                         const ClubProfile& profile)
{
  return std::min(
      Stories::DILEMMA_MAX_MONEY,
      base + per_point * static_cast<std::int64_t>(profile.reputation));
}

std::optional<std::uint32_t> crossed(std::span<const std::uint32_t> marks,
                                     std::uint32_t before, std::uint32_t after)
{
  for (const std::uint32_t mark : marks)
    if (before < mark && after >= mark) return mark;
  return std::nullopt;
}

std::uint16_t seasonOf(const GameDateValue& date)
{
  return static_cast<std::uint16_t>(date.month >= 7 ? date.year
                                                    : date.year - 1);
}

template <typename T>
T columnAs(sqlite3_stmt* stmt, int column)
{
  return static_cast<T>(sqlite3_column_int64(stmt, column));
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

bool validKind(std::uint8_t kind)
{
  return kind <= std::to_underlying(Stories::LAST_DILEMMA);
}

StoryRecord storyRecord(const GameDateValue& date, StoryKind kind,
                        std::uint32_t entity, std::uint32_t key)
{
  return StoryRecord{kind, entity, key, dayOrdinal(date), false};
}

InboxMessage storyMessage(const GameDateValue& date, StoryKind kind,
                          const char* title_key, const char* body_key,
                          std::vector<std::string> args,
                          std::optional<PlayerID> player_id,
                          std::optional<TeamID> team_id)
{
  InboxMessage message;
  message.date = date;
  message.category = kind == StoryKind::TransferSaga ? InboxCategory::Transfer
                                                     : InboxCategory::General;
  message.title_key = title_key;
  message.body_key = body_key;
  message.args = std::move(args);
  message.player_id = player_id;
  message.team_id = team_id;
  // The manager watched these happen: filed as read, like match results.
  message.read = kind == StoryKind::Debut || kind == StoryKind::Rivalry;
  return message;
}
}  // namespace

namespace Stories
{
std::optional<std::uint32_t> appearanceMilestone(std::uint32_t before,
                                                 std::uint32_t after)
{
  return crossed(APPEARANCE_MILESTONES, before, after);
}

std::optional<std::uint32_t> goalMilestone(std::uint32_t before,
                                           std::uint32_t after)
{
  return crossed(GOAL_MILESTONES, before, after);
}

bool isPoorRun(std::string_view recent_form)
{
  if (recent_form.size() < static_cast<std::size_t>(POOR_RUN_MATCHES))
    return false;
  return !recent_form.substr(0, POOR_RUN_MATCHES).contains('W');
}

const char* kindKey(StoryKind kind)
{
  using enum StoryKind;
  switch (kind)
  {
    case Debut:
      return "STORY_KIND_DEBUT";
    case Breakout:
      return "STORY_KIND_BREAKOUT";
    case CaptainDispute:
      return "STORY_KIND_CAPTAIN";
    case PoorRun:
      return "STORY_KIND_POOR_RUN";
    case TransferSaga:
      return "STORY_KIND_SAGA";
    case Comeback:
      return "STORY_KIND_COMEBACK";
    case Milestone:
      return "STORY_KIND_MILESTONE";
    case Rivalry:
      return "STORY_KIND_RIVALRY";
    case CompassionateLeave:
    case HomesickYouth:
    case FineDispute:
    case RivalComments:
    case SponsorAppearance:
    case TrainingClash:
    case CoachingCourse:
    case TicketProtest:
      break;
  }
  return "STORY_KIND_DILEMMA";
}

bool isDilemma(StoryKind kind)
{
  return kind >= FIRST_DILEMMA && kind <= LAST_DILEMMA;
}

const char* dilemmaTitleKey(StoryKind kind)
{
  const DilemmaKeys* keys = keysOf(kind);
  return keys ? keys->title : "";
}

const char* dilemmaBodyKey(StoryKind kind)
{
  const DilemmaKeys* keys = keysOf(kind);
  return keys ? keys->body : "";
}

const char* dilemmaOptionKey(StoryKind kind, int option)
{
  const DilemmaKeys* keys = keysOf(kind);
  return keys && (option == 0 || option == 1)
             ? keys->options[static_cast<std::size_t>(option)]
             : "";
}

const char* dilemmaDoneKey(StoryKind kind, int option)
{
  const DilemmaKeys* keys = keysOf(kind);
  return keys && (option == 0 || option == 1)
             ? keys->done[static_cast<std::size_t>(option)]
             : "";
}

std::optional<StoryKind> dilemmaForTitle(std::string_view title_key)
{
  const auto found =
      std::ranges::find(DILEMMA_KEYS, title_key, [](const DilemmaKeys& keys)
                        { return std::string_view(keys.title); });
  if (found == DILEMMA_KEYS.end()) return std::nullopt;
  return found->kind;
}

namespace
{
DilemmaEffects generousDilemmaEffects(StoryKind kind,
                                      const ClubProfile& profile)
{
  DilemmaEffects effects;
  switch (kind)
  {
    case StoryKind::CompassionateLeave:
      effects.morale = 5.0f;
      effects.trust = 6.0f;
      effects.sharpness = -10.0f;  // A week away from training.
      break;
    case StoryKind::HomesickYouth:
      effects.morale = 5.0f;
      effects.trust = 4.0f;
      effects.money = -scaledMoney(5'000, 100, profile);
      effects.category = FinanceCategory::Facilities;
      break;
    case StoryKind::FineDispute:
      effects.morale = -4.0f;
      effects.trust = -5.0f;
      effects.squad_morale = 1.0f;  // The rules hold for everyone.
      break;
    case StoryKind::RivalComments:
      effects.squad_morale = 2.0f;
      effects.money = -scaledMoney(10'000, 300, profile);  // League fine.
      effects.category = FinanceCategory::Adjustment;
      break;
    case StoryKind::SponsorAppearance:
      effects.squad_morale = -2.0f;
      effects.money = scaledMoney(15'000, 1'000, profile);
      effects.category = FinanceCategory::Sponsorship;
      break;
    case StoryKind::TrainingClash:
      effects.morale = 3.0f;
      effects.trust = 4.0f;
      effects.other_morale = -5.0f;
      effects.other_trust = -6.0f;
      break;
    case StoryKind::CoachingCourse:
      effects.morale = 5.0f;
      effects.trust = 5.0f;
      effects.sharpness = -6.0f;
      effects.money = -scaledMoney(8'000, 120, profile);
      effects.category = FinanceCategory::Staff;
      break;
    case StoryKind::TicketProtest:
      // A discount on the next home gate.
      effects.squad_morale = 2.0f;
      effects.money =
          -std::min(DILEMMA_MAX_MONEY,
                    static_cast<std::int64_t>(profile.stadium_capacity) *
                        profile.ticket_price * 8 / 100);
      effects.category = FinanceCategory::Matchday;
      break;
    default:
      break;
  }
  return effects;
}

DilemmaEffects firmDilemmaEffects(StoryKind kind)
{
  DilemmaEffects effects;
  switch (kind)
  {
    case StoryKind::CompassionateLeave:
      effects.morale = -4.0f;
      effects.trust = -6.0f;
      break;
    case StoryKind::HomesickYouth:
      effects.morale = -3.0f;
      effects.trust = -3.0f;
      break;
    case StoryKind::FineDispute:
      effects.morale = 3.0f;
      effects.trust = 4.0f;
      effects.squad_morale = -1.0f;
      break;
    case StoryKind::RivalComments:
      effects.squad_morale = -1.0f;
      break;
    case StoryKind::SponsorAppearance:
      effects.squad_morale = 1.0f;
      break;
    case StoryKind::TrainingClash:
      effects.morale = -2.0f;
      effects.trust = -2.0f;
      effects.other_morale = -2.0f;
      effects.other_trust = -2.0f;
      effects.squad_morale = 1.0f;
      break;
    case StoryKind::CoachingCourse:
      effects.morale = -3.0f;
      effects.trust = -4.0f;
      break;
    case StoryKind::TicketProtest:
      effects.squad_morale = -2.0f;
      break;
    default:
      break;
  }
  return effects;
}
}  // namespace

DilemmaEffects dilemmaEffects(StoryKind kind, int option,
                              const ClubProfile& profile)
{
  // Small, bounded nudges: a dilemma colours the week, it does not decide
  // the season. The first answer is the generous one, the second the firm
  // one; each costs something. [P]
  if (option == 0) return generousDilemmaEffects(kind, profile);
  if (option == 1) return firmDilemmaEffects(kind);
  return {};
}
}  // namespace Stories

StoryEngine::StoryEngine(std::shared_ptr<GameData> gd) : gamedata(std::move(gd))
{
}

void StoryEngine::setCareerProvider(
    std::function<CareerTotals(PlayerID)> provider)
{
  career_provider = std::move(provider);
  career_cache.clear();
}

CareerTotals StoryEngine::careerBefore(PlayerID player_id)
{
  if (const auto cached = career_cache.find(player_id);
      cached != career_cache.end())
    return cached->second;
  const CareerTotals totals =
      career_provider ? career_provider(player_id) : CareerTotals{};
  career_cache.emplace(player_id, totals);
  return totals;
}

bool StoryEngine::fired(StoryKind kind, std::uint32_t entity,
                        std::uint32_t key) const
{
  return std::ranges::any_of(state.records,
                             [kind, entity, key](const StoryRecord& record)
                             {
                               return record.kind == kind &&
                                      record.entity == entity &&
                                      record.key == key;
                             });
}

bool StoryEngine::firedSince(StoryKind kind, std::uint32_t entity,
                             std::int32_t since_day) const
{
  return std::ranges::any_of(
      state.records,
      [kind, entity, since_day](const StoryRecord& record)
      {
        return record.kind == kind && record.entity == entity &&
               record.day > since_day;
      });
}

bool StoryEngine::post(StoryRecord record, InboxMessage message, Inbox& inbox,
                       bool with_choice)
{
  const std::int32_t today = dayOrdinal(message.date);
  // Decisions always get through; the rest shares a weekly allowance.
  const auto recent = std::ranges::count_if(
      state.records, [today](const StoryRecord& previous)
      { return previous.posted && previous.day > today - 7; });
  const bool posted = with_choice || recent < Stories::WEEKLY_CAP;
  record.day = today;
  record.posted = posted;
  state.records.push_back(record);
  if (!posted) return false;
  const std::optional<PlayerID> player_id = message.player_id;
  inbox.add(std::move(message));
  if (with_choice && player_id.has_value())
  {
    std::erase_if(state.choices,
                  [player_id = *player_id](const StoryChoice& choice)
                  { return choice.player_id == player_id; });
    state.choices.push_back(StoryChoice{record.kind, *player_id, today,
                                        today + Stories::CHOICE_DAYS});
  }
  return true;
}

void StoryEngine::onDayAdvanced(const GameDateValue& date,
                                TeamID managed_team_id,
                                const InteractionSystem& interactions,
                                Inbox& inbox, const StoryDayContext& context)
{
  const std::int32_t today = dayOrdinal(date);
  std::erase_if(state.choices, [today](const StoryChoice& choice)
                { return choice.expires_day < today; });
  for (Dilemma& dilemma : state.dilemmas)
  {
    // Unanswered moments lapse without effects (and with the job).
    if (dilemma.open() &&
        (dilemma.expires_day < today || managed_team_id == FREE_AGENTS_TEAM_ID))
    {
      dilemma.chosen = 2;
      dilemma.resolved_day = today;
    }
  }
  trackFollows(date, managed_team_id, context.awards, inbox);
  if (managed_team_id == FREE_AGENTS_TEAM_ID) return;

  updateInjuries(today, managed_team_id);

  raiseDilemma(date, managed_team_id, interactions, context, inbox);

  if (today % 7 != 0) return;
  std::erase_if(state.bids, [today](const auto& bid)
                { return today - bid.second > Stories::SAGA_WINDOW_DAYS; });
  std::erase_if(state.records, [today](const StoryRecord& record)
                { return today - record.day > RECORD_RETENTION_DAYS; });
  std::erase_if(state.dilemmas,
                [today](const Dilemma& dilemma)
                {
                  return !dilemma.open() &&
                         today - dilemma.day > RECORD_RETENTION_DAYS;
                });

  // Captain dispute: the dressing room's top figure is unhappy or dropped.
  postCaptainDispute(date, today, managed_team_id, interactions, inbox);
}

void StoryEngine::updateInjuries(std::int32_t today, TeamID managed_team_id)
{
  // Long injuries of the managed squad: a comeback is due at the player's
  // next appearance.
  for (const auto& player : gamedata->getPlayersForTeam(managed_team_id))
  {
    const PlayerID player_id = player.get().getId();
    const bool injured = player.get().getDynamics().injury_days > 0;
    const auto start = state.injury_start.find(player_id);
    if (injured && start == state.injury_start.end())
      state.injury_start.emplace(player_id, today);
    else if (!injured && start != state.injury_start.end())
    {
      if (const std::int32_t days_out = today - start->second;
          days_out >= Stories::LONG_INJURY_DAYS)
        state.comeback_due[player_id] = days_out;
      state.injury_start.erase(start);
    }
  }
  const auto left_club = [this, managed_team_id](const auto& entry)
  {
    const auto player = gamedata->getPlayer(entry.first);
    return !player || player->get().getTeamId() != managed_team_id;
  };
  std::erase_if(state.injury_start, left_club);
  std::erase_if(state.comeback_due, left_club);
}

void StoryEngine::postCaptainDispute(const GameDateValue& date,
                                     std::int32_t today, TeamID managed_team_id,
                                     const InteractionSystem& interactions,
                                     Inbox& inbox)
{
  const DressingRoom room = interactions.dressingRoom(managed_team_id, date);
  if (room.leaders.empty()) return;
  const PlayerID captain_id = room.leaders.front().player_id;
  const auto captain = gamedata->getPlayer(captain_id);
  if (!captain) return;
  const PlayerDynamics& dynamics = captain->get().getDynamics();
  const bool dropped = dynamics.injury_days == 0 &&
                       dynamics.season_appearances >= 1 &&
                       dynamics.playing_share < DISPUTE_SHARE;
  if ((dynamics.morale < DISPUTE_MORALE || dropped) &&
      !firedSince(StoryKind::CaptainDispute, captain_id,
                  today - DISPUTE_COOLDOWN_DAYS))
  {
    post(storyRecord(date, StoryKind::CaptainDispute, captain_id, 0),
         storyMessage(date, StoryKind::CaptainDispute, "STORY_CAPTAIN_TITLE",
                      dropped ? "STORY_CAPTAIN_BODY_DROPPED"
                              : "STORY_CAPTAIN_BODY_UNHAPPY",
                      {captain->get().getName()}, captain_id, managed_team_id),
         inbox, true);
  }
}

void StoryEngine::onMatchPlayed(const MatchReport& report,
                                TeamID managed_team_id,
                                const std::string& recent_form, Inbox& inbox)
{
  using enum StoryKind;
  if (!state.follows.empty()) followMatch(report, managed_team_id, inbox);
  const bool home = report.home_team_id == managed_team_id;
  if (managed_team_id == FREE_AGENTS_TEAM_ID ||
      (!home && report.away_team_id != managed_team_id))
    return;
  const bool competitive = report.match_type != MatchType::FRIENDLY;
  const TeamID opponent_id = home ? report.away_team_id : report.home_team_id;
  const auto opponent = gamedata->getTeam(opponent_id);
  const std::string opponent_name =
      opponent ? opponent->get().getName() : std::string();
  for (const PlayerMatchLine& line : report.players)
  {
    processMatchPlayer(report, line, managed_team_id, opponent_name,
                       competitive, inbox);
  }
  if (competitive)
    postTeamMatchStories(report, managed_team_id, home, recent_form,
                         opponent_id, opponent_name, inbox);
}

void StoryEngine::processMatchPlayer(const MatchReport& report,
                                     const PlayerMatchLine& line,
                                     TeamID managed_team_id,
                                     const std::string& opponent_name,
                                     bool competitive, Inbox& inbox)
{
  if (line.team_id != managed_team_id || line.minutes == 0) return;
  const auto player = gamedata->getPlayer(line.player_id);
  if (!player) return;
  const Player& who = player->get();
  const std::string name = who.getName();
  const GameDateValue& date = report.date;
  if (const auto due = state.comeback_due.find(line.player_id);
      due != state.comeback_due.end())
  {
    post(storyRecord(date, StoryKind::Comeback, line.player_id,
                     static_cast<std::uint32_t>(dayOrdinal(date))),
         storyMessage(date, StoryKind::Comeback, "STORY_COMEBACK_TITLE",
                      "STORY_COMEBACK_BODY",
                      {name, std::to_string(due->second / 7), opponent_name},
                      line.player_id, std::nullopt),
         inbox);
    state.comeback_due.erase(due);
  }
  if (competitive)
    postCareerMilestones(date, line, who, name, opponent_name, inbox);
  postBreakout(date, line, who, name, inbox);
}

void StoryEngine::postCareerMilestones(const GameDateValue& date,
                                       const PlayerMatchLine& line,
                                       const Player& player,
                                       const std::string& name,
                                       const std::string& opponent_name,
                                       Inbox& inbox)
{
  using enum StoryKind;
  const CareerTotals before = careerBefore(line.player_id);
  const CareerTotals after{before.appearances + 1, before.goals + line.goals};
  career_cache[line.player_id] = after;
  if (before.appearances == 0 && player.getAge() <= Stories::DEBUT_MAX_AGE &&
      !fired(Debut, line.player_id, 0))
  {
    post(storyRecord(date, Debut, line.player_id, 0),
         storyMessage(
             date, Debut, "STORY_DEBUT_TITLE",
             line.goals > 0 ? "STORY_DEBUT_BODY_GOAL" : "STORY_DEBUT_BODY",
             {name, std::to_string(player.getAge()), opponent_name},
             line.player_id, std::nullopt),
         inbox);
  }
  if (const auto mark =
          Stories::appearanceMilestone(before.appearances, after.appearances);
      mark && !fired(Milestone, line.player_id, *mark))
  {
    post(
        storyRecord(date, Milestone, line.player_id, *mark),
        storyMessage(date, Milestone, "STORY_MILESTONE_TITLE",
                     "STORY_MILESTONE_APPS_BODY", {name, std::to_string(*mark)},
                     line.player_id, std::nullopt),
        inbox);
  }
  if (const auto mark = Stories::goalMilestone(before.goals, after.goals);
      mark && !fired(Milestone, line.player_id, GOAL_KEY_OFFSET + *mark))
  {
    post(storyRecord(date, Milestone, line.player_id, GOAL_KEY_OFFSET + *mark),
         storyMessage(date, Milestone, "STORY_MILESTONE_TITLE",
                      "STORY_MILESTONE_GOALS_BODY",
                      {name, std::to_string(*mark)}, line.player_id,
                      std::nullopt),
         inbox);
  }
}

void StoryEngine::postBreakout(const GameDateValue& date,
                               const PlayerMatchLine& line,
                               const Player& player, const std::string& name,
                               Inbox& inbox)
{
  const std::uint16_t season = seasonOf(date);
  if (player.getAge() <= Stories::BREAKOUT_MAX_AGE &&
      player.getDynamics().rating_count >= 3 &&
      player.getForm() >= Stories::BREAKOUT_RATING &&
      !fired(StoryKind::Breakout, line.player_id, season))
  {
    post(storyRecord(date, StoryKind::Breakout, line.player_id, season),
         storyMessage(date, StoryKind::Breakout, "STORY_BREAKOUT_TITLE",
                      "STORY_BREAKOUT_BODY",
                      {name, std::to_string(player.getAge())}, line.player_id,
                      std::nullopt),
         inbox);
  }
}

void StoryEngine::postTeamMatchStories(const MatchReport& report,
                                       TeamID managed_team_id, bool home,
                                       const std::string& recent_form,
                                       TeamID opponent_id,
                                       const std::string& opponent_name,
                                       Inbox& inbox)
{
  using enum StoryKind;
  const GameDateValue& date = report.date;
  const std::int32_t today = dayOrdinal(date);
  const auto team = gamedata->getTeam(managed_team_id);
  if (Stories::isPoorRun(recent_form) &&
      !firedSince(PoorRun, managed_team_id, today - POOR_RUN_COOLDOWN_DAYS))
  {
    post(storyRecord(date, PoorRun, managed_team_id,
                     static_cast<std::uint32_t>(today)),
         storyMessage(date, PoorRun, "STORY_POOR_RUN_TITLE",
                      "STORY_POOR_RUN_BODY",
                      {team ? team->get().getName() : std::string(),
                       std::to_string(Stories::POOR_RUN_MATCHES)},
                      std::nullopt, managed_team_id),
         inbox);
  }
  if (report.match_type == MatchType::LEAGUE &&
      rivalOf(managed_team_id).has_value() &&
      *rivalOf(managed_team_id) == opponent_id &&
      !fired(Rivalry, opponent_id, static_cast<std::uint32_t>(today)))
  {
    const int own = home ? report.home_goals : report.away_goals;
    const int other = home ? report.away_goals : report.home_goals;
    const char* body = "STORY_RIVALRY_BODY_DRAW";
    if (own > other)
      body = "STORY_RIVALRY_BODY_WIN";
    else if (own < other)
      body = "STORY_RIVALRY_BODY_LOSS";
    post(storyRecord(date, Rivalry, opponent_id,
                     static_cast<std::uint32_t>(today)),
         storyMessage(
             date, Rivalry, "STORY_RIVALRY_TITLE", body,
             {opponent_name, std::to_string(own), std::to_string(other)},
             std::nullopt, opponent_id),
         inbox);
  }
}

void StoryEngine::onTransferBid(const GameDateValue& date, PlayerID player_id,
                                TeamID bidder_id, bool key_player,
                                TeamID managed_team_id, Inbox& inbox)
{
  const auto player = gamedata->getPlayer(player_id);
  if (!player || managed_team_id == FREE_AGENTS_TEAM_ID ||
      player->get().getTeamId() != managed_team_id)
    return;
  const std::int32_t today = dayOrdinal(date);
  state.bids.emplace_back(player_id, today);
  const auto bids = std::ranges::count_if(
      state.bids,
      [player_id, today](const auto& bid)
      {
        return bid.first == player_id &&
               today - bid.second <= Stories::SAGA_WINDOW_DAYS;
      });
  if (!key_player || bids < 2 ||
      firedSince(StoryKind::TransferSaga, player_id,
                 today - SAGA_COOLDOWN_DAYS))
    return;
  const auto bidder = gamedata->getTeam(bidder_id);
  post(storyRecord(date, StoryKind::TransferSaga, player_id, 0),
       storyMessage(date, StoryKind::TransferSaga, "STORY_SAGA_TITLE",
                    "STORY_SAGA_BODY",
                    {player->get().getName(),
                     bidder ? bidder->get().getName() : std::string(),
                     std::to_string(bids)},
                    player_id, bidder_id),
       inbox, true);
}

void StoryEngine::onPlayerLeft(PlayerID player_id)
{
  std::erase_if(state.choices, [player_id](const StoryChoice& choice)
                { return choice.player_id == player_id; });
  for (Dilemma& dilemma : state.dilemmas)
  {
    const bool involved = dilemma.subject == player_id ||
                          (dilemma.kind == StoryKind::TrainingClash &&
                           dilemma.other == player_id);
    if (dilemma.open() && involved) dilemma.chosen = 2;
  }
  state.injury_start.erase(player_id);
  state.comeback_due.erase(player_id);
  std::erase_if(state.bids, [player_id](const auto& bid)
                { return bid.first == player_id; });
}

void StoryEngine::onSeasonStart()
{
  const std::scoped_lock lock(rivals_mutex);
  rivals.clear();
}

std::optional<TeamID> StoryEngine::rivalOf(TeamID team_id) const
{
  const std::scoped_lock lock(rivals_mutex);
  if (const auto cached = rivals.find(team_id); cached != rivals.end())
    return cached->second;
  std::optional<TeamID> rival;
  if (const auto team = gamedata->getTeam(team_id); team)
  {
    int best_gap = RIVAL_REPUTATION_GAP + 1;
    const int reputation = team->get().getReputation();
    for (const auto& [other_id, other] : gamedata->getTeams())
    {
      if (other_id == team_id || other_id == FREE_AGENTS_TEAM_ID ||
          other.getLeagueId() != team->get().getLeagueId())
        continue;
      const int gap = std::abs(other.getReputation() - reputation);
      if (gap < best_gap || (gap == best_gap && rival && other_id < *rival))
      {
        best_gap = gap;
        rival = other_id;
      }
    }
  }
  rivals.emplace(team_id, rival);
  return rival;
}

std::optional<StoryChoice> StoryEngine::choiceFor(PlayerID player_id,
                                                  std::int32_t today) const
{
  for (const StoryChoice& choice : state.choices)
    if (choice.player_id == player_id && choice.expires_day >= today)
      return choice;
  return std::nullopt;
}

void StoryEngine::resolveChoice(PlayerID player_id)
{
  std::erase_if(state.choices, [player_id](const StoryChoice& choice)
                { return choice.player_id == player_id; });
}

// ---------------------------------------------------------------------------
// Decision moments
// ---------------------------------------------------------------------------

const Dilemma* StoryEngine::openDilemma() const
{
  const auto found = std::ranges::find_if(state.dilemmas, &Dilemma::open);
  return found != state.dilemmas.end() ? std::to_address(found) : nullptr;
}

const Dilemma* StoryEngine::dilemmaOn(std::int32_t day) const
{
  const auto found = std::ranges::find(state.dilemmas, day, &Dilemma::day);
  return found != state.dilemmas.end() ? std::to_address(found) : nullptr;
}

std::optional<DilemmaEffects> StoryEngine::previewDilemma(
    int option, TeamID managed_team_id) const
{
  const Dilemma* dilemma = openDilemma();
  const auto team = gamedata->getTeam(managed_team_id);
  if (dilemma == nullptr || !team || (option != 0 && option != 1))
    return std::nullopt;
  return Stories::dilemmaEffects(dilemma->kind, option,
                                 team->get().getProfile());
}

void StoryEngine::raiseDilemma(const GameDateValue& date,
                               TeamID managed_team_id,
                               const InteractionSystem& interactions,
                               const StoryDayContext& context, Inbox& inbox)
{
  const std::int32_t today = dayOrdinal(date);
  // Never on match day, one at a time, at most one a fortnight.
  if (context.days_to_match == 0 || openDilemma() != nullptr) return;
  if (!state.dilemmas.empty() &&
      today - state.dilemmas.back().day < Stories::DILEMMA_GAP_DAYS)
    return;
  const std::uint64_t seed = gamedata->getWorldSeed();
  if (WorldRng::hashUniform(seed, RngDomain::Stories,
                            static_cast<std::uint64_t>(today),
                            DILEMMA_ROLL_SALT) >= Stories::DILEMMA_DAILY_CHANCE)
    return;
  const auto team = gamedata->getTeam(managed_team_id);
  if (!team) return;

  // Senior squad by id, so the draw does not depend on container order.
  std::vector<const Player*> squad;
  for (const auto& player : gamedata->getPlayersForTeam(managed_team_id))
    squad.push_back(&player.get());
  std::ranges::sort(squad, {}, &Player::getId);
  auto candidates = dilemmaCandidates(today, managed_team_id, interactions,
                                      date, context, squad);
  if (candidates.empty()) return;

  WorldRng rng =
      WorldRng::stream(seed, RngDomain::Stories,
                       static_cast<std::uint64_t>(today), DILEMMA_PICK_SALT);
  DilemmaCandidate& pick = candidates[static_cast<std::size_t>(
      rng.uniformInt(0, static_cast<int>(candidates.size()) - 1))];
  postDilemmaPick(date, managed_team_id, team->get().getProfile(), pick, rng,
                  inbox);
}

std::vector<StoryEngine::DilemmaCandidate> StoryEngine::dilemmaCandidates(
    std::int32_t today, TeamID managed_team_id,
    const InteractionSystem& interactions, const GameDateValue& date,
    const StoryDayContext& context,
    const std::vector<const Player*>& squad) const
{
  const auto pending = [this](const Player* player)
  {
    return std::ranges::any_of(
        state.choices, [player_id = player->getId()](const StoryChoice& choice)
        { return choice.player_id == player_id; });
  };
  const auto pool = [&squad, &pending](auto&& keep)
  {
    std::vector<const Player*> chosen;
    for (const Player* player : squad)
      if (!pending(player) && keep(*player)) chosen.push_back(player);
    return chosen;
  };
  const auto fit = [](const Player& player)
  { return player.getDynamics().injury_days == 0; };

  std::vector<DilemmaCandidate> candidates;
  const auto cooling = [this, today](StoryKind kind)
  {
    return std::ranges::any_of(state.dilemmas,
                               [kind, today](const Dilemma& dilemma)
                               {
                                 return dilemma.kind == kind &&
                                        today - dilemma.day <
                                            Stories::DILEMMA_KIND_COOLDOWN_DAYS;
                               });
  };
  const auto offer = [&candidates, &cooling](
                         StoryKind kind, std::vector<const Player*> players,
                         std::size_t needed, std::uint32_t other = 0)
  {
    if (!cooling(kind) && players.size() >= needed)
      candidates.emplace_back(kind, std::move(players), other);
  };
  offer(StoryKind::CompassionateLeave,
        pool([&fit](const Player& player)
             { return fit(player) && player.getAge() >= SENIOR_MIN_AGE; }),
        1);
  offer(StoryKind::HomesickYouth,
        pool([](const Player& player)
             { return player.getAge() <= HOMESICK_MAX_AGE; }),
        1);
  if (!cooling(StoryKind::FineDispute))
  {
    const DressingRoom room = interactions.dressingRoom(managed_team_id, date);
    if (!room.leaders.empty())
    {
      const PlayerID captain_id = room.leaders.front().player_id;
      offer(StoryKind::FineDispute,
            pool([captain_id](const Player& player)
                 { return player.getId() == captain_id; }),
            1);
    }
  }
  if (const auto rival = rivalOf(managed_team_id); rival.has_value())
    offer(StoryKind::RivalComments, {}, 0, *rival);
  if (context.days_to_match >= 1 && context.days_to_match <= 3)
    offer(StoryKind::SponsorAppearance, {}, 0);
  offer(StoryKind::TrainingClash,
        pool([&fit](const Player& player)
             { return fit(player) && player.getAge() >= 18; }),
        2);
  offer(StoryKind::CoachingCourse,
        pool([](const Player& player)
             { return player.getAge() >= VETERAN_MIN_AGE; }),
        1);
  const auto team = gamedata->getTeam(managed_team_id);
  if (team && std::ranges::count(team->get().getRecentForm(), 'L') >= 2)
    offer(StoryKind::TicketProtest, {}, 0);
  return candidates;
}

void StoryEngine::postDilemmaPick(const GameDateValue& date,
                                  TeamID managed_team_id,
                                  const ClubProfile& profile,
                                  DilemmaCandidate& pick, WorldRng& rng,
                                  Inbox& inbox)
{
  const std::int32_t today = dayOrdinal(date);
  Dilemma dilemma;
  dilemma.kind = pick.kind;
  dilemma.day = today;
  dilemma.expires_day = today + Stories::DILEMMA_ANSWER_DAYS;
  dilemma.other = pick.other;
  std::string subject_name;
  std::string other_name;
  if (!pick.players.empty())
  {
    const int last = static_cast<int>(pick.players.size()) - 1;
    const int first = rng.uniformInt(0, last);
    dilemma.subject = pick.players[static_cast<std::size_t>(first)]->getId();
    subject_name = pick.players[static_cast<std::size_t>(first)]->getName();
    if (pick.kind == StoryKind::TrainingClash)
    {
      // A different player: draw among the others.
      int second = rng.uniformInt(0, last - 1);
      if (second >= first) ++second;
      const Player* other = pick.players[static_cast<std::size_t>(second)];
      dilemma.other = other->getId();
      other_name = other->getName();
    }
  }
  if (pick.kind == StoryKind::RivalComments)
  {
    if (const auto rival = gamedata->getTeam(static_cast<TeamID>(pick.other)))
      other_name = rival->get().getName();
  }
  // The money at stake, whichever answer carries it.
  std::int64_t stake = 0;
  for (const int option : {0, 1})
    stake = std::max(
        stake,
        std::abs(Stories::dilemmaEffects(pick.kind, option, profile).money));
  state.dilemmas.push_back(dilemma);

  InboxMessage message;
  message.date = date;
  message.category = pick.kind == StoryKind::SponsorAppearance ||
                             pick.kind == StoryKind::TicketProtest
                         ? InboxCategory::Finance
                         : InboxCategory::General;
  message.title_key = Stories::dilemmaTitleKey(pick.kind);
  message.body_key = Stories::dilemmaBodyKey(pick.kind);
  message.args = {subject_name, other_name, formatMoney(stake)};
  if (dilemma.subject != 0) message.player_id = dilemma.subject;
  message.team_id = pick.kind == StoryKind::RivalComments
                        ? static_cast<TeamID>(pick.other)
                        : managed_team_id;
  inbox.add(std::move(message));
}

bool StoryEngine::resolveDilemma(int option, const GameDateValue& date,
                                 TeamID managed_team_id,
                                 InteractionSystem& interactions, Inbox& inbox)
{
  const auto open = std::ranges::find_if(state.dilemmas, &Dilemma::open);
  const auto team = gamedata->getTeam(managed_team_id);
  if (open == state.dilemmas.end() || !team || (option != 0 && option != 1))
    return false;
  Dilemma& dilemma = *open;
  const DilemmaEffects effects =
      Stories::dilemmaEffects(dilemma.kind, option, team->get().getProfile());
  const PlayerID other_player =
      dilemma.kind == StoryKind::TrainingClash ? dilemma.other : 0;
  auto& players = gamedata->getPlayers();
  std::string subject_name;
  std::string other_name;
  const auto apply = [&players, managed_team_id, &interactions](
                         PlayerID player_id, float morale, float trust,
                         float sharpness, std::string& name)
  {
    const auto found = players.find(player_id);
    if (player_id == 0 || found == players.end() ||
        found->second.getTeamId() != managed_team_id)
      return;
    name = found->second.getName();
    PlayerDynamics& dynamics = found->second.mutableDynamics();
    dynamics.morale = std::clamp(dynamics.morale + morale, 0.0f, 100.0f);
    dynamics.sharpness =
        std::clamp(dynamics.sharpness + sharpness, 0.0f, 100.0f);
    if (trust != 0.0f) interactions.adjustTrust(player_id, trust);
  };
  apply(dilemma.subject, effects.morale, effects.trust, effects.sharpness,
        subject_name);
  apply(other_player, effects.other_morale, effects.other_trust, 0.0f,
        other_name);
  if (effects.squad_morale != 0.0f)
  {
    for (const auto& member : gamedata->getPlayersForTeam(managed_team_id))
    {
      const PlayerID player_id = member.get().getId();
      if (player_id == dilemma.subject || player_id == other_player) continue;
      PlayerDynamics& dynamics = players.at(player_id).mutableDynamics();
      dynamics.morale =
          std::clamp(dynamics.morale + effects.squad_morale, 0.0f, 100.0f);
    }
  }
  if (effects.money != 0)
    team->get().getFinances().record(date, effects.category, effects.money);
  if (dilemma.kind == StoryKind::RivalComments)
  {
    if (const auto rival =
            gamedata->getTeam(static_cast<TeamID>(dilemma.other)))
      other_name = rival->get().getName();
  }
  dilemma.chosen = static_cast<std::int8_t>(option);
  dilemma.resolved_day = dayOrdinal(date);
  dilemma.money = effects.money;

  // The answer is on file: filed as read, the manager just took it.
  InboxMessage message;
  message.date = date;
  message.category = InboxCategory::General;
  message.title_key = "INBOX_DILEMMA_DONE_TITLE";
  message.body_key = Stories::dilemmaDoneKey(dilemma.kind, option);
  message.args = {
      subject_name, other_name, formatMoney(std::abs(effects.money)),
      std::string("@") + Stories::dilemmaOptionKey(dilemma.kind, option)};
  if (dilemma.subject != 0) message.player_id = dilemma.subject;
  message.team_id = managed_team_id;
  message.read = true;
  inbox.add(std::move(message));
  return true;
}

// ---------------------------------------------------------------------------
// Followed players
// ---------------------------------------------------------------------------

bool StoryEngine::follow(PlayerID player_id, const GameDateValue& date)
{
  const auto player = gamedata->getPlayer(player_id);
  if (!player || isFollowed(player_id) ||
      state.follows.size() >= Stories::MAX_FOLLOWS)
    return false;
  const Player& who = player->get();
  state.follows.emplace_back(player_id, dayOrdinal(date), who.getTeamId(),
                             who.getDynamics().injury_days > 0,
                             who.getContractYears(), who.getWage(),
                             HONOURS_UNKNOWN, 0);
  return true;
}

bool StoryEngine::unfollow(PlayerID player_id)
{
  return std::erase_if(state.follows,
                       [player_id](const FollowedPlayer& followed)
                       { return followed.player_id == player_id; }) > 0;
}

bool StoryEngine::isFollowed(PlayerID player_id) const
{
  return std::ranges::contains(state.follows, player_id,
                               &FollowedPlayer::player_id);
}

void StoryEngine::trackFollows(const GameDateValue& date,
                               TeamID managed_team_id,
                               const AwardSystem* awards, Inbox& inbox)
{
  if (state.follows.empty()) return;
  // Retired players leave the database: nothing more to follow.
  std::erase_if(state.follows, [this](const FollowedPlayer& followed)
                { return !gamedata->getPlayer(followed.player_id); });
  for (FollowedPlayer& followed : state.follows)
    trackFollow(date, managed_team_id, awards, followed, inbox);
}

void StoryEngine::trackFollow(const GameDateValue& date, TeamID managed_team_id,
                              const AwardSystem* awards,
                              FollowedPlayer& followed, Inbox& inbox)
{
  const Player& player = gamedata->getPlayer(followed.player_id)->get();
  const std::string name = player.getName();
  const TeamID team_id = player.getTeamId();
  reportFollowChanges(date, managed_team_id, followed, player, name, team_id,
                      inbox);
  if (awards != nullptr)
    reportFollowHonours(date, managed_team_id, *awards, followed, player, name,
                        inbox);
  followed.team_id = team_id;
  followed.injured = player.getDynamics().injury_days > 0;
  followed.contract_years = player.getContractYears();
  followed.wage = player.getWage();
}

void StoryEngine::reportFollowChanges(
    const GameDateValue& date, TeamID managed_team_id, FollowedPlayer& followed,
    const Player& player, const std::string& name, TeamID team_id, Inbox& inbox)
{
  const auto team_name = [this](TeamID id)
  {
    const auto team = gamedata->getTeam(id);
    return team ? team->get().getName() : std::string();
  };
  const auto note = [&date, &inbox](InboxCategory category, const char* title,
                                    const char* body,
                                    std::vector<std::string> args,
                                    PlayerID player_id, TeamID note_team_id)
  {
    InboxMessage message;
    message.date = date;
    message.category = category;
    message.title_key = title;
    message.body_key = body;
    message.args = std::move(args);
    message.player_id = player_id;
    if (note_team_id != FREE_AGENTS_TEAM_ID) message.team_id = note_team_id;
    inbox.add(std::move(message));
  };
  const bool own =
      managed_team_id != FREE_AGENTS_TEAM_ID && team_id == managed_team_id;
  const bool moved = team_id != followed.team_id;
  if (moved && !own && followed.team_id != managed_team_id)
  {
    if (team_id == FREE_AGENTS_TEAM_ID)
      note(InboxCategory::Transfer, "INBOX_FOLLOW_RELEASED_TITLE",
           "INBOX_FOLLOW_RELEASED_BODY", {name, team_name(followed.team_id)},
           followed.player_id, followed.team_id);
    else
      note(InboxCategory::Transfer, "INBOX_FOLLOW_TRANSFER_TITLE",
           "INBOX_FOLLOW_TRANSFER_BODY",
           {name,
            followed.team_id == FREE_AGENTS_TEAM_ID
                ? std::string("@INBOX_FOLLOW_NO_CLUB")
                : team_name(followed.team_id),
            team_name(team_id)},
           followed.player_id, team_id);
  }
  const PlayerDynamics& dynamics = player.getDynamics();
  const bool injured = dynamics.injury_days > 0;
  if (injured && !followed.injured && !own)
    note(InboxCategory::Injury, "INBOX_FOLLOW_INJURY_TITLE",
         "INBOX_FOLLOW_INJURY_BODY",
         {name, team_name(team_id),
          std::string("@") + InjuryModel::nameKey(dynamics.injury),
          std::to_string(dynamics.injury_days)},
         followed.player_id, team_id);
  // A longer deal or new terms at the same club; a move brings new terms
  // anyway and is told as a transfer.
  if (!moved && !own && team_id != FREE_AGENTS_TEAM_ID &&
      (player.getContractYears() > followed.contract_years ||
       player.getWage() != followed.wage))
    note(InboxCategory::Contract, "INBOX_FOLLOW_CONTRACT_TITLE",
         "INBOX_FOLLOW_CONTRACT_BODY",
         {name, team_name(team_id), std::to_string(player.getContractYears()),
          formatMoney(player.getWage())},
         followed.player_id, team_id);
}

void StoryEngine::reportFollowHonours(const GameDateValue& date,
                                      TeamID managed_team_id,
                                      const AwardSystem& awards,
                                      FollowedPlayer& followed,
                                      const Player& player,
                                      const std::string& name, Inbox& inbox)
{
  const auto team_name = [this](TeamID id)
  {
    const auto team = gamedata->getTeam(id);
    return team ? team->get().getName() : std::string();
  };
  const std::vector<AwardRecord> honours =
      awards.honoursFor(followed.player_id);
  const auto count = static_cast<std::uint16_t>(
      std::min<std::size_t>(honours.size(), HONOURS_UNKNOWN - 1));
  const bool own = managed_team_id != FREE_AGENTS_TEAM_ID &&
                   player.getTeamId() == managed_team_id;
  if (followed.honours != HONOURS_UNKNOWN && count > followed.honours && !own)
  {
    // Newest first: the honours won since the last look.
    const auto fresh = static_cast<std::size_t>(count - followed.honours);
    for (std::size_t index = 0; index < fresh; ++index)
    {
      InboxMessage message;
      message.date = date;
      message.category = InboxCategory::General;
      message.title_key = "INBOX_FOLLOW_AWARD_TITLE";
      message.body_key = "INBOX_FOLLOW_AWARD_BODY";
      message.args = {name, team_name(honours[index].team_id),
                      std::string("@") + awardTypeKey(honours[index].type)};
      message.player_id = followed.player_id;
      message.team_id = honours[index].team_id;
      inbox.add(std::move(message));
    }
  }
  followed.honours = count;
}

void StoryEngine::followMatch(const MatchReport& report, TeamID managed_team_id,
                              Inbox& inbox)
{
  const std::int32_t today = dayOrdinal(report.date);
  for (const PlayerMatchLine& line : report.players)
  {
    if (line.minutes == 0 || line.team_id == managed_team_id ||
        (line.rating < Stories::BIG_MATCH_RATING &&
         line.goals < Stories::BIG_MATCH_GOALS))
      continue;
    const auto followed = std::ranges::find(state.follows, line.player_id,
                                            &FollowedPlayer::player_id);
    if (followed == state.follows.end() ||
        (followed->last_match_day != 0 &&
         today - followed->last_match_day < Stories::BIG_MATCH_GAP_DAYS))
      continue;
    const auto player = gamedata->getPlayer(line.player_id);
    const auto team = gamedata->getTeam(line.team_id);
    const TeamID opponent_id = line.team_id == report.home_team_id
                                   ? report.away_team_id
                                   : report.home_team_id;
    const auto opponent = gamedata->getTeam(opponent_id);
    if (!player || !team || !opponent) continue;
    followed->last_match_day = today;
    InboxMessage message;
    message.date = report.date;
    message.category = InboxCategory::Match;
    message.title_key = "INBOX_FOLLOW_MATCH_TITLE";
    message.body_key = line.goals >= Stories::BIG_MATCH_GOALS
                           ? "INBOX_FOLLOW_MATCH_BODY_GOALS"
                           : "INBOX_FOLLOW_MATCH_BODY";
    message.args = {player->get().getName(), team->get().getName(),
                    opponent->get().getName(), std::to_string(line.goals),
                    line.rating > 0.0f ? std::format("{:.1f}", line.rating)
                                       : std::string("-")};
    message.player_id = line.player_id;
    message.team_id = line.team_id;
    inbox.add(std::move(message));
  }
}

// ---------------------------------------------------------------------------
// Persistence (tables StoryRecords, StoryChoices, StoryInjuries, StoryBids,
// StoryDilemmas and StoryFollows)
// ---------------------------------------------------------------------------

void StoryEngine::restore(StoryState restored)
{
  state = std::move(restored);
  career_cache.clear();
  const std::scoped_lock lock(rivals_mutex);
  rivals.clear();
}

void StoryEngine::load(const std::shared_ptr<DatabaseConnection>& db_conn)
{
  StoryState loaded;
  forEachRow(
      *db_conn,
      "SELECT kind, entity, story_key, day, posted FROM StoryRecords "
      "ORDER BY rowid;",
      [&loaded](sqlite3_stmt* stmt)
      {
        const auto kind = columnAs<std::uint8_t>(stmt, 0);
        if (!validKind(kind)) return;
        loaded.records.push_back(StoryRecord{
            static_cast<StoryKind>(kind), columnAs<std::uint32_t>(stmt, 1),
            columnAs<std::uint32_t>(stmt, 2), columnAs<std::int32_t>(stmt, 3),
            sqlite3_column_int(stmt, 4) != 0});
      });
  forEachRow(
      *db_conn,
      "SELECT player_id, kind, day, expires_day FROM StoryChoices "
      "ORDER BY rowid;",
      [&loaded](sqlite3_stmt* stmt)
      {
        const auto kind = columnAs<std::uint8_t>(stmt, 1);
        if (!validKind(kind)) return;
        loaded.choices.push_back(StoryChoice{
            static_cast<StoryKind>(kind), columnAs<PlayerID>(stmt, 0),
            columnAs<std::int32_t>(stmt, 2), columnAs<std::int32_t>(stmt, 3)});
      });
  forEachRow(*db_conn,
             "SELECT player_id, start_day, comeback_days FROM StoryInjuries;",
             [&loaded](sqlite3_stmt* stmt)
             {
               const auto player_id = columnAs<PlayerID>(stmt, 0);
               const auto start = columnAs<std::int32_t>(stmt, 1);
               const auto comeback = columnAs<std::int32_t>(stmt, 2);
               if (start > 0) loaded.injury_start.emplace(player_id, start);
               if (comeback > 0)
                 loaded.comeback_due.emplace(player_id, comeback);
             });
  forEachRow(*db_conn, "SELECT player_id, day FROM StoryBids ORDER BY rowid;",
             [&loaded](sqlite3_stmt* stmt)
             {
               loaded.bids.emplace_back(columnAs<PlayerID>(stmt, 0),
                                        columnAs<std::int32_t>(stmt, 1));
             });
  forEachRow(
      *db_conn,
      "SELECT day, kind, subject, other, expires_day, chosen, resolved_day, "
      "money FROM StoryDilemmas ORDER BY day;",
      [&loaded](sqlite3_stmt* stmt)
      {
        const auto kind = columnAs<std::uint8_t>(stmt, 1);
        if (!validKind(kind) ||
            !Stories::isDilemma(static_cast<StoryKind>(kind)))
          return;
        Dilemma dilemma;
        dilemma.day = columnAs<std::int32_t>(stmt, 0);
        dilemma.kind = static_cast<StoryKind>(kind);
        dilemma.subject = columnAs<PlayerID>(stmt, 2);
        dilemma.other = columnAs<std::uint32_t>(stmt, 3);
        dilemma.expires_day = columnAs<std::int32_t>(stmt, 4);
        dilemma.chosen = columnAs<std::int8_t>(stmt, 5);
        dilemma.resolved_day = columnAs<std::int32_t>(stmt, 6);
        dilemma.money = columnAs<std::int64_t>(stmt, 7);
        loaded.dilemmas.push_back(dilemma);
      });
  forEachRow(
      *db_conn,
      "SELECT player_id, since_day, team_id, injured, contract_years, "
      "wage, honours, last_match_day FROM StoryFollows ORDER BY rowid;",
      [&loaded](sqlite3_stmt* stmt)
      {
        loaded.follows.emplace_back(
            columnAs<PlayerID>(stmt, 0), columnAs<std::int32_t>(stmt, 1),
            columnAs<TeamID>(stmt, 2), sqlite3_column_int(stmt, 3) != 0,
            columnAs<std::uint8_t>(stmt, 4), columnAs<std::uint32_t>(stmt, 5),
            columnAs<std::uint16_t>(stmt, 6), columnAs<std::int32_t>(stmt, 7));
      });
  restore(std::move(loaded));
}

void StoryEngine::save(const std::shared_ptr<DatabaseConnection>& db_conn) const
{
  for (const char* table : {"StoryRecords", "StoryChoices", "StoryInjuries",
                            "StoryBids", "StoryDilemmas", "StoryFollows"})
  {
    sqlite3_exec(db_conn->getRaw(),
                 (std::string("DELETE FROM ") + table + ";").c_str(), nullptr,
                 nullptr, nullptr);
  }
  insertAll(*db_conn,
            "INSERT INTO StoryRecords (kind, entity, story_key, day, posted) "
            "VALUES (?, ?, ?, ?, ?);",
            state.records,
            [](sqlite3_stmt* row, const StoryRecord& record)
            {
              sqlite3_bind_int(row, 1, std::to_underlying(record.kind));
              sqlite3_bind_int64(row, 2, record.entity);
              sqlite3_bind_int64(row, 3, record.key);
              sqlite3_bind_int(row, 4, record.day);
              sqlite3_bind_int(row, 5, record.posted ? 1 : 0);
            });
  insertAll(*db_conn,
            "INSERT INTO StoryChoices (player_id, kind, day, expires_day) "
            "VALUES (?, ?, ?, ?);",
            state.choices,
            [](sqlite3_stmt* row, const StoryChoice& choice)
            {
              sqlite3_bind_int(row, 1, static_cast<int>(choice.player_id));
              sqlite3_bind_int(row, 2, std::to_underlying(choice.kind));
              sqlite3_bind_int(row, 3, choice.day);
              sqlite3_bind_int(row, 4, choice.expires_day);
            });
  // One row per player: injury start and/or comeback due.
  std::unordered_map<PlayerID, std::pair<std::int32_t, std::int32_t>> injuries;
  for (const auto& [player_id, start] : state.injury_start)
    injuries[player_id].first = start;
  for (const auto& [player_id, days] : state.comeback_due)
    injuries[player_id].second = days;
  insertAll(*db_conn,
            "INSERT INTO StoryInjuries (player_id, start_day, comeback_days) "
            "VALUES (?, ?, ?);",
            injuries,
            [](sqlite3_stmt* row, const auto& entry)
            {
              sqlite3_bind_int(row, 1, static_cast<int>(entry.first));
              sqlite3_bind_int(row, 2, entry.second.first);
              sqlite3_bind_int(row, 3, entry.second.second);
            });
  insertAll(*db_conn, "INSERT INTO StoryBids (player_id, day) VALUES (?, ?);",
            state.bids,
            [](sqlite3_stmt* row, const auto& bid)
            {
              sqlite3_bind_int(row, 1, static_cast<int>(bid.first));
              sqlite3_bind_int(row, 2, bid.second);
            });
  insertAll(*db_conn,
            "INSERT INTO StoryDilemmas (day, kind, subject, other, "
            "expires_day, chosen, resolved_day, money) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
            state.dilemmas,
            [](sqlite3_stmt* row, const Dilemma& dilemma)
            {
              sqlite3_bind_int(row, 1, dilemma.day);
              sqlite3_bind_int(row, 2, std::to_underlying(dilemma.kind));
              sqlite3_bind_int64(row, 3, dilemma.subject);
              sqlite3_bind_int64(row, 4, dilemma.other);
              sqlite3_bind_int(row, 5, dilemma.expires_day);
              sqlite3_bind_int(row, 6, dilemma.chosen);
              sqlite3_bind_int(row, 7, dilemma.resolved_day);
              sqlite3_bind_int64(row, 8, dilemma.money);
            });
  insertAll(*db_conn,
            "INSERT INTO StoryFollows (player_id, since_day, team_id, injured, "
            "contract_years, wage, honours, last_match_day) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?);",
            state.follows,
            [](sqlite3_stmt* row, const FollowedPlayer& followed)
            {
              sqlite3_bind_int64(row, 1, followed.player_id);
              sqlite3_bind_int(row, 2, followed.since_day);
              sqlite3_bind_int64(row, 3, followed.team_id);
              sqlite3_bind_int(row, 4, followed.injured ? 1 : 0);
              sqlite3_bind_int(row, 5, followed.contract_years);
              sqlite3_bind_int64(row, 6, followed.wage);
              sqlite3_bind_int(row, 7, followed.honours);
              sqlite3_bind_int(row, 8, followed.last_match_day);
            });
}

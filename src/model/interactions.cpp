// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/interactions.h"

#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "database/database_connection.h"
#include "database/gamedata.h"
#include "model/calendar.h"
#include "model/inbox.h"
#include "model/match_report.h"
#include "model/team.h"
#include "model/transfer_windows.h"
#include "model/world_rng.h"
#include "model/world_simulation.h"

namespace
{
/** Spread of the personal noise in a reaction (uniform +-NOISE). [P] */
constexpr float NOISE = 0.45f;
/** Score (plus noise) above which a reply is positive, below its negative
 * counterpart negative. [P] */
constexpr float THRESHOLD = 0.2f;

constexpr std::int32_t PLAYING_TIME_PROMISE_DAYS = 49;
constexpr std::int32_t CONTRACT_PROMISE_DAYS = 60;
constexpr std::int32_t SIGNING_WINDOW_SEARCH_DAYS = 120;
constexpr std::uint16_t PLAYING_TIME_MIN_MATCHES = 3;
constexpr std::uint16_t PLAYING_TIME_EARLY_MATCHES = 6;
/** A broken promise makes new ones unbelievable for this long. [P] */
constexpr std::int32_t PROMISE_CREDIBILITY_DAYS = 120;
/** Resolved promises stay visible on the profile for a year. */
constexpr std::int32_t PROMISE_HISTORY_DAYS = 365;

constexpr float KEPT_MORALE = 6.0f;
constexpr float KEPT_TRUST = 12.0f;
constexpr float BROKEN_MORALE = 12.0f;
constexpr float BROKEN_TRUST = 20.0f;
constexpr float BROKEN_REQUEST_TRUST = -30.0f;
constexpr std::uint8_t BROKEN_REQUEST_AMBITION = 65;

/** Weekly morale below this counts towards an unhappy request. [P] */
constexpr float UNHAPPY_MORALE = 35.0f;
constexpr std::uint8_t UNHAPPY_WEEKS = 3;
/** At most one new request squad-wide in this many days. [P] */
constexpr std::int32_t REQUEST_SPACING_DAYS = 21;
constexpr std::int32_t REQUEST_ANSWER_DAYS = 21;
constexpr std::int32_t REQUEST_QUIET_DAYS = 42;
constexpr float TRUST_WEEKLY_RETENTION = 0.97f;
/** Morale target points per trust point (trust +-100 -> +-6). [P] */
constexpr float TRUST_MORALE_WEIGHT = 0.06f;
constexpr float OPEN_REQUEST_MORALE = -3.0f;

constexpr std::size_t LEADER_COUNT = 3;
constexpr float LEADER_INFLUENCE = 0.15f;
constexpr float LEADER_WEEKLY_CAP = 1.5f;
constexpr std::size_t GROUP_MIN_SIZE = 3;
constexpr std::size_t MAX_GROUPS = 3;

float unit(std::uint8_t trait) { return static_cast<float>(trait) / 100.0f; }

// High temperament = composed: morale swings 0.7x-1.3x (as the weekly
// morale model). [P]
float temperamentScale(const PlayerTraits& traits)
{
  return 1.3f - 0.6f * unit(traits.temperament);
}

float clampMorale(float value) { return std::clamp(value, 0.0f, 100.0f); }
float clampTrust(float value) { return std::clamp(value, -100.0f, 100.0f); }

bool isFirstChoice(SquadRole role)
{
  return role == SquadRole::KeyPlayer || role == SquadRole::FirstTeam;
}

float formDelta(const TalkContext& context)
{
  if (context.rated_matches == 0) return 0.0f;
  return std::clamp(context.form - context.expected_rating, -1.5f, 1.5f);
}

/** Mean score of an option before the player's mood of the day. [P] */
float talkScore(const TalkContext& context, TalkOption option)
{
  using enum TalkOption;
  const float professionalism = unit(context.traits.professionalism) - 0.5f;
  const float ambition = unit(context.traits.ambition) - 0.5f;
  const float temperament = unit(context.traits.temperament) - 0.5f;
  const float loyalty = unit(context.traits.loyalty) - 0.5f;
  const float trust = context.trust / 100.0f;
  const float standing = (context.manager_standing - 50.0f) / 50.0f;
  const float form = formDelta(context);
  const float credibility = context.promise_credible ? 0.0f : 0.8f;
  switch (option)
  {
    case PraiseForm:
      // Professionals see through praise that was not earned.
      return 0.1f + 0.6f * form + 0.15f * trust -
             (form < -0.2f ? 0.35f * (professionalism + 0.5f) : 0.0f);
    case CriticiseForm:
      // Volatile players take criticism badly even when it is deserved.
      return -0.15f - 0.4f * form + 0.6f * professionalism +
             0.7f * temperament + 0.15f * standing + 0.15f * trust;
    case DiscussPlayingTime:
      return 0.05f + 1.2f * (context.playing_share - context.expected_share) +
             0.25f * trust + 0.4f * loyalty - 0.4f * ambition;
    case PromisePlayingTime:
      return 0.35f + 0.3f * trust + 0.15f * standing - 0.2f * ambition -
             credibility;
    case PromiseContract:
      return 0.4f + 0.3f * trust + 0.3f * loyalty - credibility;
    case PromiseSigning:
      return 0.2f + 0.6f * ambition + 0.25f * trust + 0.2f * standing -
             credibility;
    case AskPatience:
      return 0.7f * loyalty + 0.4f * professionalism - 0.5f * ambition +
             0.4f * trust + 0.2f * standing -
             (context.request == TalkRequest::Transfer ? 0.25f : 0.0f);
    case ReassureStay:
      return 0.9f * loyalty - 0.7f * ambition + 0.35f * trust +
             0.2f * standing + (isFirstChoice(context.role) ? 0.15f : -0.15f);
    case OpenToOffers:
      return 0.05f + 0.9f * ambition - 0.8f * loyalty;
    case AcceptTransferRequest:
      return 1.0f;
    case RefuseTransferRequest:
      return -0.15f + 0.7f * loyalty + 0.4f * professionalism -
             0.6f * ambition + 0.35f * trust + 0.15f * standing;
    case COUNT:
      break;
  }
  return 0.0f;
}

/** Morale and trust changes of an option by reaction. [P] */
struct Deltas
{
  float morale;
  float trust;
};

Deltas deltasFor(TalkOption option, TalkReaction reaction)
{
  static constexpr std::array<std::array<Deltas, 3>,
                              std::to_underlying(TalkOption::COUNT)>
      TABLE = {{
          {{{4.0f, 3.0f}, {1.0f, 0.0f}, {-2.0f, -3.0f}}},      // Praise
          {{{2.0f, 2.0f}, {-1.0f, -1.0f}, {-6.0f, -6.0f}}},    // Criticise
          {{{3.0f, 3.0f}, {0.0f, 0.0f}, {-3.0f, -3.0f}}},      // Playing time
          {{{6.0f, 2.0f}, {2.0f, 0.0f}, {-1.0f, -2.0f}}},      // Promise PT
          {{{5.0f, 3.0f}, {2.0f, 0.0f}, {-1.0f, -2.0f}}},      // Promise deal
          {{{4.0f, 2.0f}, {1.0f, 0.0f}, {-1.0f, -2.0f}}},      // Promise sign
          {{{3.0f, 1.0f}, {0.0f, 0.0f}, {-4.0f, -4.0f}}},      // Patience
          {{{4.0f, 3.0f}, {1.0f, 0.0f}, {-5.0f, -3.0f}}},      // Reassure
          {{{3.0f, 1.0f}, {-1.0f, 0.0f}, {-6.0f, -6.0f}}},     // Open door
          {{{5.0f, 2.0f}, {5.0f, 2.0f}, {5.0f, 2.0f}}},        // Accept
          {{{-1.0f, 0.0f}, {-4.0f, -3.0f}, {-8.0f, -10.0f}}},  // Refuse
      }};
  return TABLE[std::to_underlying(option)][std::to_underlying(reaction)];
}

const char* optionToken(TalkOption option)
{
  using enum TalkOption;
  switch (option)
  {
    case PraiseForm:
      return "PRAISE";
    case CriticiseForm:
      return "CRITICISE";
    case DiscussPlayingTime:
      return "PLAYING_TIME";
    case PromisePlayingTime:
      return "PROMISE_PLAYING_TIME";
    case PromiseContract:
      return "PROMISE_CONTRACT";
    case PromiseSigning:
      return "PROMISE_SIGNING";
    case AskPatience:
      return "PATIENCE";
    case ReassureStay:
      return "REASSURE";
    case OpenToOffers:
      return "OPEN_TO_OFFERS";
    case AcceptTransferRequest:
      return "ACCEPT_TRANSFER";
    case RefuseTransferRequest:
      return "REFUSE_TRANSFER";
    case COUNT:
      break;
  }
  return "PRAISE";
}

/** Reply wordings per reaction (the common topics have more). */
int replyVariants(TalkOption option)
{
  using enum TalkOption;
  switch (option)
  {
    case PraiseForm:
    case CriticiseForm:
    case DiscussPlayingTime:
      return 3;
    default:
      return 2;
  }
}

std::uint64_t optionKeyOf(PlayerID player_id, TalkOption option)
{
  return (static_cast<std::uint64_t>(player_id) << 8U) |
         std::to_underlying(option);
}

// ---------------------------------------------------------------- Team talks

enum class Situation : std::uint8_t
{
  Favourite,
  Balanced,
  Underdog,
  Winning,
  Drawing,
  Losing
};

Situation situationOf(const TeamTalkContext& context)
{
  using enum Situation;
  if (context.moment == TeamTalkMoment::HalfTime)
  {
    if (context.goal_difference > 0) return Winning;
    return context.goal_difference == 0 ? Drawing : Losing;
  }
  if (context.expected_points >= 1.9f) return Favourite;
  return context.expected_points <= 1.0f ? Underdog : Balanced;
}

/** How well a tone fits the situation, before personalities. [P] */
float toneFit(const TeamTalkContext& context, TeamTalkTone tone)
{
  using enum Situation;
  using enum TeamTalkTone;
  // Calm, Motivate, DemandMore, Praise, NoPressure.
  static constexpr std::array<std::array<float, 5>, 6> BASE = {{
      {{0.15f, 0.15f, 0.30f, -0.05f, -0.30f}},  // Favourite
      {{0.10f, 0.35f, 0.05f, 0.05f, 0.00f}},    // Balanced
      {{0.10f, 0.25f, -0.30f, 0.05f, 0.40f}},   // Underdog
      {{0.30f, 0.05f, -0.05f, 0.35f, 0.10f}},   // Winning
      {{0.10f, 0.35f, 0.15f, -0.05f, 0.05f}},   // Drawing
      {{0.10f, 0.25f, 0.25f, -0.40f, 0.05f}},   // Losing
  }};
  const Situation situation = situationOf(context);
  float fit = BASE[std::to_underlying(situation)][std::to_underlying(tone)];
  const bool favourite = context.expected_points >= 1.9f;
  if (situation == Losing && context.goal_difference <= -2)
  {
    // Heavy deficit: favourites answer to demands, underdogs to relief.
    if (tone == DemandMore) fit += favourite ? 0.05f : -0.15f;
    if (tone == NoPressure && !favourite) fit += 0.2f;
  }
  if (situation == Winning && context.goal_difference >= 2)
  {
    if (tone == Calm) fit += 0.1f;
    if (tone == Praise) fit += 0.05f;
  }
  if (context.derby || context.final_match)
  {
    // Big occasions: settle nerves; "no pressure" rings hollow.
    if (tone == Calm) fit += 0.15f;
    if (tone == NoPressure) fit -= 0.05f;
    if (tone == Motivate && context.derby) fit += 0.05f;
  }
  return fit;
}

float listenerScore(const TeamTalkContext& context, const TalkListener& who,
                    TeamTalkTone tone)
{
  using enum TeamTalkTone;
  const float professionalism = unit(who.traits.professionalism) - 0.5f;
  const float ambition = unit(who.traits.ambition) - 0.5f;
  const float temperament = unit(who.traits.temperament) - 0.5f;
  const float low_morale =
      std::clamp((55.0f - who.morale) / 45.0f, -1.0f, 1.0f);
  float score = toneFit(context, tone);
  switch (tone)
  {
    case Calm:
      score -= 0.4f * temperament;  // Volatile players need calming.
      break;
    case Motivate:
      score += 0.4f * ambition + 0.2f * low_morale;
      break;
    case DemandMore:
      score += 0.6f * professionalism + 0.5f * temperament - 0.3f * low_morale;
      break;
    case Praise:
      score -= 0.2f * professionalism;
      break;
    case NoPressure:
      score += -0.4f * ambition - 0.3f * temperament;
      break;
    case COUNT:
      break;
  }
  return score;
}

TalkReaction reactionFor(float score, double roll)
{
  using enum TalkReaction;
  const float noisy = score + (static_cast<float>(roll) * 2.0f - 1.0f) * NOISE;
  if (noisy > THRESHOLD) return Positive;
  return noisy < -THRESHOLD ? Negative : Neutral;
}

float chanceAbove(float score)
{
  return std::clamp((NOISE - (THRESHOLD - score)) / (2.0f * NOISE), 0.0f, 1.0f);
}

float chanceBelow(float score)
{
  return std::clamp((NOISE - THRESHOLD - score) / (2.0f * NOISE), 0.0f, 1.0f);
}

// ---------------------------------------------------------------- SQLite

template <typename T>
T columnAs(sqlite3_stmt* stmt, int column)
{
  return static_cast<T>(sqlite3_column_int64(stmt, column));
}

PlayerRelation readPlayerRelation(sqlite3_stmt* stmt)
{
  PlayerRelation rel;
  rel.trust = clampTrust(static_cast<float>(sqlite3_column_double(stmt, 1)));
  for (std::size_t group = 0; group < rel.last_talk.size(); ++group)
    rel.last_talk[group] =
        columnAs<std::int32_t>(stmt, 2 + static_cast<int>(group));
  const auto request = columnAs<std::uint8_t>(stmt, 7);
  rel.request = request <= std::to_underlying(TalkRequest::Transfer)
                    ? static_cast<TalkRequest>(request)
                    : TalkRequest::None;
  rel.request_day = columnAs<std::int32_t>(stmt, 8);
  rel.quiet_until = columnAs<std::int32_t>(stmt, 9);
  rel.escalations = columnAs<std::uint8_t>(stmt, 10);
  rel.low_morale_weeks = columnAs<std::uint8_t>(stmt, 11);
  rel.joined_day = columnAs<std::int32_t>(stmt, 12);
  rel.broken_promise_day = columnAs<std::int32_t>(stmt, 13);
  return rel;
}

std::optional<Promise> readPromise(sqlite3_stmt* stmt)
{
  Promise promise;
  promise.id = columnAs<std::uint32_t>(stmt, 0);
  promise.player_id = columnAs<PlayerID>(stmt, 1);
  const auto type = columnAs<std::uint8_t>(stmt, 2);
  const auto promise_state = columnAs<std::uint8_t>(stmt, 3);
  const auto reason = columnAs<std::uint8_t>(stmt, 4);
  if (type > std::to_underlying(PromiseType::Signing) ||
      promise_state > std::to_underlying(PromiseState::Voided) ||
      reason > std::to_underlying(PromiseVoidReason::TooFewMatches))
    return std::nullopt;
  promise.type = static_cast<PromiseType>(type);
  promise.state = static_cast<PromiseState>(promise_state);
  promise.void_reason = static_cast<PromiseVoidReason>(reason);
  promise.made_day = columnAs<std::int32_t>(stmt, 5);
  promise.deadline_day = columnAs<std::int32_t>(stmt, 6);
  promise.resolved_day = columnAs<std::int32_t>(stmt, 7);
  promise.target = static_cast<float>(sqlite3_column_double(stmt, 8));
  promise.baseline = static_cast<float>(sqlite3_column_double(stmt, 9));
  promise.team_minutes = columnAs<std::uint32_t>(stmt, 10);
  promise.player_minutes = columnAs<std::uint32_t>(stmt, 11);
  promise.matches = columnAs<std::uint16_t>(stmt, 12);
  promise.fulfilled = sqlite3_column_int(stmt, 13) != 0;
  return promise;
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

struct InboxNotice
{
  InboxCategory category;
  const char* title_key;
  const char* body_key;
  std::vector<std::string> args;
  PlayerID player_id;
  bool read = false;
};

void post(Inbox& inbox, const GameDateValue& date, InboxNotice notice)
{
  InboxMessage message;
  message.read = notice.read;
  message.date = date;
  message.category = notice.category;
  message.title_key = notice.title_key;
  message.body_key = notice.body_key;
  message.args = std::move(notice.args);
  message.player_id = notice.player_id;
  inbox.add(std::move(message));
}
/** League of @p team_id (its country's transfer windows apply). */
LeagueID leagueOf(const GameData& gamedata, TeamID team_id)
{
  const auto team = gamedata.getTeam(team_id);
  return team ? team->get().getLeagueId() : LeagueID{0};
}
}  // namespace

// ---------------------------------------------------------------------------
// Pure model
// ---------------------------------------------------------------------------

float Promise::progress() const
{
  using enum PromiseType;
  using enum PromiseState;
  switch (type)
  {
    case PlayingTime:
    {
      if (team_minutes == 0 || target <= 0.0f) return 0.0f;
      const float share =
          static_cast<float>(player_minutes) / static_cast<float>(team_minutes);
      return std::clamp(share / target, 0.0f, 1.0f);
    }
    case NewContract:
    case Signing:
      return fulfilled || state == Kept ? 1.0f : 0.0f;
  }
  return 0.0f;
}

namespace Interactions
{
const char* optionKey(TalkOption option)
{
  using enum TalkOption;
  switch (option)
  {
    case PraiseForm:
      return "TALK_OPTION_PRAISE";
    case CriticiseForm:
      return "TALK_OPTION_CRITICISE";
    case DiscussPlayingTime:
      return "TALK_OPTION_PLAYING_TIME";
    case PromisePlayingTime:
      return "TALK_OPTION_PROMISE_PLAYING_TIME";
    case PromiseContract:
      return "TALK_OPTION_PROMISE_CONTRACT";
    case PromiseSigning:
      return "TALK_OPTION_PROMISE_SIGNING";
    case AskPatience:
      return "TALK_OPTION_PATIENCE";
    case ReassureStay:
      return "TALK_OPTION_REASSURE";
    case OpenToOffers:
      return "TALK_OPTION_OPEN_TO_OFFERS";
    case AcceptTransferRequest:
      return "TALK_OPTION_ACCEPT_TRANSFER";
    case RefuseTransferRequest:
      return "TALK_OPTION_REFUSE_TRANSFER";
    case COUNT:
      break;
  }
  return "TALK_OPTION_PRAISE";
}

const char* optionDescriptionKey(TalkOption option)
{
  using enum TalkOption;
  switch (option)
  {
    case PraiseForm:
      return "TALK_DESC_PRAISE";
    case CriticiseForm:
      return "TALK_DESC_CRITICISE";
    case DiscussPlayingTime:
      return "TALK_DESC_PLAYING_TIME";
    case PromisePlayingTime:
      return "TALK_DESC_PROMISE_PLAYING_TIME";
    case PromiseContract:
      return "TALK_DESC_PROMISE_CONTRACT";
    case PromiseSigning:
      return "TALK_DESC_PROMISE_SIGNING";
    case AskPatience:
      return "TALK_DESC_PATIENCE";
    case ReassureStay:
      return "TALK_DESC_REASSURE";
    case OpenToOffers:
      return "TALK_DESC_OPEN_TO_OFFERS";
    case AcceptTransferRequest:
      return "TALK_DESC_ACCEPT_TRANSFER";
    case RefuseTransferRequest:
      return "TALK_DESC_REFUSE_TRANSFER";
    case COUNT:
      break;
  }
  return "TALK_DESC_PRAISE";
}

const char* reactionKey(TalkReaction reaction)
{
  using enum TalkReaction;
  switch (reaction)
  {
    case Positive:
      return "TALK_REACTION_POSITIVE";
    case Neutral:
      return "TALK_REACTION_NEUTRAL";
    case Negative:
      break;
  }
  return "TALK_REACTION_NEGATIVE";
}

const char* hintKey(TalkHint hint)
{
  using enum TalkHint;
  switch (hint)
  {
    case LikelyPositive:
      return "TALK_HINT_POSITIVE";
    case Uncertain:
      return "TALK_HINT_UNCERTAIN";
    case Risky:
      break;
  }
  return "TALK_HINT_RISKY";
}

const char* blockKey(TalkBlock block)
{
  using enum TalkBlock;
  switch (block)
  {
    case None:
      return "";
    case Cooldown:
      return "TALK_BLOCK_COOLDOWN";
    case NotAtClub:
      return "TALK_BLOCK_NOT_AT_CLUB";
    case NoRecentMatches:
      return "TALK_BLOCK_NO_MATCHES";
    case Injured:
      return "TALK_BLOCK_INJURED";
    case PromiseActive:
      return "TALK_BLOCK_PROMISE_ACTIVE";
    case PromiseNotCredible:
      return "TALK_BLOCK_NOT_CREDIBLE";
    case ContractLong:
      return "TALK_BLOCK_CONTRACT_LONG";
    case NoWindowAhead:
      return "TALK_BLOCK_NO_WINDOW";
    case NoTransferInterest:
      return "TALK_BLOCK_NO_INTEREST";
    case NoTransferRequest:
      return "TALK_BLOCK_NO_REQUEST";
    case NotUnhappy:
      break;
  }
  return "TALK_BLOCK_NOT_UNHAPPY";
}

const char* requestKey(TalkRequest request)
{
  using enum TalkRequest;
  switch (request)
  {
    case None:
      return "";
    case PlayingTime:
      return "TALK_REQUEST_PLAYING_TIME";
    case NewContract:
      return "TALK_REQUEST_CONTRACT";
    case Transfer:
      break;
  }
  return "TALK_REQUEST_TRANSFER";
}

const char* promiseTypeKey(PromiseType type)
{
  using enum PromiseType;
  switch (type)
  {
    case PlayingTime:
      return "PROMISE_TYPE_PLAYING_TIME";
    case NewContract:
      return "PROMISE_TYPE_CONTRACT";
    case Signing:
      break;
  }
  return "PROMISE_TYPE_SIGNING";
}

const char* promiseStateKey(PromiseState state)
{
  using enum PromiseState;
  switch (state)
  {
    case Active:
      return "PROMISE_STATE_ACTIVE";
    case Kept:
      return "PROMISE_STATE_KEPT";
    case Broken:
      return "PROMISE_STATE_BROKEN";
    case Voided:
      break;
  }
  return "PROMISE_STATE_VOIDED";
}

const char* voidReasonKey(PromiseVoidReason reason)
{
  using enum PromiseVoidReason;
  switch (reason)
  {
    case None:
      return "";
    case PlayerLeft:
      return "PROMISE_VOID_LEFT";
    case LongInjury:
      return "PROMISE_VOID_INJURY";
    case TooFewMatches:
      break;
  }
  return "PROMISE_VOID_MATCHES";
}

const char* toneKey(TeamTalkTone tone)
{
  using enum TeamTalkTone;
  switch (tone)
  {
    case Calm:
      return "TEAMTALK_TONE_CALM";
    case Motivate:
      return "TEAMTALK_TONE_MOTIVATE";
    case DemandMore:
      return "TEAMTALK_TONE_DEMAND";
    case Praise:
      return "TEAMTALK_TONE_PRAISE";
    case NoPressure:
    case COUNT:
      break;
  }
  return "TEAMTALK_TONE_NO_PRESSURE";
}

const char* toneDescriptionKey(TeamTalkTone tone)
{
  using enum TeamTalkTone;
  switch (tone)
  {
    case Calm:
      return "TEAMTALK_DESC_CALM";
    case Motivate:
      return "TEAMTALK_DESC_MOTIVATE";
    case DemandMore:
      return "TEAMTALK_DESC_DEMAND";
    case Praise:
      return "TEAMTALK_DESC_PRAISE";
    case NoPressure:
    case COUNT:
      break;
  }
  return "TEAMTALK_DESC_NO_PRESSURE";
}

const char* moodKey(DressingMood mood)
{
  using enum DressingMood;
  switch (mood)
  {
    case Buoyant:
      return "DRESSING_MOOD_BUOYANT";
    case Settled:
      return "DRESSING_MOOD_SETTLED";
    case Uneasy:
      return "DRESSING_MOOD_UNEASY";
    case Tense:
      break;
  }
  return "DRESSING_MOOD_TENSE";
}

TalkGroup groupOf(TalkOption option)
{
  using enum TalkOption;
  switch (option)
  {
    case PraiseForm:
    case CriticiseForm:
      return TalkGroup::Form;
    case DiscussPlayingTime:
      return TalkGroup::PlayingTime;
    case PromisePlayingTime:
    case PromiseContract:
    case PromiseSigning:
      return TalkGroup::Promise;
    case AskPatience:
      return TalkGroup::Patience;
    default:
      return TalkGroup::Transfer;
  }
}

std::int32_t cooldownDays(TalkGroup group)
{
  using enum TalkGroup;
  switch (group)
  {
    case Form:
      return 14;
    case PlayingTime:
      return 28;
    case Promise:
      return 21;
    case Patience:
      return 28;
    case Transfer:
    case COUNT:
      break;
  }
  return 21;
}

float expectedRating(SquadRole role)
{
  using enum SquadRole;
  switch (role)
  {
    case KeyPlayer:
      return 7.0f;
    case FirstTeam:
      return 6.8f;
    case Rotation:
      return 6.6f;
    case Backup:
    case Fringe:
      break;
  }
  return 6.4f;
}

float expectedShare(SquadRole role, int age)
{
  using enum SquadRole;
  // Mirrors the weekly morale model's playing-time expectation.
  float share = 0.03f;
  switch (role)
  {
    case KeyPlayer:
      share = 0.85f;
      break;
    case FirstTeam:
      share = 0.70f;
      break;
    case Rotation:
      share = 0.40f;
      break;
    case Backup:
      share = 0.15f;
      break;
    case Fringe:
      break;
  }
  return age <= 19 ? std::min(share, 0.10f) : share;
}

float positiveChance(const TalkContext& context, TalkOption option)
{
  return chanceAbove(talkScore(context, option));
}

float negativeChance(const TalkContext& context, TalkOption option)
{
  return chanceBelow(talkScore(context, option));
}

TalkHint hintFor(float positive, float negative)
{
  using enum TalkHint;
  if (positive >= 0.6f) return LikelyPositive;
  return negative >= 0.4f ? Risky : Uncertain;
}

TalkOutcome evaluateTalk(const TalkContext& context, TalkOption option,
                         double roll, double variant_roll)
{
  using enum TalkOption;
  TalkOutcome outcome;
  outcome.option = option;
  outcome.reaction = reactionFor(talkScore(context, option), roll);
  const Deltas deltas = deltasFor(option, outcome.reaction);
  outcome.morale_delta = deltas.morale * temperamentScale(context.traits);
  outcome.trust_delta = deltas.trust;

  const bool negative = outcome.reaction == TalkReaction::Negative;
  const bool positive = outcome.reaction == TalkReaction::Positive;
  const float ambition = unit(context.traits.ambition);
  switch (option)
  {
    case PromisePlayingTime:
      if (!negative) outcome.promise = PromiseType::PlayingTime;
      outcome.request_resolved =
          !negative && context.request == TalkRequest::PlayingTime;
      break;
    case PromiseContract:
      if (!negative) outcome.promise = PromiseType::NewContract;
      outcome.request_resolved =
          !negative && context.request == TalkRequest::NewContract;
      break;
    case PromiseSigning:
      if (!negative) outcome.promise = PromiseType::Signing;
      break;
    case AskPatience:
      outcome.request_resolved = !negative;
      outcome.quiet_days = positive ? 42 : 21;
      outcome.transfer_request = negative && ambition >= 0.6f &&
                                 context.request != TalkRequest::Transfer &&
                                 context.request != TalkRequest::None;
      break;
    case ReassureStay:
      outcome.settled = positive;
      outcome.transfer_request =
          negative && ambition >= 0.7f && context.request == TalkRequest::None;
      break;
    case AcceptTransferRequest:
      outcome.list_player = true;
      outcome.request_resolved = true;
      outcome.quiet_days = 90;
      break;
    case RefuseTransferRequest:
      outcome.request_resolved = true;
      if (positive)
        outcome.quiet_days = 90;
      else if (negative)
        outcome.quiet_days = 45;
      else
        outcome.quiet_days = 60;
      break;
    default:
      break;
  }

  static constexpr std::array REACTION_TOKENS = {"POS", "NEU", "NEG"};
  const int variants = replyVariants(option);
  const int variant =
      std::min(variants - 1,
               static_cast<int>(variant_roll * static_cast<double>(variants)));
  outcome.reply_key = std::format(
      "TALK_REPLY_{}_{}_{}", optionToken(option),
      REACTION_TOKENS[std::to_underlying(outcome.reaction)], variant);
  return outcome;
}

std::vector<TeamTalkPrediction> predictTeamTalk(const TeamTalkContext& context)
{
  static const std::vector<TalkListener> NEUTRAL_SQUAD = {TalkListener{}};
  const std::vector<TalkListener>& listeners =
      context.listeners.empty() ? NEUTRAL_SQUAD : context.listeners;
  std::vector<TeamTalkPrediction> predictions;
  predictions.reserve(std::to_underlying(TeamTalkTone::COUNT));
  for (std::size_t index = 0; index < std::to_underlying(TeamTalkTone::COUNT);
       ++index)
  {
    const auto tone = static_cast<TeamTalkTone>(index);
    TeamTalkPrediction prediction;
    prediction.tone = tone;
    for (const TalkListener& listener : listeners)
    {
      const float score = listenerScore(context, listener, tone);
      prediction.positive_share += chanceAbove(score);
      prediction.negative_share += chanceBelow(score);
    }
    const auto count = static_cast<float>(listeners.size());
    prediction.positive_share /= count;
    prediction.negative_share /= count;
    prediction.hint =
        hintFor(prediction.positive_share, prediction.negative_share);
    predictions.push_back(prediction);
  }
  return predictions;
}

TeamTalkResult evaluateTeamTalk(const TeamTalkContext& context,
                                TeamTalkTone tone, std::uint64_t seed)
{
  using enum TalkReaction;
  TeamTalkResult result;
  result.tone = tone;
  const std::uint64_t moment_key =
      mixHash(static_cast<std::uint64_t>(context.day),
              std::to_underlying(context.moment) + 1);
  float morale_total = 0.0f;
  for (const TalkListener& listener : context.listeners)
  {
    const double roll = WorldRng::hashUniform(seed, RngDomain::Interactions,
                                              moment_key, listener.player_id);
    const TalkReaction reaction =
        reactionFor(listenerScore(context, listener, tone), roll);
    float delta = 0.0f;
    switch (reaction)
    {
      case Positive:
        ++result.positive;
        delta = 2.5f;
        break;
      case Neutral:
        ++result.neutral;
        break;
      case Negative:
        ++result.negative;
        delta = -3.0f;
        break;
    }
    delta *= temperamentScale(listener.traits);
    morale_total += delta;
    result.reactions.emplace_back(listener.player_id, reaction);
    result.morale_changes.emplace_back(listener.player_id, delta);
  }
  const std::size_t count = context.listeners.size();
  if (count > 0)
  {
    const float balance = (static_cast<float>(result.positive) -
                           static_cast<float>(result.negative)) /
                          static_cast<float>(count);
    result.modifier = std::clamp(TALK_CAP * balance, -TALK_CAP, TALK_CAP);
    result.morale_delta = morale_total / static_cast<float>(count);
  }
  const float positive_share =
      count == 0
          ? 0.0f
          : static_cast<float>(result.positive) / static_cast<float>(count);
  const float negative_share =
      count == 0
          ? 0.0f
          : static_cast<float>(result.negative) / static_cast<float>(count);
  if (positive_share >= 0.6f)
    result.summary_key = "TEAMTALK_SUMMARY_GOOD";
  else if (negative_share >= 0.4f)
    result.summary_key = "TEAMTALK_SUMMARY_POOR";
  else
    result.summary_key = "TEAMTALK_SUMMARY_MIXED";
  return result;
}

float cohesionModifier(float new_share)
{
  // [WR 9.6] 0 below 25% of minutes by newcomers, -2% from 40%.
  constexpr float START = 0.25f;
  constexpr float FULL = 0.40f;
  if (!(new_share > START)) return 0.0f;
  return -TOTAL_CAP * std::min(1.0f, (new_share - START) / (FULL - START));
}

float newness(std::int32_t joined_day, std::int32_t today)
{
  if (joined_day <= 0 || today < joined_day) return 0.0f;
  return std::clamp(1.0f - static_cast<float>(today - joined_day) /
                               static_cast<float>(SETTLE_DAYS),
                    0.0f, 1.0f);
}

float hierarchyScore(int tenure_days, int age, float standing,
                     const PlayerTraits& traits)
{
  const float tenure =
      std::clamp(static_cast<float>(tenure_days) / (365.0f * 6.0f), 0.0f, 1.0f);
  const float seniority =
      std::clamp(static_cast<float>(age - 18) / 14.0f, 0.0f, 1.0f);
  const float personality =
      (unit(traits.professionalism) + unit(traits.temperament)) * 0.5f;
  return 100.0f *
         (0.30f * tenure + 0.25f * seniority +
          0.25f * std::clamp(standing, 0.0f, 1.0f) + 0.20f * personality);
}
}  // namespace Interactions

// ---------------------------------------------------------------------------
// InteractionSystem
// ---------------------------------------------------------------------------

InteractionSystem::InteractionSystem(std::shared_ptr<GameData> gd,
                                     std::function<SquadRole(PlayerID)> role)
    : gamedata(std::move(gd)), role_of(std::move(role))
{
}

PlayerRelation& InteractionSystem::relationFor(PlayerID player_id)
{
  return state.relations[player_id];
}

const PlayerRelation* InteractionSystem::relation(PlayerID player_id) const
{
  const auto found = state.relations.find(player_id);
  return found == state.relations.end() ? nullptr : &found->second;
}

float InteractionSystem::moraleTargetOffset(PlayerID player_id) const
{
  const PlayerRelation* rel = relation(player_id);
  if (rel == nullptr) return 0.0f;
  return rel->trust * TRUST_MORALE_WEIGHT +
         (rel->request != TalkRequest::None ? OPEN_REQUEST_MORALE : 0.0f);
}

TalkContext InteractionSystem::contextFor(const Player& player,
                                          const GameDateValue& date,
                                          float manager_standing) const
{
  const PlayerDynamics& dynamics = player.getDynamics();
  TalkContext context;
  context.traits = player.getTraits();
  context.age = player.getAge();
  context.role = role_of ? role_of(player.getId()) : SquadRole::Rotation;
  context.form = player.getForm();
  context.rated_matches = dynamics.rating_count;
  context.expected_rating = Interactions::expectedRating(context.role);
  context.playing_share = dynamics.playing_share;
  context.expected_share =
      Interactions::expectedShare(context.role, context.age);
  context.morale = dynamics.morale;
  context.manager_standing = manager_standing;
  context.injured = dynamics.injury_days > 0;
  context.transfer_interest = dynamics.transfer_interest_weeks > 0;
  context.contract_years = player.getContractYears();
  if (const PlayerRelation* rel = relation(player.getId()))
  {
    context.trust = rel->trust;
    context.request = rel->request;
    context.promise_credible =
        rel->broken_promise_day == 0 ||
        dayOrdinal(date) - rel->broken_promise_day > PROMISE_CREDIBILITY_DAYS;
  }
  return context;
}

std::optional<GameDateValue> InteractionSystem::signingDeadline(
    const GameDateValue& date, LeagueID league)
{
  const GameDateValue opening = TransferWindows::nextOpening(league, date);
  if (SeasonCalendar::addDays(date, SIGNING_WINDOW_SEARCH_DAYS) < opening)
    return std::nullopt;
  return TransferWindows::windowEnd(league, opening);
}

TalkBlock InteractionSystem::blockFor(const Player& player, TalkOption option,
                                      const TalkContext& context,
                                      const GameDateValue& date,
                                      TeamID managed_team_id,
                                      std::int32_t& cooldown) const
{
  cooldown = 0;
  const std::int32_t today = dayOrdinal(date);
  if (managed_team_id == FREE_AGENTS_TEAM_ID ||
      player.getTeamId() != managed_team_id)
    return TalkBlock::NotAtClub;
  const TalkBlock situation =
      optionBlockFor(player, option, context, date, managed_team_id);
  if (situation != TalkBlock::None) return situation;
  const PlayerRelation* rel = relation(player.getId());
  if (rel == nullptr) return TalkBlock::None;
  const TalkGroup group = Interactions::groupOf(option);
  const std::int32_t last = rel->last_talk[std::to_underlying(group)];
  const bool answers_request =
      context.request != TalkRequest::None &&
      (group == TalkGroup::Patience || group == TalkGroup::Transfer ||
       group == TalkGroup::Promise);
  if (last > 0 && !answers_request &&
      today - last < Interactions::cooldownDays(group))
  {
    cooldown = Interactions::cooldownDays(group) - (today - last);
    return TalkBlock::Cooldown;
  }
  return TalkBlock::None;
}

bool InteractionSystem::hasActivePromise(PlayerID player_id,
                                         PromiseType type) const
{
  return std::ranges::any_of(state.promises,
                             [player_id, type](const Promise& promise)
                             {
                               return promise.player_id == player_id &&
                                      promise.type == type &&
                                      promise.state == PromiseState::Active;
                             });
}

TalkBlock InteractionSystem::optionBlockFor(const Player& player,
                                            TalkOption option,
                                            const TalkContext& context,
                                            const GameDateValue& date,
                                            TeamID managed_team_id) const
{
  using enum TalkOption;
  // Situation first: an option that does not apply is hidden, not cooling.
  switch (option)
  {
    case PraiseForm:
    case CriticiseForm:
      if (context.rated_matches == 0) return TalkBlock::NoRecentMatches;
      break;
    case PromisePlayingTime:
      if (context.injured) return TalkBlock::Injured;
      if (hasActivePromise(player.getId(), PromiseType::PlayingTime))
        return TalkBlock::PromiseActive;
      if (!context.promise_credible) return TalkBlock::PromiseNotCredible;
      break;
    case PromiseContract:
      if (context.contract_years > 2) return TalkBlock::ContractLong;
      if (hasActivePromise(player.getId(), PromiseType::NewContract))
        return TalkBlock::PromiseActive;
      if (!context.promise_credible) return TalkBlock::PromiseNotCredible;
      break;
    case PromiseSigning:
      if (hasActivePromise(player.getId(), PromiseType::Signing))
        return TalkBlock::PromiseActive;
      if (!context.promise_credible) return TalkBlock::PromiseNotCredible;
      if (!signingDeadline(date, leagueOf(*gamedata, managed_team_id)))
        return TalkBlock::NoWindowAhead;
      break;
    case AskPatience:
      if (context.request == TalkRequest::None && context.morale >= 45.0f)
        return TalkBlock::NotUnhappy;
      break;
    case ReassureStay:
    case OpenToOffers:
      if (!context.transfer_interest &&
          context.request != TalkRequest::Transfer)
        return TalkBlock::NoTransferInterest;
      break;
    case AcceptTransferRequest:
    case RefuseTransferRequest:
      if (context.request != TalkRequest::Transfer)
        return TalkBlock::NoTransferRequest;
      break;
    default:
      break;
  }
  return TalkBlock::None;
}

std::vector<TalkOptionView> InteractionSystem::options(
    PlayerID player_id, const GameDateValue& date, TeamID managed_team_id,
    float manager_standing) const
{
  using enum TalkOption;
  std::vector<TalkOptionView> views;
  const auto player = gamedata->getPlayer(player_id);
  if (!player) return views;
  const TalkContext context = contextFor(player->get(), date, manager_standing);
  const float form = formDelta(context);
  views.reserve(std::to_underlying(COUNT));
  for (std::size_t index = 0; index < std::to_underlying(COUNT); ++index)
  {
    TalkOptionView view;
    view.option = static_cast<TalkOption>(index);
    view.block = blockFor(player->get(), view.option, context, date,
                          managed_team_id, view.cooldown_days);
    view.positive_chance = Interactions::positiveChance(context, view.option);
    view.hint = Interactions::hintFor(
        view.positive_chance,
        Interactions::negativeChance(context, view.option));
    switch (view.option)
    {
      case PraiseForm:
        view.suggested = context.rated_matches > 0 && form > 0.4f;
        break;
      case CriticiseForm:
        view.suggested = context.rated_matches > 0 && form < -0.6f;
        break;
      case DiscussPlayingTime:
      case PromisePlayingTime:
        view.suggested = context.request == TalkRequest::PlayingTime;
        break;
      case PromiseContract:
        view.suggested = context.request == TalkRequest::NewContract;
        break;
      case AskPatience:
        view.suggested = context.request != TalkRequest::None;
        break;
      case ReassureStay:
      case OpenToOffers:
        view.suggested = context.transfer_interest;
        break;
      case AcceptTransferRequest:
      case RefuseTransferRequest:
        view.suggested = context.request == TalkRequest::Transfer;
        break;
      default:
        break;
    }
    views.push_back(view);
  }
  return views;
}

float InteractionSystem::firstTeamThreshold(TeamID team_id) const
{
  const StatsConfig& config = gamedata->getStatsConfig();
  std::vector<double> overalls;
  for (const auto& player : gamedata->getPlayersForTeam(team_id))
    overalls.push_back(player.get().getOverall(config));
  if (overalls.empty()) return 0.0f;
  std::ranges::sort(overalls, std::greater<>());
  constexpr std::size_t XI = 11;
  return static_cast<float>(overalls[std::min(XI, overalls.size()) - 1]);
}

std::optional<TalkOutcome> InteractionSystem::talk(PlayerID player_id,
                                                   TalkOption option,
                                                   const GameDateValue& date,
                                                   TeamID managed_team_id,
                                                   float manager_standing)
{
  using enum PromiseType;
  auto& players = gamedata->getPlayers();
  const auto found = players.find(player_id);
  if (found == players.end() || option >= TalkOption::COUNT)
    return std::nullopt;
  Player& player = found->second;
  const TalkContext context = contextFor(player, date, manager_standing);
  if (std::int32_t cooldown = 0;
      blockFor(player, option, context, date, managed_team_id, cooldown) !=
      TalkBlock::None)
    return std::nullopt;

  const std::int32_t today = dayOrdinal(date);
  const std::uint64_t seed = gamedata->getWorldSeed();
  const std::uint64_t key = optionKeyOf(player_id, option);
  const double roll = WorldRng::hashUniform(
      seed, RngDomain::Interactions, static_cast<std::uint64_t>(today), key);
  const double variant_roll = WorldRng::hashUniform(
      seed, RngDomain::Interactions,
      mixHash(static_cast<std::uint64_t>(today), key), key);
  TalkOutcome outcome =
      Interactions::evaluateTalk(context, option, roll, variant_roll);

  PlayerDynamics& dynamics = player.mutableDynamics();
  dynamics.morale = clampMorale(dynamics.morale + outcome.morale_delta);
  PlayerRelation& rel = relationFor(player_id);
  rel.trust = clampTrust(rel.trust + outcome.trust_delta);
  rel.last_talk[std::to_underlying(Interactions::groupOf(option))] = today;
  if (outcome.settled) dynamics.transfer_interest_weeks = 0;

  if (outcome.promise)
  {
    Promise promise;
    promise.id = state.next_promise_id++;
    promise.player_id = player_id;
    promise.type = *outcome.promise;
    promise.made_day = today;
    switch (promise.type)
    {
      case PlayingTime:
        promise.target = isFirstChoice(context.role) ? 0.7f : 0.5f;
        promise.deadline_day = today + PLAYING_TIME_PROMISE_DAYS;
        break;
      case NewContract:
        promise.baseline = static_cast<float>(player.getContractYears());
        promise.deadline_day = today + CONTRACT_PROMISE_DAYS;
        break;
      case Signing:
        promise.target = firstTeamThreshold(managed_team_id);
        promise.deadline_day = dayOrdinal(
            signingDeadline(date, leagueOf(*gamedata, managed_team_id))
                .value_or(date));
        break;
    }
    state.promises.push_back(promise);
  }
  if (outcome.request_resolved)
  {
    rel.request = TalkRequest::None;
    rel.escalations = 0;
    rel.low_morale_weeks = 0;
    rel.quiet_until = today + (outcome.quiet_days > 0 ? outcome.quiet_days
                                                      : REQUEST_QUIET_DAYS);
  }
  if (outcome.transfer_request)
  {
    rel.request = TalkRequest::Transfer;
    rel.request_day = today;
  }
  return outcome;
}

// ---------------------------------------------------------------------------
// Promises and requests
// ---------------------------------------------------------------------------

void InteractionSystem::onDayAdvanced(const GameDateValue& date,
                                      TeamID managed_team_id, Inbox& inbox)
{
  const std::int32_t today = dayOrdinal(date);
  if (today <= state.last_evaluated_day) return;
  state.last_evaluated_day = today;
  evaluatePromises(date, managed_team_id, inbox);
  if (today % 7 == 0) onWeek(date, managed_team_id, inbox);
}

void InteractionSystem::resolvePromise(Promise& promise, PromiseState outcome,
                                       PromiseVoidReason reason,
                                       const GameDateValue& date, Inbox& inbox)
{
  using enum PromiseVoidReason;
  using enum PromiseState;
  const std::int32_t today = dayOrdinal(date);
  promise.state = outcome;
  promise.void_reason = reason;
  promise.resolved_day = today;
  auto& players = gamedata->getPlayers();
  const auto found = players.find(promise.player_id);
  if (found == players.end() || reason == PlayerLeft) return;
  Player& player = found->second;
  PlayerDynamics& dynamics = player.mutableDynamics();
  PlayerRelation& rel = relationFor(promise.player_id);
  const float scale = temperamentScale(player.getTraits());
  const std::string what =
      std::string("@") + Interactions::promiseTypeKey(promise.type);
  switch (outcome)
  {
    case Kept:
      dynamics.morale = clampMorale(dynamics.morale + KEPT_MORALE * scale);
      rel.trust = clampTrust(rel.trust + KEPT_TRUST);
      post(inbox, date,
           {InboxCategory::General,
            "PROMISE_KEPT_TITLE",
            "PROMISE_KEPT_BODY",
            {player.getName(), what},
            promise.player_id});
      break;
    case Broken:
    {
      dynamics.morale = clampMorale(dynamics.morale - BROKEN_MORALE * scale);
      rel.trust = clampTrust(rel.trust - BROKEN_TRUST);
      rel.broken_promise_day = today;
      const bool wants_out =
          rel.trust < BROKEN_REQUEST_TRUST ||
          player.getTraits().ambition >= BROKEN_REQUEST_AMBITION;
      if (wants_out && rel.request != TalkRequest::Transfer)
      {
        rel.request = TalkRequest::Transfer;
        rel.request_day = today;
        rel.escalations = 1;
      }
      post(inbox, date,
           {InboxCategory::Contract,
            "PROMISE_BROKEN_TITLE",
            wants_out ? "PROMISE_BROKEN_BODY_TRANSFER" : "PROMISE_BROKEN_BODY",
            {player.getName(), what},
            promise.player_id});
      break;
    }
    case Voided:
      post(inbox, date,
           {InboxCategory::General,
            "PROMISE_VOIDED_TITLE",
            "PROMISE_VOIDED_BODY",
            {player.getName(), what,
             std::string("@") + Interactions::voidReasonKey(reason)},
            promise.player_id});
      break;
    case Active:
      break;
  }
}

void InteractionSystem::evaluatePromises(const GameDateValue& date,
                                         TeamID managed_team_id, Inbox& inbox)
{
  const std::int32_t today = dayOrdinal(date);
  for (Promise& promise : state.promises)
  {
    if (promise.state != PromiseState::Active) continue;
    const auto player = gamedata->getPlayer(promise.player_id);
    if (!player || player->get().getTeamId() != managed_team_id)
    {
      resolvePromise(promise, PromiseState::Voided,
                     PromiseVoidReason::PlayerLeft, date, inbox);
      continue;
    }
    const bool due = today >= promise.deadline_day;
    evaluatePromise(promise, player->get(), due, date, today, inbox);
  }
  std::erase_if(state.promises,
                [today](const Promise& promise)
                {
                  return promise.state != PromiseState::Active &&
                         today - promise.resolved_day > PROMISE_HISTORY_DAYS;
                });
}

void InteractionSystem::evaluatePromise(Promise& promise, const Player& player,
                                        bool due, const GameDateValue& date,
                                        std::int32_t today, Inbox& inbox)
{
  using enum PromiseType;
  switch (promise.type)
  {
    case PlayingTime:
      evaluatePlayingTimePromise(promise, player, due, date, today, inbox);
      break;
    case NewContract:
      evaluateContractPromise(promise, player, due, date, inbox);
      break;
    case Signing:
      evaluateSigningPromise(promise, due, date, inbox);
      break;
  }
}

void InteractionSystem::evaluatePlayingTimePromise(
    Promise& promise, const Player& player, bool due, const GameDateValue& date,
    std::int32_t today, Inbox& inbox)
{
  constexpr std::uint16_t LONG_LAYOFF_DAYS = 21;
  const PlayerDynamics& dynamics = player.getDynamics();
  if (!due && dynamics.injury_days >= LONG_LAYOFF_DAYS &&
      today + dynamics.injury_days >= promise.deadline_day)
  {
    resolvePromise(promise, PromiseState::Voided, PromiseVoidReason::LongInjury,
                   date, inbox);
    return;
  }
  const float share = promise.team_minutes == 0
                          ? 0.0f
                          : static_cast<float>(promise.player_minutes) /
                                static_cast<float>(promise.team_minutes);
  const bool met = share + 1e-6f >= promise.target;
  if (!due && met && promise.matches >= PLAYING_TIME_EARLY_MATCHES)
    resolvePromise(promise, PromiseState::Kept, PromiseVoidReason::None, date,
                   inbox);
  else if (due && promise.matches < PLAYING_TIME_MIN_MATCHES)
    resolvePromise(promise, PromiseState::Voided,
                   PromiseVoidReason::TooFewMatches, date, inbox);
  else if (due)
    resolvePromise(promise, met ? PromiseState::Kept : PromiseState::Broken,
                   PromiseVoidReason::None, date, inbox);
}

void InteractionSystem::evaluateContractPromise(Promise& promise,
                                                const Player& player, bool due,
                                                const GameDateValue& date,
                                                Inbox& inbox)
{
  const auto years = static_cast<float>(player.getContractYears());
  if (years < promise.baseline) promise.baseline = years;
  if (years > promise.baseline) promise.fulfilled = true;
  if (promise.fulfilled || due)
    resolvePromise(
        promise, promise.fulfilled ? PromiseState::Kept : PromiseState::Broken,
        PromiseVoidReason::None, date, inbox);
}

void InteractionSystem::evaluateSigningPromise(Promise& promise, bool due,
                                               const GameDateValue& date,
                                               Inbox& inbox)
{
  if (promise.fulfilled || due)
    resolvePromise(
        promise, promise.fulfilled ? PromiseState::Kept : PromiseState::Broken,
        PromiseVoidReason::None, date, inbox);
}

void InteractionSystem::onWeek(const GameDateValue& date,
                               TeamID managed_team_id, Inbox& inbox)
{
  const std::int32_t today = dayOrdinal(date);
  for (auto& [player_id, rel] : state.relations)
    rel.trust *= TRUST_WEEKLY_RETENTION;
  if (managed_team_id != FREE_AGENTS_TEAM_ID)
  {
    raiseRequests(date, managed_team_id, inbox);
    spreadLeaderMood(managed_team_id, date);
  }
  // Relations of other clubs' players only keep a recent arrival date.
  std::erase_if(state.relations,
                [this, managed_team_id, today](const auto& entry)
                {
                  const auto player = gamedata->getPlayer(entry.first);
                  if (!player) return true;
                  if (player->get().getTeamId() == managed_team_id)
                    return false;
                  return Interactions::newness(entry.second.joined_day,
                                               today) <= 0.0f;
                });
}

void InteractionSystem::raiseRequests(const GameDateValue& date,
                                      TeamID managed_team_id, Inbox& inbox)
{
  const std::int32_t today = dayOrdinal(date);
  const auto team = gamedata->getTeam(managed_team_id);
  if (!team) return;
  auto& players = gamedata->getPlayers();
  PlayerID candidate = 0;
  TalkRequest candidate_request = TalkRequest::None;
  float candidate_morale = 101.0f;
  for (const PlayerID player_id : team->get().getPlayerIDs())
  {
    const auto found = players.find(player_id);
    if (found == players.end()) continue;
    Player& player = found->second;
    PlayerDynamics& dynamics = player.mutableDynamics();
    PlayerRelation& rel = relationFor(player_id);
    rel.low_morale_weeks =
        dynamics.morale < UNHAPPY_MORALE
            ? static_cast<std::uint8_t>(std::min(rel.low_morale_weeks + 1, 52))
            : 0;
    if (resolveIgnoredRequest(player, dynamics, rel, date, today, inbox))
      continue;
    const TalkRequest request =
        requestForPlayer(player, dynamics, rel, date, today);
    if (request != TalkRequest::None && dynamics.morale < candidate_morale)
    {
      candidate = player_id;
      candidate_request = request;
      candidate_morale = dynamics.morale;
    }
  }
  if (candidate == 0 || (state.last_request_day > 0 &&
                         today - state.last_request_day < REQUEST_SPACING_DAYS))
    return;
  PlayerRelation& rel = relationFor(candidate);
  rel.request = candidate_request;
  rel.request_day = today;
  rel.escalations = 0;
  state.last_request_day = today;
  const Player& player = players.at(candidate);
  post(inbox, date,
       {candidate_request == TalkRequest::PlayingTime ? InboxCategory::General
                                                      : InboxCategory::Contract,
        "TALK_REQUEST_TITLE",
        std::format("{}_BODY", Interactions::requestKey(candidate_request))
            .c_str(),
        {player.getName()},
        candidate});
}

bool InteractionSystem::resolveIgnoredRequest(Player& player,
                                              PlayerDynamics& dynamics,
                                              PlayerRelation& relation,
                                              const GameDateValue& date,
                                              std::int32_t today, Inbox& inbox)
{
  if (relation.request == TalkRequest::None) return false;
  if (today - relation.request_day < REQUEST_ANSWER_DAYS) return true;
  if (relation.escalations == 0 && relation.request != TalkRequest::Transfer &&
      player.getTraits().ambition >= 50)
  {
    relation.request = TalkRequest::Transfer;
    relation.request_day = today;
    relation.escalations = 1;
    relation.trust = clampTrust(relation.trust - 8.0f);
    post(inbox, date,
         {InboxCategory::Contract,
          "TALK_ESCALATED_TITLE",
          "TALK_ESCALATED_BODY",
          {player.getName()},
          player.getId()});
  }
  else
  {
    relation.request = TalkRequest::None;
    relation.escalations = 0;
    relation.quiet_until = today + REQUEST_QUIET_DAYS;
    relation.trust = clampTrust(relation.trust - 10.0f);
    dynamics.morale = clampMorale(dynamics.morale - 3.0f);
    post(inbox, date,
         {InboxCategory::General,
          "TALK_IGNORED_TITLE",
          "TALK_IGNORED_BODY",
          {player.getName()},
          player.getId(),
          true});
  }
  return true;
}

TalkRequest InteractionSystem::requestForPlayer(const Player& player,
                                                const PlayerDynamics& dynamics,
                                                const PlayerRelation& relation,
                                                const GameDateValue& date,
                                                std::int32_t today) const
{
  using enum TalkRequest;
  if (relation.low_morale_weeks < UNHAPPY_WEEKS ||
      today < relation.quiet_until || dynamics.injury_days > 0 ||
      date.month == 6 || date.month == 7)
    return None;
  const SquadRole role =
      role_of ? role_of(player.getId()) : SquadRole::Rotation;
  if (dynamics.playing_share <
      Interactions::expectedShare(role, player.getAge()) - 0.15f)
    return PlayingTime;
  if (player.getContractYears() <= 1) return NewContract;
  if ((dynamics.transfer_interest_weeks > 0 || relation.trust < -30.0f) &&
      player.getTraits().ambition >= 55)
    return Transfer;
  return None;
}

void InteractionSystem::spreadLeaderMood(TeamID managed_team_id,
                                         const GameDateValue& date)
{
  const DressingRoom room = dressingRoom(managed_team_id, date);
  if (room.leaders.empty()) return;
  float leader_morale = 0.0f;
  for (const LeaderInfo& leader : room.leaders) leader_morale += leader.morale;
  leader_morale /= static_cast<float>(room.leaders.size());
  const float nudge =
      std::clamp(LEADER_INFLUENCE * (leader_morale - room.team_morale),
                 -LEADER_WEEKLY_CAP, LEADER_WEEKLY_CAP);
  if (std::abs(nudge) < 1e-3f) return;
  auto& players = gamedata->getPlayers();
  const auto team = gamedata->getTeam(managed_team_id);
  if (!team) return;
  for (const PlayerID player_id : team->get().getPlayerIDs())
  {
    if (std::ranges::any_of(room.leaders, [player_id](const LeaderInfo& leader)
                            { return leader.player_id == player_id; }))
      continue;
    const auto found = players.find(player_id);
    if (found == players.end()) continue;
    PlayerDynamics& dynamics = found->second.mutableDynamics();
    // Volatile players follow the leaders more. [P]
    const float follow =
        1.15f - 0.3f * unit(found->second.getTraits().temperament);
    dynamics.morale = clampMorale(dynamics.morale + nudge * follow);
  }
}

void InteractionSystem::onMatchPlayed(const MatchReport& report,
                                      TeamID managed_team_id)
{
  team_talks.erase(report.home_team_id);
  team_talks.erase(report.away_team_id);
  if (report.match_type == MatchType::FRIENDLY ||
      (report.home_team_id != managed_team_id &&
       report.away_team_id != managed_team_id))
    return;
  std::uint32_t match_minutes = 0;
  for (const PlayerMatchLine& line : report.players)
    if (line.team_id == managed_team_id)
      match_minutes = std::max<std::uint32_t>(match_minutes, line.minutes);
  if (match_minutes == 0) return;
  for (Promise& promise : state.promises)
  {
    if (promise.state != PromiseState::Active ||
        promise.type != PromiseType::PlayingTime)
      continue;
    const auto line = std::ranges::find_if(
        report.players,
        [promise_player_id = promise.player_id](const PlayerMatchLine& entry)
        { return entry.player_id == promise_player_id; });
    const std::uint32_t minutes =
        line == report.players.end() ? 0U : line->minutes;
    // Matches missed through injury do not count against the promise.
    if (const auto player = gamedata->getPlayer(promise.player_id);
        minutes == 0 && player && player->get().getDynamics().injury_days > 0)
      continue;
    promise.team_minutes += match_minutes;
    promise.player_minutes += std::min(minutes, match_minutes);
    ++promise.matches;
  }
}

void InteractionSystem::onTransferCompleted(
    const GameDateValue& date, PlayerID player_id, TeamID from_team_id,
    TeamID to_team_id, TeamID managed_team_id, Inbox& inbox)
{
  const std::int32_t today = dayOrdinal(date);
  if (from_team_id == managed_team_id)
  {
    for (Promise& promise : state.promises)
      if (promise.player_id == player_id &&
          promise.state == PromiseState::Active)
        resolvePromise(promise, PromiseState::Voided,
                       PromiseVoidReason::PlayerLeft, date, inbox);
  }
  // A new club, a new relationship.
  PlayerRelation fresh;
  fresh.joined_day = to_team_id == FREE_AGENTS_TEAM_ID ? 0 : today;
  state.relations[player_id] = fresh;

  if (to_team_id != managed_team_id) return;
  const auto player = gamedata->getPlayer(player_id);
  if (!player) return;
  const auto overall =
      static_cast<float>(player->get().getOverall(gamedata->getStatsConfig()));
  for (Promise& promise : state.promises)
  {
    if (promise.state == PromiseState::Active &&
        promise.type == PromiseType::Signing &&
        promise.player_id != player_id && overall + 1e-3f >= promise.target)
      promise.fulfilled = true;
  }
}

float InteractionSystem::adjustTrust(PlayerID player_id, float delta)
{
  PlayerRelation& rel = relationFor(player_id);
  const float before = rel.trust;
  rel.trust = clampTrust(rel.trust + delta);
  return rel.trust - before;
}

float InteractionSystem::onBidRejected(const GameDateValue& date,
                                       PlayerID player_id,
                                       const std::string& buyer_name,
                                       float morale_delta, float trust_delta,
                                       bool transfer_request, Inbox& inbox)
{
  auto& players = gamedata->getPlayers();
  const auto found = players.find(player_id);
  if (found == players.end() ||
      (morale_delta == 0.0f && trust_delta == 0.0f && !transfer_request))
    return 0.0f;
  Player& player = found->second;
  PlayerDynamics& dynamics = player.mutableDynamics();
  const float before = dynamics.morale;
  dynamics.morale = clampMorale(
      dynamics.morale + morale_delta * temperamentScale(player.getTraits()));
  PlayerRelation& rel = relationFor(player_id);
  rel.trust = clampTrust(rel.trust + trust_delta);
  const bool asks = transfer_request && rel.request != TalkRequest::Transfer;
  if (asks)
  {
    rel.request = TalkRequest::Transfer;
    rel.request_day = dayOrdinal(date);
    rel.escalations = 0;
  }
  if (asks)
    post(inbox, date,
         {InboxCategory::Contract,
          "OFFER_REJECTED_REQUEST_TITLE",
          "OFFER_REJECTED_REQUEST_BODY",
          {player.getName(), buyer_name},
          player_id});
  else
    post(inbox, date,
         {InboxCategory::General,
          "OFFER_REJECTED_UPSET_TITLE",
          "OFFER_REJECTED_UPSET_BODY",
          {player.getName(), buyer_name},
          player_id});
  return dynamics.morale - before;
}

std::vector<Promise> InteractionSystem::promisesFor(PlayerID player_id) const
{
  std::vector<Promise> result;
  for (const Promise& promise : state.promises)
    if (promise.player_id == player_id) result.push_back(promise);
  std::ranges::sort(
      result, [](const Promise& a, const Promise& b) { return a.id > b.id; });
  return result;
}

std::vector<PlayerID> InteractionSystem::pendingRequests() const
{
  std::vector<PlayerID> result;
  for (const auto& [player_id, rel] : state.relations)
    if (rel.request != TalkRequest::None) result.push_back(player_id);
  std::ranges::sort(result);
  return result;
}

// ---------------------------------------------------------------------------
// Team talks
// ---------------------------------------------------------------------------

TeamTalkContext InteractionSystem::teamTalkContext(
    TeamID team_id, TeamTalkMoment moment, const GameDateValue& date) const
{
  TeamTalkContext context;
  context.moment = moment;
  context.day = dayOrdinal(date);
  const auto team = gamedata->getTeam(team_id);
  if (!team) return context;
  const Lineup& lineup = team->get().getLineup();
  const auto add = [&context](const Player* player)
  {
    if (player == nullptr) return;
    context.listeners.push_back(TalkListener{
        player->getId(), player->getTraits(), player->getDynamics().morale});
  };
  add(lineup.getGoalkeeper());
  for (const auto& positioned : lineup.getOutfieldPlayers())
    add(positioned.player);
  return context;
}

bool InteractionSystem::canGiveTeamTalk(TeamID team_id, TeamTalkMoment moment,
                                        const GameDateValue& date) const
{
  const auto found = team_talks.find(team_id);
  if (found == team_talks.end() || found->second.day != dayOrdinal(date))
    return true;
  return moment == TeamTalkMoment::PreMatch ? !found->second.pre_done
                                            : !found->second.half_done;
}

std::optional<TeamTalkResult> InteractionSystem::giveTeamTalk(
    const TeamTalkContext& context, TeamID team_id, TeamTalkTone tone)
{
  if (tone >= TeamTalkTone::COUNT) return std::nullopt;
  TeamTalkRecord& record = team_talks[team_id];
  if (record.day != context.day) record = TeamTalkRecord{context.day};
  bool& done = context.moment == TeamTalkMoment::PreMatch ? record.pre_done
                                                          : record.half_done;
  if (done) return std::nullopt;
  TeamTalkResult result =
      Interactions::evaluateTeamTalk(context, tone, gamedata->getWorldSeed());
  done = true;
  (context.moment == TeamTalkMoment::PreMatch ? record.pre_modifier
                                              : record.half_modifier) =
      result.modifier;
  auto& players = gamedata->getPlayers();
  for (const auto& [player_id, delta] : result.morale_changes)
  {
    const auto found = players.find(player_id);
    if (found == players.end()) continue;
    PlayerDynamics& dynamics = found->second.mutableDynamics();
    dynamics.morale = clampMorale(dynamics.morale + delta);
  }
  return result;
}

float InteractionSystem::teamTalkModifier(TeamID team_id,
                                          const GameDateValue& date,
                                          int half) const
{
  const auto found = team_talks.find(team_id);
  if (found == team_talks.end() || found->second.day != dayOrdinal(date))
    return 0.0f;
  const TeamTalkRecord& record = found->second;
  // The pre-match words fade by the second half; half-time ones add.
  const float value = half <= 1
                          ? record.pre_modifier
                          : 0.5f * record.pre_modifier + record.half_modifier;
  return std::clamp(value, -Interactions::TALK_CAP, Interactions::TALK_CAP);
}

// ---------------------------------------------------------------------------
// Dressing room
// ---------------------------------------------------------------------------

DressingRoom InteractionSystem::dressingRoom(TeamID team_id,
                                             const GameDateValue& date) const
{
  DressingRoom room;
  const auto team = gamedata->getTeam(team_id);
  if (!team) return room;
  const std::int32_t today = dayOrdinal(date);
  const StatsConfig& config = gamedata->getStatsConfig();
  struct Member
  {
    const Player* player;
    double overall;
  };
  std::vector<Member> members;
  const auto& ids = team->get().getPlayerIDs();
  members.reserve(ids.size());
  for (const PlayerID player_id : ids)
  {
    const auto player = gamedata->getPlayer(player_id);
    if (player)
      members.push_back({&player->get(), player->get().getOverall(config)});
  }
  if (members.empty()) return room;
  std::ranges::sort(members,
                    [](const Member& a, const Member& b)
                    {
                      return a.overall > b.overall ||
                             (a.overall == b.overall &&
                              a.player->getId() < b.player->getId());
                    });

  std::vector<LeaderInfo> ranked;
  ranked.reserve(members.size());
  double minutes_total = 0.0;
  double minutes_new = 0.0;
  float morale_total = 0.0f;
  std::unordered_map<Language, std::vector<PlayerID>> by_nation;
  for (std::size_t rank = 0; rank < members.size(); ++rank)
  {
    const Player& player = *members[rank].player;
    const PlayerDynamics& dynamics = player.getDynamics();
    const PlayerRelation* rel = relation(player.getId());
    const std::int32_t joined = rel != nullptr ? rel->joined_day : 0;
    // Arrivals before the records: established, capped by the age.
    const int tenure =
        joined > 0 ? today - joined
                   : std::min(3 * 365, std::max(0, player.getAge() - 17) * 365);
    const float standing =
        members.size() <= 1 ? 1.0f
                            : 1.0f - static_cast<float>(rank) /
                                         static_cast<float>(members.size() - 1);
    ranked.push_back(
        LeaderInfo{player.getId(),
                   Interactions::hierarchyScore(tenure, player.getAge(),
                                                standing, player.getTraits()),
                   dynamics.morale, false});
    morale_total += dynamics.morale;
    if (dynamics.morale < 40.0f) ++room.unhappy;
    if (rel != nullptr && rel->request != TalkRequest::None)
      ++room.pending_requests;
    const double minutes = dynamics.season_minutes;
    minutes_total += minutes;
    minutes_new +=
        minutes * static_cast<double>(Interactions::newness(joined, today));
    by_nation[player.getNationality()].push_back(player.getId());
  }
  room.team_morale = morale_total / static_cast<float>(members.size());

  if (minutes_total <= 0.0)
  {
    // No minutes yet this season: the selected XI stands in.
    const Lineup& lineup = team->get().getLineup();
    std::vector<const Player*> starters;
    starters.push_back(lineup.getGoalkeeper());
    for (const auto& positioned : lineup.getOutfieldPlayers())
      starters.push_back(positioned.player);
    for (const Player* starter : starters)
    {
      if (starter == nullptr) continue;
      const PlayerRelation* rel = relation(starter->getId());
      minutes_total += 1.0;
      minutes_new += static_cast<double>(
          Interactions::newness(rel != nullptr ? rel->joined_day : 0, today));
    }
  }
  room.new_share = minutes_total > 0.0
                       ? static_cast<float>(minutes_new / minutes_total)
                       : 0.0f;
  room.cohesion = 1.0f - room.new_share;
  room.cohesion_modifier = Interactions::cohesionModifier(room.new_share);

  std::ranges::sort(ranked,
                    [](const LeaderInfo& a, const LeaderInfo& b)
                    {
                      return a.score > b.score ||
                             (a.score == b.score && a.player_id < b.player_id);
                    });
  ranked.resize(std::min(LEADER_COUNT, ranked.size()));
  if (!ranked.empty()) ranked.front().captain = true;
  room.leaders = std::move(ranked);

  for (auto& [nation, group] : by_nation)
  {
    if (group.size() < GROUP_MIN_SIZE) continue;
    std::ranges::sort(group);
    room.groups.emplace_back(nation, std::move(group));
  }
  std::ranges::sort(room.groups,
                    [](const SocialGroup& a, const SocialGroup& b)
                    {
                      return a.members.size() > b.members.size() ||
                             (a.members.size() == b.members.size() &&
                              a.nationality < b.nationality);
                    });
  if (room.groups.size() > MAX_GROUPS) room.groups.resize(MAX_GROUPS);

  for (const Promise& promise : state.promises)
  {
    if (promise.state != PromiseState::Active) continue;
    const auto player = gamedata->getPlayer(promise.player_id);
    if (player && player->get().getTeamId() == team_id) ++room.active_promises;
  }
  if (room.team_morale >= 70.0f)
    room.mood = DressingMood::Buoyant;
  else if (room.team_morale >= 55.0f)
    room.mood = DressingMood::Settled;
  else if (room.team_morale >= 42.0f)
    room.mood = DressingMood::Uneasy;
  else
    room.mood = DressingMood::Tense;
  return room;
}

float InteractionSystem::cohesionModifier(TeamID team_id,
                                          const GameDateValue& date) const
{
  return dressingRoom(team_id, date).cohesion_modifier;
}

// ---------------------------------------------------------------------------
// Persistence (tables PlayerRelations, Promises, InteractionState)
// ---------------------------------------------------------------------------

void InteractionSystem::restore(InteractionState restored)
{
  state = std::move(restored);
  team_talks.clear();
}

void InteractionSystem::load(const std::shared_ptr<DatabaseConnection>& db_conn)
{
  InteractionState loaded;
  forEachRow(*db_conn,
             "SELECT next_promise_id, last_evaluated_day, last_request_day "
             "FROM InteractionState WHERE id = 1;",
             [&loaded](sqlite3_stmt* stmt)
             {
               loaded.next_promise_id = columnAs<std::uint32_t>(stmt, 0);
               loaded.last_evaluated_day = columnAs<std::int32_t>(stmt, 1);
               loaded.last_request_day = columnAs<std::int32_t>(stmt, 2);
             });
  forEachRow(
      *db_conn,
      "SELECT player_id, trust, talk_form, talk_playing_time, talk_promise, "
      "talk_patience, talk_transfer, request, request_day, quiet_until, "
      "escalations, low_morale_weeks, joined_day, broken_promise_day "
      "FROM PlayerRelations;",
      [&loaded](sqlite3_stmt* stmt)
      {
        const PlayerID player_id = columnAs<PlayerID>(stmt, 0);
        loaded.relations.try_emplace(player_id, readPlayerRelation(stmt));
      });
  forEachRow(*db_conn,
             "SELECT id, player_id, type, state, void_reason, made_day, "
             "deadline_day, resolved_day, target, baseline, team_minutes, "
             "player_minutes, matches, fulfilled FROM Promises ORDER BY id;",
             [&loaded](sqlite3_stmt* stmt)
             {
               auto promise = readPromise(stmt);
               if (!promise)
                 return;  // Written by a newer version: skip rather than guess.
               loaded.next_promise_id =
                   std::max(loaded.next_promise_id, promise->id + 1);
               loaded.promises.push_back(std::move(*promise));
             });
  restore(std::move(loaded));
}

void InteractionSystem::save(
    const std::shared_ptr<DatabaseConnection>& db_conn) const
{
  for (const char* table : {"InteractionState", "PlayerRelations", "Promises"})
  {
    sqlite3_exec(db_conn->getRaw(),
                 (std::string("DELETE FROM ") + table + ";").c_str(), nullptr,
                 nullptr, nullptr);
  }
  sqlite3_stmt* stmt = db_conn->prepareStatement(
      "INSERT INTO InteractionState (id, next_promise_id, last_evaluated_day, "
      "last_request_day) VALUES (1, ?, ?, ?);");
  sqlite3_bind_int64(stmt, 1, state.next_promise_id);
  sqlite3_bind_int(stmt, 2, state.last_evaluated_day);
  sqlite3_bind_int(stmt, 3, state.last_request_day);
  db_conn->executeStep(stmt);
  sqlite3_finalize(stmt);

  insertAll(*db_conn,
            "INSERT INTO PlayerRelations (player_id, trust, talk_form, "
            "talk_playing_time, talk_promise, talk_patience, talk_transfer, "
            "request, request_day, quiet_until, escalations, low_morale_weeks, "
            "joined_day, broken_promise_day) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            state.relations,
            [](sqlite3_stmt* row, const auto& entry)
            {
              const auto& [player_id, rel] = entry;
              sqlite3_bind_int(row, 1, static_cast<int>(player_id));
              sqlite3_bind_double(row, 2, rel.trust);
              for (std::size_t group = 0; group < rel.last_talk.size(); ++group)
                sqlite3_bind_int(row, 3 + static_cast<int>(group),
                                 rel.last_talk[group]);
              sqlite3_bind_int(row, 8, static_cast<int>(rel.request));
              sqlite3_bind_int(row, 9, rel.request_day);
              sqlite3_bind_int(row, 10, rel.quiet_until);
              sqlite3_bind_int(row, 11, rel.escalations);
              sqlite3_bind_int(row, 12, rel.low_morale_weeks);
              sqlite3_bind_int(row, 13, rel.joined_day);
              sqlite3_bind_int(row, 14, rel.broken_promise_day);
            });
  insertAll(*db_conn,
            "INSERT INTO Promises (id, player_id, type, state, void_reason, "
            "made_day, deadline_day, resolved_day, target, baseline, "
            "team_minutes, player_minutes, matches, fulfilled) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);",
            state.promises,
            [](sqlite3_stmt* row, const Promise& promise)
            {
              sqlite3_bind_int64(row, 1, promise.id);
              sqlite3_bind_int(row, 2, static_cast<int>(promise.player_id));
              sqlite3_bind_int(row, 3, std::to_underlying(promise.type));
              sqlite3_bind_int(row, 4, std::to_underlying(promise.state));
              sqlite3_bind_int(row, 5, std::to_underlying(promise.void_reason));
              sqlite3_bind_int(row, 6, promise.made_day);
              sqlite3_bind_int(row, 7, promise.deadline_day);
              sqlite3_bind_int(row, 8, promise.resolved_day);
              sqlite3_bind_double(row, 9, promise.target);
              sqlite3_bind_double(row, 10, promise.baseline);
              sqlite3_bind_int64(row, 11, promise.team_minutes);
              sqlite3_bind_int64(row, 12, promise.player_minutes);
              sqlite3_bind_int(row, 13, promise.matches);
              sqlite3_bind_int(row, 14, promise.fulfilled ? 1 : 0);
            });
}

// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "global/types.h"
#include "model/gamedate.h"
#include "model/player.h"

class DatabaseConnection;
class GameData;
class Inbox;
struct MatchReport;
enum class SquadRole : std::uint8_t;

/**
 * @brief The human side of management: one-to-one conversations, promises,
 * team talks and the dressing room of the managed club.
 *
 * Design rules (research D-14, FR-119..FR-123):
 * - Interactions change morale points and the player's trust in the manager;
 *   they never touch results directly. The only performance knobs exposed
 *   (team talk and cohesion modifiers) are bounded to +-2% and are read-only
 *   hints for the match engine.
 * - Conversations are rare and consequential: every option has a cooldown
 *   and the player's reply depends on his personality, form against what his
 *   squad role demands, trust and the manager's standing.
 * - Every promise has a measurable condition and a deadline and always ends
 *   Kept, Broken or Voided (with a reason).
 */

/** A manager's line in a one-to-one conversation (values are persisted). */
enum class TalkOption : std::uint8_t
{
  PraiseForm = 0,
  CriticiseForm,
  DiscussPlayingTime,
  PromisePlayingTime,
  PromiseContract,
  PromiseSigning,
  AskPatience,
  ReassureStay,          /*!< Transfer interest: "you are not for sale". */
  OpenToOffers,          /*!< Transfer interest: "we would listen". */
  AcceptTransferRequest, /*!< Lists the player. */
  RefuseTransferRequest,
  COUNT
};

/** How the player took it. */
enum class TalkReaction : std::uint8_t
{
  Positive,
  Neutral,
  Negative
};

/** Predicted reaction shown next to an option. */
enum class TalkHint : std::uint8_t
{
  LikelyPositive,
  Uncertain,
  Risky
};

/** Something an unhappy player asked the manager for (persisted). */
enum class TalkRequest : std::uint8_t
{
  None = 0,
  PlayingTime,
  NewContract,
  Transfer
};

/** Why an option cannot be used right now. */
enum class TalkBlock : std::uint8_t
{
  None,
  Cooldown,
  NotAtClub,
  NoRecentMatches,
  Injured,
  PromiseActive,
  PromiseNotCredible,
  ContractLong,
  NoWindowAhead,
  NoTransferInterest,
  NoTransferRequest,
  NotUnhappy
};

enum class PromiseType : std::uint8_t
{
  PlayingTime = 0, /*!< Share of the club's competitive minutes. */
  NewContract,     /*!< Contract extended before the deadline. */
  Signing          /*!< A first-team level signing by the window's end. */
};

enum class PromiseState : std::uint8_t
{
  Active = 0,
  Kept,
  Broken,
  Voided
};

enum class PromiseVoidReason : std::uint8_t
{
  None = 0,
  PlayerLeft,
  LongInjury,
  TooFewMatches
};

/**
 * @struct Promise
 * @brief A measurable commitment with a deadline.
 *
 * PlayingTime: player_minutes / team_minutes >= target over the window
 * (minutes the player was injured do not count against it).
 * NewContract: contract years above baseline. Signing: a signing whose
 * overall is at least target (the club's 11th best overall when promised).
 */
struct Promise
{
  std::uint32_t id = 0;
  PlayerID player_id = 0;
  PromiseType type = PromiseType::PlayingTime;
  PromiseState state = PromiseState::Active;
  PromiseVoidReason void_reason = PromiseVoidReason::None;
  std::int32_t made_day = 0;
  std::int32_t deadline_day = 0;
  std::int32_t resolved_day = 0;
  float target = 0.0f;
  float baseline = 0.0f;
  std::uint32_t team_minutes = 0;
  std::uint32_t player_minutes = 0;
  std::uint16_t matches = 0;
  bool fulfilled = false; /*!< Signing / contract condition met. */

  /** Progress towards the condition in [0, 1]. */
  float progress() const;
};

/** Cooldown groups of the conversation options. */
enum class TalkGroup : std::uint8_t
{
  Form = 0,
  PlayingTime,
  Promise,
  Patience,
  Transfer,
  COUNT
};

/**
 * @struct PlayerRelation
 * @brief A managed player's relationship with the manager (persisted).
 */
struct PlayerRelation
{
  float trust = 0.0f; /*!< -100 (no faith) to 100 (full backing). */
  std::array<std::int32_t,
             static_cast<std::size_t>(std::to_underlying(TalkGroup::COUNT))>
      last_talk{}; /*!< Day ordinals, 0 = never. */
  TalkRequest request = TalkRequest::None;
  std::int32_t request_day = 0;
  std::int32_t quiet_until = 0; /*!< No new request before this day. */
  std::uint8_t escalations = 0; /*!< Times an ignored request escalated. */
  std::uint8_t low_morale_weeks = 0;
  std::int32_t joined_day = 0; /*!< Arrival at the club, 0 = unknown. */
  std::int32_t broken_promise_day = 0;
};

/**
 * @struct TalkContext
 * @brief Everything the reply to a conversation depends on.
 */
struct TalkContext
{
  PlayerTraits traits;
  int age = 25;
  SquadRole role{};
  float form = 0.0f; /*!< Mean recent rating, 0 when none. */
  std::uint8_t rated_matches = 0;
  float expected_rating = 6.7f; /*!< What the squad role demands. */
  float playing_share = 0.0f;
  float expected_share = 0.5f;
  float morale = 60.0f;
  float trust = 0.0f;
  float manager_standing = 50.0f; /*!< 0-100 (board confidence proxy). */
  bool injured = false;
  bool transfer_interest = false;
  std::uint8_t contract_years = 2;
  TalkRequest request = TalkRequest::None;
  bool promise_credible = true;
};

/** @brief Result of one conversation line. */
struct TalkOutcome
{
  TalkOption option = TalkOption::PraiseForm;
  TalkReaction reaction = TalkReaction::Neutral;
  float morale_delta = 0.0f;
  float trust_delta = 0.0f;
  std::string reply_key; /*!< Localised quote of the player. */
  std::optional<PromiseType> promise;
  bool request_resolved = false;
  bool transfer_request = false; /*!< Player now asks to leave. */
  bool list_player = false;      /*!< Manager agreed to sell. */
  bool settled = false;          /*!< Transfer interest calmed down. */
  std::int32_t quiet_days = 0;   /*!< Patience bought. */
};

/** @brief One option as offered to the manager. */
struct TalkOptionView
{
  TalkOption option = TalkOption::PraiseForm;
  TalkBlock block = TalkBlock::None;
  std::int32_t cooldown_days = 0; /*!< Days until usable (Cooldown). */
  TalkHint hint = TalkHint::Uncertain;
  float positive_chance = 0.0f;
  bool suggested = false; /*!< Answers the player's request / situation. */
};

// ---------------------------------------------------------------- Team talks

enum class TeamTalkMoment : std::uint8_t
{
  PreMatch = 0,
  HalfTime
};

enum class TeamTalkTone : std::uint8_t
{
  Calm = 0,
  Motivate,
  DemandMore,
  Praise,
  NoPressure,
  COUNT
};

/** A player listening to a team talk. */
struct TalkListener
{
  PlayerID player_id = 0;
  PlayerTraits traits;
  float morale = 60.0f;
};

/** @brief Situation of a team talk. */
struct TeamTalkContext
{
  TeamTalkMoment moment = TeamTalkMoment::PreMatch;
  int goal_difference = 0;      /*!< Own minus opponent (half-time). */
  float expected_points = 1.4f; /*!< Own expectation before kick-off. */
  bool derby = false;
  bool final_match = false;
  bool cup = false;
  std::int32_t day = 0;
  std::vector<TalkListener> listeners;
};

/** @brief Predicted reception of a tone. */
struct TeamTalkPrediction
{
  TeamTalkTone tone = TeamTalkTone::Calm;
  float positive_share = 0.0f;
  float negative_share = 0.0f;
  TalkHint hint = TalkHint::Uncertain;
};

/** @brief What a team talk did. */
struct TeamTalkResult
{
  TeamTalkTone tone = TeamTalkTone::Calm;
  std::uint8_t positive = 0;
  std::uint8_t neutral = 0;
  std::uint8_t negative = 0;
  float morale_delta = 0.0f; /*!< Mean morale change of the listeners. */
  float modifier = 0.0f;     /*!< Execution-quality hint, +-TALK_CAP. */
  std::string summary_key;
  std::vector<std::pair<PlayerID, TalkReaction>> reactions;
  std::vector<std::pair<PlayerID, float>> morale_changes;
};

// ----------------------------------------------------------- Dressing room

/** Overall mood of the dressing room. */
enum class DressingMood : std::uint8_t
{
  Buoyant,
  Settled,
  Uneasy,
  Tense
};

struct LeaderInfo
{
  PlayerID player_id = 0;
  float score = 0.0f; /*!< Hierarchy score 0-100. */
  float morale = 0.0f;
  bool captain = false;
};

/** Players sharing a nationality (the dressing room's natural cliques). */
struct SocialGroup
{
  Language nationality = Language::EN;
  std::vector<PlayerID> members;
};

/**
 * @struct DressingRoom
 * @brief Summary of the managed squad's social state.
 */
struct DressingRoom
{
  std::vector<LeaderInfo> leaders; /*!< Best first; the first is captain. */
  std::vector<SocialGroup> groups;
  float team_morale = 0.0f;
  float cohesion = 1.0f;          /*!< 1 - weighted new-signing share. */
  float new_share = 0.0f;         /*!< Minutes share of settling newcomers. */
  float cohesion_modifier = 0.0f; /*!< 0 to -2% execution quality. */
  DressingMood mood = DressingMood::Settled;
  std::uint8_t unhappy = 0;
  std::uint8_t pending_requests = 0;
  std::uint8_t active_promises = 0;
};

/** @brief Persisted state of the interaction system. */
struct InteractionState
{
  std::uint32_t next_promise_id = 1;
  std::int32_t last_evaluated_day = 0;
  std::int32_t last_request_day = 0; /*!< Last new request squad-wide. */
  std::unordered_map<PlayerID, PlayerRelation> relations;
  std::vector<Promise> promises;
};

namespace Interactions
{
/** Execution-quality cap of one team talk (+-1.2%). */
constexpr float TALK_CAP = 0.012f;
/** Execution-quality cap of team talks and cohesion combined (+-2%). */
constexpr float TOTAL_CAP = 0.02f;

/** Language key of an option's label, e.g. "TALK_OPTION_PRAISE_FORM". */
const char* optionKey(TalkOption option);
/** Language key of an option's one-line description. */
const char* optionDescriptionKey(TalkOption option);
const char* reactionKey(TalkReaction reaction);
const char* hintKey(TalkHint hint);
const char* blockKey(TalkBlock block);
const char* requestKey(TalkRequest request);
const char* promiseTypeKey(PromiseType type);
const char* promiseStateKey(PromiseState state);
const char* voidReasonKey(PromiseVoidReason reason);
const char* toneKey(TeamTalkTone tone);
const char* toneDescriptionKey(TeamTalkTone tone);
const char* moodKey(DressingMood mood);
TalkGroup groupOf(TalkOption option);
/** Days an option's group stays closed after use. */
std::int32_t cooldownDays(TalkGroup group);

/** Rating a player of @p role is expected to average. */
float expectedRating(SquadRole role);
/** Share of minutes a player of @p role (and age) expects. */
float expectedShare(SquadRole role, int age);

/** Chance in [0, 1] that the player takes @p option well. */
float positiveChance(const TalkContext& context, TalkOption option);
/** Chance in [0, 1] that the player takes @p option badly. */
float negativeChance(const TalkContext& context, TalkOption option);
TalkHint hintFor(float positive, float negative);

/**
 * Deterministic reply: @p roll (uniform in [0, 1)) decides the reaction,
 * @p variant_roll the reply wording. Morale deltas are scaled by the
 * player's temperament.
 */
TalkOutcome evaluateTalk(const TalkContext& context, TalkOption option,
                         double roll, double variant_roll);

/** Predicted reception of every tone in @p context. */
std::vector<TeamTalkPrediction> predictTeamTalk(const TeamTalkContext& context);

/**
 * Applies nothing: computes each listener's reaction (rolls keyed by
 * @p seed, day and player) and the bounded modifier.
 */
TeamTalkResult evaluateTeamTalk(const TeamTalkContext& context,
                                TeamTalkTone tone, std::uint64_t seed);

/**
 * Execution-quality change from squad turnover: 0 up to a 25% share of
 * minutes by newcomers, falling linearly to -2% at 40% and beyond.
 */
float cohesionModifier(float new_share);

/**
 * Newness weight of a player who joined on @p joined_day: 1 on arrival,
 * 0 after SETTLE_DAYS (and for players whose arrival predates the records).
 */
float newness(std::int32_t joined_day, std::int32_t today);
constexpr std::int32_t SETTLE_DAYS = 240;

/**
 * Hierarchy score 0-100 from tenure, age, standing in the squad
 * (overall rank as a 0-1 fraction, 1 = best) and personality.
 */
float hierarchyScore(int tenure_days, int age, float standing,
                     const PlayerTraits& traits);
}  // namespace Interactions

/**
 * @class InteractionSystem
 * @brief Relations, promises, requests and team talks of the managed club.
 *
 * Owned by WorldSimulation; all randomness is keyed by the world seed, the
 * day and the player so that a reloaded save continues identically.
 */
class InteractionSystem
{
 public:
  /** @p role_of gives a player's playing-time expectation in his squad. */
  InteractionSystem(std::shared_ptr<GameData> gamedata,
                    std::function<SquadRole(PlayerID)> role_of);

  // ---- Hooks (called by WorldSimulation) ----

  /** Daily promise evaluation (exactly once per day) and weekly requests. */
  void onDayAdvanced(const GameDateValue& date, TeamID managed_team_id,
                     Inbox& inbox);
  /** Weekly: trust drift, leader influence, request lifecycle. */
  void onWeek(const GameDateValue& date, TeamID managed_team_id, Inbox& inbox);
  /** Playing-time promise progress; clears the match's team talks. */
  void onMatchPlayed(const MatchReport& report, TeamID managed_team_id);
  /** Arrival dates, voided promises, signing promises. */
  void onTransferCompleted(const GameDateValue& date, PlayerID player_id,
                           TeamID from_team_id, TeamID to_team_id,
                           TeamID managed_team_id, Inbox& inbox);
  /**
   * The club turned down a bid the player wanted to hear more about: his
   * morale falls by @p morale_delta scaled by his temperament, trust by
   * @p trust_delta, and with @p transfer_request he asks to leave. Posts
   * his reaction to the inbox. Returns the morale change applied.
   */
  float onBidRejected(const GameDateValue& date, PlayerID player_id,
                      const std::string& buyer_name, float morale_delta,
                      float trust_delta, bool transfer_request, Inbox& inbox);
  /** Moves a player's trust by @p delta (decision moments); returns the
   * change applied after clamping. */
  float adjustTrust(PlayerID player_id, float delta);
  /** Weekly morale target offset of a player (trust and open requests). */
  float moraleTargetOffset(PlayerID player_id) const;

  // ---- Conversations ----

  /** Options for a managed player with availability and hints. */
  std::vector<TalkOptionView> options(PlayerID player_id,
                                      const GameDateValue& date,
                                      TeamID managed_team_id,
                                      float manager_standing) const;
  /**
   * Holds the conversation line and applies morale, trust, promises and
   * request changes. Returns nullopt when the option is not available.
   * Listing the player (list_player) is left to the caller.
   */
  std::optional<TalkOutcome> talk(PlayerID player_id, TalkOption option,
                                  const GameDateValue& date,
                                  TeamID managed_team_id,
                                  float manager_standing);

  // ---- Team talks ----

  /** Listeners of the managed club's talk: its selected XI. */
  TeamTalkContext teamTalkContext(TeamID team_id, TeamTalkMoment moment,
                                  const GameDateValue& date) const;
  /** True when this moment's talk has not been given for this match. */
  bool canGiveTeamTalk(TeamID team_id, TeamTalkMoment moment,
                       const GameDateValue& date) const;
  /** Gives the talk once per moment and match; applies morale changes. */
  std::optional<TeamTalkResult> giveTeamTalk(const TeamTalkContext& context,
                                             TeamID team_id, TeamTalkTone tone);
  /**
   * Execution-quality modifier of the team's talks for @p half (1 or 2) of
   * its match on @p date: the pre-match talk counts fully in the first half
   * and half in the second, where the half-time talk adds; the total is
   * capped at +-TOTAL_CAP. 0 on any other day.
   */
  float teamTalkModifier(TeamID team_id, const GameDateValue& date,
                         int half) const;

  // ---- Dressing room ----

  DressingRoom dressingRoom(TeamID team_id, const GameDateValue& date) const;
  /** Cohesion modifier of a club (0 when unknown / not managed). */
  float cohesionModifier(TeamID team_id, const GameDateValue& date) const;

  // ---- Queries ----

  const PlayerRelation* relation(PlayerID player_id) const;
  /** Promises made to a player (all states), newest first. */
  std::vector<Promise> promisesFor(PlayerID player_id) const;
  const std::vector<Promise>& promises() const { return state.promises; }
  /** Managed players with an unanswered request. */
  std::vector<PlayerID> pendingRequests() const;

  // ---- Persistence ----

  const InteractionState& getState() const { return state; }
  void restore(InteractionState restored);
  void load(const std::shared_ptr<DatabaseConnection>& db_conn);
  void save(const std::shared_ptr<DatabaseConnection>& db_conn) const;

  /** Deadline of a signing promise made on @p date by a club of @p league
   * (end of its next window within 120 days), nullopt if no window is that
   * close. */
  static std::optional<GameDateValue> signingDeadline(const GameDateValue& date,
                                                      LeagueID league);

 private:
  struct TeamTalkRecord
  {
    std::int32_t day = 0;
    bool pre_done = false;
    bool half_done = false;
    float pre_modifier = 0.0f;
    float half_modifier = 0.0f;
  };

  TalkContext contextFor(const Player& player, const GameDateValue& date,
                         float manager_standing) const;
  TalkBlock blockFor(const Player& player, TalkOption option,
                     const TalkContext& context, const GameDateValue& date,
                     TeamID managed_team_id, std::int32_t& cooldown) const;
  TalkBlock optionBlockFor(const Player& player, TalkOption option,
                           const TalkContext& context,
                           const GameDateValue& date,
                           TeamID managed_team_id) const;
  bool hasActivePromise(PlayerID player_id, PromiseType type) const;
  void evaluatePromises(const GameDateValue& date, TeamID managed_team_id,
                        Inbox& inbox);
  void evaluatePromise(Promise& promise, const Player& player, bool due,
                       const GameDateValue& date, std::int32_t today,
                       Inbox& inbox);
  void evaluatePlayingTimePromise(Promise& promise, const Player& player,
                                  bool due, const GameDateValue& date,
                                  std::int32_t today, Inbox& inbox);
  void evaluateContractPromise(Promise& promise, const Player& player, bool due,
                               const GameDateValue& date, Inbox& inbox);
  void evaluateSigningPromise(Promise& promise, bool due,
                              const GameDateValue& date, Inbox& inbox);
  void resolvePromise(Promise& promise, PromiseState outcome,
                      PromiseVoidReason reason, const GameDateValue& date,
                      Inbox& inbox);
  void raiseRequests(const GameDateValue& date, TeamID managed_team_id,
                     Inbox& inbox);
  bool resolveIgnoredRequest(Player& player, PlayerDynamics& dynamics,
                             PlayerRelation& relation,
                             const GameDateValue& date, std::int32_t today,
                             Inbox& inbox);
  TalkRequest requestForPlayer(const Player& player,
                               const PlayerDynamics& dynamics,
                               const PlayerRelation& relation,
                               const GameDateValue& date,
                               std::int32_t today) const;
  void spreadLeaderMood(TeamID managed_team_id, const GameDateValue& date);
  PlayerRelation& relationFor(PlayerID player_id);
  float firstTeamThreshold(TeamID team_id) const;

  std::shared_ptr<GameData> gamedata;
  std::function<SquadRole(PlayerID)> role_of;
  InteractionState state;
  std::unordered_map<TeamID, TeamTalkRecord> team_talks; /*!< Transient. */
};

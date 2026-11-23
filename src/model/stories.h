// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "global/types.h"
#include "model/finances.h"
#include "model/gamedate.h"

class AwardSystem;
struct ClubProfile;
class DatabaseConnection;
class GameData;
class Inbox;
struct InboxMessage;
class InteractionSystem;
class Player;
class WorldRng;
struct MatchReport;
struct PlayerMatchLine;

/**
 * @brief Story chains that grow out of what actually happens in the save.
 *
 * Each story is triggered by a real event of the managed club (a debut, a
 * string of high ratings, a long injury ending, repeated bids for a key
 * player...) and produces at most one inbox message per instance. Stories
 * that call for a decision carry a StoryChoice: the inbox offers to talk to
 * the player with the conversation options that answer it. A weekly cap
 * keeps busy periods from flooding the inbox.
 *
 * The kinds after Rivalry are decision moments: short two-option dilemmas
 * (see Dilemma) answered from the inbox. Values are persisted: append only.
 */
enum class StoryKind : std::uint8_t
{
  Debut = 0,
  Breakout,
  CaptainDispute,
  PoorRun,
  TransferSaga,
  Comeback,
  Milestone,
  Rivalry,
  CompassionateLeave, /*!< A player asks to be with his family. */
  HomesickYouth,      /*!< A young player misses home. */
  FineDispute,        /*!< The captain disputes a club fine. */
  RivalComments,      /*!< The press asks about the rival's remarks. */
  SponsorAppearance,  /*!< A sponsor wants the squad before a match. */
  TrainingClash,      /*!< Two players clash in training. */
  CoachingCourse,     /*!< A veteran wants to start his coaching badges. */
  TicketProtest       /*!< Supporters protest about ticket prices. */
};

/** Competitive career totals of a player (all clubs). */
struct CareerTotals
{
  std::uint32_t appearances = 0;
  std::uint32_t goals = 0;
};

/** A story waiting for the manager's answer. */
struct StoryChoice
{
  StoryKind kind = StoryKind::TransferSaga;
  PlayerID player_id = 0;
  std::int32_t day = 0;
  std::int32_t expires_day = 0;
};

/** Dedupe record: a story fired for (kind, entity, key) on day. */
struct StoryRecord
{
  StoryKind kind = StoryKind::Debut;
  std::uint32_t entity = 0;
  std::uint32_t key = 0;
  std::int32_t day = 0;
  bool posted = false; /*!< False when the weekly cap held it back. */
};

/**
 * @brief Bounded effects of one answer to a decision moment.
 *
 * Shown before the manager chooses and applied once: morale and match
 * fitness are 0-100 points, trust is the -100..100 relation scale, money
 * changes the club balance through the ledger (positive is income).
 */
struct DilemmaEffects
{
  float morale = 0.0f;       /*!< The player at the centre of it. */
  float trust = 0.0f;        /*!< His trust in the manager. */
  float other_morale = 0.0f; /*!< The second player (training clash). */
  float other_trust = 0.0f;
  float squad_morale = 0.0f; /*!< Everyone else in the senior squad. */
  float sharpness = 0.0f;    /*!< The player's match fitness. */
  std::int64_t money = 0;
  FinanceCategory category = FinanceCategory::Adjustment;
};

/** @brief A decision moment of the managed club (table StoryDilemmas). */
struct Dilemma
{
  StoryKind kind = StoryKind::CompassionateLeave;
  PlayerID subject = 0;    /*!< 0 for club-wide moments. */
  std::uint32_t other = 0; /*!< Second player, or the rival club. */
  std::int32_t day = 0;    /*!< Raised on this day (unique). */
  std::int32_t expires_day = 0;
  std::int8_t chosen = -1; /*!< -1 open, 0 / 1 answered, 2 lapsed. */
  std::int32_t resolved_day = 0;
  std::int64_t money = 0; /*!< Balance change applied by the answer. */

  bool open() const { return chosen < 0; }
};

/** @brief A followed player and what was last reported about him. */
struct FollowedPlayer
{
  PlayerID player_id = 0;
  std::int32_t since_day = 0;
  TeamID team_id = 0;
  bool injured = false;
  std::uint8_t contract_years = 0;
  std::uint32_t wage = 0;
  std::uint16_t honours = 0;
  std::int32_t last_match_day = 0; /*!< Last big-match note. */
};

/** @brief What the day loop knows beyond the squad. */
struct StoryDayContext
{
  /** Days to the managed club's next fixture: 0 today, -1 none soon. */
  int days_to_match = -1;
  const AwardSystem* awards = nullptr;
};

/** @brief Persisted state of the story engine. */
struct StoryState
{
  std::vector<StoryRecord> records;
  std::vector<StoryChoice> choices;
  /** Decision moments, oldest first (open, answered and lapsed). */
  std::vector<Dilemma> dilemmas;
  std::vector<FollowedPlayer> follows;
  /** Managed players currently injured: first day out. */
  std::unordered_map<PlayerID, std::int32_t> injury_start;
  /** Returned from a long injury, story due at the next appearance: days
   * out. */
  std::unordered_map<PlayerID, std::int32_t> comeback_due;
  /** Recent bids for managed players: (player, day). */
  std::vector<std::pair<PlayerID, std::int32_t>> bids;
};

namespace Stories
{
/** Stories posted per rolling week at most. */
constexpr int WEEKLY_CAP = 2;
/** Injuries at least this long make a comeback story. */
constexpr std::int32_t LONG_INJURY_DAYS = 60;
/** Mean recent rating of a young player that makes a breakout. */
constexpr float BREAKOUT_RATING = 7.3f;
constexpr int BREAKOUT_MAX_AGE = 21;
/** Senior debuts are told for players up to this age. */
constexpr int DEBUT_MAX_AGE = 19;
/** Competitive matches without a win that make a poor run. */
constexpr int POOR_RUN_MATCHES = 5;
/** Bids within this many days that make a transfer saga. */
constexpr std::int32_t SAGA_WINDOW_DAYS = 30;
constexpr std::int32_t CHOICE_DAYS = 21;

/** Appearance milestones crossed from @p before to @p after (e.g. 100). */
std::optional<std::uint32_t> appearanceMilestone(std::uint32_t before,
                                                 std::uint32_t after);
/** Goal milestones crossed from @p before to @p after (e.g. 50). */
std::optional<std::uint32_t> goalMilestone(std::uint32_t before,
                                           std::uint32_t after);
/** True when the latest results (newest first, 'W'/'D'/'L') hold a poor
 * run: POOR_RUN_MATCHES or more without a win. */
bool isPoorRun(std::string_view recent_form);
/** Language key of a story kind. */
const char* kindKey(StoryKind kind);

// ---- Decision moments ----

/** At most one decision moment in this many days. */
constexpr std::int32_t DILEMMA_GAP_DAYS = 14;
/** The same kind of moment does not come back for this many days. */
constexpr std::int32_t DILEMMA_KIND_COOLDOWN_DAYS = 180;
/** Days to answer before the moment lapses (without effects). */
constexpr std::int32_t DILEMMA_ANSWER_DAYS = 7;
/** Chance per eligible day once the gap has passed. */
constexpr double DILEMMA_DAILY_CHANCE = 0.06;
/** Effect bounds (absolute values). */
constexpr float DILEMMA_MAX_MORALE = 6.0f;
constexpr float DILEMMA_MAX_TRUST = 6.0f;
constexpr float DILEMMA_MAX_SQUAD_MORALE = 2.0f;
constexpr float DILEMMA_MAX_SHARPNESS = 10.0f;
constexpr std::int64_t DILEMMA_MAX_MONEY = 250'000;
constexpr StoryKind FIRST_DILEMMA = StoryKind::CompassionateLeave;
constexpr StoryKind LAST_DILEMMA = StoryKind::TicketProtest;

/** True for the two-option decision moments. */
bool isDilemma(StoryKind kind);
/** Language keys of a moment's message (title and body take the player,
 * the second player or rival club, and the money at stake). */
const char* dilemmaTitleKey(StoryKind kind);
const char* dilemmaBodyKey(StoryKind kind);
/** Label of answer @p option (0 or 1). */
const char* dilemmaOptionKey(StoryKind kind, int option);
/** Body of the note filed once answer @p option is taken. */
const char* dilemmaDoneKey(StoryKind kind, int option);
/** The moment a title key belongs to, if any. */
std::optional<StoryKind> dilemmaForTitle(std::string_view title_key);
/** Effects of answer @p option (0 or 1) for a club of @p profile. */
DilemmaEffects dilemmaEffects(StoryKind kind, int option,
                              const ClubProfile& profile);

// ---- Followed players ----

/** Players the manager can follow at once. */
constexpr std::size_t MAX_FOLLOWS = 25;
/** A followed player's match makes news from this rating... */
constexpr float BIG_MATCH_RATING = 8.5f;
/** ...or this many goals. */
constexpr int BIG_MATCH_GOALS = 2;
/** At most one big-match note per player in this many days. */
constexpr std::int32_t BIG_MATCH_GAP_DAYS = 7;
}  // namespace Stories

/**
 * @class StoryEngine
 * @brief Detects story triggers for the managed club and posts them.
 */
class StoryEngine
{
 public:
  explicit StoryEngine(std::shared_ptr<GameData> gamedata);

  /**
   * Competitive career totals before the match being processed (the
   * competitions record a result after the world simulation). Queried once
   * per player and session, then kept up to date from match reports.
   */
  void setCareerProvider(std::function<CareerTotals(PlayerID)> provider);

  /**
   * Daily: long-injury tracking, captain disputes (weekly), expiries,
   * followed players' news and decision moments (never on match day).
   */
  void onDayAdvanced(const GameDateValue& date, TeamID managed_team_id,
                     const InteractionSystem& interactions, Inbox& inbox,
                     const StoryDayContext& context = {});
  /** Debut, breakout, milestones, comeback, poor run and rivalry of the
   * managed club; big matches of followed players anywhere. */
  void onMatchPlayed(const MatchReport& report, TeamID managed_team_id,
                     const std::string& recent_form, Inbox& inbox);
  /** Transfer saga: a second bid within a month for a key player. */
  void onTransferBid(const GameDateValue& date, PlayerID player_id,
                     TeamID bidder_id, bool key_player, TeamID managed_team_id,
                     Inbox& inbox);
  /** A player left: his open choices lapse. */
  void onPlayerLeft(PlayerID player_id);
  /** Rivals are recomputed at the start of each season. */
  void onSeasonStart();

  /** Same-league club of the nearest reputation (lower id on ties). */
  std::optional<TeamID> rivalOf(TeamID team_id) const;

  /** Open decision for a player, if any. */
  std::optional<StoryChoice> choiceFor(PlayerID player_id,
                                       std::int32_t today) const;
  /** The manager answered (or dismissed) the player's story. */
  void resolveChoice(PlayerID player_id);

  // ---- Decision moments ----

  /** The open decision moment, if any. */
  const Dilemma* openDilemma() const;
  /** The moment raised on @p day (open or not). */
  const Dilemma* dilemmaOn(std::int32_t day) const;
  /** Effects of @p option for the open moment of @p managed_team_id. */
  std::optional<DilemmaEffects> previewDilemma(int option,
                                               TeamID managed_team_id) const;
  /**
   * Answers the open moment with @p option (0 or 1): applies its effects to
   * the players, their trust (through @p interactions) and the club's
   * ledger, records the answer and files a note. False when nothing is
   * open or the option is invalid.
   */
  bool resolveDilemma(int option, const GameDateValue& date,
                      TeamID managed_team_id, InteractionSystem& interactions,
                      Inbox& inbox);

  // ---- Followed players ----

  /** Starts following @p player_id (false if unknown, followed or full). */
  bool follow(PlayerID player_id, const GameDateValue& date);
  bool unfollow(PlayerID player_id);
  bool isFollowed(PlayerID player_id) const;
  const std::vector<FollowedPlayer>& getFollows() const
  {
    return state.follows;
  }

  const StoryState& getState() const { return state; }
  void restore(StoryState restored);
  void load(const std::shared_ptr<DatabaseConnection>& db_conn);
  void save(const std::shared_ptr<DatabaseConnection>& db_conn) const;

 private:
  bool fired(StoryKind kind, std::uint32_t entity, std::uint32_t key) const;
  bool firedSince(StoryKind kind, std::uint32_t entity,
                  std::int32_t since_day) const;
  /** Posts when the weekly cap allows; records the story either way. */
  bool post(StoryRecord record, InboxMessage message, Inbox& inbox,
            bool with_choice = false);
  CareerTotals careerBefore(PlayerID player_id);
  void trackFollows(const GameDateValue& date, TeamID managed_team_id,
                    const AwardSystem* awards, Inbox& inbox);
  void trackFollow(const GameDateValue& date, TeamID managed_team_id,
                   const AwardSystem* awards, FollowedPlayer& followed,
                   Inbox& inbox);
  void reportFollowChanges(const GameDateValue& date, TeamID managed_team_id,
                           FollowedPlayer& followed, const Player& player,
                           const std::string& name, TeamID team_id,
                           Inbox& inbox);
  void reportFollowHonours(const GameDateValue& date, TeamID managed_team_id,
                           const AwardSystem& awards, FollowedPlayer& followed,
                           const Player& player, const std::string& name,
                           Inbox& inbox);
  void followMatch(const MatchReport& report, TeamID managed_team_id,
                   Inbox& inbox);
  void raiseDilemma(const GameDateValue& date, TeamID managed_team_id,
                    const InteractionSystem& interactions,
                    const StoryDayContext& context, Inbox& inbox);
  struct DilemmaCandidate
  {
    StoryKind kind;
    std::vector<const Player*> players;
    std::uint32_t other = 0;
  };
  std::vector<DilemmaCandidate> dilemmaCandidates(
      std::int32_t today, TeamID managed_team_id,
      const InteractionSystem& interactions, const GameDateValue& date,
      const StoryDayContext& context,
      const std::vector<const Player*>& squad) const;
  void postDilemmaPick(const GameDateValue& date, TeamID managed_team_id,
                       const ClubProfile& profile, DilemmaCandidate& pick,
                       WorldRng& rng, Inbox& inbox);
  void updateInjuries(std::int32_t today, TeamID managed_team_id);
  void postCaptainDispute(const GameDateValue& date, std::int32_t today,
                          TeamID managed_team_id,
                          const InteractionSystem& interactions, Inbox& inbox);
  void processMatchPlayer(const MatchReport& report,
                          const PlayerMatchLine& line, TeamID managed_team_id,
                          const std::string& opponent_name, bool competitive,
                          Inbox& inbox);
  void postCareerMilestones(const GameDateValue& date,
                            const PlayerMatchLine& line, const Player& player,
                            const std::string& name,
                            const std::string& opponent_name, Inbox& inbox);
  void postBreakout(const GameDateValue& date, const PlayerMatchLine& line,
                    const Player& player, const std::string& name,
                    Inbox& inbox);
  void postTeamMatchStories(const MatchReport& report, TeamID managed_team_id,
                            bool home, const std::string& recent_form,
                            TeamID opponent_id,
                            const std::string& opponent_name, Inbox& inbox);

  std::shared_ptr<GameData> gamedata;
  StoryState state;
  std::function<CareerTotals(PlayerID)> career_provider;
  /** Transient cache of career totals, advanced from match reports. */
  std::unordered_map<PlayerID, CareerTotals> career_cache;
  mutable std::unordered_map<TeamID, std::optional<TeamID>> rivals;
  mutable std::mutex rivals_mutex;
};

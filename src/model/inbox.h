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
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "global/types.h"
#include "model/gamedate.h"

/**
 * @enum InboxCategory
 * @brief Message categories (values are persisted).
 */
enum class InboxCategory : std::uint8_t
{
  General = 0,
  Match,
  Injury,
  Transfer,
  Contract,
  Youth,
  Board,
  Finance,
  COUNT
};

/** Language key naming @p category (e.g. "INBOX_CAT_MATCH"). */
const char* inboxCategoryKey(InboxCategory category);

/**
 * @enum InboxAction
 * @brief What the manager can do straight from a message. Derived from the
 * title key (see Inbox::actionFor()), so producers only need to add their
 * key to the table in inbox.cpp; the linked player identifies the entity.
 */
enum class InboxAction : std::uint8_t
{
  None = 0,
  RespondOffer,   /*!< Accept / reject / counter a bid for player_id. */
  ReplyToPlayer,  /*!< Answer player_id's request or story. */
  YouthTrialists, /*!< Sign or release the intake trialists. */
  Shortlist,      /*!< Add the recommended player_id to the shortlist. */
  Dilemma         /*!< Pick one of the two answers of a decision moment. */
};

/**
 * @struct InboxView
 * @brief Filters of the inbox screen, kept with the career (table InboxView)
 * so they survive a scene change and a reload.
 */
struct InboxView
{
  std::int8_t tab = -1;      /*!< 0 decisions, 1 information, -1 pick. */
  std::int8_t category = -1; /*!< InboxCategory, -1 all. */
  bool unread_only = false;
  bool followed_only = false;        /*!< Messages about followed players. */
  std::optional<PlayerID> player_id; /*!< Only this player's messages. */
  std::optional<TeamID> team_id;     /*!< Only this club's messages. */

  bool operator==(const InboxView&) const = default;
};

/**
 * @struct InboxMessage
 * @brief A news item for the managed club.
 *
 * Title and body are stored as language keys plus arguments so that messages
 * follow the current UI language. Arguments starting with '@' are themselves
 * language keys (e.g. "@INJURY_HAMSTRING_STRAIN"). Use formatTitle() and
 * formatBody() to obtain display text.
 */
struct InboxMessage
{
  std::uint32_t id = 0;
  GameDateValue date;
  InboxCategory category = InboxCategory::General;
  std::string title_key;
  std::string body_key;
  std::vector<std::string> args;
  bool read = false;
  std::optional<PlayerID> player_id; /*!< Linked player, if any. */
  std::optional<TeamID> team_id;     /*!< Linked club, if any. */

  /** Localised title with arguments substituted ({0}, {1}, ...). */
  std::string formatTitle() const;

  /** Localised body with arguments substituted ({0}, {1}, ...). */
  std::string formatBody() const;
};

/**
 * @class Inbox
 * @brief Chronological list of messages with read tracking.
 *
 * The inbox keeps the newest MAX_MESSAGES messages; older ones are dropped.
 * Only news that needs the manager's attention arrives unread: routine
 * round-ups (transfer news, instalments) are filed as read, and repeated
 * offers of the same kind (bids and loan offers for the club's players) are
 * grouped into a weekly digest with a single unread message.
 */
class Inbox
{
 public:
  static constexpr std::size_t MAX_MESSAGES = 400;
  /** Window of the weekly digest of repeated offers and workload advice. */
  static constexpr std::int32_t DIGEST_DAYS = 7;

  /** Appends a message, assigns its id and returns it. */
  std::uint32_t add(InboxMessage message);

  /** Routine news that is filed as read on arrival. */
  static bool isRoutine(const std::string& title_key);

  /** Messages grouped into a weekly digest (one unread per week). */
  static bool isDigested(const std::string& title_key);

  /** Days after which read information leaves the main list. */
  static constexpr std::int32_t ARCHIVE_DAYS = 14;

  /** Action offered by messages with this title key. */
  static InboxAction actionFor(const std::string& title_key);

  /** Actions that ask for a decision (pinned while pending). */
  static bool isDecision(InboxAction action);

  /** Read information older than ARCHIVE_DAYS on @p today. */
  static bool isArchived(const InboxMessage& message,
                         const GameDateValue& today);

  /**
   * True when @p message passes the information filters of @p view:
   * category, unread, the chosen player or club, and the followed players
   * (@p followed) when followed_only is set.
   */
  static bool matchesView(const InboxMessage& message, const InboxView& view,
                          std::span<const PlayerID> followed);

  /** Saved filters of the inbox screen. */
  const InboxView& getView() const { return view; }
  void setView(const InboxView& updated) { view = updated; }

  /** Marks a message as read; false if the id is unknown. */
  bool markRead(std::uint32_t id);

  /** Marks every message as read. */
  void markAllRead();

  /** Number of unread messages. */
  std::size_t unreadCount() const;

  /** Messages, oldest first. */
  const std::vector<InboxMessage>& getMessages() const;

  /** Replaces the content with persisted messages. */
  void restore(std::vector<InboxMessage> messages);

 private:
  std::vector<InboxMessage> messages;
  std::uint32_t next_id = 1;
  InboxView view;
};

/**
 * Formats a localised template, substituting {n} with @p args.
 *
 * Arguments starting with '@' are language keys; ISO dates ("2027-06-30")
 * are shown with the localised month. Two placeholder forms serve grammar:
 * "{n:di}" puts the Italian preposition and article before club name n
 * ("della Roma", see ClubArticle), and "{n|one|other}" picks a word by
 * whether argument n is 1 ("{0} {0|giorno|giorni}").
 */
std::string formatLocalized(const std::string& key,
                            const std::vector<std::string>& args);

/**
 * An ISO date ("2027-06-30", GameDateValue::toString()) in the current
 * language, e.g. "30 giu 2027"; any other text is returned unchanged.
 */
std::string localizedDate(const std::string& text);

/**
 * Money for a message argument: "€" and the plain amount ("€1250000"),
 * shown by formatLocalized() in the reader's language ("€1.25M",
 * "€ 1,25 mln"), so saved messages follow a later change of language.
 */
std::string formatMoney(std::int64_t amount);

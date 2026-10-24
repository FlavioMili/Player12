// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/inbox.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <format>
#include <string_view>
#include <utility>

#include "global/language_manager.h"
#include "global/number_format.h"
#include "model/club_article.h"
#include "model/world_rng.h"

namespace
{
constexpr std::array<const char*, static_cast<std::size_t>(
                                      std::to_underlying(InboxCategory::COUNT))>
    CATEGORY_KEYS = {"INBOX_CAT_GENERAL",  "INBOX_CAT_MATCH",
                     "INBOX_CAT_INJURY",   "INBOX_CAT_TRANSFER",
                     "INBOX_CAT_CONTRACT", "INBOX_CAT_YOUTH",
                     "INBOX_CAT_BOARD",    "INBOX_CAT_FINANCE"};

constexpr std::array<std::string_view, 5> ROUTINE_TITLES = {
    "INBOX_TRANSFER_NEWS_TITLE", "INBOX_DEADLINE_SUMMARY_TITLE",
    "INBOX_INSTALMENT_PAID_TITLE", "INBOX_INSTALMENT_RECEIVED_TITLE",
    "SCOUT_ALERT_MOVED_TITLE"};

// Repeated advisory workload warnings share the weekly unread digest. Keep
// individual injury/carrying notices and actionable decisions distinct.
constexpr std::array<std::string_view, 3> DIGEST_TITLES = {
    "INBOX_BID_TITLE", "INBOX_LOAN_OFFER_TITLE", "MEDICAL_WARN_LOAD_TITLE"};

struct ActionEntry
{
  std::string_view title_key;
  InboxAction action;
};

constexpr std::array<ActionEntry, 21> ACTION_TITLES = {{
    {"INBOX_BID_TITLE", InboxAction::RespondOffer},
    {"INBOX_LOAN_OFFER_TITLE", InboxAction::RespondOffer},
    {"INBOX_OFFER_REPLY_TITLE", InboxAction::RespondOffer},
    {"TALK_REQUEST_TITLE", InboxAction::ReplyToPlayer},
    {"OFFER_REJECTED_REQUEST_TITLE", InboxAction::ReplyToPlayer},
    {"TALK_ESCALATED_TITLE", InboxAction::ReplyToPlayer},
    {"STORY_SAGA_TITLE", InboxAction::ReplyToPlayer},
    {"STORY_CAPTAIN_TITLE", InboxAction::ReplyToPlayer},
    {"INBOX_YOUTH_INTAKE_TITLE", InboxAction::YouthTrialists},
    {"INBOX_YOUTH_REMINDER_TITLE", InboxAction::YouthTrialists},
    {"SCOUT_MSG_RECOMMEND_TITLE", InboxAction::Shortlist},
    {"SCOUT_MSG_REPORT_TITLE", InboxAction::Shortlist},
    {"INBOX_SCOUT_SUGGESTION_TITLE", InboxAction::Shortlist},
    {"DILEMMA_LEAVE_TITLE", InboxAction::Dilemma},
    {"DILEMMA_HOMESICK_TITLE", InboxAction::Dilemma},
    {"DILEMMA_FINE_TITLE", InboxAction::Dilemma},
    {"DILEMMA_RIVAL_TITLE", InboxAction::Dilemma},
    {"DILEMMA_SPONSOR_TITLE", InboxAction::Dilemma},
    {"DILEMMA_CLASH_TITLE", InboxAction::Dilemma},
    {"DILEMMA_COURSE_TITLE", InboxAction::Dilemma},
    {"DILEMMA_TICKETS_TITLE", InboxAction::Dilemma},
}};

constexpr std::array<const char*, 12> MONTH_KEYS = {
    "MONTH_JAN", "MONTH_FEB", "MONTH_MAR", "MONTH_APR",
    "MONTH_MAY", "MONTH_JUN", "MONTH_JUL", "MONTH_AUG",
    "MONTH_SEP", "MONTH_OCT", "MONTH_NOV", "MONTH_DEC"};

bool isKeyArgument(std::string_view argument)
{
  return argument.size() > 1 && argument.front() == '@';
}

// Dates travel as ISO text ("2027-06-30", GameDateValue::toString()) so
// saved messages stay language-neutral; they are shown in the reader's
// language, e.g. "30 giu 2027".
bool isIsoDate(const std::string& argument)
{
  if (argument.size() != 10 || argument[4] != '-' || argument[7] != '-')
    return false;
  for (std::size_t i = 0; i < argument.size(); ++i)
  {
    if (i != 4 && i != 7 && (argument[i] < '0' || argument[i] > '9'))
      return false;
  }
  return true;
}

// Money travels as "€" and the plain amount ("€1250000", formatMoney()),
// so a saved message shows it in the reader's language. Messages saved
// before that carry finished text ("€1.25M"), which is left as it is.
std::string expandMoney(const std::string& argument)
{
  static constexpr std::string_view EURO =
      "\xE2"
      "\x82"
      "\xAC";
  std::size_t found = argument.find(EURO);
  if (found == std::string::npos) return argument;
  std::string result;
  std::size_t copied = 0;
  for (; found != std::string::npos; found = argument.find(EURO, found))
  {
    std::size_t end = found + EURO.size();
    if (end < argument.size() && argument[end] == '-') ++end;
    const std::size_t digits = end;
    while (end < argument.size() && argument[end] >= '0' &&
           argument[end] <= '9')
      ++end;
    if (const bool token =
            end > digits && end - digits <= 18 &&
            (end == argument.size() ||
             (std::isalnum(static_cast<unsigned char>(argument[end])) == 0 &&
              argument[end] != '.' && argument[end] != ','));
        !token)
    {
      found += EURO.size();
      continue;
    }
    result.append(argument, copied, found - copied);
    result += NumberFormat::money(
        std::strtoll(argument.c_str() + found + EURO.size(), nullptr, 10));
    copied = end;
    found = end;
  }
  result.append(argument, copied, std::string::npos);
  return result;
}

bool isKeyCharacter(char character)
{
  return (character >= 'A' && character <= 'Z') ||
         (character >= 'a' && character <= 'z') ||
         (character >= '0' && character <= '9') || character == '_';
}

// A composite argument (a list of transfers, of called-up players) can name
// language keys inline: "Rossi: @INBOX_FREE_AGENCY -> Roma", "Rossi
// (@NT_Italian)". Only known keys are replaced, so a lone "@" stays.
std::string expandKeys(const std::string& argument)
{
  std::size_t found = argument.find('@');
  if (found == std::string::npos) return argument;
  std::string result;
  std::size_t copied = 0;
  for (; found != std::string::npos; found = argument.find('@', found + 1))
  {
    std::size_t end = found + 1;
    while (end < argument.size() && isKeyCharacter(argument[end])) ++end;
    // Keys start with a capital ("INBOX_...", "NT_Italian").
    if (end - found < 3 || argument[found + 1] < 'A' ||
        argument[found + 1] > 'Z')
      continue;
    const std::string key = argument.substr(found + 1, end - found - 1);
    const char* text = LOC(key.c_str());
    if (text == key.c_str()) continue;
    result.append(argument, copied, found - copied);
    result += text;
    copied = end;
    found = end - 1;
  }
  result.append(argument, copied, std::string::npos);
  return result;
}

std::string resolveArgument(const std::string& argument)
{
  if (isKeyArgument(argument))
  {
    // "@KEY_A, @KEY_B" lists several keys (e.g. two position names).
    static constexpr std::string_view SEPARATOR = ", @";
    std::string resolved;
    std::size_t start = 1;
    for (std::size_t next = argument.find(SEPARATOR, start);
         next != std::string::npos; next = argument.find(SEPARATOR, start))
    {
      resolved += LOC(argument.substr(start, next - start).c_str());
      resolved += ", ";
      start = next + SEPARATOR.size();
    }
    return resolved + LOC(argument.c_str() + start);
  }
  if (isIsoDate(argument)) return localizedDate(argument);
  return expandKeys(expandMoney(argument));
}

// "{N:di}": club name N with the Italian preposition and its article
// ("della Roma"); "{N|one|other}": "one" when argument N is 1.
std::string resolveSpec(const std::string& argument, std::string_view spec)
{
  if (spec.front() == ':')
  {
    return isKeyArgument(argument)
               ? resolveArgument(argument)
               : ClubArticle::withPreposition(argument, spec.substr(1));
  }
  const std::size_t bar = spec.find('|', 1);
  if (bar == std::string_view::npos) return resolveArgument(argument);
  return std::string(argument == "1" ? spec.substr(1, bar - 1)
                                     : spec.substr(bar + 1));
}
}  // namespace

const char* inboxCategoryKey(InboxCategory category)
{
  const auto index = static_cast<std::size_t>(std::to_underlying(category));
  return index < CATEGORY_KEYS.size() ? CATEGORY_KEYS[index]
                                      : "INBOX_CAT_GENERAL";
}

std::string formatLocalized(const std::string& key,
                            const std::vector<std::string>& args)
{
  const std::string pattern = LOC(key.c_str());
  std::string result;
  result.reserve(pattern.size() + 32);
  for (std::size_t i = 0; i < pattern.size(); ++i)
  {
    if (pattern[i] == '{')
    {
      const std::size_t close = pattern.find('}', i);
      if (close != std::string::npos && close > i + 1)
      {
        char* end = nullptr;
        const unsigned long index =
            std::strtoul(pattern.c_str() + i + 1, &end, 10);
        const char* const stop = pattern.c_str() + close;
        if (end != pattern.c_str() + i + 1 &&
            (end == stop || *end == ':' || *end == '|'))
        {
          if (index < args.size())
          {
            result += end == stop ? resolveArgument(args[index])
                                  : resolveSpec(args[index],
                                                std::string_view(end, stop));
          }
          i = close;
          continue;
        }
      }
    }
    result += pattern[i];
  }
  return result;
}

std::string localizedDate(const std::string& text)
{
  if (!isIsoDate(text)) return text;
  const auto year = std::stoi(text.substr(0, 4));
  const auto month = static_cast<std::size_t>(std::stoi(text.substr(5, 2)));
  const auto day = std::stoi(text.substr(8, 2));
  if (month < 1 || month > MONTH_KEYS.size() || day < 1) return text;
  return std::format("{} {} {}", day, LOC(MONTH_KEYS[month - 1]), year);
}

std::string formatMoney(std::int64_t amount)
{
  return std::format("{}{}",
                     "\xE2"
                     "\x82"
                     "\xAC",
                     amount);
}

std::string InboxMessage::formatTitle() const
{
  return formatLocalized(title_key, args);
}

std::string InboxMessage::formatBody() const
{
  return formatLocalized(body_key, args);
}

bool Inbox::isRoutine(const std::string& title_key)
{
  return std::ranges::contains(ROUTINE_TITLES, title_key);
}

bool Inbox::isDigested(const std::string& title_key)
{
  return std::ranges::contains(DIGEST_TITLES, title_key);
}

InboxAction Inbox::actionFor(const std::string& title_key)
{
  using enum InboxAction;
  const auto found =
      std::ranges::find(ACTION_TITLES, title_key, &ActionEntry::title_key);
  return found != ACTION_TITLES.end() ? found->action : None;
}

bool Inbox::isDecision(InboxAction action)
{
  return action == InboxAction::RespondOffer ||
         action == InboxAction::ReplyToPlayer ||
         action == InboxAction::YouthTrialists ||
         action == InboxAction::Dilemma;
}

bool Inbox::matchesView(const InboxMessage& message, const InboxView& view,
                        std::span<const PlayerID> followed)
{
  if (view.category >= 0 &&
      static_cast<int>(std::to_underlying(message.category)) !=
          static_cast<int>(view.category))
    return false;
  if (view.unread_only && message.read) return false;
  if (view.player_id.has_value() && message.player_id != view.player_id)
    return false;
  if (view.team_id.has_value() && message.team_id != view.team_id) return false;
  return !view.followed_only ||
         (message.player_id.has_value() &&
          std::ranges::contains(followed, *message.player_id));
}

bool Inbox::isArchived(const InboxMessage& message, const GameDateValue& today)
{
  return message.read && !isDecision(actionFor(message.title_key)) &&
         dayOrdinal(today) - dayOrdinal(message.date) > ARCHIVE_DAYS;
}

std::uint32_t Inbox::add(InboxMessage message)
{
  if (isRoutine(message.title_key)) message.read = true;
  if (!message.read && isDigested(message.title_key))
  {
    // One unread message of the kind per week; the rest join its thread.
    const std::int32_t day = dayOrdinal(message.date);
    for (auto it = messages.rbegin(); it != messages.rend(); ++it)
    {
      if (day - dayOrdinal(it->date) >= DIGEST_DAYS) break;
      if (!it->read && it->title_key == message.title_key)
      {
        message.read = true;
        break;
      }
    }
  }
  message.id = next_id++;
  const std::uint32_t id = message.id;
  messages.push_back(std::move(message));
  if (messages.size() > MAX_MESSAGES)
  {
    messages.erase(messages.begin(),
                   messages.begin() + static_cast<std::ptrdiff_t>(
                                          messages.size() - MAX_MESSAGES));
  }
  return id;
}

bool Inbox::markRead(std::uint32_t id)
{
  const auto found = std::ranges::find(messages, id, &InboxMessage::id);
  if (found == messages.end()) return false;
  found->read = true;
  return true;
}

void Inbox::markAllRead()
{
  for (InboxMessage& message : messages) message.read = true;
}

std::size_t Inbox::unreadCount() const
{
  return static_cast<std::size_t>(
      std::ranges::count(messages, false, &InboxMessage::read));
}

const std::vector<InboxMessage>& Inbox::getMessages() const { return messages; }

void Inbox::restore(std::vector<InboxMessage> restored)
{
  messages = std::move(restored);
  std::ranges::sort(messages, {}, &InboxMessage::id);
  next_id = messages.empty() ? 1 : messages.back().id + 1;
}

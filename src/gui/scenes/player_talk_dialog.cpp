// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/player_talk_dialog.h"

#include <fmt/printf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/calendar.h"
#include "model/inbox.h"
#include "model/stories.h"
#include "model/world_rng.h"

namespace
{
constexpr const char* POPUP_ID = "###player_talk_dialog";
constexpr float DIALOG_WIDTH = 600.0f;
constexpr float VIEWPORT_WIDTH_SHARE = 0.94f;
constexpr float VIEWPORT_HEIGHT_SHARE = 0.9f;
constexpr float METER_LABEL_WIDTH = 90.0f;
constexpr float BUTTON_WIDTH = 150.0f;
constexpr float ACCENT_STRIPE = 3.0f;

/** Topic sections in display order. */
struct SectionSpec
{
  const char* title_key;
  std::array<TalkOption, 4> options;
  std::size_t count;
};

constexpr std::array<SectionSpec, 4> SECTIONS = {{
    {"TALK_SECTION_FORM",
     {TalkOption::PraiseForm, TalkOption::CriticiseForm},
     2},
    {"TALK_SECTION_PLAYING_TIME",
     {TalkOption::DiscussPlayingTime, TalkOption::PromisePlayingTime},
     2},
    {"TALK_SECTION_FUTURE",
     {TalkOption::PromiseContract, TalkOption::PromiseSigning,
      TalkOption::AskPatience},
     3},
    {"TALK_SECTION_TRANSFER",
     {TalkOption::ReassureStay, TalkOption::OpenToOffers,
      TalkOption::AcceptTransferRequest, TalkOption::RefuseTransferRequest},
     4},
}};

/** Options that do not apply to the situation are hidden, not greyed. */
bool hidden(TalkBlock block)
{
  using enum TalkBlock;
  return block == NotAtClub || block == NoTransferInterest ||
         block == NoTransferRequest || block == NotUnhappy;
}

ImVec4 hintColor(TalkHint hint)
{
  using enum TalkHint;
  const Theme::Palette& palette = Theme::palette();
  switch (hint)
  {
    case LikelyPositive:
      return palette.positive;
    case Uncertain:
      return palette.muted;
    case Risky:
      break;
  }
  return palette.warning;
}

ImVec4 reactionColor(TalkReaction reaction)
{
  using enum TalkReaction;
  const Theme::Palette& palette = Theme::palette();
  switch (reaction)
  {
    case Positive:
      return palette.positive;
    case Neutral:
      return palette.info;
    case Negative:
      break;
  }
  return palette.negative;
}

float scaled(float value) { return value * Theme::scale(); }

std::string dateOf(std::int32_t day_ordinal, const GameDateValue& today)
{
  return Format::date(
      SeasonCalendar::addDays(today, day_ordinal - dayOrdinal(today)));
}
}  // namespace

void PlayerTalkDialog::open(GameController& controller, PlayerID id)
{
  player_id = id;
  has_result = false;
  consequences.clear();
  rebuild(controller);
  open_requested = true;
}

void PlayerTalkDialog::rebuild(const GameController& controller)
{
  const Theme::Palette& palette = Theme::palette();
  const auto gamedata = controller.getGameData();
  const auto player = gamedata ? gamedata->getPlayer(player_id) : std::nullopt;
  sections.clear();
  promises.clear();
  request_text.clear();
  story_text.clear();
  if (!player) return;
  const Player& who = player->get();
  const PlayerDynamics& dynamics = who.getDynamics();
  title = who.getName();
  subtitle = fmt::sprintf(LOC("TALK_SUBTITLE"),
                          LOC(squadRoleKey(controller.getSquadRole(player_id))),
                          who.getAge());
  if (dynamics.rating_count > 0)
    subtitle += fmt::sprintf(LOC("TALK_SUBTITLE_FORM"), who.getForm());
  morale = dynamics.morale;
  morale_text = fmt::sprintf("%.0f", morale);
  const PlayerRelation* relation = controller.getPlayerRelation(player_id);
  trust = relation != nullptr ? relation->trust : 0.0f;
  trust_text = fmt::sprintf("%+.0f", trust);
  if (relation != nullptr && relation->request != TalkRequest::None)
    request_text = fmt::sprintf(
        LOC("TALK_WANTS"), LOC(Interactions::requestKey(relation->request)));
  if (const auto choice = controller.getStoryChoice(player_id))
    story_text = fmt::sprintf(LOC("TALK_OPEN_STORY"),
                              LOC(Stories::kindKey(choice->kind)));

  const GameDateValue today = controller.getCurrentDate();
  const std::int32_t today_ordinal = dayOrdinal(today);
  for (const Promise& promise : controller.getPlayerPromises(player_id))
  {
    PromiseLine line;
    line.text = LOC(Interactions::promiseTypeKey(promise.type));
    if (promise.type == PromiseType::PlayingTime && promise.team_minutes > 0)
      line.text += fmt::sprintf(
          LOC("TALK_PROMISE_SHARE"),
          std::lround(100.0 * promise.player_minutes / promise.team_minutes),
          std::lround(100.0f * promise.target));
    line.progress = promise.progress();
    using enum PromiseState;
    switch (promise.state)
    {
      case Active:
        line.when = fmt::sprintf(
            Format::plural("TALK_PROMISE_DUE",
                           std::max(0, promise.deadline_day - today_ordinal)),
            dateOf(promise.deadline_day, today).c_str(),
            std::max(0, promise.deadline_day - today_ordinal));
        line.color = palette.accent;
        break;
      case Kept:
        line.when = LOC(Interactions::promiseStateKey(promise.state));
        line.color = palette.positive;
        break;
      case Broken:
        line.when = LOC(Interactions::promiseStateKey(promise.state));
        line.color = palette.negative;
        break;
      case PromiseState::Voided:
        line.when = fmt::sprintf(
            "%s (%s)", LOC(Interactions::promiseStateKey(promise.state)),
            LOC(Interactions::voidReasonKey(promise.void_reason)));
        line.color = palette.muted;
        break;
    }
    promises.push_back(std::move(line));
  }

  const std::vector<TalkOptionView> views =
      controller.getTalkOptions(player_id);
  for (const SectionSpec& spec : SECTIONS)
  {
    Section section;
    section.title = LOC(spec.title_key);
    for (std::size_t index = 0; index < spec.count; ++index)
    {
      const auto view = std::ranges::find(views, spec.options[index],
                                          &TalkOptionView::option);
      if (view == views.end() || hidden(view->block)) continue;
      OptionRow row;
      row.option = view->option;
      row.label = LOC(Interactions::optionKey(view->option));
      row.description = LOC(Interactions::optionDescriptionKey(view->option));
      row.enabled = view->block == TalkBlock::None;
      row.suggested = row.enabled && view->suggested;
      if (row.enabled)
      {
        row.status = LOC(Interactions::hintKey(view->hint));
        row.status_color = hintColor(view->hint);
      }
      else
      {
        row.status =
            view->block == TalkBlock::Cooldown
                ? fmt::sprintf(Format::plural("TALK_BLOCK_COOLDOWN",
                                              view->cooldown_days),
                               view->cooldown_days)
                : std::string(LOC(Interactions::blockKey(view->block)));
        row.status_color = palette.faint;
      }
      section.rows.push_back(std::move(row));
    }
    if (!section.rows.empty()) sections.push_back(std::move(section));
  }
}

bool PlayerTalkDialog::render(GameController& controller)
{
  if (open_requested)
  {
    open_requested = false;
    ImGui::OpenPopup(POPUP_ID);
  }
  visible = ImGui::IsPopupOpen(POPUP_ID);
  if (!visible) return false;

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float width = std::min(scaled(DIALOG_WIDTH),
                               viewport->WorkSize.x * VIEWPORT_WIDTH_SHARE);
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(width, 0.0f),
      ImVec2(width, viewport->WorkSize.y * VIEWPORT_HEIGHT_SHARE));
  if (!ImGui::BeginPopupModal(POPUP_ID, nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoSavedSettings))
  {
    visible = false;
    return false;
  }
  const bool had_result = has_result;
  renderHeader();
  if (has_result)
    renderResult();
  else
    renderOptions(controller);

  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));
  const ImVec2 buttonSize(
      std::min(scaled(BUTTON_WIDTH), ImGui::GetContentRegionAvail().x), 0.0f);
  if (const bool close = has_result
                             ? UI::primaryButton(LOC("TALK_CLOSE"), buttonSize)
                             : ImGui::Button(LOC("TALK_LEAVE"), buttonSize);
      close || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
  {
    ImGui::CloseCurrentPopup();
    visible = false;
  }
  ImGui::EndPopup();
  return has_result && !had_result;
}

void PlayerTalkDialog::renderHeader() const
{
  const Theme::Palette& palette = Theme::palette();
  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    UI::textFitted(title, ImGui::GetContentRegionAvail().x, palette.text);
  }
  UI::textFitted(subtitle, ImGui::GetContentRegionAvail().x, palette.muted);
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  const float labelWidth = std::min(scaled(METER_LABEL_WIDTH),
                                    ImGui::GetContentRegionAvail().x * 0.4f);
  UI::meter(LOC("TALK_MORALE"), morale / 100.0f, labelWidth,
            Theme::ratingColor(morale), morale_text.c_str());
  UI::meter(LOC("TALK_TRUST"), (trust + 100.0f) / 200.0f, labelWidth,
            trust >= 0.0f ? palette.info : palette.warning, trust_text.c_str());

  if (!request_text.empty() || !story_text.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
    ImGui::PushTextWrapPos(0.0f);
    if (!request_text.empty())
      ImGui::TextColored(palette.warning, "%s", request_text.c_str());
    if (!story_text.empty())
      ImGui::TextColored(palette.info, "%s", story_text.c_str());
    ImGui::PopTextWrapPos();
  }

  if (!promises.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
    UI::sectionLabel(LOC("TALK_PROMISES"));
    for (const PromiseLine& line : promises)
    {
      UI::meter(line.text.c_str(), line.progress,
                ImGui::GetContentRegionAvail().x * 0.45f, line.color, "");
      ImGui::TextColored(palette.muted, "%s", line.when.c_str());
    }
  }
  ImGui::Separator();
}

void PlayerTalkDialog::renderOptions(GameController& controller)
{
  const Theme::Palette& palette = Theme::palette();
  if (sections.empty())
  {
    UI::emptyState(LOC("TALK_NOTHING_TITLE"), LOC("TALK_NOTHING_BODY"));
    return;
  }
  const float lineHeight = ImGui::GetTextLineHeight();
  const float padding = scaled(Theme::Space::S);
  const float rowHeight = 2.0f * lineHeight + 2.0f * padding;
  std::optional<TalkOption> chosen;
  for (const Section& section : sections)
  {
    ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
    UI::sectionLabel(section.title.c_str());
    for (const OptionRow& row : section.rows)
    {
      ImGui::PushID(static_cast<int>(std::to_underlying(row.option)));
      const float width = ImGui::GetContentRegionAvail().x;
      const ImVec2 start = ImGui::GetCursorScreenPos();
      ImGui::BeginDisabled(!row.enabled);
      if (ImGui::Selectable("##talk_option", false, ImGuiSelectableFlags_None,
                            ImVec2(width, rowHeight)))
        chosen = row.option;
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", row.description.c_str());

      ImDrawList* drawList = ImGui::GetWindowDrawList();
      const float alpha = row.enabled ? 1.0f : 0.55f;
      if (row.suggested)
        drawList->AddRectFilled(ImVec2(start.x, start.y + padding * 0.5f),
                                ImVec2(start.x + scaled(ACCENT_STRIPE),
                                       start.y + rowHeight - padding * 0.5f),
                                Theme::toU32(palette.accent));
      const float statusWidth =
          std::min(ImGui::CalcTextSize(row.status.c_str()).x, width * 0.4f);
      const float textX = start.x + padding + scaled(ACCENT_STRIPE);
      const float textWidth = std::max(
          0.0f, width - statusWidth - 3.0f * padding - scaled(ACCENT_STRIPE));
      UI::drawTextFitted(drawList, ImVec2(textX, start.y + padding),
                         Theme::toU32(palette.text, alpha), row.label,
                         textWidth);
      UI::drawTextFitted(
          drawList, ImVec2(textX, start.y + padding + lineHeight),
          Theme::toU32(palette.muted, alpha), row.description, textWidth);
      UI::drawTextFitted(drawList,
                         ImVec2(start.x + width - padding - statusWidth,
                                start.y + (rowHeight - lineHeight) * 0.5f),
                         Theme::toU32(row.status_color), row.status,
                         statusWidth);
      ImGui::PopID();
    }
  }
  if (chosen) choose(controller, *chosen);
}

void PlayerTalkDialog::choose(GameController& controller, TalkOption option)
{
  const auto outcome = controller.talkToPlayer(player_id, option);
  if (!outcome)
  {
    rebuild(controller);  // The situation changed; show the fresh options.
    return;
  }
  const auto gamedata = controller.getGameData();
  const auto player = gamedata->getPlayer(player_id);
  const std::string first_name =
      player ? player->get().getFirstName() : std::string();
  reaction_text = LOC(Interactions::reactionKey(outcome->reaction));
  reaction_color = reactionColor(outcome->reaction);
  reply_text = formatLocalized(outcome->reply_key, {first_name});
  consequences.clear();
  consequences.push_back(
      fmt::sprintf(LOC("TALK_RESULT_MORALE"), outcome->morale_delta));
  if (std::abs(outcome->trust_delta) > 0.0f)
    consequences.push_back(
        fmt::sprintf(LOC("TALK_RESULT_TRUST"), outcome->trust_delta));
  if (outcome->promise)
  {
    const auto made = controller.getPlayerPromises(player_id);
    if (!made.empty())
      consequences.push_back(fmt::sprintf(
          LOC("TALK_RESULT_PROMISE"),
          LOC(Interactions::promiseTypeKey(*outcome->promise)),
          dateOf(made.front().deadline_day, controller.getCurrentDate())
              .c_str()));
  }
  if (outcome->list_player)
    consequences.push_back(fmt::sprintf(
        LOC("TALK_RESULT_LISTED"),
        Format::moneyFull(controller.getPlayerMarketValue(player_id)).c_str()));
  if (outcome->settled) consequences.emplace_back(LOC("TALK_RESULT_SETTLED"));
  if (outcome->request_resolved)
    consequences.emplace_back(LOC("TALK_RESULT_REQUEST_SETTLED"));
  if (outcome->transfer_request)
    consequences.emplace_back(LOC("TALK_RESULT_TRANSFER_REQUEST"));
  has_result = true;
  rebuild(controller);
}

void PlayerTalkDialog::renderResult() const
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  UI::badge(reaction_text.c_str(), reaction_color);
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  {
    Theme::ScopedText body(Theme::Text::BODY);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(reply_text.c_str());
    ImGui::PopTextWrapPos();
  }
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  ImGui::PushTextWrapPos(0.0f);
  for (const std::string& line : consequences)
    ImGui::TextColored(palette.muted, "%s", line.c_str());
  ImGui::PopTextWrapPos();
}

void PlayerTalkDialog::inboxAction(GameController& controller,
                                   const InboxMessage& message)
{
  if (!message.player_id.has_value() ||
      !controller.hasPendingTalk(*message.player_id))
    return;
  ImGui::PushID("talk_reply");
  if (UI::primaryButton(LOC("TALK_INBOX_REPLY")))
    open(controller, *message.player_id);
  ImGui::PopID();
}

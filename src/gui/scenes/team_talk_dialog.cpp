// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/team_talk_dialog.h"

#include <fmt/printf.h>

#include <algorithm>
#include <optional>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/match_engine.h"

namespace
{
constexpr const char* POPUP_ID = "###team_talk_dialog";
constexpr float DIALOG_WIDTH = 560.0f;
constexpr float VIEWPORT_WIDTH_SHARE = 0.94f;
constexpr float VIEWPORT_HEIGHT_SHARE = 0.9f;
constexpr float BUTTON_WIDTH = 150.0f;
constexpr float SHARE_BAR_WIDTH = 72.0f;
constexpr float SHARE_BAR_HEIGHT = 5.0f;
/** The pre-match talk is offered in the first minute and withdrawn after
 * the third; the half-time talk is withdrawn five minutes into the second
 * half (match clock). */
constexpr float PRE_MATCH_OFFER_MINUTES = 1.0f;
constexpr float PRE_MATCH_LAPSE_MINUTES = 3.0f;
constexpr float HALF_TIME_LAPSE_MINUTES = 50.0f;

float scaled(float value) { return value * Theme::scale(); }

ImVec4 hintColor(TalkHint hint)
{
  const Theme::Palette& palette = Theme::palette();
  using enum TalkHint;
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
}  // namespace

void TeamTalkDialog::open(const GameController& controller, TeamTalkMoment when,
                          int own, int other)
{
  moment = when;
  own_goals = own;
  other_goals = other;
  has_result = false;
  const TeamTalkContext context =
      controller.getTeamTalkContext(moment, own_goals, other_goals);

  title = LOC(moment == TeamTalkMoment::PreMatch ? "TEAMTALK_TITLE_PRE"
                                                 : "TEAMTALK_TITLE_HALF");
  if (moment == TeamTalkMoment::HalfTime)
  {
    const char* state_key = "TEAMTALK_LEVEL";
    if (own_goals > other_goals)
      state_key = "TEAMTALK_LEADING";
    else if (own_goals < other_goals)
      state_key = "TEAMTALK_TRAILING";
    situation = fmt::sprintf(LOC("TEAMTALK_SCORE"), own_goals, other_goals,
                             LOC(state_key));
  }
  else
  {
    const char* expect_key = "TEAMTALK_EXPECT_EVEN";
    if (context.expected_points >= 1.9f)
      expect_key = "TEAMTALK_EXPECT_FAVOURITE";
    else if (context.expected_points <= 1.0f)
      expect_key = "TEAMTALK_EXPECT_UNDERDOG";
    situation = LOC(expect_key);
  }
  tags.clear();
  if (context.final_match) tags.emplace_back(LOC("TEAMTALK_TAG_FINAL"));
  if (context.cup && !context.final_match)
    tags.emplace_back(LOC("TEAMTALK_TAG_CUP"));
  if (context.derby) tags.emplace_back(LOC("TEAMTALK_TAG_DERBY"));

  const DressingRoom room = controller.getDressingRoom();
  mood =
      fmt::sprintf(LOC("TEAMTALK_MOOD"), LOC(Interactions::moodKey(room.mood)));
  leaders.clear();
  const auto gamedata = controller.getGameData();
  for (const LeaderInfo& leader : room.leaders)
  {
    const auto player = gamedata->getPlayer(leader.player_id);
    if (!player) continue;
    if (!leaders.empty()) leaders += ", ";
    leaders += player->get().getName();
  }
  if (!leaders.empty())
    leaders = fmt::sprintf(LOC("TEAMTALK_LEADERS"), leaders.c_str());

  rows.clear();
  for (const TeamTalkPrediction& prediction :
       Interactions::predictTeamTalk(context))
  {
    ToneRow row;
    row.tone = prediction.tone;
    row.label = LOC(Interactions::toneKey(prediction.tone));
    row.description = LOC(Interactions::toneDescriptionKey(prediction.tone));
    row.hint = LOC(Interactions::hintKey(prediction.hint));
    row.hint_color = hintColor(prediction.hint);
    row.positive = prediction.positive_share;
    row.negative = prediction.negative_share;
    rows.push_back(std::move(row));
  }
  open_requested = true;
}

bool TeamTalkDialog::render(GameController& controller)
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
  if (dismiss_requested)
  {
    dismiss_requested = false;
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    visible = false;
    return false;
  }
  const Theme::Palette& palette = Theme::palette();
  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    UI::textFitted(title, ImGui::GetContentRegionAvail().x, palette.text);
  }
  UI::textFitted(situation, ImGui::GetContentRegionAvail().x, palette.muted);
  for (std::size_t index = 0; index < tags.size(); ++index)
  {
    if (index > 0) ImGui::SameLine();
    UI::badge(tags[index].c_str(), palette.accent);
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.muted, "%s", mood.c_str());
  if (!leaders.empty())
    ImGui::TextColored(palette.muted, "%s", leaders.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Separator();

  const bool had_result = has_result;
  if (has_result)
    renderResult();
  else
    renderOptions(controller);

  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));
  const ImVec2 buttonSize(
      std::min(scaled(BUTTON_WIDTH), ImGui::GetContentRegionAvail().x), 0.0f);
  const bool close = has_result
                         ? UI::primaryButton(LOC("TALK_CLOSE"), buttonSize)
                         : ImGui::Button(LOC("TEAMTALK_SKIP"), buttonSize);
  if (!has_result && ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", LOC("TEAMTALK_SKIP_HINT"));
  if (close || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
  {
    ImGui::CloseCurrentPopup();
    visible = false;
  }
  ImGui::EndPopup();
  return has_result && !had_result;
}

void TeamTalkDialog::renderOptions(GameController& controller)
{
  const Theme::Palette& palette = Theme::palette();
  const float lineHeight = ImGui::GetTextLineHeight();
  const float padding = scaled(Theme::Space::S);
  const float rowHeight = 2.0f * lineHeight + 2.0f * padding;
  std::optional<TeamTalkTone> chosen;
  for (const ToneRow& row : rows)
  {
    ImGui::PushID(static_cast<int>(std::to_underlying(row.tone)));
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 start = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable("##tone", false, ImGuiSelectableFlags_None,
                          ImVec2(width, rowHeight)))
      chosen = row.tone;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", row.description.c_str());
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    // Right column: the hint above a bar of the predicted reception.
    const float barWidth = std::min(scaled(SHARE_BAR_WIDTH), width * 0.3f);
    const float hintWidth = std::max(
        barWidth,
        std::min(ImGui::CalcTextSize(row.hint.c_str()).x, width * 0.35f));
    const float rightX = start.x + width - padding - hintWidth;
    UI::drawTextFitted(drawList, ImVec2(rightX, start.y + padding),
                       Theme::toU32(row.hint_color), row.hint, hintWidth);
    const float barY = start.y + padding + lineHeight + scaled(4.0f);
    const float barHeight = scaled(SHARE_BAR_HEIGHT);
    const ImVec2 barStart(start.x + width - padding - barWidth, barY);
    drawList->AddRectFilled(
        barStart, ImVec2(barStart.x + barWidth, barY + barHeight),
        Theme::toU32(palette.faint, 0.35f), barHeight * 0.5f);
    drawList->AddRectFilled(
        barStart,
        ImVec2(barStart.x + barWidth * std::clamp(row.positive, 0.0f, 1.0f),
               barY + barHeight),
        Theme::toU32(palette.positive), barHeight * 0.5f);
    if (const float negativeWidth =
            barWidth * std::clamp(row.negative, 0.0f, 1.0f);
        negativeWidth > 0.5f)
      drawList->AddRectFilled(
          ImVec2(barStart.x + barWidth - negativeWidth, barY),
          ImVec2(barStart.x + barWidth, barY + barHeight),
          Theme::toU32(palette.negative), barHeight * 0.5f);

    const float textWidth = std::max(0.0f, width - hintWidth - 3.0f * padding);
    UI::drawTextFitted(drawList, ImVec2(start.x + padding, start.y + padding),
                       Theme::toU32(palette.text), row.label, textWidth);
    UI::drawTextFitted(
        drawList, ImVec2(start.x + padding, start.y + padding + lineHeight),
        Theme::toU32(palette.muted), row.description, textWidth);
    ImGui::PopID();
  }
  if (!chosen) return;
  const auto result =
      controller.giveTeamTalk(moment, *chosen, own_goals, other_goals);
  if (!result) return;
  result_summary = LOC(result->summary_key.c_str());
  result_counts = fmt::sprintf(LOC("TEAMTALK_RESULT_COUNTS"), result->positive,
                               result->neutral, result->negative);
  result_morale =
      fmt::sprintf(LOC("TEAMTALK_RESULT_MORALE"), result->morale_delta);
  result_color = result->positive >= result->negative ? palette.positive
                                                      : palette.negative;
  has_result = true;
}

void TeamTalkDialog::renderResult() const
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(result_color, "%s", result_summary.c_str());
  ImGui::TextUnformatted(result_counts.c_str());
  ImGui::TextColored(palette.muted, "%s", result_morale.c_str());
  ImGui::PopTextWrapPos();
}

void TeamTalkDialog::renderForMatch(GameController& controller,
                                    MatchEngine& engine, TeamID home_id,
                                    TeamID away_id)
{
  using enum TeamTalkMoment;
  const auto team = controller.getManagedTeam();
  const TeamID managed = team ? team->get().getId() : 0;
  const MatchState state = engine.getState();
  const int period = engine.getPeriod();
  const float minute = engine.getMatchTimeMinutes();
  if (team && (managed == home_id || managed == away_id) && !visible &&
      !open_requested && state != MatchState::FULL_TIME)
  {
    const bool home = managed == home_id;
    const int own = home ? engine.getHomeScore() : engine.getAwayScore();
    const int other = home ? engine.getAwayScore() : engine.getHomeScore();
    if (!pre_offered && period == 1 && minute < PRE_MATCH_OFFER_MINUTES)
    {
      pre_offered = true;
      if (controller.canGiveTeamTalk(PreMatch)) open(controller, PreMatch);
    }
    else if (!half_offered && (state == MatchState::HALF_TIME || period >= 2))
    {
      pre_offered = true;
      half_offered = true;
      if (controller.canGiveTeamTalk(HalfTime))
        open(controller, HalfTime, own, other);
    }
  }
  // An unanswered talk lapses once play has moved on.
  if (const bool lapsed = state == MatchState::FULL_TIME ||
                          (moment == PreMatch &&
                           (period > 1 || minute >= PRE_MATCH_LAPSE_MINUTES)) ||
                          (moment == HalfTime && period >= 2 &&
                           minute >= HALF_TIME_LAPSE_MINUTES);
      lapsed && (visible || open_requested))
  {
    open_requested = false;
    dismiss_requested = visible;
  }
  render(controller);
  // The talks' outcome plays its part in the match: only changes reach the
  // engine (each one resets its look-ahead).
  if (team && (managed == home_id || managed == away_id))
  {
    for (int half = 1; half <= 2; ++half)
    {
      const float modifier = controller.getTeamTalkModifier(managed, half);
      float& applied = applied_talk[static_cast<std::size_t>(half - 1)];
      if (modifier == applied) continue;
      applied = modifier;
      engine.setTeamTalkModifier(managed == home_id, half, modifier);
    }
  }
}

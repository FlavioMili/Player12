// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "match_subs_panel.h"

#include <fmt/printf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "gui/scenes/match_scene_tuning.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/match_engine.h"
#include "model/role_utils.h"
#include "model/team.h"

namespace
{
constexpr float DIALOG_WIDTH = 1040.0f;
constexpr float DIALOG_HEIGHT = 760.0f;
/** Pitch and bench side by side from this dialog content width. */
constexpr float TWO_COLUMN_WIDTH = 760.0f;
constexpr float PITCH_SHARE = 0.56f;
constexpr float PITCH_MAX_HEIGHT = 430.0f;
constexpr float PITCH_MIN_HEIGHT = 150.0f;
constexpr float PITCH_ASPECT = 68.0f / 105.0f;
constexpr float MARKER_RADIUS = 12.0f;
constexpr float MARKER_LABEL_WIDTH = 92.0f;
constexpr float CONDITION_BAR_WIDTH = 36.0f;
constexpr float CONDITION_BAR_HEIGHT = 4.0f;
constexpr float BENCH_BAR_WIDTH = 64.0f;
constexpr std::size_t SUGGESTION_COUNT = 3;
/** Fixed engine steps between two looks at the suggestions (2 s). */
constexpr std::uint64_t SUGGESTION_REFRESH_STEPS = 20;
constexpr const char* BENCH_PAYLOAD = "FM_SUB_BENCH";
constexpr const char* PITCH_PAYLOAD = "FM_SUB_PITCH";

float scaled(float value) { return value * Theme::scale(); }

/** Light text on dark kits, dark text on light ones. */
ImU32 textOn(ImU32 kit)
{
  const ImVec4 colour = ImGui::ColorConvertU32ToFloat4(kit);
  const float luminance =
      0.299f * colour.x + 0.587f * colour.y + 0.114f * colour.z;
  return luminance > 0.6f ? IM_COL32(20, 20, 24, 255)
                          : IM_COL32(250, 250, 250, 255);
}

void conditionBar(ImDrawList* drawList, ImVec2 min, float width, float height,
                  float condition)
{
  const ImVec2 max(min.x + width, min.y + height);
  drawList->AddRectFilled(min, max, IM_COL32(0, 0, 0, 120), height * 0.5f);
  drawList->AddRectFilled(
      min, ImVec2(min.x + width * std::clamp(condition, 0.0f, 1.0f), max.y),
      Theme::toU32(Touchline::conditionColor(condition)), height * 0.5f);
}

void drawPitchMarkings(ImDrawList* drawList, ImVec2 min, ImVec2 max)
{
  using Pitch = MatchSceneTuning::Pitch;
  const float width = max.x - min.x;
  const float height = max.y - min.y;
  drawList->AddRectFilled(min, max, Pitch::GRASS_COLOR, scaled(4.0f));
  const float stripe = width / static_cast<float>(Pitch::MOWING_STRIPE_COUNT);
  for (int index = 1; index < Pitch::MOWING_STRIPE_COUNT; index += 2)
  {
    const float left = min.x + stripe * static_cast<float>(index);
    drawList->AddRectFilled(ImVec2(left, min.y), ImVec2(left + stripe, max.y),
                            Pitch::ALTERNATE_GRASS_COLOR);
  }
  const float line = scaled(1.2f);
  const ImU32 colour = IM_COL32(255, 255, 255, 120);
  drawList->AddRect(min, max, colour, scaled(4.0f), 0, line);
  const float midX = min.x + width * 0.5f;
  drawList->AddLine(ImVec2(midX, min.y), ImVec2(midX, max.y), colour, line);
  drawList->AddCircle(ImVec2(midX, min.y + height * 0.5f),
                      height * Pitch::CENTRE_CIRCLE_RADIUS_RATIO, colour, 32,
                      line);
  const float boxWidth = width * Pitch::PENALTY_BOX_WIDTH_RATIO;
  const float boxTop =
      min.y + height * (1.0f - Pitch::PENALTY_BOX_HEIGHT_RATIO) * 0.5f;
  const float boxBottom = max.y - (boxTop - min.y);
  drawList->AddRect(ImVec2(min.x, boxTop), ImVec2(min.x + boxWidth, boxBottom),
                    colour, 0.0f, 0, line);
  drawList->AddRect(ImVec2(max.x - boxWidth, boxTop), ImVec2(max.x, boxBottom),
                    colour, 0.0f, 0, line);
}

const char* fitLabel(float fit)
{
  if (fit >= 0.99f) return LOC("SUBSTITUTION_FIT_NATURAL");
  if (fit >= 0.7f) return LOC("SUBSTITUTION_FIT_GOOD");
  if (fit >= 0.5f) return LOC("SUBSTITUTION_FIT_FAIR");
  return LOC("SUBSTITUTION_FIT_POOR");
}
}  // namespace

void MatchSubsPanel::resetSelection()
{
  selected_out = 0;
  selected_in = 0;
  swap_mode = false;
}

bool MatchSubsPanel::plan(const TouchlineContext& context, PlayerID out,
                          PlayerID in)
{
  const auto team = context.controller.getTeamById(context.team_id);
  if (!team) return false;
  const Lineup& lineup = team->get().getLineup();
  const MatchChanges::Refusal refusal = substitution_plan.add(
      context.engine, context.home, lineup.getReserves(), out, in);
  selected_out = 0;
  selected_in = 0;
  swap_mode = false;
  if (refusal != MatchChanges::Refusal::NONE)
  {
    context.status = Touchline::refusalText(refusal, context.engine,
                                            Touchline::playerName(lineup, out),
                                            Touchline::playerName(lineup, in));
    context.status_refused = true;
    return false;
  }
  context.status = fmt::sprintf(LOC("SUBSTITUTION_PLANNED"),
                                Touchline::playerName(lineup, in).c_str(),
                                Touchline::playerName(lineup, out).c_str());
  context.status_refused = false;
  return true;
}

bool MatchSubsPanel::substituteNow(const TouchlineContext& context,
                                   PlayerID out, PlayerID in)
{
  const auto team = context.controller.getTeamById(context.team_id);
  if (!team) return false;
  const Lineup& lineup = team->get().getLineup();
  MatchChanges::SubstitutionPlan once;
  if (const MatchChanges::Refusal refusal =
          once.add(context.engine, context.home, lineup.getReserves(), out, in);
      refusal != MatchChanges::Refusal::NONE)
  {
    context.status = Touchline::refusalText(refusal, context.engine,
                                            Touchline::playerName(lineup, out),
                                            Touchline::playerName(lineup, in));
    context.status_refused = true;
    return false;
  }
  const std::vector<MatchChanges::SubstitutionOutcome> outcomes =
      once.apply(context.engine, context.home, lineup.getReserves());
  report(context, lineup, outcomes);
  return !outcomes.empty() &&
         outcomes.front().refusal == MatchChanges::Refusal::NONE;
}

void MatchSubsPanel::confirm(const TouchlineContext& context)
{
  substitution_plan.confirm();
  if (!substitution_plan.isConfirmed()) return;
  if (MatchChanges::atStoppage(context.engine))
  {
    update(context);
    return;
  }
  context.status =
      fmt::sprintf(LOC("SUBSTITUTION_WAITING"),
                   static_cast<int>(substitution_plan.entries().size()));
  context.status_refused = false;
}

void MatchSubsPanel::update(const TouchlineContext& context)
{
  if (!substitution_plan.isConfirmed()) return;
  const auto team = context.controller.getTeamById(context.team_id);
  if (!team) return;
  const Lineup& lineup = team->get().getLineup();
  const std::vector<MatchChanges::SubstitutionOutcome> outcomes =
      substitution_plan.applyIfDue(context.engine, context.home,
                                   lineup.getReserves());
  if (!outcomes.empty()) report(context, lineup, outcomes);
}

void MatchSubsPanel::report(
    const TouchlineContext& context, const Lineup& lineup,
    const std::vector<MatchChanges::SubstitutionOutcome>& outcomes) const
{
  std::string done;
  std::string refused;
  for (const MatchChanges::SubstitutionOutcome& outcome : outcomes)
  {
    const std::string out = Touchline::playerName(lineup, outcome.change.out);
    const std::string in = Touchline::playerName(lineup, outcome.change.in);
    std::string& line =
        outcome.refusal == MatchChanges::Refusal::NONE ? done : refused;
    if (!line.empty()) line += ' ';
    line +=
        outcome.refusal == MatchChanges::Refusal::NONE
            ? fmt::sprintf(LOC("SUBSTITUTION_DONE"), in.c_str(), out.c_str())
            : Touchline::refusalText(outcome.refusal, context.engine, out, in);
  }
  context.status_refused = !refused.empty();
  if (done.empty())
    context.status = refused;
  else if (refused.empty())
    context.status = done;
  else
    context.status = done + ' ' + refused;
}

bool MatchSubsPanel::swapPositions(const TouchlineContext& context,
                                   PlayerID first, PlayerID second)
{
  const auto team = context.controller.getTeamById(context.team_id);
  swap_mode = false;
  selected_out = 0;
  if (!team) return false;
  const Lineup& lineup = team->get().getLineup();
  const std::string firstName = Touchline::playerName(lineup, first);
  const std::string secondName = Touchline::playerName(lineup, second);
  if (const auto slot = context.engine.getFormationSlot(second);
      first == second || !slot.has_value() ||
      !context.engine.getFormationSlot(first).has_value() ||
      !context.engine.movePlayerToSlot(first, *slot))
  {
    context.status = fmt::sprintf(LOC("SUBSTITUTION_SWAP_REFUSED"),
                                  firstName.c_str(), secondName.c_str());
    context.status_refused = true;
    return false;
  }
  context.status = fmt::sprintf(LOC("SUBSTITUTION_SWAPPED"), firstName.c_str(),
                                secondName.c_str());
  context.status_refused = false;
  return true;
}

void MatchSubsPanel::selectionChanged(const TouchlineContext& context)
{
  if (selected_out != 0 && selected_in != 0)
    plan(context, selected_out, selected_in);
}

void MatchSubsPanel::collectMarkers(const TouchlineContext& context)
{
  markers.clear();
  const std::vector<Vector2F> shape = context.engine.getFormation(context.home);
  const auto& stats = context.engine.getPlayerStats();
  for (const MatchPlayer& player : context.engine.getPlayers())
  {
    if (!player.player || !player.onPitch || player.isHomeTeam != context.home)
      continue;
    PitchMarker marker;
    marker.player = player.player;
    marker.id = player.player->getId();
    marker.goalkeeper = player.isGoalkeeper;
    if (player.isGoalkeeper)
    {
      marker.spot = {MatchTuning::Pitch::LINEUP_GOALKEEPER_X,
                     MatchTuning::Pitch::CENTRE};
    }
    else if (player.formationSlot >= 0 &&
             static_cast<std::size_t>(player.formationSlot) < shape.size())
    {
      marker.spot = shape[static_cast<std::size_t>(player.formationSlot)];
    }
    else
    {
      // Base positions are in match coordinates: the away side attacks
      // toward x = 0.
      marker.spot = context.home ? player.basePosition
                                 : Vector2F{1.0f - player.basePosition.x,
                                            1.0f - player.basePosition.y};
    }
    marker.condition = player.stamina;
    marker.rating = player.statsIndex < stats.size()
                        ? stats[player.statsIndex].rating
                        : 0.0f;
    marker.yellow_cards = player.yellowCards;
    marker.injured = player.isInjured;
    markers.push_back(marker);
  }
}

bool MatchSubsPanel::render(const TouchlineContext& context)
{
  std::array<char, 128> title{};
  const auto titleText =
      std::format("{}{}", LOC("SUBSTITUTION_TITLE"), WINDOW_ID);
  const auto titleResult = std::format_to_n(
      title.data(), static_cast<std::ptrdiff_t>(title.size() - 1), "{}",
      titleText);
  title[std::min(static_cast<std::size_t>(titleResult.size),
                 title.size() - 1)] = '\0';
  if (!Touchline::beginDialog(title.data(), popup_opened, DIALOG_WIDTH,
                              DIALOG_HEIGHT))
  {
    close_requested = false;
    return false;
  }

  const auto team = context.controller.getTeamById(context.team_id);
  if (!team || close_requested || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
  {
    close_requested = false;
    popup_opened = false;
    resetSelection();
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return false;
  }
  const Lineup& lineup = team->get().getLineup();
  collectMarkers(context);

  ImGui::BeginChild("##substitution_body",
                    ImVec2(0.0f, -Touchline::footerHeight()));
  renderUsage(context);
  if (const float width = ImGui::GetContentRegionAvail().x;
      width >= scaled(TWO_COLUMN_WIDTH))
  {
    const float spacing = ImGui::GetStyle().ItemSpacing.x * 2.0f;
    const float pitchWidth = std::floor((width - spacing) * PITCH_SHARE);
    // The plan and the advice stay in view under the pitch; the (long)
    // bench takes the other column.
    ImGui::BeginGroup();
    renderPitch(context, pitchWidth);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + pitchWidth);
    ImGui::BeginChild(
        "##plan_column", ImVec2(pitchWidth, 0.0f), ImGuiChildFlags_AutoResizeY,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
    renderPlan(context, lineup);
    renderSuggestions(context, lineup);
    ImGui::EndChild();
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SameLine(0.0f, spacing);
    ImGui::BeginGroup();
    renderBench(context, lineup, width - pitchWidth - spacing);
    ImGui::EndGroup();
  }
  else
  {
    renderPitch(context, width);
    renderPlan(context, lineup);
    renderSuggestions(context, lineup);
    renderBench(context, lineup, width);
  }
  ImGui::EndChild();

  renderFooter(context);
  const bool open = popup_opened;
  if (!open)
  {
    resetSelection();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
  return open;
}

void MatchSubsPanel::renderUsage(const TouchlineContext& context) const
{
  const Theme::Palette& palette = Theme::palette();
  const MatchEngine& engine = context.engine;
  const std::string usage = fmt::sprintf(
      LOC("SUBSTITUTION_USAGE"), engine.getSubstitutionsUsed(context.home),
      MatchChanges::maxSubstitutions(engine),
      engine.getSubstitutionWindowsUsed(context.home),
      MatchChanges::maxWindows(engine));
  ImGui::TextColored(palette.muted, "%s", usage.c_str());
  const MatchChanges::Refusal rules =
      MatchChanges::rulesRefusal(engine, context.home, 0);
  if (rules != MatchChanges::Refusal::NONE)
  {
    const std::string reason = Touchline::refusalText(rules, engine, {}, {});
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.warning, "%s", reason.c_str());
    ImGui::PopTextWrapPos();
  }
  else
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.faint, "%s", LOC("SUBSTITUTION_HELP"));
    ImGui::PopTextWrapPos();
  }
}

void MatchSubsPanel::renderPitch(const TouchlineContext& context, float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::sectionLabel(LOC("SUBSTITUTION_ON_PITCH"));
  // Swapping works on the selected player; it needs two outfield players.
  if (selected_out != 0 &&
      context.engine.getFormationSlot(selected_out).has_value())
  {
    ImGui::SameLine();
    if (UI::toggleButton(LOC("SUBSTITUTION_SWAP"), swap_mode, ImVec2(0, 0),
                         UI::ButtonSize::COMPACT))
      swap_mode = !swap_mode;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", LOC("SUBSTITUTION_SWAP_HINT"));
  }

  const auto team = context.controller.getTeamById(context.team_id);
  const auto benchPlayer = [&team](PlayerID id) -> const Player*
  {
    if (!team) return nullptr;
    for (const Player* reserve : team->get().getLineup().getReserves())
      if (reserve && reserve->getId() == id) return reserve;
    return nullptr;
  };
  // Short windows (or a big UI scale) keep the whole XI in view.
  const float height =
      std::min({width * PITCH_ASPECT, scaled(PITCH_MAX_HEIGHT),
                std::max(scaled(PITCH_MIN_HEIGHT),
                         ImGui::GetContentRegionAvail().y -
                             ImGui::GetFrameHeightWithSpacing())});
  const float pitchWidth = height / PITCH_ASPECT;
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  const ImVec2 min(cursor.x + (width - pitchWidth) * 0.5f, cursor.y);
  const ImVec2 max(min.x + pitchWidth, min.y + height);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawPitchMarkings(drawList, min, max);

  const float radius = scaled(MARKER_RADIUS);
  const float margin = radius * 1.6f;
  const float labelWidth = scaled(MARKER_LABEL_WIDTH);
  const float lineHeight = ImGui::GetTextLineHeight();
  const ImU32 kitText = textOn(context.kit);
  for (const PitchMarker& marker : markers)
  {
    const ImVec2 centre(
        min.x + margin + marker.spot.x * (pitchWidth - 2.0f * margin),
        min.y + margin + marker.spot.y * (height - 2.0f * margin - lineHeight));
    const bool selected = selected_out == marker.id;
    const PlayerID replacement = substitution_plan.replacementFor(marker.id);

    ImGui::PushID(static_cast<int>(marker.id));
    ImGui::SetCursorScreenPos(
        ImVec2(centre.x - labelWidth * 0.5f, centre.y - radius));
    const bool clicked = ImGui::InvisibleButton(
        "##marker", ImVec2(labelWidth, radius * 2.0f + lineHeight * 1.6f));
    const bool hovered = ImGui::IsItemHovered();
    if (clicked)
    {
      if (swap_mode && selected_out != 0 && selected_out != marker.id)
      {
        swapPositions(context, selected_out, marker.id);
      }
      else
      {
        selected_out = selected ? 0 : marker.id;
        swap_mode = false;
        selectionChanged(context);
      }
    }
    if (ImGui::BeginDragDropSource())
    {
      ImGui::SetDragDropPayload(PITCH_PAYLOAD, &marker.id, sizeof(marker.id));
      ImGui::TextUnformatted(marker.player->getLastName().c_str());
      ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget())
    {
      if (const ImGuiPayload* payload =
              ImGui::AcceptDragDropPayload(BENCH_PAYLOAD))
      {
        const PlayerID incoming = *static_cast<const PlayerID*>(payload->Data);
        plan(context, marker.id, incoming);
      }
      else if (const ImGuiPayload* moved =
                   ImGui::AcceptDragDropPayload(PITCH_PAYLOAD))
      {
        const PlayerID other = *static_cast<const PlayerID*>(moved->Data);
        if (other != marker.id) swapPositions(context, other, marker.id);
      }
      ImGui::EndDragDropTarget();
    }
    if (hovered)
    {
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(marker.player->getName().c_str());
      ImGui::TextColored(palette.muted, "%s  ·  %s %.0f%%  ·  %s %.1f",
                         RoleUtils::shortName(marker.player->getRole()),
                         LOC("ROSTER_COL_CONDITION"),
                         static_cast<double>(marker.condition * 100.0f),
                         LOC("STATS_COL_RATING"),
                         static_cast<double>(marker.rating));
      if (marker.injured)
        ImGui::TextColored(palette.negative, "%s", LOC("SUBSTITUTION_INJURED"));
      if (marker.yellow_cards > 0)
        ImGui::TextColored(palette.warning, "%s", LOC("SUBSTITUTION_BOOKED"));
      ImGui::TextColored(palette.faint, "%s", LOC("SUBSTITUTION_MARKER_HINT"));
      ImGui::EndTooltip();
    }
    ImGui::PopID();

    // Shirt with the position code, selection or planned-change ring.
    drawList->AddCircleFilled(centre, radius, context.kit);
    drawList->AddCircle(centre, radius, IM_COL32(255, 255, 255, 220), 0,
                        scaled(1.5f));
    if (selected || hovered)
      drawList->AddCircle(centre, radius + scaled(3.0f),
                          Theme::toU32(palette.accent), 0, scaled(2.5f));
    if (replacement != 0)
      drawList->AddCircle(centre, radius + scaled(3.0f),
                          Theme::toU32(palette.warning), 0, scaled(2.5f));
    {
      Theme::ScopedText caption(Theme::Text::CAPTION);
      const char* code = RoleUtils::shortName(marker.player->getRole());
      const ImVec2 codeSize = ImGui::CalcTextSize(code);
      drawList->AddText(
          ImVec2(centre.x - codeSize.x * 0.5f, centre.y - codeSize.y * 0.5f),
          kitText, code);
      // Rating beside the shirt.
      std::array<char, 8> rating{};
      const auto formatted = std::format_to_n(
          rating.data(), static_cast<std::ptrdiff_t>(rating.size() - 1),
          "{:.1f}", static_cast<double>(marker.rating));
      rating[std::min(static_cast<std::size_t>(formatted.size),
                      rating.size() - 1)] = '\0';
      const ImVec2 ratingAt(centre.x + radius + scaled(3.0f),
                            centre.y - radius);
      const ImVec2 ratingSize = ImGui::CalcTextSize(rating.data());
      drawList->AddRectFilled(ImVec2(ratingAt.x - scaled(2.0f), ratingAt.y),
                              ImVec2(ratingAt.x + ratingSize.x + scaled(2.0f),
                                     ratingAt.y + ratingSize.y),
                              IM_COL32(0, 0, 0, 150), scaled(3.0f));
      drawList->AddText(ratingAt, IM_COL32(255, 255, 255, 235), rating.data());
      // Name and condition under the shirt.
      const std::string& lastName = marker.player->getLastName();
      const float nameWidth =
          std::min(ImGui::CalcTextSize(lastName.c_str()).x, labelWidth);
      const float nameTop = centre.y + radius + scaled(2.0f);
      drawList->AddRectFilled(
          ImVec2(centre.x - nameWidth * 0.5f - scaled(3.0f), nameTop),
          ImVec2(centre.x + nameWidth * 0.5f + scaled(3.0f),
                 nameTop + ImGui::GetTextLineHeight()),
          IM_COL32(0, 0, 0, 110), scaled(3.0f));
      UI::drawTextFitted(drawList, ImVec2(centre.x - nameWidth * 0.5f, nameTop),
                         IM_COL32(255, 255, 255, 240), lastName, labelWidth);
      conditionBar(drawList,
                   ImVec2(centre.x - scaled(CONDITION_BAR_WIDTH) * 0.5f,
                          nameTop + ImGui::GetTextLineHeight() + scaled(2.0f)),
                   scaled(CONDITION_BAR_WIDTH), scaled(CONDITION_BAR_HEIGHT),
                   marker.condition);
      // The planned replacement under the player, after a small arrow.
      if (replacement != 0)
        if (const Player* incoming = benchPlayer(replacement))
        {
          const std::string& inName = incoming->getLastName();
          const float arrow = scaled(4.0f);
          const float inWidth = std::min(ImGui::CalcTextSize(inName.c_str()).x,
                                         labelWidth - 3.0f * arrow);
          const float top = nameTop + ImGui::GetTextLineHeight() +
                            scaled(CONDITION_BAR_HEIGHT) + scaled(4.0f);
          const float left = centre.x - (inWidth + 3.0f * arrow) * 0.5f;
          drawList->AddRectFilled(
              ImVec2(left - scaled(3.0f), top),
              ImVec2(left + inWidth + 3.0f * arrow + scaled(3.0f),
                     top + ImGui::GetTextLineHeight()),
              IM_COL32(0, 0, 0, 150), scaled(3.0f));
          const float mid = top + ImGui::GetTextLineHeight() * 0.5f;
          drawList->AddTriangleFilled(ImVec2(left, mid + arrow),
                                      ImVec2(left + 2.0f * arrow, mid + arrow),
                                      ImVec2(left + arrow, mid - arrow),
                                      Theme::toU32(palette.warning));
          UI::drawTextFitted(drawList, ImVec2(left + 3.0f * arrow, top),
                             Theme::toU32(palette.warning), inName, inWidth);
        }
    }
    // Booking and injury marks on the shirt's shoulder.
    const ImVec2 corner(centre.x - radius - scaled(2.0f),
                        centre.y - radius - scaled(2.0f));
    if (marker.yellow_cards > 0)
      drawList->AddRectFilled(
          corner, ImVec2(corner.x + scaled(6.0f), corner.y + scaled(8.0f)),
          IM_COL32(245, 200, 40, 255), scaled(1.0f));
    if (marker.injured)
    {
      const ImVec2 cross(corner.x + scaled(12.0f), corner.y + scaled(4.0f));
      const float arm = scaled(4.0f);
      const ImU32 red = IM_COL32(230, 60, 60, 255);
      drawList->AddLine(ImVec2(cross.x - arm, cross.y),
                        ImVec2(cross.x + arm, cross.y), red, scaled(2.5f));
      drawList->AddLine(ImVec2(cross.x, cross.y - arm),
                        ImVec2(cross.x, cross.y + arm), red, scaled(2.5f));
    }
  }
  ImGui::SetCursorScreenPos(cursor);
  ImGui::Dummy(ImVec2(width, height));
}

void MatchSubsPanel::renderBench(const TouchlineContext& context,
                                 const Lineup& lineup, float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::sectionLabel(LOC("SUBSTITUTION_BENCH"));
  const StatsConfig& config = context.controller.getStatsConfig();
  PlayerRole position = PlayerRole::UNKNOWN;
  for (const PitchMarker& marker : markers)
    if (marker.id == selected_out) position = marker.player->getRole();
  const bool showFit = position != PlayerRole::UNKNOWN;

  if (const ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_BordersInnerH |
                                    ImGuiTableFlags_SizingFixedFit;
      !ImGui::BeginTable("##substitution_bench", showFit ? 5 : 4, flags,
                         ImVec2(width, 0.0f)))
    return;
  ImGui::TableSetupColumn(LOC("MAIN_GAME_POS"));
  ImGui::TableSetupColumn(LOC("ROSTER_COL_NAME"),
                          ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn(LOC("ROSTER_COL_CONDITION"));
  ImGui::TableSetupColumn(LOC("MAIN_GAME_OVR"));
  if (showFit) ImGui::TableSetupColumn(LOC("SUBSTITUTION_FIT_HEADER"));
  UI::staticHeadersRow();

  for (const Player* reserve : lineup.getReserves())
  {
    if (!reserve) continue;
    const PlayerID id = reserve->getId();
    // Players who already took part (or were replaced) cannot come on.
    const bool used = context.engine.findPlayerStats(id) != nullptr;
    const bool planned = substitution_plan.involves(id);
    const bool selected = selected_in == id;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::PushID(static_cast<int>(id));
    ImGui::BeginDisabled(used || planned);
    if (ImGui::Selectable(RoleUtils::shortName(reserve->getRole()), selected,
                          ImGuiSelectableFlags_SpanAllColumns |
                              ImGuiSelectableFlags_AllowOverlap))
    {
      selected_in = selected ? 0 : id;
      selectionChanged(context);
    }
    if (!used && !planned && ImGui::BeginDragDropSource())
    {
      ImGui::SetDragDropPayload(BENCH_PAYLOAD, &id, sizeof(id));
      ImGui::TextUnformatted(reserve->getName().c_str());
      ImGui::EndDragDropSource();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
        (used || planned))
      ImGui::SetTooltip("%s", used ? LOC("SUBSTITUTION_ALREADY_PLAYED")
                                   : LOC("SUBSTITUTION_IN_PLAN"));
    ImGui::PopID();

    ImGui::TableNextColumn();
    UI::textFitted(reserve->getName(), ImGui::GetContentRegionAvail().x,
                   used || planned ? palette.faint : palette.text);
    ImGui::TableNextColumn();
    const float condition = reserve->getDynamics().condition / 100.0f;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float barHeight = scaled(CONDITION_BAR_HEIGHT) * 1.5f;
    conditionBar(
        ImGui::GetWindowDrawList(),
        ImVec2(at.x, at.y + (ImGui::GetTextLineHeight() - barHeight) * 0.5f),
        scaled(BENCH_BAR_WIDTH), barHeight, condition);
    ImGui::Dummy(ImVec2(scaled(BENCH_BAR_WIDTH), ImGui::GetTextLineHeight()));
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%.0f%%", static_cast<double>(condition * 100.0f));
    ImGui::TableNextColumn();
    ImGui::Text("%.0f", reserve->getOverall(config));
    if (showFit)
    {
      ImGui::TableNextColumn();
      float fit = 0.0f;
      if (reserve->getRole() == PlayerRole::GK || position == PlayerRole::GK)
        fit = reserve->getRole() == position ? 1.0f : 0.0f;
      else
        fit = MatchChanges::positionFit(reserve->getRole(), position);
      ImGui::TextColored(fit >= 0.7f ? palette.text : palette.muted, "%s",
                         fitLabel(fit));
    }
  }
  ImGui::EndTable();
}

void MatchSubsPanel::renderPlan(const TouchlineContext& context,
                                const Lineup& lineup)
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  UI::sectionLabel(LOC("SUBSTITUTION_PLAN"));
  const auto entries = substitution_plan.entries();
  if (entries.empty())
  {
    ImGui::TextColored(palette.faint, "%s", LOC("SUBSTITUTION_PLAN_EMPTY"));
    return;
  }
  std::optional<std::size_t> removed;
  const float removeWidth =
      UI::buttonWidth(LOC("COMPARE_REMOVE"), UI::ButtonSize::COMPACT);
  for (std::size_t index = 0; index < entries.size(); ++index)
  {
    ImGui::PushID(static_cast<int>(index));
    const std::string line =
        fmt::sprintf(LOC("SUBSTITUTION_PLAN_ROW"),
                     Touchline::playerName(lineup, entries[index].in).c_str(),
                     Touchline::playerName(lineup, entries[index].out).c_str());
    ImGui::AlignTextToFramePadding();
    UI::textFitted(line,
                   ImGui::GetContentRegionAvail().x - removeWidth -
                       ImGui::GetStyle().ItemSpacing.x,
                   palette.text);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - removeWidth);
    if (UI::secondaryButton(LOC("COMPARE_REMOVE"), ImVec2(removeWidth, 0.0f),
                            UI::ButtonSize::COMPACT))
      removed = index;
    ImGui::PopID();
  }
  if (removed.has_value()) substitution_plan.remove(*removed);
  if (substitution_plan.isConfirmed())
    ImGui::TextColored(palette.info, "%s", LOC("SUBSTITUTION_PLAN_WAITING"));
  (void)context;
}

void MatchSubsPanel::renderSuggestions(const TouchlineContext& context,
                                       const Lineup& lineup)
{
  const Theme::Palette& palette = Theme::palette();
  if (MatchChanges::rulesRefusal(
          context.engine, context.home,
          static_cast<int>(substitution_plan.entries().size())) !=
      MatchChanges::Refusal::NONE)
    return;
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  UI::sectionLabel(LOC("SUBSTITUTION_ASSISTANT"));
  // Re-evaluated every couple of match seconds or when the squad changes,
  // not every frame.
  const std::uint64_t key =
      context.engine.getSimulatedSteps() / SUGGESTION_REFRESH_STEPS;
  if (const std::size_t changes =
          substitution_plan.entries().size() * 16 +
          static_cast<std::size_t>(
              context.engine.getSubstitutionsUsed(context.home));
      key != suggestions_key || changes != suggestions_changes)
  {
    suggestions = MatchChanges::suggestSubstitutions(
        context.engine, context.home, lineup.getReserves(), substitution_plan,
        context.controller.getStatsConfig(), SUGGESTION_COUNT);
    suggestions_key = key;
    suggestions_changes = changes;
  }
  if (suggestions.empty())
  {
    ImGui::TextColored(palette.faint, "%s", LOC("SUBSTITUTION_ASSISTANT_NONE"));
    return;
  }
  const float addWidth =
      UI::buttonWidth(LOC("SUBSTITUTION_ACCEPT"), UI::ButtonSize::COMPACT);
  std::optional<MatchChanges::Suggestion> accepted;
  for (const MatchChanges::Suggestion& suggestion : suggestions)
  {
    std::string reasons;
    const auto addReason = [&reasons](std::string_view text)
    {
      if (!reasons.empty()) reasons += ", ";
      reasons.append(text);
    };
    const std::byte reasonFlags = static_cast<std::byte>(suggestion.reasons);
    if ((reasonFlags & static_cast<std::byte>(MatchChanges::SUGGEST_INJURED)) !=
        std::byte{})
      addReason(LOC("SUBSTITUTION_REASON_INJURED"));
    if ((reasonFlags & static_cast<std::byte>(MatchChanges::SUGGEST_TIRED)) !=
        std::byte{})
      addReason(fmt::sprintf(
          LOC("SUBSTITUTION_REASON_TIRED"),
          static_cast<int>(std::lround(suggestion.condition * 100.0f))));
    if ((reasonFlags & static_cast<std::byte>(MatchChanges::SUGGEST_BOOKED)) !=
        std::byte{})
      addReason(LOC("SUBSTITUTION_REASON_BOOKED"));
    const std::string line = fmt::sprintf(
        LOC("SUBSTITUTION_SUGGESTION"),
        Touchline::playerName(lineup, suggestion.out).c_str(), reasons.c_str(),
        Touchline::playerName(lineup, suggestion.in).c_str());
    ImGui::PushID(static_cast<int>(suggestion.out));
    ImGui::AlignTextToFramePadding();
    UI::textFitted(line,
                   ImGui::GetContentRegionAvail().x - addWidth -
                       ImGui::GetStyle().ItemSpacing.x,
                   palette.text);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - addWidth);
    if (UI::secondaryButton(LOC("SUBSTITUTION_ACCEPT"), ImVec2(addWidth, 0.0f),
                            UI::ButtonSize::COMPACT))
      accepted = suggestion;
    ImGui::PopID();
  }
  if (accepted) plan(context, accepted->out, accepted->in);
}

void MatchSubsPanel::renderFooter(const TouchlineContext& context)
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::Separator();
  // Status on the left, the pause setting on the right of the same row.
  const float settingWidth =
      ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
      ImGui::CalcTextSize(LOC("MATCH_PAUSE_FOR_CHANGES")).x;
  const float rowStart = ImGui::GetCursorPosX();
  const float available = ImGui::GetContentRegionAvail().x;
  ImGui::AlignTextToFramePadding();
  UI::textFitted(context.status,
                 std::max(0.0f, available - settingWidth -
                                    ImGui::GetStyle().ItemSpacing.x),
                 context.status_refused ? palette.negative : palette.positive);
  ImGui::SameLine(rowStart + std::max(0.0f, available - settingWidth));
  Touchline::pauseSetting();

  const auto planned = static_cast<int>(substitution_plan.entries().size());
  const bool waiting = substitution_plan.isConfirmed();
  const char* confirmKey = "SUBSTITUTION_MAKE_AT_STOPPAGE";
  if (waiting)
    confirmKey = "SUBSTITUTION_WAITING_BUTTON";
  else if (MatchChanges::atStoppage(context.engine))
    confirmKey = "SUBSTITUTION_MAKE_NOW";
  // Stable id while the count in the label changes.
  const std::string confirmLabel =
      fmt::sprintf(LOC(confirmKey), planned) + "###substitution_confirm";
  ImGui::BeginDisabled(planned == 0 || waiting);
  if (UI::primaryButton(confirmLabel.c_str())) confirm(context);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(planned == 0);
  if (UI::secondaryButton(LOC("SUBSTITUTION_CLEAR"))) substitution_plan.clear();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (UI::secondaryButton(LOC("SUBSTITUTION_CLOSE"))) popup_opened = false;
}

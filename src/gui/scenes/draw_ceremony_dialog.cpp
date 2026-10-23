// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/draw_ceremony_dialog.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/scenes/management_scene.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/inbox.h"

namespace
{
constexpr const char* POPUP_ID = "###draw_ceremony";
constexpr float DIALOG_WIDTH = 720.0f;
constexpr float DIALOG_HEIGHT = 620.0f;
constexpr float VIEWPORT_WIDTH_SHARE = 0.94f;
constexpr float VIEWPORT_HEIGHT_SHARE = 0.88f;
constexpr float ROW_HEIGHT = 46.0f;
constexpr float FOCUS_BAR_WIDTH = 3.0f;
/** Seconds a newly drawn tie takes to fade in. */
constexpr float FADE_SECONDS = 0.35f;
constexpr std::array<float, 3> SPEEDS = {1.0f, 2.0f, 4.0f};
constexpr std::array<const char*, 3> SPEED_LABELS = {"1x", "2x", "4x"};

float scaled(float value) { return value * Theme::scale(); }

std::string teamName(const GameController& controller, TeamID id)
{
  const auto team = controller.getTeamById(id);
  return team ? team->get().getName() : std::string("-");
}
}  // namespace

void DrawCeremonyDialog::open(const DrawCeremony& ceremony,
                              const GameController& controller)
{
  rows.clear();
  rows.reserve(ceremony.reveals.size());
  league_phase = ceremony.kind == DrawCeremony::Kind::ContinentalLeaguePhase;
  for (const DrawReveal& reveal : ceremony.reveals)
  {
    Row row;
    row.home_id = reveal.home_id;
    row.away_id = reveal.away_id;
    row.home = teamName(controller, reveal.home_id);
    row.away = teamName(controller, reveal.away_id);
    row.when =
        reveal.second_leg
            ? formatLocalized("DRAW_LEGS", {reveal.date.toString(),
                                            reveal.second_leg->toString()})
            : Format::date(reveal.date);
    if (reveal.pot > 0)
      row.pot = formatLocalized("DRAW_POT", {std::to_string(reveal.pot)});
    row.focus =
        ceremony.focus_team != 0 && (reveal.home_id == ceremony.focus_team ||
                                     reveal.away_id == ceremony.focus_team);
    rows.push_back(std::move(row));
  }
  title = formatLocalized("DRAW_TITLE",
                          {ceremony.competition_name, ceremony.round_name});
  subtitle =
      league_phase
          ? formatLocalized("DRAW_SUBTITLE_LEAGUE_PHASE",
                            {ceremony.drawn_on.toString(),
                             teamName(controller, ceremony.focus_team)})
          : formatLocalized("DRAW_SUBTITLE", {ceremony.drawn_on.toString(),
                                              std::to_string(rows.size())});
  elapsed = 0.0f;
  skipped = false;
  last_shown = 0;
  open_requested = true;
}

std::size_t DrawCeremonyDialog::shownCount() const
{
  if (skipped) return rows.size();
  return DrawCeremonies::shownAt(elapsed,
                                 SPEEDS[static_cast<std::size_t>(speed_index)],
                                 rows.size(), Theme::reducedMotion());
}

void DrawCeremonyDialog::renderRow(GUIView* view, const Row& row,
                                   std::size_t index, float alpha)
{
  const Theme::Palette& palette = Theme::palette();
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = scaled(ROW_HEIGHT);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
  if (index % 2 == 1)
    drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                            Theme::toU32(palette.raised), scaled(4.0f));
  if (row.focus)
    drawList->AddRectFilled(
        origin, ImVec2(origin.x + scaled(FOCUS_BAR_WIDTH), origin.y + height),
        Theme::toU32(palette.accent));

  ImGui::PushID(static_cast<int>(index));
  const float padding = scaled(Theme::Space::M);
  const float numberWidth = scaled(34.0f);
  const float whenWidth = std::min(scaled(210.0f), width * 0.32f);
  const float potWidth = league_phase ? scaled(64.0f) : 0.0f;
  const float clubsWidth =
      std::max(0.0f, width - numberWidth - whenWidth - potWidth - 2 * padding);
  const float textY = origin.y + (height - ImGui::GetTextLineHeight()) * 0.5f;

  ImGui::SetCursorScreenPos(ImVec2(origin.x + padding, textY));
  ImGui::TextColored(palette.faint, "%zu", index + 1);
  float x = origin.x + padding + numberWidth;
  if (league_phase)
  {
    ImGui::SetCursorScreenPos(ImVec2(x, textY));
    UI::textFitted(row.pot, potWidth, palette.muted);
    x += potWidth;
  }
  const float versus = ImGui::CalcTextSize(" v ").x;
  const float side = std::max(0.0f, (clubsWidth - versus) * 0.5f);
  const auto club =
      [&](TeamID id, const std::string& name, float left, const char* button_id)
  {
    ImGui::SetCursorScreenPos(ImVec2(left, textY));
    ImGui::PushID(button_id);
    if (ImGui::CalcTextSize(name.c_str()).x > side)
    {
      UI::textFitted(name, side, row.focus ? palette.text : palette.muted);
    }
    else if (UI::link(name.c_str(), "##club"))
    {
      ImGui::CloseCurrentPopup();
      visible = false;
      Navigation::openClub(view, id);
    }
    ImGui::PopID();
  };
  club(row.home_id, row.home, x, "home");
  ImGui::SetCursorScreenPos(ImVec2(x + side, textY));
  ImGui::TextColored(palette.faint, " v ");
  club(row.away_id, row.away, x + side + versus, "away");

  ImGui::SetCursorScreenPos(
      ImVec2(origin.x + width - padding - whenWidth, textY));
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    UI::textFitted(row.when, whenWidth, palette.muted);
  }
  ImGui::PopID();
  ImGui::PopStyleVar();
  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
  ImGui::Dummy(ImVec2(width, 0.0f));
}

void DrawCeremonyDialog::render(GUIView* view)
{
  if (open_requested)
  {
    open_requested = false;
    ImGui::OpenPopup(POPUP_ID);
  }
  visible = ImGui::IsPopupOpen(POPUP_ID);
  if (!visible) return;

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 size(std::min(scaled(DIALOG_WIDTH),
                             viewport->WorkSize.x * VIEWPORT_WIDTH_SHARE),
                    std::min(scaled(DIALOG_HEIGHT),
                             viewport->WorkSize.y * VIEWPORT_HEIGHT_SHARE));
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);
  if (!ImGui::BeginPopupModal(POPUP_ID, nullptr,
                              ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoResize |
                                  ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse |
                                  ImGuiWindowFlags_NoSavedSettings))
  {
    visible = false;
    return;
  }
  const Theme::Palette& palette = Theme::palette();
  const bool reduced = Theme::reducedMotion();
  elapsed += ImGui::GetIO().DeltaTime;
  const std::size_t shown = shownCount();
  const bool done = shown >= rows.size();

  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    UI::textFitted(title, ImGui::GetContentRegionAvail().x, palette.text);
  }
  UI::textFitted(subtitle, ImGui::GetContentRegionAvail().x, palette.muted);
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));

  // Speed and skip while the draw runs; Reduced motion has nothing to wait.
  if (!done && !reduced)
  {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(palette.muted, "%s", LOC("DRAW_SPEED"));
    ImGui::SameLine();
    UI::segmented("##draw_speed", speed_index, SPEED_LABELS, scaled(150.0f));
    const char* skip = LOC("DRAW_REVEAL_ALL");
    UI::sameLineIfFits(UI::buttonWidth(skip));
    if (UI::secondaryButton(skip)) skipped = true;
  }
  else
  {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(palette.muted, "%s", LOC("DRAW_COMPLETE"));
  }
  ImGui::Separator();

  // The list is the dialog's only scrolling area.
  if (const float footer =
          UI::buttonHeight() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
      ImGui::BeginChild(
          "##draw_list",
          ImVec2(0.0f, std::max(scaled(80.0f),
                                ImGui::GetContentRegionAvail().y - footer)),
          ImGuiChildFlags_None))
  {
    for (std::size_t index = 0; index < shown && index < rows.size(); ++index)
    {
      float alpha = 1.0f;
      if (!reduced && !skipped && index + 1 == shown && !done)
      {
        const float since =
            elapsed - static_cast<float>(index) *
                          DrawCeremonies::REVEAL_SECONDS /
                          SPEEDS[static_cast<std::size_t>(speed_index)];
        alpha = std::clamp(since / FADE_SECONDS, 0.2f, 1.0f);
      }
      renderRow(view, rows[index], index, alpha);
      if (!visible) break;
    }
    if (shown > last_shown && !reduced && !skipped) ImGui::SetScrollHereY(1.0f);
    if (rows.empty())
      ImGui::TextColored(palette.faint, "%s", LOC("DRAW_EMPTY"));
  }
  ImGui::EndChild();
  last_shown = shown;

  if (!visible)
  {
    ImGui::EndPopup();
    return;
  }
  const char* close = LOC("TALK_CLOSE");
  const ImVec2 closeSize(
      std::min(scaled(150.0f), ImGui::GetContentRegionAvail().x), 0.0f);
  ImGui::SetCursorPosX(
      ImGui::GetCursorPosX() +
      std::max(0.0f, ImGui::GetContentRegionAvail().x - closeSize.x));
  if ((done ? UI::primaryButton(close, closeSize)
            : UI::secondaryButton(close, closeSize)) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape, false))
  {
    ImGui::CloseCurrentPopup();
    visible = false;
  }
  ImGui::EndPopup();
}

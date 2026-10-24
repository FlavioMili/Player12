// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/welcome_tour.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "gui/input_actions.h"
#include "gui/view_models/competition_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/board.h"
#include "model/competition.h"
#include "model/inbox.h"
#include "model/next_action.h"

namespace
{
constexpr const char* POPUP_ID = "###welcome_tour";
constexpr float DIALOG_WIDTH = 560.0f;

constexpr std::array<const char*, WelcomeTour::PAGE_COUNT> PAGE_TITLES = {
    "WELCOME_PAGE_CLUB", "WELCOME_PAGE_BOARD", "WELCOME_PAGE_NEXT",
    "WELCOME_PAGE_CONTINUE"};

/** One dot per page; the current one filled (shape, not only colour). */
void pageDots(std::size_t current)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const float radius = 4.0f * scale;
  const float gap = 14.0f * scale;
  const ImVec2 start = ImGui::GetCursorScreenPos();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  for (std::size_t index = 0; index < WelcomeTour::PAGE_COUNT; ++index)
  {
    const ImVec2 centre(start.x + radius + static_cast<float>(index) * gap,
                        start.y + ImGui::GetFrameHeight() * 0.5f);
    if (index == current)
      drawList->AddCircleFilled(centre, radius, Theme::toU32(palette.text));
    else
      drawList->AddCircle(centre, radius, Theme::toU32(palette.muted), 0,
                          1.5f * scale);
  }
  ImGui::Dummy(ImVec2(2.0f * radius + gap * (WelcomeTour::PAGE_COUNT - 1),
                      ImGui::GetFrameHeight()));
}

std::string shortcutLabel(std::string_view id)
{
  const auto action = Input::registry().find(id);
  return action ? Input::registry().label(*action) : std::string("-");
}
}  // namespace

void WelcomeTour::open(const GameController& controller)
{
  const auto managed = controller.getManagedTeam();
  if (!managed) return;
  const Team& club = managed->get();
  club_name = club.getName();
  const auto league = controller.getLeagueById(club.getLeagueId());
  const std::string leagueName =
      league ? Competitions::leagueName(league->get()) : std::string();
  const auto& squad = controller.getPlayersForTeam(club.getId());
  bodies[0] = formatLocalized(
      "WELCOME_CLUB_BODY",
      {club_name, leagueName, std::to_string(squad.size()),
       Format::money(controller.transferBudgetForTeam(club.getId()))});

  const BoardState& board = controller.getBoardState();
  bodies[1] = formatLocalized(
      "WELCOME_BOARD_BODY",
      {std::string("@") + BoardModel::objectiveKey(board.objective),
       std::to_string(board.expected_position),
       std::to_string(std::max(board.target_position, board.expected_position)),
       std::to_string(static_cast<int>(std::lround(board.confidence)))});

  if (const auto fixture =
          CompetitionView::nextFixture(controller, club.getId()))
  {
    const bool home = fixture->home_id == club.getId();
    bodies[2] = formatLocalized(
        "WELCOME_NEXT_BODY",
        {home ? fixture->away_name : fixture->home_name,
         std::string("@") + (home ? "WELCOME_AT_HOME" : "WELCOME_AWAY"),
         Format::date(fixture->date),
         std::string("@") + CompetitionView::matchTypeKey(fixture->type)});
  }
  else
  {
    bodies[2] = LOC("WELCOME_NEXT_NONE");
  }
  next_action_title.clear();
  next_action_reason.clear();
  if (const auto actions = controller.getNextActions(1); !actions.empty())
  {
    next_action_title =
        formatLocalized(actions.front().title.key, actions.front().title.args);
    next_action_reason = formatLocalized(actions.front().reason.key,
                                         actions.front().reason.args);
  }

  bodies[3] = formatLocalized("WELCOME_CONTINUE_BODY",
                              {shortcutLabel(Input::Ids::CAREER_CONTINUE),
                               shortcutLabel(Input::Ids::NAV_PALETTE),
                               shortcutLabel(Input::Ids::CAREER_HELP)});

  current_page = 0;
  visible = true;
  open_requested = true;
}

void WelcomeTour::next()
{
  if (current_page + 1 < PAGE_COUNT)
    ++current_page;
  else
    close();
}

void WelcomeTour::back()
{
  if (current_page > 0) --current_page;
}

void WelcomeTour::close() { visible = false; }

void WelcomeTour::render()
{
  if (!visible) return;
  if (open_requested)
  {
    open_requested = false;
    ImGui::OpenPopup(POPUP_ID);
  }
  const float scale = Theme::scale();
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float width =
      std::min(DIALOG_WIDTH * scale,
               viewport->WorkSize.x - 2.0f * Theme::Space::L * scale);
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  // Fixed width, height fitted to the page every frame (setting the size
  // each frame would let the auto-fit only grow, leaving a gap after a long
  // page).
  ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f),
                                      ImVec2(width, FLT_MAX));
  ImGui::PushStyleVar(
      ImGuiStyleVar_WindowPadding,
      ImVec2(Theme::Space::XL * scale, Theme::Space::L * scale));
  const std::string title =
      formatLocalized("WELCOME_TITLE", {club_name}) + POPUP_ID;
  const bool shown = ImGui::BeginPopupModal(
      title.c_str(), nullptr,
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
  ImGui::PopStyleVar();
  if (!shown)
  {
    // Closed from outside (another modal took over): the tour is over.
    visible = false;
    return;
  }

  const Theme::Palette& palette = Theme::palette();
  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    ImGui::TextUnformatted(LOC(PAGE_TITLES[current_page]));
  }
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
  // An explicit wrap width: wrapping at the window edge would feed back
  // into the auto-fitted size.
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width -
                         2.0f * Theme::Space::XL * scale);
  ImGui::TextUnformatted(bodies[current_page].c_str());
  if (current_page == 2 && !next_action_title.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
    ImGui::TextColored(palette.muted, "%s", LOC("WELCOME_NEXT_ACTION"));
    ImGui::TextUnformatted(next_action_title.c_str());
    Theme::ScopedText small(Theme::Text::SMALL);
    ImGui::TextColored(palette.muted, "%s", next_action_reason.c_str());
  }
  if (current_page == 2)
  {
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
    ImGui::TextColored(palette.muted, "%s", LOC("WELCOME_CHECKLIST_HINT"));
  }
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::M * scale));
  ImGui::Separator();

  pageDots(current_page);
  const bool last = current_page + 1 == PAGE_COUNT;
  const char* nextLabel = LOC(last ? "WELCOME_START" : "WELCOME_NEXT");
  const char* backLabel = LOC("WELCOME_BACK");
  const char* skipLabel = LOC("WELCOME_SKIP");
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  float buttons = UI::buttonWidth(nextLabel) + spacing;
  if (current_page > 0) buttons += UI::buttonWidth(backLabel) + spacing;
  if (!last) buttons += UI::buttonWidth(skipLabel) + spacing;
  ImGui::SameLine(std::max(ImGui::GetCursorPosX(),
                           ImGui::GetContentRegionMax().x - buttons + spacing));
  if (!last)
  {
    if (UI::secondaryButton(skipLabel)) close();
    ImGui::SameLine();
  }
  if (current_page > 0)
  {
    if (UI::secondaryButton(backLabel)) back();
    ImGui::SameLine();
  }
  if (UI::primaryButton(nextLabel)) next();
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(-1);
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) close();

  if (!visible) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

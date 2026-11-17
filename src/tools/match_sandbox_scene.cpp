// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_scene.h"

#include <SDL3/SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/scenes/match_scene.h"
#include "gui/view_models/formation.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/league.h"
#include "model/lineup.h"
#include "model/match_engine.h"
#include "model/opposition_report.h"
#include "model/player.h"
#include "model/role_utils.h"
#include "model/strategy.h"
#include "model/tactics.h"
#include "model/team.h"
#include "tools/lab_fixtures.h"
#include "tools/match_sandbox_bench_scene.h"
#include "tools/match_sandbox_contact_view.h"
#include "tools/match_sandbox_drill_scene.h"
#include "tools/match_sandbox_measure_scene.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_pickers.h"
#include "tools/match_sandbox_session_scene.h"
#include "tools/match_sandbox_setup_ui.h"
#include "tools/match_sandbox_shot_map.h"
#include "tools/match_sandbox_test_teams.h"
#include "tools/match_sandbox_training_scene.h"
#include "tools/match_sandbox_true_view.h"
#include "tools/match_sandbox_typography.h"
#include "tools/match_sandbox_xg_map.h"
#include "tools/match_sandbox_zone_recording.h"
#include "tools/match_sandbox_zone_replay_scene.h"

namespace
{
constexpr std::size_t HOME = 0;
constexpr std::size_t AWAY = 1;
constexpr float SLIDER_LABEL_WIDTH = 150.0F;
constexpr float OVERALL_COLUMN_WIDTH = 34.0F;
constexpr float POSITION_COLUMN_WIDTH = 38.0F;
/** Lineup coordinates closer than this are the same formation slot. */
constexpr float SLOT_TOLERANCE = 0.02F;

struct SliderInfo
{
  const char* key;
  float StrategySliders::* value;
  const char* helpKey;
};

// The five instructions, labelled as on the tactics screen.
constexpr std::array<SliderInfo, 5> SLIDERS{{
    {.key = "STRATEGY_PRESSING",
     .value = &StrategySliders::pressing,
     .helpKey = "TACTIC_PRESSING_HELP"},
    {.key = "STRATEGY_RISK_TAKING",
     .value = &StrategySliders::riskTaking,
     .helpKey = "TACTIC_RISK_HELP"},
    {.key = "STRATEGY_OFFENSIVE_BIAS",
     .value = &StrategySliders::offensiveBias,
     .helpKey = "TACTIC_OFFENSIVE_HELP"},
    {.key = "STRATEGY_WIDTH_USAGE",
     .value = &StrategySliders::widthUsage,
     .helpKey = "TACTIC_WIDTH_HELP"},
    {.key = "STRATEGY_COMPACTNESS",
     .value = &StrategySliders::compactness,
     .helpKey = "TACTIC_COMPACTNESS_HELP"},
}};

std::optional<std::reference_wrapper<Team>> mutableTeam(GUIView* view,
                                                        TeamID id)
{
  const auto data = view->getController().getGameData();
  if (!data)
  {
    return std::nullopt;
  }
  return data->getTeam(id);
}

std::vector<const Player*> squadOf(const GameController& controller, TeamID id)
{
  std::vector<const Player*> squad;
  for (const auto& player : controller.getPlayersForTeam(id))
  {
    squad.push_back(&player.get());
  }
  return squad;
}

std::string leagueName(const GameController& controller, LeagueID id)
{
  for (const auto& league : controller.getLeagues())
  {
    if (league.get().getId() == id)
    {
      return league.get().getName();
    }
  }
  return {};
}

/** Role, duty and shape of the slot at @p anchor (Standard when unset). */
SlotInstruction slotAt(const Strategy& strategy, Vector2F anchor)
{
  SlotInstruction instruction;
  instruction.anchor = anchor;
  if (const SlotInstruction* stored = strategy.findSlot(anchor))
  {
    instruction.role = stored->role;
    instruction.duty = stored->duty;
    instruction.possessionOffset = stored->possessionOffset;
  }
  // A role of another position group (the slot moved) plays Standard.
  if (!Tactics::allows(Tactics::familyForSlot(anchor), instruction.role))
  {
    instruction.role = TacticalRole::Standard;
  }
  return instruction;
}

/** The assistant's roles and duties for every starter (as on the tactics
 * screen), keeping the in-possession shape. */
void suggestRoles(Strategy& strategy, const Lineup& lineup)
{
  for (const auto& positioned : lineup.getOutfieldPlayers())
  {
    if (positioned.player == nullptr)
    {
      continue;
    }
    SlotInstruction instruction = slotAt(strategy, positioned.position);
    const RoleFamily family = Tactics::familyForSlot(positioned.position);
    instruction.role = Tactics::suggestedRole(*positioned.player, family);
    instruction.duty = Tactics::suggestedDuty(*positioned.player, family);
    strategy.setSlot(instruction);
  }
  if (const Player* goalkeeper = lineup.getGoalkeeper())
  {
    strategy.setKeeperRole(
        Tactics::suggestedRole(*goalkeeper, RoleFamily::Goalkeeper));
  }
}

void applyPossessionShape(Strategy& strategy, const Lineup& lineup,
                          PossessionShape shape)
{
  for (const auto& positioned : lineup.getOutfieldPlayers())
  {
    SlotInstruction instruction = slotAt(strategy, positioned.position);
    instruction.possessionOffset =
        Tactics::possessionOffset(shape, positioned.position);
    strategy.setSlot(instruction);
  }
}

bool sameSliders(const StrategySliders& left, const StrategySliders& right)
{
  return std::ranges::all_of(
      SLIDERS, [&](const SliderInfo& slider)
      { return std::abs(left.*slider.value - right.*slider.value) < 0.001F; });
}

std::string playerLabel(const Player& player, const StatsConfig& config)
{
  return std::format("{}  ({} {:.0f})", player.getName(),
                     RoleUtils::shortName(player.getRole()),
                     player.getOverall(config));
}

/** Replaces @p current in the XI by @p chosen: from the bench, from outside
 * the matchday squad, or by trading places with another outfield starter. */
void selectPlayer(Lineup& lineup, const Player& current, const Player& chosen)
{
  if (lineup.swapPlayers(chosen.getId(), current.getId()))
  {
    return;
  }
  if (lineup.bringIn(&chosen, current.getId()))
  {
    return;
  }
  const auto& outfield = lineup.getOutfieldPlayers();
  const auto spotOf = [&outfield](PlayerID id) -> std::optional<Vector2F>
  {
    for (const auto& positioned : outfield)
    {
      if (positioned.player && positioned.player->getId() == id)
      {
        return positioned.position;
      }
    }
    return std::nullopt;
  };
  const auto from = spotOf(current.getId());
  const auto to = spotOf(chosen.getId());
  if (!from || !to)
  {
    return;
  }
  lineup.moveOutfieldPlayer(current.getId(), *to);
  lineup.moveOutfieldPlayer(chosen.getId(), *from);
}
}  // namespace

MatchSandboxScene::MatchSandboxScene(GUIView* guiView_ptr, bool kick_off_now,
                                     std::string open_drill, int open_view)
    : GUIScene(guiView_ptr),
      kick_off_pending(kick_off_now),
      drill_pending(std::move(open_drill)),
      drill_view(open_view)
{
}

SceneID MatchSandboxScene::getID() const { return SceneID::MATCH_SANDBOX; }

void MatchSandboxScene::onEnter()
{
  if (sidebar_font == nullptr)
  {
    const std::string fontPath =
        AssetPaths::file("assets/fonts/Manrope-Bold.ttf");
    const auto fontConfig = SandboxTypography::manropeConfig();
    sidebar_font = ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath.c_str(),
                                                            14.0F, &fontConfig);
    setup_regular_font = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        AssetPaths::file("assets/fonts/Manrope-Regular.ttf").c_str(), 16.0F,
        &fontConfig);
    setup_medium_font = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        AssetPaths::file("assets/fonts/Manrope-Medium.ttf").c_str(), 14.0F,
        &fontConfig);
  }
  // Told apart from the game in the taskbar, and opened maximised (once:
  // a size the user picks afterwards is kept).
  static bool maximised = false;
  int windowCount = 0;
  if (SDL_Window** windows = SDL_GetWindows(&windowCount))
  {
    if (windowCount > 0)
    {
      SDL_SetWindowTitle(windows[0], "Player12 match sandbox");
      if (!maximised)
      {
        SDL_MaximizeWindow(windows[0]);
      }
      maximised = true;
    }
    SDL_free(static_cast<void*>(windows));
  }

  // Team 66 and its copy start as home and away; without the test teams,
  // the first two clubs of the first league.
  if (const auto& clubs = sandboxDefaultClubs(); clubs[0] != 0 && clubs[1] != 0)
  {
    sides[HOME].team = clubs[0];
    sides[AWAY].team = clubs[1];
    return;
  }
  const GameController& controller = guiView->getController();
  const auto& leagues = controller.getLeagues();
  const auto& teams = controller.getTeams();
  std::vector<TeamID> candidates;
  for (const auto& team : teams)
  {
    if (!leagues.empty() &&
        team.get().getLeagueId() == leagues.front().get().getId())
    {
      candidates.push_back(team.get().getId());
    }
  }
  if (candidates.size() < 2)
  {
    for (const auto& team : teams)
    {
      candidates.push_back(team.get().getId());
    }
  }
  if (candidates.size() >= 2)
  {
    sides[HOME].team = candidates[0];
    sides[AWAY].team = candidates[1];
  }
}

void MatchSandboxScene::update(float /*deltaTime*/)
{
  if (drill_view == 3)
  {
    drill_view = 0;
    mode = 3;
    guiView->overlayScene(std::make_unique<ContactViewScene>(guiView));
  }
  if (drill_view == 4)
  {
    drill_view = 0;
    mode = 3;
    guiView->overlayScene(std::make_unique<TechniqueBenchScene>(guiView, true));
  }
  if (drill_view == 5)
  {
    drill_view = 0;
    mode = 3;
    guiView->overlayScene(std::make_unique<TrueViewScene>(guiView));
  }
  if (!drill_pending.empty())
  {
    const auto lower = [](std::string text)
    {
      std::ranges::transform(text, text.begin(), [](unsigned char c)
                             { return static_cast<char>(std::tolower(c)); });
      return text;
    };
    for (std::size_t index = 0; index < drills.size(); ++index)
    {
      if (lower(drills[index]->name()) == lower(drill_pending))
      {
        mode = 1;
        drill_index = static_cast<int>(index);
        if (drill_view == 2)
        {
          guiView->overlayScene(
              std::make_unique<ShotMapScene>(guiView, *drills[index], true));
        }
        else if (drill_view == 1)
        {
          guiView->overlayScene(std::make_unique<DrillMeasureScene>(
              guiView, *drills[index], true));
        }
        else
        {
          guiView->overlayScene(
              std::make_unique<DrillScene>(guiView, *drills[index]));
        }
      }
    }
    drill_pending.clear();
  }
  if (!kick_off_pending)
  {
    return;
  }
  kick_off_pending = false;
  if (sides[HOME].team != 0 && sides[AWAY].team != 0)
  {
    kickOff();
  }
}

std::optional<std::size_t> MatchSandboxScene::currentPreset(
    const Lineup& lineup)
{
  const auto& outfield = lineup.getOutfieldPlayers();
  for (std::size_t index = 0; index < Formation::PRESETS.size(); ++index)
  {
    const auto& slots = Formation::PRESETS[index].slots;
    if (outfield.size() != slots.size())
    {
      continue;
    }
    const bool matches = std::ranges::all_of(
        slots,
        [&outfield](const Formation::Slot& slot)
        {
          return std::ranges::any_of(
              outfield,
              [&slot](const Lineup::PositionedPlayer& positioned)
              {
                return std::abs(positioned.position.x - slot.position.x) <
                           SLOT_TOLERANCE &&
                       std::abs(positioned.position.y - slot.position.y) <
                           SLOT_TOLERANCE;
              });
        });
    if (matches)
    {
      return index;
    }
  }
  return std::nullopt;
}

void MatchSandboxScene::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  const float scale = Theme::scale();
  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        ImVec4(22 / 255.0F, 19 / 255.0F, 16 / 255.0F, 1.0F));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(24.0F * scale, 24.0F * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
  ImGui::Begin("##match_sandbox", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();

  renderSidebar();
  ImGui::SameLine(0.0F, 6.0F * scale);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, SandboxPageUI::PAGE);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, SandboxPageUI::SCROLL_TRACK);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, SandboxPageUI::SCROLL_GRAB);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,
                        SandboxPageUI::SCROLL_HOVER);
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,
                        SandboxPageUI::SCROLL_HOVER);
  const float gutter = (6 * scale) + ImGui::GetStyle().ScrollbarSize;
  const float available = ImGui::GetContentRegionAvail().x;
  const float pageWidth =
      mode == 0 ? SandboxPageUI::matchPageWidth(available, scale, gutter)
      : mode == 1 || mode == 2
          ? SandboxPageUI::activityPageWidth(available, scale, gutter)
      : mode == 3 ? SandboxPageUI::toolsPageWidth(available, scale, gutter)
      : mode == 5 ? std::min(available, 1120 * scale)
                  : 0.0F;
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 0.0F);
  ImGui::BeginChild("##sandbox_content", ImVec2(pageWidth, 0.0F),
                    ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(5);
  ImGui::PopStyleVar();
  if (mode == 5)
  {
    renderRecordings();
  }
  else if (mode == 6)
  {
    renderReviewPage();
  }
  else if (mode == 4)
  {
    renderDeveloperSetup();
  }
  else if (mode == 1)
  {
    renderDrills();
  }
  else if (mode == 2)
  {
    renderTraining();
  }
  else if (mode == 3)
  {
    renderVisualisers();
  }
  else
  {
    namespace Page = SandboxPageUI;
    const Page::Style style(setup_regular_font, scale);
    const float usable = ImGui::GetWindowSize().x - gutter;
    const bool columns = usable >= 1062 * scale;
    const float width = columns ? (usable - 6 * scale) / 2 : usable;
    Page::header(
        "Matches",
        "Choose two clubs, prepare both teams and manage the home side.",
        usable, scale, sidebar_font, setup_regular_font,
        "Engine versions and simulation conditions come from Set-up.");
    Page::beginCard("##match_controls", "MATCH CONTROLS", usable, Page::DARK,
                    scale, sidebar_font);
    renderFooter();
    Page::endCard(scale);
    ImGui::BeginGroup();
    renderSide(sides[HOME], true, width, 0);
    ImGui::EndGroup();
    if (columns)
    {
      ImGui::SameLine(0, 6 * scale);
    }
    ImGui::BeginGroup();
    renderSide(sides[AWAY], false, width, 0);
    ImGui::EndGroup();
  }
  ImGui::EndChild();
  ImGui::End();
}

void MatchSandboxScene::renderDeveloperSetup()
{
  SandboxSetupUI::render(Theme::scale(), setup_regular_font, setup_medium_font,
                         sidebar_font, full_familiarity, debug_at_kick_off);
}

void MatchSandboxScene::renderSidebar()
{
  const float scale = Theme::scale();
  const ImU32 bg = IM_COL32(15, 13, 11, 255);
  const ImU32 cream = IM_COL32(240, 234, 226, 255);
  const ImU32 white = IM_COL32(255, 255, 255, 255);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(12 * scale, 16 * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2 * scale));
  ImGui::BeginChild("##sandbox_sidebar", ImVec2(220 * scale, 0),
                    ImGuiChildFlags_AlwaysUseWindowPadding);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 panel = ImGui::GetWindowPos();
  const ImVec2 size = ImGui::GetWindowSize();
  const ImVec2 brand = ImGui::GetCursorScreenPos();
  SandboxTypography::label(
      draw, sidebar_font, 24, "Player 12", brand,
      ImVec2(brand.x + (196 * scale), brand.y + (80 * scale)), cream);
  ImGui::Dummy(ImVec2(196 * scale, 80 * scale));
  ImGui::Dummy(ImVec2(0, 22 * scale));
  ImGui::PushFont(sidebar_font, 14);
  const auto item = [&](const char* label, int destination, int icon)
  {
    const bool selected = mode == destination;
    const ImVec2 row = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton(label, ImVec2(196 * scale, 44 * scale),
                               ImGuiButtonFlags_EnableNav))
    {
      mode = destination;
    }
    if (selected || ImGui::IsItemHovered())
    {
      draw->AddRectFilled(
          row, ImVec2(row.x + (196 * scale), row.y + (44 * scale)),
          selected ? IM_COL32(46, 39, 32, 255) : IM_COL32(31, 26, 21, 255));
    }
    if (selected)
    {
      draw->AddRectFilled(
          row, ImVec2(row.x + (3 * scale), row.y + (44 * scale)), white);
    }
    if (ImGui::IsItemFocused())
    {
      draw->AddRect(row, ImVec2(row.x + (196 * scale), row.y + (44 * scale)),
                    IM_COL32(133, 122, 110, 255), 0, 0, scale);
    }
    const ImU32 ink = selected ? white : IM_COL32(196, 186, 174, 255);
    const auto pt = [&](float x, float y)
    {
      return ImVec2(row.x + ((14 + x * 20 / 24) * scale),
                    row.y + ((12 + y * 20 / 24) * scale));
    };
    const auto line = [&](float x, float y, float xx, float yy)
    { draw->AddLine(pt(x, y), pt(xx, yy), ink, 1.5F * scale); };
    if (icon == 0)
    {
      draw->AddRect(pt(3, 5), pt(21, 21), ink, 0, 0, 1.5F * scale);
      line(3, 10, 21, 10);
      line(8, 3, 8, 7);
      line(16, 3, 16, 7);
    }
    else if (icon == 1)
    {
      draw->AddRect(pt(3, 4), pt(21, 20), ink, 0, 0, 1.5F * scale);
      line(7, 15, 16, 8);
      line(12, 8, 16, 8);
      line(16, 8, 16, 12);
      draw->AddCircle(pt(7, 8), 1.5F * scale, ink, 12, 1.5F * scale);
    }
    else if (icon == 2)
    {
      line(7, 19, 10, 5);
      line(10, 5, 14, 5);
      line(14, 5, 17, 19);
      line(5, 20, 19, 20);
      line(8, 14, 16, 14);
    }
    else if (icon == 3)
    {
      line(3, 20, 21, 20);
      line(6, 17, 6, 11);
      line(12, 17, 12, 5);
      line(18, 17, 18, 8);
    }
    else if (icon == 5)
    {
      draw->AddRect(pt(3, 4), pt(21, 20), ink, 0, 0, 1.5F * scale);
      line(9, 8, 16, 12);
      line(16, 12, 9, 16);
      line(9, 16, 9, 8);
    }
    else if (icon == 6)
    {
      draw->AddCircle(pt(10, 10), 6 * scale, ink, 20, 1.5F * scale);
      line(15, 15, 21, 21);
    }
    else
    {
      line(4, 6, 20, 6);
      line(4, 12, 20, 12);
      line(4, 18, 20, 18);
      for (const ImVec2 knob : {ImVec2(9, 6), ImVec2(15, 12), ImVec2(8, 18)})
      {
        draw->AddCircleFilled(pt(knob.x, knob.y), 2.5F * scale, bg);
        draw->AddCircle(pt(knob.x, knob.y), 2.5F * scale, ink, 12,
                        1.5F * scale);
      }
    }
    SandboxTypography::label(
        draw, sidebar_font, 14, label, row,
        ImVec2(row.x + (196 * scale), row.y + (44 * scale)),
        selected ? white : cream, 1.5F * scale, false, 46 * scale);
  };
  item("MATCHES", 0, 0);
  item("DRILLS", 1, 1);
  item("TRAINING", 2, 2);
  item("TOOLS", 3, 3);
  item("RECORDINGS", 5, 5);
  item("REVIEW", 6, 6);
  // The sandbox's settings destination follows the reference footer placement.
  ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), size.y - (69 * scale)));
  const ImVec2 divider = ImGui::GetCursorScreenPos();
  draw->AddLine(divider, ImVec2(divider.x + (196 * scale), divider.y),
                IM_COL32(51, 44, 36, 255), scale);
  ImGui::Dummy(ImVec2(0, 7 * scale));
  item("SET-UP", 4, 4);
  ImGui::PopFont();
  const ImU32 edge = IM_COL32(22, 19, 16, 255), clear = IM_COL32(22, 19, 16, 0);
  const float f = 6 * scale;
  const ImVec2 br(panel.x + size.x, panel.y + size.y);
  draw->AddRectFilledMultiColor(panel, ImVec2(br.x, panel.y + f), edge, edge,
                                clear, clear);
  draw->AddRectFilledMultiColor(ImVec2(panel.x, br.y - f), br, clear, clear,
                                edge, edge);
  draw->AddRectFilledMultiColor(panel, ImVec2(panel.x + f, br.y), edge, clear,
                                clear, edge);
  draw->AddRectFilledMultiColor(ImVec2(br.x - f, panel.y), br, clear, edge,
                                edge, clear);
  ImGui::EndChild();
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
}

void MatchSandboxScene::renderDrills()
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const Page::Style style(setup_regular_font, scale);
  const Page::ActivityLayout layout(
      ImGui::GetWindowSize().x - ImGui::GetStyle().ScrollbarSize, scale);
  const bool columns = layout.columns;
  const float detailWidth = layout.details;
  const float listWidth = layout.catalogue;
  Page::header(
      "Drills",
      "Choose a drill, configure the test and watch or measure the result.",
      layout.page, scale, sidebar_font, setup_regular_font,
      "Engine versions and simulation conditions come from Set-up.");
  if (drills.empty())
  {
    return;
  }
  drill_index = std::clamp(drill_index, 0, static_cast<int>(drills.size()) - 1);
  auto category = drills[static_cast<std::size_t>(drill_index)]->category;
  Page::beginCard("##drill_categories", "CATEGORIES", layout.page, Page::MID,
                  scale, sidebar_font);
  const float rowWidth = ImGui::GetContentRegionAvail().x;
  float usedWidth = 0;
  for (int c = 0; c < static_cast<int>(DrillCategory::Count); ++c)
  {
    const auto candidate = static_cast<DrillCategory>(c);
    const auto* label = drillCategoryName(candidate);
    const float width = Page::buttonWidth(label, scale, sidebar_font);
    if (usedWidth > 0 && usedWidth + 6 * scale + width <= rowWidth)
    {
      ImGui::SameLine(0, 6 * scale);
      usedWidth += 6 * scale;
    }
    else
    {
      usedWidth = 0;
    }
    if (Page::button(label, candidate == category, scale, sidebar_font))
    {
      category_drill_indices[static_cast<std::size_t>(category)] = drill_index;
      const int previous =
          category_drill_indices[static_cast<std::size_t>(candidate)];
      if (previous >= 0 && static_cast<std::size_t>(previous) < drills.size() &&
          drills[static_cast<std::size_t>(previous)]->category == candidate)
      {
        drill_index = previous;
      }
      else
      {
        for (std::size_t i = 0; i < drills.size(); ++i)
        {
          if (drills[i]->category == candidate)
          {
            drill_index = static_cast<int>(i);
            break;
          }
        }
      }
      category = drills[static_cast<std::size_t>(drill_index)]->category;
    }
    usedWidth += width;
  }
  Page::endCard(scale);
  ImGui::BeginGroup();
  Page::beginCard("##drill_list", drillCategoryName(category), listWidth,
                  Page::LIGHT, scale, sidebar_font);
  for (std::size_t index = 0; index < drills.size(); ++index)
  {
    if (drills[index]->category == category &&
        Page::choice(drills[index]->name(),
                     static_cast<std::size_t>(drill_index) == index, scale,
                     sidebar_font))
    {
      drill_index = static_cast<int>(index);
      category_drill_indices[static_cast<std::size_t>(category)] = drill_index;
    }
  }
  Page::endCard(scale);
  ImGui::EndGroup();
  if (columns)
  {
    ImGui::SameLine(0, 6 * scale);
  }
  Drill& drill = *drills[static_cast<std::size_t>(drill_index)];
  ImGui::BeginGroup();
  Page::beginCard("##drill_overview", "SELECTED DRILL", detailWidth, Page::MID,
                  scale, sidebar_font);
  ImGui::PushFont(sidebar_font, 24);
  ImGui::TextWrapped("%s", drill.name());
  ImGui::PopFont();
  ImGui::PushTextWrapPos(0.0F);
  ImGui::TextUnformatted(drill.summary());
  ImGui::PopTextWrapPos();
  Page::endCard(scale);
  Page::beginCard("##drill_run", "RUN DRILL", detailWidth, Page::DARK, scale,
                  sidebar_font);
  if (Page::button("WATCH", true, scale, sidebar_font))
  {
    guiView->overlayScene(std::make_unique<DrillScene>(guiView, drill));
  }
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip(
        "Run the drill on the pitch, step by step if you like, "
        "with the debugger windows.");
  }
  ImGui::SameLine();
  if (Page::button("MEASURE", false, scale, sidebar_font))
  {
    guiView->overlayScene(std::make_unique<DrillMeasureScene>(guiView, drill));
  }
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip(
        "Run the drill headless over a range of a setting: "
        "curves, tables and CSV.");
  }
  if (drill.offersShotMap())
  {
    ImGui::SameLine();
    if (Page::button("SHOT MAP", false, scale, sidebar_font))
    {
      guiView->overlayScene(std::make_unique<ShotMapScene>(guiView, drill));
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip(
          "A grid of spots, many shots each: conversion against "
          "the engine's xG as a heatmap, and the calibration.");
    }
  }
  Page::endCard(scale);
  Page::beginCard("##drill_settings", "SETTINGS", detailWidth, Page::MID, scale,
                  sidebar_font);
  ImGui::PushItemWidth(
      std::min(280 * scale, ImGui::GetContentRegionAvail().x * 0.5F));
  drill.renderSettings(detailWidth >= 780 * scale ? 2 : 1, true);
  ImGui::PopItemWidth();
  Page::endCard(scale);
  ImGui::EndGroup();
}

void MatchSandboxScene::renderVisualisers()
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const Page::Style style(setup_regular_font, scale);
  const float usable =
      std::max(scale, ImGui::GetWindowSize().x -
                          ImGui::GetStyle().ScrollbarSize - (6 * scale));
  const int columns = usable >= 1332 * scale  ? 3
                      : usable >= 806 * scale ? 2
                                              : 1;
  const float cardWidth = std::min(
      440 * scale, columns == 2 && usable < 886 * scale ? 400 * scale : usable);
  const float width = (static_cast<float>(columns) * cardWidth) +
                      (static_cast<float>(columns - 1) * 6.0F * scale);
  Page::header("Tools",
               "Inspect movement, ball contact and technique performance.",
               width, scale, sidebar_font, setup_regular_font,
               "Visualisers and test benches for the Binturong body model.");
  const struct Tool
  {
    const char* id;
    const char* title;
    const char* description;
    const char* command;
    ImU32 background;
  } tools[] = {
      {.id = "##visualiser_page",
       .title = "CONTACT DIFFICULTY",
       .description =
           "Inspect how reach, approach angle and ball movement affect a touch "
           "or "
           "strike. "
           "Adjust the contact model and see each factor's contribution.",
       .command = "match_sandbox --contact-view",
       .background = Page::LIGHT},
      {.id = "##true_view_page",
       .title = "TRUE VIEW",
       .description =
           "Explore the player's body model, movement, reach and personal "
           "space in "
           "3D. "
           "Inspect the values behind each shape without the player figure.",
       .command = "match_sandbox --true-view",
       .background = Page::MID},
      {.id = "##bench_page",
       .title = "TECHNIQUE BENCH",
       .description =
           "Run repeatable passing and shooting tests across techniques and "
           "conditions. "
           "Compare success rates over many seeds and against the previous "
           "run.",
       .command = "match_sandbox --bench-view\n--bench for headless tests",
       .background = Page::DARK},
      {.id = "##xg_map_page",
       .title = "XG MAP",
       .description =
           "See the engine's xG over a match or training pitch of any size and "
           "goal, "
           "and where the brain thinks a shot is worth it. Hover for every "
           "part.",
       .command = "Training ground plan, section 24",
       .background = Page::LIGHT}};
  for (int tool = 0; tool < 4; ++tool)
  {
    if (tool % columns != 0)
    {
      ImGui::SameLine(0, 6 * scale);
    }
    Page::beginCard(tools[tool].id, tools[tool].title, cardWidth,
                    tools[tool].background, scale, sidebar_font);
    ImGui::TextWrapped("%s", tools[tool].description);
    if (Page::button("OPEN", true, scale, sidebar_font))
    {
      if (tool == 0)
      {
        guiView->overlayScene(std::make_unique<ContactViewScene>(guiView));
      }
      else if (tool == 1)
      {
        guiView->overlayScene(std::make_unique<TrueViewScene>(guiView));
      }
      else if (tool == 2)
      {
        guiView->overlayScene(std::make_unique<TechniqueBenchScene>(guiView));
      }
      else
      {
        guiView->overlayScene(std::make_unique<XgMapScene>(guiView));
      }
    }
    ImGui::PushFont(setup_regular_font, 14);
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", tools[tool].command);
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    Page::endCard(scale);
  }
}
void MatchSandboxScene::renderTraining()
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const Page::Style style(setup_regular_font, scale);
  const Page::ActivityLayout layout(
      ImGui::GetWindowSize().x - ImGui::GetStyle().ScrollbarSize, scale);
  const bool columns = layout.columns;
  const float detailWidth = layout.details;
  const float listWidth = layout.catalogue;
  const GameController& controller = guiView->getController();
  const TeamID clubId = sides[HOME].team;
  const std::vector<const Player*> squad = squadOf(controller, clubId);
  std::string clubName;
  for (const auto& team : controller.getTeams())
  {
    if (team.get().getId() == clubId)
    {
      clubName = team.get().getName();
    }
  }
  // A new club: the first of its squad, as many as fit on the pitch.
  if (clubId != training_club)
  {
    training_club = clubId;
    training_picks.clear();
    for (const Player* player : squad)
    {
      if (training_picks.size() < TrainingDrill::MAX_PLAYERS)
      {
        training_picks.push_back(player->getId());
      }
    }
  }

  const std::vector<TrainingSessionInfo>& sessions = trainingSessions();
  Page::header("Training",
               "Choose a session and the players who will take part.",
               layout.page, scale, sidebar_font, setup_regular_font,
               std::format("Training club: {}. Choose it on Matches. Engine "
                           "versions and conditions come from Set-up.",
                           clubName)
                   .c_str());
  ImGui::BeginGroup();
  Page::beginCard("##session_list", "SESSIONS", listWidth, Page::LIGHT, scale,
                  sidebar_font);
  // Sessions of drills one after another on the pitch, then sessions on the
  // training ground (its zones side by side, each its own game; training
  // ground plan section 23).
  const std::vector<SessionPlan>& zonePlans = sessionPlans();
  const std::size_t total = sessions.size() + zonePlans.size();
  for (std::size_t index = 0; index < total; ++index)
  {
    const char* name = index < sessions.size()
                           ? sessions[index].name
                           : zonePlans[index - sessions.size()].name.c_str();
    if (Page::choice(name, static_cast<std::size_t>(session_index) == index,
                     scale, sidebar_font))
    {
      session_index = static_cast<int>(index);
    }
  }
  Page::endCard(scale);
  // Saved zones of training sessions (training ground plan section 23,
  // T7f): each opens as a replay of that zone alone.
  Page::beginCard("##zone_recordings", "ZONE RECORDINGS", listWidth,
                  Page::LIGHT, scale, sidebar_font);
  static std::vector<std::filesystem::path> zoneFiles;
  static bool zoneFilesListed = false;
  if (!zoneFilesListed || Page::button("Refresh", false, scale, sidebar_font))
  {
    zoneFiles = ZoneRecordings::list();
    zoneFilesListed = true;
  }
  static std::string zoneOpenError;
  if (zoneFiles.empty())
  {
    ImGui::TextDisabled("None saved yet (Save this zone, in a session).");
  }
  for (const std::filesystem::path& file : zoneFiles)
  {
    const std::string label = file.filename().string();
    if (Page::choice(
            label
                .substr(0, label.size() - std::string_view(" zone.json").size())
                .c_str(),
            false, scale, sidebar_font))
    {
      std::string error;
      if (std::optional<ZoneRecording> recording =
              ZoneRecordings::load(file, error))
      {
        zoneOpenError.clear();
        guiView->overlayScene(
            std::make_unique<ZoneReplayScene>(guiView, std::move(*recording)));
      }
      else
      {
        zoneOpenError = std::format("Can't open it: {}", error);
      }
    }
  }
  if (!zoneOpenError.empty())
  {
    ImGui::PushTextWrapPos(0);
    ImGui::TextDisabled("%s", zoneOpenError.c_str());
    ImGui::PopTextWrapPos();
  }
  Page::endCard(scale);
  ImGui::EndGroup();
  if (columns)
  {
    ImGui::SameLine(0, 6 * scale);
  }
  if (total == 0)
  {
    return;
  }
  session_index = std::min(session_index, static_cast<int>(total) - 1);
  const std::size_t chosen = static_cast<std::size_t>(session_index);
  const SessionPlan* zonePlan = chosen >= sessions.size()
                                    ? &zonePlans[chosen - sessions.size()]
                                    : nullptr;
  ImGui::BeginGroup();
  Page::beginCard("##session_overview", "SELECTED SESSION", detailWidth,
                  Page::MID, scale, sidebar_font);
  ImGui::PushFont(sidebar_font, 24);
  ImGui::TextWrapped("%s", (zonePlan != nullptr) ? zonePlan->name.c_str()
                                                 : sessions[chosen].name);
  ImGui::PopFont();
  ImGui::PushTextWrapPos(0.0F);
  ImGui::TextUnformatted((zonePlan != nullptr) ? zonePlan->summary.c_str()
                                               : sessions[chosen].summary);
  if (zonePlan != nullptr)
  {
    ImGui::TextDisabled(
        "%s", std::format("On the training ground: {} players; the first "
                          "{} picks take part (reference players fill "
                          "any gap).",
                          zonePlan->squadSize, zonePlan->squadSize)
                  .c_str());
  }
  ImGui::PopTextWrapPos();
  Page::endCard(scale);
  Page::beginCard("##training_start", "RUN SESSION", detailWidth, Page::DARK,
                  scale, sidebar_font);
  ImGui::BeginDisabled(training_picks.empty());
  if (Page::button("START SESSION", true, scale, sidebar_font))
  {
    // The picks in squad order.
    std::vector<TrainingPlayer> trainees;
    for (const Player* player : squad)
    {
      if (std::ranges::find(training_picks, player->getId()) !=
          training_picks.end())
      {
        trainees.push_back({.name = player->getName(),
                            .role = player->getRole(),
                            .stats = player->getStats(),
                            .height = player->getHeight(),
                            .foot = player->getFoot()});
      }
    }
    if (zonePlan != nullptr)
    {
      guiView->overlayScene(std::make_unique<ZoneSessionScene>(
          guiView, *zonePlan, std::move(trainees)));
    }
    else
    {
      guiView->overlayScene(std::make_unique<TrainingSessionScene>(
          guiView, sessions[chosen].make(std::move(trainees)), clubName));
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("%zu players selected", training_picks.size());
  Page::endCard(scale);
  Page::beginCard("##training_squad_card", "SQUAD", detailWidth, Page::MID,
                  scale, sidebar_font);
  ImGui::TextWrapped("%s", std::format("{}: {} of {} picked (up to {})",
                                       clubName, training_picks.size(),
                                       squad.size(), TrainingDrill::MAX_PLAYERS)
                               .c_str());
  if (squad.empty())
  {
    ImGui::TextWrapped("No players are available for this club.");
  }
  const auto drawRoster = [&](std::size_t first, std::size_t last, int column)
  {
    ImGui::PushID(column);
    if (ImGui::BeginTable("##training_squad", 6,
                          ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_SizingFixedFit,
                          ImVec2(0.0F, 0.0F)))
    {
      ImGui::TableSetupColumn("");
      ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Pos");
      ImGui::TableSetupColumn("Pace");
      ImGui::TableSetupColumn("Phys");
      ImGui::TableSetupColumn("Stam");
      ImGui::PushFont(sidebar_font, 14);
      ImGui::TableHeadersRow();
      ImGui::PopFont();
      for (std::size_t row = first; row < last; ++row)
      {
        const Player* player = squad[row];
        const PlayerID id = player->getId();
        const auto found = std::ranges::find(training_picks, id);
        bool picked = found != training_picks.end();
        const auto stat = [player](const char* name)
        {
          const auto& stats = player->getStats();
          const auto at = stats.find(name);
          return at == stats.end() ? 0.0F : at->second;
        };
        ImGui::PushID(static_cast<int>(id));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::BeginDisabled(!picked && training_picks.size() >=
                                            TrainingDrill::MAX_PLAYERS);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(3 * scale, 3 * scale));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                              picked ? Page::LIME : Page::MID);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                              picked ? Page::LIME : Page::MID);
        if (ImGui::Checkbox("##pick", &picked))
        {
          if (picked)
          {
            training_picks.push_back(id);
          }
          else
          {
            training_picks.erase(found);
          }
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        ImGui::TableNextColumn();
        UI::textFitted(player->getName(), ImGui::GetContentRegionAvail().x,
                       ImGui::ColorConvertU32ToFloat4(Page::CREAM));
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(RoleUtils::toString(player->getRole()).c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", static_cast<double>(stat("Pace")));
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", static_cast<double>(stat("Physicality")));
        ImGui::TableNextColumn();
        ImGui::Text("%.0f", static_cast<double>(stat("Stamina")));
        ImGui::PopID();
      }
      ImGui::EndTable();
    }

    ImGui::PopID();
  };
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(3 * scale, 3 * scale));
  if (detailWidth >= 780 * scale)
  {
    if (ImGui::BeginTable(
            "##roster_columns", 2,
            ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
    {
      const std::size_t halfway = (squad.size() + 1) / 2;
      ImGui::TableNextColumn();
      drawRoster(0, halfway, 0);
      ImGui::TableNextColumn();
      drawRoster(halfway, squad.size(), 1);
      ImGui::EndTable();
    }
  }
  else
  {
    drawRoster(0, squad.size(), 0);
  }
  ImGui::PopStyleVar();
  Page::endCard(scale);
  ImGui::EndGroup();
}

void MatchSandboxScene::renderMatchPanel(MatchEngine& engine, bool& paused)
{
  if (match_true_view &&
      ImGui::CollapsingHeader("True view", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::PushID("true_view");
    true_view.renderSettings(
        (review.shownEngine() != nullptr) ? *review.shownEngine() : engine);
    ImGui::PopID();
  }
  if (!recorder)
  {
    return;
  }
  if (SandboxPageUI::button(match_debug ? "Show Touchline" : "Debugger", false,
                            Theme::scale(), sidebar_font))
  {
    match_debug = !match_debug;
  }
  if (ImGui::CollapsingHeader("Review", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::PushID("review");
    review.renderPanel(*recorder, &engine, &paused, &inspector);
    ImGui::PopID();
  }
}

void MatchSandboxScene::renderSide(Side& side, bool home, float width,
                                   float height)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  (void)height;
  ImGui::PushID(home ? "home" : "away");
  Page::beginCard("##club_card",
                  home ? "HOME / YOU MANAGE" : "AWAY / AI MANAGES", width,
                  Page::LIGHT, scale, sidebar_font);
  renderClubPicker(side, home);
  if (const auto team = mutableTeam(guiView, side.team))
  {
    renderFormation(team->get());
  }
  Page::endCard(scale);
  if (const auto team = mutableTeam(guiView, side.team))
  {
    Page::beginCard("##xi_card", "STARTING XI / ROLES & DUTIES", width,
                    Page::MID, scale, sidebar_font);
    renderSelection(team->get());
    Page::endCard(scale);
    Page::beginCard("##tactics_card", "TEAM INSTRUCTIONS", width, Page::DARK,
                    scale, sidebar_font);
    renderInstructions(team->get().getStrategy());
    Page::endCard(scale);
  }
  ImGui::PopID();
}

void MatchSandboxScene::renderClubPicker(Side& side, bool home)
{
  const GameController& controller = guiView->getController();
  const auto current = controller.getTeamById(side.team);
  const std::string preview =
      current
          ? std::format("{}  /  {}", current->get().getName(),
                        leagueName(controller, current->get().getLeagueId()))
          : std::string("Choose a club");
  (void)home;
  ImGui::SetNextItemWidth(-1.0F);
  if (!SandboxPageUI::beginCombo("##club", preview.c_str(),
                                 ImGuiComboFlags_HeightLarge))
  {
    return;
  }
  for (const auto& league : controller.getLeagues())
  {
    ImGui::SeparatorText(league.get().getName().c_str());
    for (const auto& team : controller.getTeams())
    {
      if (team.get().getLeagueId() != league.get().getId())
      {
        continue;
      }
      const TeamID id = team.get().getId();
      ImGui::PushID(id);
      if (ImGui::Selectable(team.get().getName().c_str(), id == side.team))
      {
        side.team = id;
      }
      ImGui::PopID();
    }
  }
  ImGui::EndCombo();
}

void MatchSandboxScene::renderFormation(Team& team)
{
  const GameController& controller = guiView->getController();
  Lineup& lineup = team.getLineup();
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  ImGui::TextDisabled("Formation");
  float used = 0;
  const float width = ImGui::GetContentRegionAvail().x;
  const std::optional<std::size_t> active = currentPreset(lineup);
  for (std::size_t index = 0; index < Formation::PRESETS.size(); ++index)
  {
    const Formation::Preset& preset = Formation::PRESETS[index];
    const float w = Page::buttonWidth(preset.name, scale, sidebar_font);
    if (used > 0 && used + w <= width)
    {
      ImGui::SameLine(0, 6 * scale);
    }
    else
    {
      used = 0;
    }
    used += w + 6 * scale;
    if (Page::button(preset.name, active == index, scale, sidebar_font))
    {
      // A new shape picks the best XI for it and the roles that fit them.
      const std::vector<const Player*> squad =
          squadOf(controller, team.getId());
      Formation::autoPick(lineup, preset, squad, controller.getStatsConfig());
      suggestRoles(team.getStrategy(), lineup);
    }
  }
  if (!active)
  {
    std::vector<PlayerRole> roles;
    for (const auto& positioned : lineup.getOutfieldPlayers())
    {
      roles.push_back(Lineup::roleAt(positioned.position));
    }
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(Page::MUTED),
                       "Current: %s", formationLabel(roles).c_str());
  }
  if (Page::button("Best XI", false, scale, sidebar_font))
  {
    const std::vector<const Player*> squad = squadOf(controller, team.getId());
    const Formation::Preset& preset = Formation::PRESETS[active.value_or(0)];
    Formation::autoPick(lineup, preset, squad, controller.getStatsConfig());
  }
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip("Pick the strongest eleven for the formation.");
  }
  ImGui::SameLine();
  if (Page::button("Suggest roles", false, scale, sidebar_font))
  {
    suggestRoles(team.getStrategy(), lineup);
  }
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip("Give every starter the role and duty that suit him.");
  }
  ImGui::SetNextItemWidth(-1);
  if (SandboxPageUI::beginCombo("##shape", LOC("TACTIC_SHAPE_PRESETS")))
  {
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(PossessionShape::COUNT); ++index)
    {
      const auto shape = static_cast<PossessionShape>(index);
      if (ImGui::Selectable(LOC(Tactics::possessionShapeKey(shape))))
      {
        applyPossessionShape(team.getStrategy(), lineup, shape);
      }
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip("Where the outfield players move with the ball.");
  }
}

void MatchSandboxScene::renderInstructions(Strategy& strategy)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  StrategySliders sliders = strategy.getSliders();
  float used = 0;
  const float rowWidth = ImGui::GetContentRegionAvail().x;
  for (const auto& preset : Lab::TACTIC_PRESETS)
  {
    const std::string label(preset.name);
    const float w = Page::buttonWidth(label.c_str(), scale, sidebar_font);
    if (used > 0 && used + 6 * scale + w <= rowWidth)
    {
      ImGui::SameLine(0, 6 * scale);
    }
    else
    {
      used = 0;
    }
    used += w + 6 * scale;
    if (Page::button(label.c_str(), sameSliders(sliders, preset.sliders), scale,
                     sidebar_font))
    {
      sliders = preset.sliders;
      strategy.setAllSliders(sliders);
    }
  }
  const float labelWidth = SLIDER_LABEL_WIDTH * Theme::scale();
  bool changed = false;
  if (ImGui::BeginTable("##instructions", 2,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_BordersInnerV))
  {
    ImGui::TableSetupColumn("Instruction", ImGuiTableColumnFlags_WidthFixed,
                            labelWidth);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    for (const SliderInfo& slider : SLIDERS)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(slider.key);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(LOC(slider.key));
      if (ImGui::IsItemHovered())
      {
        ImGui::SetTooltip("%s", LOC(slider.helpKey));
      }
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0F);
      changed |= ImGui::SliderFloat("##value", &(sliders.*slider.value), 0.0F,
                                    1.0F, "%.2f");
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (changed)
  {
    strategy.setAllSliders(sliders);
  }
}

void MatchSandboxScene::renderSelection(Team& team)
{
  const GameController& controller = guiView->getController();
  const StatsConfig& config = controller.getStatsConfig();
  Lineup& lineup = team.getLineup();
  Strategy& strategy = team.getStrategy();
  const std::vector<const Player*> squad = squadOf(controller, team.getId());

  constexpr ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                      ImVec2(6 * Theme::scale(), 3 * Theme::scale()));
  const bool table = ImGui::BeginTable("##xi", 5, flags);
  if (!table)
  {
    ImGui::PopStyleVar();
    return;
  }
  const float scale = Theme::scale();
  ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed,
                          POSITION_COLUMN_WIDTH * scale);
  ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch, 2.2F);
  ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthStretch, 1.6F);
  ImGui::TableSetupColumn("Duty", ImGuiTableColumnFlags_WidthFixed, 96 * scale);
  ImGui::TableSetupColumn("Fit", ImGuiTableColumnFlags_WidthFixed,
                          OVERALL_COLUMN_WIDTH * scale);
  ImGui::PushFont(sidebar_font, 14);
  ImGui::TableHeadersRow();
  ImGui::PopFont();

  // The player picker of one slot: the whole squad, best first. Picking a
  // player already in the XI trades places with him.
  std::vector<const Player*> ordered = squad;
  std::ranges::sort(
      ordered, [&config](const Player* left, const Player* right)
      { return left->getOverall(config) > right->getOverall(config); });
  const auto playerPicker = [&](const Player& current, bool goalkeeper)
  {
    ImGui::SetNextItemWidth(-1.0F);
    if (!SandboxPageUI::beginCombo("##player", current.getName().c_str(),
                                   ImGuiComboFlags_HeightLarge))
    {
      if (ImGui::IsItemHovered())
      {
        ImGui::SetTooltip("%s", playerLabel(current, config).c_str());
      }
      return;
    }
    for (const Player* candidate : ordered)
    {
      // The goalkeeper's slot takes keepers, outfield slots outfielders.
      if ((candidate->getRole() == PlayerRole::GK) != goalkeeper)
      {
        continue;
      }
      const bool starter = lineup.isStarter(candidate->getId());
      ImGui::PushID(static_cast<int>(candidate->getId()));
      if (starter && candidate != &current)
      {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::palette().muted);
      }
      if (ImGui::Selectable(playerLabel(*candidate, config).c_str(),
                            candidate == &current) &&
          candidate != &current)
      {
        selectPlayer(lineup, current, *candidate);
      }
      if (starter && candidate != &current)
      {
        ImGui::PopStyleColor();
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  };

  // Goalkeeper: a role, no duty.
  if (const Player* keeper = lineup.getGoalkeeper())
  {
    ImGui::PushID("keeper");
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(RoleUtils::shortName(PlayerRole::GK));
    ImGui::TableNextColumn();
    playerPicker(*keeper, true);
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1.0F);
    const TacticalRole keeperRole = strategy.getKeeperRole();
    if (SandboxPageUI::beginCombo("##role", LOC(Tactics::roleKey(keeperRole))))
    {
      for (const TacticalRole role : Tactics::rolesFor(RoleFamily::Goalkeeper))
      {
        if (ImGui::Selectable(LOC(Tactics::roleKey(role)), role == keeperRole))
        {
          strategy.setKeeperRole(role);
        }
      }
      ImGui::EndCombo();
    }
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.0f",
                Tactics::roleFit(*keeper, RoleFamily::Goalkeeper, keeperRole));
    ImGui::PopID();
  }

  // Outfield slots, back to front.
  std::vector<Lineup::PositionedPlayer> outfield = lineup.getOutfieldPlayers();
  std::ranges::stable_sort(
      outfield,
      [](const auto& left, const auto& right)
      {
        if (std::abs(left.position.x - right.position.x) > SLOT_TOLERANCE)
        {
          return left.position.x < right.position.x;
        }
        return left.position.y < right.position.y;
      });
  for (const Lineup::PositionedPlayer& positioned : outfield)
  {
    if (positioned.player == nullptr)
    {
      continue;
    }
    const Player& player = *positioned.player;
    const RoleFamily family = Tactics::familyForSlot(positioned.position);
    SlotInstruction instruction = slotAt(strategy, positioned.position);
    ImGui::PushID(static_cast<int>(player.getId()));
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(
        RoleUtils::shortName(Lineup::roleAt(positioned.position)));
    ImGui::TableNextColumn();
    playerPicker(player, false);

    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(-1.0F);
    if (SandboxPageUI::beginCombo("##role",
                                  LOC(Tactics::roleKey(instruction.role))))
    {
      for (const TacticalRole role : Tactics::rolesFor(family))
      {
        if (ImGui::Selectable(LOC(Tactics::roleKey(role)),
                              role == instruction.role))
        {
          instruction.role = role;
          strategy.setSlot(instruction);
        }
        if (ImGui::IsItemHovered())
        {
          ImGui::SetTooltip("%s", LOC(Tactics::roleDescriptionKey(role)));
        }
      }
      ImGui::EndCombo();
    }

    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("%s", LOC(Tactics::roleKey(instruction.role)));
    }
    ImGui::TableNextColumn();
    if (Tactics::hasDuty(family))
    {
      ImGui::SetNextItemWidth(-1.0F);
      if (SandboxPageUI::beginCombo("##duty",
                                    LOC(Tactics::dutyKey(instruction.duty))))
      {
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(RoleDuty::COUNT); ++index)
        {
          const auto duty = static_cast<RoleDuty>(index);
          if (ImGui::Selectable(LOC(Tactics::dutyKey(duty)),
                                duty == instruction.duty))
          {
            instruction.duty = duty;
            strategy.setSlot(instruction);
          }
        }
        ImGui::EndCombo();
      }
    }

    if (Tactics::hasDuty(family) && ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("%s", LOC(Tactics::dutyKey(instruction.duty)));
    }
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.0f", Tactics::roleFit(player, family, instruction.role));
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("How well he suits the role (0-100).");
    }
    ImGui::PopID();
  }
  ImGui::EndTable();
  ImGui::PopStyleVar();
}

void MatchSandboxScene::renderFooter()
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const auto next = [&](float width)
  {
    if (ImGui::GetItemRectMax().x + 6 * scale + width <=
        ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
    {
      ImGui::SameLine(0, 6 * scale);
    }
  };
  const auto buttonNext = [&](const char* label)
  { next(Page::buttonWidth(label, scale, sidebar_font)); };
  const bool sameClub = sides[HOME].team == sides[AWAY].team;
  const bool ready =
      !sameClub && sides[HOME].team != 0 && sides[AWAY].team != 0;
  ImGui::BeginDisabled(!ready);
  if (Page::button("Kick off", true, scale, sidebar_font))
  {
    kickOff();
  }
  ImGui::EndDisabled();
  if (sameClub && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
  {
    ImGui::SetTooltip("Choose two different clubs.");
  }
  next(240 * scale);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Match seed");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(130 * scale);
  ImGui::InputScalar("##seed", ImGuiDataType_U32, &match_seed);
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip(
        "The same seed, clubs and tactics replay the same match.");
  }
  next(ImGui::CalcTextSize("New seed each match").x + (40 * scale));
  ImGui::Checkbox("New seed each match", &new_seed_each_match);
  if (recorder)
  {
    buttonNext("Review");
    if (Page::button("Review", false, scale, sidebar_font))
    {
      mode = 6;
      reviewTab = 0;
    }
    buttonNext("Debugger");
    if (Page::button("Debugger", false, scale, sidebar_font))
    {
      mode = 6;
      reviewTab = 1;
    }
  }
  ImGui::BeginDisabled(!recorder || !match_setup || loaded.has_value());
  if (Page::button("Save recording", false, scale, sidebar_font))
  {
    saveRecording();
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
  {
    ImGui::SetTooltip(
        "Save the last match to review it later or compare engine versions.");
  }
  next(ImGui::CalcTextSize("with full detail").x + (40 * scale));
  ImGui::Checkbox("with full detail", &save_with_detail);
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip(
        "Save decision breakdowns for term-by-term engine comparison (a larger "
        "file).");
  }
  buttonNext("Recordings...");
  if (Page::button("Recordings...", false, scale, sidebar_font))
  {
    mode = 5;
    recording_files.clear();
    recording_index = -1;
    recording_preview.reset();
  }
  if (!last_result.empty())
  {
    ImGui::TextWrapped("Last match: %s", last_result.c_str());
  }
  if (!status.empty())
  {
    ImGui::TextWrapped("%s", status.c_str());
  }
}

void MatchSandboxScene::saveRecording()
{
  if (!recorder || !match_setup)
  {
    return;
  }
  const MatchRecording recording =
      Recordings::capture(*match_setup, *recorder, save_with_detail);
  const std::filesystem::path path =
      Recordings::folder() / Recordings::fileName(recording);
  std::string error;
  status = Recordings::save(recording, path, error)
               ? "Saved " + path.filename().string()
               : "Could not save: " + error;
  recording_files.clear();
}

void MatchSandboxScene::loadRecording(
    const std::filesystem::path& path,
    std::optional<std::array<BrainId, 2>> brains,
    std::optional<std::array<BodyId, 2>> bodies,
    std::optional<HandshakeId> handshake,
    std::optional<PossessionId> possession)
{
  std::string error;
  std::optional<MatchRecording> recording = Recordings::load(path, error);
  if (!recording)
  {
    status = "Could not open " + path.filename().string() + ": " + error;
    return;
  }
  Recordings::Replay replayed =
      Recordings::replay(*recording, guiView->getController(), brains, bodies,
                         handshake, possession);
  if (!replayed.match)
  {
    status = "Could not replay: " + replayed.error;
    return;
  }
  // The replayed match takes the place of the last match in every window.
  recorder = std::move(replayed.match);
  match_setup = recording->setup;
  if (brains)
  {
    match_setup->brains = *brains;
  }
  if (bodies)
  {
    match_setup->bodies = *bodies;
  }
  if (handshake)
  {
    match_setup->handshake = *handshake;
  }
  if (possession)
  {
    match_setup->possession = *possession;
  }
  match_home = recording->setup.sides[0].team;
  match_away = recording->setup.sides[1].team;
  debugger.reset();
  review.reset();
  inspector.reset();
  comparison.reset();
  loaded = std::move(recording);
  replay_info = std::move(replayed);
  mode = 6;
  reviewTab = 0;
  last_result = std::format(
      "{} {} - {} {} (recording, seed {}, {})", loaded->setup.sides[0].name,
      replay_info->homeScore, replay_info->awayScore,
      loaded->setup.sides[1].name, loaded->setup.matchSeed,
      brains || bodies || handshake || possession
          ? std::format("replayed with brains {}, bodies {}, handshake {}; "
                        "recorded with brains {}, bodies {}, handshake {}",
                        brainPairName(match_setup->brains),
                        bodyPairName(match_setup->bodies),
                        handshakeName(match_setup->handshake),
                        brainPairName(loaded->setup.brains),
                        bodyPairName(loaded->setup.bodies),
                        handshakeName(loaded->setup.handshake))
          : std::format("brains {}; bodies {}; handshake {}",
                        brainPairName(loaded->setup.brains),
                        bodyPairName(loaded->setup.bodies),
                        handshakeName(loaded->setup.handshake)));
  last_result += std::format("; possession {} (recorded {})",
                             possessionName(match_setup->possession),
                             possessionName(loaded->setup.possession));
  status = "Opened " + path.filename().string();
}

void MatchSandboxScene::renderRecordings()
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const Page::Style style(setup_regular_font, scale);
  const float width = ImGui::GetContentRegionAvail().x - (6 * scale) -
                      ImGui::GetStyle().ScrollbarSize;
  Page::header("Recordings",
               "Browse saved matches and compare engine versions.", width,
               scale, sidebar_font, setup_regular_font, nullptr, true);
  Page::beginCard("##recording_actions", "LIBRARY", width, Page::DARK, scale,
                  sidebar_font);
  if (Page::button("Refresh", false, scale, sidebar_font))
  {
    recording_files.clear();
    recording_index = -1;
    recording_preview.reset();
  }
  ImGui::SameLine(0, 6 * scale);
  if (Page::button("Current review", false, scale, sidebar_font))
  {
    mode = 6;
  }
  ImGui::TextWrapped("%s", Recordings::folder().string().c_str());
  if (!status.empty())
  {
    ImGui::TextWrapped("%s", status.c_str());
  }
  Page::endCard(scale);
  if (recording_files.empty())
  {
    std::error_code ignored;
    for (const auto& entry :
         std::filesystem::directory_iterator(Recordings::folder(), ignored))
    {
      if (entry.path().extension() == ".json")
      {
        recording_files.push_back(entry.path());
      }
    }
    std::ranges::sort(recording_files, std::greater{});
  }
  const bool columns = width >= 900 * scale;
  const float listWidth = columns ? 400 * scale : width;
  ImGui::BeginGroup();
  Page::beginCard("##recording_list", "SAVED MATCHES", listWidth, Page::MID,
                  scale, sidebar_font);
  if (recording_files.empty())
  {
    ImGui::TextWrapped(
        "No recordings yet. Save a completed match from Matches to add it "
        "here.");
  }
  for (std::size_t i = 0; i < recording_files.size(); ++i)
  {
    ImGui::PushID(int(i));
    const auto filename = recording_files[i].stem().string();
    const auto label =
        filename.size() > 38 ? filename.substr(0, 35) + "..." : filename;
    ImGui::PushTextWrapPos(0);
    if (ImGui::Selectable(label.c_str(), recording_index == int(i),
                          ImGuiSelectableFlags_None, ImVec2(0, 44 * scale)))
    {
      recording_index = int(i);
      recording_preview =
          Recordings::load(recording_files[i], recording_preview_error);
    }
    if (ImGui::IsItemHovered())
    {
      ImGui::SetTooltip("%s", filename.c_str());
    }
    ImGui::PopTextWrapPos();
    ImGui::PopID();
  }
  Page::endCard(scale);
  ImGui::EndGroup();
  if (columns)
  {
    ImGui::SameLine(0, 6 * scale);
  }
  const float detailWidth = columns ? width - listWidth - (6 * scale) : width;
  Page::beginCard("##recording_detail", "RECORDING DETAILS", detailWidth,
                  Page::LIGHT, scale, sidebar_font);
  if (recording_index < 0)
  {
    ImGui::TextWrapped(
        "Select a recording to see its details and replay options.");
  }
  else if (!recording_preview)
  {
    ImGui::TextWrapped("Could not read recording: %s",
                       recording_preview_error.c_str());
  }
  else
  {
    const auto& r = *recording_preview;
    ImGui::TextWrapped(
        "%s", recording_files[recording_index].filename().string().c_str());
    ImGui::PushFont(sidebar_font, 17);
    ImGui::TextWrapped("%s %d - %d %s", r.setup.sides[0].name.c_str(),
                       r.homeScore, r.awayScore, r.setup.sides[1].name.c_str());
    ImGui::PopFont();
    ImGui::TextWrapped("Recorded %s / seed %u", r.created.c_str(),
                       r.setup.matchSeed);
    ImGui::TextWrapped("Engine build: %s", r.build.c_str());
    ImGui::TextWrapped(
        "%zu decisions / %s", r.decisions.size(),
        r.details.empty() ? "without full detail" : "with full detail");
    if (Page::button("Open", true, scale, sidebar_font))
    {
      loadRecording(recording_files[recording_index]);
    }
    ImGui::SameLine(0, 6 * scale);
    if (Page::button("Replay with", false, scale, sidebar_font))
    {
      loadRecording(recording_files[recording_index], sandboxBrains(),
                    sandboxBodies(), sandboxHandshake(), sandboxPossession());
    }
    ImGui::TextWrapped(
        "Open uses the recorded engine selections. Replay with uses the "
        "brains, bodies, handshake and possession selected in Set-up. Both "
        "open the Review page with comparison results.");
  }
  Page::endCard(scale);
}

void MatchSandboxScene::renderReviewPage()
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const Page::Style style(setup_regular_font, scale);
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float gutter = (6 * scale) + ImGui::GetStyle().ScrollbarSize;
  const bool columns = available.x >= 1000 * scale;
  const float panel = columns ? 400 * scale : available.x - gutter;
  const auto controls = [&]
  {
    Page::header("Review", loaded ? "Recorded match" : "Current / last match",
                 panel, scale, sidebar_font, setup_regular_font, nullptr, true);
    Page::beginCard("##review_navigation", "REVIEW MATCH", panel, Page::MID,
                    scale, sidebar_font);
    if (Page::button("Recordings", false, scale, sidebar_font))
    {
      mode = 5;
    }
    ImGui::SameLine(0, 6 * scale);
    if (Page::button("Matches", false, scale, sidebar_font))
    {
      mode = 0;
    }
    if (!recorder)
    {
      ImGui::TextWrapped(
          "Play a match or open a recording to start reviewing.");
    }
    else
    {
      if (!last_result.empty())
      {
        ImGui::TextWrapped("%s", last_result.c_str());
      }
      const char* tabs[] = {"Pitch", "Analysis", "Comparison"};
      for (int i = 0; i < 3; ++i)
      {
        if (i)
        {
          ImGui::SameLine(0, 6 * scale);
        }
        if (Page::button(tabs[i], reviewTab == i, scale, sidebar_font))
        {
          reviewTab = i;
        }
      }
    }
    Page::endCard(scale);
    if (recorder)
    {
      Page::beginCard("##review_transport", "TIMELINE / PLAYBACK", panel,
                      Page::DARK, scale, sidebar_font);
      review.renderPanel(*recorder, nullptr, nullptr, nullptr);
      Page::endCard(scale);
    }
  };
  const auto stage = [&](float width, float height)
  {
    Page::beginCard("##review_stage",
                    reviewTab == 0   ? "PITCH"
                    : reviewTab == 1 ? "ENGINE ANALYSIS"
                                     : "RECORDING COMPARISON",
                    width, Page::LIGHT, scale, sidebar_font);
    ImGui::BeginChild("##review_content",
                      ImVec2(0, std::max(300 * scale, height - (56 * scale))),
                      ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushTextWrapPos(0);
    if (!recorder)
    {
      ImGui::TextWrapped(
          "No match available. Open a recording or play a match first.");
    }
    else if (reviewTab == 0)
    {
      review.renderPitch(*recorder, nullptr, match_home, match_away,
                         &inspector);
    }
    else if (reviewTab == 1)
    {
      debugger.renderContents(*recorder, nullptr, nullptr, review.cursor(),
                              review.shownEngine(), inspector);
    }
    else if (loaded && replay_info)
    {
      if (comparison.render(*loaded, *replay_info, recorder.get(), review,
                            inspector))
      {
        reviewTab = 0;
      }
    }
    else
    {
      ImGui::TextWrapped(
          "Open a saved recording to compare its replay with the original "
          "result and decisions.");
    }
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    Page::endCard(scale);
  };
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  if (columns)
  {
    ImGui::BeginChild("##review_controls", ImVec2(panel + gutter, available.y),
                      ImGuiChildFlags_AlwaysUseWindowPadding);
    controls();
    ImGui::EndChild();
    ImGui::SameLine(0, 6 * scale);
    stage(ImGui::GetContentRegionAvail().x - gutter, available.y);
  }
  else
  {
    controls();
    stage(panel, std::max(440 * scale, available.y));
  }
  ImGui::PopStyleVar();
  if (recorder)
  {
    if (const auto tick = debugger.takeSeekRequest())
    {
      review.seek(*recorder, *tick);
      reviewTab = 0;
    }
  }
}

void MatchSandboxScene::kickOff()
{
  if (new_seed_each_match)
  {
    match_seed = std::random_device{}();
  }
  GameController& controller = guiView->getController();
  // The home side is the managed one: its touchline and Play mode.
  controller.selectManagedTeam(sides[HOME].team);

  // The match is built from its setup, so its recording can build it again.
  match_setup =
      MatchSetup::capture(controller, sides[HOME].team, sides[AWAY].team,
                          match_seed, full_familiarity);
  MatchScene::Sandbox sandbox;
  sandbox.make_engine = [this]()
  {
    std::string error;
    auto engine = match_setup->build(guiView->getController(), error);
    if (!engine)
    {
      status = "Could not build the match: " + error;
    }
    return engine;
  };
  // A fresh log for this match; the previous one is discarded.
  loaded.reset();
  recorder = std::make_unique<SandboxRecorder>();
  debugger.reset();
  review.reset();
  inspector.reset();
  match_debug = debug_at_kick_off;
  match_engine_tools = false;
  match_home = sides[HOME].team;
  match_away = sides[AWAY].team;
  sandbox.recorder = recorder.get();
  // In the match: Debug and True view buttons in its controls, their
  // sections in its side panel, and the moment and shapes on its pitch.
  sandbox.on_side_panel = [this](MatchEngine& engine, bool& paused)
  { renderMatchPanel(engine, paused); };
  sandbox.on_debug_panel = [this](MatchEngine& engine, bool& paused)
  {
    if (!recorder)
    {
      return;
    }
    debugger.renderContents(
        *recorder, &engine, &paused, review.cursor(),
        review.shownEngine() ? review.shownEngine() : &engine, inspector);
    if (const auto tick = debugger.takeSeekRequest())
    {
      paused = true;
      review.seek(*recorder, *tick);
    }
  };
  sandbox.debugger_visible = [this] { return match_debug; };
  sandbox.set_debugger = [this](bool value) { match_debug = value; };
  sandbox.engine_tools_visible = [this](bool value)
  { match_engine_tools = value; };
  sandbox.set_true_view = [this](bool value) { match_true_view = value; };
  sandbox.shown_engine = [this]() -> const MatchEngine*
  { return review.shownEngine(); };
  sandbox.true_view = [this] { return match_true_view; };
  sandbox.on_view_overlay = [this](const IMatchRenderer& renderer,
                                   const MatchEngine& engine,
                                   const MatchRenderSnapshot& snapshot,
                                   const MatchScene::Sandbox::ViewClick& click)
  {
    // The true view takes the click (A, or B with Ctrl) and hands A to the
    // inspector; without it the inspector picks.
    if (match_true_view)
    {
      const PlayerID picked =
          true_view.drawOverlay(renderer, engine, snapshot, click.clicked,
                                click.ctrl, ImVec2(click.x, click.y));
      if (picked != 0 && !click.ctrl)
      {
        inspector.selected = picked;
      }
    }
    if ((match_debug || match_engine_tools) && recorder)
    {
      inspector.drawOverlays(*recorder, engine, renderer,
                             click.clicked && !match_true_view);
    }
  };
  const std::uint32_t seed = match_seed;
  const std::array<BrainId, 2> brains = match_setup->brains;
  const std::array<BodyId, 2> bodies = match_setup->bodies;
  const HandshakeId handshake = match_setup->handshake;
  const float homeAdvantage = match_setup->homeAdvantage;
  sandbox.on_finish = [this, seed, brains, bodies, handshake,
                       homeAdvantage](const MatchEngine& engine)
  {
    const GameController& gameController = guiView->getController();
    const auto home = gameController.getTeamById(sides[HOME].team);
    const auto away = gameController.getTeamById(sides[AWAY].team);
    std::string score =
        std::format("{} {} - {} {}", home ? home->get().getName() : "Home",
                    engine.getHomeScore(), engine.getAwayScore(),
                    away ? away->get().getName() : "Away");
    if (engine.hasShootout())
    {
      score += std::format(" ({} - {} pens)", engine.getShootoutScore(true),
                           engine.getShootoutScore(false));
    }
    last_result = std::format(
        "{}  (seed {}; brains {}; bodies {}; handshake {}; home advantage "
        "{:.2f})",
        score, seed, brainPairName(brains), bodyPairName(bodies),
        handshakeName(handshake), homeAdvantage);
  };
  guiView->overlayScene(std::make_unique<MatchScene>(
      guiView, sides[HOME].team, sides[AWAY].team, std::move(sandbox)));
}

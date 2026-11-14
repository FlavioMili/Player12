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
#include "tools/match_sandbox_drill_scene.h"
#include "tools/match_sandbox_contact_view.h"
#include "tools/match_sandbox_bench_scene.h"
#include "tools/match_sandbox_measure_scene.h"
#include "tools/match_sandbox_pickers.h"
#include "tools/match_sandbox_setup_ui.h"
#include "tools/match_sandbox_typography.h"
#include "tools/match_sandbox_test_teams.h"
#include "tools/match_sandbox_training_scene.h"
#include "tools/match_sandbox_shot_map.h"
#include "tools/match_sandbox_true_view.h"

namespace
{
constexpr std::size_t HOME = 0;
constexpr std::size_t AWAY = 1;
constexpr float FOOTER_HEIGHT = 92.0f;
constexpr float SLIDER_LABEL_WIDTH = 150.0f;
constexpr float OVERALL_COLUMN_WIDTH = 34.0f;
constexpr float POSITION_COLUMN_WIDTH = 38.0f;
/** Lineup coordinates closer than this are the same formation slot. */
constexpr float SLOT_TOLERANCE = 0.02f;

struct SliderInfo
{
  const char* key;
  float StrategySliders::* value;
  const char* helpKey;
};

// The five instructions, labelled as on the tactics screen.
constexpr std::array<SliderInfo, 5> SLIDERS{{
    {"STRATEGY_PRESSING", &StrategySliders::pressing, "TACTIC_PRESSING_HELP"},
    {"STRATEGY_RISK_TAKING", &StrategySliders::riskTaking, "TACTIC_RISK_HELP"},
    {"STRATEGY_OFFENSIVE_BIAS", &StrategySliders::offensiveBias,
     "TACTIC_OFFENSIVE_HELP"},
    {"STRATEGY_WIDTH_USAGE", &StrategySliders::widthUsage,
     "TACTIC_WIDTH_HELP"},
    {"STRATEGY_COMPACTNESS", &StrategySliders::compactness,
     "TACTIC_COMPACTNESS_HELP"},
}};

std::optional<std::reference_wrapper<Team>> mutableTeam(GUIView* view,
                                                        TeamID id)
{
  const auto data = view->getController().getGameData();
  if (!data) return std::nullopt;
  return data->getTeam(id);
}

std::vector<const Player*> squadOf(const GameController& controller,
                                   TeamID id)
{
  std::vector<const Player*> squad;
  for (const auto& player : controller.getPlayersForTeam(id))
    squad.push_back(&player.get());
  return squad;
}

std::string leagueName(const GameController& controller, LeagueID id)
{
  for (const auto& league : controller.getLeagues())
    if (league.get().getId() == id) return league.get().getName();
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
    instruction.role = TacticalRole::Standard;
  return instruction;
}

/** The assistant's roles and duties for every starter (as on the tactics
 * screen), keeping the in-possession shape. */
void suggestRoles(Strategy& strategy, const Lineup& lineup)
{
  for (const auto& positioned : lineup.getOutfieldPlayers())
  {
    if (!positioned.player) continue;
    SlotInstruction instruction = slotAt(strategy, positioned.position);
    const RoleFamily family = Tactics::familyForSlot(positioned.position);
    instruction.role = Tactics::suggestedRole(*positioned.player, family);
    instruction.duty = Tactics::suggestedDuty(*positioned.player, family);
    strategy.setSlot(instruction);
  }
  if (const Player* goalkeeper = lineup.getGoalkeeper())
    strategy.setKeeperRole(
        Tactics::suggestedRole(*goalkeeper, RoleFamily::Goalkeeper));
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
      { return std::abs(left.*slider.value - right.*slider.value) < 0.001f; });
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
  if (lineup.swapPlayers(chosen.getId(), current.getId())) return;
  if (lineup.bringIn(&chosen, current.getId())) return;
  const auto& outfield = lineup.getOutfieldPlayers();
  const auto spotOf = [&outfield](PlayerID id) -> std::optional<Vector2F>
  {
    for (const auto& positioned : outfield)
      if (positioned.player && positioned.player->getId() == id)
        return positioned.position;
    return std::nullopt;
  };
  const auto from = spotOf(current.getId());
  const auto to = spotOf(chosen.getId());
  if (!from || !to) return;
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
    const std::string fontPath = AssetPaths::file("assets/fonts/Manrope-Bold.ttf");
    const auto fontConfig = SandboxTypography::manropeConfig();
    sidebar_font = ImGui::GetIO().Fonts->AddFontFromFileTTF(fontPath.c_str(), 14.0f, &fontConfig);
    setup_regular_font = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        AssetPaths::file("assets/fonts/Manrope-Regular.ttf").c_str(), 16.0f, &fontConfig);
    setup_medium_font = ImGui::GetIO().Fonts->AddFontFromFileTTF(
        AssetPaths::file("assets/fonts/Manrope-Medium.ttf").c_str(), 14.0f, &fontConfig);
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
      if (!maximised) SDL_MaximizeWindow(windows[0]);
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
    if (!leagues.empty() &&
        team.get().getLeagueId() == leagues.front().get().getId())
      candidates.push_back(team.get().getId());
  if (candidates.size() < 2)
    for (const auto& team : teams) candidates.push_back(team.get().getId());
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
      if (lower(drills[index]->name()) == lower(drill_pending))
      {
        mode = 1;
        drill_index = static_cast<int>(index);
        if (drill_view == 2)
          guiView->overlayScene(
              std::make_unique<ShotMapScene>(guiView, *drills[index], true));
        else if (drill_view == 1)
          guiView->overlayScene(
              std::make_unique<DrillMeasureScene>(guiView, *drills[index], true));
        else
          guiView->overlayScene(
              std::make_unique<DrillScene>(guiView, *drills[index]));
      }
    drill_pending.clear();
  }
  if (!kick_off_pending) return;
  kick_off_pending = false;
  if (sides[HOME].team != 0 && sides[AWAY].team != 0) kickOff();
}

std::optional<std::size_t> MatchSandboxScene::currentPreset(
    const Lineup& lineup)
{
  const auto& outfield = lineup.getOutfieldPlayers();
  for (std::size_t index = 0; index < Formation::PRESETS.size(); ++index)
  {
    const auto& slots = Formation::PRESETS[index].slots;
    if (outfield.size() != slots.size()) continue;
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
    if (matches) return index;
  }
  return std::nullopt;
}

void MatchSandboxScene::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  const float scale = Theme::scale();
  const ImVec4 contentBackground = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        ImVec4(22/255.0f, 19/255.0f, 16/255.0f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * scale, 24.0f * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::Begin("##match_sandbox", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();

  renderSidebar();
  ImGui::SameLine(0.0f, 6.0f * scale);
  // The inset shell follows the reference; the content retains its existing
  // internal padding and the navigation owns any overflow scrolling.
  ImGui::PushStyleVar(
      ImGuiStyleVar_WindowPadding,
      mode == 4 ? ImVec2(0, 0) :
      ImVec2(Theme::Space::XL * scale, Theme::Space::L * scale));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, mode == 4 ?
      ImVec4(22/255.0f, 19/255.0f, 16/255.0f, 1) : contentBackground);
  if (mode == 4)
  {
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(15,13,11,255));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, IM_COL32(155,208,107,255));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(170,215,130,255));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, IM_COL32(170,215,130,255));
  }
  ImGui::BeginChild("##sandbox_content", ImVec2(0.0f, 0.0f),
                    ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PopStyleColor(mode == 4 ? 5 : 1);
  ImGui::PopStyleVar();
  if (mode != 4)
    UI::pageHeader("Match sandbox",
                   "Pick two clubs and both sides' tactics, then play the "
                   "match as the home side, or run a drill that isolates one "
                   "behaviour. Nothing here is saved.");
  if (mode == 4)
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
    const float gap = Theme::Space::L * scale;
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float columnWidth = std::floor((available.x - gap) * 0.5f);
    const float columnHeight =
        std::max(200.0f * scale, available.y - FOOTER_HEIGHT * scale);
    renderSide(sides[HOME], true, columnWidth, columnHeight);
    ImGui::SameLine(0.0f, gap);
    renderSide(sides[AWAY], false, columnWidth, columnHeight);

    renderFooter();
  }
  ImGui::EndChild();
  ImGui::End();
  renderDebugTools(nullptr, nullptr);
  if ((show_review || show_debugger) && loaded && replay_info)
    comparison.render(*loaded, *replay_info, recorder.get(), review, inspector);
  renderRecordings();
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
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(12 * scale, 16 * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2 * scale));
  ImGui::BeginChild("##sandbox_sidebar", ImVec2(220 * scale, 0),
                    ImGuiChildFlags_AlwaysUseWindowPadding);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 panel = ImGui::GetWindowPos();
  const ImVec2 size = ImGui::GetWindowSize();
  const ImVec2 brand = ImGui::GetCursorScreenPos();
  ImGui::PushFont(sidebar_font, 24);
  const ImVec2 extent = ImGui::CalcTextSize("Player 12");
  draw->AddText(ImVec2(brand.x + (196 * scale - extent.x) / 2,
                       brand.y + (80 * scale - extent.y) / 2),
                cream, "Player 12");
  ImGui::PopFont();
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
      show_recordings = false;
    }
    if (selected || ImGui::IsItemHovered())
      draw->AddRectFilled(
          row, ImVec2(row.x + 196 * scale, row.y + 44 * scale),
          selected ? IM_COL32(46, 39, 32, 255) : IM_COL32(31, 26, 21, 255));
    if (selected)
      draw->AddRectFilled(row, ImVec2(row.x + 3 * scale, row.y + 44 * scale),
                          white);
    if (ImGui::IsItemFocused())
      draw->AddRect(row, ImVec2(row.x + 196 * scale, row.y + 44 * scale),
                    IM_COL32(133, 122, 110, 255), 0, 0, scale);
    const ImU32 ink = selected ? white : IM_COL32(196, 186, 174, 255);
    const auto pt = [&](float x, float y)
    {
      return ImVec2(row.x + (14 + x * 20 / 24) * scale,
                    row.y + (12 + y * 20 / 24) * scale);
    };
    const auto line = [&](float x, float y, float xx, float yy)
    { draw->AddLine(pt(x, y), pt(xx, yy), ink, 1.5f * scale); };
    if (icon == 0)
    {
      draw->AddRect(pt(3, 5), pt(21, 21), ink, 0, 0, 1.5f * scale);
      line(3, 10, 21, 10);
      line(8, 3, 8, 7);
      line(16, 3, 16, 7);
    }
    else if (icon == 1)
    {
      draw->AddRect(pt(3, 4), pt(21, 20), ink, 0, 0, 1.5f * scale);
      line(7, 15, 16, 8);
      line(12, 8, 16, 8);
      line(16, 8, 16, 12);
      draw->AddCircle(pt(7, 8), 1.5f * scale, ink, 12, 1.5f * scale);
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
    else
    {
      line(4, 6, 20, 6);
      line(4, 12, 20, 12);
      line(4, 18, 20, 18);
      for (const ImVec2 knob : {ImVec2(9, 6), ImVec2(15, 12), ImVec2(8, 18)})
      {
        draw->AddCircleFilled(pt(knob.x, knob.y), 2.5f * scale, bg);
        draw->AddCircle(pt(knob.x, knob.y), 2.5f * scale, ink, 12,
                        1.5f * scale);
      }
    }
    ImVec2 pos(row.x + 46 * scale, row.y + 15 * scale);
    for (const char* c = label; *c; ++c)
    {
      draw->AddText(pos, selected ? white : cream, c, c + 1);
      pos.x += ImGui::CalcTextSize(c, c + 1).x + 1.5f * scale;
    }
  };
  item("MATCHES", 0, 0);
  item("DRILLS", 1, 1);
  item("TRAINING", 2, 2);
  item("TOOLS", 3, 3);
  // The sandbox's settings destination follows the reference footer placement.
  ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), size.y - 69 * scale));
  const ImVec2 divider = ImGui::GetCursorScreenPos();
  draw->AddLine(divider, ImVec2(divider.x + 196 * scale, divider.y),
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
  const float scale = Theme::scale();
  const float listWidth = 260.0f * scale;
  UI::beginCard("##drill_list", "Drills", ImVec2(listWidth, 0.0f), true);
  for (std::size_t index = 0; index < drills.size(); ++index)
    if (ImGui::Selectable(drills[index]->name(),
                          static_cast<std::size_t>(drill_index) == index))
      drill_index = static_cast<int>(index);
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled(
      "Movement drills run by the AI, by a scripted bot on "
      "the stick, or by you. Shooting drills are on the way.");
  ImGui::PopTextWrapPos();
  UI::endCard();
  ImGui::SameLine();
  if (drills.empty()) return;
  Drill& drill = *drills[static_cast<std::size_t>(drill_index)];
  UI::beginCard("##drill_page", drill.name(), ImVec2(0.0f, 0.0f), true);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(drill.summary());
  ImGui::PopTextWrapPos();
  ImGui::SeparatorText("Settings");
  ImGui::PushItemWidth(320.0f * scale);
  drill.renderSettings();
  ImGui::InputScalar("Seed", ImGuiDataType_U32, &drill.seed);
  ImGui::PopItemWidth();
  ImGui::Spacing();
  if (UI::primaryButton("Watch"))
    guiView->overlayScene(std::make_unique<DrillScene>(guiView, drill));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Run the drill on the pitch, step by step if you like, "
                      "with the debugger windows.");
  ImGui::SameLine();
  if (ImGui::Button("Measure"))
    guiView->overlayScene(std::make_unique<DrillMeasureScene>(guiView, drill));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Run the drill headless over a range of a setting: "
                      "curves, tables and CSV.");
  if (drill.offersShotMap())
  {
    ImGui::SameLine();
    if (ImGui::Button("Shot map"))
      guiView->overlayScene(std::make_unique<ShotMapScene>(guiView, drill));
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("A grid of spots, many shots each: conversion against "
                        "the engine's xG as a heatmap, and the calibration.");
  }
  UI::endCard();
}

void MatchSandboxScene::renderVisualisers()
{
  const float scale = Theme::scale();
  UI::beginAutoHeightCard("##visualiser_page", "Contact difficulty");
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(
      "How hard a touch or strike is across his reach (Binturong body, "
      "the sandbox's tuning, editable): his reach circle coloured by where "
      "he meets the ball, a ring by where it comes from and a ring by where "
      "he sends it, with every condition's share of the angle and weight "
      "jobs, and every knob of the contact.");
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  if (UI::primaryButton("Open"))
    guiView->overlayScene(std::make_unique<ContactViewScene>(guiView));
  ImGui::SameLine(0.0f, Theme::Space::L * scale);
  ImGui::TextDisabled("Also: match_sandbox --contact-view");
  UI::endCard();

  UI::beginAutoHeightCard("##true_view_page", "True view");
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(
      "One player in 3D as the Binturong body models him, with no figure on "
      "top: the body cylinder, the lines between feet, thigh, chest and "
      "head, each part's reach with its comfort gap, dead zone and stretch, "
      "each technique's own range, jumping, falling and getting up, and the "
      "personal space the brain keeps, with where every number comes from.");
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  if (UI::primaryButton("Open##true_view"))
    guiView->overlayScene(std::make_unique<TrueViewScene>(guiView));
  ImGui::SameLine(0.0f, Theme::Space::L * scale);
  ImGui::TextDisabled("Also: match_sandbox --true-view");
  UI::endCard();

  UI::beginAutoHeightCard("##bench_page", "Technique bench");
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(
      "A catalogue of pass and shot tests (short and long, ground, driven, "
      "lofted, chipped and curled, still and moving balls, standing and on "
      "the run, at angles to the body) swept over Passing or Shooting with "
      "many seeds, run with the tuning the contact view edits: success "
      "curves and a grid, filtered by tag, compared with the run before.");
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  if (UI::primaryButton("Open##bench"))
    guiView->overlayScene(std::make_unique<TechniqueBenchScene>(guiView));
  ImGui::SameLine(0.0f, Theme::Space::L * scale);
  ImGui::TextDisabled("Also: match_sandbox --bench-view, or --bench headless");
  UI::endCard();
}

void MatchSandboxScene::renderTraining()
{
  const float scale = Theme::scale();
  const GameController& controller = guiView->getController();
  const TeamID clubId = sides[HOME].team;
  const std::vector<const Player*> squad = squadOf(controller, clubId);
  std::string clubName;
  for (const auto& team : controller.getTeams())
    if (team.get().getId() == clubId) clubName = team.get().getName();
  // A new club: the first of its squad, as many as fit on the pitch.
  if (clubId != training_club)
  {
    training_club = clubId;
    training_picks.clear();
    for (const Player* player : squad)
      if (training_picks.size() < TrainingDrill::MAX_PLAYERS)
        training_picks.push_back(player->getId());
  }

  const std::vector<TrainingSessionInfo>& sessions = trainingSessions();
  UI::beginCard("##session_list", "Sessions", ImVec2(260.0f * scale, 0.0f), true);
  for (std::size_t index = 0; index < sessions.size(); ++index)
    if (ImGui::Selectable(sessions[index].name,
                          static_cast<std::size_t>(session_index) == index))
      session_index = static_cast<int>(index);
  ImGui::Spacing();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextDisabled("The home club trains (pick it on the Match tab), the "
                      "whole squad at once.");
  ImGui::PopTextWrapPos();
  UI::endCard();
  ImGui::SameLine();
  if (sessions.empty()) return;
  const TrainingSessionInfo& info =
      sessions[static_cast<std::size_t>(session_index)];
  UI::beginCard("##session_page", info.name, ImVec2(0.0f, 0.0f), true);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(info.summary);
  ImGui::PopTextWrapPos();

  ImGui::SeparatorText(std::format("{}: {} of {} picked (up to {})", clubName,
                                   training_picks.size(), squad.size(),
                                   TrainingDrill::MAX_PLAYERS)
                           .c_str());
  const bool full = training_picks.size() >= TrainingDrill::MAX_PLAYERS;
  const float tableHeight = std::max(
      120.0f * scale,
      ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.0f);
  if (ImGui::BeginTable("##training_squad", 6,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingFixedFit,
                        ImVec2(0.0f, tableHeight)))
  {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("");
    ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Pos");
    ImGui::TableSetupColumn("Pace");
    ImGui::TableSetupColumn("Physicality");
    ImGui::TableSetupColumn("Stamina");
    ImGui::TableHeadersRow();
    for (const Player* player : squad)
    {
      const PlayerID id = player->getId();
      const auto found = std::ranges::find(training_picks, id);
      bool picked = found != training_picks.end();
      const auto stat = [player](const char* name)
      {
        const auto& stats = player->getStats();
        const auto at = stats.find(name);
        return at == stats.end() ? 0.0f : at->second;
      };
      ImGui::PushID(static_cast<int>(id));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::BeginDisabled(!picked && full);
      if (ImGui::Checkbox("##pick", &picked))
      {
        if (picked)
          training_picks.push_back(id);
        else
          training_picks.erase(found);
      }
      ImGui::EndDisabled();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(player->getName().c_str());
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

  ImGui::BeginDisabled(training_picks.empty());
  if (UI::primaryButton("Start session"))
  {
    // The picks in squad order.
    std::vector<TrainingPlayer> trainees;
    for (const Player* player : squad)
      if (std::ranges::find(training_picks, player->getId()) !=
          training_picks.end())
        trainees.push_back({player->getName(), player->getRole(),
                            player->getStats(), player->getHeight(),
                            player->getFoot()});
    guiView->overlayScene(std::make_unique<TrainingSessionScene>(
        guiView, info.make(std::move(trainees)), clubName));
  }
  ImGui::EndDisabled();
  UI::endCard();
}

void MatchSandboxScene::renderDebugTools(MatchEngine* live, bool* livePaused)
{
  if (!recorder) return;
  if (show_review)
    review.render(*recorder, live, livePaused, match_home, match_away,
                  &inspector, &show_review);
  // The inspector reads the moment under review, or the live match.
  const MatchEngine* inspected =
      review.shownEngine() ? review.shownEngine() : live;
  if (show_debugger)
    debugger.render(*recorder, live, livePaused, review.cursor(), inspected,
                    inspector, &show_debugger);
  // A clicked log row rewinds the review (and pauses the live match).
  if (const auto tick = debugger.takeSeekRequest())
  {
    if (livePaused) *livePaused = true;
    review.seek(*recorder, *tick);
  }
}

void MatchSandboxScene::renderMatchPanel(MatchEngine& engine, bool& paused)
{
  if (match_true_view && ImGui::CollapsingHeader("True view", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::PushID("true_view");
    true_view.renderSettings(review.shownEngine() ? *review.shownEngine() : engine);
    ImGui::PopID();
  }
  if (!match_debug || !recorder) return;
  if (ImGui::CollapsingHeader("Review", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::PushID("review");
    review.renderPanel(*recorder, &engine, &paused, &inspector);
    ImGui::PopID();
  }
  if (ImGui::CollapsingHeader("Debugger", ImGuiTreeNodeFlags_DefaultOpen))
  {
    // The log and the inspector's tabs scroll inside a box whose height
    // the bottom edge drags.
    const MatchEngine* inspected = review.shownEngine() ? review.shownEngine() : &engine;
    if (ImGui::BeginChild("##debug_feed", ImVec2(0.0f, 560.0f * Theme::scale()),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY))
      debugger.renderContents(*recorder, &engine, &paused, review.cursor(), inspected,
                              inspector);
    ImGui::EndChild();
  }
  // A clicked log row rewinds the review (and pauses the live match).
  if (const auto tick = debugger.takeSeekRequest())
  {
    paused = true;
    review.seek(*recorder, *tick);
  }
}

void MatchSandboxScene::renderSide(Side& side, bool home, float width,
                                   float height)
{
  ImGui::PushID(home ? "home" : "away");
  UI::beginCard("##side",
                home ? "Home - you manage this side" : "Away - played by the AI",
                ImVec2(width, height), true);
  renderClubPicker(side, home);
  if (const auto team = mutableTeam(guiView, side.team))
  {
    ImGui::Spacing();
    renderFormation(team->get());
    ImGui::Spacing();
    renderSelection(team->get());
    ImGui::Spacing();
    renderInstructions(team->get().getStrategy());
  }
  UI::endCard();
  ImGui::PopID();
}

void MatchSandboxScene::renderClubPicker(Side& side, bool home)
{
  const GameController& controller = guiView->getController();
  const auto current = controller.getTeamById(side.team);
  const std::string preview =
      current ? std::format("{}  ·  {}", current->get().getName(),
                            leagueName(controller, current->get().getLeagueId()))
              : std::string("Choose a club");
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::clubAccent(side.team));
  ImGui::TextUnformatted(home ? "HOME" : "AWAY");
  ImGui::PopStyleColor();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1.0f);
  if (!ImGui::BeginCombo("##club", preview.c_str(),
                         ImGuiComboFlags_HeightLarge))
    return;
  for (const auto& league : controller.getLeagues())
  {
    ImGui::SeparatorText(league.get().getName().c_str());
    for (const auto& team : controller.getTeams())
    {
      if (team.get().getLeagueId() != league.get().getId()) continue;
      const TeamID id = team.get().getId();
      ImGui::PushID(id);
      if (ImGui::Selectable(team.get().getName().c_str(), id == side.team))
        side.team = id;
      ImGui::PopID();
    }
  }
  ImGui::EndCombo();
}

void MatchSandboxScene::renderFormation(Team& team)
{
  const GameController& controller = guiView->getController();
  Lineup& lineup = team.getLineup();
  UI::sectionLabel("Formation");
  const std::optional<std::size_t> active = currentPreset(lineup);
  for (std::size_t index = 0; index < Formation::PRESETS.size(); ++index)
  {
    if (index > 0) ImGui::SameLine();
    const Formation::Preset& preset = Formation::PRESETS[index];
    if (UI::toggleButton(preset.name, active == index))
    {
      // A new shape picks the best XI for it and the roles that fit them.
      const std::vector<const Player*> squad = squadOf(controller, team.getId());
      Formation::autoPick(lineup, preset, squad, controller.getStatsConfig());
      suggestRoles(team.getStrategy(), lineup);
    }
  }
  if (!active)
  {
    std::vector<PlayerRole> roles;
    for (const auto& positioned : lineup.getOutfieldPlayers())
      roles.push_back(Lineup::roleAt(positioned.position));
    ImGui::SameLine();
    ImGui::TextColored(Theme::palette().muted, "Current: %s",
                       formationLabel(roles).c_str());
  }
  if (UI::secondaryButton("Best XI", ImVec2(0, 0), UI::ButtonSize::COMPACT))
  {
    const std::vector<const Player*> squad = squadOf(controller, team.getId());
    const Formation::Preset& preset =
        Formation::PRESETS[active.value_or(0)];
    Formation::autoPick(lineup, preset, squad, controller.getStatsConfig());
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Pick the strongest eleven for the formation.");
  ImGui::SameLine();
  if (UI::secondaryButton("Suggest roles", ImVec2(0, 0),
                          UI::ButtonSize::COMPACT))
    suggestRoles(team.getStrategy(), lineup);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Give every starter the role and duty that suit him.");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(170.0f * Theme::scale());
  if (ImGui::BeginCombo("##shape", LOC("TACTIC_SHAPE_PRESETS")))
  {
    for (std::size_t index = 0;
         index < static_cast<std::size_t>(PossessionShape::COUNT); ++index)
    {
      const auto shape = static_cast<PossessionShape>(index);
      if (ImGui::Selectable(LOC(Tactics::possessionShapeKey(shape))))
        applyPossessionShape(team.getStrategy(), lineup, shape);
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Where the outfield players move with the ball.");
}

void MatchSandboxScene::renderInstructions(Strategy& strategy)
{
  UI::sectionLabel("Team instructions");
  StrategySliders sliders = strategy.getSliders();
  for (std::size_t index = 0; index < Lab::TACTIC_PRESETS.size(); ++index)
  {
    if (index > 0) ImGui::SameLine();
    const Lab::TacticPreset& preset = Lab::TACTIC_PRESETS[index];
    const std::string label(preset.name);
    if (UI::toggleButton(label.c_str(), sameSliders(sliders, preset.sliders)))
      strategy.setAllSliders(preset.sliders);
  }
  const float labelWidth = SLIDER_LABEL_WIDTH * Theme::scale();
  bool changed = false;
  for (const SliderInfo& slider : SLIDERS)
  {
    ImGui::PushID(slider.key);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(LOC(slider.key));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", LOC(slider.helpKey));
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    changed |= ImGui::SliderFloat("##value", &(sliders.*slider.value), 0.0f,
                                  1.0f, "%.2f");
    ImGui::PopID();
  }
  if (changed) strategy.setAllSliders(sliders);
}

void MatchSandboxScene::renderSelection(Team& team)
{
  const GameController& controller = guiView->getController();
  const StatsConfig& config = controller.getStatsConfig();
  Lineup& lineup = team.getLineup();
  Strategy& strategy = team.getStrategy();
  const std::vector<const Player*> squad = squadOf(controller, team.getId());
  UI::sectionLabel("Starting XI, roles and duties");

  constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_BordersInnerH |
                                    ImGuiTableFlags_SizingStretchProp;
  if (!ImGui::BeginTable("##xi", 5, flags)) return;
  const float scale = Theme::scale();
  ImGui::TableSetupColumn("Pos", ImGuiTableColumnFlags_WidthFixed,
                          POSITION_COLUMN_WIDTH * scale);
  ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch, 2.2f);
  ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthStretch, 1.6f);
  ImGui::TableSetupColumn("Duty", ImGuiTableColumnFlags_WidthStretch, 1.0f);
  ImGui::TableSetupColumn("Fit", ImGuiTableColumnFlags_WidthFixed,
                          OVERALL_COLUMN_WIDTH * scale);
  ImGui::TableHeadersRow();

  // The player picker of one slot: the whole squad, best first. Picking a
  // player already in the XI trades places with him.
  std::vector<const Player*> ordered = squad;
  std::ranges::sort(ordered,
                    [&config](const Player* left, const Player* right)
                    { return left->getOverall(config) > right->getOverall(config); });
  const auto playerPicker = [&](const Player& current, bool goalkeeper)
  {
    ImGui::SetNextItemWidth(-1.0f);
    if (!ImGui::BeginCombo("##player", playerLabel(current, config).c_str(),
                           ImGuiComboFlags_HeightLarge))
      return;
    for (const Player* candidate : ordered)
    {
      // The goalkeeper's slot takes keepers, outfield slots outfielders.
      if ((candidate->getRole() == PlayerRole::GK) != goalkeeper) continue;
      const bool starter = lineup.isStarter(candidate->getId());
      ImGui::PushID(static_cast<int>(candidate->getId()));
      if (starter && candidate != &current)
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::palette().muted);
      if (ImGui::Selectable(playerLabel(*candidate, config).c_str(),
                            candidate == &current) &&
          candidate != &current)
        selectPlayer(lineup, current, *candidate);
      if (starter && candidate != &current) ImGui::PopStyleColor();
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
    ImGui::SetNextItemWidth(-1.0f);
    const TacticalRole keeperRole = strategy.getKeeperRole();
    if (ImGui::BeginCombo("##role", LOC(Tactics::roleKey(keeperRole))))
    {
      for (const TacticalRole role : Tactics::rolesFor(RoleFamily::Goalkeeper))
        if (ImGui::Selectable(LOC(Tactics::roleKey(role)), role == keeperRole))
          strategy.setKeeperRole(role);
      ImGui::EndCombo();
    }
    ImGui::TableNextColumn();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.0f", Tactics::roleFit(*keeper, RoleFamily::Goalkeeper,
                                          keeperRole));
    ImGui::PopID();
  }

  // Outfield slots, back to front.
  std::vector<Lineup::PositionedPlayer> outfield = lineup.getOutfieldPlayers();
  std::ranges::stable_sort(outfield,
                           [](const auto& left, const auto& right)
                           {
                             if (std::abs(left.position.x - right.position.x) >
                                 SLOT_TOLERANCE)
                               return left.position.x < right.position.x;
                             return left.position.y < right.position.y;
                           });
  for (const Lineup::PositionedPlayer& positioned : outfield)
  {
    if (!positioned.player) continue;
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
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##role", LOC(Tactics::roleKey(instruction.role))))
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
          ImGui::SetTooltip("%s", LOC(Tactics::roleDescriptionKey(role)));
      }
      ImGui::EndCombo();
    }

    ImGui::TableNextColumn();
    if (Tactics::hasDuty(family))
    {
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::BeginCombo("##duty", LOC(Tactics::dutyKey(instruction.duty))))
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

    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.0f", Tactics::roleFit(player, family, instruction.role));
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("How well he suits the role (0-100).");
    ImGui::PopID();
  }
  ImGui::EndTable();
}

void MatchSandboxScene::renderFooter()
{
  const float scale = Theme::scale();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale));
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Match seed");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(130.0f * scale);
  ImGui::InputScalar("##seed", ImGuiDataType_U32, &match_seed);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(
        "The same seed, clubs and tactics replay the same match.");
  ImGui::SameLine();
  ImGui::Checkbox("New seed each match", &new_seed_each_match);
  if (recorder)
  {
    // The last match's review and debugger, as windows (no match pitch
    // here).
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Last match:");
    ImGui::SameLine();
    if (UI::toggleButton("Review", show_review)) show_review = !show_review;
    ImGui::SameLine();
    if (UI::toggleButton("Debugger", show_debugger)) show_debugger = !show_debugger;
  }

  const bool sameClub = sides[HOME].team == sides[AWAY].team;
  const bool ready = !sameClub && sides[HOME].team != 0 && sides[AWAY].team != 0;
  const char* kickOffLabel = "Kick off";
  ImGui::SameLine(ImGui::GetContentRegionMax().x -
                  UI::buttonWidth(kickOffLabel));
  ImGui::BeginDisabled(!ready);
  if (UI::primaryButton(kickOffLabel)) kickOff();
  ImGui::EndDisabled();
  if (sameClub && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Choose two different clubs.");

  // Second row: the last match and recordings.
  if (!last_result.empty())
  {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(Theme::palette().muted, "Last match: %s",
                       last_result.c_str());
    ImGui::SameLine();
  }
  ImGui::BeginDisabled(!recorder || !match_setup || loaded.has_value());
  if (ImGui::Button("Save recording")) saveRecording();
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Save the last match to review it later, or to see how "
                      "a changed engine plays it differently.");
  ImGui::SameLine();
  ImGui::Checkbox("with full detail", &save_with_detail);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Also save every decision's breakdown (a larger file): "
                      "enables term-by-term comparison with a later engine.");
  ImGui::SameLine();
  if (UI::toggleButton("Recordings...", show_recordings))
  {
    show_recordings = !show_recordings;
    recording_files.clear();
  }
  if (!status.empty())
  {
    ImGui::SameLine();
    ImGui::TextColored(Theme::palette().muted, "%s", status.c_str());
  }
}

void MatchSandboxScene::saveRecording()
{
  if (!recorder || !match_setup) return;
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
  if (brains) match_setup->brains = *brains;
  if (bodies) match_setup->bodies = *bodies;
  if (handshake) match_setup->handshake = *handshake;
  if (possession) match_setup->possession = *possession;
  match_home = recording->setup.sides[0].team;
  match_away = recording->setup.sides[1].team;
  debugger.reset();
  review.reset();
  inspector.reset();
  comparison.reset();
  loaded = std::move(recording);
  replay_info = std::move(replayed);
  show_review = true;
  show_debugger = true;
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
  if (!show_recordings) return;
  const float scale = Theme::scale();
  ImGui::SetNextWindowSize(ImVec2(620.0f * scale, 360.0f * scale),
                           ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Recordings", &show_recordings))
  {
    ImGui::End();
    return;
  }
  const std::filesystem::path folder = Recordings::folder();
  if (recording_files.empty())
  {
    std::error_code ignored;
    for (const auto& entry :
         std::filesystem::directory_iterator(folder, ignored))
      if (entry.path().extension() == ".json")
        recording_files.push_back(entry.path());
    // Newest first (the names start with the date).
    std::ranges::sort(recording_files, std::greater{});
  }
  ImGui::TextDisabled("%s", folder.string().c_str());
  ImGui::SameLine();
  if (ImGui::SmallButton("Refresh")) recording_files.clear();
  ImGui::TextDisabled(
      "Opening replays the recording on this engine: review it like a live "
      "match, and see where it plays differently.");
  ImGui::TextWrapped(
      "Replay with uses the brains, bodies, handshake and possession selected "
      "in Set-up.");
  ImGui::Separator();
  if (recording_files.empty()) ImGui::TextDisabled("No recordings yet.");
  for (const std::filesystem::path& path : recording_files)
  {
    ImGui::PushID(path.string().c_str());
    if (ImGui::SmallButton("Open")) loadRecording(path);
    ImGui::SameLine();
    if (ImGui::SmallButton("Replay with"))
      loadRecording(path, sandboxBrains(), sandboxBodies(), sandboxHandshake(),
                    sandboxPossession());
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(
          "Play this match again with the brains, bodies and "
          "handshake and possession selected in Set-up and compare it with the "
          "recording.");
    ImGui::SameLine();
    ImGui::TextUnformatted(path.stem().string().c_str());
    ImGui::PopID();
  }
  ImGui::End();
}

void MatchSandboxScene::kickOff()
{
  if (new_seed_each_match) match_seed = std::random_device{}();
  GameController& controller = guiView->getController();
  // The home side is the managed one: its touchline and Play mode.
  controller.selectManagedTeam(sides[HOME].team);

  // The match is built from its setup, so its recording can build it again.
  match_setup = MatchSetup::capture(controller, sides[HOME].team,
                                    sides[AWAY].team, match_seed,
                                    full_familiarity);
  MatchScene::Sandbox sandbox;
  sandbox.make_engine = [this]()
  {
    std::string error;
    auto engine = match_setup->build(guiView->getController(), error);
    if (!engine) status = "Could not build the match: " + error;
    return engine;
  };
  // A fresh log for this match; the previous one is discarded.
  loaded.reset();
  recorder = std::make_unique<SandboxRecorder>();
  debugger.reset();
  review.reset();
  inspector.reset();
  match_debug = debug_at_kick_off;
  show_review = false;
  show_debugger = false;
  match_home = sides[HOME].team;
  match_away = sides[AWAY].team;
  sandbox.recorder = recorder.get();
  // In the match: Debug and True view buttons in its controls, their
  // sections in its side panel, and the moment and shapes on its pitch.
  sandbox.on_controls = [this]
  {
    UI::sameLineIfFits(UI::buttonWidth("Debug") + UI::buttonWidth("True view") +
                       ImGui::GetStyle().ItemSpacing.x);
    if (UI::toggleButton("Debug", match_debug)) match_debug = !match_debug;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The review and the debugger in the side panel; the "
                        "pitch shows the moment under review, with the "
                        "inspector's overlays.");
    ImGui::SameLine();
    if (UI::toggleButton("True view", match_true_view)) match_true_view = !match_true_view;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The pitch in 3D with every player as the engine "
                        "models him; its settings in the side panel.");
  };
  sandbox.on_side_panel = [this](MatchEngine& engine, bool& paused)
  { renderMatchPanel(engine, paused); };
  sandbox.shown_engine = [this]() -> const MatchEngine*
  { return match_debug ? review.shownEngine() : nullptr; };
  sandbox.true_view = [this] { return match_true_view; };
  sandbox.on_view_overlay = [this](const IMatchRenderer& renderer, const MatchEngine& engine,
                                   const MatchRenderSnapshot& snapshot,
                                   const MatchScene::Sandbox::ViewClick& click)
  {
    // The true view takes the click (A, or B with Ctrl) and hands A to the
    // inspector; without it the inspector picks.
    if (match_true_view)
    {
      const PlayerID picked = true_view.drawOverlay(renderer, engine, snapshot, click.clicked,
                                                    click.ctrl, ImVec2(click.x, click.y));
      if (picked != 0 && !click.ctrl) inspector.selected = picked;
    }
    if (match_debug && recorder)
      inspector.drawOverlays(*recorder, engine, renderer, click.clicked && !match_true_view);
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
      score += std::format(" ({} - {} pens)", engine.getShootoutScore(true),
                           engine.getShootoutScore(false));
    last_result = std::format(
        "{}  (seed {}; brains {}; bodies {}; handshake {}; home advantage "
        "{:.2f})",
        score, seed, brainPairName(brains), bodyPairName(bodies),
        handshakeName(handshake), homeAdvantage);
  };
  guiView->overlayScene(std::make_unique<MatchScene>(
      guiView, sides[HOME].team, sides[AWAY].team, std::move(sandbox)));
}

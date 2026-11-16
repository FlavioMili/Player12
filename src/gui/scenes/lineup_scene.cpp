// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "lineup_scene.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <format>
#include <numbers>
#include <unordered_set>

#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/player_ui.h"
#include "gui/view_models/formation.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/game.h"
#include "model/role_utils.h"
#include "model/team.h"

namespace
{
constexpr float PLAYER_RADIUS = 17.0f;
constexpr float PITCH_ASPECT_RATIO = 1.55f;
constexpr float MINIMUM_PITCH_WIDTH = 320.0f;
constexpr float SIDE_PANEL_WIDTH = 340.0f;
constexpr float NAME_LABEL_WIDTH = 104.0f;
constexpr float GOALKEEPER_X = 0.05f;
constexpr int PITCH_STRIPES = 10;

void renderPlayerTooltip(const Player& player, const StatsConfig& statsConfig)
{
  if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) return;
  ImGui::BeginTooltip();
  ImGui::TextUnformatted(player.getName().c_str());
  ImGui::TextColored(Theme::palette().muted, "%s  ·  %s %d  ·  %s %.0f",
                     RoleUtils::shortName(player.getRole()), LOC("PLAYER_AGE"),
                     player.getAge(), LOC("MAIN_GAME_OVR"),
                     player.getOverall(statsConfig));
  ImGui::TextColored(Theme::palette().faint, "%s", LOC("LINEUP_TOKEN_HINT"));
  ImGui::EndTooltip();
}

void drawPitch(ImDrawList* drawList, ImVec2 pitchMin, ImVec2 pitchMax)
{
  const float width = pitchMax.x - pitchMin.x;
  const float height = pitchMax.y - pitchMin.y;
  const float stripe = width / static_cast<float>(PITCH_STRIPES);
  for (int index = 0; index < PITCH_STRIPES; ++index)
  {
    const ImU32 color = index % 2 == 0 ? IM_COL32(30, 104, 58, 255)
                                       : IM_COL32(34, 114, 63, 255);
    drawList->AddRectFilled(
        ImVec2(pitchMin.x + stripe * static_cast<float>(index), pitchMin.y),
        ImVec2(pitchMin.x + stripe * static_cast<float>(index + 1), pitchMax.y),
        color);
  }
  const ImU32 line = IM_COL32(255, 255, 255, 170);
  const float thickness = 1.6f * Theme::scale();
  const float centerX = pitchMin.x + width * 0.5f;
  const float centerY = pitchMin.y + height * 0.5f;
  drawList->AddRect(pitchMin, pitchMax, line, 0.0f, 0, thickness);
  drawList->AddLine(ImVec2(centerX, pitchMin.y), ImVec2(centerX, pitchMax.y),
                    line, thickness);
  drawList->AddCircle(ImVec2(centerX, centerY), height * 0.15f, line, 48,
                      thickness);
  drawList->AddCircleFilled(ImVec2(centerX, centerY), 3.0f * Theme::scale(),
                            line);

  const float boxWidth = width * 0.157f;
  const float boxHeight = height * 0.59f;
  const float goalBoxWidth = width * 0.052f;
  const float goalBoxHeight = height * 0.27f;
  for (const bool left : {true, false})
  {
    const float edge = left ? pitchMin.x : pitchMax.x;
    const float direction = left ? 1.0f : -1.0f;
    drawList->AddRect(ImVec2(std::min(edge, edge + direction * boxWidth),
                             centerY - boxHeight * 0.5f),
                      ImVec2(std::max(edge, edge + direction * boxWidth),
                             centerY + boxHeight * 0.5f),
                      line, 0.0f, 0, thickness);
    drawList->AddRect(ImVec2(std::min(edge, edge + direction * goalBoxWidth),
                             centerY - goalBoxHeight * 0.5f),
                      ImVec2(std::max(edge, edge + direction * goalBoxWidth),
                             centerY + goalBoxHeight * 0.5f),
                      line, 0.0f, 0, thickness);
    drawList->AddCircleFilled(
        ImVec2(edge + direction * width * 0.105f, centerY),
        2.5f * Theme::scale(), line);
  }
}
}  // namespace

LineupScene::LineupScene(GUIView* parent) : ManagementScene(parent) {}

LineupScene::LineupScene(GUIView* parent, PlayerID focusPlayer)
    : ManagementScene(parent), focus_player_id(focusPlayer)
{
}

void LineupScene::update(float deltaTime) { (void)deltaTime; }

void LineupScene::loadLineup()
{
  auto managedTeamOpt = guiView->getController().getManagedTeam();
  if (!managedTeamOpt) return;
  Team& team = managedTeamOpt.value().get();
  current_lineup = &team.getLineup();
  captain_summary = SetPiecesDialog::captainSummary(guiView->getController());
  formation_index = Formation::detectPreset(*current_lineup);
  const GameController& controller = guiView->getController();
  unavailable.clear();
  // Bans count for the competition of the next match (league by default).
  const auto nextFixture = controller.getNextManagedFixture();
  const MatchType nextType =
      nextFixture ? nextFixture->type : MatchType::LEAGUE;
  for (const auto& record : controller.getSuspendedPlayers(team.getId()))
    if (record.scope == nextType && record.ban_matches > 0)
      unavailable[record.player_id] = Unavailability::SUSPENDED;
  for (const PlayerID injured : controller.getInjuredPlayers(team.getId()))
    unavailable[injured] = Unavailability::INJURED;
  if (focus_player_id == PlayerID{}) return;
  if (const Player* goalkeeper = current_lineup->getGoalkeeper();
      goalkeeper && goalkeeper->getId() == focus_player_id)
    selected_pitch_player_id = focus_player_id;
  for (const auto& positioned : current_lineup->getOutfieldPlayers())
    if (positioned.player && positioned.player->getId() == focus_player_id)
      selected_pitch_player_id = focus_player_id;
  for (const Player* reserve : current_lineup->getReserves())
    if (reserve && reserve->getId() == focus_player_id)
      selected_bench_player_id = focus_player_id;
  focus_player_id = PlayerID{};
}

void LineupScene::renderContent()
{
  UI::pageHeader(LOC("LINEUP_TITLE"), LOC("LINEUP_HELP"));
  if (!current_lineup)
  {
    UI::emptyState(LOC("LINEUP_EMPTY"), nullptr);
    return;
  }
  renderToolbar();

  const float available = ImGui::GetContentRegionAvail().x;
  const float height = ImGui::GetContentRegionAvail().y;
  const float sideWidth = SIDE_PANEL_WIDTH * Theme::scale();
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const float pitchArea = std::max(MINIMUM_PITCH_WIDTH * Theme::scale(),
                                   available - sideWidth - gap);
  const float captionHeight = ImGui::GetTextLineHeightWithSpacing();
  float pitchWidth = pitchArea;
  float pitchHeight = pitchWidth / PITCH_ASPECT_RATIO;
  if (pitchHeight > height - captionHeight)
  {
    pitchHeight = std::max(200.0f * Theme::scale(), height - captionHeight);
    pitchWidth = pitchHeight * PITCH_ASPECT_RATIO;
  }
  ImGui::BeginChild("lineup_pitch_area", ImVec2(pitchArea, height),
                    ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
  renderPitch(pitchWidth, pitchHeight);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("lineup_side", ImVec2(0.0f, height));
  renderBench(height);
  ImGui::EndChild();
  if (set_pieces.render(guiView->getController()))
    captain_summary = SetPiecesDialog::captainSummary(guiView->getController());
}

void LineupScene::renderToolbar()
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(palette.muted, "%s", LOC("LINEUP_FORMATION"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(130.0f * Theme::scale());
  if (const char* preview =
          formation_index >= 0
              ? Formation::PRESETS[static_cast<size_t>(formation_index)].name
              : LOC("LINEUP_FORMATION_CUSTOM");
      ImGui::BeginCombo("##formation", preview))
  {
    for (size_t index = 0; index < Formation::PRESETS.size(); ++index)
    {
      if (ImGui::Selectable(Formation::PRESETS[index].name,
                            formation_index == static_cast<int>(index)))
      {
        Formation::applyPreset(*current_lineup, Formation::PRESETS[index],
                               guiView->getController().getStatsConfig());
        formation_index = static_cast<int>(index);
        showToast(fmt::sprintf(LOC("LINEUP_FORMATION_APPLIED"),
                               Formation::PRESETS[index].name));
      }
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    ImGui::SetTooltip("%s", LOC("LINEUP_FORMATION_HELP"));
  ImGui::SameLine();
  if (UI::primaryButton(LOC("LINEUP_AUTO_PICK"))) autoPickBestEleven();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    ImGui::SetTooltip("%s", LOC("LINEUP_AUTO_PICK_HELP"));
  renderLeadership();

  const auto& statsConfig = guiView->getController().getStatsConfig();
  double total = 0.0;
  int starters = 0;
  if (const Player* goalkeeper = current_lineup->getGoalkeeper())
  {
    total += goalkeeper->getOverall(statsConfig);
    ++starters;
  }
  for (const auto& positioned : current_lineup->getOutfieldPlayers())
  {
    if (!positioned.player) continue;
    total += positioned.player->getOverall(statsConfig);
    ++starters;
  }
  const std::string summary =
      fmt::sprintf(LOC("LINEUP_SUMMARY"), starters,
                   starters > 0 ? total / static_cast<double>(starters) : 0.0);
  UI::sameLineIfFits(ImGui::CalcTextSize(summary.c_str()).x);
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(starters == 11 ? palette.muted : palette.warning, "%s",
                     summary.c_str());
  if (const size_t blocked = unavailableStarters(); blocked > 0)
  {
    const std::string warning =
        fmt::sprintf(LOC("LINEUP_UNAVAILABLE_WARNING"), blocked);
    UI::sameLineIfFits(ImGui::CalcTextSize(warning.c_str()).x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(palette.negative, "%s", warning.c_str());
  }
}

size_t LineupScene::unavailableStarters() const
{
  size_t blocked = 0;
  if (const Player* goalkeeper = current_lineup->getGoalkeeper();
      goalkeeper && unavailable.contains(goalkeeper->getId()))
    ++blocked;
  for (const auto& positioned : current_lineup->getOutfieldPlayers())
    if (positioned.player && unavailable.contains(positioned.player->getId()))
      ++blocked;
  return blocked;
}

void LineupScene::autoPickBestEleven()
{
  GameController& controller = guiView->getController();
  const auto managed = controller.getManagedTeam();
  if (!managed) return;
  std::vector<const Player*> squad;
  for (const auto& player :
       controller.getPlayersForTeam(managed->get().getId()))
    squad.push_back(&player.get());
  std::unordered_set<PlayerID> sidelined;
  for (const auto& [id, reason] : unavailable) sidelined.insert(id);
  const size_t preset =
      formation_index >= 0 ? static_cast<size_t>(formation_index) : 0U;
  Formation::autoPickAvailable(*current_lineup, Formation::PRESETS[preset],
                               squad, sidelined, controller.getStatsConfig());
  formation_index = static_cast<int>(preset);
  selected_pitch_player_id = PlayerID{};
  selected_bench_player_id = PlayerID{};
  showToast(LOC("LINEUP_AUTO_PICKED"));
  captain_summary = SetPiecesDialog::captainSummary(controller);
}

void LineupScene::renderLeadership()
{
  const char* label = LOC("LINEUP_SET_PIECES");
  UI::sameLineIfFits(UI::buttonWidth(label));
  if (UI::secondaryButton(label)) set_pieces.open();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    ImGui::SetTooltip("%s", captain_summary.empty()
                                ? LOC("LINEUP_SET_PIECES_HELP")
                                : captain_summary.c_str());
}

void LineupScene::renderPlayerToken(const Player& player, ImVec2 center,
                                    bool goalkeeper, ImVec2 pitchMin,
                                    ImVec2 pitchSize)
{
  const Theme::Palette& palette = Theme::palette();
  const auto& statsConfig = guiView->getController().getStatsConfig();
  const float radius = PLAYER_RADIUS * Theme::scale();
  ImDrawList* drawList = ImGui::GetWindowDrawList();

  ImGui::SetCursorScreenPos(ImVec2(center.x - radius, center.y - radius));
  const std::string buttonId = std::format("##player_{}", player.getId());
  ImGui::InvisibleButton(buttonId.c_str(),
                         ImVec2(radius * 2.0f, radius * 2.0f));
  const bool hovered = ImGui::IsItemHovered();
  if (ImGui::IsItemClicked())
  {
    selected_pitch_player_id = selected_pitch_player_id == player.getId()
                                   ? PlayerID{}
                                   : player.getId();
  }
  if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    Navigation::openPlayer(guiView, player.getId());

  if (!goalkeeper && ImGui::IsItemActive() &&
      ImGui::IsMouseDragging(ImGuiMouseButton_Left))
  {
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    const float newX =
        std::clamp((center.x + delta.x - pitchMin.x) / pitchSize.x, 0.0f, 1.0f);
    const float newY =
        std::clamp((center.y + delta.y - pitchMin.y) / pitchSize.y, 0.0f, 1.0f);
    current_lineup->moveOutfieldPlayer(player.getId(), {newX, newY});
    center = ImVec2(pitchMin.x + newX * pitchSize.x,
                    pitchMin.y + newY * pitchSize.y);
    formation_index = -1;
  }
  renderPlayerTooltip(player, statsConfig);

  if (ImGui::BeginDragDropTarget())
  {
    if (const ImGuiPayload* payload =
            ImGui::AcceptDragDropPayload("BENCH_PLAYER"))
    {
      IM_ASSERT(payload->DataSize == sizeof(PlayerID));
      const PlayerID benchId = *static_cast<const PlayerID*>(payload->Data);
      current_lineup->swapPlayers(benchId, player.getId());
      selected_pitch_player_id = PlayerID{};
      selected_bench_player_id = PlayerID{};
    }
    ImGui::EndDragDropTarget();
  }

  // Token: shirt disc with the overall inside, role above, surname below.
  // Light discs stay readable on the grass whatever the club accent is; the
  // accent (or amber for the keeper) rings the disc.
  const bool selected = selected_pitch_player_id == player.getId();
  const ImVec4 ring = goalkeeper ? palette.warning : palette.accent;
  drawList->AddCircleFilled(ImVec2(center.x, center.y + 2.0f * Theme::scale()),
                            radius, IM_COL32(0, 0, 0, 80));
  drawList->AddCircleFilled(
      center, radius,
      hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(238, 242, 246, 255));
  drawList->AddCircle(center, radius - 1.2f * Theme::scale(),
                      Theme::toU32(ring), 0, 3.0f * Theme::scale());
  if (selected)
    drawList->AddCircle(center, radius + 4.0f * Theme::scale(),
                        IM_COL32(255, 230, 90, 255), 0, 2.2f * Theme::scale());
  if (const auto blocked = unavailable.find(player.getId());
      blocked != unavailable.end())
  {
    // Unavailable starters get a red ring and a corner tag.
    drawList->AddCircle(center, radius + 1.5f * Theme::scale(),
                        Theme::toU32(palette.negative), 0,
                        2.5f * Theme::scale());
    Theme::ScopedText caption(Theme::Text::CAPTION);
    const char* tag = LOC(blocked->second == Unavailability::INJURED
                              ? "ROSTER_BADGE_INJURED"
                              : "ROSTER_BADGE_SUSPENDED");
    const ImVec2 tagSize = ImGui::CalcTextSize(tag);
    const ImVec2 tagMin(center.x + radius * 0.4f, center.y - radius - 2.0f);
    drawList->AddRectFilled(
        tagMin,
        ImVec2(tagMin.x + tagSize.x + 6.0f * Theme::scale(),
               tagMin.y + tagSize.y + 2.0f * Theme::scale()),
        Theme::toU32(palette.negative), 3.0f * Theme::scale());
    drawList->AddText(ImVec2(tagMin.x + 3.0f * Theme::scale(),
                             tagMin.y + 1.0f * Theme::scale()),
                      IM_COL32(255, 255, 255, 255), tag);
  }

  const std::string overall =
      std::format("{:.0f}", player.getOverall(statsConfig));
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    const ImVec2 size = ImGui::CalcTextSize(overall.c_str());
    drawList->AddText(
        ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f),
        IM_COL32(16, 20, 26, 255), overall.c_str());
  }
  Theme::ScopedText caption(Theme::Text::CAPTION);
  const std::string role = RoleUtils::shortName(player.getRole());
  const ImVec2 roleSize = ImGui::CalcTextSize(role.c_str());
  drawList->AddText(ImVec2(center.x - roleSize.x * 0.5f,
                           center.y - radius - roleSize.y - 1.0f),
                    IM_COL32(255, 255, 255, 220), role.c_str());
  const std::string& surname = player.getLastName();
  const float labelWidth = NAME_LABEL_WIDTH * Theme::scale();
  const ImVec2 nameSize = ImGui::CalcTextSize(surname.c_str());
  const float shownWidth = std::min(nameSize.x, labelWidth);
  const ImVec2 labelMin(center.x - shownWidth * 0.5f - 4.0f * Theme::scale(),
                        center.y + radius + 3.0f * Theme::scale());
  const ImVec2 labelMax(center.x + shownWidth * 0.5f + 4.0f * Theme::scale(),
                        labelMin.y + nameSize.y + 2.0f * Theme::scale());
  drawList->AddRectFilled(labelMin, labelMax, IM_COL32(8, 12, 16, 170),
                          3.0f * Theme::scale());
  drawList->PushClipRect(labelMin, labelMax, true);
  drawList->AddText(
      ImVec2(center.x - shownWidth * 0.5f, labelMin.y + 1.0f * Theme::scale()),
      IM_COL32(255, 255, 255, 240), surname.c_str());
  drawList->PopClipRect();
}

void LineupScene::renderPitch(float pitchWidth, float pitchHeight)
{
  UI::sectionLabel(LOC("LINEUP_STARTING_XI"));
  const ImVec2 pitchMin = ImGui::GetCursorScreenPos();
  const ImVec2 pitchMax(pitchMin.x + pitchWidth, pitchMin.y + pitchHeight);
  drawPitch(ImGui::GetWindowDrawList(), pitchMin, pitchMax);
  const ImVec2 pitchSize(pitchWidth, pitchHeight);

  if (const Player* goalkeeper = current_lineup->getGoalkeeper())
    renderPlayerToken(*goalkeeper,
                      ImVec2(pitchMin.x + GOALKEEPER_X * pitchWidth,
                             pitchMin.y + pitchHeight * 0.5f),
                      true, pitchMin, pitchSize);
  for (const auto& positioned : current_lineup->getOutfieldPlayers())
  {
    if (!positioned.player) continue;
    renderPlayerToken(*positioned.player,
                      ImVec2(pitchMin.x + positioned.position.x * pitchWidth,
                             pitchMin.y + positioned.position.y * pitchHeight),
                      false, pitchMin, pitchSize);
  }
  ImGui::SetCursorScreenPos(pitchMin);
  ImGui::Dummy(pitchSize);
}

void LineupScene::renderBench(float height)
{
  const Theme::Palette& palette = Theme::palette();
  const auto& statsConfig = guiView->getController().getStatsConfig();
  UI::sectionLabel(std::format("{}  ({}/{})", LOC("LINEUP_BENCH"),
                               current_lineup->getReserves().size(),
                               Lineup::MAX_SUBSTITUTES)
                       .c_str());
  const float benchHeight = std::max(160.0f * Theme::scale(), height * 0.42f);
  UI::beginCard("bench_card", nullptr, ImVec2(0.0f, benchHeight), true);
  if (ImGui::BeginTable("bench", 3,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
  {
    ImGui::TableSetupColumn("role");
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("ovr");
    for (const Player* player : current_lineup->getReserves())
    {
      if (!player) continue;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.muted, "%s",
                         RoleUtils::shortName(player->getRole()));
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(player->getId()));
      if (const bool selected = selected_bench_player_id == player->getId();
          ImGui::Selectable(player->getName().c_str(), selected,
                            ImGuiSelectableFlags_SpanAllColumns |
                                ImGuiSelectableFlags_AllowDoubleClick))
      {
        selected_bench_player_id = selected ? PlayerID{} : player->getId();
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
          Navigation::openPlayer(guiView, player->getId());
      }
      renderPlayerTooltip(*player, statsConfig);
      if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
      {
        const PlayerID playerId = player->getId();
        ImGui::SetDragDropPayload("BENCH_PLAYER", &playerId, sizeof(PlayerID));
        ImGui::TextUnformatted(
            fmt::sprintf(LOC("LINEUP_DRAG_SWAP"), player->getName()).c_str());
        ImGui::EndDragDropSource();
      }
      ImGui::PopID();
      ImGui::TableNextColumn();
      if (const auto blocked = unavailable.find(player->getId());
          blocked != unavailable.end())
        UI::badge(LOC(blocked->second == Unavailability::INJURED
                          ? "ROSTER_BADGE_INJURED"
                          : "ROSTER_BADGE_SUSPENDED"),
                  palette.negative);
      else
        UI::ratingChip(player->getOverall(statsConfig));
    }
    ImGui::EndTable();
  }
  UI::endCard();

  renderOutsiders(height);

  const Player* pitchPlayer = selectedPitchPlayer();
  const Player* benchPlayer = selectedBenchPlayer();
  const Player* outsider = selectedOutsider();
  // A player from outside the matchday squad replaces a starter or a
  // substitute, who drops out of the squad.
  const bool canSwap =
      outsider ? (pitchPlayer || benchPlayer) : (pitchPlayer && benchPlayer);
  ImGui::BeginDisabled(!canSwap);
  if (UI::primaryButton(LOC("LINEUP_SWAP_SELECTED"), ImVec2(-FLT_MIN, 0.0f)))
  {
    bool swapped = false;
    if (outsider)
    {
      const PlayerID replacedPlayer =
          pitchPlayer ? selected_pitch_player_id : selected_bench_player_id;
      swapped = current_lineup->bringIn(outsider, replacedPlayer);
    }
    else
    {
      swapped = current_lineup->swapPlayers(selected_bench_player_id,
                                            selected_pitch_player_id);
    }
    if (swapped)
    {
      selected_pitch_player_id = PlayerID{};
      selected_bench_player_id = PlayerID{};
      selected_outsider_id = PlayerID{};
    }
  }
  ImGui::EndDisabled();
  if (const bool benchRoom =
          current_lineup->getReserves().size() < Lineup::MAX_SUBSTITUTES;
      outsider && benchRoom)
  {
    if (UI::secondaryButton(LOC("LINEUP_ADD_TO_BENCH"), ImVec2(-FLT_MIN, 0.0f)))
    {
      std::vector<const Player*> bench = current_lineup->getReserves();
      bench.push_back(outsider);
      current_lineup->setReserves(bench);
      selected_outsider_id = PlayerID{};
    }
  }
  else if (benchPlayer && !pitchPlayer && !outsider &&
           UI::secondaryButton(LOC("LINEUP_REMOVE_FROM_BENCH"),
                               ImVec2(-FLT_MIN, 0.0f)))
  {
    std::vector<const Player*> bench = current_lineup->getReserves();
    std::erase(bench, benchPlayer);
    current_lineup->setReserves(bench);
    selected_bench_player_id = PlayerID{};
  }
  UI::sectionLabel(LOC("LINEUP_COMPARISON"));
  PlayerUI::detailPanel("LineupComparison", benchPlayer, statsConfig,
                        pitchPlayer);
}

const Player* LineupScene::selectedPitchPlayer() const
{
  if (!current_lineup || selected_pitch_player_id == PlayerID{}) return nullptr;
  if (const Player* goalkeeper = current_lineup->getGoalkeeper();
      goalkeeper && goalkeeper->getId() == selected_pitch_player_id)
    return goalkeeper;
  const auto found = std::ranges::find_if(current_lineup->getOutfieldPlayers(),
                                          [this](const auto& positioned)
                                          {
                                            return positioned.player &&
                                                   positioned.player->getId() ==
                                                       selected_pitch_player_id;
                                          });
  return found == current_lineup->getOutfieldPlayers().end() ? nullptr
                                                             : found->player;
}

void LineupScene::renderOutsiders(float height)
{
  const Theme::Palette& palette = Theme::palette();
  GameController& controller = guiView->getController();
  const auto managed = controller.getManagedTeam();
  outsiders.clear();
  if (managed)
  {
    for (const auto& player :
         controller.getPlayersForTeam(managed->get().getId()))
    {
      const Player* candidate = &player.get();
      if (!current_lineup->isStarter(candidate->getId()) &&
          !std::ranges::contains(current_lineup->getReserves(), candidate))
        outsiders.push_back(candidate);
    }
  }
  if (outsiders.empty()) return;
  UI::sectionLabel(
      std::format("{}  ({})", LOC("LINEUP_NOT_IN_SQUAD"), outsiders.size())
          .c_str());
  const float listHeight = std::max(120.0f * Theme::scale(), height * 0.28f);
  UI::beginCard("outsiders_card", nullptr, ImVec2(0.0f, listHeight), true);
  if (ImGui::BeginTable("outsiders", 3,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
  {
    ImGui::TableSetupColumn("role");
    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("status");
    for (const Player* player : outsiders)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.muted, "%s",
                         RoleUtils::shortName(player->getRole()));
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(player->getId()));
      if (const bool selected = selected_outsider_id == player->getId();
          ImGui::Selectable(player->getName().c_str(), selected,
                            ImGuiSelectableFlags_SpanAllColumns |
                                ImGuiSelectableFlags_AllowDoubleClick))
      {
        selected_outsider_id = selected ? PlayerID{} : player->getId();
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
          Navigation::openPlayer(guiView, player->getId());
      }
      ImGui::PopID();
      ImGui::TableNextColumn();
      if (const auto blocked = unavailable.find(player->getId());
          blocked != unavailable.end())
        UI::badge(LOC(blocked->second == Unavailability::INJURED
                          ? "ROSTER_BADGE_INJURED"
                          : "ROSTER_BADGE_SUSPENDED"),
                  palette.negative);
      else
        UI::ratingChip(player->getOverall(controller.getStatsConfig()));
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

const Player* LineupScene::selectedOutsider() const
{
  if (selected_outsider_id == PlayerID{}) return nullptr;
  const auto found =
      std::ranges::find_if(outsiders, [this](const Player* player)
                           { return player->getId() == selected_outsider_id; });
  return found == outsiders.end() ? nullptr : *found;
}

const Player* LineupScene::selectedBenchPlayer() const
{
  if (!current_lineup || selected_bench_player_id == PlayerID{}) return nullptr;
  const auto found = std::ranges::find_if(
      current_lineup->getReserves(), [this](const Player* player)
      { return player && player->getId() == selected_bench_player_id; });
  return found == current_lineup->getReserves().end() ? nullptr : *found;
}

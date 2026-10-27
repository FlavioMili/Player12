// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/player_compare_scene.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/view_models/player_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/inbox.h"
#include "model/role_utils.h"

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 900.0f;
/** Bars longer than this are hard to compare at a glance. */
constexpr float BAR_MAX_WIDTH = 520.0f;
constexpr float RADAR_MAX_SIZE = 420.0f;
constexpr std::size_t PICKER_MAX_RESULTS = 40;

/**
 * Series colours (categorical slots 1-3: blue, orange, aqua), stepped for
 * light and dark surfaces and checked for colour-vision separation. Values
 * are always printed next to the marks, so colour never carries a number.
 */
constexpr std::array<ImVec4, 3> SERIES_DARK = {
    ImVec4(0.224f, 0.529f, 0.898f, 1.0f),   // #3987e5
    ImVec4(0.851f, 0.349f, 0.149f, 1.0f),   // #d95926
    ImVec4(0.098f, 0.620f, 0.439f, 1.0f)};  // #199e70
constexpr std::array<ImVec4, 3> SERIES_LIGHT = {
    ImVec4(0.165f, 0.471f, 0.839f, 1.0f),   // #2a78d6
    ImVec4(0.922f, 0.408f, 0.204f, 1.0f),   // #eb6834
    ImVec4(0.106f, 0.686f, 0.478f, 1.0f)};  // #1baf7a

constexpr std::array<const char*, 10> FACT_KEYS = {
    "COMPARE_FACT_OVERALL", "COMPARE_FACT_POTENTIAL", "COMPARE_FACT_VALUE",
    "COMPARE_FACT_WAGE",    "COMPARE_FACT_CONTRACT",  "COMPARE_FACT_APPS",
    "COMPARE_FACT_GOALS",   "COMPARE_FACT_ASSISTS",   "COMPARE_FACT_RATING",
    "COMPARE_FACT_MINUTES"};

std::string statLabel(const std::string& name)
{
  return LOC(("STAT_" + name).c_str());
}
}  // namespace

PlayerCompareScene::PlayerCompareScene(GUIView* parent, PlayerID first,
                                       PlayerID second)
    : ManagementScene(parent), opened_with{first, second}
{
  requested[0] = first;
  requested[1] = second;
}

void PlayerCompareScene::update(float /*deltaTime*/)
{
  // The comparison view has no time-driven state; refresh() rebuilds its
  // player estimates when the scene is entered or data changes.
}

ImVec4 PlayerCompareScene::seriesColor(std::size_t index) const
{
  const bool light = Theme::appearance().preset == Theme::Preset::LIGHT;
  return (light ? SERIES_LIGHT : SERIES_DARK)[index % SERIES_DARK.size()];
}

std::size_t PlayerCompareScene::filledSlots() const
{
  return static_cast<std::size_t>(std::ranges::count_if(
      slots, [](const Slot& slot) { return slot.id != 0; }));
}

void PlayerCompareScene::refresh()
{
  // Rebuild every slot from the current estimates (knowledge may have grown).
  for (std::size_t index = 0; index < MAX_PLAYERS; ++index)
  {
    const PlayerID id =
        slots[index].id != 0 ? slots[index].id : requested[index];
    requested[index] = 0;
    setSlot(index, id);
  }
  buildCandidates();
}

void PlayerCompareScene::setSlot(std::size_t index, PlayerID id)
{
  const GameController& controller = guiView->getController();
  Slot& slot = slots[index];
  slot = Slot{};
  const auto data = controller.getGameData();
  const auto player = data && id != 0 ? data->getPlayer(id) : std::nullopt;
  const auto view = id != 0 ? controller.getScoutedView(id) : std::nullopt;
  if (!player || !view)
  {
    rebuildAxes();
    return;
  }
  const Player& p = player->get();
  slot.id = id;
  slot.own = view->own;
  slot.name = p.getName();
  const auto team = controller.getTeamById(p.getTeamId());
  slot.subtitle = std::format(
      "{}  ·  {}  ·  {}", RoleUtils::shortName(p.getRole()), p.getAge(),
      team && p.getTeamId() != FREE_AGENTS_TEAM_ID
          ? team->get().getName()
          : std::string(LOC("TRANSFER_FREE_AGENT_LABEL")));
  slot.knowledge = view->own
                       ? std::string(LOC("COMPARE_OWN_PLAYER"))
                       : formatLocalized("COMPARE_KNOWLEDGE",
                                         {std::to_string(view->knowledge)});
  slot.attributes = view->attributes;
  for (const ScoutedAttribute& attribute : slot.attributes)
  {
    const bool exact = view->own || attribute.high - attribute.low < 1.0f;
    slot.attribute_texts.push_back(
        exact ? std::format("{:.0f}", attribute.estimate)
              : std::format("{:.0f}-{:.0f}", attribute.low, attribute.high));
  }

  // Facts: estimates for other clubs' players, public figures for all.
  const bool exact = view->own;
  slot.facts.push_back(exact ? std::format("{:.0f}", view->overall)
                             : std::format("{:.0f} ({:.0f}-{:.0f})",
                                           view->overall, view->overall_low,
                                           view->overall_high));
  slot.facts.push_back(
      std::format("{:.0f}-{:.0f}", view->potential_low, view->potential_high));
  slot.facts.push_back(Format::money(view->estimated_value));
  const auto row = controller.getScoutedRow(id);
  slot.facts.push_back(
      row ? formatLocalized("COMPARE_WAGE_WEEK", {Format::money(row->wage)})
          : std::string("-"));
  const int years = p.getContractYears();
  slot.facts.push_back(years <= 1 ? std::string(LOC("PLANNER_CONTRACT_ENDS"))
                                  : formatLocalized("STAFF_CONTRACT_YEARS",
                                                    {std::to_string(years)}));
  PlayerSeasonStats total;
  for (const PlayerSeasonStats& season : controller.getPlayerSeasonStats(id))
  {
    total.appearances += season.appearances;
    total.minutes += season.minutes;
    total.goals += season.goals;
    total.assists += season.assists;
    total.rating_total += season.rating_total;
    total.rated_matches += season.rated_matches;
  }
  slot.facts.push_back(std::to_string(total.appearances));
  slot.facts.push_back(std::to_string(total.goals));
  slot.facts.push_back(std::to_string(total.assists));
  slot.facts.push_back(total.rated_matches > 0
                           ? std::format("{:.2f}", total.averageRating())
                           : std::string("-"));
  slot.facts.push_back(Format::thousands(total.minutes));
  rebuildAxes();
}

void PlayerCompareScene::rebuildAxes()
{
  axes.clear();
  axis_labels.clear();
  for (const Slot& slot : slots)
    for (const ScoutedAttribute& attribute : slot.attributes)
      if (std::ranges::find(axes, attribute.name) == axes.end())
      {
        axes.push_back(attribute.name);
        axis_labels.push_back(statLabel(attribute.name));
      }
}

void PlayerCompareScene::buildCandidates()
{
  candidates.clear();
  filtered_query.assign(1, char{1});  // Forces the filter to run.
  GameController& controller = guiView->getController();
  const auto data = controller.getGameData();
  if (!data) return;
  std::vector<PlayerID> ids;
  if (const auto managed = controller.getManagedTeam())
    for (const auto& player :
         controller.getPlayersForTeam(managed->get().getId()))
      ids.push_back(player.get().getId());
  for (const ShortlistEntry& entry : controller.getShortlist())
    ids.push_back(entry.player_id);
  for (const ScoutReport& report : controller.getScoutReports())
    ids.push_back(report.player_id);
  for (const PlayerID id : ids)
  {
    if (std::ranges::any_of(candidates, [id](const Candidate& candidate)
                            { return candidate.id == id; }))
      continue;
    const auto player = data->getPlayer(id);
    if (!player) continue;
    const auto team = controller.getTeamById(player->get().getTeamId());
    Candidate candidate;
    candidate.id = id;
    candidate.label = player->get().getName();
    candidate.lower = PlayerView::toLower(candidate.label);
    candidate.detail =
        std::format("{}  ·  {}", RoleUtils::shortName(player->get().getRole()),
                    team ? team->get().getName() : std::string());
    candidates.push_back(std::move(candidate));
  }
}

void PlayerCompareScene::renderContent()
{
  UI::pageHeader(LOC("COMPARE_TITLE"), LOC("COMPARE_SUBTITLE"));
  renderSlots();
  if (filledSlots() < 2)
  {
    UI::emptyState(LOC("COMPARE_EMPTY_TITLE"), LOC("COMPARE_EMPTY_BODY"));
    return;
  }
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool twoColumns = available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
  const float left =
      twoColumns ? std::floor((available - gap) * 0.45f) : available;
  renderRadar(left);
  if (twoColumns) ImGui::SameLine();
  renderBars(twoColumns ? available - gap - left : available);
  renderFacts();
}

void PlayerCompareScene::renderSlots()
{
  const Theme::Palette& palette = Theme::palette();
  UI::TileRow row(static_cast<int>(MAX_PLAYERS), 240.0f);
  const float width = row.width();
  std::optional<std::size_t> removed;
  for (std::size_t index = 0; index < MAX_PLAYERS; ++index)
  {
    row.next();
    const Slot& slot = slots[index];
    ImGui::PushID(static_cast<int>(index));
    UI::beginAutoHeightCard("slot", nullptr, width);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float swatch = 10.0f * Theme::scale();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(at.x, at.y + 4.0f * Theme::scale()),
        ImVec2(at.x + swatch, at.y + 4.0f * Theme::scale() + swatch),
        Theme::toU32(slot.id != 0 ? seriesColor(index) : palette.border),
        2.0f * Theme::scale());
    ImGui::SetCursorScreenPos(
        ImVec2(at.x + swatch + 8.0f * Theme::scale(), at.y));
    const float inner = ImGui::GetContentRegionAvail().x;
    if (slot.id != 0)
    {
      ImGui::BeginGroup();
      {
        Theme::ScopedText title(Theme::Text::TITLE);
        UI::textFitted(slot.name, inner, palette.text);
      }
      UI::textFitted(slot.subtitle, inner, palette.muted);
      ImGui::TextColored(slot.own ? palette.muted : palette.info, "%s",
                         slot.knowledge.c_str());
      ImGui::EndGroup();
      if (UI::secondaryButton(LOC("COMPARE_PROFILE"), {},
                              UI::ButtonSize::COMPACT))
        Navigation::openPlayer(guiView, slot.id);
      UI::sameLineIfFits(
          UI::buttonWidth(LOC("COMPARE_REMOVE"), UI::ButtonSize::COMPACT));
      if (UI::secondaryButton(LOC("COMPARE_REMOVE"), {},
                              UI::ButtonSize::COMPACT))
        removed = index;
    }
    else
    {
      ImGui::TextColored(palette.muted, "%s", LOC("COMPARE_SLOT_EMPTY"));
      ImGui::SetCursorPosX(ImGui::GetCursorPosX());
    }
    renderPicker(index, ImGui::GetContentRegionAvail().x);
    UI::endCard();
    ImGui::PopID();
  }
  if (removed.has_value()) setSlot(*removed, 0);
}

void PlayerCompareScene::renderPicker(std::size_t index, float width)
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::SetNextItemWidth(width);
  if (const char* preview = LOC(slots[index].id != 0 ? "COMPARE_CHANGE_PLAYER"
                                                     : "COMPARE_ADD_PLAYER");
      !ImGui::BeginCombo("##pick", preview, ImGuiComboFlags_HeightLarge))
    return;
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputTextWithHint("##filter", LOC("COMPARE_SEARCH_HINT"),
                           filter.data(), filter.size());
  if (const std::string query = PlayerView::toLower(filter.data());
      query != filtered_query)
  {
    filtered_query = query;
    filtered.clear();
    for (std::size_t candidate = 0; candidate < candidates.size(); ++candidate)
    {
      if (!query.empty() && !candidates[candidate].lower.contains(query))
        continue;
      filtered.push_back(candidate);
      if (filtered.size() >= PICKER_MAX_RESULTS) break;
    }
  }
  if (filtered.empty())
    ImGui::TextColored(palette.muted, "%s", LOC("COMPARE_NO_MATCH"));
  for (const std::size_t candidate : filtered)
  {
    const Candidate& entry = candidates[candidate];
    const bool taken = std::ranges::any_of(
        slots, [&](const Slot& slot) { return slot.id == entry.id; });
    ImGui::PushID(static_cast<int>(entry.id));
    ImGui::BeginDisabled(taken);
    if (ImGui::Selectable(entry.label.c_str(), false))
    {
      setSlot(index, entry.id);
      filter.fill('\0');
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextColored(palette.faint, "%s", entry.detail.c_str());
    ImGui::PopID();
  }
  ImGui::EndCombo();
}

void PlayerCompareScene::renderRadar(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  UI::beginAutoHeightCard("compare_radar", LOC("COMPARE_RADAR"), width);
  const float inner = ImGui::GetContentRegionAvail().x;
  const float size = std::min(inner, RADAR_MAX_SIZE * scale);
  // Side labels sit fully outside the rings: leave room for the widest.
  float widest = 0.0f;
  for (const std::string& label : axis_labels)
    widest = std::max(widest, ImGui::CalcTextSize(label.c_str()).x);
  const float labelRoom = widest + 8.0f * scale;
  const float radius =
      std::max(40.0f * scale, std::min(size * 0.5f, inner * 0.5f - labelRoom));
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const ImVec2 centre(
      start.x + inner * 0.5f,
      start.y + radius + ImGui::GetTextLineHeight() + 6.0f * scale);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const std::size_t count = axes.size();
  const auto point = [&](std::size_t axis, float value)
  {
    const float angle =
        -std::numbers::pi_v<float> * 0.5f + 2.0f * std::numbers::pi_v<float> *
                                                static_cast<float>(axis) /
                                                static_cast<float>(count);
    const float distance = radius * std::clamp(value / 100.0f, 0.0f, 1.0f);
    return ImVec2(centre.x + std::cos(angle) * distance,
                  centre.y + std::sin(angle) * distance);
  };
  if (count >= 3)
  {
    // Recessive grid: rings at 25/50/75/100 and the spokes.
    for (const float ring : {25.0f, 50.0f, 75.0f, 100.0f})
    {
      for (std::size_t axis = 0; axis < count; ++axis)
        drawList->PathLineTo(point(axis, ring));
      drawList->PathStroke(Theme::toU32(palette.border), ImDrawFlags_Closed,
                           1.0f);
    }
    for (std::size_t axis = 0; axis < count; ++axis)
    {
      drawList->AddLine(centre, point(axis, 100.0f),
                        Theme::toU32(palette.border), 1.0f);
      const std::string& label = axis_labels[axis];
      const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
      const ImVec2 edge = point(axis, 100.0f);
      const float dx = edge.x - centre.x;
      const float dy = edge.y - centre.y;
      float x = edge.x - textSize.x * 0.5f;
      if (dx > 1.0f)
        x = edge.x + 4.0f * scale;
      else if (dx < -1.0f)
        x = edge.x - textSize.x - 4.0f * scale;
      float y = edge.y - textSize.y * 0.5f;
      if (dy > 1.0f)
        y = edge.y + 2.0f * scale;
      else if (dy < -1.0f)
        y = edge.y - textSize.y - 2.0f * scale;
      drawList->AddText(ImVec2(x, y), Theme::toU32(palette.muted),
                        label.c_str());
    }
    // One polygon per player at the (estimated) values.
    for (std::size_t index = 0; index < MAX_PLAYERS; ++index)
    {
      const Slot& slot = slots[index];
      if (slot.id == 0) continue;
      std::array<ImVec2, 16> points{};
      const std::size_t used = std::min(count, points.size());
      for (std::size_t axis = 0; axis < used; ++axis)
      {
        const auto found = std::ranges::find(slot.attributes, axes[axis],
                                             &ScoutedAttribute::name);
        points[axis] = point(
            axis, found != slot.attributes.end() ? found->estimate : 0.0f);
      }
      const ImVec4 color = seriesColor(index);
      drawList->AddConcavePolyFilled(points.data(), static_cast<int>(used),
                                     Theme::toU32(color, 0.12f));
      drawList->AddPolyline(points.data(), static_cast<int>(used),
                            Theme::toU32(color), ImDrawFlags_Closed,
                            2.0f * scale);
      for (std::size_t axis = 0; axis < used; ++axis)
        drawList->AddCircleFilled(points[axis], 2.5f * scale,
                                  Theme::toU32(color));
    }
  }
  ImGui::Dummy(ImVec2(
      inner, 2.0f * (radius + ImGui::GetTextLineHeight() + 6.0f * scale)));
  // Legend: identity is never colour alone.
  bool first = true;
  for (std::size_t index = 0; index < MAX_PLAYERS; ++index)
  {
    const Slot& slot = slots[index];
    if (slot.id == 0) continue;
    const float itemWidth =
        16.0f * scale + ImGui::CalcTextSize(slot.name.c_str()).x;
    if (!first) UI::sameLineIfFits(itemWidth, Theme::Space::L * scale);
    first = false;
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float line = ImGui::GetTextLineHeight();
    drawList->AddLine(ImVec2(at.x, at.y + line * 0.5f),
                      ImVec2(at.x + 12.0f * scale, at.y + line * 0.5f),
                      Theme::toU32(seriesColor(index)), 3.0f * scale);
    ImGui::SetCursorScreenPos(ImVec2(at.x + 16.0f * scale, at.y));
    ImGui::TextUnformatted(slot.name.c_str());
  }
  UI::endCard();
}

void PlayerCompareScene::renderBars(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  UI::beginAutoHeightCard("compare_bars", LOC("COMPARE_ATTRIBUTES"), width);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const float inner = ImGui::GetContentRegionAvail().x;
  const float valueWidth = ImGui::CalcTextSize("00-00").x + 8.0f * scale;
  const float barHeight = 6.0f * scale;
  const float rowGap = 4.0f * scale;
  const float line = ImGui::GetTextLineHeight();
  for (std::size_t row = 0; row < axes.size(); ++row)
  {
    const std::string& axis = axes[row];
    ImGui::TextColored(palette.muted, "%s", axis_labels[row].c_str());
    for (std::size_t index = 0; index < MAX_PLAYERS; ++index)
    {
      const Slot& slot = slots[index];
      if (slot.id == 0) continue;
      const auto found =
          std::ranges::find(slot.attributes, axis, &ScoutedAttribute::name);
      if (found == slot.attributes.end()) continue;
      const auto position =
          static_cast<std::size_t>(found - slot.attributes.begin());
      const ImVec2 at = ImGui::GetCursorScreenPos();
      const float barWidth =
          std::clamp(inner - valueWidth, 0.0f, BAR_MAX_WIDTH * scale);
      const float y = at.y + (line - barHeight) * 0.5f;
      const ImVec4 color = seriesColor(index);
      drawList->AddRectFilled(ImVec2(at.x, y),
                              ImVec2(at.x + barWidth, y + barHeight),
                              Theme::toU32(palette.raised), barHeight * 0.5f);
      // Scouted range behind the estimate.
      if (!slot.own && found->high > found->low)
        drawList->AddRectFilled(
            ImVec2(
                at.x + barWidth * std::clamp(found->low / 100.0f, 0.0f, 1.0f),
                y),
            ImVec2(
                at.x + barWidth * std::clamp(found->high / 100.0f, 0.0f, 1.0f),
                y + barHeight),
            Theme::toU32(color, 0.35f), barHeight * 0.5f);
      drawList->AddRectFilled(
          ImVec2(at.x, y),
          ImVec2(at.x + barWidth *
                            std::clamp(found->estimate / 100.0f, 0.0f, 1.0f),
                 y + barHeight),
          Theme::toU32(color), barHeight * 0.5f);
      const std::string& text = slot.attribute_texts[position];
      drawList->AddText(ImVec2(at.x + barWidth + 8.0f * scale, at.y),
                        Theme::toU32(palette.text), text.c_str());
      ImGui::Dummy(ImVec2(inner, line));
    }
    ImGui::Dummy(ImVec2(0.0f, rowGap));
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("COMPARE_RANGE_NOTE"));
  ImGui::PopTextWrapPos();
  UI::endCard();
}

void PlayerCompareScene::renderFacts()
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("compare_facts", LOC("COMPARE_FACTS"));
  if (const int columns = 1 + static_cast<int>(filledSlots());
      ImGui::BeginTable("facts", columns,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_SizingStretchSame))
  {
    ImGui::TableSetupColumn("##fact");
    for (std::size_t index = 0; index < MAX_PLAYERS; ++index)
      if (slots[index].id != 0)
        ImGui::TableSetupColumn(slots[index].name.c_str());
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableNextColumn();
    for (std::size_t index = 0; index < MAX_PLAYERS; ++index)
    {
      if (slots[index].id == 0) continue;
      ImGui::TableNextColumn();
      UI::textFitted(slots[index].name, ImGui::GetContentRegionAvail().x,
                     palette.text);
    }
    for (std::size_t fact = 0; fact < FACT_KEYS.size(); ++fact)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.muted, "%s", LOC(FACT_KEYS[fact]));
      for (const Slot& slot : slots)
      {
        if (slot.id == 0) continue;
        ImGui::TableNextColumn();
        if (fact < slot.facts.size())
          UI::textFitted(slot.facts[fact], ImGui::GetContentRegionAvail().x,
                         palette.text);
      }
    }
    ImGui::EndTable();
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("COMPARE_FACTS_NOTE"));
  ImGui::PopTextWrapPos();
  UI::endCard();
}

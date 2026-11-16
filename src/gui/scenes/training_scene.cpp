// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/training_scene.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>

#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/inbox.h"
#include "model/role_utils.h"

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 1000.0f;
/** Below this card width the microcycle turns into one row per day. */
constexpr float MICROCYCLE_WIDE_WIDTH = 760.0f;
constexpr float WEEK_CELL_MIN_WIDTH = 104.0f;
constexpr float WEEK_CELL_HEIGHT = 64.0f;
constexpr float TREND_UP = 0.03f;
constexpr int VETERAN_AGE = 31;

enum Column : int
{
  NAME = 0,
  POSITION,
  AGE,
  CONDITION,
  WORKLOAD,
  RISK,
  TREND,
  FOCUS,
  COLUMN_COUNT
};

float dpi() { return Theme::scale(); }

// Individual table: columns hide from the highest priority number down.
const std::array<UI::Column, COLUMN_COUNT>& playerColumns()
{
  static const std::array<UI::Column, COLUMN_COUNT> columns = {{
      {"TRAINING_COL_PLAYER", 0.0f, 0, ImGuiTableColumnFlags_DefaultSort, NAME},
      {"TRAINING_COL_POSITION", 64.0f, 1, ImGuiTableColumnFlags_None, POSITION},
      {"TRAINING_COL_AGE", 44.0f, 4, ImGuiTableColumnFlags_None, AGE},
      {"TRAINING_COL_CONDITION", 130.0f, 1, ImGuiTableColumnFlags_None,
       CONDITION},
      {"TRAINING_COL_WORKLOAD", 76.0f, 3, ImGuiTableColumnFlags_None, WORKLOAD},
      {"TRAINING_COL_RISK", 84.0f, 2, ImGuiTableColumnFlags_None, RISK},
      {"TRAINING_COL_TREND", 52.0f, 3, ImGuiTableColumnFlags_None, TREND},
      {"TRAINING_COL_FOCUS", 180.0f, 0, ImGuiTableColumnFlags_None, FOCUS},
  }};
  return columns;
}

ImVec4 riskColor(WorkloadRisk risk)
{
  const Theme::Palette& palette = Theme::palette();
  using enum WorkloadRisk;
  switch (risk)
  {
    case High:
      return palette.negative;
    case Moderate:
      return palette.warning;
    case Low:
      break;
  }
  return palette.positive;
}

ImVec4 severityColor(AdviceSeverity severity)
{
  const Theme::Palette& palette = Theme::palette();
  using enum AdviceSeverity;
  switch (severity)
  {
    case Warning:
      return palette.negative;
    case Suggestion:
      return palette.warning;
    case Info:
      break;
  }
  return palette.info;
}

/** Up/down triangle or a flat bar for a development trend. */
void trendArrow(float trend, bool veteran)
{
  const Theme::Palette& palette = Theme::palette();
  const float size = ImGui::GetTextLineHeight();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImVec2 center(start.x + size * 0.5f, start.y + size * 0.5f);
  const float half = size * 0.3f;
  const char* tooltip = "TRAINING_TREND_STEADY";
  if (trend >= TREND_UP)
  {
    drawList->AddTriangleFilled(ImVec2(center.x, center.y - half),
                                ImVec2(center.x + half, center.y + half),
                                ImVec2(center.x - half, center.y + half),
                                Theme::toU32(palette.positive));
    tooltip = "TRAINING_TREND_UP";
  }
  else if (veteran)
  {
    drawList->AddTriangleFilled(ImVec2(center.x - half, center.y - half),
                                ImVec2(center.x + half, center.y - half),
                                ImVec2(center.x, center.y + half),
                                Theme::toU32(palette.negative));
    tooltip = "TRAINING_TREND_DOWN";
  }
  else
  {
    drawList->AddRectFilled(ImVec2(center.x - half, center.y - 1.5f * dpi()),
                            ImVec2(center.x + half, center.y + 1.5f * dpi()),
                            Theme::toU32(palette.muted));
  }
  ImGui::Dummy(ImVec2(size, size));
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", LOC(tooltip));
}

template <typename Enum, typename KeyFunction>
bool enumCombo(const char* id, Enum& value, std::size_t count, KeyFunction key,
               float width)
{
  bool changed = false;
  ImGui::SetNextItemWidth(width);
  if (ImGui::BeginCombo(id, LOC(key(value))))
  {
    for (std::size_t index = 0; index < count; ++index)
    {
      const auto option = static_cast<Enum>(index);
      if (ImGui::Selectable(LOC(key(option)), option == value))
      {
        changed = option != value;
        value = option;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}
}  // namespace

TrainingScene::TrainingScene(GUIView* parent) : ManagementScene(parent) {}

void TrainingScene::update(float /*deltaTime*/)
{
  /* This page changes only in response to controller actions; its model is
   * refreshed after edits, with no frame-driven work to perform. */
}

void TrainingScene::refresh()
{
  GameController& controller = guiView->getController();
  rows.clear();
  week.clear();
  advice.clear();
  has_plan = false;
  const auto managed = controller.getManagedTeam();
  const TeamTrainingPlan* current = controller.getTrainingPlan();
  if (!managed || !current) return;
  plan = *current;
  has_plan = true;
  const TeamID team_id = managed->get().getId();

  float condition_total = 0.0f;
  int fit = 0;
  at_risk = 0;
  for (const auto& player_ref : controller.getPlayersForTeam(team_id))
  {
    const Player& player = player_ref.get();
    const GameController::PlayerWorkload workload =
        controller.getPlayerWorkload(player.getId());
    PlayerRow row;
    row.id = player.getId();
    row.name = player.getName();
    row.role = RoleUtils::shortName(player.getRole());
    row.age = player.getAge();
    row.condition = player.getDynamics().condition;
    row.ratio = workload.ratio;
    row.risk = workload.risk;
    row.trend = workload.trend;
    row.focus = workload.focus;
    row.injured = !player.isAvailable();
    row.veteran = row.age >= VETERAN_AGE && row.trend < TREND_UP;
    row.condition_text = std::format("{:.0f}", row.condition);
    row.ratio_text = std::format("{:.2f}", row.ratio);
    if (!row.injured)
    {
      condition_total += row.condition;
      ++fit;
      if (row.risk == WorkloadRisk::High) ++at_risk;
    }
    rows.push_back(std::move(row));
  }
  average_condition =
      fit > 0 ? condition_total / static_cast<float>(fit) : 0.0f;
  familiarity = controller.getTacticalFamiliarity(team_id);
  effectiveness = controller.getStaffEffects(team_id).training_quality;
  sortRows();

  for (const GameController::TrainingDayPreview& preview :
       controller.getTrainingWeekPreview())
  {
    DayCell cell;
    cell.date = Format::dayMonth(preview.date);
    cell.match_day = preview.day.match_day;
    if (cell.match_day)
    {
      const auto opponent = controller.getTeamById(preview.opponent);
      cell.title = LOC("TRAINING_MATCH_DAY");
      cell.detail = std::format(
          "{} {}", LOC(preview.home ? "TRAINING_VS" : "TRAINING_AT"),
          opponent ? opponent->get().getName() : std::string("–"));
    }
    else
    {
      cell.title = LOC(TrainingModel::sessionKey(preview.day.slot.session));
      cell.detail =
          preview.day.slot.session == SessionType::Rest
              ? std::string()
              : LOC(TrainingModel::intensityKey(preview.day.slot.intensity));
      cell.adjusted = preview.day.adjusted;
      cell.load = TrainingModel::sessionLoad(preview.day.slot, plan.intensity);
    }
    week.push_back(std::move(cell));
  }

  for (const TrainingAdvice& item : controller.getTrainingAdvice())
    advice.emplace_back(item.severity, formatLocalized(item.key, item.args));
}

void TrainingScene::sortRows()
{
  const auto key = [this](const PlayerRow& row)
  {
    switch (sort_column)
    {
      case AGE:
        return static_cast<float>(row.age);
      case CONDITION:
        return row.condition;
      case WORKLOAD:
        return row.ratio;
      case RISK:
        return static_cast<float>(row.risk);
      case TREND:
        return row.trend;
      case FOCUS:
        return static_cast<float>(row.focus);
      default:
        return 0.0f;
    }
  };
  std::ranges::stable_sort(rows,
                           [&](const PlayerRow& a, const PlayerRow& b)
                           {
                             int order = 0;
                             if (sort_column == NAME)
                               order = UI::compare(a.name, b.name);
                             else if (sort_column == POSITION)
                               order = UI::compare(a.role, b.role);
                             else
                               order = UI::compare(key(a), key(b));
                             return sort_ascending ? order < 0 : order > 0;
                           });
}

void TrainingScene::renderContent()
{
  if (!has_plan) return;
  const Theme::Palette& palette = Theme::palette();
  UI::pageHeader(LOC("TRAINING_TITLE"), LOC("TRAINING_SUBTITLE"));

  UI::TileRow tiles(4);
  const float tile = tiles.width();
  const std::string condition = std::format("{:.0f}%", average_condition);
  tiles.next();
  ImVec4 conditionColor = palette.negative;
  if (average_condition >= 85.0f)
    conditionColor = palette.positive;
  else if (average_condition >= 75.0f)
    conditionColor = palette.warning;
  UI::statTile("condition", LOC("TRAINING_TILE_CONDITION"), condition.c_str(),
               LOC("TRAINING_TILE_CONDITION_NOTE"), conditionColor, tile);
  const std::string drilled = std::format("{:.0f}%", familiarity * 100.0f);
  tiles.next();
  ImVec4 familiarityColor = palette.negative;
  if (familiarity >= 0.7f)
    familiarityColor = palette.positive;
  else if (familiarity >= 0.45f)
    familiarityColor = palette.warning;
  UI::statTile("familiarity", LOC("TRAINING_TILE_FAMILIARITY"), drilled.c_str(),
               LOC("TRAINING_TILE_FAMILIARITY_NOTE"), familiarityColor, tile);
  const std::string risk = std::to_string(at_risk);
  tiles.next();
  UI::statTile("risk", LOC("TRAINING_TILE_RISK"), risk.c_str(),
               LOC("TRAINING_TILE_RISK_NOTE"),
               at_risk == 0 ? palette.positive : palette.negative, tile);
  const std::string coaching = std::format("{:.0f}", effectiveness * 100.0f);
  tiles.next();
  UI::statTile("coaching", LOC("TRAINING_TILE_COACHING"), coaching.c_str(),
               LOC("TRAINING_TILE_COACHING_NOTE"),
               Theme::ratingColor(effectiveness * 100.0f), tile);

  // One scroll surface: every card sizes to its content and the page scrolls.
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * dpi()));
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool twoColumns = available >= TWO_COLUMN_MIN_WIDTH * dpi();
  const float left =
      twoColumns ? std::floor((available - gap) * 0.66f) : available;
  renderMicrocycle(left);
  if (twoColumns) ImGui::SameLine();
  renderAdvice(twoColumns ? available - gap - left : available);
  renderWeek();
  renderPlayers();
}

void TrainingScene::renderControls()
{
  GameController& controller = guiView->getController();
  const Theme::Palette& palette = Theme::palette();
  const float spacing = Theme::Space::L * dpi();
  const float presetWidth = 170.0f * dpi();
  const float intensityWidth = 120.0f * dpi();
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(palette.muted, "%s", LOC("TRAINING_PRESET"));
  ImGui::SameLine();
  if (TrainingPreset preset = plan.preset;
      enumCombo(
          "##preset", preset,
          static_cast<std::size_t>(std::to_underlying(TrainingPreset::Custom)),
          TrainingModel::presetKey, presetWidth) &&
      controller.setTrainingPreset(preset))
  {
    refresh();
    showToast(LOC("TRAINING_PRESET_TOAST"));
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", LOC("TRAINING_PRESET_HELP"));

  const char* intensityLabel = LOC("TRAINING_INTENSITY");
  UI::sameLineIfFits(ImGui::CalcTextSize(intensityLabel).x +
                         ImGui::GetStyle().ItemSpacing.x + intensityWidth,
                     spacing);
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(palette.muted, "%s", intensityLabel);
  ImGui::SameLine();
  if (TrainingIntensity intensity = plan.intensity;
      enumCombo("##squad_intensity", intensity,
                static_cast<std::size_t>(
                    std::to_underlying(TrainingIntensity::COUNT)),
                TrainingModel::intensityKey, intensityWidth) &&
      controller.setTrainingIntensity(intensity))
    refresh();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", LOC("TRAINING_INTENSITY_HELP"));

  const char* autoLabel = LOC("TRAINING_AUTO_CONGESTION");
  UI::sameLineIfFits(ImGui::GetFrameHeight() +
                         ImGui::GetStyle().ItemInnerSpacing.x +
                         ImGui::CalcTextSize(autoLabel).x,
                     spacing);
  if (bool automatic = plan.auto_congestion;
      ImGui::Checkbox(autoLabel, &automatic) &&
      controller.setCongestionAutoAdjust(automatic))
    refresh();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", LOC("TRAINING_AUTO_CONGESTION_HELP"));
}

void TrainingScene::renderSlot(int day, bool compact)
{
  GameController& controller = guiView->getController();
  const Theme::Palette& palette = Theme::palette();
  ImGui::PushID(day);
  TrainingSlot slot = plan.slots[static_cast<std::size_t>(day)];
  bool changed = false;
  if (!compact)
  {
    ImGui::TableNextColumn();
    const float cellWidth = ImGui::GetContentRegionAvail().x;
    changed |= enumCombo("##session", slot.session, SESSION_TYPE_COUNT,
                         TrainingModel::sessionKey, cellWidth);
    ImGui::BeginDisabled(slot.session == SessionType::Rest);
    changed |= enumCombo(
        "##intensity", slot.intensity,
        static_cast<std::size_t>(std::to_underlying(TrainingIntensity::COUNT)),
        TrainingModel::intensityKey, cellWidth);
    ImGui::EndDisabled();
  }
  else
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(
        palette.muted, "%s",
        LOC(TrainingModel::dayKey(static_cast<MicrocycleDay>(day))));
    ImGui::TableNextColumn();
    changed |= enumCombo("##session", slot.session, SESSION_TYPE_COUNT,
                         TrainingModel::sessionKey, -FLT_MIN);
    ImGui::TableNextColumn();
    ImGui::BeginDisabled(slot.session == SessionType::Rest);
    changed |= enumCombo(
        "##intensity", slot.intensity,
        static_cast<std::size_t>(std::to_underlying(TrainingIntensity::COUNT)),
        TrainingModel::intensityKey, -FLT_MIN);
    ImGui::EndDisabled();
    ImGui::TableNextColumn();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                         ImGui::GetStyle().FramePadding.y);
  }
  const float load = TrainingModel::sessionLoad(slot, plan.intensity);
  ImVec4 loadColor = palette.positive;
  if (load > 1.3f)
    loadColor = palette.negative;
  else if (load > 0.7f)
    loadColor = palette.warning;
  UI::meter("", std::min(1.0f, load / 1.8f), 0.0f, loadColor, "");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", LOC("TRAINING_LOAD_HELP"));
  if (changed &&
      controller.setTrainingSlot(static_cast<MicrocycleDay>(day), slot))
    refresh();
  ImGui::PopID();
}

void TrainingScene::renderMicrocycle(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("training_schedule", LOC("TRAINING_SCHEDULE"), width);
  renderControls();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * dpi()));
  constexpr auto DAYS = static_cast<int>(MICROCYCLE_DAYS);
  // Seven columns when they fit, otherwise one row per day: never scrolls.
  if (const bool wide =
          ImGui::GetContentRegionAvail().x >= MICROCYCLE_WIDE_WIDTH * dpi();
      wide && ImGui::BeginTable("microcycle", DAYS,
                                ImGuiTableFlags_BordersInnerV |
                                    ImGuiTableFlags_SizingStretchSame))
  {
    for (int day = 0; day < DAYS; ++day)
      ImGui::TableSetupColumn(
          LOC(TrainingModel::dayKey(static_cast<MicrocycleDay>(day))));
    UI::staticHeadersRow();
    ImGui::TableNextRow();
    for (int day = 0; day < DAYS; ++day) renderSlot(day, false);
    ImGui::EndTable();
  }
  else if (!wide && ImGui::BeginTable("microcycle_rows", 4,
                                      ImGuiTableFlags_BordersInnerH |
                                          ImGuiTableFlags_SizingStretchProp))
  {
    ImGui::TableSetupColumn(LOC("TRAINING_COL_DAY"),
                            ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn(LOC("TRAINING_COL_SESSION"),
                            ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn(LOC("TRAINING_COL_INTENSITY"),
                            ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn(LOC("TRAINING_COL_LOAD"),
                            ImGuiTableColumnFlags_WidthStretch, 1.0f);
    UI::staticHeadersRow();
    for (int day = 0; day < DAYS; ++day) renderSlot(day, true);
    ImGui::EndTable();
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("TRAINING_SCHEDULE_HELP"));
  ImGui::PopTextWrapPos();
  UI::endCard();
}

void TrainingScene::renderWeek() const
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("training_week", LOC("TRAINING_WEEK"));
  if (!week.empty())
  {
    // Seven cells on one line when they fit, wrapping onto more lines below.
    UI::TileRow cells(static_cast<int>(week.size()), WEEK_CELL_MIN_WIDTH);
    const float cellWidth = cells.width();
    const float cellHeight = WEEK_CELL_HEIGHT * dpi();
    const float pad = Theme::Space::S * dpi();
    const float line = ImGui::GetTextLineHeight();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (const DayCell& cell : week)
    {
      cells.next();
      const ImVec2 start = ImGui::GetCursorScreenPos();
      const ImVec2 end(start.x + cellWidth, start.y + cellHeight);
      const ImVec4& tone = cell.match_day ? palette.accent : palette.raised;
      drawList->AddRectFilled(start, end,
                              Theme::toU32(tone, cell.match_day ? 0.25f : 1.0f),
                              4.0f * dpi());
      if (!cell.match_day && cell.load > 0.0f)
      {
        const float bar = std::min(1.0f, cell.load / 1.8f);
        drawList->AddRectFilled(
            ImVec2(start.x, end.y - 3.0f * dpi()),
            ImVec2(start.x + cellWidth * bar, end.y),
            Theme::toU32(bar > 0.72f ? palette.negative : palette.warning));
      }
      const float textWidth = cellWidth - 2.0f * pad;
      const float top = start.y + pad * 0.5f;
      UI::drawTextFitted(drawList, ImVec2(start.x + pad, top),
                         Theme::toU32(palette.muted), cell.date, textWidth);
      UI::drawTextFitted(drawList, ImVec2(start.x + pad, top + line),
                         Theme::toU32(palette.text), cell.title, textWidth);
      UI::drawTextFitted(
          drawList, ImVec2(start.x + pad, top + 2.0f * line),
          Theme::toU32(cell.adjusted ? palette.warning : palette.faint),
          cell.detail, textWidth);
      ImGui::Dummy(ImVec2(cellWidth, cellHeight));
      if (cell.adjusted && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", LOC("TRAINING_ADJUSTED_HELP"));
    }
  }
  UI::endCard();
}

void TrainingScene::renderAdvice(float width) const
{
  UI::beginAutoHeightCard("training_advice", LOC("TRAINING_ADVICE"), width);
  for (const AdviceLine& line : advice)
  {
    const ImVec4 color = severityColor(line.severity);
    const float size = 8.0f * dpi();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(start.x + size * 0.5f,
               start.y + ImGui::GetTextLineHeight() * 0.5f),
        size * 0.5f, Theme::toU32(color));
    ImGui::Dummy(ImVec2(size, 0.0f));
    ImGui::SameLine();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(line.text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * dpi()));
  }
  UI::endCard();
}

void TrainingScene::renderPlayers()
{
  GameController& controller = guiView->getController();
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("training_players", LOC("TRAINING_INDIVIDUAL"));
  if (rows.empty())
  {
    UI::emptyState(LOC("TRAINING_NO_PLAYERS_TITLE"),
                   LOC("TRAINING_NO_PLAYERS_BODY"));
    UI::endCard();
    return;
  }
  std::array<UI::Column, COLUMN_COUNT> columns = playerColumns();
  for (UI::Column& column : columns) column.label = LOC(column.label);
  // No inner scrolling: the table grows and the page scrolls (the clipper
  // still skips rows outside the page viewport).
  if (const UI::ColumnMask mask =
          UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
      UI::beginResponsiveTable("training_table", columns, mask,
                               ImGuiTableFlags_RowBg |
                                   ImGuiTableFlags_BordersInnerH |
                                   ImGuiTableFlags_Sortable,
                               UI::TableHeader::SORTABLE))
  {
    if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
        specs && specs->SpecsDirty && specs->SpecsCount > 0)
    {
      sort_column = static_cast<int>(specs->Specs[0].ColumnUserID);
      sort_ascending =
          specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
      sortRows();
      specs->SpecsDirty = false;
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step())
    {
      for (int index = clipper.DisplayStart; index < clipper.DisplayEnd;
           ++index)
      {
        PlayerRow& row = rows[static_cast<std::size_t>(index)];
        ImGui::PushID(static_cast<int>(row.id));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if (UI::link(row.name.c_str(), "##player"))
          Navigation::openPlayer(guiView, row.id);
        if (UI::cell(mask, POSITION))
        {
          ImGui::AlignTextToFramePadding();
          ImGui::TextColored(palette.muted, "%s", row.role.c_str());
        }
        if (UI::cell(mask, AGE))
        {
          ImGui::AlignTextToFramePadding();
          ImGui::Text("%d", row.age);
        }
        if (UI::cell(mask, CONDITION))
        {
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                               ImGui::GetStyle().FramePadding.y);
          if (row.injured)
            UI::badge(LOC("TRAINING_INJURED"), palette.negative);
          else
          {
            ImVec4 conditionColor = palette.negative;
            if (row.condition >= 85.0f)
              conditionColor = palette.positive;
            else if (row.condition >= 70.0f)
              conditionColor = palette.warning;
            UI::meter("", row.condition / 100.0f, 0.0f, conditionColor,
                      row.condition_text.c_str());
          }
        }
        if (UI::cell(mask, WORKLOAD))
        {
          ImGui::AlignTextToFramePadding();
          ImGui::TextColored(row.ratio > 1.3f ? palette.warning : palette.text,
                             "%s", row.ratio_text.c_str());
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", LOC("TRAINING_WORKLOAD_HELP"));
        }
        if (UI::cell(mask, RISK))
        {
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                               ImGui::GetStyle().FramePadding.y);
          UI::badge(LOC(TrainingModel::riskKey(row.risk)), riskColor(row.risk));
        }
        if (UI::cell(mask, TREND))
        {
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                               ImGui::GetStyle().FramePadding.y);
          trendArrow(row.trend, row.veteran);
        }
        ImGui::TableNextColumn();
        if (enumCombo("##focus", row.focus, TRAINING_FOCUS_COUNT,
                      TrainingModel::focusKey, -FLT_MIN) &&
            controller.setPlayerTrainingFocus(row.id, row.focus))
          showToast(LOC("TRAINING_FOCUS_TOAST"));
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

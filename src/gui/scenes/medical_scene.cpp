// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/medical_scene.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <tuple>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/game.h"
#include "model/inbox.h"
#include "model/role_utils.h"
#include "model/training.h"
#include "model/world_rng.h"

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 1000.0f;
/** Acute:chronic ratios read as a rising or falling load. */
constexpr float RISING_LOAD = 1.15f;
constexpr float FALLING_LOAD = 0.85f;
/** Load chart: plot height and the top of both scales. */
constexpr float CHART_HEIGHT = 132.0f;
constexpr float CHART_RATIO_TOP = 2.0f;
constexpr float CHART_LOAD_TOP = TrainingModel::MATCH_LOAD * 1.25f;
constexpr float BAND_ALPHA = 0.14f;
constexpr std::byte REST_FLAG = static_cast<std::byte>(MEDICAL_FLAG_REST);
constexpr std::byte LIMIT_MINUTES_FLAG =
    static_cast<std::byte>(MEDICAL_FLAG_LIMIT_MINUTES);

/** Short tags of the medical instructions, e.g. "Rest · 60'". */
std::string flagTags(std::byte flags)
{
  std::string tags;
  if ((flags & REST_FLAG) != std::byte{}) tags = LOC("MEDICAL_TAG_REST");
  if ((flags & LIMIT_MINUTES_FLAG) != std::byte{})
  {
    if (!tags.empty()) tags += " · ";
    tags += LOC("MEDICAL_TAG_LIMIT");
  }
  return tags;
}

ImVec4 bandColor(RiskBand band)
{
  const Theme::Palette& palette = Theme::palette();
  using enum RiskBand;
  switch (band)
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

std::string reasonsText(std::byte reasons)
{
  static constexpr std::array<std::pair<RiskReason, const char*>, 5> KEYS = {{
      {RISK_REASON_WORKLOAD, "MEDICAL_REASON_WORKLOAD"},
      {RISK_REASON_FATIGUE, "MEDICAL_REASON_FATIGUE"},
      {RISK_REASON_RECENT_INJURY, "MEDICAL_REASON_RECENT_INJURY"},
      {RISK_REASON_CONGESTION, "MEDICAL_REASON_CONGESTION"},
      {RISK_REASON_AGE, "MEDICAL_REASON_AGE"},
  }};
  std::string text;
  for (const auto& [flag, key] : KEYS)
  {
    if ((reasons & static_cast<std::byte>(flag)) == std::byte{}) continue;
    if (!text.empty()) text += ", ";
    text += LOC(key);
  }
  return text.empty() ? std::string(LOC("MEDICAL_REASON_NONE")) : text;
}

/** Percentage change of a multiplier, e.g. 0.92 -> "-8%". */
std::string percentChange(float multiplier)
{
  return std::format("{:+.0f}%", (multiplier - 1.0f) * 100.0f);
}

const std::array<UI::Column, 5>& injuryColumns()
{
  static const std::array<UI::Column, 5> columns = {{
      {"MEDICAL_COL_PLAYER", 0.0f, 0},
      {"MEDICAL_COL_INJURY", 170.0f, 1},
      {"MEDICAL_COL_DAYS", 64.0f, 0},
      {"MEDICAL_COL_BACK", 150.0f, 2},
      {"MEDICAL_COL_RECURRENCE", 100.0f, 3},
  }};
  return columns;
}

const std::array<UI::Column, 6>& riskColumns()
{
  static const std::array<UI::Column, 6> columns = {{
      {"MEDICAL_COL_PLAYER", 0.0f, 0},
      {"MEDICAL_COL_RISK", 92.0f, 0},
      {"MEDICAL_COL_CONDITION", 96.0f, 1},
      {"MEDICAL_COL_SHARPNESS", 96.0f, 4},
      {"MEDICAL_COL_LOAD", 118.0f, 2},
      {"MEDICAL_COL_REASONS", 200.0f, 3},
  }};
  return columns;
}

template <std::size_t N>
std::array<UI::Column, N> localized(const std::array<UI::Column, N>& columns)
{
  std::array<UI::Column, N> result = columns;
  for (UI::Column& column : result) column.label = LOC(column.label);
  return result;
}

/** Thin bar with the value, coloured by the rating scale. */
void conditionBar(float value)
{
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = ImGui::GetTextLineHeight();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const float barHeight = 5.0f * Theme::scale();
  const float textWidth = ImGui::CalcTextSize("100").x + 6.0f * Theme::scale();
  const float barWidth = std::max(0.0f, width - textWidth);
  const float y = start.y + (height - barHeight) * 0.5f;
  drawList->AddRectFilled(ImVec2(start.x, y),
                          ImVec2(start.x + barWidth, y + barHeight),
                          Theme::toU32(Theme::palette().raised), barHeight);
  drawList->AddRectFilled(
      ImVec2(start.x, y),
      ImVec2(start.x + barWidth * std::clamp(value / 100.0f, 0.0f, 1.0f),
             y + barHeight),
      Theme::toU32(Theme::ratingColor(value)), barHeight);
  const std::string label = std::format("{:.0f}", value);
  drawList->AddText(ImVec2(start.x + barWidth + 6.0f * Theme::scale(), start.y),
                    Theme::toU32(Theme::palette().text), label.c_str());
  ImGui::Dummy(ImVec2(width, height));
}
}  // namespace

MedicalScene::MedicalScene(GUIView* parent) : ManagementScene(parent) {}

void MedicalScene::update(float /*deltaTime*/)
{
  /* Medical data changes after an explicit player or instruction action; no
   * frame-driven state needs updating. */
}

void MedicalScene::refresh()
{
  injured.clear();
  risks.clear();
  staff.clear();
  high_risk = 0;
  days_lost = 0;
  GameController& controller = guiView->getController();
  const auto data = controller.getGameData();
  if (!data || !controller.getManagedTeam()) return;
  const MedicalReport report = controller.getMedicalReport();
  const GameDateValue today = controller.getCurrentDate();
  const auto nameOf = [&](PlayerID id) -> std::pair<std::string, std::string>
  {
    const auto player = data->getPlayer(id);
    if (!player) return {};
    return {player->get().getName(),
            RoleUtils::shortName(player->get().getRole())};
  };

  for (const MedicalInjuryRow& row : report.injured)
  {
    InjuryLine line;
    line.id = row.player_id;
    std::tie(line.name, line.role) = nameOf(row.player_id);
    line.injury = LOC(InjuryModel::nameKey(row.type));
    line.days = std::to_string(row.days_left);
    const GameDateValue earliest =
        today + static_cast<size_t>(row.window.earliest_days);
    const GameDateValue latest =
        today + static_cast<size_t>(row.window.latest_days);
    line.back = row.window.earliest_days == row.window.latest_days
                    ? Format::dayMonth(earliest)
                    : std::format("{} - {}", Format::dayMonth(earliest),
                                  Format::dayMonth(latest));
    line.reinjury = row.reinjury;
    line.severity = row.severity;
    days_lost += row.days_left;
    injured.push_back(std::move(line));
  }

  for (const MedicalRiskRow& row : report.squad)
  {
    RiskLine line;
    line.id = row.player_id;
    std::tie(line.name, line.role) = nameOf(row.player_id);
    line.band = row.risk.band;
    line.condition = row.condition;
    line.sharpness = row.sharpness;
    line.load_trend = 0;
    if (row.workload_ratio > RISING_LOAD)
      line.load_trend = 1;
    else if (row.workload_ratio < FALLING_LOAD)
      line.load_trend = -1;
    line.load = std::format("{:.2f}  ·  {}", row.workload_ratio,
                            LOC(line.load_trend > 0   ? "MEDICAL_LOAD_RISING"
                                : line.load_trend < 0 ? "MEDICAL_LOAD_FALLING"
                                                      : "MEDICAL_LOAD_STABLE"));
    line.reasons = reasonsText(static_cast<std::byte>(row.risk.reasons));
    line.returning = row.returning;
    if (const Game* game = controller.getGame())
      line.flags =
          static_cast<std::byte>(game->getMedical().flags(row.player_id));
    if (row.risk.band == RiskBand::High) ++high_risk;
    risks.push_back(std::move(line));
  }

  for (const StaffMember* member : report.medical_staff)
    staff.emplace_back(member->name(), LOC(StaffModel::roleKey(member->role)),
                       StaffModel::rating(*member));
  recovery_effect = percentChange(report.effects.layoff_multiplier);
  prevention_effect = percentChange(report.effects.injury_prevention);
  recovery_good = report.effects.layoff_multiplier <= 1.0f;
  prevention_good = report.effects.injury_prevention <= 1.0f;
  average_condition = report.average_condition;
  average_sharpness = report.average_sharpness;

  // Keep the selected player while he is still in the squad; otherwise
  // start with the player most at risk.
  const auto listed = [this](PlayerID id)
  {
    return std::ranges::contains(risks, id, &RiskLine::id) ||
           std::ranges::contains(injured, id, &InjuryLine::id);
  };
  PlayerID next = listed(selected) ? selected : 0;
  if (next == 0 && !risks.empty()) next = risks.front().id;
  if (next == 0 && !injured.empty()) next = injured.front().id;
  select(next);
}

void MedicalScene::select(PlayerID id)
{
  selected = id;
  selected_name.clear();
  selected_flags = std::byte{};
  chart = {};
  chart_empty = true;
  GameController& controller = guiView->getController();
  const Game* game = controller.getGame();
  const auto data = controller.getGameData();
  if (id == 0 || !game || !data) return;
  if (const auto player = data->getPlayer(id))
    selected_name = player->get().getName();
  selected_flags = static_cast<std::byte>(game->getMedical().flags(id));
  // Today's load is complete only once the day is over.
  chart_today = dayOrdinal(controller.getCurrentDate());
  chart = game->getMedical().loadChart(id, chart_today - 1);
  chart_empty = std::ranges::none_of(chart, &LoadChartDay::recorded);
}

void MedicalScene::setFlag(std::byte flag, bool enabled)
{
  Game* game = guiView->getController().getGame();
  if (!game || selected == 0) return;
  game->getMedical().setFlag(
      selected, static_cast<MedicalFlag>(std::to_integer<std::uint8_t>(flag)),
      enabled);
  selected_flags = static_cast<std::byte>(game->getMedical().flags(selected));
  for (RiskLine& line : risks)
    if (line.id == selected) line.flags = selected_flags;
}

void MedicalScene::nameCell(PlayerID id, const std::string& name,
                            const std::string& role, bool returning)
{
  if (ImGui::Selectable(name.c_str(), id == selected,
                        ImGuiSelectableFlags_SpanAllColumns))
    select(id);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    ImGui::SetTooltip(
        "%s  ·  %s", role.c_str(),
        LOC(returning ? "MEDICAL_RETURNING_HINT" : "MEDICAL_ROW_HINT"));
}

void MedicalScene::renderContent()
{
  UI::pageHeader(LOC("MEDICAL_TITLE"), LOC("MEDICAL_SUBTITLE"));
  if (!guiView->getController().getManagedTeam())
  {
    UI::emptyState(LOC("MEDICAL_NO_CLUB"), nullptr);
    return;
  }
  const Theme::Palette& palette = Theme::palette();
  UI::TileRow tiles(4);
  const float tile = tiles.width();
  const std::string out = std::to_string(injured.size());
  const std::string lost =
      formatLocalized(Format::plural("MEDICAL_TILE_DAYS_LOST", days_lost),
                      {std::to_string(days_lost)});
  tiles.next();
  UI::statTile("out", LOC("MEDICAL_TILE_INJURED"), out.c_str(), lost.c_str(),
               injured.empty() ? palette.positive : palette.text, tile);
  const std::string condition = std::format("{:.0f}", average_condition);
  const std::string sharpness = formatLocalized(
      "MEDICAL_TILE_SHARPNESS", {std::format("{:.0f}", average_sharpness)});
  tiles.next();
  UI::statTile("condition", LOC("MEDICAL_TILE_CONDITION"), condition.c_str(),
               sharpness.c_str(), Theme::ratingColor(average_condition), tile);
  const std::string risky = std::to_string(high_risk);
  tiles.next();
  UI::statTile("risk", LOC("MEDICAL_TILE_HIGH_RISK"), risky.c_str(),
               LOC("MEDICAL_TILE_HIGH_RISK_NOTE"),
               high_risk > 0 ? palette.warning : palette.positive, tile);
  tiles.next();
  UI::statTile("recovery", LOC("MEDICAL_TILE_RECOVERY"),
               recovery_effect.c_str(), LOC("MEDICAL_TILE_RECOVERY_NOTE"),
               recovery_good ? palette.positive : palette.negative, tile);

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool twoColumns = available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
  const float left =
      twoColumns ? std::floor((available - gap) * 0.64f) : available;
  ImGui::BeginGroup();
  renderInjured(left);
  renderRisk(left);
  ImGui::EndGroup();
  if (twoColumns) ImGui::SameLine();
  const float right = twoColumns ? available - gap - left : available;
  ImGui::BeginGroup();
  renderPlayerLoad(right);
  renderStaff(right);
  ImGui::EndGroup();
}

void MedicalScene::renderInjured(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("medical_injured", LOC("MEDICAL_INJURED"), width);
  if (injured.empty())
  {
    UI::emptyState(LOC("MEDICAL_NO_INJURIES"), LOC("MEDICAL_NO_INJURIES_BODY"));
    UI::endCard();
    return;
  }
  const auto columns = localized(injuryColumns());
  if (const UI::ColumnMask mask =
          UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
      UI::beginResponsiveTable(
          "injured", columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (const InjuryLine& line : injured)
    {
      ImGui::PushID(static_cast<int>(line.id));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      nameCell(line.id, line.name, line.role, false);
      if (UI::cell(mask, 1))
        UI::textFitted(line.injury, ImGui::GetContentRegionAvail().x,
                       palette.muted);
      if (UI::cell(mask, 2))
      {
        ImVec4 severityColor = palette.text;
        if (line.severity == InjurySeverity::Major)
          severityColor = palette.negative;
        else if (line.severity == InjurySeverity::Moderate)
          severityColor = palette.warning;
        UI::textRightColored(severityColor, line.days.c_str());
      }
      if (UI::cell(mask, 3)) ImGui::TextUnformatted(line.back.c_str());
      if (UI::cell(mask, 4))
        ImGui::TextColored(bandColor(line.reinjury), "%s",
                           LOC(MedicalCentre::bandKey(line.reinjury)));
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("MEDICAL_BACK_NOTE"));
  ImGui::PopTextWrapPos();
  UI::endCard();
}

void MedicalScene::renderRisk(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("medical_risk", LOC("MEDICAL_RISK"), width);
  static const std::array<const char*, 2> FILTER_KEYS = {
      "MEDICAL_FILTER_RAISED", "MEDICAL_FILTER_ALL"};
  const std::array<const char*, 2> filters = {LOC(FILTER_KEYS[0]),
                                              LOC(FILTER_KEYS[1])};
  if (int filter = show_all ? 1 : 0;
      UI::segmented("##risk_filter", filter, filters))
    show_all = filter == 1;
  if (const auto raised = static_cast<std::size_t>(
          std::ranges::count_if(risks, [](const RiskLine& line)
                                { return line.band != RiskBand::Low; }));
      !show_all && raised == 0)
  {
    UI::emptyState(LOC("MEDICAL_NO_RISK"), LOC("MEDICAL_NO_RISK_BODY"));
    UI::endCard();
    return;
  }
  const auto columns = localized(riskColumns());
  if (const UI::ColumnMask mask =
          UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
      UI::beginResponsiveTable(
          "risk", columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (const RiskLine& line : risks)
    {
      if (!show_all && line.band == RiskBand::Low) continue;
      ImGui::PushID(static_cast<int>(line.id));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      nameCell(line.id, line.name, line.role, line.returning);
      if (UI::cell(mask, 1))
        ImGui::TextColored(bandColor(line.band), "%s",
                           LOC(MedicalCentre::bandKey(line.band)));
      if (UI::cell(mask, 2)) conditionBar(line.condition);
      if (UI::cell(mask, 3)) conditionBar(line.sharpness);
      if (UI::cell(mask, 4))
        ImGui::TextColored(line.load_trend > 0 ? palette.warning : palette.text,
                           "%s", line.load.c_str());
      if (UI::cell(mask, 5))
      {
        if (line.flags != std::byte{})
        {
          ImGui::TextColored(palette.warning, "%s",
                             flagTags(line.flags).c_str());
          ImGui::SameLine();
        }
        UI::textFitted(line.reasons, ImGui::GetContentRegionAvail().x,
                       palette.muted);
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("MEDICAL_RISK_NOTE"));
  ImGui::PopTextWrapPos();
  UI::endCard();
}

void MedicalScene::renderStaff(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("medical_staff", LOC("MEDICAL_STAFF"), width);
  const float keyWidth = std::min(ImGui::GetContentRegionAvail().x * 0.55f,
                                  190.0f * Theme::scale());
  UI::keyValue(LOC("MEDICAL_EFFECT_RECOVERY"), recovery_effect.c_str(),
               keyWidth);
  UI::keyValue(LOC("MEDICAL_EFFECT_PREVENTION"), prevention_effect.c_str(),
               keyWidth);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  if (staff.empty())
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.warning, "%s", LOC("MEDICAL_STAFF_NONE"));
    ImGui::PopTextWrapPos();
  }
  for (const StaffLine& line : staff)
  {
    ImGui::PushID(line.name.c_str());
    UI::ratingChip(line.rating);
    ImGui::SameLine();
    ImGui::BeginGroup();
    UI::textFitted(line.name, ImGui::GetContentRegionAvail().x, palette.text);
    {
      Theme::ScopedText small(Theme::Text::SMALL);
      ImGui::TextColored(palette.muted, "%s", line.role.c_str());
    }
    ImGui::EndGroup();
    ImGui::PopID();
  }
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  if (UI::secondaryButton(LOC("MEDICAL_OPEN_STAFF"), ImVec2(-FLT_MIN, 0.0f)))
    Navigation::open(guiView, NavSection::STAFF);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("MEDICAL_STAFF_NOTE"));
  ImGui::PopTextWrapPos();
  UI::endCard();

  UI::beginAutoHeightCard("medical_fitness", LOC("MEDICAL_FITNESS"), width);
  const float labelWidth = 110.0f * Theme::scale();
  const std::string conditionText = std::format("{:.0f}", average_condition);
  UI::meter(LOC("MEDICAL_COL_CONDITION"), average_condition / 100.0f,
            labelWidth, Theme::ratingColor(average_condition),
            conditionText.c_str());
  const std::string sharpnessText = std::format("{:.0f}", average_sharpness);
  UI::meter(LOC("MEDICAL_COL_SHARPNESS"), average_sharpness / 100.0f,
            labelWidth, Theme::ratingColor(average_sharpness),
            sharpnessText.c_str());
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("MEDICAL_FITNESS_NOTE"));
  ImGui::PopTextWrapPos();
  if (UI::secondaryButton(LOC("MEDICAL_OPEN_TRAINING"), ImVec2(-FLT_MIN, 0.0f)))
    Navigation::open(guiView, NavSection::TRAINING);
  UI::endCard();
}

void MedicalScene::renderPlayerLoad(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("medical_load", LOC("MEDICAL_LOAD"), width);
  if (selected == 0)
  {
    UI::emptyState(LOC("MEDICAL_LOAD_NONE"), nullptr);
    UI::endCard();
    return;
  }
  UI::textFitted(selected_name, ImGui::GetContentRegionAvail().x, palette.text);
  if (chart_empty)
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.faint, "%s", LOC("MEDICAL_LOAD_EMPTY"));
    ImGui::PopTextWrapPos();
  }
  else
  {
    renderLoadChart(CHART_HEIGHT * Theme::scale());
    const auto latest = std::ranges::find_if(chart.rbegin(), chart.rend(),
                                             &LoadChartDay::recorded);
    const RiskBand band = MedicalCentre::loadBand(latest->ratio);
    const std::string ratio = formatLocalized(
        "MEDICAL_LOAD_LATEST", {std::format("{:.2f}", latest->ratio)});
    ImGui::TextUnformatted(ratio.c_str());
    ImGui::SameLine();
    ImGui::TextColored(bandColor(band), "%s",
                       LOC(MedicalCentre::bandKey(band)));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.faint, "%s", LOC("MEDICAL_LOAD_NOTE"));
    ImGui::PopTextWrapPos();
  }

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  UI::sectionLabel(LOC("MEDICAL_INSTRUCTIONS"));
  if (bool rest = (selected_flags & REST_FLAG) != std::byte{};
      ImGui::Checkbox(LOC("MEDICAL_FLAG_REST"), &rest))
    setFlag(REST_FLAG, rest);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("%s", LOC("MEDICAL_FLAG_REST_HINT"));
  if (bool limit = (selected_flags & LIMIT_MINUTES_FLAG) != std::byte{};
      ImGui::Checkbox(LOC("MEDICAL_FLAG_LIMIT"), &limit))
    setFlag(LIMIT_MINUTES_FLAG, limit);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("%s", LOC("MEDICAL_FLAG_LIMIT_HINT"));
  if (selected_flags != std::byte{})
  {
    ImGui::PushTextWrapPos(0.0f);
    if ((selected_flags & REST_FLAG) != std::byte{})
      ImGui::TextColored(palette.muted, "%s", LOC("MEDICAL_FLAG_REST_HINT"));
    if ((selected_flags & LIMIT_MINUTES_FLAG) != std::byte{})
      ImGui::TextColored(palette.muted, "%s", LOC("MEDICAL_FLAG_LIMIT_HINT"));
    ImGui::PopTextWrapPos();
  }
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  if (UI::secondaryButton(LOC("MEDICAL_OPEN_PROFILE"), ImVec2(-FLT_MIN, 0.0f)))
    Navigation::openPlayer(guiView, selected);
  UI::endCard();
}

void MedicalScene::renderLoadChart(float height)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##load_chart", ImVec2(width, height));
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const float bottom = origin.y + height;
  const auto ratioY = [&](float ratio)
  { return bottom - height * std::clamp(ratio / CHART_RATIO_TOP, 0.0f, 1.0f); };

  // Risk zones of the acute:chronic ratio behind the bars.
  const auto band = [&](float from, float to, const ImVec4& color)
  {
    drawList->AddRectFilled(ImVec2(origin.x, ratioY(to)),
                            ImVec2(origin.x + width, ratioY(from)),
                            Theme::toU32(color, BAND_ALPHA));
  };
  band(0.0f, MedicalCentre::LOAD_ZONE_LOW, palette.warning);
  band(MedicalCentre::LOAD_ZONE_LOW, MedicalCentre::LOAD_ZONE_HIGH,
       palette.positive);
  band(MedicalCentre::LOAD_ZONE_HIGH, MedicalCentre::LOAD_SPIKE,
       palette.warning);
  band(MedicalCentre::LOAD_SPIKE, CHART_RATIO_TOP, palette.negative);

  const float slot = width / static_cast<float>(chart.size());
  const float barWidth = std::max(1.0f, slot * 0.62f);
  int hoveredIndex = -1;
  if (hovered)
    hoveredIndex = std::clamp(
        static_cast<int>((ImGui::GetIO().MousePos.x - origin.x) / slot), 0,
        static_cast<int>(chart.size()) - 1);
  std::array<ImVec2, MedicalCentre::LOAD_CHART_DAYS> line{};
  std::size_t points = 0;
  const auto flushLine = [&]
  {
    if (points > 1)
      drawList->AddPolyline(line.data(), static_cast<int>(points),
                            Theme::toU32(palette.text), ImDrawFlags_None,
                            2.0f * scale);
    points = 0;
  };
  for (std::size_t index = 0; index < chart.size(); ++index)
  {
    const LoadChartDay& day = chart[index];
    const float x = origin.x + slot * static_cast<float>(index);
    if (!day.recorded)
    {
      flushLine();
      continue;
    }
    const float barHeight =
        height * std::clamp(day.load / CHART_LOAD_TOP, 0.0f, 1.0f);
    const bool highlight = static_cast<int>(index) == hoveredIndex;
    drawList->AddRectFilled(
        ImVec2(x + (slot - barWidth) * 0.5f, bottom - barHeight),
        ImVec2(x + (slot + barWidth) * 0.5f, bottom),
        Theme::toU32(highlight ? palette.text : palette.muted,
                     highlight ? 0.9f : 0.55f),
        2.0f * scale);
    line[points++] = ImVec2(x + slot * 0.5f, ratioY(day.ratio));
  }
  flushLine();
  drawList->AddLine(ImVec2(origin.x, bottom), ImVec2(origin.x + width, bottom),
                    Theme::toU32(palette.border));

  if (hoveredIndex >= 0 &&
      chart[static_cast<std::size_t>(hoveredIndex)].recorded)
  {
    const LoadChartDay& day = chart[static_cast<std::size_t>(hoveredIndex)];
    const GameDateValue date = guiView->getController().getCurrentDate() -
                               static_cast<size_t>(chart_today - day.day);
    ImGui::SetTooltip("%s", formatLocalized("MEDICAL_LOAD_TOOLTIP",
                                            {Format::dayMonth(date),
                                             std::format("{:.1f}", day.load),
                                             std::format("{:.2f}", day.ratio)})
                                .c_str());
  }
  Theme::ScopedText small(Theme::Text::SMALL);
  ImGui::TextColored(palette.faint, "%s", LOC("MEDICAL_LOAD_FOUR_WEEKS"));
  const char* today = LOC("MEDICAL_LOAD_YESTERDAY");
  ImGui::SameLine(std::max(0.0f, ImGui::GetCursorPosX() +
                                     ImGui::GetContentRegionAvail().x -
                                     ImGui::CalcTextSize(today).x));
  ImGui::TextColored(palette.faint, "%s", today);
}

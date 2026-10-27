// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/squad_planner_scene.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/inbox.h"
#include "model/role_utils.h"
#include "model/season_agenda.h"

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 1000.0f;
constexpr std::array<const char*, AGE_BAND_COUNT> AGE_BAND_LABELS = {
    "16-21", "22-25", "26-29", "30-32", "33+"};

const char* tierKey(DepthTier tier)
{
  using enum DepthTier;
  switch (tier)
  {
    case FirstChoice:
      return "PLANNER_TIER_FIRST";
    case Prospect:
      return "PLANNER_TIER_PROSPECT";
    case Backup:
      break;
  }
  return "PLANNER_TIER_BACKUP";
}

/** Position a recruitment focus for a group searches for. */
PlayerRole focusRole(PlannerGroup group)
{
  using enum PlannerGroup;
  using enum PlayerRole;
  switch (group)
  {
    case Goalkeeper:
      return GK;
    case CentreBack:
      return CB;
    case LeftBack:
      return LB;
    case RightBack:
      return RB;
    case Wide:
      return LW;
    case Striker:
      return ST;
    case Midfield:
    case COUNT:
      break;
  }
  return CM;
}

const std::array<UI::Column, 6>& depthColumns()
{
  static const std::array<UI::Column, 6> columns = {{
      {"PLANNER_COL_PLAYER", 0.0f, 0},
      {"PLANNER_COL_TIER", 100.0f, 1},
      {"PLANNER_COL_AGE", 44.0f, 2},
      {"PLANNER_COL_OVERALL", 60.0f, 0},
      {"PLANNER_COL_CONTRACT", 84.0f, 3},
      {"PLANNER_COL_STATUS", 120.0f, 1},
  }};
  return columns;
}
}  // namespace

SquadPlannerScene::SquadPlannerScene(GUIView* parent) : ManagementScene(parent)
{
}

void SquadPlannerScene::update(float /*deltaTime*/)
{
  // This screen has no time-driven state; refresh() rebuilds its projections
  // when the career data changes.
}

void SquadPlannerScene::refresh()
{
  const GameController& controller = guiView->getController();
  if (!controller.getManagedTeam()) return;
  season_year = static_cast<std::uint16_t>(
      SeasonAgenda::seasonStart(controller.getCurrentDate()).year + 1);
  build(views[0], 0);
  build(views[1], 1);
}

void SquadPlannerScene::build(View& view, int season_offset) const
{
  const GameController& controller = guiView->getController();
  const auto data = controller.getGameData();
  const SquadPlan plan = controller.getSquadPlan(season_offset);
  const auto nameOf = [&](PlayerID id)
  {
    const auto player = data ? data->getPlayer(id) : std::nullopt;
    return player ? player->get().getName() : std::string();
  };

  view = View{};
  view.squad_size = formatLocalized(
      "PLANNER_SQUAD_SIZE",
      {std::to_string(plan.squad_size),
       std::to_string(SquadPlanner::targetDepth(PlannerGroup::Goalkeeper) +
                      SquadPlanner::targetDepth(PlannerGroup::CentreBack) +
                      SquadPlanner::targetDepth(PlannerGroup::LeftBack) +
                      SquadPlanner::targetDepth(PlannerGroup::RightBack) +
                      SquadPlanner::targetDepth(PlannerGroup::Midfield) +
                      SquadPlanner::targetDepth(PlannerGroup::Wide) +
                      SquadPlanner::targetDepth(PlannerGroup::Striker))});
  view.average_age = std::format("{:.1f}", plan.average_age);
  view.age_bands = plan.age_bands;
  for (std::size_t band = 0; band < AGE_BAND_COUNT; ++band)
    view.age_counts[band] = std::to_string(plan.age_bands[band]);

  for (std::size_t index = 0; index < PLANNER_GROUP_COUNT; ++index)
  {
    const GroupDepth& depth = plan.groups[index];
    Group& group = view.groups[index];
    group.group = depth.group;
    group.missing = depth.missing;
    group.header = std::format("{}  \u{B7}  {} / {}",
                               LOC(SquadPlanner::groupKey(depth.group)),
                               depth.players.size(), depth.target);
    for (const DepthEntry& entry : depth.players)
    {
      Row row;
      row.id = entry.player.id;
      row.name = nameOf(entry.player.id);
      row.role = RoleUtils::shortName(entry.player.role);
      row.age_value = entry.player.age;
      row.age = std::to_string(entry.player.age);
      row.overall_value = entry.player.overall;
      row.overall = std::format("{}{:.0f}", season_offset > 0 ? "~" : "",
                                entry.player.overall);
      row.contract =
          entry.player.contract_years <= 1
              ? std::string(LOC("PLANNER_CONTRACT_ENDS"))
              : formatLocalized("STAFF_CONTRACT_YEARS",
                                {std::to_string(entry.player.contract_years)});
      row.tier = entry.tier;
      row.expiring = entry.expiring;
      row.ageing = entry.ageing;
      row.injured = entry.player.injured;
      row.given = controller.getSquadStatus(entry.player.id);
      row.deserved = controller.getDeservedSquadStatus(entry.player.id);
      row.status = row.given ? LOC(SquadStatusModel::nameKey(*row.given))
                             : LOC(SquadStatusModel::nameKey(row.deserved));
      view.expiring += entry.expiring ? 1 : 0;
      group.rows.push_back(std::move(row));
    }
  }

  for (const PlannerNeed& need : plan.needs)
  {
    Need line;
    line.kind = need.kind;
    line.group = need.group;
    const std::string group = LOC(SquadPlanner::groupKey(need.group));
    const GroupDepth& depth =
        plan.groups[static_cast<std::size_t>(std::to_underlying(need.group))];
    line.min_ability = depth.first_choice_average;
    switch (need.kind)
    {
      case NeedKind::Missing:
        line.text =
            formatLocalized(Format::plural("PLANNER_NEED_MISSING", need.count),
                            {std::to_string(need.count), group});
        line.min_ability = depth.first_choice_average - 6.0f;
        break;
      case NeedKind::Upgrade:
        line.text = formatLocalized("PLANNER_NEED_UPGRADE", {group});
        line.min_ability = depth.first_choice_average + 3.0f;
        break;
      case NeedKind::Succession:
      {
        const auto found =
            std::ranges::find_if(depth.players, [&](const DepthEntry& entry)
                                 { return entry.player.id == need.player_id; });
        const bool contract = found != depth.players.end() && found->expiring;
        const std::string age = found != depth.players.end()
                                    ? std::to_string(found->player.age)
                                    : std::string();
        line.text = contract
                        ? formatLocalized("PLANNER_NEED_SUCCESSION_CONTRACT",
                                          {nameOf(need.player_id), group})
                        : formatLocalized("PLANNER_NEED_SUCCESSION_AGE",
                                          {nameOf(need.player_id), age, group});
        break;
      }
      case NeedKind::Surplus:
        line.text = formatLocalized("PLANNER_NEED_SURPLUS",
                                    {std::to_string(need.count), group});
        break;
    }
    view.needs.push_back(std::move(line));
  }

  const int end_year = season_year + season_offset;
  for (const ContractExpiry& expiry : plan.expiries)
  {
    std::string names;
    for (const PlayerID id : expiry.players)
    {
      if (!names.empty()) names += ", ";
      names += nameOf(id);
    }
    view.expiries.emplace_back(std::to_string(end_year + expiry.years_left - 1),
                               std::move(names));
  }
  for (const PlayerID id : plan.departures)
  {
    if (!view.departures.empty()) view.departures += ", ";
    view.departures += nameOf(id);
  }
}

void SquadPlannerScene::renderContent()
{
  UI::pageHeader(LOC("PLANNER_TITLE"), LOC("PLANNER_SUBTITLE"));
  if (!guiView->getController().getManagedTeam())
  {
    UI::emptyState(LOC("PLANNER_NO_CLUB"), nullptr);
    return;
  }
  const Theme::Palette& palette = Theme::palette();
  const std::array<const char*, 2> seasons = {LOC("PLANNER_THIS_SEASON"),
                                              LOC("PLANNER_NEXT_SEASON")};
  UI::segmented("##planner_season", season, seasons);
  if (season == 1)
  {
    UI::sameLineIfFits(ImGui::CalcTextSize(LOC("PLANNER_NEXT_NOTE")).x,
                       Theme::Space::L * Theme::scale());
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                         (UI::buttonHeight() - ImGui::GetTextLineHeight()) *
                             0.5f);
    ImGui::TextColored(palette.faint, "%s", LOC("PLANNER_NEXT_NOTE"));
  }
  const View& view = views[static_cast<std::size_t>(season)];

  UI::TileRow tiles(4);
  const float tile = tiles.width();
  tiles.next();
  UI::statTile("size", LOC("PLANNER_TILE_SIZE"), view.squad_size.c_str(),
               LOC("PLANNER_TILE_SIZE_NOTE"), palette.text, tile);
  tiles.next();
  UI::statTile("age", LOC("PLANNER_TILE_AGE"), view.average_age.c_str(),
               LOC("PLANNER_TILE_AGE_NOTE"), palette.text, tile);
  const std::string expiring = std::to_string(view.expiring);
  tiles.next();
  UI::statTile("expiring", LOC("PLANNER_TILE_EXPIRING"), expiring.c_str(),
               LOC("PLANNER_TILE_EXPIRING_NOTE"),
               view.expiring > 0 ? palette.warning : palette.text, tile);
  const std::string needs = std::to_string(view.needs.size());
  tiles.next();
  UI::statTile("needs", LOC("PLANNER_TILE_NEEDS"), needs.c_str(),
               LOC("PLANNER_TILE_NEEDS_NOTE"),
               view.needs.empty() ? palette.positive : palette.text, tile);

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool twoColumns = available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
  const float left =
      twoColumns ? std::floor((available - gap) * 0.62f) : available;
  renderDepth(left);
  if (twoColumns) ImGui::SameLine();
  ImGui::BeginGroup();
  const float right = twoColumns ? available - gap - left : available;
  renderNeeds(right);
  renderProfile(right);
  ImGui::EndGroup();
  if (stale)
  {
    stale = false;
    refresh();
  }
}

void SquadPlannerScene::renderDepth(float width)
{
  UI::beginAutoHeightCard("planner_depth", LOC("PLANNER_DEPTH"), width);
  for (const Group& group : views[static_cast<std::size_t>(season)].groups)
    renderGroup(group);
  UI::endCard();
}

void SquadPlannerScene::renderGroup(const Group& group)
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::PushID(static_cast<int>(std::to_underlying(group.group)));
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  UI::sectionLabel(group.header.c_str());
  if (group.missing > 0)
  {
    ImGui::SameLine();
    const std::string missing =
        formatLocalized(Format::plural("PLANNER_MISSING_BADGE", group.missing),
                        {std::to_string(group.missing)});
    UI::badge(missing.c_str(), palette.warning);
  }
  if (group.rows.empty())
  {
    ImGui::TextColored(palette.faint, "%s", LOC("PLANNER_GROUP_EMPTY"));
    ImGui::PopID();
    return;
  }
  std::array<UI::Column, 6> columns = depthColumns();
  for (UI::Column& column : columns) column.label = LOC(column.label);
  const UI::ColumnMask mask =
      UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
  const ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH;
  // The selected row opens its detail strip right under it: the table is
  // split there and continues with the same id (shared column widths).
  UI::TableHeader header = UI::TableHeader::STATIC;
  std::size_t next = 0;
  while (next < group.rows.size())
  {
    if (!UI::beginResponsiveTable("depth", columns, mask, flags, header)) break;
    header = UI::TableHeader::NONE;
    const Row* opened = nullptr;
    while (next < group.rows.size() && opened == nullptr)
    {
      const Row& row = group.rows[next++];
      ImGui::PushID(static_cast<int>(row.id));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      const bool isSelected = selected == row.id;
      if (ImGui::Selectable(row.name.c_str(), isSelected,
                            ImGuiSelectableFlags_SpanAllColumns))
        selected = isSelected ? 0 : row.id;
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s  ·  %s", row.role.c_str(),
                          LOC("PLANNER_ROW_HINT"));
      if (UI::cell(mask, 1))
        ImGui::TextColored(
            row.tier == DepthTier::FirstChoice ? palette.text : palette.muted,
            "%s", LOC(tierKey(row.tier)));
      if (UI::cell(mask, 2))
        ImGui::TextColored(row.ageing ? palette.warning : palette.text, "%s",
                           row.age.c_str());
      if (UI::cell(mask, 3))
        ImGui::TextColored(Theme::ratingColor(row.overall_value), "%s",
                           row.overall.c_str());
      if (UI::cell(mask, 4))
        ImGui::TextColored(row.expiring ? palette.warning : palette.text, "%s",
                           row.contract.c_str());
      if (UI::cell(mask, 5))
        ImGui::TextColored(row.given ? palette.text : palette.faint, "%s",
                           row.status.c_str());
      ImGui::PopID();
      if (isSelected) opened = &row;
    }
    ImGui::EndTable();
    if (opened != nullptr) renderDetail(*opened);
  }
  ImGui::PopID();
}

void SquadPlannerScene::renderDetail(const Row& row)
{
  const Theme::Palette& palette = Theme::palette();
  GameController& controller = guiView->getController();
  const float scale = Theme::scale();
  ImGui::PushID(static_cast<int>(row.id));
  ImGui::Indent(Theme::Space::M * scale);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
  if (season == 0)
  {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(palette.muted, "%s", LOC("PLANNER_STATUS"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(
        std::min(220.0f * scale, ImGui::GetContentRegionAvail().x));
    const std::string none = formatLocalized(
        "PLANNER_STATUS_NONE", {LOC(SquadStatusModel::nameKey(row.deserved))});
    const char* preview =
        row.given ? LOC(SquadStatusModel::nameKey(*row.given)) : none.c_str();
    std::optional<std::optional<SquadStatus>> chosen;
    if (ImGui::BeginCombo("##status", preview))
    {
      if (ImGui::Selectable(none.c_str(), !row.given))
        chosen.emplace(std::nullopt);
      for (std::size_t index = 0; index < SQUAD_STATUS_COUNT; ++index)
      {
        const auto status = static_cast<SquadStatus>(index);
        const bool allowed =
            status != SquadStatus::Prospect ||
            row.age_value <= SquadStatusModel::PROSPECT_MAX_AGE;
        ImGui::BeginDisabled(!allowed);
        if (ImGui::Selectable(LOC(SquadStatusModel::nameKey(status)),
                              row.given == status))
          chosen = std::optional<SquadStatus>(status);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                                 ImGuiHoveredFlags_AllowWhenDisabled))
          ImGui::SetTooltip("%s",
                            LOC(SquadStatusModel::expectationKey(status)));
      }
      ImGui::EndCombo();
    }
    if (chosen && controller.setSquadStatus(row.id, *chosen))
    {
      showToast(formatLocalized("PLANNER_STATUS_SET", {row.name}));
      stale = true;
    }
    const SquadStatus shown = row.given.value_or(row.deserved);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.faint, "%s",
                       LOC(SquadStatusModel::expectationKey(shown)));
    if (row.given &&
        static_cast<int>(std::to_underlying(*row.given)) >
            static_cast<int>(std::to_underlying(row.deserved)) + 1 &&
        *row.given != SquadStatus::Prospect)
      ImGui::TextColored(palette.warning, "%s", LOC("PLANNER_STATUS_DEMOTION"));
    ImGui::PopTextWrapPos();
  }
  if (UI::secondaryButton(LOC("PLANNER_OPEN_PROFILE")))
    Navigation::openPlayer(guiView, row.id);
  UI::sameLineIfFits(UI::buttonWidth(LOC("PLANNER_COMPARE")));
  if (UI::secondaryButton(LOC("PLANNER_COMPARE")))
    Navigation::openCompare(guiView, row.id);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
  ImGui::Unindent(Theme::Space::M * scale);
  ImGui::PopID();
}

void SquadPlannerScene::renderNeeds(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const View& view = views[static_cast<std::size_t>(season)];
  UI::beginAutoHeightCard("planner_needs", LOC("PLANNER_NEEDS"), width);
  if (view.needs.empty())
    ImGui::TextColored(palette.positive, "%s", LOC("PLANNER_NEEDS_NONE"));
  const auto& focuses = guiView->getController().getRecruitmentFocuses();
  for (std::size_t index = 0; index < view.needs.size(); ++index)
  {
    const Need& need = view.needs[index];
    ImGui::PushID(static_cast<int>(index));
    ImVec4 color = palette.text;
    if (need.kind == NeedKind::Surplus)
      color = palette.muted;
    else if (need.kind == NeedKind::Missing)
      color = palette.warning;
    const bool recruit = need.kind != NeedKind::Surplus;
    const char* action = LOC("PLANNER_ADD_FOCUS");
    const float buttonWidth =
        recruit ? UI::buttonWidth(action, UI::ButtonSize::COMPACT) : 0.0f;
    const float textWidth = ImGui::GetContentRegionAvail().x - buttonWidth -
                            (recruit ? ImGui::GetStyle().ItemSpacing.x : 0.0f);
    ImGui::AlignTextToFramePadding();
    UI::textFitted(need.text, std::max(40.0f, textWidth), color);
    if (recruit)
    {
      ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonWidth);
      const PlayerRole role = focusRole(need.group);
      const bool exists =
          std::ranges::any_of(focuses, [role](const RecruitmentFocus& focus)
                              { return focus.role == role; });
      ImGui::BeginDisabled(exists);
      if (UI::secondaryButton(action, {}, UI::ButtonSize::COMPACT))
        addFocus(need);
      ImGui::EndDisabled();
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled |
                               ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("%s", LOC(exists ? "PLANNER_FOCUS_EXISTS"
                                           : "PLANNER_ADD_FOCUS_HELP"));
    }
    ImGui::PopID();
  }
  if (!view.departures.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
    UI::sectionLabel(LOC("PLANNER_DEPARTURES"));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.muted, "%s", view.departures.c_str());
    ImGui::PopTextWrapPos();
  }
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  if (UI::secondaryButton(LOC("PLANNER_OPEN_SCOUTING"), ImVec2(-FLT_MIN, 0.0f)))
    Navigation::open(guiView, NavSection::SCOUTING);
  UI::endCard();
}

void SquadPlannerScene::renderProfile(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const View& view = views[static_cast<std::size_t>(season)];
  UI::beginAutoHeightCard("planner_ages", LOC("PLANNER_AGE_PROFILE"), width);
  std::array<UI::BarDatum, AGE_BAND_COUNT> bars;
  for (std::size_t band = 0; band < AGE_BAND_COUNT; ++band)
    bars[band] = {AGE_BAND_LABELS[band],
                  static_cast<float>(view.age_bands[band]), palette.info,
                  view.age_counts[band]};
  UI::barChart("ages", bars, ImGui::GetContentRegionAvail().x,
               56.0f * Theme::scale());
  UI::endCard();

  UI::beginAutoHeightCard("planner_contracts", LOC("PLANNER_CONTRACTS"), width);
  const float yearWidth =
      ImGui::CalcTextSize("0000").x + Theme::Space::M * Theme::scale();
  for (const auto& [year, names] : view.expiries)
  {
    ImGui::TextColored(palette.muted, "%s", year.c_str());
    ImGui::SameLine(yearWidth);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(names.c_str());
    ImGui::PopTextWrapPos();
  }
  UI::endCard();
}

void SquadPlannerScene::addFocus(const Need& need)
{
  RecruitmentFocus focus;
  focus.role = focusRole(need.group);
  focus.min_ability = static_cast<std::uint8_t>(
      std::clamp(std::lround(need.min_ability), 0L, 99L));
  if (need.kind == NeedKind::Succession) focus.max_age = 27;
  if (guiView->getController().saveRecruitmentFocus(focus) != 0)
    showToast(formatLocalized("PLANNER_FOCUS_ADDED",
                              {LOC(SquadPlanner::groupKey(need.group))}));
  else
    showToast(LOC("PLANNER_FOCUS_FAILED"), true);
}

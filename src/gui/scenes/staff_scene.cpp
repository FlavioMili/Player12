// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/staff_scene.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <tuple>
#include <utility>
#include <vector>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/inbox.h"

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 940.0f;
constexpr float KEY_WIDTH = 150.0f;
constexpr const char* RELEASE_POPUP_ID = "##release_staff";
constexpr int MAX_HIRE_YEARS = 4;
constexpr int MAX_CONTRACT_YEARS = 5;

/** Backroom departments, in display order. */
constexpr std::array<const char*, 4> DEPARTMENT_KEYS = {
    "STAFF_DEPT_COACHING", "STAFF_DEPT_MEDICAL", "STAFF_DEPT_SCOUTING",
    "STAFF_DEPT_YOUTH"};

std::size_t departmentOf(StaffRole role)
{
  using enum StaffRole;
  switch (role)
  {
    case Physio:
    case SportsScientist:
      return 1;
    case Scout:
      return 2;
    case YouthCoach:
    case HeadOfYouth:
      return 3;
    default:
      return 0;
  }
}

/** Department, then role, then best rating first: groups become ranges. */
template <typename Row>
void sortByDepartment(std::vector<Row>& rows)
{
  std::ranges::stable_sort(
      rows,
      [](const Row& a, const Row& b)
      {
        const auto left = std::tuple(departmentOf(a.role),
                                     static_cast<int>(a.role), -a.rating);
        const auto right = std::tuple(departmentOf(b.role),
                                      static_cast<int>(b.role), -b.rating);
        return left < right;
      });
}

// Columns hide from the highest priority number down as the card narrows;
// actions live in the selected row's detail strip, never in a column.
const std::array<UI::Column, 6>& staffColumns()
{
  static const std::array<UI::Column, 6> columns = {{
      {"STAFF_COL_NAME", 0.0f, 0},
      {"STAFF_COL_ROLE", 150.0f, 1},
      {"STAFF_COL_RATING", 60.0f, 0},
      {"STAFF_COL_AGE", 44.0f, 3},
      {"STAFF_COL_WAGE", 84.0f, 2},
      {"STAFF_COL_CONTRACT", 118.0f, 1},
  }};
  return columns;
}

const std::array<UI::Column, 6>& marketColumns()
{
  static const std::array<UI::Column, 6> columns = {{
      {"STAFF_COL_NAME", 0.0f, 0},
      {"STAFF_COL_ROLE", 150.0f, 1},
      {"STAFF_COL_RATING", 60.0f, 0},
      {"STAFF_COL_AGE", 44.0f, 3},
      {"STAFF_COL_ATTRIBUTES", 230.0f, 2},
      {"STAFF_COL_DEMAND", 84.0f, 1},
  }};
  return columns;
}

std::string keyAttributes(const StaffMember& member)
{
  const auto keys = StaffModel::keyAttributes(member.role);
  return std::format("{} {}  ·  {} {}", LOC(StaffModel::attributeKey(keys[0])),
                     member.attribute(keys[0]),
                     LOC(StaffModel::attributeKey(keys[1])),
                     member.attribute(keys[1]));
}

const char* resultKey(GameController::StaffActionResult result)
{
  using enum GameController::StaffActionResult;
  switch (result)
  {
    case RoleFull:
      return "STAFF_RESULT_ROLE_FULL";
    case CannotAfford:
      return "STAFF_RESULT_CANNOT_AFFORD";
    case InvalidTerms:
      return "STAFF_RESULT_INVALID_TERMS";
    case NotAvailable:
    case UnknownStaff:
      return "STAFF_RESULT_NOT_AVAILABLE";
    case NoClub:
    case Ok:
      break;
  }
  return "STAFF_RESULT_NO_CLUB";
}
}  // namespace

StaffScene::StaffScene(GUIView* parent) : ManagementScene(parent) {}

void StaffScene::update(float /*deltaTime*/)
{
  // Staff data changes only through scene actions; refresh() rebuilds the
  // rows after navigation or an action, so no per-frame work is needed.
}

void StaffScene::refresh()
{
  GameController& controller = guiView->getController();
  staff_rows.clear();
  responsibilities.clear();
  const auto managed = controller.getManagedTeam();
  if (!managed)
  {
    market_rows.clear();
    return;
  }
  const TeamID team_id = managed->get().getId();
  effects = controller.getStaffEffects(team_id);
  const auto staff = controller.getStaff(team_id);
  for (const StaffMember* member : staff)
  {
    StaffRow row;
    row.id = member->id;
    row.role = member->role;
    row.name = member->name();
    row.attributes = keyAttributes(*member);
    row.age = member->age;
    row.rating = StaffModel::rating(*member);
    row.wage = Format::money(member->wage);
    row.demand = Format::money(controller.getStaffWageDemand(member->id));
    row.contract_years = member->contract_years;
    row.contract =
        member->contract_years == 1
            ? std::string(LOC("STAFF_CONTRACT_ONE_SEASON"))
            : formatLocalized("STAFF_CONTRACT_YEARS",
                              {std::to_string(member->contract_years)});
    row.extend_help = formatLocalized("STAFF_EXTEND_HELP", {row.demand});
    staff_rows.push_back(std::move(row));
  }
  sortByDepartment(staff_rows);

  // Who covers each area (the best specialist, else the assistant).
  const auto holder = [&](StaffRole role) -> std::string
  {
    for (const StaffMember* member : staff)
    {
      if (member->role == role) return member->name();
    }
    for (const StaffMember* member : staff)
    {
      if (member->role == StaffRole::AssistantManager)
        return std::format("{} ({})", member->name(), LOC("STAFF_COVERING"));
    }
    return LOC("STAFF_VACANT");
  };
  responsibilities = {
      {"STAFF_AREA_TACTICS", effects.coaching_tactical,
       holder(StaffRole::AssistantManager)},
      {"STAFF_AREA_FITNESS", effects.coaching_fitness,
       holder(StaffRole::FitnessCoach)},
      {"STAFF_AREA_ATTACKING", effects.coaching_attacking,
       holder(StaffRole::AttackingCoach)},
      {"STAFF_AREA_DEFENDING", effects.coaching_defending,
       holder(StaffRole::DefendingCoach)},
      {"STAFF_AREA_GOALKEEPING", effects.coaching_goalkeeping,
       holder(StaffRole::GoalkeepingCoach)},
      {"STAFF_AREA_YOUTH", effects.coaching_youth,
       holder(StaffRole::YouthCoach)},
      {"STAFF_AREA_MEDICAL", (1.12f - effects.layoff_multiplier) / 0.27f,
       holder(StaffRole::Physio)},
      {"STAFF_AREA_SCIENCE", (1.08f - effects.injury_prevention) / 0.20f,
       holder(StaffRole::SportsScientist)},
      {"STAFF_AREA_SCOUTING", effects.scouting_accuracy,
       holder(StaffRole::Scout)},
  };
  rebuildMarket();
}

void StaffScene::rebuildMarket()
{
  GameController& controller = guiView->getController();
  market_rows.clear();
  const auto managed = controller.getManagedTeam();
  if (!managed) return;
  // Occupied posts per role, counted once.
  std::array<std::size_t, STAFF_ROLE_COUNT> employed{};
  for (const StaffMember* member : controller.getStaff(managed->get().getId()))
    ++employed[static_cast<std::size_t>(std::to_underlying(member->role))];
  for (const StaffMember* member : controller.getStaffMarket())
  {
    if (role_filter >= 0 &&
        static_cast<int>(std::to_underlying(member->role)) != role_filter)
      continue;
    const int rating = StaffModel::rating(*member);
    if (rating < min_rating) continue;
    StaffRow row;
    row.id = member->id;
    row.role = member->role;
    row.name = member->name();
    row.attributes = keyAttributes(*member);
    row.age = member->age;
    row.rating = rating;
    row.demand = Format::money(controller.getStaffWageDemand(member->id));
    row.hire_label = formatLocalized("STAFF_HIRE_FOR", {row.demand});
    const auto role =
        static_cast<std::size_t>(std::to_underlying(member->role));
    row.role_full = employed[role] >= StaffModel::ROLE_LIMITS[role];
    market_rows.push_back(std::move(row));
  }
  sortByDepartment(market_rows);
}

void StaffScene::showResult(int result, const char* success_key)
{
  const auto value = static_cast<GameController::StaffActionResult>(result);
  if (value == GameController::StaffActionResult::Ok)
  {
    showToast(LOC(success_key));
    refresh();
  }
  else
  {
    showToast(LOC(resultKey(value)), true);
  }
}

void StaffScene::renderContent()
{
  if (const auto managed = guiView->getController().getManagedTeam(); !managed)
    return;
  const Theme::Palette& palette = Theme::palette();
  UI::pageHeader(LOC("STAFF_TITLE"), LOC("STAFF_SUBTITLE"));

  UI::TileRow tiles(4);
  const float tile = tiles.width();
  const std::string payroll = Format::money(effects.weekly_payroll);
  const std::string payroll_note = formatLocalized(
      "STAFF_TILE_PAYROLL_NOTE", {std::to_string(staff_rows.size())});
  tiles.next();
  UI::statTile("payroll", LOC("STAFF_TILE_PAYROLL"), payroll.c_str(),
               payroll_note.c_str(), palette.text, tile);
  const std::string coaching =
      std::format("{:.0f}", effects.training_quality * 100.0f);
  tiles.next();
  UI::statTile("coaching", LOC("STAFF_TILE_COACHING"), coaching.c_str(),
               LOC("STAFF_TILE_COACHING_NOTE"),
               Theme::ratingColor(effects.training_quality * 100.0f), tile);
  const std::string layoffs =
      std::format("{:+.0f}%", (effects.layoff_multiplier - 1.0f) * 100.0f);
  tiles.next();
  UI::statTile(
      "medical", LOC("STAFF_TILE_MEDICAL"), layoffs.c_str(),
      LOC("STAFF_TILE_MEDICAL_NOTE"),
      effects.layoff_multiplier <= 1.0f ? palette.positive : palette.negative,
      tile);
  const std::string scouting =
      std::format("{:.0f}%", effects.scouting_accuracy * 100.0f);
  tiles.next();
  UI::statTile("scouting", LOC("STAFF_TILE_SCOUTING"), scouting.c_str(),
               LOC("STAFF_TILE_SCOUTING_NOTE"),
               Theme::ratingColor(effects.scouting_accuracy * 100.0f), tile);

  // One scroll surface: cards grow with their content and the page scrolls.
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool twoColumns = available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
  const float left =
      twoColumns ? std::floor((available - gap) * 0.62f) : available;
  renderStaff(left);
  if (twoColumns) ImGui::SameLine();
  renderImpact(twoColumns ? available - gap - left : available);
  renderMarket();
  renderReleaseConfirm();

  // Actions run after the tables so the rows are never rebuilt mid-draw.
  GameController& controller = guiView->getController();
  using enum PendingAction::Kind;
  switch (pending.kind)
  {
    case HIRE:
      showResult(static_cast<int>(std::to_underlying(
                     controller.hireStaff(pending.id, pending.years))),
                 "STAFF_HIRED_TOAST");
      selected = 0;
      break;
    case EXTEND:
      showResult(
          static_cast<int>(std::to_underlying(
              controller.extendStaffContract(pending.id, pending.years))),
          "STAFF_EXTENDED_TOAST");
      break;
    case RELEASE:
      showResult(static_cast<int>(
                     std::to_underlying(controller.releaseStaff(pending.id))),
                 "STAFF_RELEASED_TOAST");
      selected = 0;
      break;
    case NONE:
      break;
  }
  pending = {};
}

void StaffScene::renderStaff(float width)
{
  UI::beginAutoHeightCard("staff_current", LOC("STAFF_CURRENT"), width);
  if (staff_rows.empty())
    UI::emptyState(LOC("STAFF_EMPTY_TITLE"), LOC("STAFF_EMPTY_BODY"));
  else
    renderGroups("staff", staff_rows, false);
  UI::endCard();
}

void StaffScene::renderGroups(const char* id, const std::vector<StaffRow>& rows,
                              bool market)
{
  const Theme::Palette& palette = Theme::palette();
  const auto& columns = market ? marketColumns() : staffColumns();
  std::array<UI::Column, 6> localized = columns;
  for (UI::Column& column : localized) column.label = LOC(column.label);
  const UI::ColumnMask mask =
      UI::fitColumns(localized, ImGui::GetContentRegionAvail().x);
  const ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH;

  ImGui::PushID(id);
  // Rows are kept sorted by department, so each group is a contiguous range.
  std::size_t begin = 0;
  while (begin < rows.size())
  {
    const std::size_t department = departmentOf(rows[begin].role);
    std::size_t end = begin;
    while (end < rows.size() && departmentOf(rows[end].role) == department)
      ++end;
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
    const std::string header = std::format(
        "{}  \u00B7  {}", LOC(DEPARTMENT_KEYS[department]), end - begin);
    UI::sectionLabel(header.c_str());
    ImGui::PushID(static_cast<int>(department));

    // The selected row opens a detail strip right under it: the table is
    // split there and continues with the same id (shared column widths).
    UI::TableHeader tableHeader = UI::TableHeader::STATIC;
    std::size_t first = begin;
    while (first < end)
    {
      if (!UI::beginResponsiveTable("group", localized, mask, flags,
                                    tableHeader))
        break;
      tableHeader = UI::TableHeader::NONE;
      std::size_t index = first;
      const StaffRow* opened = nullptr;
      while (index < end && opened == nullptr)
      {
        const StaffRow& row = rows[index++];
        ImGui::PushID(static_cast<int>(row.id));
        ImGui::TableNextRow(ImGuiTableRowFlags_None,
                            UI::buttonHeight(UI::ButtonSize::COMPACT));
        ImGui::TableNextColumn();
        if (const bool isSelected = selected == row.id;
            ImGui::Selectable(row.name.c_str(), isSelected,
                              ImGuiSelectableFlags_SpanAllColumns))
          selected = isSelected ? 0 : row.id;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
          ImGui::SetTooltip("%s", LOC("STAFF_ROW_HINT"));
        if (UI::cell(mask, 1))
          ImGui::TextColored(palette.muted, "%s",
                             LOC(StaffModel::roleKey(row.role)));
        if (UI::cell(mask, 2)) UI::ratingChip(row.rating);
        if (UI::cell(mask, 3)) ImGui::Text("%d", row.age);
        if (market)
        {
          if (UI::cell(mask, 4))
            UI::textFitted(row.attributes, ImGui::GetContentRegionAvail().x,
                           palette.muted);
          if (UI::cell(mask, 5)) ImGui::TextUnformatted(row.demand.c_str());
        }
        else
        {
          if (UI::cell(mask, 4)) ImGui::TextUnformatted(row.wage.c_str());
          if (UI::cell(mask, 5))
            ImGui::TextColored(
                row.contract_years <= 1 ? palette.warning : palette.text, "%s",
                row.contract.c_str());
        }
        ImGui::PopID();
        if (selected == row.id) opened = &row;
      }
      ImGui::EndTable();
      if (opened != nullptr) renderDetail(*opened, market);
      first = index;
    }
    ImGui::PopID();
    begin = end;
  }
  ImGui::PopID();
}

void StaffScene::renderDetail(const StaffRow& row, bool market)
{
  const GameController& controller = guiView->getController();
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->ChannelsSplit(2);
  drawList->ChannelsSetCurrent(1);
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const float pad = Theme::Space::M * scale;
  ImGui::SetCursorScreenPos(ImVec2(start.x + pad, start.y + pad));
  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 2.0f * pad);
  ImGui::TextColored(palette.muted, "%s",
                     LOC(StaffModel::roleDescriptionKey(row.role)));
  ImGui::TextUnformatted(row.attributes.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
  ImGui::PushID(static_cast<int>(row.id));
  if (market)
  {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(palette.muted, "%s", LOC("STAFF_HIRE_YEARS"));
    ImGui::SameLine();
    static constexpr std::array<const char*, MAX_HIRE_YEARS> YEARS = {"1", "2",
                                                                      "3", "4"};
    if (int years = hire_years - 1; UI::segmented("##years", years, YEARS))
      hire_years = years + 1;
    const char* hire = row.hire_label.c_str();
    UI::sameLineIfFits(UI::buttonWidth(hire));
    ImGui::BeginDisabled(row.role_full);
    if (UI::primaryButton(hire))
      pending = {PendingAction::Kind::HIRE, row.id,
                 static_cast<uint8_t>(hire_years)};
    ImGui::EndDisabled();
    if (row.role_full)
      ImGui::TextColored(palette.warning, "%s", LOC("STAFF_RESULT_ROLE_FULL"));
  }
  else
  {
    const int extended = row.contract_years + 1;
    ImGui::BeginDisabled(extended > MAX_CONTRACT_YEARS);
    if (UI::secondaryButton(LOC("STAFF_EXTEND")))
      pending = {PendingAction::Kind::EXTEND, row.id,
                 static_cast<uint8_t>(extended)};
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(palette.faint, "%s", row.extend_help.c_str());
    UI::sameLineIfFits(UI::buttonWidth(LOC("STAFF_RELEASE")));
    if (UI::dangerButton(LOC("STAFF_RELEASE")))
    {
      release_candidate = row.id;
      const auto data = controller.getGameData();
      const StaffMember* member =
          data ? data->getStaff().find(row.id) : nullptr;
      release_text = formatLocalized(
          "STAFF_RELEASE_BODY",
          {row.name,
           Format::money(member ? StaffModel::severance(*member) : 0)});
      release_requested = true;
    }
  }
  ImGui::PopID();
  ImGui::EndGroup();
  const float bottom = ImGui::GetItemRectMax().y + pad;
  drawList->ChannelsSetCurrent(0);
  drawList->AddRectFilled(start, ImVec2(start.x + width, bottom),
                          Theme::toU32(palette.accent, 0.07f), 4.0f * scale);
  drawList->AddRectFilled(start, ImVec2(start.x + 3.0f * scale, bottom),
                          Theme::toU32(palette.accent), 2.0f * scale);
  drawList->ChannelsMerge();
  ImGui::SetCursorScreenPos(ImVec2(start.x, bottom));
  ImGui::Dummy(ImVec2(width, Theme::Space::XS * scale));
}

void StaffScene::renderImpact(float width) const
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("staff_impact", LOC("STAFF_IMPACT"), width);
  const float keyWidth = KEY_WIDTH * Theme::scale();
  for (const Responsibility& item : responsibilities)
  {
    const float quality = std::clamp(item.quality, 0.0f, 1.0f);
    const std::string value = std::format("{:.0f}", quality * 100.0f);
    UI::meter(LOC(item.area_key), quality, keyWidth,
              Theme::ratingColor(quality * 100.0f), value.c_str());
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + keyWidth);
    UI::textFitted(item.holder, ImGui::GetContentRegionAvail().x,
                   palette.faint);
  }
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  UI::sectionLabel(LOC("STAFF_EFFECTS"));
  const std::string risk =
      std::format("x{:.2f}", static_cast<double>(effects.injury_prevention));
  UI::keyValue(LOC("STAFF_EFFECT_INJURY_RISK"), risk.c_str(), keyWidth);
  const std::string youth = std::format(
      "{:+.1f}", static_cast<double>(effects.youth_potential_bonus));
  UI::keyValue(LOC("STAFF_EFFECT_YOUTH"), youth.c_str(), keyWidth);
  const std::string familiarity =
      std::format("x{:.2f}", static_cast<double>(effects.familiarity_rate));
  UI::keyValue(LOC("STAFF_EFFECT_FAMILIARITY"), familiarity.c_str(), keyWidth);
  UI::endCard();
}

void StaffScene::renderFilters()
{
  const Theme::Palette& palette = Theme::palette();
  bool filters_changed = false;
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(palette.muted, "%s", LOC("STAFF_FILTER_ROLE"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(200.0f * Theme::scale());
  if (const char* role_label =
          role_filter < 0
              ? LOC("STAFF_FILTER_ALL")
              : LOC(StaffModel::roleKey(static_cast<StaffRole>(role_filter)));
      ImGui::BeginCombo("##role_filter", role_label))
  {
    if (ImGui::Selectable(LOC("STAFF_FILTER_ALL"), role_filter < 0))
    {
      filters_changed = role_filter >= 0;
      role_filter = -1;
    }
    for (int role = 0; role < static_cast<int>(STAFF_ROLE_COUNT); ++role)
    {
      if (ImGui::Selectable(
              LOC(StaffModel::roleKey(static_cast<StaffRole>(role))),
              role == role_filter))
      {
        filters_changed = role != role_filter;
        role_filter = role;
      }
    }
    ImGui::EndCombo();
  }
  const float ratingWidth = 160.0f * Theme::scale();
  UI::sameLineIfFits(ImGui::CalcTextSize(LOC("STAFF_FILTER_RATING")).x +
                     ratingWidth + Theme::Space::L * Theme::scale());
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(palette.muted, "%s", LOC("STAFF_FILTER_RATING"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ratingWidth);
  ImGui::SliderInt("##min_rating", &min_rating, 0, 90);
  filters_changed |= ImGui::IsItemDeactivatedAfterEdit();
  if (filters_changed) rebuildMarket();
}

void StaffScene::renderMarket()
{
  UI::beginAutoHeightCard("staff_market", LOC("STAFF_MARKET"));
  renderFilters();
  if (market_rows.empty())
    UI::emptyState(LOC("STAFF_MARKET_EMPTY_TITLE"),
                   LOC("STAFF_MARKET_EMPTY_BODY"));
  else
    renderGroups("market", market_rows, true);
  UI::endCard();
}

void StaffScene::renderReleaseConfirm()
{
  if (release_requested)
  {
    release_requested = false;
    ImGui::OpenPopup(RELEASE_POPUP_ID);
  }
  const UI::DialogResult result = UI::confirmDialog(
      RELEASE_POPUP_ID, LOC("STAFF_RELEASE_TITLE"), release_text.c_str(),
      LOC("STAFF_RELEASE"), LOC("SETTINGS_CANCEL"));
  if (result == UI::DialogResult::CONFIRM && release_candidate != 0)
  {
    pending = {PendingAction::Kind::RELEASE, release_candidate, 0};
    release_candidate = 0;
  }
  else if (result == UI::DialogResult::CANCEL)
  {
    release_candidate = 0;
  }
}

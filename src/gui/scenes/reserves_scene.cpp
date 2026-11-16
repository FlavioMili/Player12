// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/reserves_scene.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <span>

#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "model/inbox.h"
#include "model/national_teams.h"
#include "model/role_utils.h"

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 1000.0f;
constexpr float KEY_WIDTH = 170.0f;
constexpr std::size_t RECENT_RESULTS = 6;

enum class Cell : uint8_t
{
  NAME,
  POS,
  AGE,
  ABILITY,
  POTENTIAL,
  CONTRACT,
  APPS,
  RATING,
  ORIGIN
};

struct ColumnSpec
{
  Cell cell;
  UI::Column column;
};

// Columns hide from the highest priority number down as the card narrows.
constexpr std::array<ColumnSpec, 8> SQUAD_COLUMNS = {{
    {Cell::NAME, {"YOUTH_COL_NAME", 0.0f, 0}},
    {Cell::POS, {"YOUTH_COL_POS", 52.0f, 0}},
    {Cell::AGE, {"YOUTH_COL_AGE", 44.0f, 0}},
    {Cell::ABILITY, {"YOUTH_COL_ABILITY", 76.0f, 0}},
    {Cell::POTENTIAL, {"YOUTH_COL_POTENTIAL", 84.0f, 1}},
    {Cell::APPS, {"YOUTH_COL_APPS", 52.0f, 2}},
    {Cell::RATING, {"YOUTH_COL_RATING", 60.0f, 2}},
    {Cell::CONTRACT, {"YOUTH_COL_CONTRACT", 150.0f, 3}},
}};

constexpr std::array<ColumnSpec, 7> CANDIDATE_COLUMNS = {{
    {Cell::NAME, {"YOUTH_COL_NAME", 0.0f, 0}},
    {Cell::POS, {"YOUTH_COL_POS", 52.0f, 0}},
    {Cell::AGE, {"YOUTH_COL_AGE", 44.0f, 0}},
    {Cell::ORIGIN, {"U21_COL_SQUAD", 110.0f, 1}},
    {Cell::ABILITY, {"YOUTH_COL_ABILITY", 76.0f, 0}},
    {Cell::POTENTIAL, {"YOUTH_COL_POTENTIAL", 84.0f, 2}},
    {Cell::CONTRACT, {"YOUTH_COL_CONTRACT", 150.0f, 3}},
}};

constexpr std::array<UI::Column, 5> TABLE_COLUMNS = {{
    {"U21_COL_POS", 40.0f, 0},
    {"U21_COL_CLUB", 0.0f, 0},
    {"U21_COL_RECORD", 130.0f, 1},
    {"U21_COL_GOALS", 70.0f, 2},
    {"U21_COL_POINTS", 50.0f, 0},
}};

std::string range(float low, float high)
{
  const auto lo = static_cast<int>(std::lround(low));
  const auto hi = static_cast<int>(std::lround(high));
  return lo == hi ? std::to_string(lo) : std::format("{}-{}", lo, hi);
}
}  // namespace

ReservesScene::ReservesScene(GUIView* parent) : ManagementScene(parent) {}

void ReservesScene::update(float /*deltaTime*/)
{
  // Reserve state is refreshed on scene entry and after explicit actions.
}

ReservesScene::Row ReservesScene::makeRow(
    const GameController::YouthPlayerView& view) const
{
  Row row;
  row.view = view;
  row.role = RoleUtils::shortName(view.role);
  row.ability = range(view.estimate.current_low, view.estimate.current_high);
  row.potential =
      range(view.estimate.potential_low, view.estimate.potential_high);
  row.contract = formatLocalized("YOUTH_CONTRACT_CELL",
                                 {LOC(YouthModel::contractKey(view.contract)),
                                  std::to_string(view.contract_years)});
  row.rating = view.appearances > 0 ? std::format("{:.1f}", view.average_rating)
                                    : std::string("–");
  row.origin = LOC(view.status == YouthStatus::Squad ? "U21_FROM_U18"
                                                     : "U21_FROM_FIRST_TEAM");
  row.terms = std::format(
      "{}  ·  {}", row.contract,
      formatLocalized("YOUTH_PER_WEEK", {Format::money(view.wage)}));
  row.overage = view.age > YouthModel::U21_MAX_AGE;
  return row;
}

void ReservesScene::refresh()
{
  GameController& controller = guiView->getController();
  cached_date = controller.getCurrentDate();
  squad_rows.clear();
  candidate_rows.clear();
  table.clear();
  results.clear();
  form.clear();
  if (!controller.hasSelectedTeam()) return;
  overview = controller.getReserveOverview();
  for (const auto& view : controller.getYouthPlayers(YouthStatus::Reserve))
    squad_rows.push_back(makeRow(view));
  for (const auto& view : controller.getReserveCandidates())
    candidate_rows.push_back(makeRow(view));
  if (const bool known =
          std::ranges::any_of(squad_rows, [this](const Row& row)
                              { return row.view.id == selected; }) ||
          std::ranges::any_of(candidate_rows, [this](const Row& row)
                              { return row.view.id == selected; });
      !known)
    selected = 0;

  const std::string country =
      LOC(International::teamNameKey(overview.country).c_str());
  league_title = formatLocalized("U21_CARD_LEAGUE", {country});
  subtitle = formatLocalized("U21_SUBTITLE", {country});
  squad_title =
      formatLocalized("U21_CARD_SQUAD", {std::to_string(squad_rows.size())});
  const YouthTableRow& own = overview.table;
  league_line = overview.league_position > 0 && own.played > 0
                    ? formatLocalized("YOUTH_LEAGUE_POSITION",
                                      {std::to_string(overview.league_position),
                                       std::to_string(overview.league_size),
                                       std::to_string(own.points())})
                    : std::string(LOC("YOUTH_LEAGUE_NOT_STARTED"));
  quota_outfield = std::format("{}/{}", overview.quota.overage_outfield,
                               YouthModel::U21_OVERAGE_OUTFIELD);
  quota_keepers = std::format("{}/{}", overview.quota.overage_goalkeepers,
                              YouthModel::U21_OVERAGE_GOALKEEPERS);
  squad_size = std::to_string(overview.quota.squad);

  int position = 0;
  const auto managed = controller.getManagedTeam();
  const TeamID own_id = managed ? managed->get().getId() : TeamID{0};
  for (const YouthTableRow& row : controller.getReserveTable())
  {
    TableLine line;
    line.position = ++position;
    const auto club = controller.getTeamById(row.team_id);
    line.club = club ? club->get().getName() : std::string("?");
    line.record = std::format("{}  ·  {}-{}-{}", row.played, row.won, row.drawn,
                              row.lost);
    line.goals = std::format("{}:{}", row.goals_for, row.goals_against);
    line.points = row.points();
    line.own = row.team_id == own_id;
    table.push_back(std::move(line));
  }

  const auto& played = controller.getReserveResults();
  const std::size_t first =
      played.size() > RECENT_RESULTS ? played.size() - RECENT_RESULTS : 0;
  for (std::size_t i = first; i < played.size(); ++i)
  {
    const YouthResult& result = played[i];
    ResultLine line;
    line.date = Format::dayMonth(result.date);
    const auto opponent = controller.getTeamById(result.opponent_id);
    line.opponent = std::format(
        "{} ({})", opponent ? opponent->get().getName() : std::string("?"),
        LOC(result.home ? "YOUTH_HOME" : "YOUTH_AWAY"));
    line.score = std::format("{}-{}", result.goals_for, result.goals_against);
    if (result.goals_for > result.goals_against)
      line.outcome = UI::Outcome::WIN;
    else if (result.goals_for == result.goals_against)
      line.outcome = UI::Outcome::DRAW;
    else
      line.outcome = UI::Outcome::LOSS;
    form.push_back(line.outcome);
    results.push_back(std::move(line));
  }
}

void ReservesScene::renderContent()
{
  const GameController& controller = guiView->getController();
  if (!controller.hasSelectedTeam()) return;
  if (!(cached_date == controller.getCurrentDate())) refresh();
  UI::pageHeader(LOC("U21_TITLE"), subtitle.c_str());

  const Theme::Palette& palette = Theme::palette();
  UI::TileRow tiles(4);
  const float tile = tiles.width();
  tiles.next();
  UI::statTile("u21_squad", LOC("U21_TILE_SQUAD"), squad_size.c_str(),
               LOC("U21_TILE_SQUAD_NOTE"), palette.text, tile);
  tiles.next();
  UI::statTile(
      "u21_outfield", LOC("U21_TILE_OVERAGE"), quota_outfield.c_str(),
      LOC("U21_TILE_OVERAGE_NOTE"),
      overview.quota.overage_outfield >= YouthModel::U21_OVERAGE_OUTFIELD
          ? palette.warning
          : palette.text,
      tile);
  tiles.next();
  UI::statTile(
      "u21_keepers", LOC("U21_TILE_OVERAGE_GK"), quota_keepers.c_str(),
      LOC("U21_TILE_OVERAGE_GK_NOTE"),
      overview.quota.overage_goalkeepers >= YouthModel::U21_OVERAGE_GOALKEEPERS
          ? palette.warning
          : palette.text,
      tile);
  const std::string position =
      overview.league_position > 0 && overview.table.played > 0
          ? std::format("{}/{}", overview.league_position, overview.league_size)
          : std::string("–");
  tiles.next();
  UI::statTile("u21_position", LOC("U21_TILE_POSITION"), position.c_str(),
               league_title.c_str(), palette.text, tile);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));

  if (ImGui::BeginTabBar("ReserveTabs"))
  {
    const auto tab = [this](Tab which, const char* label, auto&& body)
    {
      const ImGuiTabItemFlags flags = tab_pending && requested_tab == which
                                          ? ImGuiTabItemFlags_SetSelected
                                          : ImGuiTabItemFlags_None;
      if (ImGui::BeginTabItem(label, nullptr, flags))
      {
        body();
        ImGui::EndTabItem();
      }
    };
    tab(Tab::SQUAD, LOC("U21_TAB_SQUAD"), [this] { renderSquad(); });
    tab(Tab::LEAGUE, LOC("U21_TAB_LEAGUE"), [this] { renderLeague(); });
    tab(Tab::CANDIDATES, LOC("U21_TAB_CANDIDATES"),
        [this] { renderCandidates(); });
    tab_pending = false;
    ImGui::EndTabBar();
  }
  runPending();
}

void ReservesScene::openTab(Tab tab)
{
  requested_tab = tab;
  tab_pending = true;
  selected = 0;
}

void ReservesScene::renderSquad()
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("u21_squad_card", squad_title.c_str());
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("U21_SQUAD_HINT"));
  ImGui::PopTextWrapPos();
  if (squad_rows.empty())
    UI::emptyState(LOC("U21_SQUAD_EMPTY_TITLE"), LOC("U21_SQUAD_EMPTY_BODY"));
  else
    renderTable("u21_squad", squad_rows, ListKind::SQUAD);
  UI::endCard();
}

void ReservesScene::renderCandidates()
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("u21_candidates_card", LOC("U21_CARD_CANDIDATES"));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("U21_CANDIDATES_HINT"));
  ImGui::PopTextWrapPos();
  if (candidate_rows.empty())
    UI::emptyState(LOC("U21_CANDIDATES_EMPTY_TITLE"),
                   LOC("U21_CANDIDATES_EMPTY_BODY"));
  else
    renderTable("u21_candidates", candidate_rows, ListKind::CANDIDATES);
  UI::endCard();
}

void ReservesScene::renderLeague()
{
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool twoColumns = available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
  const float right =
      twoColumns ? std::floor((available - gap) * 0.38f) : available;
  const float left = twoColumns ? available - gap - right : available;

  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("u21_table_card", league_title.c_str(), left);
  if (table.empty())
  {
    UI::emptyState(LOC("U21_TABLE_EMPTY_TITLE"), LOC("U21_TABLE_EMPTY_BODY"));
  }
  else
  {
    std::array<UI::Column, TABLE_COLUMNS.size()> localized{};
    for (std::size_t i = 0; i < TABLE_COLUMNS.size(); ++i)
    {
      localized[i] = TABLE_COLUMNS[i];
      localized[i].label = LOC(TABLE_COLUMNS[i].label);
    }
    const std::span<const UI::Column> columns(localized);
    const UI::ColumnMask mask =
        UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
    if (UI::beginResponsiveTable(
            "u21_table", columns, mask,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
    {
      for (const TableLine& line : table)
      {
        ImGui::TableNextRow();
        const ImVec4 color = line.own ? palette.text : palette.muted;
        if (UI::cell(mask, 0)) ImGui::TextColored(color, "%d", line.position);
        if (UI::cell(mask, 1))
          UI::textFitted(line.club, ImGui::GetContentRegionAvail().x, color);
        if (UI::cell(mask, 2))
          ImGui::TextColored(color, "%s", line.record.c_str());
        if (UI::cell(mask, 3))
          ImGui::TextColored(color, "%s", line.goals.c_str());
        if (UI::cell(mask, 4)) ImGui::TextColored(color, "%d", line.points);
      }
      ImGui::EndTable();
    }
  }
  UI::endCard();
  if (twoColumns) ImGui::SameLine();
  ImGui::BeginGroup();
  renderResultsCard(right);
  renderQuotaCard(right);
  ImGui::EndGroup();
}

void ReservesScene::renderResultsCard(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("u21_results_card", LOC("U21_CARD_RESULTS"), width);
  const float keyWidth = KEY_WIDTH * Theme::scale();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(league_line.c_str());
  ImGui::PopTextWrapPos();
  if (!form.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
    UI::formStrip(form);
    for (auto it = results.rbegin(); it != results.rend(); ++it)
    {
      ImGui::TextColored(palette.faint, "%s", it->date.c_str());
      ImGui::SameLine(keyWidth * 0.45f);
      ImGui::TextUnformatted(it->score.c_str());
      ImGui::SameLine();
      UI::textFitted(it->opponent, ImGui::GetContentRegionAvail().x,
                     palette.muted);
    }
  }
  UI::endCard();
}

void ReservesScene::renderQuotaCard(float width) const
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("u21_rules_card", LOC("U21_CARD_RULES"), width);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.muted, "%s", LOC("U21_RULES_BODY"));
  ImGui::PopTextWrapPos();
  UI::endCard();
}

void ReservesScene::renderTable(const char* id, const std::vector<Row>& rows,
                                ListKind kind)
{
  const Theme::Palette& palette = Theme::palette();
  const bool squad = kind == ListKind::SQUAD;
  std::array<UI::Column, SQUAD_COLUMNS.size()> localized{};
  std::array<Cell, SQUAD_COLUMNS.size()> cells{};
  const std::size_t count =
      squad ? SQUAD_COLUMNS.size() : CANDIDATE_COLUMNS.size();
  for (std::size_t i = 0; i < count; ++i)
  {
    const ColumnSpec& spec = squad ? SQUAD_COLUMNS[i] : CANDIDATE_COLUMNS[i];
    localized[i] = spec.column;
    localized[i].label = LOC(spec.column.label);
    cells[i] = spec.cell;
  }
  const std::span<const UI::Column> columns(localized.data(), count);
  const UI::ColumnMask mask =
      UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
  const ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH;

  ImGui::PushID(id);
  // The selected row opens a detail strip right under it: the table is split
  // there and continues with the same id (shared column widths).
  UI::TableHeader header = UI::TableHeader::STATIC;
  std::size_t first = 0;
  while (first < rows.size())
  {
    if (!UI::beginResponsiveTable("rows", columns, mask, flags, header)) break;
    header = UI::TableHeader::NONE;
    std::size_t index = first;
    const Row* opened = nullptr;
    while (index < rows.size() && opened == nullptr)
    {
      const Row& row = rows[index++];
      const GameController::YouthPlayerView& view = row.view;
      ImGui::PushID(static_cast<int>(view.id));
      ImGui::TableNextRow(ImGuiTableRowFlags_None,
                          UI::buttonHeight(UI::ButtonSize::COMPACT));
      const bool isSelected = selected == view.id;
      for (std::size_t column = 0; column < count; ++column)
      {
        if (!UI::cell(mask, static_cast<int>(column))) continue;
        switch (cells[column])
        {
          case Cell::NAME:
          {
            const float x = ImGui::GetCursorPosX();
            if (ImGui::Selectable("##row", isSelected,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowOverlap))
              selected = isSelected ? 0 : view.id;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
              ImGui::SetTooltip("%s", LOC("YOUTH_ROW_HINT"));
            ImGui::SameLine();
            ImGui::SetCursorPosX(x);
            if (UI::link(view.name.c_str(), "name"))
              Navigation::openPlayer(guiView, view.id);
            break;
          }
          case Cell::POS:
            ImGui::TextColored(palette.muted, "%s", row.role.c_str());
            break;
          case Cell::AGE:
            if (row.overage)
            {
              ImGui::TextColored(palette.warning, "%d", view.age);
              if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", LOC("U21_OVERAGE_HINT"));
            }
            else
            {
              ImGui::Text("%d", view.age);
            }
            break;
          case Cell::ABILITY:
            ImGui::TextUnformatted(row.ability.c_str());
            break;
          case Cell::POTENTIAL:
            ImGui::TextColored(
                Theme::ratingColor(0.5f * (view.estimate.potential_low +
                                           view.estimate.potential_high)),
                "%s", row.potential.c_str());
            break;
          case Cell::CONTRACT:
            ImGui::TextColored(
                view.contract_years <= 1 ? palette.warning : palette.text, "%s",
                row.contract.c_str());
            break;
          case Cell::APPS:
            ImGui::Text("%d", view.appearances);
            break;
          case Cell::RATING:
            ImGui::TextUnformatted(row.rating.c_str());
            break;
          case Cell::ORIGIN:
            UI::textFitted(row.origin, ImGui::GetContentRegionAvail().x,
                           palette.muted);
            break;
        }
      }
      ImGui::PopID();
      if (selected == view.id) opened = &row;
    }
    ImGui::EndTable();
    if (opened != nullptr) renderDetail(*opened, kind);
    first = index;
  }
  ImGui::PopID();
}

void ReservesScene::renderDetail(const Row& row, ListKind kind)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const GameController::YouthPlayerView& view = row.view;
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->ChannelsSplit(2);
  drawList->ChannelsSetCurrent(1);
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const float pad = Theme::Space::M * scale;
  ImGui::SetCursorScreenPos(ImVec2(start.x + pad, start.y + pad));
  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 2.0f * pad);
  ImGui::TextUnformatted(row.terms.c_str());
  if (row.overage)
    ImGui::TextColored(palette.warning, "%s", LOC("U21_OVERAGE_HINT"));
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
  ImGui::PushID(static_cast<int>(view.id));
  const auto button = [](const char* label, bool primary)
  {
    UI::sameLineIfFits(UI::buttonWidth(label));
    return primary ? UI::primaryButton(label) : UI::secondaryButton(label);
  };
  if (UI::secondaryButton(LOC("YOUTH_VIEW_PROFILE")))
    Navigation::openPlayer(guiView, view.id);
  if (kind == ListKind::SQUAD)
  {
    if (button(LOC("U21_PROMOTE"), true))
      pending = {PendingAction::Kind::PROMOTE, view.id};
    if (view.contract == YouthContract::Scholarship &&
        button(LOC("U21_OFFER_PROFESSIONAL"), false))
      pending = {PendingAction::Kind::PROFESSIONAL, view.id};
    if (view.age <= YouthModel::U18_MAX_AGE && button(LOC("U21_TO_U18"), false))
      pending = {PendingAction::Kind::TO_U18, view.id};
  }
  else if (button(LOC("U21_SEND_DOWN"), true))
  {
    pending = {PendingAction::Kind::TO_U21, view.id};
  }
  ImGui::PopID();
  ImGui::EndGroup();
  const float bottom = ImGui::GetItemRectMax().y + pad;
  drawList->ChannelsSetCurrent(0);
  drawList->AddRectFilled(start, ImVec2(start.x + width, bottom),
                          Theme::toU32(palette.raised), 4.0f * scale);
  drawList->AddRectFilled(start, ImVec2(start.x + 3.0f * scale, bottom),
                          Theme::toU32(palette.accent), 2.0f * scale);
  drawList->ChannelsMerge();
  ImGui::SetCursorScreenPos(ImVec2(start.x, bottom));
  ImGui::Dummy(ImVec2(width, Theme::Space::XS * scale));
}

void ReservesScene::runPending()
{
  if (pending.kind == PendingAction::Kind::NONE) return;
  GameController& controller = guiView->getController();
  const PendingAction action = pending;
  pending = {};
  std::string name;
  if (const auto data = controller.getGameData())
    if (const auto player = data->getPlayer(action.id))
      name = player->get().getName();
  const auto report = [&](YouthActionResult result, const char* success_key)
  {
    if (result == YouthActionResult::Ok)
      showToast(formatLocalized(success_key, {name}));
    else
      showToast(LOC(YouthModel::actionResultKey(result)), true);
  };
  switch (action.kind)
  {
    case PendingAction::Kind::PROMOTE:
      report(controller.promoteReservePlayer(action.id), "U21_TOAST_PROMOTED");
      selected = 0;
      break;
    case PendingAction::Kind::TO_U18:
      report(controller.moveReserveToU18(action.id), "U21_TOAST_TO_U18");
      selected = 0;
      break;
    case PendingAction::Kind::TO_U21:
      report(controller.moveToReserves(action.id), "U21_TOAST_SENT_DOWN");
      selected = 0;
      break;
    case PendingAction::Kind::PROFESSIONAL:
      report(controller.offerYouthProfessionalContract(action.id),
             "YOUTH_TOAST_PROFESSIONAL");
      break;
    case PendingAction::Kind::NONE:
      break;
  }
  refresh();
}

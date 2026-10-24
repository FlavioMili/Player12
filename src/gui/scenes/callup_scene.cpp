// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/callup_scene.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <span>

#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/inbox.h"
#include "model/national_teams.h"
#include "model/role_utils.h"

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 1100.0f;
/** Candidates listed under the squad (the assistant's best first). */
constexpr std::size_t CANDIDATE_ROWS = 60;

enum class Cell : uint8_t
{
  NAME,
  POS,
  AGE,
  CLUB,
  ABILITY,
  FORM,
  CONDITION,
  CAPS
};

struct ColumnSpec
{
  Cell cell;
  UI::Column column;
};

constexpr std::array<ColumnSpec, 8> COLUMNS = {{
    {Cell::NAME, {"YOUTH_COL_NAME", 0.0f, 0}},
    {Cell::POS, {"YOUTH_COL_POS", 52.0f, 0}},
    {Cell::AGE, {"YOUTH_COL_AGE", 44.0f, 2}},
    {Cell::CLUB, {"CALLUP_COL_CLUB", 150.0f, 3}},
    {Cell::ABILITY, {"CALLUP_COL_ABILITY", 64.0f, 0}},
    {Cell::FORM, {"CALLUP_COL_FORM", 56.0f, 1}},
    {Cell::CONDITION, {"CALLUP_COL_CONDITION", 72.0f, 4}},
    {Cell::CAPS, {"CALLUP_COL_CAPS", 72.0f, 2}},
}};

int roleGroup(PlayerRole role)
{
  using enum PlayerRole;
  switch (role)
  {
    case GK:
      return 0;
    case CB:
    case LB:
    case RB:
      return 1;
    case CDM:
    case CM:
    case CAM:
    case LM:
    case RM:
      return 2;
    default:
      return 3;
  }
}

std::string nationName(Language nation)
{
  return LOC(International::teamNameKey(nation).c_str());
}
}  // namespace

CallUpScene::CallUpScene(GUIView* parent) : ManagementScene(parent) {}

void CallUpScene::update(float /*deltaTime*/)
{
  // Call-up lists change on scene actions and fixture dates, not per frame;
  // refresh() rebuilds the cached rows when those changes occur.
}

void CallUpScene::refresh()
{
  const GameController& controller = guiView->getController();
  cached_date = controller.getCurrentDate();
  view = controller.getCallUpView();
  rows.clear();
  fixtures.clear();
  working.clear();
  dirty = false;
  selected = 0;
  if (!controller.hasNationalJob()) return;
  rows.reserve(view.candidates.size());
  for (const GameController::CallUpCandidate& candidate : view.candidates)
  {
    Row row;
    row.candidate = candidate;
    row.role = RoleUtils::shortName(candidate.role);
    const auto club = controller.getTeamById(candidate.club);
    row.club = club ? club->get().getName() : std::string();
    row.form = candidate.form > 0.0f ? std::format("{:.1f}", candidate.form)
                                     : std::string("–");
    row.caps = std::format("{} ({})", candidate.caps, candidate.goals);
    row.group = roleGroup(candidate.role);
    if (candidate.selected) working.push_back(candidate.id);
    rows.push_back(std::move(row));
  }
  subtitle = formatLocalized("CALLUP_SUBTITLE", {nationName(view.nation)});
  if (view.announced && view.locked)
  {
    status_value = LOC("CALLUP_STATUS_AWAY");
    status_note =
        formatLocalized("CALLUP_STATUS_AWAY_NOTE", {Format::date(view.until)});
  }
  else if (view.announced)
  {
    status_value = LOC("CALLUP_STATUS_OPEN");
    status_note =
        formatLocalized("CALLUP_STATUS_OPEN_NOTE", {Format::date(view.start)});
  }
  else
  {
    status_value = LOC("CALLUP_STATUS_WAITING");
    status_note = view.next_announcement == GameDateValue()
                      ? std::string()
                      : formatLocalized("CALLUP_STATUS_WAITING_NOTE",
                                        {Format::date(view.next_announcement)});
  }
  for (const International::Fixture& fixture : view.fixtures)
  {
    FixtureLine line;
    line.date = Format::dayMonth(fixture.date);
    const bool home = fixture.home == view.nation;
    line.opponent = std::format(
        "{} ({})", nationName(home ? fixture.away : fixture.home),
        LOC(fixture.neutral ? "CALLUP_NEUTRAL"
                            : (home ? "YOUTH_HOME" : "YOUTH_AWAY")));
    line.competition = LOC(International::competitionKey(fixture.competition));
    fixtures.push_back(std::move(line));
  }
  rebuildLists();
}

void CallUpScene::rebuildLists()
{
  squad_rows.clear();
  candidate_rows.clear();
  keepers = 0;
  for (const Row& row : rows)
  {
    if (std::ranges::contains(working, row.candidate.id))
    {
      squad_rows.push_back(&row);
      if (row.group == 0) ++keepers;
    }
    else if (candidate_rows.size() < CANDIDATE_ROWS &&
             (filter == 0 || row.group == filter - 1))
    {
      candidate_rows.push_back(&row);
    }
  }
  std::ranges::stable_sort(squad_rows, {}, &Row::group);
  squad_value = std::format("{}/{}", working.size(), view.limit);
  keepers_value = std::to_string(keepers);
  squad_title =
      formatLocalized(dirty ? "CALLUP_CARD_SQUAD_EDITED" : "CALLUP_CARD_SQUAD",
                      {std::to_string(working.size())});
}

void CallUpScene::renderContent()
{
  const GameController& controller = guiView->getController();
  if (!(cached_date == controller.getCurrentDate())) refresh();
  if (!controller.hasNationalJob())
  {
    UI::pageHeader(LOC("CALLUP_TITLE"));
    UI::beginAutoHeightCard("callup_none", LOC("CALLUP_TITLE"));
    UI::emptyState(LOC("CALLUP_NO_JOB_TITLE"), LOC("CALLUP_NO_JOB_BODY"));
    UI::endCard();
    return;
  }
  UI::pageHeader(LOC("CALLUP_TITLE"), subtitle.c_str());
  renderTiles();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));

  // One scroll surface: cards grow with their content and the page scrolls.
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  if (const bool twoColumns =
          available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
      twoColumns)
  {
    const float right = std::floor((available - gap) * 0.3f);
    const float left = available - gap - right;
    ImGui::BeginGroup();
    renderSquad(left);
    renderCandidates(left);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    renderFixtures(right);
    ImGui::EndGroup();
  }
  else
  {
    renderSquad(available);
    renderFixtures(available);
    renderCandidates(available);
  }
  if (lists_stale)
  {
    lists_stale = false;
    rebuildLists();
  }
  if (confirm_requested)
  {
    confirm_requested = false;
    confirm();
  }
}

void CallUpScene::renderTiles() const
{
  const Theme::Palette& palette = Theme::palette();
  UI::TileRow tiles(4);
  const float tile = tiles.width();
  tiles.next();
  UI::statTile("callup_status", LOC("CALLUP_TILE_STATUS"), status_value.c_str(),
               status_note.c_str(), palette.text, tile);
  tiles.next();
  UI::statTile(
      "callup_squad", LOC("CALLUP_TILE_SQUAD"), squad_value.c_str(),
      LOC(view.finals ? "CALLUP_TILE_SQUAD_FINALS" : "CALLUP_TILE_SQUAD_NOTE"),
      working.size() > view.limit ||
              (view.announced && working.size() < International::MIN_CALL_UPS)
          ? palette.warning
          : palette.text,
      tile);
  tiles.next();
  UI::statTile(
      "callup_keepers", LOC("CALLUP_TILE_KEEPERS"), keepers_value.c_str(),
      LOC("CALLUP_TILE_KEEPERS_NOTE"),
      view.announced && keepers < International::MIN_CALL_UP_GOALKEEPERS
          ? palette.warning
          : palette.text,
      tile);
  const NationalJob* job = guiView->getController().getNationalJob();
  const std::string record =
      job ? std::format("{}-{}-{}", job->won, job->drawn, job->lost)
          : std::string("–");
  tiles.next();
  UI::statTile("callup_record", LOC("CALLUP_TILE_RECORD"), record.c_str(),
               LOC("CALLUP_TILE_RECORD_NOTE"), palette.text, tile);
}

void CallUpScene::renderSquad(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("callup_squad_card", squad_title.c_str(), width);
  if (!view.announced)
  {
    UI::emptyState(LOC("CALLUP_WAITING_TITLE"), status_note.c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.faint, "%s", LOC("CALLUP_WAITING_HINT"));
    ImGui::PopTextWrapPos();
    UI::endCard();
    return;
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(
      palette.faint, "%s",
      LOC(view.locked ? "CALLUP_LOCKED_HINT" : "CALLUP_OPEN_HINT"));
  ImGui::PopTextWrapPos();
  if (!view.locked)
  {
    const bool editable = dirty;
    ImGui::BeginDisabled(!editable);
    if (UI::primaryButton(LOC("CALLUP_CONFIRM"))) confirm_requested = true;
    UI::sameLineIfFits(UI::buttonWidth(LOC("CALLUP_UNDO")));
    if (UI::secondaryButton(LOC("CALLUP_UNDO"))) refresh();
    ImGui::EndDisabled();
  }
  renderTable("callup_squad", squad_rows, true);
  UI::endCard();
}

void CallUpScene::renderCandidates(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("callup_candidates_card", LOC("CALLUP_CARD_POOL"),
                          width);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("CALLUP_POOL_HINT"));
  ImGui::PopTextWrapPos();
  if (const std::array<const char*, 5> labels = {LOC("CALLUP_FILTER_ALL"),
                                                 LOC("CALLUP_FILTER_GK"),
                                                 LOC("CALLUP_FILTER_DEF"),
                                                 LOC("CALLUP_FILTER_MID"),
                                                 LOC("CALLUP_FILTER_FWD")};
      UI::segmented("callup_filter", filter, labels))
    lists_stale = true;
  if (candidate_rows.empty())
    UI::emptyState(LOC("CALLUP_POOL_EMPTY_TITLE"),
                   LOC("CALLUP_POOL_EMPTY_BODY"));
  else
    renderTable("callup_pool", candidate_rows, false);
  UI::endCard();
}

void CallUpScene::renderFixtures(float width) const
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("callup_fixtures_card", LOC("CALLUP_CARD_FIXTURES"),
                          width);
  if (fixtures.empty())
  {
    ImGui::TextColored(palette.muted, "%s", LOC("CALLUP_FIXTURES_EMPTY"));
  }
  for (const FixtureLine& line : fixtures)
  {
    ImGui::TextColored(palette.faint, "%s", line.date.c_str());
    ImGui::SameLine();
    UI::textFitted(line.opponent, ImGui::GetContentRegionAvail().x,
                   palette.text);
    UI::textFitted(line.competition, ImGui::GetContentRegionAvail().x,
                   palette.muted);
  }
  UI::endCard();
}

void CallUpScene::renderTable(const char* id,
                              const std::vector<const Row*>& list, bool squad)
{
  const Theme::Palette& palette = Theme::palette();
  std::array<UI::Column, COLUMNS.size()> localized{};
  for (std::size_t i = 0; i < COLUMNS.size(); ++i)
  {
    localized[i] = COLUMNS[i].column;
    localized[i].label = LOC(COLUMNS[i].column.label);
  }
  const std::span<const UI::Column> columns(localized);
  const UI::ColumnMask mask =
      UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
  const ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH;
  ImGui::PushID(id);
  UI::TableHeader header = UI::TableHeader::STATIC;
  std::size_t first = 0;
  while (first < list.size())
  {
    if (!UI::beginResponsiveTable("rows", columns, mask, flags, header)) break;
    header = UI::TableHeader::NONE;
    std::size_t index = first;
    const Row* opened = nullptr;
    while (index < list.size() && opened == nullptr)
    {
      const Row& row = *list[index++];
      const GameController::CallUpCandidate& candidate = row.candidate;
      ImGui::PushID(static_cast<int>(candidate.id));
      ImGui::TableNextRow(ImGuiTableRowFlags_None,
                          UI::buttonHeight(UI::ButtonSize::COMPACT));
      const bool isSelected = selected == candidate.id;
      const ImVec4 text = candidate.available ? palette.text : palette.faint;
      for (std::size_t column = 0; column < COLUMNS.size(); ++column)
      {
        if (!UI::cell(mask, static_cast<int>(column))) continue;
        using enum Cell;
        switch (COLUMNS[column].cell)
        {
          case NAME:
          {
            const float x = ImGui::GetCursorPosX();
            if (ImGui::Selectable("##row", isSelected,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowOverlap))
              selected = isSelected ? 0 : candidate.id;
            ImGui::SameLine();
            ImGui::SetCursorPosX(x);
            if (UI::link(candidate.name.c_str(), "name"))
              Navigation::openPlayer(guiView, candidate.id);
            if (!candidate.available)
            {
              ImGui::SameLine();
              UI::badge(LOC("CALLUP_INJURED"), palette.negative);
            }
            break;
          }
          case POS:
            ImGui::TextColored(palette.muted, "%s", row.role.c_str());
            break;
          case AGE:
            ImGui::TextColored(text, "%d", candidate.age);
            break;
          case CLUB:
            UI::textFitted(row.club, ImGui::GetContentRegionAvail().x,
                           palette.muted);
            break;
          case ABILITY:
            ImGui::TextColored(Theme::ratingColor(candidate.overall), "%d",
                               candidate.overall);
            break;
          case FORM:
            ImGui::TextColored(text, "%s", row.form.c_str());
            break;
          case CONDITION:
            ImGui::TextColored(
                candidate.condition < 75 ? palette.warning : text, "%d%%",
                candidate.condition);
            break;
          case CAPS:
            ImGui::TextColored(text, "%s", row.caps.c_str());
            break;
        }
      }
      ImGui::PopID();
      if (isSelected) opened = &row;
    }
    ImGui::EndTable();
    if (opened != nullptr) renderDetail(*opened, squad);
    first = index;
  }
  ImGui::PopID();
}

void CallUpScene::renderDetail(const Row& row, bool squad)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const GameController::CallUpCandidate& candidate = row.candidate;
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->ChannelsSplit(2);
  drawList->ChannelsSetCurrent(1);
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const float pad = Theme::Space::M * scale;
  ImGui::SetCursorScreenPos(ImVec2(start.x + pad, start.y + pad));
  ImGui::BeginGroup();
  ImGui::PushID(static_cast<int>(candidate.id));
  if (UI::secondaryButton(LOC("YOUTH_VIEW_PROFILE")))
    Navigation::openPlayer(guiView, candidate.id);
  if (const bool editable = view.announced && !view.locked; editable && squad)
  {
    UI::sameLineIfFits(UI::buttonWidth(LOC("CALLUP_DROP")));
    if (UI::secondaryButton(LOC("CALLUP_DROP")))
    {
      std::erase(working, candidate.id);
      dirty = true;
      lists_stale = true;
      selected = 0;
    }
  }
  else if (editable)
  {
    const bool room = working.size() < view.limit;
    ImGui::BeginDisabled(!room || !candidate.available);
    UI::sameLineIfFits(UI::buttonWidth(LOC("CALLUP_ADD")));
    if (UI::primaryButton(LOC("CALLUP_ADD")))
    {
      working.push_back(candidate.id);
      dirty = true;
      lists_stale = true;
      selected = 0;
    }
    ImGui::EndDisabled();
    if (!room)
    {
      ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 2.0f * pad);
      ImGui::TextColored(palette.faint, "%s", LOC("CALLUP_FULL_HINT"));
      ImGui::PopTextWrapPos();
    }
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

void CallUpScene::confirm()
{
  GameController& controller = guiView->getController();
  const International::CallUpResult result =
      controller.setNationalSquad(working);
  if (result == International::CallUpResult::Ok)
  {
    showToast(LOC("CALLUP_RESULT_OK"));
    refresh();
  }
  else
  {
    showToast(LOC(International::callUpResultKey(result)), true);
  }
}

// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/timeline_scene.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <format>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/game.h"
#include "model/inbox.h"
#include "model/records.h"

namespace
{
constexpr float DATE_WIDTH = 110.0f;
constexpr float KIND_WIDTH = 150.0f;
constexpr float COMBO_WIDTH = 240.0f;
/** Below this width date and kind go above the text. */
constexpr float SIDE_COLUMNS_MIN_WIDTH = 720.0f;
constexpr std::array<const char*, 12> MONTH_KEYS = {
    "MONTH_JAN", "MONTH_FEB", "MONTH_MAR", "MONTH_APR",
    "MONTH_MAY", "MONTH_JUN", "MONTH_JUL", "MONTH_AUG",
    "MONTH_SEP", "MONTH_OCT", "MONTH_NOV", "MONTH_DEC"};

std::string seasonLabel(std::uint16_t year)
{
  return std::format("{}/{:02}", year, (year + 1U) % 100U);
}

}  // namespace

TimelineScene::TimelineScene(GUIView* parent) : ManagementScene(parent) {}

void TimelineScene::refresh()
{
  const GameController& controller = guiView->getController();
  entries.clear();
  rows.clear();
  clubs.clear();
  groups.clear();
  const Game* game = controller.getGame();
  const auto gamedata = controller.getGameData();
  if (game == nullptr || !gamedata) return;
  std::vector<RecordEntry> records;
  const TimelineSources sources =
      CareerTimeline::sourcesFor(*game, *gamedata, records);
  entries = CareerTimeline::build(sources);

  rows.reserve(entries.size());
  for (const TimelineEntry& entry : entries)
  {
    Row row;
    row.date = Format::date(entry.date);
    row.kind = LOC(timelineKindKey(entry.kind));
    row.text = entry.text();
    if (entry.player_id != 0 && sources.gamedata != nullptr)
      if (const auto player = sources.gamedata->getPlayer(entry.player_id))
        row.player = player->get().getName();
    if (const auto team = controller.getTeamById(entry.team_id))
      row.club = team->get().getName();
    rows.push_back(std::move(row));
  }

  int played = 0;
  int won = 0;
  int drawn = 0;
  int lost = 0;
  int trophies = 0;
  for (const ManagerStint& stint : sources.stints)
  {
    played += stint.played;
    won += stint.won;
    drawn += stint.drawn;
    lost += stint.lost;
    trophies += stint.trophies;
    if (!std::ranges::contains(clubs, stint.team_id,
                               &std::pair<TeamID, std::string>::first))
      clubs.emplace_back(stint.team_id, stint.club_name);
  }
  if (club_filter != 0 &&
      !std::ranges::contains(clubs, club_filter,
                             &std::pair<TeamID, std::string>::first))
    club_filter = 0;
  clubs_value = std::to_string(clubs.size());
  matches_value = std::to_string(played);
  matches_note = formatLocalized(
      "TIMELINE_RECORD_LINE",
      {std::to_string(won), std::to_string(drawn), std::to_string(lost)});
  win_rate_value = played == 0
                       ? std::string("-")
                       : std::format("{}%", (won * 100 + played / 2) / played);
  trophies_value = std::to_string(trophies);
  regroup();
}

void TimelineScene::setZoom(Zoom value)
{
  zoom = value;
  zoom_index = value == Zoom::SEASON ? 0 : 1;
  regroup();
}

void TimelineScene::regroup()
{
  groups.clear();
  club_label = LOC("TIMELINE_ALL_CLUBS");
  for (const auto& [id, name] : clubs)
    if (id == club_filter) club_label = name;
  // Newest first: the latest season (or month) leads the page.
  for (std::size_t index = entries.size(); index-- > 0;)
  {
    const TimelineEntry& entry = entries[index];
    if (club_filter != 0 && entry.team_id != club_filter) continue;
    if (std::string label =
            zoom == Zoom::SEASON
                ? formatLocalized("TIMELINE_SEASON_HEADER",
                                  {seasonLabel(entry.season_year)})
                : std::format(
                      "{} {}",
                      LOC(MONTH_KEYS[static_cast<std::size_t>(
                          std::clamp<int>(entry.date.month, 1, 12) - 1)]),
                      entry.date.year);
        groups.empty() || groups.back().label != label)
      groups.push_back({std::move(label), {}});
    groups.back().entries.push_back(index);
  }
}

std::optional<std::filesystem::path> TimelineScene::exportJournal()
{
  const GameController& controller = guiView->getController();
  const Game* game = controller.getGame();
  const auto gamedata = controller.getGameData();
  if (game == nullptr || !gamedata) return std::nullopt;
  std::vector<RecordEntry> records;
  const TimelineSources sources =
      CareerTimeline::sourcesFor(*game, *gamedata, records);
  const GameDateValue today = controller.getCurrentDate();
  const std::string markdown =
      CareerTimeline::toMarkdown(sources, entries, today);
  return CareerTimeline::writeJournal(
      markdown, RuntimePaths::root() / "journals",
      CareerTimeline::journalFileName(sources.manager_name, today));
}

void TimelineScene::renderSummary() const
{
  const Theme::Palette& palette = Theme::palette();
  UI::TileRow tiles(4);
  const float width = tiles.width();
  tiles.next();
  UI::statTile("clubs", LOC("TIMELINE_TILE_CLUBS"), clubs_value.c_str(),
               nullptr, palette.text, width);
  tiles.next();
  UI::statTile("matches", LOC("TIMELINE_TILE_MATCHES"), matches_value.c_str(),
               matches_note.c_str(), palette.text, width);
  tiles.next();
  UI::statTile("win_rate", LOC("TIMELINE_TILE_WIN_RATE"),
               win_rate_value.c_str(), nullptr, palette.text, width);
  tiles.next();
  UI::statTile("trophies", LOC("TIMELINE_TILE_TROPHIES"),
               trophies_value.c_str(), nullptr, palette.text, width);
}

void TimelineScene::renderControls()
{
  const float scale = Theme::scale();
  if (const std::array<const char*, 2> zooms = {LOC("TIMELINE_BY_SEASON"),
                                                LOC("TIMELINE_BY_MONTH")};
      UI::segmented("##timeline_zoom", zoom_index, zooms, 260.0f * scale))
    setZoom(zoom_index == 0 ? Zoom::SEASON : Zoom::MONTH);
  const float comboWidth =
      std::min(COMBO_WIDTH * scale, ImGui::GetContentRegionAvail().x);
  UI::sameLineIfFits(comboWidth, Theme::Space::L * scale);
  ImGui::SetNextItemWidth(comboWidth);
  if (ImGui::BeginCombo("##timeline_club", club_label.c_str()))
  {
    if (ImGui::Selectable(LOC("TIMELINE_ALL_CLUBS"), club_filter == 0))
    {
      club_filter = 0;
      regroup();
    }
    for (const auto& [id, name] : clubs)
    {
      ImGui::PushID(static_cast<int>(id));
      if (ImGui::Selectable(name.c_str(), club_filter == id))
      {
        club_filter = id;
        regroup();
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  const char* exportLabel = LOC("TIMELINE_EXPORT");
  UI::sameLineIfFits(UI::buttonWidth(exportLabel), Theme::Space::L * scale);
  if (UI::secondaryButton(exportLabel))
  {
    if (const auto path = exportJournal())
      showToast(formatLocalized("TIMELINE_EXPORTED", {path->string()}));
    else
      showToast(LOC("TIMELINE_EXPORT_FAILED"), true);
  }
}

void TimelineScene::renderEntry(std::size_t index)
{
  const TimelineEntry& entry = entries[index];
  const Row& row = rows[index];
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const bool side =
      ImGui::GetContentRegionAvail().x >= SIDE_COLUMNS_MIN_WIDTH * scale;
  ImGui::PushID(static_cast<int>(index));
  const float startX = ImGui::GetCursorPosX();
  if (side)
  {
    {
      Theme::ScopedText small(Theme::Text::SMALL);
      ImGui::TextColored(palette.muted, "%s", row.date.c_str());
    }
    ImGui::SameLine(startX + DATE_WIDTH * scale);
    UI::badge(row.kind.c_str(), palette.muted);
    ImGui::SameLine(startX + (DATE_WIDTH + KIND_WIDTH) * scale);
  }
  ImGui::BeginGroup();
  if (!side)
  {
    {
      Theme::ScopedText small(Theme::Text::SMALL);
      ImGui::TextColored(palette.muted, "%s", row.date.c_str());
    }
    ImGui::SameLine();
    UI::badge(row.kind.c_str(), palette.muted);
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(row.text.c_str());
  ImGui::PopTextWrapPos();
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    bool first = true;
    if (!row.player.empty())
    {
      if (UI::link(row.player.c_str(), "##player"))
        Navigation::openPlayer(guiView, entry.player_id);
      first = false;
    }
    if (!row.club.empty())
    {
      if (!first)
        UI::sameLineIfFits(ImGui::CalcTextSize(row.club.c_str()).x,
                           Theme::Space::L * scale);
      if (UI::link(row.club.c_str(), "##club"))
        Navigation::openClub(guiView, entry.team_id);
    }
  }
  ImGui::EndGroup();
  ImGui::PopID();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
}

void TimelineScene::renderContent()
{
  UI::pageHeader(LOC("TIMELINE_TITLE"), LOC("TIMELINE_SUBTITLE"));
  if (entries.empty())
  {
    UI::emptyState(LOC("TIMELINE_EMPTY_TITLE"), LOC("TIMELINE_EMPTY_BODY"));
    return;
  }
  renderSummary();
  renderControls();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  for (std::size_t group = 0; group < groups.size(); ++group)
  {
    ImGui::PushID(static_cast<int>(group));
    UI::beginAutoHeightCard("timeline_group", groups[group].label.c_str());
    for (const std::size_t index : groups[group].entries) renderEntry(index);
    UI::endCard();
    ImGui::PopID();
  }
}

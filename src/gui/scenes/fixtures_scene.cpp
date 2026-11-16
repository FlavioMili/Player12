// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/fixtures_scene.h"

#include <fmt/printf.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>

#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/view_models/competition_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"

namespace
{
constexpr float BADGE_HEIGHT = 16.0f;
// Club fixture columns (unscaled widths); priority 0 never hides.
const std::array<UI::Column, 6>& clubFixtureColumns()
{
  static const std::array<UI::Column, 6> columns = {{
      {"FIXTURES_COL_DATE", 140.0f, 1},
      {"FIXTURES_COL_COMPETITION", 170.0f, 2},
      {"FIXTURES_COL_VENUE", 70.0f, 3},
      {"FIXTURES_COL_OPPONENT", 0.0f, 0},
      {"FIXTURES_COL_RESULT", 76.0f, 0},
      {"FIXTURES_COL_OUTCOME", 60.0f, 3},
  }};
  return columns;
}

ImVec4 matchTypeColor(MatchType type)
{
  using enum MatchType;
  const Theme::Palette& palette = Theme::palette();
  switch (type)
  {
    case LEAGUE:
      return palette.info;
    case FRIENDLY:
      return palette.muted;
    case CUP:
    case CONTINENTAL:
      return palette.warning;
  }
  return palette.info;
}

}  // namespace

FixturesScene::FixturesScene(GUIView* parent) : ManagementScene(parent) {}

void FixturesScene::update(float /*deltaTime*/)
{
  // Fixture data is event-driven and cached by refresh(); this page has no
  // time-dependent state to update each frame.
}

void FixturesScene::refresh()
{
  GameController& controller = guiView->getController();
  const auto managed = controller.getManagedTeam();
  club_fixtures.clear();
  league_fixtures.clear();
  wins = 0;
  draws = 0;
  losses = 0;
  goals_for = 0;
  goals_against = 0;
  next_fixture_index = -1;
  if (!managed) return;
  club_id = managed->get().getId();
  club_fixtures = CompetitionView::buildClubFixtures(controller, club_id);
  league_fixtures = CompetitionView::buildLeagueFixtures(
      controller, managed->get().getLeagueId());

  for (size_t index = 0; index < club_fixtures.size(); ++index)
  {
    const CompetitionView::FixtureRow& fixture = club_fixtures[index];
    if (!fixture.played)
    {
      if (next_fixture_index < 0) next_fixture_index = static_cast<int>(index);
      continue;
    }
    if (fixture.type != MatchType::LEAGUE) continue;
    const bool home = fixture.home_id == club_id;
    goals_for += home ? fixture.home_score : fixture.away_score;
    goals_against += home ? fixture.away_score : fixture.home_score;
    using enum UI::Outcome;
    switch (CompetitionView::outcomeFor(fixture, club_id))
    {
      case WIN:
        ++wins;
        break;
      case DRAW:
        ++draws;
        break;
      case LOSS:
        ++losses;
        break;
    }
  }

  round_count = league_fixtures.empty() ? 0 : league_fixtures.back().round;
  // Open the league view on the first round that still has unplayed games.
  const auto firstOpen = std::ranges::find_if(
      league_fixtures, [](const auto& fixture) { return !fixture.played; });
  selected_round = firstOpen != league_fixtures.end()
                       ? firstOpen->round
                       : std::max(1, round_count);
}

void FixturesScene::renderContent()
{
  UI::pageHeader(LOC("FIXTURES_TITLE"), LOC("FIXTURES_SUBTITLE"));
  // The view already shown is a label, not a button that does nothing.
  const auto viewToggle = [this](const char* label, View option)
  {
    const bool active = view == option;
    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, active);
    if (UI::toggleButton(label, active)) view = option;
    ImGui::PopItemFlag();
  };
  viewToggle(LOC("FIXTURES_VIEW_CLUB"), View::CLUB);
  ImGui::SameLine();
  viewToggle(LOC("FIXTURES_VIEW_LEAGUE"), View::LEAGUE);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));

  if (view == View::CLUB)
  {
    renderClubSummary();
    renderClubFixtures(ImGui::GetContentRegionAvail().y);
  }
  else
  {
    renderLeagueRound();
  }
}

void FixturesScene::renderClubSummary()
{
  const Theme::Palette& palette = Theme::palette();
  UI::TileRow tiles(4);
  const float width = tiles.width();
  const std::string record = fmt::sprintf("%d–%d–%d", wins, draws, losses);
  tiles.next();
  UI::statTile("record", LOC("FIXTURES_RECORD"), record.c_str(),
               LOC("FIXTURES_RECORD_NOTE"), palette.text, width);
  tiles.next();
  const std::string goals = fmt::sprintf("%d : %d", goals_for, goals_against);
  UI::statTile(
      "goals", LOC("FIXTURES_GOALS"), goals.c_str(), LOC("FIXTURES_GOALS_NOTE"),
      goals_for >= goals_against ? palette.positive : palette.negative, width);
  tiles.next();
  const int played = wins + draws + losses;
  const std::string ppg =
      played > 0 ? fmt::sprintf("%.2f", static_cast<double>(wins * 3 + draws) /
                                            static_cast<double>(played))
                 : std::string("–");
  UI::statTile("ppg", LOC("FIXTURES_PPG"), ppg.c_str(),
               LOC("FIXTURES_PPG_NOTE"), palette.text, width);
  tiles.next();
  const auto remaining = static_cast<int>(std::ranges::count_if(
      club_fixtures, [](const auto& fixture) { return !fixture.played; }));
  const std::string remainingText = std::to_string(remaining);
  UI::statTile("remaining", LOC("FIXTURES_REMAINING"), remainingText.c_str(),
               LOC("FIXTURES_REMAINING_NOTE"), palette.text, width);
}

void FixturesScene::renderClubFixtures(float height)
{
  const Theme::Palette& palette = Theme::palette();
  if (club_fixtures.empty())
  {
    UI::emptyState(LOC("FIXTURES_EMPTY_TITLE"), LOC("FIXTURES_EMPTY_BODY"));
    return;
  }
  UI::beginCard("club_fixtures_card", nullptr, ImVec2(0.0f, height));
  // Fills the page height (vertical scroll only); columns hide by priority.
  std::array<UI::Column, 6> columns = clubFixtureColumns();
  for (UI::Column& column : columns) column.label = LOC(column.label);
  const UI::ColumnMask mask =
      UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
  if (const ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_BordersInnerH |
                                    ImGuiTableFlags_ScrollY;
      UI::beginResponsiveTable("club_fixtures", columns, mask, flags))
  {
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(club_fixtures.size()));
    if (next_fixture_index >= 0) clipper.IncludeItemByIndex(next_fixture_index);
    while (clipper.Step())
    {
      for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line)
      {
        const CompetitionView::FixtureRow& fixture =
            club_fixtures[static_cast<size_t>(line)];
        const bool home = fixture.home_id == club_id;
        const TeamID opponent = home ? fixture.away_id : fixture.home_id;
        ImGui::TableNextRow(ImGuiTableRowFlags_None,
                            ImGui::GetTextLineHeight() + 6.0f * Theme::scale());
        const bool isNext = line == next_fixture_index;
        if (isNext)
        {
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                                 Theme::toU32(palette.accent, 0.16f));
          if (scroll_to_next)
          {
            ImGui::SetScrollHereY(0.3f);
            scroll_to_next = false;
          }
        }
        if (UI::cell(mask, 0))
          ImGui::TextColored(
              fixture.played ? palette.muted : palette.text, "%s",
              Format::matchDay(fixture.date, fixture.kickoff).c_str());
        if (UI::cell(mask, 1))
        {
          UI::badge(LOC(CompetitionView::matchTypeKey(fixture.type)),
                    matchTypeColor(fixture.type));
          if (fixture.round > 0)
          {
            ImGui::SameLine();
            ImGui::TextColored(
                palette.faint, "%s",
                fmt::sprintf(LOC("FIXTURES_MATCHDAY_SHORT"), fixture.round)
                    .c_str());
          }
        }
        if (UI::cell(mask, 2))
          ImGui::TextColored(palette.muted, "%s",
                             LOC(home ? "FIXTURE_HOME" : "FIXTURE_AWAY"));
        ImGui::TableNextColumn();
        ImGui::PushID(line);
        const std::string& opponentName =
            home ? fixture.away_name : fixture.home_name;
        if (const ClubIdentity* identity =
                guiView->getController().getClubIdentity(opponent))
        {
          UI::clubBadge(nullptr, identity->primary_colour,
                        identity->secondary_colour, BADGE_HEIGHT);
          ImGui::SameLine(0.0f, Theme::Space::S * Theme::scale());
        }
        if (ImGui::Selectable(opponentName.c_str(), false,
                              ImGuiSelectableFlags_SpanAllColumns))
        {
          if (fixture.played)
            Navigation::openMatchReport(guiView, fixture.date, fixture.home_id,
                                        fixture.away_id);
          else
            Navigation::openClub(guiView, opponent);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
          ImGui::SetTooltip("%s",
                            LOC(fixture.played ? "FIXTURES_OPEN_REPORT_HINT"
                                               : "STANDINGS_OPEN_CLUB_HINT"));
        ImGui::PopID();
        ImGui::TableNextColumn();
        if (fixture.played)
          ImGui::Text("%u – %u", fixture.home_score, fixture.away_score);
        else
          ImGui::TextColored(palette.faint, "%s",
                             isNext ? LOC("FIXTURES_NEXT") : "–");
        if (UI::cell(mask, 5) && fixture.played)
        {
          const UI::Outcome outcome =
              CompetitionView::outcomeFor(fixture, club_id);
          UI::formStrip(std::span(&outcome, 1));
        }
      }
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void FixturesScene::renderLeagueRound()
{
  const Theme::Palette& palette = Theme::palette();
  if (round_count == 0)
  {
    UI::emptyState(LOC("FIXTURES_EMPTY_TITLE"), LOC("FIXTURES_EMPTY_BODY"));
    return;
  }
  ImGui::BeginDisabled(selected_round <= 1);
  if (ImGui::ArrowButton("##previous_round", ImGuiDir_Left)) --selected_round;
  ImGui::EndDisabled();
  ImGui::SameLine();
  const auto firstInRound =
      std::ranges::find_if(league_fixtures, [this](const auto& fixture)
                           { return fixture.round == selected_round; });
  {
    Theme::ScopedText title(Theme::Text::TITLE);
    const std::string label =
        fmt::sprintf(LOC("FIXTURES_ROUND_OF"), selected_round, round_count);
    ImGui::TextUnformatted(label.c_str());
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(selected_round >= round_count);
  if (ImGui::ArrowButton("##next_round", ImGuiDir_Right)) ++selected_round;
  ImGui::EndDisabled();
  if (firstInRound != league_fixtures.end())
  {
    // A round is spread over several days: show the first and the last.
    GameDateValue lastDay = firstInRound->date;
    for (auto fixture = firstInRound;
         fixture != league_fixtures.end() && fixture->round == selected_round;
         ++fixture)
      if (lastDay < fixture->date) lastDay = fixture->date;
    const std::string days = lastDay == firstInRound->date
                                 ? Format::date(lastDay)
                                 : Format::dayMonth(firstInRound->date) +
                                       " – " + Format::date(lastDay);
    ImGui::SameLine(0.0f, Theme::Space::L * Theme::scale());
    ImGui::TextColored(palette.muted, "%s", days.c_str());
  }

  UI::beginCard("league_round_card", nullptr,
                ImVec2(0.0f, ImGui::GetContentRegionAvail().y));
  if (ImGui::BeginTable("league_round", 3,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_ScrollY))
  {
    ImGui::TableSetupColumn("home", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("score", ImGuiTableColumnFlags_WidthFixed,
                            90.0f * Theme::scale());
    ImGui::TableSetupColumn("away", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    for (auto fixture = firstInRound;
         fixture != league_fixtures.end() && fixture->round == selected_round;
         ++fixture)
    {
      const bool involvesClub =
          fixture->home_id == club_id || fixture->away_id == club_id;
      ImGui::TableNextRow(ImGuiTableRowFlags_None,
                          ImGui::GetTextLineHeight() + 10.0f * Theme::scale());
      if (involvesClub)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                               Theme::toU32(palette.accent, 0.16f));
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(fixture->home_id));
      const float homeWidth = ImGui::CalcTextSize(fixture->home_name.c_str()).x;
      ImGui::SetCursorPosX(
          ImGui::GetCursorPosX() +
          std::max(0.0f, ImGui::GetContentRegionAvail().x - homeWidth));
      if (UI::link(fixture->home_name.c_str(), "home"))
        Navigation::openClub(guiView, fixture->home_id);
      ImGui::PopID();
      ImGui::TableNextColumn();
      // Upcoming matches show when they kick off.
      const std::string score =
          fixture->played ? fmt::sprintf("%u – %u", fixture->home_score,
                                         fixture->away_score)
                          : std::string(Format::weekday(fixture->date)) + " " +
                                Format::kickoff(fixture->kickoff);
      const float scoreWidth = ImGui::CalcTextSize(score.c_str()).x;
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                           (ImGui::GetContentRegionAvail().x - scoreWidth) *
                               0.5f);
      if (fixture->played)
      {
        ImGui::PushID(static_cast<int>(fixture->home_id) + 0x20000);
        if (UI::link(score.c_str(), "score"))
          Navigation::openMatchReport(guiView, fixture->date, fixture->home_id,
                                      fixture->away_id);
        ImGui::PopID();
      }
      else
      {
        ImGui::TextColored(palette.faint, "%s", score.c_str());
      }
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(fixture->away_id) + 0x10000);
      if (UI::link(fixture->away_name.c_str(), "away"))
        Navigation::openClub(guiView, fixture->away_id);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

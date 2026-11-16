// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/news_scene.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <set>
#include <tuple>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/competition.h"
#include "model/continental.h"
#include "model/game.h"

namespace
{
/** Stories shown at first, and added by each Show more. */
constexpr std::size_t PAGE_SIZE = 40;
constexpr float COMBO_WIDTH = 260.0f;
constexpr float DATE_WIDTH = 110.0f;
/** Below this width the date goes above the headline. */
constexpr float SIDE_DATE_MIN_WIDTH = 640.0f;

constexpr std::array<NewsKind, 6> KINDS = {
    NewsKind::Transfer, NewsKind::Manager, NewsKind::Race,
    NewsKind::Upset,    NewsKind::Record,  NewsKind::Award};

std::string teamName(const GameController& controller, TeamID id)
{
  if (id == 0) return {};
  const auto team = controller.getTeamById(id);
  return team ? team->get().getName() : std::string();
}

std::string competitionName(const GameController& controller,
                            const NewsCompetition& competition)
{
  if (competition.id == 0) return {};
  if (competition.type == MatchType::CONTINENTAL)
  {
    const auto* rules = Continental::rules(competition.id);
    return rules != nullptr ? std::string(LOC(rules->name_key)) : std::string();
  }
  if (competition.type == MatchType::CUP)
    return controller.getCupName(competition.id);
  const auto league = controller.getLeagueById(competition.id);
  return league ? Competitions::leagueName(league->get()) : std::string();
}
}  // namespace

NewsScene::NewsScene(GUIView* parent) : ManagementScene(parent) {}

void NewsScene::refresh()
{
  const GameController& controller = guiView->getController();
  items.clear();
  rows.clear();
  const Game* game = controller.getGame();
  const auto gamedata = controller.getGameData();
  if (game == nullptr || !gamedata)
  {
    applyFilter();
    return;
  }

  items = NewsFeed::forGame(*game, *gamedata);

  std::set<LeagueID> country_ids;
  rows.reserve(items.size());
  for (const NewsItem& item : items)
  {
    country_ids.insert(item.country);
    Row row;
    row.headline = item.headline();
    row.date = Format::date(item.date);
    const std::string competition =
        competitionName(controller, item.competition);
    row.meta = std::string(LOC(newsKindKey(item.kind))) +
               (competition.empty() ? std::string() : "  ·  " + competition);
    if (item.player_id != 0 && gamedata->getPlayer(item.player_id))
      row.player = gamedata->getPlayer(item.player_id)->get().getName();
    row.team = teamName(controller, item.team_id);
    row.other_team = teamName(controller, item.other_team_id);
    rows.push_back(std::move(row));
  }

  countries.clear();
  for (const LeagueID id : country_ids)
  {
    if (id == 0)
    {
      countries.emplace_back(0, LOC("NEWS_CONTINENTAL"));
      continue;
    }
    if (const auto league = controller.getLeagueById(id))
      countries.emplace_back(id, Competitions::leagueName(league->get()));
  }
  // Continental first, then the countries by name.
  std::ranges::sort(countries,
                    [](const auto& left, const auto& right)
                    {
                      if ((left.first == 0) != (right.first == 0))
                        return left.first == 0;
                      return left.second < right.second;
                    });
  if (filter.country.has_value() &&
      std::ranges::find(countries, *filter.country,
                        &std::pair<LeagueID, std::string>::first) ==
          countries.end())
    filter.country.reset();
  rebuildCompetitions();
  applyFilter();
}

void NewsScene::rebuildCompetitions()
{
  const GameController& controller = guiView->getController();
  competitions.clear();
  std::set<std::pair<int, LeagueID>> seen;
  for (const NewsItem& item : items)
  {
    if (item.competition.id == 0) continue;
    if (filter.country.has_value() && item.country != *filter.country) continue;
    if (!seen.insert(
                 {static_cast<int>(std::to_underlying(item.competition.type)),
                  item.competition.id})
             .second)
      continue;
    competitions.emplace_back(item.competition,
                              competitionName(controller, item.competition));
  }
  // Leagues by tier and name, then cups, then continental competitions.
  const auto data = controller.getGameData();
  std::ranges::sort(
      competitions,
      [&data](const CompetitionOption& left, const CompetitionOption& right)
      {
        const auto rank = [&data](const CompetitionOption& option)
        {
          int type = 2;
          if (option.competition.type == MatchType::LEAGUE)
            type = 0;
          else if (option.competition.type == MatchType::CUP)
            type = 1;
          const int tier =
              type == 0 && data && data->getLeague(option.competition.id)
                  ? Competitions::leagueTier(*data, option.competition.id)
                  : 0;
          return std::tuple{type, tier, option.name};
        };
        return rank(left) < rank(right);
      });
  if (filter.competition &&
      std::ranges::find(competitions, *filter.competition,
                        &CompetitionOption::competition) == competitions.end())
    filter.competition.reset();
}

void NewsScene::applyFilter()
{
  kept = NewsFeed::filter(items, filter);
  visible = std::min(PAGE_SIZE, kept.size());
  country_label = LOC("NEWS_ALL_COUNTRIES");
  if (filter.country.has_value())
    if (const auto found =
            std::ranges::find(countries, *filter.country,
                              &std::pair<LeagueID, std::string>::first);
        found != countries.end())
      country_label = found->second;
  competition_label = LOC("NEWS_ALL_COMPETITIONS");
  if (filter.competition)
    if (const auto found = std::ranges::find(competitions, *filter.competition,
                                             &CompetitionOption::competition);
        found != competitions.end())
      competition_label = found->name;
}

void NewsScene::filterCountry(std::optional<LeagueID> country)
{
  filter.country = country;
  filter.competition.reset();
  rebuildCompetitions();
  applyFilter();
}

void NewsScene::filterKind(std::optional<NewsKind> kind)
{
  filter.kind = kind;
  applyFilter();
}

void NewsScene::renderFilters()
{
  const float comboWidth =
      std::min(COMBO_WIDTH * Theme::scale(), ImGui::GetContentRegionAvail().x);
  ImGui::SetNextItemWidth(comboWidth);
  if (ImGui::BeginCombo("##news_country", country_label.c_str(),
                        ImGuiComboFlags_HeightLarge))
  {
    if (ImGui::Selectable(LOC("NEWS_ALL_COUNTRIES"),
                          !filter.country.has_value()))
      filterCountry(std::nullopt);
    for (const auto& [id, name] : countries)
    {
      ImGui::PushID(static_cast<int>(id));
      if (ImGui::Selectable(name.c_str(), filter.country.has_value() &&
                                              *filter.country == id))
        filterCountry(id);
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  UI::sameLineIfFits(comboWidth);
  ImGui::SetNextItemWidth(comboWidth);
  if (ImGui::BeginCombo("##news_competition", competition_label.c_str(),
                        ImGuiComboFlags_HeightLarge))
  {
    if (ImGui::Selectable(LOC("NEWS_ALL_COMPETITIONS"), !filter.competition))
    {
      filter.competition.reset();
      applyFilter();
    }
    for (std::size_t index = 0; index < competitions.size(); ++index)
    {
      const CompetitionOption& option = competitions[index];
      ImGui::PushID(static_cast<int>(index));
      if (ImGui::Selectable(
              option.name.c_str(),
              filter.competition && *filter.competition == option.competition))
      {
        filter.competition = option.competition;
        applyFilter();
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }

  // Kinds as toggles that wrap onto more lines on narrow windows.
  if (const char* all = LOC("NEWS_KIND_ALL"); UI::toggleButton(
          all, !filter.kind, ImVec2(0, 0), UI::ButtonSize::COMPACT))
    filterKind(std::nullopt);
  for (const NewsKind kind : KINDS)
  {
    const char* label = LOC(newsKindKey(kind));
    UI::sameLineIfFits(UI::buttonWidth(label, UI::ButtonSize::COMPACT));
    ImGui::PushID(static_cast<int>(std::to_underlying(kind)));
    if (UI::toggleButton(label, filter.kind && *filter.kind == kind,
                         ImVec2(0, 0), UI::ButtonSize::COMPACT))
      filterKind(filter.kind && *filter.kind == kind
                     ? std::nullopt
                     : std::optional<NewsKind>(kind));
    ImGui::PopID();
  }
}

void NewsScene::renderStory(std::size_t index)
{
  const NewsItem& item = items[index];
  const Row& row = rows[index];
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const float available = ImGui::GetContentRegionAvail().x;
  const bool sideDate = available >= SIDE_DATE_MIN_WIDTH * scale;
  const float dateWidth = DATE_WIDTH * scale;

  ImGui::PushID(static_cast<int>(index));
  const float startX = ImGui::GetCursorPosX();
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    if (sideDate)
    {
      ImGui::TextColored(palette.muted, "%s", row.date.c_str());
      ImGui::SameLine(startX + dateWidth);
    }
  }
  ImGui::BeginGroup();
  if (!sideDate)
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    ImGui::TextColored(palette.muted, "%s", row.date.c_str());
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(row.headline.c_str());
  ImGui::PopTextWrapPos();
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    ImGui::TextColored(palette.faint, "%s", row.meta.c_str());
    const auto linkTo =
        [&](const std::string& label, const char* id, bool leading)
    {
      if (label.empty()) return false;
      if (!leading)
        UI::sameLineIfFits(ImGui::CalcTextSize(label.c_str()).x,
                           Theme::Space::L * scale);
      return UI::link(label.c_str(), id);
    };
    bool first = true;
    if (!row.player.empty())
    {
      if (linkTo(row.player, "##player", first))
        Navigation::openPlayer(guiView, item.player_id);
      first = false;
    }
    if (!row.team.empty())
    {
      if (linkTo(row.team, "##team", first))
        Navigation::openClub(guiView, item.team_id);
      first = false;
    }
    if (!row.other_team.empty())
    {
      if (linkTo(row.other_team, "##other", first))
        Navigation::openClub(guiView, item.other_team_id);
      first = false;
    }
    if (item.has_match)
    {
      const char* report = LOC("NEWS_MATCH_REPORT");
      if (!first)
        UI::sameLineIfFits(ImGui::CalcTextSize(report).x,
                           Theme::Space::L * scale);
      if (UI::link(report, "##report"))
        Navigation::openMatchReport(guiView, item.match_date, item.home_id,
                                    item.away_id);
    }
  }
  ImGui::EndGroup();
  ImGui::PopID();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
  ImGui::Separator();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
}

void NewsScene::renderContent()
{
  UI::pageHeader(LOC("NEWS_TITLE"), LOC("NEWS_SUBTITLE"));
  renderFilters();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  if (kept.empty())
  {
    UI::emptyState(
        LOC("NEWS_EMPTY_TITLE"),
        LOC(items.empty() ? "NEWS_EMPTY_BODY" : "NEWS_EMPTY_FILTERED"));
    return;
  }
  UI::beginAutoHeightCard("news_feed", nullptr);
  for (std::size_t shown = 0; shown < visible && shown < kept.size(); ++shown)
    renderStory(kept[shown]);
  if (visible < kept.size())
  {
    if (UI::secondaryButton(LOC("NEWS_SHOW_MORE")))
      visible = std::min(kept.size(), visible + PAGE_SIZE);
  }
  else
  {
    ImGui::TextColored(Theme::palette().faint, "%s", LOC("NEWS_END"));
  }
  UI::endCard();
}

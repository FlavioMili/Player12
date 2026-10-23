// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/international_scene.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "model/competition.h"
#include "model/continental.h"
#include "model/inbox.h"
#include "model/national_teams.h"

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 1100.0f;
constexpr float LEFT_SHARE = 0.6f;
constexpr float ROUND_CARD_MIN_WIDTH = 300.0f;
constexpr float GROUP_CARD_MIN_WIDTH = 280.0f;
constexpr size_t MAX_NATION_FIXTURES = 24;
constexpr size_t MAX_LEADERS = 10;

const std::array<UI::Column, 10>& tableColumns()
{
  static const std::array<UI::Column, 10> columns = {{
      {"MAIN_GAME_POS", 36.0f, 0},
      {"MAIN_GAME_TEAM", 0.0f, 0},
      {"INTL_COL_ASSOCIATION", 130.0f, 4},
      {"TABLE_COL_PLAYED", 34.0f, 1},
      {"TABLE_COL_WON", 34.0f, 3},
      {"TABLE_COL_DRAWN", 34.0f, 3},
      {"TABLE_COL_LOST", 34.0f, 3},
      {"TABLE_COL_GD", 46.0f, 2},
      {"MAIN_GAME_PTS", 44.0f, 0},
      {"TABLE_COL_FORM", 118.0f, 5},
  }};
  return columns;
}

const std::array<UI::Column, 5>& entrantColumns()
{
  static const std::array<UI::Column, 5> columns = {{
      {"INTL_COL_POT", 44.0f, 0},
      {"MAIN_GAME_TEAM", 0.0f, 0},
      {"INTL_COL_ASSOCIATION", 140.0f, 2},
      {"INTL_COL_ROUTE", 150.0f, 3},
      {"INTL_COL_COEFFICIENT", 90.0f, 1},
  }};
  return columns;
}

const std::array<UI::Column, 4>& fixtureColumns()
{
  static const std::array<UI::Column, 4> columns = {{
      {"INTL_COL_DATE", 110.0f, 2},
      {"INTL_COL_HOME", 0.0f, 0},
      {"INTL_COL_SCORE", 64.0f, 0},
      {"INTL_COL_AWAY", 0.0f, 0},
  }};
  return columns;
}

const std::array<UI::Column, 5>& calledColumns()
{
  static const std::array<UI::Column, 5> columns = {{
      {"INTL_COL_PLAYER", 0.0f, 0},
      {"INTL_COL_NATION", 130.0f, 1},
      {"INTL_COL_CAPS", 54.0f, 0},
      {"INTL_COL_GOALS", 54.0f, 2},
      {"INTL_COL_STATUS", 110.0f, 3},
  }};
  return columns;
}

const std::array<UI::Column, 5>& nationFixtureColumns()
{
  static const std::array<UI::Column, 5> columns = {{
      {"INTL_COL_DATE", 100.0f, 2},
      {"INTL_COL_HOME", 0.0f, 0},
      {"INTL_COL_SCORE", 64.0f, 0},
      {"INTL_COL_AWAY", 0.0f, 0},
      {"INTL_COL_COMPETITION", 170.0f, 3},
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

std::string nationName(Language nation)
{
  return LOC(International::teamNameKey(nation).c_str());
}

std::string scoreText(uint8_t home, uint8_t away)
{
  return std::format("{} - {}", home, away);
}

/** Two cards side by side on wide pages, stacked otherwise. */
std::pair<float, float> columnWidths(bool& two_columns)
{
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  two_columns = available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
  if (!two_columns) return {available, available};
  const float left = std::floor((available - gap) * LEFT_SHARE);
  return {left, available - gap - left};
}
}  // namespace

InternationalScene::InternationalScene(GUIView* parent)
    : ManagementScene(parent)
{
}

void InternationalScene::update(float /*deltaTime*/)
{
  /* The screen refreshes when the game date changes; no frame-driven state
   * needs to be updated. */
}

void InternationalScene::refresh()
{
  refreshed_on = guiView->getController().getCurrentDate();
  refreshContinental();
  refreshNational();
}

void InternationalScene::refreshContinental()
{
  GameController& controller = guiView->getController();
  competitions.clear();
  table.clear();
  entrants.clear();
  rounds.clear();
  fixtures_by_matchday.clear();
  associations.clear();
  drawn = false;
  stage.clear();
  club_status.clear();
  holder.clear();
  next_date.clear();
  const ContinentalCompetitions* continental = controller.getContinental();
  const Game* game = controller.getGame();
  if (!continental || !game) return;
  const auto managed = controller.getManagedTeam();
  const TeamID club = managed ? managed->get().getId() : TeamID{0};
  for (const auto& season : continental->getSeasons())
    if (const auto* rules = Continental::rules(season.competition_id))
      competitions.emplace_back(season.competition_id, LOC(rules->name_key));
  if (competitions.empty()) return;
  if (!competition_chosen)
  {
    if (const auto own = continental->competitionOf(club); own.has_value())
      for (size_t i = 0; i < competitions.size(); ++i)
        if (competitions[i].id == *own) competition_index = i;
    competition_chosen = true;
  }
  competition_index = std::min(competition_index, competitions.size() - 1);
  const LeagueID id = competitions[competition_index].id;
  const auto* season = continental->getSeason(id);
  const auto* rules = Continental::rules(id);
  if (!season || !rules) return;

  const auto teamName = [&controller](TeamID team_id)
  {
    const auto team = controller.getTeamById(team_id);
    return team ? team->get().getName() : std::string();
  };
  const auto associationName = [&controller](LeagueID root)
  {
    const auto league = controller.getLeagueById(root);
    return league ? Competitions::leagueName(league->get()) : std::string();
  };
  const auto data = controller.getGameData();
  const auto associationOf = [&](TeamID team_id)
  {
    const auto team = controller.getTeamById(team_id);
    return team && data
               ? Competitions::rootLeague(*data, team->get().getLeagueId())
               : LeagueID{0};
  };

  drawn = season->drawn;
  draw_date = Format::date(season->draw_date);
  direct_places = Continental::directPlaces(season->clubs);
  playoff_places = Continental::playoffPlaces(season->clubs);
  matchdays = season->matches;

  for (const StandingRow& row : continental->getTable(id))
  {
    TableLine line;
    line.id = row.team_id;
    line.position = row.position;
    line.name = teamName(row.team_id);
    line.association = associationName(associationOf(row.team_id));
    line.played = row.played;
    line.won = row.won;
    line.drawn = row.drawn;
    line.lost = row.lost;
    line.goal_difference = row.goal_difference;
    line.points = row.points;
    for (const char outcome : row.form)
    {
      if (line.form_count >= line.form.size()) break;
      UI::Outcome result = UI::Outcome::LOSS;
      if (outcome == 'W')
        result = UI::Outcome::WIN;
      else if (outcome == 'D')
        result = UI::Outcome::DRAW;
      line.form[line.form_count++] = result;
    }
    table.push_back(std::move(line));
  }

  for (const auto& entrant : season->entrants)
  {
    EntrantLine line;
    line.id = entrant.team_id;
    line.name = teamName(entrant.team_id);
    line.association = associationName(entrant.association);
    line.pot = static_cast<uint8_t>(entrant.pot + 1);
    line.coefficient = std::format("{:.1f}", entrant.coefficient);
    if (entrant.cup_winner)
      line.route = LOC("INTL_ROUTE_CUP");
    else if (entrant.league_position > 0)
      line.route = formatLocalized("INTL_ROUTE_LEAGUE",
                                   {std::to_string(entrant.league_position)});
    else
      line.route = LOC("INTL_ROUTE_REPUTATION");
    entrants.push_back(std::move(line));
  }

  // Matchdays and the next fixture from the calendar.
  fixtures_by_matchday.assign(season->matches, {});
  std::optional<GameDateValue> next;
  uint8_t played_matchdays = 0;
  for (const auto& [date, matches] : game->getCalendar().getFullCalendar())
  {
    for (const Match& match : matches)
    {
      if (match.getMatchType() != MatchType::CONTINENTAL ||
          match.getCompetitionId() != id)
        continue;
      if (!match.isPlayed() && !next) next = date;
      if (Continental::roundOf(match.getStage()) !=
              Continental::Round::LeaguePhase ||
          match.getStage() == 0 || match.getStage() > season->matches)
        continue;
      if (match.isPlayed())
        played_matchdays = std::max(played_matchdays, match.getStage());
      FixtureLine line;
      line.date = date;
      line.when = Format::dayMonth(date);
      line.home_id = match.getHomeTeamId();
      line.away_id = match.getAwayTeamId();
      line.home = teamName(line.home_id);
      line.away = teamName(line.away_id);
      line.played = match.isPlayed();
      line.score = line.played
                       ? scoreText(match.getHomeScore(), match.getAwayScore())
                       : std::string("-");
      line.ours = line.home_id == club || line.away_id == club;
      fixtures_by_matchday[match.getStage() - 1].push_back(std::move(line));
    }
  }
  if (!matchday_chosen)
  {
    matchday = std::clamp<int>(played_matchdays + (next ? 1 : 0), 1,
                               std::max<int>(1, season->matches));
    matchday_chosen = true;
  }
  next_date = next ? Format::date(*next) : std::string(LOC("INTL_NONE"));

  // Knockout rounds.
  std::map<Continental::Round, RoundBlock> by_round;
  for (const auto& tie : season->ties)
  {
    RoundBlock& block = by_round[tie.round];
    block.title = LOC(Continental::roundKey(tie.round));
    TieLine line;
    line.first = teamName(tie.seeded_id);
    line.second = teamName(tie.unseeded_id);
    line.ours = tie.seeded_id == club || tie.unseeded_id == club;
    line.winner = 0;
    if (tie.winner_id != 0)
      line.winner = tie.winner_id == tie.seeded_id ? 1 : 2;
    const auto score = controller.getContinentalTieScore(id, tie);
    const Match* last = score.second_leg;
    const bool started = (score.first_leg && score.first_leg->isPlayed()) ||
                         (last && last->isPlayed() && !score.first_leg);
    line.score = started ? std::format("{} - {}", score.seeded_goals,
                                       score.unseeded_goals)
                         : std::string("-");
    if (tie.round == Continental::Round::Final)
    {
      line.detail = last ? formatLocalized("INTL_FINAL_DETAIL",
                                           {Format::date(last->getDate())})
                         : std::string();
    }
    else if (!started && score.first_leg && last)
    {
      line.detail = formatLocalized(
          "INTL_LEGS_DATES", {Format::dayMonth(score.first_leg->getDate()),
                              Format::dayMonth(last->getDate())});
    }
    else if (score.first_leg && score.first_leg->isPlayed())
    {
      // Legs from the seeded club's point of view.
      std::string legs = scoreText(score.first_leg->getAwayScore(),
                                   score.first_leg->getHomeScore());
      if (last && last->isPlayed())
        legs += ", " + scoreText(last->getHomeScore(), last->getAwayScore());
      line.detail = formatLocalized("INTL_LEGS", {legs});
    }
    if (last && last->isPlayed() && last->wentToPenalties())
      line.detail +=
          "  ·  " + formatLocalized("INTL_PENALTIES",
                                    {scoreText(last->getHomePenalties(),
                                               last->getAwayPenalties())});
    else if (last && last->isPlayed() && last->wentToExtraTime())
      line.detail += std::string("  ·  ") + LOC("INTL_AET");
    block.ties.push_back(std::move(line));
  }
  for (auto& [round, block] : by_round) rounds.push_back(std::move(block));

  // Headline tiles.
  if (!drawn)
    stage = formatLocalized("INTL_STAGE_DRAW", {draw_date});
  else if (!season->league_phase_complete)
    stage = formatLocalized(
        "INTL_STAGE_LEAGUE",
        {std::to_string(played_matchdays), std::to_string(season->matches)});
  else if (season->winner_id != 0)
    stage = LOC("INTL_STAGE_FINISHED");
  else if (!season->ties.empty())
    stage = LOC(Continental::roundKey(season->ties.back().round));

  if (season->winner_id != 0)
    holder = teamName(season->winner_id);
  else if (!table.empty() && table.front().played > 0)
    holder = table.front().name;
  else
    holder = LOC("INTL_NONE");

  if (const bool entered =
          std::ranges::any_of(season->entrants, [club](const auto& entrant)
                              { return entrant.team_id == club; });
      club == 0 || !entered)
  {
    club_status = LOC("INTL_CLUB_NOT_IN");
  }
  else if (season->winner_id == club)
  {
    club_status = LOC("INTL_CLUB_WINNER");
  }
  else
  {
    const bool in_knockouts = std::ranges::any_of(
        season->ties, [club](const auto& tie)
        { return tie.seeded_id == club || tie.unseeded_id == club; });
    const bool out = std::ranges::any_of(
        season->ties,
        [club](const auto& tie)
        {
          return (tie.seeded_id == club || tie.unseeded_id == club) &&
                 tie.winner_id != 0 && tie.winner_id != club;
        });
    const auto row = std::ranges::find(table, club, &TableLine::id);
    if (out ||
        (season->league_phase_complete && !in_knockouts && row != table.end() &&
         row->position > direct_places + playoff_places))
      club_status = LOC("INTL_CLUB_OUT");
    else if (row != table.end() && row->played > 0)
      club_status = formatLocalized("INTL_CLUB_POSITION",
                                    {std::to_string(row->position)});
    else
      club_status = LOC("INTL_CLUB_IN");
  }

  if (const auto order = continental->associationRanking(rules->continent);
      !order.empty())
  {
    for (const auto& coefficient : order)
      associations.emplace_back(
          associationName(coefficient.association),
          std::format("{:.2f}", coefficient.seasons.front()),
          std::format("{:.2f}", coefficient.total()));
  }
}

void InternationalScene::refreshNational()
{
  GameController& controller = guiView->getController();
  called.clear();
  upcoming.clear();
  results.clear();
  groups.clear();
  knockout_lines.clear();
  leaders.clear();
  ranking.clear();
  groups_title.clear();
  const NationalTeams* nations = controller.getNationalTeams();
  if (!nations) return;
  const GameDateValue today = controller.getCurrentDate();
  const auto managed = controller.getManagedTeam();
  const TeamID club = managed ? managed->get().getId() : TeamID{0};
  const auto data = controller.getGameData();

  const auto recordOf = [nations](PlayerID id)
  { return nations->getRecord(id); };
  for (const auto& squad : nations->getSquads())
  {
    for (const PlayerID player_id : squad.players)
    {
      const auto player = data ? data->getPlayer(player_id) : std::nullopt;
      if (!player || club == 0 || player->get().getTeamId() != club) continue;
      const auto* record = recordOf(player_id);
      CalledLine line;
      line.id = player_id;
      line.name = player->get().getName();
      line.nation = nationName(squad.nation);
      line.caps = std::to_string(record ? record->caps : 0);
      line.goals = std::to_string(record ? record->goals : 0);
      line.away = !(today < squad.start);
      called.push_back(std::move(line));
    }
  }
  std::ranges::sort(called, {}, &CalledLine::name);

  std::vector<const International::Fixture*> future;
  std::vector<const International::Fixture*> past;
  for (const auto& fixture : nations->getFixtures())
    (fixture.played ? past : future).push_back(&fixture);
  std::ranges::sort(
      future, [](const auto* a, const auto* b)
      { return a->date < b->date || (a->date == b->date && a->id < b->id); });
  std::ranges::sort(
      past, [](const auto* a, const auto* b)
      { return b->date < a->date || (a->date == b->date && a->id < b->id); });
  const auto toLine = [](const International::Fixture& fixture)
  {
    NationFixture line;
    line.when = Format::dayMonth(fixture.date);
    line.home = nationName(fixture.home);
    line.away = nationName(fixture.away);
    line.played = fixture.played;
    line.score = fixture.played
                     ? scoreText(fixture.home_goals, fixture.away_goals)
                     : std::string("-");
    if (fixture.played && fixture.penalties)
      line.score += std::format(" ({}-{})", fixture.home_penalties,
                                fixture.away_penalties);
    line.competition = LOC(International::competitionKey(fixture.competition));
    if (fixture.stage != International::Stage::Group)
      line.competition +=
          std::string("  ·  ") + LOC(International::stageKey(fixture.stage));
    return line;
  };
  for (size_t i = 0; i < future.size() && i < MAX_NATION_FIXTURES; ++i)
    upcoming.push_back(toLine(*future[i]));
  for (size_t i = 0; i < past.size() && i < MAX_NATION_FIXTURES; ++i)
    results.push_back(toLine(*past[i]));
  next_international = future.empty() ? std::string(LOC("INTL_NONE"))
                                      : Format::date(future.front()->date);

  // A finals tournament that is drawn (or running) takes precedence over
  // the qualifying or nations-league groups.
  const NationalTeams::Finals* finals = nullptr;
  for (const auto& entry : nations->getFinals())
    if (entry.drawn && !entry.qualified.empty() &&
        (!entry.winner || finals == nullptr))
      finals = &entry;
  const auto groupRows = [&](const International::Group& group, size_t through)
  {
    GroupBlock block;
    block.title = formatLocalized("INTL_GROUP", {std::to_string(group.index)});
    const auto rows = nations->table(group);
    for (size_t i = 0; i < rows.size(); ++i)
    {
      const auto& row = rows[i];
      block.rows.emplace_back(nationName(row.nation), row.played, row.won,
                              row.drawn, row.lost, row.goalDifference(),
                              row.points, i < through);
    }
    return block;
  };
  if (finals)
  {
    groups_title = LOC(International::competitionKey(finals->competition));
    for (const auto& group : finals->groups)
      groups.push_back(groupRows(group, 2));
    for (const auto& fixture : nations->getFixtures())
    {
      if (fixture.competition != finals->competition ||
          fixture.stage == International::Stage::Group ||
          !std::ranges::contains(finals->qualified, fixture.home))
        continue;
      std::string text =
          std::string(LOC(International::stageKey(fixture.stage))) + ":  " +
          nationName(fixture.home) + "  " +
          (fixture.played ? scoreText(fixture.home_goals, fixture.away_goals)
                          : std::string("-")) +
          "  " + nationName(fixture.away);
      if (fixture.played && fixture.penalties)
        text += "  " + formatLocalized("INTL_PENALTIES",
                                       {scoreText(fixture.home_penalties,
                                                  fixture.away_penalties)});
      knockout_lines.push_back(std::move(text));
    }
    if (finals->winner)
      knockout_lines.push_back(
          formatLocalized("INTL_FINALS_WINNER", {nationName(*finals->winner)}));
  }
  else if (!nations->getGroups().empty())
  {
    const auto competition = nations->getGroups().front().competition;
    groups_title = LOC(International::competitionKey(competition));
    const size_t through =
        competition == International::Competition::NationsLeague ? 1 : 2;
    for (const auto& group : nations->getGroups())
      groups.push_back(groupRows(group, through));
  }
  cycle = groups_title.empty() ? std::string(LOC("INTL_NONE")) : groups_title;
  champion = nations->getHonours().empty()
                 ? std::string(LOC("INTL_NONE"))
                 : formatLocalized(
                       "INTL_CHAMPION_OF",
                       {nationName(nations->getHonours().back().winner),
                        LOC(International::competitionKey(
                            nations->getHonours().back().competition)),
                        std::to_string(nations->getHonours().back().year)});

  for (const auto& [player_id, record] : nations->capsLeaders(MAX_LEADERS))
  {
    const bool active = data && data->getPlayer(player_id).has_value();
    leaders.emplace_back(player_id, record.name, nationName(record.nation),
                         record.caps, record.goals, active);
  }
  for (const Language nation : nations->ranking())
  {
    const auto* team = nations->getTeam(nation);
    ranking.emplace_back(
        nationName(nation), team ? team->coach : std::string(),
        team ? static_cast<int>(std::lround(team->rating)) : 0);
  }
}

void InternationalScene::renderContent()
{
  if (!(refreshed_on == guiView->getController().getCurrentDate())) refresh();
  UI::pageHeader(LOC("INTL_TITLE"), LOC("INTL_SUBTITLE"));
  const std::array<const char*, 2> tabs = {LOC("INTL_TAB_CONTINENTAL"),
                                           LOC("INTL_TAB_NATIONAL")};
  UI::segmented("##intl_tab", tab, tabs);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  if (tab == 0)
    renderContinental();
  else
    renderNational();
}

void InternationalScene::renderCompetitionPicker()
{
  ImGui::SetNextItemWidth(320.0f * Theme::scale());
  const char* preview = competitions[competition_index].name.c_str();
  if (ImGui::BeginCombo("##continental_competition", preview))
  {
    for (size_t i = 0; i < competitions.size(); ++i)
    {
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::Selectable(competitions[i].name.c_str(),
                            i == competition_index))
      {
        competition_index = i;
        matchday_chosen = false;
        refreshContinental();
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
}

void InternationalScene::renderContinental()
{
  if (competitions.empty())
  {
    UI::emptyState(LOC("INTL_NO_CONTINENTAL"), LOC("INTL_NO_CONTINENTAL_BODY"));
    return;
  }
  renderCompetitionPicker();
  const Theme::Palette& palette = Theme::palette();
  UI::TileRow tiles(4);
  const float tile = tiles.width();
  tiles.next();
  UI::statTile("stage", LOC("INTL_TILE_STAGE"), stage.c_str(), nullptr,
               palette.text, tile);
  tiles.next();
  UI::statTile("club", LOC("INTL_TILE_CLUB"), club_status.c_str(), nullptr,
               palette.text, tile);
  tiles.next();
  UI::statTile("leader", LOC("INTL_TILE_LEADER"), holder.c_str(), nullptr,
               palette.text, tile);
  tiles.next();
  UI::statTile("next", LOC("INTL_TILE_NEXT"), next_date.c_str(), nullptr,
               palette.text, tile);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));

  bool two_columns = false;
  const auto [left, right] = columnWidths(two_columns);
  ImGui::BeginGroup();
  if (drawn)
  {
    renderLeagueTable(left);
    renderMatchday(left);
  }
  else
  {
    renderEntrants(left);
  }
  ImGui::EndGroup();
  if (two_columns) ImGui::SameLine();
  ImGui::BeginGroup();
  renderBracket(right);
  renderAssociations(right);
  ImGui::EndGroup();
}

void InternationalScene::renderLeagueTable(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const auto managed = guiView->getController().getManagedTeam();
  const TeamID club = managed ? managed->get().getId() : TeamID{0};
  UI::beginAutoHeightCard("intl_table", LOC("INTL_LEAGUE_PHASE"), width);
  const auto columns = localized(tableColumns());
  if (const UI::ColumnMask mask =
          UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
      UI::beginResponsiveTable(
          "intl_league_table", columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (const TableLine& line : table)
    {
      ImGui::PushID(static_cast<int>(line.id));
      ImGui::TableNextRow();
      const bool started = line.played > 0;
      const bool direct = started && line.position <= direct_places;
      const bool playoff =
          started && !direct && line.position <= direct_places + playoff_places;
      if (line.id == club)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                               Theme::toU32(palette.accent, 0.16f));
      else if (direct || playoff)
        ImGui::TableSetBgColor(
            ImGuiTableBgTarget_RowBg1,
            Theme::toU32(direct ? palette.positive : palette.warning, 0.07f));
      ImGui::TableNextColumn();
      ImVec4 positionColor = palette.muted;
      if (direct)
        positionColor = palette.positive;
      else if (playoff)
        positionColor = palette.warning;
      ImGui::TextColored(positionColor, "%u", line.position);
      ImGui::TableNextColumn();
      if (ImGui::Selectable(line.name.c_str(), false,
                            ImGuiSelectableFlags_SpanAllColumns))
        Navigation::openClub(guiView, line.id);
      if (UI::cell(mask, 2))
        UI::textFitted(line.association, ImGui::GetContentRegionAvail().x,
                       palette.muted);
      if (UI::cell(mask, 3)) ImGui::Text("%u", line.played);
      if (UI::cell(mask, 4)) ImGui::Text("%u", line.won);
      if (UI::cell(mask, 5)) ImGui::Text("%u", line.drawn);
      if (UI::cell(mask, 6)) ImGui::Text("%u", line.lost);
      if (UI::cell(mask, 7))
      {
        ImVec4 differenceColor = palette.muted;
        if (line.goal_difference > 0)
          differenceColor = palette.positive;
        else if (line.goal_difference < 0)
          differenceColor = palette.negative;
        ImGui::TextColored(differenceColor, "%s",
                           Format::signedInt(line.goal_difference).c_str());
      }
      if (UI::cell(mask, 8)) ImGui::Text("%u", line.points);
      if (UI::cell(mask, 9))
        UI::formStrip(std::span(line.form.data(), line.form_count));
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  UI::badge(formatLocalized("INTL_ZONE_DIRECT", {std::to_string(direct_places)})
                .c_str(),
            palette.positive);
  ImGui::SameLine();
  UI::badge(formatLocalized("INTL_ZONE_PLAYOFF",
                            {std::to_string(direct_places + 1),
                             std::to_string(direct_places + playoff_places)})
                .c_str(),
            palette.warning);
  UI::endCard();
}

void InternationalScene::renderEntrants(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("intl_entrants", LOC("INTL_ENTRANTS"), width);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.muted, "%s",
                     formatLocalized("INTL_DRAW_PENDING", {draw_date}).c_str());
  ImGui::PopTextWrapPos();
  const auto columns = localized(entrantColumns());
  if (const UI::ColumnMask mask =
          UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
      UI::beginResponsiveTable(
          "intl_entrants_table", columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (const EntrantLine& line : entrants)
    {
      ImGui::PushID(static_cast<int>(line.id));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.muted, "%u", line.pot);
      ImGui::TableNextColumn();
      if (ImGui::Selectable(line.name.c_str(), false,
                            ImGuiSelectableFlags_SpanAllColumns))
        Navigation::openClub(guiView, line.id);
      if (UI::cell(mask, 2))
        UI::textFitted(line.association, ImGui::GetContentRegionAvail().x,
                       palette.muted);
      if (UI::cell(mask, 3))
        UI::textFitted(line.route, ImGui::GetContentRegionAvail().x,
                       palette.muted);
      if (UI::cell(mask, 4)) UI::textRight(line.coefficient.c_str());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void InternationalScene::renderBracket(float width) const
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("intl_bracket", LOC("INTL_KNOCKOUTS"), width);
  if (rounds.empty())
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.faint, "%s", LOC("INTL_KNOCKOUTS_EMPTY"));
    ImGui::PopTextWrapPos();
    UI::endCard();
    return;
  }
  // Rounds flow into as many columns as fit; ties never scroll sideways.
  const float available = ImGui::GetContentRegionAvail().x;
  const int per_row = std::max(
      1, static_cast<int>(available / (ROUND_CARD_MIN_WIDTH * Theme::scale())));
  if (const int columns =
          std::min<int>(per_row, static_cast<int>(rounds.size()));
      ImGui::BeginTable("intl_rounds", columns,
                        ImGuiTableFlags_SizingStretchSame))
  {
    for (const RoundBlock& block : rounds)
    {
      ImGui::TableNextColumn();
      UI::sectionLabel(block.title.c_str());
      for (size_t i = 0; i < block.ties.size(); ++i)
      {
        const TieLine& tie = block.ties[i];
        ImGui::PushID(static_cast<int>(i));
        const float cell = ImGui::GetContentRegionAvail().x;
        const float score_width = ImGui::CalcTextSize("00 - 00").x;
        const float name_width = std::max(
            0.0f, cell - score_width - ImGui::GetStyle().ItemSpacing.x);
        const auto nameColor = [&](int side)
        {
          if (tie.winner == 0) return tie.ours ? palette.text : palette.muted;
          return tie.winner == side ? palette.text : palette.faint;
        };
        UI::textFitted(tie.first, name_width, nameColor(1));
        ImGui::SameLine(cell - score_width);
        ImGui::TextUnformatted(tie.score.c_str());
        UI::textFitted(tie.second, name_width, nameColor(2));
        if (!tie.detail.empty())
        {
          Theme::ScopedText small(Theme::Text::SMALL);
          UI::textFitted(tie.detail, cell, palette.faint);
        }
        ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void InternationalScene::renderMatchday(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("intl_matchday", LOC("INTL_MATCHDAYS"), width);
  if (matchdays == 0 || fixtures_by_matchday.empty())
  {
    UI::endCard();
    return;
  }
  // Matchday stepper: previous / label / next (disabled at the ends).
  ImGui::BeginDisabled(matchday <= 1);
  if (UI::secondaryButton("<", ImVec2(0, 0), UI::ButtonSize::COMPACT))
    --matchday;
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(
      formatLocalized("INTL_MATCHDAY_OF",
                      {std::to_string(matchday), std::to_string(matchdays)})
          .c_str());
  ImGui::SameLine();
  ImGui::BeginDisabled(matchday >= matchdays);
  if (UI::secondaryButton(">", ImVec2(0, 0), UI::ButtonSize::COMPACT))
    ++matchday;
  ImGui::EndDisabled();
  const auto& lines = fixtures_by_matchday[static_cast<size_t>(
      std::clamp<int>(matchday, 1, matchdays) - 1)];
  const auto columns = localized(fixtureColumns());
  if (const UI::ColumnMask mask =
          UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
      UI::beginResponsiveTable(
          "intl_matchday_table", columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (size_t i = 0; i < lines.size(); ++i)
    {
      const FixtureLine& line = lines[i];
      ImGui::PushID(static_cast<int>(i));
      ImGui::TableNextRow();
      if (line.ours)
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,
                               Theme::toU32(palette.accent, 0.16f));
      if (UI::cell(mask, 0))
        ImGui::TextColored(palette.muted, "%s", line.when.c_str());
      if (UI::cell(mask, 1))
        UI::textFitted(line.home, ImGui::GetContentRegionAvail().x,
                       palette.text);
      if (UI::cell(mask, 2))
      {
        if (line.played)
        {
          if (UI::link(line.score.c_str(), "report"))
            Navigation::openMatchReport(guiView, line.date, line.home_id,
                                        line.away_id);
          if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("%s", LOC("INTL_OPEN_REPORT"));
        }
        else
        {
          ImGui::TextColored(palette.faint, "%s", line.score.c_str());
        }
      }
      if (UI::cell(mask, 3))
        UI::textFitted(line.away, ImGui::GetContentRegionAvail().x,
                       palette.text);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void InternationalScene::renderAssociations(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("intl_associations", LOC("INTL_ASSOCIATIONS"), width);
  if (ImGui::BeginTable("intl_association_table", 4,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
  {
    ImGui::TableSetupColumn("#");
    ImGui::TableSetupColumn(LOC("INTL_COL_ASSOCIATION"),
                            ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(LOC("INTL_COL_LAST_SEASON"));
    ImGui::TableSetupColumn(LOC("INTL_COL_TOTAL"));
    UI::staticHeadersRow();
    for (size_t i = 0; i < associations.size(); ++i)
    {
      const AssociationLine& line = associations[i];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.muted, "%zu", i + 1);
      ImGui::TableNextColumn();
      UI::textFitted(line.name, ImGui::GetContentRegionAvail().x, palette.text);
      ImGui::TableNextColumn();
      UI::textRight(line.last_season.c_str());
      ImGui::TableNextColumn();
      UI::textRight(line.total.c_str());
    }
    ImGui::EndTable();
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("INTL_ASSOCIATIONS_NOTE"));
  ImGui::PopTextWrapPos();
  UI::endCard();
}

void InternationalScene::renderNational()
{
  const Theme::Palette& palette = Theme::palette();
  UI::TileRow tiles(4);
  const float tile = tiles.width();
  const std::string count = std::to_string(called.size());
  tiles.next();
  UI::statTile("called", LOC("INTL_TILE_CALLED"), count.c_str(), nullptr,
               palette.text, tile);
  tiles.next();
  UI::statTile("next_intl", LOC("INTL_TILE_NEXT_INTL"),
               next_international.c_str(), nullptr, palette.text, tile);
  tiles.next();
  UI::statTile("cycle", LOC("INTL_TILE_CYCLE"), cycle.c_str(), nullptr,
               palette.text, tile);
  tiles.next();
  UI::statTile("champion", LOC("INTL_TILE_CHAMPION"), champion.c_str(), nullptr,
               palette.text, tile);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));

  bool two_columns = false;
  const auto [left, right] = columnWidths(two_columns);
  ImGui::BeginGroup();
  renderCalledUp(left);
  renderNationFixtures(left);
  ImGui::EndGroup();
  if (two_columns) ImGui::SameLine();
  ImGui::BeginGroup();
  renderGroups(right);
  renderLeaders(right);
  renderRanking(right);
  ImGui::EndGroup();
}

void InternationalScene::renderCalledUp(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("intl_called", LOC("INTL_CALLED_UP"), width);
  if (called.empty())
  {
    UI::emptyState(LOC("INTL_CALLED_EMPTY"), LOC("INTL_CALLED_EMPTY_BODY"));
    UI::endCard();
    return;
  }
  const auto columns = localized(calledColumns());
  if (const UI::ColumnMask mask =
          UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
      UI::beginResponsiveTable(
          "intl_called_table", columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (const CalledLine& line : called)
    {
      ImGui::PushID(static_cast<int>(line.id));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (ImGui::Selectable(line.name.c_str(), false,
                            ImGuiSelectableFlags_SpanAllColumns))
        Navigation::openPlayer(guiView, line.id);
      if (UI::cell(mask, 1))
        UI::textFitted(line.nation, ImGui::GetContentRegionAvail().x,
                       palette.muted);
      if (UI::cell(mask, 2)) UI::textRight(line.caps.c_str());
      if (UI::cell(mask, 3)) UI::textRight(line.goals.c_str());
      if (UI::cell(mask, 4))
        ImGui::TextColored(
            line.away ? palette.warning : palette.muted, "%s",
            LOC(line.away ? "INTL_STATUS_AWAY" : "INTL_STATUS_ANNOUNCED"));
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void InternationalScene::renderNationFixtures(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("intl_nation_fixtures", LOC("INTL_NATION_FIXTURES"),
                          width);
  const std::array<const char*, 2> filters = {LOC("INTL_FILTER_UPCOMING"),
                                              LOC("INTL_FILTER_RESULTS")};
  UI::segmented("##intl_fixture_filter", fixture_filter, filters);
  const auto& lines = fixture_filter == 0 ? upcoming : results;
  if (lines.empty())
  {
    ImGui::TextColored(palette.faint, "%s", LOC("INTL_NO_FIXTURES"));
    UI::endCard();
    return;
  }
  const auto columns = localized(nationFixtureColumns());
  if (const UI::ColumnMask mask =
          UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
      UI::beginResponsiveTable(
          "intl_nation_table", columns, mask,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
  {
    for (const NationFixture& line : lines)
    {
      ImGui::TableNextRow();
      if (UI::cell(mask, 0))
        ImGui::TextColored(palette.muted, "%s", line.when.c_str());
      if (UI::cell(mask, 1))
        UI::textFitted(line.home, ImGui::GetContentRegionAvail().x,
                       palette.text);
      if (UI::cell(mask, 2))
        ImGui::TextColored(line.played ? palette.text : palette.faint, "%s",
                           line.score.c_str());
      if (UI::cell(mask, 3))
        UI::textFitted(line.away, ImGui::GetContentRegionAvail().x,
                       palette.text);
      if (UI::cell(mask, 4))
        UI::textFitted(line.competition, ImGui::GetContentRegionAvail().x,
                       palette.muted);
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void InternationalScene::renderGroups(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const std::string title =
      groups_title.empty() ? std::string(LOC("INTL_GROUPS")) : groups_title;
  UI::beginAutoHeightCard("intl_groups", title.c_str(), width);
  if (groups.empty() && knockout_lines.empty())
  {
    ImGui::TextColored(palette.faint, "%s", LOC("INTL_NO_GROUPS"));
    UI::endCard();
    return;
  }
  const float available = ImGui::GetContentRegionAvail().x;
  const int per_row = std::max(
      1, static_cast<int>(available / (GROUP_CARD_MIN_WIDTH * Theme::scale())));
  if (const int columns =
          std::max(1, std::min<int>(per_row, static_cast<int>(groups.size())));
      !groups.empty() && ImGui::BeginTable("intl_group_grid", columns,
                                           ImGuiTableFlags_SizingStretchSame))
  {
    for (size_t g = 0; g < groups.size(); ++g)
    {
      const GroupBlock& block = groups[g];
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(g));
      UI::sectionLabel(block.title.c_str());
      if (ImGui::BeginTable(
              "group", 4,
              ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
      {
        ImGui::TableSetupColumn("nation", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("p");
        ImGui::TableSetupColumn("gd");
        ImGui::TableSetupColumn("pts");
        for (const GroupLine& row : block.rows)
        {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          UI::textFitted(
              row.nation, ImGui::GetContentRegionAvail().x,
              row.through && row.played > 0 ? palette.positive : palette.text);
          ImGui::TableNextColumn();
          ImGui::TextColored(palette.muted, "%u", row.played);
          ImGui::TableNextColumn();
          ImGui::TextColored(palette.muted, "%s",
                             Format::signedInt(row.goal_difference).c_str());
          ImGui::TableNextColumn();
          ImGui::Text("%u", row.points);
        }
        ImGui::EndTable();
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  for (const std::string& line : knockout_lines)
    UI::textFitted(line, ImGui::GetContentRegionAvail().x, palette.text);
  UI::endCard();
}

void InternationalScene::renderLeaders(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("intl_leaders", LOC("INTL_CAPS_LEADERS"), width);
  if (leaders.empty())
  {
    ImGui::TextColored(palette.faint, "%s", LOC("INTL_NO_CAPS"));
    UI::endCard();
    return;
  }
  if (ImGui::BeginTable("intl_leader_table", 4,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
  {
    ImGui::TableSetupColumn(LOC("INTL_COL_PLAYER"),
                            ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(LOC("INTL_COL_NATION"));
    ImGui::TableSetupColumn(LOC("INTL_COL_CAPS"));
    ImGui::TableSetupColumn(LOC("INTL_COL_GOALS"));
    UI::staticHeadersRow();
    for (const LeaderLine& line : leaders)
    {
      ImGui::PushID(static_cast<int>(line.id));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (line.active)
      {
        if (UI::link(line.name.c_str(), "leader"))
          Navigation::openPlayer(guiView, line.id);
      }
      else
      {
        UI::textFitted(line.name, ImGui::GetContentRegionAvail().x,
                       palette.muted);
      }
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.muted, "%s", line.nation.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%u", line.caps);
      ImGui::TableNextColumn();
      ImGui::Text("%u", line.goals);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

void InternationalScene::renderRanking(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("intl_ranking", LOC("INTL_RANKING"), width);
  if (ImGui::BeginTable("intl_ranking_table", 4,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
  {
    ImGui::TableSetupColumn("#");
    ImGui::TableSetupColumn(LOC("INTL_COL_NATION"),
                            ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(LOC("INTL_COL_COACH"),
                            ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(LOC("INTL_COL_RATING"));
    UI::staticHeadersRow();
    for (size_t i = 0; i < ranking.size(); ++i)
    {
      const RankingLine& line = ranking[i];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.muted, "%zu", i + 1);
      ImGui::TableNextColumn();
      UI::textFitted(line.nation, ImGui::GetContentRegionAvail().x,
                     palette.text);
      ImGui::TableNextColumn();
      UI::textFitted(line.coach, ImGui::GetContentRegionAvail().x,
                     palette.muted);
      ImGui::TableNextColumn();
      ImGui::Text("%d", line.rating);
    }
    ImGui::EndTable();
  }
  UI::endCard();
}

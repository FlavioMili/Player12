// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/data_hub_scene.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <optional>
#include <utility>

#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/match_tuning.h"
#include "model/role_utils.h"

namespace
{
constexpr float TREND_HEIGHT = 180.0f;
constexpr float TWO_COLUMN_MIN_WIDTH = 760.0f;
constexpr float SHOT_MAP_MAX_WIDTH = 420.0f;
constexpr float CHART_LEFT_AXIS = 36.0f;
constexpr float PERFORMANCE_HEIGHT = 150.0f;
constexpr float LEADER_COLUMN_MIN_WIDTH = 220.0f;

/** Second categorical series colour (orange), stepped for the surface. */
ImVec4 secondSeries()
{
  const ImVec4& surface = Theme::palette().surface;
  const float luminance =
      0.2126f * surface.x + 0.7152f * surface.y + 0.0722f * surface.z;
  return luminance > 0.5f ? ImVec4(0.922f, 0.408f, 0.204f, 1.0f)
                          : ImVec4(0.851f, 0.349f, 0.149f, 1.0f);
}

std::string lastName(const std::string& name)
{
  const auto space = name.find_last_of(' ');
  return space == std::string::npos ? name : name.substr(space + 1);
}

// Pitch proportions of the attacking half (metres).
constexpr float HALF_LENGTH = MatchTuning::Pitch::LENGTH_METRES * 0.5f;
constexpr float PITCH_WIDTH = MatchTuning::Pitch::WIDTH_METRES;
constexpr float BOX_DEPTH = 16.5f;
constexpr float BOX_WIDTH = 40.32f;
constexpr float SIX_DEPTH = 5.5f;
constexpr float SIX_WIDTH = 18.32f;

std::string decimal(float value) { return std::format("{:.2f}", value); }

std::string metricValue(HubMetric metric, float value)
{
  if (metric == HubMetric::PassCompletion || metric == HubMetric::SetPieceShare)
    return std::format("{:.0f}%", value);
  if (metric == HubMetric::Finishing || metric == HubMetric::GoalsPrevented)
    return std::format("{:+.2f}", value);
  if (metric == HubMetric::ShotsFor || metric == HubMetric::ShotsAgainst)
    return std::format("{:.1f}", value);
  return decimal(value);
}

const char* outcomeKey(ShotOutcome outcome)
{
  using enum ShotOutcome;
  switch (outcome)
  {
    case Goal:
      return "HUB_SHOT_GOAL";
    case Saved:
      return "HUB_SHOT_SAVED";
    case Blocked:
      return "HUB_SHOT_BLOCKED";
    case Woodwork:
      return "HUB_SHOT_WOODWORK";
    case OffTarget:
      break;
  }
  return "HUB_SHOT_OFF_TARGET";
}

void legendItem(const char* label, const ImVec4& color)
{
  const float size = ImGui::GetTextLineHeight();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddRectFilled(
      ImVec2(start.x, start.y + size * 0.45f),
      ImVec2(start.x + size, start.y + size * 0.45f + 2.0f * Theme::scale()),
      Theme::toU32(color));
  ImGui::Dummy(ImVec2(size, size));
  ImGui::SameLine();
  ImGui::TextUnformatted(label);
}

void footnote(const char* text)
{
  Theme::ScopedText small(Theme::Text::SMALL);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(Theme::palette().muted, "%s", text);
  ImGui::PopTextWrapPos();
}

void footnote(const std::string& text) { footnote(text.c_str()); }
}  // namespace

DataHubScene::DataHubScene(GUIView* parent) : ManagementScene(parent) {}

void DataHubScene::refresh()
{
  const GameController& controller = guiView->getController();
  hub = controller.getDataHub();
  opponent_names.clear();
  for (const TeamTrendPoint& point : hub.team.trend)
  {
    const auto team = controller.getTeamById(point.opponent);
    opponent_names.push_back(team ? team->get().getName() : std::string());
  }
  players.clear();
  const auto data = controller.getGameData();
  for (const PlayerAnalyticsRow& stats : hub.players)
  {
    PlayerRow row;
    row.id = stats.player;
    row.stats = stats;
    if (data)
    {
      if (const auto player = std::as_const(*data).getPlayer(stats.player))
      {
        row.name = player->get().getName();
        row.role = RoleUtils::shortName(player->get().getRole());
      }
    }
    players.push_back(std::move(row));
  }

  // Finishing and goalkeeping, cached per match.
  const TeamAnalytics& team = hub.team;
  performance_tooltips.clear();
  performance_top = 0.5f;
  for (std::size_t index = 0; index < team.trend.size(); ++index)
  {
    const TeamTrendPoint& point = team.trend[index];
    const float finishing = team.cumulative_finishing[index];
    const float prevention = team.cumulative_prevention[index];
    performance_top =
        std::max({performance_top, std::abs(finishing), std::abs(prevention)});
    performance_tooltips.push_back(fmt::sprintf(
        LOC("HUB_PERF_TOOLTIP"), Format::dayMonth(point.date).c_str(),
        opponent_names[index].c_str(), point.goals_for,
        decimal(point.xg_for).c_str(),
        std::format("{:+.2f}", finishing).c_str(), point.goals_against,
        decimal(point.xg_against).c_str(),
        std::format("{:+.2f}", prevention).c_str()));
  }
  performance_top = std::ceil(performance_top * 2.0f) / 2.0f;

  // Leaderboards over the tracked matches.
  leaders_note =
      fmt::sprintf(LOC("HUB_LEADERS_NOTE"), DataHub::LEADER_MIN_MINUTES);
  for (std::size_t metric = 0; metric < LEADER_METRIC_COUNT; ++metric)
  {
    const auto which = static_cast<LeaderMetric>(metric);
    leaders[metric].clear();
    for (const std::size_t index : DataHub::leaders(hub.players, which))
    {
      const PlayerAnalyticsRow& stats = hub.players[index];
      const float value = DataHub::leaderValue(stats, which);
      const int minutes = DataHub::leaderMinutes(stats, which);
      const bool counted = which == LeaderMetric::ProgressivePasses ||
                           which == LeaderMetric::Pressures;
      LeaderRow row;
      row.id = stats.player;
      row.name =
          players[index].name.empty() ? std::string("–") : players[index].name;
      row.short_name = lastName(row.name);
      row.value = counted ? std::format("{:.0f}", value) : decimal(value);
      row.tooltip = fmt::sprintf(
          LOC("HUB_LEADER_TOOLTIP"),
          decimal(PlayerAnalyticsRow::per90(value, minutes)).c_str(), minutes);
      leaders[metric].push_back(std::move(row));
    }
  }
}

void DataHubScene::renderContent()
{
  const std::string subtitle = fmt::sprintf(
      LOC("HUB_SUBTITLE"), guiView->getController().getCurrentSeason(),
      hub.team.trend.size(), hub.team.tracked_matches);
  UI::pageHeader(LOC("HUB_TITLE"), subtitle.c_str());
  const std::array<const char*, 2> tabs = {LOC("HUB_TAB_TEAM"),
                                           LOC("HUB_TAB_PLAYERS")};
  UI::segmented("##hub_tab", tab, tabs);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  if (tab == 0)
    renderTeam();
  else
    renderPlayers();
}

void DataHubScene::renderTeam()
{
  const TeamAnalytics& team = hub.team;
  if (!team.hasEnoughMatches())
  {
    const std::string body = fmt::sprintf(
        LOC("HUB_EMPTY_BODY"), DataHub::MIN_MATCHES, team.trend.size());
    UI::emptyState(LOC("HUB_EMPTY_TITLE"), body.c_str());
    return;
  }
  const Theme::Palette& palette = Theme::palette();
  const std::array<HubMetric, 4> tiles = {
      HubMetric::XgFor, HubMetric::XgAgainst, HubMetric::ShotsFor,
      HubMetric::PassCompletion};
  UI::TileRow row(4);
  for (const HubMetric metric : tiles)
  {
    row.next();
    const MetricComparison& value =
        team.metrics[static_cast<std::size_t>(std::to_underlying(metric))];
    const bool league = team.team_league_matches > 0;
    const std::string text =
        league ? metricValue(metric, value.team) : std::string("–");
    std::string note;
    if (!league)
      note = LOC("HUB_TILE_NO_LEAGUE");
    else if (value.rank > 0)
      note = fmt::sprintf(LOC("HUB_TILE_NOTE"),
                          metricValue(metric, value.league).c_str(), value.rank,
                          value.ranked_teams);
    else
      note = fmt::sprintf(LOC("HUB_TILE_NOTE_UNRANKED"),
                          metricValue(metric, value.league).c_str());
    ImGui::PushID(static_cast<int>(std::to_underlying(metric)));
    UI::statTile("hub_tile", LOC(hubMetricKey(metric)), text.c_str(),
                 note.c_str(), palette.text, row.width());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
      ImGui::SetTooltip("%s", LOC(hubMetricHelpKey(metric)));
    ImGui::PopID();
  }

  const float available = ImGui::GetContentRegionAvail().x;
  renderTrendChart(available);
  renderPerformance(available);
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  if (available >= TWO_COLUMN_MIN_WIDTH * Theme::scale())
  {
    const float left = std::min(std::floor((available - gap) * 0.5f),
                                SHOT_MAP_MAX_WIDTH * Theme::scale());
    ImGui::BeginGroup();
    renderShotMap(left);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    const float right = available - left - gap;
    renderSetPieces(right);
    renderComparison(right);
    ImGui::EndGroup();
  }
  else
  {
    renderShotMap(std::min(available, SHOT_MAP_MAX_WIDTH * Theme::scale()));
    renderSetPieces(available);
    renderComparison(available);
  }
  renderLeaders(available);
}

void DataHubScene::renderPerformance(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const TeamAnalytics& team = hub.team;
  UI::beginAutoHeightCard("hub_performance", LOC("HUB_PERF_TITLE"), width);
  legendItem(LOC("HUB_PERF_FINISHING"), palette.info);
  ImGui::SameLine(0.0f, Theme::Space::L * scale);
  legendItem(LOC("HUB_PERF_PREVENTION"), secondSeries());

  const float plotWidth = ImGui::GetContentRegionAvail().x;
  const float plotHeight = PERFORMANCE_HEIGHT * scale;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  // Hover shows the match under the cursor; a click opens its report.
  const bool clicked = ImGui::InvisibleButton("##performance_plot",
                                              ImVec2(plotWidth, plotHeight));
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const float left = origin.x + CHART_LEFT_AXIS * scale;
  const float right = origin.x + plotWidth - Theme::Space::S * scale;
  const float top = origin.y + Theme::Space::S * scale;
  const float bottom = origin.y + plotHeight - Theme::Space::S * scale;
  const float zero = (top + bottom) * 0.5f;
  const auto yOf = [&](float value)
  {
    return zero -
           (zero - top) * std::clamp(value / performance_top, -1.0f, 1.0f);
  };
  const std::size_t count = team.trend.size();
  const auto xOf = [&](std::size_t index)
  {
    return count <= 1 ? (left + right) * 0.5f
                      : left + (right - left) * static_cast<float>(index) /
                                   static_cast<float>(count - 1);
  };
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    for (int step = -1; step <= 1; ++step)
    {
      const float value = performance_top * static_cast<float>(step);
      const float y = yOf(value);
      drawList->AddLine(
          ImVec2(left, y), ImVec2(right, y),
          Theme::toU32(step == 0 ? palette.muted : palette.border), 1.0f);
      std::array<char, 16> label{};
      const auto formatted = std::format_to_n(
          label.data(), static_cast<std::ptrdiff_t>(label.size() - 1),
          "{:+.1f}", value);
      label[std::min(static_cast<std::size_t>(formatted.size),
                     label.size() - 1)] = '\0';
      drawList->AddText(ImVec2(origin.x, y - ImGui::GetTextLineHeight() * 0.5f),
                        Theme::toU32(palette.muted),
                        step == 0 ? "0" : label.data());
    }
  }
  const auto series = [&](const std::vector<float>& values, const ImVec4& color)
  {
    for (std::size_t index = 0; index < values.size(); ++index)
    {
      drawList->AddCircleFilled(ImVec2(xOf(index), yOf(values[index])),
                                2.5f * scale, Theme::toU32(color));
      if (index > 0)
        drawList->AddLine(ImVec2(xOf(index - 1), yOf(values[index - 1])),
                          ImVec2(xOf(index), yOf(values[index])),
                          Theme::toU32(color), 2.0f * scale);
    }
  };
  series(team.cumulative_finishing, palette.info);
  series(team.cumulative_prevention, secondSeries());
  if (hovered && count > 0)
  {
    const float mouse = ImGui::GetIO().MousePos.x;
    std::size_t nearest = 0;
    for (std::size_t index = 1; index < count; ++index)
      if (std::abs(xOf(index) - mouse) < std::abs(xOf(nearest) - mouse))
        nearest = index;
    drawList->AddLine(ImVec2(xOf(nearest), top), ImVec2(xOf(nearest), bottom),
                      Theme::toU32(palette.faint), 1.0f);
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetTooltip("%s\n%s", performance_tooltips[nearest].c_str(),
                      LOC("FIXTURES_OPEN_REPORT_HINT"));
    const auto managed = guiView->getController().getManagedTeam();
    if (clicked && managed)
    {
      const TeamTrendPoint& point = team.trend[nearest];
      const TeamID club = managed->get().getId();
      Navigation::openMatchReport(guiView, point.date,
                                  point.home ? club : point.opponent,
                                  point.home ? point.opponent : club);
    }
  }
  footnote(LOC("HUB_PERF_NOTE"));
  UI::endCard();
}

void DataHubScene::renderLeaders(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  UI::beginAutoHeightCard("hub_leaders", LOC("HUB_LEADERS_TITLE"), width);
  if (const bool any =
          std::ranges::any_of(leaders, [](const std::vector<LeaderRow>& rows)
                              { return !rows.empty(); });
      !any)
  {
    footnote(LOC("HUB_LEADERS_NONE"));
    UI::endCard();
    return;
  }
  const float inner = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x * 2.0f;
  const float minimum = LEADER_COLUMN_MIN_WIDTH * scale;
  std::size_t columns = 1;
  if (inner >= 4.0f * minimum + 3.0f * gap)
    columns = 4;
  else if (inner >= 2.0f * minimum + gap)
    columns = 2;
  const float columnWidth =
      std::floor((inner - gap * static_cast<float>(columns - 1)) /
                 static_cast<float>(columns));
  const float startX = ImGui::GetCursorPosX();
  for (std::size_t metric = 0; metric < LEADER_METRIC_COUNT; ++metric)
  {
    if (metric % columns != 0)
      ImGui::SameLine(startX + static_cast<float>(metric % columns) *
                                   (columnWidth + gap));
    ImGui::PushID(static_cast<int>(metric));
    ImGui::BeginGroup();
    const float x0 = ImGui::GetCursorPosX();
    {
      Theme::ScopedText caption(Theme::Text::CAPTION);
      ImGui::TextColored(
          palette.muted, "%s",
          LOC(leaderMetricKey(static_cast<LeaderMetric>(metric))));
    }
    if (leaders[metric].empty()) ImGui::TextColored(palette.faint, "%s", "–");
    for (std::size_t rank = 0; rank < leaders[metric].size(); ++rank)
    {
      const LeaderRow& row = leaders[metric][rank];
      ImGui::PushID(static_cast<int>(rank));
      ImGui::SetCursorPosX(x0);
      ImGui::TextColored(palette.faint, "%zu", rank + 1);
      // Inside a group SameLine() offsets count from the group's start.
      ImGui::SameLine(ImGui::CalcTextSize("00").x);
      const float valueWidth = ImGui::CalcTextSize(row.value.c_str()).x;
      const float nameRoom = columnWidth - ImGui::CalcTextSize("00").x -
                             valueWidth - ImGui::GetStyle().ItemSpacing.x;
      if (const std::string& label =
              ImGui::CalcTextSize(row.name.c_str()).x <= nameRoom
                  ? row.name
                  : row.short_name;
          UI::link(label.c_str(), "leader"))
        Navigation::openPlayer(guiView, row.id);
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s\n%s", row.name.c_str(), row.tooltip.c_str());
      ImGui::SameLine(columnWidth - valueWidth);
      ImGui::TextUnformatted(row.value.c_str());
      ImGui::PopID();
    }
    ImGui::EndGroup();
    ImGui::PopID();
  }
  footnote(leaders_note);
  UI::endCard();
}

void DataHubScene::renderTrendChart(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const TeamAnalytics& team = hub.team;
  UI::beginAutoHeightCard("hub_trend", LOC("HUB_TREND_TITLE"), width);
  legendItem(LOC("HUB_TREND_FOR"), palette.info);
  ImGui::SameLine(0.0f, Theme::Space::L * scale);
  legendItem(LOC("HUB_TREND_AGAINST"), palette.warning);

  const float plotWidth = ImGui::GetContentRegionAvail().x;
  const float plotHeight = TREND_HEIGHT * scale;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  // Hover shows the match under the cursor; a click opens its report.
  const bool clicked =
      ImGui::InvisibleButton("##trend_plot", ImVec2(plotWidth, plotHeight));
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();

  float top = 0.5f;
  for (const TeamTrendPoint& point : team.trend)
    top = std::max({top, point.xg_for, point.xg_against});
  top = std::ceil(top * 2.0f) / 2.0f;
  const float left = origin.x + CHART_LEFT_AXIS * scale;
  const float right = origin.x + plotWidth - Theme::Space::S * scale;
  const float bottom = origin.y + plotHeight - ImGui::GetTextLineHeight();
  const float ceiling = origin.y + Theme::Space::S * scale;
  const auto yOf = [&](float value)
  { return bottom - (bottom - ceiling) * std::clamp(value / top, 0.0f, 1.0f); };
  const std::size_t count = team.trend.size();
  const auto xOf = [&](std::size_t index)
  {
    return count <= 1 ? (left + right) * 0.5f
                      : left + (right - left) * static_cast<float>(index) /
                                   static_cast<float>(count - 1);
  };

  // Recessive grid with axis labels in text ink.
  std::optional<Theme::ScopedText> small;
  small.emplace(Theme::Text::SMALL);
  for (int step = 0; step <= 2; ++step)
  {
    const float value = top * static_cast<float>(step) / 2.0f;
    const float y = yOf(value);
    drawList->AddLine(ImVec2(left, y), ImVec2(right, y),
                      Theme::toU32(palette.border), 1.0f);
    const std::string label = std::format("{:.1f}", value);
    drawList->AddText(ImVec2(origin.x, y - ImGui::GetTextLineHeight() * 0.5f),
                      Theme::toU32(palette.muted), label.c_str());
  }
  const std::string first = Format::dayMonth(team.trend.front().date);
  const std::string last = Format::dayMonth(team.trend.back().date);
  drawList->AddText(ImVec2(left, bottom + 2.0f * scale),
                    Theme::toU32(palette.muted), first.c_str());
  drawList->AddText(ImVec2(right - ImGui::CalcTextSize(last.c_str()).x,
                           bottom + 2.0f * scale),
                    Theme::toU32(palette.muted), last.c_str());

  const auto series =
      [&](const std::vector<float>& rolling, bool against, const ImVec4& color)
  {
    for (std::size_t index = 0; index < count; ++index)
    {
      // Match values as small markers, the rolling mean as the line.
      const float raw =
          against ? team.trend[index].xg_against : team.trend[index].xg_for;
      drawList->AddCircleFilled(ImVec2(xOf(index), yOf(raw)), 2.5f * scale,
                                Theme::toU32(color, 0.45f));
      if (index > 0)
        drawList->AddLine(ImVec2(xOf(index - 1), yOf(rolling[index - 1])),
                          ImVec2(xOf(index), yOf(rolling[index])),
                          Theme::toU32(color), 2.0f * scale);
    }
  };
  series(team.rolling_xg_for, false, palette.info);
  series(team.rolling_xg_against, true, palette.warning);

  const auto nearestToMouse = [&]()
  {
    const float mouse = ImGui::GetIO().MousePos.x;
    std::size_t nearest = 0;
    for (std::size_t index = 1; index < count; ++index)
      if (std::abs(xOf(index) - mouse) < std::abs(xOf(nearest) - mouse))
        nearest = index;
    return nearest;
  };
  if (hovered && count > 0)
  {
    const std::size_t nearest = nearestToMouse();
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    drawList->AddLine(ImVec2(xOf(nearest), ceiling),
                      ImVec2(xOf(nearest), bottom), Theme::toU32(palette.faint),
                      1.0f);
    const TeamTrendPoint& point = team.trend[nearest];
    ImGui::SetTooltip(
        "%s\n%s",
        fmt::sprintf(LOC("HUB_TREND_TOOLTIP"),
                     Format::dayMonth(point.date).c_str(),
                     opponent_names[nearest].c_str(), point.goals_for,
                     point.goals_against, decimal(point.xg_for).c_str(),
                     decimal(point.xg_against).c_str(),
                     decimal(team.rolling_xg_for[nearest]).c_str(),
                     decimal(team.rolling_xg_against[nearest]).c_str())
            .c_str(),
        LOC("FIXTURES_OPEN_REPORT_HINT"));
  }
  if (clicked && count > 0)
  {
    const auto managed = guiView->getController().getManagedTeam();
    if (managed)
    {
      const TeamTrendPoint& point = team.trend[nearestToMouse()];
      const TeamID club = managed->get().getId();
      Navigation::openMatchReport(guiView, point.date,
                                  point.home ? club : point.opponent,
                                  point.home ? point.opponent : club);
    }
  }
  small.reset();
  footnote(fmt::sprintf(LOC("HUB_TREND_NOTE"), DataHub::ROLLING_WINDOW,
                        team.trend.size()));
  UI::endCard();
}

void DataHubScene::renderShotMap(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const TeamAnalytics& team = hub.team;
  UI::beginAutoHeightCard("hub_shots", LOC("HUB_SHOTS_TITLE"), width);
  const std::array<const char*, 2> sides = {LOC("HUB_SHOTS_FOR"),
                                            LOC("HUB_SHOTS_AGAINST")};
  UI::segmented("##shot_side", shot_side, sides,
                ImGui::GetContentRegionAvail().x);
  const std::vector<ShotRecord>& shots =
      shot_side == 0 ? team.shots_for : team.shots_against;
  const ImVec4& color = shot_side == 0 ? palette.info : palette.warning;

  const float mapWidth = ImGui::GetContentRegionAvail().x;
  const float mapHeight = mapWidth * HALF_LENGTH / PITCH_WIDTH;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##shot_map", ImVec2(mapWidth, mapHeight));
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImU32 line = Theme::toU32(palette.border);
  const float thickness = 1.5f * scale;
  // Attacking half with the goal at the top.
  drawList->AddRect(origin, ImVec2(origin.x + mapWidth, origin.y + mapHeight),
                    line, 0.0f, 0, thickness);
  const auto box = [&](float depth, float boxWidth)
  {
    const float w = mapWidth * boxWidth / PITCH_WIDTH;
    const float h = mapHeight * depth / HALF_LENGTH;
    const float x = origin.x + (mapWidth - w) * 0.5f;
    drawList->AddRect(ImVec2(x, origin.y), ImVec2(x + w, origin.y + h), line,
                      0.0f, 0, thickness);
  };
  box(BOX_DEPTH, BOX_WIDTH);
  box(SIX_DEPTH, SIX_WIDTH);
  drawList->AddCircle(ImVec2(origin.x + mapWidth * 0.5f, origin.y + mapHeight),
                      mapWidth * 9.15f / PITCH_WIDTH, line, 0, thickness);

  const auto position = [&](const ShotRecord& shot)
  {
    const float depth = std::clamp((1.0f - shot.x) * 2.0f, 0.0f, 1.0f);
    return ImVec2(origin.x + mapWidth * shot.y, origin.y + mapHeight * depth);
  };
  const ShotRecord* nearest = nullptr;
  float nearestDistance = 12.0f * scale;
  float totalXg = 0.0f;
  int goals = 0;
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  for (const ShotRecord& shot : shots)
  {
    const ImVec2 at = position(shot);
    const float radius = (3.0f + 10.0f * std::sqrt(shot.xg)) * scale;
    totalXg += shot.xg;
    if (shot.outcome == ShotOutcome::Goal)
    {
      ++goals;
      drawList->AddCircleFilled(at, radius, Theme::toU32(color));
      drawList->AddCircle(at, radius, Theme::toU32(palette.surface), 0,
                          2.0f * scale);
    }
    else
    {
      drawList->AddCircle(at, radius, Theme::toU32(color, 0.8f), 0,
                          1.5f * scale);
    }
    const float distance = std::hypot(at.x - mouse.x, at.y - mouse.y);
    if (hovered && distance < std::max(nearestDistance, radius))
    {
      nearest = &shot;
      nearestDistance = distance;
    }
  }
  if (nearest != nullptr)
    ImGui::SetTooltip("%s", fmt::sprintf(LOC("HUB_SHOT_TOOLTIP"),
                                         static_cast<int>(nearest->minute) + 1,
                                         decimal(nearest->xg).c_str(),
                                         LOC(outcomeKey(nearest->outcome)))
                                .c_str());
  footnote(fmt::sprintf(LOC("HUB_SHOTS_NOTE"), shots.size(), goals,
                        decimal(totalXg).c_str(), team.tracked_matches));
  UI::endCard();
}

void DataHubScene::renderSetPieces(float width)
{
  const SetPieceSummary& set = hub.team.set_pieces;
  UI::beginAutoHeightCard("hub_set_pieces", LOC("HUB_SET_PIECES_TITLE"), width);
  if (set.matches == 0)
  {
    footnote(LOC("HUB_SET_PIECES_NONE"));
    UI::endCard();
    return;
  }
  const auto matches = static_cast<float>(set.matches);
  const std::string shots =
      std::format("{:.1f}", static_cast<float>(set.shots_for) / matches);
  const std::string share =
      set.all_shots_for > 0
          ? std::format("{:.0f}%", 100.0f * static_cast<float>(set.shots_for) /
                                       static_cast<float>(set.all_shots_for))
          : std::string("–");
  const std::string goals = std::to_string(set.goals_for);
  const std::string corners =
      std::format("{:.1f}", static_cast<float>(set.corners_for) / matches);
  const std::string against =
      fmt::sprintf(LOC("HUB_SET_PIECES_AGAINST_VALUE"), set.shots_against,
                   set.goals_against);
  UI::summaryRow(LOC("HUB_SET_PIECES_SHOTS"), shots.c_str());
  UI::summaryRow(LOC("HUB_SET_PIECES_SHARE"), share.c_str());
  UI::summaryRow(LOC("HUB_SET_PIECES_GOALS"), goals.c_str());
  UI::summaryRow(LOC("HUB_SET_PIECES_CORNERS"), corners.c_str());
  UI::summaryRow(LOC("HUB_SET_PIECES_AGAINST"), against.c_str());
  footnote(fmt::sprintf(LOC("HUB_SET_PIECES_NOTE"), set.matches));
  UI::endCard();
}

void DataHubScene::renderComparison(float width)
{
  const Theme::Palette& palette = Theme::palette();
  const TeamAnalytics& team = hub.team;
  UI::beginAutoHeightCard("hub_league", LOC("HUB_LEAGUE_TITLE"), width);
  if (team.team_league_matches == 0)
  {
    footnote(LOC("HUB_TILE_NO_LEAGUE"));
    UI::endCard();
    return;
  }
  static constexpr std::array<UI::Column, 4> COLUMNS = {
      {{"HUB_COL_METRIC", 0.0f, 0},
       {"HUB_COL_CLUB", 64.0f, 0},
       {"HUB_COL_LEAGUE", 64.0f, 1},
       {"HUB_COL_RANK", 64.0f, 0}}};
  std::array<UI::Column, 4> columns{};
  for (std::size_t index = 0; index < columns.size(); ++index)
  {
    columns[index] = COLUMNS[index];
    columns[index].label = LOC(COLUMNS[index].label);
  }
  if (const UI::ColumnMask mask =
          UI::fitColumns(columns, ImGui::GetContentRegionAvail().x, 120.0f);
      UI::beginResponsiveTable("hub_league_table", columns, mask,
                               ImGuiTableFlags_RowBg))
  {
    for (std::size_t index = 0; index < HUB_METRIC_COUNT; ++index)
    {
      const auto metric = static_cast<HubMetric>(index);
      const MetricComparison& value = team.metrics[index];
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(LOC(hubMetricKey(metric)));
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", LOC(hubMetricHelpKey(metric)));
      if (UI::cell(mask, 1))
        UI::textRight(metricValue(metric, value.team).c_str());
      if (UI::cell(mask, 2))
        UI::textRightColored(palette.muted,
                             metricValue(metric, value.league).c_str());
      if (UI::cell(mask, 3))
      {
        const std::string rank =
            value.rank > 0
                ? fmt::sprintf("%d / %d", value.rank, value.ranked_teams)
                : std::string("–");
        UI::textRight(rank.c_str());
      }
    }
    ImGui::EndTable();
  }
  footnote(fmt::sprintf(LOC("HUB_LEAGUE_NOTE"), team.league_matches));
  UI::endCard();
}

void DataHubScene::renderPlayers()
{
  const Theme::Palette& palette = Theme::palette();
  if (players.empty())
  {
    UI::emptyState(LOC("HUB_PLAYERS_EMPTY_TITLE"),
                   LOC("HUB_PLAYERS_EMPTY_BODY"));
    return;
  }
  UI::beginAutoHeightCard("hub_players", nullptr, 0.0f);
  static constexpr std::array<UI::Column, 14> COLUMNS = {
      {{"HUB_COL_PLAYER", 0.0f, 0},
       {"HUB_COL_APPS", 44.0f, 3},
       {"HUB_COL_MINUTES", 56.0f, 2},
       {"HUB_COL_GOALS90", 56.0f, 1},
       {"HUB_COL_ASSISTS90", 56.0f, 2},
       {"HUB_COL_XG90", 56.0f, 3},
       {"HUB_COL_KEY90", 60.0f, 4},
       {"HUB_COL_XA90", 56.0f, 4},
       {"HUB_COL_PROG90", 60.0f, 5},
       {"HUB_COL_PRESS90", 60.0f, 5},
       {"HUB_COL_PASS", 56.0f, 4},
       {"HUB_COL_SHARE", 60.0f, 5},
       {"HUB_COL_RATING", 56.0f, 1},
       {"HUB_COL_TREND", 96.0f, 3}}};
  constexpr std::size_t COLUMN_COUNT = COLUMNS.size();
  std::array<UI::Column, COLUMN_COUNT> columns{};
  for (std::size_t index = 0; index < COLUMN_COUNT; ++index)
  {
    columns[index] = COLUMNS[index];
    columns[index].label = LOC(COLUMNS[index].label);
  }
  const UI::ColumnMask mask =
      UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
  const auto per90 = [](float value, int minutes)
  {
    return minutes > 0 ? std::format("{:.2f}",
                                     PlayerAnalyticsRow::per90(value, minutes))
                       : std::string("–");
  };
  if (UI::beginResponsiveTable("hub_player_table", columns, mask,
                               ImGuiTableFlags_RowBg))
  {
    for (const PlayerRow& row : players)
    {
      const PlayerAnalyticsRow& s = row.stats;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(row.id));
      // Players no longer in the world (released youth) have no profile.
      if (row.name.empty())
        ImGui::TextColored(palette.muted, "%s", "–");
      else if (UI::link(row.name.c_str(), "player"))
        Navigation::openPlayer(guiView, row.id);
      ImGui::SameLine();
      ImGui::TextColored(palette.faint, "%s", row.role.c_str());
      if (UI::cell(mask, 1))
        UI::textRight(std::to_string(s.appearances).c_str());
      if (UI::cell(mask, 2)) UI::textRight(std::to_string(s.minutes).c_str());
      if (UI::cell(mask, 3))
        UI::textRight(per90(static_cast<float>(s.goals), s.minutes).c_str());
      if (UI::cell(mask, 4))
        UI::textRight(per90(static_cast<float>(s.assists), s.minutes).c_str());
      if (UI::cell(mask, 5))
        UI::textRight(per90(s.xg, s.tracked_minutes).c_str());
      if (UI::cell(mask, 6))
        UI::textRight(
            per90(static_cast<float>(s.key_passes), s.tracked_minutes).c_str());
      if (UI::cell(mask, 7))
        UI::textRight(per90(s.xa, s.detail_minutes).c_str());
      if (UI::cell(mask, 8))
        UI::textRight(
            per90(static_cast<float>(s.progressive_passes), s.detail_minutes)
                .c_str());
      if (UI::cell(mask, 9))
        UI::textRight(
            per90(static_cast<float>(s.pressures), s.detail_minutes).c_str());
      if (UI::cell(mask, 10))
      {
        const std::string pass =
            s.passes_attempted > 0
                ? std::format("{:.0f}%",
                              100.0f * static_cast<float>(s.passes_completed) /
                                  static_cast<float>(s.passes_attempted))
                : std::string("–");
        UI::textRight(pass.c_str());
      }
      if (UI::cell(mask, 11))
      {
        const std::string share =
            s.tracked_minutes > 0
                ? std::format("{:.0f}%", 100.0f * s.pass_share)
                : std::string("–");
        UI::textRight(share.c_str());
      }
      if (UI::cell(mask, 12))
      {
        if (s.rated_matches > 0)
          UI::ratingChip(s.average_rating);
        else
          UI::textRight("–");
      }
      if (UI::cell(mask, 13) && s.ratings.size() >= 2)
      {
        UI::sparkline(
            "##trend", s.ratings,
            ImVec2(ImGui::GetContentRegionAvail().x,
                   ImGui::GetTextLineHeight()),
            s.rating_trend >= 0.0f ? palette.positive : palette.negative);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
          ImGui::SetTooltip("%s",
                            fmt::sprintf(LOC("HUB_TREND_PLAYER_TOOLTIP"),
                                         s.rating_trend >= 0.0f ? "+" : "",
                                         s.rating_trend, s.ratings.size())
                                .c_str());
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  footnote(LOC("HUB_PLAYERS_NOTE"));
  UI::endCard();
}

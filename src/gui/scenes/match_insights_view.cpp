// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/match_insights_view.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/onboarding_overlay.h"
#include "gui/view_models/match_clock.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/match_tuning.h"

namespace
{
namespace T = MatchTracking;

constexpr float TWO_COLUMN_MIN_WIDTH = 860.0f;
constexpr float TIMELINE_HEIGHT = 230.0f;
constexpr float AXIS_LEFT = 34.0f;
/** Share of the timeline height for the xG race (the rest is pressure). */
constexpr float XG_SHARE = 0.6f;
constexpr float PITCH_ASPECT =
    MatchTuning::Pitch::WIDTH_METRES / MatchTuning::Pitch::LENGTH_METRES;
/** Pass links shown in the network (fewer are noise). */
constexpr int MIN_LINK = 3;
constexpr float HIT_RADIUS = 12.0f;

float scaled(float value) { return value * Theme::scale(); }

/** Categorical team colours: home blue, away orange (stepped for light and
 * dark surfaces). */
ImVec4 sideColor(std::size_t side)
{
  const Theme::Palette& palette = Theme::palette();
  if (side == 0) return palette.info;
  const ImVec4& surface = palette.surface;
  const float luminance =
      0.2126f * surface.x + 0.7152f * surface.y + 0.0722f * surface.z;
  return luminance > 0.5f ? ImVec4(0.922f, 0.408f, 0.204f, 1.0f)
                          : ImVec4(0.851f, 0.349f, 0.149f, 1.0f);
}

int regulationEnd(int period)
{
  const int clamped = std::clamp(period, 1, 4);
  return clamped <= 2 ? 45 * clamped : 90 + 15 * (clamped - 2);
}

/** Minute on the timeline: added time stays at the end of its period. */
float axisMinute(float minute, int period)
{
  return std::min(minute, static_cast<float>(regulationEnd(period)));
}

std::string lastName(const std::string& name)
{
  const auto space = name.find_last_of(' ');
  return space == std::string::npos ? name : name.substr(space + 1);
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

const char* momentKey(KeyMoment::Kind kind)
{
  using enum KeyMoment::Kind;
  switch (kind)
  {
    case Goal:
      return "INSIGHT_MOMENT_GOAL";
    case OwnGoal:
      return "INSIGHT_MOMENT_OWN_GOAL";
    case BigChance:
      return "INSIGHT_MOMENT_BIG_CHANCE";
    case Woodwork:
      return "INSIGHT_MOMENT_WOODWORK";
    case Yellow:
      return "INSIGHT_MOMENT_YELLOW";
    case SecondYellow:
      return "INSIGHT_MOMENT_SECOND_YELLOW";
    case Red:
      return "INSIGHT_MOMENT_RED";
    case Substitution:
      break;
  }
  return "INSIGHT_MOMENT_SUBSTITUTION";
}

void footnote(const char* text)
{
  Theme::ScopedText small(Theme::Text::SMALL);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(Theme::palette().muted, "%s", text);
  ImGui::PopTextWrapPos();
}

void footnote(const std::string& text) { footnote(text.c_str()); }

void legendSwatch(const char* label, const ImVec4& color, bool line)
{
  const float size = ImGui::GetTextLineHeight();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  if (line)
    drawList->AddRectFilled(
        ImVec2(start.x, start.y + size * 0.45f),
        ImVec2(start.x + size, start.y + size * 0.45f + scaled(2.0f)),
        Theme::toU32(color));
  else
    drawList->AddCircleFilled(
        ImVec2(start.x + size * 0.5f, start.y + size * 0.5f), size * 0.3f,
        Theme::toU32(color));
  ImGui::Dummy(ImVec2(size, size));
  ImGui::SameLine();
  ImGui::TextUnformatted(label);
}

/** Pitch markings, horizontal, home goal on the left. */
void drawPitch(ImDrawList* drawList, ImVec2 min, ImVec2 size, bool half_line)
{
  const Theme::Palette& palette = Theme::palette();
  const ImU32 grass = Theme::toU32(palette.raised);
  const ImU32 line = Theme::toU32(palette.border);
  const float thickness = scaled(1.25f);
  const ImVec2 max(min.x + size.x, min.y + size.y);
  drawList->AddRectFilled(min, max, grass, scaled(3.0f));
  drawList->AddRect(min, max, line, scaled(3.0f), 0, thickness);
  constexpr float L = MatchTuning::Pitch::LENGTH_METRES;
  constexpr float W = MatchTuning::Pitch::WIDTH_METRES;
  const auto px = [&](float metres) { return size.x * metres / L; };
  const auto py = [&](float metres) { return size.y * metres / W; };
  if (half_line)
  {
    const float mid = min.x + size.x * 0.5f;
    drawList->AddLine(ImVec2(mid, min.y), ImVec2(mid, max.y), line, thickness);
    drawList->AddCircle(ImVec2(mid, min.y + size.y * 0.5f), px(9.15f), line, 0,
                        thickness);
  }
  const auto box = [&](float depth, float width, bool left)
  {
    const float top = min.y + (size.y - py(width)) * 0.5f;
    const float x0 = left ? min.x : max.x - px(depth);
    drawList->AddRect(ImVec2(x0, top), ImVec2(x0 + px(depth), top + py(width)),
                      line, 0.0f, 0, thickness);
  };
  for (const bool left : {true, false})
  {
    box(16.5f, 40.32f, left);
    box(5.5f, 18.32f, left);
  }
}

ImVec2 onPitch(ImVec2 min, ImVec2 size, float x, float y)
{
  return ImVec2(min.x + size.x * x, min.y + size.y * y);
}

float segmentDistance(ImVec2 point, ImVec2 a, ImVec2 b)
{
  const float dx = b.x - a.x;
  const float dy = b.y - a.y;
  const float length = dx * dx + dy * dy;
  const float t =
      length > 0.0f
          ? std::clamp(((point.x - a.x) * dx + (point.y - a.y) * dy) / length,
                       0.0f, 1.0f)
          : 0.0f;
  return std::hypot(point.x - (a.x + t * dx), point.y - (a.y + t * dy));
}
}  // namespace

// ---- View model
// --------------------------------------------------------------

void MatchInsightsView::build(
    const GameController& controller, const MatchReport& report,
    const std::optional<ManagedMatchSnapshot>& snapshot,
    const std::string& home_name, const std::string& away_name)
{
  *this = MatchInsightsView();
  if (!snapshot || (snapshot->shots.empty() && snapshot->detail.empty()))
    return;
  has_data = true;
  has_detail = !snapshot->detail.empty();
  names = {home_name, away_name};
  const MatchDetail& detail = snapshot->detail;
  const auto data = controller.getGameData();
  const auto name_of = [&data](PlayerID id) -> std::string
  {
    if (!data || id == 0) return {};
    const auto player = std::as_const(*data).getPlayer(id);
    return player ? player->get().getName() : std::string();
  };
  const auto nameOrUnknown = [&name_of](PlayerID id)
  {
    std::string name = name_of(id);
    return name.empty() ? std::string(LOC("REPORT_UNKNOWN_PLAYER")) : name;
  };

  InsightInput input;
  input.report = &report;
  input.shots = snapshot->shots;
  input.detail = has_detail ? &detail : nullptr;
  input.managed_home = snapshot->managed_home;
  input.name_of = name_of;
  for (const AnalysisLine& line : summariseMatch(input))
    summary.push_back(GuidanceUI::text(line));

  // ---- xG race and final-third pressure
  axis_end = report.extra_time ? 120.0f : 90.0f;
  std::array<float, 2> totals{};
  for (std::size_t side = 0; side < 2; ++side)
    xg_steps[side].push_back({0.0f, 0.0f});
  for (const ShotRecord& shot : snapshot->shots)
  {
    const std::size_t side = shot.home ? 0 : 1;
    const float axis = std::min(axisMinute(shot.minute, shot.period), axis_end);
    xg_steps[side].push_back({axis, totals[side]});
    totals[side] += shot.xg;
    xg_steps[side].push_back({axis, totals[side]});
  }
  for (std::size_t side = 0; side < 2; ++side)
  {
    xg_steps[side].push_back({axis_end, totals[side]});
    xg_totals[side] = std::format("{:.2f}", totals[side]);
  }
  xg_top =
      std::max(0.5f, std::ceil(std::max(totals[0], totals[1]) * 2.0f) / 2.0f);
  const auto buckets = static_cast<std::size_t>(axis_end) / T::BUCKET_MINUTES;
  for (std::size_t side = 0; side < 2; ++side)
  {
    pressure[side].assign(buckets, 0);
    if (has_detail)
      for (std::size_t bucket = 0; bucket < buckets; ++bucket)
      {
        pressure[side][bucket] = detail.final_third[side][bucket];
        pressure_top = std::max(pressure_top, pressure[side][bucket]);
      }
  }
  for (std::size_t bucket = 0; bucket < buckets; ++bucket)
  {
    const auto start = static_cast<int>(bucket) * T::BUCKET_MINUTES;
    const auto end = start + T::BUCKET_MINUTES;
    std::array<float, 2> sofar{};
    for (const ShotRecord& shot : snapshot->shots)
      if (axisMinute(shot.minute, shot.period) < static_cast<float>(end))
        sofar[shot.home ? 0 : 1] += shot.xg;
    std::string text = fmt::sprintf(
        LOC("INSIGHT_BUCKET_TOOLTIP"), start, end, home_name.c_str(),
        std::format("{:.2f}", sofar[0]).c_str(), away_name.c_str(),
        std::format("{:.2f}", sofar[1]).c_str());
    if (has_detail)
      text += "\n" + fmt::sprintf(LOC("INSIGHT_BUCKET_PRESSURE"),
                                  home_name.c_str(), pressure[0][bucket],
                                  away_name.c_str(), pressure[1][bucket]);
    bucket_tooltips.push_back(std::move(text));
  }
  for (int minute = 0; minute <= static_cast<int>(axis_end); minute += 15)
    axis_labels.push_back(std::format("{}'", minute));

  // ---- Shots
  int goals = 0;
  for (const ShotRecord& shot : snapshot->shots)
  {
    ShotMark mark;
    mark.home = shot.home;
    // Shots are stored in the shooter's attacking frame.
    mark.x = shot.home ? shot.x : 1.0f - shot.x;
    mark.y = shot.home ? shot.y : 1.0f - shot.y;
    mark.xg = shot.xg;
    mark.axis = std::min(axisMinute(shot.minute, shot.period), axis_end);
    mark.goal = shot.outcome == ShotOutcome::Goal;
    goals += mark.goal ? 1 : 0;
    std::string kind;
    if (shot.penalty)
      kind = LOC("INSIGHT_SHOT_PENALTY");
    else if (shot.set_piece)
      kind = LOC("INSIGHT_SHOT_SET_PIECE");
    if (shot.header)
      kind +=
          (kind.empty() ? "" : ", ") + std::string(LOC("INSIGHT_SHOT_HEADER"));
    const std::string minute = MatchClock::minuteLabel(
        shot.minute, shot.period,
        shot.minute >= static_cast<float>(regulationEnd(shot.period)));
    mark.tooltip = fmt::sprintf(LOC("INSIGHT_SHOT_TOOLTIP"), minute.c_str(),
                                nameOrUnknown(shot.player).c_str(),
                                std::format("{:.2f}", shot.xg).c_str(),
                                LOC(outcomeKey(shot.outcome)));
    if (!kind.empty()) mark.tooltip += "\n" + kind;
    shots.push_back(std::move(mark));
  }
  shot_note = fmt::sprintf(LOC("INSIGHT_SHOTS_NOTE"), shots.size(), goals);

  // ---- Key moments
  for (const KeyMoment& moment : keyMoments(input))
  {
    MomentRow row;
    row.kind = moment.kind;
    row.home = moment.home;
    row.shot = moment.shot;
    MatchReportEvent clock;
    clock.minute = moment.minute;
    clock.added_minute = moment.added;
    row.minute = MatchClock::minuteLabel(clock);
    row.axis = std::min(static_cast<float>(moment.minute - moment.added) + 0.5f,
                        axis_end);
    row.text = fmt::sprintf(LOC(momentKey(moment.kind)),
                            nameOrUnknown(moment.player).c_str());
    if (moment.kind == KeyMoment::Kind::Substitution)
      row.detail = fmt::sprintf(LOC("INSIGHT_MOMENT_OFF"),
                                nameOrUnknown(moment.other).c_str());
    else if (moment.kind == KeyMoment::Kind::Goal && moment.other != 0)
      row.detail = fmt::sprintf(LOC("INSIGHT_MOMENT_ASSIST"),
                                nameOrUnknown(moment.other).c_str());
    if (moment.shot >= 0)
    {
      const std::string xg = fmt::sprintf(
          LOC("INSIGHT_MOMENT_XG"), std::format("{:.2f}", moment.xg).c_str());
      row.detail += row.detail.empty() ? xg : " · " + xg;
    }
    moments.push_back(std::move(row));
  }

  if (!has_detail) return;
  // ---- Pass networks
  for (std::size_t side = 0; side < 2; ++side)
  {
    Network& network = networks[side];
    const bool home = side == 0;
    std::vector node_of(detail.players.size(), -1);
    int most_touches = 1;
    for (const DetailPlayer& line : detail.players)
      if (line.home == home)
        most_touches = std::max(most_touches, static_cast<int>(line.touches));
    for (std::size_t index = 0; index < detail.players.size(); ++index)
    {
      const DetailPlayer& line = detail.players[index];
      // Starters only: a substitute takes over his man's spot and would sit
      // on top of him.
      if (const bool substitute = std::ranges::any_of(
              detail.substitutions, [&line](const DetailSubstitution& change)
              { return change.on == line.player; });
          line.home != home || line.touches == 0 || substitute)
        continue;
      node_of[index] = static_cast<int>(network.nodes.size());
      NetworkNode node;
      node.x = line.avg_x;
      node.y = line.avg_y;
      node.weight =
          static_cast<float>(line.touches) / static_cast<float>(most_touches);
      node.detail_index = index;
      const std::string name = nameOrUnknown(line.player);
      node.label = lastName(name);
      int passes = 0;
      const PassLink* partner = nullptr;
      for (const PassLink& link : detail.links)
      {
        if (link.from != index) continue;
        passes += link.count;
        if (partner == nullptr || link.count > partner->count) partner = &link;
      }
      node.tooltip =
          fmt::sprintf(LOC("INSIGHT_NODE_TOOLTIP"), name.c_str(), line.touches,
                       passes, line.progressive_passes, line.pressures);
      if (partner != nullptr)
        node.tooltip +=
            "\n" +
            fmt::sprintf(
                LOC("INSIGHT_NODE_PARTNER"),
                nameOrUnknown(detail.players[partner->to].player).c_str(),
                partner->count);
      network.nodes.push_back(std::move(node));
    }
    // Both directions of a pair make one line.
    for (const PassLink& link : detail.links)
    {
      const int a = node_of[link.from];
      const int b = node_of[link.to];
      if (a < 0 || b < 0 || a == b) continue;
      const auto low = static_cast<std::size_t>(std::min(a, b));
      const auto high = static_cast<std::size_t>(std::max(a, b));
      auto found =
          std::ranges::find_if(network.edges, [&](const NetworkEdge& edge)
                               { return edge.a == low && edge.b == high; });
      if (found == network.edges.end())
      {
        network.edges.push_back({low, high, 0, {}});
        found = network.edges.end() - 1;
      }
      found->total += link.count;
    }
    std::erase_if(network.edges, [](const NetworkEdge& edge)
                  { return edge.total < MIN_LINK; });
    for (NetworkEdge& edge : network.edges)
    {
      network.busiest = std::max(network.busiest, edge.total);
      const std::size_t a = network.nodes[edge.a].detail_index;
      const std::size_t b = network.nodes[edge.b].detail_index;
      int forward = 0;
      int back = 0;
      for (const PassLink& link : detail.links)
      {
        if (link.from == a && link.to == b) forward = link.count;
        if (link.from == b && link.to == a) back = link.count;
      }
      edge.tooltip =
          fmt::sprintf(LOC("INSIGHT_EDGE_TOOLTIP"),
                       nameOrUnknown(detail.players[a].player).c_str(),
                       nameOrUnknown(detail.players[b].player).c_str(),
                       edge.total, forward, back);
    }
    // Strong links on top.
    std::ranges::sort(network.edges, {}, &NetworkEdge::total);
    network.note = fmt::sprintf(LOC("INSIGHT_NETWORK_NOTE"), MIN_LINK);
  }

  // ---- Touch maps: the whole side, then players by touches.
  for (std::size_t side = 0; side < 2; ++side)
  {
    const bool home = side == 0;
    HeatChoice team;
    team.label = fmt::sprintf(LOC("INSIGHT_HEAT_TEAM"), names[side].c_str());
    std::vector<std::size_t> order;
    for (std::size_t index = 0; index < detail.players.size(); ++index)
    {
      const DetailPlayer& line = detail.players[index];
      if (line.home != home || line.touches == 0) continue;
      order.push_back(index);
      for (std::size_t cell = 0; cell < T::CELLS; ++cell)
        team.cells[cell] += line.cells[cell];
    }
    std::ranges::stable_sort(order, std::greater{}, [&](std::size_t index)
                             { return detail.players[index].touches; });
    heat[side].push_back(std::move(team));
    heat_detail[side].push_back(detail.players.size());
    for (const std::size_t index : order)
    {
      const DetailPlayer& line = detail.players[index];
      HeatChoice choice;
      choice.label =
          fmt::sprintf(LOC("INSIGHT_HEAT_PLAYER"),
                       nameOrUnknown(line.player).c_str(), line.touches);
      for (std::size_t cell = 0; cell < T::CELLS; ++cell)
        choice.cells[cell] = line.cells[cell];
      heat[side].push_back(std::move(choice));
      heat_detail[side].push_back(index);
    }
    for (HeatChoice& choice : heat[side])
    {
      for (const int value : choice.cells)
      {
        choice.total += value;
        choice.most = std::max(choice.most, value);
      }
    }
  }
  heat_side = snapshot->managed_home ? 0 : 1;
  network_side = heat_side;
}

void MatchInsightsView::selectHeat(std::size_t side, std::size_t choice)
{
  heat_side = static_cast<int>(side);
  heat_choice =
      std::min(choice, heat[side].empty() ? 0 : heat[side].size() - 1);
  buildCellTooltips();
}

void MatchInsightsView::buildCellTooltips()
{
  cell_tooltip_side = heat_side;
  cell_tooltip_choice = heat_choice;
  const std::vector<HeatChoice>& choices =
      heat[static_cast<std::size_t>(heat_side)];
  if (heat_choice >= choices.size()) return;
  const HeatChoice& choice = choices[heat_choice];
  for (std::size_t cell = 0; cell < T::CELLS; ++cell)
  {
    const int value = choice.cells[cell];
    const float share = choice.total > 0 ? 100.0f * static_cast<float>(value) /
                                               static_cast<float>(choice.total)
                                         : 0.0f;
    cell_tooltips[cell] = fmt::sprintf(LOC("INSIGHT_HEAT_TOOLTIP"), value,
                                       std::format("{:.0f}%", share).c_str());
  }
}

// ---- Layout
// ------------------------------------------------------------------

void MatchInsightsView::render(const GUIView* view)
{
  (void)view;
  if (!has_data) return;
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool two = available >= scaled(TWO_COLUMN_MIN_WIDTH);
  const float half = two ? std::floor((available - gap) * 0.5f) : available;
  // A moment under the cursor (last frame) wins over the picked one.
  focus_moment = hovered_moment >= 0 ? hovered_moment : picked_moment;

  renderSummary(available);
  renderTimeline(available);
  // Wide: the pitches on the left, the (long) moments list beside them.
  const auto pitches = [this](float width)
  {
    renderShotMap(width);
    if (has_detail)
      renderHeatmap(width);
    else
    {
      UI::beginAutoHeightCard("insight_no_detail", LOC("INSIGHT_NETWORK_TITLE"),
                              width);
      footnote(LOC("INSIGHT_NO_DETAIL"));
      UI::endCard();
    }
  };
  if (two)
  {
    const float left = std::floor((available - gap) * 0.58f);
    ImGui::BeginGroup();
    pitches(left);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    renderMoments(available - gap - left);
    ImGui::EndGroup();
  }
  else
  {
    renderShotMap(available);
    renderMoments(available);
    if (has_detail) renderHeatmap(available);
  }
  if (!has_detail)
  {
    if (!two)
    {
      UI::beginAutoHeightCard("insight_no_detail", LOC("INSIGHT_NETWORK_TITLE"),
                              available);
      footnote(LOC("INSIGHT_NO_DETAIL"));
      UI::endCard();
    }
    return;
  }
  if (two)
  {
    ImGui::BeginGroup();
    renderNetwork(0, half, false);
    ImGui::EndGroup();
    ImGui::SameLine();
    ImGui::BeginGroup();
    renderNetwork(1, available - gap - half, false);
    ImGui::EndGroup();
  }
  else
  {
    renderNetwork(static_cast<std::size_t>(network_side), available, true);
  }
}

void MatchInsightsView::renderSummary(float width) const
{
  UI::beginAutoHeightCard("insight_summary", LOC("INSIGHT_SUMMARY_TITLE"),
                          width);
  if (summary.empty())
  {
    footnote(LOC("INSIGHT_SUMMARY_NONE"));
    UI::endCard();
    return;
  }
  const Theme::Palette& palette = Theme::palette();
  const float bullet = ImGui::GetTextLineHeight();
  for (const std::string& line : summary)
  {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(start.x + bullet * 0.35f, start.y + bullet * 0.5f),
        bullet * 0.14f, Theme::toU32(palette.muted));
    ImGui::SetCursorScreenPos(ImVec2(start.x + bullet, start.y));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(line.c_str());
    ImGui::PopTextWrapPos();
  }
  footnote(LOC("INSIGHT_SUMMARY_NOTE"));
  UI::endCard();
}

void MatchInsightsView::renderTimeline(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("insight_timeline", LOC("INSIGHT_TIMELINE_TITLE"),
                          width);
  for (std::size_t side = 0; side < 2; ++side)
  {
    if (side == 1) ImGui::SameLine(0.0f, scaled(Theme::Space::L));
    legendSwatch(names[side].c_str(), sideColor(side), true);
    ImGui::SameLine();
    ImGui::TextColored(palette.muted, "%s xG", xg_totals[side].c_str());
  }

  const float plotWidth = ImGui::GetContentRegionAvail().x;
  const float plotHeight = scaled(TIMELINE_HEIGHT);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##insight_timeline_plot",
                         ImVec2(plotWidth, plotHeight));
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  std::optional<Theme::ScopedText> small;
  small.emplace(Theme::Text::SMALL);
  const float lineHeight = ImGui::GetTextLineHeight();
  const float left = origin.x + scaled(AXIS_LEFT);
  const float right = origin.x + plotWidth - scaled(Theme::Space::S);
  const float top = origin.y + scaled(Theme::Space::XS);
  const float bottom = origin.y + plotHeight - lineHeight - scaled(2.0f);
  const float xgBottom = std::lerp(top, bottom, XG_SHARE);
  const float pressTop = xgBottom + scaled(Theme::Space::M);
  const float pressMid = (pressTop + bottom) * 0.5f;
  const float pressHalf = (bottom - pressTop) * 0.5f;
  const auto xOf = [&](float minute)
  { return left + (right - left) * std::clamp(minute / axis_end, 0.0f, 1.0f); };
  const auto yOf = [&](float xg)
  { return xgBottom - (xgBottom - top) * std::clamp(xg / xg_top, 0.0f, 1.0f); };
  const ImU32 grid = Theme::toU32(palette.border);
  const ImU32 muted = Theme::toU32(palette.muted);

  // Recessive grid: xG levels and the half-time line.
  for (int step = 0; step <= 2; ++step)
  {
    const float value = xg_top * static_cast<float>(step) / 2.0f;
    const float y = yOf(value);
    drawList->AddLine(ImVec2(left, y), ImVec2(right, y), grid, 1.0f);
    std::array<char, 16> label{};
    std::format_to_n(label.data(), label.size() - 1, "{:.1f}", value);
    drawList->AddText(ImVec2(origin.x, y - lineHeight * 0.5f), muted,
                      label.data());
  }
  drawList->AddLine(ImVec2(xOf(45.0f), top), ImVec2(xOf(45.0f), bottom), grid,
                    1.0f);
  drawList->AddLine(ImVec2(left, pressMid), ImVec2(right, pressMid), grid,
                    1.0f);
  for (std::size_t index = 0; index < axis_labels.size(); ++index)
  {
    const float x = xOf(static_cast<float>(index * 15));
    const float labelWidth = ImGui::CalcTextSize(axis_labels[index].c_str()).x;
    drawList->AddText(
        ImVec2(std::clamp(x - labelWidth * 0.5f, left, right - labelWidth),
               bottom + scaled(2.0f)),
        muted, axis_labels[index].c_str());
  }
  drawList->AddText(ImVec2(origin.x, pressTop), muted,
                    LOC("INSIGHT_PRESSURE_AXIS"));

  // Final-third touches: home above the line, away below.
  const std::size_t buckets = pressure[0].size();
  const float bucketWidth =
      buckets > 0 ? (right - left) / static_cast<float>(buckets) : 0.0f;
  const float barGap = std::min(scaled(2.0f), bucketWidth * 0.2f);
  for (std::size_t bucket = 0; bucket < buckets; ++bucket)
  {
    const float x0 = left + bucketWidth * static_cast<float>(bucket) + barGap;
    const float x1 = x0 + bucketWidth - 2.0f * barGap;
    for (std::size_t side = 0; side < 2; ++side)
    {
      const float value = static_cast<float>(pressure[side][bucket]) /
                          static_cast<float>(pressure_top);
      if (value <= 0.0f) continue;
      const float extent = pressHalf * value;
      const ImVec2 a(x0, side == 0 ? pressMid - extent : pressMid + 1.0f);
      const ImVec2 b(x1, side == 0 ? pressMid - 1.0f : pressMid + extent);
      drawList->AddRectFilled(a, b, Theme::toU32(sideColor(side), 0.75f),
                              scaled(2.0f),
                              side == 0 ? ImDrawFlags_RoundCornersTop
                                        : ImDrawFlags_RoundCornersBottom);
    }
  }

  // xG race as steps, goals as filled dots on the step.
  for (std::size_t side = 0; side < 2; ++side)
  {
    const auto& steps = xg_steps[side];
    const ImU32 color = Theme::toU32(sideColor(side));
    for (std::size_t index = 1; index < steps.size(); ++index)
      drawList->AddLine(
          ImVec2(xOf(steps[index - 1].axis), yOf(steps[index - 1].total)),
          ImVec2(xOf(steps[index].axis), yOf(steps[index].total)), color,
          scaled(2.0f));
  }
  {
    std::array<float, 2> totals{};
    for (const ShotMark& shot : shots)
    {
      const std::size_t side = shot.home ? 0 : 1;
      totals[side] += shot.xg;
      if (!shot.goal) continue;
      const ImVec2 at(xOf(shot.axis), yOf(totals[side]));
      drawList->AddCircleFilled(at, scaled(4.5f),
                                Theme::toU32(sideColor(side)));
      drawList->AddCircle(at, scaled(4.5f), Theme::toU32(palette.surface), 0,
                          scaled(1.5f));
    }
  }
  if (focus_moment >= 0 && focus_moment < static_cast<int>(moments.size()))
  {
    const float x = xOf(moments[static_cast<std::size_t>(focus_moment)].axis);
    drawList->AddLine(ImVec2(x, top), ImVec2(x, bottom),
                      Theme::toU32(palette.text, 0.6f), scaled(1.0f));
  }
  if (hovered && buckets > 0)
  {
    const float mouse = ImGui::GetIO().MousePos.x;
    const auto bucket = static_cast<std::size_t>(
        std::clamp((mouse - left) / std::max(bucketWidth, 1.0f), 0.0f,
                   static_cast<float>(buckets - 1)));
    const float x0 = left + bucketWidth * static_cast<float>(bucket);
    drawList->AddRectFilled(ImVec2(x0, top), ImVec2(x0 + bucketWidth, bottom),
                            Theme::toU32(palette.text, 0.06f));
    ImGui::SetTooltip("%s", bucket_tooltips[bucket].c_str());
  }
  small.reset();
  footnote(
      LOC(has_detail ? "INSIGHT_TIMELINE_NOTE" : "INSIGHT_TIMELINE_NOTE_XG"));
  UI::endCard();
}

void MatchInsightsView::renderShotMap(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("insight_shots", LOC("INSIGHT_SHOTS_TITLE"), width);
  legendSwatch(names[0].c_str(), sideColor(0), false);
  ImGui::SameLine(0.0f, scaled(Theme::Space::L));
  legendSwatch(names[1].c_str(), sideColor(1), false);
  const float mapWidth = ImGui::GetContentRegionAvail().x;
  const ImVec2 size(mapWidth, std::floor(mapWidth * PITCH_ASPECT));
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton("##insight_shot_map", size);
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawPitch(drawList, origin, size, true);
  const int focusShot =
      focus_moment >= 0 && focus_moment < static_cast<int>(moments.size())
          ? moments[static_cast<std::size_t>(focus_moment)].shot
          : -1;
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  int nearest = -1;
  float nearestDistance = scaled(HIT_RADIUS);
  for (std::size_t index = 0; index < shots.size(); ++index)
  {
    const ShotMark& shot = shots[index];
    const ImVec2 at = onPitch(origin, size, shot.x, shot.y);
    const float radius = scaled(3.0f) + scaled(11.0f) * std::sqrt(shot.xg);
    const ImVec4 color = sideColor(shot.home ? 0 : 1);
    if (shot.goal)
    {
      drawList->AddCircleFilled(at, radius, Theme::toU32(color));
      drawList->AddCircle(at, radius, Theme::toU32(palette.surface), 0,
                          scaled(2.0f));
    }
    else
    {
      drawList->AddCircleFilled(at, radius, Theme::toU32(color, 0.18f));
      drawList->AddCircle(at, radius, Theme::toU32(color, 0.9f), 0,
                          scaled(1.5f));
    }
    if (static_cast<int>(index) == focusShot)
      drawList->AddCircle(at, radius + scaled(4.0f), Theme::toU32(palette.text),
                          0, scaled(2.0f));
    const float distance = std::hypot(at.x - mouse.x, at.y - mouse.y);
    if (hovered && distance < std::max(nearestDistance, radius))
    {
      nearest = static_cast<int>(index);
      nearestDistance = distance;
    }
  }
  if (nearest >= 0)
  {
    ImGui::SetTooltip("%s",
                      shots[static_cast<std::size_t>(nearest)].tooltip.c_str());
    if (clicked)
    {
      const auto moment = std::ranges::find(moments, nearest, &MomentRow::shot);
      picked_moment = moment == moments.end()
                          ? -1
                          : static_cast<int>(moment - moments.begin());
    }
  }
  footnote(shot_note);
  UI::endCard();
}

void MatchInsightsView::renderMoments(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("insight_moments", LOC("INSIGHT_MOMENTS_TITLE"),
                          width);
  int hovered = -1;
  if (moments.empty())
  {
    footnote(LOC("INSIGHT_MOMENTS_NONE"));
  }
  const float lineHeight = ImGui::GetTextLineHeight();
  float smallHeight = 0.0f;
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    smallHeight = ImGui::GetTextLineHeight();
  }
  const float minuteWidth = ImGui::CalcTextSize("90+10'").x;
  const float marker = lineHeight * 0.7f;
  for (std::size_t index = 0; index < moments.size(); ++index)
  {
    const MomentRow& row = moments[index];
    ImGui::PushID(static_cast<int>(index));
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    const float rowHeight = lineHeight +
                            (row.detail.empty() ? 0.0f : smallHeight) +
                            scaled(Theme::Space::XS);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    if (ImGui::Selectable("##moment", picked_moment == static_cast<int>(index),
                          ImGuiSelectableFlags_None,
                          ImVec2(rowWidth, rowHeight)))
      picked_moment = picked_moment == static_cast<int>(index)
                          ? -1
                          : static_cast<int>(index);
    if (ImGui::IsItemHovered())
    {
      hovered = static_cast<int>(index);
      if (row.shot >= 0 || row.kind == KeyMoment::Kind::Substitution)
        ImGui::SetTooltip("%s", LOC("INSIGHT_MOMENT_HINT"));
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float top = start.y + scaled(Theme::Space::XS) * 0.5f;
    drawList->AddText(ImVec2(start.x, top), Theme::toU32(palette.muted),
                      row.minute.c_str());
    const ImVec2 centre(start.x + minuteWidth + marker,
                        top + lineHeight * 0.5f);
    const ImU32 side = Theme::toU32(sideColor(row.home ? 0 : 1));
    using enum KeyMoment::Kind;
    switch (row.kind)
    {
      case Goal:
      case OwnGoal:
        drawList->AddCircleFilled(centre, marker * 0.5f, side);
        break;
      case BigChance:
      case Woodwork:
        drawList->AddCircle(centre, marker * 0.5f, side, 0, scaled(1.5f));
        break;
      case Yellow:
        drawList->AddRectFilled(
            ImVec2(centre.x - marker * 0.3f, centre.y - marker * 0.45f),
            ImVec2(centre.x + marker * 0.3f, centre.y + marker * 0.45f),
            IM_COL32(245, 200, 40, 255), scaled(1.5f));
        break;
      case SecondYellow:
      case Red:
        drawList->AddRectFilled(
            ImVec2(centre.x - marker * 0.3f, centre.y - marker * 0.45f),
            ImVec2(centre.x + marker * 0.3f, centre.y + marker * 0.45f),
            IM_COL32(220, 50, 50, 255), scaled(1.5f));
        break;
      case Substitution:
        drawList->AddTriangleFilled(
            ImVec2(centre.x - marker * 0.4f, centre.y + marker * 0.1f),
            ImVec2(centre.x + marker * 0.4f, centre.y + marker * 0.1f),
            ImVec2(centre.x, centre.y - marker * 0.45f), side);
        break;
    }
    const float textX = centre.x + marker;
    const float textWidth = start.x + rowWidth - textX;
    UI::drawTextFitted(drawList, ImVec2(textX, top), Theme::toU32(palette.text),
                       row.text, textWidth);
    if (!row.detail.empty())
    {
      Theme::ScopedText small(Theme::Text::SMALL);
      UI::drawTextFitted(drawList, ImVec2(textX, top + lineHeight),
                         Theme::toU32(palette.muted), row.detail, textWidth);
    }
    ImGui::PopID();
  }
  hovered_moment = hovered;
  UI::endCard();
}

void MatchInsightsView::renderNetwork(std::size_t side, float width,
                                      bool selector)
{
  const Theme::Palette& palette = Theme::palette();
  const Network& network = networks[side];
  ImGui::PushID(static_cast<int>(side));
  UI::beginAutoHeightCard("insight_network", LOC("INSIGHT_NETWORK_TITLE"),
                          width);
  if (selector)
  {
    // Narrow layout: one network at a time.
    const std::array<const char*, 2> sides = {names[0].c_str(),
                                              names[1].c_str()};
    UI::segmented("##network_side", network_side, sides,
                  ImGui::GetContentRegionAvail().x);
  }
  else
  {
    legendSwatch(names[side].c_str(), sideColor(side), false);
  }
  const float mapWidth = ImGui::GetContentRegionAvail().x;
  const ImVec2 size(mapWidth, std::floor(mapWidth * PITCH_ASPECT));
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::InvisibleButton("##insight_network_map", size);
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawPitch(drawList, origin, size, true);
  const ImVec4 color = sideColor(side);
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const auto at = [&](const NetworkNode& node)
  { return onPitch(origin, size, node.x, node.y); };

  const NetworkEdge* hoveredEdge = nullptr;
  float edgeDistance = scaled(5.0f);
  for (const NetworkEdge& edge : network.edges)
  {
    const float share =
        static_cast<float>(edge.total) / static_cast<float>(network.busiest);
    const ImVec2 a = at(network.nodes[edge.a]);
    const ImVec2 b = at(network.nodes[edge.b]);
    drawList->AddLine(a, b, Theme::toU32(color, 0.25f + 0.6f * share),
                      scaled(1.0f + 5.0f * share));
    const float distance = segmentDistance(mouse, a, b);
    if (hovered && distance < edgeDistance)
    {
      edgeDistance = distance;
      hoveredEdge = &edge;
    }
  }
  const NetworkNode* hoveredNode = nullptr;
  float nodeDistance = scaled(HIT_RADIUS);
  const float labelHeight = ImGui::GetTextLineHeight();
  for (const NetworkNode& node : network.nodes)
  {
    const ImVec2 centre = at(node);
    const float radius = scaled(4.0f) + scaled(8.0f) * std::sqrt(node.weight);
    drawList->AddCircleFilled(centre, radius, Theme::toU32(color));
    drawList->AddCircle(centre, radius, Theme::toU32(palette.surface), 0,
                        scaled(1.5f));
    {
      Theme::ScopedText small(Theme::Text::SMALL);
      const float labelWidth = ImGui::CalcTextSize(node.label.c_str()).x;
      drawList->AddText(
          ImVec2(std::clamp(centre.x - labelWidth * 0.5f, origin.x,
                            origin.x + size.x - labelWidth),
                 std::min(centre.y + radius + scaled(1.0f),
                          origin.y + size.y - labelHeight)),
          Theme::toU32(palette.text), node.label.c_str());
    }
    const float distance = std::hypot(centre.x - mouse.x, centre.y - mouse.y);
    if (hovered && distance < std::max(nodeDistance, radius))
    {
      nodeDistance = distance;
      hoveredNode = &node;
    }
  }
  if (hoveredNode != nullptr)
  {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetTooltip("%s\n%s", hoveredNode->tooltip.c_str(),
                      LOC("INSIGHT_NODE_HINT"));
    if (clicked)
    {
      const auto& details = heat_detail[side];
      const auto choice = std::ranges::find(details, hoveredNode->detail_index);
      if (choice != details.end())
        selectHeat(side, static_cast<std::size_t>(choice - details.begin()));
    }
  }
  else if (hoveredEdge != nullptr)
  {
    ImGui::SetTooltip("%s", hoveredEdge->tooltip.c_str());
  }
  footnote(network.note);
  UI::endCard();
  ImGui::PopID();
}

void MatchInsightsView::renderHeatmap(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("insight_heat", LOC("INSIGHT_HEAT_TITLE"), width);
  const std::array<const char*, 2> sides = {names[0].c_str(), names[1].c_str()};
  const float controlWidth =
      std::min(ImGui::GetContentRegionAvail().x, scaled(360.0f));
  if (int side = heat_side;
      UI::segmented("##heat_side", side, sides, controlWidth))
    selectHeat(static_cast<std::size_t>(side), 0);
  if (cell_tooltip_side != heat_side || cell_tooltip_choice != heat_choice)
    buildCellTooltips();
  std::vector<HeatChoice>& choices = heat[static_cast<std::size_t>(heat_side)];
  if (choices.empty())
  {
    UI::endCard();
    return;
  }
  heat_choice = std::min(heat_choice, choices.size() - 1);
  UI::sameLineIfFits(controlWidth);
  ImGui::SetNextItemWidth(
      std::min(ImGui::GetContentRegionAvail().x, scaled(360.0f)));
  if (ImGui::BeginCombo("##heat_player", choices[heat_choice].label.c_str()))
  {
    for (std::size_t index = 0; index < choices.size(); ++index)
    {
      ImGui::PushID(static_cast<int>(index));
      if (ImGui::Selectable(choices[index].label.c_str(), index == heat_choice))
        selectHeat(static_cast<std::size_t>(heat_side), index);
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  const HeatChoice& choice = choices[heat_choice];
  const float mapWidth =
      std::min(ImGui::GetContentRegionAvail().x, scaled(720.0f));
  const ImVec2 size(mapWidth, std::floor(mapWidth * PITCH_ASPECT));
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##insight_heat_map", size);
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawPitch(drawList, origin, size, true);
  const float cellWidth = size.x / static_cast<float>(T::GRID_COLUMNS);
  const float cellHeight = size.y / static_cast<float>(T::GRID_ROWS);
  const ImVec4 color = sideColor(static_cast<std::size_t>(heat_side));
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  int hoveredCell = -1;
  for (std::size_t row = 0; row < T::GRID_ROWS; ++row)
  {
    for (std::size_t column = 0; column < T::GRID_COLUMNS; ++column)
    {
      const std::size_t cell = row * T::GRID_COLUMNS + column;
      const ImVec2 a(origin.x + cellWidth * static_cast<float>(column),
                     origin.y + cellHeight * static_cast<float>(row));
      const ImVec2 b(a.x + cellWidth, a.y + cellHeight);
      if (const int value = choice.cells[cell]; value > 0 && choice.most > 0)
      {
        const float share =
            static_cast<float>(value) / static_cast<float>(choice.most);
        drawList->AddRectFilled(
            ImVec2(a.x + 1.0f, a.y + 1.0f), ImVec2(b.x - 1.0f, b.y - 1.0f),
            Theme::toU32(color, 0.1f + 0.75f * share), scaled(2.0f));
      }
      if (hovered && mouse.x >= a.x && mouse.x < b.x && mouse.y >= a.y &&
          mouse.y < b.y)
        hoveredCell = static_cast<int>(cell);
    }
  }
  // Direction of play.
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    const char* arrow = LOC("INSIGHT_ATTACKING");
    const ImVec2 textSize = ImGui::CalcTextSize(arrow);
    drawList->AddText(ImVec2(origin.x + size.x - textSize.x - scaled(4.0f),
                             origin.y + size.y - textSize.y - scaled(2.0f)),
                      Theme::toU32(palette.muted), arrow);
  }
  if (hoveredCell >= 0)
  {
    const auto cell = static_cast<std::size_t>(hoveredCell);
    const ImVec2 a(
        origin.x + cellWidth * static_cast<float>(cell % T::GRID_COLUMNS),
        origin.y + cellHeight * static_cast<float>(cell / T::GRID_COLUMNS));
    drawList->AddRect(a, ImVec2(a.x + cellWidth, a.y + cellHeight),
                      Theme::toU32(palette.text), 0.0f, 0, scaled(1.5f));
    ImGui::SetTooltip("%s", cell_tooltips[cell].c_str());
  }
  footnote(LOC("INSIGHT_HEAT_NOTE"));
  UI::endCard();
}

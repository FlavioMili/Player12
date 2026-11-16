// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_page_ui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <numbers>

#include "gui/render/imatch_renderer.h"
#include "gui/view_models/formation.h"
#include "gui/widgets/theme.h"
#include "model/lineup.h"
#include "model/player.h"
#include "model/strategy.h"
#include "tools/match_sandbox_setup.h"

namespace
{
/** Ids well clear of the world's players. */
constexpr PlayerID FIRST_ID = 900'000;
constexpr std::size_t SQUAD = 11;
}  // namespace

DrillRun::DrillRun(Drill& drill_ref, const StatsConfig& config, bool record,
                   const std::function<void(MatchEngine&)>& prepare,
                   const std::vector<Player*>& squad)
    : drill(drill_ref), log(std::make_unique<SandboxRecorder>())
{
  // Two full squads of reference players, the drill's own players in the
  // first slots of their side (a keeper in goal). The whole squads count
  // for the engine's level adjustment, so it is the same in every run.
  const std::vector<DrillPlayerSpec> specs = drill.players();
  const Formation::Preset& shape = Formation::PRESETS[0];
  std::array<Lineup, 2> lineups;
  PlayerID nextId = FIRST_ID;
  drillIds.assign(specs.size(), 0);
  for (std::size_t side = 0; side < 2; ++side)
  {
    const bool home = side == 0;
    // This side's drill players: the keeper first, then outfielders.
    std::vector<std::size_t> keepers;
    std::vector<std::size_t> outfield;
    for (std::size_t index = 0; index < specs.size(); ++index)
      if (specs[index].homeTeam == home)
        (specs[index].role == PlayerRole::GK ? keepers : outfield)
            .push_back(index);
    for (std::size_t slot = 0; slot < SQUAD; ++slot)
    {
      const bool keeperSlot = slot == 0;
      const std::vector<std::size_t>& own = keeperSlot ? keepers : outfield;
      const std::size_t ownIndex = keeperSlot ? 0 : slot - 1;
      const DrillPlayerSpec* spec =
          ownIndex < own.size() ? &specs[own[ownIndex]] : nullptr;
      std::map<std::string, float> stats;
      for (const std::string& stat : config.possible_stats)
        stats[stat] = REFERENCE_RATING;
      if (spec)
        for (const auto& [stat, value] : spec->stats) stats[stat] = value;
      const PlayerRole role = keeperSlot ? PlayerRole::GK
                              : spec     ? spec->role
                                         : shape.slots[slot - 1].role;
      const std::string name =
          spec ? spec->name : std::format("Reference {}", slot + 1);
      // A session's own player in a drill player's place (section 23).
      if (spec && !squad.empty())
      {
        const std::size_t specIndex = static_cast<std::size_t>(spec - specs.data());
        Player* own = specIndex < squad.size() ? squad[specIndex] : nullptr;
        if (own)
        {
          drillIds[specIndex] = own->getId();
          if (keeperSlot)
            lineups[side].setGoalkeeper(own);
          else
            lineups[side].addOutfieldPlayer(own, shape.slots[slot - 1].position);
          continue;
        }
      }
      auto player = std::make_unique<Player>(
          nextId++, static_cast<TeamID>(side + 1), "", name, role, Language::EN,
          0, 0, 25, 1,
          static_cast<std::uint8_t>(spec ? spec->height : 180),
          spec ? spec->foot : Foot::Right,
          stats);
      if (spec) drillIds[static_cast<std::size_t>(spec - specs.data())] =
                    player->getId();
      if (keeperSlot)
        lineups[side].setGoalkeeper(player.get());
      else
        lineups[side].addOutfieldPlayer(player.get(),
                                        shape.slots[slot - 1].position);
      pool.push_back(std::move(player));
    }
  }

  match = std::make_unique<MatchEngine>(lineups[0], lineups[1], Strategy{},
                                        Strategy{}, config, drill.seed,
                                        sandboxHandshake(), sandboxPossession(),
                                        squad.empty()
                                            ? sandboxDrillPitch().value_or(drill.pitch())
                                            : drill.pitch());
  applyHomeAdvantage(*match, sandboxHomeAdvantage());
  match->setTeamNames("Drill home", "Drill away");
  match->setBrain(true, sandboxBrains()[0]);
  match->setBrain(false, sandboxBrains()[1]);
  match->setBody(true, sandboxBodies()[0]);
  match->setBody(false, sandboxBodies()[1]);
  match->setReferee(sandboxReferee());
  match->setManager(true, sandboxManagers()[0]);
  match->setManager(false, sandboxManagers()[1]);
  match->setBallPhysics(sandboxBallPhysics());
  match->setOutOfPlay(sandboxOutOfPlay());
  match->setDrillRules({false, false});
  // Only the drill's players take part.
  for (const auto& player : pool)
    if (std::ranges::find(drillIds, player->getId()) == drillIds.end())
      (void)match->removeBeforeKickOff(player->getId());
  for (std::size_t index = 0; index < specs.size(); ++index)
  {
    for (const auto& [stat, value] : specs[index].derived)
      match->setDerivedStat(drillIds[index], stat, value / 100.0f);
    if (specs[index].condition < 100.0f)
      (void)match->setPlayerCondition(drillIds[index],
                                      specs[index].condition / 100.0f);
  }
  if (record) match->setRecorder(log.get());
  drill.begin(*match, drillIds);
  if (prepare) prepare(*match);
}

DrillRun::~DrillRun()
{
  // The engine and its saved copies borrow the players: release them first.
  if (match) match->setRecorder(nullptr);
  match.reset();
  log.reset();
}

bool DrillRun::step()
{
  if (over) return false;
  drill.beforeStep(*match);
  match->advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  if (!drill.afterStep(*match) || match->getState() == MatchState::FULL_TIME)
    over = true;
  return !over;
}

bool Drill::applyPreset(const std::string& name)
{
  for (const DrillPreset& preset : presets())
  {
    if (preset.name != name) continue;
    for (const DrillParameter& parameter : parameters())
      if (parameter.value)
        if (const auto found = preset.settings.find(parameter.name);
            found != preset.settings.end())
          *parameter.value = found->second;
    return true;
  }
  return false;
}

void Drill::renderSettings(int columns, bool includeSeed, bool stackedLabels)
{
  if (const std::vector<DrillPreset> list = presets(); !list.empty())
  {
    // A picker that fills the settings in; it keeps no state of its own.
    if (stackedLabels)
    {
      ImGui::TextUnformatted("Situation");
      ImGui::SetNextItemWidth(-FLT_MIN);
    }
    if (SandboxPageUI::beginCombo(stackedLabels ? "##situation" : "Situation",
                          "choose to fill in the settings..."))
    {
      for (const DrillPreset& preset : list)
      {
        if (ImGui::Selectable(preset.name.c_str()))
          (void)applyPreset(preset.name);
        if (!preset.summary.empty() && ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", preset.summary.c_str());
      }
      ImGui::EndCombo();
    }
  }
  const bool split = columns == 2;
  const bool stacked = split || stackedLabels;
  bool formOpen = false;
  int formId = 0;
  const auto closeForm = [&]
  {
    if (!formOpen) return;
    ImGui::EndTable();
    ImGui::PopStyleVar(3);
    ImGui::PopID();
    formOpen = false;
  };
  const auto nextField = [&]() -> bool
  {
    if (!split) return true;
    if (!formOpen)
    {
      ImGui::PushID(formId++);
      const float scale = Theme::scale();
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                          ImVec2(8 * scale, 4 * scale));
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                          ImVec2(6 * scale, 2 * scale));
      ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                          ImVec2(3 * scale, 3 * scale));
      formOpen = ImGui::BeginTable(
          "##parameter_columns", 2,
          ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV |
              ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg);
      if (!formOpen)
      {
        ImGui::PopStyleVar(3);
        ImGui::PopID();
        return false;
      }
    }
    ImGui::TableNextColumn();
    return true;
  };
  // A section starts where its title changes (by text: settings may each
  // hold their own copy of it); a closed collapsible one hides its settings.
  const char* section = nullptr;
  bool open = true;
  for (const DrillParameter& parameter : parameters())
  {
    if (!parameter.value) continue;
    if (parameter.section &&
        (!section || std::strcmp(parameter.section, section) != 0))
    {
      closeForm();
      section = parameter.section;
      if (parameter.collapsible)
        open = ImGui::CollapsingHeader(section);
      else
      {
        ImGui::SeparatorText(section);
        open = true;
      }
    }
    if (!open) continue;
    if (!nextField()) continue;
    if (stacked)
    {
      // Clear a baseline inherited from the neighbouring cell's framed input.
      ImGui::Dummy(ImVec2(0, 0));
      ImGui::TextWrapped("%s",
                         parameter.label ? parameter.label : parameter.name);
      if (parameter.tooltip && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", parameter.tooltip);
      ImGui::SetNextItemWidth(-FLT_MIN);
    }
    const std::string id =
        stacked ? std::string("##") + parameter.name
        : parameter.label
            ? std::format("{}##{}", parameter.label, parameter.name)
            : std::string(parameter.name);
    float& value = *parameter.value;
    if (!parameter.choices.empty())
    {
      int index = static_cast<int>(value);
      if (SandboxPageUI::combo(id.c_str(), &index, parameter.choices.data(),
                       static_cast<int>(parameter.choices.size())))
        value = static_cast<float>(index);
    }
    else if (parameter.integer)
    {
      int whole = static_cast<int>(std::lround(value));
      if (ImGui::SliderInt(id.c_str(), &whole, static_cast<int>(parameter.min),
                           static_cast<int>(parameter.max)))
        value = static_cast<float>(whole);
    }
    else
    {
      // Small ranges (tuning knobs) need more decimals. An automatic
      // setting shows the value it is set to, in the warning colour, until
      // moved.
      const bool automatic = parameter.automatic && value == 0.0f;
      float shown = automatic ? parameter.automatic() : value;
      if (automatic)
      {
        const ImVec4 warning = Theme::palette().warning;
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, warning);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, warning);
        ImGui::PushStyleColor(ImGuiCol_Text, warning);
      }
      if (ImGui::SliderFloat(
              id.c_str(), &shown, parameter.min, parameter.max,
              parameter.max - parameter.min < 2.5f ? "%.3f" : "%.1f"))
        value = shown;
      if (automatic) ImGui::PopStyleColor(3);
    }
    if (parameter.tooltip && ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", parameter.tooltip);
  }
  if (includeSeed && nextField())
  {
    if (stacked)
    {
      ImGui::Dummy(ImVec2(0, 0));
      ImGui::TextUnformatted("Seed");
      ImGui::SetNextItemWidth(-FLT_MIN);
    }
    ImGui::InputScalar(stacked ? "##seed" : "Seed", ImGuiDataType_U32, &seed);
  }
  closeForm();
}

// --- Drawing ---------------------------------------------------------------

bool DrillDraw::project(const IMatchRenderer& renderer, PitchPoint pitch,
                        ImVec2& screen)
{
  return renderer.projectPitch(pitch, 0.0f, screen.x, screen.y);
}

void DrillDraw::line(const IMatchRenderer& renderer, ImDrawList& draw,
                     PitchPoint from, PitchPoint to, ImU32 color,
                     float thickness)
{
  ImVec2 a;
  ImVec2 b;
  if (project(renderer, from, a) && project(renderer, to, b))
    draw.AddLine(a, b, color, thickness);
}

void DrillDraw::label(const IMatchRenderer& renderer, ImDrawList& draw,
                      PitchPoint at, const char* text, ImU32 color)
{
  ImVec2 point;
  if (!project(renderer, at, point)) return;
  const ImVec2 size = ImGui::CalcTextSize(text);
  draw.AddRectFilled(ImVec2(point.x - 2.0f, point.y - 1.0f),
                     ImVec2(point.x + size.x + 2.0f, point.y + size.y + 1.0f),
                     IM_COL32(0, 0, 0, 170), 3.0f);
  draw.AddText(point, color, text);
}

void DrillDraw::area(const IMatchRenderer& renderer, ImDrawList& draw,
                     PitchPoint min, PitchPoint max, ImU32 color, float thickness)
{
  line(renderer, draw, PitchPoint{min.x, min.y}, PitchPoint{max.x, min.y}, color, thickness);
  line(renderer, draw, PitchPoint{max.x, min.y}, PitchPoint{max.x, max.y}, color, thickness);
  line(renderer, draw, PitchPoint{max.x, max.y}, PitchPoint{min.x, max.y}, color, thickness);
  line(renderer, draw, PitchPoint{min.x, max.y}, PitchPoint{min.x, min.y}, color, thickness);
}

void DrillDraw::cone(const IMatchRenderer& renderer, ImDrawList& draw,
                     PitchPoint at, ImU32 color, const char* text)
{
  // To scale: its radius across the pitch as drawn (at least a pixel).
  ImVec2 centre;
  ImVec2 edge;
  if (!project(renderer, at, centre) ||
      !project(renderer,
               PitchPoint{at.x + CONE_RADIUS_METRES, at.y},
               edge))
    return;
  const float radius =
      std::max(1.0f, std::hypot(edge.x - centre.x, edge.y - centre.y));
  // Outlined in its own colour: drawn this small, a dark outline would
  // swamp the fill.
  draw.AddCircleFilled(centre, radius, color);
  draw.AddCircle(centre, radius, color, 0, 1.5f);
  if (text) label(renderer, draw, at, text, color);
}

void DrillDraw::marker(const IMatchRenderer& renderer, ImDrawList& draw,
                       PitchPoint at, float radius, ImU32 color, const char* text)
{
  ImVec2 point;
  if (!project(renderer, at, point)) return;
  draw.AddCircleFilled(point, radius, color);
  draw.AddCircle(point, radius, IM_COL32(0, 0, 0, 200), 0, 1.5f);
  if (text)
  {
    const ImVec2 at2(point.x + radius + 3.0f, point.y - radius - 3.0f);
    const ImVec2 size = ImGui::CalcTextSize(text);
    draw.AddRectFilled(ImVec2(at2.x - 2.0f, at2.y - 1.0f),
                       ImVec2(at2.x + size.x + 2.0f, at2.y + size.y + 1.0f),
                       IM_COL32(0, 0, 0, 170), 3.0f);
    draw.AddText(at2, color, text);
  }
}

void DrillDraw::path(const IMatchRenderer& renderer, ImDrawList& draw,
                     const std::vector<PitchPoint>& points, ImU32 color,
                     float thickness)
{
  for (std::size_t index = 1; index < points.size(); ++index)
    line(renderer, draw, points[index - 1], points[index], color, thickness);
}

void DrillDraw::circle(const IMatchRenderer& renderer, ImDrawList& draw,
                       PitchPoint centre, float radiusMetres, ImU32 fill,
                       ImU32 outline, float thickness)
{
  // Points round it on the ground, each projected (the view may tilt).
  constexpr int POINTS = 48;
  std::array<ImVec2, POINTS> screen{};
  for (int index = 0; index < POINTS; ++index)
  {
    const float angle = 2.0f * std::numbers::pi_v<float> *
                        static_cast<float>(index) / static_cast<float>(POINTS);
    const PitchPoint at{
        centre.x + (std::cos(angle) * radiusMetres),
        centre.y + (std::sin(angle) * radiusMetres)};
    if (!project(renderer, at, screen[static_cast<std::size_t>(index)])) return;
  }
  if (fill != 0) draw.AddConvexPolyFilled(screen.data(), POINTS, fill);
  if (outline != 0)
    draw.AddPolyline(screen.data(), POINTS, outline, ImDrawFlags_Closed,
                     thickness);
}

// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_drill.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <numbers>

#include "gui/render/imatch_renderer.h"
#include "gui/view_models/formation.h"
#include "gui/widgets/text.h"
#include "gui/widgets/theme.h"
#include "model/lineup.h"
#include "model/player.h"
#include "model/strategy.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_setup.h"

namespace
{
/** Ids well clear of the world's players. */
constexpr PlayerID FIRST_ID = 900'000;
constexpr std::size_t SQUAD = 11;
/** Drill players: 25 years old, 180 cm unless the drill says. */
constexpr std::uint8_t DRILL_PLAYER_AGE = 25;
constexpr std::uint8_t DEFAULT_HEIGHT_CM = 180;
/** Parameter sliders show three decimals across a range this narrow. */
constexpr float FINE_SLIDER_RANGE = 2.5F;
/** Pitch labels: a dark box this far round the text, rounded. */
constexpr float LABEL_PAD_X = 2.0F;
constexpr float LABEL_PAD_Y = 1.0F;
constexpr float LABEL_ROUNDING = 3.0F;
constexpr float MARKER_LABEL_GAP = 3.0F;
constexpr float OUTLINE_THICKNESS = 1.5F;
/** Form spacing (scaled). */
constexpr float FORM_PADDING_X = 8.0F;
constexpr float FORM_PADDING_Y = 4.0F;
constexpr float FORM_SPACING_X = 6.0F;
constexpr float FORM_SPACING_Y = 2.0F;
constexpr float FORM_CELL_PADDING = 3.0F;
}  // namespace

namespace
{
/** Every stat at the reference rating, then the drill player's own. */
std::map<std::string, float> drillPlayerStats(const StatsConfig& config,
                                              const DrillPlayerSpec* spec)
{
  std::map<std::string, float> stats;
  const float referenceRating = spec != nullptr
                                    ? spec->referenceRating.value_or(
                                          DrillRun::REFERENCE_RATING)
                                    : DrillRun::REFERENCE_RATING;
  for (const std::string& stat : config.possible_stats)
  {
    stats[stat] = referenceRating;
  }
  if (spec != nullptr)
  {
    for (const auto& [stat, value] : spec->stats)
    {
      stats[stat] = value;
    }
  }
  return stats;
}

/** A player into his slot: in goal for the first, else the shape's place. */
void placeInSlot(Lineup& lineup, Player* player, std::size_t slot,
                 const Formation::Preset& shape)
{
  if (slot == 0)
  {
    lineup.setGoalkeeper(player);
  }
  else
  {
    lineup.addOutfieldPlayer(player, shape.slots.at(slot - 1).position);
  }
}
}  // namespace

Player* DrillRun::playerFor(const Slot& at,
                            const std::vector<DrillPlayerSpec>& specs,
                            const StatsConfig& config,
                            const std::vector<Player*>& squad, PlayerID& nextId)
{
  const Formation::Preset& shape = Formation::PRESETS[0];
  const bool keeperSlot = at.slot == 0;
  const DrillPlayerSpec* spec = at.spec ? &specs.at(*at.spec) : nullptr;
  // A session's own player in a drill player's place (section 23).
  if (at.spec && !squad.empty())
  {
    Player* own = *at.spec < squad.size() ? squad[*at.spec] : nullptr;
    if (own != nullptr)
    {
      drillIds[*at.spec] = own->getId();
      return own;
    }
  }
  PlayerRole role = PlayerRole::GK;
  if (!keeperSlot)
  {
    role = (spec != nullptr) ? spec->role : shape.slots.at(at.slot - 1).role;
  }
  const std::string name =
      (spec != nullptr) ? spec->name : std::format("Reference {}", at.slot + 1);
  auto player = std::make_unique<Player>(
      nextId++, static_cast<TeamID>(at.side + 1), "", name, role, Language::EN,
      0, 0, DRILL_PLAYER_AGE, 1,
      static_cast<std::uint8_t>((spec != nullptr) ? spec->height
                                                  : DEFAULT_HEIGHT_CM),
      (spec != nullptr) ? spec->foot : Foot::Right,
      drillPlayerStats(config, spec));
  if (at.spec)
  {
    drillIds[*at.spec] = player->getId();
  }
  pool.push_back(std::move(player));
  return pool.back().get();
}

void DrillRun::fillSide(std::size_t side,
                        const std::vector<DrillPlayerSpec>& specs,
                        const StatsConfig& config,
                        const std::vector<Player*>& squad, Lineup& lineup,
                        PlayerID& nextId)
{
  const Formation::Preset& shape = Formation::PRESETS[0];
  const bool home = side == 0;
  // This side's drill players: the keeper first, then outfielders.
  std::vector<std::size_t> keepers;
  std::vector<std::size_t> outfield;
  for (std::size_t index = 0; index < specs.size(); ++index)
  {
    if (specs[index].homeTeam == home)
    {
      (specs[index].role == PlayerRole::GK ? keepers : outfield)
          .push_back(index);
    }
  }
  for (std::size_t slot = 0; slot < SQUAD; ++slot)
  {
    const std::vector<std::size_t>& own = slot == 0 ? keepers : outfield;
    const std::size_t ownIndex = slot == 0 ? 0 : slot - 1;
    const Slot at{.side = side,
                  .slot = slot,
                  .spec = ownIndex < own.size()
                              ? std::optional<std::size_t>(own[ownIndex])
                              : std::nullopt};
    placeInSlot(lineup, playerFor(at, specs, config, squad, nextId), slot,
                shape);
  }
}

/** The engine set as the sandbox has it, for the drill's players only. */
void DrillRun::prepareEngine(const std::vector<DrillPlayerSpec>& specs,
                             bool record)
{
  // Home advantage is a layer: none in a drill without them.
  applyHomeAdvantage(*match, drill.layers() ? sandboxHomeAdvantage() : 0.0F);
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
  match->setDrillRules(
      {.offside = false, .minimumPlayers = false, .layers = drill.layers()});
  // Only the drill's players take part.
  for (const auto& player : pool)
  {
    if (std::ranges::find(drillIds, player->getId()) == drillIds.end())
    {
      (void)match->removeBeforeKickOff(player->getId());
    }
  }
  for (std::size_t index = 0; index < specs.size(); ++index)
  {
    for (const auto& [stat, value] : specs[index].derived)
    {
      match->setDerivedStat(drillIds[index], stat, value / 100.0F);
    }
    if (specs[index].condition < 100.0F)
    {
      (void)match->setPlayerCondition(drillIds[index],
                                      specs[index].condition / 100.0F);
    }
  }
  if (record)
  {
    match->setRecorder(log.get());
  }
}

DrillRun::DrillRun(Drill& drill_ref, const StatsConfig& config, bool record,
                   const std::function<void(MatchEngine&)>& prepare,
                   const std::vector<Player*>& squad)
    : drill(drill_ref), log(std::make_unique<SandboxRecorder>())
{
  // Two full squads of reference players, the drill's own players in the
  // first slots of their side (a keeper in goal). The whole squads count
  // for the engine's level adjustment, so it is the same in every run.
  const std::vector<DrillPlayerSpec> specs = drill.players();
  std::array<Lineup, 2> lineups;
  PlayerID nextId = FIRST_ID;
  drillIds.assign(specs.size(), 0);
  for (std::size_t side = 0; side < 2; ++side)
  {
    fillSide(side, specs, config, squad, lineups.at(side), nextId);
  }
  match = std::make_unique<MatchEngine>(
      lineups[0], lineups[1], Strategy{}, Strategy{}, config, drill.seed,
      sandboxHandshake(), sandboxPossession(),
      squad.empty() ? sandboxDrillPitch().value_or(drill.pitch())
                    : drill.pitch());
  prepareEngine(specs, record);
  drill.begin(*match, drillIds);
  if (prepare)
  {
    prepare(*match);
  }
}

DrillRun::~DrillRun()
{
  // The engine and its saved copies borrow the players: release them first.
  if (match)
  {
    match->setRecorder(nullptr);
  }
  match.reset();
  log.reset();
}

bool DrillRun::step()
{
  if (over)
  {
    return false;
  }
  drill.beforeStep(*match);
  match->advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  if (!drill.afterStep(*match) || match->getState() == MatchState::FULL_TIME)
  {
    over = true;
  }
  return !over;
}

bool Drill::applyPreset(const std::string& name)
{
  for (const DrillPreset& preset : presets())
  {
    if (preset.name != name)
    {
      continue;
    }
    for (const DrillParameter& parameter : parameters())
    {
      if (parameter.value != nullptr)
      {
        if (const auto found = preset.settings.find(parameter.name);
            found != preset.settings.end())
        {
          *parameter.value = found->second;
        }
      }
    }
    return true;
  }
  return false;
}

namespace
{
/** The two-column form the settings go in, opened at a section's first
 * setting and closed at its end (one column: no form). */
class SettingsForm
{
 public:
  explicit SettingsForm(bool twoColumns) : split(twoColumns) {}
  SettingsForm(const SettingsForm&) = delete;
  SettingsForm& operator=(const SettingsForm&) = delete;
  SettingsForm(SettingsForm&&) = delete;
  SettingsForm& operator=(SettingsForm&&) = delete;
  ~SettingsForm() { close(); }

  void close()
  {
    if (!open)
    {
      return;
    }
    ImGui::EndTable();
    ImGui::PopStyleVar(3);
    ImGui::PopID();
    open = false;
  }

  /** Moves to the next cell; false if the form could not open. */
  bool next()
  {
    if (!split)
    {
      return true;
    }
    if (!open && !begin())
    {
      return false;
    }
    ImGui::TableNextColumn();
    return true;
  }

 private:
  bool begin()
  {
    ImGui::PushID(id++);
    const float scale = Theme::scale();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(FORM_PADDING_X * scale, FORM_PADDING_Y * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(FORM_SPACING_X * scale, FORM_SPACING_Y * scale));
    ImGui::PushStyleVar(
        ImGuiStyleVar_CellPadding,
        ImVec2(FORM_CELL_PADDING * scale, FORM_CELL_PADDING * scale));
    open = ImGui::BeginTable(
        "##parameter_columns", 2,
        ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg);
    if (!open)
    {
      ImGui::PopStyleVar(3);
      ImGui::PopID();
    }
    return open;
  }

  bool split;
  bool open = false;
  int id = 0;
};

/** A picker that fills the settings in; it keeps no state of its own. */
void renderPresetPicker(Drill& drill, const std::vector<DrillPreset>& list,
                        bool stackedLabels)
{
  if (stackedLabels)
  {
    ImGui::TextUnformatted("Situation");
    ImGui::SetNextItemWidth(-FLT_MIN);
  }
  if (!SandboxPageUI::beginCombo(stackedLabels ? "##situation" : "Situation",
                                 "choose to fill in the settings..."))
  {
    return;
  }
  for (const DrillPreset& preset : list)
  {
    if (ImGui::Selectable(preset.name.c_str()))
    {
      (void)drill.applyPreset(preset.name);
    }
    if (!preset.summary.empty() && ImGui::IsItemHovered())
    {
      UI::tooltip("{}", preset.summary.c_str());
    }
  }
  ImGui::EndCombo();
}

/** A section starts where its title changes (by text: settings may each
 * hold their own copy of it). */
bool startsSection(const DrillParameter& parameter, const char* section)
{
  return (parameter.section != nullptr) &&
         ((section == nullptr) || std::strcmp(parameter.section, section) != 0);
}

/** A section's title; false if it is a closed collapsible one. */
bool sectionOpen(const DrillParameter& parameter)
{
  if (parameter.collapsible)
  {
    return ImGui::CollapsingHeader(parameter.section);
  }
  ImGui::SeparatorText(parameter.section);
  return true;
}

/** Small ranges (tuning knobs) need more decimals. An automatic setting
 * shows the value it is set to, in the warning colour, until moved. */
void floatSetting(const DrillParameter& parameter, const std::string& id)
{
  float& value = *parameter.value;
  const bool automatic = parameter.automatic && value == 0.0F;
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
          parameter.max - parameter.min < FINE_SLIDER_RANGE ? "%.3f" : "%.1f"))
  {
    value = shown;
  }
  if (automatic)
  {
    ImGui::PopStyleColor(3);
  }
}

/** One setting's control: a choice, a whole number or a slider. */
void settingControl(const DrillParameter& parameter, const std::string& id)
{
  float& value = *parameter.value;
  if (!parameter.choices.empty())
  {
    int index = static_cast<int>(value);
    if (SandboxPageUI::combo(id.c_str(), &index, parameter.choices.data(),
                             static_cast<int>(parameter.choices.size())))
    {
      value = static_cast<float>(index);
    }
  }
  else if (parameter.integer)
  {
    int whole = static_cast<int>(std::lround(value));
    if (ImGui::SliderInt(id.c_str(), &whole, static_cast<int>(parameter.min),
                         static_cast<int>(parameter.max)))
    {
      value = static_cast<float>(whole);
    }
  }
  else
  {
    floatSetting(parameter, id);
  }
}

/** One setting: its label (above it when stacked), its control, its tip. */
void renderSetting(const DrillParameter& parameter, bool stacked)
{
  if (stacked)
  {
    // Clear a baseline inherited from the neighbouring cell's framed input.
    ImGui::Dummy(ImVec2(0, 0));
    UI::textWrapped(
        "{}", (parameter.label != nullptr) ? parameter.label : parameter.name);
    if ((parameter.tooltip != nullptr) && ImGui::IsItemHovered())
    {
      UI::tooltip("{}", parameter.tooltip);
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
  }
  std::string id = std::string("##") + parameter.name;
  if (!stacked)
  {
    id = (parameter.label != nullptr)
             ? std::format("{}##{}", parameter.label, parameter.name)
             : std::string(parameter.name);
  }
  settingControl(parameter, id);
  if ((parameter.tooltip != nullptr) && ImGui::IsItemHovered())
  {
    UI::tooltip("{}", parameter.tooltip);
  }
}
}  // namespace

void Drill::renderSettings(int columns, bool includeSeed, bool stackedLabels)
{
  if (const std::vector<DrillPreset> list = presets(); !list.empty())
  {
    renderPresetPicker(*this, list, stackedLabels);
  }
  const bool split = columns == 2;
  const bool stacked = split || stackedLabels;
  SettingsForm form(split);
  // A closed collapsible section hides its settings.
  const char* section = nullptr;
  bool open = true;
  for (const DrillParameter& parameter : parameters())
  {
    if (parameter.value == nullptr)
    {
      continue;
    }
    if (startsSection(parameter, section))
    {
      form.close();
      section = parameter.section;
      open = sectionOpen(parameter);
    }
    if (open && form.next())
    {
      renderSetting(parameter, stacked);
    }
  }
  if (includeSeed && form.next())
  {
    if (stacked)
    {
      ImGui::Dummy(ImVec2(0, 0));
      ImGui::TextUnformatted("Seed");
      ImGui::SetNextItemWidth(-FLT_MIN);
    }
    ImGui::InputScalar(stacked ? "##seed" : "Seed", ImGuiDataType_U32, &seed);
  }
  form.close();
}

// --- Drawing ---------------------------------------------------------------

bool DrillDraw::project(const IMatchRenderer& renderer, PitchPoint pitch,
                        ImVec2& screen)
{
  return renderer.projectPitch(pitch, 0.0F, screen.x, screen.y);
}

void DrillDraw::line(const IMatchRenderer& renderer, ImDrawList& draw,
                     PitchPoint from, PitchPoint to, ImU32 color,
                     float thickness)
{
  ImVec2 a;
  ImVec2 b;
  if (project(renderer, from, a) && project(renderer, to, b))
  {
    draw.AddLine(a, b, color, thickness);
  }
}

void DrillDraw::label(const IMatchRenderer& renderer, ImDrawList& draw,
                      PitchPoint at, const char* text, ImU32 color)
{
  ImVec2 point;
  if (!project(renderer, at, point))
  {
    return;
  }
  const ImVec2 size = ImGui::CalcTextSize(text);
  draw.AddRectFilled(
      ImVec2(point.x - LABEL_PAD_X, point.y - LABEL_PAD_Y),
      ImVec2(point.x + size.x + LABEL_PAD_X, point.y + size.y + LABEL_PAD_Y),
      IM_COL32(0, 0, 0, 170), LABEL_ROUNDING);
  draw.AddText(point, color, text);
}

void DrillDraw::area(const IMatchRenderer& renderer, ImDrawList& draw,
                     PitchPoint min, PitchPoint max, ImU32 color,
                     float thickness)
{
  line(renderer, draw, PitchPoint{min.x, min.y}, PitchPoint{max.x, min.y},
       color, thickness);
  line(renderer, draw, PitchPoint{max.x, min.y}, PitchPoint{max.x, max.y},
       color, thickness);
  line(renderer, draw, PitchPoint{max.x, max.y}, PitchPoint{min.x, max.y},
       color, thickness);
  line(renderer, draw, PitchPoint{min.x, max.y}, PitchPoint{min.x, min.y},
       color, thickness);
}

void DrillDraw::cone(const IMatchRenderer& renderer, ImDrawList& draw,
                     PitchPoint at, ImU32 color, const char* text)
{
  // To scale: its radius across the pitch as drawn (at least a pixel).
  ImVec2 centre;
  ImVec2 edge;
  if (!project(renderer, at, centre) ||
      !project(renderer, PitchPoint{at.x + CONE_RADIUS_METRES, at.y}, edge))
  {
    return;
  }
  const float radius =
      std::max(1.0F, std::hypot(edge.x - centre.x, edge.y - centre.y));
  // Outlined in its own colour: drawn this small, a dark outline would
  // swamp the fill.
  draw.AddCircleFilled(centre, radius, color);
  draw.AddCircle(centre, radius, color, 0, OUTLINE_THICKNESS);
  if (text != nullptr)
  {
    label(renderer, draw, at, text, color);
  }
}

void DrillDraw::marker(const IMatchRenderer& renderer, ImDrawList& draw,
                       PitchPoint at, float radius, ImU32 color,
                       const char* text)
{
  ImVec2 point;
  if (!project(renderer, at, point))
  {
    return;
  }
  draw.AddCircleFilled(point, radius, color);
  draw.AddCircle(point, radius, IM_COL32(0, 0, 0, 200), 0, OUTLINE_THICKNESS);
  if (text != nullptr)
  {
    const ImVec2 at2(point.x + radius + MARKER_LABEL_GAP,
                     point.y - radius - MARKER_LABEL_GAP);
    const ImVec2 size = ImGui::CalcTextSize(text);
    draw.AddRectFilled(
        ImVec2(at2.x - LABEL_PAD_X, at2.y - LABEL_PAD_Y),
        ImVec2(at2.x + size.x + LABEL_PAD_X, at2.y + size.y + LABEL_PAD_Y),
        IM_COL32(0, 0, 0, 170), LABEL_ROUNDING);
    draw.AddText(at2, color, text);
  }
}

void DrillDraw::path(const IMatchRenderer& renderer, ImDrawList& draw,
                     const std::vector<PitchPoint>& points, ImU32 color,
                     float thickness)
{
  for (std::size_t index = 1; index < points.size(); ++index)
  {
    line(renderer, draw, points[index - 1], points[index], color, thickness);
  }
}

// Argument order: radius, fill, outline, as ImDrawList takes them.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
void DrillDraw::circle(const IMatchRenderer& renderer, ImDrawList& draw,
                       PitchPoint centre, float radiusMetres, ImU32 fill,
                       ImU32 outline, float thickness)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
  // Points round it on the ground, each projected (the view may tilt).
  constexpr int POINTS = 48;
  std::array<ImVec2, POINTS> screen{};
  for (int index = 0; index < POINTS; ++index)
  {
    const float angle = 2.0F * std::numbers::pi_v<float> *
                        static_cast<float>(index) / static_cast<float>(POINTS);
    const PitchPoint at{centre.x + (std::cos(angle) * radiusMetres),
                        centre.y + (std::sin(angle) * radiusMetres)};
    if (!project(renderer, at, screen.at(static_cast<std::size_t>(index))))
    {
      return;
    }
  }
  if (fill != 0)
  {
    draw.AddConvexPolyFilled(screen.data(), POINTS, fill);
  }
  if (outline != 0)
  {
    draw.AddPolyline(screen.data(), POINTS, outline, ImDrawFlags_Closed,
                     thickness);
  }
}

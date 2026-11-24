// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_brain_flow.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/view_models/formation.h"
#include "gui/widgets/theme.h"
#include "model/match_engine.h"
#include "model/match_game_xg.h"
#include "model/match_scenario.h"
#include "model/player.h"
#include "model/strategy.h"
#include "model/tactics.h"
#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_typography.h"

namespace
{
using BrainFlow::Layer;
using BrainFlow::OptionIndex;

constexpr std::array<const char*, BrainFlow::LAYER_COUNT> LAYER_NAMES = {
    "Team instructions", "Role & duty", "Shout",
    "Opposition orders", "Game state",  "Familiarity & mood"};
constexpr std::array<const char*, BrainFlow::LAYER_COUNT> LAYER_TAGS = {
    "TI", "RO", "SH", "OO", "GS", "MO"};
constexpr std::array<ImU32, BrainFlow::LAYER_COUNT> LAYER_COLOURS = {
    IM_COL32(110, 165, 235, 255), IM_COL32(235, 165, 85, 255),
    IM_COL32(185, 135, 225, 255), IM_COL32(230, 105, 105, 255),
    IM_COL32(95, 205, 185, 255),  IM_COL32(230, 210, 95, 255)};
constexpr std::array<const char*, BrainFlow::OPTION_COUNT> OPTION_NAMES = {
    "Pass", "Shot", "Carry", "Shield", "Clear"};
/** Not yet in the plan's terms, or not built: a warning colour. */
constexpr ImU32 WARNING = IM_COL32(235, 120, 70, 255);
/** A change smaller than this shows as no change. */
constexpr float CHANGE_EPSILON = 0.005F;
// Layout, in unscaled pixels.
constexpr float PANEL_WIDTH = 340.0F;
constexpr float PANEL_SHARE = 0.3F;
constexpr float STAGE_PADDING_X = 12.0F;
constexpr float STAGE_PADDING_Y = 10.0F;
constexpr float TITLE_SIZE = 14.0F;
/** The gap between two stages, which holds the down arrow. */
constexpr float ARROW_GAP = 22.0F;
constexpr float ARROW_HEAD = 6.0F;
constexpr float ARROW_LINE = 2.0F;

/** One stage of the flow: a full-width band, as tall as its content. */
struct StageSpec
{
  const char* id;
  const char* title;
  /** False: not built yet (drawn darker). */
  bool built;
};
constexpr StageSpec SITUATION{
    .id = "##situation", .title = "SITUATION", .built = true};
constexpr StageSpec READING{
    .id = "##reading", .title = "READING", .built = false};
constexpr StageSpec GOAL{.id = "##goal", .title = "GOAL", .built = true};
constexpr StageSpec OPTIONS{
    .id = "##options", .title = "OPTIONS", .built = true};
constexpr StageSpec MEASURES{
    .id = "##measures", .title = "MEASURES", .built = true};
constexpr StageSpec WEIGHTS{
    .id = "##weights", .title = "WEIGHTS", .built = true};
constexpr StageSpec JUDGEMENT{
    .id = "##judgement", .title = "JUDGEMENT", .built = true};
constexpr StageSpec PICK{.id = "##pick", .title = "PICK", .built = true};
constexpr StageSpec BODY{.id = "##body", .title = "BODY", .built = true};
constexpr StageSpec OFF_BALL{
    .id = "##off_ball", .title = "OFF THE BALL", .built = true};
void readingStage();
void goalStage();

std::size_t at(Layer layer) { return static_cast<std::size_t>(layer); }
std::size_t at(OptionIndex option) { return static_cast<std::size_t>(option); }

/** The layer a term's own weight comes from, if a switchable one. */
std::optional<Layer> layerOfTerm(TermSource source)
{
  switch (source)
  {
    case TermSource::SLOT_ROLE:
      return Layer::ROLE;
    case TermSource::INSTRUCTION:
      return Layer::OPPOSITION_ORDERS;
    case TermSource::GAME_STATE:
      return Layer::GAME_STATE;
    default:
      return std::nullopt;
  }
}

void text(const std::string& line, ImU32 colour = SandboxPageUI::CREAM)
{
  ImGui::PushStyleColor(ImGuiCol_Text, colour);
  ImGui::PushTextWrapPos(0);
  ImGui::TextUnformatted(line.c_str());
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

void muted(const std::string& line) { text(line, SandboxPageUI::MUTED); }

/** A small coloured tag for a layer, then the rest of the line. */
void tagged(Layer layer, const std::string& line)
{
  ImGui::PushStyleColor(ImGuiCol_Text, LAYER_COLOURS.at(at(layer)));
  ImGui::TextUnformatted(LAYER_TAGS.at(at(layer)));
  ImGui::PopStyleColor();
  ImGui::SameLine();
  text(line);
}

/** "a -> b" when the layers change a value, else "a". */
std::string change(float before, float after, const char* format = "{:.2f}")
{
  std::string from = std::vformat(format, std::make_format_args(before));
  if (std::abs(after - before) < CHANGE_EPSILON)
  {
    return from;
  }
  return from + " -> " + std::vformat(format, std::make_format_args(after));
}

/** The example: a carrier, three team-mates and five opponents where the
 * settings put them, scored by the brain (never played on). */
class BrainFlowExample final : public Drill
{
 public:
  explicit BrainFlowExample(const BrainFlowScene::ExampleSettings& chosen)
      : settings(chosen)
  {
  }
  [[nodiscard]] const char* name() const override { return "Brain flow"; }
  [[nodiscard]] const char* summary() const override { return ""; }
  [[nodiscard]] std::vector<DrillParameter> parameters() override { return {}; }
  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    const auto spec = [](const char* who, PlayerRole role, bool home)
    {
      DrillPlayerSpec player;
      player.name = who;
      player.role = role;
      player.homeTeam = home;
      return player;
    };
    DrillPlayerSpec carrier = spec("Carrier", PlayerRole::CM, true);
    carrier.stats = {{"Passing", settings.passing},
                     {"Shooting", settings.shooting},
                     {"Dribbling", settings.dribbling},
                     {"Pace", settings.pace},
                     {"Vision", settings.vision}};
    carrier.derived = {{DerivedStat::DECISIONS, settings.decisions}};
    return {carrier,
            spec("Left midfielder", PlayerRole::CM, true),
            spec("Right winger", PlayerRole::RW, true),
            spec("Striker", PlayerRole::ST, true),
            spec("Keeper", PlayerRole::GK, false),
            spec("Presser", PlayerRole::CM, false),
            spec("Midfielder", PlayerRole::CM, false),
            spec("Left centre-back", PlayerRole::CB, false),
            spec("Right centre-back", PlayerRole::CB, false)};
  }
  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    // Fractions of the match pitch, the home side attacking to the right;
    // the striker (the fourth) is making a run.
    constexpr std::size_t STRIKER = 3;
    const MatchPitch pitch;
    scenario = {};
    for (std::size_t index = 0;
         index < ids.size() && index < settings.spots.size(); ++index)
    {
      const auto& spot = settings.spots.at(index);
      scenario.players.push_back({.playerId = ids[index],
                                  .position = pitch.at(spot[0], spot[1]),
                                  .makingRun = index == STRIKER,
                                  .facing = std::nullopt});
    }
    scenario.carrierId = ids.empty() ? 0 : ids.front();
    scenario.ballPosition =
        scenario.players.empty()
            ? pitch.at(MatchTuning::Pitch::CENTRE, MatchTuning::Pitch::CENTRE)
            : scenario.players.front().position;
    (void)engine.startDrill(scenario);
    playerIds = ids;
  }
  bool afterStep(MatchEngine& /*engine*/) override { return true; }
  [[nodiscard]] std::vector<std::string> report() const override { return {}; }
  [[nodiscard]] std::vector<DrillMetric> metrics() const override { return {}; }
  /** The scenario the last start loaded, and its players in spot order. */
  [[nodiscard]] const MatchScenario& startingScenario() const
  {
    return scenario;
  }
  [[nodiscard]] const std::vector<PlayerID>& spotPlayerIds() const
  {
    return playerIds;
  }

 private:
  MatchScenario scenario;
  std::vector<PlayerID> playerIds;
  BrainFlowScene::ExampleSettings settings;
};

// The example's presets: where the nine players stand.
/** The example players, in spot order (ExampleSettings::spots). */
constexpr std::array<const char*, BrainFlowScene::EXAMPLE_PLAYERS> SPOT_NAMES =
    {"Carrier",          "Left midfielder",   "Right winger",
     "Striker",          "Their keeper",      "Their presser",
     "Their midfielder", "Their centre-back", "Their centre-back 2"};
constexpr std::array<const char*, 3> PRESET_NAMES = {
    "Build-up in midfield", "Edge of the box", "Pressed near his own goal"};
constexpr std::array<BrainFlowScene::Spots, 3> PRESET_SPOTS = {{
    {{{0.45F, 0.50F},
      {0.50F, 0.28F},
      {0.63F, 0.84F},
      {0.72F, 0.52F},
      {0.98F, 0.50F},
      {0.48F, 0.47F},
      {0.56F, 0.62F},
      {0.80F, 0.40F},
      {0.80F, 0.60F}}},
    {{{0.80F, 0.45F},
      {0.78F, 0.22F},
      {0.84F, 0.80F},
      {0.90F, 0.55F},
      {0.99F, 0.50F},
      {0.83F, 0.47F},
      {0.76F, 0.60F},
      {0.92F, 0.42F},
      {0.92F, 0.60F}}},
    {{{0.15F, 0.30F},
      {0.25F, 0.18F},
      {0.30F, 0.70F},
      {0.50F, 0.50F},
      {0.98F, 0.50F},
      {0.17F, 0.28F},
      {0.22F, 0.40F},
      {0.30F, 0.35F},
      {0.28F, 0.55F}}},
}};
// Each layer's choices.
constexpr std::array<const char*, 8> CARRIER_ROLE_NAMES = {
    "Standard",       "Anchor",         "Playmaker",        "Box-to-box",
    "Second striker", "Inside forward", "Touchline winger", "False nine"};
constexpr std::array<TacticalRole, 8> CARRIER_ROLES = {
    TacticalRole::Standard,        TacticalRole::Anchor,
    TacticalRole::Playmaker,       TacticalRole::BoxToBox,
    TacticalRole::SecondStriker,   TacticalRole::InsideForward,
    TacticalRole::TouchlineWinger, TacticalRole::FalseNine};
constexpr std::array<const char*, 5> STRIKER_ROLE_NAMES = {
    "Standard", "Target forward", "Poacher", "Pressing forward", "False nine"};
constexpr std::array<TacticalRole, 5> STRIKER_ROLES = {
    TacticalRole::Standard, TacticalRole::TargetForward, TacticalRole::Poacher,
    TacticalRole::PressingForward, TacticalRole::FalseNine};
constexpr std::array<const char*, 3> DUTY_NAMES = {"Defend", "Support",
                                                   "Attack"};
/** None, then the MatchShout values in order. */
constexpr std::array<const char*, 12> SHOUT_NAMES = {
    "None",      "Push higher", "Drop deeper",        "Press more",
    "Calm down", "Encourage",   "Work ball into box", "Shoot on sight",
    "Stand off", "Demand more", "Hit on the counter", "Keep possession"};
/** The default example: the first preset, each layer set so that switching
 * it on shows something, the carrier's attributes at 50. */
constexpr BrainFlowScene::ExampleSettings DEFAULT_SETTINGS{
    .spots = PRESET_SPOTS[0],
    .attacking = 0.8F,
    .risk = 0.75F,
    .carrierRole = 2,
    .carrierDuty = 2,
    .strikerRole = 1,
    .shout = 7,
    .tightMarkStriker = true,
    .weakFootCarrier = true,
    .minute = 80,
    .ourGoals = 0,
    .theirGoals = 1,
    .familiarity = 0.4F,
    .decisions = 50.0F,
    .passing = 50.0F,
    .shooting = 50.0F,
    .dribbling = 50.0F,
    .pace = 50.0F,
    .vision = 50.0F};

/** A combo over a list of names; true when the choice changes. */
template <std::size_t N>
bool pick(const char* id, int& index, const std::array<const char*, N>& list)
{
  bool changed = false;
  const auto current =
      static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(N) - 1));
  if (ImGui::BeginCombo(id, list.at(current)))
  {
    for (std::size_t option = 0; option < N; ++option)
    {
      if (ImGui::Selectable(list.at(option), option == current))
      {
        index = static_cast<int>(option);
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}
}  // namespace

BrainFlowScene::BrainFlowScene(GUIView* guiView_ptr)
    : GUIScene(guiView_ptr), settings(DEFAULT_SETTINGS)
{
}

BrainFlowScene::~BrainFlowScene() = default;

SceneID BrainFlowScene::getID() const { return SceneID::MATCH_SANDBOX; }

void BrainFlowScene::onEnter()
{
  auto* atlas = ImGui::GetIO().Fonts;
  const auto font = [&](const char* file, float size)
  {
    const auto config = SandboxTypography::manropeConfig();
    for (auto* existing : atlas->Fonts)
    {
      if (std::string_view(existing->GetDebugName()).find(file) !=
              std::string_view::npos &&
          !existing->Sources.empty() &&
          existing->Sources[0]->ExtraSizeScale == config.ExtraSizeScale)
      {
        return existing;
      }
    }
    return atlas->AddFontFromFileTTF(
        AssetPaths::file((std::string("assets/fonts/") + file).c_str()).c_str(),
        size, &config);
  };
  regularFont = font("Manrope-Regular.ttf", SandboxPageUI::REGULAR_FONT_SIZE);
  boldFont = font("Manrope-Bold.ttf", SandboxPageUI::BOLD_FONT_SIZE);
}

void BrainFlowScene::update(float /*deltaTime*/) {}

bool BrainFlowScene::layerOn(Layer layer) const
{
  return BrainFlow::isOn(layers, layer);
}

std::string BrainFlowScene::nameOf(std::uint32_t player) const
{
  const auto found = names.find(player);
  return found == names.end() ? std::string("nobody") : found->second;
}

void BrainFlowScene::rebuild()
{
  dirty = false;
  error.clear();
  BrainFlowExample example(settings);
  example.seed = rollSeed;
  const Formation::Preset& shape = Formation::PRESETS[0];
  const auto choice = [](int index, std::size_t count)
  {
    return static_cast<std::size_t>(
        std::clamp(index, 0, static_cast<int>(count) - 1));
  };
  const auto prepare = [&](MatchEngine& engine)
  {
    // Capybara on both sides; the layers' settings for this example. The
    // drill players fill the first slots of the default shape, so the
    // carrier is slot 0 and the striker slot 3.
    engine.setBrain(true, BrainId::CAPYBARA);
    engine.setBrain(false, BrainId::CAPYBARA);
    constexpr std::size_t CARRIER_SLOT = 0;
    constexpr std::size_t STRIKER_SLOT = 3;
    Strategy home;
    home.setOffensiveBias(settings.attacking);
    home.setRiskTaking(settings.risk);
    home.setSlot({.anchor = shape.slots.at(CARRIER_SLOT).position,
                  .role = CARRIER_ROLES.at(
                      choice(settings.carrierRole, CARRIER_ROLES.size())),
                  .duty = static_cast<RoleDuty>(
                      choice(settings.carrierDuty, DUTY_NAMES.size()))});
    home.setSlot({.anchor = shape.slots.at(STRIKER_SLOT).position,
                  .role = STRIKER_ROLES.at(
                      choice(settings.strikerRole, STRIKER_ROLES.size())),
                  .duty = RoleDuty::Attack});
    engine.setStrategy(true, home);
    Strategy away;
    std::vector<PlayerInstruction> orders;
    if (settings.tightMarkStriker &&
        example.spotPlayerIds().size() > STRIKER_SLOT)
    {
      orders.push_back({.player = example.spotPlayerIds().at(STRIKER_SLOT),
                        .instruction = OppositionInstruction::TightMark});
    }
    if (settings.weakFootCarrier && !example.spotPlayerIds().empty())
    {
      orders.push_back({.player = example.spotPlayerIds().front(),
                        .instruction = OppositionInstruction::ShowWeakFoot});
    }
    away.setOppositionOrders(orders);
    engine.setStrategy(false, away);
    if (settings.shout > 0)
    {
      engine.applyShout(true, static_cast<MatchShout>(settings.shout - 1));
    }
    engine.setTacticalFamiliarity(true, settings.familiarity);
  };
  DrillRun run(example, guiView->getController().getStatsConfig(), false,
               prepare);
  MatchEngine& engine = run.engine();
  // One step applies the tactics and the shout; the scenario sets the score
  // and minute, and a second step brings the team sliders up to date.
  engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  const auto score =
      [&](BrainFlow::CarrierRecord& record, BrainFlow::LayerMask mask)
  {
    record = {};
    record.steady = steady;
    engine.setBrainFlowCapybara(&record, mask);
    const bool applied =
        engine.applyScenario(example.startingScenario(), MatchState::PLAYING,
                             static_cast<float>(settings.minute),
                             settings.ourGoals, settings.theirGoals);
    engine.setBrainFlowCapybara(nullptr, BrainFlow::ALL_LAYERS);
    return applied && record.filled;
  };
  BrainFlow::CarrierRecord warmUp;
  (void)score(warmUp, BrainFlow::ALL_LAYERS);
  engine.advance(MatchTuning::Timing::FIXED_STEP_SECONDS);
  if (!score(base, 0) || !score(layered, layers))
  {
    error =
        "The example could not be scored (the carrier's brain must be "
        "Capybara).";
  }
  names.clear();
  for (const MatchPlayer& slot : engine.getPlayers())
  {
    if (slot.player != nullptr)
    {
      names[slot.player->getId()] = slot.player->getName();
    }
  }
  // The off-ball frame of the chosen player at the same moment: the
  // example placed again, nobody deciding, the team planner run once.
  offBall = {};
  const auto& ids = example.spotPlayerIds();
  const auto pick = static_cast<std::size_t>(offBallPlayer);
  if (pick < ids.size())
  {
    BrainFlow::CarrierRecord holder;
    holder.offBall.player = ids.at(pick);
    if (engine.brainFlowOffBallCapybara(
            example.startingScenario(), static_cast<float>(settings.minute),
            settings.ourGoals, settings.theirGoals, holder))
    {
      offBall = holder.offBall;
    }
  }
  exampleIds.assign(example.spotPlayerIds().begin(),
                    example.spotPlayerIds().end());
  carrierName = example.spotPlayerIds().empty()
                    ? std::string()
                    : nameOf(example.spotPlayerIds().front());
  if (!example.startingScenario().players.empty())
  {
    carrierAt = example.startingScenario().players.front().position;
  }
}

void BrainFlowScene::render()
{
  namespace Page = SandboxPageUI;
  const auto* viewport = ImGui::GetMainViewport();
  const float scale = Theme::scale();
  const Page::Style style(regularFont, scale);
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, Page::PAGE);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(SandboxPageUI::PAGE_PADDING * scale,
                             SandboxPageUI::PAGE_PADDING * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::Begin("##brain_flow", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar |
                   ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
  if (dirty)
  {
    rebuild();
  }
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float gutter =
      (SandboxPageUI::PANEL_GAP * scale) + ImGui::GetStyle().ScrollbarSize;
  const float panel = std::min(PANEL_WIDTH * scale, available.x * PANEL_SHARE);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::BeginChild("##brain_flow_controls",
                    ImVec2(panel + gutter, available.y),
                    ImGuiChildFlags_AlwaysUseWindowPadding,
                    ImGuiWindowFlags_NoScrollbar |
                        ImGuiWindowFlags_NoScrollWithMouse);
  renderPanel(panel);
  ImGui::EndChild();
  ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
  renderFlow(std::max(scale, ImGui::GetContentRegionAvail().x), available.y);
  ImGui::PopStyleVar();
  ImGui::End();
}

void BrainFlowScene::renderPanel(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::header("Tools", "Brain flow", width, scale, boldFont, regularFont,
               "The new brain framework, top to bottom, on a live example "
               "scored by Capybara's own code. Start from the default flow, "
               "then switch layers on to see what each one touches.",
               true);
  if (Page::button("Back to tools", false, scale, boldFont))
  {
    guiView->popScene();
  }

  const auto toggleCard = [&](const char* label, bool& visible)
  {
    if (Page::button(label, visible, scale, boldFont))
    {
      visible = !visible;
    }
  };
  const auto sameLineIfFits = [&](const char* label)
  {
    const float nextX = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x +
                        ImGui::GetStyle().ItemSpacing.x;
    if (nextX + Page::buttonWidth(label, scale, boldFont) <= width)
    {
      ImGui::SameLine();
    }
  };
  toggleCard("Layers", showLayers);
  sameLineIfFits("Randomness");
  toggleCard("Randomness", showRandomness);
  sameLineIfFits("Example");
  toggleCard("Example", showExample);

  ImGui::BeginChild("##brain_flow_cards", ImVec2(0, 0));
  if (showLayers)
  {
    renderLayers(width);
  }
  if (showRandomness)
  {
    renderRandomness(width);
  }
  if (showExample)
  {
    renderExample(width);
  }
  ImGui::EndChild();
}

void BrainFlowScene::renderLayers(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##brain_flow_layers", "LAYERS", width, Page::DARK, scale,
                  boldFont);
  for (std::size_t index = 0; index < BrainFlow::LAYER_COUNT; ++index)
  {
    const auto layer = static_cast<Layer>(index);
    ImGui::PushStyleColor(ImGuiCol_Text, LAYER_COLOURS.at(index));
    ImGui::TextUnformatted(LAYER_TAGS.at(index));
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (Page::choice(LAYER_NAMES.at(index), layerOn(layer), scale, boldFont))
    {
      layers = static_cast<BrainFlow::LayerMask>(
          layers ^ (1U << static_cast<unsigned>(index)));
      dirty = true;
    }
    if (layerOn(layer))
    {
      ImGui::Indent();
      ImGui::PushItemWidth(-1);
      renderLayerSettings(layer);
      ImGui::PopItemWidth();
      ImGui::Unindent();
    }
  }
  if (Page::button("All off (default flow)", false, scale, boldFont))
  {
    layers = 0;
    dirty = true;
  }
  Page::endCard(scale);
}

void BrainFlowScene::renderRandomness(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##brain_flow_randomness", "RANDOMNESS", width, Page::MID,
                  scale, boldFont);
  muted(
      "Steady shows the scores at the true chances, with the size of each "
      "misreading and of the pick's noise as a range. Roll draws one "
      "sample.");
  if (Page::choice("Steady", steady, scale, boldFont))
  {
    steady = true;
    dirty = true;
  }
  if (Page::button("Roll", !steady, scale, boldFont))
  {
    steady = false;
    ++rollSeed;
    dirty = true;
  }
  Page::endCard(scale);
}

void BrainFlowScene::renderLayerSettings(Layer layer)
{
  constexpr int LAST_MINUTE = 90;
  constexpr int MOST_GOALS = 5;
  bool changed = false;
  switch (layer)
  {
    case Layer::INSTRUCTIONS:
      changed |= ImGui::SliderFloat("##attacking", &settings.attacking, 0.0F,
                                    1.0F, "attacking %.2f");
      changed |= ImGui::SliderFloat("##risk", &settings.risk, 0.0F, 1.0F,
                                    "risk-taking %.2f");
      break;
    case Layer::ROLE:
      muted("Carrier, his duty, the striker:");
      changed |=
          pick("##carrier_role", settings.carrierRole, CARRIER_ROLE_NAMES);
      changed |= pick("##carrier_duty", settings.carrierDuty, DUTY_NAMES);
      changed |=
          pick("##striker_role", settings.strikerRole, STRIKER_ROLE_NAMES);
      break;
    case Layer::SHOUT:
      changed |= pick("##shout", settings.shout, SHOUT_NAMES);
      break;
    case Layer::OPPOSITION_ORDERS:
      changed |=
          ImGui::Checkbox("tight mark the striker", &settings.tightMarkStriker);
      changed |= ImGui::Checkbox("show the carrier onto his weaker foot",
                                 &settings.weakFootCarrier);
      break;
    case Layer::GAME_STATE:
      changed |= ImGui::SliderInt("##minute", &settings.minute, 0, LAST_MINUTE,
                                  "minute %d");
      changed |= ImGui::SliderInt("##ours", &settings.ourGoals, 0, MOST_GOALS,
                                  "our goals %d");
      changed |= ImGui::SliderInt("##theirs", &settings.theirGoals, 0,
                                  MOST_GOALS, "their goals %d");
      break;
    case Layer::MOOD:
      changed |= ImGui::SliderFloat("##familiarity", &settings.familiarity,
                                    0.0F, 1.0F, "familiarity %.2f");
      break;
    case Layer::COUNT:
      break;
  }
  if (changed)
  {
    dirty = true;
  }
}

void BrainFlowScene::renderExample(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  Page::beginCard("##brain_flow_example", "EXAMPLE", width, Page::LIGHT, scale,
                  boldFont);
  for (std::size_t index = 0; index < PRESET_NAMES.size(); ++index)
  {
    if (Page::choice(PRESET_NAMES.at(index), preset == static_cast<int>(index),
                     scale, boldFont))
    {
      preset = static_cast<int>(index);
      settings.spots = PRESET_SPOTS.at(index);
      dirty = true;
    }
  }
  renderPitch(ImGui::GetContentRegionAvail().x);
  muted(
      "Drag a player to move him. Lime: our side (the carrier ringed); red: "
      "the opponents. The chosen option is drawn bold.");
  muted("Off the ball (the OFF THE BALL stage): whose frame to show.");
  for (std::size_t index = 0; index < SPOT_NAMES.size(); ++index)
  {
    if (Page::choice(SPOT_NAMES.at(index),
                     offBallPlayer == static_cast<int>(index), scale, boldFont))
    {
      offBallPlayer = static_cast<int>(index);
      dirty = true;
    }
  }
  muted("The carrier's attributes:");
  constexpr float LOWEST = 1.0F;
  constexpr float HIGHEST = 100.0F;
  bool changed = false;
  ImGui::PushItemWidth(-1);
  changed |= ImGui::SliderFloat("##decisions", &settings.decisions, LOWEST,
                                HIGHEST, "Decisions %.0f");
  changed |= ImGui::SliderFloat("##passing", &settings.passing, LOWEST, HIGHEST,
                                "Passing %.0f");
  changed |= ImGui::SliderFloat("##shooting", &settings.shooting, LOWEST,
                                HIGHEST, "Shooting %.0f");
  changed |= ImGui::SliderFloat("##dribbling", &settings.dribbling, LOWEST,
                                HIGHEST, "Dribbling %.0f");
  changed |= ImGui::SliderFloat("##pace", &settings.pace, LOWEST, HIGHEST,
                                "Pace %.0f");
  changed |= ImGui::SliderFloat("##vision", &settings.vision, LOWEST, HIGHEST,
                                "Vision %.0f");
  ImGui::PopItemWidth();
  if (changed)
  {
    dirty = true;
  }
  if (Page::button("Reset the example", false, scale, boldFont))
  {
    settings = DEFAULT_SETTINGS;
    preset = 0;
    dirty = true;
  }
  Page::endCard(scale);
}

namespace
{
constexpr ImU32 PITCH_GRASS = IM_COL32(34, 62, 40, 255);
constexpr ImU32 PITCH_LINES = IM_COL32(200, 215, 200, 140);
constexpr ImU32 HOME_DOT = IM_COL32(155, 208, 107, 255);
constexpr ImU32 AWAY_DOT = IM_COL32(230, 105, 105, 255);
constexpr float PLAYER_RADIUS = 5.0F;
constexpr float CARRIER_RING = 9.0F;
constexpr float GRAB_RADIUS = 12.0F;
constexpr float THIN_LINE = 1.5F;
constexpr float BOLD_LINE = 3.0F;
constexpr float CANDIDATE_DOT = 2.0F;
constexpr float PITCH_EDGE = 0.01F;
// Penalty areas: 16.5 m deep and 40.32 m wide on a 105 x 68 m pitch.
constexpr float BOX_DEPTH = 16.5F / 105.0F;
constexpr float BOX_WIDTH = 40.32F / 68.0F;
/** The example's first four are the home side. */
constexpr std::size_t FIRST_OPPONENT = 4;

/** The pitch's markings: outline, halfway line and both penalty areas. */
void drawPitchMarkings(ImDrawList* draw, const BrainPitchFrame& frame)
{
  draw->AddRectFilled(frame.origin, frame.screen(1.0F, 1.0F), PITCH_GRASS);
  draw->AddRect(frame.origin, frame.screen(1.0F, 1.0F), PITCH_LINES);
  draw->AddLine(frame.screen(MatchTuning::Pitch::CENTRE, 0.0F),
                frame.screen(MatchTuning::Pitch::CENTRE, 1.0F), PITCH_LINES);
  const float boxTop = MatchTuning::Pitch::CENTRE - (BOX_WIDTH / 2.0F);
  const float boxBottom = MatchTuning::Pitch::CENTRE + (BOX_WIDTH / 2.0F);
  draw->AddRect(frame.screen(0.0F, boxTop), frame.screen(BOX_DEPTH, boxBottom),
                PITCH_LINES);
  draw->AddRect(frame.screen(1.0F - BOX_DEPTH, boxTop),
                frame.screen(1.0F, boxBottom), PITCH_LINES);
}
}  // namespace

// Argument order: across then down, as points go.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
ImVec2 BrainPitchFrame::screen(float fx, float fy) const
{
  return {origin.x + (fx * width), origin.y + (fy * height)};
}

ImVec2 BrainPitchFrame::screen(PitchPoint at) const
{
  const MatchPitch pitch;
  return screen(pitch.fractionX(at), pitch.fractionY(at));
}

ImVec2 BrainFlowScene::spotOf(const BrainPitchFrame& frame,
                              std::size_t index) const
{
  const auto& spot = settings.spots.at(index);
  return frame.screen(spot[0], spot[1]);
}

/** Dragging a player: grab the nearest within reach, move him, and the
 * example rebuilds when he is let go. */
void BrainFlowScene::dragPlayers(const BrainPitchFrame& frame)
{
  const float scale = Theme::scale();
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  if (ImGui::IsItemActivated())
  {
    float best = GRAB_RADIUS * scale;
    for (std::size_t index = 0; index < settings.spots.size(); ++index)
    {
      const ImVec2 at = spotOf(frame, index);
      const float metres = std::hypot(at.x - mouse.x, at.y - mouse.y);
      if (metres <= best)
      {
        best = metres;
        dragging = index;
      }
    }
  }
  if (dragging && ImGui::IsItemActive())
  {
    auto& spot = settings.spots.at(*dragging);
    spot[0] = std::clamp((mouse.x - frame.origin.x) / frame.width, PITCH_EDGE,
                         1.0F - PITCH_EDGE);
    spot[1] = std::clamp((mouse.y - frame.origin.y) / frame.height, PITCH_EDGE,
                         1.0F - PITCH_EDGE);
  }
  if (dragging && ImGui::IsItemDeactivated())
  {
    dragging.reset();
    preset = -1;
    dirty = true;
  }
}

/** The options from the carrier: the best pass, every carry candidate's 1 s
 * point and the best one, a clearance and the shot; the chosen one bold. */
void BrainFlowScene::drawOptionLines(ImDrawList* draw,
                                     const BrainPitchFrame& frame) const
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const MatchPitch pitch;
  const ImVec2 from = spotOf(frame, 0);
  const auto line = [&](ImVec2 to, OptionIndex option)
  {
    const bool chosen = layered.chosen == at(option);
    draw->AddLine(from, to, chosen ? Page::LIME : Page::MUTED,
                  (chosen ? BOLD_LINE : THIN_LINE) * scale);
  };
  for (std::size_t index = 0; index < exampleIds.size(); ++index)
  {
    if (exampleIds[index] == layered.bestReceiver &&
        index < settings.spots.size())
    {
      line(spotOf(frame, index), OptionIndex::PASS);
    }
  }
  for (const BrainFlow::CarryCandidate& candidate : layered.carryCandidates)
  {
    draw->AddCircleFilled(frame.screen(candidate.end), CANDIDATE_DOT * scale,
                          Page::MUTED);
  }
  line(frame.screen(layered.carryEnd), OptionIndex::CARRY);
  if (layered.clearAimed)
  {
    line(frame.screen(layered.clearTarget), OptionIndex::CLEAR);
  }
  if (layered.options.at(at(OptionIndex::SHOT)).ruledOut == nullptr)
  {
    // To the aim the shot option carries (the goal's centre if none).
    const PitchPoint aim{pitch.atX(1.0F),
                         pitch.atY(MatchTuning::Pitch::CENTRE) +
                             (layered.shotAimed ? layered.shotAimY : 0.0F)};
    line(frame.screen(1.0F, pitch.fractionY(aim)), OptionIndex::SHOT);
  }
}

/** The players; the carrier ringed. Returns the one under the mouse. */
std::optional<std::size_t> BrainFlowScene::drawPlayers(
    ImDrawList* draw, const BrainPitchFrame& frame) const
{
  const float scale = Theme::scale();
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  std::optional<std::size_t> hovered;
  for (std::size_t index = 0; index < settings.spots.size(); ++index)
  {
    const ImVec2 at = spotOf(frame, index);
    draw->AddCircleFilled(at, PLAYER_RADIUS * scale,
                          index < FIRST_OPPONENT ? HOME_DOT : AWAY_DOT);
    if (index == 0)
    {
      draw->AddCircle(at, CARRIER_RING * scale, HOME_DOT, 0, THIN_LINE * scale);
    }
    if (std::hypot(at.x - mouse.x, at.y - mouse.y) <= GRAB_RADIUS * scale)
    {
      hovered = index;
    }
  }
  return hovered;
}

/** The off-ball player: ringed, and a line to where his frame sends him. */
void BrainFlowScene::drawOffBall(ImDrawList* draw,
                                 const BrainPitchFrame& frame) const
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const auto offBallIndex = static_cast<std::size_t>(offBallPlayer);
  if (dragging || !offBall.filled || offBallIndex >= settings.spots.size())
  {
    return;
  }
  const ImVec2 at = spotOf(frame, offBallIndex);
  draw->AddCircle(at, CARRIER_RING * scale, Page::CREAM, 0, THIN_LINE * scale);
  draw->AddLine(at, frame.screen(offBall.target), Page::CREAM,
                THIN_LINE * scale);
}

void BrainFlowScene::renderPitch(float width)
{
  const MatchPitch pitch;
  const float height = width * pitch.width / pitch.length;
  const BrainPitchFrame frame{
      .origin = ImGui::GetCursorScreenPos(), .width = width, .height = height};
  ImGui::InvisibleButton("##brain_flow_pitch", ImVec2(width, height));
  dragPlayers(frame);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  drawPitchMarkings(draw, frame);
  // The options (not while dragging: they are from before the move).
  if (!dragging && layered.filled && !exampleIds.empty())
  {
    drawOptionLines(draw, frame);
  }
  const std::optional<std::size_t> hovered = drawPlayers(draw, frame);
  drawOffBall(draw, frame);
  if (hovered && ImGui::IsItemHovered() && *hovered < exampleIds.size())
  {
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(nameOf(exampleIds.at(*hovered)).c_str());
    ImGui::EndTooltip();
  }
}

namespace
{
/** A down arrow in the gap between two stages. */
void arrow()
{
  const float scale = Theme::scale();
  const ImVec2 top = ImGui::GetCursorScreenPos();
  const float middle = top.x + (ImGui::GetContentRegionAvail().x * 0.5F);
  const float bottom = top.y + (ARROW_GAP * scale);
  const float head = ARROW_HEAD * scale;
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddLine(ImVec2(middle, top.y), ImVec2(middle, bottom - head),
                SandboxPageUI::MUTED, ARROW_LINE * scale);
  draw->AddTriangleFilled(ImVec2(middle - head, bottom - head),
                          ImVec2(middle + head, bottom - head),
                          ImVec2(middle, bottom), SandboxPageUI::MUTED);
  ImGui::Dummy(ImVec2(1.0F, ARROW_GAP * scale));
}

/** A stage: a full-width band with its title, then its body drawn by
 * `body`; an arrow above it unless it is the first. */
template <typename Body>
void stage(const StageSpec& spec, ImFont* bold, bool resetOpenState,
           const Body& body)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  if (&spec != &SITUATION)
  {
    arrow();
  }
  ImGui::PushStyleColor(ImGuiCol_ChildBg,
                        spec.built ? Page::LIGHT : Page::DARK);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(STAGE_PADDING_X * scale, STAGE_PADDING_Y * scale));
  const ImGuiChildFlags childFlags =
      ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY |
      (resetOpenState ? ImGuiChildFlags_AlwaysAutoResize : 0);
  ImGui::BeginChild(spec.id, ImVec2(0, 0), childFlags);
  ImGui::PushStyleColor(ImGuiCol_Text, spec.built ? Page::LIME : Page::MUTED);
  ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Page::RULE);
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, Page::RULE);
  ImGui::PushFont(bold, TITLE_SIZE);
  if (resetOpenState)
  {
    ImGui::SetNextItemOpen(false);
  }
  const bool open = ImGui::CollapsingHeader(spec.title);
  ImGui::PopFont();
  constexpr int HEADER_COLOURS = 4;
  ImGui::PopStyleColor(HEADER_COLOURS);
  if (open)
  {
    ImGui::Separator();
    body();
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}
}  // namespace

void BrainFlowScene::renderFlow(float width, float height)
{
  if (collapseStages)
  {
    ImGui::SetNextWindowScroll(ImVec2(0, 0));
  }
  ImGui::BeginChild("##brain_flow_stages", ImVec2(width, height));
  if (!error.empty())
  {
    text(error, WARNING);
    ImGui::EndChild();
    return;
  }
  const auto renderStage = [this](const StageSpec& spec, const auto& body)
  {
    stage(spec, boldFont, collapseStages, body);
  };
  renderStage(SITUATION, [this] { situationStage(); });
  renderStage(READING, readingStage);
  renderStage(GOAL, goalStage);
  renderStage(OPTIONS, [this] { optionsStage(); });
  renderStage(MEASURES, [this] { measuresStage(); });
  renderStage(WEIGHTS, [this] { weightsStage(); });
  renderStage(JUDGEMENT, [this] { judgementStage(); });
  renderStage(PICK, [this] { pickStage(); });
  renderStage(BODY, [this] { bodyStage(); });
  renderStage(OFF_BALL, [this] { offBallStage(); });
  collapseStages = false;
  ImGui::EndChild();
}

namespace
{
constexpr BrainFlow::LayerMask bit(Layer layer)
{
  return static_cast<BrainFlow::LayerMask>(1U << static_cast<unsigned>(layer));
}
/** The team sliders carry three layers: the manager's tactics, the game
 * state (team mentality) and the shout. */
constexpr BrainFlow::LayerMask SLIDERS =
    bit(Layer::INSTRUCTIONS) | bit(Layer::GAME_STATE) | bit(Layer::SHOUT);
constexpr BrainFlow::LayerMask ROLE = bit(Layer::ROLE);
constexpr BrainFlow::LayerMask ORDERS = bit(Layer::OPPOSITION_ORDERS);
constexpr BrainFlow::LayerMask MOOD = bit(Layer::MOOD);

/** One situation of the whole brain (hand-written; keep it up to date with
 * the brain, plan section 6). */
struct Situation
{
  int depth;
  const char* title;
  /** The Capybara functions that decide it today. */
  const char* decidedBy;
  /** The layers whose inputs those functions read. */
  BrainFlow::LayerMask layers;
  /** The layers are read by the team planner for all its jobs, not traced
   * job by job yet. */
  bool plannerWide;
  /** On the decision frame: shown in full in the stages below. */
  bool onFrame;
  /** On a decision frame of its own, not shown in the stages below yet. */
  bool ownFrame;
};
constexpr std::array<Situation, 20> SITUATIONS = {{
    {.depth = 0,
     .title = "Restart",
     .decidedBy = "",
     .layers = 0,
     .plannerWide = false,
     .onFrame = false,
     .ownFrame = false},
    {.depth = 1,
     .title = "I am the taker",
     .decidedBy =
         "who takes it: chooseRestartTaker (the team's rule: designated, "
         "else the best by the stat for it). Its own frame (step H, "
         "chooseRestartPlay, chooseCornerPlay): pass, short corner, delivery "
         "into the box, direct shot (carries its aim), hold; open as before, "
         "stopgap pick in today's order (follow-up: measures). Penalties: "
         "decided before the run-up (decidePenalty), carrying its aim",
     .layers = 0,
     .plannerWide = false,
     .onFrame = false,
     .ownFrame = true},
    {.depth = 1,
     .title = "Someone else takes it",
     .decidedBy = "arrangeSetPieceAttack, arrangeSetPieceDefence, "
                  "updateRestartSupport",
     .layers = 0,
     .plannerWide = false,
     .onFrame = false,
     .ownFrame = false},
    {.depth = 1,
     .title = "I'm facing a penalty (keeper)",
     .decidedBy =
         "its own frame (step L, choosePenaltyGuess), before the kick: dive "
         "left, dive right (each carrying its height, high or low, held to "
         "the dive), stay. Open on today's blind rolls; stopgap pick is the "
         "rolled option (follow-up: measures, reading the taker)",
     .layers = 0,
     .plannerWide = false,
     .onFrame = false,
     .ownFrame = true},
    {.depth = 0,
     .title = "Open play",
     .decidedBy = "",
     .layers = 0,
     .plannerWide = false,
     .onFrame = false,
     .ownFrame = false},
    {.depth = 1,
     .title = "Ball loose",
     .decidedBy =
         "its own off-ball frame (step O3, refreshTacticalTargets): chase it "
         "(first by arrival), cover the chase, transition run or support, "
         "recover. Stopgap pick in today's order (follow-up: measures). "
         "Shadow (step O6): each player's own call (mine, in doubt, not "
         "mine) and where he could meet it, tallied against the team's "
         "pick; it changes nothing yet. Routing: planWayRound, planOvertake",
     .layers = SLIDERS | ROLE | ORDERS | MOOD,
     .plannerWide = true,
     .onFrame = false,
     .ownFrame = true},
    {.depth = 1,
     .title = "Our pass travelling",
     .decidedBy =
         "its own off-ball frame (step O3): receive it (the collector, set by "
         "afterPassReleased), or support while it travels. Stopgap pick in "
         "today's order (follow-up: measures)",
     .layers = SLIDERS | ROLE | ORDERS | MOOD,
     .plannerWide = true,
     .onFrame = false,
     .ownFrame = true},
    {.depth = 1,
     .title = "Their pass, or a shot, travelling",
     .decidedBy =
         "its own off-ball frame (step O3): chase it (the first presser: an "
         "interception), cover the chase, tight mark, recover in transition, "
         "hold shape. Stopgap pick in today's order (follow-up: measures)",
     .layers = SLIDERS | ROLE | ORDERS | MOOD,
     .plannerWide = true,
     .onFrame = false,
     .ownFrame = true},
    {.depth = 1,
     .title = "Opponents' ball",
     .decidedBy = "",
     .layers = 0,
     .plannerWide = false,
     .onFrame = false,
     .ownFrame = false},
    {.depth = 2,
     .title = "I can intercept the ball",
     .decidedBy =
         "its own frame. Head (chooseHeaderPlay, step G): head it behind "
         "(today's 33% roll inside his penalty spot), clear it, knock it "
         "down. Feet (chooseFirstTouch, step I): the opponents' pass reaching "
         "him: control touch, lay it out; first-time pass, shot, clear listed, "
         "not judged yet. Shot block (choosesToBlock, step L): throw myself "
         "in (always open), let it go; put it behind and clear a cross "
         "behind closed. Stopgap picks in today's order (follow-up: "
         "measures)",
     .layers = 0,
     .plannerWide = false,
     .onFrame = false,
     .ownFrame = true},
    {.depth = 2,
     .title = "My job: press, cover, mark or hold shape",
     .decidedBy =
         "its own off-ball frame (step O2, refreshTacticalTargets): press, "
         "double up, block a lane, cover, tight mark, recover in transition, "
         "zonal mark, hold shape. Open when the team's assignments (pressers, "
         "assignMarks), orders or phase give it to him (hold shape always); "
         "stopgap pick in today's order (follow-up: measures). Targets: "
         "engageBoost, pressStandOffShare, doubleUpPoint, tightMarkPoint. "
         "Shadow (step P1, changes nothing yet): each defender's own call on "
         "pressing the carrier (mine, in doubt, not mine), by the loose "
         "ball's rough look against the 3 team-mates nearest him",
     .layers = SLIDERS | ROLE | ORDERS | MOOD,
     .plannerWide = true,
     .onFrame = false,
     .ownFrame = true},
    {.depth = 2,
     .title = "I can challenge the carrier",
     .decidedBy =
         "its own frame (step K), three ways in. The engage roll "
         "(chooseChallenge, the older model): hold, tackle, slide in. The "
         "contest (goesIntoContest: the cue he reads against his bar): hold, "
         "go in to win it, go in to clear it; the intent carried by the "
         "option to the contact. A free ball outside the carrier's reach "
         "(chooseTackleIntent): control, clear or win it. Go in to pass it "
         "listed, not judged. Stopgap picks (follow-up: measures)",
     .layers = SLIDERS | ORDERS,
     .plannerWide = false,
     .onFrame = false,
     .ownFrame = true},
    {.depth = 2,
     .title = "Keeper",
     .decidedBy =
         "its own frame (step O4): sweep a ball going into his own goal (once "
         "he has reacted), take a team-mate's pass, take up his position "
         "(goalkeeperTarget, its own state machine, left as it is). Stopgap "
         "pick in today's order (follow-up: measures)",
     .layers = ROLE,
     .plannerWide = false,
     .onFrame = false,
     .ownFrame = true},
    {.depth = 1,
     .title = "I'm on the ground",
     .decidedBy =
         "its own frame (step J, chooseGetUp): up for the restart, jump up "
         "for the ball (near and still to be won), up gingerly (play ran on a "
         "while), stay down. Stopgap pick in today's order (follow-up: "
         "measures)",
     .layers = 0,
     .plannerWide = false,
     .onFrame = false,
     .ownFrame = true},
    {.depth = 1,
     .title = "A high ball coming down",
     .decidedBy =
         "a convention, not his choice yet (step O4): one player per side "
         "goes for it, whoever meets it first (aerialRoles); it overrides his "
         "job. During the opponents' delivery into our box the defenders hold "
         "their marks (an override)",
     .layers = 0,
     .plannerWide = true,
     .onFrame = false,
     .ownFrame = false},
    {.depth = 1,
     .title = "Our ball, a team-mate has it",
     .decidedBy =
         "its own off-ball frame (step O1, refreshTacticalTargets): run in "
         "behind, attack the box, overlap, support, come short (forwards), "
         "hold shape. Open when the team's assignments give it to him (hold "
         "shape always); stopgap pick in today's order (follow-up: measures)",
     .layers = SLIDERS | ROLE | ORDERS | MOOD,
     .plannerWide = true,
     .onFrame = false,
     .ownFrame = true},
    {.depth = 1,
     .title = "Our ball, it's coming to me",
     .decidedBy =
         "its own frame. Feet (chooseFirstTouch, step F stage 1): control "
         "touch, lay it out (along his run, when running into space); "
         "first-time pass, shot and clear listed, not judged yet. Head "
         "(chooseHeaderAttempt, step G): at goal, cushion it, knock it "
         "down. Stopgap picks in today's order (follow-up: measures)",
     .layers = 0,
     .plannerWide = false,
     .onFrame = false,
     .ownFrame = true},
    {.depth = 1,
     .title = "Our ball, my touch still rolling",
     .decidedBy = "when to play it: touchNow, dribbleTouchNow, "
                  "waitsForCloser",
     .layers = 0,
     .plannerWide = false,
     .onFrame = false,
     .ownFrame = false},
    {.depth = 1,
     .title = "Our ball, I have it (collected)",
     .decidedBy = "chooseBallAction: pass, shot, carry (direction x pace), "
                  "shield, clear",
     .layers = SLIDERS | ROLE | ORDERS | MOOD,
     .plannerWide = false,
     .onFrame = true,
     .ownFrame = false},
    {.depth = 2,
     .title = "Then: carrying it",
     .decidedBy = "follows the chosen carry (carryTarget, carryUrgency, "
                  "carryPace); no take-ons until the take-on rework",
     .layers = 0,
     .plannerWide = false,
     .onFrame = false,
     .ownFrame = false},
}};

/** The situations one level under `parent`, or the top level when there is
 * no parent. */
std::vector<std::size_t> childrenOf(std::optional<std::size_t> parent)
{
  std::vector<std::size_t> children;
  const int depth = parent ? SITUATIONS.at(*parent).depth + 1 : 0;
  for (std::size_t index = parent ? *parent + 1 : 0; index < SITUATIONS.size();
       ++index)
  {
    const int at = SITUATIONS.at(index).depth;
    if (at < depth)
    {
      break;
    }
    if (at == depth)
    {
      children.push_back(index);
    }
  }
  return children;
}

/** The situation and all those under it. */
std::size_t subtreeEnd(std::size_t index)
{
  std::size_t end = index + 1;
  while (end < SITUATIONS.size() &&
         SITUATIONS.at(end).depth > SITUATIONS.at(index).depth)
  {
    ++end;
  }
  return end;
}

/** True when the situation, or one under it, is on the decision frame. */
bool leadsToFrame(std::size_t index)
{
  for (std::size_t at = index; at < subtreeEnd(index); ++at)
  {
    if (SITUATIONS.at(at).onFrame)
    {
      return true;
    }
  }
  return false;
}

/** The layers that read the situation or any situation under it. */
BrainFlow::LayerMask subtreeLayers(std::size_t index)
{
  BrainFlow::LayerMask layers = 0;
  for (std::size_t at = index; at < subtreeEnd(index); ++at)
  {
    layers |= SITUATIONS.at(at).layers;
  }
  return layers;
}

/** The cards from the top level down to the situation on the frame. */
std::vector<std::size_t> pathToFrame()
{
  std::vector<std::size_t> path;
  std::optional<std::size_t> parent;
  while (true)
  {
    const std::vector<std::size_t> children = childrenOf(parent);
    const auto next = std::ranges::find_if(
        children, [](std::size_t at) { return leadsToFrame(at); });
    if (next == children.end())
    {
      return path;
    }
    path.push_back(*next);
    if (SITUATIONS.at(*next).onFrame)
    {
      return path;
    }
    parent = *next;
  }
}

// Situation cards, in unscaled pixels.
constexpr float CARD_PADDING_X = 10.0F;
constexpr float CARD_PADDING_Y = 6.0F;
constexpr float CARD_GAP = 6.0F;
constexpr float CARD_ROUNDING = 4.0F;
constexpr float CARD_BORDER = 1.0F;
constexpr float CARD_SELECTED_BORDER = 2.0F;
constexpr float DOT_RADIUS = 3.0F;
constexpr float DOT_GAP = 4.0F;
constexpr float DOT_ROW = 10.0F;
/** From one layer dot to the next. */
constexpr float DOT_STEP = (2.0F * DOT_RADIUS) + DOT_GAP;
/** A card's tooltip wraps at this many font heights. */
constexpr float TOOLTIP_WIDTH_EMS = 30.0F;

/** One situation card: its title (lime when it leads to the frame) and a
 * dot for each switched-on layer that reads it or a situation under it.
 * True when clicked. */
bool situationCard(std::size_t index, bool selected, BrainFlow::LayerMask on)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const Situation& situation = SITUATIONS.at(index);
  const ImVec2 title = ImGui::CalcTextSize(situation.title);
  const ImVec2 size(title.x + (2.0F * CARD_PADDING_X * scale),
                    title.y + ((2.0F * CARD_PADDING_Y + DOT_ROW) * scale));
  ImGui::PushID(static_cast<int>(index));
  const bool clicked = ImGui::InvisibleButton("##card", size);
  ImGui::PopID();
  const bool hovered = ImGui::IsItemHovered();
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(min, max, selected || hovered ? Page::MID : Page::DARK,
                      CARD_ROUNDING * scale);
  draw->AddRect(min, max, selected ? Page::LIME : Page::RULE,
                CARD_ROUNDING * scale, 0,
                (selected ? CARD_SELECTED_BORDER : CARD_BORDER) * scale);
  draw->AddText(ImVec2(min.x + (CARD_PADDING_X * scale),
                       min.y + (CARD_PADDING_Y * scale)),
                leadsToFrame(index) ? Page::LIME : Page::CREAM,
                situation.title);
  const BrainFlow::LayerMask layers = subtreeLayers(index);
  float dotX = min.x + ((CARD_PADDING_X + DOT_RADIUS) * scale);
  const float dotY = max.y - ((CARD_PADDING_Y + DOT_RADIUS) * scale);
  for (std::size_t layer = 0; layer < BrainFlow::LAYER_COUNT; ++layer)
  {
    const auto which = static_cast<Layer>(layer);
    if ((layers & bit(which)) != 0 && BrainFlow::isOn(on, which))
    {
      draw->AddCircleFilled(ImVec2(dotX, dotY), DOT_RADIUS * scale,
                            LAYER_COLOURS.at(layer));
      dotX += DOT_STEP * scale;
    }
  }
  if (hovered && *situation.decidedBy != '\0')
  {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * TOOLTIP_WIDTH_EMS);
    ImGui::TextUnformatted(situation.decidedBy);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
  return clicked;
}

/** The selected situation in full: what decides it, whether it is on the
 * frame, and the switched-on layers that read it. */
void situationDetail(const Situation& situation, BrainFlow::LayerMask on)
{
  text(situation.title,
       situation.onFrame ? SandboxPageUI::LIME : SandboxPageUI::CREAM);
  if (*situation.decidedBy == '\0')
  {
    muted("Pick a card below it.");
    return;
  }
  muted(situation.decidedBy);
  if (situation.onFrame)
  {
    text("on the decision frame: the stages below", SandboxPageUI::LIME);
  }
  else if (situation.ownFrame)
  {
    text("on a decision frame of its own (not in the stages below yet)",
         SandboxPageUI::LIME);
  }
  else
  {
    muted("not on the frame yet");
  }
  for (std::size_t layer = 0; layer < BrainFlow::LAYER_COUNT; ++layer)
  {
    const auto which = static_cast<Layer>(layer);
    if ((situation.layers & bit(which)) != 0 && BrainFlow::isOn(on, which))
    {
      tagged(which, situation.plannerWide
                        ? std::string(LAYER_NAMES.at(layer)) + " (planner-wide)"
                        : std::string(LAYER_NAMES.at(layer)));
    }
  }
}
}  // namespace

void BrainFlowScene::situationStage()
{
  if (situationPath.empty())
  {
    situationPath = pathToFrame();
  }
  const float scale = Theme::scale();
  // A row of cards for each level, down the selected path.
  std::optional<std::size_t> parent;
  for (std::size_t level = 0;; ++level)
  {
    const std::vector<std::size_t> children = childrenOf(parent);
    if (children.empty())
    {
      break;
    }
    const float right =
        ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    bool first = true;
    for (const std::size_t index : children)
    {
      if (!first)
      {
        // Wrap to a new line when the next card would not fit.
        const float next = ImGui::GetItemRectMax().x + (CARD_GAP * scale);
        const float width = ImGui::CalcTextSize(SITUATIONS.at(index).title).x +
                            (2.0F * CARD_PADDING_X * scale);
        if (next + width <= right)
        {
          ImGui::SameLine(0, CARD_GAP * scale);
        }
      }
      first = false;
      const bool selected =
          level < situationPath.size() && situationPath.at(level) == index;
      if (situationCard(index, selected, layers))
      {
        situationPath.resize(level);
        situationPath.push_back(index);
      }
    }
    if (level >= situationPath.size())
    {
      break;
    }
    parent = situationPath.at(level);
    ImGui::Dummy(ImVec2(1.0F, CARD_GAP * scale));
  }
  if (!situationPath.empty())
  {
    ImGui::Separator();
    situationDetail(SITUATIONS.at(situationPath.back()), layers);
  }
  ImGui::Spacing();
  muted(
      "Layers: from the inputs each function reads (the team sliders carry "
      "team instructions, game state and the shout). Planner-wide: the team "
      "planner reads them for all its jobs; not traced job by job yet.");
}

namespace
{
void readingStage()
{
  text("Not built yet", WARNING);
  muted(
      "What he notices and believes: awareness, scanning, anticipation "
      "(brain redesign plan, sections 5 and 6). Today Capybara reads the "
      "truth.");
}

void goalStage()
{
  text("Keep the ball and make progress towards a goal");
  muted(
      "The one default (DecisionFrame::Goal). No goal layer yet: it "
      "weights nothing.");
}
}  // namespace

namespace
{
constexpr std::array<const char*, 4> PACE_NAMES = {"walk", "jog", "run",
                                                   "sprint"};
/** Angles this close are the same direction (degrees). */
constexpr float SAME_ANGLE = 0.5F;
constexpr float BACK_ANGLE = 180.0F;

std::size_t paceIndex(float level)
{
  const auto steps = static_cast<float>(PACE_NAMES.size() - 1);
  return std::min(PACE_NAMES.size() - 1,
                  static_cast<std::size_t>(std::lround(level * steps)));
}

std::string carryDirectionName(const BrainFlow::CarryCandidate& candidate)
{
  const float angle = candidate.angleDegrees;
  if (candidate.towardsGoal)
  {
    return std::format("towards goal ({:.0f} deg)", angle);
  }
  if (std::abs(angle) < SAME_ANGLE)
  {
    return "straight on";
  }
  if (std::abs(std::abs(angle) - BACK_ANGLE) < SAME_ANGLE)
  {
    return "back";
  }
  return std::format("{:.0f} deg {}", std::abs(angle),
                     angle > 0.0F ? "right" : "left");
}

/** The aim the shot option carries, in words. */
std::string shotAimText(const BrainFlow::CarrierRecord& record)
{
  return std::format(
      "aim: {:.1f} m {} of the goal's centre, {:.1f} m up (today's aim "
      "rules, rolled with the option; held for the strike)",
      std::abs(record.shotAimY), record.shotAimY >= 0.0F ? "right" : "left",
      record.shotAimZ);
}

/** The carry candidates as a grid: a row per direction, a column per pace;
 * each cell its score on the true chances (hover: keep and end xG); the
 * best, which is the carry option, in lime. */
void carryGrid(const BrainFlow::CarrierRecord& record)
{
  const std::vector<BrainFlow::CarryCandidate>& candidates =
      record.carryCandidates;
  if (candidates.empty())
  {
    return;
  }
  const auto best = std::ranges::max_element(candidates, {},
                                             &BrainFlow::CarryCandidate::score);
  std::vector<float> angles;
  for (const BrainFlow::CarryCandidate& candidate : candidates)
  {
    if (std::ranges::none_of(
            angles, [&](float angle)
            { return std::abs(angle - candidate.angleDegrees) < SAME_ANGLE; }))
    {
      angles.push_back(candidate.angleDegrees);
    }
  }
  muted(
      "Carry candidates, scored at each 1 s point on the true chances "
      "(hover a score for its keep and end xG):");
  constexpr int COLUMNS = 1 + static_cast<int>(PACE_NAMES.size());
  if (!ImGui::BeginTable(
          "##carry_candidates", COLUMNS,
          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV))
  {
    return;
  }
  ImGui::TableSetupColumn("Direction");
  for (const char* pace : PACE_NAMES)
  {
    ImGui::TableSetupColumn(pace);
  }
  ImGui::TableHeadersRow();
  for (const float angle : angles)
  {
    ImGui::TableNextRow();
    bool named = false;
    for (const BrainFlow::CarryCandidate& candidate : candidates)
    {
      if (std::abs(candidate.angleDegrees - angle) >= SAME_ANGLE)
      {
        continue;
      }
      if (!named)
      {
        ImGui::TableSetColumnIndex(0);
        text(carryDirectionName(candidate));
        named = true;
      }
      ImGui::TableSetColumnIndex(1 +
                                 static_cast<int>(paceIndex(candidate.level)));
      text(std::format("{:.2f}", candidate.score),
           &candidate == &*best ? SandboxPageUI::LIME : SandboxPageUI::CREAM);
      if (ImGui::IsItemHovered())
      {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            std::format("keeps it {:.2f}, xG at the end {:.3f}", candidate.keep,
                        candidate.endXG)
                .c_str());
        ImGui::EndTooltip();
      }
    }
  }
  ImGui::EndTable();
  muted(
      "Missing cells: off the pitch, backwards near his own goal, or a pace "
      "the Dribbling cap makes the same as the one before.");
}
}  // namespace

void BrainFlowScene::optionsStage()
{
  for (std::size_t index = 0; index < BrainFlow::OPTION_COUNT; ++index)
  {
    const BrainFlow::Option& option = layered.options.at(index);
    const BrainFlow::Option& before = base.options.at(index);
    const bool chosen = layered.chosen == index;
    std::string title = OPTION_NAMES.at(index);
    if (index == at(OptionIndex::PASS) && layered.bestReceiver != 0)
    {
      title += " to " + nameOf(layered.bestReceiver);
    }
    text(title, chosen ? SandboxPageUI::LIME : SandboxPageUI::CREAM);
    if (option.ruledOut != nullptr)
    {
      muted(std::string("ruled out: ") + option.ruledOut);
    }
    else
    {
      muted(chosen ? "open, chosen" : "open");
    }
    if ((before.ruledOut == nullptr) != (option.ruledOut == nullptr))
    {
      text("the layers change whether it is open", WARNING);
    }
    if (index == at(OptionIndex::SHOT) && layered.shotAimed)
    {
      muted(shotAimText(layered));
    }
    if (index == at(OptionIndex::CARRY))
    {
      carryGrid(layered);
    }
    ImGui::Spacing();
  }
}

void BrainFlowScene::measuresStage()
{
  muted("Chances (as true; he may misread them, see Judgement):");
  for (const BrainFlow::Chance& chance : layered.chances)
  {
    text(std::format("{}: {:.3f}", chance.name, chance.truth));
  }
  ImGui::Spacing();
  muted("Each option's measures (the terms' measures, before weights):");
  const auto list = [this](const char* name, const DecisionFrame::Score& score,
                           const DecisionFrame::Score& before)
  {
    if (!score.isAvailable())
    {
      return;
    }
    text(name, SandboxPageUI::LIME);
    const auto terms = score.allTerms();
    const auto old = before.allTerms();
    for (std::size_t index = 0; index < terms.size(); ++index)
    {
      const DecisionFrame::Term& term = terms[index];
      const float was = index < old.size() ? old[index].measure : term.measure;
      const std::string line =
          std::format("{}: {}", term.name, change(was, term.measure));
      if (const auto layer = layerOfTerm(term.layer); layer && layerOn(*layer))
      {
        tagged(*layer, line);
      }
      else
      {
        text(line);
      }
    }
  };
  list("Best pass", layered.bestPass, base.bestPass);
  list("Shot", layered.options.at(at(OptionIndex::SHOT)).score,
       base.options.at(at(OptionIndex::SHOT)).score);
  if (layered.options.at(at(OptionIndex::SHOT)).score.isAvailable())
  {
    // Where the shot's xG comes from (game xG plan).
    if (GameXg::table(GameXgTable::EMPTY_GOAL, GoalKind::FULL) == nullptr)
    {
      muted("xG: the fitted model (no game xG map for this goal).");
    }
    else if (GameXg::table(GameXgTable::KEEPER, GoalKind::FULL) != nullptr)
    {
      muted(
          "xG: the game maps, the empty goal times the keeper's factor at his "
          "set position; \"goal more open than usual\" when he is off it or "
          "rushing.");
    }
    else
    {
      muted("xG: the game's empty-goal map (no keeper map yet).");
    }
  }
  list("Carry (the best candidate)",
       layered.options.at(at(OptionIndex::CARRY)).score,
       base.options.at(at(OptionIndex::CARRY)).score);
  if (layered.options.at(at(OptionIndex::CARRY)).score.isAvailable())
  {
    muted(
        "Keeps the ball: the path keep, 1 minus the worst opponent near the "
        "line he covers (how close, within 8.5 m, times his tackle chance). "
        "A longer, faster carry passes nearer more of them. Opponents where "
        "they stand now; whether they can reach the line in time comes "
        "later.");
  }
  list("Shield", layered.options.at(at(OptionIndex::SHIELD)).score,
       base.options.at(at(OptionIndex::SHIELD)).score);
  list("Clear", layered.options.at(at(OptionIndex::CLEAR)).score,
       base.options.at(at(OptionIndex::CLEAR)).score);
  if (layered.options.at(at(OptionIndex::CLEAR)).score.isAvailable())
  {
    text(
        "Follow-up: the clear has no measures of its own yet; it is scored "
        "with the shield's keep (what decided a clearance before).",
        WARNING);
  }
}

void BrainFlowScene::weightsStage()
{
  muted(
      "Default x 1.1^steps, the summed steps soft-capped at 6. Each "
      "layer that is on shows its steps.");
  for (std::size_t index = 0; index < layered.weights.size(); ++index)
  {
    const BrainFlow::Weight& weight = layered.weights[index];
    const float was =
        index < base.weights.size() ? base.weights[index].used : weight.used;
    text(std::format("{}: {}", weight.name, change(was, weight.used)),
         std::abs(weight.used - was) >= CHANGE_EPSILON ? SandboxPageUI::LIME
                                                       : SandboxPageUI::CREAM);
    for (std::size_t layer = 0; layer < BrainFlow::LAYER_COUNT; ++layer)
    {
      const float steps = weight.steps.at(layer);
      if (steps != 0.0F && layerOn(static_cast<Layer>(layer)))
      {
        ImGui::Indent();
        tagged(static_cast<Layer>(layer), std::format("{:+.2f} steps", steps));
        ImGui::Unindent();
      }
    }
  }
  // Drift (plan section 6): a layer acting on the frame that the
  // situation table doesn't list for it.
  BrainFlow::LayerMask mapped = 0;
  for (const Situation& situation : SITUATIONS)
  {
    if (situation.onFrame)
    {
      mapped |= situation.layers;
    }
  }
  for (const BrainFlow::Weight& weight : layered.weights)
  {
    for (std::size_t layer = 0; layer < BrainFlow::LAYER_COUNT; ++layer)
    {
      if (weight.steps.at(layer) != 0.0F &&
          (mapped & bit(static_cast<Layer>(layer))) == 0)
      {
        text(std::format("{}: steps from {}, not in the map yet", weight.name,
                         LAYER_NAMES.at(layer)),
             WARNING);
      }
    }
  }
  if (layered.receiverRoleSteps != 0.0F && layerOn(Layer::ROLE))
  {
    ImGui::Spacing();
    tagged(
        Layer::ROLE,
        std::format("{:+.2f} steps on progress for passes to {} (his "
                    "role)",
                    layered.receiverRoleSteps, nameOf(layered.bestReceiver)));
  }
}

void BrainFlowScene::judgementStage()
{
  text(std::format("Decisions {:.0f}", layered.decisions * 100.0F));
  muted(
      std::format("Each chance may be misread by up to {:.2f} on the "
                  "log-odds scale (1 - Decisions), drawn once per "
                  "decision.",
                  layered.judgement));
  for (const BrainFlow::Chance& chance : layered.chances)
  {
    const float low = DecisionFrame::misread(chance.truth, -layered.judgement);
    const float high = DecisionFrame::misread(chance.truth, layered.judgement);
    std::string line = std::format("{}: {:.3f}  (reads {:.3f} to {:.3f})",
                                   chance.name, chance.truth, low, high);
    if (!layered.steady)
    {
      line += std::format("; this roll {:.3f}", chance.read);
    }
    text(line);
  }
}

void BrainFlowScene::pickStage()
{
  const float noise = layered.pickNoise;
  muted(
      std::format("Highest score wins after pick noise of up to +-{:.2f}: "
                  "(1 - Decisions) x 0.4, x familiarity {:.2f}, x team "
                  "edge {:.2f}, x team talk {:.2f}.",
                  noise, layered.familiarityFactor, layered.edgeFactor,
                  layered.talkFactor));
  if (layerOn(Layer::MOOD))
  {
    tagged(Layer::MOOD,
           std::format("pick noise {}", change(base.pickNoise, noise)));
  }
  ImGui::Spacing();
  std::array<float, BrainFlow::OPTION_COUNT> totals{};
  for (std::size_t index = 0; index < BrainFlow::OPTION_COUNT; ++index)
  {
    const BrainFlow::Option& option = layered.options.at(index);
    totals.at(index) = option.score.total();
    if (option.ruledOut != nullptr)
    {
      muted(std::format("{}: -", OPTION_NAMES.at(index)));
      continue;
    }
    std::string line = std::format(
        "{}: {}", OPTION_NAMES.at(index),
        change(base.options.at(index).score.total(), option.score.total()));
    line += std::format("  ({:.2f} to {:.2f})", option.score.total() - noise,
                        option.score.total() + noise);
    if (!layered.steady)
    {
      line += std::format("; rolled {:+.2f}", option.noise);
    }
    text(line,
         layered.chosen == index ? SandboxPageUI::LIME : SandboxPageUI::CREAM);
  }
  if (layered.chosen < BrainFlow::OPTION_COUNT)
  {
    float second = -INFINITY;
    for (std::size_t index = 0; index < totals.size(); ++index)
    {
      if (index != layered.chosen &&
          layered.options.at(index).ruledOut == nullptr)
      {
        second = std::max(second, totals.at(index));
      }
    }
    ImGui::Spacing();
    text(std::format("Chosen: {}", OPTION_NAMES.at(layered.chosen)),
         SandboxPageUI::LIME);
    if (std::isfinite(second))
    {
      muted(std::format("margin {:.2f} over the next best",
                        totals.at(layered.chosen) - second));
    }
    if (base.chosen != layered.chosen && base.chosen < BrainFlow::OPTION_COUNT)
    {
      text(std::format("With no layers: {}", OPTION_NAMES.at(base.chosen)),
           WARNING);
    }
  }
}

void BrainFlowScene::offBallStage()
{
  const auto index = static_cast<std::size_t>(offBallPlayer);
  if (!offBall.filled || index >= exampleIds.size())
  {
    muted("Pick a player in the example card (OFF THE BALL).");
    return;
  }
  text(
      std::format("{}: {}", SPOT_NAMES.at(index), nameOf(exampleIds.at(index))),
      SandboxPageUI::LIME);
  text(std::format("Situation: {}", offBall.situation));
  muted(
      "His off-ball frame at this moment (all layers on): an option is open "
      "when the team's assignments, orders or phase give it to him; the "
      "stopgap pick is the first open one in today's order (follow-up: "
      "measures). The team planner ran once with nobody deciding.");
  for (std::size_t option = 0; option < offBall.options.size(); ++option)
  {
    const BrainFlow::OffBallOption& entry = offBall.options.at(option);
    // Picked (lime, ">"), open ("+"), or closed (muted, "-").
    const char* mark = "-";
    ImU32 colour = SandboxPageUI::MUTED;
    if (option == offBall.chosen)
    {
      mark = ">";
      colour = SandboxPageUI::LIME;
    }
    else if (entry.open)
    {
      mark = "+";
      colour = SandboxPageUI::CREAM;
    }
    text(std::format("{} {} - {}{}", mark, entry.name,
                     entry.open ? "open: " : "closed: opens when ",
                     entry.opensWhen),
         colour);
  }
  if (offBall.overriddenBy != nullptr)
  {
    text(std::format("Overridden: {}", offBall.overriddenBy), WARNING);
  }
  pressShadow(offBall);
  looseShadow(offBall);
  looseCommit(offBall);
  muted("The line on the pitch: from him to the target his job sets.");
}

void BrainFlowScene::looseCommit(const BrainFlow::OffBallRecord& offBall) const
{
  if (!offBall.commit)
  {
    return;
  }
  const BrainFlow::OffBallRecord::CommitCall& commit = *offBall.commit;
  text("First to commit (shadow, step O6): he goes for it.",
       SandboxPageUI::CREAM);
  text(std::format("He {} {:.2f} s, calls {:.2f} s later (Leadership): "
                   "{:.2f} s in all.",
                   commit.seesAtOnce ? "sees his team-mate at once (in view or "
                                       "alongside):"
                                     : "looks up after (Concentration)",
                   commit.lookUpSeconds, commit.callSeconds,
                   commit.lookUpSeconds + commit.callSeconds));
  if (commit.partner == 0)
  {
    muted("No team-mate is going for it: no call needed.");
    return;
  }
  const std::string mate = nameOf(commit.partner);
  text(std::format("{} is going too: {:.2f} s + {:.2f} s = {:.2f} s.", mate,
                   commit.partnerLookUpSeconds, commit.partnerCallSeconds,
                   commit.partnerLookUpSeconds + commit.partnerCallSeconds));
  text(std::format(
      "The detailed look: him {:.2f} s, {} {:.2f} s. {} It should be {}.",
      commit.hisSeconds, mate, commit.partnerSeconds,
      commit.byShape ? "Within the margin: the shape spot nearest where it is "
                       "collected decides."
                     : "Further apart than the margin: the quicker takes it.",
      commit.shouldBeHis ? "his" : mate));
  // The caller says mine when it should be his own, else yours.
  const bool callerTakesIt = commit.heCalls == commit.shouldBeHis;
  text(std::format("{} calls first: \"{}\" (if he reads it right: the "
                   "caller's Decisions; his own {:.0f}%).",
                   commit.heCalls ? "He" : mate,
                   callerTakesIt ? "mine" : "yours",
                   commit.rightChance * 100.0F),
       SandboxPageUI::LIME);
  muted("A projection: the moment does not run on. Delays: 1 s at the stat's "
        "1 to 0.1 s at 100; right call 50% at Decisions 1 to 100% at 100 "
        "(estimates, to tune).");
}

void BrainFlowScene::pressShadow(const BrainFlow::OffBallRecord& offBall)
{
  if (offBall.pressCall == nullptr)
  {
    return;
  }
  // Step P1: his own call on pressing the carrier, in the shadow of the
  // team's pick (it changes nothing yet).
  text(std::format("His own press call (shadow, step P1): {}",
                   offBall.pressCall),
       SandboxPageUI::CREAM);
  muted(
      "Can he get to the carrier before the 3 team-mates nearest him (rough "
      "look: straight line at top speed, his way already toward him, the "
      "turn)? Beaten by more than the margin (10% of his time, at most "
      "0.5 s): not his; closer: in doubt (estimates, to tune). The team's "
      "press eagerness (role, team talk) is left out of the shadow.");
}

void BrainFlowScene::looseShadow(const BrainFlow::OffBallRecord& offBall)
{
  if (offBall.looseCall == nullptr)
  {
    return;
  }
  // Step O6: his own call, in the shadow of the team's pick (it changes
  // nothing yet).
  text(std::format("His own call (shadow, step O6): {}", offBall.looseCall),
       SandboxPageUI::CREAM);
  muted(
      "Can he beat the 3 closest to the earliest playable point? If nobody "
      "gets there in time, his own earliest point against those in "
      "contention. Beaten by more than the margin (10% of his time, at "
      "most 0.5 s): not his; closer: he chases in "
      "doubt (estimates, to tune).");
  for (const BrainFlow::OffBallRecord::MeetPoint& point : offBall.meetPoints)
  {
    text(std::format("{} meet it {}: {:.1f} s, {:.1f} m up, {:+.1f} s on "
                     "the nearest rival",
                     point.open ? "+" : "-", point.name, point.seconds,
                     point.height, point.margin),
         point.open ? SandboxPageUI::CREAM : SandboxPageUI::MUTED);
  }
  if (!offBall.meetPoints.empty())
  {
    muted(
        "Where to meet it: open when nobody beats him there within the margin. "
        "Stopgap pick: the earliest (follow-up: measures, control against "
        "time given away).");
  }
}

void BrainFlowScene::bodyStage()
{
  if (layered.chosen >= BrainFlow::OPTION_COUNT)
  {
    muted("No choice.");
    return;
  }
  const auto option = static_cast<OptionIndex>(layered.chosen);
  switch (option)
  {
    case OptionIndex::PASS:
      text(std::format("Plays the pass to {}", nameOf(layered.bestReceiver)));
      break;
    case OptionIndex::SHOT:
      text(layered.shotAimed ? "Shoots at the aim it carried" : "Shoots");
      if (layered.shotAimed)
      {
        muted(shotAimText(layered));
      }
      break;
    case OptionIndex::CARRY:
    {
      const auto& candidates = layered.carryCandidates;
      const auto best = std::ranges::max_element(
          candidates, {}, &BrainFlow::CarryCandidate::score);
      const std::string how =
          best == candidates.end()
              ? std::string("Carries it")
              : std::format("Carries it {} at a {}", carryDirectionName(*best),
                            PACE_NAMES.at(paceIndex(best->level)));
      text(std::format("{}, about {:.0f} m by his next decision", how,
                       std::hypot(layered.carryEnd.x - carrierAt.x,
                                  layered.carryEnd.y - carrierAt.y)));
      muted(
          "Held until he decides again: the planner aims along it and the "
          "dribble runs at that pace. No take-ons (until the take-on "
          "rework).");
      break;
    }
    case OptionIndex::SHIELD:
      text("Shields it");
      break;
    case OptionIndex::CLEAR:
      text("Clears it");
      if (layered.clearAimed)
      {
        muted(std::format(
            "aimed {:.0f} m upfield and towards the touchline (today's aim "
            "rules, rolled with the option; held for the strike)",
            std::hypot(layered.clearTarget.x - carrierAt.x,
                       layered.clearTarget.y - carrierAt.y)));
      }
      break;
    case OptionIndex::COUNT:
      break;
  }
  muted(
      "The body carries it out (the side's body model): technique, "
      "touches, contacts.");
}

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
    "Pass", "Shot", "Carry", "Shield"};
/** Not yet in the plan's terms, or not built: a warning colour. */
constexpr ImU32 WARNING = IM_COL32(235, 120, 70, 255);
/** A change smaller than this shows as no change. */
constexpr float CHANGE_EPSILON = 0.005F;
// Layout, in unscaled pixels.
constexpr float PANEL_WIDTH = 340.0F;
constexpr float PANEL_SHARE = 0.3F;
constexpr float COLUMN_PADDING_X = 12.0F;
constexpr float COLUMN_PADDING_Y = 10.0F;
constexpr float TITLE_SIZE = 14.0F;
constexpr float FLOW_BOTTOM_MARGIN = 4.0F;

/** One column of the flow. */
struct ColumnSpec
{
  const char* id;
  const char* title;
  float width;
  /** False: not built yet (drawn darker). */
  bool built;
};
constexpr ColumnSpec SITUATION{
    .id = "##situation", .title = "SITUATION", .width = 195.0F, .built = true};
constexpr ColumnSpec READING{
    .id = "##reading", .title = "READING", .width = 150.0F, .built = false};
constexpr ColumnSpec GOAL{
    .id = "##goal", .title = "GOAL", .width = 180.0F, .built = true};
constexpr ColumnSpec OPTIONS{
    .id = "##options", .title = "OPTIONS", .width = 240.0F, .built = true};
constexpr ColumnSpec MEASURES{
    .id = "##measures", .title = "MEASURES", .width = 320.0F, .built = true};
constexpr ColumnSpec WEIGHTS{
    .id = "##weights", .title = "WEIGHTS", .width = 335.0F, .built = true};
constexpr ColumnSpec JUDGEMENT{
    .id = "##judgement", .title = "JUDGEMENT", .width = 280.0F, .built = true};
constexpr ColumnSpec PICK{
    .id = "##pick", .title = "PICK", .width = 280.0F, .built = true};
constexpr ColumnSpec BODY{
    .id = "##body", .title = "BODY", .width = 210.0F, .built = true};
void situationColumn();
void readingColumn();
void goalColumn();

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

/** The example: a midfielder on the ball in build-up, three team-mates and
 * five opponents, scored once by the brain (never stepped on). */
class BrainFlowExample final : public Drill
{
 public:
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
    return {spec("Carrier", PlayerRole::CM, true),
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
    // Fractions of the match pitch, the home side attacking to the right.
    constexpr std::array<std::array<float, 2>, 9> SPOTS = {{{0.45F, 0.50F},
                                                            {0.50F, 0.28F},
                                                            {0.63F, 0.84F},
                                                            {0.72F, 0.52F},
                                                            {0.98F, 0.50F},
                                                            {0.48F, 0.47F},
                                                            {0.56F, 0.62F},
                                                            {0.80F, 0.40F},
                                                            {0.80F, 0.60F}}};
    const MatchPitch pitch;
    scenario = {};
    for (std::size_t index = 0; index < ids.size() && index < SPOTS.size();
         ++index)
    {
      scenario.players.push_back(
          {.playerId = ids[index],
           .position = pitch.at(SPOTS.at(index)[0], SPOTS.at(index)[1]),
           .makingRun = index == 3});
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

  MatchScenario scenario;
  std::vector<PlayerID> playerIds;
};

/** The example's layer settings (phase 4 makes them editable). */
constexpr float ATTACKING_SLIDER = 0.8F;
constexpr float RISK_SLIDER = 0.75F;
constexpr float FAMILIARITY = 0.4F;
constexpr float MINUTE = 80.0F;
constexpr int HOME_GOALS = 0;
constexpr int AWAY_GOALS = 1;
}  // namespace

BrainFlowScene::BrainFlowScene(GUIView* guiView_ptr) : GUIScene(guiView_ptr) {}

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
  BrainFlowExample example;
  example.seed = rollSeed;
  const Formation::Preset& shape = Formation::PRESETS[0];
  std::vector<PlayerID> ids;
  const auto prepare = [&](MatchEngine& engine)
  {
    // Capybara on both sides; the layers' settings for this example. The
    // drill players fill the first slots of the default shape, so the
    // carrier is slot 0 and the striker slot 3.
    engine.setBrain(true, BrainId::CAPYBARA);
    engine.setBrain(false, BrainId::CAPYBARA);
    Strategy home;
    home.setOffensiveBias(ATTACKING_SLIDER);
    home.setRiskTaking(RISK_SLIDER);
    home.setSlot({.anchor = shape.slots[0].position,
                  .role = TacticalRole::Playmaker,
                  .duty = RoleDuty::Attack});
    home.setSlot({.anchor = shape.slots[3].position,
                  .role = TacticalRole::TargetForward,
                  .duty = RoleDuty::Attack});
    engine.setStrategy(true, home);
    Strategy away;
    if (example.playerIds.size() > 3)
    {
      away.setOppositionOrders(
          {{.player = example.playerIds[3],
            .instruction = OppositionInstruction::TightMark},
           {.player = example.playerIds[0],
            .instruction = OppositionInstruction::ShowWeakFoot}});
    }
    engine.setStrategy(false, away);
    engine.applyShout(true, MatchShout::SHOOT_ON_SIGHT);
    engine.setTacticalFamiliarity(true, FAMILIARITY);
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
    const bool applied = engine.applyScenario(
        example.scenario, MatchState::PLAYING, MINUTE, HOME_GOALS, AWAY_GOALS);
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
  carrierName = example.playerIds.empty() ? std::string()
                                          : nameOf(example.playerIds.front());
  if (!example.scenario.players.empty())
  {
    carrierAt = example.scenario.players.front().position;
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
                    ImGuiChildFlags_AlwaysUseWindowPadding);
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
               "The new brain framework, left to right, on a live example "
               "scored by Capybara's own code. Start from the default flow, "
               "then switch layers on to see what each one touches.",
               true);
  Page::beginCard("##brain_flow_layers", "LAYERS", width, Page::DARK, scale,
                  boldFont);
  if (Page::button("Back to tools", false, scale, boldFont))
  {
    guiView->popScene();
  }
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
  }
  if (Page::button("All off (default flow)", false, scale, boldFont))
  {
    layers = 0;
    dirty = true;
  }
  Page::endCard(scale);

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

  Page::beginCard("##brain_flow_example", "EXAMPLE", width, Page::LIGHT, scale,
                  boldFont);
  text("Build-up in midfield");
  muted(std::format(
      "{} on the ball near halfway; three team-mates (the striker running), "
      "five opponents. Layer settings when on: attacking {:.0f}, risk {:.0f}; "
      "carrier a Playmaker (attack), striker a Target forward; shout: shoot "
      "on sight; orders: tight mark on the striker, show the carrier onto "
      "his weaker foot; {:.0f}', 0-1 down; familiarity {:.0f}%.",
      carrierName, ATTACKING_SLIDER * 100.0F, RISK_SLIDER * 100.0F, MINUTE,
      FAMILIARITY * 100.0F));
  Page::endCard(scale);
}

namespace
{
/** A column: its title, then its body drawn by `body`. */
template <typename Body>
void column(const ColumnSpec& spec, float height, ImFont* bold,
            const Body& body)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  ImGui::PushStyleColor(ImGuiCol_ChildBg,
                        spec.built ? Page::LIGHT : Page::DARK);
  ImGui::PushStyleVar(
      ImGuiStyleVar_WindowPadding,
      ImVec2(COLUMN_PADDING_X * scale, COLUMN_PADDING_Y * scale));
  ImGui::BeginChild(spec.id, ImVec2(spec.width * scale, height),
                    ImGuiChildFlags_AlwaysUseWindowPadding);
  ImGui::PushFont(bold, TITLE_SIZE);
  text(spec.title, spec.built ? Page::LIME : Page::MUTED);
  ImGui::PopFont();
  ImGui::Separator();
  body();
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::SameLine(0, SandboxPageUI::PANEL_GAP * scale);
}
}  // namespace

void BrainFlowScene::renderFlow(float width, float height)
{
  const float scale = Theme::scale();
  ImGui::BeginChild("##brain_flow_columns", ImVec2(width, height),
                    ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
  if (!error.empty())
  {
    text(error, WARNING);
    ImGui::EndChild();
    return;
  }
  const float full = std::max(scale, height - ImGui::GetStyle().ScrollbarSize -
                                         (FLOW_BOTTOM_MARGIN * scale));
  column(SITUATION, full, boldFont, situationColumn);
  column(READING, full, boldFont, readingColumn);
  column(GOAL, full, boldFont, goalColumn);
  column(OPTIONS, full, boldFont, [this] { optionsColumn(); });
  column(MEASURES, full, boldFont, [this] { measuresColumn(); });
  column(WEIGHTS, full, boldFont, [this] { weightsColumn(); });
  column(JUDGEMENT, full, boldFont, [this] { judgementColumn(); });
  column(PICK, full, boldFont, [this] { pickColumn(); });
  column(BODY, full, boldFont, [this] { bodyColumn(); });
  ImGui::EndChild();
}

namespace
{
void situationColumn()
{
  text("Open play");
  muted("our ball");
  text("I have it (collected)", SandboxPageUI::LIME);
  ImGui::Spacing();
  muted(
      "The whole situation tree (restarts, loose balls, our pass "
      "travelling, the opponents' ball, off the ball) comes in phase 3. "
      "Only this situation is on the decision frame so far.");
}

void readingColumn()
{
  text("Not built yet", WARNING);
  muted(
      "What he notices and believes: awareness, scanning, anticipation "
      "(brain redesign plan, sections 5 and 6). Today Capybara reads the "
      "truth.");
}

void goalColumn()
{
  text("Keep the ball and make progress towards a goal");
  muted(
      "The one default (DecisionFrame::Goal). No goal layer yet: it "
      "weights nothing.");
}
}  // namespace

void BrainFlowScene::optionsColumn()
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
    if (index == at(OptionIndex::SHIELD) && layered.clearance)
    {
      title += " (clear)";
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
    ImGui::Spacing();
  }
}

void BrainFlowScene::measuresColumn()
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
  list("Carry", layered.options.at(at(OptionIndex::CARRY)).score,
       base.options.at(at(OptionIndex::CARRY)).score);
  list("Shield", layered.options.at(at(OptionIndex::SHIELD)).score,
       base.options.at(at(OptionIndex::SHIELD)).score);
}

void BrainFlowScene::weightsColumn()
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

void BrainFlowScene::judgementColumn()
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

void BrainFlowScene::pickColumn()
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

void BrainFlowScene::bodyColumn()
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
      text("Shoots");
      break;
    case OptionIndex::CARRY:
      text(std::format("Carries it, about {:.0f} m by his next decision",
                       std::hypot(layered.carryEnd.x - carrierAt.x,
                                  layered.carryEnd.y - carrierAt.y)));
      break;
    case OptionIndex::SHIELD:
      text(layered.clearance ? "Clears it" : "Shields it");
      break;
    case OptionIndex::COUNT:
      break;
  }
  muted(
      "The body carries it out (the side's body model): technique, "
      "touches, contacts.");
}

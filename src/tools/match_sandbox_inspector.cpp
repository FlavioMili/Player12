// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_inspector.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <unordered_map>
#include <variant>

#include "gui/render/imatch_renderer.h"
#include "gui/widgets/theme.h"
#include "model/match_engine.h"
#include "model/player.h"
#include "model/role_utils.h"
#include "tools/match_sandbox_analysis_ui.h"
#include "tools/match_sandbox_debugger.h"
#include "tools/match_sandbox_names.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_text.h"

namespace
{
using Recorder = SandboxRecorder;
using Job = SandboxRecorder::Job;

constexpr int SECONDS_PER_MINUTE = 60;
constexpr float HEADER_FONT_SIZE = 14.0F;
constexpr int TEAM_TABLE_COLUMNS = 7;
constexpr int SUMMARY_TABLE_COLUMNS = 8;
constexpr float PLAYER_COLUMN_WEIGHT = 2.0F;
constexpr float DETAIL_COLUMN_WEIGHT = 1.6F;
constexpr float JOB_COLUMN_WEIGHT = 1.2F;
constexpr float TOP_INTENTS_WEIGHT = 2.4F;
constexpr float CURSOR_PADDING = 2.0F;
constexpr float CURSOR_STROKE = 2.0F;
constexpr float PLAYER_PICKER_WIDTH = 240.0F;
constexpr float PROPERTY_COLUMN_WIDTH = 140.0F;
constexpr float EVENT_TIME_WIDTH = 60.0F;
constexpr float EVENT_TYPE_WIDTH = 110.0F;
constexpr int RECENT_EVENT_LIMIT = 8;
constexpr float DEGREES_HALF_TURN = 180.0F;
constexpr float PICK_RADIUS = 22.0F;
constexpr float SELECTED_STROKE = 2.5F;
constexpr float OVERLAY_STROKE = 1.2F;
constexpr float TARGET_RADIUS = 3.0F;
constexpr float LABEL_PADDING_X = 2.0F;
constexpr float LABEL_ROUNDING = 3.0F;
constexpr float SELECTION_RADIUS = 11.0F;
constexpr float PASS_MARKER_RADIUS = 4.0F;
constexpr float RUNNER_UP_STROKE = 2.0F;
constexpr float BEST_PASS_STROKE = 3.0F;
constexpr float PASS_LABEL_PADDING_X = 3.0F;
constexpr float PASS_LABEL_PADDING_Y = 2.0F;

constexpr float SECONDS_PER_TICK = MatchTuning::Timing::FIXED_STEP_SECONDS;
/** A decision this recent (ticks) still shows its pass options. */
constexpr std::uint64_t PASS_OPTION_TICKS = 30;
constexpr std::size_t INTENTS =
    static_cast<std::size_t>(PlayerIntent::GOALKEEP) + 1;

float metres(PitchPoint from, PitchPoint to)
{
  return std::hypot(to.x - from.x, to.y - from.y);
}

float seconds(std::uint64_t ticks)
{
  return static_cast<float>(ticks) * SECONDS_PER_TICK;
}

std::size_t side(bool homeTeam) { return homeTeam ? 0 : 1; }

/** Colour of an intent: runs orange, pressing red, support green, shape grey.
 */
ImU32 intentColor(PlayerIntent intent)
{
  switch (intent)
  {
    case PlayerIntent::RUN_IN_BEHIND:
    case PlayerIntent::ATTACK_BOX:
    case PlayerIntent::OVERLAP:
      return IM_COL32(255, 160, 50, 255);
    case PlayerIntent::PRESS_BALL:
    case PlayerIntent::COVER_PRESS:
    case PlayerIntent::CLAIM_LOOSE_BALL:
    case PlayerIntent::BLOCK_PASSING_LANE:
      return IM_COL32(235, 70, 70, 255);
    case PlayerIntent::OFFER_SUPPORT:
    case PlayerIntent::RECEIVE_PASS:
    case PlayerIntent::CARRY_BALL:
      return IM_COL32(90, 210, 110, 255);
    case PlayerIntent::MARK_OPPONENT:
      return IM_COL32(110, 150, 240, 255);
    case PlayerIntent::RECOVER_SHAPE:
      return IM_COL32(200, 200, 90, 255);
    case PlayerIntent::GOALKEEP:
      return IM_COL32(200, 120, 220, 255);
    case PlayerIntent::HOLD_SHAPE:
      return IM_COL32(150, 150, 160, 255);
  }
  return IM_COL32(150, 150, 160, 255);
}

/** Two-character tag of a job on the pitch. */
const char* jobTag(Job job)
{
  switch (job)
  {
    case Job::NONE:
      return "";
    case Job::PRESSER_1:
      return "B1";
    case Job::PRESSER_2:
      return "B2";
    case Job::RUNNER_1:
      return "R1";
    case Job::RUNNER_2:
      return "R2";
    case Job::RUNNER_3:
      return "R3";
    case Job::MIDFIELD_ARRIVAL:
      return "LA";
    case Job::FAR_POST:
      return "FP";
    case Job::OVERLAP:
      return "OV";
    case Job::SUPPORTER_1:
      return "S1";
    case Job::SUPPORTER_2:
      return "S2";
    case Job::SUPPORTER_3:
      return "S3";
    case Job::COVER_OUTLET:
      return "CO";
  }
  return "";
}

const MatchPlayer* findSlot(const MatchEngine& engine, PlayerID id)
{
  for (const MatchPlayer& slot : engine.getPlayers())
  {
    if ((slot.player != nullptr) && slot.player->getId() == id)
    {
      return &slot;
    }
  }
  return nullptr;
}

std::string clock(float minute)
{
  const int total = static_cast<int>(std::floor(
      std::max(0.0F, minute) * static_cast<float>(SECONDS_PER_MINUTE)));
  return std::format("{:02}:{:02}", total / SECONDS_PER_MINUTE,
                     total % SECONDS_PER_MINUTE);
}

const char* possessionTag(PlayerID id, const PossessionSnapshot& possession)
{
  if (id == possession.controller)
  {
    return "(control)";
  }
  if (id == possession.claimant)
  {
    return "(claim)";
  }
  if (id == possession.legacyOwner)
  {
    return possession.openPlay ? "(attached)" : "(restart)";
  }
  return nullptr;
}

struct PassArrowStyle
{
  ImU32 color;
  float width;
};

/** Key/value row of a two-column table. */
void property(const char* key, const std::string& value,
              const char* tooltip = nullptr)
{
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  SandboxText::disabledText(key);
  if ((tooltip != nullptr) && ImGui::IsItemHovered())
  {
    SandboxText::tooltipText(tooltip);
  }
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(value.c_str());
}
}  // namespace

void DebugInspector::reset()
{
  asOf = AsOf{};
  followed = 0;
  detail_session.reset();
  opened_decision.reset();
  summary_prefix = std::numeric_limits<std::size_t>::max();
  totals.clear();
}

void DebugInspector::refresh(const SandboxRecorder& recorder,
                             std::uint64_t tick)
{
  const auto& entries = recorder.entries();
  // Moving forward continues from the last scan; otherwise start again.
  if (asOf.tick == NONE || tick < asOf.tick || asOf.prefix > entries.size())
  {
    asOf = AsOf{};
  }
  asOf.tick = tick;
  std::size_t index = asOf.prefix;
  for (; index < entries.size() && entries[index].tick <= tick; ++index)
  {
    const auto& entry = entries[index];
    if (const auto* change = std::get_if<Recorder::JobChange>(&entry.payload))
    {
      const auto job = static_cast<std::size_t>(change->job);
      asOf.jobs.at(side(change->homeTeam)).at(job) = change->player;
      asOf.scores.at(side(change->homeTeam)).at(job) = change->score;
    }
    else if (const auto* phase =
                 std::get_if<Recorder::PhaseChange>(&entry.payload))
    {
      asOf.phaseSince.at(side(phase->homeTeam)) = entry.tick;
    }
    else if (std::holds_alternative<MatchDecisionRecord>(entry.payload))
    {
      asOf.lastDecision = index;
    }
  }
  asOf.prefix = index;
}

Job DebugInspector::jobOf(PlayerID player, bool homeTeam) const
{
  const auto& held = asOf.jobs.at(side(homeTeam));
  for (std::size_t job = 1; job < held.size(); ++job)
  {
    if (held.at(job) == player && player != 0)
    {
      return static_cast<Job>(job);
    }
  }
  return Job::NONE;
}

void DebugInspector::selectable(const SandboxRecorder& recorder,
                                PlayerID player)
{
  const std::string label =
      std::format("{}##select{}", recorder.playerName(player), player);
  if (ImGui::Selectable(label.c_str(), selected == player,
                        ImGuiSelectableFlags_SpanAllColumns))
  {
    selected = player;
  }
}

// --- Team ------------------------------------------------------------------

namespace
{
constexpr ImGuiTableFlags TEAM_TABLE_FLAGS = ImGuiTableFlags_RowBg |
                                             ImGuiTableFlags_BordersInnerV |
                                             ImGuiTableFlags_SizingStretchProp;
}  // namespace

/** The side picker, its phase and how long it has held. */
void DebugInspector::renderTeamHeader(const MatchEngine& engine)
{
  constexpr std::array<const char*, 2> SIDES{"Home", "Away"};
  for (int index = 0; index < 2; ++index)
  {
    if (index > 0)
    {
      ImGui::SameLine();
    }
    if (ImGui::RadioButton(SIDES.at(static_cast<std::size_t>(index)),
                           team_side == index))
    {
      team_side = index;
    }
  }
  const bool home = team_side == 0;
  const std::size_t team = side(home);
  const TeamPhase phase = home ? engine.getHomePhase() : engine.getAwayPhase();
  ImGui::SameLine();
  ImGui::TextUnformatted(
      std::format("   Phase: {} for {:.1f} s", SandboxNames::phase(phase),
                  static_cast<double>(seconds(engine.getSimulatedSteps() -
                                              asOf.phaseSince.at(team))))
          .c_str());
  if (engine.getTransitionSecondsRemaining() > 0.0F)
  {
    ImGui::SameLine();
    SandboxText::disabledText(
        std::format("(transition {:.1f} s left)",
                    static_cast<double>(engine.getTransitionSecondsRemaining()))
            .c_str());
  }
}

/** The planner's jobs, with the scores that ranked them. */
void DebugInspector::renderPlannerJobs(const SandboxRecorder& recorder,
                                       std::size_t team)
{
  if (!ImGui::BeginTable("##jobs", 3, TEAM_TABLE_FLAGS))
  {
    return;
  }
  ImGui::TableSetupColumn("Job");
  ImGui::TableSetupColumn("Player");
  ImGui::TableSetupColumn("Ranking score");
  ImGui::PushFont(SandboxAnalysisUI::bold(), HEADER_FONT_SIZE);
  ImGui::TableHeadersRow();
  ImGui::PopFont();
  for (std::size_t job = 1; job < JOBS; ++job)
  {
    const PlayerID player = asOf.jobs.at(team).at(job);
    if (player == 0)
    {
      continue;
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(SandboxNames::job(static_cast<Job>(job)));
    ImGui::TableNextColumn();
    selectable(recorder, player);
    ImGui::TableNextColumn();
    const auto kind = static_cast<Job>(job);
    const auto score = static_cast<double>(asOf.scores.at(team).at(job));
    if (kind == Job::PRESSER_1 || kind == Job::PRESSER_2)
    {
      ImGui::TextUnformatted(std::format("arrives in {:.2f} s", score).c_str());
    }
    else if (kind == Job::RUNNER_1 || kind == Job::RUNNER_2 ||
             kind == Job::RUNNER_3)
    {
      ImGui::TextUnformatted(std::format("run priority {:.2f}", score).c_str());
    }
  }
  ImGui::EndTable();
}

/** One player of the side at this moment. */
void DebugInspector::renderTeamPlayerRow(const SandboxRecorder& recorder,
                                         const MatchPlayer& slot,
                                         const PossessionSnapshot& possession)
{
  const PlayerID id = slot.player->getId();
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  selectable(recorder, id);
  if (const char* ballStatus = possessionTag(id, possession);
      ballStatus != nullptr)
  {
    ImGui::SameLine();
    SandboxText::coloredText(Theme::palette().positive, ballStatus);
  }
  if (id == possession.expectedReceiver)
  {
    ImGui::SameLine();
    SandboxText::disabledText("(expected)");
  }
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(RoleUtils::shortName(slot.player->getRole()));
  ImGui::TableNextColumn();
  SandboxText::coloredText(
      ImGui::ColorConvertU32ToFloat4(intentColor(slot.intent)),
      SandboxNames::intent(slot.intent));
  ImGui::TableNextColumn();
  if (const Job job = jobOf(id, slot.isHomeTeam); job != Job::NONE)
  {
    ImGui::TextUnformatted(SandboxNames::job(job));
  }
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(
      std::format("{:.1f} m", static_cast<double>(
                                  metres(slot.position, slot.tacticalTarget)))
          .c_str());
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(
      std::format("{:.1f} m/s", static_cast<double>(std::hypot(
                                    slot.velocity.x, slot.velocity.y)))
          .c_str());
  ImGui::TableNextColumn();
  ImGui::TextUnformatted(
      std::format("{:.0f}%", static_cast<double>(slot.stamina * 100.0F))
          .c_str());
}

void DebugInspector::renderTeam(const SandboxRecorder& recorder,
                                const MatchEngine& engine)
{
  auto guard = recorder.lock();
  refresh(recorder, engine.getSimulatedSteps());
  renderTeamHeader(engine);
  const bool home = team_side == 0;
  ImGui::SeparatorText("Planner jobs");
  renderPlannerJobs(recorder, side(home));

  // Full detail: both rankings, every candidate broken down.
  if (ImGui::CollapsingHeader("Ranking breakdowns (full detail)"))
  {
    guard.unlock();
    ensureDetail(recorder, engine);
    renderRankingDetail(recorder, home, engine.getSimulatedSteps());
    guard.lock();
  }

  // Every player of the side at this moment.
  ImGui::SeparatorText("Players");
  if (!ImGui::BeginTable("##players", TEAM_TABLE_COLUMNS,
                         TEAM_TABLE_FLAGS | ImGuiTableFlags_ScrollY,
                         ImVec2(0.0F, 0.0F)))
  {
    return;
  }
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch,
                          PLAYER_COLUMN_WEIGHT);
  ImGui::TableSetupColumn("Pos");
  ImGui::TableSetupColumn("Intent", ImGuiTableColumnFlags_WidthStretch,
                          DETAIL_COLUMN_WEIGHT);
  ImGui::TableSetupColumn("Job", ImGuiTableColumnFlags_WidthStretch,
                          JOB_COLUMN_WEIGHT);
  ImGui::TableSetupColumn("To target");
  ImGui::TableSetupColumn("Speed");
  ImGui::TableSetupColumn("Stamina");
  ImGui::PushFont(SandboxAnalysisUI::bold(), HEADER_FONT_SIZE);
  ImGui::TableHeadersRow();
  ImGui::PopFont();
  const PossessionSnapshot possession = engine.getPossessionSnapshot();
  for (const MatchPlayer& slot : engine.getPlayers())
  {
    if ((slot.player != nullptr) && slot.isHomeTeam == home && slot.onPitch)
    {
      renderTeamPlayerRow(recorder, slot, possession);
    }
  }
  ImGui::EndTable();
}

// --- Player ----------------------------------------------------------------

void DebugInspector::renderIntentStrip(const SandboxRecorder& recorder,
                                       PlayerID player, IntentTimeline timeline)
{
  const std::uint64_t cursor = timeline.cursor;
  const std::uint64_t end = timeline.end;
  // His intents over the whole match; the cursor marks the moment shown.
  const float scale = Theme::scale();
  const float width = std::max(50.0F, ImGui::GetContentRegionAvail().x);
  const float height = 14.0F * scale;
  const ImVec2 min = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##intents", ImVec2(width, height));
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(min, ImVec2(min.x + width, min.y + height),
                          IM_COL32(25, 27, 32, 255));
  if (end == 0)
  {
    return;
  }
  const auto xOf = [&](std::uint64_t tick)
  {
    return min.x + (width * std::min(1.0F, static_cast<float>(tick) /
                                               static_cast<float>(end)));
  };
  struct Span
  {
    std::uint64_t from = 0;
    PlayerIntent intent = PlayerIntent::HOLD_SHAPE;
  };
  std::vector<Span> spans{{.from = 0, .intent = PlayerIntent::HOLD_SHAPE}};
  for (const auto& entry : recorder.entries())
  {
    if (const auto* change =
            std::get_if<Recorder::IntentChange>(&entry.payload);
        (change != nullptr) && change->player == player)
    {
      spans.push_back({.from = entry.tick, .intent = change->to});
    }
  }
  for (std::size_t index = 0; index < spans.size(); ++index)
  {
    const std::uint64_t until =
        index + 1 < spans.size() ? spans[index + 1].from : end;
    drawList->AddRectFilled(
        ImVec2(xOf(spans[index].from), min.y),
        ImVec2(std::max(xOf(until), xOf(spans[index].from) + 1.0F),
               min.y + height),
        intentColor(spans[index].intent));
  }
  const float cursorX = xOf(cursor);
  drawList->AddLine(ImVec2(cursorX, min.y - CURSOR_PADDING),
                    ImVec2(cursorX, min.y + height + CURSOR_PADDING),
                    IM_COL32(255, 255, 255, 255), CURSOR_STROKE * scale);
  if (ImGui::IsItemHovered())
  {
    const float share =
        std::clamp((ImGui::GetIO().MousePos.x - min.x) / width, 0.0F, 1.0F);
    const auto tick =
        static_cast<std::uint64_t>(share * static_cast<float>(end));
    PlayerIntent intent = PlayerIntent::HOLD_SHAPE;
    for (const Span& span : spans)
    {
      if (span.from <= tick)
      {
        intent = span.intent;
      }
    }
    SandboxText::tooltipText(
        std::format("{} (tick {})", SandboxNames::intent(intent), tick)
            .c_str());
  }
}

namespace
{
/** His movement mode now and his own bands (fatigue plan 4.2). */
std::string movementText(const MatchEngine& engine, const MatchPlayer& slot)
{
  const float speedNow = std::hypot(slot.velocity.x, slot.velocity.y);
  std::string bands;
  for (const MovementMode mode : {MovementMode::WALK, MovementMode::JOG,
                                  MovementMode::RUN, MovementMode::SPRINT})
  {
    bands +=
        std::format("{}{} ≤{:.2f}", bands.empty() ? "" : " · ",
                    movementModeName(mode), engine.speedBand(slot, mode).high);
  }
  return std::format("{} · {}",
                     movementModeName(engine.movementModeAt(slot, speedNow)),
                     bands);
}

/** Down: how long, and what he is doing about it. */
std::string downText(const MatchPlayer& slot)
{
  if (!slot.fallen)
  {
    return {};
  }
  std::string text = std::format("down {:.1f} s", slot.downSeconds);
  if (slot.bigHitSeconds > 0.0F)
  {
    text += std::format(" (big hit, at least {:.1f} s)", slot.bigHitSeconds);
  }
  if (slot.velocity.x != 0.0F || slot.velocity.y != 0.0F)
  {
    text += slot.lyingAcross ? ", tumbling" : ", sliding";
  }
  else if (slot.rollSeconds > 0.0F)
  {
    text += ", rolling in pain";
  }
  else if (slot.gettingUp)
  {
    text += std::format(", getting up ({:.1f} s)", slot.getUpSeconds);
  }
  return text;
}

/** Whom he is going round, and on which side. */
std::string goingRoundText(const MatchEngine& engine, const MatchPlayer& slot)
{
  if (slot.wayRoundOf < 0 ||
      static_cast<std::size_t>(slot.wayRoundOf) >= engine.getPlayers().size())
  {
    return {};
  }
  const MatchPlayer& other =
      engine.getPlayers()[static_cast<std::size_t>(slot.wayRoundOf)];
  return std::format(
      "going round {} ({})",
      (other.player != nullptr) ? other.player->getName() : std::string("?"),
      slot.wayRoundSide > 0 ? "left" : "right");
}

/** Everything true of him now, joined; "-" if nothing. */
std::string stateText(const MatchEngine& engine, const MatchPlayer& slot,
                      PlayerID inspected, PlayerID owner)
{
  std::string text;
  const auto flag = [&text](bool on, const std::string& what)
  {
    if (!on)
    {
      return;
    }
    if (!text.empty())
    {
      text += " · ";
    }
    text += what;
  };
  flag(inspected == owner, "has the ball");
  flag(slot.isPressing, "pressing");
  flag(slot.isMakingRun, "making a run");
  flag(slot.isTrapping, "controlling the ball");
  flag(slot.isDiving, "diving");
  flag(slot.isInjured, "injured");
  flag(slot.stumbleSeconds > 0.0F, "stumbling");
  flag(slot.collisionPause > 0.0F && slot.stumbleSeconds <= 0.0F,
       "knocked (paused)");
  flag(slot.fallen, downText(slot));
  flag(slot.wayRoundOf >= 0, goingRoundText(engine, slot));
  flag(engine.getControlledPlayer() == inspected, "human-controlled");
  return text.empty() ? std::string("-") : text;
}

std::string engineAttributesText(const MatchPlayer& slot)
{
  return std::format(
      "Pac {:.0f} · Sho {:.0f} · Pas {:.0f} · Dri {:.0f} · "
      "Def {:.0f} · Phy {:.0f} · Sta {:.0f} · Vis {:.0f}"
      "{} · {:.2f} m",
      slot.pace * 100.0F, slot.shooting * 100.0F, slot.passing * 100.0F,
      slot.dribbling * 100.0F, slot.defending * 100.0F,
      slot.physicality * 100.0F, slot.endurance * 100.0F, slot.vision * 100.0F,
      slot.isGoalkeeper ? std::format(" · GK {:.0f}", slot.goalkeeping * 100.0F)
                        : std::string(),
      slot.heightMetres);
}

/** Every derived stat in the registry, with its physical value if it has
 * one, and the help that explains them. */
void derivedStatsRow(const MatchPlayer& slot)
{
  std::string derivedText;
  std::string derivedHelp =
      "Stats the match works out from the attributes above, with the "
      "handshake's recipes (match_derived_stats.h). Future game stats "
      "stand in for stats the game will carry; match-engine-only stats "
      "exist only in the match:";
  for (std::size_t index = 0; index < DERIVED_STAT_COUNT; ++index)
  {
    const auto stat = static_cast<DerivedStat>(index);
    const DerivedStatInfo& info = derivedStatInfo(stat);
    const float rating = slot.derivedStat(stat);
    if (!derivedText.empty())
    {
      derivedText += " · ";
    }
    derivedText += std::format("{} {:.0f}", info.label, rating * 100.0F);
    if (!info.unit.empty())
    {
      derivedText += std::format(" ({:.4g} {})",
                                 derivedPhysicalValue(stat, rating), info.unit);
    }
    derivedHelp += std::format("\n{} ({}): {}", info.name,
                               derivedLayerName(info.layer), info.description);
  }
  derivedText +=
      std::format(" · weight {:.0f} kg",
                  bodyMassKilograms(slot.heightMetres,
                                    slot.derivedStat(DerivedStat::STRENGTH)));
  derivedHelp +=
      "\nWeight: a proxy until the game carries one, from his height and "
      "Strength (collision plan).";
  property("Derived stats", derivedText, derivedHelp.c_str());
}
}  // namespace

/** The follow toggle; returns whom to inspect (the ball's man while
 * following, else the selection). A new selection stops following. */
PlayerID DebugInspector::followedOrSelected(const MatchEngine& engine)
{
  if (selected != last_selected)
  {
    follow_ball = false;
    last_selected = selected;
  }
  ImGui::Checkbox("Follow the ball", &follow_ball);
  if (ImGui::IsItemHovered())
  {
    SandboxText::tooltipText(
        "Follow the controller, touch claimant or restart taker; "
        "keep following the last attachment while the ball is free. "
        "An expected receiver does not have control. Picking a "
        "player switches this off.");
  }
  ImGui::SameLine();
  const PlayerID owner = (engine.getPossessionAttachment() != nullptr)
                             ? engine.getPossessionAttachment()->getId()
                             : 0;
  if (follow_ball && owner != 0)
  {
    followed = owner;
  }
  return follow_ball && followed != 0 ? followed : selected;
}

/** Picker: every player seen in the match, by name. */
void DebugInspector::renderPlayerPicker(const SandboxRecorder& recorder,
                                        const MatchEngine& engine)
{
  const std::string current = (selected != 0U) ? recorder.playerName(selected)
                                               : std::string("Choose a player");
  ImGui::SetNextItemWidth(PLAYER_PICKER_WIDTH * Theme::scale());
  if (SandboxPageUI::beginCombo("##inspect", current.c_str(),
                                ImGuiComboFlags_HeightLarge))
  {
    for (const MatchPlayer& slot : engine.getPlayers())
    {
      if (slot.player == nullptr)
      {
        continue;
      }
      const std::string label =
          std::format("{}  ({}, {})", slot.player->getName(),
                      slot.isHomeTeam ? "home" : "away",
                      RoleUtils::shortName(slot.player->getRole()));
      if (ImGui::Selectable(label.c_str(), slot.player->getId() == selected))
      {
        selected = slot.player->getId();
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  SandboxText::disabledText("or click him on the review pitch");
}

/** Now: what he is doing and why. */
void DebugInspector::renderPlayerNow(const MatchEngine& engine,
                                     const MatchPlayer& slot,
                                     PlayerID inspected) const
{
  constexpr ImGuiTableFlags flags =
      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH;
  if (!ImGui::BeginTable("##now", 2, flags))
  {
    return;
  }
  const Job job = jobOf(inspected, slot.isHomeTeam);
  const PlayerID owner = (engine.getPossessionAttachment() != nullptr)
                             ? engine.getPossessionAttachment()->getId()
                             : 0;
  ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed,
                          PROPERTY_COLUMN_WIDTH * Theme::scale());
  ImGui::TableSetupColumn("value");
  property("Brain", std::string(brainName(engine.getBrain(slot.isHomeTeam))));
  property("Body", std::string(bodyName(engine.getBody(slot.isHomeTeam))));
  property("Intent", SandboxNames::intent(slot.intent));
  property("Why", job != Job::NONE
                      ? std::format("planner job: {}", SandboxNames::job(job))
                      : std::string("no team job: his role, the team phase and "
                                    "the shape rules set it"));
  property("Target", std::format("{:.1f} m away{}",
                                 metres(slot.position, slot.tacticalTarget),
                                 slot.urgentMovement ? " · urgent" : ""));
  property("Speed",
           std::format("{:.1f} m/s (top {:.1f}){}",
                       std::hypot(slot.velocity.x, slot.velocity.y),
                       slot.maxSpeed, slot.isSprinting ? " · sprinting" : ""));
  property("Movement", movementText(engine, slot),
           "His movement mode by his speed, and the top of each mode's "
           "band for him now (m/s): walk to his walk-run transition, "
           "jog to 0.82 x critical speed, run to critical speed, sprint "
           "to his top speed. Fuel lowers the bands.");
  property("Facing",
           std::format("{:.0f}°", slot.facingAngle * DEGREES_HALF_TURN /
                                      std::numbers::pi_v<float>));
  property("Stamina",
           std::format("{:.0f}% · sprint reserve {:.0f}%",
                       slot.stamina * 100.0F, slot.sprintReserve * 100.0F));
  property("Cooldowns", std::format("action {:.2f} s · tackle {:.2f} s",
                                    slot.actionCooldown, slot.tackleCooldown));
  property("State", stateText(engine, slot, inspected, owner));
  property("Engine attributes", engineAttributesText(slot),
           "The values the match engine uses: the career attributes after "
           "the level shift and the contrast curve, capped at 100. Strong "
           "players often reach the cap.");
  derivedStatsRow(slot);
  if (const PlayerMatchStats* stats = engine.findPlayerStats(inspected))
  {
    property("Match so far",
             std::format("touches {} · passes {}/{} · shots {} · tackles {}/{} "
                         "· {:.0f} m covered · rating {:.1f}",
                         stats->touches, stats->passesCompleted,
                         stats->passesAttempted, stats->shots,
                         stats->tacklesWon, stats->tacklesAttempted,
                         stats->distanceMetres, stats->rating));
  }
  ImGui::EndTable();
}

/** His latest decisions and actions up to this moment. */
void DebugInspector::renderRecentEvents(const SandboxRecorder& recorder,
                                        PlayerID inspected) const
{
  int shown = 0;
  for (std::size_t index = asOf.prefix; index > 0 && shown < RECENT_EVENT_LIMIT;
       --index)
  {
    const auto& entry = recorder.entries()[index - 1];
    const bool wanted =
        std::holds_alternative<MatchDecisionRecord>(entry.payload) ||
        std::holds_alternative<MatchActionRecord>(entry.payload) ||
        std::holds_alternative<Recorder::PossessionChange>(entry.payload);
    if (!wanted)
    {
      continue;
    }
    const EngineDebugger::Described described =
        EngineDebugger::describe(recorder, entry);
    if (described.player != inspected)
    {
      continue;
    }
    SandboxText::disabledText(clock(entry.minute).c_str());
    ImGui::SameLine();
    SandboxText::wrappedText(described.detail.c_str());
    ++shown;
  }
  if (shown == 0)
  {
    SandboxText::disabledText("None yet.");
  }
}

void DebugInspector::renderPlayer(const SandboxRecorder& recorder,
                                  const MatchEngine& engine)
{
  const auto guard = recorder.lock();
  const std::uint64_t tick = engine.getSimulatedSteps();
  refresh(recorder, tick);
  const PlayerID inspected = followedOrSelected(engine);
  renderPlayerPicker(recorder, engine);
  const MatchPlayer* slot = findSlot(engine, inspected);
  if (slot == nullptr)
  {
    SandboxText::disabledText(
        "No player inspected (or he is not on the pitch).");
    return;
  }
  ImGui::TextUnformatted(
      std::format("{}  ·  {}  ·  natural {}{}", slot->player->getName(),
                  slot->isHomeTeam ? "Home" : "Away",
                  RoleUtils::shortName(slot->player->getRole()),
                  slot->isGoalkeeper ? "  ·  in goal" : "")
          .c_str());
  renderPlayerNow(engine, *slot, inspected);
  ImGui::SeparatorText("Intent over the match");
  renderIntentStrip(recorder, inspected,
                    {.cursor = tick, .end = recorder.lastTick()});
  ImGui::SeparatorText("Recent decisions and actions");
  renderRecentEvents(recorder, inspected);
}

// --- Referee ---------------------------------------------------------------

void DebugInspector::renderReferee(const SandboxRecorder& recorder,
                                   const MatchEngine& engine)
{
  auto guard = recorder.lock();
  refresh(recorder, engine.getSimulatedSteps());
  ImGui::TextUnformatted(
      std::format("Strictness this match: {:.2f} (1 is average)",
                  static_cast<double>(engine.getRefereeStrictness()))
          .c_str());
  SandboxText::disabledText(
      "Tackles carry a foul risk; the roll against it is drawn from the "
      "referee's own random stream (the roll itself arrives with phase 4).");

  int tackles = 0;
  int fouls = 0;
  float riskSum = 0.0F;
  float highestUnpunished = 0.0F;
  for (std::size_t index = 0; index < asOf.prefix; ++index)
  {
    const auto* action =
        std::get_if<MatchActionRecord>(&recorder.entries()[index].payload);
    if ((action == nullptr) || action->kind != MatchActionKind::TACKLE)
    {
      continue;
    }
    ++tackles;
    riskSum += action->foulPropensity;
    if (action->result == MatchDuelResult::FOUL)
    {
      ++fouls;
    }
    else
    {
      highestUnpunished = std::max(highestUnpunished, action->foulPropensity);
    }
  }
  ImGui::TextUnformatted(
      std::format(
          "Tackles {}  ·  fouls given {}  ·  mean foul risk {:.2f}  ·  "
          "riskiest tackle not given {:.2f}",
          tackles, fouls,
          static_cast<double>(
              tackles > 0 ? riskSum / static_cast<float>(tackles) : 0.0F),
          static_cast<double>(highestUnpunished))
          .c_str());

  // Full detail: the duels of this stretch with their chances and rolls.
  if (ImGui::CollapsingHeader("Duels with their rolls (full detail)"))
  {
    guard.unlock();
    ensureDetail(recorder, engine);
    renderDuelDetail(recorder, engine.getSimulatedSteps());
    guard.lock();
  }

  ImGui::SeparatorText("Calls");
  constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_ScrollY |
                                    ImGuiTableFlags_SizingStretchProp;
  if (!ImGui::BeginTable("##calls", 3, flags, ImVec2(0.0F, 0.0F)))
  {
    return;
  }
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed,
                          EVENT_TIME_WIDTH * Theme::scale());
  ImGui::TableSetupColumn("Call", ImGuiTableColumnFlags_WidthFixed,
                          EVENT_TYPE_WIDTH * Theme::scale());
  ImGui::TableSetupColumn("Detail");
  ImGui::PushFont(SandboxAnalysisUI::bold(), HEADER_FONT_SIZE);
  ImGui::TableHeadersRow();
  ImGui::PopFont();
  for (std::size_t index = 0; index < asOf.prefix; ++index)
  {
    const auto& entry = recorder.entries()[index];
    const char* call = nullptr;
    if (const auto* event = std::get_if<Recorder::EventEntry>(&entry.payload))
    {
      switch (recorder.events()[event->index].type)
      {
        case MatchEventType::FOUL:
          call = "Foul";
          break;
        case MatchEventType::ADVANTAGE:
          call = "Advantage";
          break;
        case MatchEventType::YELLOW_CARD:
          call = "Yellow card";
          break;
        case MatchEventType::SECOND_YELLOW:
          call = "Second yellow";
          break;
        case MatchEventType::RED_CARD:
          call = "Red card";
          break;
        case MatchEventType::OFFSIDE:
          call = "Offside";
          break;
        case MatchEventType::PENALTY:
          call = "Penalty";
          break;
        case MatchEventType::ADDED_TIME:
          call = "Added time";
          break;
        default:
          break;
      }
    }
    else if (const auto* action =
                 std::get_if<MatchActionRecord>(&entry.payload);
             (action != nullptr) && action->kind == MatchActionKind::TACKLE &&
             action->result == MatchDuelResult::FOUL)
    {
      call = "Foul tackle";
    }
    if (call == nullptr)
    {
      continue;
    }
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(clock(entry.minute).c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(call);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(
        EngineDebugger::describe(recorder, entry).detail.c_str());
  }
  ImGui::EndTable();
}

// --- Summary ---------------------------------------------------------------

void DebugInspector::refreshSummary(const SandboxRecorder& recorder)
{
  if (summary_prefix == asOf.prefix)
  {
    return;
  }
  summary_prefix = asOf.prefix;
  totals.clear();
  phaseTicks = {};
  std::unordered_map<PlayerID, std::size_t> slots;
  const auto totalsOf = [&](PlayerID player, bool homeTeam) -> PlayerTotals&
  {
    const auto [found, added] = slots.emplace(player, totals.size());
    if (added)
    {
      totals.push_back({.player = player, .homeTeam = homeTeam});
    }
    return totals[found->second];
  };
  struct Current
  {
    PlayerIntent intent = PlayerIntent::HOLD_SHAPE;
    std::uint64_t since = 0;
  };
  std::unordered_map<PlayerID, Current> intents;
  std::array<TeamPhase, 2> phases{TeamPhase::SET_PIECE, TeamPhase::SET_PIECE};
  std::array<std::uint64_t, 2> phaseStart{};

  for (std::size_t index = 0; index < asOf.prefix; ++index)
  {
    const auto& entry = recorder.entries()[index];
    if (const auto* decision = std::get_if<MatchDecisionRecord>(&entry.payload))
    {
      PlayerTotals& player = totalsOf(decision->player, decision->homeTeam);
      ++player.decisions;
      ++player.chosen.at(static_cast<std::size_t>(decision->chosen));
      if (decision->chosen != decision->chosenWithoutNoise)
      {
        ++player.noiseFlips;
      }
    }
    else if (const auto* action =
                 std::get_if<MatchActionRecord>(&entry.payload))
    {
      ++totalsOf(action->player, action->homeTeam).actions;
    }
    else if (const auto* change =
                 std::get_if<Recorder::IntentChange>(&entry.payload))
    {
      PlayerTotals& player = totalsOf(change->player, change->homeTeam);
      ++player.intentChanges;
      Current& current = intents[change->player];
      player.intentTicks.at(static_cast<std::size_t>(current.intent)) +=
          entry.tick - current.since;
      current = {.intent = change->to, .since = entry.tick};
    }
    else if (const auto* phase =
                 std::get_if<Recorder::PhaseChange>(&entry.payload))
    {
      const std::size_t team = side(phase->homeTeam);
      phaseTicks.at(team).at(static_cast<std::size_t>(phases.at(team))) +=
          entry.tick - phaseStart.at(team);
      phases.at(team) = phase->to;
      phaseStart.at(team) = entry.tick;
    }
  }
  // The intents and phases still running at the moment shown.
  for (auto& [player, current] : intents)
  {
    if (const auto found = slots.find(player); found != slots.end())
    {
      totals[found->second].intentTicks.at(static_cast<std::size_t>(
          current.intent)) += asOf.tick - current.since;
    }
  }
  for (std::size_t team = 0; team < 2; ++team)
  {
    phaseTicks.at(team).at(static_cast<std::size_t>(phases.at(team))) +=
        asOf.tick - phaseStart.at(team);
  }
  std::ranges::sort(totals,
                    [](const PlayerTotals& left, const PlayerTotals& right)
                    {
                      return left.homeTeam != right.homeTeam
                                 ? left.homeTeam
                                 : left.player < right.player;
                    });
}

void DebugInspector::renderSummary(const SandboxRecorder& recorder,
                                   const MatchEngine& engine)
{
  const auto guard = recorder.lock();
  refresh(recorder, engine.getSimulatedSteps());
  refreshSummary(recorder);
  SandboxText::disabledText(
      std::format("From kick-off to {} (the moment shown).",
                  clock(engine.getMatchTimeMinutes()))
          .c_str());

  // Time in each phase, per side.
  ImGui::SeparatorText("Team phases (share of time)");
  for (std::size_t team = 0; team < 2; ++team)
  {
    std::uint64_t total = 0;
    for (const std::uint64_t ticks : phaseTicks.at(team))
    {
      total += ticks;
    }
    std::string line = team == 0 ? "Home: " : "Away: ";
    for (std::size_t phase = 0; phase < phaseTicks.at(team).size(); ++phase)
    {
      if (total > 0 && phaseTicks.at(team).at(phase) > 0)
      {
        line += std::format(
            "{} {:.0f}%  ", SandboxNames::phase(static_cast<TeamPhase>(phase)),
            100.0 * static_cast<double>(phaseTicks.at(team).at(phase)) /
                static_cast<double>(total));
      }
    }
    ImGui::TextUnformatted(line.c_str());
  }

  ImGui::SeparatorText("Players");
  constexpr ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
  if (!ImGui::BeginTable("##summary", SUMMARY_TABLE_COLUMNS, flags,
                         ImVec2(0.0F, 0.0F)))
  {
    return;
  }
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch,
                          PLAYER_COLUMN_WEIGHT);
  ImGui::TableSetupColumn("Side");
  ImGui::TableSetupColumn("Decisions");
  ImGui::TableSetupColumn("Pass/shot/carry/shield %",
                          ImGuiTableColumnFlags_WidthStretch,
                          DETAIL_COLUMN_WEIGHT);
  ImGui::TableSetupColumn("Noise flips");
  ImGui::TableSetupColumn("Actions");
  ImGui::TableSetupColumn("Intent changes/min");
  ImGui::TableSetupColumn("Main intents", ImGuiTableColumnFlags_WidthStretch,
                          TOP_INTENTS_WEIGHT);
  ImGui::PushFont(SandboxAnalysisUI::bold(), HEADER_FONT_SIZE);
  ImGui::TableHeadersRow();
  ImGui::PopFont();
  const float minutes = std::max(1.0F / 60.0F, seconds(asOf.tick) / 60.0F);
  for (const PlayerTotals& player : totals)
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    selectable(recorder, player.player);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(player.homeTeam ? "H" : "A");
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(std::format("{}", player.decisions).c_str());
    ImGui::TableNextColumn();
    if (player.decisions > 0)
    {
      const auto share = [&player](ScenarioAction action)
      {
        return 100.0F *
               static_cast<float>(
                   player.chosen.at(static_cast<std::size_t>(action))) /
               static_cast<float>(player.decisions);
      };
      ImGui::TextUnformatted(
          std::format("{:.0f} / {:.0f} / {:.0f} / {:.0f}",
                      static_cast<double>(share(ScenarioAction::PASS)),
                      static_cast<double>(share(ScenarioAction::SHOT)),
                      static_cast<double>(share(ScenarioAction::CARRY)),
                      static_cast<double>(share(ScenarioAction::SHIELD) +
                                          share(ScenarioAction::CLEAR)))
              .c_str());
    }
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(std::format("{}", player.noiseFlips).c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(std::format("{}", player.actions).c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(
        std::format("{:.1f}",
                    static_cast<double>(
                        static_cast<float>(player.intentChanges) / minutes))
            .c_str());
    ImGui::TableNextColumn();
    // His three most-held intents, by share of time.
    std::uint64_t total = 0;
    std::array<std::size_t, INTENTS> order{};
    for (std::size_t intent = 0; intent < INTENTS; ++intent)
    {
      order.at(intent) = intent;
      total += player.intentTicks.at(intent);
    }
    std::ranges::sort(
        order, [&player](std::size_t left, std::size_t right)
        { return player.intentTicks.at(left) > player.intentTicks.at(right); });
    std::string main;
    for (std::size_t rank = 0; rank < 3 && total > 0; ++rank)
    {
      const std::uint64_t ticks = player.intentTicks.at(order.at(rank));
      if (ticks == 0)
      {
        break;
      }
      main += std::format(
          "{} {:.0f}%  ",
          SandboxNames::intent(static_cast<PlayerIntent>(order.at(rank))),
          100.0 * static_cast<double>(ticks) / static_cast<double>(total));
    }
    ImGui::TextUnformatted(main.c_str());
  }
  ImGui::EndTable();
}

// --- Overlays --------------------------------------------------------------

void DebugInspector::renderOverlayToggles()
{
  ImGui::Checkbox("Targets", &show_targets);
  if (ImGui::IsItemHovered())
  {
    SandboxText::tooltipText(
        "Line from each player to where he is heading, coloured "
        "by intent: runs orange, pressing red, support green, "
        "marking blue, recovering yellow, holding grey.");
  }
  SandboxAnalysisUI::next();
  ImGui::Checkbox("Jobs", &show_jobs);
  if (ImGui::IsItemHovered())
  {
    SandboxText::tooltipText(
        "Planner jobs: B1/B2 to the ball, R1-R3 runners, LA late "
        "arrival, FP far post, OV overlap, S1-S3 supporters, CO "
        "cover outlet.");
  }
  SandboxAnalysisUI::next();
  ImGui::Checkbox("Pass options", &show_pass_options);
  if (ImGui::IsItemHovered())
  {
    SandboxText::tooltipText(
        "The latest on-ball decision (within 3 s): best pass "
        "green, runner-up amber, and the option scores.");
  }
  SandboxAnalysisUI::next();
  ImGui::Checkbox("Selected only", &selected_only);
}

namespace
{
bool projectOnPitch(const IMatchRenderer& renderer, PitchPoint pitch,
                    ImVec2& screen)
{
  return renderer.projectPitch(pitch, 0.0F, screen.x, screen.y);
}

/** The decision's scores, by his feet. */
void drawDecisionLabel(ImDrawList* drawList, ImVec2 from,
                       const MatchDecisionRecord& decision, float scale)
{
  const auto score = [](float value)
  {
    return std::isfinite(value) ? std::format("{:.2f}", value)
                                : std::string("-");
  };
  const std::string label = std::format(
      "{}  pass {} shot {} carry {} shield {}{}",
      SandboxNames::choice(decision.chosen), score(decision.pass),
      score(decision.shot), score(decision.carry), score(decision.shield),
      decision.chosen != decision.chosenWithoutNoise ? "  (noise)" : "");
  const ImVec2 textAt(from.x + (10.0F * scale), from.y + (8.0F * scale));
  const ImVec2 size = ImGui::CalcTextSize(label.c_str());
  drawList->AddRectFilled(
      ImVec2(textAt.x - PASS_LABEL_PADDING_X, textAt.y - PASS_LABEL_PADDING_Y),
      ImVec2(textAt.x + size.x + PASS_LABEL_PADDING_X,
             textAt.y + size.y + PASS_LABEL_PADDING_Y),
      IM_COL32(0, 0, 0, 190), LABEL_ROUNDING);
  drawList->AddText(textAt, IM_COL32(255, 255, 255, 255), label.c_str());
}
}  // namespace

/** One player's overlay: his target, his job tag, and the selection ring. */
void DebugInspector::drawPlayerOverlay(ImDrawList* drawList,
                                       const IMatchRenderer& renderer,
                                       const MatchPlayer& slot, ImVec2 at) const
{
  const float scale = Theme::scale();
  const PlayerID id = slot.player->getId();
  const bool isSelected = id == selected;
  ImVec2 target;
  if (show_targets && projectOnPitch(renderer, slot.tacticalTarget, target))
  {
    const ImU32 color = intentColor(slot.intent);
    drawList->AddLine(at, target, color,
                      (isSelected ? SELECTED_STROKE : OVERLAY_STROKE) * scale);
    drawList->AddCircle(target, TARGET_RADIUS * scale, color, 0,
                        OVERLAY_STROKE * scale);
  }
  const char* tag = show_jobs ? jobTag(jobOf(id, slot.isHomeTeam)) : "";
  if (*tag != '\0')
  {
    const ImVec2 textAt(at.x + (7.0F * scale), at.y - (16.0F * scale));
    const ImVec2 size = ImGui::CalcTextSize(tag);
    drawList->AddRectFilled(
        ImVec2(textAt.x - LABEL_PADDING_X, textAt.y - 1.0F),
        ImVec2(textAt.x + size.x + LABEL_PADDING_X, textAt.y + size.y + 1.0F),
        IM_COL32(0, 0, 0, 170), LABEL_ROUNDING);
    drawList->AddText(textAt, IM_COL32(255, 230, 120, 255), tag);
  }
  if (isSelected)
  {
    drawList->AddCircle(at, SELECTION_RADIUS * scale,
                        IM_COL32(255, 220, 60, 255), 0,
                        SELECTED_STROKE * scale);
  }
}

/** The latest decision's pass options, while it is recent. */
void DebugInspector::drawPassOptions(const SandboxRecorder& recorder,
                                     const MatchEngine& engine,
                                     const IMatchRenderer& renderer,
                                     ImDrawList* drawList) const
{
  if (!show_pass_options || !asOf.lastDecision)
  {
    return;
  }
  const float scale = Theme::scale();
  const auto& entry = recorder.entries()[*asOf.lastDecision];
  const auto& decision = std::get<MatchDecisionRecord>(entry.payload);
  const MatchPlayer* passer = findSlot(engine, decision.player);
  const bool recent =
      engine.getSimulatedSteps() - entry.tick <= PASS_OPTION_TICKS;
  ImVec2 from;
  if ((passer == nullptr) || !recent ||
      (selected_only && decision.player != selected) ||
      !projectOnPitch(renderer, passer->position, from))
  {
    return;
  }
  const auto arrow = [&](PlayerID receiver, PassArrowStyle style)
  {
    const MatchPlayer* target = findSlot(engine, receiver);
    ImVec2 to;
    if (!target || !projectOnPitch(renderer, target->position, to))
    {
      return;
    }
    drawList->AddLine(from, to, style.color, style.width * scale);
    drawList->AddCircleFilled(to, PASS_MARKER_RADIUS * scale, style.color);
  };
  arrow(decision.runnerUpReceiver,
        {.color = IM_COL32(240, 180, 40, 220), .width = RUNNER_UP_STROKE});
  arrow(decision.bestReceiver,
        {.color = IM_COL32(80, 230, 110, 255), .width = BEST_PASS_STROKE});
  drawDecisionLabel(drawList, from, decision, scale);
}

void DebugInspector::drawOverlays(const SandboxRecorder& recorder,
                                  const MatchEngine& engine,
                                  const IMatchRenderer& renderer, bool clicked)
{
  const auto guard = recorder.lock();
  refresh(recorder, engine.getSimulatedSteps());
  const float scale = Theme::scale();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  PlayerID nearest = 0;
  float nearestDistance = PICK_RADIUS * scale;
  for (const MatchPlayer& slot : engine.getPlayers())
  {
    ImVec2 at;
    if ((slot.player == nullptr) || !slot.onPitch ||
        !projectOnPitch(renderer, slot.position, at))
    {
      continue;
    }
    const float distance = std::hypot(mouse.x - at.x, mouse.y - at.y);
    if (distance < nearestDistance)
    {
      nearestDistance = distance;
      nearest = slot.player->getId();
    }
    if (!selected_only || slot.player->getId() == selected)
    {
      drawPlayerOverlay(drawList, renderer, slot, at);
    }
  }
  drawPassOptions(recorder, engine, renderer, drawList);
  if (clicked && nearest != 0)
  {
    selected = nearest;
  }
}

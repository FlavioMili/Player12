// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_inspector.h"
#include "tools/match_sandbox_analysis_ui.h"

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
#include "tools/match_sandbox_debugger.h"
#include "tools/match_sandbox_names.h"

namespace
{
using Recorder = SandboxRecorder;
using Job = SandboxRecorder::Job;

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

/** Colour of an intent: runs orange, pressing red, support green, shape grey. */
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
    case Job::NONE: return "";
    case Job::PRESSER_1: return "B1";
    case Job::PRESSER_2: return "B2";
    case Job::RUNNER_1: return "R1";
    case Job::RUNNER_2: return "R2";
    case Job::RUNNER_3: return "R3";
    case Job::MIDFIELD_ARRIVAL: return "LA";
    case Job::FAR_POST: return "FP";
    case Job::OVERLAP: return "OV";
    case Job::SUPPORTER_1: return "S1";
    case Job::SUPPORTER_2: return "S2";
    case Job::SUPPORTER_3: return "S3";
    case Job::COVER_OUTLET: return "CO";
  }
  return "";
}

const MatchPlayer* findSlot(const MatchEngine& engine, PlayerID id)
{
  for (const MatchPlayer& slot : engine.getPlayers())
    if (slot.player && slot.player->getId() == id) return &slot;
  return nullptr;
}

std::string clock(float minute)
{
  const int total =
      static_cast<int>(std::floor(std::max(0.0f, minute) * 60.0f));
  return std::format("{:02}:{:02}", total / 60, total % 60);
}

/** Key/value row of a two-column table. */
void property(const char* key, const std::string& value,
              const char* tooltip = nullptr)
{
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TextDisabled("%s", key);
  if (tooltip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
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
    asOf = AsOf{};
  asOf.tick = tick;
  std::size_t index = asOf.prefix;
  for (; index < entries.size() && entries[index].tick <= tick; ++index)
  {
    const auto& entry = entries[index];
    if (const auto* change = std::get_if<Recorder::JobChange>(&entry.payload))
    {
      const auto job = static_cast<std::size_t>(change->job);
      asOf.jobs[side(change->homeTeam)][job] = change->player;
      asOf.scores[side(change->homeTeam)][job] = change->score;
    }
    else if (const auto* phase =
                 std::get_if<Recorder::PhaseChange>(&entry.payload))
    {
      asOf.phaseSince[side(phase->homeTeam)] = entry.tick;
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
  const auto& held = asOf.jobs[side(homeTeam)];
  for (std::size_t job = 1; job < held.size(); ++job)
    if (held[job] == player && player != 0) return static_cast<Job>(job);
  return Job::NONE;
}

void DebugInspector::selectable(const SandboxRecorder& recorder,
                                PlayerID player)
{
  const std::string label =
      std::format("{}##select{}", recorder.playerName(player), player);
  if (ImGui::Selectable(label.c_str(), selected == player,
                        ImGuiSelectableFlags_SpanAllColumns))
    selected = player;
}

// --- Team ------------------------------------------------------------------

void DebugInspector::renderTeam(const SandboxRecorder& recorder,
                                const MatchEngine& engine)
{
  auto guard = recorder.lock();
  refresh(recorder, engine.getSimulatedSteps());
  constexpr std::array<const char*, 2> SIDES{"Home", "Away"};
  for (int index = 0; index < 2; ++index)
  {
    if (index > 0) ImGui::SameLine();
    if (ImGui::RadioButton(SIDES[static_cast<std::size_t>(index)],
                           team_side == index))
      team_side = index;
  }
  const bool home = team_side == 0;
  const std::size_t team = side(home);
  const TeamPhase phase = home ? engine.getHomePhase() : engine.getAwayPhase();
  ImGui::SameLine();
  ImGui::Text("   Phase: %s for %.1f s", SandboxNames::phase(phase),
              seconds(engine.getSimulatedSteps() - asOf.phaseSince[team]));
  if (engine.getTransitionSecondsRemaining() > 0.0f)
  {
    ImGui::SameLine();
    ImGui::TextDisabled("(transition %.1f s left)",
                        engine.getTransitionSecondsRemaining());
  }

  // The planner's jobs, with the scores that ranked them.
  ImGui::SeparatorText("Planner jobs");
  constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_RowBg |
                                         ImGuiTableFlags_BordersInnerV |
                                         ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("##jobs", 3, tableFlags))
  {
    ImGui::TableSetupColumn("Job");
    ImGui::TableSetupColumn("Player");
    ImGui::TableSetupColumn("Ranking score");
    ImGui::PushFont(SandboxAnalysisUI::bold(),14);
    ImGui::TableHeadersRow();
    ImGui::PopFont();
    for (std::size_t job = 1; job < JOBS; ++job)
    {
      const PlayerID player = asOf.jobs[team][job];
      if (player == 0) continue;
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(SandboxNames::job(static_cast<Job>(job)));
      ImGui::TableNextColumn();
      selectable(recorder, player);
      ImGui::TableNextColumn();
      const auto kind = static_cast<Job>(job);
      if (kind == Job::PRESSER_1 || kind == Job::PRESSER_2)
        ImGui::Text("arrives in %.2f s", asOf.scores[team][job]);
      else if (kind == Job::RUNNER_1 || kind == Job::RUNNER_2 ||
               kind == Job::RUNNER_3)
        ImGui::Text("run priority %.2f", asOf.scores[team][job]);
    }
    ImGui::EndTable();
  }

  // Every player of the side at this moment.
  // Full detail: both rankings, every candidate broken down.
  if (ImGui::CollapsingHeader("Ranking breakdowns (full detail)"))
  {
    guard.unlock();
    ensureDetail(recorder, engine);
    renderRankingDetail(recorder, home, engine.getSimulatedSteps());
    guard.lock();
  }

  ImGui::SeparatorText("Players");
  if (ImGui::BeginTable("##players", 7, tableFlags | ImGuiTableFlags_ScrollY,
                        ImVec2(0.0f, 0.0f)))
  {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Pos");
    ImGui::TableSetupColumn("Intent", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("Job", ImGuiTableColumnFlags_WidthStretch, 1.2f);
    ImGui::TableSetupColumn("To target");
    ImGui::TableSetupColumn("Speed");
    ImGui::TableSetupColumn("Stamina");
    ImGui::PushFont(SandboxAnalysisUI::bold(),14);
    ImGui::TableHeadersRow();
    ImGui::PopFont();
    const PossessionSnapshot possession = engine.getPossessionSnapshot();
    for (const MatchPlayer& slot : engine.getPlayers())
    {
      if (!slot.player || slot.isHomeTeam != home || !slot.onPitch) continue;
      const PlayerID id = slot.player->getId();
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      selectable(recorder, id);
      const char* ballStatus = id == possession.controller ? "(control)"
                               : id == possession.claimant ? "(claim)"
                               : id == possession.legacyOwner
                                   ? (possession.openPlay ? "(attached)" : "(restart)")
                                   : nullptr;
      if (ballStatus)
      {
        ImGui::SameLine();
        ImGui::TextColored(Theme::palette().positive, "%s", ballStatus);
      }
      if (id == possession.expectedReceiver)
      {
        ImGui::SameLine();
        ImGui::TextDisabled("(expected)");
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(RoleUtils::shortName(slot.player->getRole()));
      ImGui::TableNextColumn();
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(intentColor(slot.intent)),
                         "%s", SandboxNames::intent(slot.intent));
      ImGui::TableNextColumn();
      const Job job = jobOf(id, home);
      if (job != Job::NONE) ImGui::TextUnformatted(SandboxNames::job(job));
      ImGui::TableNextColumn();
      ImGui::Text("%.1f m", metres(slot.position, slot.tacticalTarget));
      ImGui::TableNextColumn();
      ImGui::Text("%.1f m/s", std::hypot(slot.velocity.x, slot.velocity.y));
      ImGui::TableNextColumn();
      ImGui::Text("%.0f%%", slot.stamina * 100.0f);
    }
    ImGui::EndTable();
  }
}

// --- Player ----------------------------------------------------------------

void DebugInspector::renderIntentStrip(const SandboxRecorder& recorder,
                                       PlayerID player, std::uint64_t cursor,
                                       std::uint64_t end)
{
  // His intents over the whole match; the cursor marks the moment shown.
  const float scale = Theme::scale();
  const float width = std::max(50.0f, ImGui::GetContentRegionAvail().x);
  const float height = 14.0f * scale;
  const ImVec2 min = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##intents", ImVec2(width, height));
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(min, ImVec2(min.x + width, min.y + height),
                          IM_COL32(25, 27, 32, 255));
  if (end == 0) return;
  const auto xOf = [&](std::uint64_t tick)
  {
    return min.x + width * std::min(1.0f, static_cast<float>(tick) /
                                              static_cast<float>(end));
  };
  struct Span
  {
    std::uint64_t from = 0;
    PlayerIntent intent = PlayerIntent::HOLD_SHAPE;
  };
  std::vector<Span> spans{{0, PlayerIntent::HOLD_SHAPE}};
  for (const auto& entry : recorder.entries())
    if (const auto* change =
            std::get_if<Recorder::IntentChange>(&entry.payload);
        change && change->player == player)
      spans.push_back({entry.tick, change->to});
  for (std::size_t index = 0; index < spans.size(); ++index)
  {
    const std::uint64_t until =
        index + 1 < spans.size() ? spans[index + 1].from : end;
    drawList->AddRectFilled(
        ImVec2(xOf(spans[index].from), min.y),
        ImVec2(std::max(xOf(until), xOf(spans[index].from) + 1.0f),
               min.y + height),
        intentColor(spans[index].intent));
  }
  const float cursorX = xOf(cursor);
  drawList->AddLine(ImVec2(cursorX, min.y - 2.0f),
                    ImVec2(cursorX, min.y + height + 2.0f),
                    IM_COL32(255, 255, 255, 255), 2.0f * scale);
  if (ImGui::IsItemHovered())
  {
    const float share = std::clamp(
        (ImGui::GetIO().MousePos.x - min.x) / width, 0.0f, 1.0f);
    const auto tick =
        static_cast<std::uint64_t>(share * static_cast<float>(end));
    PlayerIntent intent = PlayerIntent::HOLD_SHAPE;
    for (const Span& span : spans)
      if (span.from <= tick) intent = span.intent;
    ImGui::SetTooltip("%s (tick %llu)", SandboxNames::intent(intent),
                      static_cast<unsigned long long>(tick));
  }
}

void DebugInspector::renderPlayer(const SandboxRecorder& recorder,
                                  const MatchEngine& engine)
{
  const auto guard = recorder.lock();
  const std::uint64_t tick = engine.getSimulatedSteps();
  refresh(recorder, tick);

  // A new selection (pitch, tables, log) stops following the ball.
  if (selected != last_selected)
  {
    follow_ball = false;
    last_selected = selected;
  }
  ImGui::Checkbox("Follow the ball", &follow_ball);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Follow the controller, touch claimant or restart taker; "
                      "keep following the last attachment while the ball is free. "
                      "An expected receiver does not have control. Picking a "
                      "player switches this off.");
  ImGui::SameLine();
  const PlayerID owner =
      engine.getPossessionAttachment() ? engine.getPossessionAttachment()->getId() : 0;
  if (follow_ball && owner != 0) followed = owner;
  const PlayerID inspected = follow_ball && followed != 0 ? followed : selected;

  // Picker: every player seen in the match, by name.
  const std::string current =
      selected ? recorder.playerName(selected) : std::string("Choose a player");
  ImGui::SetNextItemWidth(240.0f * Theme::scale());
  if (ImGui::BeginCombo("##inspect", current.c_str(),
                        ImGuiComboFlags_HeightLarge))
  {
    for (const MatchPlayer& slot : engine.getPlayers())
    {
      if (!slot.player) continue;
      const std::string label = std::format(
          "{}  ({}, {})", slot.player->getName(), slot.isHomeTeam ? "home" : "away",
          RoleUtils::shortName(slot.player->getRole()));
      if (ImGui::Selectable(label.c_str(), slot.player->getId() == selected))
        selected = slot.player->getId();
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("or click him on the review pitch");
  const MatchPlayer* slot = findSlot(engine, inspected);
  if (!slot)
  {
    ImGui::TextDisabled("No player inspected (or he is not on the pitch).");
    return;
  }

  const Job job = jobOf(inspected, slot->isHomeTeam);
  ImGui::Text("%s  ·  %s  ·  natural %s%s", slot->player->getName().c_str(),
              slot->isHomeTeam ? "Home" : "Away",
              RoleUtils::shortName(slot->player->getRole()),
              slot->isGoalkeeper ? "  ·  in goal" : "");

  // Now: what he is doing and why.
  constexpr ImGuiTableFlags flags =
      ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH;
  if (ImGui::BeginTable("##now", 2, flags))
  {
    ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed,
                            140.0f * Theme::scale());
    ImGui::TableSetupColumn("value");
    property("Brain", std::string(brainName(engine.getBrain(slot->isHomeTeam))));
    property("Body", std::string(bodyName(engine.getBody(slot->isHomeTeam))));
    property("Intent", SandboxNames::intent(slot->intent));
    property("Why",
             job != Job::NONE
                 ? std::format("planner job: {}", SandboxNames::job(job))
                 : std::string("no team job: his role, the team phase and "
                               "the shape rules set it"));
    property("Target", std::format("{:.1f} m away{}",
                                   metres(slot->position, slot->tacticalTarget),
                                   slot->urgentMovement ? " · urgent" : ""));
    property("Speed", std::format("{:.1f} m/s (top {:.1f}){}",
                                  std::hypot(slot->velocity.x, slot->velocity.y),
                                  slot->maxSpeed,
                                  slot->isSprinting ? " · sprinting" : ""));
    {
      // His movement mode now and his own bands (fatigue plan 4.2).
      const float speedNow = std::hypot(slot->velocity.x, slot->velocity.y);
      std::string bands;
      for (const MovementMode mode : {MovementMode::WALK, MovementMode::JOG,
                                      MovementMode::RUN, MovementMode::SPRINT})
        bands += std::format("{}{} ≤{:.2f}", bands.empty() ? "" : " · ",
                             movementModeName(mode),
                             engine.speedBand(*slot, mode).high);
      property("Movement",
               std::format("{} · {}",
                           movementModeName(engine.movementModeAt(*slot, speedNow)),
                           bands),
               "His movement mode by his speed, and the top of each mode's "
               "band for him now (m/s): walk to his walk-run transition, "
               "jog to 0.82 x critical speed, run to critical speed, sprint "
               "to his top speed. Fuel lowers the bands.");
    }
    property("Facing", std::format("{:.0f}°", slot->facingAngle * 180.0f /
                                                  std::numbers::pi_v<float>));
    property("Stamina", std::format("{:.0f}% · sprint reserve {:.0f}%",
                                    slot->stamina * 100.0f,
                                    slot->sprintReserve * 100.0f));
    property("Cooldowns", std::format("action {:.2f} s · tackle {:.2f} s",
                                      slot->actionCooldown,
                                      slot->tackleCooldown));
    std::string flagsText;
    const auto flag = [&flagsText](bool on, const char* text)
    {
      if (!on) return;
      if (!flagsText.empty()) flagsText += " · ";
      flagsText += text;
    };
    flag(inspected == owner, "has the ball");
    flag(slot->isPressing, "pressing");
    flag(slot->isMakingRun, "making a run");
    flag(slot->isTrapping, "controlling the ball");
    flag(slot->isDiving, "diving");
    flag(slot->isInjured, "injured");
    flag(slot->stumbleSeconds > 0.0f, "stumbling");
    flag(slot->collisionPause > 0.0f && slot->stumbleSeconds <= 0.0f,
         "knocked (paused)");
    std::string downText;
    if (slot->fallen)
    {
      downText = std::format("down {:.1f} s", slot->downSeconds);
      if (slot->bigHitSeconds > 0.0f)
        downText += std::format(" (big hit, at least {:.1f} s)", slot->bigHitSeconds);
      if (slot->velocity.x != 0.0f || slot->velocity.y != 0.0f)
        downText += slot->lyingAcross ? ", tumbling" : ", sliding";
      else if (slot->rollSeconds > 0.0f)
        downText += ", rolling in pain";
      else if (slot->gettingUp)
        downText += std::format(", getting up ({:.1f} s)", slot->getUpSeconds);
    }
    flag(slot->fallen, downText.c_str());
    std::string roundText;
    if (slot->wayRoundOf >= 0 &&
        static_cast<std::size_t>(slot->wayRoundOf) < engine.getPlayers().size())
    {
      const MatchPlayer& other = engine.getPlayers()[static_cast<std::size_t>(slot->wayRoundOf)];
      roundText = std::format("going round {} ({})",
                              other.player ? other.player->getName() : std::string("?"),
                              slot->wayRoundSide > 0 ? "left" : "right");
    }
    flag(slot->wayRoundOf >= 0, roundText.c_str());
    flag(engine.getControlledPlayer() == inspected, "human-controlled");
    property("State", flagsText.empty() ? std::string("-") : flagsText);
    property("Engine attributes",
             std::format("Pac {:.0f} · Sho {:.0f} · Pas {:.0f} · Dri {:.0f} · "
                         "Def {:.0f} · Phy {:.0f} · Sta {:.0f} · Vis {:.0f}"
                         "{} · {:.2f} m",
                         slot->pace * 100.0f, slot->shooting * 100.0f,
                         slot->passing * 100.0f, slot->dribbling * 100.0f,
                         slot->defending * 100.0f, slot->physicality * 100.0f,
                         slot->endurance * 100.0f, slot->vision * 100.0f,
                         slot->isGoalkeeper
                             ? std::format(" · GK {:.0f}", slot->goalkeeping * 100.0f)
                             : std::string(),
                         slot->heightMetres),
             "The values the match engine uses: the career attributes after "
             "the level shift and the contrast curve, capped at 100. Strong "
             "players often reach the cap.");
    {
      // Every derived stat in the registry, with its physical value if it
      // has one.
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
        const float rating = slot->derivedStat(stat);
        if (!derivedText.empty()) derivedText += " · ";
        derivedText += std::format("{} {:.0f}", info.label, rating * 100.0f);
        if (!info.unit.empty())
          derivedText += std::format(
              " ({:.4g} {})", derivedPhysicalValue(stat, rating), info.unit);
        derivedHelp += std::format("\n{} ({}): {}", info.name,
                                   derivedLayerName(info.layer),
                                   info.description);
      }
      derivedText += std::format(
          " · weight {:.0f} kg",
          bodyMassKilograms(slot->heightMetres,
                            slot->derivedStat(DerivedStat::STRENGTH)));
      derivedHelp +=
          "\nWeight: a proxy until the game carries one, from his height and "
          "Strength (collision plan).";
      property("Derived stats", derivedText, derivedHelp.c_str());
    }
    if (const PlayerMatchStats* stats = engine.findPlayerStats(inspected))
      property("Match so far",
               std::format("touches {} · passes {}/{} · shots {} · tackles {}/{} "
                           "· {:.0f} m covered · rating {:.1f}",
                           stats->touches, stats->passesCompleted,
                           stats->passesAttempted, stats->shots,
                           stats->tacklesWon, stats->tacklesAttempted,
                           stats->distanceMetres, stats->rating));
    ImGui::EndTable();
  }

  ImGui::SeparatorText("Intent over the match");
  renderIntentStrip(recorder, inspected, tick, recorder.lastTick());

  // His latest decisions and actions up to this moment.
  ImGui::SeparatorText("Recent decisions and actions");
  int shown = 0;
  for (std::size_t index = asOf.prefix; index > 0 && shown < 8; --index)
  {
    const auto& entry = recorder.entries()[index - 1];
    const bool decision =
        std::holds_alternative<MatchDecisionRecord>(entry.payload);
    const bool action = std::holds_alternative<MatchActionRecord>(entry.payload);
    const bool possession =
        std::holds_alternative<Recorder::PossessionChange>(entry.payload);
    if (!decision && !action && !possession) continue;
    const EngineDebugger::Described described =
        EngineDebugger::describe(recorder, entry);
    if (described.player != inspected) continue;
    ImGui::TextDisabled("%s", clock(entry.minute).c_str());
    ImGui::SameLine();
    ImGui::TextWrapped("%s", described.detail.c_str());
    ++shown;
  }
  if (shown == 0) ImGui::TextDisabled("None yet.");
}

// --- Referee ---------------------------------------------------------------

void DebugInspector::renderReferee(const SandboxRecorder& recorder,
                                   const MatchEngine& engine)
{
  auto guard = recorder.lock();
  refresh(recorder, engine.getSimulatedSteps());
  ImGui::Text("Strictness this match: %.2f (1 is average)",
              engine.getRefereeStrictness());
  ImGui::TextDisabled(
      "Tackles carry a foul risk; the roll against it is drawn from the "
      "referee's own random stream (the roll itself arrives with phase 4).");

  int tackles = 0;
  int fouls = 0;
  float riskSum = 0.0f;
  float highestUnpunished = 0.0f;
  for (std::size_t index = 0; index < asOf.prefix; ++index)
  {
    const auto* action =
        std::get_if<MatchActionRecord>(&recorder.entries()[index].payload);
    if (!action || action->kind != MatchActionKind::TACKLE) continue;
    ++tackles;
    riskSum += action->foulPropensity;
    if (action->result == MatchDuelResult::FOUL)
      ++fouls;
    else
      highestUnpunished = std::max(highestUnpunished, action->foulPropensity);
  }
  ImGui::Text("Tackles %d  ·  fouls given %d  ·  mean foul risk %.2f  ·  "
              "riskiest tackle not given %.2f",
              tackles, fouls, tackles > 0 ? riskSum / static_cast<float>(tackles) : 0.0f,
              highestUnpunished);

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
  if (!ImGui::BeginTable("##calls", 3, flags, ImVec2(0.0f, 0.0f))) return;
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed,
                          60.0f * Theme::scale());
  ImGui::TableSetupColumn("Call", ImGuiTableColumnFlags_WidthFixed,
                          110.0f * Theme::scale());
  ImGui::TableSetupColumn("Detail");
  ImGui::PushFont(SandboxAnalysisUI::bold(),14);
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
        case MatchEventType::FOUL: call = "Foul"; break;
        case MatchEventType::ADVANTAGE: call = "Advantage"; break;
        case MatchEventType::YELLOW_CARD: call = "Yellow card"; break;
        case MatchEventType::SECOND_YELLOW: call = "Second yellow"; break;
        case MatchEventType::RED_CARD: call = "Red card"; break;
        case MatchEventType::OFFSIDE: call = "Offside"; break;
        case MatchEventType::PENALTY: call = "Penalty"; break;
        case MatchEventType::ADDED_TIME: call = "Added time"; break;
        default: break;
      }
    }
    else if (const auto* action = std::get_if<MatchActionRecord>(&entry.payload);
             action && action->kind == MatchActionKind::TACKLE &&
             action->result == MatchDuelResult::FOUL)
    {
      call = "Foul tackle";
    }
    if (!call) continue;
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
  if (summary_prefix == asOf.prefix) return;
  summary_prefix = asOf.prefix;
  totals.clear();
  phaseTicks = {};
  std::unordered_map<PlayerID, std::size_t> slots;
  const auto totalsOf = [&](PlayerID player, bool homeTeam) -> PlayerTotals&
  {
    const auto [found, added] = slots.emplace(player, totals.size());
    if (added) totals.push_back({player, homeTeam});
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
      ++player.chosen[static_cast<std::size_t>(decision->chosen)];
      if (decision->chosen != decision->chosenWithoutNoise) ++player.noiseFlips;
    }
    else if (const auto* action = std::get_if<MatchActionRecord>(&entry.payload))
    {
      ++totalsOf(action->player, action->homeTeam).actions;
    }
    else if (const auto* change =
                 std::get_if<Recorder::IntentChange>(&entry.payload))
    {
      PlayerTotals& player = totalsOf(change->player, change->homeTeam);
      ++player.intentChanges;
      Current& current = intents[change->player];
      player.intentTicks[static_cast<std::size_t>(current.intent)] +=
          entry.tick - current.since;
      current = {change->to, entry.tick};
    }
    else if (const auto* phase =
                 std::get_if<Recorder::PhaseChange>(&entry.payload))
    {
      const std::size_t team = side(phase->homeTeam);
      phaseTicks[team][static_cast<std::size_t>(phases[team])] +=
          entry.tick - phaseStart[team];
      phases[team] = phase->to;
      phaseStart[team] = entry.tick;
    }
  }
  // The intents and phases still running at the moment shown.
  for (auto& [player, current] : intents)
    if (const auto found = slots.find(player); found != slots.end())
      totals[found->second].intentTicks[static_cast<std::size_t>(current.intent)] +=
          asOf.tick - current.since;
  for (std::size_t team = 0; team < 2; ++team)
    phaseTicks[team][static_cast<std::size_t>(phases[team])] +=
        asOf.tick - phaseStart[team];
  std::ranges::sort(totals, [](const PlayerTotals& left, const PlayerTotals& right)
                    { return left.homeTeam != right.homeTeam ? left.homeTeam
                                                             : left.player < right.player; });
}

void DebugInspector::renderSummary(const SandboxRecorder& recorder,
                                   const MatchEngine& engine)
{
  const auto guard = recorder.lock();
  refresh(recorder, engine.getSimulatedSteps());
  refreshSummary(recorder);
  ImGui::TextDisabled("From kick-off to %s (the moment shown).",
                      clock(engine.getMatchTimeMinutes()).c_str());

  // Time in each phase, per side.
  ImGui::SeparatorText("Team phases (share of time)");
  for (std::size_t team = 0; team < 2; ++team)
  {
    std::uint64_t total = 0;
    for (const std::uint64_t ticks : phaseTicks[team]) total += ticks;
    std::string line = team == 0 ? "Home: " : "Away: ";
    for (std::size_t phase = 0; phase < phaseTicks[team].size(); ++phase)
      if (total > 0 && phaseTicks[team][phase] > 0)
        line += std::format(
            "{} {:.0f}%  ", SandboxNames::phase(static_cast<TeamPhase>(phase)),
            100.0 * static_cast<double>(phaseTicks[team][phase]) /
                static_cast<double>(total));
    ImGui::TextUnformatted(line.c_str());
  }

  ImGui::SeparatorText("Players");
  constexpr ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
      ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp;
  if (!ImGui::BeginTable("##summary", 8, flags, ImVec2(0.0f, 0.0f))) return;
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthStretch, 2.0f);
  ImGui::TableSetupColumn("Side");
  ImGui::TableSetupColumn("Decisions");
  ImGui::TableSetupColumn("Pass/shot/carry/shield %", ImGuiTableColumnFlags_WidthStretch, 1.6f);
  ImGui::TableSetupColumn("Noise flips");
  ImGui::TableSetupColumn("Actions");
  ImGui::TableSetupColumn("Intent changes/min");
  ImGui::TableSetupColumn("Main intents", ImGuiTableColumnFlags_WidthStretch, 2.4f);
  ImGui::PushFont(SandboxAnalysisUI::bold(),14);
    ImGui::TableHeadersRow();
    ImGui::PopFont();
  const float minutes =
      std::max(1.0f / 60.0f, seconds(asOf.tick) / 60.0f);
  for (const PlayerTotals& player : totals)
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    selectable(recorder, player.player);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(player.homeTeam ? "H" : "A");
    ImGui::TableNextColumn();
    ImGui::Text("%d", player.decisions);
    ImGui::TableNextColumn();
    if (player.decisions > 0)
    {
      const auto share = [&player](ScenarioAction action)
      {
        return 100.0f *
               static_cast<float>(player.chosen[static_cast<std::size_t>(action)]) /
               static_cast<float>(player.decisions);
      };
      ImGui::Text("%.0f / %.0f / %.0f / %.0f", share(ScenarioAction::PASS),
                  share(ScenarioAction::SHOT), share(ScenarioAction::CARRY),
                  share(ScenarioAction::SHIELD) + share(ScenarioAction::CLEAR));
    }
    ImGui::TableNextColumn();
    ImGui::Text("%d", player.noiseFlips);
    ImGui::TableNextColumn();
    ImGui::Text("%d", player.actions);
    ImGui::TableNextColumn();
    ImGui::Text("%.1f", static_cast<float>(player.intentChanges) / minutes);
    ImGui::TableNextColumn();
    // His three most-held intents, by share of time.
    std::uint64_t total = 0;
    std::array<std::size_t, INTENTS> order{};
    for (std::size_t intent = 0; intent < INTENTS; ++intent)
    {
      order[intent] = intent;
      total += player.intentTicks[intent];
    }
    std::ranges::sort(order, [&player](std::size_t left, std::size_t right)
                      { return player.intentTicks[left] > player.intentTicks[right]; });
    std::string main;
    for (std::size_t rank = 0; rank < 3 && total > 0; ++rank)
    {
      const std::uint64_t ticks = player.intentTicks[order[rank]];
      if (ticks == 0) break;
      main += std::format("{} {:.0f}%  ",
                          SandboxNames::intent(static_cast<PlayerIntent>(order[rank])),
                          100.0 * static_cast<double>(ticks) /
                              static_cast<double>(total));
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
    ImGui::SetTooltip("Line from each player to where he is heading, coloured "
                      "by intent: runs orange, pressing red, support green, "
                      "marking blue, recovering yellow, holding grey.");
  SandboxAnalysisUI::next();
  ImGui::Checkbox("Jobs", &show_jobs);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Planner jobs: B1/B2 to the ball, R1-R3 runners, LA late "
                      "arrival, FP far post, OV overlap, S1-S3 supporters, CO "
                      "cover outlet.");
  SandboxAnalysisUI::next();
  ImGui::Checkbox("Pass options", &show_pass_options);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The latest on-ball decision (within 3 s): best pass "
                      "green, runner-up amber, and the option scores.");
  SandboxAnalysisUI::next();
  ImGui::Checkbox("Selected only", &selected_only);
}

void DebugInspector::drawOverlays(const SandboxRecorder& recorder,
                                  const MatchEngine& engine,
                                  const IMatchRenderer& renderer, bool clicked)
{
  const auto guard = recorder.lock();
  const std::uint64_t tick = engine.getSimulatedSteps();
  refresh(recorder, tick);
  const float scale = Theme::scale();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const auto project = [&renderer](PitchPoint pitch, ImVec2& screen)
  { return renderer.projectPitch(pitch, 0.0f, screen.x, screen.y); };

  const ImVec2 mouse = ImGui::GetIO().MousePos;
  PlayerID nearest = 0;
  float nearestDistance = 22.0f * scale;
  for (const MatchPlayer& slot : engine.getPlayers())
  {
    if (!slot.player || !slot.onPitch) continue;
    const PlayerID id = slot.player->getId();
    ImVec2 at;
    if (!project(slot.position, at)) continue;
    const float distance = std::hypot(mouse.x - at.x, mouse.y - at.y);
    if (distance < nearestDistance)
    {
      nearestDistance = distance;
      nearest = id;
    }
    const bool isSelected = id == selected;
    if (selected_only && !isSelected) continue;
    if (show_targets)
    {
      ImVec2 target;
      if (project(slot.tacticalTarget, target))
      {
        const ImU32 color = intentColor(slot.intent);
        drawList->AddLine(at, target, color,
                          (isSelected ? 2.5f : 1.2f) * scale);
        drawList->AddCircle(target, 3.0f * scale, color, 0, 1.2f * scale);
      }
    }
    if (show_jobs)
    {
      const char* tag = jobTag(jobOf(id, slot.isHomeTeam));
      if (*tag != '\0')
      {
        const ImVec2 textAt(at.x + 7.0f * scale, at.y - 16.0f * scale);
        const ImVec2 size = ImGui::CalcTextSize(tag);
        drawList->AddRectFilled(
            ImVec2(textAt.x - 2.0f, textAt.y - 1.0f),
            ImVec2(textAt.x + size.x + 2.0f, textAt.y + size.y + 1.0f),
            IM_COL32(0, 0, 0, 170), 3.0f);
        drawList->AddText(textAt, IM_COL32(255, 230, 120, 255), tag);
      }
    }
    if (isSelected)
      drawList->AddCircle(at, 11.0f * scale, IM_COL32(255, 220, 60, 255), 0,
                          2.5f * scale);
  }

  // The latest decision's pass options, while it is recent.
  if (show_pass_options && asOf.lastDecision)
  {
    const auto& entry = recorder.entries()[*asOf.lastDecision];
    const auto& decision = std::get<MatchDecisionRecord>(entry.payload);
    const MatchPlayer* passer = findSlot(engine, decision.player);
    if (passer && tick - entry.tick <= PASS_OPTION_TICKS &&
        (!selected_only || decision.player == selected))
    {
      ImVec2 from;
      if (project(passer->position, from))
      {
        const auto arrow = [&](PlayerID receiver, ImU32 color, float width)
        {
          const MatchPlayer* target = findSlot(engine, receiver);
          ImVec2 to;
          if (!target || !project(target->position, to)) return;
          drawList->AddLine(from, to, color, width * scale);
          drawList->AddCircleFilled(to, 4.0f * scale, color);
        };
        arrow(decision.runnerUpReceiver, IM_COL32(240, 180, 40, 220), 2.0f);
        arrow(decision.bestReceiver, IM_COL32(80, 230, 110, 255), 3.0f);
        const auto score = [](float value)
        { return std::isfinite(value) ? std::format("{:.2f}", value) : std::string("-"); };
        const std::string label = std::format(
            "{}  pass {} shot {} carry {} shield {}{}",
            SandboxNames::choice(decision.chosen), score(decision.pass),
            score(decision.shot), score(decision.carry), score(decision.shield),
            decision.chosen != decision.chosenWithoutNoise ? "  (noise)" : "");
        const ImVec2 textAt(from.x + 10.0f * scale, from.y + 8.0f * scale);
        const ImVec2 size = ImGui::CalcTextSize(label.c_str());
        drawList->AddRectFilled(
            ImVec2(textAt.x - 3.0f, textAt.y - 2.0f),
            ImVec2(textAt.x + size.x + 3.0f, textAt.y + size.y + 2.0f),
            IM_COL32(0, 0, 0, 190), 3.0f);
        drawList->AddText(textAt, IM_COL32(255, 255, 255, 255), label.c_str());
      }
    }
  }

  if (clicked && nearest != 0) selected = nearest;
}

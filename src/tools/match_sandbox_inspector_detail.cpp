// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The inspector's full-detail views (debugger phase 4): the Decision tab's
// tree, the planner's rankings and the duels with their rolls.

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <ranges>
#include <string>
#include <variant>
#include <vector>

#include "gui/widgets/theme.h"
#include "model/match_engine.h"
#include "tools/match_sandbox_analysis_ui.h"
#include "tools/match_sandbox_debugger.h"
#include "tools/match_sandbox_inspector.h"
#include "tools/match_sandbox_names.h"
#include "tools/match_sandbox_rewind.h"
#include "tools/match_sandbox_scenario.h"
#include "tools/match_sandbox_text.h"

namespace
{
constexpr ImVec4 SITUATION_COLOR{0.70F, 0.72F, 0.76F, 1.0F};
constexpr ImVec4 LOCATION_COLOR{0.45F, 0.80F, 0.85F, 1.0F};
constexpr ImVec4 ATTRIBUTE_COLOR{0.45F, 0.65F, 1.00F, 1.0F};
constexpr ImVec4 SLOT_ROLE_COLOR{0.80F, 0.55F, 1.00F, 1.0F};
constexpr ImVec4 NATURAL_POSITION_COLOR{1.00F, 0.55F, 0.80F, 1.0F};
constexpr ImVec4 INSTRUCTION_COLOR{1.00F, 0.75F, 0.35F, 1.0F};
constexpr ImVec4 GAME_STATE_COLOR{0.95F, 0.95F, 0.50F, 1.0F};
constexpr ImVec4 MORALE_COLOR{0.50F, 0.90F, 0.55F, 1.0F};
constexpr ImVec4 NOISE_COLOR{1.00F, 0.45F, 0.45F, 1.0F};
constexpr float PERCENT = 100.0F;
constexpr int SECONDS_PER_MINUTE = 60;
constexpr float BREAKDOWN_RESIDUAL_TOLERANCE = 1e-3F;
constexpr int MAX_DISPLAYED_DUELS = 40;
constexpr float DECISION_LIST_HEIGHT = 130.0F;

const char* sourceName(TermSource source)
{
  switch (source)
  {
    case TermSource::SITUATION:
      return "Situation";
    case TermSource::LOCATION:
      return "Pitch location";
    case TermSource::ATTRIBUTE:
      return "Attributes";
    case TermSource::SLOT_ROLE:
      return "Slot role";
    case TermSource::NATURAL_POSITION:
      return "Natural position";
    case TermSource::INSTRUCTION:
      return "Instructions";
    case TermSource::GAME_STATE:
      return "Game state";
    case TermSource::MORALE:
      return "Morale";
    case TermSource::NOISE:
      return "Noise";
    case TermSource::COUNT:
      break;
  }
  return "?";
}

ImVec4 sourceColor(TermSource source)
{
  switch (source)
  {
    case TermSource::SITUATION:
      return SITUATION_COLOR;
    case TermSource::LOCATION:
      return LOCATION_COLOR;
    case TermSource::ATTRIBUTE:
      return ATTRIBUTE_COLOR;
    case TermSource::SLOT_ROLE:
      return SLOT_ROLE_COLOR;
    case TermSource::NATURAL_POSITION:
      return NATURAL_POSITION_COLOR;
    case TermSource::INSTRUCTION:
      return INSTRUCTION_COLOR;
    case TermSource::GAME_STATE:
      return GAME_STATE_COLOR;
    case TermSource::MORALE:
      return MORALE_COLOR;
    case TermSource::NOISE:
      return NOISE_COLOR;
    case TermSource::COUNT:
      break;
  }
  return {1.0F, 1.0F, 1.0F, 1.0F};
}

const char* exclusionName(PassExclusion reason)
{
  switch (reason)
  {
    case PassExclusion::NONE:
      return "";
    case PassExclusion::TOO_CLOSE:
      return "too close";
    case PassExclusion::TOO_FAR:
      return "too far";
    case PassExclusion::KEEPER_TOO_FAR:
      return "long ball back to the keeper";
    case PassExclusion::LOOKS_OFFSIDE:
      return "looks offside to him";
    case PassExclusion::BLOCKED_BY_DUMMY:
      return "a dummy in the way";
  }
  return "?";
}

std::string clock(float minute)
{
  const int total = static_cast<int>(std::floor(
      std::max(0.0F, minute) * static_cast<float>(SECONDS_PER_MINUTE)));
  return std::format("{:02}:{:02}", total / SECONDS_PER_MINUTE,
                     total % SECONDS_PER_MINUTE);
}

std::string signedValue(float value) { return std::format("{:+.3f}", value); }

/**
 * A score's terms grouped by source, each group expandable to its terms;
 * a warning when the terms no longer add up to the engine's value.
 */
void renderBreakdown(const ScoreBreakdown& score, const char* id)
{
  ImGui::PushID(id);
  constexpr auto SOURCES = static_cast<std::size_t>(TermSource::COUNT);
  std::array<float, SOURCES> sums{};
  std::array<int, SOURCES> counts{};
  for (const ScoreTerm& term : score.terms)
  {
    sums.at(static_cast<std::size_t>(term.source)) += term.value;
    ++counts.at(static_cast<std::size_t>(term.source));
  }
  for (std::size_t source = 0; source < SOURCES; ++source)
  {
    if (counts.at(source) == 0)
    {
      continue;
    }
    const auto kind = static_cast<TermSource>(source);
    ImGui::PushID(static_cast<int>(source));
    ImGui::PushStyleColor(ImGuiCol_Text, sourceColor(kind));
    const bool open = SandboxText::treeNode(
        "source",
        std::format("{:<17} {}", sourceName(kind), signedValue(sums.at(source)))
            .c_str(),
        ImGuiTreeNodeFlags_SpanAvailWidth);
    ImGui::PopStyleColor();
    if (open)
    {
      for (const ScoreTerm& term : score.terms)
      {
        if (term.source == kind)
        {
          SandboxText::bulletText(
              std::format("{}  {}", signedValue(term.value), term.name)
                  .c_str());
        }
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (const float residual = score.residual();
      std::abs(residual) > BREAKDOWN_RESIDUAL_TOLERANCE)
  {
    SandboxText::coloredText(
        Theme::palette().warning,
        std::format("Unexplained {}: the breakdown no longer matches the "
                    "engine's formula.",
                    signedValue(residual))
            .c_str());
  }
  ImGui::PopID();
}

constexpr std::array<const char*, 4> OPTION_NAMES{"Pass", "Shot", "Carry",
                                                  "Shield"};

/** The option's score in force: before noise plus its noise. */
float withNoise(const MatchDecisionDetail& detail, std::size_t option)
{
  return detail.options.at(option).total + detail.noise.at(option);
}

/** How far the winner was ahead of the next best option. */
float winningMargin(const MatchDecisionDetail& detail)
{
  float best = -std::numeric_limits<float>::infinity();
  float second = -std::numeric_limits<float>::infinity();
  for (std::size_t option = 0; option < detail.options.size(); ++option)
  {
    const float value = withNoise(detail, option);
    if (!std::isfinite(value))
    {
      continue;
    }
    if (value > best)
    {
      second = best;
      best = value;
    }
    else if (value > second)
    {
      second = value;
    }
  }
  return std::isfinite(second) ? best - second : 0.0F;
}

/** Index of an option among OPTION_NAMES (a clearance is the shield). */
std::size_t optionIndex(ScenarioAction action)
{
  switch (action)
  {
    case ScenarioAction::SHOT:
      return 1;
    case ScenarioAction::CARRY:
      return 2;
    case ScenarioAction::SHIELD:
    case ScenarioAction::CLEAR:
      return 3;
    default:
      return 0;
  }
}

/** The option the scores alone favoured (the engine's comparison order). */
std::size_t winnerWithoutNoise(const MatchDecisionDetail& detail)
{
  const auto& o = detail.options;
  if (o[1].total >= o[0].total && o[1].total >= o[2].total &&
      o[1].total >= o[3].total)
  {
    return 1;
  }
  if (o[2].total >= o[0].total && o[2].total >= o[3].total)
  {
    return 2;
  }
  if (o[3].total >= o[0].total)
  {
    return 3;
  }
  return 0;
}
void renderDecisionOptions(const MatchDecisionDetail& detail)
{
  // Options: the score before noise, the noise and the result.
  ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
  if (ImGui::TreeNode("Options"))
  {
    const std::size_t plain = winnerWithoutNoise(detail);
    for (std::size_t option = 0; option < detail.options.size(); ++option)
    {
      const ScoreBreakdown& score = detail.options.at(option);
      ImGui::PushID(static_cast<int>(option));
      if (!std::isfinite(score.total))
      {
        SandboxText::bulletText(
            std::format("{}  not available", OPTION_NAMES.at(option)).c_str());
        ImGui::PopID();
        continue;
      }
      // The chosen option opens with its terms by source.
      if (option == optionIndex(detail.chosen))
      {
        ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
      }
      const bool open = SandboxText::treeNode(
          "option",
          std::format("{}  {:.3f}  {} noise  =  {:.3f}{}{}",
                      OPTION_NAMES.at(option), static_cast<double>(score.total),
                      signedValue(detail.noise.at(option)),
                      static_cast<double>(withNoise(detail, option)),
                      option == optionIndex(detail.chosen) ? "   CHOSEN" : "",
                      option == plain && plain != optionIndex(detail.chosen)
                          ? "   (best before noise)"
                          : "")
              .c_str(),
          ImGuiTreeNodeFlags_SpanAvailWidth);
      if (open)
      {
        renderBreakdown(score, OPTION_NAMES.at(option));
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
    ImGui::TreePop();
  }

  if (SandboxText::treeNode(
          "noise",
          std::format("Noise scale  {:.3f}  (the largest noise either way)",
                      static_cast<double>(detail.noiseScale.total))
              .c_str()))
  {
    renderBreakdown(detail.noiseScale, "noise");
    ImGui::TreePop();
  }
}

void renderPassCandidates(const SandboxRecorder& recorder,
                          const MatchDecisionDetail& detail)
{
  const auto name = [&recorder](PlayerID id)
  { return recorder.playerName(id); };
  // Every team-mate he looked at, best first; those left out at the end.
  std::vector<const PassCandidateDetail*> candidates;
  candidates.reserve(detail.candidates.size());
  for (const PassCandidateDetail& candidate : detail.candidates)
  {
    candidates.push_back(&candidate);
  }
  std::ranges::stable_sort(
      candidates,
      [](const PassCandidateDetail* left, const PassCandidateDetail* right)
      {
        if ((left->excluded == PassExclusion::NONE) !=
            (right->excluded == PassExclusion::NONE))
        {
          return left->excluded == PassExclusion::NONE;
        }
        return left->utility.total > right->utility.total;
      });
  if (SandboxText::treeNode(
          "candidates",
          std::format("Pass candidates ({})", candidates.size()).c_str()))
  {
    for (const PassCandidateDetail* candidate : candidates)
    {
      ImGui::PushID(static_cast<int>(candidate->receiver));
      if (candidate->excluded != PassExclusion::NONE)
      {
        SandboxText::bulletText(
            std::format("{}  left out: {} ({:.0f} m)",
                        name(candidate->receiver),
                        exclusionName(candidate->excluded),
                        static_cast<double>(candidate->distanceMetres))
                .c_str());
        ImGui::PopID();
        continue;
      }
      if (SandboxText::treeNode(
              "candidate",
              std::format(
                  "{}  utility {:.3f}  ·  completion {:.0f}%  ·  {}{}  ·  "
                  "{:.0f} m",
                  name(candidate->receiver),
                  static_cast<double>(candidate->utility.total),
                  static_cast<double>(candidate->completion.total * PERCENT),
                  SandboxNames::passIntent(candidate->intent),
                  candidate->lofted ? ", lofted" : "",
                  static_cast<double>(candidate->distanceMetres))
                  .c_str(),
              ImGuiTreeNodeFlags_SpanAvailWidth))
      {
        if (SandboxText::treeNode(
                "utility",
                std::format("Utility  {:.3f}",
                            static_cast<double>(candidate->utility.total))
                    .c_str()))
        {
          renderBreakdown(candidate->utility, "utility");
          ImGui::TreePop();
        }
        if (SandboxText::treeNode(
                "completion",
                std::format(
                    "Completion estimate  {:.0f}%",
                    static_cast<double>(candidate->completion.total * PERCENT))
                    .c_str()))
        {
          renderBreakdown(candidate->completion, "completion");
          ImGui::TreePop();
        }
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
    ImGui::TreePop();
  }
}

void renderDecisionOutcome(const SandboxRecorder& recorder,
                           const DetailRecorder::At<MatchDecisionDetail>& at)
{
  const MatchDecisionDetail& detail = at.detail;
  // What he did and what came of it: the log at and after this tick.
  ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
  if (ImGui::TreeNode("Action and outcome"))
  {
    const auto guard = recorder.lock();
    const auto& entries = recorder.entries();
    const auto first = std::ranges::lower_bound(
        entries, at.tick, {},
        [](const SandboxRecorder::Entry& entry) { return entry.tick; });
    int shown = 0;
    for (auto entry = first; entry != entries.end() && shown < 4; ++entry)
    {
      const bool action =
          std::holds_alternative<MatchActionRecord>(entry->payload);
      const bool possession =
          std::holds_alternative<SandboxRecorder::PossessionChange>(
              entry->payload);
      if (!action && !possession)
      {
        continue;
      }
      const EngineDebugger::Described described =
          EngineDebugger::describe(recorder, *entry);
      if (action && described.player != detail.player)
      {
        continue;
      }
      SandboxText::bulletText(
          std::format("{}  {}", clock(entry->minute), described.detail)
              .c_str());
      ++shown;
      if (possession)
      {
        break;
      }
    }
    if (shown == 0)
    {
      SandboxText::disabledText("Nothing recorded yet.");
    }
    ImGui::TreePop();
  }
}

void renderRankingCandidates(
    const SandboxRecorder& recorder,
    const DetailRecorder::At<MatchRankingDetail>& ranking)
{
  const auto* latest = &ranking;
  const bool toBall = ranking.detail.kind == MatchRankingDetail::Kind::TO_BALL;
  std::vector<const MatchRankingDetail::Candidate*> order;
  order.reserve(latest->detail.candidates.size());
  for (const auto& candidate : latest->detail.candidates)
  {
    order.push_back(&candidate);
  }
  std::ranges::sort(order,
                    [toBall](const auto* left, const auto* right)
                    {
                      return toBall ? left->score.total < right->score.total
                                    : left->score.total > right->score.total;
                    });
  ImGui::PushID(toBall ? "toball" : "runs");
  if (SandboxText::treeNode(
          "ranking",
          std::format("{} (tick {})",
                      toBall ? "To the ball: estimated arrival, soonest first"
                             : "Run priority, highest first",
                      latest->tick)
              .c_str()))
  {
    for (const auto* candidate : order)
    {
      ImGui::PushID(static_cast<int>(candidate->player));
      if (SandboxText::treeNode(
              "candidate",
              std::format("{}  {:.3f}{}",
                          recorder.playerName(candidate->player),
                          static_cast<double>(candidate->score.total),
                          toBall ? " s" : "")
                  .c_str(),
              ImGuiTreeNodeFlags_SpanAvailWidth))
      {
        renderBreakdown(candidate->score, "score");
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
    if (toBall)
    {
      SandboxText::disabledText(
          "A pass's intended receiver is put first whatever "
          "his time.");
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}

void renderFirstTouchDetail(const SandboxRecorder& recorder,
                            const DetailRecorder::At<MatchDuelDetail>* at,
                            int shown)
{
  const MatchDuelDetail& duel = at->detail;

  // Two jobs, each rolled: did it go wrong, and by how much.
  constexpr float DEGREES = 180.0F / std::numbers::pi_v<float>;
  const std::string label = std::format(
      "{}  First touch  {}  ·  turn {:.0f}° {} {}({:.0f}%) {:+.0f}°  ·  "
      "pace {:+.1f} m/s {}({:.0f}%) {:+.1f} m/s  ·  {:.1f} -> {:.1f} m/s",
      clock(at->minute), recorder.playerName(duel.player), duel.turn * DEGREES,
      duel.outward ? "out" : "in", duel.angleFailed ? "FAILED " : "",
      duel.angleFailChance * PERCENT, duel.angleError * DEGREES,
      duel.paceChange, duel.weightFailed ? "FAILED " : "",
      duel.weightFailChance * PERCENT, duel.weightError, duel.intendedSpeed,
      duel.actualSpeed);
  ImGui::PushID(shown);
  if (SandboxText::treeNode("touch", label.c_str(),
                            ImGuiTreeNodeFlags_SpanAvailWidth))
  {
    SandboxText::disabledText(
        std::format(
            "Touch skill {:.0f}. Each job's chance of going wrong = (base "
            "+ "
            "difficulty x range) x (1 - skill x {:.1f}) x tiredness and "
            "nerves; "
            "a failed job misses by a share of it, sized by the "
            "difficulty.",
            static_cast<double>(duel.touchSkill * PERCENT),
            static_cast<double>(MatchTuning::FirstTouch::SKILL_REDUCTION))
            .c_str());
    SandboxText::disabledText("Angle difficulty:");
    renderBreakdown(duel.chance, "angle");
    SandboxText::disabledText("Weight difficulty:");
    renderBreakdown(duel.foul, "weight");
    ImGui::TreePop();
  }
  ImGui::PopID();
}

void renderStrikeDetail(const SandboxRecorder& recorder,
                        const DetailRecorder::At<MatchDuelDetail>* at,
                        int shown)
{
  const MatchDuelDetail& duel = at->detail;

  // Carried out (step 3) like a touch, or measured only (step 2).
  constexpr float DEGREES = 180.0F / std::numbers::pi_v<float>;
  std::string label = std::format(
      "{}  {}  {}  ·  turn {:.0f}° {}  ·  pace {:+.1f} m/s  ·  "
      "difficulty angle {:.2f}, weight {:.2f}",
      clock(at->minute),
      STRIKE_KIND_NAMES.at(static_cast<std::size_t>(duel.strike)),
      recorder.playerName(duel.player), duel.turn * DEGREES,
      duel.outward ? "out" : "in", duel.paceChange, duel.chance.total,
      duel.foul.total);
  if (duel.strikeCarriedOut)
  {
    label += std::format(
        "  ·  angle {}({:.0f}%) {:+.1f}°  ·  weight {}({:.0f}%) "
        "{:+.1f} m/s  ·  {:.1f} -> {:.1f} m/s",
        duel.angleFailed ? "FAILED " : "", duel.angleFailChance * PERCENT,
        duel.angleError * DEGREES, duel.weightFailed ? "FAILED " : "",
        duel.weightFailChance * PERCENT, duel.weightError, duel.intendedSpeed,
        duel.actualSpeed);
  }
  else
  {
    label += " (measured)";
  }
  ImGui::PushID(shown);
  if (SandboxText::treeNode("strike", label.c_str(),
                            ImGuiTreeNodeFlags_SpanAvailWidth))
  {
    if (duel.strikeCarriedOut)
    {
      SandboxText::disabledText(
          std::format(
              "Struck by the contact model with skill {:.0f}: each job's "
              "chance of going wrong = (base + difficulty x range) x (1 - "
              "skill x reduction) x tiredness and nerves.",
              static_cast<double>(duel.touchSkill * PERCENT))
              .c_str());
    }
    else
    {
      SandboxText::disabledText(
          "The contact model reads this strike but does not carry it "
          "out yet: provisional weights, no effect on the ball.");
    }
    SandboxText::disabledText("Angle difficulty:");
    renderBreakdown(duel.chance, "angle");
    SandboxText::disabledText("Weight difficulty:");
    renderBreakdown(duel.foul, "weight");
    ImGui::TreePop();
  }
  ImGui::PopID();
}

void renderContestDetail(const SandboxRecorder& recorder,
                         const DetailRecorder::At<MatchDuelDetail>* at,
                         int shown)
{
  const MatchDuelDetail& duel = at->detail;
  const char* kind = "Tackle";
  if (duel.kind == MatchDuelDetail::Kind::ENGAGE)
  {
    kind = "Goes in";
  }
  else if (duel.kind == MatchDuelDetail::Kind::TAKE_ON)
  {
    kind = "Take-on";
  }
  std::string label = std::format(
      "{}  {}  {} on {}  ·  chance {:.0f}%  roll {:.0f}%  ->  {}",
      clock(at->minute), kind, recorder.playerName(duel.player),
      recorder.playerName(duel.opponent), duel.probability * PERCENT,
      duel.roll * PERCENT, duel.roll < duel.probability ? "success" : "failed");
  if (duel.foulRoll)
  {
    label +=
        std::format("  ·  foul threshold {:.0f}%  roll {:.0f}%  ->  {}",
                    duel.foulThreshold * PERCENT, *duel.foulRoll * PERCENT,
                    *duel.foulRoll < duel.foulThreshold ? "FOUL" : "no foul");
  }
  ImGui::PushID(shown);
  if (SandboxText::treeNode("duel", label.c_str(),
                            ImGuiTreeNodeFlags_SpanAvailWidth))
  {
    SandboxText::disabledText(
        duel.kind == MatchDuelDetail::Kind::ENGAGE
            ? "Engagement rate per second (the chance this "
              "step is 1 - exp(-rate x 0.1 s)):"
            : "Chance of success:");
    renderBreakdown(duel.chance, "chance");
    if (duel.kind == MatchDuelDetail::Kind::TACKLE)
    {
      SandboxText::disabledText("Foul propensity:");
      renderBreakdown(duel.foul, "foul");
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
}

}  // namespace

void DebugInspector::ensureDetail(const SandboxRecorder& recorder,
                                  const MatchEngine& engine)
{
  detail_session.ensure(recorder, engine.getSimulatedSteps());
  SandboxText::disabledText(
      std::format("Full detail rebuilt from tick {} to {} (from the saved copy "
                  "before this moment).",
                  detail_session.from(), engine.getSimulatedSteps())
          .c_str());
  if (const auto diverged = detail_session.divergedAt())
  {
    SandboxText::coloredText(
        Theme::palette().negative,
        std::format("The rebuild differs from the match from tick {}: "
                    "this detail is not what was played.",
                    *diverged)
            .c_str());
  }
}

void DebugInspector::renderDecisionTree(
    const SandboxRecorder& recorder,
    const DetailRecorder::At<MatchDecisionDetail>& at)
{
  renderDecisionOptions(at.detail);
  renderPassCandidates(recorder, at.detail);
  renderDecisionOutcome(recorder, at);
}

void DebugInspector::renderDecision(const SandboxRecorder& recorder,
                                    const MatchEngine& engine)
{
  ensureDetail(recorder, engine);
  const auto& decisions = detail_session.detail().decisions;
  ImGui::Checkbox("Only the selected player", &decisions_of_selected);
  ImGui::SameLine();
  SandboxText::disabledText(
      std::format("({} decisions in this stretch)", decisions.size()).c_str());

  const auto* opened = renderDecisionList(recorder);
  if (opened == nullptr)
  {
    SandboxText::disabledText("No decision in this stretch yet.");
    return;
  }
  ImGui::TextUnformatted(
      std::format("{}  ·  {} chose {}  ·  winning margin {:.3f}",
                  clock(opened->minute),
                  recorder.playerName(opened->detail.player),
                  SandboxNames::choice(opened->detail.chosen),
                  static_cast<double>(winningMargin(opened->detail)))
          .c_str());
  // The moment as a test case: positions at the end of the tick before.
  if (SandboxAnalysisUI::button("Export as test scenario") && opened->tick > 0)
  {
    const MatchRewind::Result before =
        MatchRewind::rebuild(recorder, opened->tick - 1);
    if (before.engine)
    {
      const std::string text =
          ScenarioExport::testCase(*before.engine, recorder, opened->detail,
                                   opened->tick, opened->minute);
      ImGui::SetClipboardText(text.c_str());
      std::string error;
      const auto path = ScenarioExport::save(text, opened->tick, error);
      export_status =
          path.empty()
              ? "Copied to the clipboard; could not save: " + error
              : "Copied to the clipboard and saved to " + path.string();
    }
  }
  if (ImGui::IsItemHovered())
  {
    SandboxText::tooltipText(
        "A TEST for test/test_match_scenarios.cpp placing its "
        "dummy teams where these players stood just before "
        "the decision.");
  }
  if (!export_status.empty())
  {
    ImGui::SameLine();
    SandboxText::disabledText(export_status.c_str());
  }
  renderDecisionTree(recorder, *opened);
}

void DebugInspector::renderRankingDetail(const SandboxRecorder& recorder,
                                         bool home, std::uint64_t tick)
{
  // The latest of each ranking for the side, at or before the moment.
  for (const auto kind : {MatchRankingDetail::Kind::TO_BALL,
                          MatchRankingDetail::Kind::RUN_PRIORITY})
  {
    const DetailRecorder::At<MatchRankingDetail>* latest = nullptr;
    for (const auto& at : detail_session.detail().rankings)
    {
      if (at.tick <= tick && at.detail.homeTeam == home &&
          at.detail.kind == kind)
      {
        latest = &at;
      }
    }
    const bool toBall = kind == MatchRankingDetail::Kind::TO_BALL;
    if (latest == nullptr)
    {
      if (!toBall)
      {
        SandboxText::disabledText(
            "No run ranking now (the side is not attacking).");
      }
      continue;
    }
    renderRankingCandidates(recorder, *latest);
  }
}

void DebugInspector::renderDuelDetail(const SandboxRecorder& recorder,
                                      std::uint64_t tick)
{
  const auto& duels = detail_session.detail().duels;
  int shown = 0;
  for (auto at = duels.rbegin();
       at != duels.rend() && shown < MAX_DISPLAYED_DUELS; ++at)
  {
    if (at->tick > tick)
    {
      continue;
    }
    const MatchDuelDetail& duel = at->detail;
    // Engagements that did not happen are frequent: only the ones that did.
    if (duel.kind == MatchDuelDetail::Kind::ENGAGE &&
        duel.roll >= duel.probability)
    {
      continue;
    }
    ++shown;
    if (duel.kind == MatchDuelDetail::Kind::FIRST_TOUCH)
    {
      renderFirstTouchDetail(recorder, &*at, shown);
      continue;
    }
    if (duel.kind == MatchDuelDetail::Kind::STRIKE)
    {
      renderStrikeDetail(recorder, &*at, shown);
      continue;
    }
    renderContestDetail(recorder, &*at, shown);
  }
  if (shown == 0)
  {
    SandboxText::disabledText("No duels in this stretch yet.");
  }
}

const DetailRecorder::At<MatchDecisionDetail>*
DebugInspector::renderDecisionList(const SandboxRecorder& recorder)
{
  const auto& decisions = detail_session.detail().decisions;
  // The list, latest first; the latest is opened until one is chosen.
  const float scale = Theme::scale();
  const DetailRecorder::At<MatchDecisionDetail>* opened = nullptr;
  const DetailRecorder::At<MatchDecisionDetail>* latest = nullptr;
  if (ImGui::BeginChild("##decisions",
                        ImVec2(0.0F, DECISION_LIST_HEIGHT * scale),
                        ImGuiChildFlags_Borders))
  {
    for (const auto& at : decisions | std::views::reverse)
    {
      const MatchDecisionDetail& detail = at.detail;
      if (decisions_of_selected && selected != 0 && detail.player != selected)
      {
        continue;
      }
      if (latest == nullptr)
      {
        latest = &at;
      }
      const bool isOpen = opened_decision &&
                          opened_decision->first == at.tick &&
                          opened_decision->second == detail.player;
      if (isOpen)
      {
        opened = &at;
      }
      const bool flipped =
          winnerWithoutNoise(detail) != optionIndex(detail.chosen);
      const std::string label = std::format(
          "{}  {}  {}  ·  margin {:.2f}{}##{}_{}", clock(at.minute),
          recorder.playerName(detail.player),
          SandboxNames::choice(detail.chosen), winningMargin(detail),
          flipped ? "  ·  NOISE FLIPPED" : "", at.tick, detail.player);
      if (ImGui::Selectable(label.c_str(),
                            isOpen || (!opened_decision && latest == &at)))
      {
        opened_decision = {{at.tick, detail.player}};
        selected = detail.player;
      }
    }
  }
  ImGui::EndChild();
  if (opened == nullptr)
  {
    opened = latest;
  }
  return opened;
}

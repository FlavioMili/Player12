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
#include <string>
#include <variant>
#include <vector>

#include "gui/widgets/theme.h"
#include "model/match_engine.h"
#include "tools/match_sandbox_debugger.h"
#include "tools/match_sandbox_inspector.h"
#include "tools/match_sandbox_analysis_ui.h"
#include "tools/match_sandbox_names.h"
#include "tools/match_sandbox_rewind.h"
#include "tools/match_sandbox_scenario.h"

namespace
{
const char* sourceName(TermSource source)
{
  switch (source)
  {
    case TermSource::SITUATION: return "Situation";
    case TermSource::LOCATION: return "Pitch location";
    case TermSource::ATTRIBUTE: return "Attributes";
    case TermSource::SLOT_ROLE: return "Slot role";
    case TermSource::NATURAL_POSITION: return "Natural position";
    case TermSource::INSTRUCTION: return "Instructions";
    case TermSource::GAME_STATE: return "Game state";
    case TermSource::MORALE: return "Morale";
    case TermSource::NOISE: return "Noise";
    case TermSource::COUNT: break;
  }
  return "?";
}

ImVec4 sourceColor(TermSource source)
{
  switch (source)
  {
    case TermSource::SITUATION: return {0.70f, 0.72f, 0.76f, 1.0f};
    case TermSource::LOCATION: return {0.45f, 0.80f, 0.85f, 1.0f};
    case TermSource::ATTRIBUTE: return {0.45f, 0.65f, 1.00f, 1.0f};
    case TermSource::SLOT_ROLE: return {0.80f, 0.55f, 1.00f, 1.0f};
    case TermSource::NATURAL_POSITION: return {1.00f, 0.55f, 0.80f, 1.0f};
    case TermSource::INSTRUCTION: return {1.00f, 0.75f, 0.35f, 1.0f};
    case TermSource::GAME_STATE: return {0.95f, 0.95f, 0.50f, 1.0f};
    case TermSource::MORALE: return {0.50f, 0.90f, 0.55f, 1.0f};
    case TermSource::NOISE: return {1.00f, 0.45f, 0.45f, 1.0f};
    case TermSource::COUNT: break;
  }
  return {1.0f, 1.0f, 1.0f, 1.0f};
}

const char* exclusionName(PassExclusion reason)
{
  switch (reason)
  {
    case PassExclusion::NONE: return "";
    case PassExclusion::TOO_CLOSE: return "too close";
    case PassExclusion::TOO_FAR: return "too far";
    case PassExclusion::KEEPER_TOO_FAR: return "long ball back to the keeper";
    case PassExclusion::LOOKS_OFFSIDE: return "looks offside to him";
    case PassExclusion::BLOCKED_BY_DUMMY: return "a dummy in the way";
  }
  return "?";
}

std::string clock(float minute)
{
  const int total =
      static_cast<int>(std::floor(std::max(0.0f, minute) * 60.0f));
  return std::format("{:02}:{:02}", total / 60, total % 60);
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
    sums[static_cast<std::size_t>(term.source)] += term.value;
    ++counts[static_cast<std::size_t>(term.source)];
  }
  for (std::size_t source = 0; source < SOURCES; ++source)
  {
    if (counts[source] == 0) continue;
    const auto kind = static_cast<TermSource>(source);
    ImGui::PushID(static_cast<int>(source));
    ImGui::PushStyleColor(ImGuiCol_Text, sourceColor(kind));
    const bool open =
        ImGui::TreeNodeEx("source", ImGuiTreeNodeFlags_SpanAvailWidth,
                          "%-17s %s", sourceName(kind),
                          signedValue(sums[source]).c_str());
    ImGui::PopStyleColor();
    if (open)
    {
      for (const ScoreTerm& term : score.terms)
        if (term.source == kind)
          ImGui::BulletText("%s  %s", signedValue(term.value).c_str(),
                            term.name);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (const float residual = score.residual(); std::abs(residual) > 1e-3f)
    ImGui::TextColored(Theme::palette().warning,
                       "Unexplained %s: the breakdown no longer matches the "
                       "engine's formula.",
                       signedValue(residual).c_str());
  ImGui::PopID();
}

constexpr std::array<const char*, 4> OPTION_NAMES{"Pass", "Shot", "Carry",
                                                  "Shield"};

/** The option's score in force: before noise plus its noise. */
float withNoise(const MatchDecisionDetail& detail, std::size_t option)
{
  return detail.options[option].total + detail.noise[option];
}

/** How far the winner was ahead of the next best option. */
float winningMargin(const MatchDecisionDetail& detail)
{
  float best = -std::numeric_limits<float>::infinity();
  float second = -std::numeric_limits<float>::infinity();
  for (std::size_t option = 0; option < detail.options.size(); ++option)
  {
    const float value = withNoise(detail, option);
    if (!std::isfinite(value)) continue;
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
  return std::isfinite(second) ? best - second : 0.0f;
}

/** Index of an option among OPTION_NAMES (a clearance is the shield). */
std::size_t optionIndex(ScenarioAction action)
{
  switch (action)
  {
    case ScenarioAction::SHOT: return 1;
    case ScenarioAction::CARRY: return 2;
    case ScenarioAction::SHIELD:
    case ScenarioAction::CLEAR: return 3;
    default: return 0;
  }
}

/** The option the scores alone favoured (the engine's comparison order). */
std::size_t winnerWithoutNoise(const MatchDecisionDetail& detail)
{
  const auto& o = detail.options;
  if (o[1].total >= o[0].total && o[1].total >= o[2].total &&
      o[1].total >= o[3].total)
    return 1;
  if (o[2].total >= o[0].total && o[2].total >= o[3].total) return 2;
  if (o[3].total >= o[0].total) return 3;
  return 0;
}
}  // namespace

void DebugInspector::ensureDetail(const SandboxRecorder& recorder,
                                  const MatchEngine& engine)
{
  detail_session.ensure(recorder, engine.getSimulatedSteps());
  ImGui::TextDisabled(
      "Full detail rebuilt from tick %llu to %llu (from the saved copy "
      "before this moment).",
      static_cast<unsigned long long>(detail_session.from()),
      static_cast<unsigned long long>(engine.getSimulatedSteps()));
  if (const auto diverged = detail_session.divergedAt())
    ImGui::TextColored(Theme::palette().negative,
                       "The rebuild differs from the match from tick %llu: "
                       "this detail is not what was played.",
                       static_cast<unsigned long long>(*diverged));
}

void DebugInspector::renderDecisionTree(
    const SandboxRecorder& recorder,
    const DetailRecorder::At<MatchDecisionDetail>& at)
{
  const MatchDecisionDetail& detail = at.detail;
  const auto name = [&recorder](PlayerID id) { return recorder.playerName(id); };

  // Options: the score before noise, the noise and the result.
  ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
  if (ImGui::TreeNode("Options"))
  {
    const std::size_t plain = winnerWithoutNoise(detail);
    for (std::size_t option = 0; option < detail.options.size(); ++option)
    {
      const ScoreBreakdown& score = detail.options[option];
      ImGui::PushID(static_cast<int>(option));
      if (!std::isfinite(score.total))
      {
        ImGui::BulletText("%s  not available", OPTION_NAMES[option]);
        ImGui::PopID();
        continue;
      }
      // The chosen option opens with its terms by source.
      if (option == optionIndex(detail.chosen))
        ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
      const bool open = ImGui::TreeNodeEx(
          "option", ImGuiTreeNodeFlags_SpanAvailWidth,
          "%s  %.3f  %s noise  =  %.3f%s%s", OPTION_NAMES[option], score.total,
          signedValue(detail.noise[option]).c_str(), withNoise(detail, option),
          option == optionIndex(detail.chosen) ? "   CHOSEN" : "",
          option == plain && plain != optionIndex(detail.chosen)
              ? "   (best before noise)"
              : "");
      if (open)
      {
        renderBreakdown(score, OPTION_NAMES[option]);
        ImGui::TreePop();
      }
      ImGui::PopID();
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNode("noise", "Noise scale  %.3f  (the largest noise either way)",
                      detail.noiseScale.total))
  {
    renderBreakdown(detail.noiseScale, "noise");
    ImGui::TreePop();
  }

  // Every team-mate he looked at, best first; those left out at the end.
  std::vector<const PassCandidateDetail*> candidates;
  for (const PassCandidateDetail& candidate : detail.candidates)
    candidates.push_back(&candidate);
  std::ranges::stable_sort(
      candidates,
      [](const PassCandidateDetail* left, const PassCandidateDetail* right)
      {
        if ((left->excluded == PassExclusion::NONE) !=
            (right->excluded == PassExclusion::NONE))
          return left->excluded == PassExclusion::NONE;
        return left->utility.total > right->utility.total;
      });
  if (ImGui::TreeNode("candidates", "Pass candidates (%zu)", candidates.size()))
  {
    for (const PassCandidateDetail* candidate : candidates)
    {
      ImGui::PushID(static_cast<int>(candidate->receiver));
      if (candidate->excluded != PassExclusion::NONE)
      {
        ImGui::BulletText("%s  left out: %s (%.0f m)",
                          name(candidate->receiver).c_str(),
                          exclusionName(candidate->excluded),
                          candidate->distanceMetres);
        ImGui::PopID();
        continue;
      }
      if (ImGui::TreeNodeEx(
              "candidate", ImGuiTreeNodeFlags_SpanAvailWidth,
              "%s  utility %.3f  ·  completion %.0f%%  ·  %s%s  ·  %.0f m",
              name(candidate->receiver).c_str(), candidate->utility.total,
              candidate->completion.total * 100.0f,
              SandboxNames::passIntent(candidate->intent),
              candidate->lofted ? ", lofted" : "", candidate->distanceMetres))
      {
        if (ImGui::TreeNode("utility", "Utility  %.3f", candidate->utility.total))
        {
          renderBreakdown(candidate->utility, "utility");
          ImGui::TreePop();
        }
        if (ImGui::TreeNode("completion", "Completion estimate  %.0f%%",
                            candidate->completion.total * 100.0f))
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
      if (!action && !possession) continue;
      const EngineDebugger::Described described =
          EngineDebugger::describe(recorder, *entry);
      if (action && described.player != detail.player) continue;
      ImGui::BulletText("%s  %s", clock(entry->minute).c_str(),
                        described.detail.c_str());
      ++shown;
      if (possession) break;
    }
    if (shown == 0) ImGui::TextDisabled("Nothing recorded yet.");
    ImGui::TreePop();
  }
}

void DebugInspector::renderDecision(const SandboxRecorder& recorder,
                                    const MatchEngine& engine)
{
  ensureDetail(recorder, engine);
  const auto& decisions = detail_session.detail().decisions;
  ImGui::Checkbox("Only the selected player", &decisions_of_selected);
  ImGui::SameLine();
  ImGui::TextDisabled("(%zu decisions in this stretch)", decisions.size());

  // The list, latest first; the latest is opened until one is chosen.
  const float scale = Theme::scale();
  const DetailRecorder::At<MatchDecisionDetail>* opened = nullptr;
  const DetailRecorder::At<MatchDecisionDetail>* latest = nullptr;
  if (ImGui::BeginChild("##decisions", ImVec2(0.0f, 130.0f * scale),
                        ImGuiChildFlags_Borders))
  {
    for (auto at = decisions.rbegin(); at != decisions.rend(); ++at)
    {
      const MatchDecisionDetail& detail = at->detail;
      if (decisions_of_selected && selected != 0 && detail.player != selected)
        continue;
      if (!latest) latest = &*at;
      const bool isOpen = opened_decision &&
                          opened_decision->first == at->tick &&
                          opened_decision->second == detail.player;
      if (isOpen) opened = &*at;
      const bool flipped =
          winnerWithoutNoise(detail) != optionIndex(detail.chosen);
      const std::string label = std::format(
          "{}  {}  {}  ·  margin {:.2f}{}##{}_{}", clock(at->minute),
          recorder.playerName(detail.player),
          SandboxNames::choice(detail.chosen), winningMargin(detail),
          flipped ? "  ·  NOISE FLIPPED" : "", at->tick, detail.player);
      if (ImGui::Selectable(label.c_str(), isOpen || (!opened_decision && latest == &*at)))
      {
        opened_decision = {{at->tick, detail.player}};
        selected = detail.player;
      }
    }
  }
  ImGui::EndChild();
  if (!opened) opened = latest;
  if (!opened)
  {
    ImGui::TextDisabled("No decision in this stretch yet.");
    return;
  }
  ImGui::Text("%s  ·  %s chose %s  ·  winning margin %.3f",
              clock(opened->minute).c_str(),
              recorder.playerName(opened->detail.player).c_str(),
              SandboxNames::choice(opened->detail.chosen),
              winningMargin(opened->detail));
  // The moment as a test case: positions at the end of the tick before.
  if (SandboxAnalysisUI::button("Export as test scenario") && opened->tick > 0)
  {
    const MatchRewind::Result before =
        MatchRewind::rebuild(recorder, opened->tick - 1);
    if (before.engine)
    {
      const std::string text = ScenarioExport::testCase(
          *before.engine, recorder, opened->detail, opened->tick,
          opened->minute);
      ImGui::SetClipboardText(text.c_str());
      std::string error;
      const auto path = ScenarioExport::save(text, opened->tick, error);
      export_status = path.empty()
                          ? "Copied to the clipboard; could not save: " + error
                          : "Copied to the clipboard and saved to " +
                                path.string();
    }
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("A TEST for test/test_match_scenarios.cpp placing its "
                      "dummy teams where these players stood just before "
                      "the decision.");
  if (!export_status.empty())
  {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", export_status.c_str());
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
      if (at.tick <= tick && at.detail.homeTeam == home &&
          at.detail.kind == kind)
        latest = &at;
    const bool toBall = kind == MatchRankingDetail::Kind::TO_BALL;
    if (!latest)
    {
      if (!toBall)
        ImGui::TextDisabled("No run ranking now (the side is not attacking).");
      continue;
    }
    std::vector<const MatchRankingDetail::Candidate*> order;
    for (const auto& candidate : latest->detail.candidates)
      order.push_back(&candidate);
    std::ranges::sort(order,
                      [toBall](const auto* left, const auto* right)
                      {
                        return toBall ? left->score.total < right->score.total
                                      : left->score.total > right->score.total;
                      });
    ImGui::PushID(toBall ? "toball" : "runs");
    if (ImGui::TreeNode("ranking", "%s (tick %llu)",
                        toBall ? "To the ball: estimated arrival, soonest first"
                               : "Run priority, highest first",
                        static_cast<unsigned long long>(latest->tick)))
    {
      for (const auto* candidate : order)
      {
        ImGui::PushID(static_cast<int>(candidate->player));
        if (ImGui::TreeNodeEx("candidate", ImGuiTreeNodeFlags_SpanAvailWidth,
                              "%s  %.3f%s",
                              recorder.playerName(candidate->player).c_str(),
                              candidate->score.total, toBall ? " s" : ""))
        {
          renderBreakdown(candidate->score, "score");
          ImGui::TreePop();
        }
        ImGui::PopID();
      }
      if (toBall)
        ImGui::TextDisabled("A pass's intended receiver is put first whatever "
                            "his time.");
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
}

void DebugInspector::renderDuelDetail(const SandboxRecorder& recorder,
                                      std::uint64_t tick)
{
  const auto& duels = detail_session.detail().duels;
  int shown = 0;
  for (auto at = duels.rbegin(); at != duels.rend() && shown < 40; ++at)
  {
    if (at->tick > tick) continue;
    const MatchDuelDetail& duel = at->detail;
    // Engagements that did not happen are frequent: only the ones that did.
    if (duel.kind == MatchDuelDetail::Kind::ENGAGE &&
        duel.roll >= duel.probability)
      continue;
    ++shown;
    if (duel.kind == MatchDuelDetail::Kind::FIRST_TOUCH)
    {
      // Two jobs, each rolled: did it go wrong, and by how much.
      constexpr float DEGREES = 180.0f / std::numbers::pi_v<float>;
      const std::string label = std::format(
          "{}  First touch  {}  ·  turn {:.0f}° {} {}({:.0f}%) {:+.0f}°  ·  "
          "pace {:+.1f} m/s {}({:.0f}%) {:+.1f} m/s  ·  {:.1f} -> {:.1f} m/s",
          clock(at->minute), recorder.playerName(duel.player),
          duel.turn * DEGREES, duel.outward ? "out" : "in",
          duel.angleFailed ? "FAILED " : "", duel.angleFailChance * 100.0f,
          duel.angleError * DEGREES, duel.paceChange,
          duel.weightFailed ? "FAILED " : "", duel.weightFailChance * 100.0f,
          duel.weightError, duel.intendedSpeed, duel.actualSpeed);
      ImGui::PushID(shown);
      if (ImGui::TreeNodeEx("touch", ImGuiTreeNodeFlags_SpanAvailWidth, "%s",
                            label.c_str()))
      {
        ImGui::TextDisabled(
            "Touch skill %.0f. Each job's chance of going wrong = (base + "
            "difficulty x range) x (1 - skill x %.1f) x tiredness and nerves; "
            "a failed job misses by a share of it, sized by the difficulty.",
            duel.touchSkill * 100.0f,
            MatchTuning::FirstTouch::SKILL_REDUCTION);
        ImGui::TextDisabled("Angle difficulty:");
        renderBreakdown(duel.chance, "angle");
        ImGui::TextDisabled("Weight difficulty:");
        renderBreakdown(duel.foul, "weight");
        ImGui::TreePop();
      }
      ImGui::PopID();
      continue;
    }
    if (duel.kind == MatchDuelDetail::Kind::STRIKE)
    {
      // Carried out (step 3) like a touch, or measured only (step 2).
      constexpr float DEGREES = 180.0f / std::numbers::pi_v<float>;
      std::string label = std::format(
          "{}  {}  {}  ·  turn {:.0f}° {}  ·  pace {:+.1f} m/s  ·  "
          "difficulty angle {:.2f}, weight {:.2f}",
          clock(at->minute),
          STRIKE_KIND_NAMES[static_cast<std::size_t>(duel.strike)],
          recorder.playerName(duel.player), duel.turn * DEGREES,
          duel.outward ? "out" : "in", duel.paceChange, duel.chance.total,
          duel.foul.total);
      if (duel.strikeCarriedOut)
        label += std::format(
            "  ·  angle {}({:.0f}%) {:+.1f}°  ·  weight {}({:.0f}%) "
            "{:+.1f} m/s  ·  {:.1f} -> {:.1f} m/s",
            duel.angleFailed ? "FAILED " : "", duel.angleFailChance * 100.0f,
            duel.angleError * DEGREES, duel.weightFailed ? "FAILED " : "",
            duel.weightFailChance * 100.0f, duel.weightError,
            duel.intendedSpeed, duel.actualSpeed);
      else
        label += " (measured)";
      ImGui::PushID(shown);
      if (ImGui::TreeNodeEx("strike", ImGuiTreeNodeFlags_SpanAvailWidth, "%s",
                            label.c_str()))
      {
        if (duel.strikeCarriedOut)
          ImGui::TextDisabled(
              "Struck by the contact model with skill %.0f: each job's "
              "chance of going wrong = (base + difficulty x range) x (1 - "
              "skill x reduction) x tiredness and nerves.",
              duel.touchSkill * 100.0f);
        else
          ImGui::TextDisabled(
              "The contact model reads this strike but does not carry it "
              "out yet: provisional weights, no effect on the ball.");
        ImGui::TextDisabled("Angle difficulty:");
        renderBreakdown(duel.chance, "angle");
        ImGui::TextDisabled("Weight difficulty:");
        renderBreakdown(duel.foul, "weight");
        ImGui::TreePop();
      }
      ImGui::PopID();
      continue;
    }
    const char* kind = duel.kind == MatchDuelDetail::Kind::ENGAGE ? "Goes in"
                       : duel.kind == MatchDuelDetail::Kind::TAKE_ON
                           ? "Take-on"
                           : "Tackle";
    std::string label = std::format(
        "{}  {}  {} on {}  ·  chance {:.0f}%  roll {:.0f}%  ->  {}",
        clock(at->minute), kind, recorder.playerName(duel.player),
        recorder.playerName(duel.opponent), duel.probability * 100.0f,
        duel.roll * 100.0f, duel.roll < duel.probability ? "success" : "failed");
    if (duel.foulRoll)
      label += std::format(
          "  ·  foul threshold {:.0f}%  roll {:.0f}%  ->  {}",
          duel.foulThreshold * 100.0f, *duel.foulRoll * 100.0f,
          *duel.foulRoll < duel.foulThreshold ? "FOUL" : "no foul");
    ImGui::PushID(shown);
    if (ImGui::TreeNodeEx("duel", ImGuiTreeNodeFlags_SpanAvailWidth, "%s",
                          label.c_str()))
    {
      ImGui::TextDisabled(duel.kind == MatchDuelDetail::Kind::ENGAGE
                              ? "Engagement rate per second (the chance this "
                                "step is 1 - exp(-rate x 0.1 s)):"
                              : "Chance of success:");
      renderBreakdown(duel.chance, "chance");
      if (duel.kind == MatchDuelDetail::Kind::TACKLE)
      {
        ImGui::TextDisabled("Foul propensity:");
        renderBreakdown(duel.foul, "foul");
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (shown == 0) ImGui::TextDisabled("No duels in this stretch yet.");
}

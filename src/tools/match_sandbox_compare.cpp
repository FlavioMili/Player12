// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_compare.h"

#include <imgui.h>

#include <cmath>
#include <format>
#include <map>
#include <string>
#include <utility>

#include "global/build_info.h"
#include "gui/widgets/theme.h"
#include "tools/match_sandbox_analysis_ui.h"
#include "tools/match_sandbox_inspector.h"
#include "tools/match_sandbox_names.h"
#include "tools/match_sandbox_recorder.h"
#include "tools/match_sandbox_review.h"

namespace
{
constexpr std::array<const char*, 4> OPTION_NAMES{"Pass", "Shot", "Carry",
                                                  "Shield"};

std::string value(float number)
{
  return std::isfinite(number) ? std::format("{:.3f}", number) : "-";
}

std::string difference(float before, float after)
{
  if (!std::isfinite(before) || !std::isfinite(after))
  {
    return std::isfinite(before) == std::isfinite(after) ? "" : "changed";
  }
  const float delta = after - before;
  return delta == 0.0F ? "" : std::format("{:+.3f}", delta);
}

std::string decisionLine(const SandboxRecorder* names,
                         const MatchRecording::Decision& decision)
{
  const MatchDecisionRecord& record = decision.record;
  return std::format(
      "tick {}  {}  {}  ·  pass {}  shot {}  carry {}  shield {}",
      decision.tick,
      (names != nullptr) ? names->playerName(record.player) : "?",
      SandboxNames::choice(record.chosen), value(record.pass),
      value(record.shot), value(record.carry), value(record.shield));
}

/** The breakdown of the decision at (tick, player) in a list, if any. */
const MatchDecisionDetail* findDetail(
    const std::vector<DetailRecorder::At<MatchDecisionDetail>>& details,
    std::uint64_t tick, PlayerID player)
{
  for (const auto& at : details)
  {
    if (at.tick == tick && at.detail.player == player)
    {
      return &at.detail;
    }
  }
  return nullptr;
}

/** Two breakdowns of one score, term by term (matched by source and name). */
void renderTermDiff(const ScoreBreakdown& before, const ScoreBreakdown& after)
{
  std::map<std::pair<int, std::string>, std::pair<float, float>> terms;
  for (const ScoreTerm& term : before.terms)
  {
    terms[{static_cast<int>(term.source), term.name}].first += term.value;
  }
  for (const ScoreTerm& term : after.terms)
  {
    terms[{static_cast<int>(term.source), term.name}].second += term.value;
  }
  constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg |
                                    ImGuiTableFlags_BordersInnerV |
                                    ImGuiTableFlags_SizingStretchProp;
  if (!ImGui::BeginTable("##terms", 4, flags))
  {
    return;
  }
  ImGui::TableSetupColumn("Term", ImGuiTableColumnFlags_WidthStretch, 3.0F);
  ImGui::TableSetupColumn("Recorded");
  ImGui::TableSetupColumn("Now");
  ImGui::TableSetupColumn("Change");
  ImGui::TableHeadersRow();
  for (const auto& [key, values] : terms)
  {
    const bool changed = values.first != values.second;
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (changed)
    {
      ImGui::TextColored(Theme::palette().warning, "%s", key.second.c_str());
    }
    else
    {
      ImGui::TextUnformatted(key.second.c_str());
    }
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value(values.first).c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(value(values.second).c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(difference(values.first, values.second).c_str());
  }
  ImGui::EndTable();
}
}  // namespace

bool RecordingComparison::render(const MatchRecording& recording,
                                 const Recordings::Replay& replay,
                                 const SandboxRecorder* replayed,
                                 MatchReview& review, DebugInspector& inspector)
{
  bool jumped = false;
  const auto& setup = recording.setup;
  ImGui::TextWrapped("%s v %s  ·  seed %u  ·  recorded %s",
                     setup.sides[0].name.c_str(), setup.sides[1].name.c_str(),
                     setup.matchSeed, recording.created.c_str());
  ImGui::TextWrapped("Recorded with: %s", recording.build.c_str());
  ImGui::TextWrapped("Replayed with: %s", BuildInfo::summary().c_str());
  if (!replay.error.empty())
  {
    ImGui::TextColored(Theme::palette().negative, "Cannot replay: %s",
                       replay.error.c_str());
    return false;
  }
  ImGui::TextWrapped("Score: recorded %d - %d, replayed %d - %d",
                     recording.homeScore, recording.awayScore, replay.homeScore,
                     replay.awayScore);
  ImGui::TextWrapped("%zu decisions recorded%s", recording.decisions.size(),
                     recording.details.empty() ? " (without full detail)"
                                               : " with full detail");
  ImGui::Separator();

  if (!replay.divergedAt && !replay.firstDifferentDecision)
  {
    ImGui::TextColored(Theme::palette().positive,
                       "Replays exactly: every tick and every decision "
                       "matches the recording.");
    return false;
  }

  if (replay.divergedAt)
  {
    ImGui::TextColored(Theme::palette().warning,
                       "The match plays differently from tick %llu.",
                       static_cast<unsigned long long>(*replay.divergedAt));
    SandboxAnalysisUI::next();
    if (SandboxAnalysisUI::button("Jump there") && (replayed != nullptr))
    {
      review.seek(*replayed, *replay.divergedAt);
      jumped = true;
    }
  }
  ImGui::TextWrapped(
      "Everything after the first difference is a different match: compare "
      "the first decision that changed, not later ones.");

  if (replay.firstDifferentDecision)
  {
    ImGui::SeparatorText("First decision made differently");
    const std::size_t index = *replay.firstDifferentDecision;
    const SandboxRecorder* names = replayed;
    if (index < recording.decisions.size())
    {
      ImGui::TextWrapped(
          "Recorded: %s",
          decisionLine(names, recording.decisions[index]).c_str());
    }
    else
    {
      ImGui::TextWrapped("Recorded: no further decision");
    }
    if (replay.replayedDecision)
    {
      ImGui::TextWrapped("Now:      %s",
                         decisionLine(names, *replay.replayedDecision).c_str());
      SandboxAnalysisUI::next();
      if (SandboxAnalysisUI::button("Jump to it") && (replayed != nullptr))
      {
        review.seek(*replayed, replay.replayedDecision->tick);
        jumped = true;
        inspector.selected = replay.replayedDecision->record.player;
      }
    }
    else
    {
      ImGui::TextWrapped("Now: no further decision");
    }
    renderDecisionDiff(recording, replay, replayed);
  }
  return jumped;
}

void RecordingComparison::renderDecisionDiff(const MatchRecording& recording,
                                             const Recordings::Replay& replay,
                                             const SandboxRecorder* replayed)
{
  const std::size_t index = *replay.firstDifferentDecision;
  if (index >= recording.decisions.size() || !replay.replayedDecision ||
      (replayed == nullptr))
  {
    return;
  }
  if (recording.details.empty())
  {
    ImGui::TextDisabled(
        "Saved without full detail: no term-by-term "
        "comparison.");
    return;
  }
  const MatchRecording::Decision& recorded = recording.decisions[index];
  const MatchRecording::Decision& now = *replay.replayedDecision;
  const MatchDecisionDetail* before =
      findDetail(recording.details, recorded.tick, recorded.record.player);
  replayDetail.ensure(*replayed, now.tick);
  const MatchDecisionDetail* after =
      findDetail(replayDetail.detail().decisions, now.tick, now.record.player);
  if ((before == nullptr) || (after == nullptr))
  {
    ImGui::TextDisabled("No breakdown of this decision on one side.");
    return;
  }
  ImGui::SeparatorText("Scores before noise, term by term");
  for (std::size_t option = 0; option < OPTION_NAMES.size(); ++option)
  {
    const ScoreBreakdown& old = before->options.at(option);
    const ScoreBreakdown& current = after->options.at(option);
    ImGui::PushID(static_cast<int>(option));
    const std::string change = difference(old.total, current.total);
    const std::string label =
        std::format("{}  {} -> {}  {}", OPTION_NAMES.at(option),
                    value(old.total), value(current.total), change);
    if (!change.empty())
    {
      ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
    }
    if (ImGui::TreeNode("option", "%s", label.c_str()))
    {
      renderTermDiff(old, current);
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
  if (ImGui::TreeNode("noise", "Noise  %s",
                      before->noise == after->noise ? "unchanged" : "changed"))
  {
    for (std::size_t option = 0; option < OPTION_NAMES.size(); ++option)
    {
      ImGui::BulletText("%s  %s -> %s", OPTION_NAMES.at(option),
                        value(before->noise.at(option)).c_str(),
                        value(after->noise.at(option)).c_str());
    }
    ImGui::TreePop();
  }
}

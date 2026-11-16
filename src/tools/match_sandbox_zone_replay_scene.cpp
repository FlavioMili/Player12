// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_zone_replay_scene.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

#include "controller/game_controller.h"
#include "global/paths.h"
#include "gui/gui_view.h"
#include "gui/widgets/theme.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_typography.h"

ZoneReplayScene::ZoneReplayScene(GUIView* guiView_ptr, ZoneRecording recording_value)
    : GUIScene(guiView_ptr), recording(std::move(recording_value))
{
}

ZoneReplayScene::~ZoneReplayScene() { replay.reset(); }

SceneID ZoneReplayScene::getID() const { return SceneID::MATCH_SANDBOX; }

void ZoneReplayScene::onEnter()
{
  auto* atlas = ImGui::GetIO().Fonts;
  const auto font = [&](const char* file, float size)
  {
    const auto config = SandboxTypography::manropeConfig();
    for (auto* existing : atlas->Fonts)
      if (std::string_view(existing->GetDebugName()).find(file) != std::string_view::npos &&
          !existing->Sources.empty() &&
          existing->Sources[0]->ExtraSizeScale == config.ExtraSizeScale)
        return existing;
    return atlas->AddFontFromFileTTF(
        AssetPaths::file((std::string("assets/fonts/") + file).c_str()).c_str(), size,
        &config);
  };
  regularFont = font("Manrope-Regular.ttf", 16);
  boldFont = font("Manrope-Bold.ttf", 14);
  // Rebuilt and played through on the current engine; then reviewed.
  replay = ZoneRecordings::replay(recording, guiView->getController().getStatsConfig());
  review.hideOffPitch = true;
  debugger.reset();
  inspector.reset();
  // Nothing to step: the replay is complete.
  debugger.stepper = [](int) {};
}

void ZoneReplayScene::update(float /*deltaTime*/) {}

void ZoneReplayScene::render()
{
  namespace Page = SandboxPageUI;
  const auto* viewport = ImGui::GetMainViewport();
  const float scale = Theme::scale();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, Page::PAGE);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * scale, 24 * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::Begin("##zone_replay", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float gutter = 6 * scale + ImGui::GetStyle().ScrollbarSize;
  const float panel = std::min(440 * scale, available.x * 0.45f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::BeginChild("##zone_replay_controls", ImVec2(panel + gutter, available.y),
                    ImGuiChildFlags_AlwaysUseWindowPadding);
  renderPanel(panel);
  ImGui::EndChild();
  ImGui::SameLine(0, 6 * scale);
  ImGui::BeginChild("##zone_replay_pitch", ImVec2(0, available.y));
  if (replay && replay->run)
  {
    Page::beginCard("##zone_replay_pitch_card", "ZONE", ImGui::GetContentRegionAvail().x,
                    Page::DARK, scale, boldFont);
    review.renderPitch(replay->run->recorder(), &replay->run->engine(), 0, 0, &inspector);
    Page::endCard(scale);
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::End();
}

void ZoneReplayScene::renderPanel(float width)
{
  namespace Page = SandboxPageUI;
  const float scale = Theme::scale();
  const std::string subtitle = std::format("{}: {}", recording.session, recording.zone);
  const std::string context =
      std::format("{}, saved {} on build {}. Played again on this engine.", recording.exercise,
                  recording.created, recording.build);
  Page::header("Zone replay", subtitle.c_str(), width, scale, boldFont, regularFont,
               context.c_str(), true);
  Page::beginCard("##zone_replay_status", "REPLAY", width, Page::DARK, scale, boldFont);
  if (Page::button("Back", false, scale, boldFont)) guiView->popScene();
  ImGui::PushTextWrapPos(0);
  if (!replay || !replay->error.empty())
  {
    ImGui::TextUnformatted(
        std::format("Can't replay: {}", replay ? replay->error : std::string("no replay"))
            .c_str());
    ImGui::PopTextWrapPos();
    Page::endCard(scale);
    return;
  }
  const MatchEngine& engine = replay->run->engine();
  ImGui::TextUnformatted(std::format("Recorded {}-{} over {:.1f} s; replayed {}-{}.",
                                     recording.homeScore, recording.awayScore,
                                     recording.lastTick *
                                         MatchTuning::Timing::FIXED_STEP_SECONDS,
                                     engine.getHomeScore(), engine.getAwayScore())
                             .c_str());
  if (replay->divergedAt)
    ImGui::TextUnformatted(
        std::format("This engine first plays it differently at tick {} ({:.1f} s).",
                    *replay->divergedAt,
                    *replay->divergedAt * MatchTuning::Timing::FIXED_STEP_SECONDS)
            .c_str());
  else
    ImGui::TextUnformatted("This engine plays it identically.");
  ImGui::PopTextWrapPos();
  Page::endCard(scale);

  Page::beginCard("##zone_replay_review", "REVIEW", width, Page::LIGHT, scale, boldFont);
  review.renderPanel(replay->run->recorder(), &replay->run->engine(), &paused, &inspector);
  Page::endCard(scale);

  Page::beginCard("##zone_replay_debugger", "DEBUGGER", width, Page::LIGHT, scale, boldFont);
  MatchEngine& live = replay->run->engine();
  const MatchEngine* inspected = review.shownEngine() ? review.shownEngine() : &live;
  debugger.renderContents(replay->run->recorder(), &live, &paused, review.cursor(), inspected,
                          inspector);
  if (const auto tick = debugger.takeSeekRequest()) review.seek(replay->run->recorder(), *tick);
  Page::endCard(scale);
}

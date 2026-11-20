// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_review.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <variant>

#include "gui/render/match_renderer_2d.h"
#include "gui/widgets/theme.h"
#include "tools/match_sandbox_analysis_ui.h"
#include "tools/match_sandbox_inspector.h"
#include "tools/match_sandbox_names.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_recorder.h"
#include "tools/match_sandbox_rewind.h"

namespace
{
constexpr float STEP_SECONDS = MatchTuning::Timing::FIXED_STEP_SECONDS;
constexpr float TIMELINE_HEIGHT = 46.0F;
constexpr float BAND_HEIGHT = 9.0F;
constexpr std::array<float, 6> SPEEDS{0.25F, 0.5F, 1.0F, 2.0F, 4.0F, 8.0F};
constexpr int SECONDS_PER_MINUTE = 60;
constexpr float TIMELINE_ROUNDING = 3.0F;
/** A marker this close to the mouse (scaled pixels) is the one hovered. */
constexpr float MARKER_PICK_PIXELS = 5.0F;
constexpr float GOAL_MARKER_THICKNESS = 2.5F;
constexpr float MARKER_THICKNESS = 1.5F;
constexpr float CURSOR_THICKNESS = 2.0F;
/** Transport controls: the width each button or the speed box takes. */
constexpr float TRANSPORT_SLOT_WIDTH = 110.0F;
constexpr float SPEED_COMBO_WIDTH = 80.0F;
constexpr float VIEWPORT_CENTRE = 0.5F;

std::string clock(float minute)
{
  const int seconds =
      static_cast<int>(std::floor(std::max(0.0F, minute) * 60.0F));
  return std::format("{:02}:{:02}", seconds / SECONDS_PER_MINUTE,
                     seconds % SECONDS_PER_MINUTE);
}

ImU32 phaseColor(TeamPhase phase)
{
  switch (phase)
  {
    case TeamPhase::POSSESSION:
      return IM_COL32(60, 160, 90, 255);
    case TeamPhase::FINAL_THIRD:
      return IM_COL32(110, 220, 120, 255);
    case TeamPhase::ATTACKING_TRANSITION:
      return IM_COL32(170, 210, 90, 255);
    case TeamPhase::DEFENSIVE_BLOCK:
      return IM_COL32(70, 110, 170, 255);
    case TeamPhase::DEFENSIVE_TRANSITION:
      return IM_COL32(220, 140, 60, 255);
    case TeamPhase::SET_PIECE:
      return IM_COL32(140, 100, 180, 255);
    case TeamPhase::STOPPAGE:
      return IM_COL32(70, 70, 75, 255);
  }
  return IM_COL32(70, 70, 75, 255);
}

bool shownOnTimeline(MatchEventType type)
{
  switch (type)
  {
    case MatchEventType::GOAL:
    case MatchEventType::OWN_GOAL:
    case MatchEventType::SHOT:
    case MatchEventType::YELLOW_CARD:
    case MatchEventType::SECOND_YELLOW:
    case MatchEventType::RED_CARD:
    case MatchEventType::SUBSTITUTION:
    case MatchEventType::PENALTY:
    case MatchEventType::HALF_TIME:
    case MatchEventType::FULL_TIME:
      return true;
    default:
      return false;
  }
}
}  // namespace

MatchReview::MatchReview() : renderer(std::make_unique<MatchRenderer2D>()) {}

MatchReview::~MatchReview() = default;

void MatchReview::reset()
{
  shown.reset();
  divergedAt.reset();
  playing = false;
  playAccumulator = 0.0F;
  timelineEntries = 0;
  homePhases.clear();
  awayPhases.clear();
  markers.clear();
}

std::optional<std::uint64_t> MatchReview::cursor() const
{
  if (!shown)
  {
    return std::nullopt;
  }
  return shown->getSimulatedSteps();
}

void MatchReview::seek(const SandboxRecorder& recorder, std::uint64_t tick)
{
  MatchRewind::Result rebuilt = MatchRewind::rebuild(recorder, tick);
  if (!rebuilt.engine)
  {
    return;
  }
  shown = std::move(rebuilt.engine);
  divergedAt = rebuilt.divergedAt;
  playAccumulator = 0.0F;
}

void MatchReview::step(const SandboxRecorder& recorder, std::int64_t ticks,
                       std::uint64_t end)
{
  const std::uint64_t now = shown ? shown->getSimulatedSteps() : end;
  if (ticks < 0)
  {
    const auto back = static_cast<std::uint64_t>(-ticks);
    seek(recorder, now > back + 1 ? now - back : 1);
    return;
  }
  if (!shown)
  {
    seek(recorder, end);
    return;
  }
  const std::uint64_t forward = std::min<std::uint64_t>(
      static_cast<std::uint64_t>(ticks), end > now ? end - now : 0);
  if (const auto diverged = MatchRewind::stepForward(recorder, *shown, forward);
      diverged && !divergedAt)
  {
    divergedAt = diverged;
  }
}

void MatchReview::refreshTimeline(const SandboxRecorder& recorder)
{
  const auto guard = recorder.lock();
  const auto& entries = recorder.entries();
  if (entries.size() < timelineEntries)
  {
    timelineEntries = 0;
  }
  if (timelineEntries == 0)
  {
    // Kick-off is a set piece for both sides until the first change.
    homePhases = {{.tick = 0, .phase = TeamPhase::SET_PIECE}};
    awayPhases = {{.tick = 0, .phase = TeamPhase::SET_PIECE}};
    markers.clear();
  }
  for (; timelineEntries < entries.size(); ++timelineEntries)
  {
    const auto& entry = entries[timelineEntries];
    if (const auto* change =
            std::get_if<SandboxRecorder::PhaseChange>(&entry.payload))
    {
      (change->homeTeam ? homePhases : awayPhases)
          .push_back({.tick = entry.tick, .phase = change->to});
    }
    else if (const auto* event =
                 std::get_if<SandboxRecorder::EventEntry>(&entry.payload))
    {
      const MatchEvent& matchEvent = recorder.events()[event->index];
      if (shownOnTimeline(matchEvent.type))
      {
        markers.push_back({.tick = entry.tick,
                           .type = matchEvent.type,
                           .homeTeam = matchEvent.isHomeTeam,
                           .event = event->index});
      }
    }
  }
}

void MatchReview::renderPanel(const SandboxRecorder& recorder,
                              const MatchEngine* live, bool* livePaused,
                              DebugInspector* inspector)
{
  ImGui::PushTextWrapPos(0);
  std::uint64_t end = 0;
  {
    const auto guard = recorder.lock();
    end = recorder.lastTick();
  }
  refreshTimeline(recorder);
  // After the match there is no live view: start on the final whistle.
  if ((live == nullptr) && !shown && end > 0)
  {
    seek(recorder, end);
  }

  if (shown)
  {
    ImGui::Text("%s  ·  tick %llu", clock(shown->getMatchTimeMinutes()).c_str(),
                static_cast<unsigned long long>(shown->getSimulatedSteps()));

    if (divergedAt)
    {
      ImGui::TextColored(Theme::palette().negative,
                         "Rebuild differs from the match from tick %llu: "
                         "what you see here is not what was played.",
                         static_cast<unsigned long long>(*divergedAt));
    }
    else
    {
      ImGui::TextColored(Theme::palette().positive, "Rebuilt exactly");
    }
  }
  else
  {
    ImGui::TextWrapped(
        "Following the live match. Click or drag the timeline to rewind "
        "(the live match pauses).");
  }

  renderTimeline(recorder, end, livePaused);
  renderTransport(recorder, end, livePaused);
  if (playing)
  {
    play(recorder, end);
  }
  // Embedded (the moment on the match's own pitch): the overlay switches
  // here; in the window they sit above its pitch.
  if (inspector != nullptr)
  {
    inspector->renderOverlayToggles();
  }
  ImGui::PopTextWrapPos();
}

void MatchReview::renderTimeline(const SandboxRecorder& recorder,
                                 std::uint64_t end, bool* livePaused)
{
  const float scale = Theme::scale();
  const float width = std::max(50.0F, ImGui::GetContentRegionAvail().x);
  const float height = TIMELINE_HEIGHT * scale;
  const ImVec2 min = ImGui::GetCursorScreenPos();
  const ImVec2 max(min.x + width, min.y + height);
  ImGui::InvisibleButton("##timeline", ImVec2(width, height));
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(min, max, IM_COL32(25, 27, 32, 255),
                          TIMELINE_ROUNDING);
  if (end == 0)
  {
    return;
  }

  const auto span = static_cast<float>(end);
  const auto xOf = [&](std::uint64_t tick)
  { return min.x + (width * std::min(1.0F, static_cast<float>(tick) / span)); };
  const float band = BAND_HEIGHT * scale;
  const auto drawBands = [&](const std::vector<PhaseSpan>& spans, float top)
  {
    for (std::size_t index = 0; index < spans.size(); ++index)
    {
      const std::uint64_t until =
          index + 1 < spans.size() ? spans[index + 1].tick : end;
      drawList->AddRectFilled(
          ImVec2(xOf(spans[index].tick), top),
          ImVec2(std::max(xOf(until), xOf(spans[index].tick) + 1.0F),
                 top + band),
          phaseColor(spans[index].phase));
    }
  };
  const float homeTop = min.y + (3.0F * scale);
  const float awayTop = homeTop + band + (2.0F * scale);
  drawBands(homePhases, homeTop);
  drawBands(awayPhases, awayTop);

  // Event markers below the bands: goals tall, shots short, home above.
  const float markerTop = awayTop + band + (3.0F * scale);
  const float markerBottom = max.y - (2.0F * scale);
  const float middle = (markerTop + markerBottom) * 0.5F;
  const Marker* nearest = nullptr;
  float nearestDistance = MARKER_PICK_PIXELS * scale;
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  for (const Marker& marker : markers)
  {
    const float x = xOf(marker.tick);
    ImU32 color = IM_COL32(200, 200, 200, 255);
    float top = marker.homeTeam ? markerTop : middle;
    float bottom = marker.homeTeam ? middle : markerBottom;
    switch (marker.type)
    {
      case MatchEventType::GOAL:
      case MatchEventType::OWN_GOAL:
        color = IM_COL32(255, 255, 255, 255);
        top = markerTop;
        bottom = markerBottom;
        break;
      case MatchEventType::SHOT:
        color = IM_COL32(150, 150, 160, 255);
        break;
      case MatchEventType::YELLOW_CARD:
      case MatchEventType::SECOND_YELLOW:
        color = IM_COL32(245, 200, 40, 255);
        break;
      case MatchEventType::RED_CARD:
        color = IM_COL32(220, 50, 50, 255);
        break;
      case MatchEventType::SUBSTITUTION:
        color = IM_COL32(80, 200, 220, 255);
        break;
      case MatchEventType::PENALTY:
        color = IM_COL32(255, 130, 200, 255);
        break;
      default:
        color = IM_COL32(120, 120, 130, 255);
        top = markerTop;
        bottom = markerBottom;
        break;
    }
    drawList->AddLine(ImVec2(x, top), ImVec2(x, bottom), color,
                      marker.type == MatchEventType::GOAL
                          ? GOAL_MARKER_THICKNESS * scale
                          : MARKER_THICKNESS * scale);
    if (hovered && std::abs(mouse.x - x) < nearestDistance)
    {
      nearestDistance = std::abs(mouse.x - x);
      nearest = &marker;
    }
  }

  // The moment shown, or the live end while following.
  const std::uint64_t at = shown ? shown->getSimulatedSteps() : end;
  const float cursorX = xOf(at);
  drawList->AddLine(ImVec2(cursorX, min.y), ImVec2(cursorX, max.y),
                    IM_COL32(255, 255, 255, 230), CURSOR_THICKNESS * scale);

  if (hovered)
  {
    const auto tick = static_cast<std::uint64_t>(
        std::clamp((mouse.x - min.x) / width, 0.0F, 1.0F) * span);
    if (nearest != nullptr)
    {
      const auto guard = recorder.lock();
      const MatchEvent& event = recorder.events()[nearest->event];
      ImGui::SetTooltip("%s'  %s", clock(event.timeMinute).c_str(),
                        event.description.c_str());
    }
    else
    {
      // The colour key: each side's phase at the hovered moment.
      const auto phaseAt = [tick](const std::vector<PhaseSpan>& spans)
      {
        TeamPhase phase = TeamPhase::SET_PIECE;
        for (const PhaseSpan& phaseSpan : spans)
        {
          if (phaseSpan.tick <= tick)
          {
            phase = phaseSpan.phase;
          }
        }
        return phase;
      };
      ImGui::SetTooltip("Home: %s\nAway: %s\ntick %llu (%.0f s of play)",
                        SandboxNames::phase(phaseAt(homePhases)),
                        SandboxNames::phase(phaseAt(awayPhases)),
                        static_cast<unsigned long long>(tick),
                        static_cast<double>(tick) * STEP_SECONDS);
    }
  }
  if (active)
  {
    // Snap to a marker under the mouse so events are easy to land on.
    std::uint64_t tick =
        (nearest != nullptr)
            ? nearest->tick
            : static_cast<std::uint64_t>(
                  std::clamp((mouse.x - min.x) / width, 0.0F, 1.0F) * span);
    tick = std::clamp<std::uint64_t>(tick, 1, end);
    if (!shown || shown->getSimulatedSteps() != tick)
    {
      if (livePaused != nullptr)
      {
        *livePaused = true;
      }
      playing = false;
      seek(recorder, tick);
    }
  }
}

void MatchReview::renderTransport(const SandboxRecorder& recorder,
                                  std::uint64_t end, bool* livePaused)
{
  const auto pauseLive = [livePaused]
  {
    if (livePaused)
    {
      *livePaused = true;
    }
  };
  if (SandboxAnalysisUI::button("|< Start"))
  {
    pauseLive();
    playing = false;
    seek(recorder, 1);
  }
  struct Jump
  {
    const char* label;
    std::int64_t ticks;
  };
  constexpr std::array<Jump, 3> BACK{{{.label = "-10 s", .ticks = -100},
                                      {.label = "-1 s", .ticks = -10},
                                      {.label = "-0.1 s", .ticks = -1}}};
  for (const Jump& jump : BACK)
  {
    SandboxAnalysisUI::next(TRANSPORT_SLOT_WIDTH);
    if (SandboxAnalysisUI::button(jump.label))
    {
      pauseLive();
      playing = false;
      step(recorder, jump.ticks, end);
    }
  }
  SandboxAnalysisUI::next(TRANSPORT_SLOT_WIDTH);
  if (SandboxAnalysisUI::button(playing ? "Pause##review" : "Play##review"))
  {
    pauseLive();
    if (!shown)
    {
      seek(recorder, end);
    }
    playing = !playing;
    playAccumulator = 0.0F;
  }
  constexpr std::array<Jump, 3> FORWARD{{{.label = "+0.1 s", .ticks = 1},
                                         {.label = "+1 s", .ticks = 10},
                                         {.label = "+10 s", .ticks = 100}}};
  for (const Jump& jump : FORWARD)
  {
    SandboxAnalysisUI::next(TRANSPORT_SLOT_WIDTH);
    if (SandboxAnalysisUI::button(jump.label))
    {
      pauseLive();
      playing = false;
      step(recorder, jump.ticks, end);
    }
  }
  SandboxAnalysisUI::next(TRANSPORT_SLOT_WIDTH);
  if (livePaused != nullptr)
  {
    ImGui::BeginDisabled(!shown);
    if (SandboxAnalysisUI::button("Back to live >|"))
    {
      shown.reset();
      divergedAt.reset();
      playing = false;
      *livePaused = false;
    }
    ImGui::EndDisabled();
  }
  else if (SandboxAnalysisUI::button("End >|"))
  {
    playing = false;
    seek(recorder, end);
  }
  SandboxAnalysisUI::next(TRANSPORT_SLOT_WIDTH);
  ImGui::SetNextItemWidth(SPEED_COMBO_WIDTH * Theme::scale());
  const std::string speedLabel = std::format("{:g}x", playSpeed);
  if (SandboxPageUI::beginCombo("##speed", speedLabel.c_str()))
  {
    for (const float speed : SPEEDS)
    {
      const std::string label = std::format("{:g}x", speed);
      if (ImGui::Selectable(label.c_str(), speed == playSpeed))
      {
        playSpeed = speed;
      }
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip("Review playback speed");
  }
}

void MatchReview::play(const SandboxRecorder& recorder, std::uint64_t end)
{
  if (!shown || shown->getSimulatedSteps() >= end)
  {
    playing = false;
    return;
  }
  playAccumulator += ImGui::GetIO().DeltaTime * playSpeed;
  const auto ticks = static_cast<std::int64_t>(playAccumulator / STEP_SECONDS);
  if (ticks <= 0)
  {
    return;
  }
  playAccumulator -= static_cast<float>(ticks) * STEP_SECONDS;
  step(recorder, ticks, end);
}

void MatchReview::renderPitch(const SandboxRecorder& recorder,
                              const MatchEngine* live, TeamID home, TeamID away,
                              DebugInspector* inspector, bool overlayControls)
{
  // The moment under review, or the live match while following it.
  const MatchEngine* engine = shown ? shown.get() : live;
  if ((inspector != nullptr) && overlayControls)
  {
    inspector->renderOverlayToggles();
  }
  const ImVec2 available = ImGui::GetContentRegionAvail();
  if (engine == nullptr)
  {
    ImGui::Dummy(available);
    return;
  }
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 size(std::max(1.0F, available.x), std::max(1.0F, available.y));
  // A click on the pitch selects the nearest player (see drawOverlays).
  const bool clicked = ImGui::InvisibleButton("##review_pitch", size);
  MatchViewport view = computeMatchViewport(origin.x, origin.y, size.x, size.y);
  view.x = origin.x + std::max(0.0F, (size.x - view.width) * VIEWPORT_CENTRE);
  view.y = origin.y + std::max(0.0F, (size.y - view.height) * VIEWPORT_CENTRE);

  fillMatchRenderSnapshot(*engine, snapshot);
  if (hideOffPitch)
  {
    std::erase_if(snapshot.players, [](const MatchRenderPlayer& player)
                  { return !player.onPitch; });
  }
  // A rebuilt moment is shown exactly, not between two steps.
  if (shown)
  {
    snapshot.interpolationAlpha = 1.0F;
  }
  snapshot.homeTeam = home;
  snapshot.awayTeam = away;
  MatchRenderOptions options;
  options.showPlayerNames = true;
  options.frameSeconds = ImGui::GetIO().DeltaTime;
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                         true);
  renderer->render(snapshot, options, view);
  if (inspector != nullptr)
  {
    inspector->drawOverlays(recorder, *engine, *renderer, clicked);
  }
  drawList->PopClipRect();
}

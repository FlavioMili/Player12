// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_debugger.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <string>
#include <utility>
#include <variant>

#include "gui/widgets/theme.h"
#include "model/match_engine.h"
#include "tools/match_sandbox_analysis_ui.h"
#include "tools/match_sandbox_inspector.h"
#include "tools/match_sandbox_names.h"
#include "tools/match_sandbox_page_ui.h"
#include "tools/match_sandbox_recorder.h"

namespace
{
using Recorder = SandboxRecorder;
using Kind = EngineDebugger::Kind;

constexpr std::array<const char*, static_cast<std::size_t>(Kind::COUNT)>
    KIND_NAMES{"Phase",  "Job",        "Intent", "Decision",
               "Action", "Possession", "Keeper", "Event"};

std::string score(float value)
{
  return std::isfinite(value) ? std::format("{:.2f}", value) : "-";
}

float metres(PitchPoint from, PitchPoint to)
{
  return std::hypot(to.x - from.x, to.y - from.y);
}

/** Match clock "mm:ss". */
std::string clock(float minute)
{
  const int seconds =
      static_cast<int>(std::floor(std::max(0.0F, minute) * 60.0F));
  return std::format("{:02}:{:02}", seconds / 60, seconds % 60);
}

/** One table row: team (-1 none), player and the detail text. */
struct Row
{
  int home = -1;
  PlayerID player = 0;
  std::string detail;
};

Row describeRow(const Recorder& recorder, const Recorder::Entry& entry)
{
  const auto name = [&recorder](PlayerID id)
  { return recorder.playerName(id); };
  return std::visit(
      [&](const auto& item) -> Row
      {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, Recorder::PhaseChange>)
        {
          return {item.homeTeam, 0,
                  std::format("{} -> {}", SandboxNames::phase(item.from),
                              SandboxNames::phase(item.to))};
        }
        else if constexpr (std::is_same_v<T, Recorder::JobChange>)
        {
          std::string text = std::format(
              "{}: {}", SandboxNames::job(item.job),
              item.player ? name(item.player) : std::string("nobody"));
          if (item.previous)
          {
            text += std::format(" (was {})", name(item.previous));
          }
          if (item.player && (item.job == Recorder::Job::PRESSER_1 ||
                              item.job == Recorder::Job::PRESSER_2))
          {
            text += std::format(" · arrives in {:.2f} s", item.score);
          }
          else if (item.player && (item.job == Recorder::Job::RUNNER_1 ||
                                   item.job == Recorder::Job::RUNNER_2 ||
                                   item.job == Recorder::Job::RUNNER_3))
          {
            text += std::format(" · run priority {:.2f}", item.score);
          }
          return {item.homeTeam, item.player, text};
        }
        else if constexpr (std::is_same_v<T, Recorder::IntentChange>)
        {
          std::string text =
              std::format("{} -> {}", SandboxNames::intent(item.from),
                          SandboxNames::intent(item.to));
          if (item.job != Recorder::Job::NONE)
          {
            text += std::format("  [{}]", SandboxNames::job(item.job));
          }
          return {item.homeTeam, item.player, text};
        }
        else if constexpr (std::is_same_v<T, MatchDecisionRecord>)
        {
          std::string text = std::format(
              "{}  ·  pass {}  shot {}  carry {}  shield {}",
              SandboxNames::choice(item.chosen), score(item.pass),
              score(item.shot), score(item.carry), score(item.shield));
          if (item.chosen == ScenarioAction::PASS && item.bestReceiver)
          {
            text += std::format("  ·  to {} ({})", name(item.bestReceiver),
                                SandboxNames::passIntent(item.bestPassIntent));
          }
          if (item.chosenWithoutNoise != item.chosen)
          {
            text += std::format("  ·  NOISE FLIPPED (scores alone: {})",
                                SandboxNames::choice(item.chosenWithoutNoise));
          }
          return {item.homeTeam, item.player, text};
        }
        else if constexpr (std::is_same_v<T, MatchActionRecord>)
        {
          std::string text = SandboxNames::action(item.kind);
          if (item.header)
          {
            text += " (header)";
          }
          switch (item.kind)
          {
            case MatchActionKind::PASS:
              text +=
                  std::format(" {}{} to {} · {:.1f} m · est. completion {:.2f}",
                              SandboxNames::passIntent(item.passIntent),
                              item.lofted ? ", lofted" : "", name(item.target),
                              metres(item.from, item.to), item.estimate);
              break;
            case MatchActionKind::SHOT:
              text += std::format(" · xG {:.2f} · {:.1f} m/s", item.estimate,
                                  item.speed);
              break;
            case MatchActionKind::CLEARANCE:
            case MatchActionKind::KNOCK_DOWN:
              text += std::format(" · {:.1f} m", metres(item.from, item.to));
              if (item.target)
              {
                text += std::format(" toward {}", name(item.target));
              }
              break;
            case MatchActionKind::TAKE_ON:
              text +=
                  std::format(" vs {} · chance {:.2f} · {}", name(item.target),
                              item.estimate, SandboxNames::duel(item.result));
              break;
            case MatchActionKind::TACKLE:
              text += std::format(
                  "{} on {} · win {:.2f} · foul risk {:.2f} · {}",
                  item.lofted ? " (sliding)" : "", name(item.target),
                  item.estimate, item.foulPropensity,
                  SandboxNames::duel(item.result));
              break;
          }
          return {item.homeTeam, item.player, text};
        }
        else if constexpr (std::is_same_v<T, Recorder::PossessionChange>)
        {
          std::string text =
              item.from == 0 ? std::format("ball to {}", name(item.to))
              : item.fromHome == item.toHome
                  ? std::format("received from {}", name(item.from))
                  : std::format("won from {}", name(item.from));
          if (item.afterAction && *item.afterAction < recorder.entries().size())
          {
            const auto& cause = recorder.entries()[*item.afterAction].payload;
            if (const auto* action = std::get_if<MatchActionRecord>(&cause))
            {
              text += std::format("  (after {} by {})",
                                  SandboxNames::action(action->kind),
                                  name(action->player));
            }
          }
          return {item.toHome, item.to, text};
        }
        else if constexpr (std::is_same_v<T, Recorder::KeeperChange>)
        {
          return {item.homeTeam, 0,
                  std::format("keeper {} -> {}", goalkeeperStateName(item.from),
                              goalkeeperStateName(item.to))};
        }
        else
        {
          const MatchEvent& event = recorder.events()[item.index];
          return {
              .home = event.hasTeam ? static_cast<int>(event.isHomeTeam) : -1,
              .player = event.primaryPlayerId,
              .detail = std::format("{}  {}", matchEventTypeName(event.type),
                                    event.description)};
        }
      },
      entry.payload);
}
}  // namespace

EngineDebugger::Described EngineDebugger::describe(
    const SandboxRecorder& recorder, const SandboxRecorder::Entry& entry)
{
  Row row = describeRow(recorder, entry);
  return {
      .home = row.home, .player = row.player, .detail = std::move(row.detail)};
}

void EngineDebugger::reset()
{
  wheel_rows = 0.0F;
  visible.clear();
  visible_for_size = 0;
  filters_changed = true;
}

std::optional<std::uint64_t> EngineDebugger::takeSeekRequest()
{
  return std::exchange(seek_request, std::nullopt);
}

void EngineDebugger::renderContents(SandboxRecorder& recorder,
                                    MatchEngine* engine, bool* paused,
                                    std::optional<std::uint64_t> cursor,
                                    const MatchEngine* inspected,
                                    DebugInspector& inspector)
{
  renderControls(recorder, engine, paused);
  if (const MatchEngine* shown = (inspected != nullptr) ? inspected : engine)
  {
    const std::string handshake(handshakeName(shown->getHandshake()));
    ImGui::TextWrapped(
        "Brains: %s · Bodies: %s · Handshake: %s · Home advantage: %.2f",
        brainPairName({shown->getBrain(true), shown->getBrain(false)}).c_str(),
        bodyPairName({shown->getBody(true), shown->getBody(false)}).c_str(),
        handshake.c_str(), shown->getMatchContext().homeAdvantageScale);
  }
  constexpr std::array tabs{"Log",      "Team",    "Player",
                            "Decision", "Referee", "Summary"};
  if (inspected == nullptr)
  {
    contentTab = 0;
  }
  for (int i = 0;
       i < ((inspected != nullptr) ? static_cast<int>(tabs.size()) : 1); ++i)
  {
    if (i != 0)
    {
      SandboxAnalysisUI::next(
          SandboxPageUI::buttonWidth(tabs.at(i), Theme::scale(),
                                     SandboxAnalysisUI::bold()) /
          Theme::scale());
    }
    if (SandboxAnalysisUI::button(tabs.at(i), contentTab == i))
    {
      contentTab = i;
    }
  }
  switch (contentTab)
  {
    case 0:
      ImGui::PushFont(SandboxAnalysisUI::regular(), 12);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                          ImVec2(6 * Theme::scale(), 3 * Theme::scale()));
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                          ImVec2(6 * Theme::scale(), 3 * Theme::scale()));
      renderFilters(recorder, inspector.selected);
      ImGui::PopStyleVar(2);
      ImGui::PopFont();
      renderLog(recorder, cursor, inspector);
      break;
    case 1:
      inspector.renderTeam(recorder, *inspected);
      break;
    case 2:
      inspector.renderPlayer(recorder, *inspected);
      break;
    case 3:
      inspector.renderDecision(recorder, *inspected);
      break;
    case 4:
      inspector.renderReferee(recorder, *inspected);
      break;
    case 5:
      inspector.renderSummary(recorder, *inspected);
      break;
    default:
      break;
  }
}

void EngineDebugger::renderControls(SandboxRecorder& recorder,
                                    MatchEngine* engine, bool* paused)
{
  if ((engine != nullptr) && (paused != nullptr))
  {
    if (SandboxAnalysisUI::button(*paused ? "Resume" : "Pause"))
    {
      *paused = !*paused;
    }
    SandboxAnalysisUI::next();
    ImGui::BeginDisabled(!*paused);
    // Stepping runs the same fixed steps as play, with the recorder attached.
    const auto stepBy = [&](int steps)
    {
      if (stepper)
      {
        stepper(steps);
      }
      else
      {
        engine->advance(static_cast<float>(steps) *
                        MatchTuning::Timing::FIXED_STEP_SECONDS);
      }
    };
    if (SandboxAnalysisUI::button("Step 0.1 s"))
    {
      stepBy(1);
    }
    SandboxAnalysisUI::next();
    if (SandboxAnalysisUI::button("Step 1 s"))
    {
      stepBy(10);
    }
    ImGui::EndDisabled();
    SandboxAnalysisUI::next();
    ImGui::TextDisabled(
        "%s  ·  tick %llu", clock(engine->getMatchTimeMinutes()).c_str(),
        static_cast<unsigned long long>(engine->getSimulatedSteps()));
  }
  else
  {
    ImGui::TextDisabled("Match over: the log of the last match.");
  }
  const auto guard = recorder.lock();
  ImGui::TextWrapped("  ·  %zu entries  ·  ~%zu KB", recorder.entries().size(),
                     recorder.approximateBytes() / 1024);
}

void EngineDebugger::renderFilters(const SandboxRecorder& recorder,
                                   PlayerID selection)
{
  // "Only selected" keeps the player filter on the inspector's selection.
  if (only_selected && player_filter != selection)
  {
    player_filter = selection;
    filters_changed = true;
  }
  ImGui::SetNextItemWidth(110.0F * Theme::scale());
  constexpr std::array<const char*, 3> TEAMS{"Both teams", "Home", "Away"};
  if (SandboxPageUI::combo("##team", &team_filter, TEAMS.data(),
                           static_cast<int>(TEAMS.size())))
  {
    filters_changed = true;
  }
  SandboxAnalysisUI::next(190);

  // Every player seen in the match, by name.
  std::map<std::string, PlayerID> players;
  {
    const auto guard = recorder.lock();
    for (const auto& [id, name] : recorder.knownPlayers())
    {
      players.emplace(name, id);
    }
  }
  ImGui::SetNextItemWidth(190.0F * Theme::scale());
  const std::string current = (player_filter != 0U)
                                  ? recorder.playerName(player_filter)
                                  : "All players";
  if (SandboxPageUI::beginCombo("##player", current.c_str(),
                                ImGuiComboFlags_HeightLarge))
  {
    if (ImGui::Selectable("All players", player_filter == 0))
    {
      player_filter = 0;
      filters_changed = true;
    }
    for (const auto& [label, id] : players)
    {
      if (ImGui::Selectable(label.c_str(), player_filter == id))
      {
        player_filter = id;
        filters_changed = true;
      }
    }
    ImGui::EndCombo();
  }
  SandboxAnalysisUI::next();
  if (ImGui::Checkbox("Only selected", &only_selected))
  {
    player_filter = only_selected ? selection : 0;
    filters_changed = true;
  }
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip(
        "Show only the player selected on the review pitch, "
        "in the Team or Player tab, or by clicking a row.");
  }
  SandboxAnalysisUI::next();
  ImGui::Checkbox("Follow", &follow);

  for (std::size_t kind = 0; kind < shown.size(); ++kind)
  {
    if (kind > 0)
    {
      SandboxAnalysisUI::next();
    }
    ImGui::PushID(static_cast<int>(kind));
    if (ImGui::Checkbox(KIND_NAMES.at(kind), &shown.at(kind)))
    {
      filters_changed = true;
    }
    ImGui::PopID();
  }
}

void EngineDebugger::refreshVisible(const SandboxRecorder& recorder)
{
  const auto& entries = recorder.entries();
  if (filters_changed || entries.size() < visible_for_size)
  {
    visible.clear();
    visible_for_size = 0;
    filters_changed = false;
  }
  const std::size_t before = visible.size();
  for (std::size_t index = visible_for_size; index < entries.size(); ++index)
  {
    const auto& entry = entries[index];
    if (!shown.at(entry.payload.index()))
    {
      continue;
    }
    if (team_filter != 0 || player_filter != 0)
    {
      const Row row = describeRow(recorder, entry);
      if (team_filter != 0 && row.home != (team_filter == 1 ? 1 : 0))
      {
        continue;
      }
      if (player_filter != 0 && row.player != player_filter)
      {
        continue;
      }
    }
    visible.push_back(index);
  }
  visible_for_size = entries.size();
  if (visible.size() != before && follow)
  {
    scroll_to_end = true;
  }
}

void EngineDebugger::renderLog(const SandboxRecorder& recorder,
                               std::optional<std::uint64_t> cursor,
                               DebugInspector& inspector)
{
  const auto guard = recorder.lock();
  refreshVisible(recorder);
  const auto& entries = recorder.entries();

  // The last row at or before the reviewed tick; scrolled to when it moves.
  std::optional<int> cursorRow;
  if (cursor)
  {
    const auto after = std::ranges::upper_bound(
        visible, *cursor, {},
        [&entries](std::size_t index) { return entries[index].tick; });
    if (after != visible.begin())
    {
      cursorRow = static_cast<int>(after - visible.begin()) - 1;
    }
  }
  const bool scrollToCursor = cursor && cursor != scrolled_cursor;
  scrolled_cursor = cursor;
  if (scrollToCursor)
  {
    follow = false;
  }

  constexpr ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
      ImGuiTableFlags_ScrollX | ImGuiTableFlags_BordersInnerV |
      ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
  const float rowHeight = std::ceil(ImGui::GetTextLineHeight() +
                                    (2 * ImGui::GetStyle().CellPadding.y));
  // Quantise the pending wheel/seek/follow request before BeginChild applies
  // it. The frozen header stays fixed; every data row has exactly this height.
  if (const auto* table = ImGui::TableFindByID(ImGui::GetID("##log"));
      (table != nullptr) && (table->InnerWindow != nullptr))
  {
    const auto* window = table->InnerWindow;
    const float decorations = window->DecoOuterSizeY1 +
                              window->DecoInnerSizeY1 + window->DecoOuterSizeY2;
    float requested =
        window->ScrollTarget.y < FLT_MAX
            ? window->ScrollTarget.y - (window->ScrollTargetCenterRatio.y *
                                        (window->SizeFull.y - decorations))
            : window->Scroll.y;
    const auto& io = ImGui::GetIO();
    if (io.MouseWheel != 0 && !io.KeyCtrl && !io.KeyShift &&
        ImGui::GetCurrentContext()->HoveredWindow == window)
    {
      wheel_rows -= io.MouseWheel * 3.F;
      const float steps = std::trunc(wheel_rows);
      wheel_rows -= steps;
      requested = window->Scroll.y + steps * rowHeight;
      follow = false;
    }
    const float limit = std::floor(window->ScrollMax.y / rowHeight) * rowHeight;
    ImGui::SetNextWindowScroll(
        ImVec2(-1, std::clamp(std::round(requested / rowHeight) * rowHeight,
                              0.F, std::max(0.F, limit))));
  }
  if (!ImGui::BeginTable("##log", 6, flags, ImVec2(0.0F, 0.0F)))
  {
    return;
  }
  ImGui::TableSetupScrollFreeze(0, 1);
  ImGui::TableSetupColumn("Time");
  ImGui::TableSetupColumn("Tick");
  ImGui::TableSetupColumn("Team");
  ImGui::TableSetupColumn("Player");
  ImGui::TableSetupColumn("Kind");
  ImGui::TableSetupColumn("Detail", ImGuiTableColumnFlags_WidthStretch);
  ImGui::PushFont(SandboxAnalysisUI::bold(), 14);
  ImGui::TableHeadersRow();
  ImGui::PopFont();

  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(visible.size()), rowHeight);
  if (scrollToCursor && cursorRow)
  {
    clipper.IncludeItemByIndex(*cursorRow);
  }
  while (clipper.Step())
  {
    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
    {
      const auto& entry = entries[visible[static_cast<std::size_t>(row)]];
      const Row described = describeRow(recorder, entry);
      ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
      ImGui::TableNextColumn();
      // The whole row selects: clicking it rewinds the review to its tick.
      ImGui::PushID(row);
      const bool atCursor =
          cursorRow &&
          entries[visible[static_cast<std::size_t>(*cursorRow)]].tick ==
              entry.tick;
      if (ImGui::Selectable(clock(entry.minute).c_str(), atCursor,
                            ImGuiSelectableFlags_SpanAllColumns))
      {
        seek_request = entry.tick;
        follow = false;
        if (described.player != 0)
        {
          inspector.selected = described.player;
        }
      }
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
      {
        ImGui::SetTooltip("Click to rewind the review to this moment.");
      }
      ImGui::PopID();
      if (scrollToCursor && cursorRow == row)
      {
        ImGui::SetScrollHereY(0.5F);
      }
      ImGui::TableNextColumn();
      ImGui::Text("%llu", static_cast<unsigned long long>(entry.tick));
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(described.home < 0   ? ""
                             : described.home > 0 ? "Home"
                                                  : "Away");
      ImGui::TableNextColumn();
      ImGui::TextUnformatted((described.player != 0U)
                                 ? recorder.playerName(described.player).c_str()
                                 : "");
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(KIND_NAMES.at(entry.payload.index()));
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(described.detail.c_str());
    }
  }
  // One blank row lets the final real row remain fully visible when the
  // viewport height isn't an exact multiple of the data-row height.
  ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);
  ImGui::TableNextColumn();
  ImGui::Dummy(ImVec2(0, rowHeight - (2 * ImGui::GetStyle().CellPadding.y)));
  if (scroll_to_end)
  {
    ImGui::SetScrollHereY(1.0F);
    scroll_to_end = false;
  }
  ImGui::EndTable();
}

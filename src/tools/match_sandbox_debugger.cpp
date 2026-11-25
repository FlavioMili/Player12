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
#include <functional>
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
#include "tools/match_sandbox_text.h"

namespace
{
using Recorder = SandboxRecorder;
using Kind = EngineDebugger::Kind;
constexpr int SECONDS_PER_MINUTE = 60;
constexpr int STEPS_PER_SECOND = 10;
constexpr std::size_t BYTES_PER_KIB = 1024;
constexpr float LOG_FONT_SIZE = 12.0F;
constexpr float HEADER_FONT_SIZE = 14.0F;
constexpr float HORIZONTAL_SPACING = 6.0F;
constexpr float VERTICAL_SPACING = 3.0F;
constexpr float TEAM_FILTER_WIDTH = 110.0F;
constexpr float PLAYER_FILTER_WIDTH = 190.0F;
constexpr float ROWS_PER_WHEEL_TICK = 3.0F;
constexpr float CURSOR_ALIGNMENT = 0.5F;
constexpr int LOG_COLUMN_COUNT = 6;
constexpr int SUMMARY_TAB_INDEX = 5;

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
      static_cast<int>(std::floor(std::max(0.0F, minute) * SECONDS_PER_MINUTE));
  return std::format("{:02}:{:02}", seconds / SECONDS_PER_MINUTE,
                     seconds % SECONDS_PER_MINUTE);
}

[[nodiscard]] const char* teamName(int home)
{
  if (home < 0)
  {
    return "";
  }
  if (home > 0)
  {
    return "Home";
  }
  return "Away";
}

/** One table row: team (-1 none), player and the detail text. */
struct Row
{
  int home = -1;
  PlayerID player = 0;
  std::string detail;
};

class RowDescriber
{
 public:
  explicit RowDescriber(const Recorder& source) : recorder(source) {}

  [[nodiscard]] static Row operator()(const Recorder::PhaseChange& item)
  {
    return {.home = static_cast<int>(item.homeTeam),
            .player = 0,
            .detail = std::format("{} -> {}", SandboxNames::phase(item.from),
                                  SandboxNames::phase(item.to))};
  }

  [[nodiscard]] Row operator()(const Recorder::JobChange& item) const
  {
    std::string text = std::format(
        "{}: {}", SandboxNames::job(item.job),
        (item.player != 0U) ? name(item.player) : std::string("nobody"));
    if (item.previous != 0U)
    {
      text += std::format(" (was {})", name(item.previous));
    }
    if (item.player != 0U && (item.job == Recorder::Job::PRESSER_1 ||
                              item.job == Recorder::Job::PRESSER_2))
    {
      text += std::format(" · arrives in {:.2f} s", item.score);
    }
    else if (item.player != 0U && (item.job == Recorder::Job::RUNNER_1 ||
                                   item.job == Recorder::Job::RUNNER_2 ||
                                   item.job == Recorder::Job::RUNNER_3))
    {
      text += std::format(" · run priority {:.2f}", item.score);
    }
    return {.home = static_cast<int>(item.homeTeam),
            .player = item.player,
            .detail = text};
  }

  [[nodiscard]] static Row operator()(const Recorder::IntentChange& item)
  {
    std::string text = std::format("{} -> {}", SandboxNames::intent(item.from),
                                   SandboxNames::intent(item.to));
    if (item.job != Recorder::Job::NONE)
    {
      text += std::format("  [{}]", SandboxNames::job(item.job));
    }
    return {.home = static_cast<int>(item.homeTeam),
            .player = item.player,
            .detail = text};
  }

  [[nodiscard]] Row operator()(const MatchDecisionRecord& item) const
  {
    std::string text =
        std::format("{}  ·  pass {}  shot {}  carry {}  shield {}",
                    SandboxNames::choice(item.chosen), score(item.pass),
                    score(item.shot), score(item.carry), score(item.shield));
    if (item.chosen == ScenarioAction::PASS && item.bestReceiver != 0U)
    {
      text += std::format("  ·  to {} ({})", name(item.bestReceiver),
                          SandboxNames::passIntent(item.bestPassIntent));
    }
    if (item.chosenWithoutNoise != item.chosen)
    {
      text += std::format("  ·  NOISE FLIPPED (scores alone: {})",
                          SandboxNames::choice(item.chosenWithoutNoise));
    }
    return {.home = static_cast<int>(item.homeTeam),
            .player = item.player,
            .detail = text};
  }

  [[nodiscard]] Row operator()(const MatchActionRecord& item) const
  {
    std::string text = SandboxNames::action(item.kind);
    if (item.header)
    {
      text += " (header)";
    }
    switch (item.kind)
    {
      case MatchActionKind::PASS:
        text += std::format(" {}{} to {} · {:.1f} m · est. completion {:.2f}",
                            SandboxNames::passIntent(item.passIntent),
                            item.lofted ? ", lofted" : "", name(item.target),
                            metres(item.from, item.to), item.estimate);
        break;
      case MatchActionKind::SHOT:
        text +=
            std::format(" · xG {:.2f} · {:.1f} m/s", item.estimate, item.speed);
        break;
      case MatchActionKind::CLEARANCE:
      case MatchActionKind::KNOCK_DOWN:
        text += std::format(" · {:.1f} m", metres(item.from, item.to));
        if (item.target != 0U)
        {
          text += std::format(" toward {}", name(item.target));
        }
        break;
      case MatchActionKind::TAKE_ON:
        text += std::format(" vs {} · chance {:.2f} · {}", name(item.target),
                            item.estimate, SandboxNames::duel(item.result));
        break;
      case MatchActionKind::TACKLE:
        text += std::format("{} on {} · win {:.2f} · foul risk {:.2f} · {}",
                            item.lofted ? " (sliding)" : "", name(item.target),
                            item.estimate, item.foulPropensity,
                            SandboxNames::duel(item.result));
        break;
    }
    return {.home = static_cast<int>(item.homeTeam),
            .player = item.player,
            .detail = text};
  }

  [[nodiscard]] Row operator()(const Recorder::PossessionChange& item) const
  {
    std::string text;
    if (item.from == 0)
    {
      text = std::format("ball to {}", name(item.to));
    }
    else if (item.fromHome == item.toHome)
    {
      text = std::format("received from {}", name(item.from));
    }
    else
    {
      text = std::format("won from {}", name(item.from));
    }
    if (item.afterAction && *item.afterAction < recorder.get().entries().size())
    {
      const auto& cause = recorder.get().entries()[*item.afterAction].payload;
      if (const auto* action = std::get_if<MatchActionRecord>(&cause))
      {
        text += std::format("  (after {} by {})",
                            SandboxNames::action(action->kind),
                            name(action->player));
      }
    }
    return {.home = static_cast<int>(item.toHome),
            .player = item.to,
            .detail = text};
  }

  [[nodiscard]] static Row operator()(const Recorder::KeeperChange& item)
  {
    return {
        .home = static_cast<int>(item.homeTeam),
        .player = 0,
        .detail = std::format("keeper {} -> {}", goalkeeperStateName(item.from),
                              goalkeeperStateName(item.to))};
  }

  [[nodiscard]] Row operator()(const Recorder::EventEntry& item) const
  {
    const MatchEvent& event = recorder.get().events()[item.index];
    return {.home = event.hasTeam ? static_cast<int>(event.isHomeTeam) : -1,
            .player = event.primaryPlayerId,
            .detail = std::format("{}  {}", matchEventTypeName(event.type),
                                  event.description)};
  }

 private:
  [[nodiscard]] std::string name(PlayerID id) const
  {
    return recorder.get().playerName(id);
  }
  std::reference_wrapper<const Recorder> recorder;
};

Row describeRow(const Recorder& recorder, const Recorder::Entry& entry)
{
  return std::visit(RowDescriber(recorder), entry.payload);
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
    SandboxText::wrappedText(
        std::format(
            "Brains: {} · Bodies: {} · Handshake: {} · Home advantage: {:.2f}",
            brainPairName({shown->getBrain(true), shown->getBrain(false)}),
            bodyPairName({shown->getBody(true), shown->getBody(false)}),
            handshake,
            static_cast<double>(shown->getMatchContext().homeAdvantageScale))
            .c_str());
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
      ImGui::PushFont(SandboxAnalysisUI::regular(), LOG_FONT_SIZE);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                          ImVec2(HORIZONTAL_SPACING * Theme::scale(),
                                 VERTICAL_SPACING * Theme::scale()));
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                          ImVec2(HORIZONTAL_SPACING * Theme::scale(),
                                 VERTICAL_SPACING * Theme::scale()));
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
    case SUMMARY_TAB_INDEX:
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
      stepBy(STEPS_PER_SECOND);
    }
    ImGui::EndDisabled();
    SandboxAnalysisUI::next();
    SandboxText::disabledText(std::format("{}  ·  tick {}",
                                          clock(engine->getMatchTimeMinutes()),
                                          engine->getSimulatedSteps())
                                  .c_str());
  }
  else
  {
    SandboxText::disabledText("Match over: the log of the last match.");
  }
  const auto guard = recorder.lock();
  SandboxText::wrappedText(
      std::format("  ·  {} entries  ·  ~{} KB", recorder.entries().size(),
                  recorder.approximateBytes() / BYTES_PER_KIB)
          .c_str());
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
  ImGui::SetNextItemWidth(TEAM_FILTER_WIDTH * Theme::scale());
  constexpr std::array<const char*, 3> TEAMS{"Both teams", "Home", "Away"};
  if (SandboxPageUI::combo("##team", &team_filter, TEAMS.data(),
                           static_cast<int>(TEAMS.size())))
  {
    filters_changed = true;
  }
  SandboxAnalysisUI::next(PLAYER_FILTER_WIDTH);

  // Every player seen in the match, by name.
  std::map<std::string, PlayerID> players;
  {
    const auto guard = recorder.lock();
    for (const auto& [id, name] : recorder.knownPlayers())
    {
      players.emplace(name, id);
    }
  }
  ImGui::SetNextItemWidth(PLAYER_FILTER_WIDTH * Theme::scale());
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
    SandboxText::tooltipText(
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

EngineDebugger::LogView EngineDebugger::prepareLogView(
    const SandboxRecorder& recorder, std::optional<std::uint64_t> cursor)
{
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
      wheel_rows -= io.MouseWheel * ROWS_PER_WHEEL_TICK;
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
  return {.cursorRow = cursorRow,
          .scrollToCursor = scrollToCursor,
          .rowHeight = rowHeight};
}

void EngineDebugger::renderLog(const SandboxRecorder& recorder,
                               std::optional<std::uint64_t> cursor,
                               DebugInspector& inspector)
{
  const auto guard = recorder.lock();
  refreshVisible(recorder);
  const auto& entries = recorder.entries();

  const LogView view = prepareLogView(recorder, cursor);
  const auto& cursorRow = view.cursorRow;
  const bool scrollToCursor = view.scrollToCursor;
  const float rowHeight = view.rowHeight;
  constexpr ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
      ImGuiTableFlags_ScrollX | ImGuiTableFlags_BordersInnerV |
      ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
  if (!ImGui::BeginTable("##log", LOG_COLUMN_COUNT, flags, ImVec2(0.0F, 0.0F)))
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
  ImGui::PushFont(SandboxAnalysisUI::bold(), HEADER_FONT_SIZE);
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
        SandboxText::tooltipText("Click to rewind the review to this moment.");
      }
      ImGui::PopID();
      if (scrollToCursor && cursorRow == row)
      {
        ImGui::SetScrollHereY(CURSOR_ALIGNMENT);
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(std::to_string(entry.tick).c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(teamName(described.home));
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

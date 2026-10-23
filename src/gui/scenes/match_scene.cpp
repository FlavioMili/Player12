// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "match_scene.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <future>
#include <optional>
#include <string_view>
#include <utility>

#include "database/datagenerator.h"
#include "database/gamedata.h"
#include "global/logger.h"
#include "global/runtime_paths.h"
#include "gui/gui_view.h"
#include "gui/input_actions.h"
#include "gui/player_ui.h"
#include "gui/render/match_kit_colors.h"
#include "gui/render/match_renderer_2d.h"
#include "gui/render/match_renderer_3d.h"
#include "gui/scenes/lineup_scene.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/match_report_scene.h"
#include "gui/scenes/match_shouts_bar.h"
#include "gui/view_models/competition_view.h"
#include "gui/view_models/match_clock.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/injury.h"
#include "model/match.h"
#include "model/match_rules.h"
#include "model/role_utils.h"
#include "model/settings_manager.h"
#include "model/team.h"
#include "model/world_rng.h"

namespace
{
template <std::size_t Size, class... Args>
void formatBuffer(std::array<char, Size>& buffer,
                  std::format_string<Args...> format, Args&&... args)
{
  static_assert(Size > 0);
  const auto result = std::format_to_n(buffer.data(), Size - 1, format,
                                       std::forward<Args>(args)...);
  *result.out = '\0';
}

std::optional<std::uint32_t> configuredMatchSeed()
{
  const char* configuredSeed = std::getenv("FM_MATCH_SEED");
  if (!configuredSeed || !*configuredSeed) return std::nullopt;

  const std::string_view seedText(configuredSeed);
  std::uint32_t seed = 0;
  const auto [end, error] =
      std::from_chars(seedText.data(), seedText.data() + seedText.size(), seed);
  if (error != std::errc{} || end != seedText.data() + seedText.size())
    return std::nullopt;
  return seed;
}

// The last chosen presentation and substitution policy carry over to the
// next match this session.
MatchViewMode lastViewMode = MatchViewMode::PITCH_2D;
MatchCameraMode lastCameraMode = MatchCameraMode::BROADCAST;
float lastPlaybackSpeed = MatchTuning::Playback::DEFAULT_SPEED;
// Highlights at real time is the comfortable default; the full match stays
// one click away.
bool lastHighlightsOnly = true;
bool lastSidePanelsHidden = false;

/// FM_MATCH_VIEW=3d|2d forces the initial view (profiling, screenshots).
std::optional<MatchViewMode> configuredMatchView()
{
  const char* configuredView = std::getenv("FM_MATCH_VIEW");
  if (!configuredView) return std::nullopt;
  const std::string_view view(configuredView);
  if (view == "3d") return MatchViewMode::BROADCAST_3D;
  if (view == "2d") return MatchViewMode::PITCH_2D;
  return std::nullopt;
}

float scaled(float value) { return value * Theme::scale(); }

/** Label of an action's current key ("B", "Esc"). */
std::string keyLabel(std::string_view id)
{
  const auto action = Input::registry().find(id);
  return action ? Input::registry().label(*action) : std::string("-");
}

/** Mouse and keyboard help of the 3D cameras with the bound keys (built
 * only while its tooltip shows). */
std::string cameraHelpText()
{
  namespace Ids = Input::Ids;
  return fmt::sprintf(
      LOC("MATCH_CAMERA_HELP"), keyLabel(Ids::CAMERA_FOLLOW_BALL).c_str(),
      keyLabel(Ids::CAMERA_RESET).c_str(),
      keyLabel(Ids::CAMERA_BROADCAST).c_str(),
      keyLabel(Ids::CAMERA_TACTICAL).c_str(), keyLabel(Ids::CAMERA_END).c_str(),
      keyLabel(Ids::CAMERA_PLAYER).c_str(), keyLabel(Ids::CAMERA_FREE).c_str(),
      keyLabel(Ids::CAMERA_DIRECTOR).c_str());
}

/** Kick-offs from 10:00 to 17:29 are played in daylight. */
constexpr int DAY_LOOK_FROM_MINUTES = 10 * 60;
constexpr int DAY_LOOK_UNTIL_MINUTES = 17 * 60 + 30;

/** Share of the focus view the shouts chip may take across. */
constexpr float FOCUS_SHOUTS_WIDTH_SHARE = 0.33f;

ImVec4 eventColor(MatchEventType type)
{
  using enum MatchEventType;
  const Theme::Palette& palette = Theme::palette();
  switch (type)
  {
    case GOAL:
      return palette.positive;
    case OWN_GOAL:
    case RED_CARD:
    case SECOND_YELLOW:
    case INJURY:
      return palette.negative;
    case YELLOW_CARD:
    case PENALTY:
    case PENALTY_MISSED:
    case PENALTY_SHOOTOUT:
      return palette.warning;
    case SUBSTITUTION:
    case ADDED_TIME:
    case KICK_OFF:
    case HALF_TIME:
    case SECOND_HALF:
    case FULL_TIME:
      return palette.info;
    default:
      return palette.text;
  }
}

/// Chances, saves, goals, cards, injuries, changes and the phases of the
/// match; routine restarts (throw-ins, goal kicks, free kicks...) are not.
bool isKeyEvent(MatchEventType type)
{
  using enum MatchEventType;
  switch (type)
  {
    case KICK_OFF:
    case GOAL:
    case OWN_GOAL:
    case SHOT:
    case SAVE:
    case SHOT_BLOCKED:
    case SHOT_OFF_TARGET:
    case WOODWORK:
    case YELLOW_CARD:
    case SECOND_YELLOW:
    case RED_CARD:
    case INJURY:
    case SUBSTITUTION:
    case PENALTY:
    case PENALTY_MISSED:
    case PENALTY_SHOOTOUT:
    case ADDED_TIME:
    case HALF_TIME:
    case SECOND_HALF:
    case FULL_TIME:
      return true;
    default:
      return false;
  }
}

/// Label under the clock: the half being played (extra time included), the
/// breaks and the shootout.
const char* periodKey(const MatchEngine& engine)
{
  using enum MatchState;
  const int period = engine.getPeriod();
  switch (engine.getState())
  {
    case FULL_TIME:
      return "MATCH_PERIOD_FULL";
    case PENALTY_SHOOTOUT:
      return "MATCH_PERIOD_SHOOTOUT";
    case HALF_TIME:
      if (period == 1) return "MATCH_PERIOD_HALF";
      if (period == 2) return "MATCH_PERIOD_EXTRA_BREAK";
      return "MATCH_PERIOD_EXTRA_HALF";
    default:
      if (period >= 4) return "MATCH_PERIOD_EXTRA_SECOND";
      if (period == 3) return "MATCH_PERIOD_EXTRA_FIRST";
      if (period == 2) return "MATCH_PERIOD_SECOND";
      return "MATCH_PERIOD_FIRST";
  }
}

/// Small glyph in front of an event: ball for goals, cards, arrows for
/// changes, a glove-like diamond for saves, rings for chances.
void drawEventIcon(ImDrawList* drawList, ImVec2 centre, float size,
                   MatchEventType type)
{
  const Theme::Palette& palette = Theme::palette();
  const float radius = size * 0.4f;
  const ImU32 yellow = IM_COL32(245, 200, 40, 255);
  const ImU32 red = IM_COL32(220, 50, 50, 255);
  const auto card = [&](float offset, ImU32 color)
  {
    drawList->AddRectFilled(
        ImVec2(centre.x - size * 0.22f + offset, centre.y - size * 0.4f),
        ImVec2(centre.x + size * 0.22f + offset, centre.y + size * 0.4f), color,
        1.0f);
  };
  switch (type)
  {
    case MatchEventType::GOAL:
      drawList->AddCircleFilled(centre, radius, Theme::toU32(palette.text));
      drawList->AddCircleFilled(centre, radius * 0.35f,
                                Theme::toU32(palette.background));
      break;
    case MatchEventType::OWN_GOAL:
      drawList->AddCircleFilled(centre, radius, Theme::toU32(palette.negative));
      break;
    case MatchEventType::SHOT:
    case MatchEventType::SHOT_BLOCKED:
      drawList->AddCircle(centre, radius, Theme::toU32(palette.muted), 0, 1.5f);
      break;
    case MatchEventType::SHOT_OFF_TARGET:
    case MatchEventType::WOODWORK:
      drawList->AddCircle(centre, radius, Theme::toU32(palette.warning), 0,
                          1.5f);
      break;
    case MatchEventType::SAVE:
      drawList->AddQuadFilled(ImVec2(centre.x, centre.y - radius),
                              ImVec2(centre.x + radius, centre.y),
                              ImVec2(centre.x, centre.y + radius),
                              ImVec2(centre.x - radius, centre.y),
                              Theme::toU32(palette.info));
      break;
    case MatchEventType::YELLOW_CARD:
      card(0.0f, yellow);
      break;
    case MatchEventType::SECOND_YELLOW:
      card(-size * 0.12f, yellow);
      card(size * 0.12f, red);
      break;
    case MatchEventType::RED_CARD:
      card(0.0f, red);
      break;
    case MatchEventType::INJURY:
      drawList->AddRectFilled(
          ImVec2(centre.x - radius, centre.y - radius * 0.3f),
          ImVec2(centre.x + radius, centre.y + radius * 0.3f),
          Theme::toU32(palette.negative));
      drawList->AddRectFilled(
          ImVec2(centre.x - radius * 0.3f, centre.y - radius),
          ImVec2(centre.x + radius * 0.3f, centre.y + radius),
          Theme::toU32(palette.negative));
      break;
    case MatchEventType::SUBSTITUTION:
      drawList->AddTriangleFilled(
          ImVec2(centre.x - radius, centre.y + radius * 0.2f),
          ImVec2(centre.x - radius * 0.1f, centre.y + radius * 0.2f),
          ImVec2(centre.x - radius * 0.55f, centre.y - radius),
          Theme::toU32(palette.positive));
      drawList->AddTriangleFilled(
          ImVec2(centre.x + radius * 0.1f, centre.y - radius * 0.2f),
          ImVec2(centre.x + radius, centre.y - radius * 0.2f),
          ImVec2(centre.x + radius * 0.55f, centre.y + radius),
          Theme::toU32(palette.negative));
      break;
    case MatchEventType::PENALTY:
    case MatchEventType::PENALTY_MISSED:
      drawList->AddRect(ImVec2(centre.x - radius, centre.y - radius * 0.7f),
                        ImVec2(centre.x + radius, centre.y + radius * 0.7f),
                        Theme::toU32(palette.warning), 1.0f, 0, 1.5f);
      drawList->AddCircleFilled(centre, radius * 0.25f,
                                Theme::toU32(palette.warning));
      break;
    default:
      drawList->AddCircleFilled(
          centre, radius * 0.35f,
          Theme::toU32(isKeyEvent(type) ? palette.info : palette.faint));
      break;
  }
}

/// Home value, centred label and away value over a split bar (the match
/// report's comparison style).
void comparisonRow(const char* label, float home, float away,
                   const char* homeText, const char* awayText)
{
  const Theme::Palette& palette = Theme::palette();
  const float width = ImGui::GetContentRegionAvail().x;
  const float startX = ImGui::GetCursorPosX();
  ImGui::TextUnformatted(homeText);
  const float labelWidth = ImGui::CalcTextSize(label).x;
  ImGui::SameLine(startX + (width - labelWidth) * 0.5f);
  ImGui::TextColored(palette.muted, "%s", label);
  ImGui::SameLine(startX + width - ImGui::CalcTextSize(awayText).x);
  ImGui::TextUnformatted(awayText);

  const ImVec2 barStart = ImGui::GetCursorScreenPos();
  const float barHeight = scaled(4.0f);
  const float total = home + away;
  const float split = total > 0.0f ? home / total : 0.5f;
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(
      barStart,
      ImVec2(barStart.x + width * split - 1.0f, barStart.y + barHeight),
      Theme::toU32(palette.accent), 2.0f);
  drawList->AddRectFilled(ImVec2(barStart.x + width * split + 1.0f, barStart.y),
                          ImVec2(barStart.x + width, barStart.y + barHeight),
                          Theme::toU32(palette.info), 2.0f);
  ImGui::Dummy(ImVec2(width, barHeight + scaled(2.0f)));
}

/// Comparison row of two counts.
void countRow(const char* label, int home, int away)
{
  std::array<char, 16> homeText{};
  std::array<char, 16> awayText{};
  formatBuffer(homeText, "{}", home);
  formatBuffer(awayText, "{}", away);
  comparisonRow(label, static_cast<float>(home), static_cast<float>(away),
                homeText.data(), awayText.data());
}

#ifdef DEBUG
const char* passIntentLabel(PassIntent intent)
{
  using enum PassIntent;
  switch (intent)
  {
    case RECYCLE:
      return "Recycle possession";
    case PROGRESSIVE:
      return "Progressive pass";
    case THROUGH_BALL:
      return "Through ball";
    case CROSS:
      return "Cross";
    case CUTBACK:
      return "Cutback";
    case SWITCH_PLAY:
      return "Switch play";
    case PRESSURE_RELEASE:
      return "Escape pressure";
    case SET_PIECE:
      return "Set piece";
  }
  return "Unknown";
}

const char* teamPhaseLabel(TeamPhase phase)
{
  using enum TeamPhase;
  switch (phase)
  {
    case STOPPAGE:
      return "Stoppage";
    case SET_PIECE:
      return "Set piece";
    case DEFENSIVE_BLOCK:
      return "Defensive block";
    case DEFENSIVE_TRANSITION:
      return "Defensive transition";
    case ATTACKING_TRANSITION:
      return "Attacking transition";
    case POSSESSION:
      return "Possession";
    case FINAL_THIRD:
      return "Final third";
  }
  return "Unknown";
}
#endif
}  // namespace

MatchScene::MatchScene(GUIView* guiView_ptr, uint16_t home_id, uint16_t away_id)
    : GUIScene(guiView_ptr), home_team_id(home_id), away_team_id(away_id)
{
}

SceneID MatchScene::getID() const { return SceneID::MATCH; }

void MatchScene::onEnter()
{
  const auto startedAt = std::chrono::steady_clock::now();
  GameController& controller = guiView->getController();
  auto home_opt = controller.getTeamById(home_team_id);
  if (auto away_opt = controller.getTeamById(away_team_id);
      home_opt && away_opt)
  {
    home_name = home_opt->get().getName();
    away_name = away_opt->get().getName();

    if (const auto managed = controller.getManagedTeam())
    {
      const TeamID managedId = managed->get().getId();
      if (managedId == home_team_id)
        managed_is_home = true;
      else if (managedId == away_team_id)
        managed_is_home = false;
    }
    if (const Game* game = controller.getGame())
    {
      if (const Match* fixture = game->getCalendar().findMatch(
              controller.getCurrentDate(), home_team_id, away_team_id))
      {
        fixture_type = fixture->getMatchType();
        fixture_when =
            Format::matchDay(fixture->getDate(), fixture->getKickoff());
        // Day kick-offs (10:00 to 17:29) are shown in daylight.
        kickoff_minutes = fixture->getKickoff();
        day_look = kickoff_minutes >= DAY_LOOK_FROM_MINUTES &&
                   kickoff_minutes < DAY_LOOK_UNTIL_MINUTES;
      }
    }

    renderer_2d = std::make_unique<MatchRenderer2D>();
    renderer_3d = std::make_unique<MatchRenderer3D>();
    if (const auto view = configuredMatchView()) lastViewMode = *view;
    view_mode = lastViewMode;
    camera_mode = lastCameraMode;
    side_panels_hidden = lastSidePanelsHidden;

    refreshLineupProblems();
    if (!lineup_problems.empty() && controller.getAssistantFixesLineup())
      applyLineupFix();
    if (lineup_problems.empty()) startMatch();
  }
  scene_entry_milliseconds = std::chrono::duration<float, std::milli>(
                                 std::chrono::steady_clock::now() - startedAt)
                                 .count();
  Logger::info(std::format("Match scene initialized in {:.2f} ms",
                           scene_entry_milliseconds));
  // A slow start is a developer diagnostic: logged, not shown to players.
  if (scene_entry_milliseconds >=
      MatchSceneTuning::Performance::SLOW_SCENE_ENTRY_MILLISECONDS)
    Logger::warn(std::format("Slow match initialization: {:.1f} ms",
                             scene_entry_milliseconds));
}

void MatchScene::refreshLineupProblems()
{
  lineup_problems.clear();
  // Only a real fixture of the managed club has eligibility rules; tooling
  // matches (profiling, tests) play the selections as they are.
  if (!managed_is_home.has_value() || !fixture_type.has_value()) return;
  const GameController& controller = guiView->getController();
  const auto data = controller.getGameData();
  const TeamID managedId = *managed_is_home ? home_team_id : away_team_id;
  const auto fixes = controller.previewLineupFix(managedId, *fixture_type);
  const auto nameOf = [&data](PlayerID id)
  {
    const auto player = data && id != 0 ? data->getPlayer(id) : std::nullopt;
    return player ? player->get().getName() : std::string();
  };
  for (const PlayerID id :
       controller.getIneligibleSelections(managedId, *fixture_type))
  {
    LineupProblem problem{id, {}, {}, {}};
    if (const auto fix = std::ranges::find_if(
            fixes, [id](const auto& change) { return change.first == id; });
        fix != fixes.end())
      problem.replacement = nameOf(fix->second);
    if (const auto player = data ? data->getPlayer(id) : std::nullopt; player)
    {
      const Player& selected = player->get();
      problem.name = selected.getName();
      const PlayerDynamics& dynamics = selected.getDynamics();
      if (!selected.isAvailable())
      {
        const int days = dynamics.injury_days;
        problem.reason =
            fmt::sprintf(Format::plural("MATCH_LINEUP_INJURED", days),
                         LOC(InjuryModel::nameKey(dynamics.injury)), days);
      }
      else
      {
        const int matches = controller.getSuspensionMatches(id, *fixture_type);
        problem.reason = fmt::sprintf(
            Format::plural("MATCH_LINEUP_SUSPENDED", matches), matches);
      }
    }
    lineup_problems.push_back(std::move(problem));
  }
}

void MatchScene::applyLineupFix()
{
  GameController& controller = guiView->getController();
  const TeamID managedId = *managed_is_home ? home_team_id : away_team_id;
  const std::vector<LineupProblem> before = lineup_problems;
  controller.autoFixLineup(managedId, *fixture_type);
  refreshLineupProblems();
  // Problems left over are injured starters nobody fit can replace: they
  // play through it once the assistant cannot improve the selection.
  if (!lineup_problems.empty() &&
      !controller.canKickOff(managedId, *fixture_type))
  {
    lineup_status = LOC("MATCH_LINEUP_FIX_FAILED");
    return;
  }
  std::string note;
  const auto add = [&note](std::string_view entry)
  {
    if (!note.empty()) note += "  ·  ";
    note.append(entry);
  };
  for (const LineupProblem& problem : before)
  {
    if (std::ranges::any_of(lineup_problems,
                            [&problem](const LineupProblem& left)
                            { return left.id == problem.id; }))
      continue;
    add(problem.replacement.empty()
            ? fmt::sprintf(LOC("MATCH_LINEUP_LEFT_OUT"), problem.name.c_str())
            : fmt::sprintf(LOC("MATCH_LINEUP_REPLACED"),
                           problem.replacement.c_str(), problem.name.c_str()));
  }
  for (const LineupProblem& problem : lineup_problems)
    add(std::format("{} ({})", problem.name, problem.reason));
  lineup_problems.clear();
  lineup_status.clear();
  pre_match_note = fmt::sprintf(LOC("MATCH_ASSISTANT_FIXED"), note.c_str());
}

void MatchScene::startMatch()
{
  GameController& controller = guiView->getController();
  auto home_opt = controller.getTeamById(home_team_id);
  auto away_opt = controller.getTeamById(away_team_id);
  if (!home_opt || !away_opt) return;
  const Team& home_team = home_opt->get();
  const Team& away_team = away_opt->get();

  // Deterministic per save and fixture (FM_MATCH_SEED overrides it), so a
  // reloaded save replays the same match until a manual change is made.
  std::uint32_t seed = 0;
  if (const auto configured = configuredMatchSeed(); configured.has_value())
  {
    seed = *configured;
  }
  else
  {
    const Match identity(home_team_id, away_team_id,
                         controller.getCurrentDate(),
                         fixture_type.value_or(MatchType::FRIENDLY));
    seed = static_cast<std::uint32_t>(
        mixHash(controller.getWorldSeed(), identity.getSeed()));
  }
  engine = std::make_unique<MatchEngine>(
      home_team.getLineup(), away_team.getLineup(), home_team.getStrategy(),
      away_team.getStrategy(), controller.getStatsConfig(), seed);
  // Cup ties and continental deciders are played to a winner: extra time,
  // then penalties (the first leg counts in a second leg).
  if (const auto rules = controller.getKnockoutRules(
          controller.getCurrentDate(), home_team_id, away_team_id))
    engine->setKnockout(*rules);
  engine->setTeamNames(home_team.getName(), away_team.getName());
  decided_by.clear();
  decided_by_ready = false;
  // Fatigue carried over from recent matches and training.
  MatchdaySquad::carryCondition(*engine, home_team.getLineup());
  MatchdaySquad::carryCondition(*engine, away_team.getLineup());
  // The medical staff's minute limits: the assistant follows them when he
  // makes the managed side's changes.
  if (const Game* game = controller.getGame();
      game && managed_is_home.has_value())
    for (const auto& [player, flags] : MatchdaySquad::medicalFlags(
             game->getMedical(),
             *managed_is_home ? home_team.getLineup() : away_team.getLineup()))
      engine->setMedicalFlags(player, flags);
  engine->setTacticalFamiliarity(
      true, controller.getTacticalFamiliarity(home_team_id));
  engine->setTacticalFamiliarity(
      false, controller.getTacticalFamiliarity(away_team_id));
  assistant_substitutions = controller.isDelegated(Duty::Substitutions);
  applySubstitutionPolicy();
  // In-match tactics start from the managed club's plan.
  subs_panel.getPlan().clear();
  if (managed_is_home.has_value())
    tactics_panel.reset(*managed_is_home ? home_team.getStrategy()
                                         : away_team.getStrategy());
  setPlaybackSpeed(lastPlaybackSpeed);
  setHighlightsOnly(lastHighlightsOnly);
  if (!audio) audio = std::make_unique<MatchAudio>();
  match_finished = false;
}

void MatchScene::setPlaybackSpeed(float speed)
{
  match_speed = speed;
  lastPlaybackSpeed = speed;
  if (engine)
  {
    engine->setPlaybackSpeed(speed);
    engine->setHighlightPlaybackSpeed(speed);
  }
}

void MatchScene::setHighlightsOnly(bool enabled)
{
  highlights_only = enabled;
  lastHighlightsOnly = enabled;
  if (engine)
    engine->setPlaybackMode(enabled ? MatchPlaybackMode::HIGHLIGHTS
                                    : MatchPlaybackMode::FULL_MATCH);
}

void MatchScene::applySubstitutionPolicy()
{
  if (!engine) return;
  const bool homeAuto = !managed_is_home.value_or(false) ||
                        (*managed_is_home && assistant_substitutions);
  const bool awayAuto = managed_is_home.value_or(true) ||
                        (!*managed_is_home && assistant_substitutions);
  engine->setAutoSubstitutions(homeAuto, awayAuto);
}

std::optional<TouchlineContext> MatchScene::touchline()
{
  // Nothing touches the engine while a quick result plays it on a worker.
  if (!engine || !managed_is_home.has_value() || quick_result.valid())
    return std::nullopt;
  const bool home = *managed_is_home;
  return TouchlineContext{guiView->getController(),
                          *engine,
                          home,
                          home ? home_team_id : away_team_id,
                          teamColor(home),
                          substitution_status,
                          substitution_refused};
}

bool MatchScene::substitute(PlayerID outgoing, PlayerID incoming)
{
  const auto context = touchline();
  if (!context)
  {
    substitution_status = LOC("SUBSTITUTION_REFUSED_NO_TEAM");
    substitution_refused = true;
    return false;
  }
  // The engine owns the rules (limit, windows, no re-entry); the saved
  // lineup is left untouched by in-match changes.
  return subs_panel.substituteNow(*context, outgoing, incoming);
}

void MatchScene::showSubstitutions(bool show)
{
  if (show == show_substitutions) return;
  if (show)
  {
    // Never over another dialog (the team talk); it can replace tactics.
    if (const bool otherDialog =
            !show_tactics && ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        !touchline() || match_finished || otherDialog)
      return;
    showTactics(false);
    subs_panel.resetSelection();
    substitution_status.clear();
    substitution_refused = false;
    subs_panel.resetPopup();
    if (Touchline::pausesMatch() && !is_paused)
    {
      is_paused = true;
      paused_for_dialog = true;
    }
    show_substitutions = true;
    return;
  }
  subs_panel.requestClose();
}

void MatchScene::showTactics(bool show)
{
  if (show == show_tactics) return;
  if (show)
  {
    if (const bool otherDialog =
            !show_substitutions &&
            ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        !touchline() || match_finished || otherDialog)
      return;
    showSubstitutions(false);
    tactics_panel.resetPopup();
    substitution_status.clear();
    substitution_refused = false;
    if (Touchline::pausesMatch() && !is_paused)
    {
      is_paused = true;
      paused_for_dialog = true;
    }
    show_tactics = true;
    return;
  }
  tactics_panel.requestClose();
}

void MatchScene::dialogClosed()
{
  if (show_substitutions || show_tactics) return;
  if (paused_for_dialog && !match_finished) is_paused = false;
  paused_for_dialog = false;
}

bool MatchScene::finishMatch()
{
  GameController& controller = guiView->getController();
  if (!engine ||
      !controller.setMatchResult(controller.getCurrentDate(), home_team_id,
                                 away_team_id, *engine))
  {
    debug_status = LOC("MATCH_RESULT_FAILED");
    return false;
  }
  // The rest of the match day (every other match, the autosave) runs on the
  // hub's Continue worker so the window stays responsive; the report opens
  // once the day is over.
  const GameDateValue date = controller.getCurrentDate();
  if (auto* hub = dynamic_cast<MainGameScene*>(guiView->getBaseScene()))
  {
    hub->requestPostMatchAdvance(date, home_team_id, away_team_id);
    return true;
  }
  // Without the club hub below (a match opened on its own) the day ends here.
  guiView->navigateTo(std::make_unique<MatchReportScene>(
      guiView, date, home_team_id, away_team_id));
  controller.advanceDay();
  return true;
}

bool MatchScene::quickResult()
{
  if (!engine || match_finished || quick_result.valid()) return false;
  // The rest of a real-time match is tens of thousands of steps: it runs off
  // the UI thread and nothing reads the engine until it is done.
  quick_result = launchBackgroundTask(
      quick_result_worker, [match = engine.get()] { match->simulateToEnd(); });
  return true;
}

std::string MatchScene::clockText() const
{
  using enum MatchState;
  switch (engine->getState())
  {
    case HALF_TIME:
      // The break before extra time comes after the full 90 minutes.
      if (engine->getPeriod() == 2)
        return MatchClock::clockLabel(MatchRules::periodEndMinute(2), 2, false);
      return LOC("MATCH_CLOCK_HALF_TIME");
    case FULL_TIME:
      return LOC("MATCH_CLOCK_FULL_TIME");
    case PENALTY_SHOOTOUT:
      return std::format("{}-{}", engine->getShootoutScore(true),
                         engine->getShootoutScore(false));
    default:
      return MatchClock::clockLabel(engine->getMatchTimeMinutes(),
                                    engine->getPeriod(),
                                    engine->isInAddedTime());
  }
}

ImU32 MatchScene::teamColor(bool home) const
{
  // The strips both views draw (clash fallback included), so the swatches
  // and markers always match the players on the pitch.
  const MatchKits kits = chooseMatchKits(home_team_id, away_team_id);
  return home ? kits.home.shirt : kits.away.shirt;
}

void MatchScene::update(float deltaTime)
{
  frame_seconds = deltaTime;
  skip_indicator_seconds = std::max(0.0f, skip_indicator_seconds - deltaTime);
  if (quick_result.valid())
  {
    if (quick_result.wait_for(std::chrono::seconds(0)) ==
        std::future_status::ready)
    {
      quick_result.get();
      match_finished = true;
      finishMatch();
    }
    return;
  }
  bool skipped = false;
  // A break reached outside this update (e.g. the engine moved on by
  // itself) is caught before any playback can skip across it.
  pauseAtBreak();
  // Play mode: the pad and keys become this step's input first.
  updatePlay(deltaTime);
  if (engine && !match_finished && !is_paused)
  {
    const auto startedAt = std::chrono::steady_clock::now();
    // Real time times the chosen speed; highlights skip the quiet spells.
    skipped = engine->advancePlayback(
        skipped_last_update
            ? std::min(deltaTime,
                       MatchSceneTuning::Controls::MAX_DELTA_AFTER_SKIP)
            : deltaTime);
    skipped_last_update = skipped;
    if (skipped)
      skip_indicator_seconds =
          MatchSceneTuning::Controls::SKIP_INDICATOR_SECONDS;
    last_update_milliseconds = std::chrono::duration<float, std::milli>(
                                   std::chrono::steady_clock::now() - startedAt)
                                   .count();
    maximum_update_milliseconds =
        std::max(maximum_update_milliseconds, last_update_milliseconds);
    if (last_update_milliseconds >=
        MatchSceneTuning::Performance::SLOW_UPDATE_MILLISECONDS)
    {
      ++slow_update_count;
    }
  }
  // Full time ends the match even while it is paused (it may have been
  // played on by a quick look-ahead or the engine directly).
  if (engine && !match_finished && engine->getState() == MatchState::FULL_TIME)
  {
    match_finished = true;
    is_paused = false;
  }
  // A break entered during this update (see pauseAtBreak).
  pauseAtBreak();
  // Confirmed substitutions are made as soon as play stops.
  if (const auto context = touchline()) subs_panel.update(*context);
  if (audio && engine)
  {
    audio->setLevels(
        MatchAudio::levelsFrom(SettingsManager::instance()->get()));
    audio->update(
        deltaTime,
        captureMatchAudioFrame(*engine, match_speed, is_paused, skipped),
        engine->getEvents());
  }
}

void MatchScene::pauseAtBreak()
{
  // The managed club's match stops at every break, whatever the speed or
  // mode, until the manager resumes it (talk and analysis open meanwhile).
  if (!engine) return;
  const MatchState state = engine->getState();
  const bool breakNow =
      state == MatchState::HALF_TIME || state == MatchState::PENALTY_SHOOTOUT;
  if (breakNow && !in_break && managed_is_home.has_value() && !match_finished &&
      SettingsManager::instance()->get().pause_at_breaks)
  {
    is_paused = true;
    paused_for_dialog = false;
  }
  in_break = breakNow;
}

void MatchScene::setViewMode(MatchViewMode mode)
{
  view_mode = mode;
  lastViewMode = mode;
}

void MatchScene::setCameraMode(MatchCameraMode mode)
{
  camera_mode = mode;
  lastCameraMode = mode;
  // A preset ends ball following, so the next drag starts from its pose.
  if (mode != MatchCameraMode::FREE) free_follow_ball = false;
  setViewMode(MatchViewMode::BROADCAST_3D);
}

void MatchScene::setPitchFocus(bool enabled) { pitch_focus = enabled; }

void MatchScene::setSidePanelsHidden(bool hidden)
{
  side_panels_hidden = hidden;
  lastSidePanelsHidden = hidden;
}

void MatchScene::takeFreeCamera()
{
  // A drag is not a choice of default camera: the next match starts with
  // the last preset picked explicitly.
  camera_mode = MatchCameraMode::FREE;
  setViewMode(MatchViewMode::BROADCAST_3D);
}

void MatchScene::setFreeFollowBall(bool follow)
{
  free_follow_ball = follow;
  if (follow) takeFreeCamera();
}

void MatchScene::toggleWindowFullscreen()
{
  SDL_Window* window = guiView->getWindow();
  if (!window) return;
  const bool fullscreen =
      (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
  SDL_SetWindowFullscreen(window, !fullscreen);
}

void MatchScene::handleEvent(const SDL_Event& event)
{
  using enum MatchViewMode;
  // Key releases always reach the play controls so nothing sticks. While the
  // user is in control the PLAY actions drive the pitch and the manager's
  // MATCH keys are not listened to (the pause menu has those functions); a
  // dialog keeps the keyboard for itself.
  play.handleEvent(event);
  if (play.isActive()) return;
  if (event.type != SDL_EVENT_KEY_DOWN) return;
  if (!event.key.repeat && !ImGui::GetIO().WantTextInput)
  {
    namespace Ids = Input::Ids;
    const Input::ActionRegistry& keys = Input::registry();
    const SDL_KeyboardEvent& key = event.key;
    for (std::size_t index = 0; index < Ids::SHOUT_COUNT; ++index)
    {
      if (!keys.matches(Ids::shout(index), key)) continue;
      if (const auto context = touchline(); context && !match_finished)
        MatchShoutsBar::shout(*context, index);
      return;
    }
    // Substitutions and tactics open (and close) from the keyboard.
    if (keys.matches(Ids::MATCH_SUBSTITUTIONS, key))
    {
      showSubstitutions(!show_substitutions);
      return;
    }
    if (keys.matches(Ids::MATCH_TACTICS, key))
    {
      showTactics(!show_tactics);
      return;
    }
    if (keys.matches(Ids::CAMERA_VIEW_TOGGLE, key))
    {
      setViewMode(view_mode == PITCH_2D ? BROADCAST_3D : PITCH_2D);
      return;
    }
    static constexpr std::array<std::pair<std::string_view, MatchCameraMode>, 6>
        CAMERAS{{{Ids::CAMERA_BROADCAST, MatchCameraMode::BROADCAST},
                 {Ids::CAMERA_TACTICAL, MatchCameraMode::TACTICAL},
                 {Ids::CAMERA_END, MatchCameraMode::END},
                 {Ids::CAMERA_PLAYER, MatchCameraMode::PLAYER_FOLLOW},
                 {Ids::CAMERA_FREE, MatchCameraMode::FREE},
                 {Ids::CAMERA_DIRECTOR, MatchCameraMode::DIRECTOR}}};
    for (const auto& [id, mode] : CAMERAS)
    {
      if (!keys.matches(id, key)) continue;
      setCameraMode(mode);
      return;
    }
    if (keys.matches(Ids::CAMERA_FOLLOW_BALL, key))
    {
      setFreeFollowBall(!free_follow_ball ||
                        camera_mode != MatchCameraMode::FREE);
      return;
    }
    if (keys.matches(Ids::CAMERA_RESET, key))
    {
      if (view_mode == BROADCAST_3D)
      {
        takeFreeCamera();
        pending_camera_input.reset = true;
      }
      return;
    }
    if (keys.matches(Ids::MATCH_PITCH_FOCUS, key))
    {
      setPitchFocus(!pitch_focus);
      return;
    }
    if (keys.matches(Ids::MATCH_BACK, key))
    {
      // Open popups (substitutions) take Esc themselves; then the
      // analysis panel closes, then pitch focus ends.
      if (ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
      if (analysis_panel.isOpen())
        analysis_panel.close();
      else if (pitch_focus)
        setPitchFocus(false);
      return;
    }
    if (keys.matches(Ids::MATCH_FULLSCREEN, key))
    {
      toggleWindowFullscreen();
      return;
    }
    if (keys.matches(Ids::MATCH_MUTE, key))
    {
      // Mute toggle, remembered like the other audio settings.
      Settings& settings = SettingsManager::instance()->get();
      settings.audio_muted = !settings.audio_muted;
      SettingsManager::instance()->save();
      return;
    }
    if (const bool faster = keys.matches(Ids::MATCH_FASTER, key);
        faster || keys.matches(Ids::MATCH_SLOWER, key))
    {
      // One step of the speed control (highlights keep their mode).
      const auto& speeds = MatchSceneTuning::Controls::SPEED_STEPS;
      const auto current = std::ranges::find(speeds, match_speed);
      std::size_t index =
          current == speeds.end()
              ? 0
              : static_cast<std::size_t>(current - speeds.begin());
      if (faster && index + 1 < speeds.size()) ++index;
      if (!faster && index > 0) --index;
      setPlaybackSpeed(speeds[index]);
      return;
    }
    if (keys.matches(Ids::MATCH_PAUSE, key))
    {
      // Space belongs to the matchday dialogs while one is open.
      if (engine && !match_finished && !show_substitutions && !show_tactics)
      {
        is_paused = !is_paused;
        paused_for_dialog = false;
      }
      return;
    }
  }
#ifdef DEBUG
  if (event.key.key == SDLK_F10)
  {
    show_ai_debug = !show_ai_debug;
  }
  else if (event.key.key == SDLK_F11)
  {
    exportDebugSnapshot();
  }
#endif
}

#ifdef DEBUG
void MatchScene::exportDebugSnapshot()
{
  if (!engine) return;
  const char* configuredPath = std::getenv("FM_MATCH_SNAPSHOT_PATH");
  const std::string path =
      configuredPath && *configuredPath
          ? configuredPath
          : RuntimePaths::capturePath("match.json").string();
  debug_status = engine->writeDebugSnapshot(path)
                     ? "Snapshot: " + path
                     : "Could not write snapshot: " + path;
}
#endif

void MatchScene::render()
{
  const ImGuiViewport* mainViewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(mainViewport->WorkPos);
  ImGui::SetNextWindowSize(mainViewport->WorkSize);
  // Pitch focus gives the view every pixel of the window.
  const bool focusLayout = pitch_focus && engine && !quick_result.valid();
  ImGui::PushStyleVar(
      ImGuiStyleVar_WindowPadding,
      focusLayout ? ImVec2(0.0f, 0.0f)
                  : ImVec2(scaled(Theme::Space::L), scaled(Theme::Space::M)));
  // Never raised over the analysis panel when the pitch is clicked.
  ImGui::Begin("MatchScene", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoScrollWithMouse |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar();

  if (!engine)
  {
    if (!lineup_problems.empty())
    {
      renderLineupGate();
    }
    else
    {
      UI::emptyState(LOC("MATCH_LOAD_ERROR"), "");
      if (ImGui::Button(LOC("NAV_BACK"))) guiView->popScene();
    }
    ImGui::End();
    return;
  }

  if (quick_result.valid())
  {
    renderQuickResultProgress();
    ImGui::End();
    return;
  }

  if (focusLayout)
  {
    // The dialogs stay available (and their popups submitted) in focus mode.
    if (show_substitutions) renderSubstitutionsModal();
    if (show_tactics) renderTacticsModal();
    renderPlayConfirm();
    renderPlayMenu();
    team_talk.renderForMatch(guiView->getController(), *engine, home_team_id,
                             away_team_id);
    analysis_panel.renderForMatch(
        guiView->getController(), *engine, home_team_id, away_team_id,
        team_talk.isOpen() || show_substitutions || show_tactics);
    renderPitchFocus();
    ImGui::End();
    return;
  }

  renderScoreboard();
  renderControls();
  renderViewControls();
#ifdef DEBUG
  renderDebugLines();
#endif

  if (show_substitutions) renderSubstitutionsModal();
  if (show_tactics) renderTacticsModal();
  renderPlayConfirm();
  renderPlayMenu();
  team_talk.renderForMatch(guiView->getController(), *engine, home_team_id,
                           away_team_id);
  analysis_panel.renderForMatch(
      guiView->getController(), *engine, home_team_id, away_team_id,
      team_talk.isOpen() || show_substitutions || show_tactics);

  // The pitch takes the free space; statistics and events sit beside it on
  // wide windows and below it (tabbed) on narrow ones.
  const ImGuiStyle& style = ImGui::GetStyle();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const bool sidePanel =
      available.x >=
      scaled(MatchSceneTuning::Panel::SIDE_PANEL_MIN_CONTENT_WIDTH);
  if (side_panels_hidden)
  {
    renderPitchArea(ImVec2(
        std::max(1.0f, available.x),
        std::max(scaled(MatchSceneTuning::View::MIN_HEIGHT), available.y)));
  }
  else if (sidePanel)
  {
    const float panelWidth =
        std::clamp(std::floor(available.x *
                              MatchSceneTuning::Panel::SIDE_PANEL_WIDTH_RATIO),
                   scaled(MatchSceneTuning::Panel::SIDE_PANEL_MIN_WIDTH),
                   scaled(MatchSceneTuning::Panel::SIDE_PANEL_MAX_WIDTH));
    const float viewWidth =
        std::max(1.0f, available.x - panelWidth - style.ItemSpacing.x);
    const float viewHeight =
        std::max(scaled(MatchSceneTuning::View::MIN_HEIGHT), available.y);
    renderPitchArea(ImVec2(viewWidth, viewHeight));
    ImGui::SameLine();
    ImGui::BeginGroup();
    const float statisticsHeight =
        std::floor((viewHeight - style.ItemSpacing.y) *
                   MatchSceneTuning::Panel::STATISTICS_HEIGHT_RATIO);
    renderStatistics(ImVec2(panelWidth, statisticsHeight));
    renderEvents(ImVec2(panelWidth,
                        viewHeight - statisticsHeight - style.ItemSpacing.y));
    ImGui::EndGroup();
  }
  else
  {
    const float panelHeight = scaled(MatchSceneTuning::Events::PANEL_HEIGHT);
    const float viewHeight =
        std::max(scaled(MatchSceneTuning::View::MIN_HEIGHT),
                 available.y - panelHeight - style.ItemSpacing.y);
    renderPitchArea(ImVec2(std::max(1.0f, available.x), viewHeight));
    const float halfWidth =
        std::floor((available.x - style.ItemSpacing.x) * 0.5f);
    renderEvents(ImVec2(halfWidth, panelHeight));
    ImGui::SameLine();
    renderStatistics(
        ImVec2(available.x - halfWidth - style.ItemSpacing.x, panelHeight));
  }

  ImGui::End();
}

void MatchScene::renderQuickResultProgress()
{
  // The engine is busy on a worker thread; only the teams are shown.
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float width =
      std::min(available.x, scaled(MatchSceneTuning::Panel::LINEUP_GATE_WIDTH));
  ImGui::SetCursorPos(
      ImVec2(ImGui::GetCursorPosX() + (available.x - width) * 0.5f,
             ImGui::GetCursorPosY() + available.y * 0.35f));
  ImGui::BeginGroup();
  const std::string teams = std::format("{}  –  {}", home_name, away_name);
  UI::pageHeader(LOC("MATCH_QUICK_RESULT_RUNNING"), teams.c_str());
  ImGui::ProgressBar(
      Theme::reducedMotion() ? 0.0f : -static_cast<float>(ImGui::GetTime()),
      ImVec2(width, scaled(6.0f)), "");
  ImGui::EndGroup();
}

void MatchScene::renderLineupGate()
{
  const Theme::Palette& palette = Theme::palette();
  // A centred column in the upper part of the screen.
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float width =
      std::min(available.x, scaled(MatchSceneTuning::Panel::LINEUP_GATE_WIDTH));
  const float top =
      available.y * MatchSceneTuning::Panel::LINEUP_GATE_TOP_RATIO;
  ImGui::SetCursorPos(
      ImVec2(ImGui::GetCursorPosX() + (available.x - width) * 0.5f,
             ImGui::GetCursorPosY() + top));
  ImGui::BeginChild("##lineup_gate_column",
                    ImVec2(width, std::max(1.0f, available.y - top)),
                    ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
  const std::string subtitle = std::format("{}  –  {}", home_name, away_name);
  UI::pageHeader(LOC("MATCH_LINEUP_BLOCKED_TITLE"), subtitle.c_str());
  UI::beginAutoHeightCard("##lineup_gate", nullptr);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(LOC("MATCH_LINEUP_BLOCKED_BODY"));
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));
  for (const LineupProblem& problem : lineup_problems)
  {
    UI::badge(problem.reason.c_str(), palette.negative);
    ImGui::SameLine();
    ImGui::TextUnformatted(problem.name.c_str());
    ImGui::SameLine();
    // What the assistant would do about it.
    if (problem.replacement.empty())
      ImGui::TextColored(palette.faint, "%s",
                         LOC("MATCH_LINEUP_NO_SUGGESTION"));
    else
      ImGui::TextColored(palette.positive, "%s",
                         fmt::sprintf(LOC("MATCH_LINEUP_SUGGESTION"),
                                      problem.replacement.c_str())
                             .c_str());
  }
  if (!lineup_status.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
    ImGui::TextColored(palette.warning, "%s", lineup_status.c_str());
  }
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));
  if (UI::primaryButton(LOC("MATCH_LINEUP_AUTOFIX")))
  {
    applyLineupFix();
    if (lineup_problems.empty()) startMatch();
  }
  ImGui::SameLine();
  if (ImGui::Button(LOC("MATCH_LINEUP_EDIT")))
    guiView->navigateTo(std::make_unique<LineupScene>(guiView));
  ImGui::SameLine();
  if (ImGui::Button(LOC("NAV_BACK"))) guiView->popScene();
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  if (bool fixes = guiView->getController().getAssistantFixesLineup();
      ImGui::Checkbox(LOC("MATCH_ASSISTANT_LINEUP"), &fixes))
    guiView->getController().setAssistantFixesLineup(fixes);
  UI::endCard();
  ImGui::EndChild();
}

void MatchScene::renderScoreboard()
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginCard("##match_scoreboard", nullptr,
                ImVec2(0.0f, scaled(MatchSceneTuning::Scoreboard::HEIGHT)));
  const ImVec2 origin = ImGui::GetCursorPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const float centreX = origin.x + width * 0.5f;

  std::array<char, 24> score{};
  formatBuffer(score, "{}  -  {}", engine->getHomeScore(),
               engine->getAwayScore());
  ImVec2 scoreSize;
  {
    Theme::ScopedText display(Theme::Text::DISPLAY);
    scoreSize = ImGui::CalcTextSize(score.data());
    ImGui::SetCursorPos(ImVec2(centreX - scoreSize.x * 0.5f, origin.y));
    ImGui::TextUnformatted(score.data());
  }

  // Clock pill and period under the score.
  const std::string clock = clockText();
  const MatchState state = engine->getState();
  // After extra time the result says how it was decided.
  if (state == MatchState::FULL_TIME && !decided_by_ready)
  {
    decided_by_ready = true;
    if (engine->hasShootout())
      decided_by =
          fmt::sprintf(LOC("RESULT_PENALTIES"), engine->getShootoutScore(true),
                       engine->getShootoutScore(false));
    else if (engine->wentToExtraTime())
      decided_by = LOC("RESULT_AFTER_EXTRA_TIME");
  }
  const char* periodText = state == MatchState::FULL_TIME && !decided_by.empty()
                               ? decided_by.c_str()
                               : LOC(periodKey(*engine));
  const int announced = engine->getAddedMinutes(engine->getPeriod());
  std::array<char, 16> addedText{};
  if (announced > 0 && state != MatchState::HALF_TIME &&
      state != MatchState::FULL_TIME && state != MatchState::PENALTY_SHOOTOUT)
    formatBuffer(addedText, "+{}", announced);
  {
    Theme::ScopedText caption(Theme::Text::CAPTION);
    const ImVec2 padding(scaled(6.0f), scaled(2.0f));
    const float clockWidth =
        ImGui::CalcTextSize(clock.c_str()).x + 2.0f * padding.x;
    const float periodWidth = ImGui::CalcTextSize(periodText).x;
    const float addedWidth = addedText[0] != '\0'
                                 ? ImGui::CalcTextSize(addedText.data()).x +
                                       2.0f * padding.x +
                                       ImGui::GetStyle().ItemSpacing.x
                                 : 0.0f;
    const float rowWidth =
        clockWidth + ImGui::GetStyle().ItemSpacing.x + periodWidth + addedWidth;
    ImGui::SetCursorPos(ImVec2(centreX - rowWidth * 0.5f,
                               origin.y + scoreSize.y + scaled(2.0f)));
    const ImVec2 pillMin = ImGui::GetCursorScreenPos();
    const ImVec2 pillMax(
        pillMin.x + clockWidth,
        pillMin.y + ImGui::GetTextLineHeight() + 2.0f * padding.y);
    ImGui::GetWindowDrawList()->AddRectFilled(
        pillMin, pillMax, Theme::toU32(palette.accent), scaled(3.0f));
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(pillMin.x + padding.x, pillMin.y + padding.y),
        Theme::toU32(palette.on_accent), clock.c_str());
    ImGui::Dummy(ImVec2(clockWidth, pillMax.y - pillMin.y));
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + padding.y);
    ImGui::TextColored(palette.muted, "%s", periodText);
    if (addedText[0] != '\0')
    {
      ImGui::SameLine();
      UI::badge(addedText.data(), palette.warning);
    }
  }

  // Team names either side of the score, with their kit colour.
  {
    Theme::ScopedText heading(Theme::Text::HEADING);
    const float gap = scaled(MatchSceneTuning::Scoreboard::NAME_GAP);
    const float swatch = scaled(MatchSceneTuning::Scoreboard::KIT_SWATCH_SIZE);
    const float nameY =
        origin.y + (scoreSize.y - ImGui::GetTextLineHeight()) * 0.5f;
    const float homeWidth = ImGui::CalcTextSize(home_name.c_str()).x;
    const float homeX =
        std::max(origin.x + swatch + gap * 0.5f,
                 centreX - scoreSize.x * 0.5f - gap - homeWidth);
    const float awayX = centreX + scoreSize.x * 0.5f + gap;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const float swatchY = windowPos.y + nameY +
                          (ImGui::GetTextLineHeight() - swatch) * 0.5f -
                          ImGui::GetScrollY();
    const float homeSwatchX = windowPos.x + homeX - gap * 0.5f - swatch;
    drawList->AddRectFilled(ImVec2(homeSwatchX, swatchY),
                            ImVec2(homeSwatchX + swatch, swatchY + swatch),
                            teamColor(true), scaled(2.0f));
    const float awayWidth = ImGui::CalcTextSize(away_name.c_str()).x;
    const float awaySwatchX = windowPos.x + awayX + awayWidth + gap * 0.5f;
    drawList->AddRectFilled(ImVec2(awaySwatchX, swatchY),
                            ImVec2(awaySwatchX + swatch, swatchY + swatch),
                            teamColor(false), scaled(2.0f));
    ImGui::SetCursorPos(ImVec2(homeX, nameY));
    ImGui::TextUnformatted(home_name.c_str());
    ImGui::SetCursorPos(ImVec2(awayX, nameY));
    ImGui::TextUnformatted(away_name.c_str());
  }

  // Competition on the left edge.
  if (fixture_type.has_value())
  {
    ImGui::SetCursorPos(origin);
    UI::badge(LOC(CompetitionView::matchTypeKey(*fixture_type)), palette.info);
    ImGui::SameLine();
    Theme::ScopedText caption(Theme::Text::CAPTION);
    ImGui::TextColored(palette.muted, "%s", fixture_when.c_str());
  }
  // Highlight playback state on the right edge.
  if (highlights_only)
  {
    const bool skipping = skip_indicator_seconds > 0.0f;
    if (skipping || engine->isInHighlight())
    {
      const char* note =
          LOC(skipping ? "MATCH_SKIPPING_TO_HIGHLIGHT" : "MATCH_HIGHLIGHT");
      float noteWidth = 0.0f;
      {
        Theme::ScopedText caption(Theme::Text::CAPTION);
        noteWidth = ImGui::CalcTextSize(note).x + scaled(12.0f);
      }
      ImGui::SetCursorPos(ImVec2(origin.x + width - noteWidth, origin.y));
      UI::badge(note, skipping ? palette.info : palette.warning);
    }
  }
  renderTimeline();
  UI::endCard();
}

void MatchScene::renderTimeline()
{
  // A strip along the bottom of the scoreboard: time played, half-time and
  // the key moments (goals above/below for home/away, cards).
  const Theme::Palette& palette = Theme::palette();
  // One segment per period played (the two halves of extra time once they
  // are reached), each as long as its regulation time plus added time.
  const int periods = engine->wentToExtraTime() ? 4 : 2;
  std::array<float, 5> segmentEnd{};
  for (int period = 1; period <= periods; ++period)
    segmentEnd[static_cast<std::size_t>(period)] =
        segmentEnd[static_cast<std::size_t>(period - 1)] +
        MatchRules::periodEndMinute(period) -
        MatchRules::periodStartMinute(period) +
        static_cast<float>(engine->getAddedMinutes(period));
  const float total = segmentEnd[static_cast<std::size_t>(periods)];
  const auto position = [&](float minute, int period)
  {
    const auto index = static_cast<std::size_t>(std::clamp(period, 1, periods));
    const float start = segmentEnd[index - 1];
    return start + std::clamp(minute - MatchRules::periodStartMinute(
                                           static_cast<int>(index)),
                              0.0f, segmentEnd[index] - start);
  };
  const float played =
      engine->getState() == MatchState::FULL_TIME
          ? total
          : position(engine->getMatchTimeMinutes(), engine->getPeriod());

  const ImVec2 windowPos = ImGui::GetWindowPos();
  const float padding = ImGui::GetStyle().WindowPadding.x;
  const float trackHeight = scaled(MatchSceneTuning::Scoreboard::TRACK_HEIGHT);
  const float markerSize =
      scaled(MatchSceneTuning::Scoreboard::TIMELINE_MARKER_SIZE);
  const float left = windowPos.x + padding;
  const float right = windowPos.x + ImGui::GetWindowWidth() - padding;
  const float trackY = windowPos.y + ImGui::GetWindowHeight() -
                       ImGui::GetStyle().WindowPadding.y - markerSize -
                       trackHeight * 0.5f;
  const auto xAt = [&](float value)
  { return left + (right - left) * std::clamp(value / total, 0.0f, 1.0f); };
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(ImVec2(left, trackY - trackHeight * 0.5f),
                          ImVec2(right, trackY + trackHeight * 0.5f),
                          Theme::toU32(palette.raised), trackHeight);
  drawList->AddRectFilled(ImVec2(left, trackY - trackHeight * 0.5f),
                          ImVec2(xAt(played), trackY + trackHeight * 0.5f),
                          Theme::toU32(palette.accent, 0.7f), trackHeight);
  for (int period = 1; period < periods; ++period)
  {
    const float x = xAt(segmentEnd[static_cast<std::size_t>(period)]);
    drawList->AddLine(ImVec2(x, trackY - markerSize),
                      ImVec2(x, trackY + markerSize),
                      Theme::toU32(palette.faint), 1.0f);
  }
  for (const MatchEvent& event : engine->getEvents())
  {
    const bool goal = event.type == MatchEventType::GOAL ||
                      event.type == MatchEventType::OWN_GOAL;
    if (const bool card = event.type == MatchEventType::YELLOW_CARD ||
                          event.type == MatchEventType::SECOND_YELLOW ||
                          event.type == MatchEventType::RED_CARD;
        !goal && !card)
      continue;
    // An own goal counts for the other side.
    const bool home = event.type == MatchEventType::OWN_GOAL ? !event.isHomeTeam
                                                             : event.isHomeTeam;
    const float x = xAt(position(event.timeMinute, event.period));
    const float y = home ? trackY - markerSize * 0.5f - trackHeight
                         : trackY + markerSize * 0.5f + trackHeight;
    if (goal)
    {
      drawList->AddCircleFilled(ImVec2(x, y), markerSize * 0.5f,
                                teamColor(home));
      drawList->AddCircle(ImVec2(x, y), markerSize * 0.5f,
                          Theme::toU32(palette.text), 0, 1.0f);
    }
    else
    {
      const ImU32 color = event.type == MatchEventType::YELLOW_CARD
                              ? IM_COL32(245, 200, 40, 255)
                              : IM_COL32(220, 50, 50, 255);
      drawList->AddRectFilled(
          ImVec2(x - markerSize * 0.3f, y - markerSize * 0.45f),
          ImVec2(x + markerSize * 0.3f, y + markerSize * 0.45f), color, 1.0f);
    }
  }
}

void MatchScene::renderControls()
{
  using enum MatchViewMode;
  const Theme::Palette& palette = Theme::palette();
  const ImVec2 pauseSize(scaled(MatchSceneTuning::Controls::PAUSE_BUTTON_WIDTH),
                         0.0f);
  if (match_finished)
  {
    if (UI::primaryButton(LOC("MATCH_FINISH"), pauseSize)) finishMatch();
  }
  else
  {
    if (is_paused ? UI::primaryButton(LOC("MATCH_RESUME"), pauseSize)
                  : ImGui::Button(LOC("MATCH_PAUSE"), pauseSize))
      is_paused = !is_paused;
    if (!play.isActive())
    {
      UI::sameLineIfFits(UI::buttonWidth(LOC("MATCH_QUICK_RESULT")));
      if (ImGui::Button(LOC("MATCH_QUICK_RESULT"))) quickResult();
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", LOC("MATCH_QUICK_RESULT_HINT"));
    }
    // Watch, Simulate (quick result) or Play.
    renderPlayButtons();
  }
  if (managed_is_home.has_value())
  {
    UI::sameLineIfFits(UI::buttonWidth(LOC("MATCH_ANALYSIS")));
    // Toggles the analysis panel (it never covers these controls).
    if (UI::toggleButton(LOC("MATCH_ANALYSIS"), analysis_panel.isOpen()))
    {
      if (analysis_panel.isOpen())
        analysis_panel.close();
      else
        analysis_panel.openNow(guiView->getController(), *engine, home_team_id,
                               away_team_id);
    }
  }

  // Segmented speed control: real time up to 16x, or highlights only. A
  // played match runs in real time.
  const auto& speeds = MatchSceneTuning::Controls::SPEED_STEPS;
  if (!play.isActive())
  {
    std::array<std::array<char, 16>, speeds.size()> speedLabels{};
    const ImGuiStyle& style = ImGui::GetStyle();
    const float gap = scaled(MatchSceneTuning::Controls::SPEED_BUTTON_GAP);
    float segmentWidth = ImGui::CalcTextSize(LOC("MATCH_SPEED")).x +
                         style.ItemSpacing.x +
                         ImGui::CalcTextSize(LOC("MATCH_HIGHLIGHTS")).x +
                         2.0f * style.FramePadding.x + gap;
    for (std::size_t index = 0; index < speeds.size(); ++index)
    {
      formatBuffer(speedLabels[index], "{:g}x",
                   static_cast<double>(speeds[index]));
      segmentWidth += ImGui::CalcTextSize(speedLabels[index].data()).x +
                      2.0f * style.FramePadding.x + gap;
    }
    UI::sameLineIfFits(segmentWidth);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(palette.muted, "%s", LOC("MATCH_SPEED"));
    ImGui::SameLine();
    ImGui::PushID("match_speed");
    for (std::size_t index = 0; index < speeds.size(); ++index)
    {
      if (index > 0) ImGui::SameLine(0.0f, gap);
      const bool active = match_speed == speeds[index];
      const ImVec2 speedSize(UI::buttonWidth(speedLabels[index].data()), 0.0f);
      if (active ? UI::primaryButton(speedLabels[index].data(), speedSize)
                 : ImGui::Button(speedLabels[index].data(), speedSize))
        setPlaybackSpeed(speeds[index]);
    }
    ImGui::SameLine(0.0f, gap);
    if (const ImVec2 highlightsSize(UI::buttonWidth(LOC("MATCH_HIGHLIGHTS")),
                                    0.0f);
        highlights_only
            ? UI::primaryButton(LOC("MATCH_HIGHLIGHTS"), highlightsSize)
            : ImGui::Button(LOC("MATCH_HIGHLIGHTS"), highlightsSize))
      setHighlightsOnly(!highlights_only);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", LOC("MATCH_HIGHLIGHTS_HINT"));
    ImGui::PopID();
  }

  const int used = managed_is_home.has_value()
                       ? engine->getSubstitutionsUsed(*managed_is_home)
                       : 0;
  // Fixed buffer: the HUD formats no heap strings per frame.
  std::array<char, 96> subsLabel{};
  formatBuffer(subsLabel, "{} {}/{}###match_substitutions",
               LOC("SUBSTITUTION_TITLE"), used,
               MatchChanges::maxSubstitutions(*engine));
  const float subsWidth =
      scaled(MatchSceneTuning::Controls::SUBSTITUTION_BUTTON_WIDTH);
  UI::sameLineIfFits(subsWidth);
  ImGui::BeginDisabled(!managed_is_home.has_value() || match_finished);
  if (ImGui::Button(subsLabel.data(), ImVec2(subsWidth, 0.0f)))
    showSubstitutions(true);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", LOC("MATCH_SUBSTITUTIONS_HINT"));
  UI::sameLineIfFits(UI::buttonWidth(LOC("MATCH_TACTICS_TITLE")));
  if (ImGui::Button(LOC("MATCH_TACTICS_TITLE"))) showTactics(true);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", LOC("MATCH_TACTICS_HINT"));
  ImGui::EndDisabled();

  const float viewWidth = scaled(MatchSceneTuning::Controls::VIEW_BUTTON_WIDTH);
  UI::sameLineIfFits(viewWidth);
  if (ImGui::Button(
          view_mode == PITCH_2D ? LOC("MATCH_VIEW_3D") : LOC("MATCH_VIEW_2D"),
          ImVec2(viewWidth, 0.0f)))
  {
    setViewMode(view_mode == PITCH_2D ? BROADCAST_3D : PITCH_2D);
  }

  // What the assistant manager may decide on the manager's behalf.
  UI::sameLineIfFits(UI::buttonWidth(LOC("MATCH_ASSISTANT")));
  ImGui::BeginDisabled(!managed_is_home.has_value());
  if (ImGui::Button(LOC("MATCH_ASSISTANT"))) ImGui::OpenPopup("##assistant");
  ImGui::EndDisabled();
  if (ImGui::BeginPopup("##assistant"))
  {
    if (ImGui::Checkbox(LOC("MATCH_ASSISTANT_SUBS"), &assistant_substitutions))
    {
      guiView->getController().setDutyOwner(
          Duty::Substitutions,
          assistant_substitutions ? DutyOwner::Assistant : DutyOwner::Manager);
      applySubstitutionPolicy();
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", LOC("MATCH_ASSISTANT_SUBS_HINT"));
    if (bool fixes = guiView->getController().getAssistantFixesLineup();
        ImGui::Checkbox(LOC("MATCH_ASSISTANT_LINEUP"), &fixes))
      guiView->getController().setAssistantFixesLineup(fixes);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", LOC("MATCH_ASSISTANT_LINEUP_HINT"));
    ImGui::EndPopup();
  }

  // Latest message: debug, then substitutions, then pre-match changes.
  const bool refused = substitution_refused && !substitution_status.empty();
  const std::string* status = &pre_match_note;
  if (!substitution_status.empty()) status = &substitution_status;
  if (!debug_status.empty()) status = &debug_status;
  ImVec4 statusColor = palette.positive;
  if (refused || !debug_status.empty())
    statusColor = palette.warning;
  else if (status == &pre_match_note)
    statusColor = palette.info;
  if (!status->empty())
  {
    UI::sameLineIfFits(ImGui::CalcTextSize(status->c_str()).x);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(statusColor, "%s", status->c_str());
  }
}

void MatchScene::renderViewControls()
{
  // Layout first (both views): pitch focus and the side panels.
  if (ImGui::Button(LOC("MATCH_FOCUS"))) setPitchFocus(true);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", LOC("MATCH_FOCUS_HINT"));
  const char* panelsLabel =
      side_panels_hidden ? LOC("MATCH_PANELS_SHOW") : LOC("MATCH_PANELS_HIDE");
  UI::sameLineIfFits(UI::buttonWidth(panelsLabel));
  if (ImGui::Button(panelsLabel)) setSidePanelsHidden(!side_panels_hidden);
  if (view_mode != MatchViewMode::BROADCAST_3D)
  {
    UI::sameLineIfFits(ImGui::CalcTextSize(LOC("MATCH_PRESSURE_MAP")).x +
                       ImGui::GetFrameHeight() * 1.5f);
    ImGui::Checkbox(LOC("MATCH_PRESSURE_MAP"), &pressure_overlay);
    return;
  }

  const float labelWidth = ImGui::CalcTextSize(LOC("MATCH_CAMERA")).x;
  UI::sameLineIfFits(labelWidth);
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(Theme::palette().muted, "%s", LOC("MATCH_CAMERA"));
  const auto cameraButton = [this](const char* label, MatchCameraMode mode)
  {
    UI::sameLineIfFits(ImGui::CalcTextSize(label).x +
                       ImGui::GetFrameHeight() * 1.5f);
    if (ImGui::RadioButton(label, camera_mode == mode)) setCameraMode(mode);
  };
  cameraButton(LOC("MATCH_CAMERA_BROADCAST"), MatchCameraMode::BROADCAST);
  cameraButton(LOC("MATCH_CAMERA_TACTICAL"), MatchCameraMode::TACTICAL);
  cameraButton(LOC("MATCH_CAMERA_END"), MatchCameraMode::END);
  cameraButton(LOC("MATCH_CAMERA_FOLLOW"), MatchCameraMode::PLAYER_FOLLOW);
  cameraButton(LOC("MATCH_CAMERA_FREE"), MatchCameraMode::FREE);
  cameraButton(LOC("MATCH_CAMERA_DIRECTOR"), MatchCameraMode::DIRECTOR);
  if (play.isActive())
    cameraButton(LOC("MATCH_CAMERA_PLAY"), MatchCameraMode::PLAY);
  if (camera_mode == MatchCameraMode::FREE)
  {
    UI::sameLineIfFits(ImGui::CalcTextSize(LOC("MATCH_FOLLOW_BALL")).x +
                       ImGui::GetFrameHeight() * 1.5f);
    if (bool follow = free_follow_ball;
        ImGui::Checkbox(LOC("MATCH_FOLLOW_BALL"), &follow))
      setFreeFollowBall(follow);
    UI::sameLineIfFits(UI::buttonWidth(LOC("MATCH_CAMERA_RESET")));
    if (ImGui::Button(LOC("MATCH_CAMERA_RESET")))
      pending_camera_input.reset = true;
  }
  UI::sameLineIfFits(ImGui::CalcTextSize(LOC("MATCH_SHOW_NAMES")).x +
                     ImGui::GetFrameHeight() * 1.5f);
  ImGui::Checkbox(LOC("MATCH_SHOW_NAMES"), &show_player_names);
  UI::sameLineIfFits(ImGui::CalcTextSize(LOC("MATCH_DAY_LOOK")).x +
                     ImGui::GetFrameHeight() * 1.5f);
  ImGui::Checkbox(LOC("MATCH_DAY_LOOK"), &day_look);
  // Mouse controls are explained on hover to keep the row short.
  UI::sameLineIfFits(ImGui::CalcTextSize("(?)").x);
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s\n\n%s", LOC("MATCH_ZOOM_HINT"),
                      cameraHelpText().c_str());
}

#ifdef DEBUG
void MatchScene::renderDebugLines()
{
  ImGui::Text(
      "Performance: entry %.2f ms | simulation %.2f ms (max %.2f ms) | "
      "slow frames %llu | %s view %.2f ms (avg %.2f ms)",
      static_cast<double>(scene_entry_milliseconds),
      static_cast<double>(last_update_milliseconds),
      static_cast<double>(maximum_update_milliseconds),
      static_cast<unsigned long long>(slow_update_count),
      view_mode == MatchViewMode::PITCH_2D ? "2D" : "3D",
      static_cast<double>(last_render_milliseconds),
      static_cast<double>(average_render_milliseconds));
  if (ImGui::SmallButton("Export Debug (F11)")) exportDebugSnapshot();
  ImGui::SameLine();
  if (ImGui::SmallButton(show_ai_debug ? "Hide AI (F10)" : "Show AI (F10)"))
    show_ai_debug = !show_ai_debug;
  if (show_ai_debug)
  {
    ImGui::SameLine();
    ImGui::Text("Team phase: %s / %s | transition %.1f s",
                teamPhaseLabel(engine->getHomePhase()),
                teamPhaseLabel(engine->getAwayPhase()),
                static_cast<double>(engine->getTransitionSecondsRemaining()));
  }
  const PassDecision& passDecision = engine->getLastPassDecision();
  if (passDecision.receiverId != 0)
  {
    ImGui::Text(
        "Last decision: %s | expected completion %.0f%% | utility %.2f",
        passIntentLabel(passDecision.intent),
        static_cast<double>(passDecision.completionProbability *
                            MatchSceneTuning::Scoreboard::PERCENT_SCALE),
        static_cast<double>(passDecision.utility));
  }
}
#endif

void MatchScene::renderPitchArea(ImVec2 size)
{
  const auto context = touchline();
  if (!context || match_finished)
  {
    renderPitch(size);
    return;
  }
  // The shouts sit under the view, wrapping onto a second row when narrow.
  const float spacing = ImGui::GetStyle().ItemSpacing.y;
  const float shouts = MatchShoutsBar::height(size.x);
  ImGui::BeginGroup();
  renderPitch(ImVec2(size.x, std::max(1.0f, size.y - shouts - spacing)));
  MatchShoutsBar::render(*context, size.x);
  ImGui::EndGroup();
}

void MatchScene::renderFocusShouts(ImVec2 origin, ImVec2 size)
{
  const auto context = touchline();
  if (!context || match_finished) return;
  const float margin = scaled(Theme::Space::M);
  const ImVec2 padding = ImGui::GetStyle().WindowPadding;
  // At most about half the view wide so the ticker keeps the left side.
  const float width = std::floor(size.x * FOCUS_SHOUTS_WIDTH_SHARE);
  const float height = MatchShoutsBar::height(width) + 2.0f * padding.y;
  ImGui::SetCursorScreenPos(
      ImVec2(origin.x + size.x - margin - width - 2.0f * padding.x,
             origin.y + size.y - margin - height));
  if (ImGui::BeginChild(
          "##focus_shouts", ImVec2(width + 2.0f * padding.x, height),
          ImGuiChildFlags_AlwaysUseWindowPadding,
          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings))
    MatchShoutsBar::render(*context, width);
  ImGui::EndChild();
}

void MatchScene::renderPitch(ImVec2 size)
{
  // Both views consume the same snapshot through the renderer interface.
  const ImVec2 viewOrigin = ImGui::GetCursorScreenPos();
  MatchViewport viewport;
  IMatchRenderer* renderer = nullptr;
  MatchRenderOptions renderOptions;
  renderOptions.frameSeconds = frame_seconds;
  renderOptions.kickoffMinutes = kickoff_minutes;
  renderOptions.dayLook = day_look;
  renderOptions.pressureOverlay = pressure_overlay;
  if (play.isActive())
    renderOptions.activePlayer = engine->getControlledPlayer();
  // Every mouse button can grab the view; widgets drawn over it (the focus
  // HUD) live in child windows and so keep their own input.
  ImGui::InvisibleButton("MatchView", size,
                         ImGuiButtonFlags_MouseButtonLeft |
                             ImGuiButtonFlags_MouseButtonRight |
                             ImGuiButtonFlags_MouseButtonMiddle);
  handleViewInput();
  if (view_mode == MatchViewMode::BROADCAST_3D)
  {
    viewport = {viewOrigin.x, viewOrigin.y, size.x, size.y};
    renderer = renderer_3d.get();
    renderOptions.cameraMode = camera_mode;
    renderOptions.showPlayerNames = show_player_names;
    pending_camera_input.zoomSteps += pending_zoom_steps;
    pending_camera_input.followBall =
        free_follow_ball && camera_mode == MatchCameraMode::FREE;
    renderOptions.cameraInput = pending_camera_input;
    // The HUD scoreboard above the view replaces the in-view score bug.
    pending_zoom_steps = 0.0f;
    pending_camera_input = MatchCameraInput{};
  }
  else
  {
    // The 2D pitch is inset by an apron so the stadium band and goal nets
    // stay visible.
    const float apron = scaled(MatchSceneTuning::Stadium::APRON_WIDTH);
    viewport = computeMatchViewport(viewOrigin.x + apron, viewOrigin.y + apron,
                                    std::max(1.0f, size.x - 2.0f * apron),
                                    std::max(1.0f, size.y - 2.0f * apron));
    // Centred in the view area when the aspect ratios differ.
    viewport.x =
        viewOrigin.x + std::max(apron, (size.x - viewport.width) * 0.5f);
    viewport.y =
        viewOrigin.y + std::max(apron, (size.y - viewport.height) * 0.5f);
    // Playing: a zoomed view that follows the active footballer.
    if (play.isActive())
      viewport =
          playViewport2D(viewport, viewOrigin,
                         ImVec2(viewOrigin.x + size.x, viewOrigin.y + size.y));
    renderer = renderer_2d.get();
  }

  if (renderer)
  {
#ifdef DEBUG
    renderOptions.showAiDebug = show_ai_debug;
#endif
    const auto renderStartedAt = std::chrono::steady_clock::now();
    const ImVec2 viewEnd(viewOrigin.x + size.x, viewOrigin.y + size.y);
    fillMatchRenderSnapshot(*engine, snapshot);
    snapshot.homeTeam = home_team_id;
    snapshot.awayTeam = away_team_id;
    ImGui::GetWindowDrawList()->PushClipRect(viewOrigin, viewEnd, true);
    renderer->render(snapshot, renderOptions, viewport);
    renderPlayOverlay(*renderer, viewOrigin, viewEnd);
    ImGui::GetWindowDrawList()->PopClipRect();
    last_render_milliseconds =
        std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - renderStartedAt)
            .count();
    average_render_milliseconds +=
        (last_render_milliseconds - average_render_milliseconds) *
        MatchSceneTuning::View::RENDER_TIME_SMOOTHING;
  }
}

void MatchScene::handleViewInput()
{
  const ImGuiIO& io = ImGui::GetIO();
  const bool view3D = view_mode == MatchViewMode::BROADCAST_3D;
  // The wheel zooms the camera instead of scrolling the scene.
  if (view3D && ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY))
    pending_zoom_steps += io.MouseWheel;
  if (ImGui::IsItemHovered() &&
      ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
  {
    // Double-click toggles pitch focus; the free camera instead looks at
    // the spot that was clicked.
    if (view3D && camera_mode == MatchCameraMode::FREE)
    {
      pending_camera_input.retarget = true;
      pending_camera_input.retargetX = io.MousePos.x;
      pending_camera_input.retargetY = io.MousePos.y;
      free_follow_ball = false;
    }
    else
    {
      setPitchFocus(!pitch_focus);
    }
    return;
  }
  if (!view3D || !ImGui::IsItemActive() ||
      (io.MouseDelta.x == 0.0f && io.MouseDelta.y == 0.0f))
    return;
  // Left-drag orbits; right-, middle- or Shift+left-drag pans. Any drag
  // hands a preset over to the free camera from the pose on screen.
  const bool leftDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
  const bool panDrag = ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                       ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                       (leftDrag && io.KeyShift);
  if (!leftDrag && !panDrag) return;
  takeFreeCamera();
  if (panDrag)
  {
    pending_camera_input.pan = true;
    pending_camera_input.panFromX = io.MousePos.x - io.MouseDelta.x;
    pending_camera_input.panFromY = io.MousePos.y - io.MouseDelta.y;
    pending_camera_input.panToX = io.MousePos.x;
    pending_camera_input.panToY = io.MousePos.y;
    free_follow_ball = false;
    return;
  }
  pending_camera_input.orbitX += io.MouseDelta.x;
  pending_camera_input.orbitY += io.MouseDelta.y;
}

void MatchScene::renderPitchFocus()
{
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const ImVec2 size(std::max(1.0f, available.x), std::max(1.0f, available.y));
  renderPitch(size);
  renderFocusHud(origin, size);
}

void MatchScene::renderFocusHud(ImVec2 origin, ImVec2 size)
{
  // Broadcast-style overlay: dark translucent chips that read on any theme
  // and over the grass. Child windows keep their widgets' input away from
  // the view's camera drag.
  const float margin = scaled(Theme::Space::M);
  const ImVec4 text(0.96f, 0.97f, 0.98f, 1.0f);
  const ImVec4 muted(0.72f, 0.75f, 0.80f, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.03f, 0.05f, 0.09f, 0.82f));
  ImGui::PushStyleColor(ImGuiCol_Text, text);
  ImGui::PushStyleVar(
      ImGuiStyleVar_WindowPadding,
      ImVec2(scaled(Theme::Space::S), scaled(Theme::Space::XS)));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, scaled(6.0f));
  const ImGuiChildFlags chip = ImGuiChildFlags_AutoResizeX |
                               ImGuiChildFlags_AutoResizeY |
                               ImGuiChildFlags_AlwaysUseWindowPadding;
  const ImGuiWindowFlags chipWindow =
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;

  // Score bug, top left: club codes, score, clock and the venue.
  const ClubIdentity* homeIdentity = findClubIdentity(home_team_id);
  const ClubIdentity* awayIdentity = findClubIdentity(away_team_id);
  const auto code = [](const ClubIdentity* identity, const std::string& name)
  {
    return identity && !identity->short_name.empty()
               ? identity->short_name.c_str()
               : name.c_str();
  };
  ImGui::SetCursorScreenPos(ImVec2(origin.x + margin, origin.y + margin));
  if (ImGui::BeginChild("##focus_score", ImVec2(0.0f, 0.0f), chip, chipWindow))
  {
    const float swatch = scaled(MatchSceneTuning::Scoreboard::KIT_SWATCH_SIZE);
    const auto teamChip = [&](bool home)
    {
      const ImVec2 at = ImGui::GetCursorScreenPos();
      const float top = at.y + (ImGui::GetFrameHeight() - swatch) * 0.5f;
      ImGui::GetWindowDrawList()->AddRectFilled(
          ImVec2(at.x, top), ImVec2(at.x + swatch, top + swatch),
          teamColor(home), scaled(2.0f));
      ImGui::Dummy(ImVec2(swatch, ImGui::GetFrameHeight()));
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(home ? code(homeIdentity, home_name)
                                  : code(awayIdentity, away_name));
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", home ? home_name.c_str() : away_name.c_str());
    };
    teamChip(true);
    ImGui::SameLine();
    std::array<char, 24> score{};
    formatBuffer(score, "{} - {}", engine->getHomeScore(),
                 engine->getAwayScore());
    {
      Theme::ScopedText heading(Theme::Text::TITLE);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(score.data());
    }
    ImGui::SameLine();
    teamChip(false);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(1.0f, 0.84f, 0.3f, 1.0f), "%s",
                       clockText().c_str());
    if (homeIdentity && !homeIdentity->stadium_name.empty())
    {
      Theme::ScopedText caption(Theme::Text::CAPTION);
      ImGui::TextColored(muted, "%s", homeIdentity->stadium_name.c_str());
    }
  }
  ImGui::EndChild();
  const ImVec2 scoreSize = ImGui::GetItemRectSize();

  // Controls, top right (below the score when the window is narrow).
  // (Its width is known from the previous frame; until then it sits below.)
  const bool besideScore =
      focus_controls_width > 0.0f &&
      scoreSize.x + focus_controls_width + 3.0f * margin <= size.x;
  ImGui::SetCursorScreenPos(
      besideScore
          ? ImVec2(std::max(origin.x + margin,
                            origin.x + size.x - margin - focus_controls_width),
                   origin.y + margin)
          : ImVec2(origin.x + margin, origin.y + 2.0f * margin + scoreSize.y));
  if (ImGui::BeginChild("##focus_controls", ImVec2(0.0f, 0.0f), chip,
                        chipWindow))
  {
    const float gap = scaled(MatchSceneTuning::Controls::SPEED_BUTTON_GAP);
    if (match_finished)
    {
      if (UI::primaryButton(LOC("MATCH_FINISH"))) finishMatch();
    }
    else if (is_paused ? UI::primaryButton(LOC("MATCH_RESUME"))
                       : ImGui::Button(LOC("MATCH_PAUSE")))
    {
      is_paused = !is_paused;
    }
    ImGui::PushID("focus_speed");
    const auto& speeds = MatchSceneTuning::Controls::SPEED_STEPS;
    for (std::size_t index = 0; index < speeds.size() && !play.isActive();
         ++index)
    {
      std::array<char, 16> label{};
      formatBuffer(label, "{:g}x", static_cast<double>(speeds[index]));
      ImGui::SameLine(0.0f, index == 0 ? -1.0f : gap);
      const bool active = !highlights_only && match_speed == speeds[index];
      if (active ? UI::primaryButton(label.data())
                 : ImGui::Button(label.data()))
      {
        setHighlightsOnly(false);
        setPlaybackSpeed(speeds[index]);
      }
    }
    if (!play.isActive())
    {
      ImGui::SameLine(0.0f, gap);
      if (highlights_only ? UI::primaryButton(LOC("MATCH_HIGHLIGHTS"))
                          : ImGui::Button(LOC("MATCH_HIGHLIGHTS")))
        setHighlightsOnly(!highlights_only);
    }
    ImGui::PopID();
    renderPlayButtons();

    // The matchday dialogs work over the focus view too.
    if (managed_is_home.has_value() && !match_finished)
    {
      ImGui::SameLine();
      if (ImGui::Button(LOC("SUBSTITUTION_TITLE"))) showSubstitutions(true);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", LOC("MATCH_SUBSTITUTIONS_HINT"));
      ImGui::SameLine(0.0f, gap);
      if (ImGui::Button(LOC("MATCH_TACTICS_TITLE"))) showTactics(true);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", LOC("MATCH_TACTICS_HINT"));
    }

    if (view_mode == MatchViewMode::BROADCAST_3D)
    {
      const std::array<std::pair<MatchCameraMode, const char*>, 7> cameras{{
          {MatchCameraMode::BROADCAST, LOC("MATCH_CAMERA_BROADCAST")},
          {MatchCameraMode::TACTICAL, LOC("MATCH_CAMERA_TACTICAL")},
          {MatchCameraMode::END, LOC("MATCH_CAMERA_END")},
          {MatchCameraMode::PLAYER_FOLLOW, LOC("MATCH_CAMERA_FOLLOW")},
          {MatchCameraMode::FREE, LOC("MATCH_CAMERA_FREE")},
          {MatchCameraMode::DIRECTOR, LOC("MATCH_CAMERA_DIRECTOR")},
          {MatchCameraMode::PLAY, LOC("MATCH_CAMERA_PLAY")},
      }};
      const char* current = cameras[0].second;
      float comboWidth = 0.0f;
      for (const auto& [mode, label] : cameras)
      {
        if (mode == MatchCameraMode::PLAY && !play.isActive()) continue;
        if (mode == camera_mode) current = label;
        comboWidth = std::max(comboWidth, ImGui::CalcTextSize(label).x);
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(comboWidth + ImGui::GetFrameHeight() +
                              2.0f * ImGui::GetStyle().FramePadding.x);
      if (ImGui::BeginCombo("##focus_camera", current))
      {
        for (const auto& [mode, label] : cameras)
          if ((mode != MatchCameraMode::PLAY || play.isActive()) &&
              ImGui::Selectable(label, mode == camera_mode))
            setCameraMode(mode);
        ImGui::EndCombo();
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", cameraHelpText().c_str());
      if (camera_mode == MatchCameraMode::FREE)
      {
        ImGui::SameLine();
        if (bool follow = free_follow_ball;
            ImGui::Checkbox(LOC("MATCH_FOLLOW_BALL"), &follow))
          setFreeFollowBall(follow);
        ImGui::SameLine();
        if (ImGui::Button(LOC("MATCH_CAMERA_RESET")))
          pending_camera_input.reset = true;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button(view_mode == MatchViewMode::PITCH_2D
                          ? LOC("MATCH_VIEW_3D")
                          : LOC("MATCH_VIEW_2D")))
    {
      setViewMode(view_mode == MatchViewMode::PITCH_2D
                      ? MatchViewMode::BROADCAST_3D
                      : MatchViewMode::PITCH_2D);
    }
    ImGui::SameLine();
    if (UI::primaryButton(LOC("MATCH_FOCUS_EXIT"))) setPitchFocus(false);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", LOC("MATCH_FOCUS_HINT"));
  }
  ImGui::EndChild();
  focus_controls_width = ImGui::GetItemRectSize().x;

  // The radar takes the bottom of the view while playing.
  if (!play.isActive()) renderFocusShouts(origin, size);

  // Latest key moments, bottom left, newest last.
  constexpr std::size_t TICKER_ROWS = 3;
  constexpr std::size_t TICKER_SCAN = 96;
  const auto& events = engine->getEvents();
  std::array<std::size_t, TICKER_ROWS> latest{};
  std::size_t found = 0;
  for (std::size_t scanned = 0; scanned < std::min(events.size(), TICKER_SCAN);
       ++scanned)
  {
    const std::size_t index = events.size() - 1 - scanned;
    if (!isKeyEvent(events[index].type)) continue;
    latest[found] = index;
    ++found;
    if (found == TICKER_ROWS) break;
  }
  if (found > 0)
  {
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const float height = static_cast<float>(found) * rowHeight +
                         2.0f * ImGui::GetStyle().WindowPadding.y;
    ImGui::SetCursorScreenPos(
        ImVec2(origin.x + margin, origin.y + size.y - margin - height));
    if (ImGui::BeginChild("##focus_ticker", ImVec2(0.0f, 0.0f), chip,
                          chipWindow))
    {
      const float iconSize = scaled(MatchSceneTuning::Panel::EVENT_ICON_SIZE);
      for (std::size_t row = found; row-- > 0;)
      {
        const MatchEvent& event = events[latest[row]];
        const std::string minute = MatchClock::minuteLabel(
            event.timeMinute, event.period, event.addedMinute > 0.0f);
        ImGui::TextColored(muted, "%s", minute.c_str());
        ImGui::SameLine(scaled(MatchSceneTuning::Panel::MINUTE_COLUMN_WIDTH));
        const ImVec2 iconOrigin = ImGui::GetCursorScreenPos();
        drawEventIcon(ImGui::GetWindowDrawList(),
                      ImVec2(iconOrigin.x + iconSize * 0.5f,
                             iconOrigin.y + ImGui::GetTextLineHeight() * 0.5f),
                      iconSize, event.type);
        ImGui::Dummy(ImVec2(iconSize, ImGui::GetTextLineHeight()));
        ImGui::SameLine();
        ImGui::TextColored(row == 0 ? text : muted, "%s",
                           event.description.c_str());
      }
    }
    ImGui::EndChild();
  }
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(2);
}

void MatchScene::renderStatistics(ImVec2 size)
{
  UI::beginCard("##match_statistics", LOC("REPORT_STATS"), size, true);
  const MatchStats& stats = engine->getStats();
  std::array<char, 16> homeText{};
  std::array<char, 16> awayText{};

  formatBuffer(homeText, "{:.0f}%", static_cast<double>(stats.homePossession));
  formatBuffer(awayText, "{:.0f}%", static_cast<double>(stats.awayPossession));
  comparisonRow(LOC("REPORT_POSSESSION"), stats.homePossession,
                stats.awayPossession, homeText.data(), awayText.data());
  countRow(LOC("REPORT_SHOTS"), stats.homeShots, stats.awayShots);
  countRow(LOC("REPORT_ON_TARGET"), stats.homeOnTarget, stats.awayOnTarget);
  formatBuffer(homeText, "{:.2f}", static_cast<double>(stats.homeShotXG));
  formatBuffer(awayText, "{:.2f}", static_cast<double>(stats.awayShotXG));
  comparisonRow(LOC("REPORT_XG"), stats.homeShotXG, stats.awayShotXG,
                homeText.data(), awayText.data());
  formatBuffer(homeText, "{}/{}", stats.homePassesCompleted,
               stats.homePassesAttempted);
  formatBuffer(awayText, "{}/{}", stats.awayPassesCompleted,
               stats.awayPassesAttempted);
  comparisonRow(LOC("REPORT_PASSES"),
                static_cast<float>(stats.homePassesCompleted),
                static_cast<float>(stats.awayPassesCompleted), homeText.data(),
                awayText.data());
  countRow(LOC("REPORT_CORNERS"), stats.homeCorners, stats.awayCorners);
  countRow(LOC("REPORT_FOULS"), stats.homeFouls, stats.awayFouls);
  countRow(LOC("REPORT_OFFSIDES"), stats.homeOffsides, stats.awayOffsides);
  countRow(LOC("REPORT_SAVES"), stats.homeSaves, stats.awaySaves);
  formatBuffer(homeText, "{} / {}", stats.homeYellowCards, stats.homeRedCards);
  formatBuffer(awayText, "{} / {}", stats.awayYellowCards, stats.awayRedCards);
  comparisonRow(LOC("REPORT_CARDS"),
                static_cast<float>(stats.homeYellowCards + stats.homeRedCards),
                static_cast<float>(stats.awayYellowCards + stats.awayRedCards),
                homeText.data(), awayText.data());
  UI::endCard();
}

void MatchScene::refreshVisibleEvents()
{
  const auto& events = engine->getEvents();
  if (indexed_show_all != show_all_events || indexed_events > events.size())
  {
    visible_events.clear();
    indexed_events = 0;
    indexed_show_all = show_all_events;
  }
  for (; indexed_events < events.size(); ++indexed_events)
  {
    if (show_all_events || isKeyEvent(events[indexed_events].type))
      visible_events.push_back(indexed_events);
  }
}

void MatchScene::renderEvents(ImVec2 size)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginCard("##match_events", LOC("MATCH_EVENTS"), size);
  ImGui::Checkbox(LOC("MATCH_EVENTS_SHOW_ALL"), &show_all_events);
  refreshVisibleEvents();
  // A whole number of rows, so the list scrolled to the latest event never
  // shows half a row at the top (the content ends without the last row's
  // spacing).
  const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
  const float rowGap = ImGui::GetStyle().ItemSpacing.y;
  const float rows = std::max(
      1.0f,
      std::floor((ImGui::GetContentRegionAvail().y + rowGap) / rowHeight));
  ImGui::BeginChild("##match_event_list",
                    ImVec2(0.0f, rows * rowHeight - rowGap));
  const bool keepScrolledToLatest =
      ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
  const float minuteWidth =
      scaled(MatchSceneTuning::Panel::MINUTE_COLUMN_WIDTH);
  const float iconSize = scaled(MatchSceneTuning::Panel::EVENT_ICON_SIZE);
  const auto& events = engine->getEvents();
  ImGuiListClipper eventClipper;
  eventClipper.Begin(static_cast<int>(visible_events.size()), rowHeight);
  while (eventClipper.Step())
  {
    for (int row = eventClipper.DisplayStart; row < eventClipper.DisplayEnd;
         ++row)
    {
      const MatchEvent& event =
          events[visible_events[static_cast<std::size_t>(row)]];
      const float startX = ImGui::GetCursorPosX();
      const std::string minute = MatchClock::minuteLabel(
          event.timeMinute, event.period, event.addedMinute > 0.0f);
      ImGui::TextColored(palette.muted, "%s", minute.c_str());
      ImGui::SameLine(startX + minuteWidth);
      const ImVec2 iconOrigin = ImGui::GetCursorScreenPos();
      drawEventIcon(ImGui::GetWindowDrawList(),
                    ImVec2(iconOrigin.x + iconSize * 0.5f,
                           iconOrigin.y + ImGui::GetTextLineHeight() * 0.5f),
                    iconSize, event.type);
      ImGui::Dummy(ImVec2(iconSize, ImGui::GetTextLineHeight()));
      ImGui::SameLine();
      UI::textFitted(event.description, ImGui::GetContentRegionAvail().x,
                     eventColor(event.type));
    }
  }
  if (keepScrolledToLatest)
    ImGui::SetScrollHereY(MatchSceneTuning::Events::LATEST_SCROLL_RATIO);
  ImGui::EndChild();
  UI::endCard();
}

void MatchScene::renderSubstitutionsModal()
{
  const auto context = touchline();
  if (!context || !subs_panel.render(*context))
  {
    show_substitutions = false;
    dialogClosed();
  }
}

void MatchScene::renderTacticsModal()
{
  const auto context = touchline();
  if (!context || !tactics_panel.render(*context))
  {
    show_tactics = false;
    dialogClosed();
  }
}

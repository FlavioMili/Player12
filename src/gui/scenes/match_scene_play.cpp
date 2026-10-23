// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// Play mode of the live match: taking control of the managed club's side,
// the pause menu with the assistance options, the zoomed 2D follow view and
// the overlay (active-player marker, power bar, radar).

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/input_actions.h"
#include "gui/render/match_kit_colors.h"
#include "gui/render/match_renderer_2d.h"
#include "gui/scenes/match_scene.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/settings_manager.h"

namespace
{
/** Zoom of the 2D play view over the fitted pitch. */
constexpr float PLAY_ZOOM_2D = 2.0f;
/** Share of the gap to the follow point closed per second (2D). */
constexpr float FOLLOW_RATE_PER_SECOND = 4.0f;
/** The follow point leans this much toward the ball (0 = active player). */
constexpr float FOLLOW_BALL_SHARE = 0.55f;

float scaled(float value) { return value * Theme::scale(); }

PlayAutoSwitch autoSwitchFrom(int value)
{
  return static_cast<PlayAutoSwitch>(std::clamp(value, 0, 2));
}
}  // namespace

MatchPlayController::Options MatchScene::playOptions() const
{
  const Settings& settings = SettingsManager::instance()->get();
  MatchPlayController::Options options;
  options.autoSwitch = autoSwitchFrom(settings.play_auto_switch);
  options.passAssist =
      static_cast<std::uint8_t>(std::clamp(settings.play_pass_assist, 0, 2));
  options.deadZone = std::clamp(settings.play_dead_zone, 0.05f, 0.5f);
  return options;
}

bool MatchScene::canTakeControl() const
{
  return SettingsManager::instance()->get().play_mode && engine &&
         managed_is_home.has_value() && !match_finished &&
         !quick_result.valid() && !play.isActive() &&
         engine->getState() != MatchState::FULL_TIME;
}

void MatchScene::requestTakeControl()
{
  if (!canTakeControl()) return;
  // The match stops first; the manager starts playing when ready.
  is_paused = true;
  paused_for_dialog = false;
  play_confirm = true;
}

void MatchScene::startPlaying()
{
  play_confirm = false;
  if (!canTakeControl()) return;
  watch_speed = match_speed;
  watch_highlights = highlights_only;
  watch_camera = camera_mode;
  watch_focus = pitch_focus;
  engine->setPlayHalfMinutes(
      SettingsManager::instance()->get().play_half_minutes);
  // Physics and inputs stay in real time; only the match clock is compressed.
  match_speed = 1.0f;
  highlights_only = false;
  engine->setPlaybackMode(MatchPlaybackMode::FULL_MATCH);
  engine->setPlaybackSpeed(1.0f);
  engine->setHighlightPlaybackSpeed(1.0f);
  camera_mode = MatchCameraMode::PLAY;
  free_follow_ball = false;
  setPitchFocus(true);
  play_follow_ready = false;
  play.begin(*engine, *managed_is_home, playOptions());
  is_paused = false;
  paused_for_dialog = false;
}

bool MatchScene::canHandBack() const
{
  return play.isActive() && engine &&
         (is_paused || engine->getState() != MatchState::PLAYING);
}

void MatchScene::handBack()
{
  if (!play.isActive() || !engine) return;
  engine->setPlayHalfMinutes(0);
  play.end(*engine);
  play_menu = false;
  suspendKeyboardNavigation(false);
  setPlaybackSpeed(watch_speed);
  setHighlightsOnly(watch_highlights);
  camera_mode = watch_camera;
  setPitchFocus(watch_focus);
}

void MatchScene::openPlayMenu()
{
  if (!play.isActive() || match_finished) return;
  play_menu = true;
  play_menu_resume = false;
  is_paused = true;
  paused_for_dialog = false;
  play.clearPresses();
}

void MatchScene::closePlayMenu()
{
  play_menu = false;
  play_menu_resume = false;
  play.clearPresses();
}

void MatchScene::suspendKeyboardNavigation(bool suspend)
{
  if (suspend == nav_keyboard_suspended) return;
  ImGuiIO& io = ImGui::GetIO();
  if (suspend)
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
  else
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  nav_keyboard_suspended = suspend;
}

void MatchScene::updatePlay(float deltaTime)
{
  if (!play.isActive() || !engine) return;
  if (match_finished)
  {
    // Full time: the result is recorded with every player back to the AI.
    handBack();
    return;
  }
  const bool dialog = play_menu || play_confirm || show_substitutions ||
                      show_tactics || team_talk.isOpen();
  // The pause control (Esc, P, Start) opens the menu and, pressed again,
  // closes it: the same press never does both, since the menu itself does
  // not listen to Esc.
  if (play.takePauseRequest())
  {
    if (play_menu)
      play_menu_resume = true;
    else if (!dialog)
      openPlayMenu();
  }
  const bool driving = !is_paused && !dialog;
  suspendKeyboardNavigation(driving);
  // Real time while playing (the speed controls are hidden).
  // Buttons pressed while paused or in a dialog do nothing on the pitch.
  if (driving)
    play.update(*engine, deltaTime, play_basis);
  else
    play.clearPresses();
}

void MatchScene::onExit()
{
  if (play.isActive() && engine && !quick_result.valid()) play.end(*engine);
  suspendKeyboardNavigation(false);
  GUIScene::onExit();
}

MatchViewport MatchScene::playViewport2D(const MatchViewport& fitted,
                                         ImVec2 viewMin, ImVec2 viewMax)
{
  // Follow a point between the active footballer and the ball, damped with
  // real frame time so the camera glides at any frame rate.
  Vector2F focus = engine->getBall().position;
  const PlayerID active = engine->getControlledPlayer();
  for (const MatchPlayer& player : engine->getPlayers())
    if (active != 0 && player.player && player.player->getId() == active)
      focus = {
          player.position.x + (focus.x - player.position.x) * FOLLOW_BALL_SHARE,
          player.position.y +
              (focus.y - player.position.y) * FOLLOW_BALL_SHARE};
  if (!play_follow_ready)
  {
    play_follow = focus;
    play_follow_ready = true;
  }
  else
  {
    const float blend = 1.0f - std::exp(-FOLLOW_RATE_PER_SECOND *
                                        std::min(frame_seconds, 0.1f));
    play_follow.x += (focus.x - play_follow.x) * blend;
    play_follow.y += (focus.y - play_follow.y) * blend;
  }
  MatchViewport zoomed = fitted;
  zoomed.width = fitted.width * PLAY_ZOOM_2D;
  zoomed.height = fitted.height * PLAY_ZOOM_2D;
  const float centreX = (viewMin.x + viewMax.x) * 0.5f;
  const float centreY = (viewMin.y + viewMax.y) * 0.5f;
  // Never further than the pitch's own surround (the apron) past an edge.
  const float apron = scaled(MatchSceneTuning::Stadium::APRON_WIDTH);
  const auto place = [](float centre, float span, float follow, float low,
                        float high, float margin)
  {
    const float origin = centre - follow * span;
    const float lowest = high - span - margin;
    const float highest = low + margin;
    return lowest > highest ? (low + high - span) * 0.5f
                            : std::clamp(origin, lowest, highest);
  };
  zoomed.x =
      place(centreX, zoomed.width, play_follow.x, viewMin.x, viewMax.x, apron);
  zoomed.y =
      place(centreY, zoomed.height, play_follow.y, viewMin.y, viewMax.y, apron);
  return zoomed;
}

void MatchScene::renderPlayOverlay(const IMatchRenderer& renderer,
                                   ImVec2 viewMin, ImVec2 viewMax)
{
  if (!play.isActive()) return;
  const PlayerID active = engine->getControlledPlayer();
  // The stick follows the screen: screen pixels per metre along the pitch's
  // length and width at the active footballer, from the view's projection.
  for (const MatchPlayer& player : engine->getPlayers())
  {
    if (!player.player || player.player->getId() != active) continue;
    float originX = 0.0f;
    float originY = 0.0f;
    float lengthX = 0.0f;
    float lengthY = 0.0f;
    float widthX = 0.0f;
    float widthY = 0.0f;
    constexpr float METRE_X = 1.0f / MatchTuning::Pitch::LENGTH_METRES;
    constexpr float METRE_Y = 1.0f / MatchTuning::Pitch::WIDTH_METRES;
    if (renderer.projectPitch(player.position, 0.0f, originX, originY) &&
        renderer.projectPitch({player.position.x + METRE_X, player.position.y},
                              0.0f, lengthX, lengthY) &&
        renderer.projectPitch({player.position.x, player.position.y + METRE_Y},
                              0.0f, widthX, widthY))
    {
      play_basis = {lengthX - originX, lengthY - originY, widthX - originX,
                    widthY - originY};
    }
  }
  MatchPlayOverlayState state;
  state.activePlayer = active;
  state.nextSwitch = play.nextSwitch();
  state.charging = play.isCharging();
  state.power = play.chargeShare(*engine);
  // Radar dots in the shirt colours, as the players appear in either view.
  const MatchKits kits = chooseMatchKits(home_team_id, away_team_id);
  state.homeColor = kits.home.shirt;
  state.awayColor = kits.away.shirt;
  drawMatchPlayOverlay(*ImGui::GetWindowDrawList(), renderer, snapshot, state,
                       viewMin, viewMax);
}

void MatchScene::renderPlayButtons()
{
  if (!engine || !managed_is_home.has_value() || match_finished ||
      quick_result.valid())
    return;
  if (!play.isActive())
  {
    if (!canTakeControl()) return;
    // Before the first whistle this is the matchday "Play"; later it takes
    // over a watched match.
    const bool beforeKickOff = engine->getState() == MatchState::KICK_OFF &&
                               engine->getSimulatedSteps() == 0;
    const char* label =
        beforeKickOff ? LOC("MATCH_PLAY") : LOC("MATCH_TAKE_CONTROL");
    UI::sameLineIfFits(UI::buttonWidth(label));
    if (UI::primaryButton(label)) requestTakeControl();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", LOC("MATCH_PLAY_HINT"));
    return;
  }
  UI::sameLineIfFits(UI::buttonWidth(LOC("PLAY_MENU")));
  if (ImGui::Button(LOC("PLAY_MENU"))) openPlayMenu();
  UI::sameLineIfFits(UI::buttonWidth(LOC("MATCH_HAND_BACK")));
  ImGui::BeginDisabled(!canHandBack());
  if (ImGui::Button(LOC("MATCH_HAND_BACK"))) handBack();
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", LOC("MATCH_HAND_BACK_HINT"));
}

void MatchScene::renderPlayControlsHelp() const
{
  const ImVec4 muted = Theme::palette().muted;
  ImGui::TextColored(muted, "%s", LOC("PLAY_CONTROLS_KEYBOARD"));
  // The keys as bound in Settings > Controls (the help is only built while
  // a dialog shows it).
  namespace Ids = Input::Ids;
  const auto key = [](std::string_view id)
  {
    const auto action = Input::registry().find(id);
    return action ? Input::registry().label(*action) : std::string("-");
  };
  const std::string pass = key(Ids::PLAY_PASS);
  const std::string shoot = key(Ids::PLAY_SHOOT);
  ImGui::TextWrapped(
      "%s",
      fmt::sprintf(LOC("PLAY_KEYS_TEXT"), key(Ids::PLAY_UP).c_str(),
                   key(Ids::PLAY_LEFT).c_str(), key(Ids::PLAY_DOWN).c_str(),
                   key(Ids::PLAY_RIGHT).c_str(), pass.c_str(), shoot.c_str(),
                   key(Ids::PLAY_THROUGH).c_str(), key(Ids::PLAY_LOB).c_str(),
                   key(Ids::PLAY_SWITCH).c_str(), key(Ids::PLAY_JOCKEY).c_str(),
                   pass.c_str(), shoot.c_str(), key(Ids::PLAY_PAUSE).c_str())
          .c_str());
  ImGui::Spacing();
  ImGui::TextColored(muted, "%s", LOC("PLAY_CONTROLS_GAMEPAD"));
  ImGui::TextWrapped("%s", LOC("PLAY_PAD_TEXT"));
  ImGui::Spacing();
  if (play.hasGamepad())
    ImGui::TextUnformatted(
        fmt::sprintf(LOC("PLAY_GAMEPAD_CONNECTED"), play.gamepadName())
            .c_str());
  else
    ImGui::TextColored(muted, "%s", LOC("PLAY_NO_GAMEPAD"));
}

void MatchScene::renderPlayConfirm()
{
  if (!play_confirm) return;
  constexpr const char* ID = "###play_confirm";
  if (!ImGui::IsPopupOpen(ID)) ImGui::OpenPopup(ID);
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(
      ImVec2(std::min(scaled(560.0f), viewport->WorkSize.x * 0.92f), 0.0f));
  if (const std::string title = std::string(LOC("PLAY_CONFIRM_TITLE")) + ID;
      !ImGui::BeginPopupModal(
          title.c_str(), nullptr,
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize))
    return;
  const std::string& club = *managed_is_home ? home_name : away_name;
  ImGui::TextWrapped(
      "%s", fmt::sprintf(LOC("PLAY_CONFIRM_TEXT"), club.c_str()).c_str());
  ImGui::Spacing();
  renderPlayControlsHelp();
  ImGui::Spacing();
  if (UI::primaryButton(LOC("PLAY_START")))
  {
    ImGui::CloseCurrentPopup();
    startPlaying();
  }
  ImGui::SameLine();
  if (UI::secondaryButton(LOC("PLAY_CANCEL")) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape, false))
  {
    ImGui::CloseCurrentPopup();
    play_confirm = false;
  }
  ImGui::EndPopup();
}

void MatchScene::renderPlayMenu()
{
  // A dialog chosen from the menu opens once the menu has gone.
  if (!play_menu && play_menu_next != PlayMenuNext::NONE &&
      !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
  {
    if (play_menu_next == PlayMenuNext::TACTICS)
      showTactics(true);
    else
      showSubstitutions(true);
    play_menu_next = PlayMenuNext::NONE;
  }
  constexpr const char* ID = "###play_menu";
  if (!play_menu)
  {
    // Closed from outside the popup (full time, hand-back): close it in
    // ImGui too, so it does not block the other dialogs.
    if (ImGui::IsPopupOpen(ID) &&
        ImGui::BeginPopupModal(ID, nullptr, ImGuiWindowFlags_NoSavedSettings))
    {
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    return;
  }
  if (!ImGui::IsPopupOpen(ID)) ImGui::OpenPopup(ID);
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(
      ImVec2(std::min(scaled(560.0f), viewport->WorkSize.x * 0.92f), 0.0f));
  if (const std::string title = std::string(LOC("PLAY_MENU_TITLE")) + ID;
      !ImGui::BeginPopupModal(
          title.c_str(), nullptr,
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoResize))
    return;
  const auto close = [this]
  {
    ImGui::CloseCurrentPopup();
    closePlayMenu();
  };
  if (UI::primaryButton(LOC("MATCH_RESUME")) || play_menu_resume)
  {
    close();
    is_paused = false;
  }
  ImGui::SameLine();
  if (UI::secondaryButton(LOC("MATCH_TACTICS_TITLE")))
  {
    close();
    play_menu_next = PlayMenuNext::TACTICS;
  }
  ImGui::SameLine();
  if (UI::secondaryButton(LOC("SUBSTITUTION_TITLE")))
  {
    close();
    play_menu_next = PlayMenuNext::SUBSTITUTIONS;
  }
  ImGui::SameLine();
  if (UI::secondaryButton(LOC("MATCH_HAND_BACK")))
  {
    close();
    handBack();
  }

  // Assistance: saved with the settings and applied at once.
  ImGui::SeparatorText(LOC("PLAY_ASSISTANCE"));
  Settings& settings = SettingsManager::instance()->get();
  bool changed = false;
  const auto choice = [&](const char* label, int& value,
                          const std::array<const char*, 3>& options)
  {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    for (int index = 0; index < 3; ++index)
    {
      ImGui::SameLine();
      ImGui::PushID(label);
      ImGui::PushID(index);
      if (ImGui::RadioButton(options[static_cast<std::size_t>(index)],
                             value == index))
      {
        value = index;
        changed = true;
      }
      ImGui::PopID();
      ImGui::PopID();
    }
  };
  choice(LOC("PLAY_AUTO_SWITCH"), settings.play_auto_switch,
         {LOC("PLAY_SWITCH_MANUAL"), LOC("PLAY_SWITCH_ASSISTED"),
          LOC("PLAY_SWITCH_AUTO")});
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", LOC("PLAY_AUTO_SWITCH_HINT"));
  choice(LOC("PLAY_PASS_ASSIST"), settings.play_pass_assist,
         {LOC("PLAY_ASSIST_NONE"), LOC("PLAY_ASSIST_NORMAL"),
          LOC("PLAY_ASSIST_STRONG")});
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(LOC("PLAY_DEAD_ZONE"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(scaled(180.0f));
  // The slider applies while dragged and is saved when let go.
  if (ImGui::SliderFloat("##play_dead_zone", &settings.play_dead_zone, 0.05f,
                         0.5f, "%.2f"))
    play.setOptions(playOptions());
  if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
  if (changed)
  {
    SettingsManager::instance()->save();
    play.setOptions(playOptions());
  }

  ImGui::SeparatorText(LOC("PLAY_CONTROLS"));
  renderPlayControlsHelp();
  ImGui::EndPopup();
}

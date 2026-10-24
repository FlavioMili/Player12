// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/match_play_controller.h"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

#include "global/logger.h"
#include "gui/input_actions.h"

namespace
{
/** Stick changes below this are noise, not a new input. */
constexpr float STICK_QUANTUM = 0.02f;
/** Triggers count as held past this share of their travel. */
constexpr float TRIGGER_THRESHOLD = 0.35f;
/** Minimum seconds between automatic switches (assisted, auto). */
constexpr double ASSISTED_SWITCH_HOLD_SECONDS = 1.0;
constexpr double AUTO_SWITCH_HOLD_SECONDS = 0.6;
/** Assisted switching waits for the stick to rest this long. */
constexpr double ASSISTED_IDLE_SECONDS = 0.3;
/** Repeated manual presses within this time cycle through candidates. */
constexpr double MANUAL_CYCLE_SECONDS = 1.5;

float quantize(float value)
{
  return std::round(value / STICK_QUANTUM) * STICK_QUANTUM;
}

bool sameInput(const MatchPlayerInput& first, const MatchPlayerInput& second)
{
  return first.moveX == second.moveX && first.moveY == second.moveY &&
         first.sprint == second.sprint && first.action == second.action &&
         first.aimX == second.aimX && first.aimY == second.aimY &&
         first.power == second.power && first.jockey == second.jockey &&
         first.passAssist == second.passAssist;
}

const MatchPlayer* findPlayer(const MatchEngine& engine, PlayerID id)
{
  if (id == 0) return nullptr;
  for (const MatchPlayer& player : engine.getPlayers())
    if (player.player && player.player->getId() == id) return &player;
  return nullptr;
}
}  // namespace

MatchPlayController::~MatchPlayController()
{
  closeGamepad();
  if (gamepad_subsystem) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void MatchPlayController::begin(MatchEngine& engine, bool homeTeam,
                                const Options& options)
{
  settings = options;
  home = homeTeam;
  active = true;
  clearPresses();
  submitted = {};
  submitted_any = false;
  last_switch_seconds = -1e9;
  last_manual_seconds = -1e9;
  manual_skip = 0;
  idle_since = engine.getSimulatedSeconds();
  was_idle = true;
  if (!gamepad_subsystem)
  {
    // Pads are only needed in play mode; the subsystem starts on demand.
    gamepad_subsystem = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    if (!gamepad_subsystem)
      Logger::warn(std::string("Gamepad support unavailable: ") +
                   SDL_GetError());
  }
  if (gamepad_subsystem && !gamepad) openFirstGamepad();
  switchTo(engine, engine.suggestActivePlayer(home, 0));
}

void MatchPlayController::end(MatchEngine& engine)
{
  if (!active) return;
  active = false;
  clearPresses();
  engine.setControlledPlayer(0);
}

MatchPlayController::Control MatchPlayController::controlForKey(
    const SDL_KeyboardEvent& key)
{
  // Sprint is held with the other keys, so it stays on the modifier.
  if (key.scancode == SDL_SCANCODE_LSHIFT ||
      key.scancode == SDL_SCANCODE_RSHIFT)
    return SPRINT;
  static constexpr std::array<std::pair<std::string_view, Control>, 11>
      BINDINGS{{{Input::Ids::PLAY_UP, UP},
                {Input::Ids::PLAY_DOWN, DOWN},
                {Input::Ids::PLAY_LEFT, LEFT},
                {Input::Ids::PLAY_RIGHT, RIGHT},
                {Input::Ids::PLAY_PASS, PASS},
                {Input::Ids::PLAY_SHOOT, SHOOT},
                {Input::Ids::PLAY_THROUGH, THROUGH},
                {Input::Ids::PLAY_LOB, LOB},
                {Input::Ids::PLAY_SWITCH, SWITCH},
                {Input::Ids::PLAY_JOCKEY, JOCKEY},
                {Input::Ids::PLAY_PAUSE, PAUSE}}};
  const Input::ActionRegistry& registry = Input::registry();
  for (const auto& [id, control] : BINDINGS)
    if (registry.matchesKey(id, key)) return control;
  return CONTROL_COUNT;
}

MatchPlayController::Control MatchPlayController::controlForButton(int button)
{
  // Standard layout: face buttons by position, not by printed label.
  switch (button)
  {
    case SDL_GAMEPAD_BUTTON_SOUTH:
      return PASS;
    case SDL_GAMEPAD_BUTTON_EAST:
      return SHOOT;
    case SDL_GAMEPAD_BUTTON_NORTH:
      return THROUGH;
    case SDL_GAMEPAD_BUTTON_WEST:
      return LOB;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
      return SWITCH;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
      return SPRINT;
    case SDL_GAMEPAD_BUTTON_START:
      return PAUSE;
    default:
      return CONTROL_COUNT;
  }
}

bool MatchPlayController::handleEvent(const SDL_Event& event)
{
  switch (event.type)
  {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    {
      const SDL_Scancode scancode = event.key.scancode;
      if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT) return false;
      const bool down = event.type == SDL_EVENT_KEY_DOWN;
      std::uint8_t& holding = keys[static_cast<std::size_t>(scancode)];
      if (!down)
      {
        // The release ends what this key's press started.
        if (holding == 0) return controlForKey(event.key) != CONTROL_COUNT;
        const auto control = static_cast<Control>(holding - 1);
        holding = 0;
        if (keyHolds[control] > 0) --keyHolds[control];
        release(control);
        return true;
      }
      if (holding != 0) return true;  // auto-repeat of a held key
      const Control control = controlForKey(event.key);
      if (control == CONTROL_COUNT) return false;
      holding = static_cast<std::uint8_t>(control + 1);
      ++keyHolds[control];
      press(control);
      return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    {
      if (!gamepad || event.gbutton.which != SDL_GetGamepadID(gamepad))
        return false;
      const Control control = controlForButton(event.gbutton.button);
      if (control == CONTROL_COUNT) return false;
      if (const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
          down != padHolds[control])
      {
        padHolds[control] = down;
        if (down)
          press(control);
        else
          release(control);
      }
      return true;
    }
    case SDL_EVENT_GAMEPAD_ADDED:
      if (!gamepad) openGamepad(event.gdevice.which);
      return false;
    case SDL_EVENT_GAMEPAD_REMOVED:
      if (gamepad && event.gdevice.which == SDL_GetGamepadID(gamepad))
      {
        // Pulling the cable is not a release: a held shot or pass is
        // dropped, not played.
        clearPresses();
        closeGamepad();
        openFirstGamepad();
      }
      return false;
    default:
      return false;
  }
}

void MatchPlayController::press(Control control)
{
  if (control == PAUSE)
  {
    pause_requested = true;
    return;
  }
  pressed[control] = true;
}

void MatchPlayController::release(Control control)
{
  if (control != PAUSE) released[control] = true;
}

bool MatchPlayController::held(Control control) const
{
  return keyHolds[control] > 0 || padHolds[control];
}

void MatchPlayController::clearPresses()
{
  pressed.fill(false);
  released.fill(false);
  pause_requested = false;
  charging = MatchInputAction::NONE;
  pending = MatchInputAction::NONE;
  pending_power = 0.0f;
}

bool MatchPlayController::takePauseRequest()
{
  const bool requested = pause_requested;
  pause_requested = false;
  return requested;
}

void MatchPlayController::openGamepad(SDL_JoystickID which)
{
  if (!gamepad_subsystem || !SDL_IsGamepad(which)) return;
  gamepad = SDL_OpenGamepad(which);
  padHolds.fill(false);
  if (gamepad) Logger::info(std::string("Gamepad connected: ") + gamepadName());
}

void MatchPlayController::openFirstGamepad()
{
  if (!gamepad_subsystem) return;
  int count = 0;
  SDL_JoystickID* pads = SDL_GetGamepads(&count);
  if (!pads) return;
  for (int index = 0; index < count && !gamepad; ++index)
    openGamepad(pads[index]);
  SDL_free(pads);
}

void MatchPlayController::closeGamepad()
{
  if (!gamepad) return;
  SDL_CloseGamepad(gamepad);
  gamepad = nullptr;
  padHolds.fill(false);
}

const char* MatchPlayController::gamepadName() const
{
  const char* name = gamepad ? SDL_GetGamepadName(gamepad) : nullptr;
  return name ? name : "";
}

void MatchPlayController::readStick(float& x, float& y) const
{
  x = 0.0f;
  y = 0.0f;
  if (gamepad)
  {
    constexpr float AXIS_SCALE = 1.0f / 32767.0f;
    x = static_cast<float>(
            SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX)) *
        AXIS_SCALE;
    y = static_cast<float>(
            SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY)) *
        AXIS_SCALE;
    // Radial dead zone, rescaled so the stick still reaches full speed.
    const float magnitude = std::hypot(x, y);
    const float deadZone = std::clamp(settings.deadZone, 0.0f, 0.9f);
    if (magnitude <= deadZone)
    {
      x = 0.0f;
      y = 0.0f;
    }
    else
    {
      const float scale =
          std::min(1.0f, (magnitude - deadZone) / (1.0f - deadZone)) /
          magnitude;
      x *= scale;
      y *= scale;
    }
  }
  // The movement keys replace the stick while held.
  const float keyX =
      (keyHolds[RIGHT] > 0 ? 1.0f : 0.0f) - (keyHolds[LEFT] > 0 ? 1.0f : 0.0f);
  const float keyY =
      (keyHolds[DOWN] > 0 ? 1.0f : 0.0f) - (keyHolds[UP] > 0 ? 1.0f : 0.0f);
  if (keyX != 0.0f || keyY != 0.0f)
  {
    const float length = std::hypot(keyX, keyY);
    x = keyX / length;
    y = keyY / length;
  }
}

void MatchPlayController::switchTo(MatchEngine& engine, PlayerID player)
{
  using enum MatchInputAction;
  if (player == 0 || player == engine.getControlledPlayer()) return;
  if (!engine.setControlledPlayer(player)) return;
  last_switch_seconds = engine.getSimulatedSeconds();
  // The new man starts from the held stick (the switch record clears it).
  charging = NONE;
  pending = NONE;
  submitted_any = false;
  MatchPlayerInput carried = submitted;
  carried.action = NONE;
  carried.power = 0.0f;
  engine.submitInput(carried);
  submitted = carried;
  submitted_any = true;
}

void MatchPlayController::applySwitchingRules(MatchEngine& engine,
                                              PlayerID current, bool idle)
{
  const MatchBall& ball = engine.getBall();
  const double now = engine.getSimulatedSeconds();
  const MatchPlayer* carrier = nullptr;
  for (const MatchPlayer& player : engine.getPlayers())
    if (player.player && player.player == ball.possessedBy) carrier = &player;

  // The carrier is always the man (whatever the level), unless it is the
  // keeper: goalkeeping stays with the AI.
  if (carrier && carrier->isHomeTeam == home)
  {
    if (!carrier->isGoalkeeper && carrier->onPitch &&
        carrier->player->getId() != current)
      switchTo(engine, carrier->player->getId());
    return;
  }
  if (settings.autoSwitch == PlayAutoSwitch::OFF) return;
  // The side's pass: its receiver, so he can be moved onto the ball.
  if (!carrier && ball.isPass && ball.passByHome == home)
  {
    if (const MatchPlayer* receiver =
            ball.intendedReceiver
                ? findPlayer(engine, ball.intendedReceiver->getId())
                : nullptr;
        receiver && receiver->onPitch && !receiver->isGoalkeeper &&
        receiver->player->getId() != current)
      switchTo(engine, receiver->player->getId());
    return;
  }
  // Defending or a loose ball: the best placed team-mate, not too often.
  const bool autoLevel = settings.autoSwitch == PlayAutoSwitch::AUTO;
  if (const double hold =
          autoLevel ? AUTO_SWITCH_HOLD_SECONDS : ASSISTED_SWITCH_HOLD_SECONDS;
      now - last_switch_seconds < hold)
    return;
  if (!autoLevel && !(idle && now - idle_since >= ASSISTED_IDLE_SECONDS))
    return;
  const PlayerID best = engine.suggestActivePlayer(home, current);
  if (best != 0 && best != current) switchTo(engine, best);
}

void MatchPlayController::update(MatchEngine& engine, float simulatedSeconds,
                                 const PlayScreenBasis& basis)
{
  if (!active) return;
  const MatchBall& ball = engine.getBall();
  const double now = engine.getSimulatedSeconds();

  float stickX = 0.0f;
  float stickY = 0.0f;
  readStick(stickX, stickY);
  const bool idle = stickX == 0.0f && stickY == 0.0f;
  if (idle && !was_idle) idle_since = now;
  was_idle = idle;

  // Who is the man: a lost one (substituted, sent off) is replaced at once;
  // otherwise the switching rules apply while the ball is in play.
  PlayerID current = engine.getControlledPlayer();
  if (const MatchPlayer* man = findPlayer(engine, current);
      !man || !man->onPitch || man->isGoalkeeper)
  {
    switchTo(engine, engine.suggestActivePlayer(home, 0));
  }
  else if (engine.getState() != MatchState::PLAYING &&
           ball.possessedBy != nullptr)
  {
    const MatchPlayer* taker = findPlayer(engine, ball.possessedBy->getId());
    if (taker != nullptr && taker->isHomeTeam == home && !taker->isGoalkeeper &&
        taker->player->getId() != current)
    {
      switchTo(engine, taker->player->getId());
    }
  }
  else if (engine.getState() == MatchState::PLAYING)
  {
    applySwitchingRules(engine, current, idle);
  }
  current = engine.getControlledPlayer();

  const bool hasBall = ball.possessedBy && ball.possessedBy->getId() == current;
  // Defending: an opponent has the ball. Otherwise the buttons attack (a
  // press before the ball arrives is a first-time finish or pass).
  bool defending = false;
  for (const MatchPlayer& player : engine.getPlayers())
    if (player.player && player.player == ball.possessedBy)
      defending = player.isHomeTeam != home;

  // Manual switch: the next best candidate; repeated presses cycle.
  if (pressed[SWITCH] && !hasBall)
  {
    const bool cycling = now - last_manual_seconds < MANUAL_CYCLE_SECONDS;
    const PlayerID target =
        engine.nextSwitchCandidate(home, current, cycling ? manual_skip : 0);
    if (target != 0)
    {
      manual_skip = current;
      switchTo(engine, target);
      last_manual_seconds = now;
      current = target;
    }
  }
  next_switch = engine.nextSwitchCandidate(
      home, current,
      now - last_manual_seconds < MANUAL_CYCLE_SECONDS ? manual_skip : 0);

  // Buttons: one-shot actions, or a hold that fills the power bar and acts
  // on release.
  const auto heldShare = [&]
  {
    return std::clamp(
        static_cast<float>(engine.getSimulatedSteps() - charge_step) *
            MatchTuning::Timing::FIXED_STEP_SECONDS / FULL_POWER_SECONDS,
        0.0f, 1.0f);
  };
  const auto startCharge = [&](MatchInputAction action)
  {
    charging = action;
    charge_step = engine.getSimulatedSteps();
  };
  if (defending)
  {
    charging = MatchInputAction::NONE;
    if (pressed[PASS])
      pending = MatchInputAction::TACKLE;
    else if (pressed[SHOOT])
      pending = MatchInputAction::SLIDE_TACKLE;
  }
  else
  {
    if (pressed[PASS])
    {
      pending = MatchInputAction::PASS;
      pending_power = 0.0f;
    }
    if (pressed[SHOOT]) startCharge(MatchInputAction::SHOOT);
    if (pressed[THROUGH]) startCharge(MatchInputAction::THROUGH_BALL);
    if (pressed[LOB]) startCharge(MatchInputAction::LOFTED_PASS);
    Control chargeControl = CONTROL_COUNT;
    if (charging == MatchInputAction::SHOOT)
      chargeControl = SHOOT;
    else if (charging == MatchInputAction::THROUGH_BALL)
      chargeControl = THROUGH;
    else if (charging == MatchInputAction::LOFTED_PASS)
      chargeControl = LOB;
    if (chargeControl != CONTROL_COUNT &&
        (released[chargeControl] || !held(chargeControl)))
    {
      pending = charging;
      // At least a sliver of power: a tap is a soft, deliberate ball.
      pending_power = std::max(heldShare(), 0.05f);
      charging = MatchInputAction::NONE;
    }
  }
  pressed.fill(false);
  released.fill(false);

  // Sample once per simulation step, on the frame the engine steps.
  if (!engine.stepsWithin(simulatedSeconds)) return;
  MatchPlayerInput input;
  // Screen direction to pitch metres through the view's projection.
  if (const float determinant =
          basis.lengthX * basis.widthY - basis.widthX * basis.lengthY;
      !idle && std::abs(determinant) > 1e-6f)
  {
    const float alongX =
        (stickX * basis.widthY - basis.widthX * stickY) / determinant;
    const float alongY =
        (basis.lengthX * stickY - stickX * basis.lengthY) / determinant;
    const float length = std::hypot(alongX, alongY);
    const float magnitude = std::min(1.0f, std::hypot(stickX, stickY));
    if (length > 1e-6f)
    {
      input.moveX = quantize(alongX / length * magnitude);
      input.moveY = quantize(alongY / length * magnitude);
    }
  }
  input.sprint =
      held(SPRINT) ||
      (gamepad && SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >
                      TRIGGER_THRESHOLD * 32767.0f);
  input.jockey =
      defending &&
      (held(JOCKEY) ||
       (gamepad && SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >
                       TRIGGER_THRESHOLD * 32767.0f));
  input.passAssist = settings.passAssist;
  // The same action twice in a row needs a release in between (the engine
  // fires on the press): this step releases, the next one presses again.
  const bool repeat = submitted_any && pending != MatchInputAction::NONE &&
                      submitted.action == pending;
  if (!repeat)
  {
    input.action = pending;
    input.power = pending == MatchInputAction::NONE ? 0.0f : pending_power;
  }
  if (submitted_any && sameInput(input, submitted)) return;
  engine.submitInput(input);
  submitted = input;
  submitted_any = true;
  if (!repeat)
  {
    pending = MatchInputAction::NONE;
    pending_power = 0.0f;
  }
}

float MatchPlayController::chargeShare(const MatchEngine& engine) const
{
  if (charging == MatchInputAction::NONE) return 0.0f;
  return std::clamp(
      static_cast<float>(engine.getSimulatedSteps() - charge_step) *
          MatchTuning::Timing::FIXED_STEP_SECONDS / FULL_POWER_SECONDS,
      0.0f, 1.0f);
}

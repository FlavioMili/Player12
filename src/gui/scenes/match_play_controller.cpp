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
#include <limits>
#include <span>
#include <string_view>
#include <utility>

#include "global/logger.h"
#include "gui/input_actions.h"

namespace
{
/** Stick changes below this are noise, not a new input. */
constexpr float STICK_QUANTUM = 0.02F;
/** Triggers count as held past this share of their travel. */
constexpr float TRIGGER_THRESHOLD = 0.35F;
/** SDL reports positive stick and trigger travel on a signed 16-bit axis. */
constexpr float GAMEPAD_AXIS_LIMIT =
    static_cast<float>(std::numeric_limits<Sint16>::max());
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

[[nodiscard]] bool triggerHeld(SDL_Gamepad* gamepad, SDL_GamepadAxis axis)
{
  return gamepad != nullptr &&
         static_cast<float>(SDL_GetGamepadAxis(gamepad, axis)) >
             TRIGGER_THRESHOLD * GAMEPAD_AXIS_LIMIT;
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
  if (id == 0)
  {
    return nullptr;
  }
  for (const MatchPlayer& player : engine.getPlayers())
  {
    if ((player.player != nullptr) && player.player->getId() == id)
    {
      return &player;
    }
  }
  return nullptr;
}
}  // namespace

MatchPlayController::~MatchPlayController()
{
  closeGamepad();
  if (gamepad_subsystem)
  {
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
  }
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
    {
      Logger::warn(std::string("Gamepad support unavailable: ") +
                   SDL_GetError());
    }
  }
  if (gamepad_subsystem && (gamepad == nullptr))
  {
    openFirstGamepad();
  }
  switchTo(engine, engine.suggestActivePlayer(home, 0));
}

void MatchPlayController::end(MatchEngine& engine)
{
  if (!active)
  {
    return;
  }
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
  {
    return SPRINT;
  }
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
  {
    if (registry.matchesKey(id, key))
    {
      return control;
    }
  }
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
      if (scancode < 0 || scancode >= SDL_SCANCODE_COUNT)
      {
        return false;
      }
      const bool down = event.type == SDL_EVENT_KEY_DOWN;
      std::uint8_t& holding = keys.at(static_cast<std::size_t>(scancode));
      if (!down)
      {
        // The release ends what this key's press started.
        if (holding == 0)
        {
          return controlForKey(event.key) != CONTROL_COUNT;
        }
        const auto control = static_cast<Control>(holding - 1);
        holding = 0;
        if (keyHolds.at(control) > 0)
        {
          --keyHolds.at(control);
        }
        release(control);
        return true;
      }
      if (holding != 0)
      {
        return true;  // auto-repeat of a held key
      }
      const Control control = controlForKey(event.key);
      if (control == CONTROL_COUNT)
      {
        return false;
      }
      holding = static_cast<std::uint8_t>(control + 1);
      ++keyHolds.at(control);
      press(control);
      return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    {
      if ((gamepad == nullptr) ||
          event.gbutton.which != SDL_GetGamepadID(gamepad))
      {
        return false;
      }
      const Control control = controlForButton(event.gbutton.button);
      if (control == CONTROL_COUNT)
      {
        return false;
      }
      if (const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
          down != padHolds.at(control))
      {
        padHolds.at(control) = down;
        if (down)
        {
          press(control);
        }
        else
        {
          release(control);
        }
      }
      return true;
    }
    case SDL_EVENT_GAMEPAD_ADDED:
      if (gamepad == nullptr)
      {
        openGamepad(event.gdevice.which);
      }
      return false;
    case SDL_EVENT_GAMEPAD_REMOVED:
      if ((gamepad != nullptr) &&
          event.gdevice.which == SDL_GetGamepadID(gamepad))
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
  pressed.at(control) = true;
}

void MatchPlayController::release(Control control)
{
  if (control != PAUSE)
  {
    released.at(control) = true;
  }
}

bool MatchPlayController::held(Control control) const
{
  return keyHolds.at(control) > 0 || padHolds.at(control);
}

void MatchPlayController::clearPresses()
{
  pressed.fill(false);
  released.fill(false);
  pause_requested = false;
  charging = MatchInputAction::NONE;
  pending = MatchInputAction::NONE;
  pending_power = 0.0F;
}

bool MatchPlayController::takePauseRequest()
{
  const bool requested = pause_requested;
  pause_requested = false;
  return requested;
}

void MatchPlayController::openGamepad(SDL_JoystickID which)
{
  if (!gamepad_subsystem || !SDL_IsGamepad(which))
  {
    return;
  }
  gamepad = SDL_OpenGamepad(which);
  padHolds.fill(false);
  if (gamepad != nullptr)
  {
    Logger::info(std::string("Gamepad connected: ") + gamepadName());
  }
}

void MatchPlayController::openFirstGamepad()
{
  if (!gamepad_subsystem)
  {
    return;
  }
  int count = 0;
  SDL_JoystickID* pads = SDL_GetGamepads(&count);
  if (pads == nullptr)
  {
    return;
  }
  const std::span<const SDL_JoystickID> available(
      pads, static_cast<std::size_t>(std::max(count, 0)));
  for (const SDL_JoystickID pad : available)
  {
    if (gamepad != nullptr)
    {
      break;
    }
    openGamepad(pad);
  }
  SDL_free(pads);
}

void MatchPlayController::closeGamepad()
{
  if (gamepad == nullptr)
  {
    return;
  }
  SDL_CloseGamepad(gamepad);
  gamepad = nullptr;
  padHolds.fill(false);
}

const char* MatchPlayController::gamepadName() const
{
  const char* name =
      (gamepad != nullptr) ? SDL_GetGamepadName(gamepad) : nullptr;
  return (name != nullptr) ? name : "";
}

void MatchPlayController::readStick(float& x, float& y) const
{
  x = 0.0F;
  y = 0.0F;
  if (gamepad != nullptr)
  {
    constexpr float AXIS_SCALE = 1.0F / GAMEPAD_AXIS_LIMIT;
    x = static_cast<float>(
            SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX)) *
        AXIS_SCALE;
    y = static_cast<float>(
            SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY)) *
        AXIS_SCALE;
    // Radial dead zone, rescaled so the stick still reaches full speed.
    const float magnitude = std::hypot(x, y);
    const float deadZone = std::clamp(settings.deadZone, 0.0F, 0.9F);
    if (magnitude <= deadZone)
    {
      x = 0.0F;
      y = 0.0F;
    }
    else
    {
      const float scale =
          std::min(1.0F, (magnitude - deadZone) / (1.0F - deadZone)) /
          magnitude;
      x *= scale;
      y *= scale;
    }
  }
  // The movement keys replace the stick while held.
  const float keyX =
      (keyHolds[RIGHT] > 0 ? 1.0F : 0.0F) - (keyHolds[LEFT] > 0 ? 1.0F : 0.0F);
  const float keyY =
      (keyHolds[DOWN] > 0 ? 1.0F : 0.0F) - (keyHolds[UP] > 0 ? 1.0F : 0.0F);
  if (keyX != 0.0F || keyY != 0.0F)
  {
    const float length = std::hypot(keyX, keyY);
    x = keyX / length;
    y = keyY / length;
  }
}

void MatchPlayController::switchTo(MatchEngine& engine, PlayerID player)
{
  using enum MatchInputAction;
  if (player == 0 || player == engine.getControlledPlayer())
  {
    return;
  }
  if (!engine.setControlledPlayer(player))
  {
    return;
  }
  last_switch_seconds = engine.getSimulatedSeconds();
  // The new man starts from the held stick (the switch record clears it).
  charging = NONE;
  pending = NONE;
  submitted_any = false;
  MatchPlayerInput carried = submitted;
  carried.action = NONE;
  carried.power = 0.0F;
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
  {
    if ((player.player != nullptr) &&
        player.player == engine.getPossessionAttachment())
    {
      carrier = &player;
    }
  }

  // The carrier is always the man (whatever the level), unless it is the
  // keeper: goalkeeping stays with the AI.
  if ((carrier != nullptr) && carrier->isHomeTeam == home)
  {
    if (!carrier->isGoalkeeper && carrier->onPitch &&
        carrier->player->getId() != current)
    {
      switchTo(engine, carrier->player->getId());
    }
    return;
  }
  if (settings.autoSwitch == PlayAutoSwitch::OFF)
  {
    return;
  }
  // The side's pass: its receiver, so he can be moved onto the ball.
  if ((carrier == nullptr) && ball.possessionState().isPass &&
      ball.possessionState().passByHome == home)
  {
    const MatchPlayer* receiver =
        (engine.getExpectedReceiver() != nullptr)
            ? findPlayer(engine, engine.getExpectedReceiver()->getId())
            : nullptr;
    if ((receiver != nullptr) && receiver->onPitch && !receiver->isGoalkeeper &&
        receiver->player->getId() != current)
    {
      switchTo(engine, receiver->player->getId());
    }
    return;
  }
  // Defending or a loose ball: the best placed team-mate, not too often.
  const bool autoLevel = settings.autoSwitch == PlayAutoSwitch::AUTO;
  if (const double hold =
          autoLevel ? AUTO_SWITCH_HOLD_SECONDS : ASSISTED_SWITCH_HOLD_SECONDS;
      now - last_switch_seconds < hold)
  {
    return;
  }
  if (!autoLevel && !(idle && now - idle_since >= ASSISTED_IDLE_SECONDS))
  {
    return;
  }
  const PlayerID best = engine.suggestActivePlayer(home, current);
  if (best != 0 && best != current)
  {
    switchTo(engine, best);
  }
}

void MatchPlayController::update(MatchEngine& engine, float simulatedSeconds,
                                 const PlayScreenBasis& basis)
{
  if (!active)
  {
    return;
  }
  const double now = engine.getSimulatedSeconds();

  float stickX = 0.0F;
  float stickY = 0.0F;
  readStick(stickX, stickY);
  const bool idle = stickX == 0.0F && stickY == 0.0F;
  if (idle && !was_idle)
  {
    idle_since = now;
  }
  was_idle = idle;

  // Who is the man: a lost one (substituted, sent off) is replaced at once;
  // otherwise the switching rules apply while the ball is in play.
  PlayerID current = engine.getControlledPlayer();
  if (const MatchPlayer* man = findPlayer(engine, current);
      (man == nullptr) || !man->onPitch || man->isGoalkeeper)
  {
    switchTo(engine, engine.suggestActivePlayer(home, 0));
  }
  else if (engine.getState() != MatchState::PLAYING &&
           engine.getPossessionAttachment() != nullptr)
  {
    const MatchPlayer* taker =
        findPlayer(engine, engine.getPossessionAttachment()->getId());
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

  const bool hasBall = (engine.getPossessionAttachment() != nullptr) &&
                       engine.getPossessionAttachment()->getId() == current;
  // Defending: an opponent has the ball. Otherwise the buttons attack (a
  // press before the ball arrives is a first-time finish or pass).
  bool defending = false;
  for (const MatchPlayer& player : engine.getPlayers())
  {
    if ((player.player != nullptr) &&
        player.player == engine.getPossessionAttachment())
    {
      defending = player.isHomeTeam != home;
    }
  }

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
        0.0F, 1.0F);
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
    {
      pending = MatchInputAction::TACKLE;
    }
    else if (pressed[SHOOT])
    {
      pending = MatchInputAction::SLIDE_TACKLE;
    }
  }
  else
  {
    if (pressed[PASS])
    {
      pending = MatchInputAction::PASS;
      pending_power = 0.0F;
    }
    if (pressed[SHOOT])
    {
      startCharge(MatchInputAction::SHOOT);
    }
    if (pressed[THROUGH])
    {
      startCharge(MatchInputAction::THROUGH_BALL);
    }
    if (pressed[LOB])
    {
      startCharge(MatchInputAction::LOFTED_PASS);
    }
    Control chargeControl = CONTROL_COUNT;
    if (charging == MatchInputAction::SHOOT)
    {
      chargeControl = SHOOT;
    }
    else if (charging == MatchInputAction::THROUGH_BALL)
    {
      chargeControl = THROUGH;
    }
    else if (charging == MatchInputAction::LOFTED_PASS)
    {
      chargeControl = LOB;
    }
    if (chargeControl != CONTROL_COUNT &&
        (released.at(chargeControl) || !held(chargeControl)))
    {
      pending = charging;
      // At least a sliver of power: a tap is a soft, deliberate ball.
      pending_power = std::max(heldShare(), 0.05F);
      charging = MatchInputAction::NONE;
    }
  }
  pressed.fill(false);
  released.fill(false);

  // Sample once per simulation step, on the frame the engine steps.
  if (!engine.stepsWithin(simulatedSeconds))
  {
    return;
  }
  MatchPlayerInput input;
  // Screen direction to pitch metres through the view's projection.
  if (const float determinant =
          (basis.lengthX * basis.widthY) - (basis.widthX * basis.lengthY);
      !idle && std::abs(determinant) > 1e-6F)
  {
    const float alongX =
        (stickX * basis.widthY - basis.widthX * stickY) / determinant;
    const float alongY =
        (basis.lengthX * stickY - stickX * basis.lengthY) / determinant;
    const float length = std::hypot(alongX, alongY);
    const float magnitude = std::min(1.0F, std::hypot(stickX, stickY));
    if (length > 1e-6F)
    {
      input.moveX = quantize(alongX / length * magnitude);
      input.moveY = quantize(alongY / length * magnitude);
    }
  }
  input.sprint =
      held(SPRINT) || triggerHeld(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
  input.jockey =
      defending &&
      (held(JOCKEY) || triggerHeld(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
  input.passAssist = settings.passAssist;
  // The same action twice in a row needs a release in between (the engine
  // fires on the press): this step releases, the next one presses again.
  const bool repeat = submitted_any && pending != MatchInputAction::NONE &&
                      submitted.action == pending;
  if (!repeat)
  {
    input.action = pending;
    input.power = pending == MatchInputAction::NONE ? 0.0F : pending_power;
  }
  if (submitted_any && sameInput(input, submitted))
  {
    return;
  }
  engine.submitInput(input);
  submitted = input;
  submitted_any = true;
  if (!repeat)
  {
    pending = MatchInputAction::NONE;
    pending_power = 0.0F;
  }
}

float MatchPlayController::chargeShare(const MatchEngine& engine) const
{
  if (charging == MatchInputAction::NONE)
  {
    return 0.0F;
  }
  return std::clamp(
      static_cast<float>(engine.getSimulatedSteps() - charge_step) *
          MatchTuning::Timing::FIXED_STEP_SECONDS / FULL_POWER_SECONDS,
      0.0F, 1.0F);
}

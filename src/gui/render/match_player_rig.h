// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "gui/render/match_render_math.h"

/**
 * Procedural skeleton helpers of the 3D match view: a two-bone IK solver for
 * legs and arms, foot planting that pins a stance foot to the grass while the
 * body moves over it, the action chosen from the engine state, and the timed
 * kick and tackle curves. Pure math (no drawing), so it is unit tested on its
 * own. Distances are metres, angles radians, times simulated seconds.
 */
namespace PlayerRig
{
using RenderMath::Vec3;

inline constexpr Vec3 UP{0.0f, 0.0f, 1.0f};
inline constexpr float PI = std::numbers::pi_v<float>;
inline constexpr float TWO_PI = 2.0f * PI;

inline float smoothStep(float t)
{
  t = std::clamp(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

inline float flatDistance(Vec3 a, Vec3 b)
{
  return std::hypot(a.x - b.x, a.y - b.y);
}

// --- two-bone IK -------------------------------------------------------------

struct TwoBone
{
  Vec3 middle; /**< Knee or elbow. */
  Vec3 end;    /**< Ankle or wrist (the target when it is reachable). */
  bool reached = false;
};

/**
 * Places the middle joint of a two-segment limb so the end lands on the
 * target, bending towards `pole` (knees forward, elbows back). Targets out
 * of reach leave the limb straight towards them.
 */
inline TwoBone solveTwoBone(Vec3 root, Vec3 target, float upper, float lower,
                            Vec3 pole)
{
  const Vec3 toTarget = target - root;
  float distance = RenderMath::length(toTarget);
  const Vec3 direction =
      distance > 1e-5f ? toTarget * (1.0f / distance) : Vec3{0.0f, 0.0f, -1.0f};
  const float minimum = std::abs(upper - lower) + 1e-3f;
  const float maximum = (upper + lower) * 0.9995f;
  const bool reached = distance >= minimum && distance <= maximum;
  distance = std::clamp(distance, minimum, maximum);
  const float along =
      (upper * upper - lower * lower + distance * distance) / (2.0f * distance);
  const float height = std::sqrt(std::max(upper * upper - along * along, 0.0f));
  Vec3 bend = pole - direction * RenderMath::dot(pole, direction);
  if (const float bendLength = RenderMath::length(bend); bendLength < 1e-4f)
  {
    // Pole along the limb: any perpendicular will do.
    bend = RenderMath::cross(
        direction, std::abs(direction.z) < 0.9f ? UP : Vec3{1.0f, 0.0f, 0.0f});
    bend = RenderMath::normalize(bend);
  }
  else
  {
    bend = bend * (1.0f / bendLength);
  }
  return {root + direction * along + bend * height, root + direction * distance,
          reached};
}

/**
 * A planted foot the hip can no longer reach rolls onto its toes: the heel
 * lifts around the toe (`toe = ankle + forward * toeLength`) until the ankle
 * is back within `reach` of the hip, so the toe never leaves its spot.
 * Returns the heel angle; `ankle` is updated in place.
 */
inline float rollOntoToes(Vec3& ankle, Vec3 hip, Vec3 footForward,
                          float toeLength, float reach, float maxAngle)
{
  if (RenderMath::length(ankle - hip) <= reach) return 0.0f;
  const Vec3 toe = ankle + footForward * toeLength;
  const auto at = [&](float angle)
  {
    return toe - footForward * (toeLength * std::cos(angle)) +
           UP * (toeLength * std::sin(angle));
  };
  float low = 0.0f;
  float high = maxAngle;
  if (RenderMath::length(at(high) - hip) > reach)
  {
    ankle = at(high);
    return high;
  }
  for (int step = 0; step < 8; ++step)
  {
    const float middle = (low + high) * 0.5f;
    if (RenderMath::length(at(middle) - hip) > reach)
      low = middle;
    else
      high = middle;
  }
  ankle = at(high);
  return high;
}

// --- foot planting -----------------------------------------------------------

/** Memory of one foot between frames. Positions are on the grass (z = 0). */
struct FootState
{
  Vec3 position;
  /** Where the foot is pinned while in stance. */
  Vec3 planted;
  /** Where the current swing left the ground. */
  Vec3 liftOff;
  /** Idle step in progress: its target and simulated seconds left. */
  Vec3 stepTarget;
  float stepSeconds = 0.0f;
  /** Facing of the boot, frozen while the foot is planted. */
  float yaw = 0.0f;
  bool inStance = false;
  bool valid = false;
};

/** One frame of a foot's gait, in world space. */
struct StrideInput
{
  /** Point on the grass under this hip at rest. */
  Vec3 rest;
  /** Unit horizontal direction of travel. */
  Vec3 forward;
  /** Body facing (the boots point this way when they are put down). */
  float yaw = 0.0f;
  /** This leg's cycle phase in [0, 2pi); stance starts at 0. */
  float legPhase = 0.0f;
  /** Share of the cycle the foot spends on the ground. */
  float duty = 0.5f;
  /** Metres the body travels while the foot is down. */
  float stanceLength = 0.0f;
  /** How far ahead of the hip a foot is put down at most. */
  float frontReach = 0.3f;
  float liftHeight = 0.12f;
  /** A pinned foot this far from where the gait wants it is put down again
   * (sharp turns, playback jumps). */
  float maxDrift = 0.5f;
  /** Standing or shuffling: feet stay down and take short timed steps. */
  bool idle = false;
  /** Idle: step once the foot is this far from its rest point. */
  float idleStepMetres = 0.25f;
  float stepDuration = 0.22f;
  /** The other foot is mid-step (never lift both while idle). */
  bool otherStepping = false;
  /** Simulated seconds of this frame (idle steps run on the match clock). */
  float deltaSeconds = 0.0f;
};

/** Ideal stance position for a stance progress `share` in [0, 1]. */
inline Vec3 stanceSpot(const StrideInput& input, float share)
{
  const float front = std::min(input.stanceLength * 0.5f, input.frontReach);
  return input.rest + input.forward * (front - share * input.stanceLength);
}

/**
 * Advances one foot and returns its world position on (or above) the grass.
 * In stance the foot keeps the exact spot it was put down on, so it never
 * slides; in swing it travels from where it lifted off to the next landing
 * spot on a low arc. Standing players keep both feet down and only step when
 * the body has drifted away from them.
 */
inline Vec3 stepFoot(FootState& foot, const StrideInput& input)
{
  if (!foot.valid)
  {
    // Standing: both feet down under the hips. Moving: the gait below puts
    // the foot down on its exact spot (or swings it from under the body).
    foot = FootState{};
    const Vec3 initialPosition =
        input.idle ? input.rest : stanceSpot(input, 0.5f);
    foot.liftOff = initialPosition;
    foot.planted = initialPosition;
    foot.position = initialPosition;
    foot.yaw = input.yaw;
    foot.inStance = input.idle;
    foot.valid = true;
  }
  if (input.idle)
  {
    if (foot.stepSeconds > 0.0f)
    {
      foot.stepSeconds = std::max(0.0f, foot.stepSeconds - input.deltaSeconds);
      const float share = 1.0f - foot.stepSeconds / input.stepDuration;
      foot.position =
          RenderMath::lerp(foot.liftOff, foot.stepTarget, smoothStep(share)) +
          UP * (input.liftHeight * 0.5f * std::sin(PI * share));
      if (foot.stepSeconds <= 0.0f)
      {
        foot.planted = foot.stepTarget;
        foot.position = foot.stepTarget;
        foot.yaw = input.yaw;
        foot.inStance = true;
      }
      return foot.position;
    }
    if (const bool drifted =
            flatDistance(foot.planted, input.rest) > input.idleStepMetres;
        (!foot.inStance || drifted) && !input.otherStepping)
    {
      // Step back under the hip (a foot caught mid-swing lands there too).
      foot.liftOff = foot.position;
      foot.stepTarget = input.rest;
      foot.stepSeconds = input.stepDuration;
      foot.inStance = false;
      return foot.position;
    }
    if (foot.inStance) foot.position = foot.planted;
    return foot.position;
  }

  if (foot.stepSeconds > 0.0f)
  {
    // Started to move during an idle step: carry on as a swing.
    foot.stepSeconds = 0.0f;
    foot.inStance = false;
    foot.liftOff = foot.position;
  }
  const float share = std::clamp(input.legPhase / TWO_PI, 0.0f, 1.0f);
  const bool stance = share < input.duty;
  if (stance)
  {
    if (const Vec3 ideal =
            stanceSpot(input, share / std::max(input.duty, 1e-3f));
        !foot.inStance || flatDistance(foot.planted, ideal) > input.maxDrift)
    {
      foot.planted = ideal;
      foot.yaw = input.yaw;
    }
    foot.position = foot.planted;
  }
  else
  {
    if (foot.inStance) foot.liftOff = foot.planted;
    const float swing =
        (share - input.duty) / std::max(1.0f - input.duty, 1e-3f);
    const Vec3 landing = stanceSpot(input, 0.0f);
    foot.position = RenderMath::lerp(foot.liftOff, landing, smoothStep(swing)) +
                    UP * (input.liftHeight * std::sin(PI * swing));
    foot.yaw = input.yaw;
  }
  foot.inStance = stance;
  return foot.position;
}

// --- actions -----------------------------------------------------------------

/** What a player's body is doing, in order of the gait speeds. */
enum class Action : std::uint8_t
{
  IDLE,
  WALK,
  JOG,
  RUN,
  SPRINT,
  TURN,
  PASS,
  SHOT,
  CROSS,
  HEADER,
  TACKLE,
  SLIDE,
  KEEPER_SET,
  KEEPER_DIVE,
  KEEPER_HOLD,
  THROW_IN,
  CELEBRATE,
  DEJECTED,
};

/** A timed ball action (started by an engine event) still playing. */
enum class Event : std::uint8_t
{
  NONE,
  PASS,
  SHOT,
  CROSS,
  HEADER,
  TACKLE,
  SLIDE,
  THROW,
};

/** Goal reaction of the player's side. */
enum class Mood : std::uint8_t
{
  NONE,
  CELEBRATE,
  DEJECTED,
};

/** Gait speed bands (m/s) of the locomotion actions. */
struct GaitBands
{
  float idle = 0.35f;
  float walk = 2.2f;
  float jog = 4.6f;
  float run = 6.6f;
  /** Turning faster than this (rad/s) below `turnSpeed` is a turn. */
  float turnRate = 3.0f;
  float turnSpeed = 3.5f;
};

/** Engine state of one player in one frame, reduced to what poses need. */
struct ActionInput
{
  float speed = 0.0f;
  float turnRate = 0.0f;
  Event event = Event::NONE;
  bool goalkeeper = false;
  bool diving = false;
  /** A keeper with the ball in his hands. */
  bool holding = false;
  /** A keeper set for a shot (ball close, play live). */
  bool keeperSet = false;
  /** Waiting to take a throw-in with the ball in his hands. */
  bool throwIn = false;
  Mood mood = Mood::NONE;
};

inline Action locomotionFor(float speed, const GaitBands& bands = {})
{
  using enum Action;
  if (speed < bands.idle) return IDLE;
  if (speed < bands.walk) return WALK;
  if (speed < bands.jog) return JOG;
  if (speed < bands.run) return RUN;
  return SPRINT;
}

/**
 * Picks the action that owns the body this frame. Dives beat everything,
 * then the timed ball actions, then holding the ball (throw-in, keeper),
 * goal reactions, the keeper's set stance and finally the gait.
 */
inline Action selectAction(const ActionInput& input,
                           const GaitBands& bands = {})
{
  if (input.goalkeeper && input.diving) return Action::KEEPER_DIVE;
  switch (input.event)
  {
    case Event::SLIDE:
      return Action::SLIDE;
    case Event::TACKLE:
      return Action::TACKLE;
    case Event::HEADER:
      return Action::HEADER;
    case Event::SHOT:
      return Action::SHOT;
    case Event::CROSS:
      return Action::CROSS;
    case Event::PASS:
      return Action::PASS;
    case Event::THROW:
      return Action::THROW_IN;
    case Event::NONE:
      break;
  }
  if (input.throwIn) return Action::THROW_IN;
  if (input.goalkeeper && input.holding) return Action::KEEPER_HOLD;
  if (input.mood == Mood::CELEBRATE) return Action::CELEBRATE;
  if (input.mood == Mood::DEJECTED) return Action::DEJECTED;
  if (input.goalkeeper && input.keeperSet && input.speed < bands.jog)
    return Action::KEEPER_SET;
  if (std::abs(input.turnRate) >= bands.turnRate && input.speed >= bands.idle &&
      input.speed < bands.turnSpeed)
    return Action::TURN;
  return locomotionFor(input.speed, bands);
}

/** Share of the gait cycle a foot is on the ground at this speed (m/s). */
inline float dutyFactor(float speed)
{
  // Walking keeps a foot down most of the time; running has flight phases.
  constexpr float WALK_DUTY = 0.62f;
  constexpr float JOG_DUTY = 0.4f;
  constexpr float SPRINT_DUTY = 0.28f;
  if (speed <= 1.5f) return WALK_DUTY;
  if (speed <= 4.0f)
    return WALK_DUTY + (JOG_DUTY - WALK_DUTY) * (speed - 1.5f) / 2.5f;
  return std::max(SPRINT_DUTY,
                  JOG_DUTY + (SPRINT_DUTY - JOG_DUTY) * (speed - 4.0f) / 3.5f);
}

// --- timed ball actions ------------------------------------------------------

/** Shape of a kick's follow-through: forward, sideways and up (metres). */
struct Swing
{
  float forward = 0.0f;
  float across = 0.0f;
  float up = 0.0f;
};

inline Swing followThroughFor(Event event)
{
  using enum Event;
  switch (event)
  {
    case SHOT:
      return {0.55f, 0.05f, 0.7f};
    case CROSS:
      return {0.4f, 0.32f, 0.6f};
    case PASS:
    default:
      return {0.38f, 0.08f, 0.22f};
  }
}

/** Phases of a kick after contact (simulated seconds). */
struct KickTiming
{
  float contactHold = 0.05f;
  float followThrough = 0.26f;
  float recover = 0.26f;
  float total() const { return contactHold + followThrough + recover; }
};

/**
 * Where the kicking ankle goes `seconds` after the ball was struck: on the
 * ball at contact, then through along `direction` and back to the gait.
 * `weight` (0..1) is how much the curve owns the leg (it hands back to foot
 * planting during the recovery).
 */
inline Vec3 kickAnkle(Event event, float seconds, Vec3 contact, Vec3 direction,
                      Vec3 across, const KickTiming& timing, float& weight)
{
  weight = 0.0f;
  if (seconds < 0.0f || seconds >= timing.total()) return contact;
  const Swing swing = followThroughFor(event);
  const Vec3 finish = contact + direction * swing.forward +
                      across * swing.across + UP * swing.up;
  if (seconds < timing.contactHold)
  {
    weight = 1.0f;
    return contact;
  }
  if (const float through =
          (seconds - timing.contactHold) / timing.followThrough;
      through < 1.0f)
  {
    weight = 1.0f;
    // Fast off the ball, slowing towards the top of the follow-through.
    const float eased = 1.0f - (1.0f - through) * (1.0f - through);
    return RenderMath::lerp(contact, finish, eased);
  }
  const float back =
      (seconds - timing.contactHold - timing.followThrough) / timing.recover;
  weight = 1.0f - smoothStep(back);
  return finish;
}
}  // namespace PlayerRig

// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The sandbox's drills (see match_sandbox_drill.h).

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <random>

#include "gui/render/imatch_renderer.h"
#include "gui/widgets/theme.h"
#include "model/match_engine_internal.h"
#include "tools/match_sandbox_drill.h"
#include "tools/match_sandbox_text.h"

namespace
{
constexpr float LENGTH = MatchTuning::Pitch::MATCH_LENGTH_METRES;
constexpr float WIDTH = MatchTuning::Pitch::MATCH_WIDTH_METRES;
constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
constexpr float PI = std::numbers::pi_v<float>;
/** How far past a point a runner aims to run through it, in metres. */
constexpr float RUN_THROUGH_METRES = 8.0F;
/** Midpoint between adjacent integer choice values (0 and 1). */
constexpr float CHOICE_MIDPOINT = 0.5F;
constexpr float COLOUR_MIDPOINT_SHARE = 0.5F;
constexpr float HALF_TO_FULL_SCALE = 2.0F;
constexpr float KEEPER_DISTANCE_SHARE = 0.5F;
constexpr float KEEPER_START_LINE_SHARE = 0.985F;
constexpr float MIN_SHOT_DEFENDER_GAP_METRES = 0.5F;
constexpr float QUARTER_TURN_PI_SHARE = 0.5F;
constexpr float TOUCH_RUN_THROUGH_SCALE = 4.0F;
constexpr double TOUCH_WAIT_TIME_SHARE = 0.5;
constexpr float FEED_GUIDE_LENGTH_SCALE = 1.3F;
constexpr float STOPPED_BALL_SPEED_METRES_PER_SECOND = 0.2F;
constexpr float LANDING_HEIGHT_EPSILON_METRES = 0.01F;
constexpr float MISSED_LANDING_DISTANCE_METRES = 1.5F;
constexpr float MIDPOINT_WEIGHT = 0.5F;
constexpr float WIDE_PASS_START_LENGTH_SHARE = 0.70F;
constexpr float WIDE_PASS_START_WIDTH_SHARE = 0.10F;
constexpr float CARRY_PACE_LEVEL_COUNT = 3.0F;
constexpr float INITIAL_CHIP_RANGE_METRES = 15.0F;
constexpr float INITIAL_KEEPER_THROW_RANGE_METRES = 40.0F;
constexpr float PASS_TOLERANCE_GROWTH_METRES = 2.0F;
constexpr float TRAINING_SAFE_MIN_SHARE = 0.02F;
constexpr float TRAINING_SAFE_MAX_SHARE = 0.98F;
constexpr float WALLED_HOME_START_SHARE = 0.3F;
constexpr float WALLED_AWAY_START_SHARE = 0.7F;
constexpr float SMALL_SIDED_HOME_START_SHARE = 0.25F;
constexpr float SMALL_SIDED_AWAY_START_SHARE = 0.75F;
constexpr float DUMMY_CLEARANCE_TOLERANCE_METRES = 0.001F;
constexpr float SMALL_SIDED_PITCH_ASPECT_RATIO = 1.5F;
constexpr float SPEED_TRAIL_WIDTH_PIXELS = 2.5F;
constexpr float BALL_TRAIL_WIDTH_PIXELS = 1.5F;
constexpr float LARGE_MARKER_RADIUS_PIXELS = 4.0F;
constexpr float SECONDARY_MARKER_RADIUS_PIXELS = 2.5F;
constexpr float HIT_OUTLINE_WIDTH_PIXELS = 2.0F;
constexpr float PRIMARY_MARKER_RADIUS_PIXELS = 3.0F;
constexpr int REFERENCE_PLAYER_HEIGHT_CM = 180;
constexpr int REFERENCE_KEEPER_HEIGHT_CM = 188;
constexpr float PITCH_MIDPOINT_SHARE = 0.5F;
constexpr float PITCH_EDGE_MAX_SHARE = 0.995F;
constexpr float PITCH_SAFE_MIN_SHARE = 0.01F;
constexpr float PITCH_SAFE_MAX_SHARE = 0.99F;
constexpr float PITCH_EDGE_MIN_SHARE = 0.005F;
constexpr float PARKED_BALL_PITCH_SHARE = 0.97F;
constexpr float MIN_DIRECTION_LENGTH_METRES = 1e-6F;
constexpr float MIN_MOVE_LENGTH_SQUARED = 1e-9F;
constexpr float DEGREES_PER_HALF_TURN = 180.0F;
constexpr float FULL_TURN_RADIANS = 2.0F * PI;
constexpr float MIN_REPORTED_CONTACT_FACTOR = 0.0005F;
constexpr float MIN_REPORTED_CONTACT_SPEED = 0.1F;

const MatchPlayer* slotOf(const MatchEngine& engine, PlayerID id)
{
  for (const MatchPlayer& slot : engine.getPlayers())
  {
    if ((slot.player != nullptr) && slot.player->getId() == id)
    {
      return &slot;
    }
  }
  return nullptr;
}

float speedOf(const MatchPlayer& player)
{
  return std::hypot(player.velocity.x, player.velocity.y);
}

double secondsOf(const MatchEngine& engine)
{
  return static_cast<double>(engine.getSimulatedSteps()) * STEP;
}

// Drills play on a match pitch. They place things by fractions of it
// (`at`: x of the length, y of the width) and think in metres; engine
// points go through the pitch either way.
const MatchPitch& PITCH = MatchPitch::matchSize();
PitchPoint at(float fx, float fy) { return MatchPitch::matchSize().at(fx, fy); }
/** Where drills park the ball when nobody should touch it. */
PitchPoint ballOutOfTheWay()
{
  return at(PARKED_BALL_PITCH_SHARE, PARKED_BALL_PITCH_SHARE);
}
float metresBetween(PitchPoint from, PitchPoint to)
{
  return std::hypot(to.x - from.x, to.y - from.y);
}
/** `from` moved `metres` along the unit direction `direction` (metres). */
PitchPoint along(PitchPoint from, Vector2F direction, float metres)
{
  return PitchPoint{from.x + (direction.x * metres),
                    from.y + (direction.y * metres)};
}
/** Unit direction from one pitch point to another, in metres. */
Vector2F directionBetween(PitchPoint from, PitchPoint to)
{
  const Vector2F offset{.x = to.x - from.x, .y = to.y - from.y};
  const float size = std::hypot(offset.x, offset.y);
  if (size < MIN_DIRECTION_LENGTH_METRES)
  {
    return {.x = 1.0F, .y = 0.0F};
  }
  return {.x = offset.x / size, .y = offset.y / size};
}
/** A direction on the pitch (radians) as a facing angle, wrapped to
 * (-pi, pi]. Facings are true angles (training ground stage 3), so this
 * only wraps. */
float facingAngleOf(float radians)
{
  return std::atan2(std::sin(radians), std::cos(radians));
}
Vector2F rotated(Vector2F direction, float radians)
{
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  return Vector2F{.x = (direction.x * c) - (direction.y * s),
                  .y = (direction.x * s) + (direction.y * c)};
}
/** Angle between two unit directions, in degrees (0-180). */
float turnDegrees(Vector2F before, Vector2F after)
{
  const float dot =
      std::clamp((before.x * after.x) + (before.y * after.y), -1.0F, 1.0F);
  return std::acos(dot) * DEGREES_PER_HALF_TURN / PI;
}

[[nodiscard]] int teamSideNumber(std::optional<bool> team)
{
  if (!team)
  {
    return 0;
  }
  return *team ? 1 : -1;
}

struct MovementSegment
{
  PitchPoint from;
  PitchPoint to;
};
struct ArrivalCircle
{
  PitchPoint centre;
  float radius{};
};
struct CrossingLine
{
  PitchPoint point;
  Vector2F direction{};
};
struct SettingRange
{
  float min{};
  float max{};
};
struct RunnerRun
{
  PlayerID player{};
  float metres{};
};
struct SpinRevolutions
{
  float side{};
  float top{};
};
struct FlightTarget
{
  float distanceMetres{};
  float speedMetresPerSecond{};
  float heightMetres{};
};

/**
 * Share [0, 1] of the move from `from` to `to` at which a runner enters the
 * circle of `radius` metres round `centre`, if he does during that move.
 */
std::optional<float> entryShare(MovementSegment movement, ArrivalCircle circle)
{
  const PitchPoint from = movement.from;
  const PitchPoint to = movement.to;
  const PitchPoint centre = circle.centre;
  const float radius = circle.radius;
  const Vector2F start = from - centre;
  const Vector2F move = to - from;
  const float c = (start.x * start.x) + (start.y * start.y) - (radius * radius);
  if (c <= 0.0F)
  {
    return 0.0F;
  }
  const float a = (move.x * move.x) + (move.y * move.y);
  if (a < MIN_MOVE_LENGTH_SQUARED)
  {
    return std::nullopt;
  }
  const float b = 2.0F * (start.x * move.x + start.y * move.y);
  const float discriminant = (b * b) - (4.0F * a * c);
  if (discriminant < 0.0F)
  {
    return std::nullopt;
  }
  const float share = (-b - std::sqrt(discriminant)) / (2.0F * a);
  if (share < 0.0F || share > 1.0F)
  {
    return std::nullopt;
  }
  return share;
}

/**
 * Share [0, 1] of the move at which a runner crosses the line through
 * `point` square to `direction` (going that way), if he does.
 */
std::optional<float> crossingShare(MovementSegment movement, CrossingLine line)
{
  const PitchPoint from = movement.from;
  const PitchPoint to = movement.to;
  const PitchPoint point = line.point;
  const Vector2F direction = line.direction;
  const Vector2F a = from - point;
  const Vector2F b = to - point;
  const float before = (a.x * direction.x) + (a.y * direction.y);
  const float after = (b.x * direction.x) + (b.y * direction.y);
  if (before >= 0.0F || after < 0.0F)
  {
    return std::nullopt;
  }
  return -before / (after - before);
}

/** A slider setting (see DrillParameter). */
DrillParameter setting(const char* name, float* value, SettingRange range,
                       bool integer, const char* tooltip = nullptr)
{
  DrillParameter parameter;
  parameter.name = name;
  parameter.value = value;
  parameter.min = range.min;
  parameter.max = range.max;
  parameter.integer = integer;
  parameter.tooltip = tooltip;
  return parameter;
}

/** Trail colour by speed: blue when slow, yellow, red flat out. */
ImU32 speedColour(float speed)
{
  const float share = std::clamp(speed / 9.0F, 0.0F, 1.0F);
  if (share < COLOUR_MIDPOINT_SHARE)
  {
    const float t = share * 2.0F;
    return IM_COL32(static_cast<int>(60 + (195 * t)),
                    static_cast<int>(140 + (70 * t)),
                    static_cast<int>(255 - (195 * t)), 220);
  }
  const float t = (share - 0.5F) * 2.0F;
  return IM_COL32(255, static_cast<int>(210 - (150 * t)), 60, 220);
}

constexpr float MIN_ATTRIBUTE = 1.0F;
constexpr float MAX_ATTRIBUTE = 100.0F;
constexpr float DEFAULT_RUNNER_ATTRIBUTE = 70.0F;

/** Attributes shared by the movement drills. */
struct RunnerAttributes
{
  float pace = DEFAULT_RUNNER_ATTRIBUTE;
  float physicality = DEFAULT_RUNNER_ATTRIBUTE;
  float stamina = DEFAULT_RUNNER_ATTRIBUTE;
  /** The Agility and Acceleration derived stats, set directly (not from
   * their recipes). */
  float agility = DEFAULT_RUNNER_ATTRIBUTE;
  float acceleration = DEFAULT_RUNNER_ATTRIBUTE;
  /** Fuel (condition) at the start, 0-100. */
  float condition = MAX_ATTRIBUTE;

  void addTo(std::vector<DrillParameter>& parameters)
  {
    parameters.push_back(setting(
        "Pace", &pace, {.min = MIN_ATTRIBUTE, .max = MAX_ATTRIBUTE}, true));
    parameters.push_back(
        setting("Physicality", &physicality,
                {.min = MIN_ATTRIBUTE, .max = MAX_ATTRIBUTE}, true,
                "Duels; braking in the Aardvark body. (Acceleration "
                "is its own setting here.)"));
    parameters.push_back(setting(
        "Stamina", &stamina, {.min = MIN_ATTRIBUTE, .max = MAX_ATTRIBUTE}, true,
        "Endurance: how fast condition and the sprint "
        "reserve drain and recover."));
    parameters.push_back(setting(
        "Agility", &agility, {.min = MIN_ATTRIBUTE, .max = MAX_ATTRIBUTE}, true,
        "Grip: how sharply he turns at speed (0.9x at 45, "
        "1.1x at 85), how fast his body turns, his braking "
        "(6 m/s² at 1, 8 at 99), and up to +0.2 m/s² "
        "acceleration."));
    parameters.push_back(
        setting("Acceleration", &acceleration,
                {.min = MIN_ATTRIBUTE, .max = MAX_ATTRIBUTE}, true,
                "His push from a standstill (5.5 m/s² at 1, 9.5 at "
                "99, plus Agility's boost), and up to +0.1 m/s² "
                "braking."));
    parameters.push_back(setting(
        "Condition", &condition, {.min = 0.0F, .max = MAX_ATTRIBUTE}, true,
        "Fuel at the start (100: fresh). Lower it to test "
        "a tired player, e.g. a sprint after a match."));
  }
  [[nodiscard]] std::map<std::string, float> stats() const
  {
    return {{"Pace", pace}, {"Physicality", physicality}, {"Stamina", stamina}};
  }
  [[nodiscard]] std::map<DerivedStat, float> derived() const
  {
    return {{DerivedStat::AGILITY, agility},
            {DerivedStat::ACCELERATION, acceleration}};
  }
  [[nodiscard]] std::string describe() const
  {
    std::string text = std::format(
        "Pace {:.0f} · Physicality {:.0f} · Stamina {:.0f} · Agility {:.0f} · "
        "Acceleration {:.0f}",
        pace, physicality, stamina, agility, acceleration);
    if (condition < MAX_ATTRIBUTE)
    {
      text += std::format(" · Condition {:.0f}", condition);
    }
    return text;
  }
};

/** Attributes of a runner with the ball (the movement drills' With the ball
 * setting). */
struct DribblerAttributes
{
  float dribbling = DEFAULT_RUNNER_ATTRIBUTE;
  float passing = DEFAULT_RUNNER_ATTRIBUTE;
  /** The First Touch derived stat, set directly (not from its recipe). */
  float firstTouch = DEFAULT_RUNNER_ATTRIBUTE;

  void addTo(std::vector<DrillParameter>& parameters)
  {
    parameters.push_back(
        setting("Dribbling", &dribbling,
                {.min = MIN_ATTRIBUTE, .max = MAX_ATTRIBUTE}, true,
                "With the ball: touch length and rhythm, heavy "
                "touches."));
    parameters.push_back(setting("Passing", &passing,
                                 {.min = MIN_ATTRIBUTE, .max = MAX_ATTRIBUTE},
                                 true, "With the ball."));
    parameters.push_back(setting("First touch", &firstTouch,
                                 {.min = MIN_ATTRIBUTE, .max = MAX_ATTRIBUTE},
                                 true,
                                 "With the ball: control of each touch."));
  }
  [[nodiscard]] std::string describe() const
  {
    return std::format("Dribbling {:.0f} · Passing {:.0f} · First touch {:.0f}",
                       dribbling, passing, firstTouch);
  }
};

/**
 * The ball of a runner who has it: what it does over a run, from outside the
 * engine (gaps, losses, touches, and any move no physics explains).
 */
struct BallWatch
{
  /** A step's ball move this much further than its fastest speed over the
   * step could take it counts as placed, not moved (m). */
  static constexpr float PLACED_METRES = 0.05F;

  int lost = 0;
  int placed = 0;
  float largestPlacement = 0.0F;
  int stepsWith = 0;
  int stepsWithout = 0;
  double gapSum = 0.0;
  float largestGap = 0.0F;
  std::uint32_t touches = 0;
  double contactGapSum = 0.0;
  /** Touches that went wrong (a loose ball's only). */
  std::uint32_t failedTouches = 0;
  /** The ball went out of play (the run ended there). */
  bool out = false;
  bool had = true;
  std::optional<PitchPoint> lastAt;
  Vector2F lastVelocity{};

  void reset() { *this = BallWatch{}; }

  void observe(const MatchEngine& engine, const MatchPlayer& player)
  {
    const MatchBall& ball = engine.getBall();
    const bool has = engine.getPossessionAttachment() == player.player;
    if (had && !has)
    {
      ++lost;
    }
    had = has;
    if (has)
    {
      ++stepsWith;
      const float gap = metresBetween(player.position, ball.position);
      gapSum += gap;
      largestGap = std::max(largestGap, gap);
    }
    else
    {
      ++stepsWithout;
    }
    // Physics moves the ball no further than its fastest speed over the
    // step allows (a touch changes the velocity, never the position); any
    // further and it was put there.
    if (lastAt)
    {
      const float moved = metresBetween(*lastAt, ball.position);
      const float off =
          moved - (std::max(std::hypot(lastVelocity.x, lastVelocity.y),
                            std::hypot(ball.velocity.x, ball.velocity.y)) *
                   STEP);
      if (off > PLACED_METRES)
      {
        ++placed;
        largestPlacement = std::max(largestPlacement, off);
      }
    }
    lastAt = ball.position;
    lastVelocity = ball.velocity;
    touches = engine.getDribbleTouches();
    contactGapSum = engine.getDribbleContactGapSum();
    failedTouches = engine.getDribbleFailedTouches();
  }

  /** How far from his centre he met it at his touches, on average (a
   * loose ball's only; 0 for the tethered dribble). */
  [[nodiscard]] double meanContactGap() const
  {
    return touches > 0 ? contactGapSum / static_cast<double>(touches) : 0.0;
  }

  [[nodiscard]] double meanGap() const
  {
    return stepsWith > 0 ? gapSum / static_cast<double>(stepsWith) : 0.0;
  }
};

// --- Movement drills -------------------------------------------------------

/**
 * A drill with one runner, moved by one of two paths through the engine:
 * the AI (the drill plans for him, as his side's brain would: the tactical
 * target and the waypoints on the way), or you on the keyboard or a gamepad.
 * With the ball (the setting) he starts with it at his feet and dribbles the
 * course; if he loses it, he is sent after it, then back to the course.
 */
class MovementDrill : public Drill
{
 public:
  enum Path : std::uint8_t
  {
    AI,
    YOU
  };

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    std::map<std::string, float> stats = runner.stats();
    std::map<DerivedStat, float> derived = runner.derived();
    stats["Dribbling"] = dribbler.dribbling;
    stats["Passing"] = dribbler.passing;
    derived[DerivedStat::FIRST_TOUCH] = dribbler.firstTouch;
    DrillPlayerSpec spec{.name = "Runner",
                         .role = PlayerRole::CM,
                         .homeTeam = true,
                         .stats = stats,
                         .height = REFERENCE_PLAYER_HEIGHT_CM,
                         .derived = derived};
    spec.condition = runner.condition;
    return {spec};
  }

  [[nodiscard]] bool humanControl() const override
  {
    return pathSetting() == YOU;
  }

 protected:
  static constexpr std::array<const char*, 2> PATH_NAMES{"AI", "You"};

  [[nodiscard]] Path pathSetting() const
  {
    return static_cast<Path>(std::clamp(static_cast<int>(pathChoice), 0, 1));
  }

  /** "Who runs" and the runner's attributes. */
  void addRunnerParameters(std::vector<DrillParameter>& parameters)
  {
    DrillParameter who =
        setting("Who runs", &pathChoice, {.min = 0.0F, .max = 1.0F}, true,
                "AI: the drill plans the runner's run as his side's "
                "brain would (where he is going, and the waypoints on "
                "the way).\nYou: you run (WASD or arrows, Shift to "
                "sprint, or a gamepad).");
    who.choices = {PATH_NAMES[0], PATH_NAMES[1]};
    parameters.push_back(who);
    DrillParameter ball =
        setting("With the ball", &ballChoice, {.min = 0.0F, .max = 1.0F}, true,
                "Yes: he starts with the ball at his feet and "
                "dribbles the course. If he loses it, he is sent "
                "after it first.");
    ball.choices = {"No", "Yes"};
    parameters.push_back(ball);
    runner.addTo(parameters);
    dribbler.addTo(parameters);
  }

  /** Places the runner (with the ball, or the ball out of the way) and hands
   * him to his path. */
  void startRunner(MatchEngine& engine, const std::vector<PlayerID>& ids,
                   PitchPoint at)
  {
    id = ids.front();
    path = pathSetting();
    withBall = ballChoice >= CHOICE_MIDPOINT;
    MatchScenario scenario;
    scenario.players.push_back(
        {.playerId = id, .position = at, .makingRun = false});
    scenario.ballPosition = withBall ? at : ballOutOfTheWay();
    if (withBall)
    {
      scenario.carrierId = id;
    }
    engine.startDrill(scenario);
    if (path != AI)
    {
      engine.setControlledPlayer(id);
    }
    // He dribbles, and does nothing else with the ball.
    if (withBall)
    {
      engine.keepBall(id, true);
    }
    aiGoal.reset();
    looseBall.reset();
    ballWatch.reset();
    trail.clear();
    trail.push_back({.at = at, .speed = 0.0F});
    distanceRun = 0.0F;
    lastPosition = at;
  }

  /**
   * The AI runner's plan: where he is going (`urgent` runs flat out), and
   * up to two waypoints on the way, and his line through them (how far
   * inside each the turn's apex may pass, negative outside, and how far
   * out the line may be pushed). You ignore it: you steer yourself. While
   * he is after a lost ball, the plan waits. `facing` asks his body to
   * face that way (an angle like facingAngle) whichever way he runs.
   */
  void steerTo(MatchEngine& engine, PitchPoint at, bool urgent,
               std::vector<PitchPoint> route = {}, float apexOffset = 0.0F,
               float outwardRoom = 0.0F,
               std::optional<float> facing = std::nullopt, bool atSpeed = false)
  {
    if (path != AI)
    {
      return;
    }
    const auto same = [](PitchPoint a, PitchPoint b)
    { return a.x == b.x && a.y == b.y; };
    if (aiGoal && same(aiGoal->at, at) && aiGoal->urgent == urgent &&
        std::ranges::equal(aiGoal->route, route, same) &&
        aiGoal->apexOffset == apexOffset &&
        aiGoal->outwardRoom == outwardRoom && aiGoal->facing == facing &&
        aiGoal->atSpeed == atSpeed)
    {
      return;
    }
    aiGoal = Goal{.at = at,
                  .urgent = urgent,
                  .route = std::move(route),
                  .apexOffset = apexOffset,
                  .outwardRoom = outwardRoom,
                  .facing = facing,
                  .atSpeed = atSpeed};
    sendPlan(engine);
  }

  void sendPlan(MatchEngine& engine)
  {
    if (aiGoal && !looseBall)
    {
      engine.setDrillTarget(id, aiGoal->at, aiGoal->urgent, aiGoal->route,
                            aiGoal->apexOffset, aiGoal->outwardRoom,
                            aiGoal->facing, aiGoal->atSpeed);
    }
  }

  /** The runner after a step (nullptr if gone, or the ball he dribbles went
   * out of play); keeps the trail, watches the ball and sends him after it
   * when he has lost it. */
  const MatchPlayer* track(MatchEngine& engine)
  {
    const MatchPlayer* player = slotOf(engine, id);
    if (player == nullptr)
    {
      return nullptr;
    }
    // Nobody takes the restart in a drill: the run ends there.
    if (withBall && engine.getState() != MatchState::PLAYING)
    {
      ballWatch.out = true;
      return nullptr;
    }
    distanceRun += metresBetween(lastPosition, player->position);
    lastPosition = player->position;
    if (engine.getSimulatedSteps() % 2 == 0)
    {
      trail.push_back({.at = player->position, .speed = speedOf(*player)});
    }
    if (withBall)
    {
      ballWatch.observe(engine, *player);
      const MatchBall& ball = engine.getBall();
      if (engine.getPossessionAttachment() == nullptr)
      {
        looseBall = ball.position;
        if (path == AI)
        {
          engine.setDrillTarget(id, ball.position, true);
        }
      }
      else if (looseBall)
      {
        // Back on the ball: back to the course.
        looseBall.reset();
        sendPlan(engine);
      }
    }
    return player;
  }

  /** The ball's lines of a report (none without the ball). */
  void addBallLines(std::vector<std::string>& lines) const
  {
    if (!withBall)
    {
      return;
    }
    lines.push_back(std::format(
        "Ball: {} touches (met {:.2f} m from him) · gap mean {:.2f} m, max "
        "{:.2f} m · lost {} ({:.1f} s without)",
        ballWatch.touches, ballWatch.meanContactGap(), ballWatch.meanGap(),
        ballWatch.largestGap, ballWatch.lost,
        static_cast<float>(ballWatch.stepsWithout) * STEP));
    if (ballWatch.failedTouches > 0)
    {
      lines.push_back(
          std::format("Touches that went wrong: {}", ballWatch.failedTouches));
    }
    lines.push_back(
        std::format("Ball placed, not moved: {} times, largest {:.2f} m",
                    ballWatch.placed, ballWatch.largestPlacement));
    if (ballWatch.out)
    {
      lines.emplace_back("The ball went out of play: run over.");
    }
  }

  /** The ball's metrics of a finished run (none without the ball). */
  void addBallMetrics(std::vector<DrillMetric>& out) const
  {
    if (!withBall)
    {
      return;
    }
    out.push_back(
        {.name = "Touches", .value = static_cast<double>(ballWatch.touches)});
    out.push_back(
        {.name = "Metres per touch",
         .value = ballWatch.touches > 0
                      ? distanceRun / static_cast<double>(ballWatch.touches)
                      : 0.0});
    out.push_back(
        {.name = "Contact gap (m)", .value = ballWatch.meanContactGap()});
    out.push_back({.name = "Failed touches",
                   .value = static_cast<double>(ballWatch.failedTouches)});
    out.push_back({.name = "Mean gap (m)", .value = ballWatch.meanGap()});
    out.push_back({.name = "Max gap (m)", .value = ballWatch.largestGap});
    out.push_back(
        {.name = "Ball lost", .value = static_cast<double>(ballWatch.lost)});
    out.push_back(
        {.name = "Without the ball (s)",
         .value = static_cast<double>(ballWatch.stepsWithout) * STEP});
    out.push_back({.name = "Ball placed",
                   .value = static_cast<double>(ballWatch.placed)});
    out.push_back(
        {.name = "Largest placement (m)", .value = ballWatch.largestPlacement});
  }

  void drawTrail(const IMatchRenderer& renderer, ImDrawList& draw) const
  {
    const float scale = Theme::scale();
    for (std::size_t point = 1; point < trail.size(); ++point)
    {
      DrillDraw::line(renderer, draw, trail[point - 1].at, trail[point].at,
                      speedColour(trail[point].speed),
                      SPEED_TRAIL_WIDTH_PIXELS * scale);
    }
  }

  [[nodiscard]] std::string runnerLine() const
  {
    if (withBall)
    {
      return std::format("{} · with the ball · {} · {}", PATH_NAMES.at(path),
                         runner.describe(), dribbler.describe());
    }
    return std::format("{} · {}", PATH_NAMES.at(path), runner.describe());
  }

  /** The runner's settings, for keys that tell runs apart. */
  [[nodiscard]] std::string runnerKey() const
  {
    return withBall ? runner.describe() + "|ball|" + dribbler.describe()
                    : runner.describe();
  }

  struct Goal
  {
    PitchPoint at;
    bool urgent = true;
    std::vector<PitchPoint> route;
    float apexOffset = 0.0F;
    float outwardRoom = 0.0F;
    std::optional<float> facing;
    /** Reached at full speed (a run through a finish). */
    bool atSpeed = false;
  };
  struct TrailPoint
  {
    PitchPoint at;
    float speed = 0.0F;
  };

  [[nodiscard]] PlayerID runnerId() const { return id; }
  [[nodiscard]] Path runnerPath() const { return path; }
  [[nodiscard]] bool runUsesBall() const { return withBall; }
  [[nodiscard]] float distanceTravelled() const { return distanceRun; }

 private:
  float pathChoice = AI;
  float ballChoice = 0.0F;
  RunnerAttributes runner;
  DribblerAttributes dribbler;

  PlayerID id = 0;
  /** The path of the current run (the setting applies on restart). */
  Path path = AI;
  /** The With the ball setting of the current run. */
  bool withBall = false;
  BallWatch ballWatch;
  /** Where the lost ball is, while he is sent after it. */
  std::optional<PitchPoint> looseBall;
  /** The AI runner's plan (as last sent). */
  std::optional<Goal> aiGoal;
  std::vector<TrailPoint> trail;
  float distanceRun = 0.0F;
  PitchPoint lastPosition;
};

// --- Sprint ----------------------------------------------------------------

/**
 * Sprint: one runner, flat out along a straight line, from standing or with
 * a run-up. Times every 10 m, the finish, top speed and the reaction delay.
 */
class SprintDrill final : public MovementDrill
{
 public:
  [[nodiscard]] const char* name() const override { return "Sprint"; }
  [[nodiscard]] const char* summary() const override
  {
    return "One runner sprints a straight line, from standing or with a "
           "run-up. Splits every 10 m, finish time, top speed, reaction.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting(
        "Distance (m)", &distance,
        {.min = MIN_DISTANCE_METRES, .max = MAX_DISTANCE_METRES}, true));
    list.push_back(setting("Run-up (m)", &runUp,
                           {.min = 0.0F, .max = MAX_RUN_UP_METRES}, true,
                           "0: a standing start, timed from the start signal. "
                           "Otherwise a flying start, timed from the line."));
    DrillParameter runs = setting("Runs", &runsChoice,
                                  {.min = 0.0F, .max = LAST_RUN_CHOICE}, true,
                                  "The way his body faces as he goes (the AI "
                                  "runner; facing another way than he runs is "
                                  "the Binturong body's). Backward and "
                                  "sideways have only a walk and a jog. He "
                                  "turns his body first, gliding as he does.");
    runs.choices = {"Forward",
                    "Backward",
                    "Side-step left",
                    "Side-step right",
                    "Diagonal forward-left",
                    "Diagonal forward-right",
                    "Diagonal back-left",
                    "Diagonal back-right"};
    list.push_back(runs);
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    metres = static_cast<int>(distance);
    runUpMetres = static_cast<int>(runUp);
    startX = PITCH.atX(START_LINE);
    finishX = startX + (static_cast<float>(metres));
    const float from = std::max(PITCH.atX(TRACK_MARGIN),
                                startX - (static_cast<float>(runUpMetres)));
    runs = static_cast<int>(std::round(runsChoice));
    startRunner(engine, ids, PitchPoint{from, PITCH.atY(TRACK_CENTRE)});
    // Through the line: the target lies beyond the finish. His body faces
    // down the track (0), back up it, across it (his left or right to the
    // run), or at 45 deg on the diagonals (real angles, as facings).
    constexpr std::array<float, 8> FACINGS{0.0F,
                                           PI,
                                           HALF_PI * PI,
                                           -HALF_PI * PI,
                                           QUARTER_PI * PI,
                                           -QUARTER_PI * PI,
                                           THREE_QUARTER_PI * PI,
                                           -THREE_QUARTER_PI * PI};
    steerTo(engine,
            PitchPoint{finishX + RUN_THROUGH_METRES, PITCH.atY(TRACK_CENTRE)},
            true, {}, 0.0F, 0.0F,
            runs == 0 ? std::nullopt
                      : std::optional<float>(facingAngleOf(
                            FACINGS.at(static_cast<std::size_t>(runs)))),
            true);
    lastX = from;
    crossings.assign(static_cast<std::size_t>(metres / SPLIT_METRES) + 1,
                     std::nullopt);
    if (runUpMetres == 0)
    {
      crossings[0] = 0.0;
    }
    finishTime.reset();
    reaction.reset();
    topSpeed = 0.0F;
    topSpeedAt = 0.0F;
    // The track's lines, laid on the pitch: a mark every 10 m, the start
    // and the finish.
    PitchKit::Layout kit;
    const float middle = PITCH.atY(TRACK_CENTRE);
    constexpr float HALF = TRACK_HALF_WIDTH_METRES;
    for (std::size_t mark = 1; mark < crossings.size(); ++mark)
    {
      const float x = startX + static_cast<float>(mark * SPLIT_METRES);
      if (x >= finishX - FINISH_LINE_TOLERANCE_METRES)
      {
        break;
      }
      kit.lines.push_back({.name = std::format("{} m", mark * SPLIT_METRES),
                           .from = PitchPoint{x, middle - HALF},
                           .to = PitchPoint{x, middle + HALF},
                           .colour = IM_COL32(255, 255, 255, 120)});
    }
    kit.lines.push_back(
        {.name = "Start",
         .from = PitchPoint{startX, middle - (HALF * END_LINE_SCALE)},
         .to = PitchPoint{startX, middle + (HALF * END_LINE_SCALE)},
         .colour = IM_COL32(90, 220, 120, 255)});
    kit.lines.push_back(
        {.name = "Finish",
         .from = PitchPoint{finishX, middle - (HALF * END_LINE_SCALE)},
         .to = PitchPoint{finishX, middle + (HALF * END_LINE_SCALE)},
         .colour = IM_COL32(240, 80, 80, 255)});
    engine.setKit(std::move(kit));
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (player == nullptr)
    {
      return false;
    }
    const double now = secondsOf(engine);
    const float x = player->position.x;
    const float speed = speedOf(*player);
    if (!reaction && speed > REACTION_SPEED_METRES_PER_SECOND)
    {
      reaction = now - STEP;
    }
    // A line is crossed between two steps: interpolate the moment.
    const auto crossed = [&](float lineX) -> std::optional<double>
    {
      if (lastX >= lineX || x < lineX || x <= lastX)
      {
        return std::nullopt;
      }
      return now - STEP +
             (STEP * static_cast<double>((lineX - lastX) / (x - lastX)));
    };
    for (std::size_t mark = 0; mark < crossings.size(); ++mark)
    {
      if (!crossings[mark])
      {
        crossings[mark] =
            crossed(startX + (static_cast<float>(mark * SPLIT_METRES)));
      }
    }
    if (!finishTime)
    {
      finishTime = crossed(finishX);
    }
    if (crossings[0] && x <= finishX && speed > topSpeed)
    {
      topSpeed = speed;
      topSpeedAt = x - startX;
    }
    lastX = x;
    // Over half a second after the finish, or a minute at most.
    return (!finishTime || now <= *finishTime + FINISH_DELAY_SECONDS) &&
           now < MAX_DURATION_SECONDS;
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    // The lines are kit on the pitch (begin); the labels stay here.
    constexpr float half = TRACK_HALF_WIDTH_METRES;
    const float middle = PITCH.atY(TRACK_CENTRE);
    DrillDraw::label(renderer, draw,
                     PitchPoint{startX, middle - (half * LABEL_OFFSET_SCALE)},
                     "start", IM_COL32(90, 220, 120, 255));
    DrillDraw::label(renderer, draw,
                     PitchPoint{finishX, middle - (half * LABEL_OFFSET_SCALE)},
                     std::format("{} m", metres).c_str(),
                     IM_COL32(240, 80, 80, 255));
    drawTrail(renderer, draw);
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format("{} m, {}", metres,
                                runUpMetres == 0
                                    ? std::string("standing start")
                                    : std::format("{} m run-up", runUpMetres)));
    lines.push_back(runnerLine());
    if (reaction)
    {
      lines.push_back(std::format("First movement after {:.1f} s", *reaction));
    }
    if (crossings.empty())
    {
      return lines;
    }
    const double start = crossings[0].value_or(0.0);
    std::string splits;
    for (std::size_t mark = 1; mark < crossings.size(); ++mark)
    {
      if (crossings[mark])
      {
        splits += std::format("{} m {:.2f} s   ", mark * SPLIT_METRES,
                              *crossings[mark] - start);
      }
    }
    if (!splits.empty())
    {
      lines.push_back(splits);
    }
    if (finishTime)
    {
      lines.push_back(std::format("FINISH  {:.2f} s", *finishTime - start));
    }
    if (topSpeed > 0.0F)
    {
      lines.push_back(std::format(
          "Top speed {:.2f} m/s ({:.1f} km/h) at {:.0f} m", topSpeed,
          topSpeed * METRES_PER_SECOND_TO_KMH, topSpeedAt));
    }
    addBallLines(lines);
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (!finishTime || crossings.empty())
    {
      return {};
    }
    const double start = crossings[0].value_or(0.0);
    std::vector<DrillMetric> out{
        {.name = "Finish (s)", .value = *finishTime - start}};
    for (std::size_t mark = 1; mark < crossings.size(); ++mark)
    {
      if (crossings[mark] &&
          mark * SPLIT_METRES < static_cast<std::size_t>(metres))
      {
        out.push_back({.name = std::format("{} m (s)", mark * SPLIT_METRES),
                       .value = *crossings[mark] - start});
      }
    }
    out.push_back({.name = "Top speed (m/s)", .value = topSpeed});
    out.push_back({.name = "Top speed at (m)", .value = topSpeedAt});
    addBallMetrics(out);
    return out;
  }

 private:
  static constexpr float MIN_DISTANCE_METRES = 10.0F;
  static constexpr float MAX_DISTANCE_METRES = 80.0F;
  static constexpr float DEFAULT_DISTANCE_METRES = 30.0F;
  static constexpr float MAX_RUN_UP_METRES = 20.0F;
  static constexpr float LAST_RUN_CHOICE = 7.0F;
  static constexpr float TRACK_MARGIN = 0.02F;
  static constexpr float TRACK_CENTRE = 0.5F;
  static constexpr float HALF_PI = 0.5F;
  static constexpr float QUARTER_PI = 0.25F;
  static constexpr float THREE_QUARTER_PI = 0.75F;
  static constexpr float FINISH_LINE_TOLERANCE_METRES = 0.01F;
  static constexpr float TRACK_HALF_WIDTH_METRES = 4.0F;
  static constexpr float END_LINE_SCALE = 1.5F;
  static constexpr float LABEL_OFFSET_SCALE = 2.4F;
  static constexpr float REACTION_SPEED_METRES_PER_SECOND = 0.1F;
  static constexpr float METRES_PER_SECOND_TO_KMH = 3.6F;
  static constexpr int SPLIT_METRES = 10;
  static constexpr double FINISH_DELAY_SECONDS = 0.5;
  static constexpr double MAX_DURATION_SECONDS = 60.0;
  static constexpr float START_LINE = 0.25F;

  float distance = DEFAULT_DISTANCE_METRES;
  float runUp = 0.0F;
  float runsChoice = 0.0F;

  int metres = static_cast<int>(DEFAULT_DISTANCE_METRES);
  int runUpMetres = 0;
  int runs = 0;
  float startX = START_LINE;
  float finishX = START_LINE;
  float lastX = 0.0F;
  std::vector<std::optional<double>> crossings;
  std::optional<double> finishTime;
  std::optional<double> reaction;
  float topSpeed = 0.0F;
  float topSpeedAt = 0.0F;
};

// --- Courses: points to reach in order ------------------------------------

constexpr ImU32 COURSE_FINISH_COLOUR = IM_COL32(240, 80, 80, 255);
constexpr ImU32 COURSE_CONE_COLOUR = IM_COL32(255, 140, 30, 255);
constexpr ImU32 COURSE_START_COLOUR = IM_COL32(90, 220, 120, 255);
constexpr ImU32 PASSED_GATE_COLOUR = IM_COL32(120, 120, 120, 200);
constexpr ImU32 DUE_GATE_COLOUR = IM_COL32(255, 255, 255, 240);
constexpr ImU32 FUTURE_GATE_COLOUR = IM_COL32(255, 255, 255, 120);
constexpr ImU32 INACTIVE_TRACK_COLOUR = IM_COL32(255, 255, 255, 160);
constexpr ImU32 FLAG_AREA_COLOUR = IM_COL32(255, 170, 40, 200);
constexpr ImU32 PASSED_FLAG_COLOUR = IM_COL32(150, 150, 150, 180);
constexpr ImU32 FLAG_ROUTE_COLOUR = IM_COL32(255, 255, 255, 70);
constexpr ImU32 UPCOMING_FLAG_COLOUR = IM_COL32(120, 170, 255, 200);
constexpr ImU32 CURRENT_FLAG_COLOUR = IM_COL32(255, 220, 40, 255);
constexpr ImU32 SWITCH_LINE_COLOUR = IM_COL32(255, 200, 60, 255);

/**
 * A course of points the runner reaches in turn (within an arrival radius).
 * The runner aims at each point itself (and turns there); the last one he
 * runs through. Times are interpolated within the step.
 */
class CourseDrill : public MovementDrill
{
  static constexpr float MIN_ARRIVAL_RADIUS_METRES = 0.3F;
  static constexpr float FINISH_HALF_WIDTH_METRES = 4.0F;
  static constexpr float HALF_SHARE = 0.5F;

 protected:
  /** Sets the course up: the runner at `start`, then `points` in order. */
  void beginCourse(MatchEngine& engine, const std::vector<PlayerID>& ids,
                   PitchPoint start, std::vector<PitchPoint> course,
                   bool finishLine = true)
  {
    startRunner(engine, ids, start);
    startPoint = start;
    points = std::move(course);
    reached.clear();
    finished = false;
    radius = std::max(MIN_ARRIVAL_RADIUS_METRES, arrivalRadius);
    const MatchPlayer* player = slotOf(engine, runnerId());
    advancesSeen = (player != nullptr) ? player->routeAdvances : 0;
    movedOn = 0;
    planRun(engine);
    // The finish line, laid on the pitch (the drill adds its cones).
    PitchKit::Layout kit;
    if (finishLine && !points.empty())
    {
      const Vector2F across = rotated(finishDirection(), PI * HALF_SHARE);
      kit.lines.push_back(
          {.name = "Finish",
           .from = along(points.back(), across, -FINISH_HALF_WIDTH_METRES),
           .to = along(points.back(), across, FINISH_HALF_WIDTH_METRES),
           .colour = COURSE_FINISH_COLOUR});
    }
    engine.setKit(std::move(kit));
  }

  /** Adds a cone to the kit laid out for the course. */
  static void addCone(MatchEngine& engine, const char* name, PitchPoint at,
                      PitchKit::ConeKind kind = PitchKit::ConeKind::TALL)
  {
    PitchKit::Layout kit = engine.getKit();
    kit.cones.push_back(
        {.name = name, .at = at, .kind = kind, .colour = COURSE_CONE_COLOUR});
    engine.setKit(std::move(kit));
  }
  /** A dummy into the engine's kit (plan section 21). */
  static void addDummy(MatchEngine& engine, const std::string& name,
                       PitchPoint at, PitchKit::DummyKind kind,
                       float facing = 0.0F)
  {
    PitchKit::Layout kit = engine.getKit();
    PitchKit::Dummy dummy;
    dummy.name = name;
    dummy.at = at;
    dummy.kind = kind;
    dummy.facing = facing;
    kit.dummies.push_back(std::move(dummy));
    engine.setKit(std::move(kit));
  }

  /**
   * The AI runner's plan, as his side's brain would make it: he is going to
   * the end of the course, by the next two of its points as waypoints. As
   * his body moves on from each one (where its turn begins), the plan moves
   * on to the next two.
   */
  void followPlan(MatchEngine& engine, const MatchPlayer& player)
  {
    if (player.routeAdvances != advancesSeen)
    {
      movedOn += player.routeAdvances - advancesSeen;
      advancesSeen = player.routeAdvances;
    }
    planRun(engine);
  }

  void planRun(MatchEngine& engine)
  {
    if (points.empty())
    {
      return;
    }
    std::vector<PitchPoint> ahead = waypointsAhead();
    if (ahead.size() > 2)
    {
      ahead.resize(2);
    }
    const PitchPoint end =
        runThroughLast
            ? along(finishAim(), finishDirection(), RUN_THROUGH_METRES)
            : points.back();
    steerTo(engine, end, true, std::move(ahead), apexOffset(), outwardRoom(),
            std::nullopt, runThroughLast);
  }

  /** The waypoints still ahead of him: the course's points before its
   * end that his body has not moved on from. */
  [[nodiscard]] virtual std::vector<PitchPoint> waypointsAhead() const
  {
    const std::size_t first = std::min<std::size_t>(movedOn, points.size() - 1);
    return {points.begin() + static_cast<std::ptrdiff_t>(first),
            points.end() - 1};
  }

  /** His line through the waypoints: by default he cuts each corner by up
   * to half the radius that counts as reaching it, and the line is not
   * pushed out (step 3's line). */
  [[nodiscard]] virtual float apexOffset() const { return radius * HALF_SHARE; }

  /** Share of the move from `from` to `to` at which he reaches point
   * `index` (not the finish), if he does: within the radius. */
  [[nodiscard]] virtual std::optional<float> reachShare(std::size_t index,
                                                        PitchPoint from,
                                                        PitchPoint to) const
  {
    return entryShare({.from = from, .to = to},
                      {.centre = points[index], .radius = radius});
  }
  [[nodiscard]] virtual float outwardRoom() const { return 0.0F; }

  /** Call after every step; true when the runner reached the next point. */
  bool advanceCourse(MatchEngine& engine, const MatchPlayer& player,
                     PitchPoint previous)
  {
    followPlan(engine, player);
    if (finished || reached.size() >= points.size())
    {
      return false;
    }
    const std::size_t next = reached.size();
    const PitchPoint target = points[next];
    // The last point of a run-through course is a finish line across the
    // last leg; the others are reached within the radius.
    const auto share =
        next + 1 == points.size() && runThroughLast
            ? crossingShare({.from = previous, .to = player.position},
                            {.point = target, .direction = finishDirection()})
            : reachShare(next, previous, player.position);
    if (!share)
    {
      return false;
    }
    recordCoursePoint(secondsOf(engine) - STEP +
                      (STEP * static_cast<double>(*share)));
    return true;
  }

  /** Direction across which the finish line lies (and the run goes on
   * through it): along the last leg. */
  /** Where on the finish line he aims to cross it (it counts anywhere
   * along it): its middle, by default. */
  [[nodiscard]] virtual PitchPoint finishAim() const { return points.back(); }

  [[nodiscard]] virtual Vector2F finishDirection() const
  {
    if (points.empty())
    {
      return {.x = 1.0F, .y = 0.0F};
    }
    const PitchPoint from =
        points.size() == 1 ? startPoint : points[points.size() - 2];
    return directionBetween(from, points.back());
  }

  /** The finish line of a run-through course, 8 m wide. */
  /** Straight-line length of leg `index` (from the start or the point before).
   */
  [[nodiscard]] float legMetres(std::size_t index) const
  {
    const PitchPoint from = index == 0 ? startPoint : points[index - 1];
    return metresBetween(from, points[index]);
  }
  /** Time of leg `index`, once reached. */
  [[nodiscard]] double legSeconds(std::size_t index) const
  {
    return reached[index] - (index == 0 ? 0.0 : reached[index - 1]);
  }
  [[nodiscard]] float courseMetres() const
  {
    float total = 0.0F;
    for (std::size_t index = 0; index < points.size(); ++index)
    {
      total += legMetres(index);
    }
    return total;
  }

  [[nodiscard]] DrillParameter courseArrivalParameter(
      SettingRange range, const char* tooltip = nullptr)
  {
    return setting("Arrival radius (m)", &arrivalRadius, range, false, tooltip);
  }
  void stopAtFinish() { runThroughLast = false; }
  [[nodiscard]] PitchPoint courseStart() const { return startPoint; }
  [[nodiscard]] const std::vector<PitchPoint>& coursePoints() const
  {
    return points;
  }
  [[nodiscard]] const std::vector<double>& reachedTimes() const
  {
    return reached;
  }
  [[nodiscard]] bool courseFinished() const { return finished; }
  [[nodiscard]] float courseRadius() const { return radius; }
  [[nodiscard]] std::size_t movedWaypointCount() const { return movedOn; }
  void resetMovedWaypoints() { movedOn = 0; }
  void appendCoursePoint(PitchPoint point)
  {
    points.push_back(point);
    finished = false;
  }
  void recordCoursePoint(double when)
  {
    reached.push_back(when);
    if (reached.size() >= points.size())
    {
      finished = true;
    }
  }

 private:
  float arrivalRadius = 1.0F;
  bool runThroughLast = true;

  PitchPoint startPoint;
  std::vector<PitchPoint> points;
  std::vector<double> reached;
  bool finished = false;
  float radius = 1.0F;
  /** Waypoints his body has moved on from (its count of them, as last
   * seen), for the plan. */
  std::uint32_t advancesSeen = 0;
  std::size_t movedOn = 0;
};

// --- Turn ------------------------------------------------------------------

/**
 * Turn: a sprint to a cone, a turn through a set angle there, and a sprint
 * on to the finish. 0° is the same distance straight, for comparison.
 */
class TurnDrill final : public CourseDrill
{
  static constexpr float APPROACH_MIN = 5.0F;
  static constexpr float APPROACH_MAX = 40.0F;
  static constexpr float ANGLE_CHOICE_MAX = 4.0F;
  static constexpr float EXIT_MIN = 5.0F;
  static constexpr float EXIT_MAX = 30.0F;
  static constexpr float ARRIVAL_RADIUS_MAX = 3.0F;
  static constexpr float APEX_SETTING_MIN = -2.0F;
  static constexpr float APEX_SETTING_MAX = 2.0F;
  static constexpr float ROOM_SETTING_MAX = 10.0F;
  static constexpr float DEFAULT_APPROACH = 20.0F;
  static constexpr float DEFAULT_ANGLE_CHOICE = 2.0F;
  static constexpr float DEFAULT_EXIT = 10.0F;
  static constexpr float DEFAULT_DEGREES = 90.0F;
  static constexpr int TURN_STEP_DEGREES = 45;
  static constexpr float HALF_TURN_DEGREES = 180.0F;
  static constexpr float START_LENGTH_SHARE = 0.3F;
  static constexpr float START_WIDTH_SHARE = 0.35F;
  static constexpr float MIN_CORNER_DIRECTION_LENGTH = 1e-3F;
  static constexpr float START_MARKER_RADIUS = 4.0F;
  static constexpr double FINISH_DELAY_SECONDS = 0.5;
  static constexpr double MAX_DURATION_SECONDS = 60.0;

 public:
  [[nodiscard]] const char* name() const override { return "Turn"; }
  [[nodiscard]] const char* summary() const override
  {
    return "Sprint to a cone, turn through 45-180° and sprint on to the "
           "finish. Times either side of the cone, the speed lost in the "
           "turn and how wide he swings. Run 0° for the straight "
           "comparison: the time lost is shown against it.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Approach (m)", &approach,
                           {.min = APPROACH_MIN, .max = APPROACH_MAX}, true));
    DrillParameter angle = setting(
        "Turn", &angleChoice, {.min = 0.0F, .max = ANGLE_CHOICE_MAX}, true);
    angle.choices = {"0° (straight)", "45°", "90°", "135°", "180°"};
    list.push_back(angle);
    list.push_back(
        setting("Exit (m)", &exit, {.min = EXIT_MIN, .max = EXIT_MAX}, true));
    list.push_back(courseArrivalParameter(
        {.min = START_LENGTH_SHARE, .max = ARRIVAL_RADIUS_MAX},
        "How close to the cone counts as reaching it (so does "
        "crossing the corner's middle line, on any line)."));
    list.push_back(
        setting("Apex offset (m)", &apexSetting,
                {.min = APEX_SETTING_MIN, .max = APEX_SETTING_MAX}, false,
                "The AI runner's line: how far inside the cone the turn's "
                "apex may pass (cutting the corner). 0 goes through the "
                "cone; negative passes outside it."));
    list.push_back(
        setting("Room outside (m)", &roomSetting,
                {.min = 0.0F, .max = ROOM_SETTING_MAX}, false,
                "How far out the AI runner's line may be pushed at the "
                "corner to widen the turn."));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    degrees =
        static_cast<float>(static_cast<int>(angleChoice) * TURN_STEP_DEGREES);
    approachMetres = approach;
    exitMetres = exit;
    apex = apexSetting;
    room = roomSetting;
    closest.reset();
    // Start on the left of the centre circle so a 180° turn stays on the
    // pitch; turns go towards the near touchline (+y).
    const PitchPoint start = at(START_LENGTH_SHARE, START_WIDTH_SHARE);
    cone = along(start, {.x = 1.0F, .y = 0.0F}, approachMetres);
    exitDirection =
        rotated({.x = 1.0F, .y = 0.0F}, degrees * PI / HALF_TURN_DEGREES);
    finishPoint = along(cone, exitDirection, exitMetres);
    beginCourse(engine, ids, start, {cone, finishPoint});
    addCone(engine, "Turn", cone);
    minSpeed.reset();
    entrySpeed = 0.0F;
    overshoot = 0.0F;
    previous = start;
    resultSaved = false;
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (player == nullptr)
    {
      return false;
    }
    const bool wasAtCone = !reachedTimes().empty();
    const float speed = speedOf(*player);
    advanceCourse(engine, *player, previous);
    if (!wasAtCone && !reachedTimes().empty())
    {
      entrySpeed = speed;
    }
    // Where he passes the cone: his closest to it, inside the turn or out.
    if (!courseFinished())
    {
      const Vector2F off = player->position - cone;
      const float metres = std::hypot(off.x, off.y);
      if (!closest || metres < std::abs(*closest))
      {
        const Vector2F inward{.x = exitDirection.x - 1.0F,
                              .y = exitDirection.y};
        closest =
            off.x * inward.x + off.y * inward.y >= 0.0F ? metres : -metres;
      }
    }
    if (!reachedTimes().empty() && !courseFinished())
    {
      // Through the turn: the slowest moment and how wide he swings off the
      // line from the cone to the finish.
      if (!minSpeed || speed < *minSpeed)
      {
        minSpeed = speed;
      }
      const Vector2F off = player->position - cone;
      overshoot = std::max(overshoot, std::abs((off.x * exitDirection.y) -
                                               (off.y * exitDirection.x)));
    }
    previous = player->position;
    const double now = secondsOf(engine);
    if (courseFinished() && !resultSaved)
    {
      results[settingsKey()] = reachedTimes().back();
      resultSaved = true;
    }
    return (!courseFinished() ||
            now <= reachedTimes().back() + FINISH_DELAY_SECONDS) &&
           now < MAX_DURATION_SECONDS;
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    drawTrail(renderer, draw);
    DrillDraw::marker(renderer, draw, courseStart(),
                      START_MARKER_RADIUS * scale, COURSE_START_COLOUR,
                      "start");
    // The cone is kit on the pitch (begin); its angle is labelled here.
    DrillDraw::label(renderer, draw, cone,
                     std::format("{:.0f}°", degrees).c_str(),
                     COURSE_CONE_COLOUR);
    DrillDraw::label(renderer, draw, finishPoint, "finish",
                     COURSE_FINISH_COLOUR);
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format("{:.0f} m, turn {:.0f}°, {:.0f} m",
                                approachMetres, degrees, exitMetres));
    lines.push_back(runnerLine());
    if (!reachedTimes().empty())
    {
      lines.push_back(std::format("To the cone {:.2f} s (at {:.2f} m/s)",
                                  reachedTimes()[0], entrySpeed));
    }
    if (minSpeed && degrees > 0.0F)
    {
      lines.push_back(
          std::format("Slowest in the turn {:.2f} m/s · swings wide "
                      "{:.1f} m",
                      *minSpeed, overshoot));
    }
    if (closest && degrees > 0.0F)
    {
      lines.push_back(std::format("Passed the cone {:.2f} m {}",
                                  std::abs(*closest),
                                  *closest >= 0.0F ? "inside" : "outside"));
    }
    if (courseFinished())
    {
      lines.push_back(std::format("Cone to finish {:.2f} s", legSeconds(1)));
      lines.push_back(std::format("TOTAL  {:.2f} s · {:.1f} m run",
                                  reachedTimes().back(), distanceTravelled()));
      if (degrees > 0.0F)
      {
        const auto straight = results.find(settingsKey(0.0F));
        if (straight != results.end())
        {
          lines.emplace_back(
              std::format("Time lost to the turn: {:+.2f} s (vs 0°)",
                          reachedTimes().back() - straight->second));
        }
        else
        {
          lines.emplace_back(
              "Run 0° with the same settings to see the time lost.");
        }
      }
    }
    addBallLines(lines);
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (!courseFinished() || reachedTimes().size() < 2)
    {
      return {};
    }
    std::vector<DrillMetric> out{
        {.name = "Total (s)", .value = reachedTimes().back()},
        {.name = "To the cone (s)", .value = reachedTimes()[0]},
        {.name = "Cone to finish (s)", .value = legSeconds(1)},
        {.name = "Speed at the cone (m/s)", .value = entrySpeed}};
    if (minSpeed && degrees > 0.0F)
    {
      out.push_back({.name = "Slowest in the turn (m/s)", .value = *minSpeed});
      out.push_back({.name = "Swings wide (m)", .value = overshoot});
      if (closest)
      {
        out.push_back(
            {.name = "Passed the cone, inside + (m)", .value = *closest});
      }
    }
    out.push_back({.name = "Distance run (m)", .value = distanceTravelled()});
    addBallMetrics(out);
    return out;
  }

 protected:
  [[nodiscard]] float apexOffset() const override { return apex; }
  [[nodiscard]] float outwardRoom() const override { return room; }

  /** The cone is reached within the radius, or on crossing the corner's
   * middle line (whatever line he takes; a turn straight back has none). */
  [[nodiscard]] std::optional<float> reachShare(std::size_t index,
                                                PitchPoint from,
                                                PitchPoint to) const override
  {
    std::optional<float> share =
        entryShare({.from = from, .to = to},
                   {.centre = coursePoints()[index], .radius = courseRadius()});
    const Vector2F across{.x = 1.0F + exitDirection.x, .y = exitDirection.y};
    const float size = std::hypot(across.x, across.y);
    if (index == 0 && size > MIN_CORNER_DIRECTION_LENGTH)
    {
      if (const auto crossed = crossingShare(
              {.from = from, .to = to},
              {.point = cone,
               .direction = {.x = across.x / size, .y = across.y / size}});
          crossed && (!share || *crossed < *share))
      {
        share = crossed;
      }
    }
    return share;
  }

 private:
  [[nodiscard]] std::string settingsKey(std::optional<float> angle = {}) const
  {
    return std::format("{}|{:.0f}|{:.0f}|{:.0f}|{:.2f}|{:.2f}|{:.2f}|{}",
                       static_cast<int>(runnerPath()), approachMetres,
                       angle.value_or(degrees), exitMetres, courseRadius(),
                       apex, room, runnerKey());
  }

  float approach = DEFAULT_APPROACH;
  float angleChoice = DEFAULT_ANGLE_CHOICE;
  float exit = DEFAULT_EXIT;
  float apexSetting = 0.5F;
  float roomSetting = 0.0F;
  float apex = 0.5F;
  float room = 0.0F;
  /** His closest to the cone (m), inside the turn + or outside -. */
  std::optional<float> closest;

  float degrees = DEFAULT_DEGREES;
  float approachMetres = DEFAULT_APPROACH;
  float exitMetres = DEFAULT_EXIT;
  PitchPoint cone;
  Vector2F exitDirection{.x = 1.0F, .y = 0.0F};
  PitchPoint finishPoint;
  PitchPoint previous;
  std::optional<float> minSpeed;
  float entrySpeed = 0.0F;
  float overshoot = 0.0F;
  /** Last total by settings, for the time lost against 0°. */
  std::map<std::string, double> results;
  bool resultSaved = false;
};

// --- Slalom ----------------------------------------------------------------

/**
 * Slalom: a line of cones, each passed on alternate sides, then the finish.
 * Each cone's gate is anywhere its correct side, from a body's width out
 * (he cannot run through the cone), and counts only when run through
 * forwards; going round a cone the wrong side misses it, and he has to come
 * back through it. However wide he goes still counts: closer is just
 * quicker. With the ball, the ball has to go through each gate as well (a
 * gate is passed once both have). He aims to pass a set distance outside
 * the cone (his line): through a point there, or, where the course turns
 * back on itself, to a stop just past the gate, turning there. The cones
 * can be staggered, each pushed out to the side it is passed on, for a
 * wider weave.
 */
class SlalomDrill final : public CourseDrill
{
  struct GateStatus
  {
    bool passed{};
    bool due{};
    bool goingBack{};
  };
  [[nodiscard]] static ImU32 gateColour(GateStatus status)
  {
    if (status.passed)
    {
      return PASSED_GATE_COLOUR;
    }
    if (status.due && status.goingBack)
    {
      return COURSE_FINISH_COLOUR;
    }
    if (status.due)
    {
      return DUE_GATE_COLOUR;
    }
    return FUTURE_GATE_COLOUR;
  }

  static constexpr float CONES_MIN = 2.0F;
  static constexpr float CONES_MAX = 12.0F;
  static constexpr float SPACING_MIN = 2.0F;
  static constexpr float SPACING_MAX = 15.0F;
  static constexpr float LINE_OFFSET_MAX = 3.0F;
  static constexpr float CONE_STAGGER_MAX = 6.0F;
  static constexpr float DEFAULT_CONES = 6.0F;
  static constexpr float DEFAULT_SPACING = 5.0F;
  static constexpr float HALF_SHARE = 0.5F;
  static constexpr float MIN_COURSE_SPACING_METRES = 0.1F;
  static constexpr float START_MARKER_RADIUS = 4.0F;
  static constexpr float AIM_MARKER_RADIUS = 2.5F;
  static constexpr float GATE_LINE_WIDTH = 2.0F;
  static constexpr double FINISH_DELAY_SECONDS = 0.5;
  static constexpr double MAX_DURATION_SECONDS = 120.0;

 public:
  [[nodiscard]] const char* name() const override { return "Slalom"; }
  [[nodiscard]] const char* summary() const override
  {
    return "Weave through a line of cones, round each on alternate sides, "
           "then sprint through the finish. A cone passed the wrong side has "
           "to be gone back for; wide is allowed, just slower. With the ball, "
           "the ball has to go through each gate too. Time, splits, gates "
           "missed and the path run.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(
        setting("Cones", &cones, {.min = CONES_MIN, .max = CONES_MAX}, true,
                "How many cones, as many as fit: the course runs up the "
                "pitch from 5 m in from one end to 5 m from the other, so "
                "wide spacing takes fewer."));
    list.push_back(setting("Spacing (m)", &spacing,
                           {.min = SPACING_MIN, .max = SPACING_MAX}, false));
    DrillParameter lineSetting =
        setting("Line off the cone (m)", &lineOffset,
                {.min = 0.0F, .max = LINE_OFFSET_MAX}, false,
                "Where he aims to pass each cone: his centre this far "
                "outside the cone's edge (at least 0.2 m, so his body "
                "clears it). Orange: set by the course, 0.6 m on a "
                "straight line of cones, out to 0.8 m as the weave gets "
                "sharper (stagger up to the spacing), room for the ball, "
                "which cuts the corner a little. Move it to set your own; "
                "back to 0 to have it set by the course again. The gate is the "
                "whole of the "
                "cone's correct side, from its edge, so going wider "
                "still counts.");
    lineSetting.automatic = [this] { return courseLine(spacing, coneStagger); };
    list.push_back(lineSetting);
    list.push_back(
        setting("Cone stagger (m)", &coneStagger,
                {.min = 0.0F, .max = CONE_STAGGER_MAX}, false,
                "Each cone pushed this far off the line, to the side it "
                "is passed on (alternate cones opposite ways): 0 is a "
                "straight line of cones, more is a wider weave."));
    DrillParameter markers =
        setting("Markers", &markersChoice, {.min = 0.0F, .max = 1.0F}, true,
                "Cones, or agility poles (training ground "
                "T5: solid dummies, 30 mm across).");
    markers.choices = {"Cones", "Poles"};
    list.push_back(markers);
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    gap = spacing;
    // As many cones as fit: the finish (a gap past the last cone) as far
    // in from the far end as the start is from this one.
    const int fit =
        static_cast<int>((LENGTH - GATE_LINE_WIDTH * END_ROOM_METRES) / gap) -
        1;
    const int count = std::clamp(fit, 1, static_cast<int>(cones));
    conesSet = static_cast<int>(cones);
    stagger = coneStagger;
    line = lineOffset > 0.0F ? std::max(lineOffset, MIN_LINE_METRES)
                             : courseLine(gap, stagger);
    const PitchPoint start{END_ROOM_METRES, PITCH.atY(HALF_SHARE)};
    conePoints.clear();
    std::vector<PitchPoint> course;
    for (int cone = 0; cone < count; ++cone)
    {
      const PitchPoint at =
          along(along(start, {.x = 1.0F, .y = 0.0F},
                      gap * static_cast<float>(cone + 1)),
                {.x = 0.0F, .y = 1.0F}, sideOf(cone) * stagger);
      conePoints.push_back(at);
      // He heads for his line round the cone.
      course.push_back(along(at, {.x = 0.0F, .y = 1.0F},
                             sideOf(cone) * (GATE_BUFFER_METRES + line)));
    }
    course.push_back(along(start, {.x = 1.0F, .y = 0.0F},
                           gap * static_cast<float>(count + 1)));
    planAims(start, course);
    missed = 0;
    goingBack = false;
    backPending = false;
    previous = start;
    bodyThrough.reset();
    ballThrough.reset();
    beginCourse(engine, ids, start, std::move(course));
    // The cones are obstacles: he keeps the ball off them. Poles instead
    // (plan section 21) are dummies in the kit.
    const bool poles = markersChoice > HALF_SHARE;
    std::vector<MatchEngine::Obstacle> obstacles;
    obstacles.reserve(conePoints.size());
    for (const PitchPoint& cone : conePoints)
    {
      obstacles.push_back(
          {.position = cone,
           .radiusMetres =
               poles ? PitchKit::dummyShape(PitchKit::DummyKind::POLE).radius
                     : DrillDraw::CONE_RADIUS_METRES});
    }
    engine.setObstacles(std::move(obstacles));
    for (std::size_t cone = 0; cone < conePoints.size(); ++cone)
    {
      if (poles)
      {
        addDummy(engine, std::format("Pole {}", cone + 1), conePoints[cone],
                 PitchKit::DummyKind::POLE);
      }
      else
      {
        addCone(engine, std::format("Cone {}", cone + 1).c_str(),
                conePoints[cone]);
      }
    }
    previousBall = engine.getBall().position;
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (player == nullptr)
    {
      return false;
    }
    followPlan(engine, *player);
    holdBackPoint(engine, *player);
    if (!courseFinished())
    {
      passGates(engine, *player);
    }
    previous = player->position;
    previousBall = engine.getBall().position;
    const double now = secondsOf(engine);
    return (!courseFinished() ||
            now <= reachedTimes().back() + FINISH_DELAY_SECONDS) &&
           now < MAX_DURATION_SECONDS;
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    drawTrail(renderer, draw);
    DrillDraw::marker(renderer, draw, courseStart(),
                      START_MARKER_RADIUS * scale, COURSE_START_COLOUR,
                      "start");
    for (std::size_t cone = 0; cone < conePoints.size(); ++cone)
    {
      // The gate, from a body's width out from its cone (open beyond what
      // is drawn), and the point he aims for: the one due bright, red while
      // he goes back for it (missed), with the point behind it he goes to.
      const bool passed = cone < reachedTimes().size();
      const bool due = cone == reachedTimes().size();
      const ImU32 colour =
          gateColour({.passed = passed, .due = due, .goingBack = goingBack});
      DrillDraw::line(renderer, draw, gateEdge(cone, 0.0F),
                      gateEdge(cone, GATE_DRAWN_METRES), colour,
                      GATE_LINE_WIDTH * scale);
      DrillDraw::marker(renderer, draw, aims[cone], AIM_MARKER_RADIUS * scale,
                        colour);
      if (due && goingBack && backPending)
      {
        DrillDraw::marker(renderer, draw, backPoint(cone),
                          AIM_MARKER_RADIUS * scale, colour, "back");
      }
      // The cone itself is kit on the pitch (begin).
    }
    DrillDraw::label(renderer, draw, coursePoints().back(), "finish",
                     COURSE_FINISH_COLOUR);
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(
        std::format("{} cones, {:.1f} m apart, staggered {:.1f} m, line {:.1f} "
                    "m off the cone",
                    conePoints.size(), gap, stagger, line));
    if (static_cast<int>(conePoints.size()) < conesSet)
    {
      lines.push_back(
          std::format("({} set: only {} fit on the pitch {:.0f} m apart)",
                      conesSet, conePoints.size(), gap));
    }
    lines.push_back(runnerLine());
    std::string splits;
    for (std::size_t index = 0; index < reachedTimes().size(); ++index)
    {
      splits += std::format("{} {:.2f}  ",
                            index + 1 == coursePoints().size()
                                ? std::string("F")
                                : std::to_string(index + 1),
                            reachedTimes()[index]);
    }
    if (!splits.empty())
    {
      lines.push_back(splits);
    }
    lines.push_back(std::format("Gates missed (gone back for): {}", missed));
    if (courseFinished())
    {
      lines.push_back(std::format("TOTAL  {:.2f} s", reachedTimes().back()));
      lines.push_back(std::format(
          "Course {:.1f} m, ran {:.1f} m, average {:.2f} m/s", courseMetres(),
          distanceTravelled(), distanceTravelled() / reachedTimes().back()));
    }
    addBallLines(lines);
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (!courseFinished() || reachedTimes().empty())
    {
      return {};
    }
    std::vector<DrillMetric> out{
        {.name = "Total (s)", .value = reachedTimes().back()},
        {.name = "Distance run (m)", .value = distanceTravelled()},
        {.name = "Average speed (m/s)",
         .value = distanceTravelled() / reachedTimes().back()},
        {.name = "Gates missed", .value = static_cast<double>(missed)}};
    addBallMetrics(out);
    return out;
  }

 protected:
  /** His line round each cone still to pass, from the next point due (after
   * a point back behind the gate, when he is going back for it), less those
   * his body has moved on from. */
  [[nodiscard]] std::vector<PitchPoint> waypointsAhead() const override
  {
    std::vector<PitchPoint> list;
    const std::size_t due = reachedTimes().size();
    if (goingBack && backPending && due < conePoints.size())
    {
      list.push_back(backPoint(due));
    }
    list.insert(list.end(), aims.begin() + static_cast<std::ptrdiff_t>(nextAim),
                aims.end());
    const std::size_t skip = std::min(movedWaypointCount(), list.size());
    return {list.begin() + static_cast<std::ptrdiff_t>(skip), list.end()};
  }

  /** Through his point round each cone, the line pushed out no more than
   * SWING_ROOM_METRES beyond it to widen the turn. */
  [[nodiscard]] float apexOffset() const override { return 0.0F; }
  [[nodiscard]] float outwardRoom() const override { return SWING_ROOM_METRES; }

 private:
  /** Side of the course cone `index` is passed on (-1 or 1). */
  [[nodiscard]] static float sideOf(std::size_t index)
  {
    return index % 2 == 0 ? -1.0F : 1.0F;
  }

  /** Where he goes back to for a missed gate `cone`: halfway back along the
   * leg he ran in to it on (half a spacing behind its line), so he comes at
   * it again as the course does. */
  [[nodiscard]] PitchPoint backPoint(std::size_t cone) const
  {
    const PitchPoint before =
        cone == 0 ? courseStart() : coursePoints()[cone - 1];
    return PitchPoint{(before.x + coursePoints()[cone].x) * HALF_SHARE,
                      (before.y + coursePoints()[cone].y) * HALF_SHARE};
  }

  /** Whether `at` is behind gate `cone`'s line (the side he comes at it
   * from). */
  [[nodiscard]] bool behindGate(std::size_t cone, PitchPoint at) const
  {
    return at.x < conePoints[cone].x;
  }

  /** A point across gate `cone`, `metres` from its inner edge. */
  [[nodiscard]] PitchPoint gateEdge(std::size_t cone, float metres) const
  {
    return along(conePoints[cone], {.x = 0.0F, .y = 1.0F},
                 sideOf(cone) * (GATE_BUFFER_METRES + metres));
  }

  /** The course runs straight down the pitch: its finish is square to it. */
  [[nodiscard]] Vector2F finishDirection() const override
  {
    return {.x = 1.0F, .y = 0.0F};
  }

  /** Straight across the finish from his line round the last cone (the
   * line counts anywhere along it): level with that, within the line as
   * drawn. No curving back to its middle. */
  [[nodiscard]] PitchPoint finishAim() const override
  {
    if (coursePoints().size() < 2)
    {
      return coursePoints().back();
    }
    const float across = std::clamp(
        coursePoints()[coursePoints().size() - 2].y - coursePoints().back().y,
        -FINISH_AIM_METRES, FINISH_AIM_METRES);
    return along(coursePoints().back(), {.x = 0.0F, .y = 1.0F}, across);
  }
  /** How far either side of the finish line's middle he may aim (its drawn
   * half-width, 4 m, less a margin). */
  static constexpr float FINISH_AIM_METRES = 3.5F;

  /** How far in from each end of the pitch the course starts and finishes
   * (the run through the finish goes on past it). */
  static constexpr float END_ROOM_METRES = 5.0F;

  /** A gate starts at its cone's edge (a cone is 20 cm across). */
  static constexpr float GATE_BUFFER_METRES = DrillDraw::CONE_RADIUS_METRES;
  /** The least his line may pass outside a cone's edge: his centre a
   * body's half-width out (a body 40 cm across), so he clears it. */
  static constexpr float MIN_LINE_METRES = 0.2F;
  /** His line by the course (the setting at 0): LINE_STRAIGHT_METRES on a
   * straight line of cones, out to LINE_WEAVE_METRES as the weave sharpens
   * (stagger up to the spacing), since the ball cuts the corner a little
   * inside his line and has to go through the gate too. */
  [[nodiscard]] static float courseLine(float spacing, float stagger)
  {
    const float sharpness = std::clamp(
        stagger / std::max(spacing, MIN_COURSE_SPACING_METRES), 0.0F, 1.0F);
    return LINE_STRAIGHT_METRES +
           ((LINE_WEAVE_METRES - LINE_STRAIGHT_METRES) * sharpness);
  }
  static constexpr float LINE_STRAIGHT_METRES = 0.6F;
  static constexpr float LINE_WEAVE_METRES = 0.8F;
  /** How much of a gate is drawn (it is open beyond). */
  static constexpr float GATE_DRAWN_METRES = 2.0F;
  /** How far beyond his point round a cone his line may be pushed out to
   * widen the turn (the old 2 m gate's half-width). */
  static constexpr float SWING_ROOM_METRES = 1.0F;

  /** The point he aims for round each cone: his line round it (its point on
   * the gate), or, where the course turns back on itself there (a sharp
   * reversal: he stops on the point and turns), STOP_PAST_METRES on past
   * it the way he comes in, so his run in goes through the gate at his line
   * and he has been through it when he stops. */
  void planAims(PitchPoint start, const std::vector<PitchPoint>& course)
  {
    aims.clear();
    stops.clear();
    for (std::size_t cone = 0; cone < conePoints.size(); ++cone)
    {
      const PitchPoint at = course[cone];
      const PitchPoint before = cone == 0 ? start : course[cone - 1];
      const float reversalDegrees = MatchTuning::Player::SHARP_REVERSAL_DEGREES;
      const bool reversal =
          turnDegrees(directionBetween(before, at),
                      directionBetween(at, course[cone + 1])) >=
          reversalDegrees;
      aims.push_back(
          reversal ? along(at, directionBetween(before, at), STOP_PAST_METRES)
                   : at);
      stops.push_back(reversal);
    }
    nextAim = 0;
  }
  /** How far past the gate a sharp reversal's stop is. */
  static constexpr float STOP_PAST_METRES = 0.5F;

  /** Going back for a missed gate, he is done with the point behind it only
   * once he, and the ball, are behind the gate's line: moved on from it
   * before, it is his point again (he brings the ball back to it). */
  void holdBackPoint(MatchEngine& engine, const MatchPlayer& player)
  {
    const std::size_t due = reachedTimes().size();
    if (!goingBack || !backPending || movedWaypointCount() == 0 ||
        due >= conePoints.size())
    {
      return;
    }
    if (behindGate(due, player.position) &&
        (!runUsesBall() || behindGate(due, engine.getBall().position)))
    {
      return;
    }
    resetMovedWaypoints();
    planRun(engine);
  }

  /** Across the line of the cone due, forwards: on its side (passed), or
   * the wrong side (missed: he goes back for it). With the ball, the ball
   * has to go through it too: the gate is passed once both have, missed if
   * either goes the wrong side. Then the finish line. */
  void passConeGate(MatchEngine& engine, const MatchPlayer& player,
                    std::size_t due)
  {
    const PitchPoint cone = conePoints[due];
    // Across the line, forwards: whether on the gate's side, and when.
    const auto through =
        [&](PitchPoint from,
            PitchPoint to) -> std::optional<std::pair<bool, double>>
    {
      const auto share =
          crossingShare({.from = from, .to = to},
                        {.point = cone, .direction = {.x = 1.0F, .y = 0.0F}});
      if (!share)
      {
        return std::nullopt;
      }
      const float y = from.y + ((to.y - from.y) * *share);
      const float out = (y - cone.y) * sideOf(due);
      return std::pair{
          out >= GATE_BUFFER_METRES,
          secondsOf(engine) - STEP + (STEP * static_cast<double>(*share))};
    };
    const auto body = through(previous, player.position);
    const auto ball = runUsesBall()
                          ? through(previousBall, engine.getBall().position)
                          : std::nullopt;
    if (!body && !ball)
    {
      return;
    }
    if ((body && !body->first) || (ball && !ball->first))
    {
      ++missed;
      goingBack = true;
      backPending = true;
      nextAim = due;
      resetMovedWaypoints();
      bodyThrough.reset();
      ballThrough.reset();
      planRun(engine);
      return;
    }
    if (body)
    {
      bodyThrough = body->second;
    }
    if (ball)
    {
      ballThrough = ball->second;
    }
    if (!bodyThrough || (runUsesBall() && !ballThrough))
    {
      return;
    }
    recordCoursePoint(runUsesBall() ? std::max(*bodyThrough, *ballThrough)
                                    : *bodyThrough);
    // The points his body has moved on from are behind him, and so are
    // the point behind the gate (going back) and his point round it (a
    // stop past it is still ahead until he moves on from it).
    std::size_t skip = movedWaypointCount();
    if (skip > 0 && goingBack && backPending)
    {
      --skip;
    }
    nextAim = std::max(std::min(nextAim + skip, aims.size()),
                       stops[due] ? due : due + 1);
    resetMovedWaypoints();
    goingBack = false;
    backPending = false;
    bodyThrough.reset();
    ballThrough.reset();
    planRun(engine);
  }

  void passGates(MatchEngine& engine, const MatchPlayer& player)
  {
    const std::size_t due = reachedTimes().size();
    if (due < conePoints.size())
    {
      passConeGate(engine, player, due);
      return;
    }
    if (const auto share = crossingShare(
            {.from = previous, .to = player.position},
            {.point = coursePoints().back(), .direction = finishDirection()}))
    {
      recordCoursePoint(secondsOf(engine) - STEP +
                        (STEP * static_cast<double>(*share)));
    }
  }

  float cones = DEFAULT_CONES;
  float spacing = DEFAULT_SPACING;
  float lineOffset = 0.0F;
  float coneStagger = 0.0F;
  float markersChoice = 0.0F;

  float gap = DEFAULT_SPACING;
  float line = HALF_SHARE;
  /** The cones set, of which only as many as fit are laid out. */
  int conesSet = 6;
  float stagger = 0.0F;
  std::vector<PitchPoint> conePoints;
  int missed = 0;
  /** Going back for a missed gate (through the point behind it first). */
  bool goingBack = false;
  PitchPoint previous;
  PitchPoint previousBall;
  /** When he, and the ball, went through the gate due (each waits for the
   * other). */
  std::optional<double> bodyThrough;
  /** The point he aims for round each cone (see planAims), and the next
   * still to aim for. */
  std::vector<PitchPoint> aims;
  /** Whether each cone's point is a stop past its gate. */
  std::vector<bool> stops;
  std::size_t nextAim = 0;
  /** Going back, and not yet moved on from the point behind the gate. */
  bool backPending = false;
  std::optional<double> ballThrough;
};

// --- Repeated sprints ------------------------------------------------------

/**
 * Repeated sprints: shuttles between two lines with a set recovery between
 * them. The decline in time, the sprint reserve and condition at each start.
 */
class RepeatedSprintsDrill final : public MovementDrill
{
  static constexpr float REPS_MIN = 2.0F;
  static constexpr float REPS_MAX = 20.0F;
  static constexpr float DISTANCE_MIN = 10.0F;
  static constexpr float DISTANCE_MAX = 60.0F;
  static constexpr float RECOVERY_MIN = 3.0F;
  static constexpr float RECOVERY_MAX = 60.0F;
  static constexpr float DEFAULT_REPS = 6.0F;
  static constexpr float DEFAULT_DISTANCE = 30.0F;
  static constexpr float DEFAULT_RECOVERY = 20.0F;
  static constexpr float START_LENGTH_SHARE = 0.25F;
  static constexpr float TRACK_CENTRE_SHARE = 0.5F;
  static constexpr float TRACK_HALF_WIDTH_METRES = 4.0F;
  static constexpr float TRACK_LINE_WIDTH = 3.0F;
  static constexpr float REP_TIMEOUT_SECONDS = 30.0F;
  static constexpr float PERCENT_SCALE = 100.0F;
  static constexpr double PERCENT_SCALE_DOUBLE = 100.0;
  static constexpr double INITIAL_BEST_SECONDS = 1e9;
  static constexpr double FINISH_DELAY_SECONDS = 0.5;

 public:
  [[nodiscard]] const char* name() const override { return "Repeated sprints"; }
  [[nodiscard]] const char* summary() const override
  {
    return "Sprints between two lines, one way then back, with a set recovery "
           "(walking back to the line) between them. Each rep is timed from "
           "the start signal; the sprint reserve and condition are read at "
           "each start.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(
        setting("Reps", &reps, {.min = REPS_MIN, .max = REPS_MAX}, true));
    list.push_back(setting("Distance (m)", &distance,
                           {.min = DISTANCE_MIN, .max = DISTANCE_MAX}, true));
    list.push_back(setting("Recovery (s)", &recovery,
                           {.min = RECOVERY_MIN, .max = RECOVERY_MAX}, true,
                           "From crossing the line to the next start signal."));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    repCount = static_cast<int>(reps);
    metres = distance;
    rest = recovery;
    lines = {at(START_LENGTH_SHARE, TRACK_CENTRE_SHARE),
             along(at(START_LENGTH_SHARE, TRACK_CENTRE_SHARE),
                   {.x = 1.0F, .y = 0.0F}, metres)};
    startRunner(engine, ids, lines.at(0));
    done.clear();
    signal = 0.0;
    crossedAt.reset();
    startRep(engine);
    previous = lines.at(0);
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (player == nullptr)
    {
      return false;
    }
    const double now = secondsOf(engine);
    if (!crossedAt)
    {
      current.topSpeed = std::max(current.topSpeed, speedOf(*player));
      const PitchPoint to = lines.at((done.size() + 1) % 2);
      const PitchPoint from = lines.at(done.size() % 2);
      if (const auto share = crossingShare(
              {.from = previous, .to = player->position},
              {.point = to, .direction = directionBetween(from, to)}))
      {
        crossedAt = now - STEP + (STEP * static_cast<double>(*share));
        current.seconds = *crossedAt - signal;
        done.push_back(current);
        // Recovery: ease back onto the line just crossed.
        steerTo(engine, to, false);
      }
    }
    else if (static_cast<int>(done.size()) < repCount &&
             now >= *crossedAt + rest)
    {
      startRep(engine);
    }
    previous = player->position;
    const bool over = static_cast<int>(done.size()) >= repCount && crossedAt &&
                      now > *crossedAt + FINISH_DELAY_SECONDS;
    return !over &&
           now < static_cast<double>(repCount) * (rest + REP_TIMEOUT_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    const float half = TRACK_HALF_WIDTH_METRES;
    const float middle = PITCH.atY(TRACK_CENTRE_SHARE);
    for (std::size_t index = 0; index < 2; ++index)
    {
      const ImU32 colour = (done.size() + 1) % 2 == index && !crossedAt
                               ? COURSE_FINISH_COLOUR
                               : INACTIVE_TRACK_COLOUR;
      DrillDraw::line(renderer, draw,
                      PitchPoint{lines.at(index).x, middle - half},
                      PitchPoint{lines.at(index).x, middle + half}, colour,
                      TRACK_LINE_WIDTH * scale);
    }
    drawTrail(renderer, draw);
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> out;
    out.push_back(std::format("{} × {:.0f} m, {:.0f} s recovery", repCount,
                              metres, rest));
    out.push_back(runnerLine());
    double best = INITIAL_BEST_SECONDS;
    double worst = 0.0;
    double total = 0.0;
    for (std::size_t index = 0; index < done.size(); ++index)
    {
      const Rep& rep = done[index];
      out.push_back(std::format(
          "{:>2}. {:.2f} s · top {:.2f} m/s · reserve {:.0f}% · "
          "condition {:.0f}%",
          index + 1, rep.seconds, rep.topSpeed, rep.reserve * PERCENT_SCALE,
          rep.condition * PERCENT_SCALE));
      best = std::min(best, rep.seconds);
      worst = std::max(worst, rep.seconds);
      total += rep.seconds;
    }
    if (!crossedAt && static_cast<int>(done.size()) < repCount && sentSignal)
    {
      out.push_back(std::format("{:>2}. running…", done.size() + 1));
    }
    if (done.size() >= 2)
    {
      const double mean = total / static_cast<double>(done.size());
      out.push_back(std::format(
          "Best {:.2f} s · worst {:.2f} s · mean {:.2f} s", best, worst, mean));
      out.push_back(
          std::format("Decrement {:.1f}% (mean vs best) · last vs first "
                      "{:+.2f} s",
                      (mean / best - 1.0) * PERCENT_SCALE_DOUBLE,
                      done.back().seconds - done.front().seconds));
    }
    addBallLines(out);
    return out;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (static_cast<int>(done.size()) < repCount || done.empty())
    {
      return {};
    }
    double best = INITIAL_BEST_SECONDS;
    double worst = 0.0;
    double total = 0.0;
    for (const Rep& rep : done)
    {
      best = std::min(best, rep.seconds);
      worst = std::max(worst, rep.seconds);
      total += rep.seconds;
    }
    const double mean = total / static_cast<double>(done.size());
    std::vector<DrillMetric> out{
        {.name = "Mean (s)", .value = mean},
        {.name = "Best (s)", .value = best},
        {.name = "Worst (s)", .value = worst},
        {.name = "Decrement (%)",
         .value = (mean / best - 1.0) * PERCENT_SCALE_DOUBLE},
        {.name = "Last - first (s)",
         .value = done.back().seconds - done.front().seconds},
        {.name = "Reserve at last start (%)",
         .value = done.back().reserve * PERCENT_SCALE_DOUBLE},
        {.name = "Condition at last start (%)",
         .value = done.back().condition * PERCENT_SCALE_DOUBLE}};
    addBallMetrics(out);
    return out;
  }

 private:
  struct Rep
  {
    double seconds = 0.0;
    float topSpeed = 0.0F;
    float reserve = 1.0F;
    float condition = 1.0F;
  };

  void startRep(MatchEngine& engine)
  {
    const MatchPlayer* player = slotOf(engine, runnerId());
    const PitchPoint from = lines.at(done.size() % 2);
    const PitchPoint to = lines.at((done.size() + 1) % 2);
    current = Rep{};
    if (player != nullptr)
    {
      current.reserve = player->sprintReserve;
      current.condition = player->stamina;
    }
    signal = secondsOf(engine);
    crossedAt.reset();
    sentSignal = true;
    steerTo(engine, along(to, directionBetween(from, to), RUN_THROUGH_METRES),
            true, {}, 0.0F, 0.0F, std::nullopt, true);
  }

  float reps = DEFAULT_REPS;
  float distance = DEFAULT_DISTANCE;
  float recovery = DEFAULT_RECOVERY;

  int repCount = 6;
  float metres = DEFAULT_DISTANCE;
  float rest = DEFAULT_RECOVERY;
  std::array<PitchPoint, 2> lines{};
  std::vector<Rep> done;
  Rep current;
  bool sentSignal = false;
  double signal = 0.0;
  std::optional<double> crossedAt;
  PitchPoint previous;
};

// --- Flag chase ------------------------------------------------------------

/**
 * Flag chase: two flags are always out, the one to run to and the one
 * after; reaching the first places a new one, from the drill's seed, so the
 * same seed is the same course for every runner.
 */
class FlagChaseDrill final : public CourseDrill
{
  static constexpr std::uint32_t COURSE_SEED_SALT = 0x5eed'f1a6U;
  static constexpr int INITIAL_FLAG_COUNT = 12;
  static constexpr float ARRIVAL_RADIUS_MIN = 0.3F;
  static constexpr float FLAGS_MIN = 2.0F;
  static constexpr float FLAGS_MAX = 50.0F;
  static constexpr float AREA_LENGTH_MIN = 20.0F;
  static constexpr float AREA_LENGTH_MAX = 100.0F;
  static constexpr float AREA_WIDTH_MIN = 10.0F;
  static constexpr float AREA_WIDTH_MAX = 64.0F;
  static constexpr float SHORTEST_LEG_MIN = 3.0F;
  static constexpr float SHORTEST_LEG_MAX = 40.0F;
  static constexpr float LONGEST_LEG_MIN = 3.0F;
  static constexpr float LONGEST_LEG_MAX = 60.0F;
  static constexpr float LEAST_TURN_MAX = 180.0F;
  static constexpr float MOST_TURN_MAX = 180.0F;
  static constexpr float ARRIVAL_RADIUS_MAX = 3.0F;
  static constexpr float DEFAULT_FLAGS = 12.0F;
  static constexpr float DEFAULT_AREA_LENGTH = 50.0F;
  static constexpr float DEFAULT_AREA_WIDTH = 40.0F;
  static constexpr float DEFAULT_LEG_MIN = 8.0F;
  static constexpr float DEFAULT_LEG_MAX = 20.0F;
  static constexpr float DEFAULT_TURN_MAX = 180.0F;
  static constexpr float DEFAULT_HALF_LENGTH = 25.0F;
  static constexpr float DEFAULT_HALF_WIDTH = 20.0F;
  static constexpr float HALF_SHARE = 0.5F;
  static constexpr float HALF_TURN_DEGREES = 180.0F;
  static constexpr float TURN_BUCKET_DEGREES = 45.0F;
  static constexpr int TURN_BUCKET_LABEL_DEGREES = 45;
  static constexpr int MAX_FLAG_ATTEMPTS = 400;
  static constexpr int RELAX_TURN_AFTER_ATTEMPTS = 300;
  static constexpr float PASSED_FLAG_RADIUS = 2.5F;
  static constexpr float UPCOMING_FLAG_RADIUS = 5.0F;
  static constexpr float CURRENT_FLAG_RADIUS = 7.0F;
  static constexpr double FINISH_DELAY_SECONDS = 0.5;
  static constexpr double MAX_DURATION_SECONDS = 600.0;

 public:
  FlagChaseDrill() { stopAtFinish(); }

  [[nodiscard]] const char* name() const override { return "Flag chase"; }
  [[nodiscard]] const char* summary() const override
  {
    return "Run to the flag, then the next: two flags are always out, and a "
           "new one is placed (from the seed) when one is reached. Times by "
           "leg against its length and the turn into it. Same seed, same "
           "course: race the AI and yourself.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(
        setting("Flags", &flags, {.min = FLAGS_MIN, .max = FLAGS_MAX}, true));
    list.push_back(
        setting("Area length (m)", &areaLength,
                {.min = AREA_LENGTH_MIN, .max = AREA_LENGTH_MAX}, true,
                "The cordon flags are placed in, round the centre spot."));
    list.push_back(setting("Area width (m)", &areaWidth,
                           {.min = AREA_WIDTH_MIN, .max = AREA_WIDTH_MAX},
                           true));
    list.push_back(setting("Shortest leg (m)", &legMin,
                           {.min = SHORTEST_LEG_MIN, .max = SHORTEST_LEG_MAX},
                           true));
    list.push_back(setting("Longest leg (m)", &legMax,
                           {.min = LONGEST_LEG_MIN, .max = LONGEST_LEG_MAX},
                           true));
    list.push_back(setting(
        "Least turn (°)", &turnMin, {.min = 0.0F, .max = LEAST_TURN_MAX}, true,
        "Turn from one leg into the next: 0 straight on, 180 "
        "straight back."));
    list.push_back(setting("Most turn (°)", &turnMax,
                           {.min = 0.0F, .max = MOST_TURN_MAX}, true));
    list.push_back(courseArrivalParameter(
        {.min = ARRIVAL_RADIUS_MIN, .max = ARRIVAL_RADIUS_MAX}));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    flagCount = static_cast<int>(flags);
    halfLength = areaLength * HALF_SHARE;
    halfWidth = areaWidth * HALF_SHARE;
    shortest = std::min(legMin, legMax);
    longest = std::max(legMin, legMax);
    leastTurn = std::min(turnMin, turnMax);
    mostTurn = std::max(turnMin, turnMax);
    random.seed(seed ^ COURSE_SEED_SALT);
    const PitchPoint start = at(HALF_SHARE, HALF_SHARE);
    heading = {.x = 1.0F, .y = 0.0F};
    last = start;
    turns.clear();
    std::vector<PitchPoint> course;
    for (int flag = 0; flag < std::min(2, flagCount); ++flag)
    {
      course.push_back(nextFlag());
    }
    beginCourse(engine, ids, start, std::move(course), false);
    PitchKit::Layout kit = engine.getKit();
    const PitchPoint centre = PITCH.at(HALF_SHARE, HALF_SHARE);
    kit.areas.push_back(
        {.name = "Area",
         .min = PitchPoint{centre.x - halfLength, centre.y - halfWidth},
         .max = PitchPoint{centre.x + halfLength, centre.y + halfWidth},
         .colour = FLAG_AREA_COLOUR});
    engine.setKit(std::move(kit));
    previous = start;
    resultSaved = false;
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (player == nullptr)
    {
      return false;
    }
    if (advanceCourse(engine, *player, previous) &&
        static_cast<int>(coursePoints().size()) < flagCount)
    {
      // Always two out: place the one after next.
      appendCoursePoint(nextFlag());
      planRun(engine);
    }
    previous = player->position;
    const double now = secondsOf(engine);
    if (courseFinished() && !resultSaved)
    {
      double& best = bests[courseKey()].at(runnerPath());
      if (best == 0.0 || reachedTimes().back() < best)
      {
        best = reachedTimes().back();
      }
      resultSaved = true;
    }
    return (!courseFinished() ||
            now <= reachedTimes().back() + FINISH_DELAY_SECONDS) &&
           now < MAX_DURATION_SECONDS;
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    // The area is kit on the pitch (begin).
    drawTrail(renderer, draw);
    for (std::size_t index = 0; index < reachedTimes().size(); ++index)
    {
      DrillDraw::marker(renderer, draw, coursePoints()[index],
                        PASSED_FLAG_RADIUS * scale, PASSED_FLAG_COLOUR);
    }
    const std::size_t next = reachedTimes().size();
    if (next + 1 < coursePoints().size())
    {
      DrillDraw::line(renderer, draw, coursePoints()[next],
                      coursePoints()[next + 1], FLAG_ROUTE_COLOUR,
                      1.0F * scale);
      DrillDraw::marker(renderer, draw, coursePoints()[next + 1],
                        UPCOMING_FLAG_RADIUS * scale, UPCOMING_FLAG_COLOUR,
                        std::format("{}", next + 2).c_str());
    }
    if (next < coursePoints().size())
    {
      DrillDraw::marker(renderer, draw, coursePoints()[next],
                        CURRENT_FLAG_RADIUS * scale, CURRENT_FLAG_COLOUR,
                        std::format("{}", next + 1).c_str());
    }
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(
        std::format("{} flags in {:.0f} × {:.0f} m, legs {:.0f}-{:.0f} m, "
                    "turns {:.0f}-{:.0f}°",
                    flagCount, halfLength * HALF_TO_FULL_SCALE,
                    halfWidth * HALF_TO_FULL_SCALE, shortest, longest,
                    leastTurn, mostTurn));
    lines.push_back(runnerLine());
    lines.push_back(
        std::format("Flags {} / {}", reachedTimes().size(), flagCount));
    // The latest legs.
    const std::size_t from =
        reachedTimes().size() > 5 ? reachedTimes().size() - 5 : 0;
    for (std::size_t index = from; index < reachedTimes().size(); ++index)
    {
      lines.push_back(
          std::format("  leg {:>2}: {:.1f} m, turn {:>3.0f}° · {:.2f} s "
                      "({:.2f} m/s)",
                      index + 1, legMetres(index), turns[index],
                      legSeconds(index), legMetres(index) / legSeconds(index)));
    }
    if (courseFinished())
    {
      lines.push_back(std::format(
          "TOTAL  {:.2f} s · course {:.0f} m, ran {:.0f} m",
          reachedTimes().back(), courseMetres(), distanceTravelled()));
    }
    // Cornering: average speed of legs by the turn into them (the first leg,
    // from standing, left out).
    std::array<double, 4> metres{};
    std::array<double, 4> seconds{};
    std::array<int, 4> count{};
    for (std::size_t index = 1; index < reachedTimes().size(); ++index)
    {
      const auto bucket = static_cast<std::size_t>(std::clamp(
          static_cast<int>(turns[index] / TURN_BUCKET_DEGREES), 0, 3));
      metres.at(bucket) += legMetres(index);
      seconds.at(bucket) += legSeconds(index);
      ++count.at(bucket);
    }
    std::string cornering;
    for (std::size_t bucket = 0; bucket < 4; ++bucket)
    {
      if (count.at(bucket) > 0)
      {
        cornering += std::format(
            "{}-{}°: {:.2f} m/s ({})  ", bucket * TURN_BUCKET_LABEL_DEGREES,
            (bucket * TURN_BUCKET_LABEL_DEGREES) + TURN_BUCKET_LABEL_DEGREES,
            metres.at(bucket) / seconds.at(bucket), count.at(bucket));
      }
    }
    if (!cornering.empty())
    {
      lines.emplace_back("Average leg speed by turn into it:");
      lines.push_back("  " + cornering);
    }
    // This course (seed and settings) by who ran it.
    const auto best = bests.find(courseKey());
    if (best != bests.end())
    {
      std::string board = "This course, best: ";
      for (std::size_t who = 0; who < PATH_NAMES.size(); ++who)
      {
        board += std::format("{} {}   ", PATH_NAMES.at(who),
                             best->second.at(who) > 0.0
                                 ? std::format("{:.2f} s", best->second.at(who))
                                 : std::string("-"));
      }
      lines.push_back(board);
    }
    addBallLines(lines);
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (!courseFinished() || reachedTimes().empty())
    {
      return {};
    }
    const double total = reachedTimes().back();
    std::vector<DrillMetric> out{
        {.name = "Total (s)", .value = total},
        {.name = "Distance run (m)", .value = distanceTravelled()},
        {.name = "Average speed (m/s)", .value = distanceTravelled() / total}};
    // Average leg speed by the turn into the leg (the first leg left out).
    std::array<double, 4> metres{};
    std::array<double, 4> seconds{};
    for (std::size_t index = 1; index < reachedTimes().size(); ++index)
    {
      const auto bucket = static_cast<std::size_t>(std::clamp(
          static_cast<int>(turns[index] / TURN_BUCKET_DEGREES), 0, 3));
      metres.at(bucket) += legMetres(index);
      seconds.at(bucket) += legSeconds(index);
    }
    for (std::size_t bucket = 0; bucket < 4; ++bucket)
    {
      if (seconds.at(bucket) > 0.0)
      {
        out.push_back(
            {.name = std::format("Leg speed {}-{}° (m/s)",
                                 bucket * TURN_BUCKET_LABEL_DEGREES,
                                 (bucket * TURN_BUCKET_LABEL_DEGREES) +
                                     TURN_BUCKET_LABEL_DEGREES),
             .value = metres.at(bucket) / seconds.at(bucket)});
      }
    }
    addBallMetrics(out);
    return out;
  }

 private:
  /** The next flag after `last`, turning off `heading` within the limits. */
  PitchPoint nextFlag()
  {
    std::uniform_real_distribution<float> unit(0.0F, 1.0F);
    const auto inside = [&](Vector2F metres)
    {
      constexpr float MARGIN = 1.0F;
      return std::abs(metres.x - (LENGTH * HALF_SHARE)) <=
                 halfLength - MARGIN &&
             std::abs(metres.y - (WIDTH * HALF_SHARE)) <= halfWidth - MARGIN;
    };
    const Vector2F from = asVector(last);
    std::optional<Vector2F> found;
    float turn = 0.0F;
    // Within the turn limits if it can be, any direction if not.
    for (int attempt = 0; attempt < MAX_FLAG_ATTEMPTS && !found; ++attempt)
    {
      const bool anyTurn = attempt >= RELAX_TURN_AFTER_ATTEMPTS;
      const float length = shortest + ((longest - shortest) * unit(random));
      const float degrees =
          anyTurn ? HALF_TURN_DEGREES * unit(random)
                  : leastTurn + ((mostTurn - leastTurn) * unit(random));
      const float sign = unit(random) < HALF_SHARE ? -1.0F : 1.0F;
      const Vector2F direction =
          rotated(heading, sign * degrees * PI / HALF_TURN_DEGREES);
      const Vector2F candidate{.x = from.x + (direction.x * length),
                               .y = from.y + (direction.y * length)};
      if (!inside(candidate))
      {
        continue;
      }
      found = candidate;
      turn = degrees;
    }
    if (!found)
    {
      // An area too small for the legs: its centre.
      found = Vector2F{.x = LENGTH * HALF_SHARE, .y = WIDTH * HALF_SHARE};
      turn = turnDegrees(heading, directionBetween(last, asPoint(*found)));
    }
    const PitchPoint flag = asPoint(*found);
    heading = directionBetween(last, flag);
    last = flag;
    turns.push_back(turn);
    return flag;
  }

  [[nodiscard]] std::string courseKey() const
  {
    return std::format(
        "{}|{}|{:.0f}|{:.0f}|{:.0f}|{:.0f}|{:.0f}|{:.0f}|{:.2f}|{}", seed,
        flagCount, halfLength, halfWidth, shortest, longest, leastTurn,
        mostTurn, courseRadius(), runnerKey());
  }

  float flags = DEFAULT_FLAGS;
  float areaLength = DEFAULT_AREA_LENGTH;
  float areaWidth = DEFAULT_AREA_WIDTH;
  float legMin = DEFAULT_LEG_MIN;
  float legMax = DEFAULT_LEG_MAX;
  float turnMin = 0.0F;
  float turnMax = DEFAULT_TURN_MAX;

  int flagCount = INITIAL_FLAG_COUNT;
  float halfLength = DEFAULT_HALF_LENGTH;
  float halfWidth = DEFAULT_HALF_WIDTH;
  float shortest = DEFAULT_LEG_MIN;
  float longest = DEFAULT_LEG_MAX;
  float leastTurn = 0.0F;
  float mostTurn = DEFAULT_TURN_MAX;
  std::mt19937 random;
  Vector2F heading{.x = 1.0F, .y = 0.0F};
  PitchPoint last;
  /** Turn into each leg (degrees), by flag. */
  std::vector<float> turns;
  PitchPoint previous;
  /** Best total of this course by who ran it. */
  std::map<std::string, std::array<double, 2>> bests;
  bool resultSaved = false;
};

// --- Turn and run
// --------------------------------------------------------------

/**
 * Turn and run: one runner goes down a straight track moving one way (a
 * backpedal, a side-step, or forward), and at a line changes how he moves:
 * a transition on the move (orientation plan). The second leg either goes
 * on down the track (his body turns to the new way of moving), or heads off
 * from how his body faces at the line (forward the way he faces, back, or
 * to his left or right: his travel changes, his body does not). The speed
 * he takes into it, the least he keeps through it, how long and how far
 * until he is settled into the new way, and the times. Turning like this is
 * the Binturong body's.
 */
class TurnAndRunDrill final : public MovementDrill
{
  static constexpr float FIRST_CHOICE_MAX = 7.0F;
  static constexpr float SWITCH_SETTING_MIN = 2.0F;
  static constexpr float SWITCH_SETTING_MAX = 40.0F;
  static constexpr float THEN_CHOICE_MAX = 7.0F;
  static constexpr float SECOND_SETTING_MIN = 2.0F;
  static constexpr float SECOND_SETTING_MAX = 60.0F;
  static constexpr float DEFAULT_SWITCH_SETTING = 10.0F;
  static constexpr float DEFAULT_SECOND_SETTING = 20.0F;
  static constexpr float TRACK_HALF_WIDTH_METRES = 4.0F;
  static constexpr float SWITCH_LABEL_OFFSET_METRES = 6.0F;
  static constexpr float QUARTER_PI = 0.25F;
  static constexpr float THREE_QUARTER_PI = 0.75F;
  static constexpr double MAX_DURATION_SECONDS = 60.0;
  static constexpr double FINISH_DELAY_SECONDS = 0.5;

 public:
  [[nodiscard]] const char* name() const override { return "Turn and run"; }
  [[nodiscard]] const char* summary() const override
  {
    return "Down a straight track moving one way (backpedal, side-step or "
           "forward), then at a line a change: on down the track moving "
           "another way (his body turns), or off from how he faces (forward, "
           "back, or to his left or right). Speed into it, the slowest "
           "through it, how long and how far until settled, and the times.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    DrillParameter first = setting("First", &firstChoice,
                                   {.min = 0.0F, .max = FIRST_CHOICE_MAX}, true,
                                   "How he moves down the track on the first "
                                   "leg.");
    first.choices = {"Forward",
                     "Backward",
                     "Side-step left",
                     "Side-step right",
                     "Diagonal forward-left",
                     "Diagonal forward-right",
                     "Diagonal back-left",
                     "Diagonal back-right"};
    list.push_back(first);
    list.push_back(
        setting("First leg (m)", &switchSetting,
                {.min = SWITCH_SETTING_MIN, .max = SWITCH_SETTING_MAX}, true,
                "From the start to the line where he changes."));
    DrillParameter then =
        setting("Then", &thenChoice, {.min = 0.0F, .max = THEN_CHOICE_MAX},
                true, "How he moves after the line.");
    then.choices = {"Forward",
                    "Backward",
                    "Side-step left",
                    "Side-step right",
                    "Diagonal forward-left",
                    "Diagonal forward-right",
                    "Diagonal back-left",
                    "Diagonal back-right"};
    list.push_back(then);
    DrillParameter heads = setting("Second leg heads", &headsChoice,
                                   {.min = 0.0F, .max = 1.0F}, true,
                                   "On down the track: the same way, his body "
                                   "turning to the new way of moving. From his "
                                   "facing: off from how his body faces at the "
                                   "line (Forward: the way he faces), his body "
                                   "staying as it is.");
    heads.choices = {"On down the track", "From his facing"};
    list.push_back(heads);
    list.push_back(
        setting("Second leg (m)", &secondSetting,
                {.min = SECOND_SETTING_MIN, .max = SECOND_SETTING_MAX}, true,
                "From the line to the finish."));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    first = static_cast<int>(std::round(firstChoice));
    then = static_cast<int>(std::round(thenChoice));
    fromFacing = headsChoice >= CHOICE_MIDPOINT;
    switchMetres = switchSetting;
    secondMetres = secondSetting;
    // Down the track (+x) on the first leg.
    const PitchPoint start = at(START_LINE, PITCH_MIDPOINT_SHARE);
    linePoint = along(start, {.x = 1.0F, .y = 0.0F}, switchMetres);
    firstBody = bodyFor(first, 0.0F);
    // The second leg: on down the track, his body turning to the new way;
    // or off from his facing, his body staying.
    secondTravel = fromFacing ? travelFor(then, firstBody) : 0.0F;
    secondBody = fromFacing ? firstBody : bodyFor(then, 0.0F);
    secondDirection = {.x = std::cos(secondTravel),
                       .y = std::sin(secondTravel)};
    finishPoint = along(linePoint, secondDirection, secondMetres);
    startRunner(engine, ids, start);
    steerTo(engine,
            along(linePoint, {.x = 1.0F, .y = 0.0F}, RUN_THROUGH_METRES), true,
            {}, 0.0F, 0.0F, facingAngleOf(firstBody), true);
    previous = start;
    switchedAt.reset();
    switchSpeed = 0.0F;
    slowest.reset();
    settledAt.reset();
    settleMetres = 0.0F;
    finishTime.reset();
    // The start line and the line where he switches, laid on the pitch.
    PitchKit::Layout kit;
    const Vector2F side{.x = 0.0F, .y = 1.0F};
    const PitchPoint startLine = at(START_LINE, PITCH_MIDPOINT_SHARE);
    kit.lines.push_back(
        {.name = "Start",
         .from = along(startLine, side, -TRACK_HALF_WIDTH_METRES),
         .to = along(startLine, side, TRACK_HALF_WIDTH_METRES),
         .colour = COURSE_START_COLOUR});
    kit.lines.push_back(
        {.name = "Switch",
         .from = along(linePoint, side, -TRACK_HALF_WIDTH_METRES),
         .to = along(linePoint, side, TRACK_HALF_WIDTH_METRES),
         .colour = SWITCH_LINE_COLOUR});
    const Vector2F finishSide = rotated(secondDirection, 0.5F * PI);
    kit.lines.push_back(
        {.name = "Finish",
         .from = along(finishPoint, finishSide, -TRACK_HALF_WIDTH_METRES),
         .to = along(finishPoint, finishSide, TRACK_HALF_WIDTH_METRES),
         .colour = COURSE_FINISH_COLOUR});
    engine.setKit(std::move(kit));
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (player == nullptr)
    {
      return false;
    }
    const double now = secondsOf(engine);
    const float speed = speedOf(*player);
    const auto at = [&](std::optional<float> share)
    { return now - STEP + (STEP * static_cast<double>(*share)); };
    if (!switchedAt)
    {
      if (const auto share = crossingShare(
              {.from = previous, .to = player->position},
              {.point = linePoint, .direction = {.x = 1.0F, .y = 0.0F}}))
      {
        // The line: the new way of moving.
        switchedAt = at(share);
        switchSpeed = speed;
        switchFrom = player->position;
        steerTo(engine, along(finishPoint, secondDirection, RUN_THROUGH_METRES),
                true, {}, 0.0F, 0.0F, facingAngleOf(secondBody), true);
      }
    }
    else if (!settledAt)
    {
      // Until settled: his body within a few degrees of the new way, and
      // his run along the second leg.
      if (!slowest || speed < *slowest)
      {
        slowest = speed;
      }
      const float bodyOff = std::remainder(
          facingAngleOf(secondBody) - player->facingAngle, FULL_TURN_RADIANS);
      const float runLength =
          std::hypot(player->velocity.x, player->velocity.y);
      const float along = runLength > 0.5F
                              ? (player->velocity.x * secondDirection.x +
                                 player->velocity.y * secondDirection.y) /
                                    runLength
                              : 0.0F;
      if (std::abs(bodyOff) < SETTLED_RADIANS && along > SETTLED_COSINE)
      {
        settledAt = now;
        settleMetres = metresBetween(switchFrom, player->position);
      }
    }
    if (switchedAt && !finishTime)
    {
      if (const auto share = crossingShare(
              {.from = previous, .to = player->position},
              {.point = finishPoint, .direction = secondDirection}))
      {
        finishTime = at(share);
      }
    }
    previous = player->position;
    return (!finishTime || now <= *finishTime + FINISH_DELAY_SECONDS) &&
           now < MAX_DURATION_SECONDS;
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    // The lines are kit on the pitch (begin); the label stays here.
    DrillDraw::label(
        renderer, draw,
        along(linePoint, {.x = 0.0F, .y = 1.0F}, -SWITCH_LABEL_OFFSET_METRES),
        std::format("then: {}", NAMES.at(static_cast<std::size_t>(then)))
            .c_str(),
        SWITCH_LINE_COLOUR);
    drawTrail(renderer, draw);
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(
        std::format("{} for {:.0f} m, then {} for {:.0f} m{}",
                    NAMES.at(static_cast<std::size_t>(first)), switchMetres,
                    NAMES.at(static_cast<std::size_t>(then)), secondMetres,
                    fromFacing ? " (from his facing)" : ""));
    lines.push_back(runnerLine());
    if (switchedAt)
    {
      lines.push_back(std::format("At the line {:.2f} s, at {:.2f} m/s",
                                  *switchedAt, switchSpeed));
    }
    if (settledAt && switchedAt)
    {
      lines.push_back(std::format(
          "Settled {:.2f} s after, over {:.1f} m · slowest {:.2f} m/s",
          *settledAt - *switchedAt, settleMetres,
          slowest.value_or(switchSpeed)));
    }
    if (finishTime)
    {
      lines.push_back(std::format("FINISH  {:.2f} s", *finishTime));
    }
    addBallLines(lines);
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (!finishTime || !switchedAt || !settledAt)
    {
      return {};
    }
    std::vector<DrillMetric> out{
        {.name = "Finish (s)", .value = *finishTime},
        {.name = "At the line (s)", .value = *switchedAt},
        {.name = "Speed at the line (m/s)", .value = switchSpeed},
        {.name = "Settled after (s)", .value = *settledAt - *switchedAt},
        {.name = "Settled after (m)", .value = settleMetres},
        {.name = "Slowest in the change (m/s)",
         .value = slowest.value_or(switchSpeed)}};
    addBallMetrics(out);
    return out;
  }

 private:
  static constexpr float START_LINE = 0.25F;
  /** Settled: his body within this of the new way, and his run within
   * this (as a cosine, ~10 deg) of the second leg. */
  static constexpr float SETTLED_RADIANS = 0.087F;
  static constexpr float SETTLED_COSINE = 0.985F;
  static constexpr std::array<const char*, 8> NAMES{"forward",
                                                    "backpedal",
                                                    "side-step left",
                                                    "side-step right",
                                                    "diagonal forward-left",
                                                    "diagonal forward-right",
                                                    "diagonal back-left",
                                                    "diagonal back-right"};

  // Ways of moving against his body, as real directions on the pitch
  // (radians, in metres; facingAngleOf() converts a body's to a facing).
  // Side-stepping left, he moves to his own left: a quarter turn clockwise
  // from the way he faces here.
  /** His body for moving `way` along a run at `travel`. */
  static float bodyFor(int way, float travel)
  {
    constexpr std::array<float, 8> OFFSETS{0.0F,
                                           PI,
                                           0.5F * PI,
                                           -0.5F * PI,
                                           QUARTER_PI * PI,
                                           -QUARTER_PI * PI,
                                           THREE_QUARTER_PI * PI,
                                           -THREE_QUARTER_PI * PI};
    return std::remainder(travel + OFFSETS.at(static_cast<std::size_t>(way)),
                          FULL_TURN_RADIANS);
  }
  /** His run for moving `way` with his body at `body`. */
  static float travelFor(int way, float body)
  {
    constexpr std::array<float, 8> OFFSETS{0.0F,
                                           PI,
                                           -0.5F * PI,
                                           0.5F * PI,
                                           -QUARTER_PI * PI,
                                           QUARTER_PI * PI,
                                           -THREE_QUARTER_PI * PI,
                                           THREE_QUARTER_PI * PI};
    return std::remainder(body + OFFSETS.at(static_cast<std::size_t>(way)),
                          FULL_TURN_RADIANS);
  }

  float firstChoice = 1.0F;
  float switchSetting = DEFAULT_SWITCH_SETTING;
  float thenChoice = 0.0F;
  float headsChoice = 0.0F;
  float secondSetting = DEFAULT_SECOND_SETTING;

  int first = 1;
  int then = 0;
  bool fromFacing = false;
  float switchMetres = DEFAULT_SWITCH_SETTING;
  float secondMetres = DEFAULT_SECOND_SETTING;
  PitchPoint linePoint;
  PitchPoint finishPoint;
  float firstBody = 0.0F;
  float secondBody = 0.0F;
  float secondTravel = 0.0F;
  Vector2F secondDirection{.x = 1.0F, .y = 0.0F};
  PitchPoint previous;
  PitchPoint switchFrom;
  std::optional<double> switchedAt;
  float switchSpeed = 0.0F;
  std::optional<float> slowest;
  std::optional<double> settledAt;
  float settleMetres = 0.0F;
  std::optional<double> finishTime;
};

// --- Overtake --------------------------------------------------------------

/**
 * Overtake: two (or three) players race flat out for the same finish. The
 * leader starts ahead but is slower; the chaser behind should go round him
 * if he can still get there first (movement targets plan 4a). A third
 * runner alongside the leader can block one side. Going round is the
 * Binturong brain's and body's.
 */
class OvertakeDrill final : public Drill
{
  [[nodiscard]] const char* thirdRunnerDescription() const
  {
    if (third == 1)
    {
      return ", a third runner on his left";
    }
    if (third == 2)
    {
      return ", a third runner on his right";
    }
    return "";
  }

  static constexpr ImU32 RUNNER_TRAIL_COLOUR = IM_COL32(120, 200, 255, 200);
  static constexpr float GAP_SETTING_MAX = 20.0F;
  static constexpr float FINISH_SETTING_MIN = 10.0F;
  static constexpr float FINISH_SETTING_MAX = 80.0F;
  static constexpr float OFFSET_SETTING_MIN = -3.0F;
  static constexpr float OFFSET_SETTING_MAX = 3.0F;
  static constexpr float LEADER_PACE_MAX = 100.0F;
  static constexpr float CHASER_PACE_MAX = 100.0F;
  static constexpr float THIRD_CHOICE_MAX = 2.0F;
  static constexpr float DEFAULT_GAP_SETTING = 6.0F;
  static constexpr float DEFAULT_FINISH_SETTING = 40.0F;
  static constexpr float DEFAULT_LEADER_PACE = 40.0F;
  static constexpr float DEFAULT_CHASER_PACE = 90.0F;
  static constexpr float START_LENGTH_SHARE = 0.25F;
  static constexpr float TRACK_CENTRE_SHARE = 0.5F;
  static constexpr float FINISH_HALF_WIDTH_METRES = 6.0F;
  static constexpr float TRAIL_WIDTH = 2.0F;
  static constexpr double FINISH_DELAY_SECONDS = 0.5;
  static constexpr double MAX_DURATION_SECONDS = 30.0;

 public:
  [[nodiscard]] const char* name() const override { return "Overtake"; }
  [[nodiscard]] const char* summary() const override
  {
    return "A slower leader and a faster chaser race for the same finish. "
           "The chaser goes round the leader if he can still get there "
           "first, on the shorter side that nobody else is about to run "
           "into. Who finished first, the closest they came, and the times.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Gap (m)", &gapSetting,
                           {.min = 1.0F, .max = GAP_SETTING_MAX}, false,
                           "How far ahead of the chaser the leader starts."));
    list.push_back(
        setting("Finish (m)", &finishSetting,
                {.min = FINISH_SETTING_MIN, .max = FINISH_SETTING_MAX}, true,
                "From the chaser's start to the finish line."));
    list.push_back(
        setting("Leader off the line (m)", &offsetSetting,
                {.min = OFFSET_SETTING_MIN, .max = OFFSET_SETTING_MAX}, false,
                "The leader's start to the side of the chaser's "
                "line (+ to his left)."));
    list.push_back(setting("Leader Pace", &leaderPace,
                           {.min = 1.0F, .max = LEADER_PACE_MAX}, true));
    list.push_back(setting("Chaser Pace", &chaserPace,
                           {.min = 1.0F, .max = CHASER_PACE_MAX}, true));
    DrillParameter team =
        setting("Leader is", &teamChoice, {.min = 0.0F, .max = 1.0F}, true);
    team.choices = {"An opponent", "A team-mate"};
    list.push_back(team);
    DrillParameter third = setting("Third runner", &thirdChoice,
                                   {.min = 0.0F, .max = THIRD_CHOICE_MAX}, true,
                                   "Alongside the leader, as fast as him, "
                                   "blocking that side.");
    third.choices = {"None", "On his left", "On his right"};
    list.push_back(third);
    return list;
  }

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    const bool mates = teamChoice >= CHOICE_MIDPOINT;
    std::vector<DrillPlayerSpec> list{{.name = "Chaser",
                                       .role = PlayerRole::CM,
                                       .homeTeam = true,
                                       .stats = {{"Pace", chaserPace}},
                                       .derived = {}},
                                      {.name = "Leader",
                                       .role = PlayerRole::CM,
                                       .homeTeam = mates,
                                       .stats = {{"Pace", leaderPace}},
                                       .derived = {}}};
    if (thirdChoice >= CHOICE_MIDPOINT)
    {
      list.push_back({.name = "Third",
                      .role = PlayerRole::CM,
                      .homeTeam = mates,
                      .stats = {{"Pace", leaderPace}},
                      .derived = {}});
    }
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    // All flat out, as if chasing a loose ball, whatever job the brain has.
    MatchEngine::DrillRules rules = engine.getDrillRules();
    rules.chaseEffort = true;
    engine.setDrillRules(rules);
    gap = gapSetting;
    finishMetres = finishSetting;
    offset = offsetSetting;
    third = static_cast<int>(std::round(thirdChoice));
    chaserId = ids[0];
    leaderId = ids[1];
    thirdId = ids.size() > 2 ? ids[2] : 0;
    const PitchPoint start = at(START_LENGTH_SHARE, TRACK_CENTRE_SHARE);
    const PitchPoint leaderAt = along(along(start, {.x = 1.0F, .y = 0.0F}, gap),
                                      {.x = 0.0F, .y = 1.0F}, offset);
    finishLine = along(start, {.x = 1.0F, .y = 0.0F}, finishMetres);
    MatchScenario scenario;
    scenario.players.push_back(
        {.playerId = chaserId, .position = start, .makingRun = false});
    scenario.players.push_back(
        {.playerId = leaderId, .position = leaderAt, .makingRun = false});
    if (thirdId != 0)
    {
      scenario.players.push_back(
          {.playerId = thirdId,
           .position =
               along(leaderAt, {.x = 0.0F, .y = 1.0F},
                     third == 1 ? THIRD_SIDE_METRES : -THIRD_SIDE_METRES),
           .makingRun = false});
    }
    scenario.ballPosition = ballOutOfTheWay();
    engine.startDrill(scenario);
    // They all race for the same point beyond the line.
    const PitchPoint target =
        along(finishLine, {.x = 1.0F, .y = 0.0F}, RUN_THROUGH_METRES);
    for (const PlayerID id : {chaserId, leaderId, thirdId})
    {
      if (id != 0)
      {
        engine.setDrillTarget(id, target, true);
      }
    }
    chaserTime.reset();
    leaderTime.reset();
    closest = std::numeric_limits<float>::max();
    closestThird = std::numeric_limits<float>::max();
    previousChaser = start;
    previousLeader = leaderAt;
    trail.clear();
    PitchKit::Layout kit;
    kit.lines.push_back({.name = "Finish",
                         .from = along(finishLine, {.x = 0.0F, .y = 1.0F},
                                       -FINISH_HALF_WIDTH_METRES),
                         .to = along(finishLine, {.x = 0.0F, .y = 1.0F},
                                     FINISH_HALF_WIDTH_METRES),
                         .colour = COURSE_FINISH_COLOUR});
    engine.setKit(std::move(kit));
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* chaser = slotOf(engine, chaserId);
    const MatchPlayer* leader = slotOf(engine, leaderId);
    if ((chaser == nullptr) || (leader == nullptr))
    {
      return false;
    }
    const double now = secondsOf(engine);
    const auto crossed =
        [&](PitchPoint from, PitchPoint to, std::optional<double>& time)
    {
      if (time)
      {
        return;
      }
      if (const auto share = crossingShare(
              {.from = from, .to = to},
              {.point = finishLine, .direction = {.x = 1.0F, .y = 0.0F}}))
      {
        time = now - STEP + (STEP * static_cast<double>(*share));
      }
    };
    crossed(previousChaser, chaser->position, chaserTime);
    crossed(previousLeader, leader->position, leaderTime);
    previousChaser = chaser->position;
    previousLeader = leader->position;
    // Until the chaser is over the line: how close he came.
    if (!chaserTime)
    {
      closest =
          std::min(closest, metresBetween(chaser->position, leader->position));
      if (const MatchPlayer* other =
              (thirdId != 0U) ? slotOf(engine, thirdId) : nullptr)
      {
        closestThird = std::min(
            closestThird, metresBetween(chaser->position, other->position));
      }
    }
    if (engine.getSimulatedSteps() % 2 == 0)
    {
      trail.push_back(chaser->position);
    }
    const bool done = chaserTime && leaderTime;
    return (!done ||
            now <= std::max(*chaserTime, *leaderTime) + FINISH_DELAY_SECONDS) &&
           now < MAX_DURATION_SECONDS;
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::path(renderer, draw, trail, RUNNER_TRAIL_COLOUR,
                    TRAIL_WIDTH * scale);
    // The finish line is kit on the pitch (begin).
    DrillDraw::label(renderer, draw, finishLine, "finish",
                     COURSE_FINISH_COLOUR);
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "Leader {:.1f} m ahead, {:+.1f} m off the line ({}), finish {:.0f} m{}",
        gap, offset, teamChoice >= CHOICE_MIDPOINT ? "team-mate" : "opponent",
        finishMetres, thirdRunnerDescription()));
    if (chaserTime)
    {
      lines.push_back(std::format("Chaser {:.2f} s", *chaserTime));
    }
    if (leaderTime)
    {
      lines.push_back(std::format("Leader {:.2f} s", *leaderTime));
    }
    lines.push_back(std::format("Closest to the leader {:.2f} m", closest));
    if (thirdId != 0)
    {
      lines.push_back(
          std::format("Closest to the third runner {:.2f} m", closestThird));
    }
    if (chaserTime && leaderTime)
    {
      lines.push_back(
          std::format("FINISH  {} first, by {:.2f} s",
                      *chaserTime < *leaderTime ? "chaser" : "leader",
                      std::abs(*chaserTime - *leaderTime)));
    }
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (!chaserTime || !leaderTime)
    {
      return {};
    }
    std::vector<DrillMetric> out{
        {.name = "Chaser (s)", .value = *chaserTime},
        {.name = "Leader (s)", .value = *leaderTime},
        {.name = "Chaser first",
         .value = *chaserTime < *leaderTime ? 1.0 : 0.0},
        {.name = "Closest to leader (m)", .value = closest}};
    if (thirdId != 0)
    {
      out.push_back({.name = "Closest to third (m)", .value = closestThird});
    }
    return out;
  }

 private:
  static constexpr float THIRD_SIDE_METRES = 1.6F;

  float gapSetting = DEFAULT_GAP_SETTING;
  float finishSetting = DEFAULT_FINISH_SETTING;
  float offsetSetting = 0.0F;
  float leaderPace = DEFAULT_LEADER_PACE;
  float chaserPace = DEFAULT_CHASER_PACE;
  float teamChoice = 0.0F;
  float thirdChoice = 0.0F;

  float gap = DEFAULT_GAP_SETTING;
  float finishMetres = DEFAULT_FINISH_SETTING;
  float offset = 0.0F;
  int third = 0;
  PlayerID chaserId = 0;
  PlayerID leaderId = 0;
  PlayerID thirdId = 0;
  PitchPoint finishLine;
  PitchPoint previousChaser;
  PitchPoint previousLeader;
  std::optional<double> chaserTime;
  std::optional<double> leaderTime;
  float closest = 0.0F;
  float closestThird = 0.0F;
  std::vector<PitchPoint> trail;
};

// --- Loose ball --------------------------------------------------------------

/**
 * Loose ball: a ball rolled along the pitch, nobody's pass. A chaser starts
 * just behind it, a team-mate further along its path (and an opponent if
 * set). The brain picks who goes for it: the one who can meet it first on
 * its path should, not the one nearest where it is now.
 */
class LooseBallDrill final : public Drill
{
  void observeBystander(const MatchEngine& engine)
  {
    // The chaser and the bystander: how close, and any way round him.
    waypoint.reset();
    if (bystanderId != 0 && !wonAt)
    {
      if (const MatchPlayer* chaser = slotOf(engine, chaserId))
      {
        if (const MatchPlayer* bystander = slotOf(engine, bystanderId))
        {
          closestBystander =
              std::min(closestBystander,
                       metresBetween(chaser->position, bystander->position));
          const auto& all = engine.getPlayers();
          if (chaser->wayRoundOf >= 0 && &all.at(static_cast<std::size_t>(
                                             chaser->wayRoundOf)) == bystander)
          {
            waypoint = chaser->movementTarget;
            wentRound = chaser->wayRoundSide;
            if (!roundAt)
            {
              roundAt = secondsOf(engine);
            }
          }
        }
      }
    }
  }

  [[nodiscard]] int winnerNumber(PlayerID holder) const
  {
    if (holder == chaserId)
    {
      return 1;
    }
    if (holder == mateId)
    {
      return 2;
    }
    if (holder == rivalId)
    {
      return 3;
    }
    return 4;
  }

  static constexpr ImU32 WAYPOINT_COLOUR = IM_COL32(120, 220, 255, 255);
  static constexpr float BALL_SPEED_SETTING_MIN = 2.0F;
  static constexpr float BALL_SPEED_SETTING_MAX = 25.0F;
  static constexpr float CHASER_SETTING_MAX = 20.0F;
  static constexpr float CHASER_OFFSET_SETTING_MIN = -15.0F;
  static constexpr float CHASER_OFFSET_SETTING_MAX = 15.0F;
  static constexpr float MATE_SETTING_MIN = 5.0F;
  static constexpr float MATE_SETTING_MAX = 50.0F;
  static constexpr float MATE_OFFSET_SETTING_MIN = -10.0F;
  static constexpr float MATE_OFFSET_SETTING_MAX = 10.0F;
  static constexpr float RIVAL_CHOICE_MAX = 2.0F;
  static constexpr float BYSTANDER_CHOICE_MAX = 2.0F;
  static constexpr float BYSTANDER_ALONG_SETTING_MIN = 2.0F;
  static constexpr float BYSTANDER_ALONG_SETTING_MAX = 40.0F;
  static constexpr float BYSTANDER_OFFSET_SETTING_MIN = -10.0F;
  static constexpr float BYSTANDER_OFFSET_SETTING_MAX = 10.0F;
  static constexpr float BYSTANDER_MOVE_CHOICE_MAX = 2.0F;
  static constexpr float DEFAULT_BALL_SPEED_SETTING = 10.0F;
  static constexpr float DEFAULT_CHASER_SETTING = 3.0F;
  static constexpr float DEFAULT_MATE_SETTING = 25.0F;
  static constexpr float DEFAULT_MATE_OFFSET_SETTING = 2.0F;
  static constexpr float DEFAULT_BYSTANDER_ALONG_SETTING = 8.0F;
  static constexpr float BALL_START_LENGTH_SHARE = 0.35F;
  static constexpr float BALL_START_WIDTH_SHARE = 0.5F;
  static constexpr float RIVAL_BESIDE_MATE_METRES = 8.0F;
  static constexpr float RIVAL_BESIDE_CHASER_METRES = 3.0F;
  static constexpr float BYSTANDER_CROSSING_METRES = 30.0F;
  static constexpr float WAYPOINT_MARKER_RADIUS = 0.15F;
  static constexpr float OPPONENT_CHOICE_THRESHOLD = 1.5F;
  static constexpr double OUTCOME_DELAY_SECONDS = 0.5;
  static constexpr double MAX_DURATION_SECONDS = 12.0;

 public:
  [[nodiscard]] const char* name() const override { return "Loose ball"; }
  [[nodiscard]] const char* summary() const override
  {
    return "A ball rolled loose along the pitch, a chaser just behind it and "
           "a team-mate further along its path (and an opponent, if set). The "
           "brain decides who goes: who wins it, how soon, and how far each "
           "ran.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting(
        "Ball speed (m/s)", &ballSpeedSetting,
        {.min = BALL_SPEED_SETTING_MIN, .max = BALL_SPEED_SETTING_MAX}, false));
    list.push_back(setting("Chaser behind it (m)", &chaserSetting,
                           {.min = 0.0F, .max = CHASER_SETTING_MAX}, false));
    list.push_back(setting(
        "Chaser off its line (m)", &chaserOffsetSetting,
        {.min = CHASER_OFFSET_SETTING_MIN, .max = CHASER_OFFSET_SETTING_MAX},
        false, "+ to the left of the ball's path."));
    list.push_back(setting("Team-mate along its path (m)", &mateSetting,
                           {.min = MATE_SETTING_MIN, .max = MATE_SETTING_MAX},
                           false));
    list.push_back(setting(
        "Team-mate off its line (m)", &mateOffsetSetting,
        {.min = MATE_OFFSET_SETTING_MIN, .max = MATE_OFFSET_SETTING_MAX},
        false));
    DrillParameter rival = setting(
        "Opponent", &rivalChoice, {.min = 0.0F, .max = RIVAL_CHOICE_MAX}, true);
    rival.choices = {"None", "Level with the team-mate", "Beside the chaser"};
    list.push_back(rival);
    DrillParameter bystander =
        setting("Bystander", &bystanderChoice,
                {.min = 0.0F, .max = BYSTANDER_CHOICE_MAX}, true,
                "A player not going for the ball, in the "
                "chaser's way: he goes round him (way-round "
                "plan).");
    bystander.choices = {"None", "A team-mate", "An opponent"};
    bystander.section = "Bystander";
    list.push_back(bystander);
    list.push_back(setting("Bystander along its path (m)",
                           &bystanderAlongSetting,
                           {.min = BYSTANDER_ALONG_SETTING_MIN,
                            .max = BYSTANDER_ALONG_SETTING_MAX},
                           false));
    list.push_back(setting("Bystander off its line (m)",
                           &bystanderOffsetSetting,
                           {.min = BYSTANDER_OFFSET_SETTING_MIN,
                            .max = BYSTANDER_OFFSET_SETTING_MAX},
                           false, "+ to the left of the ball's path."));
    DrillParameter moving =
        setting("Bystander moves", &bystanderMoveChoice,
                {.min = 0.0F, .max = BYSTANDER_MOVE_CHOICE_MAX}, true,
                "Across the ball's path, from his side of it.");
    moving.choices = {"Stands", "Walks across", "Runs across"};
    list.push_back(moving);
    return list;
  }

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    std::vector<DrillPlayerSpec> list{{.name = "Chaser",
                                       .role = PlayerRole::CM,
                                       .homeTeam = true,
                                       .stats = {},
                                       .derived = {}},
                                      {.name = "Team-mate",
                                       .role = PlayerRole::CM,
                                       .homeTeam = true,
                                       .stats = {},
                                       .derived = {}}};
    if (rivalChoice >= CHOICE_MIDPOINT)
    {
      list.push_back({.name = "Opponent",
                      .role = PlayerRole::CM,
                      .homeTeam = false,
                      .stats = {},
                      .derived = {}});
    }
    if (bystanderChoice >= CHOICE_MIDPOINT)
    {
      list.push_back({.name = "Bystander",
                      .role = PlayerRole::CM,
                      .homeTeam = bystanderChoice < OPPONENT_CHOICE_THRESHOLD,
                      .stats = {},
                      .derived = {}});
    }
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    ballSpeed = ballSpeedSetting;
    rival = static_cast<int>(std::round(rivalChoice));
    chaserId = ids[0];
    mateId = ids[1];
    rivalId = rivalChoice >= CHOICE_MIDPOINT && ids.size() > 2 ? ids[2] : 0;
    bystanderId = bystanderChoice >= CHOICE_MIDPOINT ? ids.back() : 0;
    const PitchPoint ballAt =
        at(BALL_START_LENGTH_SHARE, BALL_START_WIDTH_SHARE);
    const PitchPoint chaserAt =
        along(along(ballAt, {.x = 1.0F, .y = 0.0F}, -chaserSetting),
              {.x = 0.0F, .y = 1.0F}, chaserOffsetSetting);
    const PitchPoint mateAt =
        along(along(ballAt, {.x = 1.0F, .y = 0.0F}, mateSetting),
              {.x = 0.0F, .y = 1.0F}, mateOffsetSetting);
    MatchScenario scenario;
    scenario.players.push_back(
        {.playerId = chaserId, .position = chaserAt, .makingRun = false});
    scenario.players.push_back(
        {.playerId = mateId, .position = mateAt, .makingRun = false});
    if (rivalId != 0)
    {
      scenario.players.push_back(
          {.playerId = rivalId,
           .position = rival == 1 ? along(mateAt, {.x = 0.0F, .y = 1.0F},
                                          RIVAL_BESIDE_MATE_METRES)
                                  : along(chaserAt, {.x = 0.0F, .y = 1.0F},
                                          RIVAL_BESIDE_CHASER_METRES),
           .makingRun = false});
    }
    const PitchPoint bystanderAt =
        along(along(ballAt, {.x = 1.0F, .y = 0.0F}, bystanderAlongSetting),
              {.x = 0.0F, .y = 1.0F}, bystanderOffsetSetting);
    if (bystanderId != 0)
    {
      scenario.players.push_back({.playerId = bystanderId,
                                  .position = bystanderAt,
                                  .makingRun = false});
    }
    scenario.ballPosition = ballAt;
    engine.startDrill(scenario);
    engine.feedBall(ballAt, {.x = ballSpeed, .y = 0.0F}, 0.0F, 0);
    if (bystanderId != 0)
    {
      // He holds his spot, or crosses the ball's path from his side of it.
      const int moves = static_cast<int>(std::round(bystanderMoveChoice));
      const float across = bystanderOffsetSetting > 0.0F ? -1.0F : 1.0F;
      engine.setDrillTarget(bystanderId,
                            moves == 0
                                ? bystanderAt
                                : along(bystanderAt, {.x = 0.0F, .y = across},
                                        BYSTANDER_CROSSING_METRES),
                            moves == 2);
    }
    closestBystander = std::numeric_limits<float>::max();
    wentRound = 0;
    roundAt.reset();
    waypoint.reset();
    winner = 0;
    wonAt.reset();
    previous.clear();
    ran.clear();
    for (const PlayerID id : {chaserId, mateId, rivalId, bystanderId})
    {
      if (id != 0)
      {
        if (const MatchPlayer* player = slotOf(engine, id))
        {
          previous[id] = player->position;
        }
      }
    }
  }

  bool afterStep(MatchEngine& engine) override
  {
    for (auto& [id, at] : previous)
    {
      if (const MatchPlayer* player = slotOf(engine, id))
      {
        ran[id] += metresBetween(at, player->position);
        at = player->position;
      }
    }
    observeBystander(engine);
    const double now = secondsOf(engine);
    if (!wonAt)
    {
      const Player* holder = engine.getPossessionAttachment();
      if (holder != nullptr)
      {
        wonAt = now;
        winner = winnerNumber(holder->getId());
      }
      else if (engine.getState() != MatchState::PLAYING)
      {
        wonAt = now;
        winner = 0;
      }
    }
    return (!wonAt || now <= *wonAt + OUTCOME_DELAY_SECONDS) &&
           now < MAX_DURATION_SECONDS;
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    // The chaser's way round the bystander, while he is on it.
    if (waypoint)
    {
      DrillDraw::marker(renderer, draw, *waypoint, WAYPOINT_MARKER_RADIUS,
                        WAYPOINT_COLOUR);
    }
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    static constexpr std::array<const char*, 5> WHO{
        "nobody (out of play)", "the chaser", "the team-mate", "the opponent",
        "someone else"};
    std::vector<std::string> lines;
    lines.push_back(
        std::format("Ball {:.0f} m/s, chaser {:.0f} m behind, team-mate "
                    "{:.0f} m along ({:+.0f} m off)",
                    ballSpeed, chaserSetting, mateSetting, mateOffsetSetting));
    const auto metres = [&](PlayerID id)
    { return ran.contains(id) ? ran.at(id) : 0.0F; };
    lines.push_back(std::format(
        "Ran: chaser {:.1f} m, team-mate {:.1f} m{}", metres(chaserId),
        metres(mateId),
        (rivalId != 0U) ? std::format(", opponent {:.1f} m", metres(rivalId))
                        : std::string()));
    if (bystanderId != 0)
    {
      lines.push_back(std::format(
          "Bystander: closest {:.2f} m{}", closestBystander,
          wentRound == 0
              ? std::string(", not gone round")
              : std::format(", gone round on his {} from {:.1f} s",
                            wentRound > 0 ? "left" : "right", *roundAt)));
    }
    if (wonAt)
    {
      lines.push_back(std::format("OUTCOME  won by {} at {:.1f} s",
                                  WHO.at(winner), *wonAt));
    }
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (!wonAt)
    {
      return {};
    }
    const auto metres = [&](PlayerID id)
    { return ran.contains(id) ? static_cast<double>(ran.at(id)) : 0.0; };
    std::vector<DrillMetric> out{
        {.name = "Winner (1 chaser, 2 mate, 3 opp)",
         .value = static_cast<double>(winner)},
        {.name = "Won at (s)", .value = *wonAt},
        {.name = "Chaser ran (m)", .value = metres(chaserId)},
        {.name = "Team-mate ran (m)", .value = metres(mateId)}};
    if (rivalId != 0)
    {
      out.push_back({.name = "Opponent ran (m)", .value = metres(rivalId)});
    }
    if (bystanderId != 0)
    {
      out.push_back(
          {.name = "Closest to bystander (m)", .value = closestBystander});
      out.push_back({.name = "Went round (+1 left, -1 right)",
                     .value = static_cast<double>(wentRound)});
    }
    return out;
  }

 private:
  float ballSpeedSetting = DEFAULT_BALL_SPEED_SETTING;
  float chaserSetting = DEFAULT_CHASER_SETTING;
  float chaserOffsetSetting = 0.0F;
  float mateSetting = DEFAULT_MATE_SETTING;
  float mateOffsetSetting = DEFAULT_MATE_OFFSET_SETTING;
  float rivalChoice = 0.0F;
  float bystanderChoice = 0.0F;
  float bystanderAlongSetting = DEFAULT_BYSTANDER_ALONG_SETTING;
  float bystanderOffsetSetting = 0.0F;
  float bystanderMoveChoice = 0.0F;
  PlayerID bystanderId = 0;
  float closestBystander = 0.0F;
  std::int8_t wentRound = 0;
  std::optional<double> roundAt;
  std::optional<PitchPoint> waypoint;

  float ballSpeed = DEFAULT_BALL_SPEED_SETTING;
  int rival = 0;
  PlayerID chaserId = 0;
  PlayerID mateId = 0;
  PlayerID rivalId = 0;
  int winner = 0;
  std::optional<double> wonAt;
  std::map<PlayerID, PitchPoint> previous;
  std::map<PlayerID, float> ran;
};

// --- Tackle
// --------------------------------------------------------------------

/**
 * Tackle (tackling plan): an attacker dribbles straight at a lone defender,
 * only carrying it (no pass, shot or take-on). The defender's brain decides
 * when to go in, or he is sent at the ball. How the ball was taken: as a
 * free ball (outside the attacker's reach), by the tackle roll, or not at
 * all.
 */
class TackleDrill final : public Drill
{
  struct DuelPlayers
  {
    const MatchPlayer* attacker{};
    const MatchPlayer* defender{};
  };
  void resolveOutcome(const MatchEngine& engine, DuelPlayers players,
                      double now)
  {
    const MatchPlayer* attacker = players.attacker;
    const MatchPlayer* defender = players.defender;
    const MatchBall& ball = engine.getBall();

    const MatchPlayer* mate = (mateId != 0U) ? slotOf(engine, mateId) : nullptr;
    const bool sideHasIt =
        (engine.getPossessionAttachment() != nullptr) &&
        (engine.getPossessionAttachment() == defender->player ||
         ((mate != nullptr) &&
          engine.getPossessionAttachment() == mate->player)) &&
        (!engine.hasTouchClaim() || engine.isTouchCollected());
    if (engine.getState() != MatchState::PLAYING)
    {
      outcome = Outcome::STOPPED;
    }
    else if (sideHasIt)
    {
      outcome = Outcome::WON;
    }
    else if (engine.getPossessionAttachment() == attacker->player &&
             attacker->position.x >= endPoint.x)
    {
      outcome = Outcome::KEPT;
    }
    else if (now > TIME_LIMIT_SECONDS)
    {
      outcome = Outcome::TIME;
    }
    if (outcome != Outcome::NONE)
    {
      endAt = now;
      endGap = metresBetween(attacker->position, ball.position);
      travelled = tackleAt ? metresBetween(*tackleAt, ball.position) : 0.0F;
    }
  }

  static constexpr float INTENT_CHOICE_MAX = 4.0F;
  static constexpr ImU32 DEFENDER_START_COLOUR = IM_COL32(240, 80, 80, 255);
  static constexpr ImU32 END_LINE_COLOUR = IM_COL32(120, 220, 120, 255);
  static constexpr ImU32 BALL_TRAIL_COLOUR = IM_COL32(255, 220, 120, 200);
  static constexpr float GAP_SETTING_MIN = 3.0F;
  static constexpr float GAP_SETTING_MAX = 30.0F;
  static constexpr float PACE_CHOICE_MAX = 3.0F;
  static constexpr float DRIBBLING_MAX = 100.0F;
  static constexpr float ATTACKER_PACE_MAX = 100.0F;
  static constexpr float DEFENDING_MAX = 100.0F;
  static constexpr float DEFENDER_PACE_MAX = 100.0F;
  static constexpr float MATE_BEHIND_SETTING_MIN = -20.0F;
  static constexpr float MATE_BEHIND_SETTING_MAX = 30.0F;
  static constexpr float MATE_ACROSS_SETTING_MIN = -30.0F;
  static constexpr float MATE_ACROSS_SETTING_MAX = 30.0F;
  static constexpr float DEFAULT_GAP_SETTING = 12.0F;
  static constexpr float DEFAULT_PACE_CHOICE = 2.0F;
  static constexpr float DEFAULT_DRIBBLING = 70.0F;
  static constexpr float DEFAULT_ATTACKER_PACE = 70.0F;
  static constexpr float DEFAULT_DEFENDING = 70.0F;
  static constexpr float DEFAULT_DEFENDER_PACE = 70.0F;
  static constexpr float DEFAULT_INTENT_CHOICE = 4.0F;
  static constexpr float DEFAULT_MATE_BEHIND_SETTING = 10.0F;
  static constexpr float DEFAULT_MATE_ACROSS_SETTING = 15.0F;
  static constexpr float ATTACKER_START_LENGTH_SHARE = 0.3F;
  static constexpr float TRACK_CENTRE_SHARE = 0.5F;
  static constexpr float DEFENDER_MARKER_RADIUS = 0.3F;
  static constexpr float END_HALF_WIDTH_METRES = 6.0F;
  static constexpr float BALL_TRAIL_WIDTH = 2.0F;
  static constexpr double NUDGE_WATCH_SECONDS = 1.5;
  static constexpr double OUTCOME_DELAY_SECONDS = 0.5;
  static constexpr float NUDGE_STOP_SPEED_METRES_PER_SECOND = 0.5F;
  static constexpr float SPRINT_PACE_CHOICE = 3.0F;

 public:
  [[nodiscard]] const char* name() const override { return "Tackle"; }
  [[nodiscard]] const char* summary() const override
  {
    return "An attacker dribbles straight at a lone defender, only carrying "
           "it. Whether the defender takes it, and how: a free ball (outside "
           "the attacker's reach, played with his intent), a contest (inside "
           "it) or a slide's roll; or the attacker reaches the end, 15 m "
           "beyond the defender, with it.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(
        setting("Gap (m)", &gapSetting,
                {.min = GAP_SETTING_MIN, .max = GAP_SETTING_MAX}, false,
                "How far ahead of the attacker the defender starts."));
    DrillParameter pace = setting("Attacker carries at", &paceChoice,
                                  {.min = 0.0F, .max = PACE_CHOICE_MAX}, true,
                                  "How hard he carries it.");
    pace.choices = {"A walk", "A jog", "A run", "A sprint"};
    list.push_back(pace);
    list.push_back(setting("Dribbling", &dribbling,
                           {.min = 1.0F, .max = DRIBBLING_MAX}, true,
                           "The attacker's: touch length and rhythm."));
    list.push_back(setting("Attacker Pace", &attackerPace,
                           {.min = 1.0F, .max = ATTACKER_PACE_MAX}, true));
    list.push_back(setting("Defending", &defending,
                           {.min = 1.0F, .max = DEFENDING_MAX}, true,
                           "The defender's."));
    list.push_back(setting("Defender Pace", &defenderPace,
                           {.min = 1.0F, .max = DEFENDER_PACE_MAX}, true));
    DrillParameter moves =
        setting("Defender", &moveChoice, {.min = 0.0F, .max = 1.0F}, true,
                "How the defender moves (the brain always "
                "decides the tackle).");
    moves.choices = {"His brain", "Straight at the ball"};
    list.push_back(moves);
    DrillParameter intent = setting(
        "Defender's intent", &intentChoice,
        {.min = 0.0F, .max = INTENT_CHOICE_MAX}, true,
        "What his tackle means, on a free ball (outside the attacker's "
        "reach) or in a block (its push's way): control it; win it (a "
        "nudge away from the attacker, closer the better his Defending); "
        "clear it; a first-time pass to his team-mate; or his brain's "
        "choice.");
    intent.choices = {"Control", "Win it", "Clear", "First-time pass",
                      "His brain"};
    list.push_back(intent);
    list.push_back(setting(
        "Team-mate behind (m)", &mateBehindSetting,
        {.min = MATE_BEHIND_SETTING_MIN, .max = MATE_BEHIND_SETTING_MAX}, false,
        "First-time pass: the defender's team-mate, this "
        "far behind his start (towards his own goal)."));
    list.push_back(setting(
        "Team-mate across (m)", &mateAcrossSetting,
        {.min = MATE_ACROSS_SETTING_MIN, .max = MATE_ACROSS_SETTING_MAX}, false,
        "First-time pass: and this far across (+ to the "
        "attacker's left)."));
    return list;
  }

  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    constexpr float PRESET_CLOSE_CONTROL_DRIBBLING = 85.0F;
    constexpr float PRESET_KNOCKED_ON_ATTACKER_CARRIES_AT = 3.0F;
    constexpr float PRESET_KNOCKED_ON_DRIBBLING = 20.0F;

    return {
        {.name = "Close control",
         .summary =
             "A good dribbler at a jog: the ball rarely leaves his reach.",
         .settings = {{"Attacker carries at", 1.0F},
                      {"Dribbling", PRESET_CLOSE_CONTROL_DRIBBLING}}},
        {.name = "Knocked on",
         .summary = "A poor dribbler at a sprint: long touches, the ball often "
                    "out of his "
                    "reach.",
         .settings = {{"Attacker carries at",
                       PRESET_KNOCKED_ON_ATTACKER_CARRIES_AT},
                      {"Dribbling", PRESET_KNOCKED_ON_DRIBBLING}}},
    };
  }

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    std::vector<DrillPlayerSpec> list{
        {.name = "Attacker",
         .role = PlayerRole::ST,
         .homeTeam = true,
         .stats = {{"Dribbling", dribbling}, {"Pace", attackerPace}},
         .derived = {}},
        {.name = "Defender",
         .role = PlayerRole::CB,
         .homeTeam = false,
         .stats = {{"Defending", defending}, {"Pace", defenderPace}},
         .derived = {}}};
    if (intentOf() == TackleIntent::PASS)
    {
      list.push_back({.name = "Team-mate",
                      .role = PlayerRole::CB,
                      .homeTeam = false,
                      .stats = {},
                      .derived = {}});
    }
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    attackerId = ids[0];
    defenderId = ids[1];
    mateId = ids.size() > 2 ? ids[2] : 0;
    gap = gapSetting;
    atBall = moveChoice >= CHOICE_MIDPOINT;
    const PitchPoint attackerAt =
        at(ATTACKER_START_LENGTH_SHARE, TRACK_CENTRE_SHARE);
    defenderStart = along(attackerAt, {.x = 1.0F, .y = 0.0F}, gap);
    mateAt =
        along(along(defenderStart, {.x = 1.0F, .y = 0.0F}, mateBehindSetting),
              {.x = 0.0F, .y = 1.0F}, mateAcrossSetting);
    MatchScenario scenario;
    scenario.players.push_back(
        {.playerId = attackerId, .position = attackerAt, .makingRun = false});
    scenario.players.push_back({.playerId = defenderId,
                                .position = defenderStart,
                                .makingRun = false});
    if (mateId != 0)
    {
      scenario.players.push_back(
          {.playerId = mateId, .position = mateAt, .makingRun = false});
    }
    scenario.carrierId = attackerId;
    scenario.ballPosition = attackerAt;
    engine.startDrill(scenario);
    engine.keepBall(attackerId, true, paceChoice / SPRINT_PACE_CHOICE);
    engine.forceTackleIntent(defenderId, intentOf());
    // The team-mate stays on his spot to be found.
    if (mateId != 0)
    {
      engine.setDrillTarget(mateId, mateAt, false);
    }
    tackleAt.reset();
    endGap = 0.0F;
    travelled = 0.0F;
    nudgesBefore = engine.getNudges();
    nudgeFrom.reset();
    nudgeEnd.reset();
    nudgeAttackerGap = 0.0F;
    // Straight on, through where the defender stands, to the end.
    endPoint = along(defenderStart, {.x = 1.0F, .y = 0.0F}, BEYOND_METRES);
    engine.setDrillTarget(
        attackerId, along(endPoint, {.x = 1.0F, .y = 0.0F}, RUN_THROUGH_METRES),
        true);
    before = engine.getStats();
    outcome = Outcome::NONE;
    endAt.reset();
    largestGap = 0.0F;
    lastGap = 0.0F;
    freeBehind = 0;
    after = before;
    trail.clear();
    PitchKit::Layout kit;
    kit.lines.push_back(
        {.name = "End",
         .from =
             along(endPoint, {.x = 0.0F, .y = 1.0F}, -END_HALF_WIDTH_METRES),
         .to = along(endPoint, {.x = 0.0F, .y = 1.0F}, END_HALF_WIDTH_METRES),
         .colour = END_LINE_COLOUR});
    engine.setKit(std::move(kit));
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchPlayer* attacker = slotOf(engine, attackerId);
    const MatchPlayer* defender = slotOf(engine, defenderId);
    if ((attacker == nullptr) || (defender == nullptr))
    {
      return false;
    }
    const MatchBall& ball = engine.getBall();
    if (atBall && outcome == Outcome::NONE)
    {
      engine.setDrillTarget(defenderId, ball.position, true);
    }
    if (engine.getSimulatedSteps() % 2 == 0)
    {
      trail.push_back(ball.position);
    }
    // A free-ball tackle this step: was the ball, a step ago, within his
    // reach's distance (so behind him, in his dead zone)?
    if (after.awayFreeBallTackles != engine.getStats().awayFreeBallTackles &&
        lastGap <= MatchTuning::Ball::OUTFIELD_CONTROL_RADIUS_METRES)
    {
      ++freeBehind;
    }
    // Where his first free-ball tackle met it (about: the ball this step).
    if (!tackleAt &&
        after.awayFreeBallTackles != engine.getStats().awayFreeBallTackles)
    {
      tackleAt = ball.position;
    }
    // His first nudge: followed until the ball has (nearly) stopped, he no
    // longer owns it, or a second and a half.
    if (!nudgeFrom && engine.getNudges() > nudgesBefore)
    {
      nudgeFrom = tackleAt.value_or(ball.position);
      nudgeAim = engine.getLastNudgeSpot();
      nudgeSince = now;
    }
    if (nudgeFrom && !nudgeEnd &&
        (std::hypot(ball.velocity.x, ball.velocity.y) <
             NUDGE_STOP_SPEED_METRES_PER_SECOND ||
         engine.getPossessionAttachment() != defender->player ||
         now - nudgeSince > NUDGE_WATCH_SECONDS))
    {
      nudgeEnd = ball.position;
      nudgeAttackerGap = metresBetween(attacker->position, ball.position);
    }
    after = engine.getStats();
    lastGap = metresBetween(attacker->position, ball.position);
    if (outcome == Outcome::NONE &&
        engine.getPossessionAttachment() == attacker->player)
    {
      largestGap = std::max(largestGap, lastGap);
    }
    if (outcome == Outcome::NONE)
    {
      resolveOutcome(engine, {.attacker = attacker, .defender = defender}, now);
    }
    return (!endAt || now <= *endAt + OUTCOME_DELAY_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    DrillDraw::path(renderer, draw, trail, BALL_TRAIL_COLOUR,
                    BALL_TRAIL_WIDTH * Theme::scale());
    DrillDraw::marker(renderer, draw, defenderStart, DEFENDER_MARKER_RADIUS,
                      DEFENDER_START_COLOUR, "defender's start");
    // The end line is kit on the pitch (begin).
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    static constexpr std::array<const char*, 5> WHAT{
        "", "the defender's side won it",
        "the attacker reached the end with it", "play stopped", "time up"};
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "Gap {:.0f} m, Dribbling {:.0f}, Defending {:.0f}{}", gap, dribbling,
        defending, atBall ? ", defender sent at the ball" : ""));
    lines.push_back(std::format(
        "Tackles: {} on a free ball ({} won), {} contested ({} won), {} by "
        "the roll ({} won)",
        freeBall(), freeBallWon(), contest(), contestWon(), rolled(),
        rolledWon()));
    if (endAt)
    {
      lines.push_back(std::format("OUTCOME  {} at {:.1f} s",
                                  WHAT.at(static_cast<int>(outcome)), *endAt));
    }
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (!endAt)
    {
      return {};
    }
    return {
        {.name = "Defender's side won it",
         .value = outcome == Outcome::WON ? 1.0 : 0.0},
        {.name = "Attacker kept it",
         .value = outcome == Outcome::KEPT ? 1.0 : 0.0},
        {.name = "Largest gap (m)", .value = largestGap},
        {.name = "Ball from attacker at the end (m)", .value = endGap},
        {.name = "Ball from free-ball tackle at the end (m)",
         .value = travelled},
        {.name = "Nudged", .value = nudgeEnd ? 1.0 : 0.0},
        {.name = "Nudge length (m)",
         .value = nudgeEnd ? metresBetween(*nudgeFrom, *nudgeEnd) : 0.0},
        {.name = "Nudge aimed (m)",
         .value = nudgeEnd ? metresBetween(*nudgeFrom, nudgeAim) : 0.0},
        {.name = "Nudge off its aim (m)",
         .value = nudgeEnd ? metresBetween(nudgeAim, *nudgeEnd) : 0.0},
        {.name = "Nudge: ball from attacker (m)", .value = nudgeAttackerGap},
        {.name = "Free-ball tackles", .value = static_cast<double>(freeBall())},
        {.name = "Free-ball tackles won",
         .value = static_cast<double>(freeBallWon())},
        {.name = "Free ball behind him",
         .value = static_cast<double>(freeBehind)},
        {.name = "Contest tackles", .value = static_cast<double>(contest())},
        {.name = "Contest tackles won",
         .value = static_cast<double>(contestWon())},
        {.name = "Blocks",
         .value = static_cast<double>(after.homeBlocks + after.awayBlocks -
                                      before.homeBlocks - before.awayBlocks)},
        {.name = "Blocks popped up",
         .value =
             static_cast<double>(after.blocksPoppedUp - before.blocksPoppedUp)},
        {.name = "Defender went in",
         .value = static_cast<double>(after.awayGoIns - before.awayGoIns)},
        {.name = "Defender held (steps)",
         .value = static_cast<double>(after.awayHolds - before.awayHolds)},
        {.name = "Went in on his free foot",
         .value = static_cast<double>(after.goInsOnFreeFoot -
                                      before.goInsOnFreeFoot)},
        {.name = "Attacker's tackles back",
         .value = static_cast<double>(
             after.homeContestTackles - before.homeContestTackles +
             after.homeFreeBallTackles - before.homeFreeBallTackles)},
        {.name = "Attacker swung at nothing",
         .value = static_cast<double>(after.homeCarrierAirSwings -
                                      before.homeCarrierAirSwings)},
        {.name = "Defender swung at nothing",
         .value = static_cast<double>(after.awayTacklerAirSwings -
                                      before.awayTacklerAirSwings)},
        {.name = "Attacker missed, path too new",
         .value = static_cast<double>(after.homeCommitMisses -
                                      before.homeCommitMisses)},
        {.name = "Defender missed, path too new",
         .value = static_cast<double>(after.awayCommitMisses -
                                      before.awayCommitMisses)},
        {.name = "Rolled tackles", .value = static_cast<double>(rolled())},
        {.name = "Rolled tackles won",
         .value = static_cast<double>(rolledWon())},
        {.name = "Ended at (s)", .value = *endAt}};
  }

 private:
  enum class Outcome : std::uint8_t
  {
    NONE,
    WON,
    KEPT,
    STOPPED,
    TIME
  };
  /** The end: this far beyond the defender's start (m). Reached with the
   * ball, the attacker kept it. */
  static constexpr float BEYOND_METRES = 15.0F;
  static constexpr double TIME_LIMIT_SECONDS = 20.0;

  // The defender is the away side.
  [[nodiscard]] int freeBall() const
  {
    return after.awayFreeBallTackles - before.awayFreeBallTackles;
  }
  [[nodiscard]] int freeBallWon() const
  {
    return after.awayFreeBallTacklesWon - before.awayFreeBallTacklesWon;
  }
  [[nodiscard]] int contest() const
  {
    return after.awayContestTackles - before.awayContestTackles;
  }
  [[nodiscard]] int contestWon() const
  {
    return after.awayContestTacklesWon - before.awayContestTacklesWon;
  }
  [[nodiscard]] int rolled() const
  {
    return after.awayTackleAttempts - before.awayTackleAttempts - freeBall() -
           contest();
  }
  [[nodiscard]] int rolledWon() const
  {
    return after.awayTackles - before.awayTackles - freeBallWon() -
           contestWon();
  }

  float gapSetting = DEFAULT_GAP_SETTING;
  float paceChoice = DEFAULT_PACE_CHOICE;
  float dribbling = DEFAULT_DRIBBLING;
  float attackerPace = DEFAULT_ATTACKER_PACE;
  float defending = DEFAULT_DEFENDING;
  float defenderPace = DEFAULT_DEFENDER_PACE;
  float moveChoice = 0.0F;
  float intentChoice = DEFAULT_INTENT_CHOICE;
  float mateBehindSetting = DEFAULT_MATE_BEHIND_SETTING;
  float mateAcrossSetting = DEFAULT_MATE_ACROSS_SETTING;

  [[nodiscard]] TackleIntent intentOf() const
  {
    return static_cast<TackleIntent>(
        static_cast<int>(std::round(intentChoice)));
  }

  float gap = DEFAULT_GAP_SETTING;
  bool atBall = false;
  PlayerID attackerId = 0;
  PlayerID defenderId = 0;
  PlayerID mateId = 0;
  PitchPoint mateAt;
  std::optional<PitchPoint> tackleAt;
  float endGap = 0.0F;
  float travelled = 0.0F;
  int nudgesBefore = 0;
  std::optional<PitchPoint> nudgeFrom;
  std::optional<PitchPoint> nudgeEnd;
  PitchPoint nudgeAim;
  double nudgeSince = 0.0;
  float nudgeAttackerGap = 0.0F;
  PitchPoint defenderStart;
  PitchPoint endPoint;
  float largestGap = 0.0F;
  float lastGap = 0.0F;
  int freeBehind = 0;
  MatchStats before;
  MatchStats after;
  Outcome outcome = Outcome::NONE;
  std::optional<double> endAt;
  std::vector<PitchPoint> trail;
};

// --- Collision ---------------------------------------------------------------

/**
 * Collision (collision plan): two players run into each other. Each sprints
 * flat out through a meeting point after his run-up (none: he stands on it),
 * the later starter held back so they get there together; B's run at an
 * angle to A's, and his line moved aside to make it glancing. Each one's
 * speed before, jolt, speed after, the two measures and his pause.
 */
class CollisionDrill final : public Drill
{
  [[nodiscard]] static std::uint8_t balanceRing(const MatchPlayer& player)
  {
    if (player.fallen)
    {
      return 2;
    }
    if (player.stumbleSeconds > 0.0F)
    {
      return 1;
    }
    return 0;
  }
  [[nodiscard]] static const MatchPlayer* firstFallen(
      std::array<const MatchPlayer*, 2> players)
  {
    for (const auto* player : players)
    {
      if (player->fallen)
      {
        return player;
      }
    }
    return nullptr;
  }
  [[nodiscard]] static int targetNumber(
      const MatchPlayer* target, std::array<const MatchPlayer*, 2> players)
  {
    for (std::size_t index = 0; index < players.size(); ++index)
    {
      if (target == players.at(index))
      {
        return static_cast<int>(index) + 1;
      }
    }
    return 0;
  }
  [[nodiscard]] static std::string pauseDescription(
      const MatchEngine::CollisionSide& side)
  {
    if (side.outcome == 0)
    {
      return std::format(", pause {:.2f} s", side.pause);
    }
    if (side.outcome == 1)
    {
      return std::format(" for {:.2f} s", side.stumble);
    }
    return {};
  }
  [[nodiscard]] std::string ballDescription() const
  {
    if (ballAt == 0)
    {
      return "Ball: nobody was down to play it at";
    }
    if (ballOff)
    {
      return std::format(
          "Ball at {}: came off him at {:.1f} s, {:.1f} -> "
          "{:.1f} m/s",
          ballAt == 1 ? "A" : "B", ballOff->after, ballOff->before,
          ballOff->speed);
    }
    return std::format("Ball at {}: did not hit him (over him, or he was up)",
                       ballAt == 1 ? "A" : "B");
  }

  static constexpr float FALLEN_BALL_DELAY_SECONDS = 2.0F;
  static constexpr ImU32 RUNNER_TRAIL_COLOUR = IM_COL32(120, 200, 255, 200);
  static constexpr ImU32 SECOND_RUNNER_TRAIL_COLOUR =
      IM_COL32(255, 140, 120, 200);
  static constexpr ImU32 MEETING_COLOUR = IM_COL32(240, 240, 240, 255);
  static constexpr ImU32 CONTACT_COLOUR = IM_COL32(255, 220, 60, 255);
  static constexpr ImU32 FALLEN_COLOUR = IM_COL32(240, 60, 60, 255);
  static constexpr ImU32 STUMBLE_COLOUR = IM_COL32(255, 180, 40, 255);
  static constexpr float RUN_UP_MAX = 40.0F;
  static constexpr float PACE_MAX = 100.0F;
  static constexpr float HEIGHT_MIN = 160.0F;
  static constexpr float HEIGHT_MAX = 205.0F;
  static constexpr float STRENGTH_MAX = 99.0F;
  static constexpr float BALANCE_MAX = 99.0F;
  static constexpr float AGILITY_MAX = 99.0F;
  static constexpr float ANGLE_SETTING_MAX = 180.0F;
  static constexpr float MISS_SETTING_MIN = -0.6F;
  static constexpr float MISS_SETTING_MAX = 0.6F;
  static constexpr float NUDGE_SETTING_MIN = -1.0F;
  static constexpr float BALL_DELAY_MAX = 10.0F;
  static constexpr float BALL_SPEED_MIN = 2.0F;
  static constexpr float BALL_SPEED_MAX = 25.0F;
  static constexpr float BALL_ANGLE_MAX = 360.0F;
  static constexpr float BALL_LIFT_MAX = 6.0F;
  static constexpr float DEFAULT_RUN_UP = 20.0F;
  static constexpr float DEFAULT_PACE = 70.0F;
  static constexpr float DEFAULT_HEIGHT = 180.0F;
  static constexpr float DEFAULT_STRENGTH = 50.0F;
  static constexpr float DEFAULT_BALANCE = 50.0F;
  static constexpr float DEFAULT_AGILITY = 50.0F;
  static constexpr float DEFAULT_ANGLE_SETTING = 180.0F;
  static constexpr float DEFAULT_BALL_SPEED = 12.0F;
  static constexpr float DEFAULT_BALL_ANGLE = 90.0F;
  static constexpr float HALF_TURN_DEGREES = 180.0F;
  static constexpr float PERCENT_SCALE = 100.0F;
  static constexpr double PERCENT_SCALE_DOUBLE = 100.0;
  static constexpr double RELEASE_TOLERANCE_SECONDS = 1e-6;
  static constexpr float MEETING_MARKER_RADIUS = 0.15F;
  static constexpr float CONTACT_MARKER_RADIUS = 0.2F;
  static constexpr float BALANCE_RING_RADIUS = 0.6F;
  static constexpr float BALANCE_RING_WIDTH = 2.5F;
  static constexpr float TRAIL_WIDTH = 2.0F;
  static constexpr float RUN_TIME_UPPER_SECONDS = 30.0F;
  static constexpr int RUN_TIME_SEARCH_PASSES = 40;
  static constexpr float MIDPOINT_SHARE = 0.5F;
  static constexpr float PITCH_CENTRE_SHARE = 0.5F;
  static constexpr float A_RUN_UP_METRES = 30.0F;

 public:
  [[nodiscard]] const char* name() const override { return "Collision"; }
  [[nodiscard]] const char* summary() const override
  {
    return "Two players run into each other: A sprints through a meeting "
           "point after his run-up, B from his own run-up (none: he stands "
           "there) at an angle to A's run. Each one's speed before and "
           "after, his jolt, where he ends up against what he can run that "
           "way, how sudden it was, and whether he kept his balance (a "
           "pause), stumbled (amber ring) or fell (red ring while down: "
           "a big hit, which way he lies, how long until he is up).";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    const auto runner = [&](const char* section, Physique& physique)
    {
      DrillParameter runUp =
          setting("Run-up (m)", &physique.runUp,
                  {.min = 0.0F, .max = RUN_UP_MAX}, false,
                  "How far he runs, flat out from standing, "
                  "to the meeting point (0: he stands on it).");
      runUp.section = section;
      list.push_back(runUp);
      list.push_back(setting("Pace", &physique.pace,
                             {.min = 1.0F, .max = PACE_MAX}, true));
      list.push_back(setting("Height (cm)", &physique.height,
                             {.min = HEIGHT_MIN, .max = HEIGHT_MAX}, true));
      list.push_back(
          setting("Strength", &physique.strength,
                  {.min = 1.0F, .max = STRENGTH_MAX}, true,
                  "With his height, his weight (BMI 17 at 1 to 31 at "
                  "99)."));
      list.push_back(setting("Balance", &physique.balance,
                             {.min = 1.0F, .max = BALANCE_MAX}, true,
                             "Where his stumble and fall lines sit, and how "
                             "much the outcome wobbles."));
      list.push_back(setting("Agility", &physique.agility,
                             {.min = 1.0F, .max = AGILITY_MAX}, true,
                             "His hardest braking (how sudden a jolt is for "
                             "him) and how fast his body turns."));
    };
    runner("A", a);
    runner("B", b);
    // Names must be unique for presets and sweeps: label the B copies.
    constexpr std::array<const char*, 6> B_NAMES{"B run-up (m)",  "B Pace",
                                                 "B height (cm)", "B Strength",
                                                 "B Balance",     "B Agility"};
    for (std::size_t index = 0; index < B_NAMES.size(); ++index)
    {
      DrillParameter& parameter = list[B_NAMES.size() + index];
      parameter.label = parameter.name;
      parameter.name = B_NAMES.at(index);
    }
    DrillParameter angle =
        setting("B's run against A's (deg)", &angleSetting,
                {.min = 0.0F, .max = ANGLE_SETTING_MAX}, true,
                "180: head-on; 90: across A's run from his "
                "right; 0: the same way (from behind).");
    angle.section = "The meeting";
    list.push_back(angle);
    list.push_back(setting("Miss by (m)", &missSetting,
                           {.min = MISS_SETTING_MIN, .max = MISS_SETTING_MAX},
                           false,
                           "B's line moved this far to his left: 0 centre to "
                           "centre, 0.4 only just touching."));
    list.push_back(setting("Timing nudge (s)", &nudgeSetting,
                           {.min = NUDGE_SETTING_MIN, .max = 1.0F}, false,
                           "B starts this much later (+) or earlier (-) than "
                           "the drill's estimate of when they meet."));
    DrillParameter ballIn =
        setting("Ball in after (s)", &ballDelay,
                {.min = 0.0F, .max = BALL_DELAY_MAX}, false,
                "This long after the contact a loose ball is "
                "played at whoever is down (A first) from "
                "10 m away; 0: no ball.");
    ballIn.section = "Ball at the fallen";
    list.push_back(ballIn);
    list.push_back(setting("Ball speed (m/s)", &ballSpeed,
                           {.min = BALL_SPEED_MIN, .max = BALL_SPEED_MAX},
                           false));
    list.push_back(setting("Ball from (deg)", &ballAngle,
                           {.min = 0.0F, .max = BALL_ANGLE_MAX}, true,
                           "Where it comes from round him: 0 from +x (the "
                           "way A ran), 90 from +y."));
    list.push_back(setting("Ball lift (m/s)", &ballLift,
                           {.min = 0.0F, .max = BALL_LIFT_MAX}, false,
                           "Upward speed: a ball above 0.5 m (centre) goes "
                           "over him."));
    return list;
  }

  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    constexpr float PRESET_SPRINTER_INTO_A_MAN_STANDING_RUN_UP_M = 30.0F;
    constexpr float PRESET_HEAD_ON_SPRINTERS_RUN_UP_M = 30.0F;
    constexpr float PRESET_HEAD_ON_SPRINTERS_B_RUN_UP_M = 30.0F;
    constexpr float PRESET_JOGGERS_HEAD_ON_RUN_UP_M = 0.9F;
    constexpr float PRESET_JOGGERS_HEAD_ON_B_RUN_UP_M = 0.9F;
    constexpr float PRESET_LIGHT_WINGER_INTO_A_BIG_DEFENDER_RUN_UP_M = 20.0F;
    constexpr float PRESET_LIGHT_WINGER_INTO_A_BIG_DEFENDER_HEIGHT_CM = 170.0F;
    constexpr float PRESET_LIGHT_WINGER_INTO_A_BIG_DEFENDER_STRENGTH = 20.0F;
    constexpr float PRESET_LIGHT_WINGER_INTO_A_BIG_DEFENDER_B_HEIGHT_CM =
        192.0F;
    constexpr float PRESET_LIGHT_WINGER_INTO_A_BIG_DEFENDER_B_STRENGTH = 90.0F;
    constexpr float PRESET_CLIPPED_FROM_THE_SIDE_RUN_UP_M = 30.0F;
    constexpr float PRESET_CLIPPED_FROM_THE_SIDE_B_RUN_UP_M = 6.0F;
    constexpr float PRESET_CLIPPED_FROM_THE_SIDE_B_S_RUN_AGAINST_A_S_DEG =
        90.0F;
    constexpr float PRESET_GLANCING_HEAD_ON_RUN_UP_M = 20.0F;
    constexpr float PRESET_GLANCING_HEAD_ON_B_RUN_UP_M = 10.0F;
    constexpr float PRESET_GLANCING_HEAD_ON_MISS_BY_M = 0.35F;
    constexpr float PRESET_BALL_INTO_A_FALLEN_MAN_RUN_UP_M = 30.0F;
    constexpr float PRESET_BALL_INTO_A_FALLEN_MAN_B_RUN_UP_M = 30.0F;
    constexpr float PRESET_BALL_INTO_A_FALLEN_MAN_BALL_SPEED_M_S = 12.0F;
    constexpr float PRESET_BALL_INTO_A_FALLEN_MAN_BALL_FROM_DEG = 90.0F;

    return {
        {.name = "Sprinter into a man standing",
         .summary = "A at full speed, B standing facing him.",
         .settings = {{"Run-up (m)",
                       PRESET_SPRINTER_INTO_A_MAN_STANDING_RUN_UP_M},
                      {"B run-up (m)", 0.0F},
                      {"B's run against A's (deg)", HALF_TURN_DEGREES}}},
        {.name = "Head-on sprinters",
         .summary = "Both at full speed, straight into each other.",
         .settings = {{"Run-up (m)", PRESET_HEAD_ON_SPRINTERS_RUN_UP_M},
                      {"B run-up (m)", PRESET_HEAD_ON_SPRINTERS_B_RUN_UP_M},
                      {"B's run against A's (deg)", HALF_TURN_DEGREES}}},
        {.name = "Joggers head-on",
         .summary = "Short run-ups: both at about 3 m/s.",
         .settings = {{"Run-up (m)", PRESET_JOGGERS_HEAD_ON_RUN_UP_M},
                      {"B run-up (m)", PRESET_JOGGERS_HEAD_ON_B_RUN_UP_M},
                      {"B's run against A's (deg)", HALF_TURN_DEGREES}}},
        {.name = "Light winger into a big defender",
         .summary = "A slight 1.70 m into a strong 1.92 m, standing.",
         .settings = {{"Run-up (m)",
                       PRESET_LIGHT_WINGER_INTO_A_BIG_DEFENDER_RUN_UP_M},
                      {"Height (cm)",
                       PRESET_LIGHT_WINGER_INTO_A_BIG_DEFENDER_HEIGHT_CM},
                      {"Strength",
                       PRESET_LIGHT_WINGER_INTO_A_BIG_DEFENDER_STRENGTH},
                      {"B run-up (m)", 0.0F},
                      {"B height (cm)",
                       PRESET_LIGHT_WINGER_INTO_A_BIG_DEFENDER_B_HEIGHT_CM},
                      {"B Strength",
                       PRESET_LIGHT_WINGER_INTO_A_BIG_DEFENDER_B_STRENGTH}}},
        {.name = "Clipped from the side",
         .summary = "B runs across A's sprint.",
         .settings = {{"Run-up (m)", PRESET_CLIPPED_FROM_THE_SIDE_RUN_UP_M},
                      {"B run-up (m)", PRESET_CLIPPED_FROM_THE_SIDE_B_RUN_UP_M},
                      {"B's run against A's (deg)",
                       PRESET_CLIPPED_FROM_THE_SIDE_B_S_RUN_AGAINST_A_S_DEG}}},
        {.name = "Glancing head-on",
         .summary = "Head-on, the lines 0.35 m apart (of a 0.4 m touch).",
         .settings = {{"Run-up (m)", PRESET_GLANCING_HEAD_ON_RUN_UP_M},
                      {"B run-up (m)", PRESET_GLANCING_HEAD_ON_B_RUN_UP_M},
                      {"B's run against A's (deg)", HALF_TURN_DEGREES},
                      {"Miss by (m)", PRESET_GLANCING_HEAD_ON_MISS_BY_M}}},
        {.name = "Ball into a fallen man",
         .summary =
             "Head-on sprinters; 2 s on, a ball rolled into A from the side.",
         .settings = {{"Run-up (m)", PRESET_BALL_INTO_A_FALLEN_MAN_RUN_UP_M},
                      {"B run-up (m)",
                       PRESET_BALL_INTO_A_FALLEN_MAN_B_RUN_UP_M},
                      {"B's run against A's (deg)", HALF_TURN_DEGREES},
                      {"Ball in after (s)", FALLEN_BALL_DELAY_SECONDS},
                      {"Ball speed (m/s)",
                       PRESET_BALL_INTO_A_FALLEN_MAN_BALL_SPEED_M_S},
                      {"Ball from (deg)",
                       PRESET_BALL_INTO_A_FALLEN_MAN_BALL_FROM_DEG},
                      {"Ball lift (m/s)", 0.0F}}},
    };
  }

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    const auto spec = [](const char* name, bool home, const Physique& physique)
    {
      DrillPlayerSpec player{
          .name = name,
          .role = PlayerRole::CM,
          .homeTeam = home,
          .stats = {{"Pace", physique.pace}},
          .height = static_cast<int>(std::round(physique.height)),
          .derived = {}};
      player.derived[DerivedStat::STRENGTH] = physique.strength;
      player.derived[DerivedStat::BALANCE] = physique.balance;
      player.derived[DerivedStat::AGILITY] = physique.agility;
      return player;
    };
    return {spec("A", true, a), spec("B", false, b)};
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    MatchEngine::DrillRules rules = engine.getDrillRules();
    rules.chaseEffort = true;
    engine.setDrillRules(rules);
    aId = ids[0];
    bId = ids[1];
    const float radians = angleSetting * PI / HALF_TURN_DEGREES;
    directionA = {.x = 1.0F, .y = 0.0F};
    directionB = {.x = std::cos(radians), .y = std::sin(radians)};
    meetA = MEET;
    meetB = along(MEET, {.x = -directionB.y, .y = directionB.x}, missSetting);
    startA = along(meetA, directionA, -a.runUp);
    startB = along(meetB, directionB, -b.runUp);
    MatchScenario scenario;
    scenario.players.push_back(
        {.playerId = aId, .position = startA, .makingRun = false});
    scenario.players.push_back(
        {.playerId = bId, .position = startB, .makingRun = false});
    scenario.ballPosition = ballOutOfTheWay();
    engine.startDrill(scenario);
    // When each sets off: the later arrival first, so they meet.
    const float timeA =
        a.runUp > 0.0F ? runTime(engine, {.player = aId, .metres = a.runUp})
                       : 0.0F;
    const float timeB =
        b.runUp > 0.0F ? runTime(engine, {.player = bId, .metres = b.runUp})
                       : 0.0F;
    releaseA.reset();
    releaseB.reset();
    if (a.runUp > 0.0F)
    {
      releaseA = std::max(0.0F, timeB - timeA - nudgeSetting);
    }
    if (b.runUp > 0.0F)
    {
      releaseB = std::max(0.0F, timeA - timeB + nudgeSetting);
    }
    // Until then each waits on his start, facing his run.
    engine.setDrillTarget(aId, startA, false, {}, 0.0F, 0.0F,
                          facingOf(directionA));
    engine.setDrillTarget(bId, startB, false, {}, 0.0F, 0.0F,
                          facingOf(directionB));
    released = {false, false};
    hits.clear();
    first.reset();
    ended = false;
    ring = {};
    wentDown = {};
    upAt = {};
    ballFedAt.reset();
    ballAt = 0;
    ballOff.reset();
    ballSpeedBefore = 0.0F;
    weights = {weightOf(engine, aId), weightOf(engine, bId)};
    trailA.clear();
    trailB.clear();
  }

 private:
  void observeCollisions(MatchEngine& engine, double now)
  {
    const auto release = [&](std::size_t which, const std::optional<float>& at,
                             PlayerID id, PitchPoint meet, Vector2F direction)
    {
      if (released.at(which) || !at || now + RELEASE_TOLERANCE_SECONDS < *at)
      {
        return;
      }
      released.at(which) = true;
      engine.setDrillTarget(id, along(meet, direction, BEYOND_METRES), true, {},
                            0.0F, 0.0F, std::nullopt, true);
    };
    release(0, releaseA, aId, meetA, directionA);
    release(1, releaseB, bId, meetB, directionB);
    for (const MatchEngine::CollisionHit& hit : engine.getLastCollisions())
    {
      const bool ours =
          (hit.sides.at(0).player == aId || hit.sides.at(1).player == aId) &&
          (hit.sides.at(0).player == bId || hit.sides.at(1).player == bId);
      if (!ours)
      {
        continue;
      }
      hits.push_back(hit);
      if (!first)
      {
        first = hit;
        firstAt = now - STEP + hit.atSeconds;
      }
    }
  }

  void observeFallenBall(MatchEngine& engine,
                         const std::array<const MatchPlayer*, 2>& players,
                         double now)
  {
    const MatchPlayer* playerA = players.at(0);
    const MatchPlayer* playerB = players.at(1);
    // The ball at whoever is down (A first), and what it does off him.
    const MatchBall& ballNow = engine.getBall();
    const int offCount = engine.getStats().collisionBallOffFallen;
    if (ballFedAt && !ballOff && offCount > ballOffAtFeed)
    {
      ballOff = BallOff{
          .who = (ballNow.possessionState().deflectedBy != nullptr) &&
                         ballNow.possessionState().deflectedBy->getId() == aId
                     ? 'A'
                     : 'B',
          .before = ballSpeedBefore,
          .speed = length2(ballNow.velocity),
          .after = now - *ballFedAt};
    }
    ballSpeedBefore = length2(ballNow.velocity);
    if (ballDelay > 0.0F && first && !ballFedAt && now >= firstAt + ballDelay)
    {
      const MatchPlayer* target = firstFallen({playerA, playerB});
      ballFedAt = now;
      ballAt = targetNumber(target, {playerA, playerB});
      if (target != nullptr)
      {
        const float radians = ballAngle * PI / HALF_TURN_DEGREES;
        const Vector2F from{.x = std::cos(radians), .y = std::sin(radians)};
        ballOffAtFeed = offCount;
        engine.feedBall(along(target->position, from, BALL_FROM_METRES),
                        {.x = -from.x * ballSpeed, .y = -from.y * ballSpeed},
                        ballLift, 0);
      }
    }
  }

 public:
  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    observeCollisions(engine, now);
    const MatchPlayer* playerA = slotOf(engine, aId);
    const MatchPlayer* playerB = slotOf(engine, bId);
    if ((playerA == nullptr) || (playerB == nullptr))
    {
      return false;
    }
    if (engine.getSimulatedSteps() % 2 == 0)
    {
      trailA.push_back(playerA->position);
      trailB.push_back(playerB->position);
    }
    // A ring round each while he stumbles (amber) or is down (red).
    for (std::size_t which = 0; which < 2; ++which)
    {
      const MatchPlayer& player = which == 0 ? *playerA : *playerB;
      at.at(which) = player.position;
      ring.at(which) = balanceRing(player);
      if (player.fallen)
      {
        wentDown.at(which) = true;
      }
      if (wentDown.at(which) && !player.fallen && !upAt.at(which))
      {
        upAt.at(which) = now;
      }
    }
    observeFallenBall(engine, {playerA, playerB}, now);
    // On until both are on their feet again (and the ball has had time).
    const bool bothUp =
        (!wentDown.at(0) || upAt.at(0)) && (!wentDown.at(1) || upAt.at(1));
    const bool ballDone = ballDelay <= 0.0F ||
                          (ballFedAt && now > *ballFedAt + BALL_WATCH_SECONDS);
    ended = (first && bothUp && ballDone && now > firstAt + AFTER_SECONDS) ||
            now >= TIME_LIMIT_SECONDS;
    return !ended;
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::path(renderer, draw, trailA, RUNNER_TRAIL_COLOUR,
                    TRAIL_WIDTH * scale);
    DrillDraw::path(renderer, draw, trailB, SECOND_RUNNER_TRAIL_COLOUR,
                    TRAIL_WIDTH * scale);
    DrillDraw::marker(renderer, draw, MEET, MEETING_MARKER_RADIUS,
                      MEETING_COLOUR);
    if (first)
    {
      DrillDraw::marker(renderer, draw, first->where, CONTACT_MARKER_RADIUS,
                        CONTACT_COLOUR);
    }
    // Stumbling: amber; down: red.
    for (std::size_t which = 0; which < 2; ++which)
    {
      if (ring.at(which) > 0)
      {
        DrillDraw::circle(renderer, draw, at.at(which), BALANCE_RING_RADIUS, 0,
                          ring.at(which) == 2 ? FALLEN_COLOUR : STUMBLE_COLOUR,
                          BALANCE_RING_WIDTH * scale);
      }
    }
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "A: {:.0f} m run-up, {:.0f} kg · B: {:.0f} m run-up, {:.0f} kg · B's "
        "run "
        "at {:.0f} deg, {:+.2f} m aside",
        a.runUp, weights.at(0), b.runUp, weights.at(1), angleSetting,
        missSetting));
    if (!first)
    {
      lines.emplace_back(ended ? "OUTCOME  no contact: they missed each other"
                               : "No contact yet.");
      return lines;
    }
    for (const PlayerID id : {aId, bId})
    {
      const MatchEngine::CollisionSide& side = sideOf(*first, id);
      lines.push_back(std::format(
          "{}: {:.1f} -> {:.1f} m/s, jolt {:.2f} m/s · ends up {:.0f}% · "
          "sudden "
          "{:.0f}%",
          id == aId ? "A" : "B", speedBefore(side), length2(side.after),
          length2(side.jolt), side.endsUp * PERCENT_SCALE,
          side.sudden * PERCENT_SCALE));
      lines.push_back(std::format(
          "   score {:.0f}% (lines {:.0f}% / {:.0f}%): {}{}{}",
          side.score * PERCENT_SCALE, side.stumbleLine * PERCENT_SCALE,
          side.fallLine * PERCENT_SCALE, OUTCOMES.at(side.outcome),
          side.slipped ? ", a stumble that slipped" : "",
          pauseDescription(side)));
      if (side.outcome == 2)
      {
        const std::size_t which = id == aId ? 0 : 1;
        lines.emplace_back(std::format(
            "   lies {} his slide{} · {}",
            side.lyingAcross ? "across" : "along",
            side.bigHit ? std::format(" · a big hit: down at least {:.1f} s",
                                      side.downAtLeast)
                        : std::string{},
            upAt.at(which)
                ? std::format("up after {:.1f} s", *upAt.at(which) - firstAt)
                : std::string("still down")));
      }
    }
    if (ballFedAt)
    {
      lines.push_back(ballDescription());
    }
    lines.push_back(
        std::format("Run-ins in all: {} (after the first: driving on "
                    "into each other)",
                    hits.size()));
    lines.push_back(std::format("OUTCOME  contact at {:.2f} s: A {}, B {}",
                                firstAt,
                                OUTCOMES.at(sideOf(*first, aId).outcome),
                                OUTCOMES.at(sideOf(*first, bId).outcome)));
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (!first)
    {
      return {{.name = "Contact", .value = 0.0}};
    }
    std::vector<DrillMetric> out{{.name = "Contact", .value = 1.0},
                                 {.name = "Contact at (s)", .value = firstAt}};
    for (const PlayerID id : {aId, bId})
    {
      const MatchEngine::CollisionSide& side = sideOf(*first, id);
      const std::string who = id == aId ? "A " : "B ";
      out.push_back(
          {.name = who + "speed before (m/s)", .value = speedBefore(side)});
      out.push_back(
          {.name = who + "speed after (m/s)", .value = length2(side.after)});
      out.push_back({.name = who + "jolt (m/s)", .value = length2(side.jolt)});
      out.push_back({.name = who + "ends up (%)",
                     .value = side.endsUp * PERCENT_SCALE_DOUBLE});
      out.push_back({.name = who + "sudden (%)",
                     .value = side.sudden * PERCENT_SCALE_DOUBLE});
      out.push_back({.name = who + "score (%)",
                     .value = side.score * PERCENT_SCALE_DOUBLE});
      out.push_back(
          {.name = who + "stumbled", .value = side.outcome == 1 ? 1.0 : 0.0});
      out.push_back(
          {.name = who + "fell", .value = side.outcome == 2 ? 1.0 : 0.0});
      out.push_back(
          {.name = who + "slipped", .value = side.slipped ? 1.0 : 0.0});
      out.push_back({.name = who + "pause (s)", .value = side.pause});
      out.push_back({.name = who + "stumble (s)", .value = side.stumble});
      out.push_back(
          {.name = who + "big hit", .value = side.bigHit ? 1.0 : 0.0});
      out.push_back(
          {.name = who + "lies across",
           .value = side.outcome == 2 && side.lyingAcross ? 1.0 : 0.0});
      const std::size_t which = id == aId ? 0 : 1;
      out.push_back(
          {.name = who + "down (s)",
           .value = upAt.at(which) ? *upAt.at(which) - firstAt : 0.0});
    }
    out.push_back(
        {.name = "Run-ins", .value = static_cast<double>(hits.size())});
    out.push_back(
        {.name = "Ball off the fallen", .value = ballOff ? 1.0 : 0.0});
    out.push_back({.name = "Ball speed in (m/s)",
                   .value = ballOff ? ballOff->before : 0.0});
    out.push_back({.name = "Ball speed off (m/s)",
                   .value = ballOff ? ballOff->speed : 0.0});
    return out;
  }

 private:
  struct Physique
  {
    float runUp = DEFAULT_RUN_UP;
    float pace = DEFAULT_PACE;
    float height = DEFAULT_HEIGHT;
    float strength = DEFAULT_STRENGTH;
    float balance = DEFAULT_BALANCE;
    float agility = DEFAULT_AGILITY;
  };
  static constexpr std::array<const char*, 3> OUTCOMES{
      "kept his feet and balance", "stumbled", "fell"};
  static inline const PitchPoint MEET =
      at(PITCH_CENTRE_SHARE, PITCH_CENTRE_SHARE);
  /** Each runs on through the meeting point to this far beyond it. */
  static constexpr float BEYOND_METRES = 20.0F;
  /** The run goes on at least this long after the first contact, and until
   * anyone knocked over is up. */
  static constexpr double AFTER_SECONDS = 2.0;
  static constexpr double TIME_LIMIT_SECONDS = 45.0;
  /** The ball comes from this far away and is watched this long. */
  static constexpr float BALL_FROM_METRES = 10.0F;
  static constexpr double BALL_WATCH_SECONDS = 3.0;

  static float length2(Vector2F vector)
  {
    return std::hypot(vector.x, vector.y);
  }
  static float speedBefore(const MatchEngine::CollisionSide& side)
  {
    return std::hypot(side.after.x - side.jolt.x, side.after.y - side.jolt.y);
  }
  static const MatchEngine::CollisionSide& sideOf(
      const MatchEngine::CollisionHit& hit, PlayerID id)
  {
    return hit.sides.at(0).player == id ? hit.sides.at(0) : hit.sides.at(1);
  }
  /** A direction in metres as a facing angle (radians). */
  static float facingOf(Vector2F direction)
  {
    return std::atan2(direction.y, direction.x);
  }
  static float weightOf(const MatchEngine& engine, PlayerID id)
  {
    const MatchPlayer* player = slotOf(engine, id);
    return (player != nullptr)
               ? bodyMassKilograms(player->heightMetres,
                                   player->derivedStat(DerivedStat::STRENGTH))
               : 0.0F;
  }
  /** About how long he takes to run `metres` flat out from standing:
   * a(v) = A0 (1 - v / vmax), so x(t) = vmax (t - tau (1 - e^(-t/tau))). */
  static float runTime(const MatchEngine& engine, RunnerRun run)
  {
    const PlayerID id = run.player;
    const float metres = run.metres;
    const MatchPlayer* player = slotOf(engine, id);
    if (player == nullptr)
    {
      return 0.0F;
    }
    const float top = std::max(player->maxSpeed, 1.0F);
    const float push = std::max(
        derivedPhysicalValue(DerivedStat::ACCELERATION,
                             player->derivedStat(DerivedStat::ACCELERATION)),
        1.0F);
    const float tau = top / push;
    const auto distance = [&](float time)
    { return top * (time - tau * (1.0F - std::exp(-time / tau))); };
    float low = 0.0F;
    float high = RUN_TIME_UPPER_SECONDS;
    for (int pass = 0; pass < RUN_TIME_SEARCH_PASSES; ++pass)
    {
      const float middle = MIDPOINT_SHARE * (low + high);
      (distance(middle) < metres ? low : high) = middle;
    }
    return MIDPOINT_SHARE * (low + high);
  }

  Physique a{.runUp = A_RUN_UP_METRES,
             .pace = DEFAULT_PACE,
             .height = DEFAULT_HEIGHT,
             .strength = DEFAULT_STRENGTH,
             .balance = DEFAULT_BALANCE,
             .agility = DEFAULT_AGILITY};
  Physique b{.runUp = 0.0F,
             .pace = DEFAULT_PACE,
             .height = DEFAULT_HEIGHT,
             .strength = DEFAULT_STRENGTH,
             .balance = DEFAULT_BALANCE,
             .agility = DEFAULT_AGILITY};
  float angleSetting = DEFAULT_ANGLE_SETTING;
  float missSetting = 0.0F;
  float nudgeSetting = 0.0F;

  PlayerID aId = 0;
  PlayerID bId = 0;
  Vector2F directionA{};
  Vector2F directionB{};
  PitchPoint meetA;
  PitchPoint meetB;
  PitchPoint startA;
  PitchPoint startB;
  std::optional<float> releaseA;
  std::optional<float> releaseB;
  std::array<bool, 2> released{};
  std::array<float, 2> weights{};
  std::vector<MatchEngine::CollisionHit> hits;
  std::optional<MatchEngine::CollisionHit> first;
  double firstAt = 0.0;
  bool ended = false;
  std::array<PitchPoint, 2> at{};
  /** 0 no ring; 1 stumbling; 2 down. */
  std::array<std::uint8_t, 2> ring{};
  /** Whether each went down, and when he was up again (drill seconds). */
  std::array<bool, 2> wentDown{};
  std::array<std::optional<double>, 2> upAt{};
  /** The ball: its settings, when it was played and at whom (0 nobody, 1
   * A, 2 B), and what it did off him. */
  float ballDelay = 0.0F;
  float ballSpeed = DEFAULT_BALL_SPEED;
  float ballAngle = DEFAULT_BALL_ANGLE;
  float ballLift = 0.0F;
  std::optional<double> ballFedAt;
  int ballAt = 0;
  int ballOffAtFeed = 0;
  float ballSpeedBefore = 0.0F;
  struct BallOff
  {
    char who = 'A';
    float before = 0.0F;
    float speed = 0.0F;
    double after = 0.0;
  };
  std::optional<BallOff> ballOff;
  std::vector<PitchPoint> trailA;
  std::vector<PitchPoint> trailB;
};

// --- Shot from a spot --------------------------------------------------------

/**
 * Shot from a spot: a shooter on the ball at a spot, with an optional
 * keeper and an optional defender in the way. He shoots at once (through
 * the AI's own shot) or decides for himself (choice mode). The outcome
 * against the engine's own xG for the shot.
 */
class ShotDrill final : public Drill
{
  void observeShotEvents(const MatchEngine& engine, double now)
  {
    const auto& events = engine.getEvents();
    for (; seenEvents < events.size() && outcome.empty(); ++seenEvents)
    {
      const MatchEvent& event = events[seenEvents];
      if (event.type == MatchEventType::SHOT && !shot &&
          event.primaryPlayerId == shooterId)
      {
        const MatchBall& ball = engine.getBall();
        shot = Shot{.from = event.position,
                    .xg = event.xg,
                    .onTarget = ball.possessionState().shotOnTarget,
                    .targetY = ball.possessionState().shotTargetY,
                    .at = now};
        continue;
      }
      if (!shot)
      {
        // Before any shot: the ball went out or play stopped.
        if (event.type == MatchEventType::GOAL_KICK ||
            event.type == MatchEventType::CORNER ||
            event.type == MatchEventType::THROW_IN ||
            event.type == MatchEventType::FOUL)
        {
          outcome = "no shot (play stopped)";
        }
        continue;
      }
      switch (event.type)
      {
        case MatchEventType::GOAL:
        case MatchEventType::OWN_GOAL:
          outcome = "goal";
          break;
        case MatchEventType::SAVE:
          outcome = "saved";
          break;
        case MatchEventType::WOODWORK:
          outcome = "woodwork";
          break;
        case MatchEventType::SHOT_BLOCKED:
          outcome = "blocked";
          break;
        // The engine logs no "off target": a miss goes out of play.
        case MatchEventType::GOAL_KICK:
        case MatchEventType::CORNER:
        case MatchEventType::THROW_IN:
          outcome = "off target";
          break;
        default:
          break;
      }
    }
  }

  [[nodiscard]] ImU32 shotColour() const
  {
    if (outcome == "goal")
    {
      return SUCCESS_COLOUR;
    }
    if (outcome == "saved")
    {
      return CONTROLLED_BALL_COLOUR;
    }
    if (outcome.empty())
    {
      return PENDING_COLOUR;
    }
    return MISSED_COLOUR;
  }

  static constexpr double FORCED_SHOT_TIMEOUT_SECONDS = 8.0;
  static constexpr double DECISION_TIMEOUT_SECONDS = 15.0;
  static constexpr double SHOT_WATCH_SECONDS = 6.0;
  static constexpr double RESULT_LINGER_SECONDS = 0.5;
  static constexpr float LINE_MAX = 45.0F;
  static constexpr float SIDE_MIN = -30.0F;
  static constexpr float SIDE_MAX = 30.0F;
  static constexpr float SHOOTING_MAX = 100.0F;
  static constexpr float GOALKEEPING_MAX = 100.0F;
  static constexpr float KEEPER_HEIGHT_MIN = 165.0F;
  static constexpr float KEEPER_HEIGHT_MAX = 205.0F;
  static constexpr float DEFENDER_DISTANCE_MIN = 0.5F;
  static constexpr float DEFENDER_DISTANCE_MAX = 15.0F;
  static constexpr float DEFENDING_MAX = 100.0F;
  static constexpr float DEFAULT_LINE = 16.0F;
  static constexpr float DEFAULT_SIDE = 4.0F;
  static constexpr float DEFAULT_SHOOTING = 70.0F;
  static constexpr float DEFAULT_GOALKEEPING = 70.0F;
  static constexpr float DEFAULT_KEEPER_HEIGHT = 188.0F;
  static constexpr float DEFAULT_DEFENDER_DISTANCE = 3.0F;
  static constexpr float DEFAULT_DEFENDING = 70.0F;
  /** The goal kinds after NONE, in GoalKind order. */
  static constexpr std::array<const char*, 7> GOAL_CHOICES{
      "Full size", "9v9",          "7v7",   "Futsal",
      "5-a-side",  "Small framed", "Pop-up"};
  static constexpr ImU32 RUNNER_PATH_COLOUR = IM_COL32(255, 255, 255, 120);
  static constexpr ImU32 SUCCESS_COLOUR = IM_COL32(90, 220, 120, 255);
  static constexpr ImU32 DEFENDER_COLOUR = IM_COL32(240, 80, 80, 200);
  static constexpr ImU32 CONTROLLED_BALL_COLOUR = IM_COL32(90, 170, 255, 255);
  static constexpr ImU32 PENDING_COLOUR = IM_COL32(255, 255, 255, 220);
  static constexpr ImU32 MISSED_COLOUR = IM_COL32(240, 140, 60, 255);
  static constexpr int SHOOTER_HEIGHT_CM = 180;
  static constexpr int DEFENDER_HEIGHT_CM = 185;

 public:
  static constexpr float KEEPER_OFF_LINE_METRES = 1.5F;

  [[nodiscard]] const char* name() const override { return "Shot"; }
  [[nodiscard]] const char* summary() const override
  {
    return "A shooter on the ball at a spot, against a keeper and a "
           "defender if you like. He shoots at once, or decides for himself "
           "(where does he choose to shoot from?). The outcome against the "
           "engine's own xG for the shot. The Shot map runs a grid of spots "
           "for the calibration heatmap.";
  }

  [[nodiscard]] bool offersShotMap() const override { return true; }

  /** The match pitch with the chosen goal at each end. */
  [[nodiscard]] MatchPitch pitch() const override
  {
    MatchPitch on = MatchPitch::matchSize();
    on.goal = PitchGoal::ofKind(
        static_cast<GoalKind>(static_cast<int>(std::lround(goalChoice)) + 1));
    return on;
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Out from goal line (m)", &line,
                           {.min = 1.0F, .max = LINE_MAX}, false,
                           "Distance of the spot from the goal line."));
    list.push_back(setting("Off centre (m)", &side,
                           {.min = SIDE_MIN, .max = SIDE_MAX}, false,
                           "Sideways from the middle of the goal (+ towards "
                           "the bottom touchline)."));
    DrillParameter goals = setting(
        "Goals", &goalChoice,
        {.min = 0.0F, .max = static_cast<float>(GOAL_CHOICES.size() - 1)}, true,
        "The goal's size, on the match pitch (game xG plan: the empty-goal "
        "sweep measures each).");
    goals.choices.assign(GOAL_CHOICES.begin(), GOAL_CHOICES.end());
    list.push_back(goals);
    DrillParameter mode =
        setting("Shooter", &choice, {.min = 0.0F, .max = 1.0F}, true,
                "Shoots now: through the AI's own shot, at "
                "the first moment he can.\nDecides: the AI "
                "plays on (carries, shoots when it wants).");
    mode.choices = {"Shoots now", "Decides"};
    list.push_back(mode);
    list.push_back(setting("Shooting", &shooting,
                           {.min = 1.0F, .max = SHOOTING_MAX}, true));
    DrillParameter keeper =
        setting("Keeper", &keeperChoice, {.min = 0.0F, .max = 1.0F}, true);
    keeper.choices = {"None", "In goal"};
    list.push_back(keeper);
    list.push_back(setting("Goalkeeping", &goalkeeping,
                           {.min = 1.0F, .max = GOALKEEPING_MAX}, true));
    DrillParameter start = setting(
        "Keeper starts", &keeperStart, {.min = 0.0F, .max = 1.0F}, true,
        "Covering the angle: 1.5 m off his line towards the ball, as a keeper "
        "sets himself.\nCentre of goal: on his line in the middle (as if he "
        "had no time to set).");
    start.choices = {"Covering the angle", "Centre of goal"};
    list.push_back(start);
    list.push_back(setting("Keeper height (cm)", &keeperHeight,
                           {.min = KEEPER_HEIGHT_MIN, .max = KEEPER_HEIGHT_MAX},
                           true));
    DrillParameter defender =
        setting("Defender", &defenderChoice, {.min = 0.0F, .max = 1.0F}, true,
                "A defender on the line from the spot to the middle of the "
                "goal.");
    defender.choices = {"None", "In the way"};
    list.push_back(defender);
    list.push_back(setting(
        "Defender distance (m)", &defenderDistance,
        {.min = DEFENDER_DISTANCE_MIN, .max = DEFENDER_DISTANCE_MAX}, false));
    list.push_back(setting("Defending", &defending,
                           {.min = 1.0F, .max = DEFENDING_MAX}, true));
    return list;
  }

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    std::vector<DrillPlayerSpec> list{{.name = "Shooter",
                                       .role = PlayerRole::ST,
                                       .homeTeam = true,
                                       .stats = {{"Shooting", shooting}},
                                       .height = SHOOTER_HEIGHT_CM,
                                       .derived = {}}};
    if (keeperChoice > CHOICE_MIDPOINT)
    {
      list.push_back({.name = "Keeper",
                      .role = PlayerRole::GK,
                      .homeTeam = false,
                      .stats = {{"Goalkeeping", goalkeeping}},
                      .height = static_cast<int>(keeperHeight),
                      .derived = {}});
    }
    if (defenderChoice > CHOICE_MIDPOINT)
    {
      list.push_back({.name = "Defender",
                      .role = PlayerRole::CB,
                      .homeTeam = false,
                      .stats = {{"Defending", defending}},
                      .height = DEFENDER_HEIGHT_CM,
                      .derived = {}});
    }
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    shooterId = ids[0];
    hasKeeper = keeperChoice > CHOICE_MIDPOINT;
    hasDefender = defenderChoice > CHOICE_MIDPOINT;
    keeperId = hasKeeper ? ids[1] : 0;
    defenderId = hasDefender ? ids[hasKeeper ? 2 : 1] : 0;
    forced = choice < CHOICE_MIDPOINT;
    spot = at(std::clamp(1.0F - (line / LENGTH), PITCH_MIDPOINT_SHARE,
                         PITCH_EDGE_MAX_SHARE),
              std::clamp(PITCH_MIDPOINT_SHARE + (side / WIDTH),
                         PITCH_SAFE_MIN_SHARE, PITCH_SAFE_MAX_SHARE));
    MatchScenario scenario;
    scenario.players.push_back(
        {.playerId = shooterId, .position = spot, .makingRun = false});
    if (hasKeeper)
    {
      // Covering the angle: a step off the line towards the ball, as a
      // keeper sets himself; or the middle of the goal (no time to set).
      const PitchPoint goal = at(1.0F, PITCH_MIDPOINT_SHARE);
      keeperAt = keeperStart < CHOICE_MIDPOINT
                     ? along(goal, directionBetween(goal, spot),
                             std::min(KEEPER_OFF_LINE_METRES,
                                      metresBetween(goal, spot) *
                                          KEEPER_DISTANCE_SHARE))
                     : at(KEEPER_START_LINE_SHARE, PITCH_MIDPOINT_SHARE);
      scenario.players.push_back(
          {.playerId = keeperId, .position = keeperAt, .makingRun = false});
    }
    if (hasDefender)
    {
      const PitchPoint goal = at(1.0F, PITCH_MIDPOINT_SHARE);
      const float towards =
          std::min(defenderDistance, metresBetween(spot, goal) - 1.0F);
      defenderAt = along(spot, directionBetween(spot, goal),
                         std::max(MIN_SHOT_DEFENDER_GAP_METRES, towards));
      scenario.players.push_back(
          {.playerId = defenderId, .position = defenderAt, .makingRun = false});
    }
    scenario.carrierId = shooterId;
    scenario.ballPosition = spot;
    engine.startDrill(scenario);
    if (forced)
    {
      engine.forceShot(shooterId);
    }
    seenEvents = engine.getEvents().size();
    shot.reset();
    outcome.clear();
    endAt.reset();
    path.clear();
    path.push_back(spot);
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    if (const MatchPlayer* shooter = slotOf(engine, shooterId);
        (shooter != nullptr) && !shot && engine.getSimulatedSteps() % 2 == 0)
    {
      path.push_back(shooter->position);
    }
    observeShotEvents(engine, now);
    const MatchBall& ball = engine.getBall();
    if (outcome.empty() && shot && !ball.possessionState().isShot &&
        (engine.getPossessionAttachment() != nullptr) &&
        engine.getPossessionAttachment()->getId() == keeperId)
    {
      // Gathered with no event: a weak shot the keeper simply picks up.
      outcome = shot->onTarget ? "saved" : "off target";
    }
    if (outcome.empty() && !shot &&
        (engine.getPossessionAttachment() != nullptr) &&
        engine.getPossessionAttachment()->getId() != shooterId)
    {
      outcome = "no shot (lost the ball)";
    }
    if (outcome.empty() && !shot &&
        now >=
            (forced ? FORCED_SHOT_TIMEOUT_SECONDS : DECISION_TIMEOUT_SECONDS))
    {
      outcome = "no shot (time up)";
    }
    if (outcome.empty() && shot && now >= shot->at + SHOT_WATCH_SECONDS)
    {
      outcome = "no outcome";
    }
    if (!outcome.empty() && !endAt)
    {
      endAt = now;
      tally(outcome);
    }
    return (!endAt || now < *endAt + RESULT_LINGER_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::path(renderer, draw, path, RUNNER_PATH_COLOUR,
                    BALL_TRAIL_WIDTH_PIXELS * scale);
    DrillDraw::marker(renderer, draw, spot, LARGE_MARKER_RADIUS_PIXELS * scale,
                      SUCCESS_COLOUR, "spot");
    if (hasDefender)
    {
      DrillDraw::marker(renderer, draw, defenderAt,
                        SECONDARY_MARKER_RADIUS_PIXELS * scale,
                        DEFENDER_COLOUR);
    }
    if (shot)
    {
      const ImU32 colour = shotColour();
      DrillDraw::line(renderer, draw, shot->from,
                      PitchPoint{PITCH.atX(1.0F), shot->targetY}, colour,
                      HIT_OUTLINE_WIDTH_PIXELS * scale);
      DrillDraw::label(renderer, draw, shot->from,
                       std::format("xG {:.2f}{}", shot->xg,
                                   outcome.empty() ? "" : "  " + outcome)
                           .c_str(),
                       colour);
    }
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(
        std::format("{:.1f} m out, {:+.1f} m off centre · {} · Shooting {:.0f}",
                    line, side, forced ? "shoots now" : "decides", shooting));
    lines.push_back(std::format(
        "{} · {}",
        hasKeeper
            ? std::format("keeper (GK {:.0f}, {:.0f} cm, {})", goalkeeping,
                          keeperHeight,
                          keeperStart < CHOICE_MIDPOINT ? "covering the angle"
                                                        : "from the centre")
            : std::string("empty goal"),
        hasDefender ? std::format("defender {:.1f} m in the way (Def {:.0f})",
                                  defenderDistance, defending)
                    : std::string("no defender")));
    if (shot)
    {
      lines.push_back(std::format(
          "Shot after {:.1f} s from {:.1f} m out, {:+.1f} m: xG {:.3f}, {}",
          shot->at, PITCH.mirrorX(shot->from.x),
          shot->from.y - PITCH.atY(PITCH_MIDPOINT_SHARE), shot->xg,
          shot->onTarget ? "on target" : "off target"));
    }
    if (!outcome.empty())
    {
      lines.push_back("OUTCOME  " + outcome);
    }
    // Every run with these settings so far (Watch: restart for another).
    if (const auto found = tallies.find(settingsKey()); found != tallies.end())
    {
      const Tally& t = found->second;
      lines.push_back(std::format(
          "These settings so far: {} runs, {} shots, {} goals ({:.0f}%), xG "
          "{:.2f}",
          t.runs, t.shots, t.goals,
          (t.shots != 0) ? 100.0 * t.goals / t.shots : 0.0, t.xg));
    }
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty() || outcome == "no outcome")
    {
      return {};
    }
    const auto is = [&](std::string_view name)
    { return outcome == name ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{{.name = "Shot", .value = shot ? 1.0 : 0.0}};
    if (shot)
    {
      out.push_back({.name = "Goal", .value = is("goal")});
      out.push_back({.name = "xG", .value = shot->xg});
      out.push_back({.name = "On target", .value = shot->onTarget ? 1.0 : 0.0});
      out.push_back({.name = "Saved", .value = is("saved")});
      out.push_back({.name = "Off target", .value = is("off target")});
      out.push_back({.name = "Woodwork", .value = is("woodwork")});
      out.push_back({.name = "Blocked", .value = is("blocked")});
      out.push_back({.name = "Shot from goal line (m)",
                     .value = PITCH.mirrorX(shot->from.x)});
      out.push_back({.name = "Shot off centre (m)",
                     .value = shot->from.y - PITCH.atY(PITCH_MIDPOINT_SHARE)});
      out.push_back({.name = "Seconds to shoot", .value = shot->at});
    }
    else
    {
      out.push_back(
          {.name = "Lost the ball", .value = is("no shot (lost the ball)")});
    }
    return out;
  }

 private:
  struct Shot
  {
    PitchPoint from;
    float xg = 0.0F;
    bool onTarget = false;
    float targetY = PITCH_MIDPOINT_SHARE;
    double at = 0.0;
  };
  struct Tally
  {
    int runs = 0;
    int shots = 0;
    int goals = 0;
    double xg = 0.0;
  };

  [[nodiscard]] std::string settingsKey() const
  {
    return std::format(
               "{:.1f}|{:.1f}|{}|{:.0f}|{}|{:.0f}|{:.0f}|{}|{:.1f}|{:.0f}",
               line, side, forced, shooting, hasKeeper, goalkeeping,
               keeperHeight, hasDefender, defenderDistance, defending) +
           std::format("|{:.0f}|{:.0f}", keeperStart, goalChoice);
  }
  void tally(const std::string& result)
  {
    Tally& t = tallies[settingsKey()];
    ++t.runs;
    if (shot)
    {
      ++t.shots;
      t.xg += shot->xg;
    }
    if (result == "goal")
    {
      ++t.goals;
    }
  }

  float line = DEFAULT_LINE;
  float side = DEFAULT_SIDE;
  float goalChoice = 0.0F;
  float choice = 0.0F;
  float shooting = DEFAULT_SHOOTING;
  float keeperChoice = 1.0F;
  float goalkeeping = DEFAULT_GOALKEEPING;
  float keeperHeight = DEFAULT_KEEPER_HEIGHT;
  float keeperStart = 0.0F;
  PitchPoint keeperAt;
  float defenderChoice = 0.0F;
  float defenderDistance = DEFAULT_DEFENDER_DISTANCE;
  float defending = DEFAULT_DEFENDING;

  PlayerID shooterId = 0;
  PlayerID keeperId = 0;
  PlayerID defenderId = 0;
  bool hasKeeper = true;
  bool hasDefender = false;
  bool forced = true;
  PitchPoint spot;
  PitchPoint defenderAt;
  std::size_t seenEvents = 0;
  std::optional<Shot> shot;
  std::string outcome;
  std::optional<double> endAt;
  std::vector<PitchPoint> path;
  std::map<std::string, Tally> tallies;
};
// --- Shot block ------------------------------------------------------------

/**
 * A shot at an empty goal past one defender (shot block plan §3d): does
 * his body, his stride or a leg he puts out stop it? Where he stands off
 * the shot's line, how he moves, his Defending (reaction) and Tackling
 * (the leg's speed and aim) are settings; the true view shows his legs.
 */
class ShotBlockDrill final : public Drill
{
  static constexpr double BLOCK_WATCH_SECONDS = 4.0;
  static constexpr double STRIKE_TIMEOUT_SECONDS = 10.0;
  static constexpr double FLIGHT_TIMEOUT_SECONDS = 6.0;
  static constexpr float DEFENDER_CROSSING_METRES = 30.0F;
  static constexpr float RUN_CHOICE_THRESHOLD = 1.5F;
  static constexpr float MIN_DEFLECTION_SPEED = 1e-3F;
  static constexpr float LINE_MIN = 6.0F;
  static constexpr float LINE_MAX = 40.0F;
  static constexpr float SIDE_MIN = -20.0F;
  static constexpr float SIDE_MAX = 20.0F;
  static constexpr float AIM_ACROSS_MIN = -3.6F;
  static constexpr float AIM_ACROSS_MAX = 3.6F;
  static constexpr float AIM_HEIGHT_MAX = 2.4F;
  static constexpr float POWER_MIN = 0.3F;
  static constexpr float SHOOTING_MAX = 100.0F;
  static constexpr float DISTANCE_MIN = 0.5F;
  static constexpr float DISTANCE_MAX = 25.0F;
  static constexpr float OFF_LINE_MIN = -2.5F;
  static constexpr float OFF_LINE_MAX = 2.5F;
  static constexpr float MOVING_MAX = 2.0F;
  static constexpr float RUN_UP_MIN = 2.0F;
  static constexpr float RUN_UP_MAX = 25.0F;
  static constexpr float DEFENDING_MAX = 100.0F;
  static constexpr float TACKLING_MAX = 100.0F;
  static constexpr float HEIGHT_MIN = 160.0F;
  static constexpr float HEIGHT_MAX = 205.0F;
  static constexpr float GOALKEEPING_MAX = 100.0F;
  static constexpr float DEFAULT_LINE = 18.0F;
  static constexpr float DEFAULT_AIM_ACROSS = 1.5F;
  static constexpr float DEFAULT_AIM_HEIGHT = 0.2F;
  static constexpr float DEFAULT_POWER = 0.8F;
  static constexpr float DEFAULT_SHOOTING = 99.0F;
  static constexpr float DEFAULT_DISTANCE = 8.0F;
  static constexpr float DEFAULT_OFF_LINE = 0.7F;
  static constexpr float DEFAULT_RUN_UP = 12.0F;
  static constexpr float DEFAULT_GOALKEEPING = 70.0F;
  static constexpr float DEFAULT_CLOSEST = 99.0F;
  static constexpr float DEFAULT_DEFENDING = 70.0F;
  static constexpr float DEFAULT_TACKLING = 70.0F;
  static constexpr float DEFAULT_HEIGHT = 185.0F;
  static constexpr ImU32 BALL_PATH_COLOUR = IM_COL32(255, 255, 255, 90);
  static constexpr ImU32 SUCCESS_COLOUR = IM_COL32(90, 220, 120, 255);
  static constexpr ImU32 DEFENDER_COLOUR = IM_COL32(240, 80, 80, 200);

 public:
  [[nodiscard]] const char* name() const override { return "Shot block"; }
  [[nodiscard]] const char* summary() const override
  {
    return "A shot at an empty goal past one defender, standing or moving "
           "near its line. Too close to react: only his body and his stride "
           "are in the way; otherwise he puts a leg across the line if he "
           "can reach it (Defending: his reaction; Tackling: the leg's speed "
           "and aim). Open the true view to see his legs.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Out from goal line (m)", &line,
                           {.min = LINE_MIN, .max = LINE_MAX}, false,
                           "Where the shooter stands."));
    list.push_back(setting("Off centre (m)", &side,
                           {.min = SIDE_MIN, .max = SIDE_MAX}, false));
    list.push_back(
        setting("Aim across goal (m)", &aimAcross,
                {.min = AIM_ACROSS_MIN, .max = AIM_ACROSS_MAX}, false,
                "Where in the goal he shoots (+ towards pitch y = 1)."));
    list.push_back(setting("Aim height (m)", &aimHeight,
                           {.min = 0.0F, .max = AIM_HEIGHT_MAX}, false,
                           "How high at the goal: 0 along the ground."));
    list.push_back(setting("Power (share)", &power,
                           {.min = POWER_MIN, .max = 1.0F}, false,
                           "Share of his shot power."));
    list.push_back(setting("Shooting", &shooting,
                           {.min = 1.0F, .max = SHOOTING_MAX}, true));
    DrillParameter kind =
        setting("Ball", &ballKind, {.min = 0.0F, .max = 1.0F}, true,
                "Shot: at the goal.\nPass: along the ground to the same "
                "spot, nobody there (pass block plan): the defender "
                "intercepts it (today's touch) or it meets his body.");
    kind.choices = {"Shot", "Pass"};
    list.push_back(kind);
    DrillParameter defender =
        setting("Defender distance (m)", &distance,
                {.min = DISTANCE_MIN, .max = DISTANCE_MAX}, false,
                "From the shooter along the shot's line.");
    defender.section = "Defender";
    list.push_back(defender);
    list.push_back(
        setting("Off the line (m)", &offLine,
                {.min = OFF_LINE_MIN, .max = OFF_LINE_MAX}, false,
                "Sideways from the shot's line (+ to the shooter's left)."));
    DrillParameter movement = setting(
        "Moving", &moving, {.min = 0.0F, .max = MOVING_MAX}, true,
        "Standing: facing the shooter.\nSidestepping: across the line, facing "
        "the shooter.\nSprinting: across the line, facing his run.");
    movement.choices = {"Standing", "Sidestepping", "Sprinting"};
    list.push_back(movement);
    list.push_back(setting(
        "Run-up (m)", &runUp, {.min = RUN_UP_MIN, .max = RUN_UP_MAX}, false,
        "Moving: how far back along his run he starts. The shot "
        "is struck as he is on course to be off the line where "
        "set when the ball gets there."));
    list.push_back(setting("Defending", &defending,
                           {.min = 1.0F, .max = DEFENDING_MAX}, true,
                           "His reaction: 0.35 s at 1 to 0.15 s at 99."));
    list.push_back(
        setting("Tackling", &tackling, {.min = 1.0F, .max = TACKLING_MAX}, true,
                "His leg out to full reach: 0.45 s at 1 to 0.25 s at "
                "99; his miss 0.25 m to 0.05 m (doubled when rushed)."));
    list.push_back(setting("Height (cm)", &height,
                           {.min = HEIGHT_MIN, .max = HEIGHT_MAX}, true));
    DrillParameter keeper =
        setting("Keeper", &keeperChoice, {.min = 0.0F, .max = 1.0F}, true,
                "In goal: on his line in the middle, reading the "
                "shot (and a deflection still going in, afresh).");
    keeper.choices = {"None", "In goal"};
    keeper.section = "Keeper";
    list.push_back(keeper);
    list.push_back(setting("Goalkeeping", &goalkeeping,
                           {.min = 1.0F, .max = GOALKEEPING_MAX}, true));
    return list;
  }

  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    constexpr float PRESET_CLOSE_STRAIGHT_AT_HIM_DEFENDER_DISTANCE_M = 2.0F;
    constexpr float PRESET_REACH_FOR_IT_DEFENDER_DISTANCE_M = 8.0F;
    constexpr float PRESET_REACH_FOR_IT_OFF_THE_LINE_M = 0.7F;
    constexpr float PRESET_EDGE_OF_HIS_REACH_DEFENDER_DISTANCE_M = 10.0F;
    constexpr float PRESET_SPRINTING_ACROSS_DEFENDER_DISTANCE_M = 8.0F;
    constexpr float PRESET_SPRINTING_ACROSS_OFF_THE_LINE_M = 0.8F;
    constexpr float PRESET_SPRINTING_ACROSS_MOVING = 2.0F;
    constexpr float PRESET_SPRINTING_ACROSS_RUN_UP_M = 15.0F;

    return {
        {.name = "Close, straight at him",
         .summary = "2 m from the shooter, on the line: too close to react.",
         .settings = {{"Defender distance (m)",
                       PRESET_CLOSE_STRAIGHT_AT_HIM_DEFENDER_DISTANCE_M},
                      {"Off the line (m)", 0.0F},
                      {"Moving", 0.0F}}},
        {.name = "Reach for it",
         .summary = "8 m out, 0.7 m off the line: time to put a leg across.",
         .settings = {{"Defender distance (m)",
                       PRESET_REACH_FOR_IT_DEFENDER_DISTANCE_M},
                      {"Off the line (m)", PRESET_REACH_FOR_IT_OFF_THE_LINE_M},
                      {"Moving", 0.0F}}},
        {.name = "Edge of his reach",
         .summary = "10 m out, 1.0 m off the line.",
         .settings = {{"Defender distance (m)",
                       PRESET_EDGE_OF_HIS_REACH_DEFENDER_DISTANCE_M},
                      {"Off the line (m)", 1.0F},
                      {"Moving", 0.0F}}},
        {.name = "Sprinting across",
         .summary = "8 m out, sprinting across 0.8 m off the line.",
         .settings = {{"Defender distance (m)",
                       PRESET_SPRINTING_ACROSS_DEFENDER_DISTANCE_M},
                      {"Off the line (m)",
                       PRESET_SPRINTING_ACROSS_OFF_THE_LINE_M},
                      {"Moving", PRESET_SPRINTING_ACROSS_MOVING},
                      {"Run-up (m)", PRESET_SPRINTING_ACROSS_RUN_UP_M}}},
    };
  }

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec defender{.name = "Defender",
                             .role = PlayerRole::CB,
                             .homeTeam = false,
                             .stats = {{"Defending", defending}},
                             .height = static_cast<int>(height),
                             .derived = {}};
    defender.derived[DerivedStat::TACKLING] = tackling;
    std::vector<DrillPlayerSpec> list{{.name = "Shooter",
                                       .role = PlayerRole::ST,
                                       .homeTeam = true,
                                       .stats = {{"Shooting", shooting}},
                                       .height = REFERENCE_PLAYER_HEIGHT_CM,
                                       .derived = {}},
                                      defender};
    if (keeperChoice > CHOICE_MIDPOINT)
    {
      list.push_back({.name = "Keeper",
                      .role = PlayerRole::GK,
                      .homeTeam = false,
                      .stats = {{"Goalkeeping", goalkeeping}},
                      .height = REFERENCE_KEEPER_HEIGHT_CM,
                      .derived = {}});
    }
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    shooterId = ids[0];
    defenderId = ids[1];
    spot = at(std::clamp(1.0F - (line / LENGTH), PITCH_MIDPOINT_SHARE,
                         PITCH_EDGE_MAX_SHARE),
              std::clamp(PITCH_MIDPOINT_SHARE + (side / WIDTH),
                         PITCH_SAFE_MIN_SHARE, PITCH_SAFE_MAX_SHARE));
    const PitchPoint aimAt =
        at(1.0F, PITCH_MIDPOINT_SHARE + (aimAcross / WIDTH));
    const Vector2F towards = directionBetween(spot, aimAt);
    // The shooter's left, looking along the shot.
    const Vector2F left{.x = towards.y, .y = -towards.x};
    const float there = std::min(distance, metresBetween(spot, aimAt) - 0.5F);
    defenderAt = along(along(spot, towards, there), left, offLine);
    // Moving: across the line, towards the shooter's left, from his run-up.
    const bool runs = moving > CHOICE_MIDPOINT;
    runLeft = left;
    defenderOut = there;
    start = runs ? along(defenderAt, left, -runUp) : defenderAt;
    MatchScenario scenario;
    scenario.players.push_back(
        {.playerId = shooterId, .position = spot, .makingRun = false});
    scenario.players.push_back(
        {.playerId = defenderId, .position = start, .makingRun = false});
    if (keeperChoice > CHOICE_MIDPOINT)
    {
      scenario.players.push_back(
          {.playerId = ids[2],
           .position = at(KEEPER_START_LINE_SHARE, PITCH_MIDPOINT_SHARE),
           .makingRun = false});
    }
    scenario.carrierId = shooterId;
    scenario.ballPosition = spot;
    engine.startDrill(scenario);
    const Vector2F toShooter = directionBetween(defenderAt, spot);
    const float faceShooter =
        facingAngleOf(std::atan2(toShooter.y, toShooter.x));
    if (runs)
    {
      engine.setDrillTarget(
          defenderId, along(defenderAt, left, DEFENDER_CROSSING_METRES),
          moving > RUN_CHOICE_THRESHOLD, {}, 0.0F, 0.0F,
          moving < RUN_CHOICE_THRESHOLD ? std::optional<float>(faceShooter)
                                        : std::nullopt,
          moving > RUN_CHOICE_THRESHOLD);
    }
    else
    {
      engine.setDrillTarget(defenderId, defenderAt, false, {}, 0.0F, 0.0F,
                            faceShooter);
    }
    shotAimed = false;
    if (!runs)
    {
      shoot(engine);
    }
    else
    {
      // He waits on his spot, facing the goal, until the shot is called.
      engine.keepBall(shooterId, true, 0.0F);
      engine.setDrillTarget(shooterId, spot, false, {}, 0.0F, 0.0F,
                            facingAngleOf(std::atan2(towards.y, towards.x)));
    }
    seenEvents = engine.getEvents().size();
    shotAt.reset();
    outcome.clear();
    endAt.reset();
    legs.reset();
    closest = DEFAULT_CLOSEST;
    stillShot = false;
    afterBlock.clear();
    part.clear();
    blocksBefore = engine.getStats().shotBlocksByPart;
    reactionBefore = engine.getStats().shotReactionBlocks;
    bodyHitsBefore = engine.getStats().ballBodyHitsByPart;
    ballReactionBefore = engine.getStats().ballReactionBlocks;
    noTouchBefore = noTouchNow = engine.getStats().ballBodyAfterNoTouch;
    throughBefore = throughNow = engine.getStats().shotTorsoPassThroughs;
    insideBefore = insideNow = engine.getStats().shotTorsoInsideAtStart;
  }

 private:
  [[nodiscard]] double noLegMetric(std::string_view why) const
  {
    const bool leg = legs && legs->blocking;
    const bool reacted = legs && legs->reacted;
    return reacted && !leg &&
                   std::string_view(legs->noBlock).find(why) !=
                       std::string_view::npos
               ? 1.0
               : 0.0;
  }

  void observePass(const MatchEngine& engine, const MatchPlayer* defender,
                   double now)
  {
    // A pass: on its way once it has left him; then off his body, his
    // ball (intercepted), or past him.
    if (ballKind > CHOICE_MIDPOINT && shotAimed && (defender != nullptr))
    {
      const MatchBall& ball = engine.getBall();
      if (!shotAt && ball.possessionState().isPass &&
          (engine.getPossessionAttachment() == nullptr ||
           engine.getPossessionAttachment()->getId() != shooterId))
      {
        shotAt = now;
        speedAtShot = speedOf(*defender);
        const Vector2F at = asVector(defender->position);
        const Vector2F from = asVector(spot);
        offLineAtShot =
            (at.x - from.x) * runLeft.x + (at.y - from.y) * runLeft.y;
        seenEvents = engine.getEvents().size();
      }
      if (shotAt && outcome.empty())
      {
        const MatchStats& stats = engine.getStats();
        noTouchNow = stats.ballBodyAfterNoTouch;
        int hits = 0;
        for (std::size_t index = 0; index < bodyHitsBefore.size(); ++index)
        {
          hits += stats.ballBodyHitsByPart.at(index) - bodyHitsBefore.at(index);
        }
        if (hits > 0)
        {
          outcome = "blocked";
        }
        else if ((engine.getPossessionAttachment() != nullptr) &&
                 engine.getPossessionAttachment()->getId() == defenderId)
        {
          outcome = "intercepted";
        }
        else if (now >= *shotAt + BLOCK_WATCH_SECONDS)
        {
          outcome = "past him";
        }
      }
    }
  }

  void observeShotEvents(const MatchEngine& engine, const MatchPlayer* defender,
                         double now)
  {
    const auto& events = engine.getEvents();
    for (; seenEvents < events.size() && outcome.empty(); ++seenEvents)
    {
      const MatchEvent& event = events[seenEvents];
      if (event.type == MatchEventType::SHOT &&
          event.primaryPlayerId == shooterId && !shotAt)
      {
        shotAt = now;
        speedAtShot = (defender != nullptr) ? speedOf(*defender) : 0.0F;
        if (defender != nullptr)
        {
          const Vector2F at = asVector(defender->position);
          const Vector2F from = asVector(spot);
          offLineAtShot =
              (at.x - from.x) * runLeft.x + (at.y - from.y) * runLeft.y;
        }
        continue;
      }
      if (!shotAt)
      {
        continue;
      }
      switch (event.type)
      {
        case MatchEventType::GOAL:
        case MatchEventType::OWN_GOAL:
          outcome = "goal";
          break;
        case MatchEventType::WOODWORK:
          outcome = "woodwork";
          break;
        case MatchEventType::SHOT_BLOCKED:
          outcome = "blocked";
          break;
        case MatchEventType::SAVE:
          outcome = "saved";
          break;
        case MatchEventType::GOAL_KICK:
        case MatchEventType::CORNER:
        case MatchEventType::THROW_IN:
          outcome = "off target";
          break;
        default:
          break;
      }
    }
  }

  void observeDeflection(const MatchEngine& engine)
  {
    if (outcome == "blocked" && part.empty())
    {
      // Off him, as it left him: how fast, turned how far from the shot's
      // line, how much it rose, its spin.
      const MatchEngine::ShotBounce& off = engine.lastShotBounce();
      offSpeed = std::hypot(off.velocity.x, off.velocity.y);
      offUp = off.velocity.z;
      offSpin = off.spin;
      offNormalUp = off.hit.normal.z;
      offPart = static_cast<int>(off.hit.part);
      const Vector2F shotLine = directionBetween(
          spot, at(1.0F, PITCH_MIDPOINT_SHARE + (aimAcross / WIDTH)));
      offTurn = offSpeed > MIN_DEFLECTION_SPEED
                    ? turnDegrees(shotLine, {.x = off.velocity.x / offSpeed,
                                             .y = off.velocity.y / offSpeed})
                    : 0.0F;
      const MatchStats& stats = engine.getStats();
      static constexpr std::array<const char*, 7> PARTS{
          "torso", "thigh", "shin", "foot", "head", "hand", "arm"};
      const bool pass = ballKind > CHOICE_MIDPOINT;
      for (std::size_t index = 0; index < PARTS.size(); ++index)
      {
        if ((pass
                 ? stats.ballBodyHitsByPart.at(index) - bodyHitsBefore.at(index)
                 : stats.shotBlocksByPart.at(index) - blocksBefore.at(index)) >
            0)
        {
          part = PARTS.at(index);
        }
      }
      if (!part.empty() && part != "torso" && part != "head")
      {
        const bool reacted = pass
                                 ? stats.ballReactionBlocks > ballReactionBefore
                                 : stats.shotReactionBlocks > reactionBefore;
        part += reacted ? " (reaction block)" : " (stride)";
      }
      // Still going in off him: still the shot (shot block plan step 4).
      stillShot = engine.getBall().possessionState().isShot;
    }
  }

  void observeAfterBlock(const MatchEngine& engine, double now)
  {
    const auto& events = engine.getEvents();
    // After a block: what came of it (a goal, in off him or from a rebound;
    // a save; out; or someone's).
    if (outcome == "blocked" && afterBlock.empty())
    {
      for (; seenEvents < events.size() && afterBlock.empty(); ++seenEvents)
      {
        switch (events[seenEvents].type)
        {
          case MatchEventType::GOAL:
          case MatchEventType::OWN_GOAL:
            afterBlock = "goal";
            break;
          case MatchEventType::SAVE:
            afterBlock = "saved";
            break;
          case MatchEventType::CORNER:
            afterBlock = "corner";
            break;
          case MatchEventType::GOAL_KICK:
          case MatchEventType::THROW_IN:
            afterBlock = "out";
            break;
          default:
            break;
        }
      }
      if (afterBlock.empty() && (engine.getPossessionAttachment() != nullptr) &&
          !engine.hasTouchClaim())
      {
        afterBlock = engine.getPossessionAttachment()->getId() == defenderId
                         ? "his"
                         : "shooter's";
      }
      if (afterBlock.empty() && endAt && now >= *endAt + BLOCK_WATCH_SECONDS)
      {
        afterBlock = "loose";
      }
    }
  }

 public:
  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchPlayer* defender = slotOf(engine, defenderId);
    // Moving: struck once he is on course to be at his mark as the ball
    // gets there (a shot of about BALL_SPEED, struck STRIKE_DELAY after
    // it is called for).
    if (!shotAimed && (defender != nullptr))
    {
      constexpr float BALL_SPEED = 27.0F;
      constexpr float STRIKE_DELAY = 0.1F;
      const float lead = (defenderOut / BALL_SPEED) + STRIKE_DELAY;
      const Vector2F at = asVector(defender->position);
      const Vector2F mark = asVector(defenderAt);
      const float ahead =
          ((at.x + defender->velocity.x * lead - mark.x) * runLeft.x) +
          ((at.y + defender->velocity.y * lead - mark.y) * runLeft.y);
      if (ahead >= 0.0F)
      {
        shoot(engine);
      }
    }
    observePass(engine, defender, now);
    observeShotEvents(engine, defender, now);
    // His legs as the shot met them, up to the step it ended (the record
    // stays as the ball last met him).
    if ((defender != nullptr) && shotAt && !endAt)
    {
      const auto slot =
          static_cast<std::size_t>(defender - engine.getPlayers().data());
      if (const auto recent = engine.recentShotLegs(slot, 10.0F))
      {
        legs = recent->first;
      }
      closest = std::min(closest, metresBetween(engine.getBall().position,
                                                defender->position));
      throughNow = engine.getStats().shotTorsoPassThroughs;
      insideNow = engine.getStats().shotTorsoInsideAtStart;
    }
    observeDeflection(engine);
    observeAfterBlock(engine, now);
    if (outcome.empty() && !shotAt && now >= STRIKE_TIMEOUT_SECONDS)
    {
      outcome = "no shot";
    }
    if (outcome.empty() && shotAt && now >= *shotAt + FLIGHT_TIMEOUT_SECONDS)
    {
      outcome = "no outcome";
    }
    if (!outcome.empty() && !endAt)
    {
      endAt = now;
      tally();
    }
    return (!endAt || now < *endAt + 1.0 ||
            (outcome == "blocked" && afterBlock.empty()));
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::line(renderer, draw, spot,
                    at(1.0F, PITCH_MIDPOINT_SHARE + (aimAcross / WIDTH)),
                    BALL_PATH_COLOUR, BALL_TRAIL_WIDTH_PIXELS * scale);
    DrillDraw::marker(renderer, draw, spot, LARGE_MARKER_RADIUS_PIXELS * scale,
                      SUCCESS_COLOUR, "shooter");
    DrillDraw::marker(renderer, draw, defenderAt,
                      SECONDARY_MARKER_RADIUS_PIXELS * scale, DEFENDER_COLOUR,
                      moving > CHOICE_MIDPOINT ? "at the strike" : nullptr);
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    static constexpr std::array<const char*, 3> MOVES{
        "standing", "sidestepping", "sprinting"};
    lines.push_back(std::format(
        "Defender {:.1f} m out, {:+.2f} m off the line, {} · Def "
        "{:.0f}, Tackling {:.0f}, "
        "{:.0f} cm",
        distance, offLine,
        MOVES.at(std::clamp(static_cast<int>(std::round(moving)), 0, 2)),
        defending, tackling, height));
    if (shotAt)
    {
      lines.push_back(
          std::format("Shot struck at {:.2f} s; defender moving {:.1f} m/s",
                      *shotAt, speedAtShot));
    }
    if (legs)
    {
      if (!legs->reacted)
      {
        lines.emplace_back(
            "Not reacted when the ball got to him: body and stride only");
      }
      else if (!legs->blocking)
      {
        lines.push_back(
            std::format("Reacted; no leg: {} (line {:.2f} m from him)",
                        legs->noBlock, legs->lineGap));
      }
      else
      {
        lines.push_back(std::format(
            "Leg across the line {:.2f} m from him: {:.0f}% out when met, "
            "needs {:.2f} s, "
            "{:+.2f} s to spare, miss {:.2f} m (of {:.2f})",
            legs->lineGap, 100.0F * legs->outShare, legs->legSeconds,
            legs->spareSeconds, legs->error, legs->errorRadius));
      }
    }
    if (!outcome.empty())
    {
      lines.push_back("OUTCOME  " + outcome +
                      (part.empty() ? "" : " off his " + part));
    }
    if (outcome == "blocked")
    {
      lines.push_back(std::format("{} off him; then: {}",
                                  stillShot ? "Still a shot" : "Loose",
                                  afterBlock.empty() ? "..." : afterBlock));
    }
    if (outcome == "blocked")
    {
      lines.push_back(std::format(
          "Off him at {:.1f} m/s, turned {:.0f} deg from the shot's line, "
          "{:+.1f} m/s up; spin {:+.0f} side, {:+.0f} top (rad/s)",
          offSpeed, offTurn, offUp, offSpin.side, offSpin.top));
    }
    if (const auto found = tallies.find(settingsKey()); found != tallies.end())
    {
      const Tally& t = found->second;
      lines.push_back(std::format(
          "These settings so far: {} shots, {} blocked ({:.0f}%): {} torso, {} "
          "stride leg, {} "
          "reaction leg",
          t.shots, t.blocked,
          (t.shots != 0) ? 100.0 * t.blocked / t.shots : 0.0, t.torso, t.stride,
          t.reaction));
    }
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (!shotAt || outcome.empty() || outcome == "no outcome")
    {
      return {};
    }
    const bool blocked = outcome == "blocked";
    const bool reaction = part.find("reaction") != std::string::npos;
    const bool leg = legs && legs->blocking;
    const bool reacted = legs && legs->reacted;
    return {
        {.name = "Blocked", .value = static_cast<double>(blocked)},
        {.name = "Torso block",
         .value = static_cast<double>(blocked && part == "torso")},
        {.name = "Stride block",
         .value = static_cast<double>(blocked && !reaction && part != "torso" &&
                                      part != "head")},
        {.name = "Head block",
         .value = static_cast<double>(blocked && part == "head")},
        {.name = "Reaction block",
         .value = static_cast<double>(blocked && reaction)},
        {.name = "Line gap (m)", .value = reacted ? legs->lineGap : 0.0},
        {.name = "Ball height (m)", .value = reacted ? legs->lineHeight : 0.0},
        {.name = "Speed (m/s)", .value = speedAtShot},
        {.name = "Off the line at strike (m)", .value = offLineAtShot},
        {.name = "Closest ball (m)", .value = closest},
        {.name = "Reacted",
         .value = static_cast<double>(legs && legs->reacted)},
        {.name = "Leg out", .value = static_cast<double>(leg)},
        {.name = "Leg out share", .value = leg ? legs->outShare : 0.0},
        {.name = "Miss (m)", .value = leg ? legs->error : 0.0},
        {.name = "Spare (s)", .value = leg ? legs->spareSeconds : 0.0},
        {.name = "Reaction (s)",
         .value = reacted ? legs->legFrom - legs->pathFor : 0.0},
        {.name = "Ball to him (s)",
         .value = reacted ? legs->ballThere - legs->pathFor : 0.0},
        {.name = "No leg: out of reach", .value = noLegMetric("reach")},
        {.name = "No leg: behind him", .value = noLegMetric("behind")},
        {.name = "No leg: past him", .value = noLegMetric("past")},
        {.name = "Still a shot",
         .value = static_cast<double>(blocked && stillShot)},
        {.name = "Goal after",
         .value = static_cast<double>(afterBlock == "goal")},
        {.name = "Saved after",
         .value = static_cast<double>(afterBlock == "saved")},
        {.name = "Corner after",
         .value = static_cast<double>(afterBlock == "corner")},
        {.name = "His ball after",
         .value = static_cast<double>(afterBlock == "his")},
        {.name = "Shooter's ball after",
         .value = static_cast<double>(afterBlock == "shooter's")},
        {.name = "Off him (m/s)", .value = blocked ? offSpeed : 0.0},
        {.name = "Turned (deg)", .value = blocked ? offTurn : 0.0},
        {.name = "Rising (m/s)", .value = blocked ? offUp : 0.0},
        {.name = "Sidespin (rad/s)", .value = blocked ? offSpin.side : 0.0},
        {.name = "Topspin (rad/s)", .value = blocked ? offSpin.top : 0.0},
        {.name = "Surface facing up", .value = blocked ? offNormalUp : 0.0},
        {.name = "Part hit",
         .value = blocked ? static_cast<double>(offPart) : -1.0},
        {.name = "Goal", .value = static_cast<double>(outcome == "goal")},
        {.name = "Intercepted",
         .value = static_cast<double>(outcome == "intercepted")},
        {.name = "Body after no touch",
         .value = static_cast<double>(noTouchNow - noTouchBefore)},
        {.name = "Through torso unmet",
         .value = static_cast<double>(throughNow - throughBefore)},
        {.name = "Inside at sub-step start",
         .value = static_cast<double>(insideNow - insideBefore)},
    };
  }

 private:
  struct Tally
  {
    int shots = 0;
    int blocked = 0;
    int torso = 0;
    int stride = 0;
    int reaction = 0;
  };

  void shoot(MatchEngine& engine)
  {
    engine.keepBall(shooterId, false);
    if (ballKind > CHOICE_MIDPOINT)
    {
      engine.forcePassTo(shooterId,
                         at(1.0F, PITCH_MIDPOINT_SHARE + (aimAcross / WIDTH)),
                         ForcedPassStyle::GROUND);
    }
    else
    {
      engine.forceShot(shooterId, Vector2F{.x = aimAcross, .y = aimHeight},
                       power);
    }
    shotAimed = true;
  }
  [[nodiscard]] std::string settingsKey() const
  {
    return std::format(
               "{:.1f}|{:.1f}|{:.2f}|{:.2f}|{:.2f}|{:.0f}|{:.1f}|{:.2f}|{:.0f}|"
               "{:.1f}|"
               "{:.0f}|{:.0f}|{:.0f}",
               line, side, aimAcross, aimHeight, power, shooting, distance,
               offLine, moving, runUp, defending, tackling, height) +
           std::format("|{:.0f}|{:.0f}|{:.0f}", keeperChoice, goalkeeping,
                       ballKind);
  }
  void tally()
  {
    if (!shotAt)
    {
      return;
    }
    Tally& t = tallies[settingsKey()];
    ++t.shots;
    if (outcome != "blocked")
    {
      return;
    }
    ++t.blocked;
    if (part == "torso" || part == "head")
    {
      ++t.torso;
    }
    else if (part.find("reaction") != std::string::npos)
    {
      ++t.reaction;
    }
    else
    {
      ++t.stride;
    }
  }

  float line = DEFAULT_LINE;
  float side = 0.0F;
  float aimAcross = DEFAULT_AIM_ACROSS;
  float aimHeight = DEFAULT_AIM_HEIGHT;
  float power = DEFAULT_POWER;
  float shooting = DEFAULT_SHOOTING;
  float distance = DEFAULT_DISTANCE;
  float offLine = DEFAULT_OFF_LINE;
  float moving = 0.0F;
  float runUp = DEFAULT_RUN_UP;
  float keeperChoice = 0.0F;
  float goalkeeping = DEFAULT_GOALKEEPING;
  float ballKind = 0.0F;
  std::array<int, static_cast<std::size_t>(ShotBodyPart::COUNT)>
      bodyHitsBefore{};
  int ballReactionBefore = 0;
  int noTouchBefore = 0;
  int noTouchNow = 0;
  float offLineAtShot = 0.0F;
  float offSpeed = 0.0F;
  float offTurn = 0.0F;
  float offUp = 0.0F;
  BallSpin offSpin{};
  float offNormalUp = 0.0F;
  int offPart = -1;
  bool stillShot = false;
  std::string afterBlock;
  /** The nearest the ball came to him at a step's end while the shot was
   * on (m). */
  float closest = DEFAULT_CLOSEST;
  Vector2F runLeft{};
  float defenderOut = 0.0F;
  float defending = DEFAULT_DEFENDING;
  float tackling = DEFAULT_TACKLING;
  float height = DEFAULT_HEIGHT;

  PlayerID shooterId = 0;
  PlayerID defenderId = 0;
  PitchPoint spot;
  PitchPoint defenderAt;
  PitchPoint start;
  bool shotAimed = false;
  std::size_t seenEvents = 0;
  std::optional<double> shotAt;
  float speedAtShot = 0.0F;
  std::string outcome;
  std::string part;
  std::optional<double> endAt;
  std::optional<ShotLegs> legs;
  std::array<int, static_cast<std::size_t>(ShotBodyPart::COUNT)> blocksBefore{};
  int reactionBefore = 0;
  int throughBefore = 0;
  int throughNow = 0;
  int insideBefore = 0;
  int insideNow = 0;
  std::map<std::string, Tally> tallies;
};
// --- First touch -----------------------------------------------------------

/**
 * One ball played to one receiver: how well his first touch
 * comes off, with every variable of the ball, the receiver and the touch's
 * tuning knobs as a setting (see Notes and plans/
 * match_engine_first_touch_tuning_plan.md).
 */
class FirstTouchDrill final : public Drill
{
  static constexpr int DEFENDER_HEIGHT_CM = 182;
  static constexpr float BALL_SPEED_MIN = 2.0F;
  static constexpr float BALL_SPEED_MAX = 30.0F;
  static constexpr float FROM_ANGLE_MAX = 180.0F;
  static constexpr float FEED_DISTANCE_MIN = 5.0F;
  static constexpr float FEED_DISTANCE_MAX = 40.0F;
  static constexpr float PASSES_BY_MIN = -1.5F;
  static constexpr float PASSES_BY_MAX = 1.5F;
  static constexpr float BOUNCE_MAX = 3.0F;
  static constexpr float RUN_TO_MEET_MAX = 10.0F;
  static constexpr float RUN_UP_MAX = 30.0F;
  static constexpr float FIRST_TOUCH_MAX = 100.0F;
  static constexpr float PACE_MAX = 100.0F;
  static constexpr float CONDITION_MIN = 30.0F;
  static constexpr float CONDITION_MAX = 100.0F;
  static constexpr float DEFENDER_DISTANCE_MAX = 15.0F;
  static constexpr float DEFENDING_MAX = 100.0F;
  static constexpr float DEFAULT_BALL_SPEED = 12.0F;
  static constexpr float DEFAULT_FEED_DISTANCE = 20.0F;
  static constexpr float DEFAULT_FIRST_TOUCH = 70.0F;
  static constexpr float DEFAULT_PACE = 70.0F;
  static constexpr float DEFAULT_CONDITION = 100.0F;
  static constexpr float DEFAULT_DEFENDER_DISTANCE = 3.0F;
  static constexpr float DEFAULT_DEFENDING = 70.0F;
  static constexpr ImU32 REFERENCE_LINE_COLOUR = IM_COL32(255, 255, 255, 70);
  static constexpr ImU32 FEED_MARKER_COLOUR = IM_COL32(255, 255, 255, 200);
  static constexpr ImU32 TARGET_COLOUR = IM_COL32(90, 220, 120, 220);
  static constexpr ImU32 DEFENDER_COLOUR = IM_COL32(240, 80, 80, 200);
  static constexpr ImU32 BALL_TRAIL_COLOUR = IM_COL32(255, 230, 120, 160);
  static constexpr ImU32 CONTROLLED_BALL_COLOUR = IM_COL32(90, 170, 255, 255);
  static constexpr float HALF_TURN_DEGREES = 180.0F;

 public:
  /** Where the receiver meets the ball (pitch), facing up the pitch. */
  static inline const PitchPoint MEET =
      at(PITCH_MIDPOINT_SHARE, PITCH_MIDPOINT_SHARE);
  /** A run ends this long after its outcome, or with no outcome
   * TIME_LIMIT seconds after the ball is played. */
  static constexpr double LINGER_SECONDS = 0.3;
  static constexpr double TIME_LIMIT = 8.0;
  /** Over this far from him during the touch: a heavy touch. */
  static constexpr float HEAVY_METRES = 2.0F;

  [[nodiscard]] const char* name() const override { return "First touch"; }
  [[nodiscard]] const char* summary() const override
  {
    return "One ball played to a receiver (the new first touch is the "
           "Binturong body's): its pace, the angle it comes from, how far he "
           "has to go to it, bounce, his own pass or a loose ball, a pressing "
           "defender. Measures how his "
           "first touch comes off (errors, heavy touches, the ball lost), "
           "with his First Touch and every tuning knob of the touch as "
           "settings. Pick a situation to start from.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    const auto add = [&list](DrillParameter parameter, const char* section)
    {
      parameter.section = section;
      list.push_back(std::move(parameter));
    };
    add(setting("Ball speed (m/s)", &ballSpeed,
                {.min = BALL_SPEED_MIN, .max = BALL_SPEED_MAX}, false,
                "Pace when it is played in."),
        "Ball");
    add(setting("Comes from (deg)", &fromAngle,
                {.min = 0.0F, .max = FROM_ANGLE_MAX}, true,
                "Where it is played from, against the way he faces (up the "
                "pitch): 0 in front of him, 90 from his side, 180 from "
                "behind."),
        "Ball");
    add(setting("Feed distance (m)", &feedDistance,
                {.min = FEED_DISTANCE_MIN, .max = FEED_DISTANCE_MAX}, false),
        "Ball");
    add(setting("Passes him by (m)", &passesBy,
                {.min = PASSES_BY_MIN, .max = PASSES_BY_MAX}, false,
                "Sideways offset of its line from where he meets it: 0 "
                "straight at him; + to one side, - the other (with Foot, "
                "his stronger or weaker side)."),
        "Ball");
    add(setting("Bounce (m/s up)", &bounce, {.min = 0.0F, .max = BOUNCE_MAX},
                false, "Upward speed when played: 0 along the ground."),
        "Ball");
    DrillParameter kind =
        setting("Ball", &ballKind, {.min = 0.0F, .max = 1.0F}, true,
                "His pass: played to him (he expects it).\nLoose: nobody's "
                "pass (a surprise).");
    kind.choices = {"His pass", "Loose"};
    add(kind, "Ball");

    add(setting("Run to meet (m)", &runToMeet,
                {.min = 0.0F, .max = RUN_TO_MEET_MAX}, false,
                "He starts this far off the ball's line and goes to it (his "
                "brain chooses how)."),
        "Receiver");
    add(setting("Run-up (m)", &runUp, {.min = 0.0F, .max = RUN_UP_MAX}, false,
                "0: standing. Otherwise he runs up the pitch, and the ball is "
                "played once he has run this far, aimed at where he will be; "
                "he keeps running until he plays it."),
        "Receiver");
    add(setting("First Touch", &firstTouch,
                {.min = 1.0F, .max = FIRST_TOUCH_MAX}, true,
                "His First Touch rating (overrides the derived stat)."),
        "Receiver");
    add(setting("Pace", &pace, {.min = 1.0F, .max = PACE_MAX}, true),
        "Receiver");
    add(setting("Condition", &condition,
                {.min = CONDITION_MIN, .max = CONDITION_MAX}, true,
                "How fresh he is at the start (tiredness widens the touch's "
                "errors)."),
        "Receiver");
    DrillParameter foot =
        setting("Foot", &footChoice, {.min = 0.0F, .max = 1.0F}, true);
    foot.choices = {"Right", "Left"};
    add(foot, "Receiver");

    DrillParameter defender =
        setting("Defender", &defenderChoice, {.min = 0.0F, .max = 1.0F}, true,
                "A defender at his back (beyond him on the ball's line), "
                "pressing once the ball is played.");
    defender.choices = {"None", "Pressing"};
    add(defender, "Defender");
    add(setting("Defender distance (m)", &defenderDistance,
                {.min = 1.0F, .max = DEFENDER_DISTANCE_MAX}, false),
        "Defender");
    add(setting("Defending", &defending, {.min = 1.0F, .max = DEFENDING_MAX},
                true),
        "Defender");

    // Every knob of the touch, by its registry name and group.
    // Parameter names outlive the returned list and are explicitly terminated.
    static const std::array<std::string, TOUCH_KNOB_COUNT> knobNames = []
    {
      std::array<std::string, TOUCH_KNOB_COUNT> names;
      for (std::size_t index = 0; index < TOUCH_KNOB_COUNT; ++index)
      {
        names.at(index) = TOUCH_KNOBS.at(index).name;
      }
      return names;
    }();
    static const std::array<std::string, TOUCH_KNOB_COUNT> sections = []
    {
      std::array<std::string, TOUCH_KNOB_COUNT> names;
      for (std::size_t index = 0; index < TOUCH_KNOB_COUNT; ++index)
      {
        names.at(index) = "Knobs: " + std::string(TOUCH_KNOBS.at(index).group);
      }
      return names;
    }();
    for (std::size_t index = 0; index < TOUCH_KNOB_COUNT; ++index)
    {
      const TouchKnobInfo& info = TOUCH_KNOBS.at(index);
      DrillParameter knob =
          setting(knobNames.at(index).c_str(), &knobs.values.at(index),
                  {.min = info.min, .max = info.max}, false);
      knob.collapsible = true;
      add(knob, sections.at(index).c_str());
    }
    return list;
  }

  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    constexpr float PRESET_BASE_BALL_SPEED_M_S = 12.0F;
    constexpr float PRESET_BASE_FEED_DISTANCE_M = 20.0F;
    constexpr float PRESET_DRIVEN_PASS_TO_FEET_BALL_SPEED_M_S = 22.0F;
    constexpr float PRESET_SOFT_PASS_STEP_TO_IT_BALL_SPEED_M_S = 6.0F;
    constexpr float PRESET_PASS_INTO_HIS_RUN_BALL_SPEED_M_S = 10.0F;
    constexpr float PRESET_PASS_INTO_HIS_RUN_COMES_FROM_DEG = 90.0F;
    constexpr float PRESET_PASS_INTO_HIS_RUN_RUN_UP_M = 20.0F;
    constexpr float PRESET_RUN_TO_MEET_IT_RUN_TO_MEET_M = 4.0F;
    constexpr float PRESET_ACROSS_HIM_COMES_FROM_DEG = 90.0F;
    constexpr float PRESET_ACROSS_HIM_PASSES_HIM_BY_M = 0.6F;
    constexpr float PRESET_OVER_HIS_SHOULDER_BALL_SPEED_M_S = 10.0F;
    constexpr float PRESET_OVER_HIS_SHOULDER_COMES_FROM_DEG = 135.0F;
    constexpr float PRESET_BOUNCING_BALL_BOUNCE_M_S_UP = 2.8F;
    constexpr float PRESET_BOUNCING_BALL_FEED_DISTANCE_M = 7.0F;
    constexpr float PRESET_LOOSE_BALL_BALL_SPEED_M_S = 8.0F;
    constexpr float PRESET_UNDER_PRESSURE_DEFENDER_DISTANCE_M = 2.0F;

    // Each starts from the same baseline; only what differs is listed
    // after it.
    const std::map<std::string, float> base{
        {"Ball speed (m/s)", PRESET_BASE_BALL_SPEED_M_S},
        {"Comes from (deg)", 0.0F},
        {"Feed distance (m)", PRESET_BASE_FEED_DISTANCE_M},
        {"Passes him by (m)", 0.0F},
        {"Bounce (m/s up)", 0.0F},
        {"Ball", 0.0F},
        {"Run to meet (m)", 0.0F},
        {"Run-up (m)", 0.0F},
        {"Defender", 0.0F}};
    const auto with = [&base](const std::map<std::string, float>& changes)
    {
      std::map<std::string, float> settings = base;
      for (const auto& [name, value] : changes)
      {
        settings[name] = value;
      }
      return settings;
    };
    return {
        {.name = "Firm pass to feet",
         .summary = "12 m/s from in front, straight at him, standing.",
         .settings = base},
        {.name = "Driven pass to feet",
         .summary = "22 m/s: pace.",
         .settings = with({{"Ball speed (m/s)",
                            PRESET_DRIVEN_PASS_TO_FEET_BALL_SPEED_M_S}})},
        {.name = "Soft pass, step to it",
         .summary = "6 m/s, 1 m off his line.",
         .settings = with(
             {{"Ball speed (m/s)", PRESET_SOFT_PASS_STEP_TO_IT_BALL_SPEED_M_S},
              {"Run to meet (m)", 1.0F}})},
        {.name = "Pass into his run",
         .summary = "10 m/s from his side, played after a 20 m run-up.",
         .settings = with(
             {{"Ball speed (m/s)", PRESET_PASS_INTO_HIS_RUN_BALL_SPEED_M_S},
              {"Comes from (deg)", PRESET_PASS_INTO_HIS_RUN_COMES_FROM_DEG},
              {"Run-up (m)", PRESET_PASS_INTO_HIS_RUN_RUN_UP_M}})},
        {.name = "Run to meet it",
         .summary = "12 m/s, he starts 4 m off its line.",
         .settings =
             with({{"Run to meet (m)", PRESET_RUN_TO_MEET_IT_RUN_TO_MEET_M}})},
        {.name = "Across him",
         .summary = "12 m/s from his side, 0.6 m off centre.",
         .settings =
             with({{"Comes from (deg)", PRESET_ACROSS_HIM_COMES_FROM_DEG},
                   {"Passes him by (m)", PRESET_ACROSS_HIM_PASSES_HIM_BY_M}})},
        {.name = "Over his shoulder",
         .summary = "10 m/s from behind him.",
         .settings = with(
             {{"Ball speed (m/s)", PRESET_OVER_HIS_SHOULDER_BALL_SPEED_M_S},
              {"Comes from (deg)", PRESET_OVER_HIS_SHOULDER_COMES_FROM_DEG}})},
        {.name = "Bouncing ball",
         .summary = "12 m/s from 7 m with 2.8 m/s up: it reaches him on a hop.",
         .settings = with(
             {{"Bounce (m/s up)", PRESET_BOUNCING_BALL_BOUNCE_M_S_UP},
              {"Feed distance (m)", PRESET_BOUNCING_BALL_FEED_DISTANCE_M}})},
        {.name = "Loose ball",
         .summary = "8 m/s, nobody's pass.",
         .settings =
             with({{"Ball speed (m/s)", PRESET_LOOSE_BALL_BALL_SPEED_M_S},
                   {"Ball", 1.0F}})},
        {.name = "Under pressure",
         .summary = "12 m/s with a defender 2 m behind him, pressing.",
         .settings = with({{"Defender", 1.0F},
                           {"Defender distance (m)",
                            PRESET_UNDER_PRESSURE_DEFENDER_DISTANCE_M}})},
    };
  }

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec receiver{
        .name = "Receiver",
        .role = PlayerRole::CM,
        .homeTeam = true,
        .stats = {{"Pace", pace}},
        .height = REFERENCE_PLAYER_HEIGHT_CM,
        .derived = {{DerivedStat::FIRST_TOUCH, firstTouch}}};
    receiver.foot = footChoice < CHOICE_MIDPOINT ? Foot::Right : Foot::Left;
    receiver.condition = condition;
    std::vector<DrillPlayerSpec> list{receiver};
    if (defenderChoice > CHOICE_MIDPOINT)
    {
      list.push_back({.name = "Defender",
                      .role = PlayerRole::CB,
                      .homeTeam = false,
                      .stats = {{"Defending", defending}},
                      .height = DEFENDER_HEIGHT_CM,
                      .derived = {}});
    }
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    receiverId = ids[0];
    hasDefender = defenderChoice > CHOICE_MIDPOINT;
    defenderId = hasDefender ? ids[1] : 0;
    receiverBrain = engine.getBrain(true);
    engine.setFirstTouchKnobs(knobs);

    // Geometry in metres: he faces up the pitch (+x); the ball comes from
    // `fromAngle` round from in front of him, along a line `passesBy` to
    // the side of the meeting point.
    const Vector2F up{.x = 1.0F, .y = 0.0F};
    from = rotated(up, fromAngle * PI / HALF_TURN_DEGREES);
    travel = Vector2F{.x = -from.x, .y = -from.y};
    side = rotated(travel, PI * QUARTER_TURN_PI_SHARE);
    feedFrom = along(along(MEET, side, passesBy), from, feedDistance);
    receiverStart = along(along(MEET, side, -runToMeet), up, -runUp);
    // The defender marks him from behind: beyond him on the ball's line, so
    // he cannot simply cut it out before it arrives.
    defenderStart = along(MEET, travel, defenderDistance);

    MatchScenario scenario;
    scenario.players.push_back({.playerId = receiverId,
                                .position = receiverStart,
                                .makingRun = false});
    if (hasDefender)
    {
      scenario.players.push_back({.playerId = defenderId,
                                  .position = defenderStart,
                                  .makingRun = false});
    }
    scenario.carrierId = 0;
    scenario.ballPosition = feedFrom;
    engine.startDrill(scenario);
    // The defender waits on his spot until the ball is played.
    if (hasDefender)
    {
      engine.setDrillTarget(defenderId, defenderStart, false);
    }
    if (runUp > 0.0F)
    {
      engine.setDrillTarget(
          receiverId,
          along(MEET, up, RUN_THROUGH_METRES * TOUCH_RUN_THROUGH_SCALE), true);
    }

    fed = false;
    touch.reset();
    outcome.clear();
    endAt.reset();
    peakGap = 0.0F;
    released = false;
    lastBallVelocity = {.x = 0.0F, .y = 0.0F};
    ballPath.clear();
    if (runUp <= 0.0F)
    {
      feed(engine);
    }
  }

 private:
  void observeFirstTouch(MatchEngine& engine, const MatchPlayer* receiver,
                         double now)
  {
    const MatchBall& ball = engine.getBall();
    // His touch: read what it was from the ball as it leaves him.
    Touch reading;
    reading.at = now;
    reading.inTouch = engine.hasTouchClaim();
    reading.contactGap = ball.touchContactGap;
    reading.height = ball.touchContactHeight;
    reading.angleError = ball.touchAngleError;
    reading.weightError = ball.touchWeightError;
    reading.angleDifficulty = ball.touchAngleDifficulty;
    reading.weightDifficulty = ball.touchWeightDifficulty;
    reading.angleFailed = ball.touchAngleFailed;
    reading.weightFailed = ball.touchWeightFailed;
    reading.turn = ball.touchTurn;
    reading.outward = ball.touchOutward;
    reading.paceChange = ball.touchPaceChange;
    reading.intoStride = ball.touchKind == TouchIntent::Kind::INTO_STRIDE;
    reading.point = ball.position;
    if (receiver != nullptr)
    {
      reading.speed = speedOf(*receiver);
      const Vector2F facing = directionBetween(
          PitchPoint{}, PitchPoint{std::cos(receiver->facingAngle),
                                   std::sin(receiver->facingAngle)});
      const float speed = std::hypot(lastBallVelocity.x, lastBallVelocity.y);
      reading.facingVsBall =
          speed > MIN_REPORTED_CONTACT_SPEED
              ? turnDegrees(facing, {.x = -lastBallVelocity.x / speed,
                                     .y = -lastBallVelocity.y / speed})
              : 0.0F;
    }
    touch = reading;
    if (runUp > 0.0F)
    {
      engine.setDrillTarget(receiverId, std::nullopt, false);
    }
    // The old trap (a ball off his feet): the ball is his at once.
    if (!engine.hasTouchClaim())
    {
      outcome = "collected (old trap)";
    }
  }

  void observeOutcome(MatchEngine& engine, const MatchPlayer* receiver,
                      double now)
  {
    const MatchBall& ball = engine.getBall();
    const bool his = (engine.getPossessionAttachment() != nullptr) &&
                     engine.getPossessionAttachment()->getId() == receiverId;
    const bool theirs = (engine.getPossessionAttachment() != nullptr) &&
                        hasDefender &&
                        engine.getPossessionAttachment()->getId() == defenderId;
    if (!fed || !outcome.empty())
    {
      return;
    }
    if (his && !touch)
    {
      observeFirstTouch(engine, receiver, now);
    }
    // Rolling to him until he collects it (then he dribbles it on).
    const bool rolling = engine.hasTouchClaim() && !engine.isTouchCollected();
    if (touch && rolling && his)
    {
      peakGap = std::max(peakGap, ball.touchPeakGap);
    }
    if (touch && his && !rolling && outcome.empty())
    {
      outcome = "collected";
    }
    if (theirs)
    {
      outcome = touch ? "lost to the defender" : "intercepted";
    }
    if (engine.getState() != MatchState::PLAYING)
    {
      outcome = touch ? "lost (out of play)" : "out of play";
    }
    if (touch && (engine.getPossessionAttachment() == nullptr))
    {
      released = true;
    }
    if (outcome.empty() && now >= fedAt + TIME_LIMIT)
    {
      outcome = touch ? "no result" : "never touched";
    }
  }

 public:
  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    const MatchPlayer* receiver = slotOf(engine, receiverId);
    // On the run: played once he has run his run-up.
    if (!fed && (receiver != nullptr) &&
        (metresBetween(receiverStart, receiver->position) >= runUp ||
         now >= TIME_LIMIT * TOUCH_WAIT_TIME_SHARE))
    {
      feed(engine);
    }
    if (fed && engine.getSimulatedSteps() % 2 == 0)
    {
      ballPath.push_back(ball.position);
    }

    observeOutcome(engine, receiver, now);
    lastBallVelocity = ball.velocity;
    if (!outcome.empty() && !endAt)
    {
      endAt = now;
    }
    return (!endAt || now < *endAt + LINGER_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::line(
        renderer, draw, feedFrom,
        along(feedFrom, travel, feedDistance * FEED_GUIDE_LENGTH_SCALE),
        REFERENCE_LINE_COLOUR, 1.0F * scale);
    DrillDraw::marker(renderer, draw, feedFrom,
                      PRIMARY_MARKER_RADIUS_PIXELS * scale, FEED_MARKER_COLOUR,
                      "feed");
    DrillDraw::marker(renderer, draw, MEET,
                      SECONDARY_MARKER_RADIUS_PIXELS * scale, TARGET_COLOUR,
                      "meet");
    if (hasDefender)
    {
      DrillDraw::marker(renderer, draw, defenderStart,
                        SECONDARY_MARKER_RADIUS_PIXELS * scale,
                        DEFENDER_COLOUR);
    }
    DrillDraw::path(renderer, draw, ballPath, BALL_TRAIL_COLOUR,
                    BALL_TRAIL_WIDTH_PIXELS * scale);
    if (touch)
    {
      DrillDraw::marker(
          renderer, draw, touch->point, PRIMARY_MARKER_RADIUS_PIXELS * scale,
          CONTROLLED_BALL_COLOUR, outcome.empty() ? "touch" : outcome.c_str());
    }
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.0f} m/s from {:.0f} deg, {:.0f} m out, {:+.1f} m by{} · {}",
        ballSpeed, fromAngle, feedDistance, passesBy,
        bounce > 0.0F ? std::format(", {:.1f} m/s up", bounce) : std::string(),
        ballKind < CHOICE_MIDPOINT ? "his pass" : "loose"));
    lines.push_back(std::format(
        "Receiver: First Touch {:.0f}, Pace {:.0f}, condition {:.0f}, {} "
        "foot, {} brain · run to meet {:.1f} m, run-up {:.0f} m{}",
        firstTouch, pace, condition,
        footChoice < CHOICE_MIDPOINT ? "right" : "left",
        brainName(receiverBrain), runToMeet, runUp,
        hasDefender ? std::format(" · defender {:.0f} m (Def {:.0f})",
                                  defenderDistance, defending)
                    : std::string()));
    if (touch)
    {
      lines.push_back(std::format(
          "Touch at {:.2f} s, {:.2f} m from his centre{}: turn {:.0f} deg {} "
          "{}{:+.1f} deg (difficulty {:.2f}), pace {:+.1f} m/s {}{:+.1f} "
          "m/s (difficulty {:.2f}){}",
          touch->at, touch->contactGap,
          touch->intoStride ? ", into stride" : "",
          touch->turn * HALF_TURN_DEGREES / PI, touch->outward ? "out" : "in",
          touch->angleFailed ? "FAILED " : "",
          touch->angleError * HALF_TURN_DEGREES / PI, touch->angleDifficulty,
          touch->paceChange, touch->weightFailed ? "FAILED " : "",
          touch->weightError, touch->weightDifficulty,
          peakGap > 0.0F ? std::format(", got {:.1f} m away", peakGap)
                         : std::string()));
    }
    if (!outcome.empty())
    {
      lines.push_back("OUTCOME  " + outcome);
    }
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty())
    {
      return {};
    }
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    const bool collected = outcome.starts_with("collected");
    std::vector<DrillMetric> out{
        {.name = "Touched", .value = flag(touch.has_value())},
        {.name = "Collected", .value = flag(collected)},
        {.name = "Lost",
         .value = flag(touch && !collected && outcome != "no result")},
        {.name = "Heavy", .value = flag(peakGap > HEAVY_METRES)},
        {.name = "Released", .value = flag(released)},
    };
    if (touch && touch->inTouch)
    {
      out.push_back({.name = "Angle error (deg)",
                     .value = touch->angleError * HALF_TURN_DEGREES / PI});
      out.push_back(
          {.name = "Abs angle error (deg)",
           .value = std::abs(touch->angleError) * HALF_TURN_DEGREES / PI});
      out.push_back(
          {.name = "Weight error (m/s)", .value = touch->weightError});
      out.push_back({.name = "Abs weight error (m/s)",
                     .value = std::abs(touch->weightError)});
      out.push_back(
          {.name = "Angle difficulty", .value = touch->angleDifficulty});
      out.push_back(
          {.name = "Weight difficulty", .value = touch->weightDifficulty});
      out.push_back(
          {.name = "Angle failed", .value = flag(touch->angleFailed)});
      out.push_back(
          {.name = "Weight failed", .value = flag(touch->weightFailed)});
      out.push_back({.name = "Turn (deg)",
                     .value = touch->turn * HALF_TURN_DEGREES / PI});
      out.push_back({.name = "Turn outward", .value = flag(touch->outward)});
      out.push_back({.name = "Pace change (m/s)", .value = touch->paceChange});
      out.push_back({.name = "Contact gap (m)", .value = touch->contactGap});
      out.push_back({.name = "Ball height (m)", .value = touch->height});
      out.push_back({.name = "Into stride", .value = flag(touch->intoStride)});
      out.push_back(
          {.name = "Facing vs ball (deg)", .value = touch->facingVsBall});
      out.push_back({.name = "His speed (m/s)", .value = touch->speed});
    }
    if (touch)
    {
      out.push_back({.name = "Old trap", .value = flag(!touch->inTouch)});
    }
    if (touch && collected && endAt)
    {
      out.push_back(
          {.name = "Time to collect (s)", .value = *endAt - touch->at});
    }
    return out;
  }

 private:
  struct Touch
  {
    double at = 0.0;
    bool inTouch = true;
    float contactGap = 0.0F;
    float angleError = 0.0F;
    float weightError = 0.0F;
    float angleDifficulty = 0.0F;
    float weightDifficulty = 0.0F;
    bool angleFailed = false;
    bool weightFailed = false;
    float turn = 0.0F;
    bool outward = false;
    float paceChange = 0.0F;
    bool intoStride = false;
    float facingVsBall = 0.0F;
    float speed = 0.0F;
    float height = 0.0F;
    PitchPoint point;
  };

  /** Seconds for the ball, as fed, to travel `metres` (the match's own
   * flight and rolling physics, in its ball sub-steps). */
  [[nodiscard]] float rollSeconds(const MatchEngine& engine, float metres) const
  {
    constexpr float DT =
        STEP / static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
    FlightState flight{.horizontalSpeed = ballSpeed,
                       .verticalSpeed = bounce,
                       .height = 0.0F,
                       .bounced = false};
    float travelled = 0.0F;
    float seconds = 0.0F;
    while (travelled < metres && flight.horizontalSpeed > 0.0F &&
           seconds < TIME_LIMIT)
    {
      travelled += engine.stepFlight(flight, DT);
      seconds += DT;
    }
    return seconds;
  }

  void feed(MatchEngine& engine)
  {
    fed = true;
    fedAt = secondsOf(engine);
    // The defender presses from here. A running receiver keeps running until
    // he plays it (his brain would stop him to wait for it).
    if (hasDefender)
    {
      engine.setDrillTarget(defenderId, std::nullopt, false);
    }
    if (const MatchPlayer* receiver = slotOf(engine, receiverId);
        (receiver != nullptr) && runUp > 0.0F)
    {
      // On the run: the feed spot is placed, at the set distance and angle,
      // from where he will be when the ball gets there (his current
      // velocity over its exact travel time), still `passesBy` to his side.
      const float seconds = rollSeconds(engine, feedDistance) + STEP;
      const PitchPoint ahead = asPoint({.x = receiver->velocity.x * seconds,
                                        .y = receiver->velocity.y * seconds});
      const PitchPoint meet{receiver->position.x + ahead.x,
                            receiver->position.y + ahead.y};
      feedFrom = along(along(meet, side, passesBy), from, feedDistance);
    }
    engine.feedBall(feedFrom,
                    {.x = travel.x * ballSpeed, .y = travel.y * ballSpeed},
                    bounce, ballKind < CHOICE_MIDPOINT ? receiverId : 0);
    ballPath.push_back(feedFrom);
  }

  // Ball
  float ballSpeed = DEFAULT_BALL_SPEED;
  float fromAngle = 0.0F;
  float feedDistance = DEFAULT_FEED_DISTANCE;
  float passesBy = 0.0F;
  float bounce = 0.0F;
  float ballKind = 0.0F;
  // Receiver
  float runToMeet = 0.0F;
  float runUp = 0.0F;
  float firstTouch = DEFAULT_FIRST_TOUCH;
  float pace = DEFAULT_PACE;
  float condition = DEFAULT_CONDITION;
  float footChoice = 0.0F;
  /** The receiver's brain (the sandbox's home brain), for the summary. */
  BrainId receiverBrain = BrainId::AARDVARK;
  // Defender
  float defenderChoice = 0.0F;
  float defenderDistance = DEFAULT_DEFENDER_DISTANCE;
  float defending = DEFAULT_DEFENDING;
  // The touch's tuning, as this drill plays it.
  FirstTouchKnobs knobs;

  PlayerID receiverId = 0;
  PlayerID defenderId = 0;
  bool hasDefender = false;
  /** Unit directions (metres): where the ball comes from, the way it
   * travels, and square to it (the side `passesBy` measures). */
  Vector2F from{.x = -1.0F, .y = 0.0F};
  Vector2F travel{.x = 1.0F, .y = 0.0F};
  Vector2F side{.x = 0.0F, .y = 1.0F};
  PitchPoint feedFrom;
  PitchPoint receiverStart;
  PitchPoint defenderStart;
  bool fed = false;
  double fedAt = 0.0;
  std::optional<Touch> touch;
  std::string outcome;
  bool released = false;
  std::optional<double> endAt;
  float peakGap = 0.0F;
  Vector2F lastBallVelocity{};
  std::vector<PitchPoint> ballPath;
};

// --- Where a pass went, on the strike alone ----------------------------------

/**
 * A pass with nobody on the end of it, judged on the strike alone (Notes and
 * plans/match_engine_technique_bench_plan.md): how close it ran past the
 * target over the ground, and where it first came down after being in the
 * air. A pass meant to fly (lofted, cross, chip) is judged by where it
 * landed; one along the ground (ground, driven) by how close it ran past.
 */
struct PassFlight
{
  /** Higher than this (m), the ball has been in the air. */
  static constexpr float AIRBORNE_METRES = 0.1F;
  /** Further than this past its closest to the target (m): gone by. */
  static constexpr float PAST_TARGET_METRES = 3.0F;

  [[nodiscard]] static bool flies(StrikeKind kind)
  {
    return kind == StrikeKind::LOFTED_PASS || kind == StrikeKind::CROSS ||
           kind == StrikeKind::CHIP || kind == StrikeKind::HEADER;
  }

  void start(const MatchBall& ball, PitchPoint target_)
  {
    *this = PassFlight{};
    target = target_;
    distance = metresBetween(ball.position, target);
    closest = distance;
    closestSpeed = std::hypot(ball.velocity.x, ball.velocity.y);
    remember(ball);
  }

  void observe(const MatchBall& ball)
  {
    const float height = ball.z;
    const float gap = metresBetween(ball.position, target);
    if (gap < closest)
    {
      closest = gap;
      closestSpeed = std::hypot(ball.velocity.x, ball.velocity.y);
      closestHeight = height;
    }
    if (height > AIRBORNE_METRES)
    {
      wasUp = true;
    }
    // First down: bounced since the last step (falling then, rising now),
    // or on the ground; where, from the last step falling under gravity
    // (drag is small over a step), as the lofted target drill works it out.
    if (wasUp && !landing)
    {
      const bool bounced = lastLift < 0.0F && ball.velocityZ > 0.0F;
      const bool grounded =
          height <= LANDING_HEIGHT_EPSILON_METRES && ball.velocityZ <= 0.0F;
      if (bounced || grounded)
      {
        constexpr float G = MatchTuning::Ball::GRAVITY;
        const float fall =
            (lastLift + std::sqrt((lastLift * lastLift) +
                                  (2.0F * G * std::max(lastHeight, 0.0F)))) /
            G;
        landing = PitchPoint{lastPosition.x + (lastVelocity.x * fall),
                             lastPosition.y + (lastVelocity.y * fall)};
        landingMiss = metresBetween(*landing, target);
      }
    }
    stopped = std::hypot(ball.velocity.x, ball.velocity.y) <
                  STOPPED_BALL_SPEED_METRES_PER_SECOND &&
              height <= LANDING_HEIGHT_EPSILON_METRES;
    remember(ball);
  }

  /** Nothing more to learn: it has come down (if it flies) and stopped or
   * gone by the target. */
  [[nodiscard]] bool settled(StrikeKind kind, const MatchBall& ball) const
  {
    if (flies(kind) && !landing)
    {
      return false;
    }
    return stopped ||
           metresBetween(ball.position, target) > closest + PAST_TARGET_METRES;
  }

  /** The strike's miss (m): where it landed for a pass that flies, how
   * close it ran past for one along the ground; none if it flies and never
   * came down. */
  [[nodiscard]] std::optional<float> miss(StrikeKind kind) const
  {
    if (!flies(kind))
    {
      return closest;
    }
    if (landing)
    {
      return landingMiss;
    }
    return std::nullopt;
  }

  /** Its numbers (Measure mode, the technique bench). */
  void addMetrics(StrikeKind kind, std::vector<DrillMetric>& out) const
  {
    out.push_back({.name = "Strike to target (m)", .value = distance});
    if (const auto off = miss(kind))
    {
      out.push_back({.name = "Strike miss (m)", .value = *off});
    }
    out.push_back({.name = "Landed", .value = landing ? 1.0 : 0.0});
    if (landing)
    {
      out.push_back({.name = "Landing miss (m)", .value = landingMiss});
    }
  }

  [[nodiscard]] float targetDistance() const { return distance; }
  [[nodiscard]] float closestDistance() const { return closest; }
  [[nodiscard]] const std::optional<PitchPoint>& landingPoint() const
  {
    return landing;
  }
  [[nodiscard]] float landingError() const { return landingMiss; }

 private:
  PitchPoint target;
  /** From where it was struck to the target (m). */
  float distance = 0.0F;
  float closest = 0.0F;
  float closestSpeed = 0.0F;
  float closestHeight = 0.0F;
  bool wasUp = false;
  std::optional<PitchPoint> landing;
  float landingMiss = 0.0F;
  bool stopped = false;

  void remember(const MatchBall& ball)
  {
    lastPosition = ball.position;
    lastVelocity = ball.velocity;
    lastHeight = ball.z;
    lastLift = ball.velocityZ;
  }

  PitchPoint lastPosition;
  Vector2F lastVelocity{};
  float lastHeight = 0.0F;
  float lastLift = 0.0F;
};

// --- Pass ------------------------------------------------------------------

/**
 * One pass to one receiver: how the strike comes off and
 * where the ball really goes, with every variable of the passer and every
 * strike knob of passes and crosses as a setting (see Notes and plans/
 * match_engine_contact_plan.md).
 */
class PassDrill final : public Drill
{
  static constexpr double RUN_UP_TIMEOUT_SECONDS = 6.0;
  static constexpr double STRIKE_TIMEOUT_SECONDS = 10.0;
  static constexpr int RECEIVER_HEIGHT_CM = 182;
  static constexpr int DEFENDER_HEIGHT_CM = 184;
  static constexpr float LENGTH_MIN = 3.0F;
  static constexpr float LENGTH_MAX = 60.0F;
  static constexpr float DIRECTION_MIN = -180.0F;
  static constexpr float DIRECTION_MAX = 180.0F;
  static constexpr float STYLE_CHOICE_MAX = 4.0F;
  static constexpr float SIDE_REVS_MIN = -10.0F;
  static constexpr float SIDE_REVS_MAX = 10.0F;
  static constexpr float TOP_REVS_MIN = -10.0F;
  static constexpr float TOP_REVS_MAX = 10.0F;
  static constexpr float RUN_UP_MAX = 30.0F;
  static constexpr float PACE_CHOICE_MAX = 2.0F;
  static constexpr float PASSING_MAX = 100.0F;
  static constexpr float DRIBBLING_MAX = 100.0F;
  static constexpr float CONDITION_MIN = 30.0F;
  static constexpr float CONDITION_MAX = 100.0F;
  static constexpr float DEFENDER_DISTANCE_MIN = 0.5F;
  static constexpr float DEFENDER_DISTANCE_MAX = 10.0F;
  static constexpr float DEFAULT_LENGTH = 15.0F;
  static constexpr float DEFAULT_PACE_CHOICE = 2.0F;
  static constexpr float DEFAULT_PASSING = 70.0F;
  static constexpr float DEFAULT_DRIBBLING = 70.0F;
  static constexpr float DEFAULT_CONDITION = 100.0F;
  static constexpr float DEFAULT_DEFENDER_DISTANCE = 2.0F;
  static constexpr ImU32 REFERENCE_LINE_COLOUR = IM_COL32(255, 255, 255, 70);
  static constexpr ImU32 FEED_MARKER_COLOUR = IM_COL32(255, 255, 255, 200);
  static constexpr ImU32 TARGET_COLOUR = IM_COL32(90, 220, 120, 220);
  static constexpr ImU32 DEFENDER_COLOUR = IM_COL32(240, 80, 80, 200);
  static constexpr ImU32 LANDING_COLOUR = IM_COL32(255, 230, 120, 255);
  static constexpr ImU32 BALL_TRAIL_COLOUR = IM_COL32(255, 230, 120, 160);
  static constexpr float HALF_TURN_DEGREES = 180.0F;

 public:
  /** A run ends this long after its outcome, or with no outcome
   * TIME_LIMIT seconds after the pass. */
  static constexpr double LINGER_SECONDS = 0.3;
  static constexpr double TIME_LIMIT = 6.0;
  /** The kinds of pass this drill plays, and so whose knobs it offers. */
  static constexpr std::array<StrikeKind, 5> KINDS = {
      StrikeKind::GROUND_PASS, StrikeKind::LOFTED_PASS, StrikeKind::CROSS,
      StrikeKind::DRIVEN_PASS, StrikeKind::CHIP};

  [[nodiscard]] const char* name() const override { return "Pass"; }
  [[nodiscard]] const char* summary() const override
  {
    return "A passer plays one pass to a receiver held on his spot "
           "(strike contact is the Binturong body's): its length and "
           "direction, played as the AI would or forced along the ground or "
           "lofted, from the middle or from wide in the final third (a "
           "cross), standing or after a run-up, with a defender pressing him "
           "if you like; or to a target with nobody there, judged on the "
           "strike alone (with spin if asked). Measures how "
           "the strike comes off (each job's failure and error, the "
           "conditions) and where the ball really went, with his Passing and "
           "every strike knob of passes and crosses as settings.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    const auto add = [&list](DrillParameter parameter, const char* section)
    {
      parameter.section = section;
      list.push_back(std::move(parameter));
    };
    add(setting("Length (m)", &length, {.min = LENGTH_MIN, .max = LENGTH_MAX},
                false, "From the passer to the receiver."),
        "Pass");
    add(setting("Direction (deg)", &direction,
                {.min = DIRECTION_MIN, .max = DIRECTION_MAX}, true,
                "Where the receiver is against the way the passer faces (up "
                "the pitch): 0 straight ahead, 90 to his right (towards the "
                "bottom touchline), -90 to his left, 180 behind him."),
        "Pass");
    DrillParameter style = setting(
        "Style", &styleChoice, {.min = 0.0F, .max = STYLE_CHOICE_MAX}, true,
        "As the AI would: along the ground, or lofted for a long "
        "ball or a cross.\nGround, Lofted, Driven (low and hard), "
        "Chip (steep, over someone close): forced.");
    style.choices = {"As the AI would", "Ground", "Lofted", "Driven", "Chip"};
    add(style, "Pass");
    DrillParameter from =
        setting("Passer at", &placeChoice, {.min = 0.0F, .max = 1.0F}, true,
                "In the middle of the pitch, or wide on the top touchline in "
                "the final third (a pass into the middle there is a cross).");
    from.choices = {"Middle", "Wide, final third"};
    add(from, "Pass");
    DrillParameter receiver = setting(
        "Receiver", &receiverChoice, {.min = 0.0F, .max = 1.0F}, true,
        "A player: a receiver held on the spot, who goes for the ball once "
        "it is played (the AI's own pass to him).\nNone, strike alone: a "
        "target with nobody there, weighted as for a man on it; the strike "
        "is judged by where the ball lands (lofted, cross, chip) or how "
        "close it runs past (ground, driven).");
    receiver.choices = {"A player", "None, strike alone"};
    add(receiver, "Pass");
    add(setting("Sidespin (rev/s)", &sideRevs,
                {.min = SIDE_REVS_MIN, .max = SIDE_REVS_MAX}, false,
                "Strike alone only: the sidespin he means (+ curls it to his "
                "right). 0 with no topspin: the body's default."),
        "Pass");
    add(setting("Topspin (rev/s)", &topRevs,
                {.min = TOP_REVS_MIN, .max = TOP_REVS_MAX}, false,
                "Strike alone only: the topspin he means (- backspin)."),
        "Pass");

    add(setting("Run-up (m)", &runUp, {.min = 0.0F, .max = RUN_UP_MAX}, false,
                "0: he passes standing. Otherwise he runs with the ball up "
                "the pitch this far, then passes on the move."),
        "Passer");
    DrillParameter pace = setting(
        "Run-up pace", &paceChoice, {.min = 0.0F, .max = PACE_CHOICE_MAX}, true,
        "How hard he carries it on the run-up: a jog, a run or a "
        "sprint (as his Dribbling allows).");
    pace.choices = {"Jog", "Run", "Sprint"};
    add(pace, "Passer");
    add(setting("Passing", &passing, {.min = 1.0F, .max = PASSING_MAX}, true),
        "Passer");
    add(setting("Dribbling", &dribbling, {.min = 1.0F, .max = DRIBBLING_MAX},
                true,
                "With the ball on the run-up: his pace, agility and "
                "acceleration with it."),
        "Passer");
    add(setting("Condition", &condition,
                {.min = CONDITION_MIN, .max = CONDITION_MAX}, true,
                "How fresh he is at the start (tiredness widens the "
                "errors)."),
        "Passer");
    DrillParameter foot =
        setting("Foot", &footChoice, {.min = 0.0F, .max = 1.0F}, true);
    foot.choices = {"Right", "Left"};
    add(foot, "Passer");

    DrillParameter defender =
        setting("Defender", &defenderChoice, {.min = 0.0F, .max = 1.0F}, true,
                "A defender held this far behind the passer until the pass: "
                "pressure, out of the passing lane.");
    defender.choices = {"None", "Close by"};
    add(defender, "Defender");
    add(setting("Defender distance (m)", &defenderDistance,
                {.min = DEFENDER_DISTANCE_MIN, .max = DEFENDER_DISTANCE_MAX},
                false),
        "Defender");

    // Every strike knob of the kinds of pass played here, by kind and group.
    static const auto names = []
    {
      std::array<std::array<std::string, STRIKE_KNOB_COUNT>, KINDS.size()> full;
      for (std::size_t k = 0; k < KINDS.size(); ++k)
      {
        for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
        {
          full.at(k).at(index) =
              strikeKnobName(KINDS.at(k), static_cast<StrikeKnob>(index));
        }
      }
      return full;
    }();
    static const auto sections = []
    {
      std::array<std::array<std::string, STRIKE_KNOB_COUNT>, KINDS.size()>
          titles;
      for (std::size_t k = 0; k < KINDS.size(); ++k)
      {
        for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
        {
          titles.at(k).at(index) =
              std::string(
                  STRIKE_KIND_NAMES.at(static_cast<std::size_t>(KINDS.at(k)))) +
              " knobs: " + std::string(STRIKE_KNOBS.at(index).group);
        }
      }
      return titles;
    }();
    for (std::size_t k = 0; k < KINDS.size(); ++k)
    {
      for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
      {
        const StrikeKnobInfo& info = STRIKE_KNOBS.at(index);
        DrillParameter knob =
            setting(names.at(k).at(index).c_str(),
                    &knobs.at(KINDS.at(k), static_cast<StrikeKnob>(index)),
                    {.min = info.min, .max = info.max}, false);
        knob.collapsible = true;
        knob.label = info.name.data();
        add(knob, sections.at(k).at(index).c_str());
      }
    }
    return list;
  }

  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    constexpr float PRESET_BASE_LENGTH_M = 15.0F;
    constexpr float PRESET_BASE_DEFENDER_DISTANCE_M = 2.0F;
    constexpr float PRESET_SQUARE_PASS_DIRECTION_DEG = 90.0F;
    constexpr float PRESET_LONG_GROUND_PASS_LENGTH_M = 35.0F;
    constexpr float PRESET_PASS_ON_THE_RUN_RUN_UP_M = 15.0F;
    constexpr float PRESET_UNDER_PRESSURE_DEFENDER_DISTANCE_M = 1.5F;
    constexpr float PRESET_LONG_BALL_LENGTH_M = 40.0F;
    constexpr float PRESET_SWITCH_OF_PLAY_LENGTH_M = 40.0F;
    constexpr float PRESET_SWITCH_OF_PLAY_DIRECTION_DEG = 90.0F;
    constexpr float PRESET_CROSS_LENGTH_M = 28.0F;
    constexpr float PRESET_CROSS_DIRECTION_DEG = 63.0F;
    constexpr float PRESET_CROSS_ON_THE_RUN_LENGTH_M = 28.0F;
    constexpr float PRESET_CROSS_ON_THE_RUN_DIRECTION_DEG = 63.0F;
    constexpr float PRESET_CROSS_ON_THE_RUN_RUN_UP_M = 12.0F;

    const std::map<std::string, float> base{
        {"Length (m)", PRESET_BASE_LENGTH_M},
        {"Direction (deg)", 0.0F},
        {"Style", 0.0F},
        {"Passer at", 0.0F},
        {"Run-up (m)", 0.0F},
        {"Defender", 0.0F},
        {"Defender distance (m)", PRESET_BASE_DEFENDER_DISTANCE_M}};
    const auto with = [&base](const std::map<std::string, float>& changes)
    {
      std::map<std::string, float> settings = base;
      for (const auto& [name, value] : changes)
      {
        settings[name] = value;
      }
      return settings;
    };
    return {
        {.name = "Short pass",
         .summary = "15 m straight ahead, standing.",
         .settings = base},
        {.name = "Square pass",
         .summary = "15 m to his right, standing.",
         .settings =
             with({{"Direction (deg)", PRESET_SQUARE_PASS_DIRECTION_DEG}})},
        {.name = "Long ground pass",
         .summary = "35 m ahead, forced along the ground.",
         .settings = with({{"Length (m)", PRESET_LONG_GROUND_PASS_LENGTH_M},
                           {"Style", 1.0F}})},
        {.name = "Pass on the run",
         .summary = "15 m ahead after a 15 m run-up.",
         .settings = with({{"Run-up (m)", PRESET_PASS_ON_THE_RUN_RUN_UP_M}})},
        {.name = "Under pressure",
         .summary = "15 m ahead, a defender 1.5 m behind him.",
         .settings = with({{"Defender", 1.0F},
                           {"Defender distance (m)",
                            PRESET_UNDER_PRESSURE_DEFENDER_DISTANCE_M}})},
        {.name = "Long ball",
         .summary = "40 m ahead: lofted, as the AI plays it.",
         .settings = with({{"Length (m)", PRESET_LONG_BALL_LENGTH_M}})},
        {.name = "Switch of play",
         .summary = "40 m to his right, lofted.",
         .settings =
             with({{"Length (m)", PRESET_SWITCH_OF_PLAY_LENGTH_M},
                   {"Direction (deg)", PRESET_SWITCH_OF_PLAY_DIRECTION_DEG}})},
        {.name = "Cross",
         .summary = "From wide in the final third into the box, 28 m.",
         .settings = with({{"Passer at", 1.0F},
                           {"Length (m)", PRESET_CROSS_LENGTH_M},
                           {"Direction (deg)", PRESET_CROSS_DIRECTION_DEG}})},
        {.name = "Cross on the run",
         .summary = "The cross after a 12 m run-up.",
         .settings =
             with({{"Passer at", 1.0F},
                   {"Length (m)", PRESET_CROSS_ON_THE_RUN_LENGTH_M},
                   {"Direction (deg)", PRESET_CROSS_ON_THE_RUN_DIRECTION_DEG},
                   {"Run-up (m)", PRESET_CROSS_ON_THE_RUN_RUN_UP_M}})},
    };
  }

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec passer{
        .name = "Passer",
        .role = PlayerRole::CM,
        .homeTeam = true,
        .stats = {{"Passing", passing}, {"Dribbling", dribbling}},
        .height = REFERENCE_PLAYER_HEIGHT_CM,
        .derived = {}};
    passer.foot = footChoice < CHOICE_MIDPOINT ? Foot::Right : Foot::Left;
    passer.condition = condition;
    std::vector<DrillPlayerSpec> list{passer};
    if (receiverChoice < CHOICE_MIDPOINT)
    {
      list.push_back({.name = "Receiver",
                      .role = PlayerRole::ST,
                      .homeTeam = true,
                      .stats = {},
                      .height = RECEIVER_HEIGHT_CM,
                      .derived = {}});
    }
    if (defenderChoice > CHOICE_MIDPOINT)
    {
      list.push_back({.name = "Defender",
                      .role = PlayerRole::CB,
                      .homeTeam = false,
                      .stats = {},
                      .height = DEFENDER_HEIGHT_CM,
                      .derived = {}});
    }
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    passerId = ids[0];
    hasReceiver = receiverChoice < CHOICE_MIDPOINT;
    receiverId = hasReceiver ? ids[1] : 0;
    hasDefender = defenderChoice > CHOICE_MIDPOINT;
    defenderId = hasDefender ? ids[hasReceiver ? 2 : 1] : 0;
    engine.setStrikeKnobs(knobs);

    // Geometry in metres: he faces up the pitch (+x) and passes from where
    // his run-up ends; the receiver is `length` away at `direction`. In the
    // middle, the run-up and the pass sit centred on the pitch, so a long
    // one stays on it.
    const Vector2F up{.x = 1.0F, .y = 0.0F};
    if (placeChoice < CHOICE_MIDPOINT)
    {
      const float radians = direction * PI / HALF_TURN_DEGREES;
      const float ahead = length * std::cos(radians);
      const float across = length * std::sin(radians);
      const float lowX = std::min(0.0F, runUp + ahead);
      const float highX = std::max(runUp, runUp + ahead);
      passerStart = asPoint(
          {.x = (LENGTH * MIDPOINT_WEIGHT) - ((lowX + highX) * MIDPOINT_WEIGHT),
           .y = (WIDTH * MIDPOINT_WEIGHT) - (across * MIDPOINT_WEIGHT)});
    }
    else
    {
      passerStart =
          at(WIDE_PASS_START_LENGTH_SHARE, WIDE_PASS_START_WIDTH_SHARE);
    }
    passPoint = along(passerStart, up, runUp);
    receiverSpot = along(
        passPoint, rotated(up, direction * PI / HALF_TURN_DEGREES), length);
    receiverSpot =
        PitchPoint{std::clamp(receiverSpot.x, PITCH.atX(PITCH_SAFE_MIN_SHARE),
                              PITCH.atX(PITCH_SAFE_MAX_SHARE)),
                   std::clamp(receiverSpot.y, PITCH.atY(PITCH_SAFE_MIN_SHARE),
                              PITCH.atY(PITCH_SAFE_MAX_SHARE))};
    defenderSpot = along(passPoint, up, -defenderDistance);

    MatchScenario scenario;
    scenario.players.push_back(
        {.playerId = passerId, .position = passerStart, .makingRun = false});
    if (hasReceiver)
    {
      scenario.players.push_back({.playerId = receiverId,
                                  .position = receiverSpot,
                                  .makingRun = false});
    }
    if (hasDefender)
    {
      scenario.players.push_back({.playerId = defenderId,
                                  .position = defenderSpot,
                                  .makingRun = false});
    }
    scenario.carrierId = passerId;
    scenario.ballPosition = passerStart;
    engine.startDrill(scenario);
    // The receiver and the defender hold their spots until the pass.
    if (hasReceiver)
    {
      engine.setDrillTarget(receiverId, receiverSpot, false);
    }
    if (hasDefender)
    {
      engine.setDrillTarget(defenderId, defenderSpot, false);
    }
    if (runUp > 0.0F)
    {
      engine.keepBall(passerId, true,
                      (paceChoice + 1.0F) / CARRY_PACE_LEVEL_COUNT);
      engine.setDrillTarget(
          passerId, along(passerStart, up, runUp + RUN_THROUGH_METRES), true);
      ordered = false;
    }
    else
    {
      order(engine);
    }
    before = engine.getStrikeContacts();
    pass.reset();
    held.reset();
    outcome.clear();
    endAt.reset();
    ballPath.clear();
  }

 private:
  void observeStrike(MatchEngine& engine, const MatchPlayer* passer, double now)
  {
    const MatchBall& ball = engine.getBall();
    // The pass: a strike the contact model has read since the start.
    const StrikeContactStats& now_ = engine.getStrikeContacts();
    for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
    {
      const auto& a = before.byKind.at(kind);
      const auto& b = now_.byKind.at(kind);
      if (b.strikes == a.strikes)
      {
        continue;
      }
      Pass reading;
      reading.at = now;
      reading.kind = static_cast<StrikeKind>(kind);
      reading.carriedOut = b.carriedOut > a.carriedOut;
      reading.angleFailed = b.angleFailed > a.angleFailed;
      reading.weightFailed = b.weightFailed > a.weightFailed;
      reading.angleError =
          static_cast<float>(b.angleErrorSum - a.angleErrorSum);
      reading.weightError =
          static_cast<float>(b.weightErrorSum - a.weightErrorSum);
      reading.elevationError =
          static_cast<float>(b.elevationErrorSum - a.elevationErrorSum);
      reading.angleDifficulty =
          static_cast<float>(b.angleDifficulty - a.angleDifficulty);
      reading.weightDifficulty =
          static_cast<float>(b.weightDifficulty - a.weightDifficulty);
      for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
      {
        reading.factors.at(index) =
            static_cast<float>(b.factorSum.at(index) - a.factorSum.at(index));
      }
      reading.from = ball.position;
      reading.speed = std::hypot(ball.velocity.x, ball.velocity.y);
      reading.lift = ball.velocityZ;
      if (passer != nullptr)
      {
        reading.passerSpeed = speedOf(*passer);
        // Where it went against where his body faced.
        reading.bodyAngle = bodyAngleTo(*passer, ball.velocity);
      }
      reading.held = held;
      pass = reading;
      closest = metresBetween(ball.position, receiverSpot);
      closestHeight = 0.0F;
      closestSpeed = reading.speed;
      flight.start(ball, receiverSpot);
      // Released: the receiver goes for it, the defender presses.
      if (hasReceiver)
      {
        engine.setDrillTarget(receiverId, std::nullopt, false);
      }
      if (hasDefender)
      {
        engine.setDrillTarget(defenderId, std::nullopt, false);
      }
      break;
    }
    if (!pass && now >= STRIKE_TIMEOUT_SECONDS)
    {
      outcome = "no pass";
    }
  }

  void observeFlight(const MatchEngine& engine, double now)
  {
    if (!pass)
    {
      return;
    }
    const MatchBall& ball = engine.getBall();
    flight.observe(ball);
    const float gap = metresBetween(ball.position, receiverSpot);
    if (gap < closest)
    {
      closest = gap;
      closestHeight = ball.z;
      closestSpeed = std::hypot(ball.velocity.x, ball.velocity.y);
    }
    const PlayerID owner = (engine.getPossessionAttachment() != nullptr)
                               ? engine.getPossessionAttachment()->getId()
                               : 0;
    if (!hasReceiver && flight.settled(pass->kind, ball))
    {
      outcome =
          flight.landingPoint()
              ? std::format("landed {:.1f} m off", flight.landingError())
              : std::format("ran {:.1f} m past", flight.closestDistance());
    }
    else if (hasReceiver && owner == receiverId)
    {
      outcome = "received";
    }
    else if (hasDefender && owner == defenderId)
    {
      outcome = "intercepted";
    }
    else if (engine.getState() != MatchState::PLAYING)
    {
      outcome = "out of play";
    }
    else if (now >= pass->at + TIME_LIMIT)
    {
      outcome = "not reached";
    }
  }

 public:
  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    const MatchPlayer* passer = slotOf(engine, passerId);
    if (!ordered && (passer != nullptr) &&
        (metresBetween(passerStart, passer->position) >= runUp ||
         now >= RUN_UP_TIMEOUT_SECONDS))
    {
      order(engine);
    }
    if (engine.getSimulatedSteps() % 2 == 0)
    {
      ballPath.push_back(ball.position);
    }
    // When he decides on it (the pass held for its strike): his body's angle
    // to its line then, and his speed.
    if (!pass && !held && (passer != nullptr) && engine.passHeldFor(passerId))
    {
      held = Held{.at = now,
                  .bodyAngle = bodyAngleToTarget(*passer, ball.position),
                  .speed = speedOf(*passer)};
    }

    if (!pass)
    {
      observeStrike(engine, passer, now);
    }
    else if (outcome.empty())
    {
      observeFlight(engine, now);
    }
    if (!outcome.empty() && !endAt)
    {
      endAt = now;
    }
    return (!endAt || now < *endAt + LINGER_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::line(renderer, draw, passPoint, receiverSpot,
                    REFERENCE_LINE_COLOUR, 1.0F * scale);
    DrillDraw::marker(renderer, draw, passPoint,
                      SECONDARY_MARKER_RADIUS_PIXELS * scale,
                      FEED_MARKER_COLOUR, "pass");
    DrillDraw::marker(renderer, draw, receiverSpot,
                      PRIMARY_MARKER_RADIUS_PIXELS * scale, TARGET_COLOUR,
                      outcome.empty() ? "target" : outcome.c_str());
    if (hasDefender)
    {
      DrillDraw::marker(renderer, draw, defenderSpot,
                        SECONDARY_MARKER_RADIUS_PIXELS * scale,
                        DEFENDER_COLOUR);
    }
    if (pass && flight.landingPoint())
    {
      DrillDraw::marker(renderer, draw, *flight.landingPoint(),
                        PRIMARY_MARKER_RADIUS_PIXELS * scale, LANDING_COLOUR,
                        "down");
    }
    DrillDraw::path(renderer, draw, ballPath, BALL_TRAIL_COLOUR,
                    BALL_TRAIL_WIDTH_PIXELS * scale);
  }

 private:
  void appendPassReport(std::vector<std::string>& lines) const
  {
    if (!pass)
    {
      return;
    }
    constexpr float DEGREES = HALF_TURN_DEGREES / PI;
    lines.push_back(std::format(
        "{} at {:.2f} s, {:.1f} m/s{}: difficulty angle {:.2f}, weight "
        "{:.2f}",
        STRIKE_KIND_NAMES.at(static_cast<std::size_t>(pass->kind)), pass->at,
        pass->speed,
        pass->lift > 0.0F ? std::format(" ({:.1f} up)", pass->lift)
                          : std::string(),
        pass->angleDifficulty, pass->weightDifficulty));
    lines.push_back(std::format(
        "Body {:.0f} deg off the pass when struck{}", pass->bodyAngle,
        pass->held
            ? std::format(" ({:.0f} deg when he decided, {:.2f} s "
                          "before; {:.1f} -> {:.1f} m/s)",
                          pass->held->bodyAngle, pass->at - pass->held->at,
                          pass->held->speed, pass->passerSpeed)
            : std::string(" (struck at once)")));
    if (pass->carriedOut)
    {
      lines.push_back(std::format(
          "Angle {}{:.1f} deg (elevation {:+.1f}), weight {}{:+.2f} m/s",
          pass->angleFailed ? "FAILED " : "", pass->angleError * DEGREES,
          pass->elevationError * DEGREES, pass->weightFailed ? "FAILED " : "",
          pass->weightError));
    }
    else
    {
      lines.emplace_back(
          "Measured only: this kind of pass keeps the old error.");
    }
    lines.push_back(
        std::format("Closest to the target {:.2f} m ({:.1f} m up, {:.1f} m/s)",
                    closest, closestHeight, closestSpeed));
    if (!hasReceiver)
    {
      lines.push_back(std::format(
          "Strike alone: {:.1f} m from where struck, {}",
          flight.targetDistance(),
          flight.miss(pass->kind)
              ? std::format("missed by {:.2f} m ({})", *flight.miss(pass->kind),
                            PassFlight::flies(pass->kind) ? "where it landed"
                                                          : "as it ran past")
              : std::string("not down yet")));
    }
  }

 public:
  [[nodiscard]] std::vector<std::string> report() const override
  {
    static constexpr std::array<const char*, 5> STYLES = {
        "as the AI would", "along the ground", "lofted", "driven", "chipped"};
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.0f} m at {:+.0f} deg, {} · from {} · run-up {:.0f} m{}{}", length,
        direction, STYLES.at(static_cast<std::size_t>(styleChoice)),
        placeChoice < CHOICE_MIDPOINT ? "the middle" : "wide", runUp,
        hasDefender ? std::format(" · defender {:.1f} m", defenderDistance)
                    : std::string(),
        hasReceiver
            ? std::string()
            : std::format(
                  " · strike alone{}{}",
                  sideRevs != 0.0F
                      ? std::format(", sidespin {:+.1f} rev/s", sideRevs)
                      : std::string(),
                  topRevs != 0.0F
                      ? std::format(", topspin {:+.1f} rev/s", topRevs)
                      : std::string())));
    lines.push_back(std::format(
        "Passer: Passing {:.0f}, Dribbling {:.0f}, condition {:.0f}, {} foot",
        passing, dribbling, condition,
        footChoice < CHOICE_MIDPOINT ? "right" : "left"));
    if (pass)
    {
      appendPassReport(lines);
    }
    if (!outcome.empty())
    {
      lines.push_back("OUTCOME  " + outcome);
    }
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty())
    {
      return {};
    }
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{
        {.name = "Passed", .value = flag(pass.has_value())}};
    if (!pass)
    {
      return out;
    }
    constexpr float DEGREES = HALF_TURN_DEGREES / PI;
    out.push_back(
        {.name = "Kind (0 ground, 1 lofted, 2 cross, 3 shot, 7 driven, 8 chip)",
         .value = static_cast<double>(pass->kind)});
    out.push_back({.name = "Received", .value = flag(outcome == "received")});
    out.push_back(
        {.name = "Intercepted", .value = flag(outcome == "intercepted")});
    out.push_back(
        {.name = "Out of play", .value = flag(outcome == "out of play")});
    out.push_back({.name = "Miss at target (m)", .value = closest});
    out.push_back({.name = "Height at target (m)", .value = closestHeight});
    out.push_back({.name = "Speed at target (m/s)", .value = closestSpeed});
    if (!hasReceiver)
    {
      flight.addMetrics(pass->kind, out);
    }
    out.push_back({.name = "Launch speed (m/s)", .value = pass->speed});
    out.push_back({.name = "Launch lift (m/s)", .value = pass->lift});
    out.push_back({.name = "Passer speed (m/s)", .value = pass->passerSpeed});
    // Turning to pass: how far round he got, in how long, at what cost.
    out.push_back({.name = "Body off the pass at strike (deg)",
                   .value = pass->bodyAngle});
    if (pass->held)
    {
      out.push_back({.name = "Body off the pass at decision (deg)",
                     .value = pass->held->bodyAngle});
      out.push_back({.name = "Turned (deg)",
                     .value = pass->held->bodyAngle - pass->bodyAngle});
      out.push_back({.name = "Decision to strike (s)",
                     .value = pass->at - pass->held->at});
      out.push_back({.name = "Passer speed at decision (m/s)",
                     .value = pass->held->speed});
      out.push_back({.name = "Speed lost (m/s)",
                     .value = pass->held->speed - pass->passerSpeed});
    }
    out.push_back({.name = "Angle difficulty", .value = pass->angleDifficulty});
    out.push_back(
        {.name = "Weight difficulty", .value = pass->weightDifficulty});
    if (pass->carriedOut)
    {
      out.push_back({.name = "Angle failed", .value = flag(pass->angleFailed)});
      out.push_back(
          {.name = "Weight failed", .value = flag(pass->weightFailed)});
      out.push_back({.name = "Abs angle error (deg)",
                     .value = pass->angleError * DEGREES});
      out.push_back({.name = "Weight error (m/s)", .value = pass->weightError});
      out.push_back({.name = "Elevation error (deg, + higher)",
                     .value = pass->elevationError * DEGREES});
    }
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
    {
      out.push_back(
          {.name = "Condition: " + std::string(CONTACT_FACTOR_LABELS.at(index)),
           .value = pass->factors.at(index)});
    }
    return out;
  }

 private:
  /** The pass when he decided on it. */
  struct Held
  {
    double at = 0.0;
    float bodyAngle = 0.0F;
    float speed = 0.0F;
  };
  struct Pass
  {
    double at = 0.0;
    StrikeKind kind = StrikeKind::GROUND_PASS;
    bool carriedOut = false;
    bool angleFailed = false;
    bool weightFailed = false;
    float angleError = 0.0F;
    float weightError = 0.0F;
    float elevationError = 0.0F;
    float angleDifficulty = 0.0F;
    float weightDifficulty = 0.0F;
    std::array<float, CONTACT_FACTOR_COUNT> factors{};
    PitchPoint from;
    float speed = 0.0F;
    float lift = 0.0F;
    float passerSpeed = 0.0F;
    std::optional<Held> held;
    /** His body's angle to where he sent it (deg). */
    float bodyAngle = 0.0F;
  };

  /** His body's angle (deg) to the line from the ball to the receiver. */
  [[nodiscard]] float bodyAngleToTarget(const MatchPlayer& player,
                                        PitchPoint ballAt) const
  {
    return bodyAngleTo(player, Vector2F{.x = receiverSpot.x - ballAt.x,
                                        .y = receiverSpot.y - ballAt.y});
  }

  /** His body's angle (deg) to a direction (metres). */
  static float bodyAngleTo(const MatchPlayer& player, Vector2F line)
  {
    const PitchPoint body{std::cos(player.facingAngle),
                          std::sin(player.facingAngle)};
    const float lengths =
        std::hypot(body.x, body.y) * std::hypot(line.x, line.y);
    if (lengths <= 0.0F)
    {
      return 0.0F;
    }
    return std::acos(std::clamp((body.x * line.x + body.y * line.y) / lengths,
                                -1.0F, 1.0F)) *
           HALF_TURN_DEGREES / PI;
  }

  /** The pass is ordered: he plays it as soon as he can act. */
  void order(MatchEngine& engine)
  {
    ordered = true;
    if (runUp > 0.0F)
    {
      engine.keepBall(passerId, false);
      engine.setDrillTarget(passerId, std::nullopt, false);
    }
    const auto style = static_cast<ForcedPassStyle>(styleChoice);
    if (hasReceiver)
    {
      engine.forcePass(passerId, receiverId, style);
      return;
    }
    std::optional<BallSpin> spin;
    if (sideRevs != 0.0F || topRevs != 0.0F)
    {
      spin = BallSpin{.side = sideRevs * FULL_TURN_RADIANS,
                      .top = topRevs * FULL_TURN_RADIANS};
    }
    engine.forcePassTo(passerId, receiverSpot, style, spin);
  }

  // Pass
  float length = DEFAULT_LENGTH;
  float direction = 0.0F;
  float styleChoice = 0.0F;
  float placeChoice = 0.0F;
  float receiverChoice = 0.0F;
  float sideRevs = 0.0F;
  float topRevs = 0.0F;
  // Passer
  float runUp = 0.0F;
  float paceChoice = DEFAULT_PACE_CHOICE;
  float passing = DEFAULT_PASSING;
  float dribbling = DEFAULT_DRIBBLING;
  float condition = DEFAULT_CONDITION;
  float footChoice = 0.0F;
  // Defender
  float defenderChoice = 0.0F;
  float defenderDistance = DEFAULT_DEFENDER_DISTANCE;
  // The strike's tuning, as this drill plays it.
  StrikeKnobs knobs;

  PlayerID passerId = 0;
  PlayerID receiverId = 0;
  PlayerID defenderId = 0;
  bool hasReceiver = true;
  bool hasDefender = false;
  bool ordered = false;
  PassFlight flight;
  PitchPoint passerStart;
  PitchPoint passPoint;
  PitchPoint receiverSpot;
  PitchPoint defenderSpot;
  StrikeContactStats before;
  std::optional<Pass> pass;
  std::optional<Held> held;
  float closest = 0.0F;
  float closestHeight = 0.0F;
  float closestSpeed = 0.0F;
  std::string outcome;
  std::optional<double> endAt;
  std::vector<PitchPoint> ballPath;
};

// --- First-time pass --------------------------------------------------------

/**
 * A ground ball played to a player, who passes it on to a target with
 * nobody there (MatchEngine::forcePassTo): first time, as it reaches him,
 * or after a first touch, to compare the two. The ball's pace and the angle
 * it comes from against where he sends it; his Passing and First Touch.
 */
class FirstTimePassDrill final : public Drill
{
  static constexpr float BALL_SPEED_MIN = 2.0F;
  static constexpr float BALL_SPEED_MAX = 30.0F;
  static constexpr float FROM_ANGLE_MIN = -180.0F;
  static constexpr float FROM_ANGLE_MAX = 180.0F;
  static constexpr float FEED_DISTANCE_MIN = 5.0F;
  static constexpr float FEED_DISTANCE_MAX = 40.0F;
  static constexpr float PASSES_BY_MIN = -1.5F;
  static constexpr float PASSES_BY_MAX = 1.5F;
  static constexpr float BOUNCE_MAX = 3.0F;
  static constexpr float LENGTH_MIN = 3.0F;
  static constexpr float LENGTH_MAX = 60.0F;
  static constexpr float DIRECTION_MIN = -180.0F;
  static constexpr float DIRECTION_MAX = 180.0F;
  static constexpr float STYLE_CHOICE_MAX = 4.0F;
  static constexpr float SIDE_REVS_MIN = -10.0F;
  static constexpr float SIDE_REVS_MAX = 10.0F;
  static constexpr float RUN_TO_MEET_MAX = 10.0F;
  static constexpr float PASSING_MAX = 100.0F;
  static constexpr float FIRST_TOUCH_MAX = 100.0F;
  static constexpr float PACE_MAX = 100.0F;
  static constexpr float DEFAULT_BALL_SPEED = 12.0F;
  static constexpr float DEFAULT_FROM_ANGLE = 90.0F;
  static constexpr float DEFAULT_FEED_DISTANCE = 20.0F;
  static constexpr float DEFAULT_LENGTH = 15.0F;
  static constexpr float DEFAULT_PASSING = 70.0F;
  static constexpr float DEFAULT_FIRST_TOUCH = 70.0F;
  static constexpr float DEFAULT_PACE = 70.0F;
  static constexpr ImU32 REFERENCE_LINE_COLOUR = IM_COL32(255, 255, 255, 70);
  static constexpr ImU32 FEED_MARKER_COLOUR = IM_COL32(255, 255, 255, 200);
  static constexpr ImU32 TARGET_COLOUR = IM_COL32(90, 220, 120, 220);
  static constexpr ImU32 CONTROLLED_BALL_COLOUR = IM_COL32(90, 170, 255, 255);
  static constexpr ImU32 BALL_TRAIL_COLOUR = IM_COL32(255, 230, 120, 160);
  static constexpr float HALF_TURN_DEGREES = 180.0F;

 public:
  /** Where he meets the ball (pitch), facing up the pitch. */
  static inline const PitchPoint MEET =
      at(PITCH_MIDPOINT_SHARE, PITCH_MIDPOINT_SHARE);
  /** No pass this long after the ball is played: the run ends. */
  static constexpr double TIME_LIMIT = 8.0;
  /** After the pass: the run ends once the ball has stopped, or this long
   * after it. */
  static constexpr double PASS_LIMIT = 6.0;
  static constexpr double LINGER_SECONDS = 0.3;
  /** The ball this much further from the target than it got: gone by. */
  static constexpr float PAST_TARGET_METRES = 3.0F;

  [[nodiscard]] const char* name() const override { return "First-time pass"; }
  [[nodiscard]] const char* summary() const override
  {
    return "A ball played to a player, who passes it on to a target (the "
           "Binturong body): first time as it reaches him, or after a first "
           "touch. The ball's pace and the angle it comes from, where he "
           "sends it, his Passing and First Touch. Measures whether he got to "
           "play it first time (a ball off the ground, he takes a touch), how "
           "the strike came off (each job's difficulty and error, the "
           "conditions) and where the pass went.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    const auto add = [&list](DrillParameter parameter, const char* section)
    {
      parameter.section = section;
      list.push_back(std::move(parameter));
    };
    add(setting("Ball speed (m/s)", &ballSpeed,
                {.min = BALL_SPEED_MIN, .max = BALL_SPEED_MAX}, false,
                "Pace when it is played to him."),
        "Ball");
    add(setting("Comes from (deg)", &fromAngle,
                {.min = FROM_ANGLE_MIN, .max = FROM_ANGLE_MAX}, true,
                "Where it is played from, against the way he faces (up the "
                "pitch): 0 in front of him, 90 from his right, -90 from his "
                "left, 180 from behind."),
        "Ball");
    add(setting("Feed distance (m)", &feedDistance,
                {.min = FEED_DISTANCE_MIN, .max = FEED_DISTANCE_MAX}, false),
        "Ball");
    add(setting("Passes him by (m)", &passesBy,
                {.min = PASSES_BY_MIN, .max = PASSES_BY_MAX}, false,
                "Sideways offset of its line from where he meets it: 0 "
                "straight at him."),
        "Ball");
    add(setting("Bounce (m/s up)", &bounce, {.min = 0.0F, .max = BOUNCE_MAX},
                false,
                "Upward speed when played: 0 along the ground. A ball off "
                "the ground when it reaches him is taken with a touch."),
        "Ball");

    DrillParameter play =
        setting("Play it", &playChoice, {.min = 0.0F, .max = 1.0F}, true,
                "First time: he plays it on as it reaches him.\nAfter a "
                "touch: he controls it, then passes.");
    play.choices = {"First time", "After a touch"};
    add(play, "Pass");
    add(setting("Length (m)", &length, {.min = LENGTH_MIN, .max = LENGTH_MAX},
                false, "From where he meets the ball to the target."),
        "Pass");
    add(setting("Direction (deg)", &direction,
                {.min = DIRECTION_MIN, .max = DIRECTION_MAX}, true,
                "Where the target is against the way he faces (up the "
                "pitch): 0 straight ahead, 90 to his right, -90 to his left, "
                "180 behind him."),
        "Pass");
    DrillParameter style = setting(
        "Style", &styleChoice, {.min = 0.0F, .max = STYLE_CHOICE_MAX}, true,
        "As the AI would (by its length), or forced along the "
        "ground, lofted, driven (low and hard) or chipped.");
    style.choices = {"As the AI would", "Ground", "Lofted", "Driven", "Chip"};
    add(style, "Pass");
    add(setting("Sidespin (rev/s)", &sideRevs,
                {.min = SIDE_REVS_MIN, .max = SIDE_REVS_MAX}, false,
                "The sidespin he means (+ curls it to his right). 0: the "
                "body's default."),
        "Pass");

    add(setting("Run to meet (m)", &runToMeet,
                {.min = 0.0F, .max = RUN_TO_MEET_MAX}, false,
                "He starts this far off the ball's line and goes to it."),
        "Player");
    add(setting("Passing", &passing, {.min = 1.0F, .max = PASSING_MAX}, true),
        "Player");
    add(setting("First Touch", &firstTouch,
                {.min = 1.0F, .max = FIRST_TOUCH_MAX}, true,
                "His First Touch rating (overrides the derived stat): his "
                "touch when he takes one."),
        "Player");
    add(setting("Pace", &pace, {.min = 1.0F, .max = PACE_MAX}, true), "Player");
    DrillParameter foot =
        setting("Foot", &footChoice, {.min = 0.0F, .max = 1.0F}, true);
    foot.choices = {"Right", "Left"};
    add(foot, "Player");
    return list;
  }

  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    constexpr float PRESET_BASE_BALL_SPEED_M_S = 12.0F;
    constexpr float PRESET_BASE_COMES_FROM_DEG = 90.0F;
    constexpr float PRESET_BASE_FEED_DISTANCE_M = 20.0F;
    constexpr float PRESET_BASE_LENGTH_M = 15.0F;
    constexpr float PRESET_SET_IT_BACK_LENGTH_M = 12.0F;
    constexpr float PRESET_SET_IT_BACK_DIRECTION_DEG = 30.0F;
    constexpr float PRESET_ROUND_THE_CORNER_COMES_FROM_DEG = 135.0F;
    constexpr float PRESET_DRIVEN_IN_BALL_SPEED_M_S = 20.0F;
    constexpr float PRESET_STEP_TO_IT_BALL_SPEED_M_S = 8.0F;
    constexpr float PRESET_STEP_TO_IT_RUN_TO_MEET_M = 3.0F;
    constexpr float PRESET_BOUNCING_BALL_BOUNCE_M_S_UP = 2.8F;
    constexpr float PRESET_BOUNCING_BALL_FEED_DISTANCE_M = 7.0F;

    const std::map<std::string, float> base{
        {"Ball speed (m/s)", PRESET_BASE_BALL_SPEED_M_S},
        {"Comes from (deg)", PRESET_BASE_COMES_FROM_DEG},
        {"Feed distance (m)", PRESET_BASE_FEED_DISTANCE_M},
        {"Passes him by (m)", 0.0F},
        {"Bounce (m/s up)", 0.0F},
        {"Play it", 0.0F},
        {"Length (m)", PRESET_BASE_LENGTH_M},
        {"Direction (deg)", 0.0F},
        {"Style", 1.0F},
        {"Run to meet (m)", 0.0F}};
    const auto with = [&base](const std::map<std::string, float>& changes)
    {
      std::map<std::string, float> settings = base;
      for (const auto& [name, value] : changes)
      {
        settings[name] = value;
      }
      return settings;
    };
    return {
        {.name = "Square ball, played on",
         .summary = "12 m/s from his right, on 15 m ahead.",
         .settings = base},
        {.name = "The same, after a touch",
         .summary = "As above, controlled first.",
         .settings = with({{"Play it", 1.0F}})},
        {.name = "Set it back",
         .summary = "12 m/s from in front, back 12 m to one side.",
         .settings =
             with({{"Comes from (deg)", 0.0F},
                   {"Length (m)", PRESET_SET_IT_BACK_LENGTH_M},
                   {"Direction (deg)", PRESET_SET_IT_BACK_DIRECTION_DEG}})},
        {.name = "Round the corner",
         .summary = "From behind his right, on up the line.",
         .settings = with(
             {{"Comes from (deg)", PRESET_ROUND_THE_CORNER_COMES_FROM_DEG}})},
        {.name = "Driven in",
         .summary = "20 m/s from his right, on 15 m ahead.",
         .settings =
             with({{"Ball speed (m/s)", PRESET_DRIVEN_IN_BALL_SPEED_M_S}})},
        {.name = "Step to it",
         .summary = "8 m/s, he starts 3 m off its line.",
         .settings =
             with({{"Ball speed (m/s)", PRESET_STEP_TO_IT_BALL_SPEED_M_S},
                   {"Run to meet (m)", PRESET_STEP_TO_IT_RUN_TO_MEET_M}})},
        {.name = "Bouncing ball",
         .summary = "It reaches him on a hop: he takes a touch.",
         .settings = with(
             {{"Bounce (m/s up)", PRESET_BOUNCING_BALL_BOUNCE_M_S_UP},
              {"Feed distance (m)", PRESET_BOUNCING_BALL_FEED_DISTANCE_M}})},
    };
  }

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec player{.name = "Player",
                           .role = PlayerRole::CM,
                           .homeTeam = true,
                           .stats = {{"Passing", passing}, {"Pace", pace}},
                           .height = REFERENCE_PLAYER_HEIGHT_CM,
                           .derived = {{DerivedStat::FIRST_TOUCH, firstTouch}}};
    player.foot = footChoice < CHOICE_MIDPOINT ? Foot::Right : Foot::Left;
    return {player};
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    playerId = ids[0];
    engine.setFirstTouchKnobs(FirstTouchKnobs{});
    engine.setStrikeKnobs(StrikeKnobs{});

    // Geometry in metres: he faces up the pitch (+x); the ball comes from
    // fromAngle round from in front of him, along a line passesBy to the
    // side of the meeting point; the target is length from there.
    const Vector2F up{.x = 1.0F, .y = 0.0F};
    from = rotated(up, fromAngle * PI / HALF_TURN_DEGREES);
    travel = Vector2F{.x = -from.x, .y = -from.y};
    side = rotated(travel, PI * QUARTER_TURN_PI_SHARE);
    feedFrom = along(along(MEET, side, passesBy), from, feedDistance);
    playerStart = along(MEET, side, -runToMeet);
    target =
        along(MEET, rotated(up, direction * PI / HALF_TURN_DEGREES), length);
    target = PitchPoint{std::clamp(target.x, PITCH.atX(PITCH_SAFE_MIN_SHARE),
                                   PITCH.atX(PITCH_SAFE_MAX_SHARE)),
                        std::clamp(target.y, PITCH.atY(PITCH_SAFE_MIN_SHARE),
                                   PITCH.atY(PITCH_SAFE_MAX_SHARE))};

    MatchScenario scenario;
    scenario.players.push_back(
        {.playerId = playerId, .position = playerStart, .makingRun = false});
    scenario.carrierId = 0;
    scenario.ballPosition = feedFrom;
    engine.startDrill(scenario);
    engine.forcePassTo(
        playerId, target, static_cast<ForcedPassStyle>(styleChoice),
        sideRevs != 0.0F
            ? std::optional<BallSpin>(
                  BallSpin{.side = sideRevs * FULL_TURN_RADIANS, .top = 0.0F})
            : std::nullopt,
        std::nullopt, playChoice < CHOICE_MIDPOINT);
    engine.feedBall(feedFrom,
                    {.x = travel.x * ballSpeed, .y = travel.y * ballSpeed},
                    bounce, playerId);
    fedAt = secondsOf(engine);
    before = engine.getStrikeContacts();
    touchedFirst = false;
    pass.reset();
    outcome.clear();
    endAt.reset();
    lastBallVelocity = {.x = travel.x * ballSpeed, .y = travel.y * ballSpeed};
    ballPath = {feedFrom};
  }

 private:
  void observeStrike(const MatchEngine& engine, const MatchPlayer* player,
                     double now)
  {
    const MatchBall& ball = engine.getBall();
    // His first touch, if he takes one: the ball his before he passes.
    if ((engine.getPossessionAttachment() != nullptr) &&
        engine.getPossessionAttachment()->getId() == playerId)
    {
      touchedFirst = true;
    }
    const StrikeContactStats& after = engine.getStrikeContacts();
    for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
    {
      const auto& a = before.byKind.at(kind);
      const auto& b = after.byKind.at(kind);
      if (b.strikes == a.strikes)
      {
        continue;
      }
      Pass reading;
      reading.at = now;
      reading.kind = static_cast<StrikeKind>(kind);
      reading.firstTime = !touchedFirst;
      reading.carriedOut = b.carriedOut > a.carriedOut;
      reading.angleFailed = b.angleFailed > a.angleFailed;
      reading.weightFailed = b.weightFailed > a.weightFailed;
      reading.angleError =
          static_cast<float>(b.angleErrorSum - a.angleErrorSum);
      reading.weightError =
          static_cast<float>(b.weightErrorSum - a.weightErrorSum);
      reading.angleDifficulty =
          static_cast<float>(b.angleDifficulty - a.angleDifficulty);
      reading.weightDifficulty =
          static_cast<float>(b.weightDifficulty - a.weightDifficulty);
      for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
      {
        reading.factors.at(index) =
            static_cast<float>(b.factorSum.at(index) - a.factorSum.at(index));
      }
      reading.speed = std::hypot(ball.velocity.x, ball.velocity.y);
      reading.lift = ball.velocityZ;
      // The ball as it came in (a step before), against where it went.
      reading.incoming = std::hypot(lastBallVelocity.x, lastBallVelocity.y);
      if (reading.incoming > MIN_REPORTED_CONTACT_SPEED &&
          reading.speed > MIN_REPORTED_CONTACT_SPEED)
      {
        reading.redirect =
            turnDegrees({.x = lastBallVelocity.x / reading.incoming,
                         .y = lastBallVelocity.y / reading.incoming},
                        {.x = ball.velocity.x / reading.speed,
                         .y = ball.velocity.y / reading.speed});
      }
      if (player != nullptr)
      {
        reading.playerSpeed = speedOf(*player);
        const PitchPoint body{std::cos(player->facingAngle),
                              std::sin(player->facingAngle)};
        const Vector2F way = ball.velocity;
        const float lengths =
            std::hypot(body.x, body.y) * std::hypot(way.x, way.y);
        if (lengths > 0.0F)
        {
          reading.bodyAngle =
              std::acos(std::clamp((body.x * way.x + body.y * way.y) / lengths,
                                   -1.0F, 1.0F)) *
              HALF_TURN_DEGREES / PI;
        }
      }
      reading.from = ball.position;
      pass = reading;
      closest = metresBetween(ball.position, target);
      closestSpeed = reading.speed;
      flight.start(ball, target);
      break;
    }
    if (!pass && now >= fedAt + TIME_LIMIT)
    {
      outcome = "no pass";
    }
  }

 public:
  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    const MatchPlayer* player = slotOf(engine, playerId);
    if (engine.getSimulatedSteps() % 2 == 0)
    {
      ballPath.push_back(ball.position);
    }

    if (!pass && outcome.empty())
    {
      observeStrike(engine, player, now);
    }
    else if (pass && outcome.empty())
    {
      flight.observe(ball);
      const float gap = metresBetween(ball.position, target);
      if (gap < closest)
      {
        closest = gap;
        closestSpeed = std::hypot(ball.velocity.x, ball.velocity.y);
      }
      // A pass that flies runs on until it has come down.
      if (engine.getState() != MatchState::PLAYING)
      {
        outcome = "out of play";
      }
      else if (flight.settled(pass->kind, ball))
      {
        outcome =
            gap > closest + PAST_TARGET_METRES ? "past the target" : "stopped";
      }
      else if (now >= pass->at + PASS_LIMIT)
      {
        outcome = "still rolling";
      }
    }
    if (!pass)
    {
      lastBallVelocity = ball.velocity;
    }
    if (!outcome.empty() && !endAt)
    {
      endAt = now;
    }
    return (!endAt || now < *endAt + LINGER_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::line(renderer, draw, feedFrom, MEET, REFERENCE_LINE_COLOUR,
                    1.0F * scale);
    DrillDraw::line(renderer, draw, MEET, target, REFERENCE_LINE_COLOUR,
                    1.0F * scale);
    DrillDraw::marker(renderer, draw, feedFrom,
                      PRIMARY_MARKER_RADIUS_PIXELS * scale, FEED_MARKER_COLOUR,
                      "feed");
    DrillDraw::marker(renderer, draw, MEET,
                      SECONDARY_MARKER_RADIUS_PIXELS * scale, TARGET_COLOUR,
                      "meet");
    DrillDraw::marker(
        renderer, draw, target, PRIMARY_MARKER_RADIUS_PIXELS * scale,
        CONTROLLED_BALL_COLOUR, outcome.empty() ? "target" : outcome.c_str());
    DrillDraw::path(renderer, draw, ballPath, BALL_TRAIL_COLOUR,
                    BALL_TRAIL_WIDTH_PIXELS * scale);
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    static constexpr std::array<const char*, 5> STYLES = {
        "as the AI would", "along the ground", "lofted", "driven", "chipped"};
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.0f} m/s from {:+.0f} deg, {:.0f} m out, {:+.1f} m by{} · on {:.0f} "
        "m "
        "at {:+.0f} deg, {}, {}",
        ballSpeed, fromAngle, feedDistance, passesBy,
        bounce > 0.0F ? std::format(", {:.1f} m/s up", bounce) : std::string(),
        length, direction, STYLES.at(static_cast<std::size_t>(styleChoice)),
        playChoice < CHOICE_MIDPOINT ? "first time" : "after a touch"));
    lines.push_back(std::format(
        "Player: Passing {:.0f}, First Touch {:.0f}, Pace {:.0f}, {} foot · "
        "run to meet {:.1f} m",
        passing, firstTouch, pace,
        footChoice < CHOICE_MIDPOINT ? "right" : "left", runToMeet));
    if (pass)
    {
      constexpr float DEGREES = HALF_TURN_DEGREES / PI;
      lines.push_back(std::format(
          "{} {} at {:.2f} s: in at {:.1f} m/s, out at {:.1f} m/s{}, turned "
          "{:.0f} deg; body {:.0f} deg off it, moving {:.1f} m/s",
          pass->firstTime ? "First time:" : "After a touch:",
          STRIKE_KIND_NAMES.at(static_cast<std::size_t>(pass->kind)),
          pass->at - fedAt, pass->incoming, pass->speed,
          pass->lift > 0.0F ? std::format(" ({:.1f} up)", pass->lift)
                            : std::string(),
          pass->redirect, pass->bodyAngle, pass->playerSpeed));
      lines.push_back(std::format(
          "Difficulty angle {:.2f}, weight {:.2f} · angle {}{:.1f} deg, "
          "weight {}{:+.2f} m/s",
          pass->angleDifficulty, pass->weightDifficulty,
          pass->angleFailed ? "FAILED " : "", pass->angleError * DEGREES,
          pass->weightFailed ? "FAILED " : "", pass->weightError));
      lines.push_back(std::format("Closest to the target {:.2f} m ({:.1f} m/s)",
                                  closest, closestSpeed));
    }
    if (!outcome.empty())
    {
      lines.push_back("OUTCOME  " + outcome);
    }
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty())
    {
      return {};
    }
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{
        {.name = "Passed", .value = flag(pass.has_value())}};
    if (!pass)
    {
      return out;
    }
    constexpr float DEGREES = HALF_TURN_DEGREES / PI;
    out.push_back({.name = "First time", .value = flag(pass->firstTime)});
    out.push_back(
        {.name = "Kind (0 ground, 1 lofted, 2 cross, 3 shot, 7 driven, 8 chip)",
         .value = static_cast<double>(pass->kind)});
    out.push_back({.name = "Feed to pass (s)", .value = pass->at - fedAt});
    out.push_back({.name = "Incoming pace (m/s)", .value = pass->incoming});
    out.push_back({.name = "Turned (deg)", .value = pass->redirect});
    out.push_back(
        {.name = "Body off the pass (deg)", .value = pass->bodyAngle});
    out.push_back({.name = "Player speed (m/s)", .value = pass->playerSpeed});
    out.push_back({.name = "Miss at target (m)", .value = closest});
    out.push_back({.name = "Speed at target (m/s)", .value = closestSpeed});
    flight.addMetrics(pass->kind, out);
    out.push_back({.name = "Launch speed (m/s)", .value = pass->speed});
    out.push_back({.name = "Angle difficulty", .value = pass->angleDifficulty});
    out.push_back(
        {.name = "Weight difficulty", .value = pass->weightDifficulty});
    if (pass->carriedOut)
    {
      out.push_back({.name = "Angle failed", .value = flag(pass->angleFailed)});
      out.push_back(
          {.name = "Weight failed", .value = flag(pass->weightFailed)});
      out.push_back({.name = "Abs angle error (deg)",
                     .value = pass->angleError * DEGREES});
      out.push_back({.name = "Weight error (m/s)", .value = pass->weightError});
    }
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
    {
      out.push_back(
          {.name = "Condition: " + std::string(CONTACT_FACTOR_LABELS.at(index)),
           .value = pass->factors.at(index)});
    }
    return out;
  }

 private:
  struct Pass
  {
    double at = 0.0;
    StrikeKind kind = StrikeKind::GROUND_PASS;
    bool firstTime = false;
    bool carriedOut = false;
    bool angleFailed = false;
    bool weightFailed = false;
    float angleError = 0.0F;
    float weightError = 0.0F;
    float angleDifficulty = 0.0F;
    float weightDifficulty = 0.0F;
    std::array<float, CONTACT_FACTOR_COUNT> factors{};
    PitchPoint from;
    float speed = 0.0F;
    float lift = 0.0F;
    float incoming = 0.0F;
    float redirect = 0.0F;
    float bodyAngle = 0.0F;
    float playerSpeed = 0.0F;
  };

  // Ball
  float ballSpeed = DEFAULT_BALL_SPEED;
  float fromAngle = DEFAULT_FROM_ANGLE;
  float feedDistance = DEFAULT_FEED_DISTANCE;
  float passesBy = 0.0F;
  float bounce = 0.0F;
  // Pass
  float playChoice = 0.0F;
  float length = DEFAULT_LENGTH;
  float direction = 0.0F;
  float styleChoice = 1.0F;
  float sideRevs = 0.0F;
  // Player
  float runToMeet = 0.0F;
  float passing = DEFAULT_PASSING;
  float firstTouch = DEFAULT_FIRST_TOUCH;
  float pace = DEFAULT_PACE;
  float footChoice = 0.0F;

  PlayerID playerId = 0;
  Vector2F from{.x = -1.0F, .y = 0.0F};
  Vector2F travel{.x = 1.0F, .y = 0.0F};
  Vector2F side{.x = 0.0F, .y = 1.0F};
  PitchPoint feedFrom;
  PitchPoint playerStart;
  PitchPoint target;
  double fedAt = 0.0;
  StrikeContactStats before;
  bool touchedFirst = false;
  std::optional<Pass> pass;
  float closest = 0.0F;
  float closestSpeed = 0.0F;
  PassFlight flight;
  std::string outcome;
  std::optional<double> endAt;
  Vector2F lastBallVelocity{};
  std::vector<PitchPoint> ballPath;
};

// --- Passes to a target ------------------------------------------------------

/**
 * A pass from the penalty spot to a target on the line towards the centre
 * circle, with nobody there (MatchEngine::forcePassTo): weighted as for a
 * man standing on it, and a lofted one aimed to land on it. The passer, the
 * strike's reading and the knobs of its kind; each drill judges where the
 * ball goes.
 */
class PassTargetDrill : public Drill
{
  static constexpr double STRIKE_TIMEOUT_SECONDS = 10.0;
  static constexpr float PASSING_MAX = 100.0F;
  static constexpr float CONDITION_MIN = 30.0F;
  static constexpr float CONDITION_MAX = 100.0F;
  static constexpr float DEFAULT_PASSING = 70.0F;
  static constexpr float DEFAULT_CONDITION = 100.0F;
  static constexpr float HALF_TURN_DEGREES = 180.0F;

 public:
  /** A run ends this long after its outcome, or with no outcome
   * TIME_LIMIT seconds after the pass. */
  static constexpr double LINGER_SECONDS = 0.5;
  static constexpr double TIME_LIMIT = 8.0;
  /** The penalty spot (the home side's own), and the way to the centre
   * circle from it (up the pitch, metres). */
  static inline const PitchPoint SPOT =
      PitchPoint{MatchTuning::Pitch::PENALTY_SPOT_METRES,
                 MatchPitch::matchSize().atY(MatchTuning::Pitch::CENTRE)};
  static constexpr Vector2F UP{.x = 1.0F, .y = 0.0F};

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec passer{.name = "Passer",
                           .role = PlayerRole::CM,
                           .homeTeam = true,
                           .stats = {{"Passing", passing}},
                           .height = REFERENCE_PLAYER_HEIGHT_CM,
                           .derived = {}};
    passer.foot = footChoice < CHOICE_MIDPOINT ? Foot::Right : Foot::Left;
    passer.condition = condition;
    return {passer};
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    preparePasser(engine, ids[0]);
    MatchScenario scenario;
    scenario.players.push_back(
        {.playerId = passerId, .position = SPOT, .makingRun = false});
    scenario.carrierId = passerId;
    scenario.ballPosition = SPOT;
    engine.startDrill(scenario);
    engine.forcePassTo(passerId, target(), style(), spin(), arrivalSpeed());
    beginRun(engine);
    layKit(engine);
  }

  /** The drill's kit laid on the pitch (none by default). */
  virtual void layKit(MatchEngine& /*engine*/) const {}

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    if (engine.getSimulatedSteps() % 2 == 0)
    {
      ballPath.push_back(ball.position);
    }
    if (!strike)
    {
      readStrike(engine, now);
      if (strike)
      {
        // Struck: he stays where he played it, and the ball runs alone.
        strikeFrom = ball.sentFrom;
        engine.setDrillTarget(passerId, SPOT, false);
        struck(ball);
      }
      else if (now >= STRIKE_TIMEOUT_SECONDS)
      {
        outcome = "no pass";
      }
    }
    else if (outcome.empty())
    {
      outcome = track(ball, now);
      if (outcome.empty() && engine.getState() != MatchState::PLAYING)
      {
        outcome = "out of play";
      }
      if (outcome.empty() && now >= strike->at + TIME_LIMIT)
      {
        outcome = "time up";
      }
    }
    if (!outcome.empty() && !endAt)
    {
      endAt = now;
    }
    return (!endAt || now < *endAt + LINGER_SECONDS);
  }

 protected:
  /** After the drill has started and the pass is asked for: the run's
   * readings cleared. */
  void beginRun(const MatchEngine& engine)
  {
    before = engine.getStrikeContacts();
    strike.reset();
    outcome.clear();
    endAt.reset();
    ballPath.clear();
    strikeFrom = SPOT;
    reset();
  }

  /** How the strike came off (StrikeContactStats, before and after). */
  struct Strike
  {
    double at = 0.0;
    StrikeKind kind = StrikeKind::GROUND_PASS;
    bool angleFailed = false;
    bool weightFailed = false;
    /** Rad (unsigned), m/s (+ harder), rad (+ higher). */
    float angleError = 0.0F;
    float weightError = 0.0F;
    float elevationError = 0.0F;
    float angleDifficulty = 0.0F;
    float weightDifficulty = 0.0F;
    /** The conditions' values (0-1). */
    std::array<float, CONTACT_FACTOR_COUNT> factors{};
    /** Off his foot: along the ground and upward (m/s). */
    float speed = 0.0F;
    float lift = 0.0F;
  };

  [[nodiscard]] PitchPoint strikeOrigin() const { return strikeFrom; }
  [[nodiscard]] float initialCondition() const { return condition; }
  [[nodiscard]] PlayerID passerPlayerId() const { return passerId; }
  [[nodiscard]] const std::optional<Strike>& lastStrike() const
  {
    return strike;
  }
  [[nodiscard]] const std::string& outcomeText() const { return outcome; }
  [[nodiscard]] const std::vector<PitchPoint>& ballTrail() const
  {
    return ballPath;
  }
  void preparePasser(MatchEngine& engine, PlayerID player)
  {
    passerId = player;
    engine.setStrikeKnobs(knobs);
  }

  /** The kind of strike whose knobs this drill offers, and how it is
   * played. */
  [[nodiscard]] virtual StrikeKind kind() const = 0;
  [[nodiscard]] virtual ForcedPassStyle style() const = 0;
  /** Distance from the spot to the target (m). */
  [[nodiscard]] virtual float range() const = 0;
  /** The spin he means (rad/s): sidespin (+ curls it to his right) and
   * topspin (- backspin); none, the body's default for the kind. */
  [[nodiscard]] virtual std::optional<BallSpin> spin() const
  {
    return std::nullopt;
  }
  /** Rev/s as asked in the settings, as the engine's spin (rad/s); none
   * when both are 0. */
  [[nodiscard]] static std::optional<BallSpin> spinOf(float sideRevs,
                                                      float topRevs)
  {
    if (sideRevs == 0.0F && topRevs == 0.0F)
    {
      return std::nullopt;
    }
    return BallSpin{.side = sideRevs * FULL_TURN_RADIANS,
                    .top = topRevs * FULL_TURN_RADIANS};
  }
  /** The spin settings, in a report line. */
  [[nodiscard]] static std::string spinText(SpinRevolutions spin)
  {
    const float sideRevs = spin.side;
    const float topRevs = spin.top;
    std::string text;
    if (sideRevs != 0.0F)
    {
      text += std::format(", sidespin {:+.1f} rev/s", sideRevs);
    }
    if (topRevs != 0.0F)
    {
      text += std::format(", {} {:.1f} rev/s",
                          topRevs > 0.0F ? "topspin" : "backspin",
                          std::abs(topRevs));
    }
    return text;
  }
  /** The pace (m/s) he means a ground pass to arrive at; none, as the AI
   * weights it. */
  [[nodiscard]] virtual std::optional<float> arrivalSpeed() const
  {
    return std::nullopt;
  }
  /** A new run: the drill's own state cleared. */
  virtual void reset() = 0;
  /** The pass is away (the ball as it is now). */
  virtual void struck(const MatchBall& ball) = 0;
  /** After every step once struck: the outcome, or empty while it runs. */
  virtual std::string track(const MatchBall& ball, double now) = 0;

  [[nodiscard]] PitchPoint target() const { return along(SPOT, UP, range()); }
  /** Where a pitch point is, in metres: along the line from the spot (+
   * towards the target) and across it (+ to his right). */
  static Vector2F lineMetres(PitchPoint point)
  {
    const Vector2F offset = point - SPOT;
    return {.x = (offset.x * UP.x) + (offset.y * UP.y),
            .y = (offset.y * UP.x) - (offset.x * UP.y)};
  }
  /** A point given in metres along and across the line. */
  static PitchPoint onLine(float alongMetres, float acrossMetres)
  {
    return along(along(SPOT, UP, alongMetres), {.x = -UP.y, .y = UP.x},
                 acrossMetres);
  }

  /** The passer's settings and the knobs of this drill's kind. */
  void addPasserAndKnobs(std::vector<DrillParameter>& list)
  {
    const auto add = [&list](DrillParameter parameter, const char* section)
    {
      parameter.section = section;
      list.push_back(std::move(parameter));
    };
    add(setting("Passing", &passing, {.min = 1.0F, .max = PASSING_MAX}, true),
        "Passer");
    add(setting("Condition", &condition,
                {.min = CONDITION_MIN, .max = CONDITION_MAX}, true,
                "How fresh he is at the start (tiredness widens the "
                "errors)."),
        "Passer");
    DrillParameter foot =
        setting("Foot", &footChoice, {.min = 0.0F, .max = 1.0F}, true);
    foot.choices = {"Right", "Left"};
    add(foot, "Passer");
    // Names live as long as the drill: the settings point at them.
    const auto kindName =
        STRIKE_KIND_NAMES.at(static_cast<std::size_t>(kind()));
    for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
    {
      knobNames.at(index) =
          strikeKnobName(kind(), static_cast<StrikeKnob>(index));
      knobSections.at(index) = std::string(kindName) + " knobs: " +
                               std::string(STRIKE_KNOBS.at(index).group);
    }
    for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
    {
      const StrikeKnobInfo& info = STRIKE_KNOBS.at(index);
      DrillParameter knob =
          setting(knobNames.at(index).c_str(),
                  &knobs.at(kind(), static_cast<StrikeKnob>(index)),
                  {.min = info.min, .max = info.max}, false);
      knob.collapsible = true;
      knob.label = info.name.data();
      add(knob, knobSections.at(index).c_str());
    }
  }

  /** The passer and his strike, for the report. */
  void reportStrike(std::vector<std::string>& lines) const
  {
    lines.push_back(std::format(
        "Passer: Passing {:.0f}, condition {:.0f}, {} foot", passing, condition,
        footChoice < CHOICE_MIDPOINT ? "right" : "left"));
    if (!strike)
    {
      return;
    }
    constexpr float DEGREES = HALF_TURN_DEGREES / PI;
    lines.push_back(std::format(
        "{} at {:.2f} s, {:.1f} m/s{}",
        STRIKE_KIND_NAMES.at(static_cast<std::size_t>(strike->kind)),
        strike->at, strike->speed,
        strike->lift > 0.0F ? std::format(" ({:.1f} up)", strike->lift)
                            : std::string()));
    // Each job: whether it went wrong, and by how much it missed.
    lines.push_back(std::format(
        "{}  angle: {:.1f} deg off (elevation {:+.1f} deg)",
        strike->angleFailed ? "FAIL" : "PASS", strike->angleError * DEGREES,
        strike->elevationError * DEGREES));
    lines.push_back(std::format("{}  weight: {:+.2f} m/s",
                                strike->weightFailed ? "FAIL" : "PASS",
                                strike->weightError));
  }

  /** The drill's outcome, PASS or FAIL first. */
  void reportOutcome(std::vector<std::string>& lines, bool passed) const
  {
    if (!outcome.empty())
    {
      lines.push_back(
          std::format("OUTCOME  {}  {}", passed ? "PASS" : "FAIL", outcome));
    }
  }

  /** How hard the last pass was: each job's difficulty and the conditions
   * (0-1) that made it so, below the settings. */
  void renderAfterSettings() const override
  {
    ImGui::SeparatorText("Difficulty of the last pass");
    if (!strike)
    {
      SandboxText::disabledText("No pass yet.");
      return;
    }
    if (!ImGui::BeginTable("##difficulty", 2,
                           ImGuiTableFlags_SizingStretchProp))
    {
      return;
    }
    const auto row = [](const char* name, float value, bool strong)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (strong)
      {
        ImGui::TextUnformatted(name);
      }
      else
      {
        SandboxText::disabledText(name);
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(
          std::format("{:.3f}", static_cast<double>(value)).c_str());
    };
    row("Angle difficulty", strike->angleDifficulty, true);
    row("Weight difficulty", strike->weightDifficulty, true);
    // The conditions that were there at all.
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
    {
      if (strike->factors.at(index) > MIN_REPORTED_CONTACT_FACTOR)
      {
        row(std::string(CONTACT_FACTOR_LABELS.at(index)).c_str(),
            strike->factors.at(index), false);
      }
    }
    ImGui::EndTable();
  }

  /** The strike, as numbers (Measure mode). */
  void strikeMetrics(std::vector<DrillMetric>& out) const
  {
    if (!strike)
    {
      return;
    }
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    constexpr float DEGREES = HALF_TURN_DEGREES / PI;
    out.push_back({.name = "Launch speed (m/s)", .value = strike->speed});
    out.push_back({.name = "Launch lift (m/s)", .value = strike->lift});
    out.push_back(
        {.name = "Angle difficulty", .value = strike->angleDifficulty});
    out.push_back(
        {.name = "Weight difficulty", .value = strike->weightDifficulty});
    out.push_back({.name = "Angle failed", .value = flag(strike->angleFailed)});
    out.push_back(
        {.name = "Weight failed", .value = flag(strike->weightFailed)});
    out.push_back({.name = "Abs angle error (deg)",
                   .value = strike->angleError * DEGREES});
    out.push_back({.name = "Weight error (m/s)", .value = strike->weightError});
    out.push_back({.name = "Elevation error (deg, + higher)",
                   .value = strike->elevationError * DEGREES});
  }

 private:
  /** Where the ball was struck from. */
  PitchPoint strikeFrom;
  // Passer
  float passing = DEFAULT_PASSING;
  float condition = DEFAULT_CONDITION;
  float footChoice = 0.0F;
  // The strike's tuning, as this drill plays it.
  StrikeKnobs knobs;

  PlayerID passerId = 0;
  std::optional<Strike> strike;
  std::string outcome;
  std::optional<double> endAt;
  std::vector<PitchPoint> ballPath;

  /** The pass: a strike the contact model has read since the start. */
  void readStrike(const MatchEngine& engine, double now)
  {
    const StrikeContactStats& after = engine.getStrikeContacts();
    for (std::size_t index = 0; index < STRIKE_KIND_COUNT; ++index)
    {
      const auto& a = before.byKind.at(index);
      const auto& b = after.byKind.at(index);
      if (b.strikes == a.strikes)
      {
        continue;
      }
      const MatchBall& ball = engine.getBall();
      Strike reading;
      reading.at = now;
      reading.kind = static_cast<StrikeKind>(index);
      reading.angleFailed = b.angleFailed > a.angleFailed;
      reading.weightFailed = b.weightFailed > a.weightFailed;
      reading.angleError =
          static_cast<float>(b.angleErrorSum - a.angleErrorSum);
      reading.weightError =
          static_cast<float>(b.weightErrorSum - a.weightErrorSum);
      reading.elevationError =
          static_cast<float>(b.elevationErrorSum - a.elevationErrorSum);
      reading.angleDifficulty =
          static_cast<float>(b.angleDifficulty - a.angleDifficulty);
      reading.weightDifficulty =
          static_cast<float>(b.weightDifficulty - a.weightDifficulty);
      for (std::size_t factor = 0; factor < CONTACT_FACTOR_COUNT; ++factor)
      {
        reading.factors.at(factor) =
            static_cast<float>(b.factorSum.at(factor) - a.factorSum.at(factor));
      }
      reading.speed = std::hypot(ball.velocity.x, ball.velocity.y);
      reading.lift = ball.velocityZ;
      strike = reading;
      return;
    }
  }

  StrikeContactStats before;
  std::array<std::string, STRIKE_KNOB_COUNT> knobNames;
  std::array<std::string, STRIKE_KNOB_COUNT> knobSections;
};

/**
 * Ground passes down a corridor: from the penalty spot along the ground to a
 * target on the line towards the centre circle, between two rows of cones.
 * The ball has to reach the target's line without leaving the corridor.
 */
class CorridorPassDrill : public PassTargetDrill
{
  static constexpr float RANGE_METRES_MIN = 3.0F;
  static constexpr float RANGE_METRES_MAX = 60.0F;
  static constexpr float WIDTH_MIN = 0.5F;
  static constexpr float WIDTH_MAX = 10.0F;
  static constexpr float CONE_SPACING_MAX = 10.0F;
  static constexpr float SIDE_REVS_MIN = -10.0F;
  static constexpr float SIDE_REVS_MAX = 10.0F;
  static constexpr float ARRIVAL_CHOICE_MAX = 20.0F;
  static constexpr float DEFAULT_RANGE_METRES = 20.0F;
  static constexpr float DEFAULT_WIDTH = 3.0F;
  static constexpr float DEFAULT_CONE_SPACING = 4.0F;
  static constexpr ImU32 CORRIDOR_EDGE_COLOUR = IM_COL32(255, 140, 30, 90);
  static constexpr ImU32 CORRIDOR_TARGET_COLOUR = IM_COL32(90, 220, 120, 160);
  static constexpr ImU32 CONE_COLOUR = IM_COL32(255, 140, 30, 255);
  static constexpr ImU32 TARGET_COLOUR = IM_COL32(90, 220, 120, 220);
  static constexpr ImU32 CORRIDOR_MISS_COLOUR = IM_COL32(240, 80, 80, 220);
  static constexpr ImU32 BALL_TRAIL_COLOUR = IM_COL32(255, 230, 120, 160);
  static constexpr float PERCENT_SCALE = 100.0F;

 public:
  [[nodiscard]] const char* name() const override
  {
    return "Ground pass corridor";
  }
  [[nodiscard]] const char* summary() const override
  {
    return "A ground pass from the penalty spot to a target on the line "
           "towards the centre circle (nobody on it: weighted as for a man "
           "standing there), down a corridor marked by cones. The ball must "
           "reach the target's line without leaving the corridor. Measures "
           "how far off the line it runs, where it leaves the corridor or "
           "stops short, and how the strike came off, with his Passing and "
           "every ground-pass strike knob as settings.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    const auto add = [&list](DrillParameter parameter)
    {
      parameter.section = "Corridor";
      list.push_back(std::move(parameter));
    };
    add(setting("Range (m)", &rangeMetres,
                {.min = RANGE_METRES_MIN, .max = RANGE_METRES_MAX}, false,
                "From the penalty spot to the target, towards the centre "
                "circle."));
    add(setting("Corridor width (m)", &width,
                {.min = WIDTH_MIN, .max = WIDTH_MAX}, false,
                "Between the two rows of cones (the ball's centre must stay "
                "inside)."));
    add(setting("Cone spacing (m)", &coneSpacing,
                {.min = 1.0F, .max = CONE_SPACING_MAX}, false,
                "Along each row, from the spot to the target's line."));
    add(setting("Sidespin (rev/s)", &sideRevs,
                {.min = SIDE_REVS_MIN, .max = SIDE_REVS_MAX}, false,
                "The sidespin he means to put on it: + curls it to his "
                "right, - to his left (it fades as it rolls). Held to what "
                "his Passing can put on. 0: a straight pass, curved only by "
                "accident (less often the better he passes)."));
    if (kind() == StrikeKind::GROUND_PASS)
    {
      add(setting("Arrival speed (m/s)", &arrivalChoice,
                  {.min = 0.0F, .max = ARRIVAL_CHOICE_MAX}, false,
                  "The pace he means it to reach the target at (the brain's "
                  "call). 0: as the AI weights it (by his Passing and the "
                  "distance)."));
    }
    addPasserAndKnobs(list);
    return list;
  }

  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    constexpr float PRESET_SHORT_PASS_RANGE_M = 10.0F;
    constexpr float PRESET_SHORT_PASS_CORRIDOR_WIDTH_M = 2.0F;
    constexpr float PRESET_SHORT_PASS_CONE_SPACING_M = 2.0F;
    constexpr float PRESET_MEDIUM_PASS_RANGE_M = 20.0F;
    constexpr float PRESET_MEDIUM_PASS_CORRIDOR_WIDTH_M = 3.0F;
    constexpr float PRESET_MEDIUM_PASS_CONE_SPACING_M = 4.0F;
    constexpr float PRESET_LONG_GROUND_PASS_RANGE_M = 35.0F;
    constexpr float PRESET_LONG_GROUND_PASS_CORRIDOR_WIDTH_M = 4.0F;
    constexpr float PRESET_LONG_GROUND_PASS_CONE_SPACING_M = 5.0F;

    return {
        {.name = "Short pass",
         .summary = "10 m down a 2 m corridor.",
         .settings = {{"Range (m)", PRESET_SHORT_PASS_RANGE_M},
                      {"Corridor width (m)",
                       PRESET_SHORT_PASS_CORRIDOR_WIDTH_M},
                      {"Cone spacing (m)", PRESET_SHORT_PASS_CONE_SPACING_M}}},
        {.name = "Medium pass",
         .summary = "20 m down a 3 m corridor.",
         .settings = {{"Range (m)", PRESET_MEDIUM_PASS_RANGE_M},
                      {"Corridor width (m)",
                       PRESET_MEDIUM_PASS_CORRIDOR_WIDTH_M},
                      {"Cone spacing (m)", PRESET_MEDIUM_PASS_CONE_SPACING_M}}},
        {.name = "Long ground pass",
         .summary = "35 m down a 4 m corridor.",
         .settings = {{"Range (m)", PRESET_LONG_GROUND_PASS_RANGE_M},
                      {"Corridor width (m)",
                       PRESET_LONG_GROUND_PASS_CORRIDOR_WIDTH_M},
                      {"Cone spacing (m)",
                       PRESET_LONG_GROUND_PASS_CONE_SPACING_M}}},
    };
  }

  void layKit(MatchEngine& engine) const override
  {
    // The corridor's edges and end line, and its cones every spacing from
    // the spot and at the target's line (flat markers).
    PitchKit::Layout kit;
    const float half = width * 0.5F;
    const ImU32 edge = CORRIDOR_EDGE_COLOUR;
    kit.lines.push_back({.name = "Left edge",
                         .from = onLine(0.0F, -half),
                         .to = onLine(rangeMetres, -half),
                         .colour = edge});
    kit.lines.push_back({.name = "Right edge",
                         .from = onLine(0.0F, half),
                         .to = onLine(rangeMetres, half),
                         .colour = edge});
    kit.lines.push_back({.name = "End",
                         .from = onLine(rangeMetres, -half),
                         .to = onLine(rangeMetres, half),
                         .colour = CORRIDOR_TARGET_COLOUR});
    const int count = static_cast<int>(rangeMetres / coneSpacing);
    for (int index = 0; index <= count + 1; ++index)
    {
      const float at =
          std::min(static_cast<float>(index) * coneSpacing, rangeMetres);
      for (const float side : {-half, half})
      {
        kit.cones.push_back({.name = std::format("Cone {:.0f} m", at),
                             .at = onLine(at, side),
                             .kind = PitchKit::ConeKind::FLAT,
                             .colour = CONE_COLOUR});
      }
    }
    engine.setKit(std::move(kit));
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    // The corridor and its cones are kit on the pitch (layKit).
    const float scale = Theme::scale();
    DrillDraw::marker(renderer, draw, target(),
                      PRIMARY_MARKER_RADIUS_PIXELS * scale, TARGET_COLOUR,
                      outcomeText().empty() ? "target" : outcomeText().c_str());
    if (leftAt)
    {
      DrillDraw::marker(renderer, draw, *leftAt,
                        SECONDARY_MARKER_RADIUS_PIXELS * scale,
                        CORRIDOR_MISS_COLOUR);
    }
    DrillDraw::path(renderer, draw, ballTrail(), BALL_TRAIL_COLOUR,
                    BALL_TRAIL_WIDTH_PIXELS * scale);
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.0f} m {}, corridor {:.1f} m wide{}{}", rangeMetres,
        kind() == StrikeKind::GROUND_PASS ? "along the ground" : "driven",
        width, spinText({.side = sideRevs, .top = 0.0F}),
        arrivalChoice > 0.0F
            ? std::format(", to arrive at {:.1f} m/s", arrivalChoice)
            : std::string()));
    reportStrike(lines);
    if (lastStrike())
    {
      lines.push_back(std::format(
          "Widest off the line before the target {:.2f} m", widest));
      if (atTarget)
      {
        lines.push_back(std::format(
            "At the target's line {:+.2f} m off it (+ right), {:.1f} m/s",
            atTarget->x, atTarget->y));
      }
      if (leftAfter)
      {
        lines.push_back(
            std::format("Left the corridor after {:.1f} m", *leftAfter));
      }
      if (shortBy)
      {
        lines.push_back(
            std::format("Stopped {:.1f} m short of the target", *shortBy));
      }
      lines.push_back(std::format("Closest to the target point {:.0f} cm",
                                  closest * PERCENT_SCALE));
    }
    reportOutcome(lines, outcomeText() == IN_CORRIDOR);
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (outcomeText().empty())
    {
      return {};
    }
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{
        {.name = "Passed", .value = flag(lastStrike().has_value())}};
    if (!lastStrike())
    {
      return out;
    }
    out.push_back({.name = "In the corridor",
                   .value = flag(outcomeText() == IN_CORRIDOR)});
    out.push_back(
        {.name = "Left the corridor", .value = flag(leftAfter.has_value())});
    out.push_back(
        {.name = "Stopped short", .value = flag(shortBy.has_value())});
    out.push_back({.name = "Widest off the line (m)", .value = widest});
    out.push_back({.name = "Miss from the target (cm)",
                   .value = closest * PERCENT_SCALE});
    if (leftAfter)
    {
      out.push_back(
          {.name = "Left the corridor after (m)", .value = *leftAfter});
    }
    if (shortBy)
    {
      out.push_back({.name = "Short of the target (m)", .value = *shortBy});
    }
    if (atTarget)
    {
      out.push_back({.name = "Off the line at the target (m, + right)",
                     .value = atTarget->x});
      out.push_back({.name = "Abs off the line at the target (m)",
                     .value = std::abs(atTarget->x)});
      out.push_back(
          {.name = "Speed at the target (m/s)", .value = atTarget->y});
    }
    strikeMetrics(out);
    return out;
  }

 protected:
  [[nodiscard]] StrikeKind kind() const override
  {
    return StrikeKind::GROUND_PASS;
  }
  [[nodiscard]] ForcedPassStyle style() const override
  {
    return ForcedPassStyle::GROUND;
  }
  [[nodiscard]] float range() const override { return rangeMetres; }
  [[nodiscard]] std::optional<BallSpin> spin() const override
  {
    return spinOf(sideRevs, 0.0F);
  }
  [[nodiscard]] std::optional<float> arrivalSpeed() const override
  {
    return arrivalChoice > 0.0F && kind() == StrikeKind::GROUND_PASS
               ? std::optional<float>(arrivalChoice)
               : std::nullopt;
  }

  void reset() override
  {
    widest = 0.0F;
    last = Vector2F{.x = 0.0F, .y = 0.0F};
    atTarget.reset();
    leftAfter.reset();
    leftAt.reset();
    shortBy.reset();
    closest = std::numeric_limits<float>::max();
  }

  void struck(const MatchBall& ball) override
  {
    last = lineMetres(ball.position);
  }

  std::string track(const MatchBall& ball, double /*now*/) override
  {
    // Followed on past the corridor's edge, to the target's line (or until
    // it stops), so a pass out of the corridor has its miss too.
    const Vector2F now = lineMetres(ball.position);
    const Vector2F was = last;
    last = now;
    const float half = width * 0.5F;
    // Closest to the target point over the step's move.
    {
      const Vector2F move{.x = now.x - was.x, .y = now.y - was.y};
      const float moved = (move.x * move.x) + (move.y * move.y);
      const float share =
          moved > 1e-9F
              ? std::clamp(
                    ((rangeMetres - was.x) * move.x - was.y * move.y) / moved,
                    0.0F, 1.0F)
              : 0.0F;
      closest =
          std::min(closest, std::hypot(was.x + (move.x * share) - rangeMetres,
                                       was.y + (move.y * share)));
    }
    const auto leave = [&](float at, PitchPoint where)
    {
      if (leftAfter)
      {
        return;
      }
      leftAfter = at;
      leftAt = where;
    };
    // Over the target's line: where it crossed, and how fast.
    if (now.x >= rangeMetres)
    {
      const float share =
          now.x > was.x ? (rangeMetres - was.x) / (now.x - was.x) : 1.0F;
      const float across =
          was.y + ((now.y - was.y) * std::clamp(share, 0.0F, 1.0F));
      if (!leftAfter)
      {
        widest = std::max(widest, std::abs(across));
      }
      if (std::abs(across) > half)
      {
        leave(rangeMetres, onLine(rangeMetres, across));
      }
      atTarget = Vector2F{.x = across,
                          .y = std::hypot(ball.velocity.x, ball.velocity.y)};
      return leftAfter ? OUT_OF_CORRIDOR : IN_CORRIDOR;
    }
    if (!leftAfter)
    {
      widest = std::max(widest, std::abs(now.y));
    }
    // Back out of the corridor's mouth.
    if (now.x < 0.0F)
    {
      leave(0.0F, ball.position);
    }
    if (std::abs(now.y) > half)
    {
      // Where it went over the edge (the step's move, crossed at the edge).
      const float edge = now.y > 0.0F ? half : -half;
      const float share =
          std::abs(now.y - was.y) > 1e-6F
              ? std::clamp((edge - was.y) / (now.y - was.y), 0.0F, 1.0F)
              : 1.0F;
      const float at = was.x + ((now.x - was.x) * share);
      leave(at, onLine(at, edge));
    }
    if (std::hypot(ball.velocity.x, ball.velocity.y) <=
        MatchTuning::Ball::STOP_SPEED)
    {
      shortBy = rangeMetres - now.x;
      return leftAfter ? OUT_OF_CORRIDOR : "stopped short";
    }
    return {};
  }

 private:
  static constexpr const char* IN_CORRIDOR =
      "reached the target in the corridor";
  static constexpr const char* OUT_OF_CORRIDOR = "out of the corridor";

  float rangeMetres = DEFAULT_RANGE_METRES;
  float width = DEFAULT_WIDTH;
  float coneSpacing = DEFAULT_CONE_SPACING;
  float sideRevs = 0.0F;
  float arrivalChoice = 0.0F;

  /** The ball's last place along and across the line (m). */
  Vector2F last{};
  float widest = 0.0F;
  /** At the target's line: across it (m, + right) and the ball's speed. */
  std::optional<Vector2F> atTarget;
  std::optional<float> leftAfter;
  std::optional<PitchPoint> leftAt;
  std::optional<float> shortBy;
  /** The ball path's closest to the target point (m). */
  float closest = std::numeric_limits<float>::max();
};

/**
 * Lofted passes at a bullseye: from the penalty spot, in the air, to a
 * target of concentric rings on the ground on the line towards the centre
 * circle. Scores where the ball first comes down.
 */
class BullseyePassDrill : public PassTargetDrill
{
  static constexpr float RANGE_METRES_MIN = 5.0F;
  static constexpr float RANGE_METRES_MAX = 60.0F;
  static constexpr float RING_WIDTH_MIN = 0.5F;
  static constexpr float RING_WIDTH_MAX = 5.0F;
  static constexpr float RING_COUNT_MAX = 10.0F;
  static constexpr float SIDE_REVS_MIN = -10.0F;
  static constexpr float SIDE_REVS_MAX = 10.0F;
  static constexpr float TOP_REVS_MIN = -10.0F;
  static constexpr float TOP_REVS_MAX = 10.0F;
  static constexpr float DEFAULT_RANGE_METRES = 35.0F;
  static constexpr float DEFAULT_RING_COUNT = 5.0F;
  static constexpr ImU32 OUTER_RING_COLOUR = IM_COL32(220, 60, 60, 110);
  static constexpr ImU32 INNER_RING_COLOUR = IM_COL32(245, 245, 245, 90);
  static constexpr ImU32 RING_OUTLINE_COLOUR = IM_COL32(20, 20, 20, 160);
  static constexpr ImU32 TARGET_LINE_COLOUR = IM_COL32(255, 255, 255, 50);
  static constexpr ImU32 FEED_MARKER_COLOUR = IM_COL32(255, 255, 255, 200);
  static constexpr ImU32 LANDING_COLOUR = IM_COL32(255, 230, 120, 255);
  static constexpr ImU32 BALL_TRAIL_COLOUR = IM_COL32(255, 230, 120, 160);

 public:
  [[nodiscard]] const char* name() const override
  {
    return "Lofted pass target";
  }
  [[nodiscard]] const char* summary() const override
  {
    return "A lofted pass from the penalty spot to a bullseye on the ground "
           "on the line towards the centre circle (nobody on it: the ball is "
           "aimed to land on it). Measures where it first comes down: how "
           "far from the centre, long or short, left or right, and the ring "
           "it lands in, and how the strike came off, with his Passing and "
           "every lofted-pass strike knob as settings.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    const auto add = [&list](DrillParameter parameter)
    {
      parameter.section = "Target";
      list.push_back(std::move(parameter));
    };
    add(setting("Range (m)", &rangeMetres,
                {.min = RANGE_METRES_MIN, .max = RANGE_METRES_MAX}, false,
                "From the penalty spot to the bullseye, towards the centre "
                "circle."));
    add(setting("Ring width (m)", &ringWidth,
                {.min = RING_WIDTH_MIN, .max = RING_WIDTH_MAX}, false,
                "Each ring's width: the bullseye's radius, and the step out to "
                "each ring beyond it."));
    add(setting("Rings", &ringCount, {.min = 1.0F, .max = RING_COUNT_MAX}, true,
                "How many rings, the bullseye included. A landing in the "
                "bullseye scores this many, the outer ring 1, outside 0."));
    add(setting("Sidespin (rev/s)", &sideRevs,
                {.min = SIDE_REVS_MIN, .max = SIDE_REVS_MAX}, false,
                "The sidespin he means to put on it: + curls it to his "
                "right, - to his left (it fades in flight). 0 with no "
                "topspin: as the AI strikes it, curled only by accident "
                "(less often the better he passes)."));
    add(setting("Topspin (rev/s)", &topRevs,
                {.min = TOP_REVS_MIN, .max = TOP_REVS_MAX}, false,
                "+ topspin (it dips, and runs on at the bounce), - backspin "
                "(it holds up, and checks at the bounce). Held, with the "
                "sidespin, to what his Passing can put on."));
    addPasserAndKnobs(list);
    return list;
  }

  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    constexpr float PRESET_SHORT_LOFTED_PASS_RANGE_M = 20.0F;
    constexpr float PRESET_LONG_BALL_RANGE_M = 35.0F;
    constexpr float PRESET_VERY_LONG_BALL_RANGE_M = 50.0F;

    return {
        {.name = "Short lofted pass",
         .summary = "20 m.",
         .settings = {{"Range (m)", PRESET_SHORT_LOFTED_PASS_RANGE_M}}},
        {.name = "Long ball",
         .summary = "35 m.",
         .settings = {{"Range (m)", PRESET_LONG_BALL_RANGE_M}}},
        {.name = "Very long ball",
         .summary = "50 m.",
         .settings = {{"Range (m)", PRESET_VERY_LONG_BALL_RANGE_M}}},
    };
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    const int rings = static_cast<int>(ringCount);
    // Outermost first, alternating, so each inner ring covers the middle.
    for (int ring = rings; ring >= 1; --ring)
    {
      const bool red = (rings - ring) % 2 == 0;
      DrillDraw::circle(renderer, draw, target(),
                        ringWidth * static_cast<float>(ring),
                        red ? OUTER_RING_COLOUR : INNER_RING_COLOUR,
                        RING_OUTLINE_COLOUR, 1.0F * scale);
    }
    DrillDraw::line(renderer, draw, SPOT, target(), TARGET_LINE_COLOUR,
                    1.0F * scale);
    DrillDraw::marker(renderer, draw, SPOT,
                      SECONDARY_MARKER_RADIUS_PIXELS * scale,
                      FEED_MARKER_COLOUR, "pass");
    if (landing)
    {
      DrillDraw::marker(renderer, draw, landing->point,
                        PRIMARY_MARKER_RADIUS_PIXELS * scale, LANDING_COLOUR,
                        std::format("{:.1f} m", landing->miss).c_str());
    }
    DrillDraw::path(renderer, draw, ballTrail(), BALL_TRAIL_COLOUR,
                    BALL_TRAIL_WIDTH_PIXELS * scale);
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(
        std::format("{:.0f} m {}, {} rings {:.1f} m wide{}", rangeMetres,
                    kind() == StrikeKind::CHIP ? "chipped" : "lofted",
                    static_cast<int>(ringCount), ringWidth,
                    spinText({.side = sideRevs, .top = topRevs})));
    reportStrike(lines);
    if (landing)
    {
      lines.push_back(std::format(
          "Landed {:.2f} m from the centre ({:+.2f} m long, {:+.2f} m right) "
          "after {:.2f} s, at {:.1f} m/s: {}",
          landing->miss, landing->longer, landing->right, landing->flight,
          landing->speed,
          landing->score > 0 ? std::format("{} points", landing->score)
                             : std::string("outside the rings")));
    }
    if (lastStrike())
    {
      lines.push_back(
          std::format("Peak {:.1f} m; {:.0f} m out {}", peak, CLEAR_METRES,
                      clearHeight ? std::format("{:.1f} m up", *clearHeight)
                                  : std::string("already down")));
    }
    reportOutcome(lines, landing && landing->score > 0);
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (outcomeText().empty())
    {
      return {};
    }
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{
        {.name = "Passed", .value = flag(lastStrike().has_value())}};
    if (!lastStrike())
    {
      return out;
    }
    out.push_back({.name = "Landed", .value = flag(landing.has_value())});
    if (landing)
    {
      out.push_back({.name = "Miss (m)", .value = landing->miss});
      out.push_back({.name = "Long (m, - short)", .value = landing->longer});
      out.push_back({.name = "Right (m, - left)", .value = landing->right});
      out.push_back(
          {.name = "Score", .value = static_cast<double>(landing->score)});
      out.push_back(
          {.name = "On the target", .value = flag(landing->score > 0)});
      out.push_back({.name = "Flight (s)", .value = landing->flight});
      out.push_back(
          {.name = "Speed at landing (m/s)", .value = landing->speed});
    }
    out.push_back({.name = "Peak height (m)", .value = peak});
    out.push_back({.name = "Height 4 m out (m, 0 already down)",
                   .value = clearHeight.value_or(0.0F)});
    strikeMetrics(out);
    return out;
  }

 protected:
  [[nodiscard]] StrikeKind kind() const override
  {
    return StrikeKind::LOFTED_PASS;
  }
  [[nodiscard]] ForcedPassStyle style() const override
  {
    return ForcedPassStyle::LOFTED;
  }
  [[nodiscard]] float range() const override { return rangeMetres; }
  [[nodiscard]] std::optional<BallSpin> spin() const override
  {
    return spinOf(sideRevs, topRevs);
  }

  void reset() override
  {
    landing.reset();
    previous = {};
    peak = 0.0F;
    clearHeight.reset();
  }

  void struck(const MatchBall& ball) override
  {
    previous = stateOf(ball);
    peak = previous.height;
  }

  std::string track(const MatchBall& ball, double now) override
  {
    const auto& contact = lastStrike();
    if (!contact)
    {
      return {};
    }
    const double strikeTime = contact->at;
    const BallState was = previous;
    previous = stateOf(ball);
    peak = std::max(peak, previous.height);
    // Its height as it passes the chip's clearance distance from where it
    // was struck (still up).
    if (!clearHeight && !landing)
    {
      const float from = lineMetres(strikeOrigin()).x;
      const float before = lineMetres(was.position).x - from;
      const float after = lineMetres(previous.position).x - from;
      if (before < CLEAR_METRES && after >= CLEAR_METRES)
      {
        const float share =
            (CLEAR_METRES - before) / std::max(after - before, 1e-6F);
        clearHeight = was.height + ((previous.height - was.height) * share);
      }
    }
    // Down: bounced since the last step (falling then, rising now), or on
    // the ground. Where it came down is worked out from the last step,
    // falling under gravity (drag is small over a step).
    const bool bounced = was.lift < 0.0F && previous.lift > 0.0F;
    const bool grounded = previous.height <= LANDING_HEIGHT_EPSILON_METRES &&
                          previous.lift <= 0.0F;
    if (!bounced && !grounded)
    {
      return {};
    }
    constexpr float G = MatchTuning::Ball::GRAVITY;
    const float fall =
        (was.lift + std::sqrt((was.lift * was.lift) +
                              (2.0F * G * std::max(was.height, 0.0F)))) /
        G;
    const PitchPoint point{was.position.x + (was.velocity.x * fall),
                           was.position.y + (was.velocity.y * fall)};
    Landing down;
    down.point = point;
    const Vector2F offset = lineMetres(point);
    down.longer = offset.x - rangeMetres;
    down.right = offset.y;
    down.miss = std::hypot(down.longer, down.right);
    const int ring = static_cast<int>(std::floor(down.miss / ringWidth));
    const int rings = static_cast<int>(ringCount);
    down.score = ring < rings ? rings - ring : 0;
    down.flight = static_cast<float>(now - STEP - strikeTime) + fall;
    down.speed = std::hypot(was.velocity.x, was.velocity.y);
    landing = down;
    return down.score > 0
               ? std::format("landed on the target, {} points", down.score)
               : std::string("landed outside the rings");
  }

 private:
  struct BallState
  {
    PitchPoint position;
    Vector2F velocity{};
    /** Metres up, and upward speed (m/s). */
    float height = 0.0F;
    float lift = 0.0F;
  };
  static BallState stateOf(const MatchBall& ball)
  {
    return {.position = ball.position,
            .velocity = ball.velocity,
            .height = ball.z,
            .lift = ball.velocityZ};
  }
  struct Landing
  {
    PitchPoint point;
    /** From the centre (m); long (+) or short, right (+) or left (m). */
    float miss = 0.0F;
    float longer = 0.0F;
    float right = 0.0F;
    int score = 0;
    /** From the strike to coming down (s, roughly: the strike is read at
     * the end of its step). */
    float flight = 0.0F;
    /** Its pace over the ground as it came down (m/s). */
    float speed = 0.0F;
  };
  /** Where its height is read on the way: the chip's clearance distance. */
  static constexpr float CLEAR_METRES =
      MatchTuning::Passing::CHIP_CLEAR_DISTANCE_METRES;
  float peak = 0.0F;
  std::optional<float> clearHeight;

 protected:
  void setInitialRange(float metres) { rangeMetres = metres; }
  [[nodiscard]] std::optional<float> landingCarryMetres() const
  {
    if (!landing)
    {
      return std::nullopt;
    }
    return lineMetres(landing->point).x - lineMetres(strikeOrigin()).x;
  }

 private:
  float rangeMetres = DEFAULT_RANGE_METRES;
  float ringWidth = 1.0F;
  float ringCount = DEFAULT_RING_COUNT;
  float sideRevs = 0.0F;
  float topRevs = 0.0F;

  BallState previous;
  std::optional<Landing> landing;
};

/** Driven passes down the corridor: low and hard with the instep. */
class DrivenCorridorDrill final : public CorridorPassDrill
{
 public:
  [[nodiscard]] const char* name() const override
  {
    return "Driven pass corridor";
  }
  [[nodiscard]] const char* summary() const override
  {
    return "A driven pass (low and hard, struck with the instep, coming "
           "down at the target) from the penalty spot down a corridor of "
           "cones to a target on the line towards the centre circle. "
           "Measures how far off the line it goes (seen from above), where "
           "it leaves the corridor, its pace at the target, and how the "
           "strike came off, with his Passing and every driven-pass strike "
           "knob as settings.";
  }
  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    constexpr float PRESET_SHORT_DRIVEN_PASS_RANGE_M = 15.0F;
    constexpr float PRESET_SHORT_DRIVEN_PASS_CORRIDOR_WIDTH_M = 2.0F;
    constexpr float PRESET_SHORT_DRIVEN_PASS_CONE_SPACING_M = 3.0F;
    constexpr float PRESET_DRIVEN_PASS_RANGE_M = 25.0F;
    constexpr float PRESET_DRIVEN_PASS_CORRIDOR_WIDTH_M = 3.0F;
    constexpr float PRESET_DRIVEN_PASS_CONE_SPACING_M = 5.0F;
    constexpr float PRESET_LONG_DRIVEN_BALL_RANGE_M = 40.0F;
    constexpr float PRESET_LONG_DRIVEN_BALL_CORRIDOR_WIDTH_M = 5.0F;
    constexpr float PRESET_LONG_DRIVEN_BALL_CONE_SPACING_M = 5.0F;

    return {
        {.name = "Short driven pass",
         .summary = "15 m down a 2 m corridor.",
         .settings = {{"Range (m)", PRESET_SHORT_DRIVEN_PASS_RANGE_M},
                      {"Corridor width (m)",
                       PRESET_SHORT_DRIVEN_PASS_CORRIDOR_WIDTH_M},
                      {"Cone spacing (m)",
                       PRESET_SHORT_DRIVEN_PASS_CONE_SPACING_M}}},
        {.name = "Driven pass",
         .summary = "25 m down a 3 m corridor.",
         .settings = {{"Range (m)", PRESET_DRIVEN_PASS_RANGE_M},
                      {"Corridor width (m)",
                       PRESET_DRIVEN_PASS_CORRIDOR_WIDTH_M},
                      {"Cone spacing (m)", PRESET_DRIVEN_PASS_CONE_SPACING_M}}},
        {.name = "Long driven ball",
         .summary = "40 m down a 5 m corridor.",
         .settings = {{"Range (m)", PRESET_LONG_DRIVEN_BALL_RANGE_M},
                      {"Corridor width (m)",
                       PRESET_LONG_DRIVEN_BALL_CORRIDOR_WIDTH_M},
                      {"Cone spacing (m)",
                       PRESET_LONG_DRIVEN_BALL_CONE_SPACING_M}}},
    };
  }

 protected:
  [[nodiscard]] StrikeKind kind() const override
  {
    return StrikeKind::DRIVEN_PASS;
  }
  [[nodiscard]] ForcedPassStyle style() const override
  {
    return ForcedPassStyle::DRIVEN;
  }
};

/** Chips at a bullseye: steep, with backspin, over someone close. */
class ChipTargetDrill final : public BullseyePassDrill
{
 public:
  ChipTargetDrill() { setInitialRange(INITIAL_CHIP_RANGE_METRES); }
  [[nodiscard]] const char* name() const override { return "Chip target"; }
  [[nodiscard]] const char* summary() const override
  {
    return "A chip (stabbed up steeply with backspin, clearing 2.5 m 4 m "
           "out) from the penalty spot to a bullseye on the ground on the "
           "line towards the centre circle. Measures where it first comes "
           "down, how high it was 4 m out and at its peak, its pace as it "
           "lands, and how the strike came off, with his Passing and every "
           "chip strike knob as settings.";
  }
  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    constexpr float PRESET_SHORT_CHIP_RANGE_M = 8.0F;
    constexpr float PRESET_CHIP_RANGE_M = 15.0F;
    constexpr float PRESET_LONG_CHIP_RANGE_M = 25.0F;

    return {
        {.name = "Short chip",
         .summary = "8 m.",
         .settings = {{"Range (m)", PRESET_SHORT_CHIP_RANGE_M}}},
        {.name = "Chip",
         .summary = "15 m.",
         .settings = {{"Range (m)", PRESET_CHIP_RANGE_M}}},
        {.name = "Long chip",
         .summary = "25 m.",
         .settings = {{"Range (m)", PRESET_LONG_CHIP_RANGE_M}}},
    };
  }

 protected:
  [[nodiscard]] StrikeKind kind() const override { return StrikeKind::CHIP; }
  [[nodiscard]] ForcedPassStyle style() const override
  {
    return ForcedPassStyle::CHIP;
  }
};

/**
 * A keeper's ball from his hands at a bullseye (goalkeeper plan step 5):
 * from the penalty spot, in his own area, a roll (up to 25 m) or overarm
 * throw (beyond), or a punt. Measures where it first comes down and how far
 * from him; set the range beyond his reach to see his longest.
 */
class KeeperThrowDrill final : public BullseyePassDrill
{
  static constexpr float GOALKEEPING_MAX = 100.0F;
  static constexpr float DEFAULT_GOALKEEPING = 70.0F;

 public:
  KeeperThrowDrill() { setInitialRange(INITIAL_KEEPER_THROW_RANGE_METRES); }
  [[nodiscard]] const char* name() const override { return "Keeper throw"; }
  [[nodiscard]] const char* summary() const override
  {
    return "A keeper with the ball in his hands on the penalty spot (his own "
           "area) lets it go at a bullseye towards the centre circle: rolled "
           "(up to 25 m) or thrown overarm (beyond), or punted. Measures "
           "where it first comes down, how far from where he let it go, and "
           "how it came off, with his Goalkeeping (Throwing's stand-in). "
           "Binturong body.";
  }
  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec keeper{.name = "Keeper",
                           .role = PlayerRole::GK,
                           .homeTeam = true,
                           .stats = {{"Goalkeeping", goalkeeping}},
                           .height = REFERENCE_KEEPER_HEIGHT_CM,
                           .derived = {}};
    keeper.condition = initialCondition();
    return {keeper};
  }
  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    preparePasser(engine, ids[0]);
    MatchScenario scenario;
    scenario.players.push_back(
        {.playerId = passerPlayerId(), .position = SPOT, .makingRun = false});
    scenario.carrierId = passerPlayerId();
    scenario.ballPosition = SPOT;
    scenario.carrierInHands = true;
    engine.startDrill(scenario);
    engine.forcePassTo(passerPlayerId(), target(), style());
    BullseyePassDrill::beginRun(engine);
  }
  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list = BullseyePassDrill::parameters();
    DrillParameter how =
        setting("Release", &releaseChoice, {.min = 0.0F, .max = 1.0F}, true,
                "A ground ball (rolled up to 25 m, thrown "
                "overarm beyond) or a punt.");
    how.choices = {"Roll or throw", "Punt"};
    how.section = "Keeper";
    list.insert(list.begin(), how);
    DrillParameter gk = setting("Goalkeeping", &goalkeeping,
                                {.min = 1.0F, .max = GOALKEEPING_MAX}, true,
                                "Throwing's stand-in: his fastest throw (20 m "
                                "at 1 to 50 m at 99) and its accuracy.");
    gk.section = "Keeper";
    list.insert(list.begin(), gk);
    return list;
  }
  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    constexpr float PRESET_ROLL_RANGE_M = 20.0F;
    constexpr float PRESET_OVERARM_THROW_RANGE_M = 40.0F;
    constexpr float PRESET_LONGEST_THROW_RANGE_M = 70.0F;
    constexpr float PRESET_PUNT_RANGE_M = 55.0F;

    return {
        {.name = "Roll",
         .summary = "20 m along the ground.",
         .settings = {{"Range (m)", PRESET_ROLL_RANGE_M}, {"Release", 0.0F}}},
        {.name = "Overarm throw",
         .summary = "40 m.",
         .settings = {{"Range (m)", PRESET_OVERARM_THROW_RANGE_M},
                      {"Release", 0.0F}}},
        {.name = "Longest throw",
         .summary = "70 m: beyond anyone, so it shows his longest.",
         .settings = {{"Range (m)", PRESET_LONGEST_THROW_RANGE_M},
                      {"Release", 0.0F}}},
        {.name = "Punt",
         .summary = "55 m.",
         .settings = {{"Range (m)", PRESET_PUNT_RANGE_M}, {"Release", 1.0F}}},
    };
  }
  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    std::vector<DrillMetric> out = BullseyePassDrill::metrics();
    if (const auto carry = landingCarryMetres())
    {
      out.push_back(
          {.name = "Carry (m, from where he let it go)", .value = *carry});
    }
    return out;
  }

 protected:
  [[nodiscard]] StrikeKind kind() const override
  {
    return releaseChoice < CHOICE_MIDPOINT ? StrikeKind::THROW
                                           : StrikeKind::LOFTED_PASS;
  }
  [[nodiscard]] ForcedPassStyle style() const override
  {
    return releaseChoice < CHOICE_MIDPOINT ? ForcedPassStyle::GROUND
                                           : ForcedPassStyle::LOFTED;
  }

 private:
  float goalkeeping = DEFAULT_GOALKEEPING;
  float releaseChoice = 0.0F;
};

// --- Shot target
// ---------------------------------------------------------------

/**
 * A dead-ball shot at a chosen spot in an empty goal (Notes and plans/
 * match_engine_shooting_technique_plan.md): the pace off his foot and at
 * the line, where it crosses the line against the spot, and how the strike
 * came off, with his Shooting and every shot strike knob as settings.
 */
class ShotTargetDrill final : public Drill
{
  [[nodiscard]] const char* carryPaceDescription() const
  {
    if (paceChoice < CHOICE_MIDPOINT)
    {
      return "jogging";
    }
    if (paceChoice < SECOND_CHOICE_THRESHOLD)
    {
      return "running";
    }
    return "sprinting";
  }

  static constexpr double RUN_AND_FLIGHT_TIMEOUT_SECONDS = 6.0;
  static constexpr double STRIKE_TIMEOUT_SECONDS = 10.0;
  static constexpr double RESULT_LINGER_SECONDS = 0.5;
  static constexpr float SECOND_CHOICE_THRESHOLD = 1.5F;
  static constexpr float LINE_MIN = 3.0F;
  static constexpr float LINE_MAX = 40.0F;
  static constexpr float SIDE_MIN = -25.0F;
  static constexpr float SIDE_MAX = 25.0F;
  static constexpr float AIM_ACROSS_MIN = -3.66F;
  static constexpr float AIM_ACROSS_MAX = 3.66F;
  static constexpr float AIM_HEIGHT_MIN = 0.1F;
  static constexpr float AIM_HEIGHT_MAX = 2.44F;
  static constexpr float TECHNIQUE_CHOICE_MAX = 4.0F;
  static constexpr float KEEPER_OFF_MAX = 12.0F;
  static constexpr float KEEPER_HEIGHT_MIN = 165.0F;
  static constexpr float KEEPER_HEIGHT_MAX = 205.0F;
  static constexpr float GOALKEEPING_MAX = 100.0F;
  static constexpr float FEED_CHOICE_MAX = 2.0F;
  static constexpr float BALL_SPEED_MIN = 2.0F;
  static constexpr float BALL_SPEED_MAX = 30.0F;
  static constexpr float FROM_ANGLE_MIN = -180.0F;
  static constexpr float FROM_ANGLE_MAX = 180.0F;
  static constexpr float FEED_DISTANCE_MIN = 5.0F;
  static constexpr float FEED_DISTANCE_MAX = 40.0F;
  static constexpr float BOUNCE_MAX = 3.0F;
  static constexpr float RUN_UP_MAX = 30.0F;
  static constexpr float RUN_DIRECTION_MIN = -180.0F;
  static constexpr float RUN_DIRECTION_MAX = 180.0F;
  static constexpr float PACE_CHOICE_MAX = 2.0F;
  static constexpr float SHOOTING_MAX = 100.0F;
  static constexpr float DRIBBLING_MAX = 100.0F;
  static constexpr float CONDITION_MIN = 30.0F;
  static constexpr float CONDITION_MAX = 100.0F;
  static constexpr float DEFAULT_LINE = 18.0F;
  static constexpr float DEFAULT_AIM_ACROSS = 2.5F;
  static constexpr float DEFAULT_AIM_HEIGHT = 0.5F;
  static constexpr float DEFAULT_KEEPER_OFF = 6.0F;
  static constexpr float DEFAULT_KEEPER_HEIGHT = 188.0F;
  static constexpr float DEFAULT_GOALKEEPING = 70.0F;
  static constexpr float DEFAULT_BALL_SPEED = 12.0F;
  static constexpr float DEFAULT_FROM_ANGLE = 90.0F;
  static constexpr float DEFAULT_FEED_DISTANCE = 20.0F;
  static constexpr float DEFAULT_SHOOTING = 70.0F;
  static constexpr float DEFAULT_DRIBBLING = 70.0F;
  static constexpr float DEFAULT_CONDITION = 100.0F;
  static constexpr ImU32 RUNNER_PATH_COLOUR = IM_COL32(255, 255, 255, 120);
  static constexpr ImU32 SUCCESS_COLOUR = IM_COL32(90, 220, 120, 255);
  static constexpr ImU32 MISSED_COLOUR = IM_COL32(240, 140, 60, 255);
  static constexpr float HALF_TURN_DEGREES = 180.0F;

 public:
  static constexpr float GOAL_HALF_WIDTH =
      MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES;
  static constexpr float BALL_RADIUS = MatchTuning::Units::BALL_RADIUS_METRES;
  static constexpr float CROSSBAR = MatchTuning::Units::CROSSBAR_HEIGHT_METRES;

  [[nodiscard]] const char* name() const override { return "Shot target"; }
  [[nodiscard]] const char* summary() const override
  {
    return "A shot from a dead ball at a spot you choose in an empty goal, "
           "nobody else on the pitch. Measures the pace off his foot and at "
           "the line, where it crosses the line against the spot (on target, "
           "wide or over), and how the strike came off, with his Shooting and "
           "every shot strike knob as settings.";
  }
  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    constexpr float PRESET_PENALTY_SPOT_OUT_FROM_GOAL_LINE_M = 11.0F;
    constexpr float PRESET_EDGE_OF_THE_BOX_OUT_FROM_GOAL_LINE_M = 18.0F;
    constexpr float PRESET_FROM_RANGE_OUT_FROM_GOAL_LINE_M = 25.0F;
    constexpr float PRESET_CUTBACK_FIRST_TIME_OUT_FROM_GOAL_LINE_M = 12.0F;
    constexpr float PRESET_CUTBACK_FIRST_TIME_BALL_SPEED_M_S = 12.0F;
    constexpr float PRESET_CUTBACK_FIRST_TIME_COMES_FROM_DEG = 45.0F;
    constexpr float PRESET_CUTBACK_FIRST_TIME_FEED_DISTANCE_M = 15.0F;
    constexpr float PRESET_CUTBACK_AFTER_A_TOUCH_OUT_FROM_GOAL_LINE_M = 12.0F;
    constexpr float PRESET_CUTBACK_AFTER_A_TOUCH_BALL_COMES_IN = 2.0F;
    constexpr float PRESET_CUTBACK_AFTER_A_TOUCH_BALL_SPEED_M_S = 12.0F;
    constexpr float PRESET_CUTBACK_AFTER_A_TOUCH_COMES_FROM_DEG = 45.0F;
    constexpr float PRESET_CUTBACK_AFTER_A_TOUCH_FEED_DISTANCE_M = 15.0F;
    constexpr float PRESET_LAY_OFF_FIRST_TIME_OUT_FROM_GOAL_LINE_M = 18.0F;
    constexpr float PRESET_LAY_OFF_FIRST_TIME_BALL_SPEED_M_S = 10.0F;
    constexpr float PRESET_LAY_OFF_FIRST_TIME_FEED_DISTANCE_M = 10.0F;

    return {
        {.name = "Penalty spot",
         .summary = "11 m, central.",
         .settings = {{"Out from goal line (m)",
                       PRESET_PENALTY_SPOT_OUT_FROM_GOAL_LINE_M},
                      {"Off centre (m)", 0.0F}}},
        {.name = "Edge of the box",
         .summary = "18 m, central.",
         .settings = {{"Out from goal line (m)",
                       PRESET_EDGE_OF_THE_BOX_OUT_FROM_GOAL_LINE_M},
                      {"Off centre (m)", 0.0F}}},
        {.name = "From range",
         .summary = "25 m, central.",
         .settings = {{"Out from goal line (m)",
                       PRESET_FROM_RANGE_OUT_FROM_GOAL_LINE_M},
                      {"Off centre (m)", 0.0F}}},
        {.name = "Cutback, first time",
         .summary = "12 m out, central: a 12 m/s ball pulled back from the "
                    "byline on his "
                    "right, struck first time.",
         .settings = {{"Out from goal line (m)",
                       PRESET_CUTBACK_FIRST_TIME_OUT_FROM_GOAL_LINE_M},
                      {"Off centre (m)", 0.0F},
                      {"Ball comes in", 1.0F},
                      {"Ball speed (m/s)",
                       PRESET_CUTBACK_FIRST_TIME_BALL_SPEED_M_S},
                      {"Comes from (deg)",
                       PRESET_CUTBACK_FIRST_TIME_COMES_FROM_DEG},
                      {"Feed distance (m)",
                       PRESET_CUTBACK_FIRST_TIME_FEED_DISTANCE_M}}},
        {.name = "Cutback, after a touch",
         .summary = "The same, controlled first.",
         .settings =
             {{"Out from goal line (m)",
               PRESET_CUTBACK_AFTER_A_TOUCH_OUT_FROM_GOAL_LINE_M},
              {"Off centre (m)", 0.0F},
              {"Ball comes in", PRESET_CUTBACK_AFTER_A_TOUCH_BALL_COMES_IN},
              {"Ball speed (m/s)", PRESET_CUTBACK_AFTER_A_TOUCH_BALL_SPEED_M_S},
              {"Comes from (deg)", PRESET_CUTBACK_AFTER_A_TOUCH_COMES_FROM_DEG},
              {"Feed distance (m)",
               PRESET_CUTBACK_AFTER_A_TOUCH_FEED_DISTANCE_M}}},
        {.name = "Lay-off, first time",
         .summary =
             "18 m out: a 10 m/s ball rolled back to him from in front, struck "
             "first time.",
         .settings = {{"Out from goal line (m)",
                       PRESET_LAY_OFF_FIRST_TIME_OUT_FROM_GOAL_LINE_M},
                      {"Off centre (m)", 0.0F},
                      {"Ball comes in", 1.0F},
                      {"Ball speed (m/s)",
                       PRESET_LAY_OFF_FIRST_TIME_BALL_SPEED_M_S},
                      {"Comes from (deg)", 0.0F},
                      {"Feed distance (m)",
                       PRESET_LAY_OFF_FIRST_TIME_FEED_DISTANCE_M}}},
    };
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    const auto add = [&list](DrillParameter parameter, const char* section)
    {
      parameter.section = section;
      list.push_back(std::move(parameter));
    };
    add(setting("Out from goal line (m)", &line,
                {.min = LINE_MIN, .max = LINE_MAX}, false,
                "Distance of the ball from the goal line."),
        "Shot");
    add(setting("Off centre (m)", &side, {.min = SIDE_MIN, .max = SIDE_MAX},
                false,
                "Sideways from the middle of the goal (+ towards the bottom "
                "touchline)."),
        "Shot");
    add(setting("Aim across (m)", &aimAcross,
                {.min = AIM_ACROSS_MIN, .max = AIM_ACROSS_MAX}, false,
                "Where in the goal he aims: from its middle (+ towards the "
                "bottom touchline; a post is 3.66 m out)."),
        "Shot");
    add(setting("Aim height (m)", &aimHeight,
                {.min = AIM_HEIGHT_MIN, .max = AIM_HEIGHT_MAX}, false,
                "How high he aims to cross the line (the bar is at 2.44 m)."),
        "Shot");
    DrillParameter power =
        setting("Power share", &powerShare, {.min = 0.0F, .max = 1.0F}, false,
                "The share of his shot power he strikes it with. 0: as the "
                "engine strikes a shot (Shooting::DEFAULT_POWER_SHARE).");
    power.automatic = [] { return MatchTuning::Shooting::DEFAULT_POWER_SHARE; };
    add(power, "Shot");
    DrillParameter how = setting(
        "Technique", &techniqueChoice,
        {.min = 0.0F, .max = TECHNIQUE_CHOICE_MAX}, true,
        "Driven: the instep, hard, a little topspin (every shot's until the "
        "brain chooses).\nPlaced: the side of the foot, slower.\nCurled: "
        "the instep's wrap, sidespin bending it in towards the middle.\n"
        "Chip: stabbed under, backspin, over the keeper.\nToe-poke.");
    how.choices = {"Driven", "Placed", "Curled", "Chip", "Toe-poke"};
    add(how, "Shot");
    DrillParameter keeper = setting(
        "Keeper", &keeperChoice, {.min = 0.0F, .max = 1.0F}, true,
        "None: an empty goal.\nOff his line: a keeper held on the line from "
        "the goal's middle to the ball, this far out (a chip goes over his "
        "reach).");
    keeper.choices = {"None", "Off his line"};
    add(keeper, "Keeper");
    add(setting("Keeper off his line (m)", &keeperOff,
                {.min = 0.0F, .max = KEEPER_OFF_MAX}, false),
        "Keeper");
    add(setting("Keeper height (cm)", &keeperHeight,
                {.min = KEEPER_HEIGHT_MIN, .max = KEEPER_HEIGHT_MAX}, true),
        "Keeper");
    add(setting("Goalkeeping", &goalkeeping,
                {.min = 1.0F, .max = GOALKEEPING_MAX}, true),
        "Keeper");
    DrillParameter feed = setting(
        "Ball comes in", &feedChoice, {.min = 0.0F, .max = FEED_CHOICE_MAX},
        true,
        "On his feet: a dead ball (or his run-up's).\nFirst time: played to "
        "him on the spot, and struck as it reaches him.\nAfter a touch: "
        "played to him, controlled, then struck.");
    feed.choices = {"On his feet", "First time", "After a touch"};
    add(feed, "Ball");
    add(setting("Ball speed (m/s)", &ballSpeed,
                {.min = BALL_SPEED_MIN, .max = BALL_SPEED_MAX}, false,
                "Pace when it is played to him."),
        "Ball");
    add(setting("Comes from (deg)", &fromAngle,
                {.min = FROM_ANGLE_MIN, .max = FROM_ANGLE_MAX}, true,
                "Where it is played from, against the way to goal: 0 from "
                "the goal's side, 90 from the bottom touchline's side, -90 "
                "from the top's, 180 from behind him."),
        "Ball");
    add(setting("Feed distance (m)", &feedDistance,
                {.min = FEED_DISTANCE_MIN, .max = FEED_DISTANCE_MAX}, false),
        "Ball");
    add(setting("Bounce (m/s up)", &bounce, {.min = 0.0F, .max = BOUNCE_MAX},
                false,
                "Upward speed when played: 0 along the ground. A ball off "
                "the ground when it reaches him is taken with a touch."),
        "Ball");
    add(setting("Run-up (m)", &runUp, {.min = 0.0F, .max = RUN_UP_MAX}, false,
                "0: he shoots standing. Otherwise he runs with the ball this "
                "far, ending on the spot, then shoots on the move (a ball on "
                "his feet only)."),
        "Shooter");
    add(setting("Run-up direction (deg)", &runDirection,
                {.min = RUN_DIRECTION_MIN, .max = RUN_DIRECTION_MAX}, true,
                "Which way he runs onto the spot against the way to goal: 0 "
                "straight at it, 90 across it towards the bottom touchline, "
                "-90 towards the top, 180 away from goal."),
        "Shooter");
    DrillParameter pace = setting(
        "Run-up pace", &paceChoice, {.min = 0.0F, .max = PACE_CHOICE_MAX}, true,
        "How hard he carries it on the run-up: a jog, a run or a "
        "sprint (as his Dribbling allows).");
    pace.choices = {"Jog", "Run", "Sprint"};
    add(pace, "Shooter");
    add(setting("Shooting", &shooting, {.min = 1.0F, .max = SHOOTING_MAX},
                true),
        "Shooter");
    add(setting("Dribbling", &dribbling, {.min = 1.0F, .max = DRIBBLING_MAX},
                true,
                "With the ball on the run-up: his pace, agility and "
                "acceleration with it."),
        "Shooter");
    add(setting("Condition", &condition,
                {.min = CONDITION_MIN, .max = CONDITION_MAX}, true,
                "How fresh he is at the start (tiredness widens the errors)."),
        "Shooter");
    DrillParameter foot =
        setting("Foot", &footChoice, {.min = 0.0F, .max = 1.0F}, true);
    foot.choices = {"Right", "Left"};
    add(foot, "Shooter");
    // Names live as long as the drill: the settings point at them.
    const auto kindName =
        STRIKE_KIND_NAMES.at(static_cast<std::size_t>(StrikeKind::SHOT));
    for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
    {
      knobNames.at(index) =
          strikeKnobName(StrikeKind::SHOT, static_cast<StrikeKnob>(index));
      knobSections.at(index) = std::string(kindName) + " knobs: " +
                               std::string(STRIKE_KNOBS.at(index).group);
    }
    for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
    {
      const StrikeKnobInfo& info = STRIKE_KNOBS.at(index);
      DrillParameter knob =
          setting(knobNames.at(index).c_str(),
                  &knobs.at(StrikeKind::SHOT, static_cast<StrikeKnob>(index)),
                  {.min = info.min, .max = info.max}, false);
      knob.collapsible = true;
      knob.label = info.name.data();
      add(knob, knobSections.at(index).c_str());
    }
    return list;
  }

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec shooter{
        .name = "Shooter",
        .role = PlayerRole::ST,
        .homeTeam = true,
        .stats = {{"Shooting", shooting}, {"Dribbling", dribbling}},
        .height = REFERENCE_PLAYER_HEIGHT_CM,
        .derived = {}};
    shooter.foot = footChoice < CHOICE_MIDPOINT ? Foot::Right : Foot::Left;
    shooter.condition = condition;
    std::vector<DrillPlayerSpec> list{shooter};
    if (keeperChoice > CHOICE_MIDPOINT)
    {
      list.push_back({.name = "Keeper",
                      .role = PlayerRole::GK,
                      .homeTeam = false,
                      .stats = {{"Goalkeeping", goalkeeping}},
                      .height = static_cast<int>(keeperHeight),
                      .derived = {}});
    }
    return list;
  }

  /** The technique asked (none: driven, as the engine strikes a shot). */
  [[nodiscard]] std::optional<ShotTechnique> technique() const
  {
    if (techniqueChoice < CHOICE_MIDPOINT)
    {
      return std::nullopt;
    }
    return static_cast<ShotTechnique>(
        static_cast<int>(std::round(techniqueChoice)));
  }

  /** The keeper, held on his spot until the shot. */
  void placeKeeper(MatchEngine& engine, MatchScenario& scenario,
                   const std::vector<PlayerID>& ids)
  {
    hasKeeper = keeperChoice > CHOICE_MIDPOINT && ids.size() > 1;
    keeperId = hasKeeper ? ids[1] : 0;
    if (!hasKeeper)
    {
      return;
    }
    const PitchPoint goal = at(1.0F, PITCH_MIDPOINT_SHARE);
    keeperAt = along(
        goal, directionBetween(goal, spot),
        std::min(keeperOff, std::max(metresBetween(goal, spot) - 1.0F, 0.0F)));
    keeperAt.x = std::min(keeperAt.x, PITCH.atX(PITCH_EDGE_MAX_SHARE));
    scenario.players.push_back(
        {.playerId = keeperId, .position = keeperAt, .makingRun = false});
    (void)engine;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    shooterId = ids[0];
    engine.setStrikeKnobs(knobs);
    spot = at(std::clamp(1.0F - (line / LENGTH), PITCH_MIDPOINT_SHARE,
                         PITCH_EDGE_MAX_SHARE),
              std::clamp(PITCH_MIDPOINT_SHARE + (side / WIDTH),
                         PITCH_SAFE_MIN_SHARE, PITCH_SAFE_MAX_SHARE));
    ordered = false;
    orderedAt = {};
    touchedFirst = false;
    strike.reset();
    outcome.clear();
    endAt.reset();
    ballPath.clear();
    if (feedChoice > CHOICE_MIDPOINT)
    {
      // Played to him on the spot from `fromAngle` round from the goal's
      // side; the shot is ordered as it is played (first time, or after his
      // touch).
      const Vector2F from =
          rotated({.x = 1.0F, .y = 0.0F}, fromAngle * PI / HALF_TURN_DEGREES);
      feedFrom = along(spot, from, feedDistance);
      feedFrom =
          PitchPoint{std::clamp(feedFrom.x, PITCH.atX(PITCH_SAFE_MIN_SHARE),
                                PITCH.atX(PITCH_SAFE_MAX_SHARE)),
                     std::clamp(feedFrom.y, PITCH.atY(PITCH_SAFE_MIN_SHARE),
                                PITCH.atY(PITCH_SAFE_MAX_SHARE))};
      start = spot;
      MatchScenario scenario;
      scenario.players.push_back(
          {.playerId = shooterId, .position = spot, .makingRun = false});
      placeKeeper(engine, scenario, ids);
      scenario.carrierId = 0;
      scenario.ballPosition = feedFrom;
      engine.startDrill(scenario);
      if (hasKeeper)
      {
        engine.setDrillTarget(keeperId, keeperAt, false);
      }
      engine.forceShot(
          shooterId, Vector2F{.x = aimAcross, .y = aimHeight},
          powerShare > 0.0F ? std::optional<float>(powerShare) : std::nullopt,
          feedChoice < SECOND_CHOICE_THRESHOLD, technique());
      const Vector2F travel = spot - feedFrom;
      const float metres = std::hypot(travel.x, travel.y);
      lastBallVelocity = metres > 0.0F
                             ? Vector2F{.x = travel.x / metres * ballSpeed,
                                        .y = travel.y / metres * ballSpeed}
                             : Vector2F{};
      engine.feedBall(feedFrom, lastBallVelocity, bounce, shooterId);
      before = engine.getStrikeContacts();
      ordered = true;
      if (const MatchPlayer* shooter = slotOf(engine, shooterId))
      {
        orderedAt = Moment{.at = secondsOf(engine),
                           .body = bodyAngleTo(*shooter, lineToAim(spot)),
                           .speed = speedOf(*shooter)};
      }
      ballPath.push_back(feedFrom);
      return;
    }
    // His run-up ends on the spot, along `runDirection` from the way to goal.
    const Vector2F runWay =
        rotated({.x = 1.0F, .y = 0.0F}, runDirection * PI / HALF_TURN_DEGREES);
    start = along(spot, runWay, -runUp);
    start = PitchPoint{std::clamp(start.x, PITCH.atX(PITCH_SAFE_MIN_SHARE),
                                  PITCH.atX(PITCH_SAFE_MAX_SHARE)),
                       std::clamp(start.y, PITCH.atY(PITCH_SAFE_MIN_SHARE),
                                  PITCH.atY(PITCH_SAFE_MAX_SHARE))};
    MatchScenario scenario;
    scenario.players.push_back(
        {.playerId = shooterId, .position = start, .makingRun = false});
    placeKeeper(engine, scenario, ids);
    scenario.carrierId = shooterId;
    scenario.ballPosition = start;
    engine.startDrill(scenario);
    if (hasKeeper)
    {
      engine.setDrillTarget(keeperId, keeperAt, false);
    }
    // He keeps it (deciding nothing) until the shot is ordered: alone in
    // front of goal, he would otherwise shoot as he pleased. On a run-up he
    // carries it through the spot.
    if (runUp > 0.0F)
    {
      engine.keepBall(shooterId, true,
                      (paceChoice + 1.0F) / CARRY_PACE_LEVEL_COUNT);
      engine.setDrillTarget(shooterId, along(spot, runWay, RUN_THROUGH_METRES),
                            true);
    }
    else
    {
      engine.keepBall(shooterId, true);
      engine.setDrillTarget(shooterId, spot, false);
    }
  }

 private:
  void observeStrike(MatchEngine& engine, const MatchPlayer* shooter,
                     double now)
  {
    const MatchBall& ball = engine.getBall();
    // A ball played to him: his touch, if he takes one, before the shot.
    if ((engine.getPossessionAttachment() != nullptr) &&
        engine.getPossessionAttachment()->getId() == shooterId)
    {
      touchedFirst = true;
    }
    readStrike(engine, now);
    if (strike && (shooter != nullptr))
    {
      strike->bodyAngle = bodyAngleTo(*shooter, ball.velocity);
      strike->shooterSpeed = speedOf(*shooter);
      // The ball as it came in (a step before), against where it went.
      strike->firstTime = feedChoice > CHOICE_MIDPOINT && !touchedFirst;
      strike->incoming = std::hypot(lastBallVelocity.x, lastBallVelocity.y);
      const float out = std::hypot(ball.velocity.x, ball.velocity.y);
      if (strike->incoming > MIN_REPORTED_CONTACT_SPEED &&
          out > MIN_REPORTED_CONTACT_SPEED)
      {
        strike->redirect = turnDegrees(
            {.x = lastBallVelocity.x / strike->incoming,
             .y = lastBallVelocity.y / strike->incoming},
            {.x = ball.velocity.x / out, .y = ball.velocity.y / out});
      }
      // He stays where he struck it; the keeper is free to save it.
      engine.setDrillTarget(shooterId, shooter->position, false);
      if (hasKeeper)
      {
        engine.setDrillTarget(keeperId, std::nullopt, false);
      }
      eventsAtStrike = engine.getEvents().size();
    }
    else
    {
      lastBallVelocity = ball.velocity;
    }
  }

  void observeKeeper(const MatchEngine& engine)
  {
    if (!strike)
    {
      return;
    }
    const MatchBall& ball = engine.getBall();
    // Over the keeper: its height as it passes where he stood.
    if (strike->overKeeper < 0.0F && ball.position.x >= keeperAt.x)
    {
      strike->overKeeper = ball.z;
    }
    const auto& events = engine.getEvents();
    for (std::size_t index = eventsAtStrike; index < events.size(); ++index)
    {
      if (events[index].type == MatchEventType::SAVE)
      {
        outcome = "saved";
      }
    }
    if ((engine.getPossessionAttachment() != nullptr) &&
        engine.getPossessionAttachment()->getId() == keeperId)
    {
      outcome = "saved";
    }
  }

 public:
  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    if (engine.getSimulatedSteps() % 2 == 0)
    {
      ballPath.push_back(ball.position);
    }
    const MatchPlayer* shooter = slotOf(engine, shooterId);
    if (!ordered && engine.getState() == MatchState::PLAYING &&
        (shooter != nullptr) &&
        (runUp <= 0.0F || metresBetween(start, shooter->position) >= runUp ||
         now >= RUN_AND_FLIGHT_TIMEOUT_SECONDS))
    {
      // Once play is running (a shot struck before then is a dead ball,
      // which the contact tallies leave out), and his run-up is done: his
      // body against the shot's line then, and his speed.
      orderedAt =
          Moment{.at = now,
                 .body = bodyAngleTo(*shooter, lineToAim(ball.position)),
                 .speed = speedOf(*shooter)};
      engine.keepBall(shooterId, false);
      engine.forceShot(
          shooterId, Vector2F{.x = aimAcross, .y = aimHeight},
          powerShare > 0.0F ? std::optional<float>(powerShare) : std::nullopt,
          false, technique());
      before = engine.getStrikeContacts();
      ordered = true;
    }
    else if (ordered && !strike)
    {
      observeStrike(engine, shooter, now);
    }
    if (!strike && outcome.empty() && now >= STRIKE_TIMEOUT_SECONDS)
    {
      outcome = "no shot";
    }
    else if (strike && outcome.empty() && hasKeeper)
    {
      observeKeeper(engine);
    }
    if (strike && outcome.empty())
    {
      // The last reading before it crosses the line: its pace there.
      if (ball.possessionState().isShot && ball.position.x < PITCH.atX(1.0F))
      {
        strike->lineSpeed = std::sqrt((ball.velocity.x * ball.velocity.x) +
                                      (ball.velocity.y * ball.velocity.y) +
                                      (ball.velocityZ * ball.velocityZ));
        strike->lineSeconds = now - strike->at;
      }
      if (ball.position.x >= PITCH.atX(1.0F) ||
          engine.getState() != MatchState::PLAYING)
      {
        outcome = result(*strike);
      }
      else if (!ball.possessionState().isShot ||
               now >= strike->at + RUN_AND_FLIGHT_TIMEOUT_SECONDS)
      {
        outcome = "short";
      }
    }
    if (!outcome.empty() && !endAt)
    {
      endAt = now;
    }
    return (!endAt || now < *endAt + RESULT_LINGER_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::path(renderer, draw, ballPath, RUNNER_PATH_COLOUR,
                    BALL_TRAIL_WIDTH_PIXELS * scale);
    const PitchPoint aimAt =
        at(1.0F, PITCH_MIDPOINT_SHARE + (aimAcross / WIDTH));
    DrillDraw::marker(renderer, draw, aimAt,
                      PRIMARY_MARKER_RADIUS_PIXELS * scale, SUCCESS_COLOUR,
                      "aim");
    if (strike)
    {
      DrillDraw::marker(
          renderer, draw,
          at(1.0F, PITCH_MIDPOINT_SHARE + (strike->crossingY / WIDTH)),
          PRIMARY_MARKER_RADIUS_PIXELS * scale,
          outcome == "on target" ? SUCCESS_COLOUR : MISSED_COLOUR);
    }
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    constexpr float DEGREES = HALF_TURN_DEGREES / PI;
    lines.push_back(std::format(
        "{:.1f} m out, {:+.1f} m off centre · aim {:+.2f} m across, {:.2f} m "
        "up "
        "· {:.0f}% of his power",
        line, side, aimAcross, aimHeight,
        100.0F * (powerShare > 0.0F
                      ? powerShare
                      : MatchTuning::Shooting::DEFAULT_POWER_SHARE)));
    lines.push_back(std::format(
        "Shooter: Shooting {:.0f}, condition {:.0f}, {} foot", shooting,
        condition, footChoice < CHOICE_MIDPOINT ? "right" : "left"));
    if (feedChoice > CHOICE_MIDPOINT)
    {
      lines.push_back(std::format(
          "Played in at {:.0f} m/s from {:+.0f} deg, {:.0f} m out{} · {}{}",
          ballSpeed, fromAngle, feedDistance,
          bounce > 0.0F ? std::format(", {:.1f} m/s up", bounce)
                        : std::string(),
          feedChoice < SECOND_CHOICE_THRESHOLD ? "first time" : "after a touch",
          strike ? std::format(": {} (in at {:.1f} m/s, turned {:.0f} deg)",
                               strike->firstTime ? "struck first time"
                                                 : "struck after a touch",
                               strike->incoming, strike->redirect)
                 : std::string()));
    }
    else if (runUp > 0.0F)
    {
      lines.push_back(
          std::format("Run-up: {:.0f} m at {:+.0f} deg to the goal, {}", runUp,
                      runDirection, carryPaceDescription()));
    }
    if (orderedAt && strike)
    {
      lines.push_back(std::format(
          "Body off the shot: {:.0f} deg at the order, {:.0f} deg at the "
          "strike "
          "({:.2f} s later); speed {:.1f} -> {:.1f} m/s",
          orderedAt->body, strike->bodyAngle, strike->at - orderedAt->at,
          orderedAt->speed, strike->shooterSpeed));
    }
    if (strike)
    {
      lines.push_back(std::format(
          "{} at {:.2f} s: {:.1f} m/s ({:.1f} up), spin {:+.1f} side, {:+.1f} "
          "top "
          "rev/s{}",
          STRIKE_KIND_NAMES.at(static_cast<std::size_t>(strike->kind)),
          strike->at, strike->speed, strike->lift,
          strike->spin.side / (FULL_TURN_RADIANS),
          strike->spin.top / (FULL_TURN_RADIANS),
          strike->overKeeper >= 0.0F
              ? std::format(" · {:.2f} m up over the keeper",
                            strike->overKeeper)
              : std::string()));
      lines.push_back(std::format(
          "{}  angle: {:.1f} deg off (elevation {:+.1f} deg)",
          strike->angleFailed ? "FAIL" : "PASS", strike->angleError * DEGREES,
          strike->elevationError * DEGREES));
      lines.push_back(std::format("{}  weight: {:+.2f} m/s",
                                  strike->weightFailed ? "FAIL" : "PASS",
                                  strike->weightError));
      lines.push_back(std::format(
          "Crosses the line {:+.2f} m across, {:.2f} m up: missed the spot by "
          "{:+.2f} m across, {:+.2f} m up",
          strike->crossingY, strike->crossingZ, strike->crossingY - aimAcross,
          strike->crossingZ - aimHeight));
      if (strike->lineSpeed > 0.0F)
      {
        lines.push_back(std::format("At the line: {:.1f} m/s after {:.2f} s",
                                    strike->lineSpeed, strike->lineSeconds));
      }
    }
    if (!outcome.empty())
    {
      lines.push_back("OUTCOME  " + outcome);
    }
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty() || !strike)
    {
      return {};
    }
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    constexpr float DEGREES = HALF_TURN_DEGREES / PI;
    std::vector<DrillMetric> out{
        {.name = "Launch speed (m/s)", .value = strike->speed},
        {.name = "Launch lift (m/s)", .value = strike->lift},
        {.name = "Speed at the line (m/s)", .value = strike->lineSpeed},
        {.name = "Seconds to the line", .value = strike->lineSeconds},
        {.name = "On target", .value = flag(outcome == "on target")},
        {.name = "Wide", .value = flag(outcome == "wide")},
        {.name = "Over", .value = flag(outcome == "over")},
        {.name = "Short", .value = flag(outcome == "short")},
        {.name = "Miss across (m)", .value = strike->crossingY - aimAcross},
        {.name = "Abs miss across (m)",
         .value = std::abs(strike->crossingY - aimAcross)},
        {.name = "Miss height (m, + higher)",
         .value = strike->crossingZ - aimHeight},
        {.name = "Power asked (condition)",
         .value = strike->factors.at(POWER_ASKED_FACTOR)},
        {.name = "Saved", .value = flag(outcome == "saved")},
        {.name = "Height over the keeper (m)", .value = strike->overKeeper},
        {.name = "Sidespin (rev/s)",
         .value = strike->spin.side / (FULL_TURN_RADIANS)},
        {.name = "Topspin (rev/s, - backspin)",
         .value = strike->spin.top / (FULL_TURN_RADIANS)},
        {.name = "First time", .value = flag(strike->firstTime)},
        {.name = "Ball in (m/s)", .value = strike->incoming},
        {.name = "Turned (deg)", .value = strike->redirect},
        {.name = "Body off the shot at order (deg)",
         .value = orderedAt ? orderedAt->body : 0.0F},
        {.name = "Body off the shot at strike (deg)",
         .value = strike->bodyAngle},
        {.name = "Order to strike (s)",
         .value = orderedAt ? strike->at - orderedAt->at : 0.0},
        {.name = "Speed at order (m/s)",
         .value = orderedAt ? orderedAt->speed : 0.0F},
        {.name = "Speed lost (m/s)",
         .value = orderedAt ? orderedAt->speed - strike->shooterSpeed : 0.0F},
        {.name = "Angle difficulty", .value = strike->angleDifficulty},
        {.name = "Weight difficulty", .value = strike->weightDifficulty},
        {.name = "Angle failed", .value = flag(strike->angleFailed)},
        {.name = "Weight failed", .value = flag(strike->weightFailed)},
        {.name = "Abs angle error (deg)",
         .value = strike->angleError * DEGREES},
        {.name = "Weight error (m/s)", .value = strike->weightError},
        {.name = "Elevation error (deg, + higher)",
         .value = strike->elevationError * DEGREES},
    };
    // Every condition's value (0-1).
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
    {
      out.push_back(
          {.name = "Condition: " + std::string(CONTACT_FACTOR_LABELS.at(index)),
           .value = strike->factors.at(index)});
    }
    return out;
  }

  void renderAfterSettings() const override
  {
    ImGui::SeparatorText("Difficulty of the last shot");
    if (!strike)
    {
      SandboxText::disabledText("No shot yet.");
      return;
    }
    if (!ImGui::BeginTable("##difficulty", 2,
                           ImGuiTableFlags_SizingStretchProp))
    {
      return;
    }
    const auto row = [](const char* name, float value, bool strong)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (strong)
      {
        ImGui::TextUnformatted(name);
      }
      else
      {
        SandboxText::disabledText(name);
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(
          std::format("{:.3f}", static_cast<double>(value)).c_str());
    };
    row("Angle difficulty", strike->angleDifficulty, true);
    row("Weight difficulty", strike->weightDifficulty, true);
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
    {
      if (strike->factors.at(index) > MIN_REPORTED_CONTACT_FACTOR)
      {
        row(std::string(CONTACT_FACTOR_LABELS.at(index)).c_str(),
            strike->factors.at(index), false);
      }
    }
    ImGui::EndTable();
  }

 private:
  struct Strike
  {
    double at = 0.0;
    bool angleFailed = false;
    bool weightFailed = false;
    float angleError = 0.0F;
    float weightError = 0.0F;
    float elevationError = 0.0F;
    float angleDifficulty = 0.0F;
    float weightDifficulty = 0.0F;
    std::array<float, CONTACT_FACTOR_COUNT> factors{};
    /** Off his foot: along the ground and upward (m/s). */
    float speed = 0.0F;
    float lift = 0.0F;
    /** Where its flight crosses the goal line (m from the goal's middle,
     * and height), as the engine launched it. */
    float crossingY = 0.0F;
    float crossingZ = 0.0F;
    /** Its pace (m/s, in 3D) at the last step before the line, and when. */
    float lineSpeed = 0.0F;
    double lineSeconds = 0.0;
    /** Where it went against where his body faced (deg), and his speed. */
    float bodyAngle = 0.0F;
    float shooterSpeed = 0.0F;
    /** A ball played to him: struck first time (no touch), the pace it came
     * in at (m/s) and how far it was turned (deg). */
    bool firstTime = false;
    float incoming = 0.0F;
    float redirect = 0.0F;
    /** The kind it was struck as, and the spin it left with (rad/s). */
    StrikeKind kind = StrikeKind::SHOT;
    BallSpin spin{};
    /** Its height as it passed the keeper (m; -1 not yet). */
    float overKeeper = -1.0F;
  };
  /** When the shot was ordered: his body against its line (deg), his
   * speed. */
  struct Moment
  {
    double at = 0.0;
    float body = 0.0F;
    float speed = 0.0F;
  };

  /** From the ball to the spot he aims at (metres). */
  [[nodiscard]] Vector2F lineToAim(PitchPoint ballAt) const
  {
    return {
        .x = PITCH.mirrorX(ballAt.x),
        .y = PITCH.atY(PITCH_MIDPOINT_SHARE + (aimAcross / WIDTH)) - ballAt.y};
  }
  /** His body's angle (deg) to a direction (metres). */
  static float bodyAngleTo(const MatchPlayer& player, Vector2F line)
  {
    const PitchPoint body{std::cos(player.facingAngle),
                          std::sin(player.facingAngle)};
    const float lengths =
        std::hypot(body.x, body.y) * std::hypot(line.x, line.y);
    if (lengths <= 0.0F)
    {
      return 0.0F;
    }
    return std::acos(std::clamp((body.x * line.x + body.y * line.y) / lengths,
                                -1.0F, 1.0F)) *
           HALF_TURN_DEGREES / PI;
  }

  /** The shot: a shot strike the contact model has read since the start. */
  void readStrike(const MatchEngine& engine, double now)
  {
    // Whichever kind of shot it was struck as.
    static constexpr std::array<StrikeKind, 5> SHOTS = {
        StrikeKind::SHOT, StrikeKind::PLACED_SHOT, StrikeKind::CURLED_SHOT,
        StrikeKind::CHIP_SHOT, StrikeKind::TOE_POKE};
    std::size_t index = STRIKE_KIND_COUNT;
    for (const StrikeKind kind : SHOTS)
    {
      const auto at = static_cast<std::size_t>(kind);
      if (engine.getStrikeContacts().byKind.at(at).strikes >
          before.byKind.at(at).strikes)
      {
        index = at;
      }
    }
    if (index == STRIKE_KIND_COUNT)
    {
      return;
    }
    const auto& a = before.byKind.at(index);
    const auto& b = engine.getStrikeContacts().byKind.at(index);
    const MatchBall& ball = engine.getBall();
    Strike reading;
    reading.at = now;
    reading.kind = static_cast<StrikeKind>(index);
    reading.spin = ball.spin;
    reading.angleFailed = b.angleFailed > a.angleFailed;
    reading.weightFailed = b.weightFailed > a.weightFailed;
    reading.angleError = static_cast<float>(b.angleErrorSum - a.angleErrorSum);
    reading.weightError =
        static_cast<float>(b.weightErrorSum - a.weightErrorSum);
    reading.elevationError =
        static_cast<float>(b.elevationErrorSum - a.elevationErrorSum);
    reading.angleDifficulty =
        static_cast<float>(b.angleDifficulty - a.angleDifficulty);
    reading.weightDifficulty =
        static_cast<float>(b.weightDifficulty - a.weightDifficulty);
    for (std::size_t factor = 0; factor < CONTACT_FACTOR_COUNT; ++factor)
    {
      reading.factors.at(factor) =
          static_cast<float>(b.factorSum.at(factor) - a.factorSum.at(factor));
    }
    reading.speed = std::hypot(ball.velocity.x, ball.velocity.y);
    reading.lift = ball.velocityZ;
    reading.crossingY =
        ball.possessionState().shotTargetY - PITCH.atY(PITCH_MIDPOINT_SHARE);
    reading.crossingZ = ball.possessionState().shotTargetHeightMetres;
    strike = reading;
  }

  /** Where it crossed the line, against the frame. */
  [[nodiscard]] static std::string result(const Strike& reading)
  {
    if (std::abs(reading.crossingY) >= GOAL_HALF_WIDTH - BALL_RADIUS)
    {
      return "wide";
    }
    if (reading.crossingZ >= CROSSBAR - BALL_RADIUS)
    {
      return "over";
    }
    return "on target";
  }

  float line = DEFAULT_LINE;
  float side = 0.0F;
  float aimAcross = DEFAULT_AIM_ACROSS;
  float aimHeight = DEFAULT_AIM_HEIGHT;
  float powerShare = 0.0F;
  float techniqueChoice = 0.0F;
  float keeperChoice = 0.0F;
  float keeperOff = DEFAULT_KEEPER_OFF;
  float keeperHeight = DEFAULT_KEEPER_HEIGHT;
  float goalkeeping = DEFAULT_GOALKEEPING;
  float feedChoice = 0.0F;
  float ballSpeed = DEFAULT_BALL_SPEED;
  float fromAngle = DEFAULT_FROM_ANGLE;
  float feedDistance = DEFAULT_FEED_DISTANCE;
  float bounce = 0.0F;
  float runUp = 0.0F;
  float runDirection = 0.0F;
  float paceChoice = 1.0F;
  float shooting = DEFAULT_SHOOTING;
  float dribbling = DEFAULT_DRIBBLING;
  float condition = DEFAULT_CONDITION;
  float footChoice = 0.0F;
  StrikeKnobs knobs;

  PlayerID shooterId = 0;
  PitchPoint spot;
  PitchPoint start;
  PitchPoint feedFrom;
  Vector2F lastBallVelocity{};
  bool touchedFirst = false;
  bool hasKeeper = false;
  PlayerID keeperId = 0;
  PitchPoint keeperAt;
  std::size_t eventsAtStrike = 0;
  bool ordered = false;
  std::optional<Moment> orderedAt;
  StrikeContactStats before;
  std::optional<Strike> strike;
  std::string outcome;
  std::optional<double> endAt;
  std::vector<PitchPoint> ballPath;
  std::array<std::string, STRIKE_KNOB_COUNT> knobNames;
  std::array<std::string, STRIKE_KNOB_COUNT> knobSections;
};

// --- Header target
// -------------------------------------------------------------

/**
 * One high ball served to a lone player, who heads it where the drill says
 * (MatchEngine::forceHeader): on to a spot on the ground, at a spot in the
 * goal, or cushioned and kept. Nobody else on the pitch: the unchallenged
 * header of the heading technique plan (Notes and plans/
 * match_engine_heading_technique_plan.md, step 2).
 */
class HeaderTargetDrill final : public Drill
{
  [[nodiscard]] std::string aimDescription() const
  {
    if (aim() == ForcedHeaderAim::SPOT)
    {
      return std::format(" {:.0f} m at {:+.0f} deg", length, direction);
    }
    if (aim() == ForcedHeaderAim::GOAL)
    {
      return std::format(" from {:.0f} m, aim {:+.1f} m across, {:.1f} m up",
                         outFromGoal, aimAcross, aimHeight);
    }
    return {};
  }

  static constexpr float MAX_FLIGHT_PREDICTION_SECONDS = 20.0F;
  static constexpr float MAX_SEARCH_LIFT_METRES_PER_SECOND = 40.0F;
  static constexpr int LIFT_SEARCH_PASSES = 40;
  static constexpr float BALL_SPEED_MIN = 2.0F;
  static constexpr float BALL_SPEED_MAX = 35.0F;
  static constexpr float FROM_ANGLE_MIN = -180.0F;
  static constexpr float FROM_ANGLE_MAX = 180.0F;
  static constexpr float FEED_DISTANCE_MIN = 3.0F;
  static constexpr float FEED_DISTANCE_MAX = 60.0F;
  static constexpr float ARRIVAL_HEIGHT_MIN = 0.5F;
  static constexpr float ARRIVAL_HEIGHT_MAX = 3.5F;
  static constexpr float PASSES_BY_MIN = -1.5F;
  static constexpr float PASSES_BY_MAX = 1.5F;
  static constexpr float AIM_CHOICE_MAX = 2.0F;
  static constexpr float LENGTH_MIN = 2.0F;
  static constexpr float LENGTH_MAX = 40.0F;
  static constexpr float DIRECTION_MIN = -180.0F;
  static constexpr float DIRECTION_MAX = 180.0F;
  static constexpr float OUT_FROM_GOAL_MIN = 3.0F;
  static constexpr float OUT_FROM_GOAL_MAX = 25.0F;
  static constexpr float OFF_CENTRE_MIN = -15.0F;
  static constexpr float OFF_CENTRE_MAX = 15.0F;
  static constexpr float AIM_ACROSS_MIN = -3.6F;
  static constexpr float AIM_ACROSS_MAX = 3.6F;
  static constexpr float AIM_HEIGHT_MAX = 2.4F;
  static constexpr float RUN_IN_MAX = 20.0F;
  static constexpr float RUN_DELAY_MAX = 4.0F;
  static constexpr float HEADING_MAX = 100.0F;
  static constexpr float HEIGHT_CM_MIN = 150.0F;
  static constexpr float HEIGHT_CM_MAX = 210.0F;
  static constexpr float PHYSICALITY_MAX = 100.0F;
  static constexpr float PACE_MAX = 100.0F;
  static constexpr float SHOOTING_MAX = 100.0F;
  static constexpr float DEFAULT_BALL_SPEED = 6.0F;
  static constexpr float DEFAULT_FEED_DISTANCE = 12.0F;
  static constexpr float DEFAULT_ARRIVAL_HEIGHT = 1.9F;
  static constexpr float DEFAULT_LENGTH = 10.0F;
  static constexpr float DEFAULT_OUT_FROM_GOAL = 10.0F;
  static constexpr float DEFAULT_AIM_HEIGHT = 0.5F;
  static constexpr float DEFAULT_HEADING = 70.0F;
  static constexpr float DEFAULT_HEIGHT_CM = 180.0F;
  static constexpr float DEFAULT_PHYSICALITY = 70.0F;
  static constexpr float DEFAULT_PACE = 70.0F;
  static constexpr float DEFAULT_SHOOTING = 70.0F;
  static constexpr ImU32 REFERENCE_LINE_COLOUR = IM_COL32(255, 255, 255, 70);
  static constexpr ImU32 FEED_MARKER_COLOUR = IM_COL32(255, 255, 255, 200);
  static constexpr ImU32 TARGET_COLOUR = IM_COL32(90, 220, 120, 220);
  static constexpr ImU32 HEADER_TARGET_FILL_COLOUR = IM_COL32(90, 170, 255, 60);
  static constexpr ImU32 HEADER_TARGET_OUTLINE_COLOUR =
      IM_COL32(90, 170, 255, 200);
  static constexpr ImU32 LANDING_COLOUR = IM_COL32(255, 230, 120, 255);
  static constexpr ImU32 CONTROLLED_BALL_COLOUR = IM_COL32(90, 170, 255, 255);
  static constexpr ImU32 BALL_TRAIL_COLOUR = IM_COL32(255, 230, 120, 160);
  static constexpr float HALF_TURN_DEGREES = 180.0F;
  static constexpr float PERCENT_SCALE = 100.0F;

 public:
  /** Where he stands for a header to a spot or a cushion (pitch). */
  static inline const PitchPoint MIDDLE =
      at(PITCH_MIDPOINT_SHARE, PITCH_MIDPOINT_SHARE);
  /** No header this long after the feed: the run ends. */
  static constexpr double TIME_LIMIT = 8.0;
  /** After the header: the run ends this long after it at most. */
  static constexpr double FLIGHT_LIMIT = 6.0;
  static constexpr double LINGER_SECONDS = 0.4;
  static constexpr float GOAL_HALF_WIDTH =
      MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES;
  static constexpr float BALL_RADIUS = MatchTuning::Units::BALL_RADIUS_METRES;
  static constexpr float CROSSBAR = MatchTuning::Units::CROSSBAR_HEIGHT_METRES;

  [[nodiscard]] const char* name() const override { return "Header target"; }
  [[nodiscard]] const char* summary() const override
  {
    return "A high ball served to a lone player, who heads it on to a spot, "
           "at a spot in the goal, or cushions it and keeps it. Where it "
           "comes from, its pace and the height it arrives at; his Heading, "
           "height, jump and the header knobs. Measures whether he got his "
           "head to it, where he met it (height, gap from his centre), the "
           "pace in and off his head, each job's difficulty and error, and "
           "where the header went against the target.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    const auto add = [&list](DrillParameter parameter, const char* section)
    {
      parameter.section = section;
      list.push_back(std::move(parameter));
    };
    add(setting("Ball speed (m/s)", &ballSpeed,
                {.min = BALL_SPEED_MIN, .max = BALL_SPEED_MAX}, false,
                "Its pace over the ground when it is played in. Slow over a "
                "long way drops steeply onto him; fast is a driven ball."),
        "Ball");
    add(setting("Comes from (deg)", &fromAngle,
                {.min = FROM_ANGLE_MIN, .max = FROM_ANGLE_MAX}, true,
                "Where it is played from, against the way he faces (up the "
                "pitch): 0 in front of him, 90 from his right, -90 from his "
                "left, 180 from behind."),
        "Ball");
    add(setting("Feed distance (m)", &feedDistance,
                {.min = FEED_DISTANCE_MIN, .max = FEED_DISTANCE_MAX}, false),
        "Ball");
    add(setting("Arrival height (m)", &arrivalHeight,
                {.min = ARRIVAL_HEIGHT_MIN, .max = ARRIVAL_HEIGHT_MAX}, false,
                "How high it is as it passes over the spot where he stands."),
        "Ball");
    add(setting("Passes him by (m)", &passesBy,
                {.min = PASSES_BY_MIN, .max = PASSES_BY_MAX}, false,
                "Sideways offset of its line from where he stands: 0 "
                "straight at him."),
        "Ball");

    DrillParameter aim = setting("Header", &aimChoice,
                                 {.min = 0.0F, .max = AIM_CHOICE_MAX}, true,
                                 "To a spot: headed on to a spot on the "
                                 "ground.\nAt goal: a headed shot at a spot "
                                 "in an empty goal.\nKeep it: cushioned "
                                 "down and kept.");
    aim.choices = {"To a spot", "At goal", "Keep it"};
    add(aim, "Header");
    DrillParameter early =
        setting("Knows it early", &knownEarly, {.min = 0.0F, .max = 1.0F}, true,
                "Yes: he knows the header before the ball "
                "comes and turns his body to it. No: he "
                "decides only as he meets it.");
    early.choices = {"No", "Yes"};
    add(early, "Header");
    add(setting("Length (m)", &length, {.min = LENGTH_MIN, .max = LENGTH_MAX},
                false, "To a spot: from where he stands to the spot."),
        "Header");
    add(setting("Direction (deg)", &direction,
                {.min = DIRECTION_MIN, .max = DIRECTION_MAX}, true,
                "To a spot: where it is against the way he faces (up the "
                "pitch): 0 straight ahead, 90 to his right, -90 to his left, "
                "180 behind him."),
        "Header");
    add(setting("Out from goal line (m)", &outFromGoal,
                {.min = OUT_FROM_GOAL_MIN, .max = OUT_FROM_GOAL_MAX}, false,
                "At goal: where he stands, facing the goal."),
        "Header");
    add(setting("Off centre (m)", &offCentre,
                {.min = OFF_CENTRE_MIN, .max = OFF_CENTRE_MAX}, false,
                "At goal: + to his right of the middle of the goal."),
        "Header");
    add(setting("Aim across (m)", &aimAcross,
                {.min = AIM_ACROSS_MIN, .max = AIM_ACROSS_MAX}, false,
                "At goal: the spot, + to his right of the middle."),
        "Header");
    add(setting("Aim height (m)", &aimHeight,
                {.min = 0.0F, .max = AIM_HEIGHT_MAX}, false,
                "At goal: the spot's height."),
        "Header");

    add(setting("Runs in from (m)", &runIn, {.min = 0.0F, .max = RUN_IN_MAX},
                false,
                "0: he stands on his spot. Otherwise he starts this far "
                "behind it and runs up the pitch through it, flat out."),
        "Player");
    add(setting("Run starts after (s)", &runDelay,
                {.min = 0.0F, .max = RUN_DELAY_MAX}, false,
                "Running in: how long after the ball is played he sets off."),
        "Player");
    add(setting("Heading", &heading, {.min = 1.0F, .max = HEADING_MAX}, true,
                "His Heading (overrides the derived stat): his header power, "
                "and how accurately he places it."),
        "Player");
    add(setting("Height (cm)", &heightCm,
                {.min = HEIGHT_CM_MIN, .max = HEIGHT_CM_MAX}, true,
                "Part of his Jumping reach (his height plus his jump)."),
        "Player");
    add(setting("Physicality", &physicality,
                {.min = 1.0F, .max = PHYSICALITY_MAX}, true,
                "With Pace, his jump (Jumping reach)."),
        "Player");
    add(setting("Pace", &pace, {.min = 1.0F, .max = PACE_MAX}, true,
                "With Physicality, his jump (Jumping reach)."),
        "Player");
    add(setting("Shooting", &shooting, {.min = 1.0F, .max = SHOOTING_MAX}, true,
                "With Defending, the stand-in for Heading when the Heading "
                "setting isn't used. The header doesn't read it."),
        "Player");

    // Names live as long as the drill: the settings point at them.
    const auto kindName =
        STRIKE_KIND_NAMES.at(static_cast<std::size_t>(StrikeKind::HEADER));
    for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
    {
      knobNames.at(index) =
          strikeKnobName(StrikeKind::HEADER, static_cast<StrikeKnob>(index));
      knobSections.at(index) = std::string(kindName) + " knobs: " +
                               std::string(STRIKE_KNOBS.at(index).group);
    }
    for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
    {
      const StrikeKnobInfo& info = STRIKE_KNOBS.at(index);
      DrillParameter knob =
          setting(knobNames.at(index).c_str(),
                  &knobs.at(StrikeKind::HEADER, static_cast<StrikeKnob>(index)),
                  {.min = info.min, .max = info.max}, false);
      knob.collapsible = true;
      knob.label = info.name.data();
      add(knob, knobSections.at(index).c_str());
    }
    return list;
  }

  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    constexpr float PRESET_BASE_BALL_SPEED_M_S = 6.0F;
    constexpr float PRESET_BASE_FEED_DISTANCE_M = 12.0F;
    constexpr float PRESET_BASE_ARRIVAL_HEIGHT_M = 1.9F;
    constexpr float PRESET_BASE_LENGTH_M = 10.0F;
    constexpr float PRESET_LONG_BALL_HEADED_BACK_BALL_SPEED_M_S = 18.0F;
    constexpr float PRESET_LONG_BALL_HEADED_BACK_FEED_DISTANCE_M = 35.0F;
    constexpr float PRESET_LONG_BALL_HEADED_BACK_ARRIVAL_HEIGHT_M = 2.0F;
    constexpr float PRESET_LONG_BALL_HEADED_BACK_LENGTH_M = 20.0F;
    constexpr float PRESET_CROSS_TURNED_90_DEG_BALL_SPEED_M_S = 18.0F;
    constexpr float PRESET_CROSS_TURNED_90_DEG_COMES_FROM_DEG = 90.0F;
    constexpr float PRESET_CROSS_TURNED_90_DEG_FEED_DISTANCE_M = 25.0F;
    constexpr float PRESET_CROSS_TURNED_90_DEG_ARRIVAL_HEIGHT_M = 2.0F;
    constexpr float PRESET_CROSS_TURNED_90_DEG_LENGTH_M = 12.0F;
    constexpr float PRESET_GLANCED_ON_BALL_SPEED_M_S = 16.0F;
    constexpr float PRESET_GLANCED_ON_COMES_FROM_DEG = 180.0F;
    constexpr float PRESET_GLANCED_ON_FEED_DISTANCE_M = 25.0F;
    constexpr float PRESET_GLANCED_ON_ARRIVAL_HEIGHT_M = 2.1F;
    constexpr float PRESET_GLANCED_ON_LENGTH_M = 15.0F;
    constexpr float PRESET_CROSS_HEADED_AT_GOAL_BALL_SPEED_M_S = 18.0F;
    constexpr float PRESET_CROSS_HEADED_AT_GOAL_COMES_FROM_DEG = 90.0F;
    constexpr float PRESET_CROSS_HEADED_AT_GOAL_FEED_DISTANCE_M = 25.0F;
    constexpr float PRESET_CROSS_HEADED_AT_GOAL_ARRIVAL_HEIGHT_M = 2.0F;
    constexpr float PRESET_CROSS_HEADED_AT_GOAL_OUT_FROM_GOAL_LINE_M = 10.0F;
    constexpr float PRESET_CROSS_HEADED_AT_GOAL_AIM_ACROSS_M = -2.5F;
    constexpr float PRESET_CROSS_HEADED_AT_GOAL_AIM_HEIGHT_M = 0.5F;
    constexpr float PRESET_CUSHION_A_DROPPING_BALL_HEADER = 2.0F;

    const std::map<std::string, float> base{
        {"Ball speed (m/s)", PRESET_BASE_BALL_SPEED_M_S},
        {"Comes from (deg)", 0.0F},
        {"Feed distance (m)", PRESET_BASE_FEED_DISTANCE_M},
        {"Arrival height (m)", PRESET_BASE_ARRIVAL_HEIGHT_M},
        {"Passes him by (m)", 0.0F},
        {"Header", 0.0F},
        {"Length (m)", PRESET_BASE_LENGTH_M},
        {"Direction (deg)", 0.0F}};
    const auto with = [&base](const std::map<std::string, float>& changes)
    {
      std::map<std::string, float> settings = base;
      for (const auto& [name, value] : changes)
      {
        settings[name] = value;
      }
      return settings;
    };
    return {
        {.name = "Dropping ball, headed ahead",
         .summary = "A ball dropping steeply onto him, headed 10 m ahead.",
         .settings = base},
        {.name = "Long ball headed back",
         .summary =
             "18 m/s from 35 m in front, headed back 20 m the way it came.",
         .settings = with(
             {{"Ball speed (m/s)", PRESET_LONG_BALL_HEADED_BACK_BALL_SPEED_M_S},
              {"Feed distance (m)",
               PRESET_LONG_BALL_HEADED_BACK_FEED_DISTANCE_M},
              {"Arrival height (m)",
               PRESET_LONG_BALL_HEADED_BACK_ARRIVAL_HEIGHT_M},
              {"Length (m)", PRESET_LONG_BALL_HEADED_BACK_LENGTH_M}})},
        {.name = "Cross turned 90 deg",
         .summary =
             "18 m/s from 25 m to his right, headed 12 m straight ahead.",
         .settings = with(
             {{"Ball speed (m/s)", PRESET_CROSS_TURNED_90_DEG_BALL_SPEED_M_S},
              {"Comes from (deg)", PRESET_CROSS_TURNED_90_DEG_COMES_FROM_DEG},
              {"Feed distance (m)", PRESET_CROSS_TURNED_90_DEG_FEED_DISTANCE_M},
              {"Arrival height (m)",
               PRESET_CROSS_TURNED_90_DEG_ARRIVAL_HEIGHT_M},
              {"Length (m)", PRESET_CROSS_TURNED_90_DEG_LENGTH_M}})},
        {.name = "Glanced on",
         .summary = "16 m/s from 25 m behind him, glanced on 15 m.",
         .settings =
             with({{"Ball speed (m/s)", PRESET_GLANCED_ON_BALL_SPEED_M_S},
                   {"Comes from (deg)", PRESET_GLANCED_ON_COMES_FROM_DEG},
                   {"Feed distance (m)", PRESET_GLANCED_ON_FEED_DISTANCE_M},
                   {"Arrival height (m)", PRESET_GLANCED_ON_ARRIVAL_HEIGHT_M},
                   {"Length (m)", PRESET_GLANCED_ON_LENGTH_M}})},
        {.name = "Cross headed at goal",
         .summary =
             "18 m/s from 25 m to his right, 10 m out, at the far side low.",
         .settings = with(
             {{"Ball speed (m/s)", PRESET_CROSS_HEADED_AT_GOAL_BALL_SPEED_M_S},
              {"Comes from (deg)", PRESET_CROSS_HEADED_AT_GOAL_COMES_FROM_DEG},
              {"Feed distance (m)",
               PRESET_CROSS_HEADED_AT_GOAL_FEED_DISTANCE_M},
              {"Arrival height (m)",
               PRESET_CROSS_HEADED_AT_GOAL_ARRIVAL_HEIGHT_M},
              {"Header", 1.0F},
              {"Out from goal line (m)",
               PRESET_CROSS_HEADED_AT_GOAL_OUT_FROM_GOAL_LINE_M},
              {"Off centre (m)", 0.0F},
              {"Aim across (m)", PRESET_CROSS_HEADED_AT_GOAL_AIM_ACROSS_M},
              {"Aim height (m)", PRESET_CROSS_HEADED_AT_GOAL_AIM_HEIGHT_M}})},
        {.name = "Cushion a dropping ball",
         .summary = "A ball dropping onto him, kept.",
         .settings = with({{"Header", PRESET_CUSHION_A_DROPPING_BALL_HEADER}})},
    };
  }

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    return {{.name = "Header",
             .role = PlayerRole::ST,
             .homeTeam = true,
             .stats = {{"Physicality", physicality},
                       {"Pace", pace},
                       {"Shooting", shooting}},
             .height = static_cast<int>(heightCm),
             .derived = {{DerivedStat::HEADING, heading}}}};
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    playerId = ids[0];
    engine.setStrikeKnobs(knobs);

    // Geometry in metres: he faces up the pitch (+x), towards the goal for
    // a header at it; the ball comes from fromAngle round from in front of
    // him, along a line passesBy to the side of where he stands.
    const Vector2F up{.x = 1.0F, .y = 0.0F};
    stand = aim() == ForcedHeaderAim::GOAL
                ? at(1.0F - (outFromGoal / LENGTH),
                     PITCH_MIDPOINT_SHARE + (offCentre / WIDTH))
                : MIDDLE;
    from = rotated(up, fromAngle * PI / HALF_TURN_DEGREES);
    travel = Vector2F{.x = -from.x, .y = -from.y};
    side = rotated(travel, PI * QUARTER_TURN_PI_SHARE);
    feedFrom = along(along(stand, side, passesBy), from, feedDistance);
    feedFrom =
        PitchPoint{std::clamp(feedFrom.x, PITCH.atX(PITCH_EDGE_MIN_SHARE),
                              PITCH.atX(PITCH_EDGE_MAX_SHARE)),
                   std::clamp(feedFrom.y, PITCH.atY(PITCH_EDGE_MIN_SHARE),
                              PITCH.atY(PITCH_EDGE_MAX_SHARE))};
    target =
        along(stand, rotated(up, direction * PI / HALF_TURN_DEGREES), length);
    target = PitchPoint{std::clamp(target.x, PITCH.atX(PITCH_SAFE_MIN_SHARE),
                                   PITCH.atX(PITCH_SAFE_MAX_SHARE)),
                        std::clamp(target.y, PITCH.atY(PITCH_SAFE_MIN_SHARE),
                                   PITCH.atY(PITCH_SAFE_MAX_SHARE))};

    // Running in: from behind his spot, through it.
    start = along(stand, up, -runIn);
    start = PitchPoint{std::clamp(start.x, PITCH.atX(PITCH_SAFE_MIN_SHARE),
                                  PITCH.atX(PITCH_SAFE_MAX_SHARE)),
                       start.y};
    running = false;
    MatchScenario scenario;
    scenario.players.push_back(
        {.playerId = playerId, .position = start, .makingRun = false});
    scenario.carrierId = 0;
    scenario.ballPosition = feedFrom;
    engine.startDrill(scenario);
    // He holds his spot (or his start), facing up the pitch.
    engine.setDrillTarget(playerId, start, false, {}, 0.0F, 0.0F,
                          facingAngleOf(0.0F));
    if (aim() == ForcedHeaderAim::KEEP)
    {
      engine.keepBall(playerId, true);
    }
    engine.forceHeader(
        playerId, aim(),
        aim() == ForcedHeaderAim::SPOT ? std::optional<PitchPoint>(target)
                                       : std::nullopt,
        aim() == ForcedHeaderAim::GOAL
            ? std::optional<Vector2F>(Vector2F{.x = aimAcross, .y = aimHeight})
            : std::nullopt,
        knownEarly >= CHOICE_MIDPOINT);
    const float feedMetres =
        metresBetween(feedFrom, along(stand, side, passesBy));
    const float lift = liftFor(engine, {.distanceMetres = feedMetres,
                                        .speedMetresPerSecond = ballSpeed,
                                        .heightMetres = arrivalHeight});
    engine.feedBall(feedFrom,
                    {.x = travel.x * ballSpeed, .y = travel.y * ballSpeed},
                    lift, playerId);
    fedAt = secondsOf(engine);
    before = engine.getStrikeContacts();
    if (const MatchPlayer* player = slotOf(engine, playerId))
    {
      jumpingReach =
          derivedPhysicalValue(DerivedStat::JUMPING_REACH,
                               player->derivedStat(DerivedStat::JUMPING_REACH));
      headingRating = player->derivedStat(DerivedStat::HEADING);
    }
    header.reset();
    tookOff.reset();
    tookOffRunning = false;
    dropped.reset();
    droppedAt = 0.0;
    collectedAt.reset();
    cushioned = false;
    touched = false;
    flight = PassFlight{};
    outcome.clear();
    endAt.reset();
    last = stateOf(engine.getBall());
    lastPlayer = stand;
    peakAfter = 0.0F;
    ballPath = {feedFrom};
  }

 private:
  void observeContact(const MatchEngine& engine, double now, bool his)
  {
    const MatchBall& ball = engine.getBall();
    readHeader(engine, now);
    if (header)
    {
      if (aim() == ForcedHeaderAim::SPOT)
      {
        flight.start(ball, target);
      }
    }
    else if (his)
    {
      // On his feet without a header: cushioned down (today's cushion has
      // no contact, but it was met with his head), or a touch (the ball
      // came to him below his head's reach).
      if (engine.getLastHeadPart())
      {
        cushioned = true;
      }
      else
      {
        touched = true;
      }
      meet = last;
      meetPlayer = lastPlayer;
      outcome = cushioned ? "cushioned and kept"
                          : "controlled (a touch, not a header)";
    }
    else if (now >= fedAt + TIME_LIMIT ||
             (last.height > LANDING_HEIGHT_EPSILON_METRES && ball.z <= 0.0F &&
              ball.velocityZ <= 0.0F &&
              metresBetween(ball.position, stand) >
                  MISSED_LANDING_DISTANCE_METRES))
    {
      outcome = "not headed (out of reach)";
    }
  }

  void observeFlight(const MatchEngine& engine, double now, bool his)
  {
    if (!header)
    {
      return;
    }
    const MatchBall& ball = engine.getBall();
    peakAfter = std::max(peakAfter, ball.z);
    if (aim() == ForcedHeaderAim::SPOT)
    {
      flight.observe(ball);
      if (flight.settled(StrikeKind::HEADER, ball))
      {
        outcome = "came down";
      }
    }
    else if (aim() == ForcedHeaderAim::GOAL)
    {
      outcome = header->shot ? goalResult(*header) : "not a shot";
    }
    else
    {
      // A cushioned header: where it first comes down, then kept once he
      // has collected it (he owns it from the header), or not.
      if (!dropped && ball.z <= 0.0F)
      {
        dropped = ball.position;
        droppedAt = now;
      }
      if (his && (!engine.hasTouchClaim() || engine.isTouchCollected()))
      {
        outcome = "cushioned and kept";
        collectedAt = now;
      }
      else if (!his)
      {
        outcome = "cushioned, not kept";
      }
    }
    if (outcome.empty() && engine.getState() != MatchState::PLAYING)
    {
      outcome = "out of play";
    }
    if (outcome.empty() && now >= header->at + FLIGHT_LIMIT)
    {
      outcome = "time up";
    }
  }

 public:
  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    const MatchPlayer* player = slotOf(engine, playerId);
    if (engine.getSimulatedSteps() % 2 == 0)
    {
      ballPath.push_back(ball.position);
    }
    const bool his = (engine.getPossessionAttachment() != nullptr) &&
                     engine.getPossessionAttachment()->getId() == playerId;
    if (runIn > 0.0F && !running && now >= fedAt + runDelay)
    {
      // Off he goes, flat out up the pitch through his spot.
      running = true;
      const PitchPoint beyond = along(stand, {.x = 1.0F, .y = 0.0F}, 15.0F);
      engine.setDrillTarget(
          playerId,
          PitchPoint{std::min(beyond.x, PITCH.atX(PITCH_SAFE_MAX_SHARE)),
                     beyond.y},
          true, {}, 0.0F, 0.0F, facingAngleOf(0.0F), true);
    }
    if ((player != nullptr) && player->airborne() && !tookOff)
    {
      tookOff = now;
      tookOffRunning = player->runningJump;
    }

    if (!header && !cushioned && !touched && outcome.empty())
    {
      observeContact(engine, now, his);
    }
    else if (header && outcome.empty())
    {
      observeFlight(engine, now, his);
    }
    last = stateOf(ball);
    if (player != nullptr)
    {
      lastPlayer = player->position;
    }
    if (!outcome.empty() && !endAt)
    {
      endAt = now;
    }
    return (!endAt || now < *endAt + LINGER_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::line(renderer, draw, feedFrom, stand, REFERENCE_LINE_COLOUR,
                    1.0F * scale);
    DrillDraw::marker(renderer, draw, feedFrom,
                      PRIMARY_MARKER_RADIUS_PIXELS * scale, FEED_MARKER_COLOUR,
                      "feed");
    DrillDraw::marker(renderer, draw, stand,
                      SECONDARY_MARKER_RADIUS_PIXELS * scale, TARGET_COLOUR,
                      "header");
    if (aim() == ForcedHeaderAim::SPOT)
    {
      DrillDraw::line(renderer, draw, stand, target, REFERENCE_LINE_COLOUR,
                      1.0F * scale);
      DrillDraw::circle(renderer, draw, target,
                        passTolerance(metresBetween(stand, target)),
                        HEADER_TARGET_FILL_COLOUR, HEADER_TARGET_OUTLINE_COLOUR,
                        1.0F * scale);
      if (flight.landingPoint())
      {
        DrillDraw::marker(
            renderer, draw, *flight.landingPoint(),
            PRIMARY_MARKER_RADIUS_PIXELS * scale, LANDING_COLOUR,
            std::format("{:.1f} m", flight.landingError()).c_str());
      }
    }
    else if (aim() == ForcedHeaderAim::GOAL)
    {
      DrillDraw::marker(
          renderer, draw, at(1.0F, PITCH_MIDPOINT_SHARE + (aimAcross / WIDTH)),
          PRIMARY_MARKER_RADIUS_PIXELS * scale, CONTROLLED_BALL_COLOUR, "aim");
      if (header && header->shot)
      {
        DrillDraw::marker(
            renderer, draw,
            at(1.0F, PITCH_MIDPOINT_SHARE + (header->crossingY / WIDTH)),
            PRIMARY_MARKER_RADIUS_PIXELS * scale, LANDING_COLOUR,
            outcome.empty() ? nullptr : outcome.c_str());
      }
    }
    DrillDraw::path(renderer, draw, ballPath, BALL_TRAIL_COLOUR,
                    BALL_TRAIL_WIDTH_PIXELS * scale);
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    static constexpr std::array<const char*, 3> AIMS = {"to a spot", "at goal",
                                                        "kept"};
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.0f} m/s from {:+.0f} deg, {:.0f} m out, over him at {:.2f} m{} · "
        "headed {}{}",
        ballSpeed, fromAngle, feedDistance, arrivalHeight,
        passesBy != 0.0F ? std::format(", {:+.1f} m by", passesBy)
                         : std::string(),
        AIMS.at(static_cast<std::size_t>(aimChoice)), aimDescription()));
    lines.push_back(std::format(
        "Player: Heading {:.0f}, {:.0f} cm, Physicality {:.0f}, Pace {:.0f}, "
        "Shooting {:.0f} · Jumping reach {:.2f} m",
        heading, heightCm, physicality, pace, shooting, jumpingReach));
    if (header || cushioned || touched)
    {
      const State& at = header ? header->meet : meet;
      const PitchPoint who = header ? header->meetPlayer : meetPlayer;
      lines.push_back(std::format(
          "Met at {:.2f} m up ({:+.2f} m against his standing head, "
          "{:+.2f} m against his Jumping reach), {:.2f} m from his centre, "
          "coming in at {:.1f} m/s",
          at.height, at.height - (heightCm / 100.0F), at.height - jumpingReach,
          metresBetween(who, at.position), paceOf(at)));
    }
    if (header)
    {
      constexpr float DEGREES = HALF_TURN_DEGREES / PI;
      lines.push_back(std::format(
          "{} ({}) at {:.2f} s: off at {:.1f} m/s ({:.1f} along, {:+.1f} up, "
          "{:+.0f} deg), turned {:.0f} deg",
          header->shot ? "Headed shot" : "Header",
          header->topOfHead ? "top of the head" : "forehead",
          header->at - fedAt, std::hypot(header->speed, header->lift),
          header->speed, header->lift,
          std::atan2(header->lift, header->speed) * DEGREES, header->turned));
      lines.push_back(std::format(
          "Difficulty angle {:.2f}, weight {:.2f} · angle {}{:.1f} deg "
          "(elevation {:+.1f} deg), weight {}{:+.2f} m/s",
          header->angleDifficulty, header->weightDifficulty,
          header->angleFailed ? "FAILED " : "", header->angleError * DEGREES,
          header->elevationError * DEGREES,
          header->weightFailed ? "FAILED " : "", header->weightError));
      if (aim() == ForcedHeaderAim::SPOT && flight.landingPoint())
      {
        const Vector2F offset = lineOffset(*flight.landingPoint());
        lines.push_back(std::format(
            "Came down {:.2f} m from the spot ({:+.2f} m long, {:+.2f} m "
            "right; within {:.1f} m counts)",
            flight.landingError(), offset.x, offset.y,
            passTolerance(flight.targetDistance())));
      }
      if (aim() == ForcedHeaderAim::GOAL && header->shot)
      {
        lines.push_back(std::format(
            "Crossed the line {:+.2f} m across, {:.2f} m up (missed the spot "
            "by {:+.2f} across, {:+.2f} up)",
            header->crossingY, header->crossingZ, header->crossingY - aimAcross,
            header->crossingZ - aimHeight));
      }
    }
    if (!outcome.empty())
    {
      lines.push_back("OUTCOME  " + outcome);
    }
    return lines;
  }

  /** How hard the last header was, below the settings. */
  void renderAfterSettings() const override
  {
    ImGui::SeparatorText("Difficulty of the last header");
    if (!header)
    {
      SandboxText::disabledText("No header yet.");
      return;
    }
    if (!ImGui::BeginTable("##difficulty", 2,
                           ImGuiTableFlags_SizingStretchProp))
    {
      return;
    }
    const auto row = [](const char* name, float value, bool strong)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (strong)
      {
        ImGui::TextUnformatted(name);
      }
      else
      {
        SandboxText::disabledText(name);
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(
          std::format("{:.3f}", static_cast<double>(value)).c_str());
    };
    row("Angle difficulty", header->angleDifficulty, true);
    row("Weight difficulty", header->weightDifficulty, true);
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
    {
      if (header->factors.at(index) > MIN_REPORTED_CONTACT_FACTOR)
      {
        row(std::string(CONTACT_FACTOR_LABELS.at(index)).c_str(),
            header->factors.at(index), false);
      }
    }
    ImGui::EndTable();
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty())
    {
      return {};
    }
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{
        {.name = "Headed", .value = flag(header.has_value() || cushioned)},
        {.name = "Jumping reach (m)", .value = jumpingReach},
        {.name = "Heading rating", .value = headingRating * PERCENT_SCALE}};
    if (aim() == ForcedHeaderAim::KEEP)
    {
      out.push_back(
          {.name = "Kept", .value = flag(outcome == "cushioned and kept")});
    }
    if (!header && !cushioned)
    {
      return out;
    }
    const State& at = header ? header->meet : meet;
    const PitchPoint who = header ? header->meetPlayer : meetPlayer;
    out.push_back({.name = "Meet height (m)", .value = at.height});
    out.push_back({.name = "Meet over standing head (m)",
                   .value = at.height - (heightCm / 100.0F)});
    out.push_back({.name = "Meet under Jumping reach (m)",
                   .value = jumpingReach - at.height});
    out.push_back(
        {.name = "Meet gap (m)", .value = metresBetween(who, at.position)});
    out.push_back({.name = "Pace in (m/s)", .value = paceOf(at)});
    if (!header)
    {
      return out;
    }
    constexpr float DEGREES = HALF_TURN_DEGREES / PI;
    out.push_back({.name = "Top of head", .value = flag(header->topOfHead)});
    out.push_back(
        {.name = "Body off its line (deg)", .value = header->bodyOff});
    out.push_back({.name = "Jumped", .value = flag(tookOff.has_value())});
    out.push_back(
        {.name = "Running jump", .value = flag(tookOff && tookOffRunning)});
    out.push_back({.name = "Feet at meet (m)", .value = header->feet});
    out.push_back({.name = "Speed at meet (m/s)", .value = header->runSpeed});
    if (tookOff)
    {
      out.push_back(
          {.name = "Take-off before meet (s)", .value = header->at - *tookOff});
    }
    out.push_back({.name = "Feed to header (s)", .value = header->at - fedAt});
    out.push_back({.name = "Pace off (m/s)",
                   .value = std::hypot(header->speed, header->lift)});
    out.push_back({.name = "Launch speed (m/s)", .value = header->speed});
    out.push_back({.name = "Launch lift (m/s)", .value = header->lift});
    out.push_back({.name = "Elevation off (deg)",
                   .value = std::atan2(header->lift, header->speed) * DEGREES});
    out.push_back({.name = "Turned (deg)", .value = header->turned});
    out.push_back({.name = "Peak after (m)", .value = peakAfter});
    out.push_back(
        {.name = "Angle difficulty", .value = header->angleDifficulty});
    out.push_back(
        {.name = "Weight difficulty", .value = header->weightDifficulty});
    out.push_back({.name = "Angle failed", .value = flag(header->angleFailed)});
    out.push_back(
        {.name = "Weight failed", .value = flag(header->weightFailed)});
    out.push_back({.name = "Abs angle error (deg)",
                   .value = header->angleError * DEGREES});
    out.push_back({.name = "Weight error (m/s)", .value = header->weightError});
    out.push_back({.name = "Elevation error (deg, + higher)",
                   .value = header->elevationError * DEGREES});
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
    {
      out.push_back(
          {.name = "Condition: " + std::string(CONTACT_FACTOR_LABELS.at(index)),
           .value = header->factors.at(index)});
    }
    if (aim() == ForcedHeaderAim::SPOT)
    {
      flight.addMetrics(StrikeKind::HEADER, out);
      if (flight.landingPoint())
      {
        const Vector2F offset = lineOffset(*flight.landingPoint());
        out.push_back({.name = "Long (m, - short)", .value = offset.x});
        out.push_back({.name = "Right (m, - left)", .value = offset.y});
      }
    }
    if (aim() == ForcedHeaderAim::KEEP && dropped)
    {
      out.push_back({.name = "Drops from him (m)",
                     .value = metresBetween(header->meetPlayer, *dropped)});
      out.push_back(
          {.name = "Drops after (s)", .value = droppedAt - header->at});
    }
    if (aim() == ForcedHeaderAim::KEEP && collectedAt)
    {
      out.push_back(
          {.name = "Collected after (s)", .value = *collectedAt - header->at});
    }
    if (aim() == ForcedHeaderAim::GOAL && header->shot)
    {
      const std::string where = goalResult(*header);
      out.push_back({.name = "On target", .value = flag(where == "on target")});
      out.push_back({.name = "Wide", .value = flag(where == "wide")});
      out.push_back({.name = "Over", .value = flag(where == "over")});
      out.push_back(
          {.name = "Miss across (m)", .value = header->crossingY - aimAcross});
      out.push_back({.name = "Miss height (m, + higher)",
                     .value = header->crossingZ - aimHeight});
    }
    return out;
  }

 private:
  /** The ball at the end of a step (pitch position, m/s, metres up). */
  struct State
  {
    PitchPoint position;
    Vector2F velocity{};
    float height = 0.0F;
    float lift = 0.0F;
  };
  static State stateOf(const MatchBall& ball)
  {
    return {.position = ball.position,
            .velocity = ball.velocity,
            .height = ball.z,
            .lift = ball.velocityZ};
  }
  static float paceOf(const State& state)
  {
    return std::hypot(std::hypot(state.velocity.x, state.velocity.y),
                      state.lift);
  }

  struct Header
  {
    double at = 0.0;
    bool shot = false;
    bool angleFailed = false;
    bool weightFailed = false;
    /** Rad (unsigned), m/s (+ harder), rad (+ higher). */
    float angleError = 0.0F;
    float weightError = 0.0F;
    float elevationError = 0.0F;
    float angleDifficulty = 0.0F;
    float weightDifficulty = 0.0F;
    std::array<float, CONTACT_FACTOR_COUNT> factors{};
    /** Off his head: along the ground and upward (m/s). */
    float speed = 0.0F;
    float lift = 0.0F;
    /** Its line seen from above, turned off the line it came on (deg). */
    float turned = 0.0F;
    /** Where he met it (where it was sent off, at its height then), its
     * pace a step before, and where he was. */
    State meet;
    PitchPoint meetPlayer;
    /** Met with the top of his head (else the forehead). */
    bool topOfHead = false;
    /** His body against the line it left his head on (deg). */
    float bodyOff = 0.0F;
    /** His feet's height (m) and his speed (m/s) as he headed it. */
    float feet = 0.0F;
    float runSpeed = 0.0F;
    /** A headed shot: where it crosses the line (m across, m up). */
    float crossingY = 0.0F;
    float crossingZ = 0.0F;
  };

  [[nodiscard]] ForcedHeaderAim aim() const
  {
    return static_cast<ForcedHeaderAim>(static_cast<int>(aimChoice));
  }

 public:
  /** The upward speed (m/s) that has a ball played along the ground at
   * `speed` (m/s) `height` metres up when it has gone `metres`, in the
   * match's own flight (bisection: more lift, higher there). Shared with the
   * Loose high ball drill. */
  static float liftFor(const MatchEngine& engine, FlightTarget target)
  {
    const float metres = target.distanceMetres;
    const float speed = target.speedMetresPerSecond;
    const float height = target.heightMetres;
    constexpr float DT =
        STEP / static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
    // Count fixed substeps so clock rounding cannot extend the prediction.
    constexpr int MAX_PREDICTION_SUBSTEPS =
        static_cast<int>(MAX_FLIGHT_PREDICTION_SECONDS / DT);
    const auto heightAt = [&](float lift)
    {
      FlightState flight{.horizontalSpeed = speed,
                         .verticalSpeed = lift,
                         .height = 0.0F,
                         .bounced = false,
                         .spin = {}};
      float gone = 0.0F;
      for (int substep = 0; substep < MAX_PREDICTION_SUBSTEPS; ++substep)
      {
        const float was = flight.height;
        const float step = engine.stepFlight(flight, DT);
        if (flight.bounced)
        {
          return -1.0F;
        }
        if (gone + step >= metres)
        {
          const float share = (metres - gone) / std::max(step, 1e-6F);
          return was + ((flight.height - was) * share);
        }
        gone += step;
        if (flight.horizontalSpeed <= 0.0F)
        {
          return -1.0F;
        }
      }
      return -1.0F;
    };
    float low = 0.0F;
    float high = MAX_SEARCH_LIFT_METRES_PER_SECOND;
    for (int round = 0; round < LIFT_SEARCH_PASSES; ++round)
    {
      const float middle = MIDPOINT_WEIGHT * (low + high);
      (heightAt(middle) < height ? low : high) = middle;
    }
    return MIDPOINT_WEIGHT * (low + high);
  }

 private:
  /** The header: a header strike the contact model has read since the
   * feed. */
  void readHeader(const MatchEngine& engine, double now)
  {
    const auto& a =
        before.byKind.at(static_cast<std::size_t>(StrikeKind::HEADER));
    const auto& b = engine.getStrikeContacts().byKind.at(
        static_cast<std::size_t>(StrikeKind::HEADER));
    if (b.strikes == a.strikes)
    {
      return;
    }
    const MatchBall& ball = engine.getBall();
    Header reading;
    reading.at = now;
    reading.shot = ball.possessionState().isShot;
    reading.angleFailed = b.angleFailed > a.angleFailed;
    reading.weightFailed = b.weightFailed > a.weightFailed;
    reading.angleError = static_cast<float>(b.angleErrorSum - a.angleErrorSum);
    reading.weightError =
        static_cast<float>(b.weightErrorSum - a.weightErrorSum);
    reading.elevationError =
        static_cast<float>(b.elevationErrorSum - a.elevationErrorSum);
    reading.angleDifficulty =
        static_cast<float>(b.angleDifficulty - a.angleDifficulty);
    reading.weightDifficulty =
        static_cast<float>(b.weightDifficulty - a.weightDifficulty);
    for (std::size_t factor = 0; factor < CONTACT_FACTOR_COUNT; ++factor)
    {
      reading.factors.at(factor) =
          static_cast<float>(b.factorSum.at(factor) - a.factorSum.at(factor));
    }
    reading.speed = std::hypot(ball.velocity.x, ball.velocity.y);
    reading.lift = ball.velocityZ;
    const float incoming = std::hypot(last.velocity.x, last.velocity.y);
    if (incoming > MIN_REPORTED_CONTACT_SPEED &&
        reading.speed > MIN_REPORTED_CONTACT_SPEED)
    {
      reading.turned = turnDegrees(
          {.x = last.velocity.x / incoming, .y = last.velocity.y / incoming},
          {.x = ball.velocity.x / reading.speed,
           .y = ball.velocity.y / reading.speed});
    }
    // Where he met it: where it was sent off from, at its height then; the
    // pace it came in at a step before.
    reading.meet = last;
    reading.meet.position = ball.sentFrom;
    reading.meet.height = ball.sentHeightMetres;
    reading.meetPlayer = lastPlayer;
    reading.topOfHead = engine.getLastHeadPart() == HeadPart::TOP;
    if (const MatchPlayer* player = slotOf(engine, playerId))
    {
      reading.feet = player->feetHeight;
      reading.runSpeed = std::hypot(player->velocity.x, player->velocity.y);
    }
    if (const MatchPlayer* player = slotOf(engine, playerId);
        (player != nullptr) && reading.speed > MIN_REPORTED_CONTACT_SPEED)
    {
      const PitchPoint body{std::cos(player->facingAngle),
                            std::sin(player->facingAngle)};
      const float bodyLength = std::hypot(body.x, body.y);
      reading.bodyOff =
          turnDegrees({.x = body.x / bodyLength, .y = body.y / bodyLength},
                      {.x = ball.velocity.x / reading.speed,
                       .y = ball.velocity.y / reading.speed});
    }
    reading.crossingY =
        ball.possessionState().shotTargetY - PITCH.atY(PITCH_MIDPOINT_SHARE);
    reading.crossingZ = ball.possessionState().shotTargetHeightMetres;
    header = reading;
  }

  /** A headed shot: where it crossed the line, against the frame. */
  [[nodiscard]] static std::string goalResult(const Header& reading)
  {
    if (std::abs(reading.crossingY) >= GOAL_HALF_WIDTH - BALL_RADIUS)
    {
      return "wide";
    }
    if (reading.crossingZ >= CROSSBAR - BALL_RADIUS)
    {
      return "over";
    }
    return "on target";
  }

  /** A point against the line from where he stands to the spot (m): along
   * it beyond the spot (+ long) and across it (+ to his right). */
  [[nodiscard]] Vector2F lineOffset(PitchPoint point) const
  {
    const Vector2F way = directionBetween(stand, target);
    const Vector2F offset = point - target;
    return Vector2F{.x = (offset.x * way.x) + (offset.y * way.y),
                    .y = (offset.y * way.x) - (offset.x * way.y)};
  }

  /** The strike tolerance at this distance (the technique bench's rule). */
  [[nodiscard]] static float passTolerance(float metres)
  {
    const float share = std::clamp((metres - 10.0F) / 50.0F, 0.0F, 1.0F);
    return 1.0F + (PASS_TOLERANCE_GROWTH_METRES * share);
  }

  // Ball
  float ballSpeed = DEFAULT_BALL_SPEED;
  float fromAngle = 0.0F;
  float feedDistance = DEFAULT_FEED_DISTANCE;
  float arrivalHeight = DEFAULT_ARRIVAL_HEIGHT;
  float passesBy = 0.0F;
  // Header
  float aimChoice = 0.0F;
  float knownEarly = 1.0F;
  float runIn = 0.0F;
  float runDelay = 0.0F;
  /** Running in: where he starts, and whether he has set off. */
  PitchPoint start;
  bool running = false;
  /** When he took off, and whether with his running jump. */
  std::optional<double> tookOff;
  bool tookOffRunning = false;
  float length = DEFAULT_LENGTH;
  float direction = 0.0F;
  float outFromGoal = DEFAULT_OUT_FROM_GOAL;
  float offCentre = 0.0F;
  float aimAcross = -2.5F;
  float aimHeight = DEFAULT_AIM_HEIGHT;
  // Player
  float heading = DEFAULT_HEADING;
  float heightCm = DEFAULT_HEIGHT_CM;
  float physicality = DEFAULT_PHYSICALITY;
  float pace = DEFAULT_PACE;
  float shooting = DEFAULT_SHOOTING;
  StrikeKnobs knobs;

  PlayerID playerId = 0;
  PitchPoint stand;
  Vector2F from{.x = -1.0F, .y = 0.0F};
  Vector2F travel{.x = 1.0F, .y = 0.0F};
  Vector2F side{.x = 0.0F, .y = 1.0F};
  PitchPoint feedFrom;
  PitchPoint target;
  double fedAt = 0.0;
  float jumpingReach = 0.0F;
  float headingRating = 0.0F;
  StrikeContactStats before;
  std::optional<Header> header;
  /** A cushioned header: where and when it first came down, and when he
   * collected it. */
  std::optional<PitchPoint> dropped;
  double droppedAt = 0.0;
  std::optional<double> collectedAt;
  bool cushioned = false;
  bool touched = false;
  State meet;
  PitchPoint meetPlayer;
  State last;
  PitchPoint lastPlayer;
  float peakAfter = 0.0F;
  PassFlight flight;
  std::string outcome;
  std::optional<double> endAt;
  std::vector<PitchPoint> ballPath;
  std::array<std::string, STRIKE_KNOB_COUNT> knobNames;
  std::array<std::string, STRIKE_KNOB_COUNT> knobSections;
};

// --- Chest control, Thigh control
// ----------------------------------------------

/**
 * One ball served at chest (or thigh) height to a lone player standing on
 * his spot, who takes it on his chest (thigh) (a Binturong body) and keeps
 * it. Nobody else on the pitch
 * (Notes and plans/match_engine_chest_thigh_control_plan.md, steps 3-4).
 */
class BodyControlDrill final : public Drill
{
  [[nodiscard]] const char* contactPart(const MatchEngine& engine,
                                        bool his) const
  {
    if (!his || !engine.hasTouchClaim() || !engine.isTouchDropping())
    {
      return nullptr;
    }
    const float height = engine.getBall().touchContactHeight;
    if (height > lines.head)
    {
      return "head";
    }
    if (height > lines.chest)
    {
      return "chest";
    }
    return "thigh";
  }

  static constexpr float MAX_DROP_PREDICTION_SECONDS = 5.0F;
  static constexpr float THIGH_ARRIVAL_HEIGHT_METRES = 0.8F;
  static constexpr float CHEST_ARRIVAL_HEIGHT_METRES = 1.3F;
  static constexpr float BALL_SPEED_MIN = 2.0F;
  static constexpr float BALL_SPEED_MAX = 35.0F;
  static constexpr float FROM_ANGLE_MIN = -90.0F;
  static constexpr float FROM_ANGLE_MAX = 90.0F;
  static constexpr float FEED_DISTANCE_MIN = 3.0F;
  static constexpr float FEED_DISTANCE_MAX = 60.0F;
  static constexpr float ARRIVAL_HEIGHT_MIN = 0.3F;
  static constexpr float ARRIVAL_HEIGHT_MAX = 2.0F;
  static constexpr float PASSES_BY_MIN = -1.0F;
  static constexpr float FIRST_TOUCH_MAX = 100.0F;
  static constexpr float HEIGHT_CM_MIN = 150.0F;
  static constexpr float HEIGHT_CM_MAX = 210.0F;
  static constexpr float DEFAULT_BALL_SPEED = 8.0F;
  static constexpr float DEFAULT_FEED_DISTANCE = 20.0F;
  static constexpr float DEFAULT_ARRIVAL_HEIGHT = 1.3F;
  static constexpr float DEFAULT_FIRST_TOUCH = 60.0F;
  static constexpr float DEFAULT_HEIGHT_CM = 180.0F;
  static constexpr ImU32 REFERENCE_LINE_COLOUR = IM_COL32(255, 255, 255, 70);
  static constexpr ImU32 FEED_MARKER_COLOUR = IM_COL32(255, 255, 255, 200);
  static constexpr ImU32 TARGET_COLOUR = IM_COL32(90, 220, 120, 220);
  static constexpr ImU32 LANDING_COLOUR = IM_COL32(255, 230, 120, 255);
  static constexpr ImU32 BALL_TRAIL_COLOUR = IM_COL32(255, 230, 120, 160);
  static constexpr float HALF_TURN_DEGREES = 180.0F;

 public:
  static inline const PitchPoint MIDDLE =
      at(PITCH_MIDPOINT_SHARE, PITCH_MIDPOINT_SHARE);
  /** No touch this long after the feed: the run ends. */
  static constexpr double TIME_LIMIT = 8.0;
  /** After the touch: the run ends this long after it at most. */
  static constexpr double KEEP_LIMIT = 5.0;
  static constexpr double LINGER_SECONDS = 0.4;

  explicit BodyControlDrill(bool thighDrill)
      : thigh(thighDrill),
        arrivalHeight(thighDrill ? THIGH_ARRIVAL_HEIGHT_METRES
                                 : CHEST_ARRIVAL_HEIGHT_METRES)
  {
  }

  [[nodiscard]] const char* name() const override
  {
    return thigh ? "Thigh control" : "Chest control";
  }
  [[nodiscard]] const char* summary() const override
  {
    return thigh
               ? "A ball served at thigh height to a lone player standing on "
                 "his spot, who takes it on his thigh and keeps it. Where it "
                 "comes from, its pace and the height it arrives at; his "
                 "First Touch and height. Measures which part met it, where "
                 "(height, gap from his centre), the pace in and off his "
                 "thigh, the angle it went off at, where it lands and "
                 "whether he kept it."
               : "A ball served at chest height to a lone player on his spot, "
                 "who takes it on his chest and keeps it. Where it comes "
                 "from, its pace and the height it arrives at; his First "
                 "Touch and height. Measures which part met it, where "
                 "(height, gap from his centre), the pace in and off his "
                 "chest, the angle it went off at, where it lands and "
                 "whether he kept it.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    const auto add = [&list](DrillParameter parameter, const char* section)
    {
      parameter.section = section;
      list.push_back(std::move(parameter));
    };
    add(setting("Ball speed (m/s)", &ballSpeed,
                {.min = BALL_SPEED_MIN, .max = BALL_SPEED_MAX}, false,
                "Its pace over the ground when it is played in. Slow over a "
                "long way drops steeply onto him; fast is a driven ball."),
        "Ball");
    add(setting("Comes from (deg)", &fromAngle,
                {.min = FROM_ANGLE_MIN, .max = FROM_ANGLE_MAX}, true,
                "Where it is played from, against the way he faces (up the "
                "pitch): 0 in front of him, 90 from his right, -90 from his "
                "left. His chest and thigh play only a ball in front of "
                "him."),
        "Ball");
    add(setting("Feed distance (m)", &feedDistance,
                {.min = FEED_DISTANCE_MIN, .max = FEED_DISTANCE_MAX}, false),
        "Ball");
    add(setting("Arrival height (m)", &arrivalHeight,
                {.min = ARRIVAL_HEIGHT_MIN, .max = ARRIVAL_HEIGHT_MAX}, false,
                "How high it is as it passes over the spot where he stands. "
                "His thigh takes it from 0.28 to 0.55 of his height, his "
                "chest from 0.55 to 0.85."),
        "Ball");
    add(setting("Passes him by (m)", &passesBy,
                {.min = PASSES_BY_MIN, .max = 1.0F}, false,
                "Sideways offset of its line from where he stands: 0 "
                "straight at him."),
        "Ball");
    add(setting("First Touch", &firstTouch,
                {.min = 1.0F, .max = FIRST_TOUCH_MAX}, true,
                "His First Touch (overrides the derived stat): how softly "
                "his chest or thigh can take it, and how well he carries the "
                "touch out."),
        "Player");
    add(setting("Height (cm)", &heightCm,
                {.min = HEIGHT_CM_MIN, .max = HEIGHT_CM_MAX}, true,
                "Where his thigh and chest are (shares of his height)."),
        "Player");
    return list;
  }

  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    constexpr float PRESET_BASE_BALL_SPEED_M_S = 8.0F;
    constexpr float PRESET_BASE_FEED_DISTANCE_M = 20.0F;
    constexpr float PRESET_LONG_BALL_BALL_SPEED_M_S = 16.0F;
    constexpr float PRESET_LONG_BALL_FEED_DISTANCE_M = 40.0F;
    constexpr float PRESET_DRIVEN_BALL_BALL_SPEED_M_S = 22.0F;
    constexpr float PRESET_DRIVEN_BALL_FEED_DISTANCE_M = 25.0F;
    constexpr float PRESET_FROM_THE_SIDE_BALL_SPEED_M_S = 10.0F;
    constexpr float PRESET_FROM_THE_SIDE_COMES_FROM_DEG = 60.0F;

    const std::map<std::string, float> base{
        {"Ball speed (m/s)", PRESET_BASE_BALL_SPEED_M_S},
        {"Comes from (deg)", 0.0F},
        {"Feed distance (m)", PRESET_BASE_FEED_DISTANCE_M},
        {"Arrival height (m)",
         thigh ? THIGH_ARRIVAL_HEIGHT_METRES : CHEST_ARRIVAL_HEIGHT_METRES},
        {"Passes him by (m)", 0.0F}};
    const auto with = [&base](const std::map<std::string, float>& changes)
    {
      std::map<std::string, float> settings = base;
      for (const auto& [name, value] : changes)
      {
        settings[name] = value;
      }
      return settings;
    };
    return {
        {.name = "Dropping ball",
         .summary = "8 m/s from 20 m in front, dropping onto him.",
         .settings = base},
        {.name = "Long ball",
         .summary = "16 m/s from 40 m in front.",
         .settings =
             with({{"Ball speed (m/s)", PRESET_LONG_BALL_BALL_SPEED_M_S},
                   {"Feed distance (m)", PRESET_LONG_BALL_FEED_DISTANCE_M}})},
        {.name = "Driven ball",
         .summary = "22 m/s from 25 m in front, flat.",
         .settings =
             with({{"Ball speed (m/s)", PRESET_DRIVEN_BALL_BALL_SPEED_M_S},
                   {"Feed distance (m)", PRESET_DRIVEN_BALL_FEED_DISTANCE_M}})},
        {.name = "From the side",
         .summary = "10 m/s from 20 m, 60 deg to his right.",
         .settings =
             with({{"Ball speed (m/s)", PRESET_FROM_THE_SIDE_BALL_SPEED_M_S},
                   {"Comes from (deg)", PRESET_FROM_THE_SIDE_COMES_FROM_DEG}})},
    };
  }

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    return {{.name = "Receiver",
             .role = PlayerRole::ST,
             .homeTeam = true,
             .stats = {},
             .height = static_cast<int>(heightCm),
             .derived = {{DerivedStat::FIRST_TOUCH, firstTouch}}}};
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    playerId = ids[0];
    // In metres: he faces up the pitch (+x); the ball comes from fromAngle
    // round from in front of him, along a line passesBy to his side.
    const Vector2F up{.x = 1.0F, .y = 0.0F};
    stand = MIDDLE;
    const Vector2F from = rotated(up, fromAngle * PI / HALF_TURN_DEGREES);
    const Vector2F travel{.x = -from.x, .y = -from.y};
    const Vector2F side = rotated(travel, PI * QUARTER_TURN_PI_SHARE);
    feedFrom = along(along(stand, side, passesBy), from, feedDistance);
    feedFrom =
        PitchPoint{std::clamp(feedFrom.x, PITCH.atX(PITCH_EDGE_MIN_SHARE),
                              PITCH.atX(PITCH_EDGE_MAX_SHARE)),
                   std::clamp(feedFrom.y, PITCH.atY(PITCH_EDGE_MIN_SHARE),
                              PITCH.atY(PITCH_EDGE_MAX_SHARE))};
    MatchScenario scenario;
    scenario.players.push_back(
        {.playerId = playerId, .position = stand, .makingRun = false});
    scenario.carrierId = 0;
    scenario.ballPosition = feedFrom;
    engine.startDrill(scenario);
    engine.setDrillTarget(playerId, stand, false, {}, 0.0F, 0.0F,
                          facingAngleOf(0.0F));
    engine.keepBall(playerId, true);
    const float feedMetres =
        metresBetween(feedFrom, along(stand, side, passesBy));
    const float lift =
        HeaderTargetDrill::liftFor(engine, {.distanceMetres = feedMetres,
                                            .speedMetresPerSecond = ballSpeed,
                                            .heightMetres = arrivalHeight});
    engine.feedBall(feedFrom,
                    {.x = travel.x * ballSpeed, .y = travel.y * ballSpeed},
                    lift, playerId);
    fedAt = secondsOf(engine);
    if (const MatchPlayer* player = slotOf(engine, playerId))
    {
      lines = engine.bodyLines(*player);
    }
    met.reset();
    dropped.reset();
    collectedAt.reset();
    outcome.clear();
    endAt.reset();
    last = stateOf(engine.getBall());
    ballPath = {feedFrom};
  }

 private:
  void observeContact(const MatchEngine& engine, const MatchPlayer* player,
                      double now, bool his)
  {
    const MatchBall& ball = engine.getBall();
    // Off his head, chest or thigh, by the height he met it at.
    const char* part = contactPart(engine, his);
    if ((part != nullptr) && std::string_view(part) != partName())
    {
      outcome = std::format("not his {} (his {})", partName(), part);
    }
    else if (part != nullptr)
    {
      Touch touch;
      touch.at = now;
      touch.height = ball.touchContactHeight;
      touch.gap = ball.touchContactGap;
      touch.paceIn =
          std::hypot(std::hypot(last.velocity.x, last.velocity.y), last.lift);
      touch.speed = std::hypot(ball.velocity.x, ball.velocity.y);
      touch.lift = ball.velocityZ;
      const float incoming = std::hypot(last.velocity.x, last.velocity.y);
      if (incoming > MIN_REPORTED_CONTACT_SPEED &&
          touch.speed > MIN_REPORTED_CONTACT_SPEED)
      {
        touch.offBack = turnDegrees({.x = -last.velocity.x / incoming,
                                     .y = -last.velocity.y / incoming},
                                    {.x = ball.velocity.x / touch.speed,
                                     .y = ball.velocity.y / touch.speed});
      }
      touch.where = (player != nullptr) ? player->position : stand;
      // Where its flight off him first comes down, from where he met
      // it (the match's own flight).
      constexpr float DT =
          STEP / static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
      FlightState flight{.horizontalSpeed = touch.speed,
                         .verticalSpeed = touch.lift,
                         .height = touch.height,
                         .bounced = false,
                         .spin = ball.spin};
      float gone = 0.0F;
      for (float time = 0.0F; time < MAX_DROP_PREDICTION_SECONDS &&
                              flight.height > 0.0F && !flight.bounced;
           time += DT)
      {
        gone += engine.stepFlight(flight, DT);
        touch.landsAfter = time + DT;
      }
      touch.landsFrom = gone;
      met = touch;
    }
    else if (his)
    {
      outcome = std::format(
          "not his {} ({})", partName(),
          engine.hasTouchClaim() ? "his feet" : "trapped dead: the old trap");
    }
    else if (engine.getLastHeadPart())
    {
      outcome = std::format("not his {} (his head)", partName());
    }
    else if (now >= fedAt + TIME_LIMIT ||
             (last.height > LANDING_HEIGHT_EPSILON_METRES && ball.z <= 0.0F &&
              metresBetween(ball.position, stand) >
                  MISSED_LANDING_DISTANCE_METRES))
    {
      outcome = "not touched (out of reach)";
    }
  }

 public:
  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    const MatchPlayer* player = slotOf(engine, playerId);
    if (engine.getSimulatedSteps() % 2 == 0)
    {
      ballPath.push_back(ball.position);
    }
    const bool his = (engine.getPossessionAttachment() != nullptr) &&
                     engine.getPossessionAttachment()->getId() == playerId;
    if (!met && outcome.empty())
    {
      observeContact(engine, player, now, his);
    }
    else if (met && outcome.empty())
    {
      if (!dropped && ball.z <= 0.0F)
      {
        dropped = ball.position;
        droppedAt = now;
      }
      if (his && engine.hasTouchClaim() && engine.isTouchCollected())
      {
        outcome = "kept";
        collectedAt = now;
      }
      else if (!his)
      {
        outcome = "not kept";
      }
      else if (engine.getState() != MatchState::PLAYING)
      {
        outcome = "out of play";
      }
      else if (now >= met->at + KEEP_LIMIT)
      {
        outcome = "time up";
      }
    }
    last = stateOf(ball);
    if (!outcome.empty() && !endAt)
    {
      endAt = now;
    }
    return (!endAt || now < *endAt + LINGER_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::line(renderer, draw, feedFrom, stand, REFERENCE_LINE_COLOUR,
                    1.0F * scale);
    DrillDraw::marker(renderer, draw, feedFrom,
                      PRIMARY_MARKER_RADIUS_PIXELS * scale, FEED_MARKER_COLOUR,
                      "feed");
    DrillDraw::marker(renderer, draw, stand,
                      SECONDARY_MARKER_RADIUS_PIXELS * scale, TARGET_COLOUR,
                      partName());
    if (dropped)
    {
      DrillDraw::marker(renderer, draw, *dropped,
                        PRIMARY_MARKER_RADIUS_PIXELS * scale, LANDING_COLOUR,
                        "drops");
    }
    DrillDraw::path(renderer, draw, ballPath, BALL_TRAIL_COLOUR,
                    BALL_TRAIL_WIDTH_PIXELS * scale);
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> out;
    out.push_back(std::format(
        "{:.0f} m/s from {:+.0f} deg, {:.0f} m out, over him at {:.2f} m{}",
        ballSpeed, fromAngle, feedDistance, arrivalHeight,
        passesBy != 0.0F ? std::format(", {:+.1f} m by", passesBy)
                         : std::string()));
    out.push_back(std::format(
        "Player: First Touch {:.0f}, {:.0f} cm · thigh from {:.2f} m, chest "
        "from {:.2f} m, head from {:.2f} m",
        firstTouch, heightCm, lines.feet, lines.chest, lines.head));
    if (met)
    {
      constexpr float DEGREES = HALF_TURN_DEGREES / PI;
      out.push_back(std::format(
          "On his {} at {:.2f} s, {:.2f} m up, {:.2f} m from his centre: in at "
          "{:.1f} m/s, off at {:.1f} m/s ({:.1f} along, {:+.1f} up, {:+.0f} "
          "deg), {:.0f} deg off straight back",
          partName(), met->at - fedAt, met->height, met->gap, met->paceIn,
          std::hypot(met->speed, met->lift), met->speed, met->lift,
          std::atan2(met->lift, met->speed) * DEGREES, met->offBack));
      out.push_back(
          std::format("Its flight lands {:.2f} m from where he met it, "
                      "{:.2f} s after",
                      met->landsFrom, met->landsAfter));
    }
    if (!outcome.empty())
    {
      out.push_back("OUTCOME  " + outcome);
    }
    return out;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty())
    {
      return {};
    }
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{
        {.name = "Met", .value = flag(met.has_value())},
        {.name = "Kept", .value = flag(outcome == "kept")},
        {.name = "Head", .value = flag(outcome.ends_with("(his head)"))},
        {.name = "Chest", .value = flag(outcome.ends_with("(his chest)"))},
        {.name = "Thigh", .value = flag(outcome.ends_with("(his thigh)"))},
        {.name = "Feet", .value = flag(outcome.ends_with("(his feet)"))},
        {.name = "Old trap", .value = flag(outcome.ends_with("the old trap)"))},
        {.name = "Missed",
         .value = flag(outcome == "not touched (out of reach)")}};
    if (!met)
    {
      return out;
    }
    constexpr float DEGREES = HALF_TURN_DEGREES / PI;
    const float paceOff = std::hypot(met->speed, met->lift);
    out.push_back({.name = "Meet height (m)", .value = met->height});
    out.push_back({.name = "Meet gap (m)", .value = met->gap});
    out.push_back({.name = "Pace in (m/s)", .value = met->paceIn});
    out.push_back({.name = "Pace off (m/s)", .value = paceOff});
    out.push_back({.name = "Pace kept",
                   .value = met->paceIn > MIN_REPORTED_CONTACT_SPEED
                                ? paceOff / met->paceIn
                                : 0.0F});
    out.push_back({.name = "Elevation off (deg)",
                   .value = std::atan2(met->lift, met->speed) * DEGREES});
    out.push_back({.name = "Off straight back (deg)", .value = met->offBack});
    out.push_back({.name = "Lands from contact (m)", .value = met->landsFrom});
    out.push_back({.name = "Lands after (s)", .value = met->landsAfter});
    if (collectedAt)
    {
      out.push_back(
          {.name = "Collected after (s)", .value = *collectedAt - met->at});
    }
    return out;
  }

 private:
  /** The ball at the end of a step (pitch position, m/s, metres up). */
  struct State
  {
    PitchPoint position;
    Vector2F velocity{};
    float height = 0.0F;
    float lift = 0.0F;
  };
  static State stateOf(const MatchBall& ball)
  {
    return {.position = ball.position,
            .velocity = ball.velocity,
            .height = ball.z,
            .lift = ball.velocityZ};
  }
  struct Touch
  {
    double at = 0.0;
    float height = 0.0F;
    float gap = 0.0F;
    float paceIn = 0.0F;
    /** Off him: along the ground and upward (m/s). */
    float speed = 0.0F;
    float lift = 0.0F;
    /** Seen from above, off the line straight back the way it came (deg). */
    float offBack = 0.0F;
    PitchPoint where;
    /** Where its flight off him first comes down: how far from where
     * he met it (m) and how long after (s). */
    float landsFrom = 0.0F;
    float landsAfter = 0.0F;
  };

  [[nodiscard]] const char* partName() const
  {
    return thigh ? "thigh" : "chest";
  }

  bool thigh = false;
  float ballSpeed = DEFAULT_BALL_SPEED;
  float fromAngle = 0.0F;
  float feedDistance = DEFAULT_FEED_DISTANCE;
  float arrivalHeight = DEFAULT_ARRIVAL_HEIGHT;
  float passesBy = 0.0F;
  float firstTouch = DEFAULT_FIRST_TOUCH;
  float heightCm = DEFAULT_HEIGHT_CM;

  PlayerID playerId = 0;
  PitchPoint stand;
  PitchPoint feedFrom;
  BodyLines lines;
  double fedAt = 0.0;
  State last;
  std::optional<Touch> met;
  std::optional<PitchPoint> dropped;
  double droppedAt = 0.0;
  std::optional<double> collectedAt;
  std::string outcome;
  std::optional<double> endAt;
  std::vector<PitchPoint> ballPath;
};

// --- Loose high ball ---------------------------------------------------------

/**
 * Loose high ball (aerial challenge plan step 2): a long ball dropping
 * between one player from each side, both left to the brain. Who goes,
 * where to, whether the other contests or backs off, who gets to it first,
 * and with what jump.
 */
class LooseHighBallDrill final : public Drill
{
  static constexpr double RESULT_LINGER_SECONDS = 0.5;
  static constexpr double BALL_WATCH_SECONDS = 10.0;
  static constexpr float BALL_SPEED_MIN = 6.0F;
  static constexpr float BALL_SPEED_MAX = 30.0F;
  static constexpr float FEED_DISTANCE_MIN = 10.0F;
  static constexpr float FEED_DISTANCE_MAX = 60.0F;
  static constexpr float HOME_DISTANCE_MAX = 25.0F;
  static constexpr float HOME_ANGLE_MIN = -180.0F;
  static constexpr float HOME_ANGLE_MAX = 180.0F;
  static constexpr float AWAY_DISTANCE_MAX = 25.0F;
  static constexpr float AWAY_ANGLE_MIN = -180.0F;
  static constexpr float AWAY_ANGLE_MAX = 180.0F;
  static constexpr float HOME_HEIGHT_MIN = 150.0F;
  static constexpr float HOME_HEIGHT_MAX = 210.0F;
  static constexpr float AWAY_HEIGHT_MIN = 150.0F;
  static constexpr float AWAY_HEIGHT_MAX = 210.0F;
  static constexpr float HOME_PHYSICALITY_MAX = 100.0F;
  static constexpr float AWAY_PHYSICALITY_MAX = 100.0F;
  static constexpr float DEFAULT_BALL_SPEED = 14.0F;
  static constexpr float DEFAULT_FEED_DISTANCE = 35.0F;
  static constexpr float DEFAULT_HOME_DISTANCE = 6.0F;
  static constexpr float DEFAULT_HOME_ANGLE = 180.0F;
  static constexpr float DEFAULT_AWAY_DISTANCE = 6.0F;
  static constexpr float DEFAULT_HOME_HEIGHT = 180.0F;
  static constexpr float DEFAULT_AWAY_HEIGHT = 180.0F;
  static constexpr float DEFAULT_HOME_PHYSICALITY = 60.0F;
  static constexpr float DEFAULT_AWAY_PHYSICALITY = 60.0F;
  static constexpr float HALF_TURN_DEGREES = 180.0F;

 public:
  /** Where the ball comes down (pitch). */
  static inline const PitchPoint LANDS = at(0.55F, PITCH_MIDPOINT_SHARE);

  [[nodiscard]] const char* name() const override { return "Loose high ball"; }
  [[nodiscard]] const char* summary() const override
  {
    return "A long ball dropping between a home and an away player, both "
           "left to the brain. Who goes for it, where to, whether the other "
           "contests it or backs off, who gets to it first, and with what "
           "jump.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Ball speed (m/s)", &ballSpeed,
                           {.min = BALL_SPEED_MIN, .max = BALL_SPEED_MAX},
                           false, "Its pace over the ground when played."));
    list.push_back(setting("Feed distance (m)", &feedDistance,
                           {.min = FEED_DISTANCE_MIN, .max = FEED_DISTANCE_MAX},
                           false, "How far it travels before it comes down."));
    list.push_back(setting("Home from it (m)", &homeDistance,
                           {.min = 0.0F, .max = HOME_DISTANCE_MAX}, false,
                           "How far the home player starts from where it comes "
                           "down."));
    list.push_back(
        setting("Home angle (deg)", &homeAngle,
                {.min = HOME_ANGLE_MIN, .max = HOME_ANGLE_MAX}, true,
                "Where he starts: 0 beyond where it comes down (along "
                "its flight), 180 short of it, 90 to its left."));
    list.push_back(setting("Away from it (m)", &awayDistance,
                           {.min = 0.0F, .max = AWAY_DISTANCE_MAX}, false));
    list.push_back(setting("Away angle (deg)", &awayAngle,
                           {.min = AWAY_ANGLE_MIN, .max = AWAY_ANGLE_MAX},
                           true));
    list.push_back(setting("Home height (cm)", &homeHeight,
                           {.min = HOME_HEIGHT_MIN, .max = HOME_HEIGHT_MAX},
                           true));
    list.push_back(setting("Away height (cm)", &awayHeight,
                           {.min = AWAY_HEIGHT_MIN, .max = AWAY_HEIGHT_MAX},
                           true));
    list.push_back(setting("Home Physicality", &homePhysicality,
                           {.min = 1.0F, .max = HOME_PHYSICALITY_MAX}, true));
    list.push_back(setting("Away Physicality", &awayPhysicality,
                           {.min = 1.0F, .max = AWAY_PHYSICALITY_MAX}, true));
    return list;
  }

  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    constexpr float PRESET_BASE_BALL_SPEED_M_S = 14.0F;
    constexpr float PRESET_BASE_FEED_DISTANCE_M = 35.0F;
    constexpr float PRESET_BASE_HOME_FROM_IT_M = 6.0F;
    constexpr float PRESET_BASE_HOME_ANGLE_DEG = 180.0F;
    constexpr float PRESET_BASE_AWAY_FROM_IT_M = 6.0F;
    constexpr float PRESET_AWAY_TOO_FAR_AWAY_FROM_IT_M = 18.0F;
    constexpr float PRESET_TALL_AGAINST_SHORT_HOME_HEIGHT_CM = 195.0F;
    constexpr float PRESET_TALL_AGAINST_SHORT_AWAY_HEIGHT_CM = 170.0F;

    const std::map<std::string, float> base{
        {"Ball speed (m/s)", PRESET_BASE_BALL_SPEED_M_S},
        {"Feed distance (m)", PRESET_BASE_FEED_DISTANCE_M},
        {"Home from it (m)", PRESET_BASE_HOME_FROM_IT_M},
        {"Home angle (deg)", PRESET_BASE_HOME_ANGLE_DEG},
        {"Away from it (m)", PRESET_BASE_AWAY_FROM_IT_M},
        {"Away angle (deg)", 0.0F}};
    const auto with = [&base](const std::map<std::string, float>& changes)
    {
      std::map<std::string, float> settings = base;
      for (const auto& [name, value] : changes)
      {
        settings[name] = value;
      }
      return settings;
    };
    return {
        {.name = "Even race",
         .summary = "Both 6 m from where it comes down, one either side.",
         .settings = base},
        {.name = "Away too far",
         .summary = "Away starts 18 m off: he can't contest it.",
         .settings =
             with({{"Away from it (m)", PRESET_AWAY_TOO_FAR_AWAY_FROM_IT_M}})},
        {.name = "Tall against short",
         .summary = "Even race, a 1.95 m home player against a 1.70 m one.",
         .settings = with(
             {{"Home height (cm)", PRESET_TALL_AGAINST_SHORT_HOME_HEIGHT_CM},
              {"Away height (cm)", PRESET_TALL_AGAINST_SHORT_AWAY_HEIGHT_CM}})},
    };
  }

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    return {{.name = "Home",
             .role = PlayerRole::ST,
             .homeTeam = true,
             .stats = {{"Physicality", homePhysicality}},
             .height = static_cast<int>(homeHeight),
             .derived = {}},
            {.name = "Away",
             .role = PlayerRole::CB,
             .homeTeam = false,
             .stats = {{"Physicality", awayPhysicality}},
             .height = static_cast<int>(awayHeight),
             .derived = {}}};
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    homeId = ids[0];
    awayId = ids[1];
    const Vector2F travel{.x = 1.0F, .y = 0.0F};
    const auto startAt = [&](float metres, float degrees)
    {
      const PitchPoint at = along(
          LANDS, rotated(travel, degrees * PI / HALF_TURN_DEGREES), metres);
      return PitchPoint{std::clamp(at.x, PITCH.atX(TRAINING_SAFE_MIN_SHARE),
                                   PITCH.atX(TRAINING_SAFE_MAX_SHARE)),
                        std::clamp(at.y, PITCH.atY(TRAINING_SAFE_MIN_SHARE),
                                   PITCH.atY(TRAINING_SAFE_MAX_SHARE))};
    };
    feedFrom = along(LANDS, travel, -feedDistance);
    feedFrom =
        PitchPoint{std::clamp(feedFrom.x, PITCH.atX(PITCH_EDGE_MIN_SHARE),
                              PITCH.atX(PITCH_EDGE_MAX_SHARE)),
                   feedFrom.y};
    MatchScenario scenario;
    scenario.players.push_back({.playerId = homeId,
                                .position = startAt(homeDistance, homeAngle),
                                .makingRun = false});
    scenario.players.push_back({.playerId = awayId,
                                .position = startAt(awayDistance, awayAngle),
                                .makingRun = false});
    scenario.ballPosition = feedFrom;
    engine.startDrill(scenario);
    const float lift = HeaderTargetDrill::liftFor(
        engine, {.distanceMetres = metresBetween(feedFrom, LANDS),
                 .speedMetresPerSecond = ballSpeed,
                 .heightMetres = 0.05F});
    engine.feedBall(feedFrom,
                    {.x = travel.x * ballSpeed, .y = travel.y * ballSpeed},
                    lift, 0);
    fedAt = secondsOf(engine);
    duelsBefore = engine.getStats().aerialDuelsContested;
    headersBefore = engine.getStrikeContacts()
                        .byKind.at(static_cast<std::size_t>(StrikeKind::HEADER))
                        .strikes;
    contact.reset();
    jumped = {};
    running = {};
    ran = {};
    last = {};
    for (std::size_t index = 0; index < 2; ++index)
    {
      if (const MatchPlayer* player =
              slotOf(engine, index == 0 ? homeId : awayId))
      {
        last.at(index) = player->position;
      }
    }
    endAt.reset();
  }

 private:
  void observeContact(const MatchEngine& engine,
                      const std::array<const MatchPlayer*, 2>& who, double now)
  {
    const MatchBall& ball = engine.getBall();
    // The first of them to play it (head, chest or foot), or it lands.
    const PlayerID kicker = (engine.getLegacyContactActor() != nullptr)
                                ? engine.getLegacyContactActor()->getId()
                                : 0;
    const PlayerID holder = (engine.getPossessionAttachment() != nullptr)
                                ? engine.getPossessionAttachment()->getId()
                                : 0;
    const PlayerID by = kicker == homeId || kicker == awayId ? kicker : holder;
    if (by == homeId || by == awayId)
    {
      Contact reading;
      reading.at = now - fedAt;
      reading.by = by == homeId ? 1 : 2;
      reading.height = ball.sentHeightMetres;
      reading.headed =
          engine.getStrikeContacts()
              .byKind.at(static_cast<std::size_t>(StrikeKind::HEADER))
              .strikes > headersBefore;
      reading.duel = engine.getStats().aerialDuelsContested > duelsBefore;
      for (std::size_t index = 0; index < 2; ++index)
      {
        if (who.at(index) != nullptr)
        {
          reading.gap.at(index) =
              metresBetween(who.at(index)->position, ball.sentFrom);
        }
      }
      contact = reading;
      endAt = now + RESULT_LINGER_SECONDS;
    }
    else if (ball.z <= 0.0F && now > fedAt + RESULT_LINGER_SECONDS)
    {
      // Down without either of them getting to it in the air.
      Contact reading;
      reading.at = now - fedAt;
      for (std::size_t index = 0; index < 2; ++index)
      {
        if (who.at(index) != nullptr)
        {
          reading.gap.at(index) =
              metresBetween(who.at(index)->position, ball.position);
        }
      }
      contact = reading;
      endAt = now + RESULT_LINGER_SECONDS;
    }
  }

 public:
  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    std::array<const MatchPlayer*, 2> who{slotOf(engine, homeId),
                                          slotOf(engine, awayId)};
    for (std::size_t index = 0; index < 2; ++index)
    {
      if (who.at(index) == nullptr)
      {
        continue;
      }
      ran.at(index) += metresBetween(last.at(index), who.at(index)->position);
      last.at(index) = who.at(index)->position;
      if (!contact && who.at(index)->airborne() && !jumped.at(index))
      {
        jumped.at(index) = true;
        running.at(index) = who.at(index)->runningJump;
      }
    }
    if (!contact)
    {
      observeContact(engine, who, now);
    }
    if (engine.getState() != MatchState::PLAYING && !endAt)
    {
      endAt = now;
    }
    return (!endAt || now < *endAt) && now < fedAt + BALL_WATCH_SECONDS;
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    static constexpr std::array<const char*, 3> WHO{"nobody: it came down",
                                                    "home", "away"};
    std::vector<std::string> lines;
    lines.push_back(
        std::format("{:.0f} m/s over {:.0f} m · home {:.0f} m off at {:+.0f} "
                    "deg ({:.0f} cm), "
                    "away {:.0f} m off at {:+.0f} deg ({:.0f} cm)",
                    ballSpeed, feedDistance, homeDistance, homeAngle,
                    homeHeight, awayDistance, awayAngle, awayHeight));
    if (contact)
    {
      lines.push_back(std::format(
          "OUTCOME  {} at {:.2f} s{}{}, {:.2f} m up · home {} ({:.1f} m away, "
          "ran "
          "{:.1f} m), away {} ({:.1f} m away, ran {:.1f} m)",
          WHO.at(static_cast<std::size_t>(contact->by)), contact->at,
          contact->headed ? ", headed" : "", contact->duel ? ", in a duel" : "",
          contact->height, jumpName(0), contact->gap.at(0), ran.at(0),
          jumpName(1), contact->gap.at(1), ran.at(1)));
    }
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (!contact)
    {
      return {};
    }
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    return {{.name = "First to it (1 home, 2 away)",
             .value = static_cast<double>(contact->by)},
            {.name = "Headed", .value = flag(contact->headed)},
            {.name = "Duel", .value = flag(contact->duel)},
            {.name = "Contact at (s)", .value = contact->at},
            {.name = "Contact height (m)", .value = contact->height},
            {.name = "Home jumped", .value = flag(jumped.at(0))},
            {.name = "Away jumped", .value = flag(jumped.at(1))},
            {.name = "Home running jump",
             .value = flag(jumped.at(0) && running.at(0))},
            {.name = "Away running jump",
             .value = flag(jumped.at(1) && running.at(1))},
            {.name = "Home gap at contact (m)", .value = contact->gap.at(0)},
            {.name = "Away gap at contact (m)", .value = contact->gap.at(1)},
            {.name = "Home ran (m)", .value = ran.at(0)},
            {.name = "Away ran (m)", .value = ran.at(1)}};
  }

 private:
  struct Contact
  {
    double at = 0.0;
    /** 0 nobody, 1 home, 2 away. */
    int by = 0;
    float height = 0.0F;
    bool headed = false;
    bool duel = false;
    /** Each player's distance from the ball at the contact (m). */
    std::array<float, 2> gap{};
  };
  [[nodiscard]] std::string jumpName(std::size_t index) const
  {
    if (!jumped.at(index))
    {
      return "didn't jump";
    }
    return running.at(index) ? "running jump" : "standing jump";
  }

  float ballSpeed = DEFAULT_BALL_SPEED;
  float feedDistance = DEFAULT_FEED_DISTANCE;
  float homeDistance = DEFAULT_HOME_DISTANCE;
  float homeAngle = DEFAULT_HOME_ANGLE;
  float awayDistance = DEFAULT_AWAY_DISTANCE;
  float awayAngle = 0.0F;
  float homeHeight = DEFAULT_HOME_HEIGHT;
  float awayHeight = DEFAULT_AWAY_HEIGHT;
  float homePhysicality = DEFAULT_HOME_PHYSICALITY;
  float awayPhysicality = DEFAULT_AWAY_PHYSICALITY;

  PlayerID homeId = 0;
  PlayerID awayId = 0;
  PitchPoint feedFrom;
  double fedAt = 0.0;
  int duelsBefore = 0;
  std::uint64_t headersBefore = 0;
  std::optional<Contact> contact;
  std::array<bool, 2> jumped{};
  std::array<bool, 2> running{};
  std::array<float, 2> ran{};
  std::array<PitchPoint, 2> last{};
  std::optional<double> endAt;
};
}  // namespace

/**
 * Walled box (training ground plan section 18, T2): a few players a side
 * play on a walled training pitch with a ball, no goals yet. The ball is
 * fed towards a wall at the start; the run counts the wall bounces and
 * checks the ball and the players never get out.
 */
class WalledBoxDrill final : public Drill
{
  static constexpr double END_TIME_TOLERANCE_SECONDS = 0.05;
  static constexpr float LENGTH_SETTING_MIN = 10.0F;
  static constexpr float LENGTH_SETTING_MAX = 60.0F;
  static constexpr float WIDTH_SETTING_MIN = 8.0F;
  static constexpr float WIDTH_SETTING_MAX = 45.0F;
  static constexpr float SIDE_SETTING_MAX = 4.0F;
  static constexpr float SECONDS_SETTING_MIN = 10.0F;
  static constexpr float SECONDS_SETTING_MAX = 120.0F;
  static constexpr float FEED_SETTING_MIN = 2.0F;
  static constexpr float FEED_SETTING_MAX = 25.0F;
  static constexpr float GOALS_CHOICE_MAX = 7.0F;
  static constexpr float SERVE_SIDE_CHOICE_MAX = 3.0F;
  static constexpr float DUMMIES_CHOICE_MAX = 2.0F;
  static constexpr float DEFAULT_LENGTH_SETTING = 30.0F;
  static constexpr float DEFAULT_WIDTH_SETTING = 20.0F;
  static constexpr float DEFAULT_SIDE_SETTING = 2.0F;
  static constexpr float DEFAULT_SECONDS_SETTING = 60.0F;
  static constexpr float DEFAULT_FEED_SETTING = 12.0F;

 public:
  [[nodiscard]] const char* name() const override { return "Walled box"; }
  [[nodiscard]] const char* summary() const override
  {
    return "A few players a side on a walled training pitch, a ball fed "
           "towards a wall. The walls send it back at any height; nobody and "
           "nothing gets out. Goals of any size at the ends (training ground "
           "T3); after a goal a coach by the pitch serves a new ball in "
           "(T4).";
  }

  [[nodiscard]] MatchPitch pitch() const override
  {
    MatchPitch box = MatchPitch::training(
        lengthSetting, widthSetting,
        static_cast<GoalKind>(static_cast<int>(std::round(goalsChoice))));
    box.laws.serveSide =
        static_cast<ServeSide>(static_cast<int>(std::round(serveSideChoice)));
    box.laws.serveReceiver = static_cast<ServeReceiver>(
        static_cast<int>(std::round(serveReceiverChoice)));
    box.laws.fouls = foulsChoice > CHOICE_MIDPOINT;
    return box;
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(
        setting("Length (m)", &lengthSetting,
                {.min = LENGTH_SETTING_MIN, .max = LENGTH_SETTING_MAX}, true));
    list.push_back(setting("Width (m)", &widthSetting,
                           {.min = WIDTH_SETTING_MIN, .max = WIDTH_SETTING_MAX},
                           true));
    list.push_back(setting("Players a side", &sideSetting,
                           {.min = 1.0F, .max = SIDE_SETTING_MAX}, true));
    list.push_back(setting(
        "Seconds", &secondsSetting,
        {.min = SECONDS_SETTING_MIN, .max = SECONDS_SETTING_MAX}, true));
    list.push_back(setting("Feed speed (m/s)", &feedSetting,
                           {.min = FEED_SETTING_MIN, .max = FEED_SETTING_MAX},
                           false));
    DrillParameter goals = setting("Goals", &goalsChoice,
                                   {.min = 0.0F, .max = GOALS_CHOICE_MAX}, true,
                                   "A goal at each end (training ground T3); "
                                   "after a goal the coach serves a new ball.");
    goals.choices = {"None",   "Full size", "9v9",          "7v7",
                     "Futsal", "5-a-side",  "Small framed", "Pop-up"};
    list.push_back(goals);
    DrillParameter side =
        setting("Serve to", &serveSideChoice,
                {.min = 0.0F, .max = SERVE_SIDE_CHOICE_MAX}, true,
                "Which side the coach serves a new ball to "
                "(training ground T4).");
    side.choices = {"Side that conceded", "Alternate", "Home", "Away"};
    list.push_back(side);
    DrillParameter receiver =
        setting("Serve receiver", &serveReceiverChoice,
                {.min = 0.0F, .max = 1.0F}, true, "Which player of that side.");
    receiver.choices = {"Most open", "Nearest the coach"};
    list.push_back(receiver);
    DrillParameter fouls =
        setting("Fouls", &foulsChoice, {.min = 0.0F, .max = 1.0F}, true,
                "Off: nothing is given. Whistled: the coach "
                "serves to the fouled side (no penalties or "
                "cards).");
    fouls.choices = {"Off", "Whistled"};
    list.push_back(fouls);
    DrillParameter dummies =
        setting("Dummies", &dummiesChoice,
                {.min = 0.0F, .max = DUMMIES_CHOICE_MAX}, true,
                "Four in a diamond: one in front of each "
                "goal, one either side of the middle "
                "(training ground T5).");
    dummies.choices = {"None", "Mannequins", "Poles"};
    list.push_back(dummies);
    return list;
  }

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    std::vector<DrillPlayerSpec> list;
    const int side = static_cast<int>(std::round(sideSetting));
    for (int index = 0; index < side; ++index)
    {
      list.push_back({.name = std::format("Home {}", index + 1),
                      .role = PlayerRole::CM,
                      .homeTeam = true,
                      .stats = {},
                      .derived = {}});
      list.push_back({.name = std::format("Away {}", index + 1),
                      .role = PlayerRole::CM,
                      .homeTeam = false,
                      .stats = {},
                      .derived = {}});
    }
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    const MatchPitch& box = engine.getPitch();
    ids_ = ids;
    // Home in the left half, away in the right, spread across.
    MatchScenario scenario;
    const std::size_t perSide = ids.size() / 2;
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
      const bool home = index % 2 == 0;
      const std::size_t rank = index / 2;
      const float across = (static_cast<float>(rank) + 1.0F) /
                           (static_cast<float>(perSide) + 1.0F);
      scenario.players.push_back(
          {.playerId = ids[index],
           .position = PitchPoint{box.length * (home ? WALLED_HOME_START_SHARE
                                                     : WALLED_AWAY_START_SHARE),
                                  box.width * across},
           .makingRun = false});
    }
    const PitchPoint centre{PITCH_MIDPOINT_SHARE * box.length,
                            PITCH_MIDPOINT_SHARE * box.width};
    scenario.ballPosition = centre;
    engine.startDrill(scenario);
    // Fed towards the far corner, so it meets the walls early.
    const Vector2F towards{.x = box.length - centre.x,
                           .y = box.width - centre.y};
    const float far = std::hypot(towards.x, towards.y);
    engine.feedBall(centre,
                    {.x = towards.x / far * feedSetting,
                     .y = towards.y / far * feedSetting},
                    0.0F, 0);
    // The kit: a halfway line and a cone in each corner (drawn, not solid).
    PitchKit::Layout kit;
    kit.lines.push_back({.name = "Halfway",
                         .from = PitchPoint{centre.x, 0.0F},
                         .to = PitchPoint{centre.x, box.width}});
    constexpr float IN = 1.0F;
    const std::array<PitchPoint, 4> corners{
        PitchPoint{IN, IN}, PitchPoint{box.length - IN, IN},
        PitchPoint{IN, box.width - IN},
        PitchPoint{box.length - IN, box.width - IN}};
    for (std::size_t index = 0; index < corners.size(); ++index)
    {
      kit.cones.push_back({.name = std::format("Corner {}", index + 1),
                           .at = corners.at(index),
                           .kind = PitchKit::ConeKind::TALL});
    }
    // Dummies (training ground T5): four in a diamond, one in front of
    // each goal and one either side of the middle, clear of the start.
    if (dummiesChoice > CHOICE_MIDPOINT)
    {
      const PitchKit::DummyKind kind = dummiesChoice > 1.5F
                                           ? PitchKit::DummyKind::POLE
                                           : PitchKit::DummyKind::MANNEQUIN;
      const std::array<PitchPoint, 4> spots{
          PitchPoint{0.25F * box.length, centre.y},
          PitchPoint{0.75F * box.length, centre.y},
          PitchPoint{centre.x, 0.25F * box.width},
          PitchPoint{centre.x, 0.75F * box.width}};
      for (std::size_t index = 0; index < spots.size(); ++index)
      {
        PitchKit::Dummy dummy;
        dummy.name = std::format("Dummy {}", index + 1);
        dummy.at = spots.at(index);
        dummy.kind = kind;
        kit.dummies.push_back(std::move(dummy));
      }
    }
    engine.setKit(std::move(kit));
    stoppedBy.clear();
    eventsSeen = engine.getEvents().size();
    foulsAtStart = engine.getFoulsNotGiven();
    foulsNotGiven = 0;
    servesAtStart = engine.getServesTaken();
    dummyHitsAtStart = engine.getDummyHits();
    bodyHitsAtStart = engine.getDummyBodyHits();
    blockedAtStart = engine.getPassesBlockedByDummies();
    blockedPasses = 0;
    bodyHits = 0;
    dummyHits = 0;
    ballInDummy = 0;
    playersInDummy = 0;
    serves = 0;
    foulsGiven = 0;
    shots = 0;
    shotXG = 0.0;
    shotDistance = 0.0;
    bounces = 0;
    ballOut = 0;
    playersOut = 0;
    lastVelocity = engine.getBall().velocity;
    nearestWall = std::numeric_limits<float>::max();
  }

 private:
  void observeDummyClearance(const MatchEngine& engine)
  {
    const MatchBall& ball = engine.getBall();
    const PitchPoint at = ball.position;
    // Nothing passes through a dummy: the ball's centre never within its
    // radius of the axis (ground to shoulder).
    for (const PitchKit::Dummy& dummy : engine.getKit().dummies)
    {
      const PitchKit::DummyShape shape = PitchKit::dummyShape(dummy.kind);
      const float axisZ = std::clamp(ball.z, 0.0F, shape.height - shape.radius);
      if (std::hypot(at.x - dummy.at.x, at.y - dummy.at.y, ball.z - axisZ) <
          shape.radius)
      {
        ++ballInDummy;
      }
      // Nor does anyone: his centre never within its radius plus his (to
      // a millimetre).
      for (const PlayerID id : ids_)
      {
        if (const MatchPlayer* player = slotOf(engine, id))
        {
          if (std::hypot(player->position.x - dummy.at.x,
                         player->position.y - dummy.at.y) <
              shape.radius + MatchTuning::Body::RADIUS_METRES -
                  DUMMY_CLEARANCE_TOLERANCE_METRES)
          {
            ++playersInDummy;
          }
        }
      }
    }
  }

  void observeWallBounce(const MatchPitch& box, const MatchBall& ball)
  {
    constexpr float RADIUS = MatchTuning::Units::BALL_RADIUS_METRES;
    constexpr float NEAR = 0.6F;
    const PitchPoint at = ball.position;
    // A bounce: the speed into a wall turned round next to it.
    if ((at.x < RADIUS + NEAR && lastVelocity.x < 0.0F &&
         ball.velocity.x > 0.0F) ||
        (at.x > box.length - RADIUS - NEAR && lastVelocity.x > 0.0F &&
         ball.velocity.x < 0.0F) ||
        (at.y < RADIUS + NEAR && lastVelocity.y < 0.0F &&
         ball.velocity.y > 0.0F) ||
        (at.y > box.width - RADIUS - NEAR && lastVelocity.y > 0.0F &&
         ball.velocity.y < 0.0F))
    {
      ++bounces;
    }
  }

  void observeEvents(const MatchEngine& engine, const MatchPitch& box)
  {
    // Shots: their xG and how far out, to the middle of the goal attacked.
    const auto& log = engine.getEvents();
    for (; eventsSeen < log.size(); ++eventsSeen)
    {
      const MatchEvent& event = log[eventsSeen];
      if (event.type == MatchEventType::FOUL)
      {
        ++foulsGiven;
      }
      if (event.type != MatchEventType::SHOT)
      {
        continue;
      }
      ++shots;
      shotXG += event.xg;
      const float goalX = event.isHomeTeam ? box.length : 0.0F;
      shotDistance +=
          std::hypot(event.position.x - goalX,
                     event.position.y - (PITCH_MIDPOINT_SHARE * box.width));
    }
  }

 public:
  bool afterStep(MatchEngine& engine) override
  {
    const MatchPitch& box = engine.getPitch();
    const MatchBall& ball = engine.getBall();
    constexpr float RADIUS = MatchTuning::Units::BALL_RADIUS_METRES;
    const PitchPoint at = ball.position;
    observeWallBounce(box, ball);
    lastVelocity = ball.velocity;
    // Waiting for a serve, the ball is with the coach outside the wall.
    const bool withCoach = engine.getState() == MatchState::SERVE;
    if (!withCoach && at.x >= 0.0F && at.x <= box.length)
    {
      nearestWall = std::min(
          {nearestWall, at.x, at.y, box.length - at.x, box.width - at.y});
    }
    // Outside: beyond the walls and not in a goal's net.
    const bool inGoal = box.goal.present() &&
                        std::abs(at.y - (PITCH_MIDPOINT_SHARE * box.width)) <
                            box.goal.halfWidth + box.goal.post &&
                        at.x > -box.goal.netDepth - RADIUS &&
                        at.x < box.length + box.goal.netDepth + RADIUS;
    if (!withCoach &&
        (at.x < 0.0F || at.y < 0.0F || at.x > box.length || at.y > box.width) &&
        !inGoal)
    {
      ++ballOut;
    }
    homeGoals = engine.getHomeScore();
    awayGoals = engine.getAwayScore();
    foulsNotGiven = engine.getFoulsNotGiven() - foulsAtStart;
    serves = engine.getServesTaken() - servesAtStart;
    dummyHits = engine.getDummyHits() - dummyHitsAtStart;
    bodyHits = engine.getDummyBodyHits() - bodyHitsAtStart;
    blockedPasses = engine.getPassesBlockedByDummies() - blockedAtStart;
    observeDummyClearance(engine);
    observeEvents(engine, box);
    for (const PlayerID id : ids_)
    {
      if (const MatchPlayer* player = slotOf(engine, id))
      {
        if (player->position.x < 0.0F || player->position.y < 0.0F ||
            player->position.x > box.length || player->position.y > box.width)
        {
          ++playersOut;
        }
      }
    }
    elapsed = secondsOf(engine);
    // Play goes on through a goal and the coach's serve; anything else
    // stops it.
    const MatchState state = engine.getState();
    const bool going = state == MatchState::PLAYING ||
                       state == MatchState::GOAL || state == MatchState::SERVE;
    if (!going && stoppedBy.empty())
    {
      const auto& events = engine.getEvents();
      stoppedBy = events.empty()
                      ? std::string("?")
                      : std::string(matchEventTypeName(events.back().type));
    }
    return elapsed < secondsSetting && going;
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> lines{
        std::format("{:.1f} s of {:.0f}", elapsed, secondsSetting),
        std::format("Wall bounces: {}", bounces),
        std::format("Goals: home {}, away {}", homeGoals, awayGoals),
        shots == 0
            ? std::string("Shots: none")
            : std::format(
                  "Shots: {}, xG {:.2f} (mean {:.2f}), mean {:.1f} m out",
                  shots, shotXG, shotXG / shots, shotDistance / shots),
        std::format("Ball's centre nearest a wall: {:.2f} m", nearestWall),
        std::format("Ticks with the ball outside: {}; players outside: {}",
                    ballOut, playersOut),
        std::format("Fouls not given (no Laws on a training pitch): {}",
                    foulsNotGiven),
        std::format("Fouls given: {}; coach's serves: {}", foulsGiven, serves),
        std::format("Dummy hits: {}; ticks with the ball inside one: {}",
                    dummyHits, ballInDummy),
        std::format("Player bounces off dummies: {}; ticks with a player "
                    "inside one: {}",
                    bodyHits, playersInDummy),
        std::format("Pass options left out for a dummy in the way: {}",
                    blockedPasses),
        stoppedBy.empty() ? std::string("Play never stopped")
                          : std::format("Play stopped by a {}", stoppedBy)};
    // The headline once the run is over.
    if (!stoppedBy.empty() ||
        elapsed >= secondsSetting - END_TIME_TOLERANCE_SECONDS)
    {
      lines.push_back(std::format(
          "OUTCOME  {} wall bounces, {} shots, {} goals, {} serves, {}",
          bounces, shots, homeGoals + awayGoals, serves,
          ballOut == 0 && playersOut == 0 ? "nothing got out"
                                          : "SOMETHING GOT OUT"));
    }
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (elapsed < secondsSetting - END_TIME_TOLERANCE_SECONDS)
    {
      return {};
    }
    return {
        {.name = "Wall bounces", .value = static_cast<double>(bounces)},
        {.name = "Goals", .value = static_cast<double>(homeGoals + awayGoals)},
        {.name = "Shots", .value = static_cast<double>(shots)},
        {.name = "Fouls not given",
         .value = static_cast<double>(foulsNotGiven)},
        {.name = "Serves", .value = static_cast<double>(serves)},
        {.name = "Fouls given", .value = static_cast<double>(foulsGiven)},
        {.name = "Dummy hits", .value = static_cast<double>(dummyHits)},
        {.name = "Ball inside a dummy (ticks)",
         .value = static_cast<double>(ballInDummy)},
        {.name = "Players inside a dummy (ticks)",
         .value = static_cast<double>(playersInDummy)},
        {.name = "Player bounces off dummies",
         .value = static_cast<double>(bodyHits)},
        {.name = "Pass options left out for dummies",
         .value = static_cast<double>(blockedPasses)},
        {.name = "Shot xG", .value = shotXG},
        {.name = "Shot distance (m)",
         .value = shots == 0 ? 0.0 : shotDistance / shots},
        {.name = "Nearest wall (m)", .value = nearestWall},
        {.name = "Ball outside (ticks)", .value = static_cast<double>(ballOut)},
        {.name = "Players outside (ticks)",
         .value = static_cast<double>(playersOut)}};
  }

 private:
  float lengthSetting = DEFAULT_LENGTH_SETTING;
  float widthSetting = DEFAULT_WIDTH_SETTING;
  float sideSetting = DEFAULT_SIDE_SETTING;
  float secondsSetting = DEFAULT_SECONDS_SETTING;
  float feedSetting = DEFAULT_FEED_SETTING;
  float goalsChoice = 0.0F;
  int homeGoals = 0;
  int awayGoals = 0;
  std::vector<PlayerID> ids_;
  std::size_t eventsSeen = 0;
  int foulsAtStart = 0;
  int foulsNotGiven = 0;
  int servesAtStart = 0;
  int serves = 0;
  int foulsGiven = 0;
  int dummyHitsAtStart = 0;
  int dummyHits = 0;
  int ballInDummy = 0;
  int playersInDummy = 0;
  int bodyHitsAtStart = 0;
  int bodyHits = 0;
  int blockedAtStart = 0;
  int blockedPasses = 0;
  float serveSideChoice = 0.0F;
  float serveReceiverChoice = 0.0F;
  float foulsChoice = 0.0F;
  float dummiesChoice = 0.0F;
  int shots = 0;
  double shotXG = 0.0;
  double shotDistance = 0.0;
  int bounces = 0;
  int ballOut = 0;
  int playersOut = 0;
  Vector2F lastVelocity{};
  float nearestWall = 0.0F;
  double elapsed = 0.0;
  std::string stoppedBy;
};

/**
 * Small-sided game (training ground plan section 22, T6): the brain
 * redesign's ladder (1 player, 1 v 1, 2 v 2, 3 v 3, N v N) on a walled
 * training pitch sized by area per player, goals at the ends, the coach
 * serving to the home side to start and after every goal. Reports what
 * each rung is there to show; measures, not targets.
 */
class SmallSidedGameDrill final : public Drill
{
  [[nodiscard]] std::string intentDescription() const
  {
    std::vector<std::size_t> order(PLAYER_INTENT_COUNT);
    for (std::size_t index = 0; index < order.size(); ++index)
    {
      order[index] = index;
    }
    std::ranges::stable_sort(order, [this](std::size_t a, std::size_t b)
                             { return intentTicks.at(a) > intentTicks.at(b); });
    std::string doing;
    for (const std::size_t index : order)
    {
      if (intentTicks.at(index) == 0)
      {
        break;
      }
      doing += std::format(
          "{}{} {:.0f}%", doing.empty() ? "" : ", ",
          MatchEngineInternal::intentName(static_cast<PlayerIntent>(index)),
          100.0 * intentTicks.at(index) / playerTicks);
    }
    return doing;
  }

  static constexpr double END_TIME_TOLERANCE_SECONDS = 0.05;
  static constexpr float RUNG_CHOICE_MAX = 4.0F;
  static constexpr float N_SETTING_MIN = 4.0F;
  static constexpr float N_SETTING_MAX = 6.0F;
  static constexpr float AREA_SETTING_MIN = 75.0F;
  static constexpr float AREA_SETTING_MAX = 200.0F;
  static constexpr float SECONDS_SETTING_MIN = 30.0F;
  static constexpr float SECONDS_SETTING_MAX = 600.0F;
  static constexpr float GOALS_CHOICE_MAX = 7.0F;
  static constexpr float DEFAULT_N_SETTING = 4.0F;
  static constexpr float DEFAULT_AREA_SETTING = 100.0F;
  static constexpr float DEFAULT_SECONDS_SETTING = 120.0F;
  static constexpr float DEFAULT_GOALS_CHOICE = 5.0F;

 public:
  [[nodiscard]] const char* name() const override { return "Small-sided game"; }
  [[nodiscard]] const char* summary() const override
  {
    return "The brain redesign's ladder: one player alone, then 1 v 1, 2 v 2, "
           "3 v 3 and N v N, on a walled pitch sized by area per player, with "
           "goals at the ends. The coach serves to the home side to start and "
           "to the side that conceded after a goal. Reports goals, shots, "
           "possession and passes (training ground T6). Layers off (the "
           "default): no positions, tactics, home advantage, game state or "
           "shouts.";
  }

  [[nodiscard]] std::vector<DrillPreset> presets() const override
  {
    std::vector<DrillPreset> list;
    const std::array<const char*, 5> names{"1 player", "1 v 1", "2 v 2",
                                           "3 v 3", "N v N"};
    list.reserve(names.size());
    for (std::size_t rung = 0; rung < names.size(); ++rung)
    {
      list.push_back(
          {.name = names.at(rung),
           .summary = std::format("The ladder's rung: {}.", names.at(rung)),
           .settings = {{"Rung", static_cast<float>(rung)}}});
    }
    return list;
  }

  [[nodiscard]] MatchPitch pitch() const override
  {
    // Area per player over the outfield players (the 1-player rung on the
    // 1 v 1 pitch), length 1.5 times the width.
    const float players = static_cast<float>(std::max(2, 2 * perSide()));
    const float area = areaSetting * players;
    const float width = std::sqrt(area / SMALL_SIDED_PITCH_ASPECT_RATIO);
    return MatchPitch::training(
        SMALL_SIDED_PITCH_ASPECT_RATIO * width, width,
        static_cast<GoalKind>(static_cast<int>(std::round(goalsChoice))));
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    DrillParameter rung = setting("Rung", &rungChoice,
                                  {.min = 0.0F, .max = RUNG_CHOICE_MAX}, true,
                                  "The ladder's rung (brain redesign plan, "
                                  "section 3).");
    rung.choices = {"1 player", "1 v 1", "2 v 2", "3 v 3", "N v N"};
    list.push_back(rung);
    list.push_back(setting("N a side (N v N)", &nSetting,
                           {.min = N_SETTING_MIN, .max = N_SETTING_MAX}, true));
    list.push_back(
        setting("Area per player (m2)", &areaSetting,
                {.min = AREA_SETTING_MIN, .max = AREA_SETTING_MAX}, true,
                "Pitch area over the outfield players. Studies of "
                "small-sided games use about 75-150; a full match is "
                "about 357."));
    list.push_back(setting(
        "Seconds", &secondsSetting,
        {.min = SECONDS_SETTING_MIN, .max = SECONDS_SETTING_MAX}, true));
    DrillParameter goals = setting(
        "Goals", &goalsChoice, {.min = 1.0F, .max = GOALS_CHOICE_MAX}, true);
    goals.choices = {"None",   "Full size", "9v9",          "7v7",
                     "Futsal", "5-a-side",  "Small framed", "Pop-up"};
    list.push_back(goals);
    DrillParameter layersSetting = setting(
        "Layers", &layersChoice, {.min = 0.0F, .max = 1.0F}, true,
        "Off: the neutral ladder (brain redesign plan, section 3). No "
        "natural positions or formation, base tactics untouched by score, "
        "underdog or shouts, no home advantage, full tactical familiarity. "
        "On: as a match sets them.");
    layersSetting.choices = {"Off", "On"};
    list.push_back(layersSetting);
    return list;
  }

  [[nodiscard]] std::vector<DrillPlayerSpec> players() const override
  {
    std::vector<DrillPlayerSpec> list;
    const int side = perSide();
    const bool alone = rung() == 0;
    // No layers: no natural position either.
    const PlayerRole role = layers() ? PlayerRole::CM : PlayerRole::UNKNOWN;
    for (int index = 0; index < std::max(1, side); ++index)
    {
      list.push_back({.name = std::format("Home {}", index + 1),
                      .role = role,
                      .homeTeam = true,
                      .stats = {},
                      .derived = {}});
      if (!alone)
      {
        list.push_back({.name = std::format("Away {}", index + 1),
                        .role = role,
                        .homeTeam = false,
                        .stats = {},
                        .derived = {}});
      }
    }
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    const MatchPitch& box = engine.getPitch();
    ids_ = ids;
    homeIds_.clear();
    // Each side spread across its own end, a quarter of the way in.
    MatchScenario scenario;
    const bool alone = rung() == 0;
    const std::size_t perSideCount = alone ? ids.size() : ids.size() / 2;
    for (std::size_t index = 0; index < ids.size(); ++index)
    {
      const bool home = alone || index % 2 == 0;
      const std::size_t rank = alone ? index : index / 2;
      const float across = (static_cast<float>(rank) + 1.0F) /
                           (static_cast<float>(perSideCount) + 1.0F);
      if (home)
      {
        homeIds_.push_back(ids[index]);
      }
      scenario.players.push_back(
          {.playerId = ids[index],
           .position =
               PitchPoint{box.length * (home ? SMALL_SIDED_HOME_START_SHARE
                                             : SMALL_SIDED_AWAY_START_SHARE),
                          box.width * across},
           .makingRun = false});
    }
    scenario.ballPosition = PitchPoint{PITCH_MIDPOINT_SHARE * box.length,
                                       PITCH_MIDPOINT_SHARE * box.width};
    engine.startDrill(scenario);
    // The coach serves to the home side to start.
    engine.serveToStart(true);
    PitchKit::Layout kit;
    kit.lines.push_back(
        {.name = "Halfway",
         .from = PitchPoint{PITCH_MIDPOINT_SHARE * box.length, 0.0F},
         .to = PitchPoint{PITCH_MIDPOINT_SHARE * box.length, box.width}});
    engine.setKit(std::move(kit));
    const MatchStats& stats = engine.getStats();
    statsAtStart = stats;
    eventsSeen = engine.getEvents().size();
    elapsed = 0.0;
    homeGoals = awayGoals = 0;
    homeShots = awayShots = 0;
    homePasses = awayPasses = homeCompleted = awayCompleted = 0;
    homeTicks = awayTicks = looseTicks = inPlayTicks = 0;
    ownGoals = 0;
    changes = 0;
    lastSide = 0;
    reachedAt.reset();
    firstShotAt.reset();
    spellSide = spells = tackles = 0;
    spellStart = longestSpell = 0.0;
    homeSwarmTicks = awaySwarmTicks = spreadSamples = 0;
    spreadSum = 0.0;
    defendingTicks = pressCoverTicks = carrierTicks = 0;
    openSum = 0.0;
    intentTicks.fill(0);
    playerTicks = 0;
    stoppedBy.clear();
  }

  bool afterStep(MatchEngine& engine) override
  {
    elapsed = secondsOf(engine);
    homeGoals = engine.getHomeScore();
    awayGoals = engine.getAwayScore();
    const MatchStats& stats = engine.getStats();
    homeShots = stats.homeShots - statsAtStart.homeShots;
    awayShots = stats.awayShots - statsAtStart.awayShots;
    homePasses = stats.homePassesAttempted - statsAtStart.homePassesAttempted;
    awayPasses = stats.awayPassesAttempted - statsAtStart.awayPassesAttempted;
    homeCompleted =
        stats.homePassesCompleted - statsAtStart.homePassesCompleted;
    awayCompleted =
        stats.awayPassesCompleted - statsAtStart.awayPassesCompleted;
    tackles = stats.homeTackles + stats.awayTackles - statsAtStart.homeTackles -
              statsAtStart.awayTackles;
    // Possession, in play only. The team in possession is the side that
    // last had the ball under control (as the match statistics count it);
    // a change is it passing to the other side. The ball is loose when
    // nobody holds it, claims a touch on it or is dribbling it (his next
    // meet with it planned).
    const MatchState state = engine.getState();
    if (state == MatchState::PLAYING)
    {
      const std::optional<bool> team =
          engine.getPossessionHistory().lastControlledTeamHome;
      if (team && *team)
      {
        ++homeTicks;
      }
      else if (team)
      {
        ++awayTicks;
      }
      const int side = teamSideNumber(team);
      if (side != 0)
      {
        if (lastSide != 0 && side != lastSide)
        {
          ++changes;
        }
        lastSide = side;
      }
      if ((engine.getPossessionAttachment() == nullptr) &&
          (engine.getBall().meetFor == nullptr))
      {
        ++looseTicks;
      }
      ++inPlayTicks;
    }
    // What each rung is there to show (training ground T6b): measures, not
    // targets.
    if (state == MatchState::PLAYING)
    {
      measureRung(engine);
    }
    // Own goals (the 1-player rung has nobody else to score).
    const auto& log = engine.getEvents();
    for (; eventsSeen < log.size(); ++eventsSeen)
    {
      if (log[eventsSeen].type == MatchEventType::OWN_GOAL)
      {
        ++ownGoals;
      }
    }
    // Play goes on through goals and the coach's serves; anything else
    // stops it.
    const bool going = state == MatchState::PLAYING ||
                       state == MatchState::GOAL || state == MatchState::SERVE;
    if (!going && stoppedBy.empty())
    {
      const auto& events = engine.getEvents();
      stoppedBy = events.empty()
                      ? std::string("?")
                      : std::string(matchEventTypeName(events.back().type));
    }
    return elapsed < secondsSetting && going;
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    const int inPlay = inPlayTicks;
    const auto share = [inPlay](int ticks)
    { return inPlay == 0 ? 0.0 : 100.0 * ticks / inPlay; };
    std::vector<std::string> lines{
        std::format("{:.1f} s of {:.0f}", elapsed, secondsSetting),
        std::format("Goals: home {}, away {} (own goals {})", homeGoals,
                    awayGoals, ownGoals),
        std::format("Shots: home {}, away {}", homeShots, awayShots),
        std::format("In possession (in play): home {:.0f}%, away {:.0f}%; the "
                    "ball loose "
                    "(nobody on it) {:.0f}%",
                    share(homeTicks), share(awayTicks), share(looseTicks)),
        std::format("The ball changed sides {} times", changes),
        std::format("Passes completed: home {} of {}, away {} of {}",
                    homeCompleted, homePasses, awayCompleted, awayPasses),
        stoppedBy.empty() ? std::string("Play never stopped")
                          : std::format("Play stopped by a {}", stoppedBy)};
    // What this rung is there to show (T6b): measures, not targets.
    const int r = rung();
    const auto seconds = [](const std::optional<double>& at)
    { return at ? std::format("{:.1f} s", *at) : std::string("never"); };
    const auto shareOf = [inPlay](int ticks)
    { return inPlay == 0 ? 0.0 : 100.0 * ticks / inPlay; };
    if (r == 0)
    {
      lines.push_back(
          std::format("WATCH  first on the ball at {}, first shot at {}",
                      seconds(reachedAt), seconds(firstShotAt)));
    }
    if (r >= 1)
    {
      lines.push_back(
          std::format("WATCH  {} spells on the ball, the longest {:.1f} s; "
                      "{} clean tackles",
                      spells, longestSpell, tackles));
    }
    if (r >= 2)
    {
      lines.push_back(std::format(
          "WATCH  passes a spell {:.2f}; two or more within 3 m of the ball: "
          "home "
          "{:.0f}%, away {:.0f}%; one on the carrier with another goal-side of "
          "the next "
          "attacker: {:.0f}% of defending",
          spells == 0 ? 0.0
                      : static_cast<double>(homePasses + awayPasses) / spells,
          shareOf(homeSwarmTicks), shareOf(awaySwarmTicks),
          defendingTicks == 0 ? 0.0
                              : 100.0 * pressCoverTicks / defendingTicks));
    }
    if (r >= 3)
    {
      lines.push_back(
          std::format("WATCH  the carrier's team-mates in passing range: "
                      "{:.2f} on average",
                      carrierTicks == 0 ? 0.0 : openSum / carrierTicks));
    }
    if (r >= 2)
    {
      lines.push_back(
          std::format("WATCH  each side's spread (average from its centre): "
                      "{:.1f} m",
                      spreadSamples == 0 ? 0.0 : spreadSum / spreadSamples));
    }
    // What the players' brains had them doing, as a share of player time in
    // play (the most first): how much of it holds a shape, for one.
    if (playerTicks > 0)
    {
      lines.push_back("WATCH  the players' intents: " + intentDescription());
    }
    if (!stoppedBy.empty() ||
        elapsed >= secondsSetting - END_TIME_TOLERANCE_SECONDS)
    {
      lines.push_back(std::format(
          "OUTCOME  {}-{}, {} shots, {} changes of side, {} passes", homeGoals,
          awayGoals, homeShots + awayShots, changes, homePasses + awayPasses));
    }
    return lines;
  }

  [[nodiscard]] std::vector<DrillMetric> metrics() const override
  {
    if (elapsed < secondsSetting - END_TIME_TOLERANCE_SECONDS)
    {
      return {};
    }
    const int inPlay = std::max(1, inPlayTicks);
    return {
        {.name = "Home goals", .value = static_cast<double>(homeGoals)},
        {.name = "Away goals", .value = static_cast<double>(awayGoals)},
        {.name = "Own goals", .value = static_cast<double>(ownGoals)},
        {.name = "Shots", .value = static_cast<double>(homeShots + awayShots)},
        {.name = "Home possession (%)", .value = 100.0 * homeTicks / inPlay},
        {.name = "Ball loose (%)", .value = 100.0 * looseTicks / inPlay},
        {.name = "Changes of side", .value = static_cast<double>(changes)},
        {.name = "Passes",
         .value = static_cast<double>(homePasses + awayPasses)},
        {.name = "Passes completed",
         .value = static_cast<double>(homeCompleted + awayCompleted)},
        // The rung measures (T6b).
        {.name = "First on the ball (s)", .value = reachedAt.value_or(-1.0)},
        {.name = "First shot (s)", .value = firstShotAt.value_or(-1.0)},
        {.name = "Spells", .value = static_cast<double>(spells)},
        {.name = "Longest spell (s)", .value = longestSpell},
        {.name = "Clean tackles", .value = static_cast<double>(tackles)},
        {.name = "Swarming home (%)", .value = 100.0 * homeSwarmTicks / inPlay},
        {.name = "Swarming away (%)", .value = 100.0 * awaySwarmTicks / inPlay},
        {.name = "Press and cover (%)",
         .value = defendingTicks == 0
                      ? 0.0
                      : 100.0 * pressCoverTicks / defendingTicks},
        {.name = "Team-mates in range",
         .value = carrierTicks == 0 ? 0.0 : openSum / carrierTicks},
        {.name = "Spread (m)",
         .value = spreadSamples == 0 ? 0.0 : spreadSum / spreadSamples},
        // Holding or recovering his shape (anchored on his start spot in a
        // drill), as a share of player time in play.
        {.name = "Holding shape (%)",
         .value = playerTicks == 0 ? 0.0
                                   : 100.0 *
                                         (intentTicks[static_cast<std::size_t>(
                                              PlayerIntent::HOLD_SHAPE)] +
                                          intentTicks[static_cast<std::size_t>(
                                              PlayerIntent::RECOVER_SHAPE)]) /
                                         playerTicks}};
  }

 private:
  [[nodiscard]] int rung() const
  {
    return static_cast<int>(std::round(rungChoice));
  }

 public:
  [[nodiscard]] bool layers() const override
  {
    return layersChoice >= CHOICE_MIDPOINT;
  }

 private:
  /** Players a side: 1 for the first two rungs, then 2, 3 or N. */
  [[nodiscard]] int perSide() const
  {
    const int r = rung();
    if (r <= 1)
    {
      return 1;
    }
    if (r == 4)
    {
      return static_cast<int>(std::round(nSetting));
    }
    return r;
  }

  [[nodiscard]] static float rungDistance(PitchPoint a, PitchPoint b)
  {
    return std::hypot(a.x - b.x, a.y - b.y);
  }

  [[nodiscard]] static const MatchPlayer* nearestRungPlayer(
      const std::vector<const MatchPlayer*>& players, PitchPoint target,
      const MatchPlayer* excluded = nullptr)
  {
    const MatchPlayer* nearest = nullptr;
    for (const MatchPlayer* player : players)
    {
      if (player != excluded &&
          (nearest == nullptr || rungDistance(player->position, target) <
                                     rungDistance(nearest->position, target)))
      {
        nearest = player;
      }
    }
    return nearest;
  }

  void measureSpacing(const std::vector<const MatchPlayer*>& home,
                      const std::vector<const MatchPlayer*>& away,
                      PitchPoint ball)
  {
    constexpr float SWARM_METRES = 3.0F;
    // Swarming: two or more of a side within 3 m of the ball.
    for (const bool isHome : {true, false})
    {
      const auto& mine = isHome ? home : away;
      int near = 0;
      for (const MatchPlayer* player : mine)
      {
        if (rungDistance(player->position, ball) <= SWARM_METRES)
        {
          ++near;
        }
      }
      if (mine.size() >= 2 && near >= 2)
      {
        ++(isHome ? homeSwarmTicks : awaySwarmTicks);
      }
      // Spread: the side's average distance from its own centre.
      if (mine.size() >= 2)
      {
        float cx = 0.0F;
        float cy = 0.0F;
        for (const MatchPlayer* player : mine)
        {
          cx += player->position.x;
          cy += player->position.y;
        }
        cx /= static_cast<float>(mine.size());
        cy /= static_cast<float>(mine.size());
        float sum = 0.0F;
        for (const MatchPlayer* player : mine)
        {
          sum += rungDistance(player->position, PitchPoint{cx, cy});
        }
        spreadSum += sum / static_cast<float>(mine.size());
        ++spreadSamples;
      }
    }
  }

  void measureDefending(const MatchEngine& engine,
                        const std::vector<const MatchPlayer*>& home,
                        const std::vector<const MatchPlayer*>& away,
                        const MatchPlayer* carrier)
  {
    constexpr float ENGAGE_METRES = 3.0F;
    const MatchPitch& box = engine.getPitch();
    const PitchPoint ball = engine.getBall().position;
    // Defending in twos: with the other side on the ball, one defender on
    // the carrier (the nearest, within 3 m) while another stays goal-side
    // of the next attacker (nearer their own goal than he is).
    if ((carrier != nullptr) && home.size() >= 2 && away.size() >= 2)
    {
      const bool attackersHome = carrier->isHomeTeam;
      const auto& attackers = attackersHome ? home : away;
      const auto& defenders = attackersHome ? away : home;
      const PitchPoint ownGoal{attackersHome ? box.length : 0.0F,
                               PITCH_MIDPOINT_SHARE * box.width};
      const MatchPlayer* presser =
          nearestRungPlayer(defenders, carrier->position);
      const MatchPlayer* next = nearestRungPlayer(attackers, ball, carrier);
      if (presser != nullptr && next != nullptr)
      {
        const MatchPlayer* cover =
            nearestRungPlayer(defenders, next->position, presser);
        ++defendingTicks;
        if (rungDistance(presser->position, carrier->position) <=
                ENGAGE_METRES &&
            cover != nullptr &&
            rungDistance(cover->position, ownGoal) <
                rungDistance(next->position, ownGoal))
        {
          ++pressCoverTicks;
        }
      }
    }
  }

  void measureOptions(const std::vector<const MatchPlayer*>& home,
                      const std::vector<const MatchPlayer*>& away,
                      const MatchPlayer* carrier)
  {
    // Open options: the carrier's team-mates within passing range (not too
    // close or far for the brain to look at them).
    if (carrier != nullptr)
    {
      const auto& mates = carrier->isHomeTeam ? home : away;
      int open = 0;
      for (const MatchPlayer* mate : mates)
      {
        if (mate == carrier)
        {
          continue;
        }
        const float metres = rungDistance(mate->position, carrier->position);
        if (metres >= MatchTuning::Passing::MIN_DISTANCE_METRES &&
            metres <= MatchTuning::Passing::MAX_DISTANCE_METRES)
        {
          ++open;
        }
      }
      openSum += open;
      ++carrierTicks;
    }
  }

  /** One in-play step of the rung's measures (plan section 22, point 7). */
  void measureRung(const MatchEngine& engine)
  {
    const PitchPoint ball = engine.getBall().position;
    std::vector<const MatchPlayer*> home;
    std::vector<const MatchPlayer*> away;
    for (const PlayerID id : ids_)
    {
      if (const MatchPlayer* slot = slotOf(engine, id))
      {
        (std::ranges::find(homeIds_, id) != homeIds_.end() ? home : away)
            .push_back(slot);
      }
    }
    // The carrier: holding the ball, or dribbling it.
    const Player* holder = engine.getPossessionAttachment();
    if (holder == nullptr)
    {
      holder = engine.getBall().meetFor;
    }
    const MatchPlayer* carrier = nullptr;
    for (const MatchPlayer* player : home)
    {
      if (player->player == holder)
      {
        carrier = player;
      }
    }
    for (const MatchPlayer* player : away)
    {
      if (player->player == holder)
      {
        carrier = player;
      }
    }
    for (const auto* side : {&home, &away})
    {
      for (const MatchPlayer* player : *side)
      {
        ++intentTicks.at(static_cast<std::size_t>(player->intent));
        ++playerTicks;
      }
    }
    const double now = elapsed;

    // 1 player: when he first had the ball, and first shot.
    if ((carrier != nullptr) && !reachedAt)
    {
      reachedAt = now;
    }
    if (!firstShotAt && homeShots + awayShots > 0)
    {
      firstShotAt = now;
    }

    // Spells: a side's unbroken time in possession; the longest.
    const std::optional<bool> team =
        engine.getPossessionHistory().lastControlledTeamHome;
    const int side = teamSideNumber(team);
    if (side != 0 && side != spellSide)
    {
      spellSide = side;
      spellStart = now;
      ++spells;
    }
    if (side != 0)
    {
      longestSpell = std::max(longestSpell, now - spellStart);
    }

    measureSpacing(home, away, ball);

    measureDefending(engine, home, away, carrier);

    measureOptions(home, away, carrier);
  }

  float rungChoice = 1.0F;
  float nSetting = DEFAULT_N_SETTING;
  float areaSetting = DEFAULT_AREA_SETTING;
  float secondsSetting = DEFAULT_SECONDS_SETTING;
  float goalsChoice = DEFAULT_GOALS_CHOICE;
  /** 0 off (the default: the neutral ladder), 1 on. */
  float layersChoice = 0.0F;
  std::vector<PlayerID> ids_;
  std::vector<PlayerID> homeIds_;
  MatchStats statsAtStart{};
  std::size_t eventsSeen = 0;
  double elapsed = 0.0;
  int homeGoals = 0;
  int awayGoals = 0;
  int homeShots = 0;
  int awayShots = 0;
  int homePasses = 0;
  int awayPasses = 0;
  int homeCompleted = 0;
  int awayCompleted = 0;
  int homeTicks = 0;
  int awayTicks = 0;
  int looseTicks = 0;
  int inPlayTicks = 0;
  int ownGoals = 0;
  int changes = 0;
  int lastSide = 0;
  // The rung measures (T6b).
  std::optional<double> reachedAt;
  std::optional<double> firstShotAt;
  int spellSide = 0;
  double spellStart = 0.0;
  int spells = 0;
  double longestSpell = 0.0;
  int tackles = 0;
  int homeSwarmTicks = 0;
  int awaySwarmTicks = 0;
  double spreadSum = 0.0;
  int spreadSamples = 0;
  int defendingTicks = 0;
  int pressCoverTicks = 0;
  double openSum = 0.0;
  /** Player ticks in play by intent, and in all. */
  std::array<int, PLAYER_INTENT_COUNT> intentTicks{};
  int playerTicks = 0;
  int carrierTicks = 0;
  std::string stoppedBy;
};

std::vector<std::unique_ptr<Drill>> makeDrills()
{
  std::vector<std::unique_ptr<Drill>> drills;
  const auto add = [&](DrillCategory category, std::unique_ptr<Drill> drill)
  {
    drill->category = category;
    drills.push_back(std::move(drill));
  };
  add(DrillCategory::Movement, std::make_unique<SprintDrill>());
  add(DrillCategory::Movement, std::make_unique<TurnAndRunDrill>());
  add(DrillCategory::Movement, std::make_unique<TurnDrill>());
  add(DrillCategory::Movement, std::make_unique<SlalomDrill>());
  add(DrillCategory::Movement, std::make_unique<RepeatedSprintsDrill>());
  add(DrillCategory::Movement, std::make_unique<FlagChaseDrill>());
  add(DrillCategory::Movement, std::make_unique<OvertakeDrill>());
  add(DrillCategory::Duels, std::make_unique<LooseBallDrill>());
  add(DrillCategory::Duels, std::make_unique<TackleDrill>());
  add(DrillCategory::Duels, std::make_unique<CollisionDrill>());
  add(DrillCategory::Shooting, std::make_unique<ShotDrill>());
  add(DrillCategory::Duels, std::make_unique<ShotBlockDrill>());
  add(DrillCategory::Shooting, std::make_unique<ShotTargetDrill>());
  add(DrillCategory::Control, std::make_unique<FirstTouchDrill>());
  add(DrillCategory::Passing, std::make_unique<PassDrill>());
  add(DrillCategory::Passing, std::make_unique<FirstTimePassDrill>());
  add(DrillCategory::Passing, std::make_unique<CorridorPassDrill>());
  add(DrillCategory::Passing, std::make_unique<BullseyePassDrill>());
  add(DrillCategory::Passing, std::make_unique<DrivenCorridorDrill>());
  add(DrillCategory::Passing, std::make_unique<ChipTargetDrill>());
  add(DrillCategory::Goalkeeping, std::make_unique<KeeperThrowDrill>());
  add(DrillCategory::Aerial, std::make_unique<HeaderTargetDrill>());
  add(DrillCategory::Control, std::make_unique<BodyControlDrill>(false));
  add(DrillCategory::Control, std::make_unique<BodyControlDrill>(true));
  add(DrillCategory::Aerial, std::make_unique<LooseHighBallDrill>());
  add(DrillCategory::Duels, std::make_unique<WalledBoxDrill>());
  add(DrillCategory::Duels, std::make_unique<SmallSidedGameDrill>());
  return drills;
}

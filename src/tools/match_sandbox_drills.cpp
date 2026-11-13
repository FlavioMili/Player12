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

namespace
{
constexpr float LENGTH = MatchTuning::Pitch::MATCH_LENGTH_METRES;
constexpr float WIDTH = MatchTuning::Pitch::MATCH_WIDTH_METRES;
constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
constexpr float PI = std::numbers::pi_v<float>;
/** How far past a point a runner aims to run through it, in metres. */
constexpr float RUN_THROUGH_METRES = 8.0f;

const MatchPlayer* slotOf(const MatchEngine& engine, PlayerID id)
{
  for (const MatchPlayer& slot : engine.getPlayers())
    if (slot.player && slot.player->getId() == id) return &slot;
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
PitchPoint ballOutOfTheWay() { return at(0.97f, 0.97f); }
Vector2F toMetres(PitchPoint pitch) { return {PITCH.metresX(pitch), PITCH.metresY(pitch)}; }
Vector2F toMetres(PitchOffset step)
{
  return {PITCH.metresX(step.x), PITCH.metresY(step.y)};
}
PitchPoint toPitch(Vector2F metres)
{
  return PitchPoint{PITCH.fromMetresX(metres.x), PITCH.fromMetresY(metres.y)};
}
float metresBetween(PitchPoint from, PitchPoint to)
{
  return std::hypot(PITCH.metresX(to.x - from.x), PITCH.metresY(to.y - from.y));
}
/** `from` moved `metres` along the unit direction `direction` (metres). */
PitchPoint along(PitchPoint from, Vector2F direction, float metres)
{
  return PitchPoint{from.x + PITCH.fromMetresX(direction.x * metres),
                    from.y + PITCH.fromMetresY(direction.y * metres)};
}
/** Unit direction from one pitch point to another, in metres. */
Vector2F directionBetween(PitchPoint from, PitchPoint to)
{
  const Vector2F offset{PITCH.metresX(to.x - from.x), PITCH.metresY(to.y - from.y)};
  const float size = std::hypot(offset.x, offset.y);
  if (size < 1e-6f) return {1.0f, 0.0f};
  return {offset.x / size, offset.y / size};
}
/** A real direction on the pitch (radians, in metres) as a facing angle,
 * which is kept in normalised pitch space (the pitch is not square, so
 * only directions along its axes keep their angle). */
float facingAngleOf(float radians)
{
  return std::atan2(std::sin(radians) / WIDTH, std::cos(radians) / LENGTH);
}
Vector2F rotated(Vector2F direction, float radians)
{
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  return Vector2F{direction.x * c - direction.y * s, direction.x * s + direction.y * c};
}
/** Angle between two unit directions, in degrees (0-180). */
float turnDegrees(Vector2F before, Vector2F after)
{
  const float dot =
      std::clamp(before.x * after.x + before.y * after.y, -1.0f, 1.0f);
  return std::acos(dot) * 180.0f / PI;
}

/**
 * Share [0, 1] of the move from `from` to `to` at which a runner enters the
 * circle of `radius` metres round `centre`, if he does during that move.
 */
std::optional<float> entryShare(PitchPoint from, PitchPoint to, PitchPoint centre,
                                float radius)
{
  const Vector2F start = toMetres(from - centre);
  const Vector2F move = toMetres(to - from);
  const float c = start.x * start.x + start.y * start.y - radius * radius;
  if (c <= 0.0f) return 0.0f;
  const float a = move.x * move.x + move.y * move.y;
  if (a < 1e-9f) return std::nullopt;
  const float b = 2.0f * (start.x * move.x + start.y * move.y);
  const float discriminant = b * b - 4.0f * a * c;
  if (discriminant < 0.0f) return std::nullopt;
  const float share = (-b - std::sqrt(discriminant)) / (2.0f * a);
  if (share < 0.0f || share > 1.0f) return std::nullopt;
  return share;
}

/**
 * Share [0, 1] of the move at which a runner crosses the line through
 * `point` square to `direction` (going that way), if he does.
 */
std::optional<float> crossingShare(PitchPoint from, PitchPoint to, PitchPoint point,
                                   Vector2F direction)
{
  const Vector2F a = toMetres(from - point);
  const Vector2F b = toMetres(to - point);
  const float before = a.x * direction.x + a.y * direction.y;
  const float after = b.x * direction.x + b.y * direction.y;
  if (before >= 0.0f || after < 0.0f) return std::nullopt;
  return -before / (after - before);
}

/** A slider setting (see DrillParameter). */
DrillParameter setting(const char* name, float* value, float min, float max,
                       bool integer, const char* tooltip = nullptr)
{
  DrillParameter parameter;
  parameter.name = name;
  parameter.value = value;
  parameter.min = min;
  parameter.max = max;
  parameter.integer = integer;
  parameter.tooltip = tooltip;
  return parameter;
}

/** Trail colour by speed: blue when slow, yellow, red flat out. */
ImU32 speedColour(float speed)
{
  const float share = std::clamp(speed / 9.0f, 0.0f, 1.0f);
  if (share < 0.5f)
  {
    const float t = share * 2.0f;
    return IM_COL32(static_cast<int>(60 + 195 * t), static_cast<int>(140 + 70 * t),
                    static_cast<int>(255 - 195 * t), 220);
  }
  const float t = (share - 0.5f) * 2.0f;
  return IM_COL32(255, static_cast<int>(210 - 150 * t), 60, 220);
}

/** Attributes shared by the movement drills. */
struct RunnerAttributes
{
  float pace = 70.0f;
  float physicality = 70.0f;
  float stamina = 70.0f;
  /** The Agility and Acceleration derived stats, set directly (not from
   * their recipes). */
  float agility = 70.0f;
  float acceleration = 70.0f;
  /** Fuel (condition) at the start, 0-100. */
  float condition = 100.0f;

  void addTo(std::vector<DrillParameter>& parameters)
  {
    parameters.push_back(setting("Pace", &pace, 1.0f, 100.0f, true));
    parameters.push_back(setting("Physicality", &physicality, 1.0f, 100.0f, true,
                          "Duels; braking in the Aardvark body. (Acceleration "
                          "is its own setting here.)"));
    parameters.push_back(setting("Stamina", &stamina, 1.0f, 100.0f, true,
                          "Endurance: how fast condition and the sprint "
                          "reserve drain and recover."));
    parameters.push_back(setting("Agility", &agility, 1.0f, 100.0f, true,
                          "Grip: how sharply he turns at speed (0.9x at 45, "
                          "1.1x at 85), how fast his body turns, his braking "
                          "(6 m/s² at 1, 8 at 99), and up to +0.2 m/s² "
                          "acceleration."));
    parameters.push_back(setting("Acceleration", &acceleration, 1.0f, 100.0f,
                          true,
                          "His push from a standstill (5.5 m/s² at 1, 9.5 at "
                          "99, plus Agility's boost), and up to +0.1 m/s² "
                          "braking."));
    parameters.push_back(setting("Condition", &condition, 0.0f, 100.0f, true,
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
    if (condition < 100.0f) text += std::format(" · Condition {:.0f}", condition);
    return text;
  }
};

/** Attributes of a runner with the ball (the movement drills' With the ball setting). */
struct DribblerAttributes
{
  float dribbling = 70.0f;
  float passing = 70.0f;
  /** The First Touch derived stat, set directly (not from its recipe). */
  float firstTouch = 70.0f;

  void addTo(std::vector<DrillParameter>& parameters)
  {
    parameters.push_back(setting("Dribbling", &dribbling, 1.0f, 100.0f, true,
                          "With the ball: touch length and rhythm, heavy "
                          "touches."));
    parameters.push_back(setting("Passing", &passing, 1.0f, 100.0f, true,
                          "With the ball."));
    parameters.push_back(setting("First touch", &firstTouch, 1.0f, 100.0f, true,
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
  static constexpr float PLACED_METRES = 0.05f;

  int lost = 0;
  int placed = 0;
  float largestPlacement = 0.0f;
  int stepsWith = 0;
  int stepsWithout = 0;
  double gapSum = 0.0;
  float largestGap = 0.0f;
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
    if (had && !has) ++lost;
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
          moved - std::max(std::hypot(lastVelocity.x, lastVelocity.y),
                           std::hypot(ball.velocity.x, ball.velocity.y)) *
                      STEP;
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

  std::vector<DrillPlayerSpec> players() const override
  {
    std::map<std::string, float> stats = runner.stats();
    std::map<DerivedStat, float> derived = runner.derived();
    stats["Dribbling"] = dribbler.dribbling;
    stats["Passing"] = dribbler.passing;
    derived[DerivedStat::FIRST_TOUCH] = dribbler.firstTouch;
    DrillPlayerSpec spec{"Runner", PlayerRole::CM, true, stats, 180, derived};
    spec.condition = runner.condition;
    return {spec};
  }

  bool humanControl() const override { return pathSetting() == YOU; }

 protected:
  static constexpr std::array<const char*, 2> PATH_NAMES{"AI", "You"};

  [[nodiscard]] Path pathSetting() const
  {
    return static_cast<Path>(std::clamp(static_cast<int>(pathChoice), 0, 1));
  }

  /** "Who runs" and the runner's attributes. */
  void addRunnerParameters(std::vector<DrillParameter>& parameters)
  {
    DrillParameter who = setting("Who runs", &pathChoice, 0.0f, 1.0f, true,
                       "AI: the drill plans the runner's run as his side's "
                       "brain would (where he is going, and the waypoints on "
                       "the way).\nYou: you run (WASD or arrows, Shift to "
                       "sprint, or a gamepad).");
    who.choices = {PATH_NAMES[0], PATH_NAMES[1]};
    parameters.push_back(who);
    DrillParameter ball = setting("With the ball", &ballChoice, 0.0f, 1.0f, true,
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
    withBall = ballChoice >= 0.5f;
    MatchScenario scenario;
    scenario.players.push_back({id, at, false});
    scenario.ballPosition = withBall ? at : ballOutOfTheWay();
    if (withBall) scenario.carrierId = id;
    engine.startDrill(scenario);
    if (path != AI) engine.setControlledPlayer(id);
    // He dribbles, and does nothing else with the ball.
    if (withBall) engine.keepBall(id, true);
    aiGoal.reset();
    looseBall.reset();
    ballWatch.reset();
    trail.clear();
    trail.push_back({at, 0.0f});
    distanceRun = 0.0f;
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
               std::vector<PitchPoint> route = {}, float apexOffset = 0.0f,
               float outwardRoom = 0.0f,
               std::optional<float> facing = std::nullopt, bool atSpeed = false)
  {
    if (path != AI) return;
    const auto same = [](PitchPoint a, PitchPoint b)
    { return a.x == b.x && a.y == b.y; };
    if (aiGoal && same(aiGoal->at, at) && aiGoal->urgent == urgent &&
        std::ranges::equal(aiGoal->route, route, same) &&
        aiGoal->apexOffset == apexOffset && aiGoal->outwardRoom == outwardRoom &&
        aiGoal->facing == facing && aiGoal->atSpeed == atSpeed)
      return;
    aiGoal = Goal{at, urgent, std::move(route), apexOffset, outwardRoom, facing,
                  atSpeed};
    sendPlan(engine);
  }

  void sendPlan(MatchEngine& engine)
  {
    if (aiGoal && !looseBall)
      engine.setDrillTarget(id, aiGoal->at, aiGoal->urgent, aiGoal->route,
                            aiGoal->apexOffset, aiGoal->outwardRoom,
                            aiGoal->facing, aiGoal->atSpeed);
  }

  /** The runner after a step (nullptr if gone, or the ball he dribbles went
   * out of play); keeps the trail, watches the ball and sends him after it
   * when he has lost it. */
  const MatchPlayer* track(MatchEngine& engine)
  {
    const MatchPlayer* player = slotOf(engine, id);
    if (!player) return nullptr;
    // Nobody takes the restart in a drill: the run ends there.
    if (withBall && engine.getState() != MatchState::PLAYING)
    {
      ballWatch.out = true;
      return nullptr;
    }
    distanceRun += metresBetween(lastPosition, player->position);
    lastPosition = player->position;
    if (engine.getSimulatedSteps() % 2 == 0)
      trail.push_back({player->position, speedOf(*player)});
    if (withBall)
    {
      ballWatch.observe(engine, *player);
      const MatchBall& ball = engine.getBall();
      if (!engine.getPossessionAttachment())
      {
        looseBall = ball.position;
        if (path == AI) engine.setDrillTarget(id, ball.position, true);
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
    if (!withBall) return;
    lines.push_back(std::format(
        "Ball: {} touches (met {:.2f} m from him) · gap mean {:.2f} m, max "
        "{:.2f} m · lost {} ({:.1f} s without)",
        ballWatch.touches, ballWatch.meanContactGap(), ballWatch.meanGap(),
        ballWatch.largestGap,
        ballWatch.lost, static_cast<float>(ballWatch.stepsWithout) * STEP));
    if (ballWatch.failedTouches > 0)
      lines.push_back(std::format("Touches that went wrong: {}", ballWatch.failedTouches));
    lines.push_back(std::format("Ball placed, not moved: {} times, largest {:.2f} m",
                                ballWatch.placed, ballWatch.largestPlacement));
    if (ballWatch.out) lines.push_back("The ball went out of play: run over.");
  }

  /** The ball's metrics of a finished run (none without the ball). */
  void addBallMetrics(std::vector<DrillMetric>& out) const
  {
    if (!withBall) return;
    out.push_back({"Touches", static_cast<double>(ballWatch.touches)});
    out.push_back({"Metres per touch",
                   ballWatch.touches > 0
                       ? distanceRun / static_cast<double>(ballWatch.touches)
                       : 0.0});
    out.push_back({"Contact gap (m)", ballWatch.meanContactGap()});
    out.push_back({"Failed touches", static_cast<double>(ballWatch.failedTouches)});
    out.push_back({"Mean gap (m)", ballWatch.meanGap()});
    out.push_back({"Max gap (m)", ballWatch.largestGap});
    out.push_back({"Ball lost", static_cast<double>(ballWatch.lost)});
    out.push_back({"Without the ball (s)",
                   static_cast<double>(ballWatch.stepsWithout) * STEP});
    out.push_back({"Ball placed", static_cast<double>(ballWatch.placed)});
    out.push_back({"Largest placement (m)", ballWatch.largestPlacement});
  }

  void drawTrail(const IMatchRenderer& renderer, ImDrawList& draw) const
  {
    const float scale = Theme::scale();
    for (std::size_t point = 1; point < trail.size(); ++point)
      DrillDraw::line(renderer, draw, trail[point - 1].at, trail[point].at,
                      speedColour(trail[point].speed), 2.5f * scale);
  }

  [[nodiscard]] std::string runnerLine() const
  {
    if (withBall)
      return std::format("{} · with the ball · {} · {}", PATH_NAMES[path],
                         runner.describe(), dribbler.describe());
    return std::format("{} · {}", PATH_NAMES[path], runner.describe());
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
    float apexOffset = 0.0f;
    float outwardRoom = 0.0f;
    std::optional<float> facing;
    /** Reached at full speed (a run through a finish). */
    bool atSpeed = false;
  };
  struct TrailPoint
  {
    PitchPoint at;
    float speed = 0.0f;
  };

  float pathChoice = AI;
  float ballChoice = 0.0f;
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
  float distanceRun = 0.0f;
  PitchPoint lastPosition{};
};

// --- Sprint ----------------------------------------------------------------

/**
 * Sprint: one runner, flat out along a straight line, from standing or with
 * a run-up. Times every 10 m, the finish, top speed and the reaction delay.
 */
class SprintDrill final : public MovementDrill
{
 public:
  const char* name() const override { return "Sprint"; }
  const char* summary() const override
  {
    return "One runner sprints a straight line, from standing or with a "
           "run-up. Splits every 10 m, finish time, top speed, reaction.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Distance (m)", &distance, 10.0f, 80.0f, true));
    list.push_back(setting("Run-up (m)", &runUp, 0.0f, 20.0f, true,
                    "0: a standing start, timed from the start signal. "
                    "Otherwise a flying start, timed from the line."));
    DrillParameter runs = setting("Runs", &runsChoice, 0.0f, 7.0f, true,
                                  "The way his body faces as he goes (the AI "
                                  "runner; facing another way than he runs is "
                                  "the Binturong body's). Backward and "
                                  "sideways have only a walk and a jog. He "
                                  "turns his body first, gliding as he does.");
    runs.choices = {"Forward", "Backward", "Side-step left", "Side-step right", "Diagonal forward-left", "Diagonal forward-right", "Diagonal back-left", "Diagonal back-right"};
    list.push_back(runs);
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    metres = static_cast<int>(distance);
    runUpMetres = static_cast<int>(runUp);
    startX = PITCH.atX(START_LINE);
    finishX = startX + PITCH.fromMetresX(static_cast<float>(metres));
    const float from = std::max(PITCH.atX(0.02f),
                                startX - PITCH.fromMetresX(static_cast<float>(runUpMetres)));
    runs = static_cast<int>(runsChoice + 0.5f);
    startRunner(engine, ids, PitchPoint{from, PITCH.atY(0.5f)});
    // Through the line: the target lies beyond the finish. His body faces
    // down the track (0), back up it, across it (his left or right to the
    // run), or at 45 deg on the diagonals (real angles, as facings).
    constexpr std::array<float, 8> FACINGS{0.0f,        PI,          0.5f * PI,
                                           -0.5f * PI,  0.25f * PI,  -0.25f * PI,
                                           0.75f * PI,  -0.75f * PI};
    steerTo(engine, PitchPoint{finishX + PITCH.fromMetresX(RUN_THROUGH_METRES), PITCH.atY(0.5f)}, true, {},
            0.0f, 0.0f,
            runs == 0 ? std::nullopt
                      : std::optional<float>(facingAngleOf(
                            FACINGS[static_cast<std::size_t>(runs)])),
            true);
    lastX = from;
    crossings.assign(static_cast<std::size_t>(metres / 10) + 1, std::nullopt);
    if (runUpMetres == 0) crossings[0] = 0.0;
    finishTime.reset();
    reaction.reset();
    topSpeed = 0.0f;
    topSpeedAt = 0.0f;
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (!player) return false;
    const double now = secondsOf(engine);
    const float x = player->position.x;
    const float speed = speedOf(*player);
    if (!reaction && speed > 0.1f) reaction = now - STEP;
    // A line is crossed between two steps: interpolate the moment.
    const auto crossed = [&](float lineX) -> std::optional<double>
    {
      if (lastX >= lineX || x < lineX || x <= lastX) return std::nullopt;
      return now - STEP + STEP * static_cast<double>((lineX - lastX) / (x - lastX));
    };
    for (std::size_t mark = 0; mark < crossings.size(); ++mark)
      if (!crossings[mark])
        crossings[mark] =
            crossed(startX + PITCH.fromMetresX(static_cast<float>(mark * 10)));
    if (!finishTime) finishTime = crossed(finishX);
    if (crossings[0] && x <= finishX && speed > topSpeed)
    {
      topSpeed = speed;
      topSpeedAt = PITCH.metresX(x - startX);
    }
    lastX = x;
    // Over half a second after the finish, or a minute at most.
    return !(finishTime && now > *finishTime + 0.5) && now < 60.0;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    const float half = PITCH.fromMetresY(4.0f);
    const float middle = PITCH.atY(0.5f);
    for (std::size_t mark = 0; mark < crossings.size(); ++mark)
    {
      const float x = startX + PITCH.fromMetresX(static_cast<float>(mark * 10));
      DrillDraw::line(renderer, draw, PitchPoint{x, middle - half}, PitchPoint{x, middle + half},
                      IM_COL32(255, 255, 255, 120), 1.5f * scale);
    }
    DrillDraw::line(renderer, draw, PitchPoint{startX, middle - half * 1.5f},
                    PitchPoint{startX, middle + half * 1.5f}, IM_COL32(90, 220, 120, 255),
                    3.0f * scale);
    DrillDraw::line(renderer, draw, PitchPoint{finishX, middle - half * 1.5f},
                    PitchPoint{finishX, middle + half * 1.5f}, IM_COL32(240, 80, 80, 255),
                    3.0f * scale);
    DrillDraw::label(renderer, draw, PitchPoint{startX, middle - half * 2.4f}, "start",
                     IM_COL32(90, 220, 120, 255));
    DrillDraw::label(renderer, draw, PitchPoint{finishX, middle - half * 2.4f},
                     std::format("{} m", metres).c_str(),
                     IM_COL32(240, 80, 80, 255));
    drawTrail(renderer, draw);
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format("{} m, {}", metres,
                                runUpMetres == 0
                                    ? std::string("standing start")
                                    : std::format("{} m run-up", runUpMetres)));
    lines.push_back(runnerLine());
    if (reaction)
      lines.push_back(std::format("First movement after {:.1f} s", *reaction));
    if (crossings.empty()) return lines;
    const double start = crossings[0].value_or(0.0);
    std::string splits;
    for (std::size_t mark = 1; mark < crossings.size(); ++mark)
      if (crossings[mark])
        splits += std::format("{} m {:.2f} s   ", mark * 10,
                              *crossings[mark] - start);
    if (!splits.empty()) lines.push_back(splits);
    if (finishTime)
      lines.push_back(std::format("FINISH  {:.2f} s", *finishTime - start));
    if (topSpeed > 0.0f)
      lines.push_back(std::format("Top speed {:.2f} m/s ({:.1f} km/h) at {:.0f} m",
                                  topSpeed, topSpeed * 3.6f, topSpeedAt));
    addBallLines(lines);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!finishTime || crossings.empty()) return {};
    const double start = crossings[0].value_or(0.0);
    std::vector<DrillMetric> out{{"Finish (s)", *finishTime - start}};
    for (std::size_t mark = 1; mark < crossings.size(); ++mark)
      if (crossings[mark] && mark * 10 < static_cast<std::size_t>(metres))
        out.push_back({std::format("{} m (s)", mark * 10), *crossings[mark] - start});
    out.push_back({"Top speed (m/s)", topSpeed});
    out.push_back({"Top speed at (m)", topSpeedAt});
    addBallMetrics(out);
    return out;
  }

 private:
  static constexpr float START_LINE = 0.25f;

  float distance = 30.0f;
  float runUp = 0.0f;
  float runsChoice = 0.0f;

  int metres = 30;
  int runUpMetres = 0;
  int runs = 0;
  float startX = START_LINE;
  float finishX = START_LINE;
  float lastX = 0.0f;
  std::vector<std::optional<double>> crossings;
  std::optional<double> finishTime;
  std::optional<double> reaction;
  float topSpeed = 0.0f;
  float topSpeedAt = 0.0f;
};

// --- Courses: points to reach in order ------------------------------------

/**
 * A course of points the runner reaches in turn (within an arrival radius).
 * The runner aims at each point itself (and turns there); the last one he
 * runs through. Times are interpolated within the step.
 */
class CourseDrill : public MovementDrill
{
 protected:
  /** Sets the course up: the runner at `start`, then `points` in order. */
  void beginCourse(MatchEngine& engine, const std::vector<PlayerID>& ids,
                   PitchPoint start, std::vector<PitchPoint> course)
  {
    startRunner(engine, ids, start);
    startPoint = start;
    points = std::move(course);
    reached.clear();
    finished = false;
    radius = std::max(0.3f, arrivalRadius);
    const MatchPlayer* player = slotOf(engine, id);
    advancesSeen = player ? player->routeAdvances : 0;
    movedOn = 0;
    planRun(engine);
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
    if (points.empty()) return;
    std::vector<PitchPoint> ahead = waypointsAhead();
    if (ahead.size() > 2) ahead.resize(2);
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
    return {points.begin() + static_cast<std::ptrdiff_t>(first), points.end() - 1};
  }

  /** His line through the waypoints: by default he cuts each corner by up
   * to half the radius that counts as reaching it, and the line is not
   * pushed out (step 3's line). */
  [[nodiscard]] virtual float apexOffset() const { return radius * 0.5f; }

  /** Share of the move from `from` to `to` at which he reaches point
   * `index` (not the finish), if he does: within the radius. */
  [[nodiscard]] virtual std::optional<float> reachShare(std::size_t index,
                                                        PitchPoint from,
                                                        PitchPoint to) const
  {
    return entryShare(from, to, points[index], radius);
  }
  [[nodiscard]] virtual float outwardRoom() const { return 0.0f; }

  /** Call after every step; true when the runner reached the next point. */
  bool advanceCourse(MatchEngine& engine, const MatchPlayer& player,
                     PitchPoint previous)
  {
    followPlan(engine, player);
    if (finished || reached.size() >= points.size()) return false;
    const std::size_t next = reached.size();
    const PitchPoint target = points[next];
    // The last point of a run-through course is a finish line across the
    // last leg; the others are reached within the radius.
    const auto share =
        next + 1 == points.size() && runThroughLast
            ? crossingShare(previous, player.position, target, finishDirection())
            : reachShare(next, previous, player.position);
    if (!share) return false;
    reached.push_back(secondsOf(engine) - STEP + STEP * static_cast<double>(*share));
    if (reached.size() >= points.size()) finished = true;
    return true;
  }

  /** Direction across which the finish line lies (and the run goes on
   * through it): along the last leg. */
  /** Where on the finish line he aims to cross it (it counts anywhere
   * along it): its middle, by default. */
  [[nodiscard]] virtual PitchPoint finishAim() const { return points.back(); }

  [[nodiscard]] virtual Vector2F finishDirection() const
  {
    if (points.empty()) return {1.0f, 0.0f};
    const PitchPoint from =
        points.size() == 1 ? startPoint : points[points.size() - 2];
    return directionBetween(from, points.back());
  }

  /** The finish line of a run-through course, 8 m wide. */
  void drawFinish(const IMatchRenderer& renderer, ImDrawList& draw) const
  {
    if (points.empty()) return;
    const Vector2F across = rotated(finishDirection(), PI * 0.5f);
    DrillDraw::line(renderer, draw, along(points.back(), across, -4.0f),
                    along(points.back(), across, 4.0f),
                    IM_COL32(240, 80, 80, 255), 3.0f * Theme::scale());
  }

  /** Straight-line length of leg `index` (from the start or the point before). */
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
    float total = 0.0f;
    for (std::size_t index = 0; index < points.size(); ++index)
      total += legMetres(index);
    return total;
  }

  float arrivalRadius = 1.0f;
  bool runThroughLast = true;

  PitchPoint startPoint{};
  std::vector<PitchPoint> points;
  std::vector<double> reached;
  bool finished = false;
  float radius = 1.0f;
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
 public:
  const char* name() const override { return "Turn"; }
  const char* summary() const override
  {
    return "Sprint to a cone, turn through 45-180° and sprint on to the "
           "finish. Times either side of the cone, the speed lost in the "
           "turn and how wide he swings. Run 0° for the straight "
           "comparison: the time lost is shown against it.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Approach (m)", &approach, 5.0f, 40.0f, true));
    DrillParameter angle = setting("Turn", &angleChoice, 0.0f, 4.0f, true);
    angle.choices = {"0° (straight)", "45°", "90°", "135°", "180°"};
    list.push_back(angle);
    list.push_back(setting("Exit (m)", &exit, 5.0f, 30.0f, true));
    list.push_back(setting("Arrival radius (m)", &arrivalRadius, 0.3f, 3.0f, false,
                    "How close to the cone counts as reaching it (so does "
                    "crossing the corner's middle line, on any line)."));
    list.push_back(setting("Apex offset (m)", &apexSetting, -2.0f, 2.0f, false,
                    "The AI runner's line: how far inside the cone the turn's "
                    "apex may pass (cutting the corner). 0 goes through the "
                    "cone; negative passes outside it."));
    list.push_back(setting("Room outside (m)", &roomSetting, 0.0f, 10.0f, false,
                    "How far out the AI runner's line may be pushed at the "
                    "corner to widen the turn."));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    degrees = static_cast<float>(static_cast<int>(angleChoice) * 45);
    approachMetres = approach;
    exitMetres = exit;
    apex = apexSetting;
    room = roomSetting;
    closest.reset();
    // Start on the left of the centre circle so a 180° turn stays on the
    // pitch; turns go towards the near touchline (+y).
    const PitchPoint start = at(0.3f, 0.35f);
    cone = along(start, {1.0f, 0.0f}, approachMetres);
    exitDirection = rotated({1.0f, 0.0f}, degrees * PI / 180.0f);
    finishPoint = along(cone, exitDirection, exitMetres);
    beginCourse(engine, ids, start, {cone, finishPoint});
    minSpeed.reset();
    entrySpeed = 0.0f;
    overshoot = 0.0f;
    previous = start;
    resultSaved = false;
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (!player) return false;
    const bool wasAtCone = !reached.empty();
    const float speed = speedOf(*player);
    advanceCourse(engine, *player, previous);
    if (!wasAtCone && !reached.empty()) entrySpeed = speed;
    // Where he passes the cone: his closest to it, inside the turn or out.
    if (!finished)
    {
      const Vector2F off = toMetres(player->position - cone);
      const float metres = std::hypot(off.x, off.y);
      if (!closest || metres < std::abs(*closest))
      {
        const Vector2F inward{exitDirection.x - 1.0f, exitDirection.y};
        closest = off.x * inward.x + off.y * inward.y >= 0.0f ? metres : -metres;
      }
    }
    if (!reached.empty() && !finished)
    {
      // Through the turn: the slowest moment and how wide he swings off the
      // line from the cone to the finish.
      if (!minSpeed || speed < *minSpeed) minSpeed = speed;
      const Vector2F off = toMetres(player->position - cone);
      overshoot = std::max(
          overshoot, std::abs(off.x * exitDirection.y - off.y * exitDirection.x));
    }
    previous = player->position;
    const double now = secondsOf(engine);
    if (finished && !resultSaved)
    {
      results[settingsKey()] = reached.back();
      resultSaved = true;
    }
    return !(finished && now > reached.back() + 0.5) && now < 60.0;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    drawTrail(renderer, draw);
    DrillDraw::marker(renderer, draw, startPoint, 4.0f * scale,
                      IM_COL32(90, 220, 120, 255), "start");
    DrillDraw::cone(renderer, draw, cone, IM_COL32(255, 140, 30, 255),
                    std::format("{:.0f}°", degrees).c_str());
    drawFinish(renderer, draw);
    DrillDraw::label(renderer, draw, finishPoint, "finish",
                     IM_COL32(240, 80, 80, 255));
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format("{:.0f} m, turn {:.0f}°, {:.0f} m", approachMetres,
                                degrees, exitMetres));
    lines.push_back(runnerLine());
    if (!reached.empty())
      lines.push_back(std::format("To the cone {:.2f} s (at {:.2f} m/s)",
                                  reached[0], entrySpeed));
    if (minSpeed && degrees > 0.0f)
      lines.push_back(std::format("Slowest in the turn {:.2f} m/s · swings wide "
                                  "{:.1f} m",
                                  *minSpeed, overshoot));
    if (closest && degrees > 0.0f)
      lines.push_back(std::format("Passed the cone {:.2f} m {}", std::abs(*closest),
                                  *closest >= 0.0f ? "inside" : "outside"));
    if (finished)
    {
      lines.push_back(std::format("Cone to finish {:.2f} s", legSeconds(1)));
      lines.push_back(std::format("TOTAL  {:.2f} s · {:.1f} m run", reached.back(),
                                  distanceRun));
      if (degrees > 0.0f)
      {
        const auto straight = results.find(settingsKey(0.0f));
        if (straight != results.end())
          lines.push_back(std::format("Time lost to the turn: {:+.2f} s (vs 0°)",
                                      reached.back() - straight->second));
        else
          lines.push_back("Run 0° with the same settings to see the time lost.");
      }
    }
    addBallLines(lines);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!finished || reached.size() < 2) return {};
    std::vector<DrillMetric> out{{"Total (s)", reached.back()},
                                 {"To the cone (s)", reached[0]},
                                 {"Cone to finish (s)", legSeconds(1)},
                                 {"Speed at the cone (m/s)", entrySpeed}};
    if (minSpeed && degrees > 0.0f)
    {
      out.push_back({"Slowest in the turn (m/s)", *minSpeed});
      out.push_back({"Swings wide (m)", overshoot});
      if (closest) out.push_back({"Passed the cone, inside + (m)", *closest});
    }
    out.push_back({"Distance run (m)", distanceRun});
    addBallMetrics(out);
    return out;
  }

 protected:
  [[nodiscard]] float apexOffset() const override { return apex; }
  [[nodiscard]] float outwardRoom() const override { return room; }

  /** The cone is reached within the radius, or on crossing the corner's
   * middle line (whatever line he takes; a turn straight back has none). */
  [[nodiscard]] std::optional<float> reachShare(std::size_t index, PitchPoint from,
                                                PitchPoint to) const override
  {
    std::optional<float> share = entryShare(from, to, points[index], radius);
    const Vector2F across{1.0f + exitDirection.x, exitDirection.y};
    const float size = std::hypot(across.x, across.y);
    if (index == 0 && size > 1e-3f)
      if (const auto crossed = crossingShare(
              from, to, cone, {across.x / size, across.y / size});
          crossed && (!share || *crossed < *share))
        share = crossed;
    return share;
  }

 private:
  [[nodiscard]] std::string settingsKey(std::optional<float> angle = {}) const
  {
    return std::format("{}|{:.0f}|{:.0f}|{:.0f}|{:.2f}|{:.2f}|{:.2f}|{}",
                       static_cast<int>(path), approachMetres,
                       angle.value_or(degrees), exitMetres, radius, apex, room,
                       runnerKey());
  }

  float approach = 20.0f;
  float angleChoice = 2.0f;
  float exit = 10.0f;
  float apexSetting = 0.5f;
  float roomSetting = 0.0f;
  float apex = 0.5f;
  float room = 0.0f;
  /** His closest to the cone (m), inside the turn + or outside -. */
  std::optional<float> closest;

  float degrees = 90.0f;
  float approachMetres = 20.0f;
  float exitMetres = 10.0f;
  PitchPoint cone{};
  Vector2F exitDirection{1.0f, 0.0f};
  PitchPoint finishPoint{};
  PitchPoint previous{};
  std::optional<float> minSpeed;
  float entrySpeed = 0.0f;
  float overshoot = 0.0f;
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
 public:
  const char* name() const override { return "Slalom"; }
  const char* summary() const override
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
    list.push_back(setting("Cones", &cones, 2.0f, 12.0f, true,
                    "How many cones, as many as fit: the course runs up the "
                    "pitch from 5 m in from one end to 5 m from the other, so "
                    "wide spacing takes fewer."));
    list.push_back(setting("Spacing (m)", &spacing, 2.0f, 15.0f, false));
    DrillParameter lineSetting = setting("Line off the cone (m)", &lineOffset, 0.0f, 3.0f, false,
                    "Where he aims to pass each cone: his centre this far "
                    "outside the cone's edge (at least 0.2 m, so his body "
                    "clears it). Orange: set by the course, 0.6 m on a "
                    "straight line of cones, out to 0.8 m as the weave gets "
                    "sharper (stagger up to the spacing), room for the ball, "
                    "which cuts the corner a little. Move it to set your own; "
                    "back to 0 to have it set by the course again. The gate is the whole of the "
                    "cone's correct side, from its edge, so going wider "
                    "still counts.");
    lineSetting.automatic = [this] { return courseLine(spacing, coneStagger); };
    list.push_back(lineSetting);
    list.push_back(setting("Cone stagger (m)", &coneStagger, 0.0f, 6.0f, false,
                    "Each cone pushed this far off the line, to the side it "
                    "is passed on (alternate cones opposite ways): 0 is a "
                    "straight line of cones, more is a wider weave."));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    gap = spacing;
    // As many cones as fit: the finish (a gap past the last cone) as far
    // in from the far end as the start is from this one.
    const int fit = static_cast<int>((LENGTH - 2.0f * END_ROOM_METRES) / gap) - 1;
    const int count = std::clamp(fit, 1, static_cast<int>(cones));
    conesSet = static_cast<int>(cones);
    stagger = coneStagger;
    line = lineOffset > 0.0f ? std::max(lineOffset, MIN_LINE_METRES)
                             : courseLine(gap, stagger);
    const PitchPoint start{PITCH.fromMetresX(END_ROOM_METRES), PITCH.atY(0.5f)};
    conePoints.clear();
    std::vector<PitchPoint> course;
    for (int cone = 0; cone < count; ++cone)
    {
      const PitchPoint at =
          along(along(start, {1.0f, 0.0f}, gap * static_cast<float>(cone + 1)),
                {0.0f, 1.0f}, sideOf(cone) * stagger);
      conePoints.push_back(at);
      // He heads for his line round the cone.
      course.push_back(
          along(at, {0.0f, 1.0f}, sideOf(cone) * (GATE_BUFFER_METRES + line)));
    }
    course.push_back(along(start, {1.0f, 0.0f}, gap * static_cast<float>(count + 1)));
    planAims(start, course);
    missed = 0;
    goingBack = false;
    backPending = false;
    previous = start;
    bodyThrough.reset();
    ballThrough.reset();
    beginCourse(engine, ids, start, std::move(course));
    // The cones are obstacles: he keeps the ball off them.
    std::vector<MatchEngine::Obstacle> obstacles;
    for (const PitchPoint& cone : conePoints)
      obstacles.push_back({cone, DrillDraw::CONE_RADIUS_METRES});
    engine.setObstacles(std::move(obstacles));
    previousBall = engine.getBall().position;
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (!player) return false;
    followPlan(engine, *player);
    holdBackPoint(engine, *player);
    if (!finished) passGates(engine, *player);
    previous = player->position;
    previousBall = engine.getBall().position;
    const double now = secondsOf(engine);
    return !(finished && now > reached.back() + 0.5) && now < 120.0;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    drawTrail(renderer, draw);
    DrillDraw::marker(renderer, draw, startPoint, 4.0f * scale,
                      IM_COL32(90, 220, 120, 255), "start");
    for (std::size_t cone = 0; cone < conePoints.size(); ++cone)
    {
      // The gate, from a body's width out from its cone (open beyond what
      // is drawn), and the point he aims for: the one due bright, red while
      // he goes back for it (missed), with the point behind it he goes to.
      const bool passed = cone < reached.size();
      const bool due = cone == reached.size();
      const ImU32 colour = passed            ? IM_COL32(120, 120, 120, 200)
                           : due && goingBack ? IM_COL32(240, 80, 80, 255)
                           : due              ? IM_COL32(255, 255, 255, 240)
                                              : IM_COL32(255, 255, 255, 120);
      DrillDraw::line(renderer, draw, gateEdge(cone, 0.0f),
                      gateEdge(cone, GATE_DRAWN_METRES), colour, 2.0f * scale);
      DrillDraw::marker(renderer, draw, aims[cone], 2.5f * scale, colour);
      if (due && goingBack && backPending)
        DrillDraw::marker(renderer, draw, backPoint(cone), 2.5f * scale, colour,
                          "back");
      DrillDraw::cone(renderer, draw, conePoints[cone],
                        IM_COL32(255, 140, 30, 255));
    }
    drawFinish(renderer, draw);
    DrillDraw::label(renderer, draw, points.back(), "finish",
                     IM_COL32(240, 80, 80, 255));
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format("{} cones, {:.1f} m apart, staggered {:.1f} m, line {:.1f} m off the cone",
                                conePoints.size(), gap, stagger, line));
    if (static_cast<int>(conePoints.size()) < conesSet)
      lines.push_back(std::format("({} set: only {} fit on the pitch {:.0f} m apart)",
                                  conesSet, conePoints.size(), gap));
    lines.push_back(runnerLine());
    std::string splits;
    for (std::size_t index = 0; index < reached.size(); ++index)
      splits += std::format("{} {:.2f}  ",
                            index + 1 == points.size() ? std::string("F")
                                                       : std::to_string(index + 1),
                            reached[index]);
    if (!splits.empty()) lines.push_back(splits);
    lines.push_back(std::format("Gates missed (gone back for): {}", missed));
    if (finished)
    {
      lines.push_back(std::format("TOTAL  {:.2f} s", reached.back()));
      lines.push_back(std::format("Course {:.1f} m, ran {:.1f} m, average {:.2f} m/s",
                                  courseMetres(), distanceRun,
                                  distanceRun / reached.back()));
    }
    addBallLines(lines);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!finished || reached.empty()) return {};
    std::vector<DrillMetric> out{{"Total (s)", reached.back()},
                                 {"Distance run (m)", distanceRun},
                                 {"Average speed (m/s)", distanceRun / reached.back()},
                                 {"Gates missed", static_cast<double>(missed)}};
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
    const std::size_t due = reached.size();
    if (goingBack && backPending && due < conePoints.size())
      list.push_back(backPoint(due));
    list.insert(list.end(), aims.begin() + static_cast<std::ptrdiff_t>(nextAim),
                aims.end());
    const std::size_t skip = std::min(movedOn, list.size());
    return {list.begin() + static_cast<std::ptrdiff_t>(skip), list.end()};
  }

  /** Through his point round each cone, the line pushed out no more than
   * SWING_ROOM_METRES beyond it to widen the turn. */
  [[nodiscard]] float apexOffset() const override { return 0.0f; }
  [[nodiscard]] float outwardRoom() const override { return SWING_ROOM_METRES; }

 private:
  /** Side of the course cone `index` is passed on (-1 or 1). */
  [[nodiscard]] static float sideOf(std::size_t index)
  {
    return index % 2 == 0 ? -1.0f : 1.0f;
  }

  /** Where he goes back to for a missed gate `cone`: halfway back along the
   * leg he ran in to it on (half a spacing behind its line), so he comes at
   * it again as the course does. */
  [[nodiscard]] PitchPoint backPoint(std::size_t cone) const
  {
    const PitchPoint before = cone == 0 ? startPoint : points[cone - 1];
    return PitchPoint{(before.x + points[cone].x) * 0.5f, (before.y + points[cone].y) * 0.5f};
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
    return along(conePoints[cone], {0.0f, 1.0f},
                 sideOf(cone) * (GATE_BUFFER_METRES + metres));
  }

  /** The course runs straight down the pitch: its finish is square to it. */
  [[nodiscard]] Vector2F finishDirection() const override { return {1.0f, 0.0f}; }

  /** Straight across the finish from his line round the last cone (the
   * line counts anywhere along it): level with that, within the line as
   * drawn. No curving back to its middle. */
  [[nodiscard]] PitchPoint finishAim() const override
  {
    if (points.size() < 2) return points.back();
    const float across =
        std::clamp(PITCH.metresY(points[points.size() - 2].y - points.back().y),
                   -FINISH_AIM_METRES, FINISH_AIM_METRES);
    return along(points.back(), {0.0f, 1.0f}, across);
  }
  /** How far either side of the finish line's middle he may aim (its drawn
   * half-width, 4 m, less a margin). */
  static constexpr float FINISH_AIM_METRES = 3.5f;

  /** How far in from each end of the pitch the course starts and finishes
   * (the run through the finish goes on past it). */
  static constexpr float END_ROOM_METRES = 5.0f;

  /** A gate starts at its cone's edge (a cone is 20 cm across). */
  static constexpr float GATE_BUFFER_METRES = DrillDraw::CONE_RADIUS_METRES;
  /** The least his line may pass outside a cone's edge: his centre a
   * body's half-width out (a body 40 cm across), so he clears it. */
  static constexpr float MIN_LINE_METRES = 0.2f;
  /** His line by the course (the setting at 0): LINE_STRAIGHT_METRES on a
   * straight line of cones, out to LINE_WEAVE_METRES as the weave sharpens
   * (stagger up to the spacing), since the ball cuts the corner a little
   * inside his line and has to go through the gate too. */
  [[nodiscard]] static float courseLine(float spacing, float stagger)
  {
    const float sharpness = std::clamp(stagger / std::max(spacing, 0.1f), 0.0f, 1.0f);
    return LINE_STRAIGHT_METRES +
           (LINE_WEAVE_METRES - LINE_STRAIGHT_METRES) * sharpness;
  }
  static constexpr float LINE_STRAIGHT_METRES = 0.6f;
  static constexpr float LINE_WEAVE_METRES = 0.8f;
  /** How much of a gate is drawn (it is open beyond). */
  static constexpr float GATE_DRAWN_METRES = 2.0f;
  /** How far beyond his point round a cone his line may be pushed out to
   * widen the turn (the old 2 m gate's half-width). */
  static constexpr float SWING_ROOM_METRES = 1.0f;

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
                      directionBetween(at, course[cone + 1])) >= reversalDegrees;
      aims.push_back(reversal ? along(at, directionBetween(before, at), STOP_PAST_METRES)
                              : at);
      stops.push_back(reversal);
    }
    nextAim = 0;
  }
  /** How far past the gate a sharp reversal's stop is. */
  static constexpr float STOP_PAST_METRES = 0.5f;

  /** Going back for a missed gate, he is done with the point behind it only
   * once he, and the ball, are behind the gate's line: moved on from it
   * before, it is his point again (he brings the ball back to it). */
  void holdBackPoint(MatchEngine& engine, const MatchPlayer& player)
  {
    const std::size_t due = reached.size();
    if (!goingBack || !backPending || movedOn == 0 || due >= conePoints.size()) return;
    if (behindGate(due, player.position) &&
        (!withBall || behindGate(due, engine.getBall().position)))
      return;
    movedOn = 0;
    planRun(engine);
  }

  /** Across the line of the cone due, forwards: on its side (passed), or
   * the wrong side (missed: he goes back for it). With the ball, the ball
   * has to go through it too: the gate is passed once both have, missed if
   * either goes the wrong side. Then the finish line. */
  void passGates(MatchEngine& engine, const MatchPlayer& player)
  {
    const std::size_t due = reached.size();
    if (due < conePoints.size())
    {
      const PitchPoint cone = conePoints[due];
      // Across the line, forwards: whether on the gate's side, and when.
      const auto through = [&](PitchPoint from, PitchPoint to)
          -> std::optional<std::pair<bool, double>>
      {
        const auto share = crossingShare(from, to, cone, {1.0f, 0.0f});
        if (!share) return std::nullopt;
        const float y = from.y + (to.y - from.y) * *share;
        const float out = PITCH.metresY(y - cone.y) * sideOf(due);
        return std::pair{out >= GATE_BUFFER_METRES,
                         secondsOf(engine) - STEP + STEP * static_cast<double>(*share)};
      };
      const auto body = through(previous, player.position);
      const auto ball =
          withBall ? through(previousBall, engine.getBall().position) : std::nullopt;
      if (!body && !ball) return;
      if ((body && !body->first) || (ball && !ball->first))
      {
        ++missed;
        goingBack = true;
        backPending = true;
        nextAim = due;
        movedOn = 0;
        bodyThrough.reset();
        ballThrough.reset();
        planRun(engine);
        return;
      }
      if (body) bodyThrough = body->second;
      if (ball) ballThrough = ball->second;
      if (!bodyThrough || (withBall && !ballThrough)) return;
      reached.push_back(withBall ? std::max(*bodyThrough, *ballThrough) : *bodyThrough);
      // The points his body has moved on from are behind him, and so are
      // the point behind the gate (going back) and his point round it (a
      // stop past it is still ahead until he moves on from it).
      std::size_t skip = movedOn;
      if (skip > 0 && goingBack && backPending) --skip;
      nextAim = std::max(std::min(nextAim + skip, aims.size()),
                         stops[due] ? due : due + 1);
      movedOn = 0;
      goingBack = false;
      backPending = false;
      bodyThrough.reset();
      ballThrough.reset();
      planRun(engine);
      return;
    }
    if (const auto share = crossingShare(previous, player.position, points.back(),
                                         finishDirection()))
    {
      reached.push_back(secondsOf(engine) - STEP + STEP * static_cast<double>(*share));
      finished = true;
    }
  }

  float cones = 6.0f;
  float spacing = 5.0f;
  float lineOffset = 0.0f;
  float coneStagger = 0.0f;

  float gap = 5.0f;
  float line = 0.5f;
  /** The cones set, of which only as many as fit are laid out. */
  int conesSet = 6;
  float stagger = 0.0f;
  std::vector<PitchPoint> conePoints;
  int missed = 0;
  /** Going back for a missed gate (through the point behind it first). */
  bool goingBack = false;
  PitchPoint previous{};
  PitchPoint previousBall{};
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
 public:
  const char* name() const override { return "Repeated sprints"; }
  const char* summary() const override
  {
    return "Sprints between two lines, one way then back, with a set recovery "
           "(walking back to the line) between them. Each rep is timed from "
           "the start signal; the sprint reserve and condition are read at "
           "each start.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Reps", &reps, 2.0f, 20.0f, true));
    list.push_back(setting("Distance (m)", &distance, 10.0f, 60.0f, true));
    list.push_back(setting("Recovery (s)", &recovery, 3.0f, 60.0f, true,
                    "From crossing the line to the next start signal."));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    repCount = static_cast<int>(reps);
    metres = distance;
    rest = recovery;
    lines = {at(0.25f, 0.5f), along(at(0.25f, 0.5f), {1.0f, 0.0f}, metres)};
    startRunner(engine, ids, lines[0]);
    done.clear();
    signal = 0.0;
    crossedAt.reset();
    startRep(engine);
    previous = lines[0];
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (!player) return false;
    const double now = secondsOf(engine);
    if (!crossedAt)
    {
      current.topSpeed = std::max(current.topSpeed, speedOf(*player));
      const PitchPoint to = lines[(done.size() + 1) % 2];
      const PitchPoint from = lines[done.size() % 2];
      if (const auto share = crossingShare(previous, player->position, to,
                                           directionBetween(from, to)))
      {
        crossedAt = now - STEP + STEP * static_cast<double>(*share);
        current.seconds = *crossedAt - signal;
        done.push_back(current);
        // Recovery: ease back onto the line just crossed.
        steerTo(engine, to, false);
      }
    }
    else if (static_cast<int>(done.size()) < repCount && now >= *crossedAt + rest)
    {
      startRep(engine);
    }
    previous = player->position;
    const bool over = static_cast<int>(done.size()) >= repCount && crossedAt &&
                      now > *crossedAt + 0.5;
    return !over && now < static_cast<double>(repCount) * (rest + 30.0f);
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    const float half = PITCH.fromMetresY(4.0f);
    const float middle = PITCH.atY(0.5f);
    for (std::size_t index = 0; index < 2; ++index)
    {
      const ImU32 colour = (done.size() + 1) % 2 == index && !crossedAt
                               ? IM_COL32(240, 80, 80, 255)
                               : IM_COL32(255, 255, 255, 160);
      DrillDraw::line(renderer, draw, PitchPoint{lines[index].x, middle - half},
                      PitchPoint{lines[index].x, middle + half}, colour, 3.0f * scale);
    }
    drawTrail(renderer, draw);
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> out;
    out.push_back(std::format("{} × {:.0f} m, {:.0f} s recovery", repCount, metres,
                              rest));
    out.push_back(runnerLine());
    double best = 1e9;
    double worst = 0.0;
    double total = 0.0;
    for (std::size_t index = 0; index < done.size(); ++index)
    {
      const Rep& rep = done[index];
      out.push_back(std::format(
          "{:>2}. {:.2f} s · top {:.2f} m/s · reserve {:.0f}% · condition {:.0f}%",
          index + 1, rep.seconds, rep.topSpeed, rep.reserve * 100.0f,
          rep.condition * 100.0f));
      best = std::min(best, rep.seconds);
      worst = std::max(worst, rep.seconds);
      total += rep.seconds;
    }
    if (!crossedAt && static_cast<int>(done.size()) < repCount && sentSignal)
      out.push_back(std::format("{:>2}. running…", done.size() + 1));
    if (done.size() >= 2)
    {
      const double mean = total / static_cast<double>(done.size());
      out.push_back(std::format("Best {:.2f} s · worst {:.2f} s · mean {:.2f} s",
                                best, worst, mean));
      out.push_back(std::format("Decrement {:.1f}% (mean vs best) · last vs first "
                                "{:+.2f} s",
                                (mean / best - 1.0) * 100.0,
                                done.back().seconds - done.front().seconds));
    }
    addBallLines(out);
    return out;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (static_cast<int>(done.size()) < repCount || done.empty()) return {};
    double best = 1e9;
    double worst = 0.0;
    double total = 0.0;
    for (const Rep& rep : done)
    {
      best = std::min(best, rep.seconds);
      worst = std::max(worst, rep.seconds);
      total += rep.seconds;
    }
    const double mean = total / static_cast<double>(done.size());
    std::vector<DrillMetric> out{{"Mean (s)", mean},
            {"Best (s)", best},
            {"Worst (s)", worst},
            {"Decrement (%)", (mean / best - 1.0) * 100.0},
            {"Last - first (s)", done.back().seconds - done.front().seconds},
            {"Reserve at last start (%)", done.back().reserve * 100.0},
            {"Condition at last start (%)", done.back().condition * 100.0}};
    addBallMetrics(out);
    return out;
  }

 private:
  struct Rep
  {
    double seconds = 0.0;
    float topSpeed = 0.0f;
    float reserve = 1.0f;
    float condition = 1.0f;
  };

  void startRep(MatchEngine& engine)
  {
    const MatchPlayer* player = slotOf(engine, id);
    const PitchPoint from = lines[done.size() % 2];
    const PitchPoint to = lines[(done.size() + 1) % 2];
    current = Rep{};
    if (player)
    {
      current.reserve = player->sprintReserve;
      current.condition = player->stamina;
    }
    signal = secondsOf(engine);
    crossedAt.reset();
    sentSignal = true;
    steerTo(engine, along(to, directionBetween(from, to), RUN_THROUGH_METRES),
            true, {}, 0.0f, 0.0f, std::nullopt, true);
  }

  float reps = 6.0f;
  float distance = 30.0f;
  float recovery = 20.0f;

  int repCount = 6;
  float metres = 30.0f;
  float rest = 20.0f;
  std::array<PitchPoint, 2> lines{};
  std::vector<Rep> done;
  Rep current;
  bool sentSignal = false;
  double signal = 0.0;
  std::optional<double> crossedAt;
  PitchPoint previous{};
};

// --- Flag chase ------------------------------------------------------------

/**
 * Flag chase: two flags are always out, the one to run to and the one
 * after; reaching the first places a new one, from the drill's seed, so the
 * same seed is the same course for every runner.
 */
class FlagChaseDrill final : public CourseDrill
{
 public:
  FlagChaseDrill() { runThroughLast = false; }

  const char* name() const override { return "Flag chase"; }
  const char* summary() const override
  {
    return "Run to the flag, then the next: two flags are always out, and a "
           "new one is placed (from the seed) when one is reached. Times by "
           "leg against its length and the turn into it. Same seed, same "
           "course: race the AI and yourself.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Flags", &flags, 2.0f, 50.0f, true));
    list.push_back(setting("Area length (m)", &areaLength, 20.0f, 100.0f, true,
                    "The cordon flags are placed in, round the centre spot."));
    list.push_back(setting("Area width (m)", &areaWidth, 10.0f, 64.0f, true));
    list.push_back(setting("Shortest leg (m)", &legMin, 3.0f, 40.0f, true));
    list.push_back(setting("Longest leg (m)", &legMax, 3.0f, 60.0f, true));
    list.push_back(setting("Least turn (°)", &turnMin, 0.0f, 180.0f, true,
                    "Turn from one leg into the next: 0 straight on, 180 "
                    "straight back."));
    list.push_back(setting("Most turn (°)", &turnMax, 0.0f, 180.0f, true));
    list.push_back(setting("Arrival radius (m)", &arrivalRadius, 0.3f, 3.0f, false));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    flagCount = static_cast<int>(flags);
    halfLength = areaLength * 0.5f;
    halfWidth = areaWidth * 0.5f;
    shortest = std::min(legMin, legMax);
    longest = std::max(legMin, legMax);
    leastTurn = std::min(turnMin, turnMax);
    mostTurn = std::max(turnMin, turnMax);
    random.seed(seed ^ 0x5eed'f1a6u);
    const PitchPoint start = at(0.5f, 0.5f);
    heading = {1.0f, 0.0f};
    last = start;
    turns.clear();
    std::vector<PitchPoint> course;
    for (int flag = 0; flag < std::min(2, flagCount); ++flag)
      course.push_back(nextFlag());
    beginCourse(engine, ids, start, std::move(course));
    previous = start;
    resultSaved = false;
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (!player) return false;
    if (advanceCourse(engine, *player, previous) &&
        static_cast<int>(points.size()) < flagCount)
    {
      // Always two out: place the one after next.
      points.push_back(nextFlag());
      finished = false;
      planRun(engine);
    }
    previous = player->position;
    const double now = secondsOf(engine);
    if (finished && !resultSaved)
    {
      double& best = bests[courseKey()][path];
      if (best == 0.0 || reached.back() < best) best = reached.back();
      resultSaved = true;
    }
    return !(finished && now > reached.back() + 0.5) && now < 600.0;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::area(renderer, draw, toPitch({LENGTH * 0.5f - halfLength,
                                             WIDTH * 0.5f - halfWidth}),
                    toPitch({LENGTH * 0.5f + halfLength, WIDTH * 0.5f + halfWidth}),
                    IM_COL32(255, 170, 40, 200), 2.0f * scale);
    drawTrail(renderer, draw);
    for (std::size_t index = 0; index < reached.size(); ++index)
      DrillDraw::marker(renderer, draw, points[index], 2.5f * scale,
                        IM_COL32(150, 150, 150, 180));
    const std::size_t next = reached.size();
    if (next + 1 < points.size())
    {
      DrillDraw::line(renderer, draw, points[next], points[next + 1],
                      IM_COL32(255, 255, 255, 70), 1.0f * scale);
      DrillDraw::marker(renderer, draw, points[next + 1], 5.0f * scale,
                        IM_COL32(120, 170, 255, 200),
                        std::format("{}", next + 2).c_str());
    }
    if (next < points.size())
      DrillDraw::marker(renderer, draw, points[next], 7.0f * scale,
                        IM_COL32(255, 220, 40, 255),
                        std::format("{}", next + 1).c_str());
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format("{} flags in {:.0f} × {:.0f} m, legs {:.0f}-{:.0f} m, "
                                "turns {:.0f}-{:.0f}°",
                                flagCount, halfLength * 2.0f, halfWidth * 2.0f,
                                shortest, longest, leastTurn, mostTurn));
    lines.push_back(runnerLine());
    lines.push_back(std::format("Flags {} / {}", reached.size(), flagCount));
    // The latest legs.
    const std::size_t from = reached.size() > 5 ? reached.size() - 5 : 0;
    for (std::size_t index = from; index < reached.size(); ++index)
      lines.push_back(std::format("  leg {:>2}: {:.1f} m, turn {:>3.0f}° · {:.2f} s "
                                  "({:.2f} m/s)",
                                  index + 1, legMetres(index), turns[index],
                                  legSeconds(index),
                                  legMetres(index) / legSeconds(index)));
    if (finished)
    {
      lines.push_back(std::format("TOTAL  {:.2f} s · course {:.0f} m, ran {:.0f} m",
                                  reached.back(), courseMetres(), distanceRun));
    }
    // Cornering: average speed of legs by the turn into them (the first leg,
    // from standing, left out).
    std::array<double, 4> metres{};
    std::array<double, 4> seconds{};
    std::array<int, 4> count{};
    for (std::size_t index = 1; index < reached.size(); ++index)
    {
      const auto bucket = static_cast<std::size_t>(
          std::clamp(static_cast<int>(turns[index] / 45.0f), 0, 3));
      metres[bucket] += legMetres(index);
      seconds[bucket] += legSeconds(index);
      ++count[bucket];
    }
    std::string cornering;
    for (std::size_t bucket = 0; bucket < 4; ++bucket)
      if (count[bucket] > 0)
        cornering += std::format("{}-{}°: {:.2f} m/s ({})  ", bucket * 45,
                                 bucket * 45 + 45, metres[bucket] / seconds[bucket],
                                 count[bucket]);
    if (!cornering.empty())
    {
      lines.push_back("Average leg speed by turn into it:");
      lines.push_back("  " + cornering);
    }
    // This course (seed and settings) by who ran it.
    const auto best = bests.find(courseKey());
    if (best != bests.end())
    {
      std::string board = "This course, best: ";
      for (std::size_t who = 0; who < PATH_NAMES.size(); ++who)
        board += std::format("{} {}   ", PATH_NAMES[who],
                             best->second[who] > 0.0
                                 ? std::format("{:.2f} s", best->second[who])
                                 : std::string("-"));
      lines.push_back(board);
    }
    addBallLines(lines);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!finished || reached.empty()) return {};
    const double total = reached.back();
    std::vector<DrillMetric> out{
        {"Total (s)", total},
        {"Distance run (m)", distanceRun},
        {"Average speed (m/s)", distanceRun / total}};
    // Average leg speed by the turn into the leg (the first leg left out).
    std::array<double, 4> metres{};
    std::array<double, 4> seconds{};
    for (std::size_t index = 1; index < reached.size(); ++index)
    {
      const auto bucket = static_cast<std::size_t>(
          std::clamp(static_cast<int>(turns[index] / 45.0f), 0, 3));
      metres[bucket] += legMetres(index);
      seconds[bucket] += legSeconds(index);
    }
    for (std::size_t bucket = 0; bucket < 4; ++bucket)
      if (seconds[bucket] > 0.0)
        out.push_back({std::format("Leg speed {}-{}° (m/s)", bucket * 45,
                                   bucket * 45 + 45),
                       metres[bucket] / seconds[bucket]});
    addBallMetrics(out);
    return out;
  }

 private:
  /** The next flag after `last`, turning off `heading` within the limits. */
  PitchPoint nextFlag()
  {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const auto inside = [&](Vector2F metres)
    {
      constexpr float MARGIN = 1.0f;
      return std::abs(metres.x - LENGTH * 0.5f) <= halfLength - MARGIN &&
             std::abs(metres.y - WIDTH * 0.5f) <= halfWidth - MARGIN;
    };
    const Vector2F from = toMetres(last);
    std::optional<Vector2F> found;
    float turn = 0.0f;
    // Within the turn limits if it can be, any direction if not.
    for (int attempt = 0; attempt < 400 && !found; ++attempt)
    {
      const bool anyTurn = attempt >= 300;
      const float length = shortest + (longest - shortest) * unit(random);
      const float degrees =
          anyTurn ? 180.0f * unit(random)
                  : leastTurn + (mostTurn - leastTurn) * unit(random);
      const float sign = unit(random) < 0.5f ? -1.0f : 1.0f;
      const Vector2F direction = rotated(heading, sign * degrees * PI / 180.0f);
      const Vector2F candidate{from.x + direction.x * length,
                               from.y + direction.y * length};
      if (!inside(candidate)) continue;
      found = candidate;
      turn = degrees;
    }
    if (!found)
    {
      // An area too small for the legs: its centre.
      found = Vector2F{LENGTH * 0.5f, WIDTH * 0.5f};
      turn = turnDegrees(heading, directionBetween(last, toPitch(*found)));
    }
    const PitchPoint flag = toPitch(*found);
    heading = directionBetween(last, flag);
    last = flag;
    turns.push_back(turn);
    return flag;
  }

  [[nodiscard]] std::string courseKey() const
  {
    return std::format("{}|{}|{:.0f}|{:.0f}|{:.0f}|{:.0f}|{:.0f}|{:.0f}|{:.2f}|{}",
                       seed, flagCount, halfLength, halfWidth, shortest, longest,
                       leastTurn, mostTurn, radius, runnerKey());
  }

  float flags = 12.0f;
  float areaLength = 50.0f;
  float areaWidth = 40.0f;
  float legMin = 8.0f;
  float legMax = 20.0f;
  float turnMin = 0.0f;
  float turnMax = 180.0f;

  int flagCount = 12;
  float halfLength = 25.0f;
  float halfWidth = 20.0f;
  float shortest = 8.0f;
  float longest = 20.0f;
  float leastTurn = 0.0f;
  float mostTurn = 180.0f;
  std::mt19937 random;
  Vector2F heading{1.0f, 0.0f};
  PitchPoint last{};
  /** Turn into each leg (degrees), by flag. */
  std::vector<float> turns;
  PitchPoint previous{};
  /** Best total of this course by who ran it. */
  std::map<std::string, std::array<double, 2>> bests;
  bool resultSaved = false;
};

// --- Turn and run --------------------------------------------------------------

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
 public:
  const char* name() const override { return "Turn and run"; }
  const char* summary() const override
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
    DrillParameter first = setting("First", &firstChoice, 0.0f, 7.0f, true,
                                   "How he moves down the track on the first "
                                   "leg.");
    first.choices = {"Forward", "Backward", "Side-step left", "Side-step right", "Diagonal forward-left", "Diagonal forward-right", "Diagonal back-left", "Diagonal back-right"};
    list.push_back(first);
    list.push_back(setting("First leg (m)", &switchSetting, 2.0f, 40.0f, true,
                           "From the start to the line where he changes."));
    DrillParameter then = setting("Then", &thenChoice, 0.0f, 7.0f, true,
                                  "How he moves after the line.");
    then.choices = {"Forward", "Backward", "Side-step left", "Side-step right", "Diagonal forward-left", "Diagonal forward-right", "Diagonal back-left", "Diagonal back-right"};
    list.push_back(then);
    DrillParameter heads = setting("Second leg heads", &headsChoice, 0.0f, 1.0f, true,
                                   "On down the track: the same way, his body "
                                   "turning to the new way of moving. From his "
                                   "facing: off from how his body faces at the "
                                   "line (Forward: the way he faces), his body "
                                   "staying as it is.");
    heads.choices = {"On down the track", "From his facing"};
    list.push_back(heads);
    list.push_back(setting("Second leg (m)", &secondSetting, 2.0f, 60.0f, true,
                           "From the line to the finish."));
    addRunnerParameters(list);
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    first = static_cast<int>(firstChoice + 0.5f);
    then = static_cast<int>(thenChoice + 0.5f);
    fromFacing = headsChoice >= 0.5f;
    switchMetres = switchSetting;
    secondMetres = secondSetting;
    // Down the track (+x) on the first leg.
    const PitchPoint start = at(START_LINE, 0.5f);
    linePoint = along(start, {1.0f, 0.0f}, switchMetres);
    firstBody = bodyFor(first, 0.0f);
    // The second leg: on down the track, his body turning to the new way;
    // or off from his facing, his body staying.
    secondTravel = fromFacing ? travelFor(then, firstBody) : 0.0f;
    secondBody = fromFacing ? firstBody : bodyFor(then, 0.0f);
    secondDirection = {std::cos(secondTravel), std::sin(secondTravel)};
    finishPoint = along(linePoint, secondDirection, secondMetres);
    startRunner(engine, ids, start);
    steerTo(engine, along(linePoint, {1.0f, 0.0f}, RUN_THROUGH_METRES), true, {},
            0.0f, 0.0f, facingAngleOf(firstBody), true);
    previous = start;
    switchedAt.reset();
    switchSpeed = 0.0f;
    slowest.reset();
    settledAt.reset();
    settleMetres = 0.0f;
    finishTime.reset();
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* player = track(engine);
    if (!player) return false;
    const double now = secondsOf(engine);
    const float speed = speedOf(*player);
    const auto at = [&](std::optional<float> share)
    { return now - STEP + STEP * static_cast<double>(*share); };
    if (!switchedAt)
    {
      if (const auto share =
              crossingShare(previous, player->position, linePoint, {1.0f, 0.0f}))
      {
        // The line: the new way of moving.
        switchedAt = at(share);
        switchSpeed = speed;
        switchFrom = player->position;
        steerTo(engine, along(finishPoint, secondDirection, RUN_THROUGH_METRES),
                true, {}, 0.0f, 0.0f, facingAngleOf(secondBody), true);
      }
    }
    else if (!settledAt)
    {
      // Until settled: his body within a few degrees of the new way, and
      // his run along the second leg.
      if (!slowest || speed < *slowest) slowest = speed;
      const float bodyOff = std::remainder(
          facingAngleOf(secondBody) - player->facingAngle, 2.0f * PI);
      const float runLength = std::hypot(player->velocity.x, player->velocity.y);
      const float along = runLength > 0.5f
                              ? (player->velocity.x * secondDirection.x +
                                 player->velocity.y * secondDirection.y) /
                                    runLength
                              : 0.0f;
      if (std::abs(bodyOff) < SETTLED_RADIANS && along > SETTLED_COSINE)
      {
        settledAt = now;
        settleMetres = metresBetween(switchFrom, player->position);
      }
    }
    if (switchedAt && !finishTime)
      if (const auto share = crossingShare(previous, player->position, finishPoint,
                                           secondDirection))
        finishTime = at(share);
    previous = player->position;
    return !(finishTime && now > *finishTime + 0.5) && now < 60.0;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    const auto across = [&](PitchPoint at, Vector2F direction, ImU32 colour, float width)
    {
      const Vector2F side = rotated(direction, 0.5f * PI);
      DrillDraw::line(renderer, draw, along(at, side, -4.0f), along(at, side, 4.0f),
                      colour, width * scale);
    };
    across(at(START_LINE, 0.5f), {1.0f, 0.0f}, IM_COL32(90, 220, 120, 255), 3.0f);
    across(linePoint, {1.0f, 0.0f}, IM_COL32(255, 200, 60, 255), 2.0f);
    DrillDraw::label(renderer, draw, along(linePoint, {0.0f, 1.0f}, -6.0f),
                     std::format("then: {}", NAMES[static_cast<std::size_t>(then)]).c_str(),
                     IM_COL32(255, 200, 60, 255));
    across(finishPoint, secondDirection, IM_COL32(240, 80, 80, 255), 3.0f);
    drawTrail(renderer, draw);
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format("{} for {:.0f} m, then {} for {:.0f} m{}",
                                NAMES[static_cast<std::size_t>(first)], switchMetres,
                                NAMES[static_cast<std::size_t>(then)], secondMetres,
                                fromFacing ? " (from his facing)" : ""));
    lines.push_back(runnerLine());
    if (switchedAt)
      lines.push_back(std::format("At the line {:.2f} s, at {:.2f} m/s", *switchedAt,
                                  switchSpeed));
    if (settledAt && switchedAt)
      lines.push_back(std::format("Settled {:.2f} s after, over {:.1f} m · slowest {:.2f} m/s",
                                  *settledAt - *switchedAt, settleMetres,
                                  slowest.value_or(switchSpeed)));
    if (finishTime)
      lines.push_back(std::format("FINISH  {:.2f} s", *finishTime));
    addBallLines(lines);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!finishTime || !switchedAt || !settledAt) return {};
    std::vector<DrillMetric> out{
        {"Finish (s)", *finishTime},
        {"At the line (s)", *switchedAt},
        {"Speed at the line (m/s)", switchSpeed},
        {"Settled after (s)", *settledAt - *switchedAt},
        {"Settled after (m)", settleMetres},
        {"Slowest in the change (m/s)", slowest.value_or(switchSpeed)}};
    addBallMetrics(out);
    return out;
  }

 private:
  static constexpr float START_LINE = 0.25f;
  /** Settled: his body within this of the new way, and his run within
   * this (as a cosine, ~10 deg) of the second leg. */
  static constexpr float SETTLED_RADIANS = 0.087f;
  static constexpr float SETTLED_COSINE = 0.985f;
  static constexpr std::array<const char*, 8> NAMES{
      "forward",           "backpedal",          "side-step left",
      "side-step right",   "diagonal forward-left", "diagonal forward-right",
      "diagonal back-left", "diagonal back-right"};

  // Ways of moving against his body, as real directions on the pitch
  // (radians, in metres; facingAngleOf() converts a body's to a facing).
  // Side-stepping left, he moves to his own left: a quarter turn clockwise
  // from the way he faces here.
  /** His body for moving `way` along a run at `travel`. */
  static float bodyFor(int way, float travel)
  {
    constexpr std::array<float, 8> OFFSETS{0.0f,        PI,          0.5f * PI,
                                           -0.5f * PI,  0.25f * PI,  -0.25f * PI,
                                           0.75f * PI,  -0.75f * PI};
    return std::remainder(travel + OFFSETS[static_cast<std::size_t>(way)], 2.0f * PI);
  }
  /** His run for moving `way` with his body at `body`. */
  static float travelFor(int way, float body)
  {
    constexpr std::array<float, 8> OFFSETS{0.0f,        PI,          -0.5f * PI,
                                           0.5f * PI,   -0.25f * PI, 0.25f * PI,
                                           -0.75f * PI, 0.75f * PI};
    return std::remainder(body + OFFSETS[static_cast<std::size_t>(way)], 2.0f * PI);
  }

  float firstChoice = 1.0f;
  float switchSetting = 10.0f;
  float thenChoice = 0.0f;
  float headsChoice = 0.0f;
  float secondSetting = 20.0f;

  int first = 1;
  int then = 0;
  bool fromFacing = false;
  float switchMetres = 10.0f;
  float secondMetres = 20.0f;
  PitchPoint linePoint{};
  PitchPoint finishPoint{};
  float firstBody = 0.0f;
  float secondBody = 0.0f;
  float secondTravel = 0.0f;
  Vector2F secondDirection{1.0f, 0.0f};
  PitchPoint previous{};
  PitchPoint switchFrom{};
  std::optional<double> switchedAt;
  float switchSpeed = 0.0f;
  std::optional<float> slowest;
  std::optional<double> settledAt;
  float settleMetres = 0.0f;
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
 public:
  const char* name() const override { return "Overtake"; }
  const char* summary() const override
  {
    return "A slower leader and a faster chaser race for the same finish. "
           "The chaser goes round the leader if he can still get there "
           "first, on the shorter side that nobody else is about to run "
           "into. Who finished first, the closest they came, and the times.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Gap (m)", &gapSetting, 1.0f, 20.0f, false,
                           "How far ahead of the chaser the leader starts."));
    list.push_back(setting("Finish (m)", &finishSetting, 10.0f, 80.0f, true,
                           "From the chaser's start to the finish line."));
    list.push_back(setting("Leader off the line (m)", &offsetSetting, -3.0f,
                           3.0f, false,
                           "The leader's start to the side of the chaser's "
                           "line (+ to his left)."));
    list.push_back(setting("Leader Pace", &leaderPace, 1.0f, 100.0f, true));
    list.push_back(setting("Chaser Pace", &chaserPace, 1.0f, 100.0f, true));
    DrillParameter team = setting("Leader is", &teamChoice, 0.0f, 1.0f, true);
    team.choices = {"An opponent", "A team-mate"};
    list.push_back(team);
    DrillParameter third = setting("Third runner", &thirdChoice, 0.0f, 2.0f, true,
                                   "Alongside the leader, as fast as him, "
                                   "blocking that side.");
    third.choices = {"None", "On his left", "On his right"};
    list.push_back(third);
    return list;
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    const bool mates = teamChoice >= 0.5f;
    std::vector<DrillPlayerSpec> list{
        {"Chaser", PlayerRole::CM, true, {{"Pace", chaserPace}}},
        {"Leader", PlayerRole::CM, mates, {{"Pace", leaderPace}}}};
    if (thirdChoice >= 0.5f)
      list.push_back({"Third", PlayerRole::CM, mates, {{"Pace", leaderPace}}});
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
    third = static_cast<int>(thirdChoice + 0.5f);
    chaserId = ids[0];
    leaderId = ids[1];
    thirdId = ids.size() > 2 ? ids[2] : 0;
    const PitchPoint start = at(0.25f, 0.5f);
    const PitchPoint leaderAt =
        along(along(start, {1.0f, 0.0f}, gap), {0.0f, 1.0f}, offset);
    finishLine = along(start, {1.0f, 0.0f}, finishMetres);
    MatchScenario scenario;
    scenario.players.push_back({chaserId, start, false});
    scenario.players.push_back({leaderId, leaderAt, false});
    if (thirdId != 0)
      scenario.players.push_back(
          {thirdId,
           along(leaderAt, {0.0f, 1.0f}, third == 1 ? THIRD_SIDE_METRES
                                                     : -THIRD_SIDE_METRES),
           false});
    scenario.ballPosition = ballOutOfTheWay();
    engine.startDrill(scenario);
    // They all race for the same point beyond the line.
    const PitchPoint target = along(finishLine, {1.0f, 0.0f}, RUN_THROUGH_METRES);
    for (const PlayerID id : {chaserId, leaderId, thirdId})
      if (id != 0) engine.setDrillTarget(id, target, true);
    chaserTime.reset();
    leaderTime.reset();
    closest = std::numeric_limits<float>::max();
    closestThird = std::numeric_limits<float>::max();
    previousChaser = start;
    previousLeader = leaderAt;
    trail.clear();
  }

  bool afterStep(MatchEngine& engine) override
  {
    const MatchPlayer* chaser = slotOf(engine, chaserId);
    const MatchPlayer* leader = slotOf(engine, leaderId);
    if (!chaser || !leader) return false;
    const double now = secondsOf(engine);
    const auto crossed = [&](PitchPoint from, PitchPoint to,
                             std::optional<double>& time)
    {
      if (time) return;
      if (const auto share = crossingShare(from, to, finishLine, {1.0f, 0.0f}))
        time = now - STEP + STEP * static_cast<double>(*share);
    };
    crossed(previousChaser, chaser->position, chaserTime);
    crossed(previousLeader, leader->position, leaderTime);
    previousChaser = chaser->position;
    previousLeader = leader->position;
    // Until the chaser is over the line: how close he came.
    if (!chaserTime)
    {
      closest = std::min(closest, metresBetween(chaser->position, leader->position));
      if (const MatchPlayer* other = thirdId ? slotOf(engine, thirdId) : nullptr)
        closestThird =
            std::min(closestThird, metresBetween(chaser->position, other->position));
    }
    if (engine.getSimulatedSteps() % 2 == 0) trail.push_back(chaser->position);
    const bool done = chaserTime && leaderTime;
    return !(done && now > std::max(*chaserTime, *leaderTime) + 0.5) &&
           now < 30.0;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::path(renderer, draw, trail, IM_COL32(120, 200, 255, 200),
                    2.0f * scale);
    DrillDraw::line(renderer, draw, along(finishLine, {0.0f, 1.0f}, -6.0f),
                    along(finishLine, {0.0f, 1.0f}, 6.0f),
                    IM_COL32(240, 80, 80, 255), 3.0f * scale);
    DrillDraw::label(renderer, draw, finishLine, "finish",
                     IM_COL32(240, 80, 80, 255));
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "Leader {:.1f} m ahead, {:+.1f} m off the line ({}), finish {:.0f} m{}",
        gap, offset, teamChoice >= 0.5f ? "team-mate" : "opponent", finishMetres,
        third == 1 ? ", a third runner on his left"
        : third == 2 ? ", a third runner on his right" : ""));
    if (chaserTime)
      lines.push_back(std::format("Chaser {:.2f} s", *chaserTime));
    if (leaderTime)
      lines.push_back(std::format("Leader {:.2f} s", *leaderTime));
    lines.push_back(std::format("Closest to the leader {:.2f} m", closest));
    if (thirdId != 0)
      lines.push_back(std::format("Closest to the third runner {:.2f} m", closestThird));
    if (chaserTime && leaderTime)
      lines.push_back(std::format("FINISH  {} first, by {:.2f} s",
                                  *chaserTime < *leaderTime ? "chaser" : "leader",
                                  std::abs(*chaserTime - *leaderTime)));
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!chaserTime || !leaderTime) return {};
    std::vector<DrillMetric> out{
        {"Chaser (s)", *chaserTime},
        {"Leader (s)", *leaderTime},
        {"Chaser first", *chaserTime < *leaderTime ? 1.0 : 0.0},
        {"Closest to leader (m)", closest}};
    if (thirdId != 0) out.push_back({"Closest to third (m)", closestThird});
    return out;
  }

 private:
  static constexpr float THIRD_SIDE_METRES = 1.6f;

  float gapSetting = 6.0f;
  float finishSetting = 40.0f;
  float offsetSetting = 0.0f;
  float leaderPace = 40.0f;
  float chaserPace = 90.0f;
  float teamChoice = 0.0f;
  float thirdChoice = 0.0f;

  float gap = 6.0f;
  float finishMetres = 40.0f;
  float offset = 0.0f;
  int third = 0;
  PlayerID chaserId = 0;
  PlayerID leaderId = 0;
  PlayerID thirdId = 0;
  PitchPoint finishLine{};
  PitchPoint previousChaser{};
  PitchPoint previousLeader{};
  std::optional<double> chaserTime;
  std::optional<double> leaderTime;
  float closest = 0.0f;
  float closestThird = 0.0f;
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
 public:
  const char* name() const override { return "Loose ball"; }
  const char* summary() const override
  {
    return "A ball rolled loose along the pitch, a chaser just behind it and "
           "a team-mate further along its path (and an opponent, if set). The "
           "brain decides who goes: who wins it, how soon, and how far each "
           "ran.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Ball speed (m/s)", &ballSpeedSetting, 2.0f, 25.0f, false));
    list.push_back(setting("Chaser behind it (m)", &chaserSetting, 0.0f, 20.0f, false));
    list.push_back(setting("Chaser off its line (m)", &chaserOffsetSetting, -15.0f, 15.0f,
                           false, "+ to the left of the ball's path."));
    list.push_back(setting("Team-mate along its path (m)", &mateSetting, 5.0f, 50.0f, false));
    list.push_back(setting("Team-mate off its line (m)", &mateOffsetSetting, -10.0f,
                           10.0f, false));
    DrillParameter rival = setting("Opponent", &rivalChoice, 0.0f, 2.0f, true);
    rival.choices = {"None", "Level with the team-mate", "Beside the chaser"};
    list.push_back(rival);
    DrillParameter bystander = setting("Bystander", &bystanderChoice, 0.0f, 2.0f, true,
                                       "A player not going for the ball, in the "
                                       "chaser's way: he goes round him (way-round "
                                       "plan).");
    bystander.choices = {"None", "A team-mate", "An opponent"};
    bystander.section = "Bystander";
    list.push_back(bystander);
    list.push_back(setting("Bystander along its path (m)", &bystanderAlongSetting, 2.0f,
                           40.0f, false));
    list.push_back(setting("Bystander off its line (m)", &bystanderOffsetSetting, -10.0f,
                           10.0f, false,
                           "+ to the left of the ball's path."));
    DrillParameter moving = setting("Bystander moves", &bystanderMoveChoice, 0.0f, 2.0f,
                                    true, "Across the ball's path, from his side of it.");
    moving.choices = {"Stands", "Walks across", "Runs across"};
    list.push_back(moving);
    return list;
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    std::vector<DrillPlayerSpec> list{{"Chaser", PlayerRole::CM, true},
                                      {"Team-mate", PlayerRole::CM, true}};
    if (rivalChoice >= 0.5f) list.push_back({"Opponent", PlayerRole::CM, false});
    if (bystanderChoice >= 0.5f)
      list.push_back({"Bystander", PlayerRole::CM, bystanderChoice < 1.5f});
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    ballSpeed = ballSpeedSetting;
    rival = static_cast<int>(rivalChoice + 0.5f);
    chaserId = ids[0];
    mateId = ids[1];
    rivalId = rivalChoice >= 0.5f && ids.size() > 2 ? ids[2] : 0;
    bystanderId = bystanderChoice >= 0.5f ? ids.back() : 0;
    const PitchPoint ballAt = at(0.35f, 0.5f);
    const PitchPoint chaserAt = along(along(ballAt, {1.0f, 0.0f}, -chaserSetting),
                                    {0.0f, 1.0f}, chaserOffsetSetting);
    const PitchPoint mateAt = along(along(ballAt, {1.0f, 0.0f}, mateSetting),
                                  {0.0f, 1.0f}, mateOffsetSetting);
    MatchScenario scenario;
    scenario.players.push_back({chaserId, chaserAt, false});
    scenario.players.push_back({mateId, mateAt, false});
    if (rivalId != 0)
      scenario.players.push_back(
          {rivalId, rival == 1 ? along(mateAt, {0.0f, 1.0f}, 8.0f)
                               : along(chaserAt, {0.0f, 1.0f}, 3.0f),
           false});
    const PitchPoint bystanderAt = along(along(ballAt, {1.0f, 0.0f}, bystanderAlongSetting),
                                      {0.0f, 1.0f}, bystanderOffsetSetting);
    if (bystanderId != 0) scenario.players.push_back({bystanderId, bystanderAt, false});
    scenario.ballPosition = ballAt;
    engine.startDrill(scenario);
    engine.feedBall(ballAt, {ballSpeed, 0.0f}, 0.0f, 0);
    if (bystanderId != 0)
    {
      // He holds his spot, or crosses the ball's path from his side of it.
      const int moves = static_cast<int>(bystanderMoveChoice + 0.5f);
      const float across = bystanderOffsetSetting > 0.0f ? -1.0f : 1.0f;
      engine.setDrillTarget(bystanderId,
                            moves == 0 ? bystanderAt
                                       : along(bystanderAt, {0.0f, across}, 30.0f),
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
      if (id != 0)
        if (const MatchPlayer* player = slotOf(engine, id))
          previous[id] = player->position;
  }

  bool afterStep(MatchEngine& engine) override
  {
    for (auto& [id, at] : previous)
      if (const MatchPlayer* player = slotOf(engine, id))
      {
        ran[id] += metresBetween(at, player->position);
        at = player->position;
      }
    // The chaser and the bystander: how close, and any way round him.
    waypoint.reset();
    if (bystanderId != 0 && !wonAt)
      if (const MatchPlayer* chaser = slotOf(engine, chaserId))
        if (const MatchPlayer* bystander = slotOf(engine, bystanderId))
        {
          closestBystander = std::min(
              closestBystander, metresBetween(chaser->position, bystander->position));
          const auto& all = engine.getPlayers();
          if (chaser->wayRoundOf >= 0 &&
              &all[static_cast<std::size_t>(chaser->wayRoundOf)] == bystander)
          {
            waypoint = chaser->movementTarget;
            wentRound = chaser->wayRoundSide;
            if (!roundAt) roundAt = secondsOf(engine);
          }
        }
    const double now = secondsOf(engine);
    if (!wonAt)
    {
      const Player* holder = engine.getPossessionAttachment();
      if (holder)
      {
        wonAt = now;
        winner = holder->getId() == chaserId ? 1
                 : holder->getId() == mateId ? 2
                 : holder->getId() == rivalId ? 3
                                              : 4;
      }
      else if (engine.getState() != MatchState::PLAYING)
      {
        wonAt = now;
        winner = 0;
      }
    }
    return !(wonAt && now > *wonAt + 0.5) && now < 12.0;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    // The chaser's way round the bystander, while he is on it.
    if (waypoint)
      DrillDraw::marker(renderer, draw, *waypoint, 0.15f, IM_COL32(120, 220, 255, 255));
  }

  std::vector<std::string> report() const override
  {
    static constexpr std::array<const char*, 5> WHO{"nobody (out of play)",
                                                   "the chaser", "the team-mate",
                                                   "the opponent", "someone else"};
    std::vector<std::string> lines;
    lines.push_back(std::format("Ball {:.0f} m/s, chaser {:.0f} m behind, team-mate "
                                "{:.0f} m along ({:+.0f} m off)",
                                ballSpeed, chaserSetting, mateSetting,
                                mateOffsetSetting));
    const auto metres = [&](PlayerID id)
    { return ran.contains(id) ? ran.at(id) : 0.0f; };
    lines.push_back(std::format("Ran: chaser {:.1f} m, team-mate {:.1f} m{}",
                                metres(chaserId), metres(mateId),
                                rivalId ? std::format(", opponent {:.1f} m",
                                                      metres(rivalId))
                                        : std::string()));
    if (bystanderId != 0)
      lines.push_back(std::format(
          "Bystander: closest {:.2f} m{}", closestBystander,
          wentRound == 0 ? std::string(", not gone round")
                         : std::format(", gone round on his {} from {:.1f} s",
                                       wentRound > 0 ? "left" : "right", *roundAt)));
    if (wonAt)
      lines.push_back(std::format("OUTCOME  won by {} at {:.1f} s", WHO[winner],
                                  *wonAt));
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!wonAt) return {};
    const auto metres = [&](PlayerID id)
    { return ran.contains(id) ? static_cast<double>(ran.at(id)) : 0.0; };
    std::vector<DrillMetric> out{{"Winner (1 chaser, 2 mate, 3 opp)", static_cast<double>(winner)},
                                 {"Won at (s)", *wonAt},
                                 {"Chaser ran (m)", metres(chaserId)},
                                 {"Team-mate ran (m)", metres(mateId)}};
    if (rivalId != 0) out.push_back({"Opponent ran (m)", metres(rivalId)});
    if (bystanderId != 0)
    {
      out.push_back({"Closest to bystander (m)", closestBystander});
      out.push_back({"Went round (+1 left, -1 right)", static_cast<double>(wentRound)});
    }
    return out;
  }

 private:
  float ballSpeedSetting = 10.0f;
  float chaserSetting = 3.0f;
  float chaserOffsetSetting = 0.0f;
  float mateSetting = 25.0f;
  float mateOffsetSetting = 2.0f;
  float rivalChoice = 0.0f;
  float bystanderChoice = 0.0f;
  float bystanderAlongSetting = 8.0f;
  float bystanderOffsetSetting = 0.0f;
  float bystanderMoveChoice = 0.0f;
  PlayerID bystanderId = 0;
  float closestBystander = 0.0f;
  int wentRound = 0;
  std::optional<double> roundAt;
  std::optional<PitchPoint> waypoint;

  float ballSpeed = 10.0f;
  int rival = 0;
  PlayerID chaserId = 0;
  PlayerID mateId = 0;
  PlayerID rivalId = 0;
  int winner = 0;
  std::optional<double> wonAt;
  std::map<PlayerID, PitchPoint> previous;
  std::map<PlayerID, float> ran;
};

// --- Tackle --------------------------------------------------------------------

/**
 * Tackle (tackling plan): an attacker dribbles straight at a lone defender,
 * only carrying it (no pass, shot or take-on). The defender's brain decides
 * when to go in, or he is sent at the ball. How the ball was taken: as a
 * free ball (outside the attacker's reach), by the tackle roll, or not at
 * all.
 */
class TackleDrill final : public Drill
{
 public:
  const char* name() const override { return "Tackle"; }
  const char* summary() const override
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
    list.push_back(setting("Gap (m)", &gapSetting, 3.0f, 30.0f, false,
                           "How far ahead of the attacker the defender starts."));
    DrillParameter pace = setting("Attacker carries at", &paceChoice, 0.0f, 3.0f,
                                  true, "How hard he carries it.");
    pace.choices = {"A walk", "A jog", "A run", "A sprint"};
    list.push_back(pace);
    list.push_back(setting("Dribbling", &dribbling, 1.0f, 100.0f, true,
                           "The attacker's: touch length and rhythm."));
    list.push_back(setting("Attacker Pace", &attackerPace, 1.0f, 100.0f, true));
    list.push_back(setting("Defending", &defending, 1.0f, 100.0f, true,
                           "The defender's."));
    list.push_back(setting("Defender Pace", &defenderPace, 1.0f, 100.0f, true));
    DrillParameter moves = setting("Defender", &moveChoice, 0.0f, 1.0f, true,
                                   "How the defender moves (the brain always "
                                   "decides the tackle).");
    moves.choices = {"His brain", "Straight at the ball"};
    list.push_back(moves);
    DrillParameter intent = setting(
        "Defender's intent", &intentChoice, 0.0f, 4.0f, true,
        "What his tackle means, on a free ball (outside the attacker's "
        "reach) or in a block (its push's way): control it; win it (a "
        "nudge away from the attacker, closer the better his Defending); "
        "clear it; a first-time pass to his team-mate; or his brain's "
        "choice.");
    intent.choices = {"Control", "Win it", "Clear", "First-time pass", "His brain"};
    list.push_back(intent);
    list.push_back(setting("Team-mate behind (m)", &mateBehindSetting, -20.0f,
                           30.0f, false,
                           "First-time pass: the defender's team-mate, this "
                           "far behind his start (towards his own goal)."));
    list.push_back(setting("Team-mate across (m)", &mateAcrossSetting, -30.0f,
                           30.0f, false,
                           "First-time pass: and this far across (+ to the "
                           "attacker's left)."));
    return list;
  }

  std::vector<DrillPreset> presets() const override
  {
    return {
        {"Close control", "A good dribbler at a jog: the ball rarely leaves his reach.",
         {{"Attacker carries at", 1.0f}, {"Dribbling", 85.0f}}},
        {"Knocked on", "A poor dribbler at a sprint: long touches, the ball often out of his reach.",
         {{"Attacker carries at", 3.0f}, {"Dribbling", 20.0f}}},
    };
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    std::vector<DrillPlayerSpec> list{
        {"Attacker", PlayerRole::ST, true,
         {{"Dribbling", dribbling}, {"Pace", attackerPace}}},
        {"Defender", PlayerRole::CB, false,
         {{"Defending", defending}, {"Pace", defenderPace}}}};
    if (intentOf() == TackleIntent::PASS)
      list.push_back({"Team-mate", PlayerRole::CB, false});
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    attackerId = ids[0];
    defenderId = ids[1];
    mateId = ids.size() > 2 ? ids[2] : 0;
    gap = gapSetting;
    atBall = moveChoice >= 0.5f;
    const PitchPoint attackerAt = at(0.3f, 0.5f);
    defenderStart = along(attackerAt, {1.0f, 0.0f}, gap);
    mateAt = along(along(defenderStart, {1.0f, 0.0f}, mateBehindSetting),
                   {0.0f, 1.0f}, mateAcrossSetting);
    MatchScenario scenario;
    scenario.players.push_back({attackerId, attackerAt, false});
    scenario.players.push_back({defenderId, defenderStart, false});
    if (mateId != 0) scenario.players.push_back({mateId, mateAt, false});
    scenario.carrierId = attackerId;
    scenario.ballPosition = attackerAt;
    engine.startDrill(scenario);
    engine.keepBall(attackerId, true, paceChoice / 3.0f);
    engine.forceTackleIntent(defenderId, intentOf());
    // The team-mate stays on his spot to be found.
    if (mateId != 0) engine.setDrillTarget(mateId, mateAt, false);
    tackleAt.reset();
    endGap = 0.0f;
    travelled = 0.0f;
    nudgesBefore = engine.getNudges();
    nudgeFrom.reset();
    nudgeEnd.reset();
    nudgeAttackerGap = 0.0f;
    // Straight on, through where the defender stands, to the end.
    endPoint = along(defenderStart, {1.0f, 0.0f}, BEYOND_METRES);
    engine.setDrillTarget(attackerId, along(endPoint, {1.0f, 0.0f}, RUN_THROUGH_METRES),
                          true);
    before = engine.getStats();
    outcome = Outcome::NONE;
    endAt.reset();
    largestGap = 0.0f;
    lastGap = 0.0f;
    freeBehind = 0;
    after = before;
    trail.clear();
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchPlayer* attacker = slotOf(engine, attackerId);
    const MatchPlayer* defender = slotOf(engine, defenderId);
    if (!attacker || !defender) return false;
    const MatchBall& ball = engine.getBall();
    if (atBall && outcome == Outcome::NONE)
      engine.setDrillTarget(defenderId, ball.position, true);
    if (engine.getSimulatedSteps() % 2 == 0) trail.push_back(ball.position);
    // A free-ball tackle this step: was the ball, a step ago, within his
    // reach's distance (so behind him, in his dead zone)?
    if (after.awayFreeBallTackles != engine.getStats().awayFreeBallTackles &&
        lastGap <= MatchTuning::Ball::OUTFIELD_CONTROL_RADIUS_METRES)
      ++freeBehind;
    // Where his first free-ball tackle met it (about: the ball this step).
    if (!tackleAt && after.awayFreeBallTackles != engine.getStats().awayFreeBallTackles)
      tackleAt = ball.position;
    // His first nudge: followed until the ball has (nearly) stopped, he no
    // longer owns it, or a second and a half.
    if (!nudgeFrom && engine.getNudges() > nudgesBefore)
    {
      nudgeFrom = tackleAt.value_or(ball.position);
      nudgeAim = engine.getLastNudgeSpot();
      nudgeSince = now;
    }
    if (nudgeFrom && !nudgeEnd &&
        (std::hypot(ball.velocity.x, ball.velocity.y) < 0.5f ||
         engine.getPossessionAttachment() != defender->player || now - nudgeSince > 1.5))
    {
      nudgeEnd = ball.position;
      nudgeAttackerGap = metresBetween(attacker->position, ball.position);
    }
    after = engine.getStats();
    lastGap = metresBetween(attacker->position, ball.position);
    if (outcome == Outcome::NONE && engine.getPossessionAttachment() == attacker->player)
      largestGap = std::max(largestGap, lastGap);
    if (outcome == Outcome::NONE)
    {
      const MatchPlayer* mate = mateId ? slotOf(engine, mateId) : nullptr;
      const bool sideHasIt =
          engine.getPossessionAttachment() &&
          (engine.getPossessionAttachment() == defender->player ||
           (mate && engine.getPossessionAttachment() == mate->player)) &&
          (!engine.hasTouchClaim() || engine.isTouchCollected());
      if (engine.getState() != MatchState::PLAYING)
        outcome = Outcome::STOPPED;
      else if (sideHasIt)
        outcome = Outcome::WON;
      else if (engine.getPossessionAttachment() == attacker->player &&
               attacker->position.x >= endPoint.x)
        outcome = Outcome::KEPT;
      else if (now > TIME_LIMIT_SECONDS)
        outcome = Outcome::TIME;
      if (outcome != Outcome::NONE)
      {
        endAt = now;
        endGap = metresBetween(attacker->position, ball.position);
        travelled = tackleAt ? metresBetween(*tackleAt, ball.position) : 0.0f;
      }
    }
    return !(endAt && now > *endAt + 0.5);
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    DrillDraw::path(renderer, draw, trail, IM_COL32(255, 220, 120, 200),
                    2.0f * Theme::scale());
    DrillDraw::marker(renderer, draw, defenderStart, 0.3f,
                      IM_COL32(240, 80, 80, 255), "defender's start");
    DrillDraw::line(renderer, draw, along(endPoint, {0.0f, 1.0f}, -6.0f),
                    along(endPoint, {0.0f, 1.0f}, 6.0f), IM_COL32(120, 220, 120, 255),
                    3.0f * Theme::scale());
  }

  std::vector<std::string> report() const override
  {
    static constexpr std::array<const char*, 5> WHAT{
        "", "the defender's side won it", "the attacker reached the end with it",
        "play stopped", "time up"};
    std::vector<std::string> lines;
    lines.push_back(std::format("Gap {:.0f} m, Dribbling {:.0f}, Defending {:.0f}{}",
                                gap, dribbling, defending,
                                atBall ? ", defender sent at the ball" : ""));
    lines.push_back(std::format(
        "Tackles: {} on a free ball ({} won), {} contested ({} won), {} by "
        "the roll ({} won)",
        freeBall(), freeBallWon(), contest(), contestWon(), rolled(), rolledWon()));
    if (endAt)
      lines.push_back(std::format("OUTCOME  {} at {:.1f} s",
                                  WHAT[static_cast<int>(outcome)], *endAt));
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!endAt) return {};
    return {{"Defender's side won it", outcome == Outcome::WON ? 1.0 : 0.0},
            {"Attacker kept it", outcome == Outcome::KEPT ? 1.0 : 0.0},
            {"Largest gap (m)", largestGap},
            {"Ball from attacker at the end (m)", endGap},
            {"Ball from free-ball tackle at the end (m)", travelled},
            {"Nudged", nudgeEnd ? 1.0 : 0.0},
            {"Nudge length (m)",
             nudgeEnd ? metresBetween(*nudgeFrom, *nudgeEnd) : 0.0},
            {"Nudge aimed (m)",
             nudgeEnd ? metresBetween(*nudgeFrom, nudgeAim) : 0.0},
            {"Nudge off its aim (m)",
             nudgeEnd ? metresBetween(nudgeAim, *nudgeEnd) : 0.0},
            {"Nudge: ball from attacker (m)", nudgeAttackerGap},
            {"Free-ball tackles", static_cast<double>(freeBall())},
            {"Free-ball tackles won", static_cast<double>(freeBallWon())},
            {"Free ball behind him", static_cast<double>(freeBehind)},
            {"Contest tackles", static_cast<double>(contest())},
            {"Contest tackles won", static_cast<double>(contestWon())},
            {"Blocks", static_cast<double>(after.homeBlocks + after.awayBlocks -
                                           before.homeBlocks - before.awayBlocks)},
            {"Blocks popped up",
             static_cast<double>(after.blocksPoppedUp - before.blocksPoppedUp)},
            {"Defender went in",
             static_cast<double>(after.awayGoIns - before.awayGoIns)},
            {"Defender held (steps)",
             static_cast<double>(after.awayHolds - before.awayHolds)},
            {"Went in on his free foot",
             static_cast<double>(after.goInsOnFreeFoot - before.goInsOnFreeFoot)},
            {"Attacker's tackles back",
             static_cast<double>(after.homeContestTackles - before.homeContestTackles +
                                 after.homeFreeBallTackles - before.homeFreeBallTackles)},
            {"Attacker swung at nothing",
             static_cast<double>(after.homeCarrierAirSwings -
                                 before.homeCarrierAirSwings)},
            {"Defender swung at nothing",
             static_cast<double>(after.awayTacklerAirSwings -
                                 before.awayTacklerAirSwings)},
            {"Attacker missed, path too new",
             static_cast<double>(after.homeCommitMisses - before.homeCommitMisses)},
            {"Defender missed, path too new",
             static_cast<double>(after.awayCommitMisses - before.awayCommitMisses)},
            {"Rolled tackles", static_cast<double>(rolled())},
            {"Rolled tackles won", static_cast<double>(rolledWon())},
            {"Ended at (s)", *endAt}};
  }

 private:
  enum class Outcome
  {
    NONE,
    WON,
    KEPT,
    STOPPED,
    TIME
  };
  /** The end: this far beyond the defender's start (m). Reached with the
   * ball, the attacker kept it. */
  static constexpr float BEYOND_METRES = 15.0f;
  static constexpr double TIME_LIMIT_SECONDS = 20.0;

  // The defender is the away side.
  int freeBall() const { return after.awayFreeBallTackles - before.awayFreeBallTackles; }
  int freeBallWon() const
  {
    return after.awayFreeBallTacklesWon - before.awayFreeBallTacklesWon;
  }
  int contest() const
  {
    return after.awayContestTackles - before.awayContestTackles;
  }
  int contestWon() const
  {
    return after.awayContestTacklesWon - before.awayContestTacklesWon;
  }
  int rolled() const
  {
    return after.awayTackleAttempts - before.awayTackleAttempts - freeBall() -
           contest();
  }
  int rolledWon() const
  {
    return after.awayTackles - before.awayTackles - freeBallWon() - contestWon();
  }

  float gapSetting = 12.0f;
  float paceChoice = 2.0f;
  float dribbling = 70.0f;
  float attackerPace = 70.0f;
  float defending = 70.0f;
  float defenderPace = 70.0f;
  float moveChoice = 0.0f;
  float intentChoice = 4.0f;
  float mateBehindSetting = 10.0f;
  float mateAcrossSetting = 15.0f;

  TackleIntent intentOf() const
  {
    return static_cast<TackleIntent>(static_cast<int>(intentChoice + 0.5f));
  }

  float gap = 12.0f;
  bool atBall = false;
  PlayerID attackerId = 0;
  PlayerID defenderId = 0;
  PlayerID mateId = 0;
  PitchPoint mateAt{};
  std::optional<PitchPoint> tackleAt;
  float endGap = 0.0f;
  float travelled = 0.0f;
  int nudgesBefore = 0;
  std::optional<PitchPoint> nudgeFrom;
  std::optional<PitchPoint> nudgeEnd;
  PitchPoint nudgeAim{};
  double nudgeSince = 0.0;
  float nudgeAttackerGap = 0.0f;
  PitchPoint defenderStart{};
  PitchPoint endPoint{};
  float largestGap = 0.0f;
  float lastGap = 0.0f;
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
 public:
  const char* name() const override { return "Collision"; }
  const char* summary() const override
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
      DrillParameter runUp = setting("Run-up (m)", &physique.runUp, 0.0f, 40.0f,
                                     false,
                                     "How far he runs, flat out from standing, "
                                     "to the meeting point (0: he stands on it).");
      runUp.section = section;
      list.push_back(runUp);
      list.push_back(setting("Pace", &physique.pace, 1.0f, 100.0f, true));
      list.push_back(setting("Height (cm)", &physique.height, 160.0f, 205.0f, true));
      list.push_back(setting("Strength", &physique.strength, 1.0f, 99.0f, true,
                             "With his height, his weight (BMI 17 at 1 to 31 at "
                             "99)."));
      list.push_back(setting("Balance", &physique.balance, 1.0f, 99.0f, true,
                             "Where his stumble and fall lines sit, and how "
                             "much the outcome wobbles."));
      list.push_back(setting("Agility", &physique.agility, 1.0f, 99.0f, true,
                             "His hardest braking (how sudden a jolt is for "
                             "him) and how fast his body turns."));
    };
    runner("A", a);
    runner("B", b);
    // Names must be unique for presets and sweeps: label the B copies.
    constexpr std::array<const char*, 6> B_NAMES{"B run-up (m)", "B Pace",
                                                 "B height (cm)", "B Strength",
                                                 "B Balance", "B Agility"};
    for (std::size_t index = 0; index < B_NAMES.size(); ++index)
    {
      DrillParameter& parameter = list[B_NAMES.size() + index];
      parameter.label = parameter.name;
      parameter.name = B_NAMES[index];
    }
    DrillParameter angle = setting("B's run against A's (deg)", &angleSetting, 0.0f,
                                   180.0f, true,
                                   "180: head-on; 90: across A's run from his "
                                   "right; 0: the same way (from behind).");
    angle.section = "The meeting";
    list.push_back(angle);
    list.push_back(setting("Miss by (m)", &missSetting, -0.6f, 0.6f, false,
                           "B's line moved this far to his left: 0 centre to "
                           "centre, 0.4 only just touching."));
    list.push_back(setting("Timing nudge (s)", &nudgeSetting, -1.0f, 1.0f, false,
                           "B starts this much later (+) or earlier (-) than "
                           "the drill's estimate of when they meet."));
    DrillParameter ballIn = setting("Ball in after (s)", &ballDelay, 0.0f, 10.0f, false,
                                    "This long after the contact a loose ball is "
                                    "played at whoever is down (A first) from "
                                    "10 m away; 0: no ball.");
    ballIn.section = "Ball at the fallen";
    list.push_back(ballIn);
    list.push_back(setting("Ball speed (m/s)", &ballSpeed, 2.0f, 25.0f, false));
    list.push_back(setting("Ball from (deg)", &ballAngle, 0.0f, 360.0f, true,
                           "Where it comes from round him: 0 from +x (the "
                           "way A ran), 90 from +y."));
    list.push_back(setting("Ball lift (m/s)", &ballLift, 0.0f, 6.0f, false,
                           "Upward speed: a ball above 0.5 m (centre) goes "
                           "over him."));
    return list;
  }

  std::vector<DrillPreset> presets() const override
  {
    return {
        {"Sprinter into a man standing", "A at full speed, B standing facing him.",
         {{"Run-up (m)", 30.0f}, {"B run-up (m)", 0.0f}, {"B's run against A's (deg)", 180.0f}}},
        {"Head-on sprinters", "Both at full speed, straight into each other.",
         {{"Run-up (m)", 30.0f}, {"B run-up (m)", 30.0f}, {"B's run against A's (deg)", 180.0f}}},
        {"Joggers head-on", "Short run-ups: both at about 3 m/s.",
         {{"Run-up (m)", 0.9f}, {"B run-up (m)", 0.9f}, {"B's run against A's (deg)", 180.0f}}},
        {"Light winger into a big defender",
         "A slight 1.70 m into a strong 1.92 m, standing.",
         {{"Run-up (m)", 20.0f}, {"Height (cm)", 170.0f}, {"Strength", 20.0f},
          {"B run-up (m)", 0.0f}, {"B height (cm)", 192.0f}, {"B Strength", 90.0f}}},
        {"Clipped from the side", "B runs across A's sprint.",
         {{"Run-up (m)", 30.0f}, {"B run-up (m)", 6.0f}, {"B's run against A's (deg)", 90.0f}}},
        {"Glancing head-on", "Head-on, the lines 0.35 m apart (of a 0.4 m touch).",
         {{"Run-up (m)", 20.0f}, {"B run-up (m)", 10.0f}, {"B's run against A's (deg)", 180.0f},
          {"Miss by (m)", 0.35f}}},
        {"Ball into a fallen man", "Head-on sprinters; 2 s on, a ball rolled into A from the side.",
         {{"Run-up (m)", 30.0f}, {"B run-up (m)", 30.0f}, {"B's run against A's (deg)", 180.0f},
          {"Ball in after (s)", 2.0f}, {"Ball speed (m/s)", 12.0f}, {"Ball from (deg)", 90.0f},
          {"Ball lift (m/s)", 0.0f}}},
    };
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    const auto spec = [](const char* name, bool home, const Physique& physique)
    {
      DrillPlayerSpec player{name, PlayerRole::CM, home, {{"Pace", physique.pace}},
                             static_cast<int>(physique.height + 0.5f)};
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
    const float radians = angleSetting * PI / 180.0f;
    directionA = {1.0f, 0.0f};
    directionB = {std::cos(radians), std::sin(radians)};
    meetA = MEET;
    meetB = along(MEET, {-directionB.y, directionB.x}, missSetting);
    startA = along(meetA, directionA, -a.runUp);
    startB = along(meetB, directionB, -b.runUp);
    MatchScenario scenario;
    scenario.players.push_back({aId, startA, false});
    scenario.players.push_back({bId, startB, false});
    scenario.ballPosition = ballOutOfTheWay();
    engine.startDrill(scenario);
    // When each sets off: the later arrival first, so they meet.
    const float timeA = a.runUp > 0.0f ? runTime(engine, aId, a.runUp) : 0.0f;
    const float timeB = b.runUp > 0.0f ? runTime(engine, bId, b.runUp) : 0.0f;
    releaseA.reset();
    releaseB.reset();
    if (a.runUp > 0.0f) releaseA = std::max(0.0f, timeB - timeA - nudgeSetting);
    if (b.runUp > 0.0f) releaseB = std::max(0.0f, timeA - timeB + nudgeSetting);
    // Until then each waits on his start, facing his run.
    engine.setDrillTarget(aId, startA, false, {}, 0.0f, 0.0f, facingOf(directionA));
    engine.setDrillTarget(bId, startB, false, {}, 0.0f, 0.0f, facingOf(directionB));
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
    ballSpeedBefore = 0.0f;
    weights = {weightOf(engine, aId), weightOf(engine, bId)};
    trailA.clear();
    trailB.clear();
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const auto release = [&](std::size_t which, const std::optional<float>& at,
                             PlayerID id, PitchPoint meet, Vector2F direction)
    {
      if (released[which] || !at || now + 1e-6 < *at) return;
      released[which] = true;
      engine.setDrillTarget(id, along(meet, direction, BEYOND_METRES), true, {}, 0.0f,
                            0.0f, std::nullopt, true);
    };
    release(0, releaseA, aId, meetA, directionA);
    release(1, releaseB, bId, meetB, directionB);
    for (const MatchEngine::CollisionHit& hit : engine.getLastCollisions())
    {
      const bool ours = (hit.sides[0].player == aId || hit.sides[1].player == aId) &&
                        (hit.sides[0].player == bId || hit.sides[1].player == bId);
      if (!ours) continue;
      hits.push_back(hit);
      if (!first)
      {
        first = hit;
        firstAt = now - STEP + hit.atSeconds;
      }
    }
    const MatchPlayer* playerA = slotOf(engine, aId);
    const MatchPlayer* playerB = slotOf(engine, bId);
    if (!playerA || !playerB) return false;
    if (engine.getSimulatedSteps() % 2 == 0)
    {
      trailA.push_back(playerA->position);
      trailB.push_back(playerB->position);
    }
    // A ring round each while he stumbles (amber) or is down (red).
    for (std::size_t which = 0; which < 2; ++which)
    {
      const MatchPlayer& player = which == 0 ? *playerA : *playerB;
      at[which] = player.position;
      ring[which] = player.fallen ? 2 : player.stumbleSeconds > 0.0f ? 1 : 0;
      if (player.fallen) wentDown[which] = true;
      if (wentDown[which] && !player.fallen && !upAt[which]) upAt[which] = now;
    }
    // The ball at whoever is down (A first), and what it does off him.
    const MatchBall& ballNow = engine.getBall();
    const int offCount = engine.getStats().collisionBallOffFallen;
    if (ballFedAt && !ballOff && offCount > ballOffAtFeed)
    {
      ballOff = BallOff{ballNow.possessionState().deflectedBy && ballNow.possessionState().deflectedBy->getId() == aId ? 'A' : 'B',
                        ballSpeedBefore, length2(ballNow.velocity), now - *ballFedAt};
    }
    ballSpeedBefore = length2(ballNow.velocity);
    if (ballDelay > 0.0f && first && !ballFedAt && now >= firstAt + ballDelay)
    {
      const MatchPlayer* target = playerA->fallen ? playerA : playerB->fallen ? playerB : nullptr;
      ballFedAt = now;
      ballAt = target == playerA ? 1 : target == playerB ? 2 : 0;
      if (target)
      {
        const float radians = ballAngle * PI / 180.0f;
        const Vector2F from{std::cos(radians), std::sin(radians)};
        ballOffAtFeed = offCount;
        engine.feedBall(along(target->position, from, BALL_FROM_METRES),
                        {-from.x * ballSpeed, -from.y * ballSpeed}, ballLift, 0);
      }
    }
    // On until both are on their feet again (and the ball has had time).
    const bool bothUp = (!wentDown[0] || upAt[0]) && (!wentDown[1] || upAt[1]);
    const bool ballDone = ballDelay <= 0.0f || (ballFedAt && now > *ballFedAt + BALL_WATCH_SECONDS);
    ended = (first && bothUp && ballDone && now > firstAt + AFTER_SECONDS) ||
            now >= TIME_LIMIT_SECONDS;
    return !ended;
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::path(renderer, draw, trailA, IM_COL32(120, 200, 255, 200), 2.0f * scale);
    DrillDraw::path(renderer, draw, trailB, IM_COL32(255, 140, 120, 200), 2.0f * scale);
    DrillDraw::marker(renderer, draw, MEET, 0.15f, IM_COL32(240, 240, 240, 255));
    if (first)
      DrillDraw::marker(renderer, draw, first->where, 0.2f, IM_COL32(255, 220, 60, 255));
    // Stumbling: amber; down: red.
    for (std::size_t which = 0; which < 2; ++which)
      if (ring[which] > 0)
        DrillDraw::circle(renderer, draw, at[which], 0.6f, 0,
                          ring[which] == 2 ? IM_COL32(240, 60, 60, 255)
                                           : IM_COL32(255, 180, 40, 255),
                          2.5f * scale);
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "A: {:.0f} m run-up, {:.0f} kg · B: {:.0f} m run-up, {:.0f} kg · B's run "
        "at {:.0f} deg, {:+.2f} m aside",
        a.runUp, weights[0], b.runUp, weights[1], angleSetting, missSetting));
    if (!first)
    {
      lines.push_back(ended ? "OUTCOME  no contact: they missed each other"
                            : "No contact yet.");
      return lines;
    }
    for (const PlayerID id : {aId, bId})
    {
      const MatchEngine::CollisionSide& side = sideOf(*first, id);
      lines.push_back(std::format(
          "{}: {:.1f} -> {:.1f} m/s, jolt {:.2f} m/s · ends up {:.0f}% · sudden "
          "{:.0f}%",
          id == aId ? "A" : "B", speedBefore(side), length2(side.after),
          length2(side.jolt), side.endsUp * 100.0f, side.sudden * 100.0f));
      lines.push_back(std::format(
          "   score {:.0f}% (lines {:.0f}% / {:.0f}%): {}{}{}", side.score * 100.0f,
          side.stumbleLine * 100.0f, side.fallLine * 100.0f, OUTCOMES[side.outcome],
          side.slipped ? ", a stumble that slipped" : "",
          side.outcome == 0   ? std::format(", pause {:.2f} s", side.pause)
          : side.outcome == 1 ? std::format(" for {:.2f} s", side.stumble)
                              : std::string{}));
      if (side.outcome == 2)
      {
        const std::size_t which = id == aId ? 0 : 1;
        lines.push_back(std::format(
            "   lies {} his slide{} · {}", side.lyingAcross ? "across" : "along",
            side.bigHit ? std::format(" · a big hit: down at least {:.1f} s", side.downAtLeast)
                        : std::string{},
            upAt[which] ? std::format("up after {:.1f} s", *upAt[which] - firstAt)
                        : std::string("still down")));
      }
    }
    if (ballFedAt)
      lines.push_back(
          ballAt == 0 ? std::string("Ball: nobody was down to play it at")
          : ballOff  ? std::format("Ball at {}: came off him at {:.1f} s, {:.1f} -> {:.1f} m/s",
                                   ballAt == 1 ? "A" : "B", ballOff->after, ballOff->before,
                                   ballOff->speed)
                     : std::format("Ball at {}: did not hit him (over him, or he was up)",
                                   ballAt == 1 ? "A" : "B"));
    lines.push_back(std::format("Run-ins in all: {} (after the first: driving on "
                                "into each other)",
                                hits.size()));
    lines.push_back(std::format("OUTCOME  contact at {:.2f} s: A {}, B {}", firstAt,
                                OUTCOMES[sideOf(*first, aId).outcome],
                                OUTCOMES[sideOf(*first, bId).outcome]));
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!first) return {{"Contact", 0.0}};
    std::vector<DrillMetric> out{{"Contact", 1.0}, {"Contact at (s)", firstAt}};
    for (const PlayerID id : {aId, bId})
    {
      const MatchEngine::CollisionSide& side = sideOf(*first, id);
      const std::string who = id == aId ? "A " : "B ";
      out.push_back({who + "speed before (m/s)", speedBefore(side)});
      out.push_back({who + "speed after (m/s)", length2(side.after)});
      out.push_back({who + "jolt (m/s)", length2(side.jolt)});
      out.push_back({who + "ends up (%)", side.endsUp * 100.0});
      out.push_back({who + "sudden (%)", side.sudden * 100.0});
      out.push_back({who + "score (%)", side.score * 100.0});
      out.push_back({who + "stumbled", side.outcome == 1 ? 1.0 : 0.0});
      out.push_back({who + "fell", side.outcome == 2 ? 1.0 : 0.0});
      out.push_back({who + "slipped", side.slipped ? 1.0 : 0.0});
      out.push_back({who + "pause (s)", side.pause});
      out.push_back({who + "stumble (s)", side.stumble});
      out.push_back({who + "big hit", side.bigHit ? 1.0 : 0.0});
      out.push_back({who + "lies across", side.outcome == 2 && side.lyingAcross ? 1.0 : 0.0});
      const std::size_t which = id == aId ? 0 : 1;
      out.push_back({who + "down (s)", upAt[which] ? *upAt[which] - firstAt : 0.0});
    }
    out.push_back({"Run-ins", static_cast<double>(hits.size())});
    out.push_back({"Ball off the fallen", ballOff ? 1.0 : 0.0});
    out.push_back({"Ball speed in (m/s)", ballOff ? ballOff->before : 0.0});
    out.push_back({"Ball speed off (m/s)", ballOff ? ballOff->speed : 0.0});
    return out;
  }

 private:
  struct Physique
  {
    float runUp = 20.0f;
    float pace = 70.0f;
    float height = 180.0f;
    float strength = 50.0f;
    float balance = 50.0f;
    float agility = 50.0f;
  };
  static constexpr std::array<const char*, 3> OUTCOMES{
      "kept his feet and balance", "stumbled", "fell"};
  static inline const PitchPoint MEET = at(0.5f, 0.5f);
  /** Each runs on through the meeting point to this far beyond it. */
  static constexpr float BEYOND_METRES = 20.0f;
  /** The run goes on at least this long after the first contact, and until
   * anyone knocked over is up. */
  static constexpr double AFTER_SECONDS = 2.0;
  static constexpr double TIME_LIMIT_SECONDS = 45.0;
  /** The ball comes from this far away and is watched this long. */
  static constexpr float BALL_FROM_METRES = 10.0f;
  static constexpr double BALL_WATCH_SECONDS = 3.0;

  static float length2(Vector2F vector) { return std::hypot(vector.x, vector.y); }
  static float speedBefore(const MatchEngine::CollisionSide& side)
  {
    return std::hypot(side.after.x - side.jolt.x, side.after.y - side.jolt.y);
  }
  static const MatchEngine::CollisionSide& sideOf(const MatchEngine::CollisionHit& hit,
                                                  PlayerID id)
  {
    return hit.sides[0].player == id ? hit.sides[0] : hit.sides[1];
  }
  /** A direction in metres as a facing angle (normalised pitch space). */
  static float facingOf(Vector2F direction)
  {
    return std::atan2(direction.y / WIDTH, direction.x / LENGTH);
  }
  static float weightOf(const MatchEngine& engine, PlayerID id)
  {
    const MatchPlayer* player = slotOf(engine, id);
    return player ? bodyMassKilograms(player->heightMetres,
                                      player->derivedStat(DerivedStat::STRENGTH))
                  : 0.0f;
  }
  /** About how long he takes to run `metres` flat out from standing:
   * a(v) = A0 (1 - v / vmax), so x(t) = vmax (t - tau (1 - e^(-t/tau))). */
  static float runTime(const MatchEngine& engine, PlayerID id, float metres)
  {
    const MatchPlayer* player = slotOf(engine, id);
    if (!player) return 0.0f;
    const float top = std::max(player->maxSpeed, 1.0f);
    const float push = std::max(
        derivedPhysicalValue(DerivedStat::ACCELERATION,
                             player->derivedStat(DerivedStat::ACCELERATION)),
        1.0f);
    const float tau = top / push;
    const auto distance = [&](float time)
    { return top * (time - tau * (1.0f - std::exp(-time / tau))); };
    float low = 0.0f;
    float high = 30.0f;
    for (int pass = 0; pass < 40; ++pass)
    {
      const float middle = 0.5f * (low + high);
      (distance(middle) < metres ? low : high) = middle;
    }
    return 0.5f * (low + high);
  }

  Physique a{30.0f, 70.0f, 180.0f, 50.0f, 50.0f, 50.0f};
  Physique b{0.0f, 70.0f, 180.0f, 50.0f, 50.0f, 50.0f};
  float angleSetting = 180.0f;
  float missSetting = 0.0f;
  float nudgeSetting = 0.0f;

  PlayerID aId = 0;
  PlayerID bId = 0;
  Vector2F directionA{};
  Vector2F directionB{};
  PitchPoint meetA{};
  PitchPoint meetB{};
  PitchPoint startA{};
  PitchPoint startB{};
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
  float ballDelay = 0.0f;
  float ballSpeed = 12.0f;
  float ballAngle = 90.0f;
  float ballLift = 0.0f;
  std::optional<double> ballFedAt;
  int ballAt = 0;
  int ballOffAtFeed = 0;
  float ballSpeedBefore = 0.0f;
  struct BallOff
  {
    char who = 'A';
    float before = 0.0f;
    float speed = 0.0f;
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
 public:
  static constexpr float KEEPER_OFF_LINE_METRES = 1.5f;

  const char* name() const override { return "Shot"; }
  const char* summary() const override
  {
    return "A shooter on the ball at a spot, against a keeper and a "
           "defender if you like. He shoots at once, or decides for himself "
           "(where does he choose to shoot from?). The outcome against the "
           "engine's own xG for the shot. The Shot map runs a grid of spots "
           "for the calibration heatmap.";
  }

  bool offersShotMap() const override { return true; }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Out from goal line (m)", &line, 1.0f, 45.0f, false,
                           "Distance of the spot from the goal line."));
    list.push_back(setting("Off centre (m)", &side, -30.0f, 30.0f, false,
                           "Sideways from the middle of the goal (+ towards "
                           "the bottom touchline)."));
    DrillParameter mode = setting("Shooter", &choice, 0.0f, 1.0f, true,
                                  "Shoots now: through the AI's own shot, at "
                                  "the first moment he can.\nDecides: the AI "
                                  "plays on (carries, shoots when it wants).");
    mode.choices = {"Shoots now", "Decides"};
    list.push_back(mode);
    list.push_back(setting("Shooting", &shooting, 1.0f, 100.0f, true));
    DrillParameter keeper = setting("Keeper", &keeperChoice, 0.0f, 1.0f, true);
    keeper.choices = {"None", "In goal"};
    list.push_back(keeper);
    list.push_back(setting("Goalkeeping", &goalkeeping, 1.0f, 100.0f, true));
    DrillParameter start = setting(
        "Keeper starts", &keeperStart, 0.0f, 1.0f, true,
        "Covering the angle: 1.5 m off his line towards the ball, as a keeper "
        "sets himself.\nCentre of goal: on his line in the middle (as if he "
        "had no time to set).");
    start.choices = {"Covering the angle", "Centre of goal"};
    list.push_back(start);
    list.push_back(setting("Keeper height (cm)", &keeperHeight, 165.0f, 205.0f,
                           true));
    DrillParameter defender =
        setting("Defender", &defenderChoice, 0.0f, 1.0f, true,
                "A defender on the line from the spot to the middle of the "
                "goal.");
    defender.choices = {"None", "In the way"};
    list.push_back(defender);
    list.push_back(setting("Defender distance (m)", &defenderDistance, 0.5f,
                           15.0f, false));
    list.push_back(setting("Defending", &defending, 1.0f, 100.0f, true));
    return list;
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    std::vector<DrillPlayerSpec> list{
        {"Shooter", PlayerRole::ST, true, {{"Shooting", shooting}}, 180}};
    if (keeperChoice > 0.5f)
      list.push_back({"Keeper", PlayerRole::GK, false,
                      {{"Goalkeeping", goalkeeping}},
                      static_cast<int>(keeperHeight)});
    if (defenderChoice > 0.5f)
      list.push_back(
          {"Defender", PlayerRole::CB, false, {{"Defending", defending}}, 185});
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    shooterId = ids[0];
    hasKeeper = keeperChoice > 0.5f;
    hasDefender = defenderChoice > 0.5f;
    keeperId = hasKeeper ? ids[1] : 0;
    defenderId = hasDefender ? ids[hasKeeper ? 2 : 1] : 0;
    forced = choice < 0.5f;
    spot = at(std::clamp(1.0f - line / LENGTH, 0.5f, 0.995f),
            std::clamp(0.5f + side / WIDTH, 0.01f, 0.99f));
    MatchScenario scenario;
    scenario.players.push_back({shooterId, spot, false});
    if (hasKeeper)
    {
      // Covering the angle: a step off the line towards the ball, as a
      // keeper sets himself; or the middle of the goal (no time to set).
      const PitchPoint goal = at(1.0f, 0.5f);
      keeperAt = keeperStart < 0.5f
                     ? along(goal, directionBetween(goal, spot),
                             std::min(KEEPER_OFF_LINE_METRES,
                                      metresBetween(goal, spot) * 0.5f))
                     : at(0.985f, 0.5f);
      scenario.players.push_back({keeperId, keeperAt, false});
    }
    if (hasDefender)
    {
      const PitchPoint goal = at(1.0f, 0.5f);
      const float towards =
          std::min(defenderDistance, metresBetween(spot, goal) - 1.0f);
      defenderAt =
          along(spot, directionBetween(spot, goal), std::max(0.5f, towards));
      scenario.players.push_back({defenderId, defenderAt, false});
    }
    scenario.carrierId = shooterId;
    scenario.ballPosition = spot;
    engine.startDrill(scenario);
    if (forced) engine.forceShot(shooterId);
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
        shooter && !shot && engine.getSimulatedSteps() % 2 == 0)
      path.push_back(shooter->position);
    const auto& events = engine.getEvents();
    for (; seenEvents < events.size() && outcome.empty(); ++seenEvents)
    {
      const MatchEvent& event = events[seenEvents];
      if (event.type == MatchEventType::SHOT && !shot &&
          event.primaryPlayerId == shooterId)
      {
        const MatchBall& ball = engine.getBall();
        shot = Shot{event.position, event.xg, ball.possessionState().shotOnTarget,
                    ball.possessionState().shotTargetY, now};
        continue;
      }
      if (!shot)
      {
        // Before any shot: the ball went out or play stopped.
        if (event.type == MatchEventType::GOAL_KICK ||
            event.type == MatchEventType::CORNER ||
            event.type == MatchEventType::THROW_IN ||
            event.type == MatchEventType::FOUL)
          outcome = "no shot (play stopped)";
        continue;
      }
      switch (event.type)
      {
        case MatchEventType::GOAL:
        case MatchEventType::OWN_GOAL: outcome = "goal"; break;
        case MatchEventType::SAVE: outcome = "saved"; break;
        case MatchEventType::WOODWORK: outcome = "woodwork"; break;
        case MatchEventType::SHOT_BLOCKED: outcome = "blocked"; break;
        // The engine logs no "off target": a miss goes out of play.
        case MatchEventType::GOAL_KICK:
        case MatchEventType::CORNER:
        case MatchEventType::THROW_IN: outcome = "off target"; break;
        default: break;
      }
    }
    const MatchBall& ball = engine.getBall();
    if (outcome.empty() && shot && !ball.possessionState().isShot && engine.getPossessionAttachment() &&
        engine.getPossessionAttachment()->getId() == keeperId)
      // Gathered with no event: a weak shot the keeper simply picks up.
      outcome = shot->onTarget ? "saved" : "off target";
    if (outcome.empty() && !shot && engine.getPossessionAttachment() &&
        engine.getPossessionAttachment()->getId() != shooterId)
      outcome = "no shot (lost the ball)";
    if (outcome.empty() && !shot && now >= (forced ? 8.0 : 15.0))
      outcome = "no shot (time up)";
    if (outcome.empty() && shot && now >= shot->at + 6.0) outcome = "no outcome";
    if (!outcome.empty() && !endAt)
    {
      endAt = now;
      tally(outcome);
    }
    return !(endAt && now >= *endAt + 0.5);
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::path(renderer, draw, path, IM_COL32(255, 255, 255, 120),
                    1.5f * scale);
    DrillDraw::marker(renderer, draw, spot, 4.0f * scale,
                      IM_COL32(90, 220, 120, 255), "spot");
    if (hasDefender)
      DrillDraw::marker(renderer, draw, defenderAt, 2.5f * scale,
                        IM_COL32(240, 80, 80, 200));
    if (shot)
    {
      const ImU32 colour = outcome == "goal"    ? IM_COL32(90, 220, 120, 255)
                           : outcome == "saved" ? IM_COL32(90, 170, 255, 255)
                           : outcome.empty()    ? IM_COL32(255, 255, 255, 220)
                                                : IM_COL32(240, 140, 60, 255);
      DrillDraw::line(renderer, draw, shot->from, PitchPoint{PITCH.atX(1.0f), shot->targetY}, colour,
                      2.0f * scale);
      DrillDraw::label(renderer, draw, shot->from,
                       std::format("xG {:.2f}{}", shot->xg,
                                   outcome.empty() ? "" : "  " + outcome)
                           .c_str(),
                       colour);
    }
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.1f} m out, {:+.1f} m off centre · {} · Shooting {:.0f}", line, side,
        forced ? "shoots now" : "decides", shooting));
    lines.push_back(std::format(
        "{} · {}",
        hasKeeper ? std::format("keeper (GK {:.0f}, {:.0f} cm, {})", goalkeeping,
                                keeperHeight,
                                keeperStart < 0.5f ? "covering the angle"
                                                   : "from the centre")
                  : std::string("empty goal"),
        hasDefender ? std::format("defender {:.1f} m in the way (Def {:.0f})",
                                  defenderDistance, defending)
                    : std::string("no defender")));
    if (shot)
      lines.push_back(std::format(
          "Shot after {:.1f} s from {:.1f} m out, {:+.1f} m: xG {:.3f}, {}",
          shot->at, PITCH.metresX(PITCH.mirrorX(shot->from.x)),
          PITCH.metresY(shot->from.y - PITCH.atY(0.5f)), shot->xg,
          shot->onTarget ? "on target" : "off target"));
    if (!outcome.empty()) lines.push_back("OUTCOME  " + outcome);
    // Every run with these settings so far (Watch: restart for another).
    if (const auto found = tallies.find(settingsKey()); found != tallies.end())
    {
      const Tally& t = found->second;
      lines.push_back(std::format(
          "These settings so far: {} runs, {} shots, {} goals ({:.0f}%), xG "
          "{:.2f}",
          t.runs, t.shots, t.goals,
          t.shots ? 100.0 * t.goals / t.shots : 0.0, t.xg));
    }
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty() || outcome == "no outcome") return {};
    const auto is = [&](std::string_view name)
    { return outcome == name ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{{"Shot", shot ? 1.0 : 0.0}};
    if (shot)
    {
      out.push_back({"Goal", is("goal")});
      out.push_back({"xG", shot->xg});
      out.push_back({"On target", shot->onTarget ? 1.0 : 0.0});
      out.push_back({"Saved", is("saved")});
      out.push_back({"Off target", is("off target")});
      out.push_back({"Woodwork", is("woodwork")});
      out.push_back({"Blocked", is("blocked")});
      out.push_back({"Shot from goal line (m)", PITCH.metresX(PITCH.mirrorX(shot->from.x))});
      out.push_back({"Shot off centre (m)", PITCH.metresY(shot->from.y - PITCH.atY(0.5f))});
      out.push_back({"Seconds to shoot", shot->at});
    }
    else
    {
      out.push_back({"Lost the ball", is("no shot (lost the ball)")});
    }
    return out;
  }

 private:
  struct Shot
  {
    PitchPoint from;
    float xg = 0.0f;
    bool onTarget = false;
    float targetY = 0.5f;
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
    return std::format("{:.1f}|{:.1f}|{}|{:.0f}|{}|{:.0f}|{:.0f}|{}|{:.1f}|{:.0f}",
                       line, side, forced, shooting, hasKeeper, goalkeeping,
                       keeperHeight, hasDefender, defenderDistance, defending) +
           std::format("|{:.0f}", keeperStart);
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
    if (result == "goal") ++t.goals;
  }

  float line = 16.0f;
  float side = 4.0f;
  float choice = 0.0f;
  float shooting = 70.0f;
  float keeperChoice = 1.0f;
  float goalkeeping = 70.0f;
  float keeperHeight = 188.0f;
  float keeperStart = 0.0f;
  PitchPoint keeperAt{};
  float defenderChoice = 0.0f;
  float defenderDistance = 3.0f;
  float defending = 70.0f;

  PlayerID shooterId = 0;
  PlayerID keeperId = 0;
  PlayerID defenderId = 0;
  bool hasKeeper = true;
  bool hasDefender = false;
  bool forced = true;
  PitchPoint spot{};
  PitchPoint defenderAt{};
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
 public:
  const char* name() const override { return "Shot block"; }
  const char* summary() const override
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
    list.push_back(setting("Out from goal line (m)", &line, 6.0f, 40.0f, false,
                           "Where the shooter stands."));
    list.push_back(setting("Off centre (m)", &side, -20.0f, 20.0f, false));
    list.push_back(setting("Aim across goal (m)", &aimAcross, -3.6f, 3.6f, false,
                           "Where in the goal he shoots (+ towards pitch y = 1)."));
    list.push_back(setting("Aim height (m)", &aimHeight, 0.0f, 2.4f, false,
                           "How high at the goal: 0 along the ground."));
    list.push_back(setting("Power (share)", &power, 0.3f, 1.0f, false,
                           "Share of his shot power."));
    list.push_back(setting("Shooting", &shooting, 1.0f, 100.0f, true));
    DrillParameter kind = setting("Ball", &ballKind, 0.0f, 1.0f, true,
                                  "Shot: at the goal.\nPass: along the ground to the same "
                                  "spot, nobody there (pass block plan): the defender "
                                  "intercepts it (today's touch) or it meets his body.");
    kind.choices = {"Shot", "Pass"};
    list.push_back(kind);
    DrillParameter defender = setting("Defender distance (m)", &distance, 0.5f, 25.0f,
                                      false, "From the shooter along the shot's line.");
    defender.section = "Defender";
    list.push_back(defender);
    list.push_back(setting("Off the line (m)", &offLine, -2.5f, 2.5f, false,
                           "Sideways from the shot's line (+ to the shooter's left)."));
    DrillParameter movement = setting(
        "Moving", &moving, 0.0f, 2.0f, true,
        "Standing: facing the shooter.\nSidestepping: across the line, facing "
        "the shooter.\nSprinting: across the line, facing his run.");
    movement.choices = {"Standing", "Sidestepping", "Sprinting"};
    list.push_back(movement);
    list.push_back(setting("Run-up (m)", &runUp, 2.0f, 25.0f, false,
                           "Moving: how far back along his run he starts. The shot "
                           "is struck as he is on course to be off the line where "
                           "set when the ball gets there."));
    list.push_back(setting("Defending", &defending, 1.0f, 100.0f, true,
                           "His reaction: 0.35 s at 1 to 0.15 s at 99."));
    list.push_back(setting("Tackling", &tackling, 1.0f, 100.0f, true,
                           "His leg out to full reach: 0.45 s at 1 to 0.25 s at "
                           "99; his miss 0.25 m to 0.05 m (doubled when rushed)."));
    list.push_back(setting("Height (cm)", &height, 160.0f, 205.0f, true));
    DrillParameter keeper = setting("Keeper", &keeperChoice, 0.0f, 1.0f, true,
                                    "In goal: on his line in the middle, reading the "
                                    "shot (and a deflection still going in, afresh).");
    keeper.choices = {"None", "In goal"};
    keeper.section = "Keeper";
    list.push_back(keeper);
    list.push_back(setting("Goalkeeping", &goalkeeping, 1.0f, 100.0f, true));
    return list;
  }

  std::vector<DrillPreset> presets() const override
  {
    return {
        {"Close, straight at him", "2 m from the shooter, on the line: too close to react.",
         {{"Defender distance (m)", 2.0f}, {"Off the line (m)", 0.0f}, {"Moving", 0.0f}}},
        {"Reach for it", "8 m out, 0.7 m off the line: time to put a leg across.",
         {{"Defender distance (m)", 8.0f}, {"Off the line (m)", 0.7f}, {"Moving", 0.0f}}},
        {"Edge of his reach", "10 m out, 1.0 m off the line.",
         {{"Defender distance (m)", 10.0f}, {"Off the line (m)", 1.0f}, {"Moving", 0.0f}}},
        {"Sprinting across", "8 m out, sprinting across 0.8 m off the line.",
         {{"Defender distance (m)", 8.0f},
          {"Off the line (m)", 0.8f},
          {"Moving", 2.0f},
          {"Run-up (m)", 15.0f}}},
    };
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec defender{"Defender", PlayerRole::CB, false, {{"Defending", defending}},
                             static_cast<int>(height)};
    defender.derived[DerivedStat::TACKLING] = tackling;
    std::vector<DrillPlayerSpec> list{
        {"Shooter", PlayerRole::ST, true, {{"Shooting", shooting}}, 180}, defender};
    if (keeperChoice > 0.5f)
      list.push_back({"Keeper", PlayerRole::GK, false, {{"Goalkeeping", goalkeeping}}, 188});
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    shooterId = ids[0];
    defenderId = ids[1];
    spot = at(std::clamp(1.0f - line / LENGTH, 0.5f, 0.995f),
            std::clamp(0.5f + side / WIDTH, 0.01f, 0.99f));
    const PitchPoint aimAt = at(1.0f, 0.5f + aimAcross / WIDTH);
    const Vector2F towards = directionBetween(spot, aimAt);
    // The shooter's left, looking along the shot.
    const Vector2F left{towards.y, -towards.x};
    const float there = std::min(distance, metresBetween(spot, aimAt) - 0.5f);
    defenderAt = along(along(spot, towards, there), left, offLine);
    // Moving: across the line, towards the shooter's left, from his run-up.
    const bool runs = moving > 0.5f;
    runLeft = left;
    defenderOut = there;
    start = runs ? along(defenderAt, left, -runUp) : defenderAt;
    MatchScenario scenario;
    scenario.players.push_back({shooterId, spot, false});
    scenario.players.push_back({defenderId, start, false});
    if (keeperChoice > 0.5f) scenario.players.push_back({ids[2], at(0.985f, 0.5f), false});
    scenario.carrierId = shooterId;
    scenario.ballPosition = spot;
    engine.startDrill(scenario);
    const Vector2F toShooter = directionBetween(defenderAt, spot);
    const float faceShooter = facingAngleOf(std::atan2(toShooter.y, toShooter.x));
    if (runs)
      engine.setDrillTarget(defenderId, along(defenderAt, left, 30.0f), moving > 1.5f, {}, 0.0f,
                            0.0f,
                            moving < 1.5f ? std::optional<float>(faceShooter) : std::nullopt,
                            moving > 1.5f);
    else
      engine.setDrillTarget(defenderId, defenderAt, false, {}, 0.0f, 0.0f, faceShooter);
    shotAimed = false;
    if (!runs)
    {
      shoot(engine);
    }
    else
    {
      // He waits on his spot, facing the goal, until the shot is called.
      engine.keepBall(shooterId, true, 0.0f);
      engine.setDrillTarget(shooterId, spot, false, {}, 0.0f, 0.0f,
                            facingAngleOf(std::atan2(towards.y, towards.x)));
    }
    seenEvents = engine.getEvents().size();
    shotAt.reset();
    outcome.clear();
    endAt.reset();
    legs.reset();
    closest = 99.0f;
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

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchPlayer* defender = slotOf(engine, defenderId);
    // Moving: struck once he is on course to be at his mark as the ball
    // gets there (a shot of about BALL_SPEED, struck STRIKE_DELAY after
    // it is called for).
    if (!shotAimed && defender)
    {
      constexpr float BALL_SPEED = 27.0f;
      constexpr float STRIKE_DELAY = 0.1f;
      const float lead = defenderOut / BALL_SPEED + STRIKE_DELAY;
      const Vector2F at = toMetres(defender->position);
      const Vector2F mark = toMetres(defenderAt);
      const float ahead = (at.x + defender->velocity.x * lead - mark.x) * runLeft.x +
                          (at.y + defender->velocity.y * lead - mark.y) * runLeft.y;
      if (ahead >= 0.0f) shoot(engine);
    }
    // A pass: on its way once it has left him; then off his body, his
    // ball (intercepted), or past him.
    if (ballKind > 0.5f && shotAimed && defender)
    {
      const MatchBall& ball = engine.getBall();
      if (!shotAt && ball.possessionState().isPass && !(engine.getPossessionAttachment() && engine.getPossessionAttachment()->getId() == shooterId))
      {
        shotAt = now;
        speedAtShot = speedOf(*defender);
        const Vector2F at = toMetres(defender->position);
        const Vector2F from = toMetres(spot);
        offLineAtShot = (at.x - from.x) * runLeft.x + (at.y - from.y) * runLeft.y;
        seenEvents = engine.getEvents().size();
      }
      if (shotAt && outcome.empty())
      {
        const MatchStats& stats = engine.getStats();
        noTouchNow = stats.ballBodyAfterNoTouch;
        int hits = 0;
        for (std::size_t index = 0; index < bodyHitsBefore.size(); ++index)
          hits += stats.ballBodyHitsByPart[index] - bodyHitsBefore[index];
        if (hits > 0)
          outcome = "blocked";
        else if (engine.getPossessionAttachment() && engine.getPossessionAttachment()->getId() == defenderId)
          outcome = "intercepted";
        else if (now >= *shotAt + 4.0)
          outcome = "past him";
      }
    }
    const auto& events = engine.getEvents();
    for (; seenEvents < events.size() && outcome.empty(); ++seenEvents)
    {
      const MatchEvent& event = events[seenEvents];
      if (event.type == MatchEventType::SHOT && event.primaryPlayerId == shooterId && !shotAt)
      {
        shotAt = now;
        speedAtShot = defender ? speedOf(*defender) : 0.0f;
        if (defender)
        {
          const Vector2F at = toMetres(defender->position);
          const Vector2F from = toMetres(spot);
          offLineAtShot = (at.x - from.x) * runLeft.x + (at.y - from.y) * runLeft.y;
        }
        continue;
      }
      if (!shotAt) continue;
      switch (event.type)
      {
        case MatchEventType::GOAL:
        case MatchEventType::OWN_GOAL: outcome = "goal"; break;
        case MatchEventType::WOODWORK: outcome = "woodwork"; break;
        case MatchEventType::SHOT_BLOCKED: outcome = "blocked"; break;
        case MatchEventType::SAVE: outcome = "saved"; break;
        case MatchEventType::GOAL_KICK:
        case MatchEventType::CORNER:
        case MatchEventType::THROW_IN: outcome = "off target"; break;
        default: break;
      }
    }
    // His legs as the shot met them, up to the step it ended (the record
    // stays as the ball last met him).
    if (defender && shotAt && !endAt)
    {
      const auto slot = static_cast<std::size_t>(defender - engine.getPlayers().data());
      if (const auto recent = engine.recentShotLegs(slot, 10.0f)) legs = recent->first;
      closest = std::min(closest, metresBetween(engine.getBall().position, defender->position));
      throughNow = engine.getStats().shotTorsoPassThroughs;
      insideNow = engine.getStats().shotTorsoInsideAtStart;
    }
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
      const Vector2F shotLine = directionBetween(spot, at(1.0f, 0.5f + aimAcross / WIDTH));
      offTurn = offSpeed > 1e-3f
                    ? turnDegrees(shotLine, {off.velocity.x / offSpeed, off.velocity.y / offSpeed})
                    : 0.0f;
      const MatchStats& stats = engine.getStats();
      static constexpr std::array<const char*, 7> PARTS{"torso", "thigh", "shin", "foot",
                                                        "head",  "hand",  "arm"};
      const bool pass = ballKind > 0.5f;
      for (std::size_t index = 0; index < PARTS.size(); ++index)
        if ((pass ? stats.ballBodyHitsByPart[index] - bodyHitsBefore[index]
                  : stats.shotBlocksByPart[index] - blocksBefore[index]) > 0)
          part = PARTS[index];
      if (!part.empty() && part != "torso" && part != "head")
        part += (pass ? stats.ballReactionBlocks > ballReactionBefore
                      : stats.shotReactionBlocks > reactionBefore)
                    ? " (reaction block)"
                    : " (stride)";
      // Still going in off him: still the shot (shot block plan step 4).
      stillShot = engine.getBall().possessionState().isShot;
    }
    // After a block: what came of it (a goal, in off him or from a rebound;
    // a save; out; or someone's).
    if (outcome == "blocked" && afterBlock.empty())
    {
      for (; seenEvents < events.size() && afterBlock.empty(); ++seenEvents)
        switch (events[seenEvents].type)
        {
          case MatchEventType::GOAL:
          case MatchEventType::OWN_GOAL: afterBlock = "goal"; break;
          case MatchEventType::SAVE: afterBlock = "saved"; break;
          case MatchEventType::CORNER: afterBlock = "corner"; break;
          case MatchEventType::GOAL_KICK:
          case MatchEventType::THROW_IN: afterBlock = "out"; break;
          default: break;
        }
      if (afterBlock.empty() && engine.getPossessionAttachment() && !engine.hasTouchClaim())
        afterBlock = engine.getPossessionAttachment()->getId() == defenderId ? "his" : "shooter's";
      if (afterBlock.empty() && endAt && now >= *endAt + 4.0) afterBlock = "loose";
    }
    if (outcome.empty() && !shotAt && now >= 10.0) outcome = "no shot";
    if (outcome.empty() && shotAt && now >= *shotAt + 6.0) outcome = "no outcome";
    if (!outcome.empty() && !endAt)
    {
      endAt = now;
      tally();
    }
    return !(endAt && now >= *endAt + 1.0 && (outcome != "blocked" || !afterBlock.empty()));
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::line(renderer, draw, spot, at(1.0f, 0.5f + aimAcross / WIDTH),
                    IM_COL32(255, 255, 255, 90), 1.5f * scale);
    DrillDraw::marker(renderer, draw, spot, 4.0f * scale, IM_COL32(90, 220, 120, 255),
                      "shooter");
    DrillDraw::marker(renderer, draw, defenderAt, 2.5f * scale, IM_COL32(240, 80, 80, 200),
                      moving > 0.5f ? "at the strike" : nullptr);
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    static constexpr std::array<const char*, 3> MOVES{"standing", "sidestepping",
                                                     "sprinting"};
    lines.push_back(std::format(
        "Defender {:.1f} m out, {:+.2f} m off the line, {} · Def {:.0f}, Tackling {:.0f}, "
        "{:.0f} cm",
        distance, offLine, MOVES[std::clamp(static_cast<int>(moving + 0.5f), 0, 2)], defending,
        tackling, height));
    if (shotAt)
      lines.push_back(std::format("Shot struck at {:.2f} s; defender moving {:.1f} m/s", *shotAt,
                                  speedAtShot));
    if (legs)
    {
      if (!legs->reacted)
        lines.push_back("Not reacted when the ball got to him: body and stride only");
      else if (!legs->blocking)
        lines.push_back(std::format("Reacted; no leg: {} (line {:.2f} m from him)",
                                    legs->noBlock, legs->lineGap));
      else
        lines.push_back(std::format(
            "Leg across the line {:.2f} m from him: {:.0f}% out when met, needs {:.2f} s, "
            "{:+.2f} s to spare, miss {:.2f} m (of {:.2f})",
            legs->lineGap, 100.0f * legs->outShare, legs->legSeconds, legs->spareSeconds,
            legs->error, legs->errorRadius));
    }
    if (!outcome.empty())
      lines.push_back("OUTCOME  " + outcome + (part.empty() ? "" : " off his " + part));
    if (outcome == "blocked")
      lines.push_back(std::format("{} off him; then: {}", stillShot ? "Still a shot" : "Loose",
                                  afterBlock.empty() ? "..." : afterBlock));
    if (outcome == "blocked")
      lines.push_back(std::format("Off him at {:.1f} m/s, turned {:.0f} deg from the shot's line, "
                                  "{:+.1f} m/s up; spin {:+.0f} side, {:+.0f} top (rad/s)",
                                  offSpeed, offTurn, offUp, offSpin.side, offSpin.top));
    if (const auto found = tallies.find(settingsKey()); found != tallies.end())
    {
      const Tally& t = found->second;
      lines.push_back(std::format(
          "These settings so far: {} shots, {} blocked ({:.0f}%): {} torso, {} stride leg, {} "
          "reaction leg",
          t.shots, t.blocked, t.shots ? 100.0 * t.blocked / t.shots : 0.0, t.torso, t.stride,
          t.reaction));
    }
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!shotAt || outcome.empty() || outcome == "no outcome") return {};
    const bool blocked = outcome == "blocked";
    const bool reaction = part.find("reaction") != std::string::npos;
    const bool leg = legs && legs->blocking;
    const bool reacted = legs && legs->reacted;
    const auto noLeg = [&](std::string_view why)
    { return reacted && !leg && std::string_view(legs->noBlock).find(why) != std::string_view::npos
                 ? 1.0
                 : 0.0; };
    return {
        {"Blocked", blocked ? 1.0 : 0.0},
        {"Torso block", blocked && part == "torso" ? 1.0 : 0.0},
        {"Stride block", blocked && !reaction && part != "torso" && part != "head" ? 1.0 : 0.0},
        {"Head block", blocked && part == "head" ? 1.0 : 0.0},
        {"Reaction block", blocked && reaction ? 1.0 : 0.0},
        {"Line gap (m)", reacted ? legs->lineGap : 0.0},
        {"Ball height (m)", reacted ? legs->lineHeight : 0.0},
        {"Speed (m/s)", speedAtShot},
        {"Off the line at strike (m)", offLineAtShot},
        {"Closest ball (m)", closest},
        {"Reacted", legs && legs->reacted ? 1.0 : 0.0},
        {"Leg out", leg ? 1.0 : 0.0},
        {"Leg out share", leg ? legs->outShare : 0.0},
        {"Miss (m)", leg ? legs->error : 0.0},
        {"Spare (s)", leg ? legs->spareSeconds : 0.0},
        {"Reaction (s)", reacted ? legs->legFrom - legs->pathFor : 0.0},
        {"Ball to him (s)", reacted ? legs->ballThere - legs->pathFor : 0.0},
        {"No leg: out of reach", noLeg("reach")},
        {"No leg: behind him", noLeg("behind")},
        {"No leg: past him", noLeg("past")},
        {"Still a shot", blocked && stillShot ? 1.0 : 0.0},
        {"Goal after", afterBlock == "goal" ? 1.0 : 0.0},
        {"Saved after", afterBlock == "saved" ? 1.0 : 0.0},
        {"Corner after", afterBlock == "corner" ? 1.0 : 0.0},
        {"His ball after", afterBlock == "his" ? 1.0 : 0.0},
        {"Shooter's ball after", afterBlock == "shooter's" ? 1.0 : 0.0},
        {"Off him (m/s)", blocked ? offSpeed : 0.0},
        {"Turned (deg)", blocked ? offTurn : 0.0},
        {"Rising (m/s)", blocked ? offUp : 0.0},
        {"Sidespin (rad/s)", blocked ? offSpin.side : 0.0},
        {"Topspin (rad/s)", blocked ? offSpin.top : 0.0},
        {"Surface facing up", blocked ? offNormalUp : 0.0},
        {"Part hit", blocked ? static_cast<double>(offPart) : -1.0},
        {"Goal", outcome == "goal" ? 1.0 : 0.0},
        {"Intercepted", outcome == "intercepted" ? 1.0 : 0.0},
        {"Body after no touch", static_cast<double>(noTouchNow - noTouchBefore)},
        {"Through torso unmet", static_cast<double>(throughNow - throughBefore)},
        {"Inside at sub-step start", static_cast<double>(insideNow - insideBefore)},
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
    if (ballKind > 0.5f)
      engine.forcePassTo(shooterId, at(1.0f, 0.5f + aimAcross / WIDTH), ForcedPassStyle::GROUND);
    else
      engine.forceShot(shooterId, Vector2F{aimAcross, aimHeight}, power);
    shotAimed = true;
  }
  [[nodiscard]] std::string settingsKey() const
  {
    return std::format("{:.1f}|{:.1f}|{:.2f}|{:.2f}|{:.2f}|{:.0f}|{:.1f}|{:.2f}|{:.0f}|{:.1f}|"
                       "{:.0f}|{:.0f}|{:.0f}",
                       line, side, aimAcross, aimHeight, power, shooting, distance, offLine,
                       moving, runUp, defending, tackling, height) +
           std::format("|{:.0f}|{:.0f}|{:.0f}", keeperChoice, goalkeeping, ballKind);
  }
  void tally()
  {
    if (!shotAt) return;
    Tally& t = tallies[settingsKey()];
    ++t.shots;
    if (outcome != "blocked") return;
    ++t.blocked;
    if (part == "torso" || part == "head")
      ++t.torso;
    else if (part.find("reaction") != std::string::npos)
      ++t.reaction;
    else
      ++t.stride;
  }

  float line = 18.0f;
  float side = 0.0f;
  float aimAcross = 1.5f;
  float aimHeight = 0.2f;
  float power = 0.8f;
  float shooting = 99.0f;
  float distance = 8.0f;
  float offLine = 0.7f;
  float moving = 0.0f;
  float runUp = 12.0f;
  float keeperChoice = 0.0f;
  float goalkeeping = 70.0f;
  float ballKind = 0.0f;
  std::array<int, static_cast<std::size_t>(ShotBodyPart::COUNT)> bodyHitsBefore{};
  int ballReactionBefore = 0;
  int noTouchBefore = 0;
  int noTouchNow = 0;
  float offLineAtShot = 0.0f;
  float offSpeed = 0.0f;
  float offTurn = 0.0f;
  float offUp = 0.0f;
  BallSpin offSpin{};
  float offNormalUp = 0.0f;
  int offPart = -1;
  bool stillShot = false;
  std::string afterBlock;
  /** The nearest the ball came to him at a step's end while the shot was
   * on (m). */
  float closest = 99.0f;
  Vector2F runLeft{};
  float defenderOut = 0.0f;
  float defending = 70.0f;
  float tackling = 70.0f;
  float height = 185.0f;

  PlayerID shooterId = 0;
  PlayerID defenderId = 0;
  PitchPoint spot{};
  PitchPoint defenderAt{};
  PitchPoint start{};
  bool shotAimed = false;
  std::size_t seenEvents = 0;
  std::optional<double> shotAt;
  float speedAtShot = 0.0f;
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
 public:
  /** Where the receiver meets the ball (pitch), facing up the pitch. */
  static inline const PitchPoint MEET = at(0.5f, 0.5f);
  /** A run ends this long after its outcome, or with no outcome
   * TIME_LIMIT seconds after the ball is played. */
  static constexpr double LINGER_SECONDS = 0.3;
  static constexpr double TIME_LIMIT = 8.0;
  /** Over this far from him during the touch: a heavy touch. */
  static constexpr float HEAVY_METRES = 2.0f;

  const char* name() const override { return "First touch"; }
  const char* summary() const override
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
    add(setting("Ball speed (m/s)", &ballSpeed, 2.0f, 30.0f, false,
                "Pace when it is played in."),
        "Ball");
    add(setting("Comes from (deg)", &fromAngle, 0.0f, 180.0f, true,
                "Where it is played from, against the way he faces (up the "
                "pitch): 0 in front of him, 90 from his side, 180 from "
                "behind."),
        "Ball");
    add(setting("Feed distance (m)", &feedDistance, 5.0f, 40.0f, false),
        "Ball");
    add(setting("Passes him by (m)", &passesBy, -1.5f, 1.5f, false,
                "Sideways offset of its line from where he meets it: 0 "
                "straight at him; + to one side, - the other (with Foot, "
                "his stronger or weaker side)."),
        "Ball");
    add(setting("Bounce (m/s up)", &bounce, 0.0f, 3.0f, false,
                "Upward speed when played: 0 along the ground."),
        "Ball");
    DrillParameter kind =
        setting("Ball", &ballKind, 0.0f, 1.0f, true,
                "His pass: played to him (he expects it).\nLoose: nobody's "
                "pass (a surprise).");
    kind.choices = {"His pass", "Loose"};
    add(kind, "Ball");

    add(setting("Run to meet (m)", &runToMeet, 0.0f, 10.0f, false,
                "He starts this far off the ball's line and goes to it (his "
                "brain chooses how)."),
        "Receiver");
    add(setting("Run-up (m)", &runUp, 0.0f, 30.0f, false,
                "0: standing. Otherwise he runs up the pitch, and the ball is "
                "played once he has run this far, aimed at where he will be; "
                "he keeps running until he plays it."),
        "Receiver");
    add(setting("First Touch", &firstTouch, 1.0f, 100.0f, true,
                "His First Touch rating (overrides the derived stat)."),
        "Receiver");
    add(setting("Pace", &pace, 1.0f, 100.0f, true), "Receiver");
    add(setting("Condition", &condition, 30.0f, 100.0f, true,
                "How fresh he is at the start (tiredness widens the touch's "
                "errors)."),
        "Receiver");
    DrillParameter foot = setting("Foot", &footChoice, 0.0f, 1.0f, true);
    foot.choices = {"Right", "Left"};
    add(foot, "Receiver");

    DrillParameter defender =
        setting("Defender", &defenderChoice, 0.0f, 1.0f, true,
                "A defender at his back (beyond him on the ball's line), "
                "pressing once the ball is played.");
    defender.choices = {"None", "Pressing"};
    add(defender, "Defender");
    add(setting("Defender distance (m)", &defenderDistance, 1.0f, 15.0f,
                false),
        "Defender");
    add(setting("Defending", &defending, 1.0f, 100.0f, true), "Defender");

    // Every knob of the touch, by its registry name and group.
    static const std::array<std::string, TOUCH_KNOB_COUNT> sections = []
    {
      std::array<std::string, TOUCH_KNOB_COUNT> names;
      for (std::size_t index = 0; index < TOUCH_KNOB_COUNT; ++index)
        names[index] = "Knobs: " + std::string(TOUCH_KNOBS[index].group);
      return names;
    }();
    for (std::size_t index = 0; index < TOUCH_KNOB_COUNT; ++index)
    {
      const TouchKnobInfo& info = TOUCH_KNOBS[index];
      DrillParameter knob = setting(info.name.data(), &knobs.values[index],
                                    info.min, info.max, false);
      knob.collapsible = true;
      add(knob, sections[index].c_str());
    }
    return list;
  }

  std::vector<DrillPreset> presets() const override
  {
    // Each starts from the same baseline; only what differs is listed
    // after it.
    const std::map<std::string, float> base{
        {"Ball speed (m/s)", 12.0f}, {"Comes from (deg)", 0.0f},
        {"Feed distance (m)", 20.0f}, {"Passes him by (m)", 0.0f},
        {"Bounce (m/s up)", 0.0f},    {"Ball", 0.0f},
        {"Run to meet (m)", 0.0f},    {"Run-up (m)", 0.0f},
        {"Defender", 0.0f}};
    const auto with = [&base](std::map<std::string, float> changes)
    {
      std::map<std::string, float> settings = base;
      for (const auto& [name, value] : changes) settings[name] = value;
      return settings;
    };
    return {
        {"Firm pass to feet", "12 m/s from in front, straight at him, standing.",
         base},
        {"Driven pass to feet", "22 m/s: pace.", with({{"Ball speed (m/s)", 22.0f}})},
        {"Soft pass, step to it", "6 m/s, 1 m off his line.",
         with({{"Ball speed (m/s)", 6.0f}, {"Run to meet (m)", 1.0f}})},
        {"Pass into his run",
         "10 m/s from his side, played after a 20 m run-up.",
         with({{"Ball speed (m/s)", 10.0f},
               {"Comes from (deg)", 90.0f},
               {"Run-up (m)", 20.0f}})},
        {"Run to meet it", "12 m/s, he starts 4 m off its line.",
         with({{"Run to meet (m)", 4.0f}})},
        {"Across him", "12 m/s from his side, 0.6 m off centre.",
         with({{"Comes from (deg)", 90.0f}, {"Passes him by (m)", 0.6f}})},
        {"Over his shoulder", "10 m/s from behind him.",
         with({{"Ball speed (m/s)", 10.0f}, {"Comes from (deg)", 135.0f}})},
        {"Bouncing ball",
         "12 m/s from 7 m with 2.8 m/s up: it reaches him on a hop.",
         with({{"Bounce (m/s up)", 2.8f}, {"Feed distance (m)", 7.0f}})},
        {"Loose ball", "8 m/s, nobody's pass.",
         with({{"Ball speed (m/s)", 8.0f}, {"Ball", 1.0f}})},
        {"Under pressure", "12 m/s with a defender 2 m behind him, pressing.",
         with({{"Defender", 1.0f}, {"Defender distance (m)", 2.0f}})},
    };
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec receiver{"Receiver", PlayerRole::CM, true,
                             {{"Pace", pace}}, 180,
                             {{DerivedStat::FIRST_TOUCH, firstTouch}}};
    receiver.foot = footChoice < 0.5f ? Foot::Right : Foot::Left;
    receiver.condition = condition;
    std::vector<DrillPlayerSpec> list{receiver};
    if (defenderChoice > 0.5f)
      list.push_back(
          {"Defender", PlayerRole::CB, false, {{"Defending", defending}}, 182, {}});
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    receiverId = ids[0];
    hasDefender = defenderChoice > 0.5f;
    defenderId = hasDefender ? ids[1] : 0;
    receiverBrain = engine.getBrain(true);
    engine.setFirstTouchKnobs(knobs);

    // Geometry in metres: he faces up the pitch (+x); the ball comes from
    // `fromAngle` round from in front of him, along a line `passesBy` to
    // the side of the meeting point.
    const Vector2F up{1.0f, 0.0f};
    from = rotated(up, fromAngle * PI / 180.0f);
    travel = Vector2F{-from.x, -from.y};
    side = rotated(travel, PI * 0.5f);
    feedFrom = along(along(MEET, side, passesBy), from, feedDistance);
    receiverStart = along(along(MEET, side, -runToMeet), up, -runUp);
    // The defender marks him from behind: beyond him on the ball's line, so
    // he cannot simply cut it out before it arrives.
    defenderStart = along(MEET, travel, defenderDistance);

    MatchScenario scenario;
    scenario.players.push_back({receiverId, receiverStart, false});
    if (hasDefender) scenario.players.push_back({defenderId, defenderStart, false});
    scenario.carrierId = 0;
    scenario.ballPosition = feedFrom;
    engine.startDrill(scenario);
    // The defender waits on his spot until the ball is played.
    if (hasDefender) engine.setDrillTarget(defenderId, defenderStart, false);
    if (runUp > 0.0f)
      engine.setDrillTarget(receiverId,
                            along(MEET, up, RUN_THROUGH_METRES * 4.0f), true);

    fed = false;
    touch.reset();
    outcome.clear();
    endAt.reset();
    peakGap = 0.0f;
    released = false;
    lastBallVelocity = {0.0f, 0.0f};
    ballPath.clear();
    if (runUp <= 0.0f) feed(engine);
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    const MatchPlayer* receiver = slotOf(engine, receiverId);
    // On the run: played once he has run his run-up.
    if (!fed && receiver &&
        (metresBetween(receiverStart, receiver->position) >= runUp ||
         now >= TIME_LIMIT * 0.5))
      feed(engine);
    if (fed && engine.getSimulatedSteps() % 2 == 0)
      ballPath.push_back(ball.position);

    const bool his = engine.getPossessionAttachment() && engine.getPossessionAttachment()->getId() == receiverId;
    const bool theirs =
        engine.getPossessionAttachment() && hasDefender && engine.getPossessionAttachment()->getId() == defenderId;
    if (fed && outcome.empty())
    {
      if (his && !touch)
      {
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
        if (receiver)
        {
          reading.speed = speedOf(*receiver);
          const Vector2F facing = directionBetween(
              PitchPoint{}, at(std::cos(receiver->facingAngle),
                             std::sin(receiver->facingAngle)));
          const float speed =
              std::hypot(lastBallVelocity.x, lastBallVelocity.y);
          reading.facingVsBall =
              speed > 0.1f ? turnDegrees(facing, {-lastBallVelocity.x / speed,
                                                  -lastBallVelocity.y / speed})
                           : 0.0f;
        }
        touch = reading;
        if (runUp > 0.0f) engine.setDrillTarget(receiverId, std::nullopt, false);
        // The old trap (a ball off his feet): the ball is his at once.
        if (!engine.hasTouchClaim()) outcome = "collected (old trap)";
      }
      // Rolling to him until he collects it (then he dribbles it on).
      const bool rolling = engine.hasTouchClaim() && !engine.isTouchCollected();
      if (touch && rolling && his)
        peakGap = std::max(peakGap, ball.touchPeakGap);
      if (touch && his && !rolling && outcome.empty())
        outcome = "collected";
      if (theirs) outcome = touch ? "lost to the defender" : "intercepted";
      if (engine.getState() != MatchState::PLAYING)
        outcome = touch ? "lost (out of play)" : "out of play";
      if (touch && !engine.getPossessionAttachment()) released = true;
      if (outcome.empty() && now >= fedAt + TIME_LIMIT)
        outcome = touch ? "no result" : "never touched";
    }
    lastBallVelocity = ball.velocity;
    if (!outcome.empty() && !endAt) endAt = now;
    return !(endAt && now >= *endAt + LINGER_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::line(renderer, draw, feedFrom,
                    along(feedFrom, travel, feedDistance * 1.3f),
                    IM_COL32(255, 255, 255, 70), 1.0f * scale);
    DrillDraw::marker(renderer, draw, feedFrom, 3.0f * scale,
                      IM_COL32(255, 255, 255, 200), "feed");
    DrillDraw::marker(renderer, draw, MEET, 2.5f * scale,
                      IM_COL32(90, 220, 120, 220), "meet");
    if (hasDefender)
      DrillDraw::marker(renderer, draw, defenderStart, 2.5f * scale,
                        IM_COL32(240, 80, 80, 200));
    DrillDraw::path(renderer, draw, ballPath, IM_COL32(255, 230, 120, 160),
                    1.5f * scale);
    if (touch)
      DrillDraw::marker(renderer, draw, touch->point, 3.0f * scale,
                        IM_COL32(90, 170, 255, 255),
                        outcome.empty() ? "touch" : outcome.c_str());
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.0f} m/s from {:.0f} deg, {:.0f} m out, {:+.1f} m by{} · {}",
        ballSpeed, fromAngle, feedDistance, passesBy,
        bounce > 0.0f ? std::format(", {:.1f} m/s up", bounce) : std::string(),
        ballKind < 0.5f ? "his pass" : "loose"));
    lines.push_back(std::format(
        "Receiver: First Touch {:.0f}, Pace {:.0f}, condition {:.0f}, {} "
        "foot, {} brain · run to meet {:.1f} m, run-up {:.0f} m{}",
        firstTouch, pace, condition, footChoice < 0.5f ? "right" : "left",
        brainName(receiverBrain), runToMeet, runUp,
        hasDefender ? std::format(" · defender {:.0f} m (Def {:.0f})",
                                  defenderDistance, defending)
                    : std::string()));
    if (touch)
      lines.push_back(std::format(
          "Touch at {:.2f} s, {:.2f} m from his centre{}: turn {:.0f} deg {} "
          "{}{:+.1f} deg (difficulty {:.2f}), pace {:+.1f} m/s {}{:+.1f} "
          "m/s (difficulty {:.2f}){}",
          touch->at, touch->contactGap, touch->intoStride ? ", into stride" : "",
          touch->turn * 180.0f / PI, touch->outward ? "out" : "in",
          touch->angleFailed ? "FAILED " : "", touch->angleError * 180.0f / PI,
          touch->angleDifficulty, touch->paceChange,
          touch->weightFailed ? "FAILED " : "", touch->weightError,
          touch->weightDifficulty,
          peakGap > 0.0f ? std::format(", got {:.1f} m away", peakGap)
                         : std::string()));
    if (!outcome.empty()) lines.push_back("OUTCOME  " + outcome);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty()) return {};
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    const bool collected = outcome.starts_with("collected");
    std::vector<DrillMetric> out{
        {"Touched", flag(touch.has_value())},
        {"Collected", flag(collected)},
        {"Lost", flag(touch && !collected && outcome != "no result")},
        {"Heavy", flag(peakGap > HEAVY_METRES)},
        {"Released", flag(released)},
    };
    if (touch && touch->inTouch)
    {
      out.push_back({"Angle error (deg)", touch->angleError * 180.0f / PI});
      out.push_back(
          {"Abs angle error (deg)", std::abs(touch->angleError) * 180.0f / PI});
      out.push_back({"Weight error (m/s)", touch->weightError});
      out.push_back({"Abs weight error (m/s)", std::abs(touch->weightError)});
      out.push_back({"Angle difficulty", touch->angleDifficulty});
      out.push_back({"Weight difficulty", touch->weightDifficulty});
      out.push_back({"Angle failed", flag(touch->angleFailed)});
      out.push_back({"Weight failed", flag(touch->weightFailed)});
      out.push_back({"Turn (deg)", touch->turn * 180.0f / PI});
      out.push_back({"Turn outward", flag(touch->outward)});
      out.push_back({"Pace change (m/s)", touch->paceChange});
      out.push_back({"Contact gap (m)", touch->contactGap});
      out.push_back({"Ball height (m)", touch->height});
      out.push_back({"Into stride", flag(touch->intoStride)});
      out.push_back({"Facing vs ball (deg)", touch->facingVsBall});
      out.push_back({"His speed (m/s)", touch->speed});
    }
    if (touch) out.push_back({"Old trap", flag(!touch->inTouch)});
    if (touch && collected && endAt)
      out.push_back({"Time to collect (s)", *endAt - touch->at});
    return out;
  }

 private:
  struct Touch
  {
    double at = 0.0;
    bool inTouch = true;
    float contactGap = 0.0f;
    float angleError = 0.0f;
    float weightError = 0.0f;
    float angleDifficulty = 0.0f;
    float weightDifficulty = 0.0f;
    bool angleFailed = false;
    bool weightFailed = false;
    float turn = 0.0f;
    bool outward = false;
    float paceChange = 0.0f;
    bool intoStride = false;
    float facingVsBall = 0.0f;
    float speed = 0.0f;
    float height = 0.0f;
    PitchPoint point{};
  };

  /** Seconds for the ball, as fed, to travel `metres` (the match's own
   * flight and rolling physics, in its ball sub-steps). */
  [[nodiscard]] float rollSeconds(const MatchEngine& engine,
                                  float metres) const
  {
    constexpr float DT =
        STEP / static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
    FlightState flight{ballSpeed, bounce, 0.0f, false};
    float travelled = 0.0f;
    float seconds = 0.0f;
    while (travelled < metres && flight.horizontalSpeed > 0.0f &&
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
    if (hasDefender) engine.setDrillTarget(defenderId, std::nullopt, false);
    if (const MatchPlayer* receiver = slotOf(engine, receiverId);
        receiver && runUp > 0.0f)
    {
      // On the run: the feed spot is placed, at the set distance and angle,
      // from where he will be when the ball gets there (his current
      // velocity over its exact travel time), still `passesBy` to his side.
      const float seconds = rollSeconds(engine, feedDistance) + STEP;
      const PitchPoint ahead = toPitch({receiver->velocity.x * seconds,
                                      receiver->velocity.y * seconds});
      const PitchPoint meet{receiver->position.x + ahead.x,
                          receiver->position.y + ahead.y};
      feedFrom = along(along(meet, side, passesBy), from, feedDistance);
    }
    engine.feedBall(feedFrom, {travel.x * ballSpeed, travel.y * ballSpeed},
                    bounce, ballKind < 0.5f ? receiverId : 0);
    ballPath.push_back(feedFrom);
  }

  // Ball
  float ballSpeed = 12.0f;
  float fromAngle = 0.0f;
  float feedDistance = 20.0f;
  float passesBy = 0.0f;
  float bounce = 0.0f;
  float ballKind = 0.0f;
  // Receiver
  float runToMeet = 0.0f;
  float runUp = 0.0f;
  float firstTouch = 70.0f;
  float pace = 70.0f;
  float condition = 100.0f;
  float footChoice = 0.0f;
  /** The receiver's brain (the sandbox's home brain), for the summary. */
  BrainId receiverBrain = BrainId::AARDVARK;
  // Defender
  float defenderChoice = 0.0f;
  float defenderDistance = 3.0f;
  float defending = 70.0f;
  // The touch's tuning, as this drill plays it.
  FirstTouchKnobs knobs;

  PlayerID receiverId = 0;
  PlayerID defenderId = 0;
  bool hasDefender = false;
  /** Unit directions (metres): where the ball comes from, the way it
   * travels, and square to it (the side `passesBy` measures). */
  Vector2F from{-1.0f, 0.0f};
  Vector2F travel{1.0f, 0.0f};
  Vector2F side{0.0f, 1.0f};
  PitchPoint feedFrom{};
  PitchPoint receiverStart{};
  PitchPoint defenderStart{};
  bool fed = false;
  double fedAt = 0.0;
  std::optional<Touch> touch;
  std::string outcome;
  bool released = false;
  std::optional<double> endAt;
  float peakGap = 0.0f;
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
  static constexpr float AIRBORNE_METRES = 0.1f;
  /** Further than this past its closest to the target (m): gone by. */
  static constexpr float PAST_TARGET_METRES = 3.0f;

  PitchPoint target{};
  /** From where it was struck to the target (m). */
  float distance = 0.0f;
  float closest = 0.0f;
  float closestSpeed = 0.0f;
  float closestHeight = 0.0f;
  bool wasUp = false;
  std::optional<PitchPoint> landing;
  float landingMiss = 0.0f;
  bool stopped = false;

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
    const float height = MatchPitch::heightMetres(ball.z);
    const float gap = metresBetween(ball.position, target);
    if (gap < closest)
    {
      closest = gap;
      closestSpeed = std::hypot(ball.velocity.x, ball.velocity.y);
      closestHeight = height;
    }
    if (height > AIRBORNE_METRES) wasUp = true;
    // First down: bounced since the last step (falling then, rising now),
    // or on the ground; where, from the last step falling under gravity
    // (drag is small over a step), as the lofted target drill works it out.
    if (wasUp && !landing)
    {
      const bool bounced = lastLift < 0.0f && ball.velocityZ > 0.0f;
      const bool grounded = height <= 0.01f && ball.velocityZ <= 0.0f;
      if (bounced || grounded)
      {
        constexpr float G = MatchTuning::Ball::GRAVITY;
        const float fall =
            (lastLift + std::sqrt(lastLift * lastLift +
                                  2.0f * G * std::max(lastHeight, 0.0f))) /
            G;
        landing = PitchPoint{lastPosition.x + PITCH.fromMetresX(lastVelocity.x * fall),
                           lastPosition.y + PITCH.fromMetresY(lastVelocity.y * fall)};
        landingMiss = metresBetween(*landing, target);
      }
    }
    stopped = std::hypot(ball.velocity.x, ball.velocity.y) < 0.2f &&
              height <= 0.01f;
    remember(ball);
  }

  /** Nothing more to learn: it has come down (if it flies) and stopped or
   * gone by the target. */
  [[nodiscard]] bool settled(StrikeKind kind, const MatchBall& ball) const
  {
    if (flies(kind) && !landing) return false;
    return stopped ||
           metresBetween(ball.position, target) > closest + PAST_TARGET_METRES;
  }

  /** The strike's miss (m): where it landed for a pass that flies, how
   * close it ran past for one along the ground; none if it flies and never
   * came down. */
  [[nodiscard]] std::optional<float> miss(StrikeKind kind) const
  {
    if (!flies(kind)) return closest;
    if (landing) return landingMiss;
    return std::nullopt;
  }

  /** Its numbers (Measure mode, the technique bench). */
  void addMetrics(StrikeKind kind, std::vector<DrillMetric>& out) const
  {
    out.push_back({"Strike to target (m)", distance});
    if (const auto off = miss(kind)) out.push_back({"Strike miss (m)", *off});
    out.push_back({"Landed", landing ? 1.0 : 0.0});
    if (landing) out.push_back({"Landing miss (m)", landingMiss});
  }

 private:
  void remember(const MatchBall& ball)
  {
    lastPosition = ball.position;
    lastVelocity = ball.velocity;
    lastHeight = MatchPitch::heightMetres(ball.z);
    lastLift = ball.velocityZ;
  }

  PitchPoint lastPosition{};
  Vector2F lastVelocity{};
  float lastHeight = 0.0f;
  float lastLift = 0.0f;
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
 public:
  /** A run ends this long after its outcome, or with no outcome
   * TIME_LIMIT seconds after the pass. */
  static constexpr double LINGER_SECONDS = 0.3;
  static constexpr double TIME_LIMIT = 6.0;
  /** The kinds of pass this drill plays, and so whose knobs it offers. */
  static constexpr std::array<StrikeKind, 5> KINDS = {
      StrikeKind::GROUND_PASS, StrikeKind::LOFTED_PASS, StrikeKind::CROSS,
      StrikeKind::DRIVEN_PASS, StrikeKind::CHIP};

  const char* name() const override { return "Pass"; }
  const char* summary() const override
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
    add(setting("Length (m)", &length, 3.0f, 60.0f, false,
                "From the passer to the receiver."),
        "Pass");
    add(setting("Direction (deg)", &direction, -180.0f, 180.0f, true,
                "Where the receiver is against the way the passer faces (up "
                "the pitch): 0 straight ahead, 90 to his right (towards the "
                "bottom touchline), -90 to his left, 180 behind him."),
        "Pass");
    DrillParameter style =
        setting("Style", &styleChoice, 0.0f, 4.0f, true,
                "As the AI would: along the ground, or lofted for a long "
                "ball or a cross.\nGround, Lofted, Driven (low and hard), "
                "Chip (steep, over someone close): forced.");
    style.choices = {"As the AI would", "Ground", "Lofted", "Driven", "Chip"};
    add(style, "Pass");
    DrillParameter from =
        setting("Passer at", &placeChoice, 0.0f, 1.0f, true,
                "In the middle of the pitch, or wide on the top touchline in "
                "the final third (a pass into the middle there is a cross).");
    from.choices = {"Middle", "Wide, final third"};
    add(from, "Pass");
    DrillParameter receiver = setting(
        "Receiver", &receiverChoice, 0.0f, 1.0f, true,
        "A player: a receiver held on the spot, who goes for the ball once "
        "it is played (the AI's own pass to him).\nNone, strike alone: a "
        "target with nobody there, weighted as for a man on it; the strike "
        "is judged by where the ball lands (lofted, cross, chip) or how "
        "close it runs past (ground, driven).");
    receiver.choices = {"A player", "None, strike alone"};
    add(receiver, "Pass");
    add(setting("Sidespin (rev/s)", &sideRevs, -10.0f, 10.0f, false,
                "Strike alone only: the sidespin he means (+ curls it to his "
                "right). 0 with no topspin: the body's default."),
        "Pass");
    add(setting("Topspin (rev/s)", &topRevs, -10.0f, 10.0f, false,
                "Strike alone only: the topspin he means (- backspin)."),
        "Pass");

    add(setting("Run-up (m)", &runUp, 0.0f, 30.0f, false,
                "0: he passes standing. Otherwise he runs with the ball up "
                "the pitch this far, then passes on the move."),
        "Passer");
    DrillParameter pace =
        setting("Run-up pace", &paceChoice, 0.0f, 2.0f, true,
                "How hard he carries it on the run-up: a jog, a run or a "
                "sprint (as his Dribbling allows).");
    pace.choices = {"Jog", "Run", "Sprint"};
    add(pace, "Passer");
    add(setting("Passing", &passing, 1.0f, 100.0f, true), "Passer");
    add(setting("Dribbling", &dribbling, 1.0f, 100.0f, true,
                "With the ball on the run-up: his pace, agility and "
                "acceleration with it."),
        "Passer");
    add(setting("Condition", &condition, 30.0f, 100.0f, true,
                "How fresh he is at the start (tiredness widens the "
                "errors)."),
        "Passer");
    DrillParameter foot = setting("Foot", &footChoice, 0.0f, 1.0f, true);
    foot.choices = {"Right", "Left"};
    add(foot, "Passer");

    DrillParameter defender =
        setting("Defender", &defenderChoice, 0.0f, 1.0f, true,
                "A defender held this far behind the passer until the pass: "
                "pressure, out of the passing lane.");
    defender.choices = {"None", "Close by"};
    add(defender, "Defender");
    add(setting("Defender distance (m)", &defenderDistance, 0.5f, 10.0f,
                false),
        "Defender");

    // Every strike knob of the kinds of pass played here, by kind and group.
    static const auto names = []
    {
      std::array<std::array<std::string, STRIKE_KNOB_COUNT>, KINDS.size()>
          full;
      for (std::size_t k = 0; k < KINDS.size(); ++k)
        for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
          full[k][index] =
              strikeKnobName(KINDS[k], static_cast<StrikeKnob>(index));
      return full;
    }();
    static const auto sections = []
    {
      std::array<std::array<std::string, STRIKE_KNOB_COUNT>, KINDS.size()>
          titles;
      for (std::size_t k = 0; k < KINDS.size(); ++k)
        for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
          titles[k][index] =
              std::string(STRIKE_KIND_NAMES[static_cast<std::size_t>(KINDS[k])]) +
              " knobs: " + std::string(STRIKE_KNOBS[index].group);
      return titles;
    }();
    for (std::size_t k = 0; k < KINDS.size(); ++k)
      for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
      {
        const StrikeKnobInfo& info = STRIKE_KNOBS[index];
        DrillParameter knob =
            setting(names[k][index].c_str(),
                    &knobs.at(KINDS[k], static_cast<StrikeKnob>(index)),
                    info.min, info.max, false);
        knob.collapsible = true;
        knob.label = info.name.data();
        add(knob, sections[k][index].c_str());
      }
    return list;
  }

  std::vector<DrillPreset> presets() const override
  {
    const std::map<std::string, float> base{
        {"Length (m)", 15.0f},   {"Direction (deg)", 0.0f},
        {"Style", 0.0f},         {"Passer at", 0.0f},
        {"Run-up (m)", 0.0f},    {"Defender", 0.0f},
        {"Defender distance (m)", 2.0f}};
    const auto with = [&base](std::map<std::string, float> changes)
    {
      std::map<std::string, float> settings = base;
      for (const auto& [name, value] : changes) settings[name] = value;
      return settings;
    };
    return {
        {"Short pass", "15 m straight ahead, standing.", base},
        {"Square pass", "15 m to his right, standing.",
         with({{"Direction (deg)", 90.0f}})},
        {"Long ground pass", "35 m ahead, forced along the ground.",
         with({{"Length (m)", 35.0f}, {"Style", 1.0f}})},
        {"Pass on the run", "15 m ahead after a 15 m run-up.",
         with({{"Run-up (m)", 15.0f}})},
        {"Under pressure", "15 m ahead, a defender 1.5 m behind him.",
         with({{"Defender", 1.0f}, {"Defender distance (m)", 1.5f}})},
        {"Long ball", "40 m ahead: lofted, as the AI plays it.",
         with({{"Length (m)", 40.0f}})},
        {"Switch of play", "40 m to his right, lofted.",
         with({{"Length (m)", 40.0f}, {"Direction (deg)", 90.0f}})},
        {"Cross", "From wide in the final third into the box, 28 m.",
         with({{"Passer at", 1.0f},
               {"Length (m)", 28.0f},
               {"Direction (deg)", 63.0f}})},
        {"Cross on the run", "The cross after a 12 m run-up.",
         with({{"Passer at", 1.0f},
               {"Length (m)", 28.0f},
               {"Direction (deg)", 63.0f},
               {"Run-up (m)", 12.0f}})},
    };
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec passer{"Passer", PlayerRole::CM, true,
                           {{"Passing", passing}, {"Dribbling", dribbling}},
                           180};
    passer.foot = footChoice < 0.5f ? Foot::Right : Foot::Left;
    passer.condition = condition;
    std::vector<DrillPlayerSpec> list{passer};
    if (receiverChoice < 0.5f)
      list.push_back({"Receiver", PlayerRole::ST, true, {}, 182});
    if (defenderChoice > 0.5f)
      list.push_back({"Defender", PlayerRole::CB, false, {}, 184});
    return list;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    passerId = ids[0];
    hasReceiver = receiverChoice < 0.5f;
    receiverId = hasReceiver ? ids[1] : 0;
    hasDefender = defenderChoice > 0.5f;
    defenderId = hasDefender ? ids[hasReceiver ? 2 : 1] : 0;
    engine.setStrikeKnobs(knobs);

    // Geometry in metres: he faces up the pitch (+x) and passes from where
    // his run-up ends; the receiver is `length` away at `direction`. In the
    // middle, the run-up and the pass sit centred on the pitch, so a long
    // one stays on it.
    const Vector2F up{1.0f, 0.0f};
    if (placeChoice < 0.5f)
    {
      const float radians = direction * PI / 180.0f;
      const float ahead = length * std::cos(radians);
      const float across = length * std::sin(radians);
      const float lowX = std::min(0.0f, runUp + ahead);
      const float highX = std::max(runUp, runUp + ahead);
      passerStart = toPitch({LENGTH * 0.5f - (lowX + highX) * 0.5f,
                             WIDTH * 0.5f - across * 0.5f});
    }
    else
    {
      passerStart = at(0.70f, 0.10f);
    }
    passPoint = along(passerStart, up, runUp);
    receiverSpot =
        along(passPoint, rotated(up, direction * PI / 180.0f), length);
    receiverSpot = PitchPoint{std::clamp(receiverSpot.x, PITCH.atX(0.01f), PITCH.atX(0.99f)),
                    std::clamp(receiverSpot.y, PITCH.atY(0.01f), PITCH.atY(0.99f))};
    defenderSpot = along(passPoint, up, -defenderDistance);

    MatchScenario scenario;
    scenario.players.push_back({passerId, passerStart, false});
    if (hasReceiver) scenario.players.push_back({receiverId, receiverSpot, false});
    if (hasDefender) scenario.players.push_back({defenderId, defenderSpot, false});
    scenario.carrierId = passerId;
    scenario.ballPosition = passerStart;
    engine.startDrill(scenario);
    // The receiver and the defender hold their spots until the pass.
    if (hasReceiver) engine.setDrillTarget(receiverId, receiverSpot, false);
    if (hasDefender) engine.setDrillTarget(defenderId, defenderSpot, false);
    if (runUp > 0.0f)
    {
      engine.keepBall(passerId, true, (paceChoice + 1.0f) / 3.0f);
      engine.setDrillTarget(passerId,
                            along(passerStart, up, runUp + RUN_THROUGH_METRES),
                            true);
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

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    const MatchPlayer* passer = slotOf(engine, passerId);
    if (!ordered && passer &&
        (metresBetween(passerStart, passer->position) >= runUp || now >= 6.0))
      order(engine);
    if (engine.getSimulatedSteps() % 2 == 0) ballPath.push_back(ball.position);
    // When he decides on it (the pass held for its strike): his body's angle
    // to its line then, and his speed.
    if (!pass && !held && passer && engine.passHeldFor(passerId))
      held = Held{now, bodyAngleToTarget(*passer, ball.position),
                  speedOf(*passer)};

    if (!pass)
    {
      // The pass: a strike the contact model has read since the start.
      const StrikeContactStats& now_ = engine.getStrikeContacts();
      for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
      {
        const auto& a = before.byKind[kind];
        const auto& b = now_.byKind[kind];
        if (b.strikes == a.strikes) continue;
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
          reading.factors[index] =
              static_cast<float>(b.factorSum[index] - a.factorSum[index]);
        reading.from = ball.position;
        reading.speed = std::hypot(ball.velocity.x, ball.velocity.y);
        reading.lift = ball.velocityZ;
        if (passer)
        {
          reading.passerSpeed = speedOf(*passer);
          // Where it went against where his body faced.
          reading.bodyAngle = bodyAngleTo(*passer, ball.velocity);
        }
        reading.held = held;
        pass = reading;
        closest = metresBetween(ball.position, receiverSpot);
        closestHeight = 0.0f;
        closestSpeed = reading.speed;
        flight.start(ball, receiverSpot);
        // Released: the receiver goes for it, the defender presses.
        if (hasReceiver) engine.setDrillTarget(receiverId, std::nullopt, false);
        if (hasDefender) engine.setDrillTarget(defenderId, std::nullopt, false);
        break;
      }
      if (!pass && now >= 10.0) outcome = "no pass";
    }
    else if (outcome.empty())
    {
      flight.observe(ball);
      const float gap = metresBetween(ball.position, receiverSpot);
      if (gap < closest)
      {
        closest = gap;
        closestHeight = MatchPitch::heightMetres(ball.z);
        closestSpeed = std::hypot(ball.velocity.x, ball.velocity.y);
      }
      const PlayerID owner = engine.getPossessionAttachment() ? engine.getPossessionAttachment()->getId() : 0;
      if (!hasReceiver && flight.settled(pass->kind, ball))
        outcome = flight.landing ? std::format("landed {:.1f} m off",
                                               flight.landingMiss)
                                 : std::format("ran {:.1f} m past",
                                               flight.closest);
      else if (hasReceiver && owner == receiverId)
        outcome = "received";
      else if (hasDefender && owner == defenderId)
        outcome = "intercepted";
      else if (engine.getState() != MatchState::PLAYING)
        outcome = "out of play";
      else if (now >= pass->at + TIME_LIMIT)
        outcome = "not reached";
    }
    if (!outcome.empty() && !endAt) endAt = now;
    return !(endAt && now >= *endAt + LINGER_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::line(renderer, draw, passPoint, receiverSpot,
                    IM_COL32(255, 255, 255, 70), 1.0f * scale);
    DrillDraw::marker(renderer, draw, passPoint, 2.5f * scale,
                      IM_COL32(255, 255, 255, 200), "pass");
    DrillDraw::marker(renderer, draw, receiverSpot, 3.0f * scale,
                      IM_COL32(90, 220, 120, 220),
                      outcome.empty() ? "target" : outcome.c_str());
    if (hasDefender)
      DrillDraw::marker(renderer, draw, defenderSpot, 2.5f * scale,
                        IM_COL32(240, 80, 80, 200));
    if (pass && flight.landing)
      DrillDraw::marker(renderer, draw, *flight.landing, 3.0f * scale,
                        IM_COL32(255, 230, 120, 255), "down");
    DrillDraw::path(renderer, draw, ballPath, IM_COL32(255, 230, 120, 160),
                    1.5f * scale);
  }

  std::vector<std::string> report() const override
  {
    static constexpr std::array<const char*, 5> STYLES = {
        "as the AI would", "along the ground", "lofted", "driven", "chipped"};
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.0f} m at {:+.0f} deg, {} · from {} · run-up {:.0f} m{}{}", length,
        direction, STYLES[static_cast<std::size_t>(styleChoice)],
        placeChoice < 0.5f ? "the middle" : "wide", runUp,
        hasDefender ? std::format(" · defender {:.1f} m", defenderDistance)
                    : std::string(),
        hasReceiver ? std::string()
                    : std::format(" · strike alone{}{}",
                                  sideRevs != 0.0f
                                      ? std::format(", sidespin {:+.1f} rev/s", sideRevs)
                                      : std::string(),
                                  topRevs != 0.0f
                                      ? std::format(", topspin {:+.1f} rev/s", topRevs)
                                      : std::string())));
    lines.push_back(std::format(
        "Passer: Passing {:.0f}, Dribbling {:.0f}, condition {:.0f}, {} foot",
        passing, dribbling, condition, footChoice < 0.5f ? "right" : "left"));
    if (pass)
    {
      constexpr float DEGREES = 180.0f / PI;
      lines.push_back(std::format(
          "{} at {:.2f} s, {:.1f} m/s{}: difficulty angle {:.2f}, weight "
          "{:.2f}",
          STRIKE_KIND_NAMES[static_cast<std::size_t>(pass->kind)], pass->at,
          pass->speed,
          pass->lift > 0.0f ? std::format(" ({:.1f} up)", pass->lift)
                            : std::string(),
          pass->angleDifficulty, pass->weightDifficulty));
      lines.push_back(std::format(
          "Body {:.0f} deg off the pass when struck{}", pass->bodyAngle,
          pass->held ? std::format(" ({:.0f} deg when he decided, {:.2f} s "
                                   "before; {:.1f} -> {:.1f} m/s)",
                                   pass->held->bodyAngle,
                                   pass->at - pass->held->at,
                                   pass->held->speed, pass->passerSpeed)
                     : std::string(" (struck at once)")));
      if (pass->carriedOut)
        lines.push_back(std::format(
            "Angle {}{:.1f} deg (elevation {:+.1f}), weight {}{:+.2f} m/s",
            pass->angleFailed ? "FAILED " : "", pass->angleError * DEGREES,
            pass->elevationError * DEGREES,
            pass->weightFailed ? "FAILED " : "", pass->weightError));
      else
        lines.push_back("Measured only: this kind of pass keeps the old error.");
      lines.push_back(std::format(
          "Closest to the target {:.2f} m ({:.1f} m up, {:.1f} m/s)", closest,
          closestHeight, closestSpeed));
      if (!hasReceiver)
        lines.push_back(std::format(
            "Strike alone: {:.1f} m from where struck, {}", flight.distance,
            flight.miss(pass->kind)
                ? std::format("missed by {:.2f} m ({})", *flight.miss(pass->kind),
                              PassFlight::flies(pass->kind) ? "where it landed"
                                                            : "as it ran past")
                : std::string("not down yet")));
    }
    if (!outcome.empty()) lines.push_back("OUTCOME  " + outcome);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty()) return {};
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{{"Passed", flag(pass.has_value())}};
    if (!pass) return out;
    constexpr float DEGREES = 180.0f / PI;
    out.push_back({"Kind (0 ground, 1 lofted, 2 cross, 3 shot, 7 driven, 8 chip)",
                   static_cast<double>(pass->kind)});
    out.push_back({"Received", flag(outcome == "received")});
    out.push_back({"Intercepted", flag(outcome == "intercepted")});
    out.push_back({"Out of play", flag(outcome == "out of play")});
    out.push_back({"Miss at target (m)", closest});
    out.push_back({"Height at target (m)", closestHeight});
    out.push_back({"Speed at target (m/s)", closestSpeed});
    if (!hasReceiver) flight.addMetrics(pass->kind, out);
    out.push_back({"Launch speed (m/s)", pass->speed});
    out.push_back({"Launch lift (m/s)", pass->lift});
    out.push_back({"Passer speed (m/s)", pass->passerSpeed});
    // Turning to pass: how far round he got, in how long, at what cost.
    out.push_back({"Body off the pass at strike (deg)", pass->bodyAngle});
    if (pass->held)
    {
      out.push_back({"Body off the pass at decision (deg)",
                     pass->held->bodyAngle});
      out.push_back({"Turned (deg)", pass->held->bodyAngle - pass->bodyAngle});
      out.push_back({"Decision to strike (s)", pass->at - pass->held->at});
      out.push_back({"Passer speed at decision (m/s)", pass->held->speed});
      out.push_back({"Speed lost (m/s)", pass->held->speed - pass->passerSpeed});
    }
    out.push_back({"Angle difficulty", pass->angleDifficulty});
    out.push_back({"Weight difficulty", pass->weightDifficulty});
    if (pass->carriedOut)
    {
      out.push_back({"Angle failed", flag(pass->angleFailed)});
      out.push_back({"Weight failed", flag(pass->weightFailed)});
      out.push_back({"Abs angle error (deg)", pass->angleError * DEGREES});
      out.push_back({"Weight error (m/s)", pass->weightError});
      out.push_back(
          {"Elevation error (deg, + higher)", pass->elevationError * DEGREES});
    }
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
      out.push_back({"Condition: " + std::string(CONTACT_FACTOR_LABELS[index]),
                     pass->factors[index]});
    return out;
  }

 private:
  /** The pass when he decided on it. */
  struct Held
  {
    double at = 0.0;
    float bodyAngle = 0.0f;
    float speed = 0.0f;
  };
  struct Pass
  {
    double at = 0.0;
    StrikeKind kind = StrikeKind::GROUND_PASS;
    bool carriedOut = false;
    bool angleFailed = false;
    bool weightFailed = false;
    float angleError = 0.0f;
    float weightError = 0.0f;
    float elevationError = 0.0f;
    float angleDifficulty = 0.0f;
    float weightDifficulty = 0.0f;
    std::array<float, CONTACT_FACTOR_COUNT> factors{};
    PitchPoint from{};
    float speed = 0.0f;
    float lift = 0.0f;
    float passerSpeed = 0.0f;
    std::optional<Held> held;
    /** His body's angle to where he sent it (deg). */
    float bodyAngle = 0.0f;
  };

  /** His body's angle (deg) to the line from the ball to the receiver. */
  float bodyAngleToTarget(const MatchPlayer& player, PitchPoint ballAt) const
  {
    return bodyAngleTo(player,
                       Vector2F{PITCH.metresX(receiverSpot.x - ballAt.x),
                        PITCH.metresY(receiverSpot.y - ballAt.y)});
  }

  /** His body's angle (deg) to a direction (metres). */
  static float bodyAngleTo(const MatchPlayer& player, Vector2F line)
  {
    const PitchPoint body{
        std::cos(player.facingAngle) * MatchTuning::Pitch::MATCH_LENGTH_METRES,
        std::sin(player.facingAngle) * MatchTuning::Pitch::MATCH_WIDTH_METRES};
    const float lengths = std::hypot(body.x, body.y) * std::hypot(line.x, line.y);
    if (lengths <= 0.0f) return 0.0f;
    return std::acos(std::clamp((body.x * line.x + body.y * line.y) / lengths,
                                -1.0f, 1.0f)) *
           180.0f / PI;
  }

  /** The pass is ordered: he plays it as soon as he can act. */
  void order(MatchEngine& engine)
  {
    ordered = true;
    if (runUp > 0.0f)
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
    if (sideRevs != 0.0f || topRevs != 0.0f)
      spin = BallSpin{sideRevs * 2.0f * PI, topRevs * 2.0f * PI};
    engine.forcePassTo(passerId, receiverSpot, style, spin);
  }

  // Pass
  float length = 15.0f;
  float direction = 0.0f;
  float styleChoice = 0.0f;
  float placeChoice = 0.0f;
  float receiverChoice = 0.0f;
  float sideRevs = 0.0f;
  float topRevs = 0.0f;
  // Passer
  float runUp = 0.0f;
  float paceChoice = 2.0f;
  float passing = 70.0f;
  float dribbling = 70.0f;
  float condition = 100.0f;
  float footChoice = 0.0f;
  // Defender
  float defenderChoice = 0.0f;
  float defenderDistance = 2.0f;
  // The strike's tuning, as this drill plays it.
  StrikeKnobs knobs;

  PlayerID passerId = 0;
  PlayerID receiverId = 0;
  PlayerID defenderId = 0;
  bool hasReceiver = true;
  bool hasDefender = false;
  bool ordered = false;
  PassFlight flight;
  PitchPoint passerStart{};
  PitchPoint passPoint{};
  PitchPoint receiverSpot{};
  PitchPoint defenderSpot{};
  StrikeContactStats before;
  std::optional<Pass> pass;
  std::optional<Held> held;
  float closest = 0.0f;
  float closestHeight = 0.0f;
  float closestSpeed = 0.0f;
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
 public:
  /** Where he meets the ball (pitch), facing up the pitch. */
  static inline const PitchPoint MEET = at(0.5f, 0.5f);
  /** No pass this long after the ball is played: the run ends. */
  static constexpr double TIME_LIMIT = 8.0;
  /** After the pass: the run ends once the ball has stopped, or this long
   * after it. */
  static constexpr double PASS_LIMIT = 6.0;
  static constexpr double LINGER_SECONDS = 0.3;
  /** The ball this much further from the target than it got: gone by. */
  static constexpr float PAST_TARGET_METRES = 3.0f;

  const char* name() const override { return "First-time pass"; }
  const char* summary() const override
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
    add(setting("Ball speed (m/s)", &ballSpeed, 2.0f, 30.0f, false,
                "Pace when it is played to him."),
        "Ball");
    add(setting("Comes from (deg)", &fromAngle, -180.0f, 180.0f, true,
                "Where it is played from, against the way he faces (up the "
                "pitch): 0 in front of him, 90 from his right, -90 from his "
                "left, 180 from behind."),
        "Ball");
    add(setting("Feed distance (m)", &feedDistance, 5.0f, 40.0f, false), "Ball");
    add(setting("Passes him by (m)", &passesBy, -1.5f, 1.5f, false,
                "Sideways offset of its line from where he meets it: 0 "
                "straight at him."),
        "Ball");
    add(setting("Bounce (m/s up)", &bounce, 0.0f, 3.0f, false,
                "Upward speed when played: 0 along the ground. A ball off "
                "the ground when it reaches him is taken with a touch."),
        "Ball");

    DrillParameter play =
        setting("Play it", &playChoice, 0.0f, 1.0f, true,
                "First time: he plays it on as it reaches him.\nAfter a "
                "touch: he controls it, then passes.");
    play.choices = {"First time", "After a touch"};
    add(play, "Pass");
    add(setting("Length (m)", &length, 3.0f, 60.0f, false,
                "From where he meets the ball to the target."),
        "Pass");
    add(setting("Direction (deg)", &direction, -180.0f, 180.0f, true,
                "Where the target is against the way he faces (up the "
                "pitch): 0 straight ahead, 90 to his right, -90 to his left, "
                "180 behind him."),
        "Pass");
    DrillParameter style =
        setting("Style", &styleChoice, 0.0f, 4.0f, true,
                "As the AI would (by its length), or forced along the "
                "ground, lofted, driven (low and hard) or chipped.");
    style.choices = {"As the AI would", "Ground", "Lofted", "Driven", "Chip"};
    add(style, "Pass");
    add(setting("Sidespin (rev/s)", &sideRevs, -10.0f, 10.0f, false,
                "The sidespin he means (+ curls it to his right). 0: the "
                "body's default."),
        "Pass");

    add(setting("Run to meet (m)", &runToMeet, 0.0f, 10.0f, false,
                "He starts this far off the ball's line and goes to it."),
        "Player");
    add(setting("Passing", &passing, 1.0f, 100.0f, true), "Player");
    add(setting("First Touch", &firstTouch, 1.0f, 100.0f, true,
                "His First Touch rating (overrides the derived stat): his "
                "touch when he takes one."),
        "Player");
    add(setting("Pace", &pace, 1.0f, 100.0f, true), "Player");
    DrillParameter foot = setting("Foot", &footChoice, 0.0f, 1.0f, true);
    foot.choices = {"Right", "Left"};
    add(foot, "Player");
    return list;
  }

  std::vector<DrillPreset> presets() const override
  {
    const std::map<std::string, float> base{
        {"Ball speed (m/s)", 12.0f}, {"Comes from (deg)", 90.0f},
        {"Feed distance (m)", 20.0f}, {"Passes him by (m)", 0.0f},
        {"Bounce (m/s up)", 0.0f},    {"Play it", 0.0f},
        {"Length (m)", 15.0f},        {"Direction (deg)", 0.0f},
        {"Style", 1.0f},              {"Run to meet (m)", 0.0f}};
    const auto with = [&base](std::map<std::string, float> changes)
    {
      std::map<std::string, float> settings = base;
      for (const auto& [name, value] : changes) settings[name] = value;
      return settings;
    };
    return {
        {"Square ball, played on", "12 m/s from his right, on 15 m ahead.",
         base},
        {"The same, after a touch", "As above, controlled first.",
         with({{"Play it", 1.0f}})},
        {"Set it back", "12 m/s from in front, back 12 m to one side.",
         with({{"Comes from (deg)", 0.0f},
               {"Length (m)", 12.0f},
               {"Direction (deg)", 30.0f}})},
        {"Round the corner", "From behind his right, on up the line.",
         with({{"Comes from (deg)", 135.0f}})},
        {"Driven in", "20 m/s from his right, on 15 m ahead.",
         with({{"Ball speed (m/s)", 20.0f}})},
        {"Step to it", "8 m/s, he starts 3 m off its line.",
         with({{"Ball speed (m/s)", 8.0f}, {"Run to meet (m)", 3.0f}})},
        {"Bouncing ball", "It reaches him on a hop: he takes a touch.",
         with({{"Bounce (m/s up)", 2.8f}, {"Feed distance (m)", 7.0f}})},
    };
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec player{"Player", PlayerRole::CM, true,
                           {{"Passing", passing}, {"Pace", pace}}, 180,
                           {{DerivedStat::FIRST_TOUCH, firstTouch}}};
    player.foot = footChoice < 0.5f ? Foot::Right : Foot::Left;
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
    const Vector2F up{1.0f, 0.0f};
    from = rotated(up, fromAngle * PI / 180.0f);
    travel = Vector2F{-from.x, -from.y};
    side = rotated(travel, PI * 0.5f);
    feedFrom = along(along(MEET, side, passesBy), from, feedDistance);
    playerStart = along(MEET, side, -runToMeet);
    target = along(MEET, rotated(up, direction * PI / 180.0f), length);
    target = PitchPoint{std::clamp(target.x, PITCH.atX(0.01f), PITCH.atX(0.99f)),
              std::clamp(target.y, PITCH.atY(0.01f), PITCH.atY(0.99f))};

    MatchScenario scenario;
    scenario.players.push_back({playerId, playerStart, false});
    scenario.carrierId = 0;
    scenario.ballPosition = feedFrom;
    engine.startDrill(scenario);
    engine.forcePassTo(playerId, target,
                       static_cast<ForcedPassStyle>(styleChoice),
                       sideRevs != 0.0f
                           ? std::optional<BallSpin>(
                                 BallSpin{sideRevs * 2.0f * PI, 0.0f})
                           : std::nullopt,
                       std::nullopt, playChoice < 0.5f);
    engine.feedBall(feedFrom, {travel.x * ballSpeed, travel.y * ballSpeed},
                    bounce, playerId);
    fedAt = secondsOf(engine);
    before = engine.getStrikeContacts();
    touchedFirst = false;
    pass.reset();
    outcome.clear();
    endAt.reset();
    lastBallVelocity = {travel.x * ballSpeed, travel.y * ballSpeed};
    ballPath = {feedFrom};
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    const MatchPlayer* player = slotOf(engine, playerId);
    if (engine.getSimulatedSteps() % 2 == 0) ballPath.push_back(ball.position);

    if (!pass && outcome.empty())
    {
      // His first touch, if he takes one: the ball his before he passes.
      if (engine.getPossessionAttachment() && engine.getPossessionAttachment()->getId() == playerId)
        touchedFirst = true;
      const StrikeContactStats& after = engine.getStrikeContacts();
      for (std::size_t kind = 0; kind < STRIKE_KIND_COUNT; ++kind)
      {
        const auto& a = before.byKind[kind];
        const auto& b = after.byKind[kind];
        if (b.strikes == a.strikes) continue;
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
          reading.factors[index] =
              static_cast<float>(b.factorSum[index] - a.factorSum[index]);
        reading.speed = std::hypot(ball.velocity.x, ball.velocity.y);
        reading.lift = ball.velocityZ;
        // The ball as it came in (a step before), against where it went.
        reading.incoming = std::hypot(lastBallVelocity.x, lastBallVelocity.y);
        if (reading.incoming > 0.1f && reading.speed > 0.1f)
          reading.redirect = turnDegrees(
              {lastBallVelocity.x / reading.incoming,
               lastBallVelocity.y / reading.incoming},
              {ball.velocity.x / reading.speed, ball.velocity.y / reading.speed});
        if (player)
        {
          reading.playerSpeed = speedOf(*player);
          const PitchPoint body{std::cos(player->facingAngle) * LENGTH,
                              std::sin(player->facingAngle) * WIDTH};
          const Vector2F way = ball.velocity;
          const float lengths =
              std::hypot(body.x, body.y) * std::hypot(way.x, way.y);
          if (lengths > 0.0f)
            reading.bodyAngle =
                std::acos(std::clamp((body.x * way.x + body.y * way.y) / lengths,
                                     -1.0f, 1.0f)) *
                180.0f / PI;
        }
        reading.from = ball.position;
        pass = reading;
        closest = metresBetween(ball.position, target);
        closestSpeed = reading.speed;
        flight.start(ball, target);
        break;
      }
      if (!pass && now >= fedAt + TIME_LIMIT) outcome = "no pass";
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
        outcome = "out of play";
      else if (flight.settled(pass->kind, ball))
        outcome = gap > closest + PAST_TARGET_METRES ? "past the target"
                                                     : "stopped";
      else if (now >= pass->at + PASS_LIMIT)
        outcome = "still rolling";
    }
    if (!pass) lastBallVelocity = ball.velocity;
    if (!outcome.empty() && !endAt) endAt = now;
    return !(endAt && now >= *endAt + LINGER_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::line(renderer, draw, feedFrom, MEET, IM_COL32(255, 255, 255, 70),
                    1.0f * scale);
    DrillDraw::line(renderer, draw, MEET, target, IM_COL32(255, 255, 255, 70),
                    1.0f * scale);
    DrillDraw::marker(renderer, draw, feedFrom, 3.0f * scale,
                      IM_COL32(255, 255, 255, 200), "feed");
    DrillDraw::marker(renderer, draw, MEET, 2.5f * scale,
                      IM_COL32(90, 220, 120, 220), "meet");
    DrillDraw::marker(renderer, draw, target, 3.0f * scale,
                      IM_COL32(90, 170, 255, 255),
                      outcome.empty() ? "target" : outcome.c_str());
    DrillDraw::path(renderer, draw, ballPath, IM_COL32(255, 230, 120, 160),
                    1.5f * scale);
  }

  std::vector<std::string> report() const override
  {
    static constexpr std::array<const char*, 5> STYLES = {
        "as the AI would", "along the ground", "lofted", "driven", "chipped"};
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.0f} m/s from {:+.0f} deg, {:.0f} m out, {:+.1f} m by{} · on {:.0f} m "
        "at {:+.0f} deg, {}, {}",
        ballSpeed, fromAngle, feedDistance, passesBy,
        bounce > 0.0f ? std::format(", {:.1f} m/s up", bounce) : std::string(),
        length, direction, STYLES[static_cast<std::size_t>(styleChoice)],
        playChoice < 0.5f ? "first time" : "after a touch"));
    lines.push_back(std::format(
        "Player: Passing {:.0f}, First Touch {:.0f}, Pace {:.0f}, {} foot · "
        "run to meet {:.1f} m",
        passing, firstTouch, pace, footChoice < 0.5f ? "right" : "left",
        runToMeet));
    if (pass)
    {
      constexpr float DEGREES = 180.0f / PI;
      lines.push_back(std::format(
          "{} {} at {:.2f} s: in at {:.1f} m/s, out at {:.1f} m/s{}, turned "
          "{:.0f} deg; body {:.0f} deg off it, moving {:.1f} m/s",
          pass->firstTime ? "First time:" : "After a touch:",
          STRIKE_KIND_NAMES[static_cast<std::size_t>(pass->kind)],
          pass->at - fedAt, pass->incoming, pass->speed,
          pass->lift > 0.0f ? std::format(" ({:.1f} up)", pass->lift)
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
    if (!outcome.empty()) lines.push_back("OUTCOME  " + outcome);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty()) return {};
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{{"Passed", flag(pass.has_value())}};
    if (!pass) return out;
    constexpr float DEGREES = 180.0f / PI;
    out.push_back({"First time", flag(pass->firstTime)});
    out.push_back({"Kind (0 ground, 1 lofted, 2 cross, 3 shot, 7 driven, 8 chip)",
                   static_cast<double>(pass->kind)});
    out.push_back({"Feed to pass (s)", pass->at - fedAt});
    out.push_back({"Incoming pace (m/s)", pass->incoming});
    out.push_back({"Turned (deg)", pass->redirect});
    out.push_back({"Body off the pass (deg)", pass->bodyAngle});
    out.push_back({"Player speed (m/s)", pass->playerSpeed});
    out.push_back({"Miss at target (m)", closest});
    out.push_back({"Speed at target (m/s)", closestSpeed});
    flight.addMetrics(pass->kind, out);
    out.push_back({"Launch speed (m/s)", pass->speed});
    out.push_back({"Angle difficulty", pass->angleDifficulty});
    out.push_back({"Weight difficulty", pass->weightDifficulty});
    if (pass->carriedOut)
    {
      out.push_back({"Angle failed", flag(pass->angleFailed)});
      out.push_back({"Weight failed", flag(pass->weightFailed)});
      out.push_back({"Abs angle error (deg)", pass->angleError * DEGREES});
      out.push_back({"Weight error (m/s)", pass->weightError});
    }
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
      out.push_back({"Condition: " + std::string(CONTACT_FACTOR_LABELS[index]),
                     pass->factors[index]});
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
    float angleError = 0.0f;
    float weightError = 0.0f;
    float angleDifficulty = 0.0f;
    float weightDifficulty = 0.0f;
    std::array<float, CONTACT_FACTOR_COUNT> factors{};
    PitchPoint from{};
    float speed = 0.0f;
    float lift = 0.0f;
    float incoming = 0.0f;
    float redirect = 0.0f;
    float bodyAngle = 0.0f;
    float playerSpeed = 0.0f;
  };

  // Ball
  float ballSpeed = 12.0f;
  float fromAngle = 90.0f;
  float feedDistance = 20.0f;
  float passesBy = 0.0f;
  float bounce = 0.0f;
  // Pass
  float playChoice = 0.0f;
  float length = 15.0f;
  float direction = 0.0f;
  float styleChoice = 1.0f;
  float sideRevs = 0.0f;
  // Player
  float runToMeet = 0.0f;
  float passing = 70.0f;
  float firstTouch = 70.0f;
  float pace = 70.0f;
  float footChoice = 0.0f;

  PlayerID playerId = 0;
  Vector2F from{-1.0f, 0.0f};
  Vector2F travel{1.0f, 0.0f};
  Vector2F side{0.0f, 1.0f};
  PitchPoint feedFrom{};
  PitchPoint playerStart{};
  PitchPoint target{};
  double fedAt = 0.0;
  StrikeContactStats before;
  bool touchedFirst = false;
  std::optional<Pass> pass;
  float closest = 0.0f;
  float closestSpeed = 0.0f;
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
 public:
  /** A run ends this long after its outcome, or with no outcome
   * TIME_LIMIT seconds after the pass. */
  static constexpr double LINGER_SECONDS = 0.5;
  static constexpr double TIME_LIMIT = 8.0;
  /** The penalty spot (the home side's own), and the way to the centre
   * circle from it (up the pitch, metres). */
  static inline const PitchPoint SPOT =
      at(MatchTuning::Pitch::LEFT_PENALTY_SPOT_X, MatchTuning::Pitch::CENTRE);
  static constexpr Vector2F UP{1.0f, 0.0f};

  std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec passer{"Passer", PlayerRole::CM, true,
                           {{"Passing", passing}}, 180};
    passer.foot = footChoice < 0.5f ? Foot::Right : Foot::Left;
    passer.condition = condition;
    return {passer};
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    passerId = ids[0];
    engine.setStrikeKnobs(knobs);
    MatchScenario scenario;
    scenario.players.push_back({passerId, SPOT, false});
    scenario.carrierId = passerId;
    scenario.ballPosition = SPOT;
    engine.startDrill(scenario);
    engine.forcePassTo(passerId, target(), style(), spin(), arrivalSpeed());
    beginRun(engine);
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    if (engine.getSimulatedSteps() % 2 == 0) ballPath.push_back(ball.position);
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
      else if (now >= 10.0)
      {
        outcome = "no pass";
      }
    }
    else if (outcome.empty())
    {
      outcome = track(ball, now);
      if (outcome.empty() && engine.getState() != MatchState::PLAYING)
        outcome = "out of play";
      if (outcome.empty() && now >= strike->at + TIME_LIMIT)
        outcome = "time up";
    }
    if (!outcome.empty() && !endAt) endAt = now;
    return !(endAt && now >= *endAt + LINGER_SECONDS);
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
    float angleError = 0.0f;
    float weightError = 0.0f;
    float elevationError = 0.0f;
    float angleDifficulty = 0.0f;
    float weightDifficulty = 0.0f;
    /** The conditions' values (0-1). */
    std::array<float, CONTACT_FACTOR_COUNT> factors{};
    /** Off his foot: along the ground and upward (m/s). */
    float speed = 0.0f;
    float lift = 0.0f;
  };

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
    if (sideRevs == 0.0f && topRevs == 0.0f) return std::nullopt;
    return BallSpin{sideRevs * 2.0f * PI, topRevs * 2.0f * PI};
  }
  /** The spin settings, in a report line. */
  [[nodiscard]] static std::string spinText(float sideRevs, float topRevs)
  {
    std::string text;
    if (sideRevs != 0.0f)
      text += std::format(", sidespin {:+.1f} rev/s", sideRevs);
    if (topRevs != 0.0f)
      text += std::format(", {} {:.1f} rev/s",
                          topRevs > 0.0f ? "topspin" : "backspin",
                          std::abs(topRevs));
    return text;
  }
  /** The pace (m/s) he means a ground pass to arrive at; none, as the AI
   * weights it. */
  [[nodiscard]] virtual std::optional<float> arrivalSpeed() const
  {
    return std::nullopt;
  }
  /** Where the ball was struck from. */
  PitchPoint strikeFrom{};
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
    const Vector2F offset = toMetres(point - SPOT);
    return {offset.x * UP.x + offset.y * UP.y, offset.y * UP.x - offset.x * UP.y};
  }
  /** A point given in metres along and across the line. */
  static PitchPoint onLine(float alongMetres, float acrossMetres)
  {
    return along(along(SPOT, UP, alongMetres), {-UP.y, UP.x}, acrossMetres);
  }

  /** The passer's settings and the knobs of this drill's kind. */
  void addPasserAndKnobs(std::vector<DrillParameter>& list)
  {
    const auto add = [&list](DrillParameter parameter, const char* section)
    {
      parameter.section = section;
      list.push_back(std::move(parameter));
    };
    add(setting("Passing", &passing, 1.0f, 100.0f, true), "Passer");
    add(setting("Condition", &condition, 30.0f, 100.0f, true,
                "How fresh he is at the start (tiredness widens the "
                "errors)."),
        "Passer");
    DrillParameter foot = setting("Foot", &footChoice, 0.0f, 1.0f, true);
    foot.choices = {"Right", "Left"};
    add(foot, "Passer");
    // Names live as long as the drill: the settings point at them.
    const auto kindName = STRIKE_KIND_NAMES[static_cast<std::size_t>(kind())];
    for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
    {
      knobNames[index] = strikeKnobName(kind(), static_cast<StrikeKnob>(index));
      knobSections[index] = std::string(kindName) + " knobs: " +
                            std::string(STRIKE_KNOBS[index].group);
    }
    for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
    {
      const StrikeKnobInfo& info = STRIKE_KNOBS[index];
      DrillParameter knob =
          setting(knobNames[index].c_str(),
                  &knobs.at(kind(), static_cast<StrikeKnob>(index)), info.min,
                  info.max, false);
      knob.collapsible = true;
      knob.label = info.name.data();
      add(knob, knobSections[index].c_str());
    }
  }

  /** The passer and his strike, for the report. */
  void reportStrike(std::vector<std::string>& lines) const
  {
    lines.push_back(std::format("Passer: Passing {:.0f}, condition {:.0f}, {} foot",
                                passing, condition,
                                footChoice < 0.5f ? "right" : "left"));
    if (!strike) return;
    constexpr float DEGREES = 180.0f / PI;
    lines.push_back(std::format(
        "{} at {:.2f} s, {:.1f} m/s{}",
        STRIKE_KIND_NAMES[static_cast<std::size_t>(strike->kind)], strike->at,
        strike->speed,
        strike->lift > 0.0f ? std::format(" ({:.1f} up)", strike->lift)
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
      lines.push_back(
          std::format("OUTCOME  {}  {}", passed ? "PASS" : "FAIL", outcome));
  }

  /** How hard the last pass was: each job's difficulty and the conditions
   * (0-1) that made it so, below the settings. */
  void renderAfterSettings() const override
  {
    ImGui::SeparatorText("Difficulty of the last pass");
    if (!strike)
    {
      ImGui::TextDisabled("No pass yet.");
      return;
    }
    if (!ImGui::BeginTable("##difficulty", 2, ImGuiTableFlags_SizingStretchProp))
      return;
    const auto row = [](const char* name, float value, bool strong)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (strong)
        ImGui::TextUnformatted(name);
      else
        ImGui::TextDisabled("%s", name);
      ImGui::TableNextColumn();
      ImGui::Text("%.3f", static_cast<double>(value));
    };
    row("Angle difficulty", strike->angleDifficulty, true);
    row("Weight difficulty", strike->weightDifficulty, true);
    // The conditions that were there at all.
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
      if (strike->factors[index] > 0.0005f)
        row(std::string(CONTACT_FACTOR_LABELS[index]).c_str(), strike->factors[index], false);
    ImGui::EndTable();
  }

  /** The strike, as numbers (Measure mode). */
  void strikeMetrics(std::vector<DrillMetric>& out) const
  {
    if (!strike) return;
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    constexpr float DEGREES = 180.0f / PI;
    out.push_back({"Launch speed (m/s)", strike->speed});
    out.push_back({"Launch lift (m/s)", strike->lift});
    out.push_back({"Angle difficulty", strike->angleDifficulty});
    out.push_back({"Weight difficulty", strike->weightDifficulty});
    out.push_back({"Angle failed", flag(strike->angleFailed)});
    out.push_back({"Weight failed", flag(strike->weightFailed)});
    out.push_back({"Abs angle error (deg)", strike->angleError * DEGREES});
    out.push_back({"Weight error (m/s)", strike->weightError});
    out.push_back(
        {"Elevation error (deg, + higher)", strike->elevationError * DEGREES});
  }

  // Passer
  float passing = 70.0f;
  float condition = 100.0f;
  float footChoice = 0.0f;
  // The strike's tuning, as this drill plays it.
  StrikeKnobs knobs;

  PlayerID passerId = 0;
  std::optional<Strike> strike;
  std::string outcome;
  std::optional<double> endAt;
  std::vector<PitchPoint> ballPath;

 private:
  /** The pass: a strike the contact model has read since the start. */
  void readStrike(const MatchEngine& engine, double now)
  {
    const StrikeContactStats& after = engine.getStrikeContacts();
    for (std::size_t index = 0; index < STRIKE_KIND_COUNT; ++index)
    {
      const auto& a = before.byKind[index];
      const auto& b = after.byKind[index];
      if (b.strikes == a.strikes) continue;
      const MatchBall& ball = engine.getBall();
      Strike reading;
      reading.at = now;
      reading.kind = static_cast<StrikeKind>(index);
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
        reading.factors[factor] =
            static_cast<float>(b.factorSum[factor] - a.factorSum[factor]);
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
 public:
  const char* name() const override { return "Ground pass corridor"; }
  const char* summary() const override
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
    add(setting("Range (m)", &rangeMetres, 3.0f, 60.0f, false,
                "From the penalty spot to the target, towards the centre "
                "circle."));
    add(setting("Corridor width (m)", &width, 0.5f, 10.0f, false,
                "Between the two rows of cones (the ball's centre must stay "
                "inside)."));
    add(setting("Cone spacing (m)", &coneSpacing, 1.0f, 10.0f, false,
                "Along each row, from the spot to the target's line."));
    add(setting("Sidespin (rev/s)", &sideRevs, -10.0f, 10.0f, false,
                "The sidespin he means to put on it: + curls it to his "
                "right, - to his left (it fades as it rolls). Held to what "
                "his Passing can put on. 0: a straight pass, curved only by "
                "accident (less often the better he passes)."));
    if (kind() == StrikeKind::GROUND_PASS)
      add(setting("Arrival speed (m/s)", &arrivalChoice, 0.0f, 20.0f, false,
                  "The pace he means it to reach the target at (the brain's "
                  "call). 0: as the AI weights it (by his Passing and the "
                  "distance)."));
    addPasserAndKnobs(list);
    return list;
  }

  std::vector<DrillPreset> presets() const override
  {
    return {
        {"Short pass", "10 m down a 2 m corridor.",
         {{"Range (m)", 10.0f}, {"Corridor width (m)", 2.0f},
          {"Cone spacing (m)", 2.0f}}},
        {"Medium pass", "20 m down a 3 m corridor.",
         {{"Range (m)", 20.0f}, {"Corridor width (m)", 3.0f},
          {"Cone spacing (m)", 4.0f}}},
        {"Long ground pass", "35 m down a 4 m corridor.",
         {{"Range (m)", 35.0f}, {"Corridor width (m)", 4.0f},
          {"Cone spacing (m)", 5.0f}}},
    };
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    const float half = width * 0.5f;
    const ImU32 edge = IM_COL32(255, 140, 30, 90);
    DrillDraw::line(renderer, draw, onLine(0.0f, -half), onLine(rangeMetres, -half),
                    edge, 1.0f * scale);
    DrillDraw::line(renderer, draw, onLine(0.0f, half), onLine(rangeMetres, half),
                    edge, 1.0f * scale);
    DrillDraw::line(renderer, draw, onLine(rangeMetres, -half),
                    onLine(rangeMetres, half), IM_COL32(90, 220, 120, 160),
                    1.5f * scale);
    // Cones every spacing from the spot, and at the target's line.
    const int count = static_cast<int>(rangeMetres / coneSpacing);
    for (int index = 0; index <= count + 1; ++index)
    {
      const float at = std::min(static_cast<float>(index) * coneSpacing, rangeMetres);
      for (const float side : {-half, half})
        DrillDraw::cone(renderer, draw, onLine(at, side), IM_COL32(255, 140, 30, 255));
    }
    DrillDraw::marker(renderer, draw, target(), 3.0f * scale,
                      IM_COL32(90, 220, 120, 220),
                      outcome.empty() ? "target" : outcome.c_str());
    if (leftAt)
      DrillDraw::marker(renderer, draw, *leftAt, 2.5f * scale,
                        IM_COL32(240, 80, 80, 220));
    DrillDraw::path(renderer, draw, ballPath, IM_COL32(255, 230, 120, 160),
                    1.5f * scale);
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.0f} m {}, corridor {:.1f} m wide{}{}", rangeMetres,
        kind() == StrikeKind::GROUND_PASS ? "along the ground" : "driven", width,
        spinText(sideRevs, 0.0f),
        arrivalChoice > 0.0f
            ? std::format(", to arrive at {:.1f} m/s", arrivalChoice)
            : std::string()));
    reportStrike(lines);
    if (strike)
    {
      lines.push_back(std::format("Widest off the line before the target {:.2f} m",
                                  widest));
      if (atTarget)
        lines.push_back(std::format(
            "At the target's line {:+.2f} m off it (+ right), {:.1f} m/s",
            atTarget->x, atTarget->y));
      if (leftAfter)
        lines.push_back(std::format("Left the corridor after {:.1f} m", *leftAfter));
      if (shortBy)
        lines.push_back(std::format("Stopped {:.1f} m short of the target", *shortBy));
      lines.push_back(
          std::format("Closest to the target point {:.0f} cm", closest * 100.0f));
    }
    reportOutcome(lines, outcome == IN_CORRIDOR);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty()) return {};
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{{"Passed", flag(strike.has_value())}};
    if (!strike) return out;
    out.push_back({"In the corridor", flag(outcome == IN_CORRIDOR)});
    out.push_back({"Left the corridor", flag(leftAfter.has_value())});
    out.push_back({"Stopped short", flag(shortBy.has_value())});
    out.push_back({"Widest off the line (m)", widest});
    out.push_back({"Miss from the target (cm)", closest * 100.0f});
    if (leftAfter) out.push_back({"Left the corridor after (m)", *leftAfter});
    if (shortBy) out.push_back({"Short of the target (m)", *shortBy});
    if (atTarget)
    {
      out.push_back({"Off the line at the target (m, + right)", atTarget->x});
      out.push_back({"Abs off the line at the target (m)", std::abs(atTarget->x)});
      out.push_back({"Speed at the target (m/s)", atTarget->y});
    }
    strikeMetrics(out);
    return out;
  }

 protected:
  StrikeKind kind() const override { return StrikeKind::GROUND_PASS; }
  ForcedPassStyle style() const override { return ForcedPassStyle::GROUND; }
  float range() const override { return rangeMetres; }
  std::optional<BallSpin> spin() const override
  {
    return spinOf(sideRevs, 0.0f);
  }
  std::optional<float> arrivalSpeed() const override
  {
    return arrivalChoice > 0.0f && kind() == StrikeKind::GROUND_PASS
               ? std::optional<float>(arrivalChoice)
               : std::nullopt;
  }

  void reset() override
  {
    widest = 0.0f;
    last = Vector2F{0.0f, 0.0f};
    atTarget.reset();
    leftAfter.reset();
    leftAt.reset();
    shortBy.reset();
    closest = std::numeric_limits<float>::max();
  }

  void struck(const MatchBall& ball) override { last = lineMetres(ball.position); }

  std::string track(const MatchBall& ball, double /*now*/) override
  {
    // Followed on past the corridor's edge, to the target's line (or until
    // it stops), so a pass out of the corridor has its miss too.
    const Vector2F now = lineMetres(ball.position);
    const Vector2F was = last;
    last = now;
    const float half = width * 0.5f;
    // Closest to the target point over the step's move.
    {
      const Vector2F move{now.x - was.x, now.y - was.y};
      const float moved = move.x * move.x + move.y * move.y;
      const float share =
          moved > 1e-9f
              ? std::clamp(((rangeMetres - was.x) * move.x - was.y * move.y) / moved,
                           0.0f, 1.0f)
              : 0.0f;
      closest = std::min(closest, std::hypot(was.x + move.x * share - rangeMetres,
                                             was.y + move.y * share));
    }
    const auto leave = [&](float at, PitchPoint where)
    {
      if (leftAfter) return;
      leftAfter = at;
      leftAt = where;
    };
    // Over the target's line: where it crossed, and how fast.
    if (now.x >= rangeMetres)
    {
      const float share =
          now.x > was.x ? (rangeMetres - was.x) / (now.x - was.x) : 1.0f;
      const float across = was.y + (now.y - was.y) * std::clamp(share, 0.0f, 1.0f);
      if (!leftAfter) widest = std::max(widest, std::abs(across));
      if (std::abs(across) > half) leave(rangeMetres, onLine(rangeMetres, across));
      atTarget = Vector2F{across, std::hypot(ball.velocity.x, ball.velocity.y)};
      return leftAfter ? OUT_OF_CORRIDOR : IN_CORRIDOR;
    }
    if (!leftAfter) widest = std::max(widest, std::abs(now.y));
    // Back out of the corridor's mouth.
    if (now.x < 0.0f) leave(0.0f, ball.position);
    if (std::abs(now.y) > half)
    {
      // Where it went over the edge (the step's move, crossed at the edge).
      const float edge = now.y > 0.0f ? half : -half;
      const float share = std::abs(now.y - was.y) > 1e-6f
                              ? std::clamp((edge - was.y) / (now.y - was.y), 0.0f, 1.0f)
                              : 1.0f;
      const float at = was.x + (now.x - was.x) * share;
      leave(at, onLine(at, edge));
    }
    if (std::hypot(ball.velocity.x, ball.velocity.y) <= MatchTuning::Ball::STOP_SPEED)
    {
      shortBy = rangeMetres - now.x;
      return leftAfter ? OUT_OF_CORRIDOR : "stopped short";
    }
    return {};
  }

 private:
  static constexpr const char* IN_CORRIDOR = "reached the target in the corridor";
  static constexpr const char* OUT_OF_CORRIDOR = "out of the corridor";

  float rangeMetres = 20.0f;
  float width = 3.0f;
  float coneSpacing = 4.0f;
  float sideRevs = 0.0f;
  float arrivalChoice = 0.0f;

  /** The ball's last place along and across the line (m). */
  Vector2F last{};
  float widest = 0.0f;
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
 public:
  const char* name() const override { return "Lofted pass target"; }
  const char* summary() const override
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
    add(setting("Range (m)", &rangeMetres, 5.0f, 60.0f, false,
                "From the penalty spot to the bullseye, towards the centre "
                "circle."));
    add(setting("Ring width (m)", &ringWidth, 0.5f, 5.0f, false,
                "Each ring's width: the bullseye's radius, and the step out to "
                "each ring beyond it."));
    add(setting("Rings", &ringCount, 1.0f, 10.0f, true,
                "How many rings, the bullseye included. A landing in the "
                "bullseye scores this many, the outer ring 1, outside 0."));
    add(setting("Sidespin (rev/s)", &sideRevs, -10.0f, 10.0f, false,
                "The sidespin he means to put on it: + curls it to his "
                "right, - to his left (it fades in flight). 0 with no "
                "topspin: as the AI strikes it, curled only by accident "
                "(less often the better he passes)."));
    add(setting("Topspin (rev/s)", &topRevs, -10.0f, 10.0f, false,
                "+ topspin (it dips, and runs on at the bounce), - backspin "
                "(it holds up, and checks at the bounce). Held, with the "
                "sidespin, to what his Passing can put on."));
    addPasserAndKnobs(list);
    return list;
  }

  std::vector<DrillPreset> presets() const override
  {
    return {
        {"Short lofted pass", "20 m.", {{"Range (m)", 20.0f}}},
        {"Long ball", "35 m.", {{"Range (m)", 35.0f}}},
        {"Very long ball", "50 m.", {{"Range (m)", 50.0f}}},
    };
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    const int rings = static_cast<int>(ringCount);
    // Outermost first, alternating, so each inner ring covers the middle.
    for (int ring = rings; ring >= 1; --ring)
    {
      const bool red = (rings - ring) % 2 == 0;
      DrillDraw::circle(renderer, draw, target(),
                        ringWidth * static_cast<float>(ring),
                        red ? IM_COL32(220, 60, 60, 110) : IM_COL32(245, 245, 245, 90),
                        IM_COL32(20, 20, 20, 160), 1.0f * scale);
    }
    DrillDraw::line(renderer, draw, SPOT, target(), IM_COL32(255, 255, 255, 50),
                    1.0f * scale);
    DrillDraw::marker(renderer, draw, SPOT, 2.5f * scale,
                      IM_COL32(255, 255, 255, 200), "pass");
    if (landing)
      DrillDraw::marker(renderer, draw, landing->point, 3.0f * scale,
                        IM_COL32(255, 230, 120, 255),
                        std::format("{:.1f} m", landing->miss).c_str());
    DrillDraw::path(renderer, draw, ballPath, IM_COL32(255, 230, 120, 160),
                    1.5f * scale);
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.0f} m {}, {} rings {:.1f} m wide{}", rangeMetres,
        kind() == StrikeKind::CHIP ? "chipped" : "lofted",
        static_cast<int>(ringCount), ringWidth, spinText(sideRevs, topRevs)));
    reportStrike(lines);
    if (landing)
      lines.push_back(std::format(
          "Landed {:.2f} m from the centre ({:+.2f} m long, {:+.2f} m right) "
          "after {:.2f} s, at {:.1f} m/s: {}",
          landing->miss, landing->longer, landing->right, landing->flight,
          landing->speed,
          landing->score > 0 ? std::format("{} points", landing->score)
                             : std::string("outside the rings")));
    if (strike)
      lines.push_back(std::format(
          "Peak {:.1f} m; {:.0f} m out {}", peak, CLEAR_METRES,
          clearHeight ? std::format("{:.1f} m up", *clearHeight)
                      : std::string("already down")));
    reportOutcome(lines, landing && landing->score > 0);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty()) return {};
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{{"Passed", flag(strike.has_value())}};
    if (!strike) return out;
    out.push_back({"Landed", flag(landing.has_value())});
    if (landing)
    {
      out.push_back({"Miss (m)", landing->miss});
      out.push_back({"Long (m, - short)", landing->longer});
      out.push_back({"Right (m, - left)", landing->right});
      out.push_back({"Score", static_cast<double>(landing->score)});
      out.push_back({"On the target", flag(landing->score > 0)});
      out.push_back({"Flight (s)", landing->flight});
      out.push_back({"Speed at landing (m/s)", landing->speed});
    }
    out.push_back({"Peak height (m)", peak});
    out.push_back({"Height 4 m out (m, 0 already down)", clearHeight.value_or(0.0f)});
    strikeMetrics(out);
    return out;
  }

 protected:
  StrikeKind kind() const override { return StrikeKind::LOFTED_PASS; }
  ForcedPassStyle style() const override { return ForcedPassStyle::LOFTED; }
  float range() const override { return rangeMetres; }
  std::optional<BallSpin> spin() const override
  {
    return spinOf(sideRevs, topRevs);
  }

  void reset() override
  {
    landing.reset();
    previous = {};
    peak = 0.0f;
    clearHeight.reset();
  }

  void struck(const MatchBall& ball) override
  {
    previous = stateOf(ball);
    peak = previous.height;
  }

  std::string track(const MatchBall& ball, double now) override
  {
    const BallState was = previous;
    previous = stateOf(ball);
    peak = std::max(peak, previous.height);
    // Its height as it passes the chip's clearance distance from where it
    // was struck (still up).
    if (!clearHeight && !landing)
    {
      const float from = lineMetres(strikeFrom).x;
      const float before = lineMetres(was.position).x - from;
      const float after = lineMetres(previous.position).x - from;
      if (before < CLEAR_METRES && after >= CLEAR_METRES)
      {
        const float share = (CLEAR_METRES - before) / std::max(after - before, 1e-6f);
        clearHeight = was.height + (previous.height - was.height) * share;
      }
    }
    // Down: bounced since the last step (falling then, rising now), or on
    // the ground. Where it came down is worked out from the last step,
    // falling under gravity (drag is small over a step).
    const bool bounced = was.lift < 0.0f && previous.lift > 0.0f;
    const bool grounded = previous.height <= 0.01f && previous.lift <= 0.0f;
    if (!bounced && !grounded) return {};
    constexpr float G = MatchTuning::Ball::GRAVITY;
    const float fall = (was.lift + std::sqrt(was.lift * was.lift +
                                             2.0f * G * std::max(was.height, 0.0f))) /
                       G;
    const PitchPoint point{was.position.x + PITCH.fromMetresX(was.velocity.x * fall),
                         was.position.y + PITCH.fromMetresY(was.velocity.y * fall)};
    Landing down;
    down.point = point;
    const Vector2F offset = lineMetres(point);
    down.longer = offset.x - rangeMetres;
    down.right = offset.y;
    down.miss = std::hypot(down.longer, down.right);
    const int ring = static_cast<int>(std::floor(down.miss / ringWidth));
    const int rings = static_cast<int>(ringCount);
    down.score = ring < rings ? rings - ring : 0;
    down.flight = static_cast<float>(now - STEP - strike->at) + fall;
    down.speed = std::hypot(was.velocity.x, was.velocity.y);
    landing = down;
    return down.score > 0
               ? std::format("landed on the target, {} points", down.score)
               : std::string("landed outside the rings");
  }

 private:
  struct BallState
  {
    PitchPoint position{};
    Vector2F velocity{};
    /** Metres up, and upward speed (m/s). */
    float height = 0.0f;
    float lift = 0.0f;
  };
  static BallState stateOf(const MatchBall& ball)
  {
    return {ball.position, ball.velocity,
            MatchPitch::heightMetres(ball.z), ball.velocityZ};
  }
  struct Landing
  {
    PitchPoint point{};
    /** From the centre (m); long (+) or short, right (+) or left (m). */
    float miss = 0.0f;
    float longer = 0.0f;
    float right = 0.0f;
    int score = 0;
    /** From the strike to coming down (s, roughly: the strike is read at
     * the end of its step). */
    float flight = 0.0f;
    /** Its pace over the ground as it came down (m/s). */
    float speed = 0.0f;
  };
  /** Where its height is read on the way: the chip's clearance distance. */
  static constexpr float CLEAR_METRES =
      MatchTuning::Passing::CHIP_CLEAR_DISTANCE_METRES;
  float peak = 0.0f;
  std::optional<float> clearHeight;

 protected:
  float rangeMetres = 35.0f;
  float ringWidth = 1.0f;
  float ringCount = 5.0f;
  float sideRevs = 0.0f;
  float topRevs = 0.0f;

  BallState previous;
  std::optional<Landing> landing;
};

/** Driven passes down the corridor: low and hard with the instep. */
class DrivenCorridorDrill final : public CorridorPassDrill
{
 public:
  const char* name() const override { return "Driven pass corridor"; }
  const char* summary() const override
  {
    return "A driven pass (low and hard, struck with the instep, coming "
           "down at the target) from the penalty spot down a corridor of "
           "cones to a target on the line towards the centre circle. "
           "Measures how far off the line it goes (seen from above), where "
           "it leaves the corridor, its pace at the target, and how the "
           "strike came off, with his Passing and every driven-pass strike "
           "knob as settings.";
  }
  std::vector<DrillPreset> presets() const override
  {
    return {
        {"Short driven pass", "15 m down a 2 m corridor.",
         {{"Range (m)", 15.0f}, {"Corridor width (m)", 2.0f},
          {"Cone spacing (m)", 3.0f}}},
        {"Driven pass", "25 m down a 3 m corridor.",
         {{"Range (m)", 25.0f}, {"Corridor width (m)", 3.0f},
          {"Cone spacing (m)", 5.0f}}},
        {"Long driven ball", "40 m down a 5 m corridor.",
         {{"Range (m)", 40.0f}, {"Corridor width (m)", 5.0f},
          {"Cone spacing (m)", 5.0f}}},
    };
  }

 protected:
  StrikeKind kind() const override { return StrikeKind::DRIVEN_PASS; }
  ForcedPassStyle style() const override { return ForcedPassStyle::DRIVEN; }
};

/** Chips at a bullseye: steep, with backspin, over someone close. */
class ChipTargetDrill final : public BullseyePassDrill
{
 public:
  ChipTargetDrill() { rangeMetres = 15.0f; }
  const char* name() const override { return "Chip target"; }
  const char* summary() const override
  {
    return "A chip (stabbed up steeply with backspin, clearing 2.5 m 4 m "
           "out) from the penalty spot to a bullseye on the ground on the "
           "line towards the centre circle. Measures where it first comes "
           "down, how high it was 4 m out and at its peak, its pace as it "
           "lands, and how the strike came off, with his Passing and every "
           "chip strike knob as settings.";
  }
  std::vector<DrillPreset> presets() const override
  {
    return {
        {"Short chip", "8 m.", {{"Range (m)", 8.0f}}},
        {"Chip", "15 m.", {{"Range (m)", 15.0f}}},
        {"Long chip", "25 m.", {{"Range (m)", 25.0f}}},
    };
  }

 protected:
  StrikeKind kind() const override { return StrikeKind::CHIP; }
  ForcedPassStyle style() const override { return ForcedPassStyle::CHIP; }
};

/**
 * A keeper's ball from his hands at a bullseye (goalkeeper plan step 5):
 * from the penalty spot, in his own area, a roll (up to 25 m) or overarm
 * throw (beyond), or a punt. Measures where it first comes down and how far
 * from him; set the range beyond his reach to see his longest.
 */
class KeeperThrowDrill final : public BullseyePassDrill
{
 public:
  KeeperThrowDrill() { rangeMetres = 40.0f; }
  const char* name() const override { return "Keeper throw"; }
  const char* summary() const override
  {
    return "A keeper with the ball in his hands on the penalty spot (his own "
           "area) lets it go at a bullseye towards the centre circle: rolled "
           "(up to 25 m) or thrown overarm (beyond), or punted. Measures "
           "where it first comes down, how far from where he let it go, and "
           "how it came off, with his Goalkeeping (Throwing's stand-in). "
           "Binturong body.";
  }
  std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec keeper{"Keeper", PlayerRole::GK, true, {{"Goalkeeping", goalkeeping}}, 188};
    keeper.condition = condition;
    return {keeper};
  }
  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    passerId = ids[0];
    engine.setStrikeKnobs(knobs);
    MatchScenario scenario;
    scenario.players.push_back({passerId, SPOT, false});
    scenario.carrierId = passerId;
    scenario.ballPosition = SPOT;
    scenario.carrierInHands = true;
    engine.startDrill(scenario);
    engine.forcePassTo(passerId, target(), style());
    BullseyePassDrill::beginRun(engine);
  }
  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list = BullseyePassDrill::parameters();
    DrillParameter how = setting("Release", &releaseChoice, 0.0f, 1.0f, true,
                                 "A ground ball (rolled up to 25 m, thrown "
                                 "overarm beyond) or a punt.");
    how.choices = {"Roll or throw", "Punt"};
    how.section = "Keeper";
    list.insert(list.begin(), how);
    DrillParameter gk = setting("Goalkeeping", &goalkeeping, 1.0f, 100.0f, true,
                                "Throwing's stand-in: his fastest throw (20 m "
                                "at 1 to 50 m at 99) and its accuracy.");
    gk.section = "Keeper";
    list.insert(list.begin(), gk);
    return list;
  }
  std::vector<DrillPreset> presets() const override
  {
    return {
        {"Roll", "20 m along the ground.", {{"Range (m)", 20.0f}, {"Release", 0.0f}}},
        {"Overarm throw", "40 m.", {{"Range (m)", 40.0f}, {"Release", 0.0f}}},
        {"Longest throw", "70 m: beyond anyone, so it shows his longest.",
         {{"Range (m)", 70.0f}, {"Release", 0.0f}}},
        {"Punt", "55 m.", {{"Range (m)", 55.0f}, {"Release", 1.0f}}},
    };
  }
  std::vector<DrillMetric> metrics() const override
  {
    std::vector<DrillMetric> out = BullseyePassDrill::metrics();
    if (landing)
      out.push_back({"Carry (m, from where he let it go)",
                     lineMetres(landing->point).x - lineMetres(strikeFrom).x});
    return out;
  }

 protected:
  StrikeKind kind() const override
  {
    return releaseChoice < 0.5f ? StrikeKind::THROW : StrikeKind::LOFTED_PASS;
  }
  ForcedPassStyle style() const override
  {
    return releaseChoice < 0.5f ? ForcedPassStyle::GROUND : ForcedPassStyle::LOFTED;
  }

 private:
  float goalkeeping = 70.0f;
  float releaseChoice = 0.0f;
};

// --- Shot target ---------------------------------------------------------------

/**
 * A dead-ball shot at a chosen spot in an empty goal (Notes and plans/
 * match_engine_shooting_technique_plan.md): the pace off his foot and at
 * the line, where it crosses the line against the spot, and how the strike
 * came off, with his Shooting and every shot strike knob as settings.
 */
class ShotTargetDrill final : public Drill
{
 public:
  static constexpr float GOAL_HALF_WIDTH = MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES;
  static constexpr float BALL_RADIUS = MatchTuning::Units::BALL_RADIUS_METRES;
  static constexpr float CROSSBAR = MatchTuning::Units::CROSSBAR_HEIGHT_METRES;

  const char* name() const override { return "Shot target"; }
  const char* summary() const override
  {
    return "A shot from a dead ball at a spot you choose in an empty goal, "
           "nobody else on the pitch. Measures the pace off his foot and at "
           "the line, where it crosses the line against the spot (on target, "
           "wide or over), and how the strike came off, with his Shooting and "
           "every shot strike knob as settings.";
  }
  std::vector<DrillPreset> presets() const override
  {
    return {
        {"Penalty spot", "11 m, central.",
         {{"Out from goal line (m)", 11.0f}, {"Off centre (m)", 0.0f}}},
        {"Edge of the box", "18 m, central.",
         {{"Out from goal line (m)", 18.0f}, {"Off centre (m)", 0.0f}}},
        {"From range", "25 m, central.",
         {{"Out from goal line (m)", 25.0f}, {"Off centre (m)", 0.0f}}},
        {"Cutback, first time",
         "12 m out, central: a 12 m/s ball pulled back from the byline on his "
         "right, struck first time.",
         {{"Out from goal line (m)", 12.0f},
          {"Off centre (m)", 0.0f},
          {"Ball comes in", 1.0f},
          {"Ball speed (m/s)", 12.0f},
          {"Comes from (deg)", 45.0f},
          {"Feed distance (m)", 15.0f}}},
        {"Cutback, after a touch", "The same, controlled first.",
         {{"Out from goal line (m)", 12.0f},
          {"Off centre (m)", 0.0f},
          {"Ball comes in", 2.0f},
          {"Ball speed (m/s)", 12.0f},
          {"Comes from (deg)", 45.0f},
          {"Feed distance (m)", 15.0f}}},
        {"Lay-off, first time",
         "18 m out: a 10 m/s ball rolled back to him from in front, struck "
         "first time.",
         {{"Out from goal line (m)", 18.0f},
          {"Off centre (m)", 0.0f},
          {"Ball comes in", 1.0f},
          {"Ball speed (m/s)", 10.0f},
          {"Comes from (deg)", 0.0f},
          {"Feed distance (m)", 10.0f}}},
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
    add(setting("Out from goal line (m)", &line, 3.0f, 40.0f, false,
                "Distance of the ball from the goal line."),
        "Shot");
    add(setting("Off centre (m)", &side, -25.0f, 25.0f, false,
                "Sideways from the middle of the goal (+ towards the bottom "
                "touchline)."),
        "Shot");
    add(setting("Aim across (m)", &aimAcross, -3.66f, 3.66f, false,
                "Where in the goal he aims: from its middle (+ towards the "
                "bottom touchline; a post is 3.66 m out)."),
        "Shot");
    add(setting("Aim height (m)", &aimHeight, 0.1f, 2.44f, false,
                "How high he aims to cross the line (the bar is at 2.44 m)."),
        "Shot");
    DrillParameter power =
        setting("Power share", &powerShare, 0.0f, 1.0f, false,
                "The share of his shot power he strikes it with. 0: as the "
                "engine strikes a shot (Shooting::DEFAULT_POWER_SHARE).");
    power.automatic = [] { return MatchTuning::Shooting::DEFAULT_POWER_SHARE; };
    add(power, "Shot");
    DrillParameter how = setting(
        "Technique", &techniqueChoice, 0.0f, 4.0f, true,
        "Driven: the instep, hard, a little topspin (every shot's until the "
        "brain chooses).\nPlaced: the side of the foot, slower.\nCurled: "
        "the instep's wrap, sidespin bending it in towards the middle.\n"
        "Chip: stabbed under, backspin, over the keeper.\nToe-poke.");
    how.choices = {"Driven", "Placed", "Curled", "Chip", "Toe-poke"};
    add(how, "Shot");
    DrillParameter keeper = setting(
        "Keeper", &keeperChoice, 0.0f, 1.0f, true,
        "None: an empty goal.\nOff his line: a keeper held on the line from "
        "the goal's middle to the ball, this far out (a chip goes over his "
        "reach).");
    keeper.choices = {"None", "Off his line"};
    add(keeper, "Keeper");
    add(setting("Keeper off his line (m)", &keeperOff, 0.0f, 12.0f, false),
        "Keeper");
    add(setting("Keeper height (cm)", &keeperHeight, 165.0f, 205.0f, true),
        "Keeper");
    add(setting("Goalkeeping", &goalkeeping, 1.0f, 100.0f, true), "Keeper");
    DrillParameter feed = setting(
        "Ball comes in", &feedChoice, 0.0f, 2.0f, true,
        "On his feet: a dead ball (or his run-up's).\nFirst time: played to "
        "him on the spot, and struck as it reaches him.\nAfter a touch: "
        "played to him, controlled, then struck.");
    feed.choices = {"On his feet", "First time", "After a touch"};
    add(feed, "Ball");
    add(setting("Ball speed (m/s)", &ballSpeed, 2.0f, 30.0f, false,
                "Pace when it is played to him."),
        "Ball");
    add(setting("Comes from (deg)", &fromAngle, -180.0f, 180.0f, true,
                "Where it is played from, against the way to goal: 0 from "
                "the goal's side, 90 from the bottom touchline's side, -90 "
                "from the top's, 180 from behind him."),
        "Ball");
    add(setting("Feed distance (m)", &feedDistance, 5.0f, 40.0f, false), "Ball");
    add(setting("Bounce (m/s up)", &bounce, 0.0f, 3.0f, false,
                "Upward speed when played: 0 along the ground. A ball off "
                "the ground when it reaches him is taken with a touch."),
        "Ball");
    add(setting("Run-up (m)", &runUp, 0.0f, 30.0f, false,
                "0: he shoots standing. Otherwise he runs with the ball this "
                "far, ending on the spot, then shoots on the move (a ball on "
                "his feet only)."),
        "Shooter");
    add(setting("Run-up direction (deg)", &runDirection, -180.0f, 180.0f, true,
                "Which way he runs onto the spot against the way to goal: 0 "
                "straight at it, 90 across it towards the bottom touchline, "
                "-90 towards the top, 180 away from goal."),
        "Shooter");
    DrillParameter pace =
        setting("Run-up pace", &paceChoice, 0.0f, 2.0f, true,
                "How hard he carries it on the run-up: a jog, a run or a "
                "sprint (as his Dribbling allows).");
    pace.choices = {"Jog", "Run", "Sprint"};
    add(pace, "Shooter");
    add(setting("Shooting", &shooting, 1.0f, 100.0f, true), "Shooter");
    add(setting("Dribbling", &dribbling, 1.0f, 100.0f, true,
                "With the ball on the run-up: his pace, agility and "
                "acceleration with it."),
        "Shooter");
    add(setting("Condition", &condition, 30.0f, 100.0f, true,
                "How fresh he is at the start (tiredness widens the errors)."),
        "Shooter");
    DrillParameter foot = setting("Foot", &footChoice, 0.0f, 1.0f, true);
    foot.choices = {"Right", "Left"};
    add(foot, "Shooter");
    // Names live as long as the drill: the settings point at them.
    const auto kindName = STRIKE_KIND_NAMES[static_cast<std::size_t>(StrikeKind::SHOT)];
    for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
    {
      knobNames[index] =
          strikeKnobName(StrikeKind::SHOT, static_cast<StrikeKnob>(index));
      knobSections[index] = std::string(kindName) + " knobs: " +
                            std::string(STRIKE_KNOBS[index].group);
    }
    for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
    {
      const StrikeKnobInfo& info = STRIKE_KNOBS[index];
      DrillParameter knob =
          setting(knobNames[index].c_str(),
                  &knobs.at(StrikeKind::SHOT, static_cast<StrikeKnob>(index)),
                  info.min, info.max, false);
      knob.collapsible = true;
      knob.label = info.name.data();
      add(knob, knobSections[index].c_str());
    }
    return list;
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    DrillPlayerSpec shooter{"Shooter", PlayerRole::ST, true,
                            {{"Shooting", shooting}, {"Dribbling", dribbling}},
                            180};
    shooter.foot = footChoice < 0.5f ? Foot::Right : Foot::Left;
    shooter.condition = condition;
    std::vector<DrillPlayerSpec> list{shooter};
    if (keeperChoice > 0.5f)
      list.push_back({"Keeper", PlayerRole::GK, false,
                      {{"Goalkeeping", goalkeeping}},
                      static_cast<int>(keeperHeight)});
    return list;
  }

  /** The technique asked (none: driven, as the engine strikes a shot). */
  [[nodiscard]] std::optional<ShotTechnique> technique() const
  {
    if (techniqueChoice < 0.5f) return std::nullopt;
    return static_cast<ShotTechnique>(static_cast<int>(techniqueChoice + 0.5f));
  }

  /** The keeper, held on his spot until the shot. */
  void placeKeeper(MatchEngine& engine, MatchScenario& scenario,
                   const std::vector<PlayerID>& ids)
  {
    hasKeeper = keeperChoice > 0.5f && ids.size() > 1;
    keeperId = hasKeeper ? ids[1] : 0;
    if (!hasKeeper) return;
    const PitchPoint goal = at(1.0f, 0.5f);
    keeperAt = along(goal, directionBetween(goal, spot),
                     std::min(keeperOff, std::max(metresBetween(goal, spot) - 1.0f,
                                                  0.0f)));
    keeperAt.x = std::min(keeperAt.x, PITCH.atX(0.995f));
    scenario.players.push_back({keeperId, keeperAt, false});
    (void)engine;
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    shooterId = ids[0];
    engine.setStrikeKnobs(knobs);
    spot = at(std::clamp(1.0f - line / LENGTH, 0.5f, 0.995f),
            std::clamp(0.5f + side / WIDTH, 0.01f, 0.99f));
    ordered = false;
    orderedAt = {};
    touchedFirst = false;
    strike.reset();
    outcome.clear();
    endAt.reset();
    ballPath.clear();
    if (feedChoice > 0.5f)
    {
      // Played to him on the spot from `fromAngle` round from the goal's
      // side; the shot is ordered as it is played (first time, or after his
      // touch).
      const Vector2F from = rotated({1.0f, 0.0f}, fromAngle * PI / 180.0f);
      feedFrom = along(spot, from, feedDistance);
      feedFrom = PitchPoint{std::clamp(feedFrom.x, PITCH.atX(0.01f), PITCH.atX(0.99f)),
                  std::clamp(feedFrom.y, PITCH.atY(0.01f), PITCH.atY(0.99f))};
      start = spot;
      MatchScenario scenario;
      scenario.players.push_back({shooterId, spot, false});
      placeKeeper(engine, scenario, ids);
      scenario.carrierId = 0;
      scenario.ballPosition = feedFrom;
      engine.startDrill(scenario);
      if (hasKeeper) engine.setDrillTarget(keeperId, keeperAt, false);
      engine.forceShot(shooterId, Vector2F{aimAcross, aimHeight},
                       powerShare > 0.0f ? std::optional<float>(powerShare)
                                         : std::nullopt,
                       feedChoice < 1.5f, technique());
      const Vector2F travel = toMetres(spot - feedFrom);
      const float metres = std::hypot(travel.x, travel.y);
      lastBallVelocity = metres > 0.0f ? Vector2F{travel.x / metres * ballSpeed,
                                                  travel.y / metres * ballSpeed}
                                       : Vector2F{};
      engine.feedBall(feedFrom, lastBallVelocity, bounce, shooterId);
      before = engine.getStrikeContacts();
      ordered = true;
      if (const MatchPlayer* shooter = slotOf(engine, shooterId))
        orderedAt = Moment{secondsOf(engine),
                           bodyAngleTo(*shooter, lineToAim(spot)),
                           speedOf(*shooter)};
      ballPath.push_back(feedFrom);
      return;
    }
    // His run-up ends on the spot, along `runDirection` from the way to goal.
    const Vector2F runWay = rotated({1.0f, 0.0f}, runDirection * PI / 180.0f);
    start = along(spot, runWay, -runUp);
    start = PitchPoint{std::clamp(start.x, PITCH.atX(0.01f), PITCH.atX(0.99f)), std::clamp(start.y, PITCH.atY(0.01f), PITCH.atY(0.99f))};
    MatchScenario scenario;
    scenario.players.push_back({shooterId, start, false});
    placeKeeper(engine, scenario, ids);
    scenario.carrierId = shooterId;
    scenario.ballPosition = start;
    engine.startDrill(scenario);
    if (hasKeeper) engine.setDrillTarget(keeperId, keeperAt, false);
    // He keeps it (deciding nothing) until the shot is ordered: alone in
    // front of goal, he would otherwise shoot as he pleased. On a run-up he
    // carries it through the spot.
    if (runUp > 0.0f)
    {
      engine.keepBall(shooterId, true, (paceChoice + 1.0f) / 3.0f);
      engine.setDrillTarget(shooterId, along(spot, runWay, RUN_THROUGH_METRES),
                            true);
    }
    else
    {
      engine.keepBall(shooterId, true);
      engine.setDrillTarget(shooterId, spot, false);
    }
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    if (engine.getSimulatedSteps() % 2 == 0) ballPath.push_back(ball.position);
    const MatchPlayer* shooter = slotOf(engine, shooterId);
    if (!ordered && engine.getState() == MatchState::PLAYING && shooter &&
        (runUp <= 0.0f || metresBetween(start, shooter->position) >= runUp ||
         now >= 6.0))
    {
      // Once play is running (a shot struck before then is a dead ball,
      // which the contact tallies leave out), and his run-up is done: his
      // body against the shot's line then, and his speed.
      orderedAt = Moment{now, bodyAngleTo(*shooter, lineToAim(ball.position)),
                         speedOf(*shooter)};
      engine.keepBall(shooterId, false);
      engine.forceShot(shooterId, Vector2F{aimAcross, aimHeight},
                       powerShare > 0.0f ? std::optional<float>(powerShare)
                                         : std::nullopt,
                       false, technique());
      before = engine.getStrikeContacts();
      ordered = true;
    }
    else if (ordered && !strike)
    {
      // A ball played to him: his touch, if he takes one, before the shot.
      if (engine.getPossessionAttachment() && engine.getPossessionAttachment()->getId() == shooterId)
        touchedFirst = true;
      readStrike(engine, now);
      if (strike && shooter)
      {
        strike->bodyAngle = bodyAngleTo(*shooter, ball.velocity);
        strike->shooterSpeed = speedOf(*shooter);
        // The ball as it came in (a step before), against where it went.
        strike->firstTime = feedChoice > 0.5f && !touchedFirst;
        strike->incoming = std::hypot(lastBallVelocity.x, lastBallVelocity.y);
        const float out = std::hypot(ball.velocity.x, ball.velocity.y);
        if (strike->incoming > 0.1f && out > 0.1f)
          strike->redirect = turnDegrees(
              {lastBallVelocity.x / strike->incoming,
               lastBallVelocity.y / strike->incoming},
              {ball.velocity.x / out, ball.velocity.y / out});
        // He stays where he struck it; the keeper is free to save it.
        engine.setDrillTarget(shooterId, shooter->position, false);
        if (hasKeeper) engine.setDrillTarget(keeperId, std::nullopt, false);
        eventsAtStrike = engine.getEvents().size();
      }
      else
      {
        lastBallVelocity = ball.velocity;
      }
    }
    if (!strike && outcome.empty() && now >= 10.0) outcome = "no shot";
    else if (strike && outcome.empty() && hasKeeper)
    {
      // Over the keeper: its height as it passes where he stood.
      if (strike->overKeeper < 0.0f && ball.position.x >= keeperAt.x)
        strike->overKeeper = MatchPitch::heightMetres(ball.z);
      const auto& events = engine.getEvents();
      for (std::size_t index = eventsAtStrike; index < events.size(); ++index)
        if (events[index].type == MatchEventType::SAVE) outcome = "saved";
      if (engine.getPossessionAttachment() && engine.getPossessionAttachment()->getId() == keeperId)
        outcome = "saved";
    }
    if (strike && outcome.empty())
    {
      // The last reading before it crosses the line: its pace there.
      if (ball.possessionState().isShot && ball.position.x < PITCH.atX(1.0f))
      {
        strike->lineSpeed = std::sqrt(ball.velocity.x * ball.velocity.x +
                                      ball.velocity.y * ball.velocity.y +
                                      ball.velocityZ * ball.velocityZ);
        strike->lineSeconds = now - strike->at;
      }
      if (ball.position.x >= PITCH.atX(1.0f) || engine.getState() != MatchState::PLAYING)
        outcome = result();
      else if (!ball.possessionState().isShot || now >= strike->at + 6.0)
        outcome = "short";
    }
    if (!outcome.empty() && !endAt) endAt = now;
    return !(endAt && now >= *endAt + 0.5);
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::path(renderer, draw, ballPath, IM_COL32(255, 255, 255, 120),
                    1.5f * scale);
    const PitchPoint aimAt = at(1.0f, 0.5f + aimAcross / WIDTH);
    DrillDraw::marker(renderer, draw, aimAt, 3.0f * scale,
                      IM_COL32(90, 220, 120, 255), "aim");
    if (strike)
      DrillDraw::marker(renderer, draw, at(1.0f, 0.5f + strike->crossingY / WIDTH),
                        3.0f * scale,
                        outcome == "on target" ? IM_COL32(90, 220, 120, 255)
                                               : IM_COL32(240, 140, 60, 255));
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    constexpr float DEGREES = 180.0f / PI;
    lines.push_back(std::format(
        "{:.1f} m out, {:+.1f} m off centre · aim {:+.2f} m across, {:.2f} m up "
        "· {:.0f}% of his power",
        line, side, aimAcross, aimHeight,
        100.0f * (powerShare > 0.0f ? powerShare
                                    : MatchTuning::Shooting::DEFAULT_POWER_SHARE)));
    lines.push_back(std::format("Shooter: Shooting {:.0f}, condition {:.0f}, {} foot",
                                shooting, condition,
                                footChoice < 0.5f ? "right" : "left"));
    if (feedChoice > 0.5f)
      lines.push_back(std::format(
          "Played in at {:.0f} m/s from {:+.0f} deg, {:.0f} m out{} · {}{}",
          ballSpeed, fromAngle, feedDistance,
          bounce > 0.0f ? std::format(", {:.1f} m/s up", bounce) : std::string(),
          feedChoice < 1.5f ? "first time" : "after a touch",
          strike ? std::format(": {} (in at {:.1f} m/s, turned {:.0f} deg)",
                               strike->firstTime ? "struck first time"
                                                 : "struck after a touch",
                               strike->incoming, strike->redirect)
                 : std::string()));
    else if (runUp > 0.0f)
      lines.push_back(std::format("Run-up: {:.0f} m at {:+.0f} deg to the goal, {}",
                                  runUp, runDirection,
                                  paceChoice < 0.5f   ? "jogging"
                                  : paceChoice < 1.5f ? "running"
                                                      : "sprinting"));
    if (orderedAt && strike)
      lines.push_back(std::format(
          "Body off the shot: {:.0f} deg at the order, {:.0f} deg at the strike "
          "({:.2f} s later); speed {:.1f} -> {:.1f} m/s",
          orderedAt->body, strike->bodyAngle, strike->at - orderedAt->at,
          orderedAt->speed, strike->shooterSpeed));
    if (strike)
    {
      lines.push_back(std::format(
          "{} at {:.2f} s: {:.1f} m/s ({:.1f} up), spin {:+.1f} side, {:+.1f} top "
          "rev/s{}",
          STRIKE_KIND_NAMES[static_cast<std::size_t>(strike->kind)], strike->at,
          strike->speed, strike->lift, strike->spin.side / (2.0f * PI),
          strike->spin.top / (2.0f * PI),
          strike->overKeeper >= 0.0f
              ? std::format(" · {:.2f} m up over the keeper", strike->overKeeper)
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
      if (strike->lineSpeed > 0.0f)
        lines.push_back(std::format("At the line: {:.1f} m/s after {:.2f} s",
                                    strike->lineSpeed, strike->lineSeconds));
    }
    if (!outcome.empty()) lines.push_back("OUTCOME  " + outcome);
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty() || !strike) return {};
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    constexpr float DEGREES = 180.0f / PI;
    std::vector<DrillMetric> out{
        {"Launch speed (m/s)", strike->speed},
        {"Launch lift (m/s)", strike->lift},
        {"Speed at the line (m/s)", strike->lineSpeed},
        {"Seconds to the line", strike->lineSeconds},
        {"On target", flag(outcome == "on target")},
        {"Wide", flag(outcome == "wide")},
        {"Over", flag(outcome == "over")},
        {"Short", flag(outcome == "short")},
        {"Miss across (m)", strike->crossingY - aimAcross},
        {"Abs miss across (m)", std::abs(strike->crossingY - aimAcross)},
        {"Miss height (m, + higher)", strike->crossingZ - aimHeight},
        {"Power asked (condition)", strike->factors[POWER_ASKED_FACTOR]},
        {"Saved", flag(outcome == "saved")},
        {"Height over the keeper (m)", strike->overKeeper},
        {"Sidespin (rev/s)", strike->spin.side / (2.0f * PI)},
        {"Topspin (rev/s, - backspin)", strike->spin.top / (2.0f * PI)},
        {"First time", flag(strike->firstTime)},
        {"Ball in (m/s)", strike->incoming},
        {"Turned (deg)", strike->redirect},
        {"Body off the shot at order (deg)", orderedAt ? orderedAt->body : 0.0f},
        {"Body off the shot at strike (deg)", strike->bodyAngle},
        {"Order to strike (s)", orderedAt ? strike->at - orderedAt->at : 0.0},
        {"Speed at order (m/s)", orderedAt ? orderedAt->speed : 0.0f},
        {"Speed lost (m/s)",
         orderedAt ? orderedAt->speed - strike->shooterSpeed : 0.0f},
        {"Angle difficulty", strike->angleDifficulty},
        {"Weight difficulty", strike->weightDifficulty},
        {"Angle failed", flag(strike->angleFailed)},
        {"Weight failed", flag(strike->weightFailed)},
        {"Abs angle error (deg)", strike->angleError * DEGREES},
        {"Weight error (m/s)", strike->weightError},
        {"Elevation error (deg, + higher)", strike->elevationError * DEGREES},
    };
    // Every condition's value (0-1).
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
      out.push_back({"Condition: " + std::string(CONTACT_FACTOR_LABELS[index]),
                     strike->factors[index]});
    return out;
  }

  void renderAfterSettings() const override
  {
    ImGui::SeparatorText("Difficulty of the last shot");
    if (!strike)
    {
      ImGui::TextDisabled("No shot yet.");
      return;
    }
    if (!ImGui::BeginTable("##difficulty", 2, ImGuiTableFlags_SizingStretchProp))
      return;
    const auto row = [](const char* name, float value, bool strong)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (strong)
        ImGui::TextUnformatted(name);
      else
        ImGui::TextDisabled("%s", name);
      ImGui::TableNextColumn();
      ImGui::Text("%.3f", static_cast<double>(value));
    };
    row("Angle difficulty", strike->angleDifficulty, true);
    row("Weight difficulty", strike->weightDifficulty, true);
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
      if (strike->factors[index] > 0.0005f)
        row(std::string(CONTACT_FACTOR_LABELS[index]).c_str(),
            strike->factors[index], false);
    ImGui::EndTable();
  }

 private:
  struct Strike
  {
    double at = 0.0;
    bool angleFailed = false;
    bool weightFailed = false;
    float angleError = 0.0f;
    float weightError = 0.0f;
    float elevationError = 0.0f;
    float angleDifficulty = 0.0f;
    float weightDifficulty = 0.0f;
    std::array<float, CONTACT_FACTOR_COUNT> factors{};
    /** Off his foot: along the ground and upward (m/s). */
    float speed = 0.0f;
    float lift = 0.0f;
    /** Where its flight crosses the goal line (m from the goal's middle,
     * and height), as the engine launched it. */
    float crossingY = 0.0f;
    float crossingZ = 0.0f;
    /** Its pace (m/s, in 3D) at the last step before the line, and when. */
    float lineSpeed = 0.0f;
    double lineSeconds = 0.0;
    /** Where it went against where his body faced (deg), and his speed. */
    float bodyAngle = 0.0f;
    float shooterSpeed = 0.0f;
    /** A ball played to him: struck first time (no touch), the pace it came
     * in at (m/s) and how far it was turned (deg). */
    bool firstTime = false;
    float incoming = 0.0f;
    float redirect = 0.0f;
    /** The kind it was struck as, and the spin it left with (rad/s). */
    StrikeKind kind = StrikeKind::SHOT;
    BallSpin spin{};
    /** Its height as it passed the keeper (m; -1 not yet). */
    float overKeeper = -1.0f;
  };
  /** When the shot was ordered: his body against its line (deg), his
   * speed. */
  struct Moment
  {
    double at = 0.0;
    float body = 0.0f;
    float speed = 0.0f;
  };

  /** From the ball to the spot he aims at (metres). */
  [[nodiscard]] Vector2F lineToAim(PitchPoint ballAt) const
  {
    return {PITCH.metresX(PITCH.mirrorX(ballAt.x)),
            PITCH.metresY(PITCH.atY(0.5f + aimAcross / WIDTH) - ballAt.y)};
  }
  /** His body's angle (deg) to a direction (metres). */
  static float bodyAngleTo(const MatchPlayer& player, Vector2F line)
  {
    const PitchPoint body{
        std::cos(player.facingAngle) * MatchTuning::Pitch::MATCH_LENGTH_METRES,
        std::sin(player.facingAngle) * MatchTuning::Pitch::MATCH_WIDTH_METRES};
    const float lengths = std::hypot(body.x, body.y) * std::hypot(line.x, line.y);
    if (lengths <= 0.0f) return 0.0f;
    return std::acos(std::clamp((body.x * line.x + body.y * line.y) / lengths,
                                -1.0f, 1.0f)) *
           180.0f / PI;
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
      if (engine.getStrikeContacts().byKind[at].strikes > before.byKind[at].strikes)
        index = at;
    }
    if (index == STRIKE_KIND_COUNT) return;
    const auto& a = before.byKind[index];
    const auto& b = engine.getStrikeContacts().byKind[index];
    const MatchBall& ball = engine.getBall();
    Strike reading;
    reading.at = now;
    reading.kind = static_cast<StrikeKind>(index);
    reading.spin = ball.spin;
    reading.angleFailed = b.angleFailed > a.angleFailed;
    reading.weightFailed = b.weightFailed > a.weightFailed;
    reading.angleError = static_cast<float>(b.angleErrorSum - a.angleErrorSum);
    reading.weightError = static_cast<float>(b.weightErrorSum - a.weightErrorSum);
    reading.elevationError =
        static_cast<float>(b.elevationErrorSum - a.elevationErrorSum);
    reading.angleDifficulty = static_cast<float>(b.angleDifficulty - a.angleDifficulty);
    reading.weightDifficulty =
        static_cast<float>(b.weightDifficulty - a.weightDifficulty);
    for (std::size_t factor = 0; factor < CONTACT_FACTOR_COUNT; ++factor)
      reading.factors[factor] =
          static_cast<float>(b.factorSum[factor] - a.factorSum[factor]);
    reading.speed = std::hypot(ball.velocity.x, ball.velocity.y);
    reading.lift = ball.velocityZ;
    reading.crossingY = PITCH.metresY(ball.possessionState().shotTargetY - PITCH.atY(0.5f));
    reading.crossingZ = ball.possessionState().shotTargetHeightMetres;
    strike = reading;
  }

  /** Where it crossed the line, against the frame. */
  [[nodiscard]] std::string result() const
  {
    if (std::abs(strike->crossingY) >= GOAL_HALF_WIDTH - BALL_RADIUS) return "wide";
    if (strike->crossingZ >= CROSSBAR - BALL_RADIUS) return "over";
    return "on target";
  }

  float line = 18.0f;
  float side = 0.0f;
  float aimAcross = 2.5f;
  float aimHeight = 0.5f;
  float powerShare = 0.0f;
  float techniqueChoice = 0.0f;
  float keeperChoice = 0.0f;
  float keeperOff = 6.0f;
  float keeperHeight = 188.0f;
  float goalkeeping = 70.0f;
  float feedChoice = 0.0f;
  float ballSpeed = 12.0f;
  float fromAngle = 90.0f;
  float feedDistance = 20.0f;
  float bounce = 0.0f;
  float runUp = 0.0f;
  float runDirection = 0.0f;
  float paceChoice = 1.0f;
  float shooting = 70.0f;
  float dribbling = 70.0f;
  float condition = 100.0f;
  float footChoice = 0.0f;
  StrikeKnobs knobs;

  PlayerID shooterId = 0;
  PitchPoint spot{};
  PitchPoint start{};
  PitchPoint feedFrom{};
  Vector2F lastBallVelocity{};
  bool touchedFirst = false;
  bool hasKeeper = false;
  PlayerID keeperId = 0;
  PitchPoint keeperAt{};
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

// --- Header target -------------------------------------------------------------

/**
 * One high ball served to a lone player, who heads it where the drill says
 * (MatchEngine::forceHeader): on to a spot on the ground, at a spot in the
 * goal, or cushioned and kept. Nobody else on the pitch: the unchallenged
 * header of the heading technique plan (Notes and plans/
 * match_engine_heading_technique_plan.md, step 2).
 */
class HeaderTargetDrill final : public Drill
{
 public:
  /** Where he stands for a header to a spot or a cushion (pitch). */
  static inline const PitchPoint MIDDLE = at(0.5f, 0.5f);
  /** No header this long after the feed: the run ends. */
  static constexpr double TIME_LIMIT = 8.0;
  /** After the header: the run ends this long after it at most. */
  static constexpr double FLIGHT_LIMIT = 6.0;
  static constexpr double LINGER_SECONDS = 0.4;
  static constexpr float GOAL_HALF_WIDTH = MatchTuning::Shooting::GOAL_HALF_WIDTH_METRES;
  static constexpr float BALL_RADIUS = MatchTuning::Units::BALL_RADIUS_METRES;
  static constexpr float CROSSBAR = MatchTuning::Units::CROSSBAR_HEIGHT_METRES;
  static constexpr float Z_METRES = MatchTuning::Units::BALL_Z_METRES;

  const char* name() const override { return "Header target"; }
  const char* summary() const override
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
    add(setting("Ball speed (m/s)", &ballSpeed, 2.0f, 35.0f, false,
                "Its pace over the ground when it is played in. Slow over a "
                "long way drops steeply onto him; fast is a driven ball."),
        "Ball");
    add(setting("Comes from (deg)", &fromAngle, -180.0f, 180.0f, true,
                "Where it is played from, against the way he faces (up the "
                "pitch): 0 in front of him, 90 from his right, -90 from his "
                "left, 180 from behind."),
        "Ball");
    add(setting("Feed distance (m)", &feedDistance, 3.0f, 60.0f, false), "Ball");
    add(setting("Arrival height (m)", &arrivalHeight, 0.5f, 3.5f, false,
                "How high it is as it passes over the spot where he stands."),
        "Ball");
    add(setting("Passes him by (m)", &passesBy, -1.5f, 1.5f, false,
                "Sideways offset of its line from where he stands: 0 "
                "straight at him."),
        "Ball");

    DrillParameter aim = setting("Header", &aimChoice, 0.0f, 2.0f, true,
                                 "To a spot: headed on to a spot on the "
                                 "ground.\nAt goal: a headed shot at a spot "
                                 "in an empty goal.\nKeep it: cushioned "
                                 "down and kept.");
    aim.choices = {"To a spot", "At goal", "Keep it"};
    add(aim, "Header");
    DrillParameter early = setting("Knows it early", &knownEarly, 0.0f, 1.0f, true,
                                   "Yes: he knows the header before the ball "
                                   "comes and turns his body to it. No: he "
                                   "decides only as he meets it.");
    early.choices = {"No", "Yes"};
    add(early, "Header");
    add(setting("Length (m)", &length, 2.0f, 40.0f, false,
                "To a spot: from where he stands to the spot."),
        "Header");
    add(setting("Direction (deg)", &direction, -180.0f, 180.0f, true,
                "To a spot: where it is against the way he faces (up the "
                "pitch): 0 straight ahead, 90 to his right, -90 to his left, "
                "180 behind him."),
        "Header");
    add(setting("Out from goal line (m)", &outFromGoal, 3.0f, 25.0f, false,
                "At goal: where he stands, facing the goal."),
        "Header");
    add(setting("Off centre (m)", &offCentre, -15.0f, 15.0f, false,
                "At goal: + to his right of the middle of the goal."),
        "Header");
    add(setting("Aim across (m)", &aimAcross, -3.6f, 3.6f, false,
                "At goal: the spot, + to his right of the middle."),
        "Header");
    add(setting("Aim height (m)", &aimHeight, 0.0f, 2.4f, false,
                "At goal: the spot's height."),
        "Header");

    add(setting("Runs in from (m)", &runIn, 0.0f, 20.0f, false,
                "0: he stands on his spot. Otherwise he starts this far "
                "behind it and runs up the pitch through it, flat out."),
        "Player");
    add(setting("Run starts after (s)", &runDelay, 0.0f, 4.0f, false,
                "Running in: how long after the ball is played he sets off."),
        "Player");
    add(setting("Heading", &heading, 1.0f, 100.0f, true,
                "His Heading (overrides the derived stat): his header power, "
                "and how accurately he places it."),
        "Player");
    add(setting("Height (cm)", &heightCm, 150.0f, 210.0f, true,
                "Part of his Jumping reach (his height plus his jump)."),
        "Player");
    add(setting("Physicality", &physicality, 1.0f, 100.0f, true,
                "With Pace, his jump (Jumping reach)."),
        "Player");
    add(setting("Pace", &pace, 1.0f, 100.0f, true,
                "With Physicality, his jump (Jumping reach)."),
        "Player");
    add(setting("Shooting", &shooting, 1.0f, 100.0f, true,
                "With Defending, the stand-in for Heading when the Heading "
                "setting isn't used. The header doesn't read it."),
        "Player");

    // Names live as long as the drill: the settings point at them.
    const auto kindName = STRIKE_KIND_NAMES[static_cast<std::size_t>(StrikeKind::HEADER)];
    for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
    {
      knobNames[index] =
          strikeKnobName(StrikeKind::HEADER, static_cast<StrikeKnob>(index));
      knobSections[index] = std::string(kindName) + " knobs: " +
                            std::string(STRIKE_KNOBS[index].group);
    }
    for (std::size_t index = 0; index < STRIKE_KNOB_COUNT; ++index)
    {
      const StrikeKnobInfo& info = STRIKE_KNOBS[index];
      DrillParameter knob =
          setting(knobNames[index].c_str(),
                  &knobs.at(StrikeKind::HEADER, static_cast<StrikeKnob>(index)),
                  info.min, info.max, false);
      knob.collapsible = true;
      knob.label = info.name.data();
      add(knob, knobSections[index].c_str());
    }
    return list;
  }

  std::vector<DrillPreset> presets() const override
  {
    const std::map<std::string, float> base{
        {"Ball speed (m/s)", 6.0f},  {"Comes from (deg)", 0.0f},
        {"Feed distance (m)", 12.0f}, {"Arrival height (m)", 1.9f},
        {"Passes him by (m)", 0.0f}, {"Header", 0.0f},
        {"Length (m)", 10.0f},       {"Direction (deg)", 0.0f}};
    const auto with = [&base](std::map<std::string, float> changes)
    {
      std::map<std::string, float> settings = base;
      for (const auto& [name, value] : changes) settings[name] = value;
      return settings;
    };
    return {
        {"Dropping ball, headed ahead",
         "A ball dropping steeply onto him, headed 10 m ahead.", base},
        {"Long ball headed back",
         "18 m/s from 35 m in front, headed back 20 m the way it came.",
         with({{"Ball speed (m/s)", 18.0f},
               {"Feed distance (m)", 35.0f},
               {"Arrival height (m)", 2.0f},
               {"Length (m)", 20.0f}})},
        {"Cross turned 90 deg",
         "18 m/s from 25 m to his right, headed 12 m straight ahead.",
         with({{"Ball speed (m/s)", 18.0f},
               {"Comes from (deg)", 90.0f},
               {"Feed distance (m)", 25.0f},
               {"Arrival height (m)", 2.0f},
               {"Length (m)", 12.0f}})},
        {"Glanced on",
         "16 m/s from 25 m behind him, glanced on 15 m.",
         with({{"Ball speed (m/s)", 16.0f},
               {"Comes from (deg)", 180.0f},
               {"Feed distance (m)", 25.0f},
               {"Arrival height (m)", 2.1f},
               {"Length (m)", 15.0f}})},
        {"Cross headed at goal",
         "18 m/s from 25 m to his right, 10 m out, at the far side low.",
         with({{"Ball speed (m/s)", 18.0f},
               {"Comes from (deg)", 90.0f},
               {"Feed distance (m)", 25.0f},
               {"Arrival height (m)", 2.0f},
               {"Header", 1.0f},
               {"Out from goal line (m)", 10.0f},
               {"Off centre (m)", 0.0f},
               {"Aim across (m)", -2.5f},
               {"Aim height (m)", 0.5f}})},
        {"Cushion a dropping ball", "A ball dropping onto him, kept.",
         with({{"Header", 2.0f}})},
    };
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    return {{"Header",
             PlayerRole::ST,
             true,
             {{"Physicality", physicality}, {"Pace", pace}, {"Shooting", shooting}},
             static_cast<int>(heightCm),
             {{DerivedStat::HEADING, heading}}}};
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    playerId = ids[0];
    engine.setStrikeKnobs(knobs);

    // Geometry in metres: he faces up the pitch (+x), towards the goal for
    // a header at it; the ball comes from fromAngle round from in front of
    // him, along a line passesBy to the side of where he stands.
    const Vector2F up{1.0f, 0.0f};
    stand = aim() == ForcedHeaderAim::GOAL
                ? at(1.0f - outFromGoal / LENGTH, 0.5f + offCentre / WIDTH)
                : MIDDLE;
    from = rotated(up, fromAngle * PI / 180.0f);
    travel = Vector2F{-from.x, -from.y};
    side = rotated(travel, PI * 0.5f);
    feedFrom = along(along(stand, side, passesBy), from, feedDistance);
    feedFrom = PitchPoint{std::clamp(feedFrom.x, PITCH.atX(0.005f), PITCH.atX(0.995f)),
                std::clamp(feedFrom.y, PITCH.atY(0.005f), PITCH.atY(0.995f))};
    target = along(stand, rotated(up, direction * PI / 180.0f), length);
    target = PitchPoint{std::clamp(target.x, PITCH.atX(0.01f), PITCH.atX(0.99f)), std::clamp(target.y, PITCH.atY(0.01f), PITCH.atY(0.99f))};

    // Running in: from behind his spot, through it.
    start = along(stand, up, -runIn);
    start = PitchPoint{std::clamp(start.x, PITCH.atX(0.01f), PITCH.atX(0.99f)), start.y};
    running = false;
    MatchScenario scenario;
    scenario.players.push_back({playerId, start, false});
    scenario.carrierId = 0;
    scenario.ballPosition = feedFrom;
    engine.startDrill(scenario);
    // He holds his spot (or his start), facing up the pitch.
    engine.setDrillTarget(playerId, start, false, {}, 0.0f, 0.0f,
                          facingAngleOf(0.0f));
    if (aim() == ForcedHeaderAim::KEEP) engine.keepBall(playerId, true);
    engine.forceHeader(playerId, aim(),
                       aim() == ForcedHeaderAim::SPOT ? std::optional<PitchPoint>(target)
                                                      : std::nullopt,
                       aim() == ForcedHeaderAim::GOAL
                           ? std::optional<Vector2F>(Vector2F{aimAcross, aimHeight})
                           : std::nullopt,
                       knownEarly >= 0.5f);
    const float feedMetres = metresBetween(feedFrom, along(stand, side, passesBy));
    const float lift = liftFor(engine, feedMetres, ballSpeed, arrivalHeight);
    engine.feedBall(feedFrom, {travel.x * ballSpeed, travel.y * ballSpeed}, lift,
                    playerId);
    fedAt = secondsOf(engine);
    before = engine.getStrikeContacts();
    if (const MatchPlayer* player = slotOf(engine, playerId))
    {
      jumpingReach = derivedPhysicalValue(
          DerivedStat::JUMPING_REACH,
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
    peakAfter = 0.0f;
    ballPath = {feedFrom};
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    const MatchPlayer* player = slotOf(engine, playerId);
    if (engine.getSimulatedSteps() % 2 == 0) ballPath.push_back(ball.position);
    const bool his = engine.getPossessionAttachment() && engine.getPossessionAttachment()->getId() == playerId;
    if (runIn > 0.0f && !running && now >= fedAt + runDelay)
    {
      // Off he goes, flat out up the pitch through his spot.
      running = true;
      const PitchPoint beyond = along(stand, {1.0f, 0.0f}, 15.0f);
      engine.setDrillTarget(playerId, PitchPoint{std::min(beyond.x, PITCH.atX(0.99f)), beyond.y},
                            true, {}, 0.0f, 0.0f, facingAngleOf(0.0f), true);
    }
    if (player && player->airborne() && !tookOff)
    {
      tookOff = now;
      tookOffRunning = player->runningJump;
    }

    if (!header && !cushioned && !touched && outcome.empty())
    {
      readHeader(engine, now);
      if (header)
      {
        if (aim() == ForcedHeaderAim::SPOT) flight.start(ball, target);
      }
      else if (his)
      {
        // On his feet without a header: cushioned down (today's cushion has
        // no contact, but it was met with his head), or a touch (the ball
        // came to him below his head's reach).
        if (engine.getLastHeadPart())
          cushioned = true;
        else
          touched = true;
        meet = last;
        meetPlayer = lastPlayer;
        outcome = cushioned ? "cushioned and kept" : "controlled (a touch, not a header)";
      }
      else if (now >= fedAt + TIME_LIMIT ||
               (last.height > 0.01f && ball.z <= 0.0f && ball.velocityZ <= 0.0f &&
                metresBetween(ball.position, stand) > 1.5f))
      {
        outcome = "not headed (out of reach)";
      }
    }
    else if (header && outcome.empty())
    {
      peakAfter = std::max(peakAfter, MatchPitch::heightMetres(ball.z));
      if (aim() == ForcedHeaderAim::SPOT)
      {
        flight.observe(ball);
        if (flight.settled(StrikeKind::HEADER, ball))
          outcome = "came down";
      }
      else if (aim() == ForcedHeaderAim::GOAL)
      {
        outcome = header->shot ? goalResult() : "not a shot";
      }
      else
      {
        // A cushioned header: where it first comes down, then kept once he
        // has collected it (he owns it from the header), or not.
        if (!dropped && ball.z <= 0.0f)
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
          outcome = "cushioned, not kept";
      }
      if (outcome.empty() && engine.getState() != MatchState::PLAYING)
        outcome = "out of play";
      if (outcome.empty() && now >= header->at + FLIGHT_LIMIT) outcome = "time up";
    }
    last = stateOf(ball);
    if (player) lastPlayer = player->position;
    if (!outcome.empty() && !endAt) endAt = now;
    return !(endAt && now >= *endAt + LINGER_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::line(renderer, draw, feedFrom, stand, IM_COL32(255, 255, 255, 70),
                    1.0f * scale);
    DrillDraw::marker(renderer, draw, feedFrom, 3.0f * scale,
                      IM_COL32(255, 255, 255, 200), "feed");
    DrillDraw::marker(renderer, draw, stand, 2.5f * scale,
                      IM_COL32(90, 220, 120, 220), "header");
    if (aim() == ForcedHeaderAim::SPOT)
    {
      DrillDraw::line(renderer, draw, stand, target, IM_COL32(255, 255, 255, 70),
                      1.0f * scale);
      DrillDraw::circle(renderer, draw, target,
                        passTolerance(metresBetween(stand, target)),
                        IM_COL32(90, 170, 255, 60), IM_COL32(90, 170, 255, 200),
                        1.0f * scale);
      if (flight.landing)
        DrillDraw::marker(renderer, draw, *flight.landing, 3.0f * scale,
                          IM_COL32(255, 230, 120, 255),
                          std::format("{:.1f} m", flight.landingMiss).c_str());
    }
    else if (aim() == ForcedHeaderAim::GOAL)
    {
      DrillDraw::marker(renderer, draw, at(1.0f, 0.5f + aimAcross / WIDTH),
                        3.0f * scale, IM_COL32(90, 170, 255, 255), "aim");
      if (header && header->shot)
        DrillDraw::marker(renderer, draw, at(1.0f, 0.5f + header->crossingY / WIDTH),
                          3.0f * scale, IM_COL32(255, 230, 120, 255),
                          outcome.empty() ? nullptr : outcome.c_str());
    }
    DrillDraw::path(renderer, draw, ballPath, IM_COL32(255, 230, 120, 160),
                    1.5f * scale);
  }

  std::vector<std::string> report() const override
  {
    static constexpr std::array<const char*, 3> AIMS = {"to a spot", "at goal",
                                                         "kept"};
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.0f} m/s from {:+.0f} deg, {:.0f} m out, over him at {:.2f} m{} · "
        "headed {}{}",
        ballSpeed, fromAngle, feedDistance, arrivalHeight,
        passesBy != 0.0f ? std::format(", {:+.1f} m by", passesBy) : std::string(),
        AIMS[static_cast<std::size_t>(aimChoice)],
        aim() == ForcedHeaderAim::SPOT
            ? std::format(" {:.0f} m at {:+.0f} deg", length, direction)
        : aim() == ForcedHeaderAim::GOAL
            ? std::format(" from {:.0f} m, aim {:+.1f} m across, {:.1f} m up",
                          outFromGoal, aimAcross, aimHeight)
            : std::string()));
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
          at.height, at.height - heightCm / 100.0f, at.height - jumpingReach,
          metresBetween(who, at.position), paceOf(at)));
    }
    if (header)
    {
      constexpr float DEGREES = 180.0f / PI;
      lines.push_back(std::format(
          "{} ({}) at {:.2f} s: off at {:.1f} m/s ({:.1f} along, {:+.1f} up, "
          "{:+.0f} deg), turned {:.0f} deg",
          header->shot ? "Headed shot" : "Header",
          header->topOfHead ? "top of the head" : "forehead", header->at - fedAt,
          std::hypot(header->speed, header->lift), header->speed, header->lift,
          std::atan2(header->lift, header->speed) * DEGREES, header->turned));
      lines.push_back(std::format(
          "Difficulty angle {:.2f}, weight {:.2f} · angle {}{:.1f} deg "
          "(elevation {:+.1f} deg), weight {}{:+.2f} m/s",
          header->angleDifficulty, header->weightDifficulty,
          header->angleFailed ? "FAILED " : "", header->angleError * DEGREES,
          header->elevationError * DEGREES, header->weightFailed ? "FAILED " : "",
          header->weightError));
      if (aim() == ForcedHeaderAim::SPOT && flight.landing)
      {
        const Vector2F offset = lineOffset(*flight.landing);
        lines.push_back(std::format(
            "Came down {:.2f} m from the spot ({:+.2f} m long, {:+.2f} m "
            "right; within {:.1f} m counts)",
            flight.landingMiss, offset.x, offset.y,
            passTolerance(flight.distance)));
      }
      if (aim() == ForcedHeaderAim::GOAL && header->shot)
        lines.push_back(std::format(
            "Crossed the line {:+.2f} m across, {:.2f} m up (missed the spot "
            "by {:+.2f} across, {:+.2f} up)",
            header->crossingY, header->crossingZ, header->crossingY - aimAcross,
            header->crossingZ - aimHeight));
    }
    if (!outcome.empty()) lines.push_back("OUTCOME  " + outcome);
    return lines;
  }

  /** How hard the last header was, below the settings. */
  void renderAfterSettings() const override
  {
    ImGui::SeparatorText("Difficulty of the last header");
    if (!header)
    {
      ImGui::TextDisabled("No header yet.");
      return;
    }
    if (!ImGui::BeginTable("##difficulty", 2, ImGuiTableFlags_SizingStretchProp))
      return;
    const auto row = [](const char* name, float value, bool strong)
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (strong)
        ImGui::TextUnformatted(name);
      else
        ImGui::TextDisabled("%s", name);
      ImGui::TableNextColumn();
      ImGui::Text("%.3f", static_cast<double>(value));
    };
    row("Angle difficulty", header->angleDifficulty, true);
    row("Weight difficulty", header->weightDifficulty, true);
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
      if (header->factors[index] > 0.0005f)
        row(std::string(CONTACT_FACTOR_LABELS[index]).c_str(), header->factors[index],
            false);
    ImGui::EndTable();
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty()) return {};
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{{"Headed", flag(header.has_value() || cushioned)},
                                 {"Jumping reach (m)", jumpingReach},
                                 {"Heading rating", headingRating * 100.0f}};
    if (aim() == ForcedHeaderAim::KEEP)
      out.push_back({"Kept", flag(outcome == "cushioned and kept")});
    if (!header && !cushioned) return out;
    const State& at = header ? header->meet : meet;
    const PitchPoint who = header ? header->meetPlayer : meetPlayer;
    out.push_back({"Meet height (m)", at.height});
    out.push_back({"Meet over standing head (m)", at.height - heightCm / 100.0f});
    out.push_back({"Meet under Jumping reach (m)", jumpingReach - at.height});
    out.push_back({"Meet gap (m)", metresBetween(who, at.position)});
    out.push_back({"Pace in (m/s)", paceOf(at)});
    if (!header) return out;
    constexpr float DEGREES = 180.0f / PI;
    out.push_back({"Top of head", flag(header->topOfHead)});
    out.push_back({"Body off its line (deg)", header->bodyOff});
    out.push_back({"Jumped", flag(tookOff.has_value())});
    out.push_back({"Running jump", flag(tookOff && tookOffRunning)});
    out.push_back({"Feet at meet (m)", header->feet});
    out.push_back({"Speed at meet (m/s)", header->runSpeed});
    if (tookOff) out.push_back({"Take-off before meet (s)", header->at - *tookOff});
    out.push_back({"Feed to header (s)", header->at - fedAt});
    out.push_back({"Pace off (m/s)", std::hypot(header->speed, header->lift)});
    out.push_back({"Launch speed (m/s)", header->speed});
    out.push_back({"Launch lift (m/s)", header->lift});
    out.push_back({"Elevation off (deg)", std::atan2(header->lift, header->speed) * DEGREES});
    out.push_back({"Turned (deg)", header->turned});
    out.push_back({"Peak after (m)", peakAfter});
    out.push_back({"Angle difficulty", header->angleDifficulty});
    out.push_back({"Weight difficulty", header->weightDifficulty});
    out.push_back({"Angle failed", flag(header->angleFailed)});
    out.push_back({"Weight failed", flag(header->weightFailed)});
    out.push_back({"Abs angle error (deg)", header->angleError * DEGREES});
    out.push_back({"Weight error (m/s)", header->weightError});
    out.push_back(
        {"Elevation error (deg, + higher)", header->elevationError * DEGREES});
    for (std::size_t index = 0; index < CONTACT_FACTOR_COUNT; ++index)
      out.push_back({"Condition: " + std::string(CONTACT_FACTOR_LABELS[index]),
                     header->factors[index]});
    if (aim() == ForcedHeaderAim::SPOT)
    {
      flight.addMetrics(StrikeKind::HEADER, out);
      if (flight.landing)
      {
        const Vector2F offset = lineOffset(*flight.landing);
        out.push_back({"Long (m, - short)", offset.x});
        out.push_back({"Right (m, - left)", offset.y});
      }
    }
    if (aim() == ForcedHeaderAim::KEEP && dropped)
    {
      out.push_back({"Drops from him (m)", metresBetween(header->meetPlayer, *dropped)});
      out.push_back({"Drops after (s)", droppedAt - header->at});
    }
    if (aim() == ForcedHeaderAim::KEEP && collectedAt)
      out.push_back({"Collected after (s)", *collectedAt - header->at});
    if (aim() == ForcedHeaderAim::GOAL && header->shot)
    {
      const std::string where = goalResult();
      out.push_back({"On target", flag(where == "on target")});
      out.push_back({"Wide", flag(where == "wide")});
      out.push_back({"Over", flag(where == "over")});
      out.push_back({"Miss across (m)", header->crossingY - aimAcross});
      out.push_back({"Miss height (m, + higher)", header->crossingZ - aimHeight});
    }
    return out;
  }

 private:
  /** The ball at the end of a step (pitch position, m/s, metres up). */
  struct State
  {
    PitchPoint position{};
    Vector2F velocity{};
    float height = 0.0f;
    float lift = 0.0f;
  };
  static State stateOf(const MatchBall& ball)
  {
    return {ball.position, ball.velocity, MatchPitch::heightMetres(ball.z), ball.velocityZ};
  }
  static float paceOf(const State& state)
  {
    return std::hypot(std::hypot(state.velocity.x, state.velocity.y), state.lift);
  }

  struct Header
  {
    double at = 0.0;
    bool shot = false;
    bool angleFailed = false;
    bool weightFailed = false;
    /** Rad (unsigned), m/s (+ harder), rad (+ higher). */
    float angleError = 0.0f;
    float weightError = 0.0f;
    float elevationError = 0.0f;
    float angleDifficulty = 0.0f;
    float weightDifficulty = 0.0f;
    std::array<float, CONTACT_FACTOR_COUNT> factors{};
    /** Off his head: along the ground and upward (m/s). */
    float speed = 0.0f;
    float lift = 0.0f;
    /** Its line seen from above, turned off the line it came on (deg). */
    float turned = 0.0f;
    /** Where he met it (where it was sent off, at its height then), its
     * pace a step before, and where he was. */
    State meet;
    PitchPoint meetPlayer{};
    /** Met with the top of his head (else the forehead). */
    bool topOfHead = false;
    /** His body against the line it left his head on (deg). */
    float bodyOff = 0.0f;
    /** His feet's height (m) and his speed (m/s) as he headed it. */
    float feet = 0.0f;
    float runSpeed = 0.0f;
    /** A headed shot: where it crosses the line (m across, m up). */
    float crossingY = 0.0f;
    float crossingZ = 0.0f;
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
  static float liftFor(const MatchEngine& engine, float metres, float speed,
                       float height)
  {
    constexpr float DT = STEP / static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
    const auto heightAt = [&](float lift)
    {
      FlightState flight{speed, lift, 0.0f, false, {}};
      float gone = 0.0f;
      for (float time = 0.0f; time < 20.0f; time += DT)
      {
        const float was = flight.height;
        const float step = engine.stepFlight(flight, DT);
        if (flight.bounced) return -1.0f;
        if (gone + step >= metres)
        {
          const float share = (metres - gone) / std::max(step, 1e-6f);
          return was + (flight.height - was) * share;
        }
        gone += step;
        if (flight.horizontalSpeed <= 0.0f) return -1.0f;
      }
      return -1.0f;
    };
    float low = 0.0f;
    float high = 40.0f;
    for (int round = 0; round < 40; ++round)
    {
      const float middle = 0.5f * (low + high);
      (heightAt(middle) < height ? low : high) = middle;
    }
    return 0.5f * (low + high);
  }

 private:
  /** The header: a header strike the contact model has read since the
   * feed. */
  void readHeader(const MatchEngine& engine, double now)
  {
    const auto& a = before.byKind[static_cast<std::size_t>(StrikeKind::HEADER)];
    const auto& b =
        engine.getStrikeContacts().byKind[static_cast<std::size_t>(StrikeKind::HEADER)];
    if (b.strikes == a.strikes) return;
    const MatchBall& ball = engine.getBall();
    Header reading;
    reading.at = now;
    reading.shot = ball.possessionState().isShot;
    reading.angleFailed = b.angleFailed > a.angleFailed;
    reading.weightFailed = b.weightFailed > a.weightFailed;
    reading.angleError = static_cast<float>(b.angleErrorSum - a.angleErrorSum);
    reading.weightError = static_cast<float>(b.weightErrorSum - a.weightErrorSum);
    reading.elevationError =
        static_cast<float>(b.elevationErrorSum - a.elevationErrorSum);
    reading.angleDifficulty = static_cast<float>(b.angleDifficulty - a.angleDifficulty);
    reading.weightDifficulty =
        static_cast<float>(b.weightDifficulty - a.weightDifficulty);
    for (std::size_t factor = 0; factor < CONTACT_FACTOR_COUNT; ++factor)
      reading.factors[factor] =
          static_cast<float>(b.factorSum[factor] - a.factorSum[factor]);
    reading.speed = std::hypot(ball.velocity.x, ball.velocity.y);
    reading.lift = ball.velocityZ;
    const float incoming = std::hypot(last.velocity.x, last.velocity.y);
    if (incoming > 0.1f && reading.speed > 0.1f)
      reading.turned = turnDegrees(
          {last.velocity.x / incoming, last.velocity.y / incoming},
          {ball.velocity.x / reading.speed, ball.velocity.y / reading.speed});
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
        player && reading.speed > 0.1f)
    {
      const PitchPoint body{std::cos(player->facingAngle) * LENGTH,
                          std::sin(player->facingAngle) * WIDTH};
      const float bodyLength = std::hypot(body.x, body.y);
      reading.bodyOff = turnDegrees(
          {body.x / bodyLength, body.y / bodyLength},
          {ball.velocity.x / reading.speed, ball.velocity.y / reading.speed});
    }
    reading.crossingY = PITCH.metresY(ball.possessionState().shotTargetY - PITCH.atY(0.5f));
    reading.crossingZ = ball.possessionState().shotTargetHeightMetres;
    header = reading;
  }

  /** A headed shot: where it crossed the line, against the frame. */
  [[nodiscard]] std::string goalResult() const
  {
    if (std::abs(header->crossingY) >= GOAL_HALF_WIDTH - BALL_RADIUS) return "wide";
    if (header->crossingZ >= CROSSBAR - BALL_RADIUS) return "over";
    return "on target";
  }

  /** A point against the line from where he stands to the spot (m): along
   * it beyond the spot (+ long) and across it (+ to his right). */
  [[nodiscard]] Vector2F lineOffset(PitchPoint point) const
  {
    const Vector2F way = directionBetween(stand, target);
    const Vector2F offset = toMetres(point - target);
    return Vector2F{offset.x * way.x + offset.y * way.y, offset.y * way.x - offset.x * way.y};
  }

  /** The strike tolerance at this distance (the technique bench's rule). */
  [[nodiscard]] static float passTolerance(float metres)
  {
    const float share = std::clamp((metres - 10.0f) / 50.0f, 0.0f, 1.0f);
    return 1.0f + 2.0f * share;
  }

  // Ball
  float ballSpeed = 6.0f;
  float fromAngle = 0.0f;
  float feedDistance = 12.0f;
  float arrivalHeight = 1.9f;
  float passesBy = 0.0f;
  // Header
  float aimChoice = 0.0f;
  float knownEarly = 1.0f;
  float runIn = 0.0f;
  float runDelay = 0.0f;
  /** Running in: where he starts, and whether he has set off. */
  PitchPoint start{};
  bool running = false;
  /** When he took off, and whether with his running jump. */
  std::optional<double> tookOff;
  bool tookOffRunning = false;
  float length = 10.0f;
  float direction = 0.0f;
  float outFromGoal = 10.0f;
  float offCentre = 0.0f;
  float aimAcross = -2.5f;
  float aimHeight = 0.5f;
  // Player
  float heading = 70.0f;
  float heightCm = 180.0f;
  float physicality = 70.0f;
  float pace = 70.0f;
  float shooting = 70.0f;
  StrikeKnobs knobs;

  PlayerID playerId = 0;
  PitchPoint stand{};
  Vector2F from{-1.0f, 0.0f};
  Vector2F travel{1.0f, 0.0f};
  Vector2F side{0.0f, 1.0f};
  PitchPoint feedFrom{};
  PitchPoint target{};
  double fedAt = 0.0;
  float jumpingReach = 0.0f;
  float headingRating = 0.0f;
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
  PitchPoint meetPlayer{};
  State last;
  PitchPoint lastPlayer{};
  float peakAfter = 0.0f;
  PassFlight flight;
  std::string outcome;
  std::optional<double> endAt;
  std::vector<PitchPoint> ballPath;
  std::array<std::string, STRIKE_KNOB_COUNT> knobNames;
  std::array<std::string, STRIKE_KNOB_COUNT> knobSections;
};

// --- Chest control, Thigh control ----------------------------------------------

/**
 * One ball served at chest (or thigh) height to a lone player standing on
 * his spot, who takes it on his chest (thigh) (a Binturong body) and keeps
 * it. Nobody else on the pitch
 * (Notes and plans/match_engine_chest_thigh_control_plan.md, steps 3-4).
 */
class BodyControlDrill final : public Drill
{
 public:
  static inline const PitchPoint MIDDLE = at(0.5f, 0.5f);
  /** No touch this long after the feed: the run ends. */
  static constexpr double TIME_LIMIT = 8.0;
  /** After the touch: the run ends this long after it at most. */
  static constexpr double KEEP_LIMIT = 5.0;
  static constexpr double LINGER_SECONDS = 0.4;
  static constexpr float Z_METRES = MatchTuning::Units::BALL_Z_METRES;

  explicit BodyControlDrill(bool thighDrill)
      : thigh(thighDrill), arrivalHeight(thighDrill ? 0.8f : 1.3f)
  {
  }

  const char* name() const override
  {
    return thigh ? "Thigh control" : "Chest control";
  }
  const char* summary() const override
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
    add(setting("Ball speed (m/s)", &ballSpeed, 2.0f, 35.0f, false,
                "Its pace over the ground when it is played in. Slow over a "
                "long way drops steeply onto him; fast is a driven ball."),
        "Ball");
    add(setting("Comes from (deg)", &fromAngle, -90.0f, 90.0f, true,
                "Where it is played from, against the way he faces (up the "
                "pitch): 0 in front of him, 90 from his right, -90 from his "
                "left. His chest and thigh play only a ball in front of "
                "him."),
        "Ball");
    add(setting("Feed distance (m)", &feedDistance, 3.0f, 60.0f, false), "Ball");
    add(setting("Arrival height (m)", &arrivalHeight, 0.3f, 2.0f, false,
                "How high it is as it passes over the spot where he stands. "
                "His thigh takes it from 0.28 to 0.55 of his height, his "
                "chest from 0.55 to 0.85."),
        "Ball");
    add(setting("Passes him by (m)", &passesBy, -1.0f, 1.0f, false,
                "Sideways offset of its line from where he stands: 0 "
                "straight at him."),
        "Ball");
    add(setting("First Touch", &firstTouch, 1.0f, 100.0f, true,
                "His First Touch (overrides the derived stat): how softly "
                "his chest or thigh can take it, and how well he carries the "
                "touch out."),
        "Player");
    add(setting("Height (cm)", &heightCm, 150.0f, 210.0f, true,
                "Where his thigh and chest are (shares of his height)."),
        "Player");
    return list;
  }

  std::vector<DrillPreset> presets() const override
  {
    const std::map<std::string, float> base{
        {"Ball speed (m/s)", 8.0f},   {"Comes from (deg)", 0.0f},
        {"Feed distance (m)", 20.0f}, {"Arrival height (m)", thigh ? 0.8f : 1.3f},
        {"Passes him by (m)", 0.0f}};
    const auto with = [&base](std::map<std::string, float> changes)
    {
      std::map<std::string, float> settings = base;
      for (const auto& [name, value] : changes) settings[name] = value;
      return settings;
    };
    return {
        {"Dropping ball", "8 m/s from 20 m in front, dropping onto him.", base},
        {"Long ball", "16 m/s from 40 m in front.",
         with({{"Ball speed (m/s)", 16.0f}, {"Feed distance (m)", 40.0f}})},
        {"Driven ball", "22 m/s from 25 m in front, flat.",
         with({{"Ball speed (m/s)", 22.0f}, {"Feed distance (m)", 25.0f}})},
        {"From the side", "10 m/s from 20 m, 60 deg to his right.",
         with({{"Ball speed (m/s)", 10.0f}, {"Comes from (deg)", 60.0f}})},
    };
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    return {{"Receiver",
             PlayerRole::ST,
             true,
             {},
             static_cast<int>(heightCm),
             {{DerivedStat::FIRST_TOUCH, firstTouch}}}};
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    playerId = ids[0];
    // In metres: he faces up the pitch (+x); the ball comes from fromAngle
    // round from in front of him, along a line passesBy to his side.
    const Vector2F up{1.0f, 0.0f};
    stand = MIDDLE;
    const Vector2F from = rotated(up, fromAngle * PI / 180.0f);
    const Vector2F travel{-from.x, -from.y};
    const Vector2F side = rotated(travel, PI * 0.5f);
    feedFrom = along(along(stand, side, passesBy), from, feedDistance);
    feedFrom = PitchPoint{std::clamp(feedFrom.x, PITCH.atX(0.005f), PITCH.atX(0.995f)),
                std::clamp(feedFrom.y, PITCH.atY(0.005f), PITCH.atY(0.995f))};
    MatchScenario scenario;
    scenario.players.push_back({playerId, stand, false});
    scenario.carrierId = 0;
    scenario.ballPosition = feedFrom;
    engine.startDrill(scenario);
    engine.setDrillTarget(playerId, stand, false, {}, 0.0f, 0.0f,
                          facingAngleOf(0.0f));
    engine.keepBall(playerId, true);
    const float feedMetres = metresBetween(feedFrom, along(stand, side, passesBy));
    const float lift =
        HeaderTargetDrill::liftFor(engine, feedMetres, ballSpeed, arrivalHeight);
    engine.feedBall(feedFrom, {travel.x * ballSpeed, travel.y * ballSpeed}, lift,
                    playerId);
    fedAt = secondsOf(engine);
    if (const MatchPlayer* player = slotOf(engine, playerId))
      lines = engine.bodyLines(*player);
    met.reset();
    dropped.reset();
    collectedAt.reset();
    outcome.clear();
    endAt.reset();
    last = stateOf(engine.getBall());
    ballPath = {feedFrom};
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    const MatchPlayer* player = slotOf(engine, playerId);
    if (engine.getSimulatedSteps() % 2 == 0) ballPath.push_back(ball.position);
    const bool his = engine.getPossessionAttachment() && engine.getPossessionAttachment()->getId() == playerId;
    if (!met && outcome.empty())
    {
      // Off his head, chest or thigh, by the height he met it at.
      const char* part =
          !(his && engine.hasTouchClaim() && engine.isTouchDropping()) ? nullptr
          : ball.touchContactHeight > lines.head       ? "head"
          : ball.touchContactHeight > lines.chest      ? "chest"
                                                       : "thigh";
      if (part && std::string_view(part) != partName())
      {
        outcome = std::format("not his {} (his {})", partName(), part);
      }
      else if (part)
      {
        Touch touch;
        touch.at = now;
        touch.height = ball.touchContactHeight;
        touch.gap = ball.touchContactGap;
        touch.paceIn = std::hypot(std::hypot(last.velocity.x, last.velocity.y),
                                  last.lift);
        touch.speed = std::hypot(ball.velocity.x, ball.velocity.y);
        touch.lift = ball.velocityZ;
        const float incoming = std::hypot(last.velocity.x, last.velocity.y);
        if (incoming > 0.1f && touch.speed > 0.1f)
          touch.offBack = turnDegrees(
              {-last.velocity.x / incoming, -last.velocity.y / incoming},
              {ball.velocity.x / touch.speed, ball.velocity.y / touch.speed});
        touch.where = player ? player->position : stand;
        // Where its flight off him first comes down, from where he met
        // it (the match's own flight).
        constexpr float DT =
            STEP / static_cast<float>(MatchTuning::Timing::BALL_SUBSTEPS);
        FlightState flight{touch.speed, touch.lift, touch.height, false, ball.spin};
        float gone = 0.0f;
        for (float time = 0.0f; time < 5.0f && flight.height > 0.0f && !flight.bounced;
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
               (last.height > 0.01f && ball.z <= 0.0f &&
                metresBetween(ball.position, stand) > 1.5f))
      {
        outcome = "not touched (out of reach)";
      }
    }
    else if (met && outcome.empty())
    {
      if (!dropped && ball.z <= 0.0f)
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
        outcome = "not kept";
      else if (engine.getState() != MatchState::PLAYING)
        outcome = "out of play";
      else if (now >= met->at + KEEP_LIMIT)
        outcome = "time up";
    }
    last = stateOf(ball);
    if (!outcome.empty() && !endAt) endAt = now;
    return !(endAt && now >= *endAt + LINGER_SECONDS);
  }

  void drawOverlay(const IMatchRenderer& renderer, ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    DrillDraw::line(renderer, draw, feedFrom, stand, IM_COL32(255, 255, 255, 70),
                    1.0f * scale);
    DrillDraw::marker(renderer, draw, feedFrom, 3.0f * scale,
                      IM_COL32(255, 255, 255, 200), "feed");
    DrillDraw::marker(renderer, draw, stand, 2.5f * scale,
                      IM_COL32(90, 220, 120, 220), partName());
    if (dropped)
      DrillDraw::marker(renderer, draw, *dropped, 3.0f * scale,
                        IM_COL32(255, 230, 120, 255), "drops");
    DrillDraw::path(renderer, draw, ballPath, IM_COL32(255, 230, 120, 160),
                    1.5f * scale);
  }

  std::vector<std::string> report() const override
  {
    std::vector<std::string> out;
    out.push_back(std::format(
        "{:.0f} m/s from {:+.0f} deg, {:.0f} m out, over him at {:.2f} m{}",
        ballSpeed, fromAngle, feedDistance, arrivalHeight,
        passesBy != 0.0f ? std::format(", {:+.1f} m by", passesBy) : std::string()));
    out.push_back(std::format(
        "Player: First Touch {:.0f}, {:.0f} cm · thigh from {:.2f} m, chest "
        "from {:.2f} m, head from {:.2f} m",
        firstTouch, heightCm, lines.feet, lines.chest, lines.head));
    if (met)
    {
      constexpr float DEGREES = 180.0f / PI;
      out.push_back(std::format(
          "On his {} at {:.2f} s, {:.2f} m up, {:.2f} m from his centre: in at "
          "{:.1f} m/s, off at {:.1f} m/s ({:.1f} along, {:+.1f} up, {:+.0f} "
          "deg), {:.0f} deg off straight back",
          partName(), met->at - fedAt, met->height, met->gap, met->paceIn,
          std::hypot(met->speed, met->lift), met->speed, met->lift,
          std::atan2(met->lift, met->speed) * DEGREES, met->offBack));
      out.push_back(std::format("Its flight lands {:.2f} m from where he met it, "
                                "{:.2f} s after",
                                met->landsFrom, met->landsAfter));
    }
    if (!outcome.empty()) out.push_back("OUTCOME  " + outcome);
    return out;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (outcome.empty()) return {};
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    std::vector<DrillMetric> out{
        {"Met", flag(met.has_value())},
        {"Kept", flag(outcome == "kept")},
        {"Head", flag(outcome.ends_with("(his head)"))},
        {"Chest", flag(outcome.ends_with("(his chest)"))},
        {"Thigh", flag(outcome.ends_with("(his thigh)"))},
        {"Feet", flag(outcome.ends_with("(his feet)"))},
        {"Old trap", flag(outcome.ends_with("the old trap)"))},
        {"Missed", flag(outcome == "not touched (out of reach)")}};
    if (!met) return out;
    constexpr float DEGREES = 180.0f / PI;
    const float paceOff = std::hypot(met->speed, met->lift);
    out.push_back({"Meet height (m)", met->height});
    out.push_back({"Meet gap (m)", met->gap});
    out.push_back({"Pace in (m/s)", met->paceIn});
    out.push_back({"Pace off (m/s)", paceOff});
    out.push_back({"Pace kept", met->paceIn > 0.1f ? paceOff / met->paceIn : 0.0f});
    out.push_back(
        {"Elevation off (deg)", std::atan2(met->lift, met->speed) * DEGREES});
    out.push_back({"Off straight back (deg)", met->offBack});
    out.push_back({"Lands from contact (m)", met->landsFrom});
    out.push_back({"Lands after (s)", met->landsAfter});
    if (collectedAt) out.push_back({"Collected after (s)", *collectedAt - met->at});
    return out;
  }

 private:
  /** The ball at the end of a step (pitch position, m/s, metres up). */
  struct State
  {
    PitchPoint position{};
    Vector2F velocity{};
    float height = 0.0f;
    float lift = 0.0f;
  };
  static State stateOf(const MatchBall& ball)
  {
    return {ball.position, ball.velocity, MatchPitch::heightMetres(ball.z), ball.velocityZ};
  }
  struct Touch
  {
    double at = 0.0;
    float height = 0.0f;
    float gap = 0.0f;
    float paceIn = 0.0f;
    /** Off him: along the ground and upward (m/s). */
    float speed = 0.0f;
    float lift = 0.0f;
    /** Seen from above, off the line straight back the way it came (deg). */
    float offBack = 0.0f;
    PitchPoint where{};
    /** Where its flight off him first comes down: how far from where
     * he met it (m) and how long after (s). */
    float landsFrom = 0.0f;
    float landsAfter = 0.0f;
  };

  [[nodiscard]] const char* partName() const { return thigh ? "thigh" : "chest"; }

  bool thigh = false;
  float ballSpeed = 8.0f;
  float fromAngle = 0.0f;
  float feedDistance = 20.0f;
  float arrivalHeight = 1.3f;
  float passesBy = 0.0f;
  float firstTouch = 60.0f;
  float heightCm = 180.0f;

  PlayerID playerId = 0;
  PitchPoint stand{};
  PitchPoint feedFrom{};
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
 public:
  static constexpr float Z_METRES = MatchTuning::Units::BALL_Z_METRES;
  /** Where the ball comes down (pitch). */
  static inline const PitchPoint LANDS = at(0.55f, 0.5f);

  const char* name() const override { return "Loose high ball"; }
  const char* summary() const override
  {
    return "A long ball dropping between a home and an away player, both "
           "left to the brain. Who goes for it, where to, whether the other "
           "contests it or backs off, who gets to it first, and with what "
           "jump.";
  }

  std::vector<DrillParameter> parameters() override
  {
    std::vector<DrillParameter> list;
    list.push_back(setting("Ball speed (m/s)", &ballSpeed, 6.0f, 30.0f, false,
                           "Its pace over the ground when played."));
    list.push_back(setting("Feed distance (m)", &feedDistance, 10.0f, 60.0f, false,
                           "How far it travels before it comes down."));
    list.push_back(setting("Home from it (m)", &homeDistance, 0.0f, 25.0f, false,
                           "How far the home player starts from where it comes "
                           "down."));
    list.push_back(setting("Home angle (deg)", &homeAngle, -180.0f, 180.0f, true,
                           "Where he starts: 0 beyond where it comes down (along "
                           "its flight), 180 short of it, 90 to its left."));
    list.push_back(setting("Away from it (m)", &awayDistance, 0.0f, 25.0f, false));
    list.push_back(setting("Away angle (deg)", &awayAngle, -180.0f, 180.0f, true));
    list.push_back(setting("Home height (cm)", &homeHeight, 150.0f, 210.0f, true));
    list.push_back(setting("Away height (cm)", &awayHeight, 150.0f, 210.0f, true));
    list.push_back(setting("Home Physicality", &homePhysicality, 1.0f, 100.0f, true));
    list.push_back(setting("Away Physicality", &awayPhysicality, 1.0f, 100.0f, true));
    return list;
  }

  std::vector<DrillPreset> presets() const override
  {
    const std::map<std::string, float> base{
        {"Ball speed (m/s)", 14.0f},  {"Feed distance (m)", 35.0f},
        {"Home from it (m)", 6.0f},   {"Home angle (deg)", 180.0f},
        {"Away from it (m)", 6.0f},   {"Away angle (deg)", 0.0f}};
    const auto with = [&base](std::map<std::string, float> changes)
    {
      std::map<std::string, float> settings = base;
      for (const auto& [name, value] : changes) settings[name] = value;
      return settings;
    };
    return {
        {"Even race", "Both 6 m from where it comes down, one either side.", base},
        {"Away too far", "Away starts 18 m off: he can't contest it.",
         with({{"Away from it (m)", 18.0f}})},
        {"Tall against short", "Even race, a 1.95 m home player against a 1.70 m one.",
         with({{"Home height (cm)", 195.0f}, {"Away height (cm)", 170.0f}})},
    };
  }

  std::vector<DrillPlayerSpec> players() const override
  {
    return {{"Home", PlayerRole::ST, true, {{"Physicality", homePhysicality}},
             static_cast<int>(homeHeight)},
            {"Away", PlayerRole::CB, false, {{"Physicality", awayPhysicality}},
             static_cast<int>(awayHeight)}};
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    homeId = ids[0];
    awayId = ids[1];
    const Vector2F travel{1.0f, 0.0f};
    const auto startAt = [&](float metres, float degrees)
    {
      const PitchPoint at = along(LANDS, rotated(travel, degrees * PI / 180.0f), metres);
      return PitchPoint{std::clamp(at.x, PITCH.atX(0.02f), PITCH.atX(0.98f)), std::clamp(at.y, PITCH.atY(0.02f), PITCH.atY(0.98f))};
    };
    feedFrom = along(LANDS, travel, -feedDistance);
    feedFrom = PitchPoint{std::clamp(feedFrom.x, PITCH.atX(0.005f), PITCH.atX(0.995f)), feedFrom.y};
    MatchScenario scenario;
    scenario.players.push_back({homeId, startAt(homeDistance, homeAngle), false});
    scenario.players.push_back({awayId, startAt(awayDistance, awayAngle), false});
    scenario.ballPosition = feedFrom;
    engine.startDrill(scenario);
    const float lift = HeaderTargetDrill::liftFor(
        engine, metresBetween(feedFrom, LANDS), ballSpeed, 0.05f);
    engine.feedBall(feedFrom, {travel.x * ballSpeed, travel.y * ballSpeed}, lift, 0);
    fedAt = secondsOf(engine);
    duelsBefore = engine.getStats().aerialDuelsContested;
    headersBefore = engine.getStrikeContacts()
                        .byKind[static_cast<std::size_t>(StrikeKind::HEADER)]
                        .strikes;
    contact.reset();
    jumped = {};
    running = {};
    ran = {};
    last = {};
    for (std::size_t index = 0; index < 2; ++index)
      if (const MatchPlayer* player = slotOf(engine, index == 0 ? homeId : awayId))
        last[index] = player->position;
    endAt.reset();
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    const MatchBall& ball = engine.getBall();
    std::array<const MatchPlayer*, 2> who{slotOf(engine, homeId), slotOf(engine, awayId)};
    for (std::size_t index = 0; index < 2; ++index)
    {
      if (!who[index]) continue;
      ran[index] += metresBetween(last[index], who[index]->position);
      last[index] = who[index]->position;
      if (!contact && who[index]->airborne() && !jumped[index])
      {
        jumped[index] = true;
        running[index] = who[index]->runningJump;
      }
    }
    if (!contact)
    {
      // The first of them to play it (head, chest or foot), or it lands.
      const PlayerID kicker = engine.getLegacyContactActor() ? engine.getLegacyContactActor()->getId() : 0;
      const PlayerID holder = engine.getPossessionAttachment() ? engine.getPossessionAttachment()->getId() : 0;
      const PlayerID by = kicker == homeId || kicker == awayId ? kicker : holder;
      if (by == homeId || by == awayId)
      {
        Contact reading;
        reading.at = now - fedAt;
        reading.by = by == homeId ? 1 : 2;
        reading.height = ball.sentHeightMetres;
        reading.headed = engine.getStrikeContacts()
                             .byKind[static_cast<std::size_t>(StrikeKind::HEADER)]
                             .strikes > headersBefore;
        reading.duel = engine.getStats().aerialDuelsContested > duelsBefore;
        for (std::size_t index = 0; index < 2; ++index)
          if (who[index])
            reading.gap[index] = metresBetween(who[index]->position, ball.sentFrom);
        contact = reading;
        endAt = now + 0.5;
      }
      else if (ball.z <= 0.0f && now > fedAt + 0.5)
      {
        // Down without either of them getting to it in the air.
        Contact reading;
        reading.at = now - fedAt;
        for (std::size_t index = 0; index < 2; ++index)
          if (who[index]) reading.gap[index] = metresBetween(who[index]->position, ball.position);
        contact = reading;
        endAt = now + 0.5;
      }
    }
    if (engine.getState() != MatchState::PLAYING && !endAt) endAt = now;
    return !(endAt && now >= *endAt) && now < fedAt + 10.0;
  }

  std::vector<std::string> report() const override
  {
    static constexpr std::array<const char*, 3> WHO{"nobody: it came down", "home",
                                                   "away"};
    std::vector<std::string> lines;
    lines.push_back(std::format(
        "{:.0f} m/s over {:.0f} m · home {:.0f} m off at {:+.0f} deg ({:.0f} cm), "
        "away {:.0f} m off at {:+.0f} deg ({:.0f} cm)",
        ballSpeed, feedDistance, homeDistance, homeAngle, homeHeight, awayDistance,
        awayAngle, awayHeight));
    if (contact)
      lines.push_back(std::format(
          "OUTCOME  {} at {:.2f} s{}{}, {:.2f} m up · home {} ({:.1f} m away, ran "
          "{:.1f} m), away {} ({:.1f} m away, ran {:.1f} m)",
          WHO[static_cast<std::size_t>(contact->by)], contact->at,
          contact->headed ? ", headed" : "", contact->duel ? ", in a duel" : "",
          contact->height, jumpName(0), contact->gap[0], ran[0], jumpName(1),
          contact->gap[1], ran[1]));
    return lines;
  }

  std::vector<DrillMetric> metrics() const override
  {
    if (!contact) return {};
    const auto flag = [](bool value) { return value ? 1.0 : 0.0; };
    return {{"First to it (1 home, 2 away)", static_cast<double>(contact->by)},
            {"Headed", flag(contact->headed)},
            {"Duel", flag(contact->duel)},
            {"Contact at (s)", contact->at},
            {"Contact height (m)", contact->height},
            {"Home jumped", flag(jumped[0])},
            {"Away jumped", flag(jumped[1])},
            {"Home running jump", flag(jumped[0] && running[0])},
            {"Away running jump", flag(jumped[1] && running[1])},
            {"Home gap at contact (m)", contact->gap[0]},
            {"Away gap at contact (m)", contact->gap[1]},
            {"Home ran (m)", ran[0]},
            {"Away ran (m)", ran[1]}};
  }

 private:
  struct Contact
  {
    double at = 0.0;
    /** 0 nobody, 1 home, 2 away. */
    int by = 0;
    float height = 0.0f;
    bool headed = false;
    bool duel = false;
    /** Each player's distance from the ball at the contact (m). */
    std::array<float, 2> gap{};
  };
  [[nodiscard]] std::string jumpName(std::size_t index) const
  {
    return !jumped[index] ? "didn't jump" : running[index] ? "running jump" : "standing jump";
  }

  float ballSpeed = 14.0f;
  float feedDistance = 35.0f;
  float homeDistance = 6.0f;
  float homeAngle = 180.0f;
  float awayDistance = 6.0f;
  float awayAngle = 0.0f;
  float homeHeight = 180.0f;
  float awayHeight = 180.0f;
  float homePhysicality = 60.0f;
  float awayPhysicality = 60.0f;

  PlayerID homeId = 0;
  PlayerID awayId = 0;
  PitchPoint feedFrom{};
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

std::vector<std::unique_ptr<Drill>> makeDrills()
{
  std::vector<std::unique_ptr<Drill>> drills;
  drills.push_back(std::make_unique<SprintDrill>());
  drills.push_back(std::make_unique<TurnAndRunDrill>());
  drills.push_back(std::make_unique<TurnDrill>());
  drills.push_back(std::make_unique<SlalomDrill>());
  drills.push_back(std::make_unique<RepeatedSprintsDrill>());
  drills.push_back(std::make_unique<FlagChaseDrill>());
  drills.push_back(std::make_unique<OvertakeDrill>());
  drills.push_back(std::make_unique<LooseBallDrill>());
  drills.push_back(std::make_unique<TackleDrill>());
  drills.push_back(std::make_unique<CollisionDrill>());
  drills.push_back(std::make_unique<ShotDrill>());
  drills.push_back(std::make_unique<ShotBlockDrill>());
  drills.push_back(std::make_unique<ShotTargetDrill>());
  drills.push_back(std::make_unique<FirstTouchDrill>());
  drills.push_back(std::make_unique<PassDrill>());
  drills.push_back(std::make_unique<FirstTimePassDrill>());
  drills.push_back(std::make_unique<CorridorPassDrill>());
  drills.push_back(std::make_unique<BullseyePassDrill>());
  drills.push_back(std::make_unique<DrivenCorridorDrill>());
  drills.push_back(std::make_unique<ChipTargetDrill>());
  drills.push_back(std::make_unique<KeeperThrowDrill>());
  drills.push_back(std::make_unique<HeaderTargetDrill>());
  drills.push_back(std::make_unique<BodyControlDrill>(false));
  drills.push_back(std::make_unique<BodyControlDrill>(true));
  drills.push_back(std::make_unique<LooseHighBallDrill>());
  return drills;
}

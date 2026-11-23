// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

// The sandbox's training sessions (see match_sandbox_training.h).

#include "tools/match_sandbox_training.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <format>
#include <print>

#include "gui/widgets/theme.h"
#include "model/match_engine_internal.h"
#include "tools/match_sandbox_setup.h"

namespace
{
constexpr float LENGTH = MatchTuning::Pitch::MATCH_LENGTH_METRES;
constexpr float WIDTH = MatchTuning::Pitch::MATCH_WIDTH_METRES;
constexpr float STEP = MatchTuning::Timing::FIXED_STEP_SECONDS;
constexpr float MPH_PER_METRE_PER_SECOND = 2.236936F;
constexpr int SECONDS_PER_MINUTE = 60;
/** Shorter than this, a vector has no direction. */
constexpr float NO_LENGTH = 1e-6F;
constexpr float HALF = 0.5F;
/** Pitch fractions: halfway (the centre spot), and in front of each goal. */
constexpr float HALFWAY = 0.5F;
constexpr float NEAR_GOAL_X = 0.03F;
constexpr float FAR_GOAL_X = 0.97F;
/** Drawn lines (scaled pixels). */
constexpr float LINE_THICKNESS = 1.5F;
constexpr float GATE_LINE_THICKNESS = 2.0F;
constexpr float START_LINE_THICKNESS = 3.0F;
/** A finished runner's place, drawn just off his lane (pitch fractions). */
constexpr float PLACE_LABEL_LEFT = 0.006F;
constexpr float PLACE_LABEL_UP = 0.04F;
/** Stamina this close to his floor counts as spent. */
constexpr float FUEL_FLOOR_TOLERANCE = 1e-4F;
/** A spent runner walks this far off the track, towards the centre. */
constexpr float OFF_TRACK_METRES = 10.0F;
/** Laps trace: faster than this he counts as moving; how far his running
 * is off his facing, by band (degrees); the followed player every half
 * second. */
constexpr float TRACE_MOVING_SPEED = 3.0F;
constexpr std::array<float, 4> OFF_FACING_DEGREES{10.0F, 20.0F, 45.0F, 90.0F};
/** Below this a direction has no length to face along. */
constexpr float MIN_DIRECTION_LENGTH = 1e-6F;
constexpr float DEGREES_PER_HALF_TURN = 180.0F;
constexpr double FOLLOW_EVERY_SECONDS = 0.5;
constexpr double CLOCK_TOLERANCE = 1e-6;

// Training plays on a match pitch, placed by fractions of it (PITCH.at)
// and thought about in metres; engine points go through the pitch.
const MatchPitch& PITCH = MatchPitch::matchSize();

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
  const float x = to.x - from.x;
  const float y = to.y - from.y;
  const float size = std::hypot(x, y);
  if (size < NO_LENGTH)
  {
    return {.x = 1.0F, .y = 0.0F};
  }
  return {.x = x / size, .y = y / size};
}

/** "4:07" from seconds. */
std::string clock(double seconds)
{
  const int whole = static_cast<int>(seconds);
  return std::format("{}:{:02}", whole / SECONDS_PER_MINUTE,
                     whole % SECONDS_PER_MINUTE);
}

/**
 * The acceleration attribute a player's acceleration is built from: Pace
 * and Physicality, weighted as both handshakes weigh them (0-100).
 */
float accelerationStat(const TrainingPlayer& player)
{
  using P = MatchTuning::Player;
  return (player.stat("Pace") * P::ACCELERATION_PACE +
          player.stat("Physicality") * P::ACCELERATION_PHYSICALITY) /
         (P::ACCELERATION_PACE + P::ACCELERATION_PHYSICALITY);
}

const ImU32 CONE = IM_COL32(255, 140, 30, 255);
const ImU32 LINE = IM_COL32(255, 255, 255, 110);
const ImU32 DONE = IM_COL32(120, 230, 140, 255);

// --- Across and back -------------------------------------------------------

/**
 * Across and back: the squad lines up along one touchline, a lane each
 * between cones, and sprints to the far touchline and back. The engine
 * keeps players just inside the lines and brakes them onto a point, so they
 * turn at the far cones and pull up at the finish as if touching the line.
 */
class AcrossAndBackDrill final : public TrainingDrill
{
 public:
  using TrainingDrill::TrainingDrill;

  [[nodiscard]] const char* name() const override { return "Across and back"; }
  [[nodiscard]] const char* summary() const override
  {
    return "The squad lines up along the near touchline, a lane each marked "
           "out by cones, and sprints to the far touchline and back. As each "
           "player crosses the finish: his time, top speed and peak "
           "acceleration.";
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    lanes.clear();
    finishedCount = 0;
    lastFinish.reset();
    const std::size_t count = std::min(ids.size(), squad.size());
    laneWidth = (LAST_X - FIRST_X) /
                static_cast<float>(std::max<std::size_t>(count, 1));
    MatchScenario scenario;
    for (std::size_t index = 0; index < count; ++index)
    {
      Lane lane;
      lane.id = ids[index];
      lane.x = FIRST_X + laneWidth * (static_cast<float>(index) + HALF);
      lane.previous = PITCH.at(lane.x, NEAR_Y);
      lanes.push_back(lane);
      scenario.players.push_back({.playerId = lane.id,
                                  .position = lane.previous,
                                  .makingRun = false,
                                  .facing = std::nullopt});
    }
    // In front of a goal, clear of every lane.
    scenario.ballPosition = PITCH.at(NEAR_GOAL_X, HALFWAY);
    engine.startDrill(scenario);
    for (const Lane& lane : lanes)
    {
      engine.setDrillTarget(lane.id, PITCH.at(lane.x, FAR_Y), true);
    }
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    for (Lane& lane : lanes)
    {
      if (lane.place != 0U)
      {
        continue;
      }
      const MatchPlayer* player = slotOf(engine, lane.id);
      if (player == nullptr)
      {
        continue;
      }
      // Hurt: he pulls up, and walks back to the start.
      if (player->isInjured)
      {
        lane.injured = true;
        lane.place = ++finishedCount;
        lastFinish = now;
        engine.setDrillTarget(lane.id, PITCH.at(lane.x, NEAR_Y), false);
        continue;
      }
      const float speed = speedOf(*player);
      lane.topSpeed = std::max(lane.topSpeed, speed);
      lane.peakAcceleration =
          std::max(lane.peakAcceleration, (speed - lane.lastSpeed) / STEP);
      lane.lastSpeed = speed;
      const PitchPoint at = player->position;
      if (!lane.turned)
      {
        // Within a metre of the far cone: he has touched the line.
        if (metresBetween(at, PITCH.at(lane.x, FAR_Y)) <= TURN_RADIUS_METRES)
        {
          lane.turned = true;
          // Back to the near touchline (the engine stops him on it).
          engine.setDrillTarget(lane.id, PITCH.at(lane.x, 0.0F), true);
        }
      }
      else if (lane.previous.y > PITCH.atY(NEAR_Y) && at.y <= PITCH.atY(NEAR_Y))
      {
        // Crossed between two steps: interpolate the moment.
        const auto share = static_cast<double>(
            (lane.previous.y - PITCH.atY(NEAR_Y)) / (lane.previous.y - at.y));
        lane.finish = now - STEP + (STEP * share);
        lane.place = ++finishedCount;
        lastFinish = now;
        engine.setDrillTarget(lane.id, PITCH.at(lane.x, NEAR_Y), false);
      }
      lane.previous = at;
    }
    const bool allIn = finishedCount >= lanes.size();
    return (!allIn || !lastFinish || now <= *lastFinish + 1.0) &&
           now < MAX_SECONDS;
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    // Cones between the lanes, on both lines; the start and turn lines.
    DrillDraw::line(renderer, draw, PITCH.at(FIRST_X, NEAR_Y),
                    PITCH.at(LAST_X, NEAR_Y), LINE, LINE_THICKNESS * scale);
    DrillDraw::line(renderer, draw, PITCH.at(FIRST_X, FAR_Y),
                    PITCH.at(LAST_X, FAR_Y), LINE, LINE_THICKNESS * scale);
    for (std::size_t edge = 0; edge <= lanes.size(); ++edge)
    {
      const float x = FIRST_X + (laneWidth * static_cast<float>(edge));
      for (const float y : {NEAR_Y, 0.5F, FAR_Y})
      {
        DrillDraw::cone(renderer, draw, PITCH.at(x, y), CONE);
      }
    }
    for (const Lane& lane : lanes)
    {
      if (lane.finish)
      {
        DrillDraw::label(
            renderer, draw,
            PITCH.at(lane.x - PLACE_LABEL_LEFT, NEAR_Y + PLACE_LABEL_UP),
            std::format("{}", lane.place).c_str(), DONE);
      }
    }
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::vector<std::string> lines;
    std::size_t turned = 0;
    std::size_t injured = 0;
    for (const Lane& lane : lanes)
    {
      if (lane.turned)
      {
        ++turned;
      }
      if (lane.injured)
      {
        ++injured;
      }
    }
    lines.push_back(std::format("{} of {} back · {} turned",
                                finishedCount - injured, lanes.size(), turned));
    if (injured > 0)
    {
      lines.push_back(std::format("Injured: {}", injured));
    }
    return lines;
  }

  [[nodiscard]] std::vector<TrainingColumn> columns() const override
  {
    return {
        {.name = "Time",
         .tooltip = "From the start signal to crossing the finish line."},
        {.name = "Top speed", .tooltip = "Fastest he ran, in miles an hour."},
        {.name = "Pace", .tooltip = "The Pace attribute: his top speed."},
        {.name = "Accel.",
         .tooltip = "Peak acceleration over the run (m/s²): the biggest "
                    "gain in speed in a tenth of a second, from standing "
                    "or out of the turn."},
        {.name = "Accel. stat",
         .tooltip = "What his acceleration is built from: Pace and "
                    "Physicality, weighted as the engine weighs them "
                    "(1.6 : 0.6)."}};
  }

  [[nodiscard]] std::vector<TrainingRow> results() const override
  {
    std::vector<const Lane*> done;
    for (const Lane& lane : lanes)
    {
      if (lane.place != 0U)
      {
        done.push_back(&lane);
      }
    }
    std::ranges::sort(done, {}, &Lane::place);
    std::vector<TrainingRow> rows;
    for (const Lane* lane : done)
    {
      const auto index = static_cast<std::size_t>(lane - lanes.data());
      const TrainingPlayer& player = squad[index];
      const float mph = lane->topSpeed * MPH_PER_METRE_PER_SECOND;
      const float accelerationRating = accelerationStat(player);
      // The injured sort after every time.
      const double seconds = lane->finish.value_or(MAX_SECONDS);
      rows.push_back(
          {.player = index,
           .cells = {lane->finish ? std::format("{:.2f} s", seconds)
                                  : std::string("injured"),
                     std::format("{:.1f} mph", mph),
                     std::format("{:.0f}", player.stat("Pace")),
                     std::format("{:.1f} m/s²", lane->peakAcceleration),
                     std::format("{:.0f}", accelerationRating)},
           .values = {seconds, mph, player.stat("Pace"), lane->peakAcceleration,
                      accelerationRating}});
    }
    return rows;
  }

  [[nodiscard]] std::optional<float> topSpeed(std::size_t player) const override
  {
    if (player >= lanes.size() || !lanes[player].finish)
    {
      return std::nullopt;
    }
    return lanes[player].topSpeed;
  }

 private:
  /** The lines, 1.5 m in from each touchline: a runner sent to the touchline
   * pulls up about a metre in, so he crosses the finish on his way there. */
  static constexpr float NEAR_Y = 1.5F / WIDTH;
  static constexpr float FAR_Y = 1.0F - (1.5F / WIDTH);
  /** The lanes span this much of the touchline. */
  static constexpr float FIRST_X = 0.1F;
  static constexpr float LAST_X = 0.9F;
  static constexpr float TURN_RADIUS_METRES = 1.0F;
  static constexpr double MAX_SECONDS = 120.0;

  struct Lane
  {
    PlayerID id = 0;
    float x = 0.0F;
    PitchPoint previous;
    bool turned = false;
    bool injured = false;
    std::optional<double> finish;
    std::size_t place = 0;
    float topSpeed = 0.0F;
    float lastSpeed = 0.0F;
    float peakAcceleration = 0.0F;
  };

  std::vector<Lane> lanes;
  float laneWidth = 0.1F;
  std::size_t finishedCount = 0;
  std::optional<double> lastFinish;
};

// --- Slalom ----------------------------------------------------------------

/**
 * Share [0, 1] of the move at which a runner crosses the line through
 * `point` square to `direction` (going that way), if he does.
 */
std::optional<float> crossingShare(PitchPoint from, PitchPoint to,
                                   PitchPoint point, Vector2F direction)
{
  const float before =
      ((from.x - point.x) * direction.x) + ((from.y - point.y) * direction.y);
  const float after =
      ((to.x - point.x) * direction.x) + ((to.y - point.y) * direction.y);
  if (before >= 0.0F || after < 0.0F)
  {
    return std::nullopt;
  }
  return -before / (after - before);
}

/**
 * Slalom: the sandbox's Slalom drill (match_sandbox_drills.cpp) for the
 * squad, one runner at a time. The squad queues behind the start in the
 * order of its Across and back times, fastest first; each runner sets off
 * once the one before him is through the second gate. The course is the
 * layout that shows Agility best: 8 cones 5 m apart, staggered 1 m (with
 * Pace, Physicality and Acceleration held, Agility 1 to 100 took about 30%
 * off the time; Pace 40 to 95 under 4%; measured with 2 m gates, aimed
 * through their middles). Each gate is the whole of its cone's correct
 * side, from the cone's edge: wide still counts, just slower. Runners
 * aim to pass LINE_METRES outside that edge.
 */
class SlalomDrill final : public TrainingDrill
{
 public:
  SlalomDrill(const std::vector<TrainingPlayer>& squad_ref,
              const TrainingDrill* sprint_ref)
      : TrainingDrill(squad_ref), sprint(sprint_ref)
  {
  }

  [[nodiscard]] const char* name() const override { return "Slalom"; }
  [[nodiscard]] const char* summary() const override
  {
    return "8 cones 5 m apart, staggered 1 m, each passed on alternate sides. "
           "The squad queues at the start, fastest in Across "
           "and back first; each runner goes once the one before him is "
           "through the second gate. A cone passed the wrong side has to be "
           "gone back for; wide is allowed, just slower. "
           "As each player finishes: his time, his time round the outside of "
           "the cones, "
           "and his Agility, Acceleration and Pace.";
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    cones.clear();
    gates.clear();
    for (std::size_t cone = 0; cone < CONES; ++cone)
    {
      const PitchPoint at =
          along(along(START, {.x = 1.0F, .y = 0.0F},
                      SPACING_METRES * static_cast<float>(cone + 1)),
                {.x = 0.0F, .y = 1.0F}, sideOf(cone) * STAGGER_METRES);
      cones.push_back(at);
      // Runners head for their line round the cone.
      gates.push_back(along(at, {.x = 0.0F, .y = 1.0F},
                            sideOf(cone) * (GATE_BUFFER_METRES + LINE_METRES)));
    }
    finish = along(START, {.x = 1.0F, .y = 0.0F},
                   SPACING_METRES * static_cast<float>(CONES + 1));

    // The queue: fastest in Across and back first, then anyone without a
    // time there in squad order.
    const std::size_t count = std::min(ids.size(), squad.size());
    std::vector<std::optional<double>> sprintTimes(count);
    if (sprint != nullptr)
    {
      for (const TrainingRow& row : sprint->results())
      {
        if (row.player < count && !row.values.empty())
        {
          sprintTimes[row.player] = row.values.front();
        }
      }
    }
    queue.clear();
    for (std::size_t index = 0; index < count; ++index)
    {
      queue.push_back(index);
    }
    std::ranges::stable_sort(queue,
                             [&](std::size_t a, std::size_t b)
                             {
                               if (sprintTimes[a] && sprintTimes[b])
                               {
                                 return *sprintTimes[a] < *sprintTimes[b];
                               }
                               return sprintTimes[a].has_value() &&
                                      !sprintTimes[b].has_value();
                             });

    runners.assign(count, Runner{});
    released = 0;
    doneCount = 0;
    lastDone.reset();
    MatchScenario scenario;
    for (std::size_t place = 0; place < queue.size(); ++place)
    {
      Runner& runner = runners[queue[place]];
      runner.id = ids[queue[place]];
      runner.previous = queueSpot(place);
      scenario.players.push_back({.playerId = runner.id,
                                  .position = runner.previous,
                                  .makingRun = false,
                                  .facing = std::nullopt});
    }
    // In front of a goal, clear of the course and the queue.
    scenario.ballPosition = PITCH.at(FAR_GOAL_X, HALFWAY);
    engine.startDrill(scenario);
    for (std::size_t index = 0; index < count; ++index)
    {
      const MatchPlayer* player = slotOf(engine, runners[index].id);
      if (player == nullptr)
      {
        continue;
      }
      runners[index].agility =
          player->derivedStat(DerivedStat::AGILITY) * 100.0F;
      runners[index].acceleration =
          player->derivedStat(DerivedStat::ACCELERATION) * 100.0F;
    }
    if (!queue.empty())
    {
      release(engine, 0.0);
    }
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    for (Runner& runner : runners)
    {
      if (!runner.started || runner.doneAt)
      {
        continue;
      }
      const MatchPlayer* player = slotOf(engine, runner.id);
      if (player == nullptr)
      {
        continue;
      }
      if (player->isInjured)
      {
        runner.injured = true;
        finishRunner(engine, runner, now);
        continue;
      }
      follow(engine, runner, *player);
      runner.cornering +=
          STEP * outsideShare(runner, runner.previous, player->position);
      passGates(engine, runner, *player, now);
      runner.previous = player->position;
    }
    // The next runner goes once the one before him is through the second
    // gate (or out of it), and he is on the start.
    if (released < queue.size())
    {
      const Runner& before = runners[queue[released - 1]];
      const MatchPlayer* next = slotOf(engine, runners[queue[released]].id);
      if ((before.gatesPassed >= 2 || before.doneAt) && (next != nullptr) &&
          metresBetween(next->position, START) <= ON_THE_START_METRES)
      {
        release(engine, now);
      }
    }
    const bool allDone = doneCount >= runners.size();
    return (!allDone || !lastDone || now <= *lastDone + 1.0) &&
           now < MAX_SECONDS;
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    const float half = 4.0F;
    DrillDraw::line(renderer, draw, PitchPoint{START.x, START.y - half},
                    PitchPoint{START.x, START.y + half},
                    IM_COL32(90, 220, 120, 255), START_LINE_THICKNESS * scale);
    for (std::size_t cone = 0; cone < cones.size(); ++cone)
    {
      // The gate, open beyond what is drawn.
      DrillDraw::line(renderer, draw, gateEdge(cone, 0.0F),
                      gateEdge(cone, GATE_DRAWN_METRES), LINE,
                      GATE_LINE_THICKNESS * scale);
      DrillDraw::cone(renderer, draw, cones[cone], CONE);
    }
    DrillDraw::line(renderer, draw, PitchPoint{finish.x, finish.y - half},
                    PitchPoint{finish.x, finish.y + half},
                    IM_COL32(240, 80, 80, 255), START_LINE_THICKNESS * scale);
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    std::size_t running = 0;
    int missed = 0;
    std::size_t injured = 0;
    for (const Runner& runner : runners)
    {
      if (runner.started && !runner.doneAt)
      {
        ++running;
      }
      missed += runner.missed;
      if (runner.injured)
      {
        ++injured;
      }
    }
    std::vector<std::string> lines;
    lines.push_back(std::format("{} of {} through · {} running · {} queuing",
                                doneCount - injured, runners.size(), running,
                                queue.size() - released));
    if (missed > 0)
    {
      lines.push_back(std::format("Gates missed (gone back for): {}", missed));
    }
    if (injured > 0)
    {
      lines.push_back(std::format("Injured: {}", injured));
    }
    return lines;
  }

  [[nodiscard]] std::vector<TrainingColumn> columns() const override
  {
    return {
        {.name = "Time",
         .tooltip = "From his start signal to crossing the finish line, gates "
                    "missed and gone back for included."},
        {.name = "Cornering",
         .tooltip = "Time spent round the outside of the cones: beyond "
                    "the cone he is turning round, on its gate's side. "
                    "A tighter, quicker turn spends less."},
        {.name = "Agility",
         .tooltip = "The Agility stat: his grip in a turn, how fast his "
                    "body turns, his braking, and a small boost to his "
                    "acceleration."},
        {.name = "Accel.",
         .tooltip = "The Acceleration stat: his push from a standstill, "
                    "and a small boost to his braking."},
        {.name = "Pace", .tooltip = "The Pace attribute: his top speed."}};
  }

  [[nodiscard]] std::vector<TrainingRow> results() const override
  {
    std::vector<const Runner*> done;
    for (const Runner& runner : runners)
    {
      if (runner.doneAt)
      {
        done.push_back(&runner);
      }
    }
    std::ranges::sort(done, {}, &Runner::place);
    std::vector<TrainingRow> rows;
    for (const Runner* runner : done)
    {
      const auto index = static_cast<std::size_t>(runner - runners.data());
      const TrainingPlayer& player = squad[index];
      // The injured sort after every time.
      const double seconds =
          runner->injured ? MAX_SECONDS : *runner->doneAt - runner->startedAt;
      rows.push_back(
          {.player = index,
           .cells = {runner->injured
                         ? std::string("injured")
                         : std::format(
                               "{:.2f} s{}", seconds,
                               runner->missed > 0
                                   ? std::format(" ({} missed)", runner->missed)
                                   : std::string()),
                     std::format("{:.2f} s", runner->cornering),
                     std::format("{:.0f}", runner->agility),
                     std::format("{:.0f}", runner->acceleration),
                     std::format("{:.0f}", player.stat("Pace"))},
           .values = {seconds, runner->cornering, runner->agility,
                      runner->acceleration, player.stat("Pace")}});
    }
    return rows;
  }

 private:
  static constexpr std::size_t CONES = 8;
  static constexpr float SPACING_METRES = 5.0F;
  static constexpr float STAGGER_METRES = 1.0F;
  /** A gate starts at its cone's edge (a cone is 20 cm across). */
  static constexpr float GATE_BUFFER_METRES = DrillDraw::CONE_RADIUS_METRES;
  /** Where he aims to pass a cone: this far outside the gate's start. */
  static constexpr float LINE_METRES = 0.5F;
  /** How much of a gate is drawn (it is open beyond). */
  static constexpr float GATE_DRAWN_METRES = 2.0F;
  /** How far beyond his point round a cone his line may be pushed out to
   * widen the turn (the old 2 m gate's half-width). */
  static constexpr float SWING_ROOM_METRES = 1.0F;
  /** The start, with room behind it for the queue. */
  static inline const PitchPoint START =
      MatchPitch::matchSize().at(0.32F, 0.5F);
  static constexpr float QUEUE_GAP_METRES = 1.5F;
  /** Close enough to the start to set off. */
  static constexpr float ON_THE_START_METRES = 0.5F;
  /** Past the finish he runs on through it (as the Slalom drill's runner). */
  static constexpr float RUN_THROUGH_METRES = 8.0F;
  /** How far either side of the finish line's middle he may aim. */
  static constexpr float FINISH_AIM_METRES = 3.5F;
  /** Where the finished stand: beside the course past the finish, out of
   * the way of those still running through it. */
  static constexpr float DONE_BEYOND_METRES = 3.0F;
  static constexpr float DONE_SIDE_METRES = 6.0F;
  static constexpr float DONE_GAP_METRES = 1.5F;
  /** Close enough to a waypoint to move on (no route of his own). */
  static constexpr float WAYPOINT_RADIUS_METRES = 1.0F;
  static constexpr double MAX_SECONDS = 600.0;

  struct Runner
  {
    PlayerID id = 0;
    bool started = false;
    double startedAt = 0.0;
    PitchPoint previous;
    std::size_t gatesPassed = 0;
    /** Time spent round the outside of the cones (s). */
    double cornering = 0.0;
    int missed = 0;
    /** Going back for a missed gate (through the point behind it first). */
    bool goingBack = false;
    /** Waypoints his body has moved on from, for the plan. */
    std::uint32_t advancesSeen = 0;
    std::size_t movedOn = 0;
    /** The plan as last sent. */
    std::vector<PitchPoint> sentRoute;
    std::optional<PitchPoint> sentTarget;
    bool injured = false;
    std::optional<double> doneAt;
    std::size_t place = 0;
    float agility = 0.0F;
    float acceleration = 0.0F;
  };

  /** Side of the course cone `index` is passed on (-1 or 1). */
  [[nodiscard]] static float sideOf(std::size_t index)
  {
    return index % 2 == 0 ? -1.0F : 1.0F;
  }

  /** A point across gate `cone`, `metres` from its inner edge. */
  [[nodiscard]] PitchPoint gateEdge(std::size_t cone, float metres) const
  {
    return along(cones[cone], {.x = 0.0F, .y = 1.0F},
                 sideOf(cone) * (GATE_BUFFER_METRES + metres));
  }

  /** Share [0, 1] of the move from `from` to `to` he spends round the
   * outside of a cone: beyond the one he is due to pass, or the one he has
   * just passed, on its gate's side (crossing in or out, the share of the
   * move beyond it). Neighbouring cones are passed on opposite sides, so he
   * is outside one at most. */
  [[nodiscard]] float outsideShare(const Runner& runner, PitchPoint from,
                                   PitchPoint to) const
  {
    const std::size_t due = runner.gatesPassed;
    float share = 0.0F;
    for (std::size_t cone = due > 0 ? due - 1 : 0;
         cone <= due && cone < cones.size(); ++cone)
    {
      const float before = (from.y - cones[cone].y) * sideOf(cone);
      const float after = (to.y - cones[cone].y) * sideOf(cone);
      if (before > 0.0F && after > 0.0F)
      {
        share += 1.0F;
      }
      else if (before > 0.0F || after > 0.0F)
      {
        share += std::max(before, after) / (std::abs(before) + std::abs(after));
      }
    }
    return std::min(share, 1.0F);
  }

  /** Place `place` in the queue (0 is the start), back down the course. */
  [[nodiscard]] static PitchPoint queueSpot(std::size_t place)
  {
    return along(START, {.x = -1.0F, .y = 0.0F},
                 QUEUE_GAP_METRES * static_cast<float>(place));
  }

  /** Whether his side plans his run as a route: a Binturong or later brain
   * on a Binturong body (as Laps). */
  static bool routes(const MatchEngine& engine, const MatchPlayer& player)
  {
    return engine.getBrain(player.isHomeTeam) != BrainId::AARDVARK &&
           engine.getBody(player.isHomeTeam) == BodyId::BINTURONG;
  }

  /** The next in the queue sets off; those behind him step up a place. */
  void release(MatchEngine& engine, double now)
  {
    Runner& runner = runners[queue[released]];
    runner.started = true;
    runner.startedAt = now;
    if (const MatchPlayer* player = slotOf(engine, runner.id))
    {
      runner.previous = player->position;
      runner.advancesSeen = player->routeAdvances;
    }
    ++released;
    plan(engine, runner, true);
    for (std::size_t place = released; place < queue.size(); ++place)
    {
      engine.setDrillTarget(runners[queue[place]].id,
                            queueSpot(place - released), false);
    }
  }

  /** His line round each cone still to pass, from the one due (after a
   * point back behind it, when he is going back for it), less those his
   * body has moved on from (the Slalom drill's plan). */
  [[nodiscard]] std::vector<PitchPoint> waypointsAhead(
      const Runner& runner) const
  {
    std::vector<PitchPoint> list;
    const std::size_t due = runner.gatesPassed;
    if (runner.goingBack && due < gates.size())
    {
      list.push_back(
          along(gates[due], {.x = 1.0F, .y = 0.0F}, -SPACING_METRES * HALF));
    }
    for (std::size_t gate = due; gate < gates.size(); ++gate)
    {
      list.push_back(gates[gate]);
    }
    const std::size_t skip = std::min(runner.movedOn, list.size());
    return {list.begin() + static_cast<std::ptrdiff_t>(skip), list.end()};
  }

  /** His plan, as a brain would make it: flat out through the finish, by
   * the next two waypoints, his line through his point round each cone and
   * pushed out no more than SWING_ROOM_METRES beyond it. Without a route of his
   * own, he heads for the next waypoint. Sent only when it changes. */
  void plan(MatchEngine& engine, Runner& runner, bool force = false)
  {
    std::vector<PitchPoint> ahead = waypointsAhead(runner);
    // Straight across the finish from his line round the last cone (the
    // line counts anywhere along it), within the line as drawn.
    const float across =
        gates.empty() ? 0.0F
                      : std::clamp(gates.back().y - finish.y,
                                   -FINISH_AIM_METRES, FINISH_AIM_METRES);
    const PitchPoint end = along(along(finish, {.x = 0.0F, .y = 1.0F}, across),
                                 {.x = 1.0F, .y = 0.0F}, RUN_THROUGH_METRES);
    const MatchPlayer* player = slotOf(engine, runner.id);
    if (player == nullptr)
    {
      return;
    }
    const auto same = [](PitchPoint a, PitchPoint b)
    { return a.x == b.x && a.y == b.y; };
    if (routes(engine, *player))
    {
      if (ahead.size() > 2)
      {
        ahead.resize(2);
      }
      if (!force && runner.sentTarget && same(*runner.sentTarget, end) &&
          std::ranges::equal(runner.sentRoute, ahead, same))
      {
        return;
      }
      runner.sentTarget = end;
      runner.sentRoute = ahead;
      engine.setDrillTarget(runner.id, end, true, ahead, 0.0F,
                            SWING_ROOM_METRES, std::nullopt, true);
      return;
    }
    const PitchPoint target = ahead.empty() ? end : ahead.front();
    if (!force && runner.sentTarget && same(*runner.sentTarget, target))
    {
      return;
    }
    runner.sentTarget = target;
    runner.sentRoute.clear();
    engine.setDrillTarget(runner.id, target, true, {}, 0.0F, 0.0F, std::nullopt,
                          ahead.empty());
  }

  /** Moves his plan on as his body moves on from each waypoint. */
  void follow(MatchEngine& engine, Runner& runner, const MatchPlayer& player)
  {
    if (routes(engine, player))
    {
      if (player.routeAdvances != runner.advancesSeen)
      {
        runner.movedOn += player.routeAdvances - runner.advancesSeen;
        runner.advancesSeen = player.routeAdvances;
      }
    }
    else
    {
      const std::vector<PitchPoint> ahead = waypointsAhead(runner);
      if (!ahead.empty() && metresBetween(player.position, ahead.front()) <=
                                WAYPOINT_RADIUS_METRES)
      {
        ++runner.movedOn;
      }
    }
    plan(engine, runner);
  }

  /** Across the line of the cone due, forwards: on its side (passed), or
   * the wrong side (missed: he goes back for it). Then the finish line. */
  void passGates(MatchEngine& engine, Runner& runner, const MatchPlayer& player,
                 double now)
  {
    const std::size_t due = runner.gatesPassed;
    if (due < cones.size())
    {
      const PitchPoint cone = cones[due];
      const auto share = crossingShare(runner.previous, player.position, cone,
                                       {.x = 1.0F, .y = 0.0F});
      if (!share)
      {
        return;
      }
      const float y = runner.previous.y +
                      ((player.position.y - runner.previous.y) * *share);
      const float out = (y - cone.y) * sideOf(due);
      // What his body had moved on from includes this gate (and the point
      // behind it, going back), now behind him either way.
      const std::size_t behind = runner.goingBack ? 2 : 1;
      if (out >= GATE_BUFFER_METRES)
      {
        ++runner.gatesPassed;
        runner.movedOn = runner.movedOn > behind ? runner.movedOn - behind : 0;
        runner.goingBack = false;
      }
      else
      {
        ++runner.missed;
        runner.goingBack = true;
        runner.movedOn = 0;
      }
      plan(engine, runner);
      return;
    }
    if (const auto share = crossingShare(runner.previous, player.position,
                                         finish, {.x = 1.0F, .y = 0.0F}))
    {
      finishRunner(engine, runner,
                   now - STEP + (STEP * static_cast<double>(*share)));
    }
  }

  /** Done (through the finish, or hurt): he walks to his place beside the
   * course past the finish, alternate sides. */
  void finishRunner(MatchEngine& engine, Runner& runner, double at)
  {
    runner.doneAt = at;
    runner.place = ++doneCount;
    lastDone = at;
    const std::size_t place = runner.place - 1;
    const float side = place % 2 == 0 ? 1.0F : -1.0F;
    // Two places to a row, one either side.
    const std::size_t row = place / 2;
    const PitchPoint spot = along(
        along(finish, {.x = 1.0F, .y = 0.0F}, DONE_BEYOND_METRES),
        {.x = 0.0F, .y = 1.0F},
        side * (DONE_SIDE_METRES + DONE_GAP_METRES * static_cast<float>(row)));
    engine.setDrillTarget(runner.id, spot, false);
  }

  const TrainingDrill* sprint = nullptr;
  std::vector<PitchPoint> cones;
  std::vector<PitchPoint> gates;
  PitchPoint finish;
  /** Squad indices in running order. */
  std::vector<std::size_t> queue;
  /** Runners by squad index. */
  std::vector<Runner> runners;
  /** How many of the queue have set off. */
  std::size_t released = 0;
  std::size_t doneCount = 0;
  std::optional<double> lastDone;
};

// --- Laps to exhaustion ----------------------------------------------------

/**
 * Laps: four cones a few metres in from the corner flags; the squad runs
 * laps round them, flat out, until each player's fuel (condition) hits its
 * floor. Players start spread round the track; each one who is spent walks
 * off it towards the centre circle.
 */
class LapsDrill final : public TrainingDrill
{
 public:
  LapsDrill(const std::vector<TrainingPlayer>& squad_ref,
            const TrainingDrill* sprint_ref)
      : TrainingDrill(squad_ref), sprint(sprint_ref)
  {
  }

  [[nodiscard]] const char* name() const override
  {
    return "Laps to exhaustion";
  }
  [[nodiscard]] const char* summary() const override
  {
    return "Four cones a few metres in from the corner flags. The squad runs "
           "laps round them, flat out, until each player's condition hits its "
           "floor. As each one drops out: how far he ran, his average speed "
           "and what share of his top speed (from Across and back) that was.";
  }

  void begin(MatchEngine& engine, const std::vector<PlayerID>& ids) override
  {
    // The cones in the order the runners go round them, then the line they
    // take round each, a metre outside it.
    const float x = CONE_INSET_METRES / LENGTH;
    const float y = CONE_INSET_METRES / WIDTH;
    cones = {PITCH.at(x, y), PITCH.at(x, 1.0F - y),
             PITCH.at(1.0F - x, 1.0F - y), PITCH.at(1.0F - x, y)};
    const std::array<PitchPoint, 4> FLAGS{
        PITCH.at(0.0F, 0.0F), PITCH.at(0.0F, 1.0F), PITCH.at(1.0F, 1.0F),
        PITCH.at(1.0F, 0.0F)};
    lapMetres = 0.0F;
    for (std::size_t corner = 0; corner < 4; ++corner)
    {
      corners.at(corner) = along(
          cones.at(corner),
          directionBetween(cones.at(corner), FLAGS.at(corner)), OUTSIDE_METRES);
    }
    for (std::size_t corner = 0; corner < 4; ++corner)
    {
      lapMetres +=
          metresBetween(corners.at(corner), corners.at((corner + 1) % 4));
    }

    runners.clear();
    doneCount = 0;
    lastDone.reset();
    const std::size_t count = std::min(ids.size(), squad.size());
    MatchScenario scenario;
    for (std::size_t index = 0; index < count; ++index)
    {
      // Spread evenly round the track.
      float metres =
          lapMetres * static_cast<float>(index) / static_cast<float>(count);
      std::size_t leg = 0;
      while (leg < 3 &&
             metres > metresBetween(corners.at(leg), corners.at(leg + 1)))
      {
        metres -= metresBetween(corners.at(leg), corners.at(leg + 1));
        ++leg;
      }
      const std::size_t next = (leg + 1) % 4;
      Runner runner;
      runner.id = ids[index];
      runner.next = next;
      runner.previous =
          along(corners.at(leg),
                directionBetween(corners.at(leg), corners.at(next)), metres);
      // The fallback for anyone without a time in the sprint: his engine top
      // speed when fresh.
      runner.topSpeed =
          (sprint != nullptr) ? sprint->topSpeed(index) : std::nullopt;
      runners.push_back(runner);
      scenario.players.push_back({.playerId = runner.id,
                                  .position = runner.previous,
                                  .makingRun = false,
                                  .facing = std::nullopt});
    }
    // On the centre spot, well inside the track.
    scenario.ballPosition = PITCH.at(HALFWAY, HALFWAY);
    engine.startDrill(scenario);
    for (Runner& runner : runners)
    {
      const MatchPlayer* player = slotOf(engine, runner.id);
      if (!runner.topSpeed && (player != nullptr))
      {
        runner.topSpeed = player->maxSpeed;
      }
      if ((player != nullptr) && routes(engine, *player))
      {
        planLap(engine, runner);
      }
      else
      {
        engine.setDrillTarget(runner.id, corners.at(runner.next), true);
      }
    }
  }

  bool afterStep(MatchEngine& engine) override
  {
    const double now = secondsOf(engine);
    for (Runner& runner : runners)
    {
      if (runner.doneAt)
      {
        continue;
      }
      const MatchPlayer* player = slotOf(engine, runner.id);
      if (player == nullptr)
      {
        continue;
      }
      const PitchPoint at = player->position;
      runner.metres += metresBetween(runner.previous, at);
      const PitchPoint from = runner.previous;
      runner.previous = at;
      if (routes(engine, *player) && passedCorner(runner, from, at))
      {
        // Round the corner (across the line from its cone to its flag),
        // whatever his route says: the plan moves on, as the slalom's
        // gates move its plan on.
        runner.next = (runner.next + 1) % 4;
        planLap(engine, runner);
      }
      else if (routes(engine, *player))
      {
        followRoute(engine, runner, *player);
      }
      // Round the cone: on to the next once he is close to this one.
      else if (metresBetween(at, corners.at(runner.next)) <=
               CORNER_RADIUS_METRES)
      {
        runner.next = (runner.next + 1) % 4;
        runner.passing = false;
        engine.setDrillTarget(runner.id, corners.at(runner.next), true);
      }
      else
      {
        // The drill's own way past, for a side without the Binturong
        // brain's overtakes (movement targets plan 4a).
        steerPast(engine, runner, at);
      }
      // Spent, or hurt (he stops there, short of his best).
      runner.injured = player->isInjured;
      if (runner.injured ||
          player->stamina <=
              engine.fuelFloor(player->isHomeTeam) + FUEL_FLOOR_TOLERANCE)
      {
        runner.doneAt = now;
        runner.place = ++doneCount;
        lastDone = now;
        // Off the track, at a walk.
        engine.setDrillTarget(
            runner.id,
            along(at, directionBetween(at, PITCH.at(HALFWAY, HALFWAY)),
                  OFF_TRACK_METRES),
            false);
      }
    }
    const bool allDone = doneCount >= runners.size();
    return (!allDone || !lastDone || now <= *lastDone + 1.0) &&
           now < MAX_SECONDS;
  }

  void drawOverlay(const IMatchRenderer& renderer,
                   ImDrawList& draw) const override
  {
    const float scale = Theme::scale();
    for (std::size_t corner = 0; corner < 4; ++corner)
    {
      DrillDraw::line(renderer, draw, corners.at(corner),
                      corners.at((corner + 1) % 4), LINE,
                      LINE_THICKNESS * scale);
    }
    for (const PitchPoint cone : cones)
    {
      DrillDraw::cone(renderer, draw, cone, CONE);
    }
  }

  [[nodiscard]] std::vector<std::string> report() const override
  {
    double furthest = 0.0;
    for (const Runner& runner : runners)
    {
      if (!runner.doneAt)
      {
        furthest = std::max(furthest, static_cast<double>(runner.metres));
      }
    }
    std::vector<std::string> lines;
    lines.push_back(std::format("Lap {:.0f} m · {} of {} still running",
                                lapMetres, runners.size() - doneCount,
                                runners.size()));
    if (doneCount < runners.size())
    {
      lines.push_back(
          std::format("Leader still running: {:.0f} m ({:.1f} laps)", furthest,
                      furthest / lapMetres));
    }
    const auto injured = std::ranges::count_if(
        runners, [](const Runner& runner) { return runner.injured; });
    if (injured > 0)
    {
      lines.push_back(
          std::format("Injured: {} (stopped where they got hurt)", injured));
    }
    return lines;
  }

  [[nodiscard]] std::vector<TrainingColumn> columns() const override
  {
    return {{.name = "Distance",
             .tooltip = "How far he ran before his condition hit its floor."},
            {.name = "Time", .tooltip = "How long he kept going."},
            {.name = "Stamina",
             .tooltip = "The Stamina attribute: endurance and fuel."},
            {.name = "Avg speed",
             .tooltip = "Distance over time, in miles an hour."},
            {.name = "% of top",
             .tooltip = "His average speed as a share of his top speed in "
                        "Across and back (his engine top speed if he has no "
                        "time there)."},
            {.name = "Pace", .tooltip = "The Pace attribute: his top speed."}};
  }

  [[nodiscard]] std::vector<TrainingRow> results() const override
  {
    std::vector<const Runner*> done;
    for (const Runner& runner : runners)
    {
      if (runner.doneAt)
      {
        done.push_back(&runner);
      }
    }
    std::ranges::sort(done, {}, &Runner::place);
    std::vector<TrainingRow> rows;
    for (const Runner* runner : done)
    {
      const auto index = static_cast<std::size_t>(runner - runners.data());
      const TrainingPlayer& player = squad[index];
      const double seconds = std::max(*runner->doneAt, 1e-6);
      const double average = static_cast<double>(runner->metres) / seconds;
      const double share =
          runner->topSpeed && *runner->topSpeed > 0.0F
              ? average / static_cast<double>(*runner->topSpeed) * 100.0
              : 0.0;
      const double mph = average * MPH_PER_METRE_PER_SECOND;
      rows.push_back(
          {.player = index,
           .cells = {std::format("{:.0f} m{}", runner->metres,
                                 runner->injured ? " (injured)" : ""),
                     clock(seconds),
                     std::format("{:.0f}", player.stat("Stamina")),
                     std::format("{:.1f} mph", mph),
                     std::format("{:.0f}%", share),
                     std::format("{:.0f}", player.stat("Pace"))},
           .values = {runner->metres, seconds, player.stat("Stamina"), mph,
                      share, player.stat("Pace")}});
    }
    return rows;
  }

 private:
  /** The cones' distance in from the touchline and the goal line. */
  static constexpr float CONE_INSET_METRES = 9.0F;
  /** The runners' line round a cone, this far outside it. */
  static constexpr float OUTSIDE_METRES = 1.0F;
  /** Close enough to a corner to head for the next. */
  static constexpr float CORNER_RADIUS_METRES = 3.0F;
  /** How far out a routed runner's line may be pushed round a corner (a
   * design choice: room on the track outside the cones). */
  static constexpr float LAP_OUTWARD_ROOM_METRES = 2.0F;
  /** Someone this close ahead, this near his line, blocks a runner. */
  static constexpr float BLOCKED_AHEAD_METRES = 3.0F;
  static constexpr float BLOCKED_SIDE_METRES = 1.0F;
  /** Going past: the point he aims at, ahead and out wide. */
  static constexpr float PASS_LEAD_METRES = 6.0F;
  static constexpr float PASS_WIDE_METRES = 1.5F;
  static constexpr int PASS_REFRESH_STEPS = 5;
  /** Three hours: anyone not spent by then never will be. */
  static constexpr double MAX_SECONDS = 3.0 * 3600.0;

  struct Runner
  {
    PlayerID id = 0;
    std::size_t next = 0;
    /** Out wide of the racing line, going past someone. */
    bool passing = false;
    int passSteps = 0;
    bool injured = false;
    PitchPoint previous;
    float metres = 0.0F;
    std::optional<float> topSpeed;
    std::optional<double> doneAt;
    std::size_t place = 0;
  };

  /** As his body moves on from a corner (heading for a later one: his next
   * waypoint, or with none left his target), the plan moves on one corner,
   * so he never skips one (a way round someone ahead can push the second
   * out of his route for a moment). A way round is not a corner. */
  void followRoute(MatchEngine& engine, Runner& runner,
                   const MatchPlayer& player) const
  {
    const PitchPoint heading = player.routeWaypoints > 0
                                   ? player.movementTarget
                                   : player.tacticalTarget;
    for (std::size_t ahead = 1; ahead <= 2; ++ahead)
    {
      const PitchPoint corner = corners.at((runner.next + ahead) % 4);
      if (heading.x == corner.x && heading.y == corner.y)
      {
        runner.next = (runner.next + 1) % 4;
        planLap(engine, runner);
        return;
      }
    }
  }

  /** Whether his side plans his laps as a route (and overtakes): a Binturong
   * or later brain on a Binturong body (movement targets plan 4a). */
  static bool routes(const MatchEngine& engine, const MatchPlayer& player)
  {
    return engine.getBrain(player.isHomeTeam) != BrainId::AARDVARK &&
           engine.getBody(player.isHomeTeam) == BodyId::BINTURONG;
  }

  /** His plan, as a brain would make it: round the next two corners as
   * waypoints, on to the one after; his line through each corner point (a
   * metre outside its cone), with room outside to widen the turn. He runs
   * on through that last one (it is not where he stops). */
  /** Whether his move from `from` to `to` took him round corner
   * `runner.next`: across the line from its cone out to its flag, going
   * on (the way through the corner, between its legs). */
  [[nodiscard]] bool passedCorner(const Runner& runner, PitchPoint from,
                                  PitchPoint to) const
  {
    const std::size_t corner = runner.next;
    const Vector2F in =
        directionBetween(corners.at((corner + 3) % 4), corners.at(corner));
    const Vector2F out =
        directionBetween(corners.at(corner), corners.at((corner + 1) % 4));
    const float size = std::hypot(in.x + out.x, in.y + out.y);
    if (size < NO_LENGTH)
    {
      return false;
    }
    const Vector2F way{.x = (in.x + out.x) / size, .y = (in.y + out.y) / size};
    return crossingShare(from, to, cones.at(corner), way).has_value();
  }

  void planLap(MatchEngine& engine, const Runner& runner) const
  {
    engine.setDrillTarget(
        runner.id, corners.at((runner.next + 2) % 4), true,
        {corners.at(runner.next), corners.at((runner.next + 1) % 4)}, 0.0F,
        LAP_OUTWARD_ROOM_METRES, std::nullopt, true);
  }

  /**
   * Players do not steer round each other, they bump: a runner who catches
   * another on the racing line would sit behind him. So one who has someone
   * just ahead of him on his line swings wide and goes past (aiming at a
   * point ahead, out wide), then heads back for the next cone.
   */
  void steerPast(MatchEngine& engine, Runner& runner, PitchPoint at)
  {
    const PitchPoint from = corners.at((runner.next + 3) % 4);
    const PitchPoint to = corners.at(runner.next);
    const Vector2F ahead = directionBetween(from, to);
    // Out wide: square to the leg, away from the middle of the pitch.
    Vector2F wide{.x = -ahead.y, .y = ahead.x};
    const Vector2F middle{.x = ((from.x + to.x) * 0.5F) - PITCH.atX(0.5F),
                          .y = ((from.y + to.y) * 0.5F) - PITCH.atY(0.5F)};
    if (wide.x * middle.x + wide.y * middle.y < 0.0F)
    {
      wide = {.x = -wide.x, .y = -wide.y};
    }
    bool blocked = false;
    for (const Runner& other : runners)
    {
      if (&other == &runner || other.doneAt)
      {
        continue;
      }
      const float x = other.previous.x - at.x;
      const float y = other.previous.y - at.y;
      const float front = (x * ahead.x) + (y * ahead.y);
      const float side = (x * wide.x) + (y * wide.y);
      if (front > 0.0F && front < BLOCKED_AHEAD_METRES &&
          std::abs(side) < BLOCKED_SIDE_METRES)
      {
        blocked = true;
      }
    }
    const float left = metresBetween(at, to);
    if (blocked && left > PASS_LEAD_METRES + CORNER_RADIUS_METRES)
    {
      // Refresh the point ahead now and then, not every step.
      if (runner.passing && ++runner.passSteps < PASS_REFRESH_STEPS)
      {
        return;
      }
      const float alongLeg =
          ((at.x - from.x) * ahead.x) + ((at.y - from.y) * ahead.y);
      engine.setDrillTarget(
          runner.id,
          along(along(from, ahead, alongLeg + PASS_LEAD_METRES), wide,
                PASS_WIDE_METRES),
          true);
      runner.passing = true;
      runner.passSteps = 0;
    }
    else if (runner.passing && !blocked)
    {
      runner.passing = false;
      engine.setDrillTarget(runner.id, to, true);
    }
  }

  const TrainingDrill* sprint = nullptr;
  std::array<PitchPoint, 4> cones{};
  std::array<PitchPoint, 4> corners{};
  float lapMetres = 1.0F;
  std::vector<Runner> runners;
  std::size_t doneCount = 0;
  std::optional<double> lastDone;
};

std::unique_ptr<TrainingSession> makeRunningSession(
    std::vector<TrainingPlayer> squad)
{
  auto session = std::make_unique<TrainingSession>();
  session->name = "Running";
  session->summary =
      "Three drills: a sprint across the pitch and back, a slalom, then laps "
      "until the tank is empty.";
  session->squad = std::move(squad);
  if (session->squad.size() > TrainingDrill::MAX_PLAYERS)
  {
    session->squad.resize(TrainingDrill::MAX_PLAYERS);
  }
  auto sprint = std::make_unique<AcrossAndBackDrill>(session->squad);
  auto slalom = std::make_unique<SlalomDrill>(session->squad, sprint.get());
  auto laps = std::make_unique<LapsDrill>(session->squad, sprint.get());
  session->drills.push_back(std::move(sprint));
  session->drills.push_back(std::move(slalom));
  session->drills.push_back(std::move(laps));
  return session;
}
}  // namespace

float TrainingPlayer::stat(const std::string& stat) const
{
  const auto found = stats.find(stat);
  return found == stats.end() ? 0.0F : found->second;
}

std::vector<DrillPlayerSpec> TrainingDrill::players() const
{
  std::vector<DrillPlayerSpec> specs;
  for (std::size_t index = 0; index < squad.size() && index < MAX_PLAYERS;
       ++index)
  {
    const TrainingPlayer& player = squad[index];
    DrillPlayerSpec spec;
    spec.name = player.name;
    spec.role = player.role == PlayerRole::GK ? PlayerRole::CM : player.role;
    spec.homeTeam = index % 2 == 0;
    spec.stats = player.stats;
    spec.height = player.height;
    spec.foot = player.foot;
    specs.push_back(std::move(spec));
  }
  return specs;
}

std::unique_ptr<DrillRun> startTrainingRun(TrainingDrill& drill,
                                           const StatsConfig& config,
                                           BrainId brain, BodyId body)
{
  // The headless laps diagnostic deliberately compares a uniform squad.
  // Restore selections even if constructing its run throws.
  struct RestoreVersions
  {
    std::array<BrainId, 2> brains = sandboxBrains();
    std::array<BodyId, 2> bodies = sandboxBodies();
    RestoreVersions() = default;
    RestoreVersions(const RestoreVersions&) = delete;
    RestoreVersions& operator=(const RestoreVersions&) = delete;
    RestoreVersions(RestoreVersions&&) = delete;
    RestoreVersions& operator=(RestoreVersions&&) = delete;
    ~RestoreVersions()
    {
      sandboxBrains() = brains;
      sandboxBodies() = bodies;
    }
  } restore;
  sandboxBrains() = {brain, brain};
  sandboxBodies() = {body, body};
  return startTrainingRun(drill, config);
}

std::unique_ptr<DrillRun> startTrainingRun(TrainingDrill& drill,
                                           const StatsConfig& config)
{
  auto run = std::make_unique<DrillRun>(drill, config, false);
  // Laps outlast a half: no half-time rest or full time to cut them short.
  // And everyone runs as hard as a lone drill runner does, not at the pace
  // of whatever job his side's brain hands him.
  MatchEngine::DrillRules rules = run->engine().getDrillRules();
  rules.periods = false;
  rules.chaseEffort = true;
  run->engine().setDrillRules(rules);
  return run;
}

const std::vector<TrainingSessionInfo>& trainingSessions()
{
  static const std::vector<TrainingSessionInfo> sessions{
      {.name = "Running",
       .summary =
           "Three drills for the whole squad. Across and back: a lane each, "
           "touchline to touchline and back, timed, with top speed and "
           "acceleration. Slalom: one at a time through 8 cones, fastest "
           "first, "
           "timed, with time spent cornering. Laps to exhaustion: round four "
           "cones near the corner flags until each player's condition hits its "
           "floor.",
       .make = &makeRunningSession}};
  return sessions;
}

// --- Laps trace (diagnostic) -----------------------------------------------

namespace
{
std::string tracedName(const MatchPlayer& player)
{
  return (player.player != nullptr) ? player.player->getName()
                                    : std::string("?");
}

/** How far a body facing `facingAngle` faces off moving along `along`
 * (degrees). */
float degreesOffLine(Vector2F along, float facingAngle)
{
  using MatchEngineInternal::length;
  const Vector2F facing{.x = std::cos(facingAngle), .y = std::sin(facingAngle)};
  return std::acos(std::clamp(
             (along.x * facing.x + along.y * facing.y) /
                 std::max(length(along) * length(facing), MIN_DIRECTION_LENGTH),
             -1.0F, 1.0F)) *
         DEGREES_PER_HALF_TURN / std::numbers::pi_v<float>;
}

/** Where he is and what he is doing, for a trace line. */
std::string tracedState(const MatchPlayer& player)
{
  using MatchEngineInternal::length;
  const PitchPoint at = player.position;
  const auto metres = [&](PitchPoint to)
  { return std::hypot(to.x - at.x, to.y - at.y); };
  return std::format(
      "at ({:.1f},{:.1f}) m speed {:.2f} · route {} next {:.1f} m then "
      "{:.1f} m · "
      "target {:.1f} m · pause {:.2f} stumble {:.2f} fallen {} · stamina "
      "{:.2f}",
      at.x, at.y, length(player.velocity), player.routeWaypoints,
      metres(player.movementTarget), metres(player.nextTarget),
      metres(player.tacticalTarget), player.collisionPause,
      player.stumbleSeconds, player.fallen, player.stamina);
}

/** One side of a run-in: his speed before and after, how far his body faced
 * off his line of travel before it, and how the knock was judged. */
std::string knockDetail(const MatchPlayer& player,
                        const MatchEngine::CollisionSide& entry)
{
  using MatchEngineInternal::length;
  const Vector2F before{.x = entry.after.x - entry.jolt.x,
                        .y = entry.after.y - entry.jolt.y};
  const float off = degreesOffLine(before, player.facingAngle);
  return std::format(
      "{} {:.1f}->{:.1f} m/s, facing {:.0f} deg off, ends up {:.0f}% "
      "sudden {:.0f}% "
      "score {:.0f}% (lines {:.0f}/{:.0f})",
      tracedName(player), length(before), length(entry.after), off,
      entry.endsUp * 100.0F, entry.sudden * 100.0F, entry.score * 100.0F,
      entry.stumbleLine * 100.0F, entry.fallLine * 100.0F);
}

/** Follows a laps drill step by step, printing its run-ins, overtakes and
 * slow-downs (and one player's route, if asked). */
class LapsTrace
{
 public:
  LapsTrace(const MatchEngine& engine, std::string followed)
      : players(&engine.getPlayers()),
        lastCollisions(&engine),
        overtaking(engine.getPlayers().size(), -1),
        slow(engine.getPlayers().size(), false),
        follow(std::move(followed))
  {
  }

  void step(double now)
  {
    clock = now;
    for (const MatchEngine::CollisionHit& hit :
         lastCollisions->getLastCollisions())
    {
      reportRunIn(hit, now);
    }
    for (std::size_t index = 0; index < players->size(); ++index)
    {
      if (MatchEngineInternal::active((*players)[index]))
      {
        trackPlayer(index);
      }
    }
  }

  /** How far bodies face off their line of travel, running (over 3 m/s). */
  void summary() const
  {
    if (movingCount == 0)
    {
      return;
    }
    std::println(
        "Running over 3 m/s: facing off his line >10 deg {:.1f}%, >20 {:.1f}%, "
        ">45 {:.1f}%, >90 {:.1f}%; worst {:.0f} deg",
        100.0 * offCounts[0] / movingCount, 100.0 * offCounts[1] / movingCount,
        100.0 * offCounts[2] / movingCount, 100.0 * offCounts[3] / movingCount,
        worstOff);
  }

 private:
  void reportRunIn(const MatchEngine::CollisionHit& hit, double now) const
  {
    using MatchEngineInternal::length;
    std::array<std::string, 2> who;
    std::array<std::string, 2> detail;
    for (std::size_t side = 0; side < 2; ++side)
    {
      for (const MatchPlayer& player : *players)
      {
        if ((player.player != nullptr) &&
            player.player->getId() == hit.sides.at(side).player)
        {
          who.at(side) = tracedName(player);
          detail.at(side) = knockDetail(player, hit.sides.at(side));
        }
      }
    }
    std::println(
        "{:6.1f}  RUN-IN {} / {}  jolt {:.2f} / {:.2f}  outcome {} / {}", now,
        who[0], who[1], length(hit.sides[0].jolt), length(hit.sides[1].jolt),
        hit.sides[0].outcome, hit.sides[1].outcome);
    for (std::size_t side = 0; side < 2; ++side)
    {
      if (hit.sides.at(side).outcome > 0)
      {
        std::println("          knocked: {}", detail.at(side));
      }
    }
  }

  void trackPlayer(std::size_t index)
  {
    const double now = clock;
    using MatchEngineInternal::length;
    const MatchPlayer& player = (*players)[index];
    const float speed = length(player.velocity);
    if (speed > TRACE_MOVING_SPEED)
    {
      countFacing(degreesOffLine(player.velocity, player.facingAngle));
    }
    // The player followed: his route every half second.
    if (!follow.empty() &&
        tracedName(player).find(follow) != std::string::npos &&
        std::fmod(now + CLOCK_TOLERANCE, FOLLOW_EVERY_SECONDS) <
            MatchTuning::Timing::FIXED_STEP_SECONDS)
    {
      printFollowed(player, now);
    }
    if (player.overtaking != overtaking[index])
    {
      reportOvertake(player, now);
      overtaking[index] = player.overtaking;
    }
    const bool nowSlow = now > 4.0 && speed < 2.0F && player.stamina > 0.05F;
    if (nowSlow != slow[index])
    {
      std::println("{:6.1f}  {} {}  {}", now, tracedName(player),
                   nowSlow ? "SLOWS" : "going again", tracedState(player));
      slow[index] = nowSlow;
    }
  }

  void countFacing(float off)
  {
    ++movingCount;
    worstOff = std::max(worstOff, off);
    for (std::size_t band = 0; band < OFF_FACING_DEGREES.size(); ++band)
    {
      if (off > OFF_FACING_DEGREES.at(band))
      {
        ++offCounts.at(band);
      }
    }
  }

  void printFollowed(const MatchPlayer& player, double now) const
  {
    std::println(
        "{:6.1f}  [{}] vel ({:.2f},{:.2f}) movT ({:.1f},{:.1f}) nextT "
        "({:.1f},{:.1f}) tacT ({:.1f},{:.1f}) wp {} apex {:.2f} room "
        "{:.2f} intent {} ovt {} {}",
        now, follow, player.velocity.x, player.velocity.y,
        player.movementTarget.x, player.movementTarget.y, player.nextTarget.x,
        player.nextTarget.y, player.tacticalTarget.x, player.tacticalTarget.y,
        player.routeWaypoints, player.routeApexOffset, player.routeOutwardRoom,
        static_cast<int>(player.intent), player.overtaking,
        tracedState(player));
  }

  void reportOvertake(const MatchPlayer& player, double now) const
  {
    using MatchEngineInternal::length;
    if (player.overtaking < 0)
    {
      std::println("{:6.1f}  {} overtake off  {}", now, tracedName(player),
                   tracedState(player));
      return;
    }
    const MatchPlayer& leader =
        (*players)[static_cast<std::size_t>(player.overtaking)];
    std::println("{:6.1f}  {} OVERTAKES {} (side {:+}, leader {:.2f} m/s) {}",
                 now, tracedName(player), tracedName(leader),
                 player.overtakeSide, length(leader.velocity),
                 tracedState(player));
  }

  const std::vector<MatchPlayer>* players;
  const MatchEngine* lastCollisions;
  std::vector<std::int8_t> overtaking;
  std::vector<bool> slow;
  std::string follow;
  std::array<int, 4> offCounts{};
  int movingCount = 0;
  float worstOff = 0.0F;
  /** The drill clock at this step (s). */
  double clock = 0.0;
};
}  // namespace

int runLapsTrace(const std::vector<TrainingPlayer>& squad,
                 const StatsConfig& config, double seconds,
                 const std::string& follow)
{
  std::unique_ptr<TrainingSession> session = trainingSessions()[0].make(squad);
  TrainingDrill& laps = *session->drills[2];
  std::unique_ptr<DrillRun> run =
      startTrainingRun(laps, config, BrainId::BINTURONG, BodyId::BINTURONG);
  LapsTrace trace(run->engine(), follow);
  const double step = MatchTuning::Timing::FIXED_STEP_SECONDS;
  double now = 0.0;
  while (now < seconds && run->step())
  {
    now += step;
    trace.step(now);
  }
  trace.summary();
  return 0;
}

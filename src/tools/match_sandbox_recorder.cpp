// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_recorder.h"

#include "model/player.h"

namespace
{
constexpr std::size_t HOME = 0;
constexpr std::size_t AWAY = 1;

std::size_t sideOf(bool homeTeam) { return homeTeam ? HOME : AWAY; }

PlayerID idOf(const Player* player)
{
  return (player != nullptr) ? player->getId() : 0;
}
}  // namespace

void SandboxRecorder::append(const MatchEngine& engine, Payload payload)
{
  log.push_back({.tick = engine.getSimulatedSteps(),
                 .minute = engine.getMatchTimeMinutes(),
                 .payload = std::move(payload)});
}

void SandboxRecorder::learnPlayers(const MatchEngine& engine)
{
  for (const MatchPlayer& slot : engine.getPlayers())
  {
    if ((slot.player != nullptr) && !names.contains(slot.player->getId()))
    {
      names.emplace(slot.player->getId(), slot.player->getName());
    }
  }
}

std::string SandboxRecorder::playerName(PlayerID id) const
{
  if (id == 0)
  {
    return "-";
  }
  const auto found = names.find(id);
  return found != names.end() ? found->second : "#" + std::to_string(id);
}

SandboxRecorder::Job SandboxRecorder::jobOf(PlayerID player,
                                            bool homeTeam) const
{
  const auto& held = jobs[sideOf(homeTeam)];
  for (std::size_t job = 1; job < held.size(); ++job)
  {
    if (held[job] == player)
    {
      return static_cast<Job>(job);
    }
  }
  return Job::NONE;
}

std::size_t SandboxRecorder::approximateBytes() const
{
  std::size_t bytes = (log.capacity() * sizeof(Entry)) +
                      (matchEvents.capacity() * sizeof(MatchEvent)) +
                      (checksums.capacity() * sizeof(std::uint64_t)) +
                      (inputLog.capacity() * sizeof(MatchInputRecord));
  for (const MatchEvent& event : matchEvents)
  {
    bytes += event.description.size();
  }
  return bytes;
}

void SandboxRecorder::onTeamPlan(const MatchEngine& engine,
                                 const MatchTeamPlan& plan)
{
  const std::scoped_lock guard(mutex);
  std::array<PlayerID, JOB_COUNT> next{};
  std::array<float, JOB_COUNT> scores{};
  const auto put = [&](Job job, PlayerID player, float score)
  {
    next[static_cast<std::size_t>(job)] = player;
    scores[static_cast<std::size_t>(job)] = score;
  };
  put(Job::PRESSER_1, plan.pressers[0], plan.pressArrivalSeconds[0]);
  put(Job::PRESSER_2, plan.pressers[1], plan.pressArrivalSeconds[1]);
  constexpr std::array<Job, 3> RUNNERS{Job::RUNNER_1, Job::RUNNER_2,
                                       Job::RUNNER_3};
  for (std::size_t index = 0;
       index < RUNNERS.size() && index < plan.runners.size(); ++index)
  {
    put(RUNNERS[index], plan.runners[index], plan.runPriority[index]);
  }
  put(Job::MIDFIELD_ARRIVAL, plan.midfieldArrival, 0.0F);
  put(Job::FAR_POST, plan.farPostRunner, 0.0F);
  put(Job::OVERLAP, plan.overlappingFullback, 0.0F);
  constexpr std::array<Job, 3> SUPPORTERS{Job::SUPPORTER_1, Job::SUPPORTER_2,
                                          Job::SUPPORTER_3};
  for (std::size_t index = 0;
       index < SUPPORTERS.size() && index < plan.supporters.size(); ++index)
  {
    put(SUPPORTERS[index], plan.supporters[index], 0.0F);
  }
  put(Job::COVER_OUTLET, plan.coverOutlet, 0.0F);

  auto& held = jobs[sideOf(plan.homeTeam)];
  for (std::size_t job = 1; job < JOB_COUNT; ++job)
  {
    if (next[job] == held[job])
    {
      continue;
    }
    append(engine, JobChange{.homeTeam = plan.homeTeam,
                             .job = static_cast<Job>(job),
                             .player = next[job],
                             .previous = held[job],
                             .score = scores[job]});
    held[job] = next[job];
  }
}

void SandboxRecorder::onDecision(const MatchEngine& engine,
                                 const MatchDecisionRecord& decision)
{
  const std::scoped_lock guard(mutex);
  append(engine, decision);
}

void SandboxRecorder::onAction(const MatchEngine& engine,
                               const MatchActionRecord& action)
{
  const std::scoped_lock guard(mutex);
  append(engine, action);
  lastAction = log.size() - 1;
}

void SandboxRecorder::onStepEnd(const MatchEngine& engine)
{
  const std::scoped_lock guard(mutex);
  tick = engine.getSimulatedSteps();
  learnPlayers(engine);

  const std::array<TeamPhase, 2> nowPhases{engine.getHomePhase(),
                                           engine.getAwayPhase()};
  const std::array<GoalkeeperState, 2> nowKeepers{
      engine.getHomeGoalkeeperState(), engine.getAwayGoalkeeperState()};
  if (!started)
  {
    // The kick-off state is the baseline, not a change.
    started = true;
    phases = nowPhases;
    keepers = nowKeepers;
    for (const MatchPlayer& slot : engine.getPlayers())
    {
      if (slot.player != nullptr)
      {
        intents[slot.player->getId()] = slot.intent;
      }
    }
  }

  for (std::size_t side = HOME; side <= AWAY; ++side)
  {
    if (nowPhases[side] != phases[side])
    {
      append(engine, PhaseChange{.homeTeam = side == HOME,
                                 .from = phases[side],
                                 .to = nowPhases[side]});
      phases[side] = nowPhases[side];
    }
    if (nowKeepers[side] != keepers[side])
    {
      append(engine, KeeperChange{.homeTeam = side == HOME,
                                  .from = keepers[side],
                                  .to = nowKeepers[side]});
      keepers[side] = nowKeepers[side];
    }
  }

  for (const MatchPlayer& slot : engine.getPlayers())
  {
    if ((slot.player == nullptr) || !slot.onPitch)
    {
      continue;
    }
    const PlayerID id = slot.player->getId();
    const auto known = intents.find(id);
    const PlayerIntent previous =
        known != intents.end() ? known->second : PlayerIntent::HOLD_SHAPE;
    if (known != intents.end() && previous == slot.intent)
    {
      continue;
    }
    append(engine, IntentChange{.player = id,
                                .homeTeam = slot.isHomeTeam,
                                .from = previous,
                                .to = slot.intent,
                                .target = slot.tacticalTarget,
                                .job = jobOf(id, slot.isHomeTeam)});
    intents[id] = slot.intent;
  }

  // Possession: only a new owner is a change (a kicked ball is in flight).
  if (const Player* owner = engine.getPossessionAttachment();
      (owner != nullptr) && owner != possessor)
  {
    bool ownerHome = true;
    for (const MatchPlayer& slot : engine.getPlayers())
    {
      if (slot.player == owner)
      {
        ownerHome = slot.isHomeTeam;
      }
    }
    append(engine, PossessionChange{.from = idOf(possessor),
                                    .to = owner->getId(),
                                    .fromHome = possessorHome,
                                    .toHome = ownerHome,
                                    .afterAction = lastAction});
    possessor = owner;
    possessorHome = ownerHome;
    lastAction.reset();
  }

  const auto& engineEvents = engine.getEvents();
  for (std::size_t index = matchEvents.size(); index < engineEvents.size();
       ++index)
  {
    matchEvents.push_back(engineEvents[index]);
    append(engine, EventEntry{index});
  }

  // Rewind: the logs only grow during a live match, so new records are
  // appended; a fingerprint per tick; a saved copy every minute of play.
  const auto& engineCommands = engine.getCommandLog();
  if (engineCommands.size() > commandLog.size())
  {
    commandLog.insert(
        commandLog.end(),
        engineCommands.begin() + static_cast<std::ptrdiff_t>(commandLog.size()),
        engineCommands.end());
  }
  const auto& engineInputs = engine.getInputLog();
  if (engineInputs.size() > inputLog.size())
  {
    inputLog.insert(
        inputLog.end(),
        engineInputs.begin() + static_cast<std::ptrdiff_t>(inputLog.size()),
        engineInputs.end());
  }
  if (checksums.size() <= tick)
  {
    checksums.resize(tick + 1, 0);
  }
  checksums[tick] = stateChecksum(engine);
  if (savedCopies.empty() || tick >= savedCopies.back().tick + KEYFRAME_TICKS)
  {
    savedCopies.push_back(
        {.tick = tick, .engine = std::make_shared<const MatchEngine>(engine)});
  }
  fullTime = engine.getState() == MatchState::FULL_TIME;
}

std::optional<std::uint64_t> SandboxRecorder::checksum(std::uint64_t at) const
{
  if (at >= checksums.size() || checksums[at] == 0)
  {
    return std::nullopt;
  }
  return checksums[at];
}

std::uint64_t SandboxRecorder::stateChecksum(const MatchEngine& engine)
{
  // FNV-1a over the state that any divergence shows up in within a tick.
  std::uint64_t hash = FNV_OFFSET_BASIS;
  const auto add = [&hash](const auto& value)
  {
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    for (std::size_t index = 0; index < sizeof(value); ++index)
    {
      hash = (hash ^ bytes[index]) * FNV_PRIME;
    }
  };
  add(engine.getSimulatedSteps());
  add(engine.getHomeScore());
  add(engine.getAwayScore());
  for (const MatchPlayer& slot : engine.getPlayers())
  {
    add(slot.position.x);
    add(slot.position.y);
    add(idOf(slot.player));
  }
  const MatchBall& ball = engine.getBall();
  add(ball.position.x);
  add(ball.position.y);
  add(ball.z);
  add(idOf(engine.getPossessionAttachment()));
  return hash == 0 ? 1 : hash;
}

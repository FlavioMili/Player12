// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "audio/match_audio.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "audio/audio_device.h"
#include "model/match_engine.h"
#include "model/player.h"
#include "model/settings_manager.h"

using Audio::Sound;
using Audio::SoundCommand;
using Audio::WhistlePattern;

namespace
{
/** Above this playback speed only the bigger moments are heard. */
constexpr float FULL_DETAIL_MAX_SPEED = 4.0f;
constexpr float REDUCED_DETAIL_MAX_SPEED = 8.0f;
/** Events older than this (match minutes) when they arrive are skipped. */
constexpr float STALE_EVENT_MINUTES = 0.5f;
/** A shot that ends in a goal kick or corner this soon was a miss. */
constexpr float SHOT_OUTCOME_MINUTES = 0.3f;
constexpr float KICK_COOLDOWN_SECONDS = 0.07f;
/** Minor crowd reactions wait this long after any other reaction. */
constexpr float REACTION_GAP_SECONDS = 0.8f;

float panFor(float x, float width)
{
  return std::clamp((x - 0.5f) * 2.0f * width, -0.85f, 0.85f);
}

std::uint64_t fingerprintOf(const MatchEvent& event)
{
  std::uint64_t hash = 1469598103934665603ULL;
  const auto mix = [&hash](std::uint64_t value)
  {
    hash ^= value;
    hash *= 1099511628211ULL;
  };
  mix(static_cast<std::uint64_t>(std::to_underlying(event.type)));
  mix(static_cast<std::uint64_t>(std::to_underlying(event.detail)));
  mix(event.primaryPlayerId);
  mix(event.secondaryPlayerId);
  mix(static_cast<std::uint64_t>(event.homeScore) << 16U |
      static_cast<std::uint64_t>(event.awayScore));
  mix((event.hasTeam ? 2U : 0U) | (event.isHomeTeam ? 1U : 0U));
  return hash;
}
}  // namespace

// --- MatchEventCursor --------------------------------------------------------

bool MatchEventCursor::isBefore(const MatchEvent& event) const
{
  if (event.period != period) return event.period < period;
  return event.timeMinute < minute;
}

bool MatchEventCursor::isNew(const MatchEvent& event,
                             std::uint64_t fingerprint) const
{
  if (event.period != period) return event.period > period;
  if (event.timeMinute != minute) return event.timeMinute > minute;
  return std::find(sameTime.begin(), sameTime.begin() + sameTimeCount,
                   fingerprint) == sameTime.begin() + sameTimeCount;
}

void MatchEventCursor::mark(const MatchEvent& event, std::uint64_t fingerprint)
{
  if (event.period != period || event.timeMinute != minute)
  {
    period = event.period;
    minute = event.timeMinute;
    sameTimeCount = 0;
  }
  if (sameTimeCount < SAME_TIME_CAPACITY)
    sameTime[sameTimeCount++] = fingerprint;
}

void MatchEventCursor::collect(std::span<const MatchEvent> events,
                               std::vector<const MatchEvent*>& fresh)
{
  // The log is ordered by match time: walk back only over the events at or
  // after the last one heard, so a frame costs O(new events).
  std::size_t start = events.size();
  while (start > 0 && !isBefore(events[start - 1])) --start;
  for (std::size_t index = start; index < events.size(); ++index)
  {
    const MatchEvent& event = events[index];
    const std::uint64_t fingerprint = fingerprintOf(event);
    if (!isNew(event, fingerprint)) continue;
    mark(event, fingerprint);
    fresh.push_back(&event);
  }
}

void MatchEventCursor::skipAll(std::span<const MatchEvent> events)
{
  std::size_t start = events.size();
  while (start > 0 && !isBefore(events[start - 1])) --start;
  for (std::size_t index = start; index < events.size(); ++index)
  {
    const std::uint64_t fingerprint = fingerprintOf(events[index]);
    if (isNew(events[index], fingerprint)) mark(events[index], fingerprint);
  }
}

// --- Frame capture -----------------------------------------------------------

MatchAudioFrame captureMatchAudioFrame(const MatchEngine& engine,
                                       float playbackSpeed, bool paused,
                                       bool skipped)
{
  MatchAudioFrame frame;
  const MatchBall& ball = engine.getBall();
  frame.ballX = ball.position.x;
  frame.ballSpeed = std::hypot(ball.velocity.x, ball.velocity.y);
  frame.ballIsShot = ball.isShot;
  frame.ballLoose = ball.possessedBy == nullptr;
  frame.kickerId = ball.kicker != nullptr ? ball.kicker->getId() : 0;
  frame.homeInPossession = engine.getHomePhase() == TeamPhase::POSSESSION;
  frame.awayInPossession = engine.getAwayPhase() == TeamPhase::POSSESSION;
  frame.matchMinute = engine.getMatchTimeMinutes();
  frame.period = engine.getPeriod();
  frame.homeScore = engine.getHomeScore();
  frame.awayScore = engine.getAwayScore();
  const MatchState state = engine.getState();
  frame.stopped =
      state == MatchState::HALF_TIME || state == MatchState::FULL_TIME;
  frame.playbackSpeed = playbackSpeed;
  frame.paused = paused;
  frame.skipped = skipped;
  return frame;
}

// --- MatchAudio --------------------------------------------------------------

MatchAudio::MatchAudio(bool openDevice)
{
  freshEvents.reserve(32);
  if (openDevice) device = std::make_unique<Audio::AudioDevice>(*mixerInstance);
}

MatchAudio::~MatchAudio()
{
  // Stop the audio thread before the mixer goes away.
  device.reset();
}

bool MatchAudio::deviceOpen() { return device && device->isOpen(); }

Audio::BusLevels MatchAudio::levelsFrom(const Settings& settings)
{
  return {settings.master_volume, settings.crowd_volume,
          settings.effects_volume, settings.audio_muted};
}

void MatchAudio::setLevels(const Audio::BusLevels& levels)
{
  mixerInstance->setLevels(levels);
}

bool MatchAudio::allowed(Priority priority) const
{
  if (playbackSpeed <= FULL_DETAIL_MAX_SPEED) return true;
  if (playbackSpeed <= REDUCED_DETAIL_MAX_SPEED)
    return priority != Priority::MINOR;
  return priority == Priority::KEY;
}

void MatchAudio::play(Priority priority, SoundCommand command)
{
  if (!allowed(priority)) return;
  if (Audio::isCrowdSound(command.sound))
  {
    // Minor murmurs never pile onto a bigger reaction still in the air.
    if (priority == Priority::MINOR && sinceReaction < REACTION_GAP_SECONDS &&
        lastReaction != Priority::MINOR)
      return;
    sinceReaction = 0.0f;
    lastReaction = priority;
  }
  mixerInstance->trigger(command);
}

void MatchAudio::whistle(Priority priority, WhistlePattern pattern, float x)
{
  play(priority, {.sound = Sound::WHISTLE,
                  .gain = 0.5f,
                  .pan = panFor(x, 0.35f),
                  .intensity = 1.0f,
                  .variant = std::to_underlying(pattern)});
}

void MatchAudio::update(float wallSeconds, const MatchAudioFrame& frame,
                        std::span<const MatchEvent> events)
{
  playbackSpeed = frame.playbackSpeed;
  sinceReaction += wallSeconds;
  kickCooldown = std::max(0.0f, kickCooldown - wallSeconds);

  const bool replaying =
      frame.period < latestPeriod ||
      (frame.period == latestPeriod && frame.matchMinute < latestMinute);
  if (!replaying)
  {
    latestPeriod = frame.period;
    latestMinute = frame.matchMinute;
  }
  if (frame.skipped)
  {
    cursor.skipAll(events);
    pendingShot.active = false;
    haveBallHistory = false;
  }
  else if (!frame.paused)
  {
    freshEvents.clear();
    cursor.collect(events, freshEvents);
    for (const MatchEvent* event : freshEvents)
    {
      const bool stale =
          event->period == frame.period &&
          frame.matchMinute - event->timeMinute > STALE_EVENT_MINUTES;
      if (!stale) react(*event);
    }
    detectKicks(frame, replaying);
  }
  if (pendingShot.active &&
      (pendingShot.period != frame.period ||
       frame.matchMinute - pendingShot.minute > SHOT_OUTCOME_MINUTES))
    pendingShot.active = false;
  updateCrowd(wallSeconds, frame);
}

void MatchAudio::detectKicks(const MatchAudioFrame& frame, bool replaying)
{
  if (!haveBallHistory)
  {
    haveBallHistory = true;
  }
  else if (!replaying && playbackSpeed <= FULL_DETAIL_MAX_SPEED &&
           kickCooldown <= 0.0f)
  {
    float power = -1.0f;
    const float speed = frame.ballSpeed;
    if (frame.ballIsShot && !previousShot)
      power = 0.75f + 0.25f * std::clamp((speed - 15.0f) / 15.0f, 0.0f, 1.0f);
    else if (frame.ballLoose && speed > 2.5f &&
             (!previousLoose || frame.kickerId != previousKicker))
      power = std::clamp((speed - 3.0f) / 26.0f, 0.08f, 0.7f);
    else if (frame.ballLoose && speed - previousBallSpeed > 4.0f &&
             speed > 6.0f)
      power = std::clamp((speed - 4.0f) / 26.0f, 0.1f, 0.7f);
    else if (!frame.ballLoose && previousLoose && previousBallSpeed > 9.0f)
      power = 0.06f;  // a first touch cushioning a firm ball
    if (power >= 0.0f)
    {
      mixerInstance->trigger({.sound = Sound::KICK,
                              .gain = 0.6f + 0.5f * power,
                              .pan = panFor(frame.ballX, 0.8f),
                              .intensity = power});
      kickCooldown = KICK_COOLDOWN_SECONDS;
    }
  }
  previousBallSpeed = frame.ballSpeed;
  previousShot = frame.ballIsShot;
  previousLoose = frame.ballLoose;
  previousKicker = frame.kickerId;
}

void MatchAudio::updateCrowd(float wallSeconds, const MatchAudioFrame& frame)
{
  tension *= std::exp(-wallSeconds / 3.0f);
  float intensity = 0.32f;
  if (frame.stopped)
  {
    intensity = 0.2f;
  }
  else
  {
    if (frame.homeInPossession && frame.ballX > 0.62f)
      intensity += 0.35f * (frame.ballX - 0.62f) / 0.38f;
    if (frame.awayInPossession && frame.ballX < 0.38f)
      intensity += 0.15f * (0.38f - frame.ballX) / 0.38f;
    if (frame.period == 2 && frame.matchMinute > 75.0f &&
        std::abs(frame.homeScore - frame.awayScore) <= 1)
      intensity += 0.08f;
  }
  intensity += tension;
  mixerInstance->setCrowd(intensity, frame.paused ? 0.25f : 1.0f);
}

void MatchAudio::reactToGoal(const MatchEvent& event)
{
  const bool ownGoal = event.type == MatchEventType::OWN_GOAL;
  const bool homeScored = ownGoal ? !event.isHomeTeam : event.isHomeTeam;
  const int lead = homeScored ? event.homeScore - event.awayScore
                              : event.awayScore - event.homeScore;
  float importance = 0.45f;
  if (lead == 0) importance += 0.25f;  // equaliser
  if (lead == 1) importance += 0.2f;   // goes ahead
  if (lead >= 3) importance -= 0.15f;
  if (event.period == 2 && event.timeMinute >= 80.0f) importance += 0.15f;
  if (event.period == 2 && event.addedMinute > 0.0f) importance += 0.15f;
  importance = std::clamp(importance, 0.2f, 1.0f);

  // Home attacks toward x = 1: the net is at the scoring side's end.
  const float netX = homeScored ? 1.0f : 0.0f;
  play(Priority::KEY, {.sound = Sound::NET,
                       .gain = 0.7f,
                       .pan = panFor(netX, 0.8f),
                       .intensity = 0.6f + 0.4f * importance});
  if (homeScored)
  {
    play(Priority::KEY, {.sound = Sound::CROWD_CHEER,
                         .gain = 0.85f + 0.3f * importance,
                         .pan = panFor(netX, 0.25f),
                         .intensity = importance,
                         .delay = 0.06f});
    play(Priority::MAJOR, {.sound = Sound::CROWD_APPLAUSE,
                           .gain = 0.45f,
                           .intensity = importance,
                           .delay = 1.4f});
    tension = 0.55f;
  }
  else
  {
    play(Priority::KEY, {.sound = Sound::CROWD_GROAN,
                         .gain = 0.75f,
                         .intensity = 0.6f,
                         .delay = 0.1f});
    // The away end celebrates on its own, smaller and off to one side.
    play(Priority::KEY, {.sound = Sound::CROWD_CHEER,
                         .gain = 0.28f,
                         .pan = -0.7f,
                         .intensity = importance * 0.6f,
                         .delay = 0.08f});
    tension = -0.15f;
  }
}

void MatchAudio::reactToMiss(float x)
{
  if (!pendingShot.home) return;
  const float size = std::clamp(0.3f + pendingShot.xg * 2.0f, 0.3f, 1.0f);
  play(Priority::MAJOR, {.sound = Sound::CROWD_OOH,
                         .gain = 0.45f + 0.5f * size,
                         .pan = panFor(x, 0.2f),
                         .intensity = size});
}

void MatchAudio::reactToKickOff()
{
  using enum Priority;
  using enum Sound;
  whistle(KEY, WhistlePattern::KICK_OFF, 0.5f);
  play(MAJOR, {.sound = CROWD_APPLAUSE,
               .gain = 0.55f,
               .intensity = 0.7f,
               .delay = 0.1f});
  play(MAJOR, {.sound = CROWD_CHEER,
               .gain = 0.45f,
               .intensity = 0.25f,
               .delay = 0.15f});
}

void MatchAudio::reactToHalfTime(const MatchEvent& event, float x)
{
  using enum Priority;
  using enum Sound;
  whistle(KEY, WhistlePattern::HALF_TIME, x);
  const bool behind = event.homeScore < event.awayScore;
  play(MAJOR, {.sound = CROWD_APPLAUSE,
               .gain = behind ? 0.2f : 0.55f,
               .intensity = 0.5f,
               .delay = 1.4f});
  if (behind)
    play(MAJOR,
         {.sound = CROWD_BOO, .gain = 0.4f, .intensity = 0.4f, .delay = 1.5f});
}

void MatchAudio::reactToFullTime(const MatchEvent& event, float x)
{
  using enum Priority;
  using enum Sound;
  whistle(KEY, WhistlePattern::FULL_TIME, x);
  const int margin = event.homeScore - event.awayScore;
  if (margin > 0)
  {
    play(KEY, {.sound = CROWD_CHEER,
               .gain = 0.95f,
               .intensity = 0.8f,
               .delay = 1.7f});
    play(KEY, {.sound = CROWD_APPLAUSE,
               .gain = 0.65f,
               .intensity = 0.9f,
               .delay = 2.3f});
  }
  else if (margin == 0)
  {
    play(KEY, {.sound = CROWD_APPLAUSE,
               .gain = 0.5f,
               .intensity = 0.5f,
               .delay = 1.7f});
  }
  else
  {
    play(KEY,
         {.sound = CROWD_BOO, .gain = 0.6f, .intensity = 0.6f, .delay = 1.7f});
    play(KEY, {.sound = CROWD_WHISTLES,
               .gain = 0.4f,
               .intensity = 0.5f,
               .delay = 1.9f});
    play(KEY, {.sound = CROWD_CHEER,
               .gain = 0.25f,
               .pan = -0.7f,
               .intensity = 0.5f,
               .delay = 1.8f});
  }
  tension = 0.0f;
}

void MatchAudio::reactToSave(bool away, float x)
{
  using enum Priority;
  using enum Sound;
  const float size =
      std::clamp(0.4f + std::min(0.6f, pendingShot.xg * 2.0f), 0.4f, 1.0f);
  pendingShot.active = false;
  if (away)
  {
    play(MAJOR, {.sound = CROWD_OOH,
                 .gain = 0.5f + 0.4f * size,
                 .pan = panFor(x, 0.2f),
                 .intensity = size});
    play(MINOR, {.sound = CROWD_APPLAUSE,
                 .gain = 0.3f,
                 .intensity = 0.3f,
                 .delay = 0.9f});
  }
  else
  {
    play(MAJOR, {.sound = CROWD_APPLAUSE,
                 .gain = 0.5f,
                 .intensity = 0.45f,
                 .delay = 0.15f});
  }
}

void MatchAudio::reactToWoodwork(bool home, float x)
{
  using enum Priority;
  using enum Sound;
  pendingShot.active = false;
  play(MAJOR, {.sound = WOODWORK,
               .gain = 0.75f,
               .pan = panFor(x, 0.8f),
               .intensity = 0.85f});
  play(MAJOR, {.sound = CROWD_OOH,
               .gain = home ? 1.0f : 0.5f,
               .pan = panFor(x, 0.2f),
               .intensity = home ? 0.95f : 0.5f,
               .delay = 0.05f});
}

void MatchAudio::reactToCorner(bool home, float x)
{
  using enum Priority;
  using enum Sound;
  if (pendingShot.active) reactToMiss(x);
  pendingShot.active = false;
  if (!home) return;
  play(MINOR,
       {.sound = CROWD_CHEER, .gain = 0.3f, .intensity = 0.1f, .delay = 0.3f});
  tension = std::max(tension, 0.15f);
}

void MatchAudio::reactToFoul(bool away, float x)
{
  using enum Priority;
  using enum Sound;
  whistle(MINOR, WhistlePattern::SHORT, x);
  if (!away) return;
  play(MINOR,
       {.sound = CROWD_BOO, .gain = 0.35f, .intensity = 0.2f, .delay = 0.2f});
  if (x > 0.66f)
    play(MINOR, {.sound = CROWD_WHISTLES,
                 .gain = 0.3f,
                 .intensity = 0.3f,
                 .delay = 0.25f});
}

void MatchAudio::reactToCard(MatchEventType type, bool home, bool away)
{
  using enum Priority;
  using enum Sound;
  if (type == MatchEventType::YELLOW_CARD)
  {
    if (away)
      play(MINOR, {.sound = CROWD_CHEER, .gain = 0.35f, .intensity = 0.12f});
    else if (home)
      play(MINOR, {.sound = CROWD_WHISTLES, .gain = 0.35f, .intensity = 0.4f});
    return;
  }
  if (away)
  {
    play(MAJOR, {.sound = CROWD_CHEER, .gain = 0.6f, .intensity = 0.4f});
  }
  else if (home)
  {
    play(MAJOR, {.sound = CROWD_BOO, .gain = 0.8f, .intensity = 0.8f});
    play(MAJOR, {.sound = CROWD_WHISTLES,
                 .gain = 0.5f,
                 .intensity = 0.7f,
                 .delay = 0.2f});
  }
}

void MatchAudio::reactToPenalty(bool home, bool away, float x)
{
  using enum Priority;
  using enum Sound;
  whistle(MAJOR, WhistlePattern::SHORT, x);
  if (home)
  {
    play(MAJOR, {.sound = CROWD_CHEER,
                 .gain = 0.8f,
                 .intensity = 0.55f,
                 .delay = 0.2f});
    tension = 0.45f;
  }
  else if (away)
  {
    play(MAJOR,
         {.sound = CROWD_BOO, .gain = 0.85f, .intensity = 0.8f, .delay = 0.2f});
    play(MAJOR, {.sound = CROWD_WHISTLES,
                 .gain = 0.55f,
                 .intensity = 0.8f,
                 .delay = 0.3f});
  }
}

void MatchAudio::react(const MatchEvent& event)
{
  using enum Priority;
  using enum Sound;
  const float x = event.position.x;
  const bool home = event.hasTeam && event.isHomeTeam;
  const bool away = event.hasTeam && !event.isHomeTeam;
  switch (event.type)
  {
    case MatchEventType::KICK_OFF:
      reactToKickOff();
      break;
    case MatchEventType::SECOND_HALF:
      whistle(KEY, WhistlePattern::KICK_OFF, 0.5f);
      play(MAJOR, {.sound = CROWD_APPLAUSE,
                   .gain = 0.45f,
                   .intensity = 0.5f,
                   .delay = 0.1f});
      break;
    case MatchEventType::HALF_TIME:
      reactToHalfTime(event, x);
      break;
    case MatchEventType::FULL_TIME:
      reactToFullTime(event, x);
      break;
    case MatchEventType::GOAL:
    case MatchEventType::OWN_GOAL:
      pendingShot.active = false;
      reactToGoal(event);
      break;
    case MatchEventType::SHOT:
      pendingShot = {true, home, event.xg, event.period, event.timeMinute};
      if (home) tension = std::max(tension, 0.2f + std::min(0.3f, event.xg));
      break;
    case MatchEventType::SAVE:
      // The team fields describe the goalkeeper.
      reactToSave(away, x);
      break;
    case MatchEventType::WOODWORK:
      reactToWoodwork(home, x);
      break;
    case MatchEventType::SHOT_BLOCKED:
      pendingShot.active = false;
      if (away)  // a defender of the away side blocked a home shot
        play(MINOR, {.sound = CROWD_GROAN, .gain = 0.35f, .intensity = 0.2f});
      break;
    case MatchEventType::SHOT_OFF_TARGET:
    case MatchEventType::PENALTY_MISSED:
      reactToMiss(x);
      pendingShot.active = false;
      break;
    case MatchEventType::GOAL_KICK:
      if (pendingShot.active) reactToMiss(x);
      pendingShot.active = false;
      break;
    case MatchEventType::CORNER:
      reactToCorner(home, x);
      break;
    case MatchEventType::FOUL:
      // The team fields describe the offender.
      reactToFoul(away, x);
      break;
    case MatchEventType::YELLOW_CARD:
    case MatchEventType::SECOND_YELLOW:
    case MatchEventType::RED_CARD:
      reactToCard(event.type, home, away);
      break;
    case MatchEventType::PENALTY:
      // The team fields describe the side awarded the kick.
      reactToPenalty(home, away, x);
      break;
    case MatchEventType::OFFSIDE:
      whistle(MINOR, WhistlePattern::SHORT, x);
      if (home)
        play(MINOR, {.sound = CROWD_GROAN,
                     .gain = 0.3f,
                     .intensity = 0.15f,
                     .delay = 0.2f});
      break;
    case MatchEventType::SUBSTITUTION:
      if (home)
        play(MINOR, {.sound = CROWD_APPLAUSE, .gain = 0.3f, .intensity = 0.2f});
      break;
    default:
      break;
  }
}

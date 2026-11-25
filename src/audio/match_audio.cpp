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
constexpr float FULL_DETAIL_MAX_SPEED = 4.0F;
constexpr float REDUCED_DETAIL_MAX_SPEED = 8.0F;
/** Events older than this (match minutes) when they arrive are skipped. */
constexpr float STALE_EVENT_MINUTES = 0.5F;
/** A shot that ends in a goal kick or corner this soon was a miss. */
constexpr float SHOT_OUTCOME_MINUTES = 0.3F;
constexpr float KICK_COOLDOWN_SECONDS = 0.07F;
/** Minor crowd reactions wait this long after any other reaction. */
constexpr float REACTION_GAP_SECONDS = 0.8F;

float panFor(float x, float width)
{
  return std::clamp((x - 0.5F) * 2.0F * width, -0.85F, 0.85F);
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
  if (event.period != period)
  {
    return event.period < period;
  }
  return event.timeMinute < minute;
}

bool MatchEventCursor::isNew(const MatchEvent& event,
                             std::uint64_t fingerprint) const
{
  if (event.period != period)
  {
    return event.period > period;
  }
  if (event.timeMinute != minute)
  {
    return event.timeMinute > minute;
  }
  const auto heard = std::span(sameTime).first(sameTimeCount);
  return std::ranges::find(heard, fingerprint) == heard.end();
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
  {
    sameTime[sameTimeCount++] = fingerprint;
  }
}

void MatchEventCursor::collect(std::span<const MatchEvent> events,
                               std::vector<const MatchEvent*>& fresh)
{
  // The log is ordered by match time: walk back only over the events at or
  // after the last one heard, so a frame costs O(new events).
  std::size_t start = events.size();
  while (start > 0 && !isBefore(events[start - 1]))
  {
    --start;
  }
  for (std::size_t index = start; index < events.size(); ++index)
  {
    const MatchEvent& event = events[index];
    const std::uint64_t fingerprint = fingerprintOf(event);
    if (!isNew(event, fingerprint))
    {
      continue;
    }
    mark(event, fingerprint);
    fresh.push_back(&event);
  }
}

void MatchEventCursor::skipAll(std::span<const MatchEvent> events)
{
  std::size_t start = events.size();
  while (start > 0 && !isBefore(events[start - 1]))
  {
    --start;
  }
  for (std::size_t index = start; index < events.size(); ++index)
  {
    const std::uint64_t fingerprint = fingerprintOf(events[index]);
    if (isNew(events[index], fingerprint))
    {
      mark(events[index], fingerprint);
    }
  }
}

// --- Frame capture -----------------------------------------------------------

MatchAudioFrame captureMatchAudioFrame(const MatchEngine& engine,
                                       float playbackSpeed, bool paused,
                                       bool skipped)
{
  MatchAudioFrame frame;
  const MatchBall& ball = engine.getBall();
  // Audio pans and builds by fractions of a match pitch (plan section 15,
  // choice 8).
  frame.ballX = MatchPitch::matchSize().fractionX(ball.position);
  frame.ballSpeed = std::hypot(ball.velocity.x, ball.velocity.y);
  frame.ballIsShot = ball.possessionState().isShot;
  frame.ballLoose = engine.getPossessionAttachment() == nullptr;
  frame.kickerId = engine.getLegacyContactActor() != nullptr
                       ? engine.getLegacyContactActor()->getId()
                       : 0;
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
  if (openDevice)
  {
    device = std::make_unique<Audio::AudioDevice>(*mixerInstance);
  }
}

MatchAudio::~MatchAudio()
{
  // Stop the audio thread before the mixer goes away.
  device.reset();
}

bool MatchAudio::deviceOpen() { return device && device->isOpen(); }

Audio::BusLevels MatchAudio::levelsFrom(const Settings& settings)
{
  return {.master = settings.master_volume,
          .crowd = settings.crowd_volume,
          .effects = settings.effects_volume,
          .muted = settings.audio_muted};
}

void MatchAudio::setLevels(const Audio::BusLevels& levels)
{
  mixerInstance->setLevels(levels);
}

bool MatchAudio::allowed(Priority priority) const
{
  if (playbackSpeed <= FULL_DETAIL_MAX_SPEED)
  {
    return true;
  }
  if (playbackSpeed <= REDUCED_DETAIL_MAX_SPEED)
  {
    return priority != Priority::MINOR;
  }
  return priority == Priority::KEY;
}

void MatchAudio::play(Priority priority, SoundCommand command)
{
  if (!allowed(priority))
  {
    return;
  }
  if (Audio::isCrowdSound(command.sound))
  {
    // Minor murmurs never pile onto a bigger reaction still in the air.
    if (priority == Priority::MINOR && sinceReaction < REACTION_GAP_SECONDS &&
        lastReaction != Priority::MINOR)
    {
      return;
    }
    sinceReaction = 0.0F;
    lastReaction = priority;
  }
  mixerInstance->trigger(command);
}

void MatchAudio::whistle(Priority priority, WhistlePattern pattern, float x)
{
  play(priority, {.sound = Sound::WHISTLE,
                  .gain = 0.5F,
                  .pan = panFor(x, 0.35F),
                  .intensity = 1.0F,
                  .variant = std::to_underlying(pattern)});
}

void MatchAudio::playCoachWhistle(bool restart)
{
  whistle(Priority::KEY,
          restart ? WhistlePattern::KICK_OFF : WhistlePattern::SHORT, 0.5F);
}

void MatchAudio::update(float wallSeconds, const MatchAudioFrame& frame,
                        std::span<const MatchEvent> events)
{
  playbackSpeed = frame.playbackSpeed;
  sinceReaction += wallSeconds;
  kickCooldown = std::max(0.0F, kickCooldown - wallSeconds);

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
      if (!stale)
      {
        react(*event);
      }
    }
    detectKicks(frame, replaying);
  }
  if (pendingShot.active &&
      (pendingShot.period != frame.period ||
       frame.matchMinute - pendingShot.minute > SHOT_OUTCOME_MINUTES))
  {
    pendingShot.active = false;
  }
  updateCrowd(wallSeconds, frame);
}

void MatchAudio::detectKicks(const MatchAudioFrame& frame, bool replaying)
{
  if (!haveBallHistory)
  {
    haveBallHistory = true;
  }
  else if (!replaying && playbackSpeed <= FULL_DETAIL_MAX_SPEED &&
           kickCooldown <= 0.0F)
  {
    float power = -1.0F;
    const float speed = frame.ballSpeed;
    if (frame.ballIsShot && !previousShot)
    {
      power = 0.75F + 0.25F * std::clamp((speed - 15.0F) / 15.0F, 0.0F, 1.0F);
    }
    else if (frame.ballLoose && speed > 2.5F &&
             (!previousLoose || frame.kickerId != previousKicker))
    {
      power = std::clamp((speed - 3.0F) / 26.0F, 0.08F, 0.7F);
    }
    else if (frame.ballLoose && speed - previousBallSpeed > 4.0F &&
             speed > 6.0F)
    {
      power = std::clamp((speed - 4.0F) / 26.0F, 0.1F, 0.7F);
    }
    else if (!frame.ballLoose && previousLoose && previousBallSpeed > 9.0F)
    {
      power = 0.06F;  // a first touch cushioning a firm ball
    }
    if (power >= 0.0F)
    {
      mixerInstance->trigger({.sound = Sound::KICK,
                              .gain = 0.6F + (0.5F * power),
                              .pan = panFor(frame.ballX, 0.8F),
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
  tension *= std::exp(-wallSeconds / 3.0F);
  float intensity = 0.32F;
  if (frame.stopped)
  {
    intensity = 0.2F;
  }
  else
  {
    if (frame.homeInPossession && frame.ballX > 0.62F)
    {
      intensity += 0.35F * (frame.ballX - 0.62F) / 0.38F;
    }
    if (frame.awayInPossession && frame.ballX < 0.38F)
    {
      intensity += 0.15F * (0.38F - frame.ballX) / 0.38F;
    }
    if (frame.period == 2 && frame.matchMinute > 75.0F &&
        std::abs(frame.homeScore - frame.awayScore) <= 1)
    {
      intensity += 0.08F;
    }
  }
  intensity += tension;
  mixerInstance->setCrowd(intensity, frame.paused ? 0.25F : 1.0F);
}

void MatchAudio::reactToGoal(const MatchEvent& event)
{
  const bool ownGoal = event.type == MatchEventType::OWN_GOAL;
  const bool homeScored = ownGoal ? !event.isHomeTeam : event.isHomeTeam;
  const int lead = homeScored ? event.homeScore - event.awayScore
                              : event.awayScore - event.homeScore;
  float importance = 0.45F;
  if (lead == 0)
  {
    importance += 0.25F;  // equaliser
  }
  if (lead == 1)
  {
    importance += 0.2F;  // goes ahead
  }
  if (lead >= 3)
  {
    importance -= 0.15F;
  }
  if (event.period == 2 && event.timeMinute >= 80.0F)
  {
    importance += 0.15F;
  }
  if (event.period == 2 && event.addedMinute > 0.0F)
  {
    importance += 0.15F;
  }
  importance = std::clamp(importance, 0.2F, 1.0F);

  // Home attacks toward x = 1: the net is at the scoring side's end.
  const float netX = homeScored ? 1.0F : 0.0F;
  play(Priority::KEY, {.sound = Sound::NET,
                       .gain = 0.7F,
                       .pan = panFor(netX, 0.8F),
                       .intensity = 0.6F + (0.4F * importance)});
  if (homeScored)
  {
    play(Priority::KEY, {.sound = Sound::CROWD_CHEER,
                         .gain = 0.85F + (0.3F * importance),
                         .pan = panFor(netX, 0.25F),
                         .intensity = importance,
                         .delay = 0.06F});
    play(Priority::MAJOR, {.sound = Sound::CROWD_APPLAUSE,
                           .gain = 0.45F,
                           .intensity = importance,
                           .delay = 1.4F});
    tension = 0.55F;
  }
  else
  {
    play(Priority::KEY, {.sound = Sound::CROWD_GROAN,
                         .gain = 0.75F,
                         .intensity = 0.6F,
                         .delay = 0.1F});
    // The away end celebrates on its own, smaller and off to one side.
    play(Priority::KEY, {.sound = Sound::CROWD_CHEER,
                         .gain = 0.28F,
                         .pan = -0.7F,
                         .intensity = importance * 0.6F,
                         .delay = 0.08F});
    tension = -0.15F;
  }
}

void MatchAudio::reactToMiss(float x)
{
  if (!pendingShot.home)
  {
    return;
  }
  const float size = std::clamp(0.3F + (pendingShot.xg * 2.0F), 0.3F, 1.0F);
  play(Priority::MAJOR, {.sound = Sound::CROWD_OOH,
                         .gain = 0.45F + (0.5F * size),
                         .pan = panFor(x, 0.2F),
                         .intensity = size});
}

void MatchAudio::react(const MatchEvent& event)
{
  using enum Priority;
  using enum Audio::Sound;
  const float x = MatchPitch::matchSize().fractionX(event.position);
  const bool home = event.hasTeam && event.isHomeTeam;
  const bool away = event.hasTeam && !event.isHomeTeam;
  switch (event.type)
  {
    case MatchEventType::KICK_OFF:
      whistle(KEY, WhistlePattern::KICK_OFF, 0.5F);
      play(MAJOR, {.sound = CROWD_APPLAUSE,
                   .gain = 0.55F,
                   .intensity = 0.7F,
                   .delay = 0.1F});
      play(MAJOR, {.sound = CROWD_CHEER,
                   .gain = 0.45F,
                   .intensity = 0.25F,
                   .delay = 0.15F});
      break;
    case MatchEventType::SECOND_HALF:
      whistle(KEY, WhistlePattern::KICK_OFF, 0.5F);
      play(MAJOR, {.sound = CROWD_APPLAUSE,
                   .gain = 0.45F,
                   .intensity = 0.5F,
                   .delay = 0.1F});
      break;
    case MatchEventType::HALF_TIME:
    {
      whistle(KEY, WhistlePattern::HALF_TIME, x);
      const bool behind = event.homeScore < event.awayScore;
      play(MAJOR, {.sound = CROWD_APPLAUSE,
                   .gain = behind ? 0.2F : 0.55F,
                   .intensity = 0.5F,
                   .delay = 1.4F});
      if (behind)
      {
        play(MAJOR, {.sound = CROWD_BOO,
                     .gain = 0.4F,
                     .intensity = 0.4F,
                     .delay = 1.5F});
      }
      break;
    }
    case MatchEventType::FULL_TIME:
    {
      whistle(KEY, WhistlePattern::FULL_TIME, x);
      if (const int margin = event.homeScore - event.awayScore; margin > 0)
      {
        play(KEY, {.sound = CROWD_CHEER,
                   .gain = 0.95F,
                   .intensity = 0.8F,
                   .delay = 1.7F});
        play(KEY, {.sound = CROWD_APPLAUSE,
                   .gain = 0.65F,
                   .intensity = 0.9F,
                   .delay = 2.3F});
      }
      else if (margin == 0)
      {
        play(KEY, {.sound = CROWD_APPLAUSE,
                   .gain = 0.5F,
                   .intensity = 0.5F,
                   .delay = 1.7F});
      }
      else
      {
        play(KEY, {.sound = CROWD_BOO,
                   .gain = 0.6F,
                   .intensity = 0.6F,
                   .delay = 1.7F});
        play(KEY, {.sound = CROWD_WHISTLES,
                   .gain = 0.4F,
                   .intensity = 0.5F,
                   .delay = 1.9F});
        play(KEY, {.sound = CROWD_CHEER,
                   .gain = 0.25F,
                   .pan = -0.7F,
                   .intensity = 0.5F,
                   .delay = 1.8F});
      }
      tension = 0.0F;
      break;
    }
    case MatchEventType::GOAL:
    case MatchEventType::OWN_GOAL:
      pendingShot.active = false;
      reactToGoal(event);
      break;
    case MatchEventType::SHOT:
      pendingShot = {.active = true,
                     .home = home,
                     .xg = event.xg,
                     .period = event.period,
                     .minute = event.timeMinute};
      if (home)
      {
        tension = std::max(tension, 0.2F + std::min(0.3F, event.xg));
      }
      break;
    case MatchEventType::SAVE:
    {
      // The team fields describe the goalkeeper.
      const float size =
          std::clamp(0.4F + std::min(0.6F, pendingShot.xg * 2.0F), 0.4F, 1.0F);
      pendingShot.active = false;
      if (away)
      {
        play(MAJOR, {.sound = CROWD_OOH,
                     .gain = 0.5F + (0.4F * size),
                     .pan = panFor(x, 0.2F),
                     .intensity = size});
        play(MINOR, {.sound = CROWD_APPLAUSE,
                     .gain = 0.3F,
                     .intensity = 0.3F,
                     .delay = 0.9F});
      }
      else
      {
        play(MAJOR, {.sound = CROWD_APPLAUSE,
                     .gain = 0.5F,
                     .intensity = 0.45F,
                     .delay = 0.15F});
      }
      break;
    }
    case MatchEventType::WOODWORK:
      pendingShot.active = false;
      play(MAJOR, {.sound = WOODWORK,
                   .gain = 0.75F,
                   .pan = panFor(x, 0.8F),
                   .intensity = 0.85F});
      play(MAJOR, {.sound = CROWD_OOH,
                   .gain = home ? 1.0F : 0.5F,
                   .pan = panFor(x, 0.2F),
                   .intensity = home ? 0.95F : 0.5F,
                   .delay = 0.05F});
      break;
    case MatchEventType::SHOT_BLOCKED:
      pendingShot.active = false;
      if (away)
      {  // a defender of the away side blocked a home shot
        play(MINOR, {.sound = CROWD_GROAN, .gain = 0.35F, .intensity = 0.2F});
      }
      break;
    case MatchEventType::SHOT_OFF_TARGET:
    case MatchEventType::PENALTY_MISSED:
      reactToMiss(x);
      pendingShot.active = false;
      break;
    case MatchEventType::GOAL_KICK:
      if (pendingShot.active)
      {
        reactToMiss(x);
      }
      pendingShot.active = false;
      break;
    case MatchEventType::CORNER:
      if (pendingShot.active)
      {
        reactToMiss(x);
      }
      pendingShot.active = false;
      if (home)
      {
        play(MINOR, {.sound = CROWD_CHEER,
                     .gain = 0.3F,
                     .intensity = 0.1F,
                     .delay = 0.3F});
        tension = std::max(tension, 0.15F);
      }
      break;
    case MatchEventType::FOUL:
      // The team fields describe the offender.
      whistle(MINOR, WhistlePattern::SHORT, x);
      if (away)
      {
        play(MINOR, {.sound = CROWD_BOO,
                     .gain = 0.35F,
                     .intensity = 0.2F,
                     .delay = 0.2F});
        if (x > 0.66F)
        {
          play(MINOR, {.sound = CROWD_WHISTLES,
                       .gain = 0.3F,
                       .intensity = 0.3F,
                       .delay = 0.25F});
        }
      }
      break;
    case MatchEventType::YELLOW_CARD:
      if (away)
      {
        play(MINOR, {.sound = CROWD_CHEER, .gain = 0.35F, .intensity = 0.12F});
      }
      else if (home)
      {
        play(MINOR,
             {.sound = CROWD_WHISTLES, .gain = 0.35F, .intensity = 0.4F});
      }
      break;
    case MatchEventType::SECOND_YELLOW:
    case MatchEventType::RED_CARD:
      if (away)
      {
        play(MAJOR, {.sound = CROWD_CHEER, .gain = 0.6F, .intensity = 0.4F});
      }
      else if (home)
      {
        play(MAJOR, {.sound = CROWD_BOO, .gain = 0.8F, .intensity = 0.8F});
        play(MAJOR, {.sound = CROWD_WHISTLES,
                     .gain = 0.5F,
                     .intensity = 0.7F,
                     .delay = 0.2F});
      }
      break;
    case MatchEventType::PENALTY:
      // The team fields describe the side awarded the kick.
      whistle(MAJOR, WhistlePattern::SHORT, x);
      if (home)
      {
        play(MAJOR, {.sound = CROWD_CHEER,
                     .gain = 0.8F,
                     .intensity = 0.55F,
                     .delay = 0.2F});
        tension = 0.45F;
      }
      else if (away)
      {
        play(MAJOR, {.sound = CROWD_BOO,
                     .gain = 0.85F,
                     .intensity = 0.8F,
                     .delay = 0.2F});
        play(MAJOR, {.sound = CROWD_WHISTLES,
                     .gain = 0.55F,
                     .intensity = 0.8F,
                     .delay = 0.3F});
      }
      break;
    case MatchEventType::OFFSIDE:
      whistle(MINOR, WhistlePattern::SHORT, x);
      if (home)
      {
        play(MINOR, {.sound = CROWD_GROAN,
                     .gain = 0.3F,
                     .intensity = 0.15F,
                     .delay = 0.2F});
      }
      break;
    case MatchEventType::SUBSTITUTION:
      if (home)
      {
        play(MINOR, {.sound = CROWD_APPLAUSE, .gain = 0.3F, .intensity = 0.2F});
      }
      break;
    default:
      break;
  }
}

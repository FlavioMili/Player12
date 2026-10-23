// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "audio/audio_mixer.h"
#include "audio/match_audio.h"
#include "audio/synth.h"
#include "model/match_engine.h"
#include "model/player.h"
#include "model/settings_manager.h"
#include "model/team.h"

using Audio::BusLevels;
using Audio::Mixer;
using Audio::Sound;
using Audio::SoundCommand;

namespace
{
constexpr int BLOCK = 480;  // 10 ms

struct Levels
{
  float peak = 0.0f;
  float rms = 0.0f;
  bool finite = true;
};

/** Renders `seconds` of the mixer and measures it. */
Levels renderSeconds(Mixer& mixer, float seconds,
                     std::vector<float>* capture = nullptr)
{
  std::vector<float> block(2 * BLOCK);
  const auto blocks = static_cast<int>(seconds * Audio::SAMPLE_RATE_F /
                                       static_cast<float>(BLOCK));
  double sum = 0.0;
  std::size_t count = 0;
  Levels levels;
  for (int index = 0; index < blocks; ++index)
  {
    mixer.render(block.data(), BLOCK);
    for (float sample : block)
    {
      if (!std::isfinite(sample)) levels.finite = false;
      levels.peak = std::max(levels.peak, std::abs(sample));
      sum += static_cast<double>(sample) * sample;
    }
    count += block.size();
    if (capture != nullptr)
      capture->insert(capture->end(), block.begin(), block.end());
  }
  levels.rms =
      count > 0
          ? static_cast<float>(std::sqrt(sum / static_cast<double>(count)))
          : 0.0f;
  return levels;
}

MatchEvent makeEvent(MatchEventType type, float minute, bool home,
                     int homeScore = 0, int awayScore = 0, int period = 1)
{
  MatchEvent event;
  event.type = type;
  event.timeMinute = minute;
  event.period = period;
  event.hasTeam = true;
  event.isHomeTeam = home;
  event.primaryPlayerId = home ? 105 : 205;
  event.homeScore = homeScore;
  event.awayScore = awayScore;
  event.position = {home ? 0.9f : 0.1f, 0.5f};
  return event;
}

MatchAudioFrame frameAt(float minute, int period = 1)
{
  MatchAudioFrame frame;
  frame.matchMinute = minute;
  frame.period = period;
  return frame;
}

/** Sounds started by event reactions (everything but ball contacts). */
std::uint64_t reactionCount(const Mixer& mixer)
{
  std::uint64_t total = 0;
  for (std::size_t index = 0; index < Audio::SOUND_COUNT; ++index)
    if (static_cast<Sound>(index) != Sound::KICK)
      total += mixer.triggeredCount(static_cast<Sound>(index));
  return total;
}

StatsConfig createStatsConfig()
{
  StatsConfig config;
  config.possible_stats = {"Pace",      "Shooting",  "Passing",
                           "Dribbling", "Defending", "Physicality",
                           "Stamina",   "Vision",    "Goalkeeping"};
  config.role_focus["Goalkeeper"] = {{"Goalkeeping", "Vision", "Physicality"},
                                     {0.7, 0.2, 0.1}};
  config.role_focus["Defender"] = {
      {"Defending", "Physicality", "Pace", "Vision"}, {0.4, 0.3, 0.15, 0.15}};
  config.role_focus["Midfielder"] = {
      {"Passing", "Vision", "Stamina", "Dribbling"}, {0.3, 0.3, 0.2, 0.2}};
  config.role_focus["Striker"] = {
      {"Shooting", "Pace", "Dribbling", "Physicality"}, {0.4, 0.2, 0.2, 0.2}};
  return config;
}

Team createTeam(TeamID id, const std::string& name,
                std::vector<std::unique_ptr<Player>>& players)
{
  using enum PlayerRole;
  Team team(id, 1, name, 50'000'000, {}, Strategy{}, Lineup{});
  static constexpr std::array ROLES = {GK, LB, CB, CB, RB, CM,
                                       CM, LW, RW, ST, ST};
  static constexpr std::array<Vector2F, 11> POSITIONS = {{{0.04f, 0.50f},
                                                          {0.18f, 0.12f},
                                                          {0.18f, 0.38f},
                                                          {0.18f, 0.62f},
                                                          {0.18f, 0.88f},
                                                          {0.43f, 0.35f},
                                                          {0.43f, 0.65f},
                                                          {0.68f, 0.16f},
                                                          {0.68f, 0.84f},
                                                          {0.78f, 0.38f},
                                                          {0.78f, 0.62f}}};
  for (uint32_t index = 0; index < 11; ++index)
  {
    const std::map<std::string, float> stats = {
        {"Pace", 65.0f},      {"Shooting", 65.0f},  {"Passing", 65.0f},
        {"Dribbling", 65.0f}, {"Defending", 65.0f}, {"Physicality", 65.0f},
        {"Stamina", 65.0f},   {"Vision", 65.0f},    {"Goalkeeping", 65.0f}};
    auto player = std::make_unique<Player>(
        static_cast<PlayerID>(id) * 100U + index, id, "First",
        std::to_string(index), ROLES[index], Language::EN, 100'000, 0, 25, 3,
        180, Foot::Right, stats);
    if (ROLES[index] == GK)
      team.getLineup().setGoalkeeper(player.get());
    else
      team.getLineup().addOutfieldPlayer(player.get(), POSITIONS[index]);
    players.push_back(std::move(player));
  }
  return team;
}

void writeWav(const std::string& path, const std::vector<float>& samples)
{
  std::ofstream out(path, std::ios::binary);
  const auto put32 = [&out](std::uint32_t value)
  { out.write(reinterpret_cast<const char*>(&value), 4); };
  const auto put16 = [&out](std::uint16_t value)
  { out.write(reinterpret_cast<const char*>(&value), 2); };
  const auto dataBytes = static_cast<std::uint32_t>(samples.size() * 2);
  out.write("RIFF", 4);
  put32(36 + dataBytes);
  out.write("WAVEfmt ", 8);
  put32(16);
  put16(1);
  put16(2);
  put32(Audio::SAMPLE_RATE);
  put32(Audio::SAMPLE_RATE * 4);
  put16(4);
  put16(16);
  out.write("data", 4);
  put32(dataBytes);
  for (float sample : samples)
    put16(static_cast<std::uint16_t>(static_cast<std::int16_t>(
        std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f))));
}
}  // namespace

static_assert(std::is_trivially_copyable_v<SoundCommand>,
              "sound commands cross to the audio thread by copy");
static_assert(std::is_trivially_copyable_v<MatchAudioFrame>,
              "the audio gets plain copies of the match state");

TEST(AudioMixerTest, OutputStaysBoundedWithEveryVoiceAtFullLevel)
{
  Mixer mixer;
  mixer.setLevels({1.0f, 1.0f, 1.0f, false});
  mixer.setCrowd(1.0f, 1.0f);
  for (int round = 0; round < 3; ++round)
    for (std::size_t index = 0; index < Audio::SOUND_COUNT; ++index)
      EXPECT_TRUE(mixer.trigger({.sound = static_cast<Sound>(index),
                                 .gain = 1.0f,
                                 .intensity = 1.0f,
                                 .variant = 3}));
  const Levels levels = renderSeconds(mixer, 4.0f);
  EXPECT_TRUE(levels.finite);
  EXPECT_LE(levels.peak, 1.0f);
  EXPECT_GT(levels.rms, 0.01f);
  EXPECT_EQ(Audio::softLimit(50.0f), 0.999f);
  EXPECT_EQ(Audio::softLimit(-50.0f), -0.999f);
  EXPECT_EQ(Audio::softLimit(0.5f), 0.5f);
}

TEST(AudioMixerTest, EveryVoiceFinishesAndThePoolIsBounded)
{
  Mixer mixer;
  for (int index = 0; index < 60; ++index)
  {
    mixer.trigger({.sound = Sound::KICK, .intensity = 0.5f});
    if (index % 20 == 19) renderSeconds(mixer, 0.01f);
  }
  renderSeconds(mixer, 0.01f);
  EXPECT_LE(mixer.activeVoices(), Mixer::MAX_VOICES);
  EXPECT_GT(mixer.activeVoices(), 0);
  renderSeconds(mixer, 1.0f);
  EXPECT_EQ(mixer.activeVoices(), 0);
  // The longest sound (a full-strength cheer) ends too.
  mixer.trigger({.sound = Sound::CROWD_CHEER, .intensity = 1.0f});
  renderSeconds(mixer, 9.0f);
  EXPECT_EQ(mixer.activeVoices(), 0);
}

TEST(AudioMixerTest, BusesAndMuteControlTheOutput)
{
  // Crowd bed on its own bus.
  Mixer crowdOnly;
  crowdOnly.setLevels({1.0f, 1.0f, 0.0f, false});
  renderSeconds(crowdOnly, 1.0f);
  const float bedRms = renderSeconds(crowdOnly, 1.0f).rms;
  EXPECT_GT(bedRms, 0.005f);

  Mixer crowdMuted;
  crowdMuted.setLevels({1.0f, 0.0f, 1.0f, false});
  renderSeconds(crowdMuted, 0.5f);
  EXPECT_LT(renderSeconds(crowdMuted, 1.0f).rms, 1e-4f);
  // The effects bus still carries a whistle.
  crowdMuted.trigger(
      {.sound = Sound::WHISTLE,
       .variant = std::to_underlying(Audio::WhistlePattern::KICK_OFF)});
  EXPECT_GT(renderSeconds(crowdMuted, 0.5f).rms, 0.01f);

  // Master at zero silences everything.
  Mixer masterOff;
  masterOff.setLevels({0.0f, 1.0f, 1.0f, false});
  masterOff.trigger({.sound = Sound::CROWD_CHEER, .intensity = 1.0f});
  EXPECT_LT(renderSeconds(masterOff, 1.0f).peak, 1e-6f);

  // Mute outputs exact silence and drops the playing voices.
  Mixer muted;
  muted.trigger({.sound = Sound::CROWD_CHEER, .intensity = 1.0f});
  renderSeconds(muted, 0.2f);
  EXPECT_GT(muted.activeVoices(), 0);
  muted.setLevels({1.0f, 1.0f, 1.0f, true});
  const Levels silent = renderSeconds(muted, 0.5f);
  EXPECT_EQ(silent.peak, 0.0f);
  EXPECT_EQ(muted.activeVoices(), 0);
  // Unmuting brings the crowd back without a jump.
  muted.setLevels({1.0f, 1.0f, 1.0f, false});
  const Levels back = renderSeconds(muted, 1.0f);
  EXPECT_GT(back.rms, 0.001f);
  EXPECT_LE(back.peak, 1.0f);
}

TEST(AudioMixerTest, RendersFarFasterThanRealTime)
{
  using enum Audio::Sound;
  Mixer mixer;
  mixer.setCrowd(0.6f, 1.0f);
  const auto started = std::chrono::steady_clock::now();
  for (int second = 0; second < 10; ++second)
  {
    mixer.trigger({.sound = KICK, .intensity = 0.4f});
    mixer.trigger({.sound = CROWD_OOH, .intensity = 0.5f});
    if (second % 3 == 0) mixer.trigger({.sound = CROWD_APPLAUSE});
    renderSeconds(mixer, 1.0f);
  }
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  // Ten seconds of busy match audio; a generous bound for loaded machines
  // (typically well under 1% of a core).
  RecordProperty("cpu_share_percent", std::to_string(seconds * 10.0));
  EXPECT_LT(seconds, 0.5);
}

TEST(MatchEventCursorTest, ReportsEachEventOnceAcrossReplaysAndEviction)
{
  using enum MatchEventType;
  MatchEventCursor cursor;
  std::vector<const MatchEvent*> fresh;
  std::vector log = {
      makeEvent(KICK_OFF, 0.0f, true), makeEvent(FOUL, 3.0f, false),
      makeEvent(YELLOW_CARD, 3.0f, false), makeEvent(FREE_KICK, 3.0f, true)};
  cursor.collect(log, fresh);
  EXPECT_EQ(fresh.size(), 4U);  // same-minute events are all distinct

  fresh.clear();
  cursor.collect(log, fresh);
  EXPECT_TRUE(fresh.empty());

  log.push_back(makeEvent(GOAL, 7.5f, true, 1, 0));
  fresh.clear();
  cursor.collect(log, fresh);
  ASSERT_EQ(fresh.size(), 1U);
  EXPECT_EQ(fresh[0]->type, GOAL);

  // The engine's log drops its oldest entries once full.
  log.erase(log.begin(), log.begin() + 2);
  log.push_back(makeEvent(CORNER, 9.0f, false, 1, 0));
  fresh.clear();
  cursor.collect(log, fresh);
  ASSERT_EQ(fresh.size(), 1U);
  EXPECT_EQ(fresh[0]->type, CORNER);

  // A replay (a new engine playing the same match from kick-off) or a seek
  // back never repeats what was heard...
  std::vector replay = {makeEvent(KICK_OFF, 0.0f, true),
                        makeEvent(FOUL, 3.0f, false),
                        makeEvent(YELLOW_CARD, 3.0f, false)};
  fresh.clear();
  cursor.collect(replay, fresh);
  EXPECT_TRUE(fresh.empty());
  replay.push_back(makeEvent(GOAL, 7.5f, true, 1, 0));
  replay.push_back(makeEvent(CORNER, 9.0f, false, 1, 0));
  fresh.clear();
  cursor.collect(replay, fresh);
  EXPECT_TRUE(fresh.empty());
  // ...but play past it is heard, second half included.
  replay.push_back(makeEvent(SECOND_HALF, 45.0f, true, 1, 0, 2));
  fresh.clear();
  cursor.collect(replay, fresh);
  ASSERT_EQ(fresh.size(), 1U);
  EXPECT_EQ(fresh[0]->type, SECOND_HALF);

  // Skipped events are consumed silently.
  replay.push_back(makeEvent(SHOT, 50.0f, true, 1, 0, 2));
  cursor.skipAll(replay);
  fresh.clear();
  cursor.collect(replay, fresh);
  EXPECT_TRUE(fresh.empty());
}

TEST(MatchAudioTest, MapsEventsToSoundsOnceAndFromTheHomeCrowdsSide)
{
  MatchAudio audio(false);
  const Mixer& mixer = audio.mixer();
  std::vector log = {makeEvent(MatchEventType::KICK_OFF, 0.0f, true)};
  audio.update(0.016f, frameAt(0.0f), log);
  EXPECT_EQ(mixer.triggeredCount(Sound::WHISTLE), 1U);

  log.push_back(makeEvent(MatchEventType::GOAL, 12.0f, true, 1, 0));
  audio.update(0.016f, frameAt(12.0f), log);
  EXPECT_EQ(mixer.triggeredCount(Sound::NET), 1U);
  // One cheer greeted the kick-off, one the goal.
  EXPECT_EQ(mixer.triggeredCount(Sound::CROWD_CHEER), 2U);
  EXPECT_EQ(mixer.triggeredCount(Sound::CROWD_GROAN), 0U);

  // The same log again (next frame, a replay, a seek) adds nothing.
  const std::uint64_t heard = reactionCount(mixer);
  for (int frame = 0; frame < 5; ++frame)
    audio.update(0.016f, frameAt(12.0f), log);
  EXPECT_EQ(reactionCount(mixer), heard);

  // An away goal: the home crowd groans, the away end cheers quietly.
  log.push_back(makeEvent(MatchEventType::GOAL, 20.0f, false, 1, 1));
  audio.update(0.016f, frameAt(20.0f), log);
  EXPECT_EQ(mixer.triggeredCount(Sound::CROWD_GROAN), 1U);
  EXPECT_EQ(mixer.triggeredCount(Sound::CROWD_CHEER), 3U);

  // A foul by the away side against the home team: whistle and boos.
  log.push_back(makeEvent(MatchEventType::FOUL, 30.0f, false, 1, 1));
  audio.update(1.0f, frameAt(30.0f), log);
  EXPECT_EQ(mixer.triggeredCount(Sound::WHISTLE), 2U);
  EXPECT_EQ(mixer.triggeredCount(Sound::CROWD_BOO), 1U);

  // A home shot that ends in a goal kick was a near miss: "ooh".
  MatchEvent shot = makeEvent(MatchEventType::SHOT, 33.0f, true, 1, 1);
  shot.xg = 0.3f;
  log.push_back(shot);
  log.push_back(makeEvent(MatchEventType::GOAL_KICK, 33.1f, false, 1, 1));
  audio.update(1.0f, frameAt(33.1f), log);
  EXPECT_EQ(mixer.triggeredCount(Sound::CROWD_OOH), 1U);

  // Woodwork rings the post.
  log.push_back(makeEvent(MatchEventType::WOODWORK, 40.0f, true, 1, 1));
  audio.update(1.0f, frameAt(40.0f), log);
  EXPECT_EQ(mixer.triggeredCount(Sound::WOODWORK), 1U);

  // Full time: the long whistle.
  log.push_back(makeEvent(MatchEventType::FULL_TIME, 94.0f, true, 1, 1, 2));
  audio.update(1.0f, frameAt(94.0f, 2), log);
  EXPECT_EQ(mixer.triggeredCount(Sound::WHISTLE), 3U);
}

TEST(MatchAudioTest, SkipsJumpsAndThinsOutFastPlayback)
{
  MatchAudio audio(false);
  const Mixer& mixer = audio.mixer();
  std::vector log = {makeEvent(MatchEventType::KICK_OFF, 0.0f, true)};
  audio.update(0.016f, frameAt(0.0f), log);
  const std::uint64_t afterKickOff = reactionCount(mixer);

  // A highlight skip brings a batch of events: none of them is played.
  log.push_back(makeEvent(MatchEventType::FOUL, 10.0f, false));
  log.push_back(makeEvent(MatchEventType::GOAL, 14.0f, true, 1, 0));
  MatchAudioFrame skip = frameAt(14.0f);
  skip.skipped = true;
  audio.update(0.016f, skip, log);
  audio.update(0.016f, frameAt(14.0f), log);
  EXPECT_EQ(reactionCount(mixer), afterKickOff);

  // Events that arrive long after they happened are not played either.
  log.push_back(makeEvent(MatchEventType::FOUL, 16.0f, false, 1, 0));
  audio.update(0.016f, frameAt(18.0f), log);
  EXPECT_EQ(reactionCount(mixer), afterKickOff);

  // At 16x only the key moments are heard: no foul whistle, but goals.
  MatchAudioFrame fast = frameAt(25.0f);
  fast.playbackSpeed = 16.0f;
  log.push_back(makeEvent(MatchEventType::FOUL, 25.0f, false, 1, 0));
  audio.update(1.0f, fast, log);
  EXPECT_EQ(reactionCount(mixer), afterKickOff);
  fast.matchMinute = 26.0f;
  log.push_back(makeEvent(MatchEventType::GOAL, 26.0f, true, 2, 0));
  audio.update(1.0f, fast, log);
  EXPECT_EQ(mixer.triggeredCount(Sound::NET), 1U);
  EXPECT_GE(mixer.triggeredCount(Sound::CROWD_CHEER), 2U);

  // Kicks are not played above 4x.
  const std::uint64_t kicks = mixer.triggeredCount(Sound::KICK);
  fast.ballLoose = false;
  audio.update(0.016f, fast, log);
  fast.ballLoose = true;
  fast.ballSpeed = 20.0f;
  fast.kickerId = 7;
  audio.update(0.016f, fast, log);
  EXPECT_EQ(mixer.triggeredCount(Sound::KICK), kicks);
}

TEST(MatchAudioTest, CommandsAreCopiesIndependentOfTheEventLog)
{
  MatchAudio audio(false);
  {
    std::vector log = {makeEvent(MatchEventType::KICK_OFF, 0.0f, true),
                       makeEvent(MatchEventType::GOAL, 0.5f, true, 1, 0)};
    audio.update(0.016f, frameAt(0.5f), log);
  }
  // The log is gone; the queued sounds still render from their own data.
  const Levels levels = renderSeconds(audio.mixer(), 1.0f);
  EXPECT_TRUE(levels.finite);
  EXPECT_GT(levels.rms, 0.01f);
  EXPECT_LE(levels.peak, 1.0f);
}

TEST(MatchAudioTest, FollowsALiveMatchWithoutRepeatingItOnReplay)
{
  std::vector<std::unique_ptr<Player>> players;
  Team home = createTeam(1, "Home", players);
  Team away = createTeam(2, "Away", players);
  const StatsConfig config = createStatsConfig();
  constexpr float FRAME = 1.0f / 60.0f;
  constexpr float SPEED = 4.0f;
  constexpr int FRAMES = 60 * 150;  // ten simulated minutes at 4x

  MatchAudio audio(false);
  Mixer& mixer = audio.mixer();
  const auto play = [&](MatchEngine& engine)
  {
    std::vector<float> scratch(2 * BLOCK);
    for (int frame = 0; frame < FRAMES; ++frame)
    {
      engine.update(FRAME * SPEED);
      audio.update(FRAME, captureMatchAudioFrame(engine, SPEED, false, false),
                   engine.getEvents());
      // Keep the command ring drained like the audio thread would.
      if (frame % 2 == 0) mixer.render(scratch.data(), BLOCK);
    }
  };

  MatchEngine first(home.getLineup(), away.getLineup(), home.getStrategy(),
                    away.getStrategy(), config, 424242);
  play(first);
  const std::uint64_t kicks = mixer.triggeredCount(Sound::KICK);
  const std::uint64_t reactions = reactionCount(mixer);
  const float minutes = first.getMatchTimeMinutes();
  EXPECT_GT(minutes, 9.0f);
  // Passes, shots and clearances all sound; dribbling does not flood it.
  EXPECT_GT(static_cast<float>(kicks) / minutes, 4.0f);
  EXPECT_LT(static_cast<float>(kicks) / minutes, 120.0f);
  EXPECT_GE(mixer.triggeredCount(Sound::WHISTLE), 1U);  // kick-off

  // The same match again (same seed) repeats nothing already heard.
  MatchEngine replay(home.getLineup(), away.getLineup(), home.getStrategy(),
                     away.getStrategy(), config, 424242);
  play(replay);
  EXPECT_EQ(reactionCount(mixer), reactions);
  EXPECT_EQ(mixer.triggeredCount(Sound::KICK), kicks);
}

TEST(MatchAudioTest, OpensTheDummyDeviceAndPlaysThroughIt)
{
  // The test environment selects SDL's dummy driver; nothing is audible.
  if (const char* driver = std::getenv("SDL_AUDIODRIVER");
      driver == nullptr || std::string(driver) != "dummy")
    GTEST_SKIP() << "needs SDL_AUDIODRIVER=dummy";
  MatchAudio audio(true);
  ASSERT_TRUE(audio.deviceOpen());
  audio.mixer().trigger(
      {.sound = Sound::WHISTLE,
       .variant = std::to_underlying(Audio::WhistlePattern::FULL_TIME)});
  bool playing = false;
  for (int attempt = 0; attempt < 100; ++attempt)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    playing = audio.mixer().activeVoices() > 0;
    if (playing) break;
  }
  EXPECT_TRUE(playing);
}

TEST(MatchAudioTest, SettingsCarryTheVolumesAndMute)
{
  Settings settings;
  settings.master_volume = 0.5f;
  settings.crowd_volume = 0.25f;
  settings.effects_volume = 0.75f;
  settings.audio_muted = true;
  const BusLevels levels = MatchAudio::levelsFrom(settings);
  EXPECT_FLOAT_EQ(levels.master, 0.5f);
  EXPECT_FLOAT_EQ(levels.crowd, 0.25f);
  EXPECT_FLOAT_EQ(levels.effects, 0.75f);
  EXPECT_TRUE(levels.muted);
}

TEST(MatchAudioTest, RendersAMatchMomentAtSaneLevels)
{
  // Bed, a pass, a foul, a shot and a home goal: the crowd bed sits well
  // below the goal roar and nothing clips.
  MatchAudio audio(false);
  audio.setLevels({0.8f, 0.8f, 0.8f, false});
  Mixer& mixer = audio.mixer();
  std::vector<float> capture;
  std::vector<MatchEvent> log;
  MatchAudioFrame frame = frameAt(0.0f);
  const auto step = [&](float seconds)
  {
    const auto frames = static_cast<int>(seconds * 100.0f);
    for (int index = 0; index < frames; ++index)
    {
      audio.update(0.01f, frame, log);
      renderSeconds(mixer, 0.01f, &capture);
      frame.matchMinute += 0.01f / 60.0f;
    }
  };
  step(1.5f);
  const Levels bed = [&]
  {
    Levels measured;
    double sum = 0.0;
    for (float sample : capture)
    {
      measured.peak = std::max(measured.peak, std::abs(sample));
      sum += static_cast<double>(sample) * sample;
    }
    measured.rms = static_cast<float>(
        std::sqrt(sum / static_cast<double>(capture.size())));
    return measured;
  }();
  // A pass.
  frame.ballLoose = true;
  frame.ballSpeed = 14.0f;
  frame.kickerId = 105;
  frame.ballX = 0.4f;
  step(0.5f);
  log.push_back(makeEvent(MatchEventType::FOUL, frame.matchMinute, false));
  step(1.5f);
  // A home attack and shot, then the goal.
  frame.homeInPossession = true;
  frame.ballX = 0.85f;
  frame.ballLoose = false;
  step(1.0f);
  MatchEvent shot = makeEvent(MatchEventType::SHOT, frame.matchMinute, true);
  shot.xg = 0.35f;
  log.push_back(shot);
  frame.ballLoose = true;
  frame.ballIsShot = true;
  frame.ballSpeed = 26.0f;
  step(0.4f);
  const std::size_t goalStart = capture.size();
  log.push_back(makeEvent(MatchEventType::GOAL, frame.matchMinute, true, 1, 0));
  frame.ballIsShot = false;
  frame.ballSpeed = 0.0f;
  step(4.0f);
  double goalSum = 0.0;
  float peak = 0.0f;
  for (std::size_t index = goalStart; index < capture.size(); ++index)
    goalSum += static_cast<double>(capture[index]) * capture[index];
  for (float sample : capture) peak = std::max(peak, std::abs(sample));
  const auto goalRms = static_cast<float>(
      std::sqrt(goalSum / static_cast<double>(capture.size() - goalStart)));

  RecordProperty("bed_rms", std::to_string(bed.rms));
  RecordProperty("goal_rms", std::to_string(goalRms));
  RecordProperty("peak", std::to_string(peak));
  EXPECT_GT(bed.rms, 0.01f);
  EXPECT_LT(bed.rms, 0.15f);
  EXPECT_GT(goalRms, bed.rms * 2.0f);
  EXPECT_LE(peak, 1.0f);
  if (const char* path = std::getenv("FM_AUDIO_WAV"); path != nullptr && *path)
    writeWav(path, capture);
}

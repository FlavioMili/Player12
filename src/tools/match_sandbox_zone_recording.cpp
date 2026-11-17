// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_zone_recording.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "global/build_info.h"
#include "model/player.h"
#include "tools/match_sandbox_recorder.h"
#include "tools/match_sandbox_recording.h"
#include "tools/match_sandbox_session.h"
#include "tools/match_sandbox_setup.h"

using json = nlohmann::json;

namespace
{
/** A zone recording's file ends so (the match recordings' don't). */
constexpr std::string_view SUFFIX = " zone.json";

std::string localTime()
{
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#ifdef _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  constexpr std::size_t TIMESTAMP_CAPACITY = 32;
  std::array<char, TIMESTAMP_CAPACITY> text{};
  const auto length =
      std::strftime(text.data(), text.size(), "%Y-%m-%d %H:%M", &local);
  return {text.data(), length};
}

template <typename Id, typename KeyOf>
Id keyed(const json& value, const char* name, KeyOf fromKey, std::string& error)
{
  const auto id = fromKey(value.at(name).get<std::string>());
  if (!id)
  {
    error = std::format("unknown {} \"{}\"", name,
                        value.at(name).get<std::string>());
  }
  return id.value_or(Id{});
}
}  // namespace

ZoneRecordings::Replay::~Replay()
{
  // The run borrows the drill and the players: it goes first.
  run.reset();
}

std::optional<ZoneRecording> ZoneRecordings::capture(const SessionRun& session,
                                                     std::size_t zoneIndex,
                                                     std::string& error)
{
  if (session.changing() || zoneIndex >= session.zones().size())
  {
    error = "no zone playing to save";
    return std::nullopt;
  }
  const SessionRun::Zone& zone = session.zones()[zoneIndex];
  if (!zone.run || !zone.drill)
  {
    error = "the zone has no run";
    return std::nullopt;
  }
  const MatchEngine& engine = zone.run->engine();
  ZoneRecording recording;
  recording.created = localTime();
  recording.build = BuildInfo::summary();
  recording.session = session.plan().name;
  recording.exercise = session.plan().exercises[session.exercise()].name;
  recording.zone = zone.plan->name;
  recording.drill = zone.plan->drill;
  recording.settings = zone.plan->settings;
  recording.seed = zone.drill->seed;
  recording.brains = {engine.getBrain(true), engine.getBrain(false)};
  recording.bodies = {engine.getBody(true), engine.getBody(false)};
  recording.handshake = engine.getHandshake();
  recording.referee = engine.getReferee();
  recording.managers = {engine.getManager(true), engine.getManager(false)};
  recording.ball = engine.getBallPhysics();
  recording.outOfPlay = engine.getOutOfPlay();
  recording.possession = engine.getPossessionVersion();
  recording.homeAdvantage = sandboxHomeAdvantage();
  for (const std::size_t index : zone.plan->players)
  {
    const Player& player = *session.squad()[index];
    recording.players.push_back({.id = player.getId(),
                                 .name = player.getName(),
                                 .role = player.getRole(),
                                 .stats = player.getStats(),
                                 .height = player.getHeight(),
                                 .foot = player.getFoot()});
    if (const auto start = zone.startStates.find(player.getId());
        start != zone.startStates.end())
    {
      recording.startStates.push_back(start->second);
    }
  }
  {
    SandboxRecorder& recorder = const_cast<DrillRun&>(*zone.run).recorder();
    const auto guard = recorder.lock();
    // The commands given after the drill's start (its start gives its own
    // again when it is rebuilt).
    for (const MatchCommandRecord& command : recorder.commands())
    {
      if (command.step > 0)
      {
        recording.commands.push_back(command);
      }
    }
    recording.lastTick = recorder.lastTick();
    recording.checksums.resize(recording.lastTick + 1, 0);
    for (std::uint64_t tick = 0; tick <= recording.lastTick; ++tick)
    {
      if (const auto checksum = recorder.checksum(tick))
      {
        recording.checksums[tick] = *checksum;
      }
    }
  }
  recording.homeScore = engine.getHomeScore();
  recording.awayScore = engine.getAwayScore();
  return recording;
}

std::string ZoneRecordings::fileName(const ZoneRecording& recording)
{
  std::string name = std::format("{} {}, {}{}", recording.created,
                                 recording.session, recording.zone, SUFFIX);
  for (char& character : name)
  {
    if (std::string_view("<>:\"/\\|?*").find(character) !=
        std::string_view::npos)
    {
      character = '-';
    }
  }
  return name;
}

bool ZoneRecordings::save(const ZoneRecording& recording,
                          const std::filesystem::path& path, std::string& error)
{
  json root;
  root["version"] = ZoneRecording::VERSION;
  root["kind"] = "training zone";
  root["created"] = recording.created;
  root["build"] = recording.build;
  root["session"] = recording.session;
  root["exercise"] = recording.exercise;
  root["zone"] = recording.zone;
  root["drill"] = recording.drill;
  root["settings"] = recording.settings;
  root["seed"] = recording.seed;
  root["brains"] = {brainKey(recording.brains[0]),
                    brainKey(recording.brains[1])};
  root["bodies"] = {bodyKey(recording.bodies[0]), bodyKey(recording.bodies[1])};
  root["handshake"] = handshakeKey(recording.handshake);
  root["referee"] = refereeKey(recording.referee);
  root["managers"] = {managerKey(recording.managers[0]),
                      managerKey(recording.managers[1])};
  root["ball"] = ballPhysicsKey(recording.ball);
  root["out_of_play"] = outOfPlayKey(recording.outOfPlay);
  root["possession"] = possessionKey(recording.possession);
  root["home_advantage"] = recording.homeAdvantage;
  root["players"] = json::array();
  for (const ZoneRecording::PlayerSpec& player : recording.players)
  {
    root["players"].push_back({{"id", player.id},
                               {"name", player.name},
                               {"role", static_cast<int>(player.role)},
                               {"stats", player.stats},
                               {"height", player.height},
                               {"foot", static_cast<int>(player.foot)}});
  }
  root["start_states"] = json::array();
  for (const MatchEngine::CarriedPlayer& state : recording.startStates)
  {
    root["start_states"].push_back(
        {{"id", state.id},
         {"position", {state.position.x, state.position.y}},
         {"velocity", {state.velocity.x, state.velocity.y}},
         {"facing", state.facing},
         {"stamina", state.stamina},
         {"sprint_reserve", state.sprintReserve},
         {"fast_burst_debt", state.fastBurstDebt},
         {"load_speed", state.loadSpeed}});
  }
  root["commands"] = json::array();
  for (const MatchCommandRecord& command : recording.commands)
  {
    root["commands"].push_back(Recordings::commandToJson(command));
  }
  root["checksums"] = recording.checksums;
  root["last_tick"] = recording.lastTick;
  root["score"] = {recording.homeScore, recording.awayScore};
  std::error_code code;
  std::filesystem::create_directories(path.parent_path(), code);
  std::ofstream file(path);
  if (!file)
  {
    error = "can't write " + path.string();
    return false;
  }
  file << root.dump();
  return static_cast<bool>(file);
}

std::optional<ZoneRecording> ZoneRecordings::load(
    const std::filesystem::path& path, std::string& error)
{
  error.clear();
  try
  {
    std::ifstream file(path);
    if (!file)
    {
      error = "can't read " + path.string();
      return std::nullopt;
    }
    const json root = json::parse(file);
    if (root.value("kind", std::string()) != "training zone" ||
        root.value("version", 0) != ZoneRecording::VERSION)
    {
      error = "not a training zone recording of this version";
      return std::nullopt;
    }
    ZoneRecording recording;
    recording.created = root.at("created").get<std::string>();
    recording.build = root.at("build").get<std::string>();
    recording.session = root.at("session").get<std::string>();
    recording.exercise = root.at("exercise").get<std::string>();
    recording.zone = root.at("zone").get<std::string>();
    recording.drill = root.at("drill").get<std::string>();
    recording.settings =
        root.at("settings").get<std::map<std::string, float>>();
    recording.seed = root.at("seed").get<std::uint32_t>();
    for (std::size_t side = 0; side < recording.brains.size(); ++side)
    {
      const auto brain =
          brainFromKey(root.at("brains").at(side).get<std::string>());
      const auto body =
          bodyFromKey(root.at("bodies").at(side).get<std::string>());
      const auto manager =
          managerFromKey(root.at("managers").at(side).get<std::string>());
      if (!brain || !body || !manager)
      {
        error = "unknown brain, body or manager";
        return std::nullopt;
      }
      recording.brains.at(side) = *brain;
      recording.bodies.at(side) = *body;
      recording.managers.at(side) = *manager;
    }
    recording.handshake =
        keyed<HandshakeId>(root, "handshake", handshakeFromKey, error);
    recording.referee =
        keyed<RefereeId>(root, "referee", refereeFromKey, error);
    recording.ball =
        keyed<BallPhysicsId>(root, "ball", ballPhysicsFromKey, error);
    recording.outOfPlay =
        keyed<OutOfPlayId>(root, "out_of_play", outOfPlayFromKey, error);
    recording.possession =
        keyed<PossessionId>(root, "possession", possessionFromKey, error);
    if (!error.empty())
    {
      return std::nullopt;
    }
    recording.homeAdvantage = root.at("home_advantage").get<float>();
    for (const json& player : root.at("players"))
    {
      recording.players.push_back(
          {.id = player.at("id").get<PlayerID>(),
           .name = player.at("name").get<std::string>(),
           .role = static_cast<PlayerRole>(player.at("role").get<int>()),
           .stats = player.at("stats").get<std::map<std::string, float>>(),
           .height = player.at("height").get<int>(),
           .foot = static_cast<Foot>(player.at("foot").get<int>())});
    }
    if (root.contains("start_states"))
    {
      for (const json& value : root.at("start_states"))
      {
        MatchEngine::CarriedPlayer state;
        state.id = value.at("id").get<PlayerID>();
        state.position = PitchPoint{value.at("position").at(0).get<float>(),
                                    value.at("position").at(1).get<float>()};
        state.velocity = Vector2F{.x = value.at("velocity").at(0).get<float>(),
                                  .y = value.at("velocity").at(1).get<float>()};
        state.facing = value.at("facing").get<float>();
        state.stamina = value.at("stamina").get<float>();
        state.sprintReserve = value.at("sprint_reserve").get<float>();
        state.fastBurstDebt = value.at("fast_burst_debt").get<float>();
        state.loadSpeed = value.at("load_speed").get<float>();
        recording.startStates.push_back(state);
      }
    }
    for (const json& command : root.at("commands"))
    {
      recording.commands.push_back(Recordings::commandFromJsonValue(command));
    }
    recording.checksums =
        root.at("checksums").get<std::vector<std::uint64_t>>();
    recording.lastTick = root.at("last_tick").get<std::uint64_t>();
    recording.homeScore = root.at("score").at(0).get<int>();
    recording.awayScore = root.at("score").at(1).get<int>();
    return recording;
  }
  catch (const std::exception& problem)
  {
    error = problem.what();
    return std::nullopt;
  }
}

std::vector<std::filesystem::path> ZoneRecordings::list()
{
  std::vector<std::filesystem::path> files;
  std::error_code code;
  for (const auto& entry :
       std::filesystem::directory_iterator(Recordings::folder(), code))
  {
    if (entry.is_regular_file() &&
        entry.path().filename().string().ends_with(SUFFIX))
    {
      files.push_back(entry.path());
    }
  }
  // The file names start with the date: newest first.
  std::ranges::sort(files, std::greater<>{});
  return files;
}

std::unique_ptr<ZoneRecordings::Replay> ZoneRecordings::replay(
    const ZoneRecording& recording, const StatsConfig& config)
{
  auto result = std::make_unique<Replay>();
  SessionZone zone;
  zone.drill = recording.drill;
  zone.settings = recording.settings;
  result->drill = makeSessionDrill(zone);
  if (!result->drill)
  {
    result->error = std::format("no drill called \"{}\"", recording.drill);
    return result;
  }
  result->drill->seed = recording.seed;
  std::vector<Player*> squad;
  constexpr int REFERENCE_PLAYER_AGE = 25;
  for (const ZoneRecording::PlayerSpec& spec : recording.players)
  {
    result->players.push_back(std::make_unique<Player>(
        spec.id, static_cast<TeamID>(1), "", spec.name, spec.role, Language::EN,
        0, 0, REFERENCE_PLAYER_AGE, 1, static_cast<std::uint8_t>(spec.height),
        spec.foot, spec.stats));
    squad.push_back(result->players.back().get());
  }
  // Built with the versions it ran (they stand in for the Set-up's while
  // the run is made), as the session built it.
  const auto brains = sandboxBrains();
  const auto bodies = sandboxBodies();
  const auto handshake = sandboxHandshake();
  const auto referee = sandboxReferee();
  const auto managers = sandboxManagers();
  const auto ball = sandboxBallPhysics();
  const auto outOfPlay = sandboxOutOfPlay();
  const auto possession = sandboxPossession();
  const auto homeAdvantage = sandboxHomeAdvantage();
  sandboxBrains() = recording.brains;
  sandboxBodies() = recording.bodies;
  sandboxHandshake() = recording.handshake;
  sandboxReferee() = recording.referee;
  sandboxManagers() = recording.managers;
  sandboxBallPhysics() = recording.ball;
  sandboxOutOfPlay() = recording.outOfPlay;
  sandboxPossession() = recording.possession;
  sandboxHomeAdvantage() = recording.homeAdvantage;
  result->run = std::make_unique<DrillRun>(
      *result->drill, config, true, std::function<void(MatchEngine&)>{}, squad);
  sandboxBrains() = brains;
  sandboxBodies() = bodies;
  sandboxHandshake() = handshake;
  sandboxReferee() = referee;
  sandboxManagers() = managers;
  sandboxBallPhysics() = ball;
  sandboxOutOfPlay() = outOfPlay;
  sandboxPossession() = possession;
  sandboxHomeAdvantage() = homeAdvantage;
  // Where its players started, then the commands given as it played.
  MatchEngine& engine = result->run->engine();
  for (const MatchEngine::CarriedPlayer& state : recording.startStates)
  {
    engine.carryPlayerIn(state);
  }
  engine.loadCommandReplay(recording.commands);
  while (!result->run->finished())
  {
    {
      const auto guard = result->run->recorder().lock();
      if (result->run->recorder().lastTick() >= recording.lastTick)
      {
        break;
      }
    }
    result->run->step();
  }
  // The first tick whose state is not the recorded one.
  const auto guard = result->run->recorder().lock();
  const std::uint64_t replayed = result->run->recorder().lastTick();
  const std::uint64_t common = std::min(replayed, recording.lastTick);
  for (std::uint64_t tick = 1; tick <= common && !result->divergedAt; ++tick)
  {
    const std::uint64_t expected =
        tick < recording.checksums.size() ? recording.checksums[tick] : 0;
    const auto actual = result->run->recorder().checksum(tick);
    if (expected != 0 && actual && *actual != expected)
    {
      result->divergedAt = tick;
    }
  }
  if (!result->divergedAt && replayed != recording.lastTick)
  {
    result->divergedAt = common + 1;
  }
  return result;
}

int runZoneRoundTrip(const StatsConfig& config)
{
  const SessionPlan& plan = sessionPlans().front();
  SessionRun session(plan, config, 1, true);
  int failures = 0;
  std::size_t checked = 0;
  while (!session.finished() && checked < 2)
  {
    // A zone saved as far as it has played: 60 s into the exercise.
    constexpr auto SAVE_AT_TICKS = static_cast<std::uint64_t>(
        60.0F / MatchTuning::Timing::FIXED_STEP_SECONDS);
    bool due = false;
    if (!session.changing() && !session.zones().empty() &&
        session.exercise() == checked && session.zones().front().run)
    {
      SandboxRecorder& recorder =
          const_cast<DrillRun&>(*session.zones().front().run).recorder();
      const auto guard = recorder.lock();
      due = recorder.lastTick() >= SAVE_AT_TICKS;
    }
    if (due)
    {
      std::string error;
      const std::optional<ZoneRecording> recording =
          ZoneRecordings::capture(session, 0, error);
      if (!recording)
      {
        std::cout << "capture failed: " << error << "\n";
        return 1;
      }
      const std::filesystem::path path =
          std::filesystem::temp_directory_path() /
          ZoneRecordings::fileName(*recording);
      std::optional<ZoneRecording> loaded;
      if (ZoneRecordings::save(*recording, path, error))
      {
        loaded = ZoneRecordings::load(path, error);
      }
      std::filesystem::remove(path);
      if (!loaded)
      {
        std::cout << "save or load failed: " << error << "\n";
        return 1;
      }
      const std::unique_ptr<ZoneRecordings::Replay> replayed =
          ZoneRecordings::replay(*loaded, config);
      const MatchEngine& engine = replayed->run->engine();
      std::cout << std::format(
          "{} / {}: {} ticks, {} carried-in starts, {} commands; recorded "
          "{}-{}, "
          "replayed {}-{}; {}\n",
          loaded->exercise, loaded->zone, loaded->lastTick,
          loaded->startStates.size(), loaded->commands.size(),
          loaded->homeScore, loaded->awayScore, engine.getHomeScore(),
          engine.getAwayScore(),
          replayed->divergedAt
              ? std::format("DIVERGES at tick {}", *replayed->divergedAt)
              : std::string("replays identically"));
      if (replayed->divergedAt || !replayed->error.empty())
      {
        ++failures;
      }
      ++checked;
    }
    session.step();
  }
  return failures == 0 && checked == 2 ? 0 : 1;
}

// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "tools/match_sandbox_recording.h"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <exception>
#include <format>
#include <fstream>
#include <limits>
#include <variant>

#include "controller/game_controller.h"
#include "global/build_info.h"
#include "tools/match_sandbox_recorder.h"
#include "tools/match_sandbox_rewind.h"

using nlohmann::json;

namespace
{
constexpr float NOT_AVAILABLE = -std::numeric_limits<float>::infinity();

/** A score for JSON: null when the option was not available. */
json number(float value)
{
  return std::isfinite(value) ? json(value) : json(nullptr);
}

float number(const json& value)
{
  return value.is_null() ? NOT_AVAILABLE : value.get<float>();
}

std::string localTime(const char* format)
{
  const std::time_t now = std::time(nullptr);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  char text[64] = {};
  std::strftime(text, sizeof(text), format, &local);
  return text;
}

// --- Records ---------------------------------------------------------------

json toJson(const MatchCommandRecord& command)
{
  json formation = json::array();
  for (const Vector2F& slot : command.formation)
    formation.push_back({slot.x, slot.y});
  json route = json::array();
  for (const PitchPoint& waypoint : command.route)
    route.push_back({waypoint.x, waypoint.y});
  return {{"step", command.step},
          {"type", static_cast<int>(command.type)},
          {"home", command.homeTeam},
          {"strategy", SandboxJson::toJson(command.strategy)},
          {"shout", static_cast<int>(command.shout)},
          {"formation", formation},
          {"player", command.player},
          {"incoming", command.incoming},
          {"slot", command.slot ? json(*command.slot) : json(nullptr)},
          {"talk_half", command.talkHalf},
          {"talk_modifier", command.talkModifier},
          {"target", command.target ? json{command.target->x, command.target->y}
                                    : json(nullptr)},
          {"urgent", command.urgent},
          {"route", route},
          {"apex_offset", command.apexOffset},
          {"outward_room", command.outwardRoom},
          {"facing", command.facing ? json(*command.facing) : json(nullptr)},
          {"at_speed", command.atSpeed},
          {"feed_velocity", json{command.feedVelocity.x, command.feedVelocity.y}},
          {"feed_lift", command.feedLift},
          {"pass_style", static_cast<int>(command.passStyle)},
          {"pass_spin", command.passSpin
                            ? json{command.passSpin->side, command.passSpin->top}
                            : json(nullptr)},
          {"pass_arrival",
           command.passArrival ? json(*command.passArrival) : json(nullptr)},
          {"pass_first_time", command.passFirstTime},
          {"shot_aim", command.shotAim
                           ? json{command.shotAim->x, command.shotAim->y}
                           : json(nullptr)},
          {"shot_power",
           command.shotPower ? json(*command.shotPower) : json(nullptr)},
          {"shot_first_time", command.shotFirstTime},
          {"shot_technique", command.shotTechnique
                                 ? json(static_cast<int>(*command.shotTechnique))
                                 : json(nullptr)},
          {"header_aim", static_cast<int>(command.headerAim)},
          {"header_known_early", command.headerKnownEarly},
          {"keep_urgency", command.keepUrgency},
          {"tackle_intent", static_cast<int>(command.tackleIntent)}};
}

MatchCommandRecord commandFromJson(const json& value)
{
  MatchCommandRecord command;
  command.step = value.at("step").get<std::uint64_t>();
  command.type = static_cast<MatchCommandType>(value.at("type").get<int>());
  command.homeTeam = value.at("home").get<bool>();
  command.strategy = SandboxJson::strategyFromJson(value.at("strategy"));
  command.shout = static_cast<MatchShout>(value.at("shout").get<int>());
  for (const auto& slot : value.at("formation"))
    command.formation.push_back({slot.at(0).get<float>(), slot.at(1).get<float>()});
  command.player = value.at("player").get<PlayerID>();
  command.incoming = value.at("incoming").get<PlayerID>();
  if (!value.at("slot").is_null())
    command.slot = value.at("slot").get<std::size_t>();
  command.talkHalf = value.at("talk_half").get<int>();
  command.talkModifier = value.at("talk_modifier").get<float>();
  // Drill commands (older recordings have none).
  if (const auto target = value.find("target");
      target != value.end() && !target->is_null())
    command.target = PitchPoint{target->at(0).get<float>(), target->at(1).get<float>()};
  command.urgent = value.value("urgent", false);
  if (const auto route = value.find("route"); route != value.end())
    for (const auto& waypoint : *route)
      command.route.push_back(
          PitchPoint{waypoint.at(0).get<float>(), waypoint.at(1).get<float>()});
  command.apexOffset = value.value("apex_offset", 0.0f);
  command.outwardRoom = value.value("outward_room", 0.0f);
  if (const auto facing = value.find("facing");
      facing != value.end() && !facing->is_null())
    command.facing = facing->get<float>();
  command.atSpeed = value.value("at_speed", false);
  if (const auto feed = value.find("feed_velocity"); feed != value.end())
    command.feedVelocity = {feed->at(0).get<float>(), feed->at(1).get<float>()};
  command.feedLift = value.value("feed_lift", 0.0f);
  command.passStyle =
      static_cast<ForcedPassStyle>(value.value("pass_style", 0));
  if (const auto spin = value.find("pass_spin");
      spin != value.end() && spin->is_array())
    command.passSpin = BallSpin{spin->at(0).get<float>(), spin->at(1).get<float>()};
  else if (const float curve = value.value("pass_curve", 0.0f); curve != 0.0f)
    // Older recordings: the curve as a heading turn, now sidespin.
    command.passSpin = BallSpin{curve / MatchTuning::Ball::TURN_PER_SIDESPIN, 0.0f};
  if (const auto arrival = value.find("pass_arrival");
      arrival != value.end() && arrival->is_number())
    command.passArrival = arrival->get<float>();
  command.passFirstTime = value.value("pass_first_time", false);
  if (const auto aim = value.find("shot_aim"); aim != value.end() && aim->is_array())
    command.shotAim = Vector2F{aim->at(0).get<float>(), aim->at(1).get<float>()};
  if (const auto power = value.find("shot_power");
      power != value.end() && power->is_number())
    command.shotPower = power->get<float>();
  command.shotFirstTime = value.value("shot_first_time", false);
  if (const auto how = value.find("shot_technique");
      how != value.end() && how->is_number())
    command.shotTechnique = static_cast<ShotTechnique>(how->get<int>());
  command.headerAim =
      static_cast<ForcedHeaderAim>(value.value("header_aim", 0));
  command.headerKnownEarly = value.value("header_known_early", true);
  command.keepUrgency = value.value("keep_urgency", 1.0f);
  command.tackleIntent =
      static_cast<TackleIntent>(value.value("tackle_intent", 0));
  return command;
}

json toJson(const MatchInputRecord& record)
{
  const MatchPlayerInput& input = record.input;
  return {record.step,
          record.player,
          input.moveX,
          input.moveY,
          input.sprint,
          static_cast<int>(input.action),
          input.aimX,
          input.aimY,
          input.power,
          input.jockey,
          input.passAssist,
          record.playHalfMinutes};
}

MatchInputRecord inputFromJson(const json& value)
{
  MatchInputRecord record;
  record.step = value.at(0).get<std::uint64_t>();
  record.player = value.at(1).get<PlayerID>();
  MatchPlayerInput& input = record.input;
  input.moveX = value.at(2).get<float>();
  input.moveY = value.at(3).get<float>();
  input.sprint = value.at(4).get<bool>();
  input.action = static_cast<MatchInputAction>(value.at(5).get<int>());
  input.aimX = value.at(6).get<float>();
  input.aimY = value.at(7).get<float>();
  input.power = value.at(8).get<float>();
  input.jockey = value.at(9).get<bool>();
  input.passAssist = value.at(10).get<std::uint8_t>();
  record.playHalfMinutes = value.at(11).get<int>();
  return record;
}

json toJson(const MatchRecording::Decision& decision)
{
  const MatchDecisionRecord& record = decision.record;
  return {decision.tick,
          record.player,
          record.homeTeam,
          static_cast<int>(record.chosen),
          static_cast<int>(record.chosenWithoutNoise),
          number(record.pass),
          number(record.shot),
          number(record.carry),
          number(record.shield),
          record.bestReceiver};
}

MatchRecording::Decision decisionFromJson(const json& value)
{
  MatchRecording::Decision decision;
  decision.tick = value.at(0).get<std::uint64_t>();
  MatchDecisionRecord& record = decision.record;
  record.player = value.at(1).get<PlayerID>();
  record.homeTeam = value.at(2).get<bool>();
  record.chosen = static_cast<ScenarioAction>(value.at(3).get<int>());
  record.chosenWithoutNoise = static_cast<ScenarioAction>(value.at(4).get<int>());
  record.pass = number(value.at(5));
  record.shot = number(value.at(6));
  record.carry = number(value.at(7));
  record.shield = number(value.at(8));
  record.bestReceiver = value.at(9).get<PlayerID>();
  return decision;
}

// --- Breakdowns ------------------------------------------------------------

json toJson(const ScoreBreakdown& score)
{
  json terms = json::array();
  for (const ScoreTerm& term : score.terms)
    terms.push_back({term.name, static_cast<int>(term.source), term.value});
  return {number(score.total), terms};
}

ScoreBreakdown breakdownFromJson(const json& value,
                                 std::unordered_set<std::string>& names)
{
  ScoreBreakdown score;
  score.total = number(value.at(0));
  for (const auto& term : value.at(1))
  {
    // ScoreTerm keeps a pointer: the recording owns the text.
    const auto& name = *names.insert(term.at(0).get<std::string>()).first;
    score.terms.push_back({name.c_str(),
                           static_cast<TermSource>(term.at(1).get<int>()),
                           term.at(2).get<float>()});
  }
  return score;
}

json toJson(const DetailRecorder::At<MatchDecisionDetail>& at)
{
  const MatchDecisionDetail& detail = at.detail;
  json options = json::array();
  for (const ScoreBreakdown& option : detail.options) options.push_back(toJson(option));
  json candidates = json::array();
  for (const PassCandidateDetail& candidate : detail.candidates)
    candidates.push_back({candidate.receiver,
                          static_cast<int>(candidate.excluded),
                          static_cast<int>(candidate.intent),
                          candidate.distanceMetres, candidate.lofted,
                          toJson(candidate.utility), toJson(candidate.completion)});
  return {{"tick", at.tick},
          {"minute", at.minute},
          {"player", detail.player},
          {"home", detail.homeTeam},
          {"chosen", static_cast<int>(detail.chosen)},
          {"options", options},
          {"noise", detail.noise},
          {"noise_scale", toJson(detail.noiseScale)},
          {"candidates", candidates}};
}

DetailRecorder::At<MatchDecisionDetail> detailFromJson(
    const json& value, std::unordered_set<std::string>& names)
{
  DetailRecorder::At<MatchDecisionDetail> at;
  at.tick = value.at("tick").get<std::uint64_t>();
  at.minute = value.at("minute").get<float>();
  MatchDecisionDetail& detail = at.detail;
  detail.player = value.at("player").get<PlayerID>();
  detail.homeTeam = value.at("home").get<bool>();
  detail.chosen = static_cast<ScenarioAction>(value.at("chosen").get<int>());
  for (std::size_t index = 0; index < detail.options.size(); ++index)
    detail.options[index] = breakdownFromJson(value.at("options").at(index), names);
  detail.noise = value.at("noise").get<std::array<float, 4>>();
  detail.noiseScale = breakdownFromJson(value.at("noise_scale"), names);
  for (const auto& item : value.at("candidates"))
  {
    PassCandidateDetail candidate;
    candidate.receiver = item.at(0).get<PlayerID>();
    candidate.excluded = static_cast<PassExclusion>(item.at(1).get<int>());
    candidate.intent = static_cast<PassIntent>(item.at(2).get<int>());
    candidate.distanceMetres = item.at(3).get<float>();
    candidate.lofted = item.at(4).get<bool>();
    candidate.utility = breakdownFromJson(item.at(5), names);
    candidate.completion = breakdownFromJson(item.at(6), names);
    detail.candidates.push_back(std::move(candidate));
  }
  return at;
}

std::string checksumText(const std::vector<std::uint64_t>& checksums)
{
  std::string text;
  text.reserve(checksums.size() * 16);
  for (const std::uint64_t value : checksums) text += std::format("{:016x}", value);
  return text;
}

std::vector<std::uint64_t> checksumsFromText(const std::string& text)
{
  std::vector<std::uint64_t> checksums;
  checksums.reserve(text.size() / 16);
  for (std::size_t at = 0; at + 16 <= text.size(); at += 16)
    checksums.push_back(std::stoull(text.substr(at, 16), nullptr, 16));
  return checksums;
}

/** Whether two decisions are the same choice with the same scores. */
bool sameDecision(const MatchRecording::Decision& left,
                  const MatchRecording::Decision& right)
{
  const auto same = [](float a, float b)
  { return (std::isfinite(a) || std::isfinite(b)) ? a == b : true; };
  const MatchDecisionRecord& a = left.record;
  const MatchDecisionRecord& b = right.record;
  return left.tick == right.tick && a.player == b.player &&
         a.chosen == b.chosen && same(a.pass, b.pass) && same(a.shot, b.shot) &&
         same(a.carry, b.carry) && same(a.shield, b.shield);
}

std::vector<MatchRecording::Decision> decisionsOf(const SandboxRecorder& match)
{
  std::vector<MatchRecording::Decision> decisions;
  const auto guard = match.lock();
  for (const auto& entry : match.entries())
    if (const auto* decision = std::get_if<MatchDecisionRecord>(&entry.payload))
      decisions.push_back({entry.tick, *decision});
  return decisions;
}
}  // namespace

std::filesystem::path Recordings::folder()
{
  std::filesystem::path base = std::filesystem::current_path();
  if (const char* documents = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS))
    base = std::filesystem::path(documents);
  return base / "Player12 match recordings";
}

MatchRecording Recordings::capture(const MatchSetup& setup,
                                   const SandboxRecorder& match,
                                   bool withDetail)
{
  MatchRecording recording;
  recording.created = localTime("%Y-%m-%d %H:%M");
  recording.build = BuildInfo::summary();
  recording.setup = setup;
  {
    const auto guard = match.lock();
    recording.commands = match.commands();
    recording.inputs = match.inputs();
    recording.lastTick = match.lastTick();
    recording.checksums.resize(recording.lastTick + 1, 0);
    for (std::uint64_t tick = 0; tick <= recording.lastTick; ++tick)
      if (const auto checksum = match.checksum(tick))
        recording.checksums[tick] = *checksum;
    // The final score is the last event's.
    if (!match.events().empty())
    {
      recording.homeScore = match.events().back().homeScore;
      recording.awayScore = match.events().back().awayScore;
    }
  }
  recording.decisions = decisionsOf(match);
  if (withDetail)
  {
    // Rebuild the whole match once from its first saved copy, breaking
    // every decision down (the rankings and duels are left out).
    DetailRecorder detail;
    detail.keepRankings = false;
    detail.keepDuels = false;
    (void)MatchRewind::rebuild(match, recording.lastTick, &detail,
                               recording.lastTick);
    recording.details = std::move(detail.decisions);
  }
  return recording;
}

std::string Recordings::fileName(const MatchRecording& recording)
{
  std::string date = recording.created;
  std::ranges::replace(date, ':', '-');
  std::string name = std::format("{} {} v {} (seed {}).json", date,
                                 recording.setup.sides[0].name,
                                 recording.setup.sides[1].name,
                                 recording.setup.matchSeed);
  // Characters Windows does not allow in file names.
  for (char& character : name)
    if (std::string_view("<>:\"/\\|?*").find(character) != std::string_view::npos)
      character = '_';
  return name;
}

bool Recordings::save(const MatchRecording& recording,
                      const std::filesystem::path& path, std::string& error)
{
  try
  {
    json commands = json::array();
    for (const auto& command : recording.commands) commands.push_back(toJson(command));
    json inputs = json::array();
    for (const auto& input : recording.inputs) inputs.push_back(toJson(input));
    json decisions = json::array();
    for (const auto& decision : recording.decisions)
      decisions.push_back(toJson(decision));
    json details = json::array();
    for (const auto& detail : recording.details) details.push_back(toJson(detail));
    const json document = {{"format", "player12-match-recording"},
                           {"version", MatchRecording::VERSION},
                           {"created", recording.created},
                           {"build", recording.build},
                           {"setup", SandboxJson::toJson(recording.setup)},
                           {"commands", commands},
                           {"inputs", inputs},
                           {"last_tick", recording.lastTick},
                           {"score", {recording.homeScore, recording.awayScore}},
                           {"checksums", checksumText(recording.checksums)},
                           {"decisions", decisions},
                           {"details", details}};
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    if (!file)
    {
      error = "cannot write " + path.string();
      return false;
    }
    file << document.dump();
    return static_cast<bool>(file);
  }
  catch (const std::exception& problem)
  {
    error = problem.what();
    return false;
  }
}

std::optional<MatchRecording> Recordings::load(
    const std::filesystem::path& path, std::string& error)
{
  try
  {
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
      error = "cannot read " + path.string();
      return std::nullopt;
    }
    const json document = json::parse(file);
    if (document.value("format", "") != "player12-match-recording")
    {
      error = "not a match recording";
      return std::nullopt;
    }
    const int version = document.value("version", 1);
    if (version != MatchRecording::VERSION ||
        !document.at("setup").contains("possession"))
    {
      error = version < MatchRecording::VERSION
                  ? "recording from an older engine (squashed facings or pitch fractions); record it again"
                  : "unsupported recording version or missing possession selection";
      return std::nullopt;
    }
    MatchRecording recording;
    recording.created = document.value("created", "");
    recording.build = document.value("build", "");
    if (!SandboxJson::fromJson(document.at("setup"), recording.setup, error))
      return std::nullopt;
    for (const auto& command : document.at("commands"))
      recording.commands.push_back(commandFromJson(command));
    for (const auto& input : document.at("inputs"))
      recording.inputs.push_back(inputFromJson(input));
    recording.lastTick = document.at("last_tick").get<std::uint64_t>();
    recording.homeScore = document.at("score").at(0).get<int>();
    recording.awayScore = document.at("score").at(1).get<int>();
    recording.checksums =
        checksumsFromText(document.at("checksums").get<std::string>());
    for (const auto& decision : document.at("decisions"))
      recording.decisions.push_back(decisionFromJson(decision));
    for (const auto& detail : document.at("details"))
      recording.details.push_back(detailFromJson(detail, *recording.names));
    return recording;
  }
  catch (const std::exception& problem)
  {
    error = problem.what();
    return std::nullopt;
  }
}

Recordings::Replay Recordings::replay(
    const MatchRecording& recording, const GameController& controller,
    std::optional<std::array<BrainId, 2>> brains,
    std::optional<std::array<BodyId, 2>> bodies,
    std::optional<HandshakeId> handshake,
    std::optional<PossessionId> possession)
{
  Replay result;
  if (recording.setup.worldSeed != controller.getWorldSeed())
  {
    result.error = std::format(
        "recorded in world {}, this sandbox runs world {} (start it with "
        "--world-seed {})",
        recording.setup.worldSeed, controller.getWorldSeed(),
        recording.setup.worldSeed);
    return result;
  }
  MatchSetup setup = recording.setup;
  if (brains) setup.brains = *brains;
  if (bodies) setup.bodies = *bodies;
  if (handshake) setup.handshake = *handshake;
  if (possession) setup.possession = *possession;
  auto engine = setup.build(controller, result.error);
  if (!engine) return result;
  engine->loadCommandReplay(recording.commands);
  engine->loadInputReplay(recording.inputs);
  result.match = std::make_unique<SandboxRecorder>();
  engine->setRecorder(result.match.get());
  engine->simulateToEnd();
  result.homeScore = engine->getHomeScore();
  result.awayScore = engine->getAwayScore();
  engine->setRecorder(nullptr);

  // The first tick whose state is not the recorded one.
  {
    const auto guard = result.match->lock();
    const std::uint64_t replayed = result.match->lastTick();
    const std::uint64_t common = std::min(replayed, recording.lastTick);
    for (std::uint64_t tick = 1; tick <= common && !result.divergedAt; ++tick)
    {
      const std::uint64_t expected =
          tick < recording.checksums.size() ? recording.checksums[tick] : 0;
      const auto actual = result.match->checksum(tick);
      if (expected != 0 && actual && *actual != expected) result.divergedAt = tick;
    }
    if (!result.divergedAt && replayed != recording.lastTick)
      result.divergedAt = common + 1;
  }

  // The first decision not made identically.
  const std::vector<MatchRecording::Decision> replayed = decisionsOf(*result.match);
  const std::size_t common = std::min(replayed.size(), recording.decisions.size());
  for (std::size_t index = 0; index < common; ++index)
    if (!sameDecision(replayed[index], recording.decisions[index]))
    {
      result.firstDifferentDecision = index;
      result.replayedDecision = replayed[index];
      break;
    }
  if (!result.firstDifferentDecision &&
      replayed.size() != recording.decisions.size())
  {
    result.firstDifferentDecision = common;
    if (common < replayed.size()) result.replayedDecision = replayed[common];
  }
  return result;
}

nlohmann::json Recordings::commandToJson(const MatchCommandRecord& command)
{
  return toJson(command);
}

MatchCommandRecord Recordings::commandFromJsonValue(const nlohmann::json& value)
{
  return commandFromJson(value);
}

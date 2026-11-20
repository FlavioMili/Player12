// Football Management Project - see LICENSE in the project root.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

#include "tools/match_sandbox_recording.h"
#include "tools/match_sandbox_session.h"
#include "tools/match_sandbox_zone_recording.h"

namespace
{
void require(bool condition, const char* message)
{
  if (!condition)
  {
    throw std::runtime_error(message);
  }
}

struct TemporaryFile
{
  TemporaryFile() = default;
  TemporaryFile(const TemporaryFile&) = delete;
  TemporaryFile& operator=(const TemporaryFile&) = delete;
  TemporaryFile(TemporaryFile&&) = delete;
  TemporaryFile& operator=(TemporaryFile&&) = delete;
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("player12-boundary-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".json");
  ~TemporaryFile()
  {
    std::error_code error;
    std::filesystem::remove(path, error);
  }
};

void checkPackedFields(nlohmann::json document,
                       const std::filesystem::path& path)
{
  // Independent version-4 wire examples: distinct fields catch index shifts.
  document["inputs"] = nlohmann::json::parse(
      R"([[37,101,0.25,-0.5,true,1,-0.75,0.125,0.625,false,2,45]])");
  document["decisions"] =
      nlohmann::json::parse(R"([[38,102,false,1,2,0.25,null,-0.5,0.75,103]])");
  document["details"] = nlohmann::json::parse(R"([{
    "tick":39,"minute":0.5,"player":104,"home":true,"chosen":2,
    "options":[[0.25,[]],[0.5,[]],[0.75,[]],[null,[]]],
    "noise":[0.125,0.25,-0.125,-0.25],"noise_scale":[0.5,[]],
    "candidates":[[105,0,1,12.5,true,[0.25,[]],[0.75,[]]]]
  }])");
  {
    std::ofstream file(path);
    file << document;
  }
  std::string error;
  const auto loaded = Recordings::load(path, error);
  require(loaded.has_value(), error.c_str());
  const bool saved = Recordings::save(*loaded, path, error);
  require(saved, error.c_str());
  nlohmann::json roundTrip;
  {
    std::ifstream file(path);
    file >> roundTrip;
  }
  for (const auto* field : {"inputs", "decisions", "details"})
  {
    require(roundTrip.at(field) == document.at(field),
            "packed recording field order changed");
  }
}

void checkRecordings(const std::filesystem::path& path)
{
  MatchRecording recording;
  recording.checksums = {0, 1, std::numeric_limits<std::uint64_t>::max()};
  std::string error;
  const bool savedMatch = Recordings::save(recording, path, error);
  require(savedMatch, error.c_str());
  const auto loaded = Recordings::load(path, error);
  require(loaded && loaded->checksums == recording.checksums,
          "checksum round trip failed");
  nlohmann::json document;
  {
    std::ifstream file(path);
    file >> document;
  }
  checkPackedFields(document, path);
  const auto writeChecksums = [&](const std::string& text)
  {
    document["checksums"] = text;
    std::ofstream file(path);
    file << document;
  };
  for (const std::string text :
       {"0", "000000000000000", "000000000000000g", " 000000000000001",
        "+000000000000001", "-000000000000001", "0x00000000000001",
        "00000000000000000"})
  {
    writeChecksums(text);
    require(!Recordings::load(path, error) && !error.empty(),
            "malformed checksum was accepted");
  }
  writeChecksums("FFFFFFFFFFFFFFFF");
  const auto uppercase = Recordings::load(path, error);
  require(uppercase && error.empty() &&
              uppercase->checksums ==
                  std::vector{std::numeric_limits<std::uint64_t>::max()},
          "valid checksum failed after a rejected recording");
  writeChecksums("");
  const auto empty = Recordings::load(path, error);
  require(empty && empty->checksums.empty(), "empty checksums were rejected");

  ZoneRecording zone;
  const bool savedZone = ZoneRecordings::save(zone, path, error);
  require(savedZone, error.c_str());
  error = "an earlier failure";
  const auto loadedZone = ZoneRecordings::load(path, error);
  require(loadedZone.has_value(), error.c_str());
  require(error.empty(), "stale error survived a valid zone recording");
}

void checkSessions()
{
  const StatsConfig config;
  const auto rejected = [&](const SessionPlan& plan)
  {
    try
    {
      const SessionRun run(plan, config, 1);
    }
    catch (const std::invalid_argument&)
    {
      return true;
    }
    return false;
  };
  SessionPlan plan;
  plan.squadSize = 1;
  SessionZone zone;
  zone.players = {std::numeric_limits<std::size_t>::max()};
  plan.exercises = {{.name = "invalid", .zones = {zone}}};
  require(rejected(plan), "out-of-range squad index was accepted");
  zone.players = {0, 0};
  plan.exercises.front().zones = {zone};
  require(rejected(plan), "duplicate zone player was accepted");
  zone.players = {0};
  plan.exercises.front().zones = {zone, zone};
  require(rejected(plan), "player assigned to two zones was accepted");
  const SessionRun empty(SessionPlan{}, config, 1);
  require(empty.finished(), "empty session no longer finishes immediately");
}

void checkPlanValidation()
{
  const SessionPlan example = sessionPlans().front();
  require(!checkSessionPlan(example), "example session plan is invalid");
  const auto rejects = [&](const auto& change, const char* reason)
  {
    SessionPlan plan = example;
    change(plan.exercises.front().zones.front());
    const auto problem = checkSessionPlan(plan);
    require(problem && problem->find(reason) != std::string::npos,
            "plan validation changed its error or precedence");
  };
  rejects([](SessionZone& zone) { zone.drill = "unknown"; }, "no drill called");
  rejects([](SessionZone& zone) { zone.settings["unknown"] = 0.0F; },
          "has no setting");
  rejects([](SessionZone& zone) { zone.players.pop_back(); }, "names");
  rejects([](SessionZone& zone) { zone.centre = {}; }, "off the ground");
  SessionPlan overlapping = example;
  auto& zones = overlapping.exercises.front().zones;
  zones.back().centre = zones.front().centre;
  const auto problem = checkSessionPlan(overlapping);
  require(problem && problem->find("too close") != std::string::npos,
          "overlapping zones were accepted");
}
}  // namespace

int main()
{
  try
  {
    const TemporaryFile file;
    checkRecordings(file.path);
    checkSessions();
    checkPlanValidation();
    std::cout << "PASS: recording and session boundaries\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::fputs("FAIL: ", stderr);
    std::fputs(error.what(), stderr);
    std::fputc('\n', stderr);
    return 1;
  }
  catch (...)
  {
    std::fputs("FAIL: unexpected exception\n", stderr);
    return 1;
  }
}

// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "database/world/world_export.h"
#include "database/world/world_io.h"
#include "database/world/world_library.h"
#include "database/world/world_runtime.h"
#include "global/logger.h"
#include "global/runtime_paths.h"

namespace fs = std::filesystem;
using World::Kind;

namespace
{
constexpr std::uint64_t SEED = 4242;

struct Slot
{
  explicit Slot(int number) : slot(number) {}
  Slot(const Slot&) = delete;
  Slot& operator=(const Slot&) = delete;
  ~Slot() { RuntimePaths::removeSave(slot); }
  int slot;
};

std::set<std::string> clubNames(const GameController& controller)
{
  std::set<std::string> names;
  for (const auto& team : controller.getTeams())
  {
    if (team.get().getId() != FREE_AGENTS_TEAM_ID)
    {
      names.insert(team.get().getName());
    }
  }
  return names;
}
}  // namespace

TEST(WorldExport, ExportedCareerIsAValidWorldThatStartsAgain)
{
  Logger::init();
  const Slot first(731'301);
  const Slot second(731'302);
  GameController controller;
  controller.newGame(first.slot, SEED);
  const GameData& data = *controller.getGameData();

  const World::Document exported = World::exportCareer(
      data, *World::current(), "Test FC", "2025-07-01", 2025);
  EXPECT_FALSE(World::hasErrors(World::validate(exported)));
  EXPECT_TRUE(World::playability(exported).empty());
  EXPECT_EQ(exported.manifest().name, "Test FC – 2025-07-01");
  EXPECT_EQ(exported.manifest().season, 2025);
  EXPECT_EQ(exported.count(Kind::Player), data.getPlayers().size());
  EXPECT_EQ(exported.count(Kind::Club), data.getTeams().size() - 1);
  EXPECT_EQ(exported.count(Kind::League), 22U);
  EXPECT_GT(exported.count(Kind::Country), 0U);
  for (const auto& row : exported.section(Kind::League))
  {
    EXPECT_TRUE(row.contains("profile"));
  }

  // Write, read back, and play the exported world.
  const fs::path file = RuntimePaths::root() / "exported_world.fmdb";
  fs::create_directories(file.parent_path());
  ASSERT_TRUE(World::writeFile(exported, file));
  const World::LoadResult loaded = World::readFile(file);
  ASSERT_TRUE(loaded.ok());
  EXPECT_EQ(World::write(*loaded.document), World::write(exported));
  fs::remove(file);

  const auto world = std::make_shared<const World::Document>(*loaded.document);
  GameController again;
  again.newGame(second.slot, SEED, world);
  EXPECT_EQ(clubNames(again), clubNames(controller));
  // Every exported player is there; the free-agent market is topped up.
  EXPECT_GE(again.getGameData()->getPlayers().size(),
            exported.count(Kind::Player));
  for (const auto& row : exported.section(Kind::Player))
  {
    EXPECT_TRUE(again.getGameData()
                    ->getPlayer(row.at("id").get<PlayerID>())
                    .has_value());
  }
  World::install(nullptr);
}

TEST(WorldFlow, ImportedTemplateWorldStartsACareer)
{
  Logger::init();
  const Slot slot(731'303);
  const World::Document tmpl = World::templateDocument();
  const fs::path file = RuntimePaths::root() / "template_world.fmdb";
  fs::create_directories(file.parent_path());
  ASSERT_TRUE(World::writeFile(tmpl, file));

  const World::Library::ImportResult imported =
      World::Library::importFile(file);
  fs::remove(file);
  ASSERT_TRUE(imported.load.ok());
  ASSERT_FALSE(imported.stored.empty());

  const auto entries = World::Library::list();
  const auto entry =
      std::ranges::find(entries, imported.stored, &World::Library::Entry::path);
  ASSERT_NE(entry, entries.end());
  EXPECT_EQ(entry->blocking, 0U);
  EXPECT_EQ(entry->counts[static_cast<std::size_t>(Kind::Club)],
            tmpl.count(Kind::Club));

  const World::LoadResult stored = World::readFile(imported.stored);
  ASSERT_TRUE(stored.ok());
  EXPECT_TRUE(World::playability(*stored.document).empty());
  GameController controller;
  controller.newGame(slot.slot, SEED,
                     std::make_shared<const World::Document>(*stored.document));
  std::set<std::string> expected;
  for (const auto& club : tmpl.section(Kind::Club))
  {
    expected.insert(club.at("name").get<std::string>());
  }
  EXPECT_EQ(clubNames(controller), expected);

  EXPECT_TRUE(World::Library::remove(imported.stored));
  World::install(nullptr);
}

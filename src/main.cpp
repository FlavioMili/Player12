// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/build_info.h"
#include "global/crash_report.h"
#include "global/logger.h"
#include "global/paths.h"
#include "gui/gui_view.h"

#if defined(__clang__) || defined(__GNUC__)
extern "C" const char* __lsan_default_suppressions()
{
  return "leak:libSDL3.so\n";
}
#endif

namespace
{
constexpr std::string_view PROFILE_MATCH_ARGUMENT = "--profile-match";
constexpr int FIRST_SAVE_SLOT = 1;
constexpr int LAST_SAVE_SLOT = 3;

bool loadFirstExistingSave(GameController& controller)
{
  for (int slot = FIRST_SAVE_SLOT; slot <= LAST_SAVE_SLOT; ++slot)
  {
    if (controller.getSaveSlotMetadata(slot).exists &&
        controller.loadGame(slot))
    {
      Logger::info(
          std::format("Profiling match rendering with save slot {}", slot));
      return true;
    }
  }
  return false;
}
}  // namespace

int main(int argc, char* argv[])
{
  try
  {
    Logger::init();
    Logger::info(BuildInfo::summary());
    CrashReport::install();
  }
  catch (const std::exception& error)
  {
    // Storage itself may be unavailable, so a crash report cannot be saved.
    std::cerr << "Startup error: " << error.what() << '\n';
    return 1;
  }
  try
  {
    auto controller = std::make_unique<GameController>();

    const bool profileMatch =
        argc == 2 && std::string_view(argv[1]) == PROFILE_MATCH_ARGUMENT;
    if (argc > 1 && !profileMatch)
    {
      std::cerr << "Usage: " << argv[0] << " [--profile-match]\n";
      return 2;
    }

    if (profileMatch)
    {
      if (!loadFirstExistingSave(*controller))
      {
        std::cerr << "No save is available for match profiling.\n";
        return 2;
      }

      GUIView profileView(*controller);
      return profileView.runMatchRenderProfile() ? 0 : 1;
    }

    GUIView view(*controller);
    view.run();
    if (controller->isGameLoaded())
    {
      controller->saveGame();
    }
  }
  catch (const std::exception& e)
  {
    // Shown on the next start; the report stays on this computer.
    const auto report = CrashReport::write(std::string("error: ") + e.what());
    std::cerr << "Error: " << e.what() << "\nCrash report: " << report.string()
              << "\n";
    return 1;
  }
  return 0;
}

// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <string_view>
#include <vector>

#include "database/world/world_io.h"

/**
 * Worlds the player imported or saved, kept as .fmdb files in the writable
 * runtime folder ("databases"). Imported files are copied, so the original
 * download can be deleted; the game never fetches files itself.
 */
namespace World::Library
{
struct Entry
{
  std::filesystem::path path;
  Manifest manifest;
  std::array<std::size_t, static_cast<std::size_t>(Kind::COUNT)> counts{};
  std::size_t errors = 0;
  std::size_t warnings = 0;
  std::size_t blocking = 0;  ///< Problems that stop a career from starting.
};

std::filesystem::path directory();
/** Every readable world of the library, by name. Files that are not worlds
 * are listed with errors so the player can remove them. */
std::vector<Entry> list();
/** Summary of a loaded document (counts, problems). */
Entry summarise(const Document& document, const std::vector<Issue>& issues);

struct ImportResult
{
  LoadResult load;
  std::filesystem::path stored;  ///< Empty when nothing was copied.
};
/** Validates @p source and, when it has no errors, copies it into the
 * library under a unique, sanitised name. */
ImportResult importFile(const std::filesystem::path& source);
/** Writes @p document into the library (new unique file named after its
 * manifest, or @p existing when given and inside the library). */
std::filesystem::path save(const Document& document,
                           const std::filesystem::path& existing = {});
/** Deletes a library file; refuses paths outside the library. */
bool remove(const std::filesystem::path& path);
/** "My World 2" -> "My_World_2" (letters, digits, - _ .), never empty. */
std::string fileStem(std::string_view name);
}  // namespace World::Library

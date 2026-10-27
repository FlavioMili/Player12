// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

/**
 * @class UserDatabase
 * @brief A user-supplied custom database ("*.fmdb"): one JSON file that
 * replaces the built-in data pack (leagues, clubs, players, names).
 *
 * The file mirrors the pack fields (see assets/user_made_data/README.md)
 * plus the database-dev extensions (skin tones, kit palettes, nicknames),
 * wrapped with a manifest. Imported files live under the writable runtime
 * root; the active one is layered over the read-only asset pack at load, so
 * the repository itself never contains real-world data.
 */
class UserDatabase
{
 public:
  static constexpr int CURRENT_SCHEMA = 1;
  static constexpr const char* FILE_EXTENSION = ".fmdb";

  struct Manifest
  {
    std::string name;
    std::string author;
    std::string version;
    int schema = 0;
    std::string min_game_version;
  };

  /** One validation finding. Errors block import; warnings do not. */
  struct Issue
  {
    std::string section;  ///< "manifest", "leagues", "clubs", "players", ...
    long long index = -1; /*!< Item index within the section, -1 for file. */
    bool is_error = true;
    std::string message;
  };

  struct Content
  {
    Manifest manifest;
    nlohmann::json leagues = nlohmann::json::array();
    nlohmann::json clubs = nlohmann::json::array();
    nlohmann::json players = nlohmann::json::array();
    nlohmann::json first_names = nullptr;
    nlohmann::json last_names = nullptr;
    bool has_names = false;
  };

  struct LoadResult
  {
    bool ok = false; /*!< True when there are no errors (warnings allowed). */
    Content content;
    std::vector<Issue> issues;
  };

  /** Parses and validates a *.fmdb file; never throws on bad data. */
  static LoadResult loadFile(const std::filesystem::path& path);

  /** Writable directory holding imported databases. */
  static std::filesystem::path databasesDir();

  /**
   * Validates @p source and, when clean, copies it into databasesDir() and
   * activates it. Returns false (with issues) when validation fails.
   */
  static bool importFile(const std::filesystem::path& source,
                         std::vector<Issue>& issues, std::string& imported_name);

  /** Makes an imported file the active database (or clears it). */
  static void setActive(const std::string& file_name);
  static void clearActive();

  /** File name of the active database, if any is recorded. */
  static std::optional<std::string> activeName();

  /** Parsed active database, if one is set and loads cleanly. */
  static std::optional<Content> activeContent();

  /** Section arrays of the active database, if any. */
  static std::optional<nlohmann::json> activeLeagues();
  static std::optional<nlohmann::json> activeClubs();
  static std::optional<nlohmann::json> activePlayers();
  static std::optional<Manifest> activeManifest();
};

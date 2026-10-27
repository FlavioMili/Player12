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
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

/**
 * World databases: one shareable JSON document ("*.fmdb") holding any subset
 * of a football world (languages, regions, countries, cities, travel times,
 * competitions, clubs, players and name pools). The built-in world and user
 * worlds share the format, the loader and the validator. Unknown keys are
 * preserved, so mods can carry extra data through the in-game tools unchanged.
 * Format reference: assets/world/README.md.
 */
namespace World
{
/** Current format version written by this build. */
inline constexpr int SCHEMA = 2;
inline constexpr std::string_view FILE_EXTENSION = ".fmdb";
/** Larger files are refused before parsing. */
inline constexpr std::uintmax_t MAX_FILE_BYTES = 64ULL * 1024 * 1024;

/** Entity kinds, in dependency order (parents before children). */
enum class Kind : std::uint8_t
{
  Language,       ///< A language: name plus optional first/last name pools.
  Confederation,  ///< Group of regions: continental cups, national finals.
  Region,         ///< Football region (pre-season tours stay inside it).
  Country,
  City,
  Travel,
  League,
  Club,
  Player,
  COUNT,
};
inline constexpr std::array<Kind, 9> KINDS = {
    Kind::Language, Kind::Confederation, Kind::Region,
    Kind::Country,  Kind::City,          Kind::Travel,
    Kind::League,   Kind::Club,          Kind::Player,
};

/** JSON key of the section holding @p kind ("countries", "clubs", ...). */
std::string_view sectionKey(Kind kind);
/** Localisation key of the kind's plural display name. */
std::string_view kindLabel(Kind kind);
/** Smallest and largest identifier the game accepts for @p kind. */
std::int64_t minId(Kind kind);
std::int64_t maxId(Kind kind);
/** Key of the parent reference ("country_id", "league_id", ...) or empty. */
std::string_view parentKey(Kind kind);
/** Kind referenced by parentKey(kind). */
std::optional<Kind> parentKind(Kind kind);

struct Manifest
{
  std::string name;
  std::string author;
  std::string version;
  std::string description;
  std::string min_game_version;
  int schema = SCHEMA;
  /** Season the ages refer to; lets players be matched across worlds made in
   * different years. */
  std::optional<int> season;
};

/** Display name of an entity ("First Last" for players, "A - B" travel). */
std::string entityName(Kind kind, const nlohmann::json& entity);
/** Identifier of an entity, or -1 (travel rows have none). */
std::int64_t entityId(const nlohmann::json& entity);

/**
 * Lower case, Latin accents folded, punctuation dropped, single spaces, so
 * "Réal  Madrid C.F." and "real madrid cf" compare equal.
 */
std::string normalise(std::string_view name);

/**
 * A world database in memory. The JSON root is the source of truth; the
 * helpers only add id lookup (cached, rebuilt after structural changes) and
 * typed manifest access. Section arrays are created on first access.
 */
class Document
{
 public:
  Document();
  explicit Document(nlohmann::json root);

  /** An empty, valid document named @p name. */
  static Document empty(std::string name);

  const nlohmann::json& root() const { return data; }
  Manifest manifest() const;
  void setManifest(const Manifest& manifest);

  nlohmann::json& section(Kind kind);
  const nlohmann::json& section(Kind kind) const;
  std::size_t count(Kind kind) const;
  bool hasSection(Kind kind) const;

  /** Name pools ("first_names"/"last_names"); null when absent. */
  const nlohmann::json& names(bool first) const;
  void setNames(bool first, nlohmann::json pool);

  /** Entity with @p id, or nullptr. O(1) after the first call. */
  nlohmann::json* find(Kind kind, std::int64_t id);
  const nlohmann::json* find(Kind kind, std::int64_t id) const;
  /** Position of the entity in its section, if present. */
  std::optional<std::size_t> indexOf(Kind kind, std::int64_t id) const;

  /** Highest identifier in use (0 when the section is empty). */
  std::int64_t highestId(Kind kind) const;
  /** A free identifier: after the highest, or the first gap when full. */
  std::optional<std::int64_t> freeId(Kind kind) const;

  /** Appends @p entity (its id must be free) and returns it. */
  nlohmann::json& add(Kind kind, nlohmann::json entity);
  /** Removes the entity; references to it are not touched. */
  bool remove(Kind kind, std::int64_t id);
  /** Call after editing ids or arrays directly. */
  void invalidate() const;

  /** Whole-section replacement, e.g. after combining. */
  void setSection(Kind kind, nlohmann::json array);

  /** Copy without players (stored in saves next to the Players table). */
  Document withoutPlayers() const;

 private:
  using Index = std::unordered_map<std::int64_t, std::size_t>;
  const Index& index(Kind kind) const;
  nlohmann::json data;
  mutable std::array<std::optional<Index>,
                     static_cast<std::size_t>(Kind::COUNT)>
      indexes;
};
}  // namespace World

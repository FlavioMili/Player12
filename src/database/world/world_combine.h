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
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "database/world/world_document.h"

/**
 * Combining two world databases. Entities are matched by identity (names in
 * the context of their parents, never by numeric id), identical duplicates
 * merge silently, differing ones become conflicts the player resolves, and
 * the children of a matched entity follow it (cascade).
 */
namespace World
{
struct Conflict
{
  Kind kind = Kind::Country;
  /** Entity in the combined document. For travel rows (no ids) the position
   * of the row in the combined "travel" section. */
  std::int64_t base_id = 0;
  /** Incoming version, references remapped to combined ids, "id" = base_id. */
  nlohmann::json incoming;
  std::string base_source;          ///< Manifest name of the base document.
  std::string incoming_source;      ///< Manifest name of the incoming document.
  std::vector<std::string> fields;  ///< differingPaths(base, incoming).
};

/** A new entity whose name resembles an existing one; both are in the result.
 */
struct PossibleDuplicate
{
  Kind kind = Kind::Country;
  std::int64_t base_id = 0;
  std::int64_t incoming_id = 0;
};

struct CombineResult
{
  Document document;
  std::vector<Conflict> conflicts;
  std::vector<PossibleDuplicate> possible_duplicates;
  /** Incoming id -> combined id, per kind. */
  std::array<std::unordered_map<std::int64_t, std::int64_t>,
             static_cast<std::size_t>(Kind::COUNT)>
      id_map;
  std::array<std::size_t, static_cast<std::size_t>(Kind::COUNT)> added{};
  std::array<std::size_t, static_cast<std::size_t>(Kind::COUNT)> merged{};
};

/** Pure: neither input is modified. The manifest stays the base one. */
CombineResult combine(const Document& base, const Document& incoming);

/**
 * Identity of @p entity inside @p document: normalised name in the context of
 * the parent country (leagues, cities, clubs), the unordered city pair
 * (travel), or name, nationality and birth year (players; the birth year is
 * the document's season minus the age when it has one, else the age).
 */
std::string identityKey(Kind kind, const nlohmann::json& entity,
                        const Document& document);

/** Names (already normalise()d) that are different but probably the same. */
bool lookAlike(std::string_view first, std::string_view second);

enum class Choice : std::uint8_t
{
  KeepBase,
  KeepIncoming,
  KeepBoth,
};

/** Applies @p choice for @p conflict to the combined @p document. Keeping both
 * is not possible for travel rows (treated as keeping the base). */
void resolve(Document& document, const Conflict& conflict, Choice choice);
/** Base values except the listed paths, which take the incoming value. */
void resolveFields(Document& document, const Conflict& conflict,
                   const std::vector<std::string>& incoming_paths);
/** Resolves every conflict: KeepIncoming where incoming_source == source,
 * else KeepBase; clears the list. */
void resolveAll(Document& document, std::vector<Conflict>& conflicts,
                const std::string& source);
/** Moves everything referencing drop_id to keep_id and removes drop_id. */
void mergeInto(Document& document, Kind kind, std::int64_t keep_id,
               std::int64_t drop_id);
}  // namespace World

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
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "database/world/world_document.h"

/**
 * Combining two world databases. Nothing different is ever merged or dropped
 * automatically. Entities match only on a strong identity (names in the
 * context of their parents, never by numeric id) that is unambiguous, with
 * exactly one candidate on each side: identical matches merge silently,
 * differing ones become conflicts the player resolves, and the children of a
 * matched entity follow it (cascade). Weaker matches (same club name in
 * another league, same player at another club, ambiguous keys, look-alike
 * names) are added as new entities and flagged as possible duplicates.
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
  /** What the match rests on, e.g. "name+nationality+birth_year+club". Only
   * strong, unambiguous matches become conflicts. */
  std::string basis;
};

/** A new entity that may be the same as an existing one without a strong,
 * unambiguous match; both are in the result and only the user can merge. */
struct PossibleDuplicate
{
  Kind kind = Kind::Country;
  std::int64_t base_id = 0;
  std::int64_t incoming_id = 0;
  /** Why they may be the same: "name+country" (club in another league),
   * "name+nationality+birth_year" (player at another club), "ambiguous"
   * (several candidates) or "similar_name". Never merged automatically. */
  std::string basis;
};

/** A reference from a child entity to an automatically merged one. */
struct ChildLink
{
  Kind kind = Kind::Club;
  std::int64_t id = 0;  ///< Combined id of the child.
  std::string path;     ///< Reference field of the child.
};

/** An incoming entity that was identical to an existing one (strong,
 * unambiguous match) and was merged for convenience. The user can review it
 * and undo it with separate(). */
struct AutoMerge
{
  Kind kind = Kind::Country;
  std::int64_t base_id = 0;       ///< Entity it merged into (travel: position).
  std::int64_t incoming_id = -1;  ///< Id in the incoming world.
  std::shared_ptr<const nlohmann::json> entity;  ///< The merged entity.
  std::string basis;                             ///< As Conflict::basis.
  /** Added children that were pointed at base_id because of the merge. */
  std::vector<ChildLink> children;
};

/** An incoming entity that is new in the combined world. */
struct Addition
{
  Kind kind = Kind::Country;
  std::int64_t old_id = -1;  ///< Id in the incoming world (travel: -1).
  std::int64_t new_id = 0;   ///< Id in the combined world (travel: position).
};

struct CombineResult
{
  Document document;
  std::vector<Conflict> conflicts;
  std::vector<PossibleDuplicate> possible_duplicates;
  /** Everything merged because it was identical; reviewable and undoable. */
  std::vector<AutoMerge> auto_merges;
  /** Everything added, with its old and new id. */
  std::vector<Addition> additions;
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
 * Strong identity of @p entity inside @p document: normalised name in the
 * context of the parent country (leagues, cities), country plus league plus
 * name (clubs), the unordered city pair (travel), or name, nationality, birth
 * year and club (players; the birth year is the document's season minus the
 * age when it has one, else the age; free agents have an empty club).
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

/** Undoes one automatic merge: re-adds the incoming entity under a free id and
 * points its added children back to it. Returns the new id (travel: row
 * position), nullopt when no id is free. */
std::optional<std::int64_t> separate(Document& document,
                                     const AutoMerge& merge);

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

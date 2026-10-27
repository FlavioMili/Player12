// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "database/world/world_document.h"

/**
 * Structural edits of a world database used by the World editor: new,
 * duplicated and removed entities with every reference kept consistent.
 * Travel rows have no id and are addressed by their position in the section.
 */
namespace World
{
/** What removing an entity changes elsewhere. */
struct RemoveImpact
{
  std::size_t players = 0;  ///< Players that become free agents.
  std::size_t clubs = 0;    ///< Clubs that lose their competition or city.
  std::size_t leagues = 0;  ///< Competitions that lose their parent/country.
  std::size_t cities = 0;   ///< Cities that lose their country.
  std::size_t travel = 0;   ///< Travel rows removed with a city.

  bool any() const { return players + clubs + leagues + cities + travel != 0; }
};

/** Copy of an entity under a free id (travel: position) with " (2)" added to
 * its name; players keep their names. Clubs get a fresh short name.
 * Returns the new id, or -1 when there is no free id or no such entity. */
std::int64_t duplicate(Document& document, Kind kind, std::int64_t id);

/** Adds a minimal valid entity inside @p parent (country of a city or
 * competition, competition of a club, club of a player, first city of a travel
 * row). Returns its id (travel: position), or -1 when there is no free id or
 * a travel row has fewer than two cities to link. */
std::int64_t create(Document& document, Kind kind,
                    std::optional<std::int64_t> parent = std::nullopt);

/** Counts what removeEntity() would change. */
RemoveImpact impact(const Document& document, Kind kind, std::int64_t id);

/** Removes the entity and detaches what referenced it: players of a club
 * become free agents, clubs of a removed competition lose it (an Unassigned
 * problem to fix), a removed city takes its travel rows with it. */
void removeEntity(Document& document, Kind kind, std::int64_t id);
}  // namespace World

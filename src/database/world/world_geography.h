// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "database/world/world_document.h"
#include "global/languages.h"
#include "model/world_tuning.h"

/**
 * The football geography of a world: regions (pre-season tours stay inside
 * one) grouped into confederations (continental cups, national-team finals).
 * A world without its own regions or confederations uses the built-in ones;
 * a region without a confederation joins the first one, a country without a
 * region belongs to the first region. Ids are the entities' ids (1-255).
 */
namespace World
{
struct Region
{
  RegionID id = 0;
  std::string name;
  ConfederationID confederation = 0;
  /** Built-in region its foreign-player mix resembles. */
  WorldRegion flavour = WorldRegion::Europe;
};

/** A national-team trip to another confederation. */
struct Route
{
  std::string to;  ///< Name of the other confederation.
  std::uint16_t km = 0;
  std::uint8_t hours = 0;
};

struct ConfederationData
{
  ConfederationID id = 0;
  std::string name;
  /** Localisation key naming it (its name when the world gives none). */
  std::string label;
  bool national_finals = true;  ///< Hosts its own national-team finals.
  std::vector<Route> routes;
  nlohmann::json cups = nlohmann::json::array();  ///< Continental cups.
};

struct Geography
{
  std::vector<Region> regions;
  std::vector<ConfederationData> confederations;
  /** Confederation of every nationality the world places explicitly or
   * through a country; others belong to the first confederation. */
  std::unordered_map<Language, ConfederationID> nations;

  const Region* region(RegionID id) const;
  const ConfederationData* confederation(ConfederationID id) const;
  /** Confederation of a nationality (the first one when unplaced). */
  ConfederationID confederationOf(Language nationality) const;
};

/** Geography of @p world. @p with_nations also places nationalities (the
 * world must be installed: it needs their numbers). */
Geography geographyOf(const Document& world, bool with_nations = false);
/** Geography of the installed world, rebuilt when another is installed. */
const Geography& geography();
/** Id of the region of @p country: region_id, else the older "region" text
 * matched by name, else the first region. */
RegionID regionIdOf(const Geography& geography,
                        const nlohmann::json& country);
}  // namespace World

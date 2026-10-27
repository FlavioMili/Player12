// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/world/world_geography.h"

#include <algorithm>
#include <array>

#include "database/world/world_runtime.h"
#include "database/world/world_schema.h"
#include "global/nationality.h"

using json = nlohmann::json;

namespace World
{
namespace
{
constexpr std::array<std::string_view, 4> BUILT_IN_REGIONS = {
    "Europe", "EasternEurope", "NorthAmerica", "SouthAmerica"};

std::string squashed(std::string_view name)
{
  std::string text = normalise(name);
  std::erase(text, ' ');
  return text;
}

std::optional<WorldRegion> builtInRegion(std::string_view name)
{
  const std::string wanted = squashed(name);
  for (std::size_t i = 0; i < BUILT_IN_REGIONS.size(); ++i)
  {
    if (squashed(BUILT_IN_REGIONS[i]) == wanted)
    {
      return static_cast<WorldRegion>(i);
    }
  }
  return std::nullopt;
}

std::string text(const json& entity, const char* key)
{
  const auto found = entity.find(key);
  return found != entity.end() && found->is_string() ? found->get<std::string>()
                                                     : std::string();
}

/** The section of @p world, or the built-in world's when it has none. */
const json& sectionOrBuiltIn(const Document& world, Kind kind)
{
  return world.count(kind) != 0 ? world.section(kind)
                                : defaultWorld()->section(kind);
}
}  // namespace

const Region* Geography::region(RegionID id) const
{
  const auto found = std::ranges::find(regions, id, &Region::id);
  return found == regions.end() ? nullptr : &*found;
}

const ConfederationData* Geography::confederation(ConfederationID id) const
{
  const auto found =
      std::ranges::find(confederations, id, &ConfederationData::id);
  return found == confederations.end() ? nullptr : &*found;
}

ConfederationID Geography::confederationOf(Language nationality) const
{
  const auto found = nations.find(nationality);
  if (found != nations.end())
  {
    return found->second;
  }
  return confederations.empty() ? 0 : confederations.front().id;
}

RegionID regionIdOf(const Geography& geography, const json& country)
{
  const auto id = country.find("region_id");
  if (id != country.end() && id->is_number_integer() &&
      geography.region(static_cast<RegionID>(id->get<int>())) != nullptr)
  {
    return static_cast<RegionID>(id->get<int>());
  }
  const std::string legacy = squashed(text(country, "region"));
  for (const Region& region : geography.regions)
  {
    if (!legacy.empty() && squashed(region.name) == legacy)
    {
      return region.id;
    }
  }
  return geography.regions.empty() ? 0 : geography.regions.front().id;
}

Geography geographyOf(const Document& world, bool with_nations)
{
  Geography result;
  const auto byId = [](const json& a, const json& b)
  { return entityId(a) < entityId(b); };
  std::vector<json> confederations;
  for (const json& item : sectionOrBuiltIn(world, Kind::Confederation))
  {
    confederations.push_back(item);
  }
  std::ranges::stable_sort(confederations, byId);
  for (const json& item : confederations)
  {
    ConfederationData data;
    data.id = static_cast<ConfederationID>(entityId(item));
    data.name = text(item, "name");
    data.label = item.contains("name_key") ? text(item, "name_key") : data.name;
    data.national_finals = item.value("national_finals", true);
    if (const auto cups = item.find("cups");
        cups != item.end() && cups->is_array())
    {
      data.cups = *cups;
    }
    if (const auto routes = item.find("routes");
        routes != item.end() && routes->is_array())
    {
      for (const json& route : *routes)
      {
        data.routes.push_back({text(route, "to"),
                               route.value("km", std::uint16_t{0}),
                               route.value("hours", std::uint8_t{0})});
      }
    }
    result.confederations.push_back(std::move(data));
  }
  std::vector<json> regions;
  for (const json& item : sectionOrBuiltIn(world, Kind::Region))
  {
    regions.push_back(item);
  }
  std::ranges::stable_sort(regions, byId);
  for (const json& item : regions)
  {
    Region region;
    region.id = static_cast<RegionID>(entityId(item));
    region.name = text(item, "name");
    const auto parent = item.find("confederation_id");
    const auto parent_id = parent != item.end() && parent->is_number_integer()
                               ? static_cast<ConfederationID>(parent->get<int>())
                               : ConfederationID{0};
    region.confederation =
        result.confederation(parent_id) != nullptr ? parent_id
        : result.confederations.empty() ? ConfederationID{0}
                                        : result.confederations.front().id;
    const WorldRegion own =
        builtInRegion(region.name).value_or(WorldRegion::Europe);
    region.flavour = builtInRegion(text(item, "base")).value_or(own);
    result.regions.push_back(std::move(region));
  }
  if (!with_nations)
  {
    return result;
  }
  for (const json& item : confederations)
  {
    const auto list = item.find("nationalities");
    if (list == item.end() || !list->is_array())
    {
      continue;
    }
    for (const json& name : *list)
    {
      if (const auto nation = name.is_string()
                                  ? nationalityFromName(name.get<std::string>())
                                  : std::nullopt)
      {
        result.nations.try_emplace(*nation,
                                   static_cast<ConfederationID>(entityId(item)));
      }
    }
  }
  for (const json& country : world.section(Kind::Country))
  {
    const auto nation = nationalityFromName(demonym(country));
    const Region* region = result.region(regionIdOf(result, country));
    if (nation && region != nullptr)
    {
      result.nations.try_emplace(*nation, region->confederation);
    }
  }
  return result;
}

const Geography& geography()
{
  static PerWorld<Geography> cache;
  return cache.get([] { return geographyOf(*current(), true); });
}
}  // namespace World

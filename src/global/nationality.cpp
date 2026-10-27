// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "global/nationality.h"

#include <algorithm>
#include <memory>
#include <mutex>

#include "global/language_manager.h"

namespace
{
using Table = std::vector<CustomNationality>;

std::mutex& tableMutex()
{
  static std::mutex mutex;
  return mutex;
}

std::shared_ptr<const Table>& table()
{
  static std::shared_ptr<const Table> peoples = std::make_shared<Table>();
  return peoples;
}

std::shared_ptr<const Table> peoples()
{
  const std::scoped_lock lock(tableMutex());
  return table();
}

bool isBuiltIn(Language nationality)
{
  return std::to_underlying(nationality) < FIRST_CUSTOM_NATIONALITY;
}
}  // namespace

void setCustomNationalities(std::vector<CustomNationality> entries)
{
  constexpr std::size_t CAPACITY =
      LAST_CUSTOM_NATIONALITY - FIRST_CUSTOM_NATIONALITY + 1;
  if (entries.size() > CAPACITY)
  {
    entries.resize(CAPACITY);
  }
  auto fresh = std::make_shared<const Table>(std::move(entries));
  const std::scoped_lock lock(tableMutex());
  table() = std::move(fresh);
}

std::optional<CustomNationality> customNationality(Language nationality)
{
  if (isBuiltIn(nationality))
  {
    return std::nullopt;
  }
  const auto known = peoples();
  const std::size_t index =
      std::to_underlying(nationality) - FIRST_CUSTOM_NATIONALITY;
  if (index >= known->size())
  {
    return std::nullopt;
  }
  return (*known)[index];
}

std::string nationalityName(Language nationality)
{
  if (isBuiltIn(nationality))
  {
    const auto found = languageToString.find(nationality);
    return found == languageToString.end() ? std::string() : found->second;
  }
  const auto people = customNationality(nationality);
  return people ? people->name : std::string();
}

std::optional<Language> nationalityFromName(std::string_view name)
{
  if (const auto found = stringToLanguage.find(std::string(name));
      found != stringToLanguage.end())
  {
    return found->second;
  }
  const auto known = peoples();
  const auto found = std::ranges::find(*known, name, &CustomNationality::name);
  if (found == known->end())
  {
    return std::nullopt;
  }
  return static_cast<Language>(FIRST_CUSTOM_NATIONALITY +
                               static_cast<unsigned>(found - known->begin()));
}

std::vector<std::pair<Language, std::string>> allNationalities()
{
  std::vector<std::pair<Language, std::string>> all;
  for (const auto& [language, name] : languageToString)
  {
    all.emplace_back(language, name);
  }
  const auto known = peoples();
  for (std::size_t i = 0; i < known->size(); ++i)
  {
    all.emplace_back(static_cast<Language>(FIRST_CUSTOM_NATIONALITY + i),
                     (*known)[i].name);
  }
  std::ranges::sort(
      all, [](const auto& a, const auto& b) { return a.second < b.second; });
  return all;
}

std::string nationalityLabel(Language nationality)
{
  std::string name = nationalityName(nationality);
  if (name.empty() || !isBuiltIn(nationality))
  {
    return name;
  }
  const std::string key = "NATION_" + name;
  const char* localized = LOC(key.c_str());
  // LOC returns the key itself when a translation is missing.
  return key == localized ? name : std::string(localized);
}

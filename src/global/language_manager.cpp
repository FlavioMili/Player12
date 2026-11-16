// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "language_manager.h"

#include <format>
#include <fstream>
#include <nlohmann/json.hpp>

#include "global/logger.h"
#include "global/paths.h"

using json = nlohmann::json;

namespace
{
/** Reads a language file; false (with a reason) when it cannot be used. */
template <typename Table>
bool readTable(const std::string& path, Table& table, std::string& reason)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    reason = "could not open " + path;
    return false;
  }
  try
  {
    const json document = json::parse(file);
    if (!document.is_object())
    {
      reason = path + " is not a table of texts";
      return false;
    }
    Table loaded;
    loaded.reserve(document.size());
    for (const auto& [key, value] : document.items())
    {
      // A stray non-text entry costs that key only (English shows).
      if (value.is_string()) loaded.emplace(key, value.get<std::string>());
    }
    table.swap(loaded);
  }
  catch (const json::exception& exception)
  {
    reason = path + ": " + exception.what();
    return false;
  }
  return true;
}

std::string languageFile(Language lang)
{
  const auto name = languageToString.find(lang);
  return name == languageToString.end() ? std::string()
                                        : AssetPaths::language(name->second);
}
}  // namespace

bool LanguageManager::ensureEnglish()
{
  if (english_loaded) return true;
  std::string reason;
  english_loaded = readTable(languageFile(Language::EN), english, reason);
  if (!english_loaded)
    Logger::error("English texts unavailable, keys are shown instead: " +
                  reason);
  return english_loaded;
}

void LanguageManager::refreshNumberLocale()
{
  const auto text = [this](const char* key, const std::string& fallback)
  {
    const char* value = get(key);
    return value == key ? fallback : std::string(value);
  };
  const NumberFormat::Locale defaults = NumberFormat::Locale::english();
  number_locale.decimal_mark =
      text("NUMBER_DECIMAL_MARK", defaults.decimal_mark);
  number_locale.group_mark = text("NUMBER_GROUP_MARK", defaults.group_mark);
  number_locale.money_pattern = text("MONEY_PATTERN", defaults.money_pattern);
  number_locale.thousand_unit =
      text("MONEY_UNIT_THOUSAND", defaults.thousand_unit);
  number_locale.million_unit =
      text("MONEY_UNIT_MILLION", defaults.million_unit);
  number_locale.billion_unit =
      text("MONEY_UNIT_BILLION", defaults.billion_unit);
  // The two marks must differ, or numbers could not be read back.
  if (number_locale.decimal_mark.empty() ||
      number_locale.decimal_mark == number_locale.group_mark)
  {
    number_locale.decimal_mark = defaults.decimal_mark;
    number_locale.group_mark = defaults.group_mark;
  }
}

bool LanguageManager::loadLanguage(Language lang)
{
  if (lang != Language::EN) return loadFromFile(languageFile(lang), lang);
  const bool loaded = ensureEnglish();
  translations.clear();
  status_ = Status{Language::EN, !loaded, 0};
  refreshNumberLocale();
  return loaded;
}

bool LanguageManager::loadFromFile(const std::string& path, Language lang)
{
  ensureEnglish();
  status_ = Status{lang, false, 0};
  if (std::string reason; !readTable(path, translations, reason))
  {
    translations.clear();
    status_.file_failed = true;
    Logger::warn("Language file unusable, showing English instead: " + reason);
  }
  else
  {
    for (const auto& [key, value] : english)
      if (!translations.contains(key)) ++status_.missing_keys;
    if (status_.missing_keys > 0)
      Logger::warn(std::format("{} lacks {} texts, shown in English", path,
                               status_.missing_keys));
  }
  refreshNumberLocale();
  return !status_.file_failed;
}

const char* LanguageManager::get(const char* key) const
{
  const std::string_view name(key);
  if (!translations.empty())
    if (const auto it = translations.find(name); it != translations.end())
      return it->second.c_str();
  if (const auto it = english.find(name); it != english.end())
    return it->second.c_str();
  // The key's own pointer: callers detect a missing text by identity.
  return key;
}

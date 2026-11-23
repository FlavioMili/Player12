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

/**
 * Type-checked reads of world-database fields. World files come from users, so
 * any field can hold any JSON type; these helpers never throw and return the
 * default when the entity is not an object, the key is absent or the value has
 * the wrong type. Use them instead of nlohmann's value() / at().
 */
namespace World::Json
{
inline const nlohmann::json* field(const nlohmann::json& entity,
                                   const char* key)
{
  if (!entity.is_object())
  {
    return nullptr;
  }
  const auto found = entity.find(key);
  return found == entity.end() ? nullptr : &*found;
}

inline std::string text(const nlohmann::json& entity, const char* key,
                        const std::string& fallback = {})
{
  const nlohmann::json* found = field(entity, key);
  return found != nullptr && found->is_string() ? found->get<std::string>()
                                                : fallback;
}

inline std::int64_t integer(const nlohmann::json& entity, const char* key,
                            std::int64_t fallback = 0)
{
  const nlohmann::json* found = field(entity, key);
  return found != nullptr && found->is_number_integer()
             ? found->get<std::int64_t>()
             : fallback;
}

inline double number(const nlohmann::json& entity, const char* key,
                     double fallback = 0)
{
  const nlohmann::json* found = field(entity, key);
  return found != nullptr && found->is_number() ? found->get<double>()
                                                : fallback;
}

inline bool boolean(const nlohmann::json& entity, const char* key,
                    bool fallback = false)
{
  const nlohmann::json* found = field(entity, key);
  return found != nullptr && found->is_boolean() ? found->get<bool>()
                                                 : fallback;
}
}  // namespace World::Json

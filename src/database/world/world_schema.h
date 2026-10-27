// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "database/world/world_document.h"

/**
 * Field specifications of every world entity. One table drives validation,
 * the World editor's controls, conflict comparison and the template file, so
 * a new field is declared once.
 */
namespace World
{
enum class FieldType : std::uint8_t
{
  Integer,
  Number,
  Text,
  Colour,     ///< "#RRGGBB".
  Choice,     ///< One of choiceValues(set).
  Reference,  ///< Id of an entity of FieldSpec::reference.
  Stats,      ///< Object of attribute name -> 0..100.
};

/** Closed value lists used by Choice fields. */
enum class ChoiceSet : std::uint8_t
{
  None,
  Nationality,
  Role,
  Foot,
  Region,
  Tiebreak,
};

struct FieldSpec
{
  std::string_view path;  ///< Dotted path: "colours.home.shirt".
  FieldType type = FieldType::Text;
  bool required = false;
  double minimum = 0;        ///< Numbers: range. Text: minimum length.
  double maximum = 0;        ///< 0 = unbounded.
  std::string_view label;    ///< Localisation key.
  std::string_view section;  ///< Localisation key of the editor tab.
  ChoiceSet choices = ChoiceSet::None;
  std::optional<Kind> reference;
};

/** Fields of @p kind in editor order (the id is not listed: never edited). */
const std::vector<FieldSpec>& fields(Kind kind);
/** Allowed values of @p set (stored strings, not translated). */
const std::vector<std::string>& choiceValues(ChoiceSet set);

/** Value at a dotted @p path, or nullptr. */
const nlohmann::json* get(const nlohmann::json& entity, std::string_view path);
/** Sets (creating parents) or, for null, removes the value at @p path. */
void set(nlohmann::json& entity, std::string_view path, nlohmann::json value);

/** Leaf paths that differ between two entities, ids excluded, unknown keys
 * included, in a stable order. */
std::vector<std::string> differingPaths(const nlohmann::json& left,
                                        const nlohmann::json& right);
}  // namespace World

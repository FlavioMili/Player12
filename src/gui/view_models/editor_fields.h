// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gui/nav_history.h"

/** Presentation belongs to the feature, never inferred from SQL storage types.
 * Labels/sections/choice labels are localisation keys; stored choice values
 * remain independent of the current language. Missing schema columns are
 * omitted. Colour values are "#RRGGBB"; Toggle values are "1"/"0".
 */
struct EditorField
{
  enum class Control
  {
    Text,
    Number,
    Choice,
    Attributes,
    Colour,
    Toggle
  };
  std::string column;
  std::string label;
  std::string section;
  Control control = Control::Text;
  std::optional<double> minimum;
  std::optional<double> maximum;
  std::vector<std::pair<std::string, std::string>> choices;
  /** False shows choice labels literally (entity names, not LOC keys). */
  bool translate_choices = true;
  /** Number that may be absent: an empty value shows "Not set" with a button
   * that sets `initial`, and a set value gets a clear button. */
  bool optional = false;
  std::string initial;
  /** Optional localisation key shown muted under the control. */
  std::string hint;
};

/** Built-in gameplay controls. Unknown tables have no implicit editing surface;
 * new features supply EditorContext::editable_fields explicitly. Player stat
 * keys remain discovered from the player's live stats object.
 */
std::vector<EditorField> editorFields(std::string_view table);

/** Navigation support used by both the pencil affordance and the dialog. */
std::string_view editorTable(NavSection section);
bool hasBuiltinEditor(const std::optional<NavEntry>& context);

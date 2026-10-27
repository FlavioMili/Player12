// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------
#pragma once

#include <sqlite3.h>

#include <string>
#include <string_view>
#include <vector>

#include "gui/view_models/editor_fields.h"

/** Descriptor-driven form shared by the in-game editor and the world editor.
 * Values hold their data as text: numbers as decimal text, toggles as "1"/"0",
 * colours as "#RRGGBB" and player stats as a JSON object. */
namespace EditorForm
{
struct Value
{
  std::string name;
  std::string text;
  int type = SQLITE_TEXT;
  bool readonly = false;
};

/** Renders one control; returns true when the value changed. */
bool renderField(Value& value, const EditorField& descriptor);

/** Renders the sectioned tab bar and the selected section. Fields without a
 * matching, writable value are skipped. Returns true when any value changed. */
bool render(std::vector<Value>& values,
            const std::vector<EditorField>& descriptors, bool writable,
            const char* id = "##editor_sections");

/** Readable fallback label for a column or stat name. */
std::string humanLabel(std::string_view name);
}  // namespace EditorForm

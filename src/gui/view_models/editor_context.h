// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <map>
#include <string>
#include <vector>

#include "database/runtime_editor.h"
#include "gui/view_models/editor_fields.h"

/** A page declares its records and player-facing controls. The live schema
 * determines which declared fields exist, never their gameplay meaning.
 * Features supply presentation metadata without changing the dialog; built-in
 * player and club pages have navigation-based defaults. */
struct EditorContext
{
  // Feature-owned, player-facing controls. Unknown columns remain preserved
  // by persistence, but never become arbitrary inputs in the game UI.
  std::vector<EditorField> editable_fields;
  std::string table;
  std::map<std::string, RuntimeEditor::Value, std::less<>> filter;
  std::vector<std::string> related_tables;
  std::map<std::string,
           std::map<std::string, RuntimeEditor::Value, std::less<>>,
           std::less<>>
      related_filters;
};

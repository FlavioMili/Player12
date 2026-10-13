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

/** A page declares the records it exposes. The editor generates controls
 * from their live schema; adding a feature never requires a database-editor
 * table switch. Built-in pages also have navigation-based default contexts. */
struct EditorContext
{
  std::string table;
  std::map<std::string, RuntimeEditor::Value, std::less<>> filter;
  std::vector<std::string> related_tables;
  std::map<std::string,
           std::map<std::string, RuntimeEditor::Value, std::less<>>,
           std::less<>>
      related_filters;
};

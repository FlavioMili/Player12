// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include "database/runtime_editor.h"

/** Current engine's semantic rules. The generic editor never knows these
 * tables/fields. Future features can register their own controller validators.
 */
namespace EditorValidation
{
void validate(sqlite3* db, const std::vector<RuntimeEditor::Change>& changes);
}

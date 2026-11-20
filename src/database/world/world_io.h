// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "database/world/world_document.h"
#include "database/world/world_validate.h"

namespace World
{
struct LoadResult
{
  /** Present whenever the file parsed as a world, even with errors, so the
   * editor can open and repair it. */
  std::optional<Document> document;
  std::vector<Issue> issues;
  /** True when the document exists and has no errors. */
  bool ok() const { return document.has_value() && !hasErrors(issues); }
};

/** Parses, upgrades older formats and validates. Never throws. */
LoadResult readText(std::string_view text);
LoadResult readFile(const std::filesystem::path& path);

/**
 * Deterministic text: fixed key order, entities sorted by id, one entity per
 * line. Saving an unchanged world twice gives identical bytes, and shared
 * worlds diff line by line.
 */
std::string write(const Document& document);
/** Writes through a temporary file and a rename; false with @p error set
 * when the file cannot be written. */
bool writeFile(const Document& document, const std::filesystem::path& path,
               std::string* error = nullptr);

/** A small complete example world for makers to start from. */
Document templateDocument();

/** Converts an older document in place (currently schema 1 → 2). */
void upgrade(nlohmann::json& root);

/** UTF-8 path helpers (portable across Windows code pages). */
std::filesystem::path pathFromUtf8(std::string_view text);
std::string pathToUtf8(const std::filesystem::path& path);
}  // namespace World

// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "database/world/world_document.h"

namespace World
{
enum class Severity : std::uint8_t
{
  Error,     ///< The document cannot be imported or saved as it is.
  Blocking,  ///< Saveable, but no career can start from it yet.
  Warning,   ///< Accepted; the game uses a fallback.
};

/** What is wrong; the UI shows "WORLD_ISSUE_<code>" with field and detail. */
enum class IssueCode : std::uint8_t
{
  NotJson,
  TooLarge,
  CannotRead,
  NoManifest,
  NewerSchema,
  NotAList,
  NotAnObject,
  BadId,
  DuplicateId,
  Missing,
  WrongType,
  OutOfRange,
  BadChoice,
  BadColour,
  BadReference,
  DuplicateName,
  DuplicateShortName,
  ParentCycle,
  NoPlayableLeague,
  OddClubCount,
  TooFewClubs,
  NoTopDivision,
  SquadTooLarge,
  UnknownNationality,
  NoCountry,
  Unassigned,  ///< A club without a competition.
};

struct Issue
{
  Severity severity = Severity::Error;
  IssueCode code = IssueCode::NotJson;
  std::optional<Kind> kind;  ///< Section, when about an entity.
  std::int64_t id = -1;      ///< Entity id, when it has one.
  std::int64_t index = -1;   ///< Position in the section.
  std::string name;          ///< Entity display name.
  std::string field;         ///< Dotted field path.
  std::string detail;        ///< Extra value (limit, version...).
};

/** File-level problems and every field of every entity: errors and
 * warnings. Never throws. */
std::vector<Issue> validate(const Document& document);
/** Structural problems that block starting a career (Blocking). */
std::vector<Issue> playability(const Document& document);
bool hasErrors(const std::vector<Issue>& issues);
std::size_t countOf(const std::vector<Issue>& issues, Severity severity);
/** Plain English description for logs and tests. */
std::string describe(const Issue& issue);
}  // namespace World

// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/world/world_library.h"

#include <algorithm>
#include <cctype>
#include <system_error>

#include "global/runtime_paths.h"

namespace fs = std::filesystem;

namespace World::Library
{
namespace
{
constexpr std::size_t MAX_STEM = 64;

fs::path uniquePath(const std::string& stem)
{
  const fs::path dir = directory();
  const std::string extension(FILE_EXTENSION);
  fs::path candidate = dir / pathFromUtf8(stem + extension);
  std::error_code error;
  for (int suffix = 2; fs::exists(candidate, error); ++suffix)
  {
    candidate =
        dir / pathFromUtf8(stem + "-" + std::to_string(suffix) + extension);
  }
  return candidate;
}

bool insideLibrary(const fs::path& path)
{
  std::error_code error;
  const fs::path dir = fs::weakly_canonical(directory(), error);
  const fs::path file = fs::weakly_canonical(path, error);
  return !error && file.parent_path() == dir;
}
}  // namespace

fs::path directory() { return RuntimePaths::root() / "databases"; }

std::string fileStem(std::string_view name)
{
  std::string stem;
  for (const char c : name)
  {
    const auto byte = static_cast<unsigned char>(c);
    if (std::isalnum(byte) != 0 || c == '-' || c == '_' || c == '.')
    {
      stem += c;
    }
    else if ((c == ' ' || byte >= 0x80) && !stem.empty() && stem.back() != '_')
    {
      stem += '_';
    }
    if (stem.size() >= MAX_STEM)
    {
      break;
    }
  }
  while (!stem.empty() && (stem.back() == '_' || stem.back() == '.'))
  {
    stem.pop_back();
  }
  while (!stem.empty() && (stem.front() == '.' || stem.front() == '_'))
  {
    stem.erase(stem.begin());
  }
  return stem.empty() ? std::string("world") : stem;
}

Entry summarise(const Document& document, const std::vector<Issue>& issues)
{
  Entry entry;
  entry.manifest = document.manifest();
  for (const Kind kind : KINDS)
  {
    entry.counts.at(static_cast<std::size_t>(kind)) = document.count(kind);
  }
  entry.errors = countOf(issues, Severity::Error);
  entry.warnings = countOf(issues, Severity::Warning);
  entry.blocking = countOf(playability(document), Severity::Blocking);
  return entry;
}

std::vector<Entry> list()
{
  std::vector<Entry> entries;
  std::error_code error;
  for (const auto& item : fs::directory_iterator(directory(), error))
  {
    if (!item.is_regular_file(error) ||
        pathToUtf8(item.path().extension()) != FILE_EXTENSION)
    {
      continue;
    }
    const LoadResult loaded = readFile(item.path());
    Entry entry;
    if (loaded.document)
    {
      entry = summarise(*loaded.document, loaded.issues);
    }
    else
    {
      entry.errors = loaded.issues.size();
    }
    if (entry.manifest.name.empty())
    {
      entry.manifest.name = pathToUtf8(item.path().stem());
    }
    entry.path = item.path();
    entries.push_back(std::move(entry));
  }
  std::ranges::sort(entries, {}, [](const Entry& entry)
                    { return normalise(entry.manifest.name); });
  return entries;
}

ImportResult importFile(const fs::path& source)
{
  ImportResult result;
  result.load = readFile(source);
  if (!result.load.ok())
  {
    return result;
  }
  std::error_code error;
  fs::create_directories(directory(), error);
  const fs::path target = uniquePath(fileStem(pathToUtf8(source.stem())));
  fs::copy_file(source, target, error);
  if (error)
  {
    Issue issue;
    issue.code = IssueCode::CannotRead;
    issue.detail = error.message();
    result.load.issues.push_back(std::move(issue));
    return result;
  }
  result.stored = target;
  return result;
}

fs::path save(const Document& document, const fs::path& existing)
{
  const fs::path target = !existing.empty() && insideLibrary(existing)
                              ? existing
                              : uniquePath(fileStem(document.manifest().name));
  std::string message;
  if (!writeFile(document, target, &message))
  {
    return {};
  }
  return target;
}

bool remove(const fs::path& path)
{
  if (!insideLibrary(path))
  {
    return false;
  }
  std::error_code error;
  return fs::remove(path, error) && !error;
}
}  // namespace World::Library

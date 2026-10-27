// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "SQLLoader.h"

#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "global/logger.h"
#include "global/paths.h"
#include "global/queries.h"

// static storage for queries
std::unordered_map<Query, std::string> SQLLoader::queries_{};

// -------------------- PUBLIC --------------------

void SQLLoader::loadQueriesFromFile()
{
  std::string content = readFile(AssetPaths::queries());
  parseQueries(content);
}

std::string SQLLoader::loadSchemaFromFile()
{
  return readFile(AssetPaths::schema());
}

const std::string& SQLLoader::getQuery(const Query query_id)
{
  auto it = queries_.find(query_id);
  if (it == queries_.end())
  {
    throw std::runtime_error("Query not found for ID: " +
                             std::to_string(static_cast<int>(query_id)));
  }
  return it->second;
}

// -------------------- PRIVATE --------------------

std::string SQLLoader::readFile(const std::string& filepath)
{
  std::ifstream file(filepath);
  if (!file.is_open())
  {
    throw std::runtime_error("Cannot open file: " + filepath);
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void SQLLoader::parseQueries(const std::string& content)
{
  queries_.clear();
  std::istringstream stream(content);
  std::string line;
  std::string current_query_str;
  Query current_query_enum{};
  bool in_query = false;
  bool has_query_id = false;

  while (std::getline(stream, line))
  {
    line = trim(line);

    // skip empty lines or comments (except query ID)
    if (line.empty() ||
        (line.starts_with("--") && !line.contains("@QUERY_ID:")))
    {
      continue;
    }

    // check for query ID
    if (line.contains("-- @QUERY_ID:"))
    {
      // save previous query
      if (has_query_id && !current_query_str.empty())
      {
        queries_[current_query_enum] = trim(current_query_str);
      }

      // extract query string
      size_t start = line.find("@QUERY_ID:") + 10;
      std::string id = trim(line.substr(start));

      // map string → enum via your constexpr map
      auto it = string_to_query_map.find(id);
      if (it == string_to_query_map.end())
      {
        throw std::runtime_error("Unknown query ID: " + id);
      }
      current_query_enum = it->second;

      current_query_str.clear();
      in_query = true;
      has_query_id = true;
      continue;
    }

    // accumulate query
    if (in_query && !line.empty())
    {
      if (!current_query_str.empty()) current_query_str += " ";
      current_query_str += line;
    }
  }

  // save last query
  if (has_query_id && !current_query_str.empty())
  {
    queries_[current_query_enum] = trim(current_query_str);
  }

  Logger::debug(std::format("Loaded {} queries from file.\n", queries_.size()));
}

std::string SQLLoader::trim(const std::string& str)
{
  size_t start = str.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) return "";

  size_t end = str.find_last_not_of(" \t\n\r");
  return str.substr(start, end - start + 1);
}

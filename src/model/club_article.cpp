// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "model/club_article.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "database/datagenerator.h"
#include "global/logger.h"

namespace
{
enum class Article : std::uint8_t
{
  Il,
  Lo,
  Elided,
  La
};

// Fused forms per article, in the order of PREPOSITIONS.
constexpr std::array<std::string_view, 6> PREPOSITIONS = {"il", "di", "a",
                                                          "da", "in", "su"};
constexpr std::array<std::array<std::string_view, 6>, 4> FUSED = {{
    {"il ", "del ", "al ", "dal ", "nel ", "sul "},
    {"lo ", "dello ", "allo ", "dallo ", "nello ", "sullo "},
    {"l'", "dell'", "all'", "dall'", "nell'", "sull'"},
    {"la ", "della ", "alla ", "dalla ", "nella ", "sulla "},
}};

std::optional<Article> parse(std::string_view text)
{
  using enum Article;
  if (text == "il") return Il;
  if (text == "lo") return Lo;
  if (text == "l'") return Elided;
  if (text == "la") return La;
  return std::nullopt;
}

const std::unordered_map<std::string, std::string>& overrides()
{
  static const auto loaded = []
  {
    try
    {
      return DataGenerator::loadClubArticles();
    }
    catch (const std::exception& error)
    {
      Logger::error(std::string("Club articles not loaded: ") + error.what());
      return std::unordered_map<std::string, std::string>{};
    }
  }();
  return loaded;
}

bool isVowel(char c)
{
  switch (std::tolower(static_cast<unsigned char>(c)))
  {
    case 'a':
    case 'e':
    case 'i':
    case 'o':
    case 'u':
      return true;
    default:
      return false;
  }
}

Article defaultArticle(std::string_view name)
{
  using enum Article;
  if (name.empty()) return Il;
  // Accented capitals and other multi-byte initials start with a vowel in
  // every club name of the pack (e.g. "Évora").
  const auto first = static_cast<unsigned char>(name.front());
  if (first >= 0x80 || isVowel(name.front())) return Elided;
  const auto lower = static_cast<char>(std::tolower(first));
  const char next =
      name.size() > 1
          ? static_cast<char>(std::tolower(static_cast<unsigned char>(name[1])))
          : '\0';
  if (lower == 'h' && isVowel(next)) return Elided;
  if (lower == 'z' || lower == 'x' || lower == 'y') return Lo;
  if (lower == 's' && next != '\0' && !isVowel(next)) return Lo;
  if ((lower == 'g' && next == 'n') || (lower == 'p' && next == 's') ||
      (lower == 'p' && next == 'n'))
    return Lo;
  return Il;
}

Article articleOf(std::string_view club_name)
{
  const auto& table = overrides();
  if (const auto found = table.find(std::string(club_name));
      found != table.end())
  {
    if (const auto article = parse(found->second)) return *article;
  }
  return defaultArticle(club_name);
}
}  // namespace

namespace ClubArticle
{

std::string_view italian(std::string_view club_name)
{
  static constexpr std::array<std::string_view, 4> BARE = {"il", "lo", "l'",
                                                           "la"};
  return BARE[static_cast<std::size_t>(
      std::to_underlying(articleOf(club_name)))];
}

std::string withPreposition(std::string_view club_name,
                            std::string_view preposition)
{
  if (preposition.empty()) return std::string(club_name);
  const bool capital =
      std::isupper(static_cast<unsigned char>(preposition.front())) != 0;
  std::string lower(preposition);
  lower.front() = static_cast<char>(
      std::tolower(static_cast<unsigned char>(lower.front())));
  // MSVC array iterators are wrapper types, not pointers.
  // NOLINTNEXTLINE(readability-qualified-auto)
  const auto found = std::ranges::find(PREPOSITIONS, lower);
  if (found == PREPOSITIONS.end()) return std::string(club_name);
  const auto index = static_cast<std::size_t>(found - PREPOSITIONS.begin());
  std::string result(FUSED[static_cast<std::size_t>(
      std::to_underlying(articleOf(club_name)))][index]);
  if (capital)
    result.front() = static_cast<char>(
        std::toupper(static_cast<unsigned char>(result.front())));
  result += club_name;
  return result;
}

}  // namespace ClubArticle

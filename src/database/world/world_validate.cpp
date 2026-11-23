// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "database/world/world_validate.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

#include "database/world/world_json.h"
#include "database/world/world_schema.h"
#include "global/languages.h"
#include "model/role_utils.h"

using json = nlohmann::json;

namespace World
{
namespace
{
/** Squads larger than this cannot be handled by the matchday code. */
constexpr std::size_t MAX_SQUAD = 60;
/** Smallest division the scheduler and promotion rules accept. */
constexpr std::size_t MIN_CLUBS = 4;

struct Context
{
  const Document& document;
  std::vector<Issue>& issues;
  Kind kind;
  std::int64_t index;
  const json& entity;
  const std::vector<std::string>& nations;  ///< Accepted nationalities.

  void add(Severity severity, IssueCode code, std::string field = {},
           std::string detail = {}) const
  {
    Issue issue;
    issue.severity = severity;
    issue.code = code;
    issue.kind = kind;
    issue.id = entityId(entity);
    issue.index = index;
    issue.name = entityName(kind, entity);
    issue.field = std::move(field);
    issue.detail = std::move(detail);
    issues.push_back(std::move(issue));
  }
};

std::string range(const FieldSpec& spec)
{
  const auto number = [](double value)
  {
    std::string text = std::to_string(value);
    text.erase(text.find_last_not_of('0') + 1);
    if (!text.empty() && text.back() == '.')
    {
      text.pop_back();
    }
    return text;
  };
  return number(spec.minimum) + "-" + number(spec.maximum);
}

bool isColour(const json& value)
{
  if (!value.is_string())
  {
    return false;
  }
  const auto& text = value.get_ref<const std::string&>();
  return text.size() == 7 && text[0] == '#' &&
         std::all_of(
             text.begin() + 1, text.end(), [](char c)
             { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

bool hasRange(const FieldSpec& spec)
{
  return spec.minimum != 0 || spec.maximum != 0;
}

void checkNumber(const Context& context, const FieldSpec& spec,
                 const json& value)
{
  const bool integer = spec.type == FieldType::Integer;
  if (integer ? !value.is_number_integer() : !value.is_number())
  {
    context.add(Severity::Error, IssueCode::WrongType, std::string(spec.path),
                integer ? "integer" : "number");
    return;
  }
  const double number = value.get<double>();
  if (hasRange(spec) && (number < spec.minimum || number > spec.maximum))
  {
    context.add(Severity::Error, IssueCode::OutOfRange, std::string(spec.path),
                range(spec));
  }
}

void checkChoice(const Context& context, const FieldSpec& spec,
                 const json& value)
{
  if (!value.is_string())
  {
    context.add(Severity::Error, IssueCode::WrongType, std::string(spec.path),
                "text");
    return;
  }
  const auto& text = value.get_ref<const std::string&>();
  if (spec.choices == ChoiceSet::Role)
  {
    if (RoleUtils::fromString(text) == PlayerRole::UNKNOWN)
    {
      context.add(Severity::Error, IssueCode::BadChoice, std::string(spec.path),
                  text);
    }
    return;
  }
  const auto& allowed = spec.choices == ChoiceSet::Nationality
                            ? context.nations
                            : choiceValues(spec.choices);
  if (std::ranges::find(allowed, text) != allowed.end())
  {
    return;
  }
  if (spec.choices == ChoiceSet::Nationality)
  {
    context.add(Severity::Warning, IssueCode::UnknownNationality,
                std::string(spec.path), text);
  }
  else
  {
    context.add(Severity::Error, IssueCode::BadChoice, std::string(spec.path),
                text);
  }
}

void checkReference(const Context& context, const FieldSpec& spec,
                    const json& value)
{
  // A language may also be named: any built-in language.
  if (spec.reference == Kind::Language && value.is_string())
  {
    const auto& text = value.get_ref<const std::string&>();
    if (!stringToLanguage.contains(text))
    {
      context.add(Severity::Error, IssueCode::BadReference,
                  std::string(spec.path), text);
    }
    return;
  }
  if (!value.is_number_integer())
  {
    context.add(Severity::Error, IssueCode::WrongType, std::string(spec.path),
                "id");
    return;
  }
  const auto id = value.get<std::int64_t>();
  // Club 0 is the free-agent pool.
  if (spec.reference == Kind::Club && id == 0)
  {
    return;
  }
  if (context.document.find(*spec.reference, id) == nullptr)
  {
    context.add(Severity::Error, IssueCode::BadReference,
                std::string(spec.path), std::to_string(id));
  }
}

void checkStats(const Context& context, const FieldSpec& spec,
                const json& value)
{
  if (!value.is_object() || value.empty())
  {
    context.add(Severity::Error, IssueCode::WrongType, std::string(spec.path),
                "attributes");
    return;
  }
  for (const auto& [name, rating] : value.items())
  {
    const std::string path = std::string(spec.path) + "." + name;
    if (!rating.is_number())
    {
      context.add(Severity::Error, IssueCode::WrongType, path, "number");
    }
    else if (rating.get<double>() < 0 || rating.get<double>() > 100)
    {
      context.add(Severity::Warning, IssueCode::OutOfRange, path, "0-100");
    }
  }
}

void checkList(const Context& context, const FieldSpec& spec, const json& value)
{
  if (!value.is_array() ||
      !std::ranges::all_of(value, [](const json& name)
                           { return name.is_string() && !name.empty(); }))
  {
    context.add(Severity::Error, IssueCode::WrongType, std::string(spec.path),
                "names");
  }
}

void checkField(const Context& context, const FieldSpec& spec)
{
  const json* value = read(context.entity, spec);
  if (value == nullptr || value->is_null())
  {
    if (spec.required)
    {
      context.add(Severity::Error, IssueCode::Missing, std::string(spec.path));
    }
    return;
  }
  switch (spec.type)
  {
    case FieldType::Integer:
    case FieldType::Number:
      checkNumber(context, spec, *value);
      break;
    case FieldType::Text:
      if (!value->is_string())
      {
        context.add(Severity::Error, IssueCode::WrongType,
                    std::string(spec.path), "text");
      }
      else if (const auto length = static_cast<double>(
                   value->get_ref<const std::string&>().size());
               length < spec.minimum ||
               (spec.maximum > 0 && length > spec.maximum))
      {
        context.add(Severity::Error, IssueCode::OutOfRange,
                    std::string(spec.path), range(spec));
      }
      break;
    case FieldType::Colour:
      if (!isColour(*value))
      {
        context.add(Severity::Error, IssueCode::BadColour,
                    std::string(spec.path));
      }
      break;
    case FieldType::Choice:
      checkChoice(context, spec, *value);
      break;
    case FieldType::Reference:
      checkReference(context, spec, *value);
      break;
    case FieldType::Stats:
      checkStats(context, spec, *value);
      break;
    case FieldType::List:
      checkList(context, spec, *value);
      break;
    case FieldType::Boolean:
      if (!value->is_boolean())
      {
        context.add(Severity::Error, IssueCode::WrongType,
                    std::string(spec.path), "yes/no");
      }
      break;
  }
}

/** Checks @p item against @p specs; issues belong to the entity of
 * @p context and name the field as @p prefix + its path ("cups[0].clubs"). */
void checkNested(const Context& context, const std::string& prefix,
                 const json& item, const std::vector<FieldSpec>& specs)
{
  std::vector<Issue> found;
  const Context inner{context.document, found, context.kind,
                      context.index,    item,  context.nations};
  for (const FieldSpec& spec : specs)
  {
    checkField(inner, spec);
  }
  for (Issue& issue : found)
  {
    issue.id = entityId(context.entity);
    issue.name = entityName(context.kind, context.entity);
    issue.field = prefix + "." + issue.field;
    context.issues.push_back(std::move(issue));
  }
}

/** A cup's list of whole numbers: "places" (@p count 0: at least one, each
 * 0-64) or "weekday_offsets" (2 days, 0-6). The code reads them as bytes, so
 * anything else would fail. */
void checkByteList(const Context& context, const std::string& path,
                   const json& item, std::string_view key, std::size_t count)
{
  const auto found = item.find(std::string(key));
  if (found == item.end())
  {
    return;
  }
  const bool fits = found->is_array() &&
                    (count == 0 ? !found->empty() : found->size() == count);
  if (!fits || !std::ranges::all_of(*found, [](const json& number)
                                    { return number.is_number_integer(); }))
  {
    context.add(Severity::Error, IssueCode::WrongType, path,
                count == 0 ? "numbers" : "2 numbers");
  }
  else if (!std::ranges::all_of(*found,
                                [&](const json& number)
                                {
                                  return number.get<std::int64_t>() >= 0 &&
                                         number.get<std::int64_t>() <=
                                             (count == 0 ? 64 : 6);
                                }))
  {
    context.add(Severity::Error, IssueCode::OutOfRange, path,
                count == 0 ? "0-64" : "0-6");
  }
}

/** Cups and routes of a confederation (lists of objects, read leniently). */
void checkConfederation(const Context& context)
{
  const auto list =
      [&](const char* key, const std::vector<FieldSpec>& specs, auto&& extra)
  {
    const auto found = context.entity.find(key);
    if (found == context.entity.end() || found->is_null())
    {
      return;
    }
    if (!found->is_array())
    {
      context.add(Severity::Error, IssueCode::WrongType, key, "list");
      return;
    }
    for (std::size_t i = 0; i < found->size(); ++i)
    {
      const std::string path = std::string(key) + "[" + std::to_string(i) + "]";
      if (!(*found)[i].is_object())
      {
        context.add(Severity::Error, IssueCode::WrongType, path, "object");
        continue;
      }
      checkNested(context, path, (*found)[i], specs);
      extra(path, (*found)[i]);
    }
  };
  list("cups", cupFields(),
       [&](const std::string& path, const json& cup)
       {
         checkByteList(context, path + ".places", cup, "places", 0);
         checkByteList(context, path + ".weekday_offsets", cup,
                       "weekday_offsets", 2);
       });
  list("routes", routeFields(),
       [&](const std::string& path, const json& route)
       {
         const std::string name =
             route.contains("to") && route["to"].is_string()
                 ? route["to"].get<std::string>()
                 : std::string();
         const json& all = context.document.section(Kind::Confederation);
         if (!name.empty() &&
             std::ranges::none_of(
                 all, [&](const json& other)
                 { return other.contains("name") && other["name"] == name; }))
         {
           context.add(Severity::Warning, IssueCode::BadReference, path + ".to",
                       name);
         }
       });
}

void checkSection(const Document& document, Kind kind,
                  std::vector<Issue>& issues,
                  const std::vector<std::string>& nations)
{
  const json& root = document.root();
  if (const auto found = root.find(std::string(sectionKey(kind)));
      found != root.end() && !found->is_array())
  {
    Issue issue;
    issue.code = IssueCode::NotAList;
    issue.kind = kind;
    issues.push_back(std::move(issue));
    return;
  }
  const json& array = document.section(kind);
  std::unordered_set<std::int64_t> ids;
  for (std::size_t i = 0; i < array.size(); ++i)
  {
    const json& entity = array[i];
    const Context context{document, issues, kind, static_cast<std::int64_t>(i),
                          entity,   nations};
    if (!entity.is_object())
    {
      context.add(Severity::Error, IssueCode::NotAnObject);
      continue;
    }
    if (kind != Kind::Travel)
    {
      const std::int64_t id = entityId(entity);
      if (id < minId(kind) || id > maxId(kind))
      {
        context.add(
            Severity::Error, IssueCode::BadId, "id",
            std::to_string(minId(kind)) + "-" + std::to_string(maxId(kind)));
      }
      else if (!ids.insert(id).second)
      {
        context.add(Severity::Error, IssueCode::DuplicateId, "id",
                    std::to_string(id));
      }
    }
    for (const FieldSpec& spec : fields(kind))
    {
      checkField(context, spec);
    }
    if (kind == Kind::Confederation)
    {
      checkConfederation(context);
    }
    if (kind == Kind::Travel && Json::field(entity, "from") != nullptr &&
        Json::field(entity, "to") != nullptr &&
        *Json::field(entity, "from") == *Json::field(entity, "to"))
    {
      context.add(Severity::Error, IssueCode::OutOfRange, "to");
    }
    if (kind == Kind::League && !entity.contains("country_id"))
    {
      context.add(Severity::Warning, IssueCode::NoCountry, "country_id");
    }
  }
}

void checkNames(const Document& document, bool first,
                std::vector<Issue>& issues)
{
  const json& pool = document.names(first);
  if (pool.is_null())
  {
    return;
  }
  const auto valid = [](const json& list)
  {
    return list.is_array() && !list.empty() &&
           std::ranges::all_of(list, [](const json& name)
                               { return name.is_string() && !name.empty(); });
  };
  bool ok = pool.is_object() && pool.contains("names") && valid(pool["names"]);
  if (ok && pool.contains("by_nationality"))
  {
    const json& by = pool["by_nationality"];
    ok = by.is_object() &&
         std::ranges::all_of(by.items(), [&valid](const auto& item)
                             { return valid(item.value()); });
  }
  if (!ok)
  {
    Issue issue;
    issue.code = IssueCode::WrongType;
    issue.field = first ? "first_names" : "last_names";
    issues.push_back(std::move(issue));
  }
}

Issue entityIssue(Severity severity, IssueCode code, Kind kind,
                  const json& entity, std::string detail = {})
{
  Issue issue;
  issue.severity = severity;
  issue.code = code;
  issue.kind = kind;
  issue.id = entityId(entity);
  issue.name = entityName(kind, entity);
  issue.detail = std::move(detail);
  return issue;
}

std::int64_t integer(const json& entity, const char* key)
{
  const auto found = entity.find(key);
  return found != entity.end() && found->is_number_integer()
             ? found->get<std::int64_t>()
             : 0;
}
}  // namespace

std::vector<Issue> validate(const Document& document)
{
  std::vector<Issue> issues;
  const Manifest manifest = document.manifest();
  if (manifest.name.empty())
  {
    Issue issue;
    issue.code = IssueCode::Missing;
    issue.field = "manifest.name";
    issues.push_back(std::move(issue));
  }
  const std::vector<std::string> nations = nationalities(document);
  for (const Kind kind : KINDS)
  {
    checkSection(document, kind, issues, nations);
  }
  checkNames(document, true, issues);
  checkNames(document, false, issues);
  return issues;
}

std::vector<Issue> playability(const Document& document)
{
  std::vector<Issue> issues;
  const json& leagues = document.section(Kind::League);
  const json& clubs = document.section(Kind::Club);
  std::map<std::int64_t, std::size_t> clubs_per_league;
  std::map<std::int64_t, std::set<std::string>> names_per_league;
  std::unordered_map<std::string, std::int64_t> short_names;
  for (const json& club : clubs)
  {
    const std::int64_t league = integer(club, "league_id");
    if (league == 0)
    {
      issues.push_back(entityIssue(Severity::Blocking, IssueCode::Unassigned,
                                   Kind::Club, club));
    }
    ++clubs_per_league[league];
    if (!names_per_league[league]
             .insert(normalise(entityName(Kind::Club, club)))
             .second)
    {
      issues.push_back(entityIssue(Severity::Blocking, IssueCode::DuplicateName,
                                   Kind::Club, club));
    }
    std::string code = Json::text(club, "short_name");
    std::ranges::transform(
        code, code.begin(),
        [](char c)
        {
          return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        });
    if (code.empty())
    {
      continue;
    }
    if (const auto [it, inserted] = short_names.try_emplace(code, 0); !inserted)
    {
      issues.push_back(entityIssue(Severity::Blocking,
                                   IssueCode::DuplicateShortName, Kind::Club,
                                   club, code));
    }
  }
  bool playable = false;
  for (const json& league : leagues)
  {
    const std::size_t count = clubs_per_league[entityId(league)];
    playable = playable || count >= MIN_CLUBS;
    if (count < MIN_CLUBS)
    {
      issues.push_back(entityIssue(Severity::Blocking, IssueCode::TooFewClubs,
                                   Kind::League, league,
                                   std::to_string(MIN_CLUBS)));
    }
    else if (count % 2 != 0)
    {
      issues.push_back(entityIssue(Severity::Blocking, IssueCode::OddClubCount,
                                   Kind::League, league,
                                   std::to_string(count)));
    }
    // Follow the parents; more steps than leagues means a loop.
    std::int64_t parent = integer(league, "parent_league");
    for (std::size_t steps = 0; parent != 0; ++steps)
    {
      if (parent == entityId(league) || steps > leagues.size())
      {
        issues.push_back(entityIssue(Severity::Blocking, IssueCode::ParentCycle,
                                     Kind::League, league));
        break;
      }
      const json* next = document.find(Kind::League, parent);
      parent = next ? integer(*next, "parent_league") : 0;
    }
  }
  if (!playable)
  {
    Issue issue;
    issue.severity = Severity::Blocking;
    issue.code = IssueCode::NoPlayableLeague;
    issues.push_back(std::move(issue));
  }
  std::unordered_map<std::int64_t, std::size_t> squads;
  for (const json& player : document.section(Kind::Player))
  {
    if (const std::int64_t club = integer(player, "team_id");
        club != 0 && ++squads[club] == MAX_SQUAD + 1)
    {
      if (const json* entity = document.find(Kind::Club, club))
      {
        issues.push_back(entityIssue(Severity::Blocking,
                                     IssueCode::SquadTooLarge, Kind::Club,
                                     *entity, std::to_string(MAX_SQUAD)));
      }
    }
  }
  return issues;
}

bool hasErrors(const std::vector<Issue>& issues)
{
  return std::ranges::any_of(issues, [](const Issue& issue)
                             { return issue.severity == Severity::Error; });
}

std::size_t countOf(const std::vector<Issue>& issues, Severity severity)
{
  return static_cast<std::size_t>(
      std::ranges::count(issues, severity, &Issue::severity));
}

std::string describe(const Issue& issue)
{
  static const std::array<const char*, 26> NAMES = {
      "not JSON",
      "file too large",
      "cannot read file",
      "no manifest",
      "newer format",
      "not a list",
      "not an object",
      "bad id",
      "duplicate id",
      "missing",
      "wrong type",
      "out of range",
      "unknown value",
      "bad colour",
      "unknown reference",
      "duplicate name",
      "duplicate short name",
      "division loop",
      "no playable league",
      "odd number of clubs",
      "too few clubs",
      "no top division",
      "squad too large",
      "unknown nationality",
      "no country",
      "no competition",
  };
  std::string text = NAMES.at(static_cast<std::size_t>(issue.code));
  if (issue.kind)
  {
    text = std::string(sectionKey(*issue.kind)) + ": " + text;
  }
  if (!issue.name.empty())
  {
    text += " [" + issue.name + "]";
  }
  if (!issue.field.empty())
  {
    text += " " + issue.field;
  }
  if (!issue.detail.empty())
  {
    text += " (" + issue.detail + ")";
  }
  return text;
}
}  // namespace World

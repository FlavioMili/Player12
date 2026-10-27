// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <imgui.h>

#include <algorithm>
#include <charconv>
#include <cmath>

#include "database/runtime_editor.h"
#include "database/world/world_schema.h"
#include "global/language_manager.h"
#include "global/nationality.h"
#include "gui/scenes/world_editor_state.h"

using json = nlohmann::json;
using World::Kind;
using State = WorldEditorScene::State;
using Page = State::Page;
using namespace WorldEditorUi;

namespace
{
std::string upper(std::string text)
{
  std::ranges::transform(
      text, text.begin(),
      [](char c)
      {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      });
  return text;
}
/** "Name (Competition)" for clubs, plain names otherwise. */
std::string referenceLabel(const World::Document& doc, Kind kind,
                           const json& entity)
{
  std::string name = World::entityName(kind, entity);
  if (kind == Kind::Club)
  {
    if (const json* league =
            doc.find(Kind::League, integer(entity, "league_id")))
    {
      name += " (" + World::entityName(Kind::League, *league) + ")";
    }
  }
  return name;
}
std::vector<std::pair<std::string, std::string>> referenceChoices(
    const World::Document& doc, Kind kind, bool optional)
{
  std::vector<std::pair<std::string, std::string>> choices;
  for (const json& entity : doc.section(kind))
  {
    choices.emplace_back(std::to_string(World::entityId(entity)),
                         referenceLabel(doc, kind, entity));
  }
  if (kind == Kind::Language)
  {
    // Any built-in language can be spoken too; those are stored by name.
    for (const auto& [language, name] : allNationalities())
    {
      if (!customNationality(language))
      {
        choices.emplace_back(name, nationalityLabel(language));
      }
    }
  }
  std::ranges::sort(
      choices, [](const auto& a, const auto& b)
      { return World::normalise(a.second) < World::normalise(b.second); });
  if (optional)
  {
    choices.insert(choices.begin(), {"", LOC("WORLD_NONE")});
  }
  return choices;
}
/** Text of @p key, or @p fallback when the key has no translation. */
std::string localised(const std::string& key, const std::string& fallback)
{
  const char* text = LOC(key.c_str());
  return text == key.c_str() ? fallback : std::string(text);
}
/** Closed value lists shown with translated labels where the game has them. */
std::vector<std::pair<std::string, std::string>> listChoices(
    const World::Document& doc, const World::FieldSpec& spec)
{
  using World::ChoiceSet;
  std::vector<std::pair<std::string, std::string>> choices;
  if (!spec.required)
  {
    choices.emplace_back("", LOC("WORLD_NONE"));
  }
  const bool people = spec.choices == ChoiceSet::Nationality;
  for (const std::string& value :
       people ? World::nationalities(doc) : World::choiceValues(spec.choices))
  {
    std::string key;
    switch (spec.choices)
    {
      case ChoiceSet::Nationality:
        key = "NATION_" + value;
        break;
      case ChoiceSet::Role:
        key = "ROLE_LONG_" + value;
        break;
      case ChoiceSet::Foot:
        key = "PLAYER_FOOT_" + upper(value);
        break;
      case ChoiceSet::Region:
        key = "WORLD_REGION_" + value;
        break;
      case ChoiceSet::Tiebreak:
        key = "WORLD_TIEBREAK_" + upper(value);
        break;
      case ChoiceSet::None:
        break;
    }
    choices.emplace_back(value, localised(key, value));
  }
  return choices;
}
bool hasRange(const World::FieldSpec& spec)
{
  return spec.minimum != 0 || spec.maximum != 0;
}
std::string numberText(const World::FieldSpec& spec, double number)
{
  return spec.type == World::FieldType::Integer
             ? std::to_string(static_cast<std::int64_t>(std::llround(number)))
             : RuntimeEditor::display(number);
}

/** Value an unset optional number takes when the user presses "Set". */
double defaultNumber(const World::FieldSpec& spec)
{
  if (spec.path == "founded")
  {
    return 1900.0;
  }
  return hasRange(spec) ? std::clamp(0.0, spec.minimum, spec.maximum) : 0.0;
}

/** JSON value of a field as the form's text (see EditorForm::Value). Absent
 * optional numbers are empty ("not set"). */
std::string toText(const World::FieldSpec& spec, const json* value)
{
  using World::FieldType;
  const bool present = value != nullptr && !value->is_null();
  switch (spec.type)
  {
    case FieldType::Integer:
    case FieldType::Number:
      if (present && value->is_number())
      {
        return numberText(spec, value->get<double>());
      }
      return spec.required ? numberText(spec, defaultNumber(spec))
                           : std::string();
    case FieldType::Reference:
      if (present && value->is_string())
      {
        return value->get<std::string>();  // A built-in language.
      }
      return present && value->is_number_integer() &&
                     value->get<std::int64_t>() != 0
                 ? std::to_string(value->get<std::int64_t>())
                 : std::string();
    case FieldType::List:
    {
      std::string names;
      for (const json& name :
           present && value->is_array() ? *value : json::array())
      {
        names += (names.empty() ? "" : ", ") + name.get<std::string>();
      }
      return names;
    }
    case FieldType::Stats:
      return present ? value->dump() : "{}";
    case FieldType::Boolean:
      return present && value->is_boolean() && !value->get<bool>() ? "0" : "1";
    case FieldType::Text:
    case FieldType::Colour:
    case FieldType::Choice:
      break;
  }
  return present && value->is_string() ? value->get<std::string>()
                                       : std::string();
}
/** Form text back to JSON; nullopt leaves the field alone (unparsable). */
std::optional<json> fromText(const World::FieldSpec& spec,
                             const std::string& text)
{
  using World::FieldType;
  switch (spec.type)
  {
    case FieldType::Integer:
    case FieldType::Reference:
    {
      if (text.empty() && (spec.type == FieldType::Reference || !spec.required))
      {
        return json();
      }
      std::int64_t number = 0;
      const auto parsed =
          std::from_chars(text.data(), text.data() + text.size(), number);
      if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size())
      {
        // A built-in language is stored by name.
        return spec.reference == Kind::Language ? std::optional<json>(text)
                                                : std::nullopt;
      }
      return number;
    }
    case FieldType::List:
    {
      json names = json::array();
      std::string current;
      const auto flush = [&]
      {
        const auto first = current.find_first_not_of(' ');
        if (first != std::string::npos)
        {
          names.push_back(
              current.substr(first, current.find_last_not_of(' ') - first + 1));
        }
        current.clear();
      };
      for (const char c : text)
      {
        if (c == ',')
        {
          flush();
        }
        else
        {
          current += c;
        }
      }
      flush();
      return names.empty() ? json() : names;
    }
    case FieldType::Number:
    {
      if (text.empty() && !spec.required)
      {
        return json();
      }
      double number = 0;
      const auto parsed =
          std::from_chars(text.data(), text.data() + text.size(), number);
      if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size())
      {
        return std::nullopt;
      }
      return number;
    }
    case FieldType::Stats:
    {
      json stats = json::parse(text, nullptr, false);
      return stats.is_object() ? std::optional<json>(stats) : std::nullopt;
    }
    case FieldType::Boolean:
      return json(text != "0");
    case FieldType::Text:
      return text.empty() && !spec.required ? json() : json(text);
    case FieldType::Colour:
    case FieldType::Choice:
      return text.empty() ? json() : json(text);
  }
  return std::nullopt;
}
EditorField describe(const World::FieldSpec& spec)
{
  using World::FieldType;
  EditorField field;
  field.column = spec.path;
  field.label = spec.label;
  field.section = spec.section;
  if (hasRange(spec) &&
      (spec.type == FieldType::Integer || spec.type == FieldType::Number))
  {
    field.minimum = spec.minimum;
    field.maximum = spec.maximum;
  }
  switch (spec.type)
  {
    case FieldType::Integer:
    case FieldType::Number:
      field.control = EditorField::Control::Number;
      field.optional = !spec.required;
      field.initial = numberText(spec, defaultNumber(spec));
      break;
    case FieldType::Text:
    case FieldType::List:
      break;
    case FieldType::Boolean:
      field.control = EditorField::Control::Toggle;
      field.hint = std::string(spec.label) + "_HINT";
      break;
    case FieldType::Colour:
      field.control = EditorField::Control::Colour;
      break;
    case FieldType::Choice:
    case FieldType::Reference:
      field.control = EditorField::Control::Choice;
      break;
    case FieldType::Stats:
      field.control = EditorField::Control::Attributes;
      break;
  }
  return field;
}
/** Details page: the manifest as form values. */
const std::array<const char*, 5> MANIFEST_FIELDS = {"name", "author", "version",
                                                    "description", "season"};
const char* manifestLabel(std::string_view column)
{
  if (column == "name")
  {
    return "WORLD_FIELD_NAME";
  }
  if (column == "author")
  {
    return "WORLD_FIELD_AUTHOR";
  }
  if (column == "version")
  {
    return "WORLD_FIELD_VERSION";
  }
  if (column == "description")
  {
    return "WORLD_FIELD_DESCRIPTION";
  }
  return "WORLD_FIELD_SEASON";
}
std::string manifestText(const World::Manifest& manifest,
                         std::string_view column)
{
  if (column == "name")
  {
    return manifest.name;
  }
  if (column == "author")
  {
    return manifest.author;
  }
  if (column == "version")
  {
    return manifest.version;
  }
  if (column == "description")
  {
    return manifest.description;
  }
  return std::to_string(manifest.season.value_or(0));
}
void setManifestText(World::Manifest& manifest, std::string_view column,
                     const std::string& text)
{
  if (column == "name")
  {
    manifest.name = text;
  }
  else if (column == "author")
  {
    manifest.author = text;
  }
  else if (column == "version")
  {
    manifest.version = text;
  }
  else if (column == "description")
  {
    manifest.description = text;
  }
  else
  {
    int year = 0;
    std::from_chars(text.data(), text.data() + text.size(), year);
    manifest.season = year > 0 ? std::optional<int>(year) : std::nullopt;
  }
}
}  // namespace

bool State::applyForm()
{
  bool any = false;
  const auto k = kind();
  if (page == Page::Details)
  {
    World::Manifest manifest = doc.manifest();
    for (std::size_t i = 0; i < values.size(); ++i)
    {
      if (values[i].text == original[i])
      {
        continue;
      }
      snapshot("manifest:" + values[i].name);
      setManifestText(manifest, values[i].name, values[i].text);
      original[i] = values[i].text;
      any = true;
    }
    if (any)
    {
      doc.setManifest(manifest);
    }
  }
  else if (json* target = k ? entity(*k, selected) : nullptr)
  {
    const auto& specs = World::fields(*k);
    for (std::size_t i = 0; i < values.size(); ++i)
    {
      if (values[i].text == original[i])
      {
        continue;
      }
      const auto parsed = fromText(specs.at(i), values[i].text);
      if (!parsed)
      {
        continue;
      }
      snapshot(std::to_string(static_cast<int>(*k)) + ":" +
               std::to_string(selected) + ":" + std::string(specs.at(i).path));
      World::set(*target, specs.at(i).path, *parsed);
      if (!specs.at(i).alias.empty())
      {
        World::set(*target, specs.at(i).alias, json());
      }
      original[i] = values[i].text;
      any = true;
    }
  }
  if (any)
  {
    changed();
  }
  return any;
}

void State::buildForm()
{
  form_stale = false;
  cup_stale = true;
  values.clear();
  descriptors.clear();
  original.clear();
  if (page == Page::Details)
  {
    const World::Manifest manifest = doc.manifest();
    for (const char* column : MANIFEST_FIELDS)
    {
      const bool season = std::string_view(column) == "season";
      EditorField field;
      field.column = column;
      field.label = manifestLabel(column);
      field.section = "WORLD_SECTION_DETAILS";
      if (season)
      {
        field.control = EditorField::Control::Number;
        field.minimum = 0;
        field.maximum = 2200;
        field.hint = "WORLD_HINT_SEASON";
      }
      descriptors.push_back(std::move(field));
      values.push_back({.name = column,
                        .text = manifestText(manifest, column),
                        .type = season ? SQLITE_INTEGER : SQLITE_TEXT});
    }
  }
  else if (const auto k = kind(); k && entity(*k, selected) != nullptr)
  {
    const json& source = *entity(*k, selected);
    for (const World::FieldSpec& spec : World::fields(*k))
    {
      EditorField field = describe(spec);
      if (spec.type == World::FieldType::Reference)
      {
        field.choices = referenceChoices(doc, *spec.reference, !spec.required);
      }
      else if (spec.type == World::FieldType::Choice)
      {
        field.choices = listChoices(doc, spec);
      }
      field.translate_choices = false;
      values.push_back(
          {.name = std::string(spec.path),
           .text = toText(spec, World::read(source, spec)),
           .type = spec.type == World::FieldType::Integer  ? SQLITE_INTEGER
                   : spec.type == World::FieldType::Number ? SQLITE_FLOAT
                                                           : SQLITE_TEXT});
      descriptors.push_back(std::move(field));
    }
  }
  for (const auto& value : values)
  {
    original.push_back(value.text);
  }
}

void State::renderForm(float width, float height)
{
  // The selection may have changed earlier in this frame.
  if (form_stale)
  {
    buildForm();
  }
  const std::string name = selectedName();
  UI::beginCard("##world_form",
                name.empty() ? LOC("WORLD_FORM_TITLE") : name.c_str(),
                ImVec2(width, height));
  if (selected < 0 || values.empty())
  {
    UI::emptyState(LOC("WORLD_SELECT_TITLE"), LOC("WORLD_SELECT_HINT"));
  }
  else if (page == Page::Confederation)
  {
    renderConfederation();
  }
  else if (EditorForm::render(values, descriptors, true, "##world_sections"))
  {
    applyForm();
  }
  UI::endCard();
}

void State::renderDetails(float width, float height)
{
  const float left = width * 0.6F;
  UI::beginCard("##world_details", LOC("WORLD_NAV_DETAILS"),
                ImVec2(left, height));
  if (EditorForm::render(values, descriptors, true, "##world_details_tabs"))
  {
    applyForm();
  }
  UI::endCard();
  ImGui::SameLine();
  UI::beginCard("##world_contents", LOC("WORLD_CONTENTS"),
                ImVec2(width - left - ImGui::GetStyle().ItemSpacing.x, height),
                true);
  for (const Kind kind : World::KINDS)
  {
    UI::summaryRow(kindName(kind).c_str(),
                   std::to_string(doc.count(kind)).c_str());
  }
  UI::summaryRow(LOC("WORLD_FILE"), file.empty() ? LOC("WORLD_NOT_SAVED_YET")
                                                 : stem(file).c_str());
  UI::endCard();
}

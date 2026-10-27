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
#include "gui/scenes/world_editor_state.h"
#include "model/continental.h"

using json = nlohmann::json;
using World::Kind;
using State = WorldEditorScene::State;
using namespace WorldEditorUi;

namespace
{
constexpr const char* PLACES = "places";
constexpr const char* WEEKDAYS = "weekday_offsets";
constexpr const char* FIRST_DAY = "weekday_offsets.0";
constexpr const char* SECOND_DAY = "weekday_offsets.1";
/** Unset route: the game's own distance between confederations. */
constexpr int DEFAULT_KM = 24'000;
constexpr int DEFAULT_HOURS = 12;

double numberOf(const json* value, double fallback)
{
  return value != nullptr && value->is_number() ? value->get<double>()
                                                : fallback;
}

std::string numberText(const World::FieldSpec& spec, double number)
{
  return spec.type == World::FieldType::Integer
             ? std::to_string(static_cast<std::int64_t>(std::llround(number)))
             : RuntimeEditor::display(number);
}

std::optional<std::int64_t> wholeNumber(std::string_view text)
{
  std::int64_t number = 0;
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), number);
  if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size())
  {
    return std::nullopt;
  }
  return number;
}

/** "4, 4, 3" as a list of numbers; nullopt when an entry is not a number. */
std::optional<json> numberList(const std::string& text)
{
  json list = json::array();
  std::size_t start = 0;
  while (start <= text.size())
  {
    auto end = text.find(',', start);
    end = end == std::string::npos ? text.size() : end;
    std::string part = text.substr(start, end - start);
    std::erase(part, ' ');
    if (!part.empty())
    {
      const auto number = wholeNumber(part);
      if (!number)
      {
        return std::nullopt;
      }
      list.push_back(*number);
    }
    start = end + 1;
  }
  return list;
}

EditorField cupField(std::string column, const std::string& label,
                     const std::string& section)
{
  EditorField field;
  field.column = std::move(column);
  field.label = label;
  field.section = section;
  field.hint = label + "_HINT";
  return field;
}
}  // namespace

json* State::confederationList(const char* key, bool create)
{
  json* confederation = entity(Kind::Confederation, selected);
  if (confederation == nullptr)
  {
    return nullptr;
  }
  if (create && !(*confederation)[key].is_array())
  {
    (*confederation)[key] = json::array();
  }
  const auto found = confederation->find(key);
  return found != confederation->end() && found->is_array() ? &*found : nullptr;
}

std::string State::cupName(std::size_t index)
{
  const json* list = confederationList("cups");
  const json* name = list != nullptr && index < list->size()
                         ? World::get((*list)[index], "name")
                         : nullptr;
  return name != nullptr && name->is_string() &&
                 !name->get<std::string>().empty()
             ? name->get<std::string>()
             : format("WORLD_CUP_DEFAULT_NAME", {selectedName()});
}

void State::selectCup(int index)
{
  const json* list = confederationList("cups");
  cup = list != nullptr && index >= 0 &&
                static_cast<std::size_t>(index) < list->size()
            ? index
            : -1;
  cup_stale = true;
}

void State::addCup()
{
  json* confederation = entity(Kind::Confederation, selected);
  if (confederation == nullptr)
  {
    return;
  }
  snapshot();
  json* list = confederationList("cups", true);
  list->push_back({{"name", format("WORLD_CUP_NEW", {selectedName()})}});
  changed();
  selectCup(static_cast<int>(list->size()) - 1);
}

void State::removeCup()
{
  json* list = confederationList("cups");
  if (list == nullptr || cup < 0 ||
      static_cast<std::size_t>(cup) >= list->size())
  {
    return;
  }
  snapshot();
  list->erase(static_cast<std::size_t>(cup));
  changed();
  selectCup(std::min(cup, static_cast<int>(list->size()) - 1));
}

void State::moveCup(int step)
{
  json* list = confederationList("cups");
  const int target = cup + step;
  if (list == nullptr || cup < 0 || target < 0 ||
      static_cast<std::size_t>(target) >= list->size())
  {
    return;
  }
  snapshot();
  std::swap((*list)[static_cast<std::size_t>(cup)],
            (*list)[static_cast<std::size_t>(target)]);
  changed();
  selectCup(target);
}

void State::buildCup()
{
  cup_stale = false;
  cup_values.clear();
  cup_descriptors.clear();
  cup_original.clear();
  const json* list = confederationList("cups");
  if (list == nullptr || cup < 0 ||
      static_cast<std::size_t>(cup) >= list->size())
  {
    return;
  }
  const json& item = (*list)[static_cast<std::size_t>(cup)];
  const json preset = Continental::presetCupJson(
      static_cast<int>(numberOf(World::get(item, "tier"), 1)));
  const auto effective = [&](std::string_view path)
  {
    const json* own = World::get(item, path);
    return own != nullptr && own->is_number() ? own : World::get(preset, path);
  };
  const auto add = [&](EditorField field, std::string text, int type)
  {
    cup_values.push_back(
        {.name = field.column, .text = std::move(text), .type = type});
    cup_descriptors.push_back(std::move(field));
  };
  const auto spec = [&](const World::FieldSpec& s)
  {
    EditorField field = cupField(std::string(s.path), std::string(s.label),
                                 std::string(s.section));
    if (s.type == World::FieldType::Text)
    {
      add(std::move(field), cupName(static_cast<std::size_t>(cup)),
          SQLITE_TEXT);
      return;
    }
    field.control = EditorField::Control::Number;
    field.minimum = s.minimum;
    field.maximum = s.maximum;
    add(std::move(field), numberText(s, numberOf(effective(s.path), 0)),
        s.type == World::FieldType::Integer ? SQLITE_INTEGER : SQLITE_FLOAT);
  };
  const auto day = [&](const char* path, std::size_t slot, const char* label)
  {
    const json* own = World::get(item, WEEKDAYS);
    const json* days = own != nullptr && own->is_array() && own->size() == 2 &&
                               (*own)[slot].is_number()
                           ? own
                           : World::get(preset, WEEKDAYS);
    EditorField field = cupField(path, label, "WORLD_SECTION_CUP_DATES");
    field.control = EditorField::Control::Number;
    field.minimum = 0;
    field.maximum = 6;
    add(std::move(field),
        std::to_string(static_cast<int>(numberOf(&(*days)[slot], 0))),
        SQLITE_INTEGER);
  };
  for (const World::FieldSpec& s : World::cupFields())
  {
    if (s.path == "final_offset")
    {
      day(FIRST_DAY, 0, "WORLD_CUP_FIRST_DAY");
      day(SECOND_DAY, 1, "WORLD_CUP_SECOND_DAY");
    }
    spec(s);
    if (s.path == "matches")
    {
      const json* own = World::get(item, PLACES);
      const json& places = own != nullptr && own->is_array() && !own->empty()
                               ? *own
                               : *World::get(preset, PLACES);
      std::string text;
      for (const json& number : places)
      {
        text += (text.empty() ? "" : ", ") +
                std::to_string(static_cast<int>(numberOf(&number, 0)));
      }
      add(cupField(PLACES, "WORLD_CUP_PLACES", "WORLD_SECTION_GENERAL"), text,
          SQLITE_TEXT);
    }
  }
  for (const auto& value : cup_values)
  {
    cup_original.push_back(value.text);
  }
}

bool State::applyCup()
{
  json* list = confederationList("cups");
  if (list == nullptr || cup < 0 ||
      static_cast<std::size_t>(cup) >= list->size())
  {
    return false;
  }
  json& item = (*list)[static_cast<std::size_t>(cup)];
  const auto text = [&](const char* path)
  {
    return std::ranges::find(cup_values, path, &EditorForm::Value::name)->text;
  };
  bool any = false;
  for (std::size_t i = 0; i < cup_values.size(); ++i)
  {
    const std::string& path = cup_values[i].name;
    const std::string& value = cup_values[i].text;
    if (value == cup_original[i])
    {
      continue;
    }
    std::optional<json> parsed;
    std::string target = path;
    if (path == PLACES)
    {
      parsed = numberList(value);
      if (parsed && parsed->empty())
      {
        parsed = json();
      }
    }
    else if (path == FIRST_DAY || path == SECOND_DAY)
    {
      const auto first = wholeNumber(text(FIRST_DAY));
      const auto second = wholeNumber(text(SECOND_DAY));
      parsed = first && second
                   ? std::optional<json>(json::array({*first, *second}))
                   : std::nullopt;
      target = WEEKDAYS;
    }
    else if (const auto spec = std::ranges::find(World::cupFields(), path,
                                                 &World::FieldSpec::path);
             spec->type == World::FieldType::Text)
    {
      parsed = value.empty() ? json() : json(value);
    }
    else if (spec->type == World::FieldType::Integer)
    {
      const auto number = wholeNumber(value);
      parsed = number ? std::optional<json>(*number) : std::nullopt;
    }
    else
    {
      double number = 0;
      const auto done =
          std::from_chars(value.data(), value.data() + value.size(), number);
      parsed = done.ec == std::errc() && done.ptr == value.data() + value.size()
                   ? std::optional<json>(number)
                   : std::nullopt;
    }
    if (!parsed)
    {
      continue;
    }
    snapshot("cup:" + std::to_string(selected) + ":" + std::to_string(cup) +
             ":" + target);
    World::set(item, target, *parsed);
    cup_original[i] = value;
    // A tier changes what every field left out stands for.
    cup_stale = cup_stale || path == "tier";
    any = true;
  }
  if (any)
  {
    changed();
  }
  return any;
}

void State::addRoute()
{
  json* confederation = entity(Kind::Confederation, selected);
  if (confederation == nullptr)
  {
    return;
  }
  std::string to;
  for (const json& other : doc.section(Kind::Confederation))
  {
    if (World::entityId(other) != selected)
    {
      to = World::entityName(Kind::Confederation, other);
      break;
    }
  }
  snapshot();
  confederationList("routes", true)
      ->push_back({{"to", to}, {"km", DEFAULT_KM}, {"hours", DEFAULT_HOURS}});
  changed();
}

void State::removeRoute(std::size_t index)
{
  json* list = confederationList("routes");
  if (list == nullptr || index >= list->size())
  {
    return;
  }
  snapshot();
  list->erase(index);
  changed();
}

void State::setRoute(std::size_t index, const char* key, json value)
{
  json* list = confederationList("routes");
  if (list == nullptr || index >= list->size() ||
      (*list)[index].value(key, json()) == value)
  {
    return;
  }
  snapshot("route:" + std::to_string(selected) + ":" + std::to_string(index) +
           ":" + key);
  (*list)[index][key] = std::move(value);
  changed();
}

void State::renderCups()
{
  if (cup_stale)
  {
    buildCup();
  }
  muted(confederationList("cups") != nullptr ? LOC("WORLD_CUPS_HINT")
                                             : LOC("WORLD_CUPS_IMPLICIT"));
  const json* list = confederationList("cups");
  const std::size_t count = list != nullptr ? list->size() : 0;
  if (ImGui::BeginChild("##world_cups", ImVec2(0, scaled(110.0F)),
                        ImGuiChildFlags_Borders))
  {
    for (std::size_t i = 0; i < count; ++i)
    {
      const std::string label = cupName(i) + "###cup" + std::to_string(i);
      if (ImGui::Selectable(label.c_str(), static_cast<int>(i) == cup))
      {
        selectCup(static_cast<int>(i));
      }
    }
  }
  ImGui::EndChild();
  constexpr auto COMPACT = UI::ButtonSize::COMPACT;
  bool first = true;
  const bool chosen = cup >= 0 && static_cast<std::size_t>(cup) < count;
  if (flowButton(LOC("WORLD_BTN_ADD"), first, true, false, COMPACT))
  {
    addCup();
  }
  if (flowButton(LOC("WORLD_BTN_DELETE"), first, chosen, false, COMPACT))
  {
    removeCup();
  }
  if (flowButton(LOC("WORLD_BTN_UP"), first, chosen && cup > 0, false, COMPACT))
  {
    moveCup(-1);
  }
  if (flowButton(LOC("WORLD_BTN_DOWN"), first,
                 chosen && static_cast<std::size_t>(cup) + 1 < count, false,
                 COMPACT))
  {
    moveCup(1);
  }
  ImGui::Separator();
  if (!cup_values.empty() &&
      EditorForm::render(cup_values, cup_descriptors, true, "##world_cup_tabs"))
  {
    applyCup();
  }
}

void State::renderRoutes()
{
  muted(LOC("WORLD_ROUTES_HINT"));
  std::vector<std::string> others;
  for (const json& other : doc.section(Kind::Confederation))
  {
    if (World::entityId(other) != selected)
    {
      others.push_back(World::entityName(Kind::Confederation, other));
    }
  }
  const json* list = confederationList("routes");
  const std::size_t count = list != nullptr ? list->size() : 0;
  if (count != 0 && ImGui::BeginTable("##world_routes", 4,
                                      ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_SizingStretchProp))
  {
    ImGui::TableSetupColumn(LOC("WORLD_ROUTE_TO"));
    ImGui::TableSetupColumn(LOC("WORLD_ROUTE_KM"));
    ImGui::TableSetupColumn(LOC("WORLD_ROUTE_HOURS"));
    ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (std::size_t i = 0; i < count; ++i)
    {
      const json route = (*list)[i];
      const std::string to = route.contains("to") && route["to"].is_string()
                                 ? route["to"].get<std::string>()
                                 : std::string();
      ImGui::PushID(static_cast<int>(i));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##to", to.c_str()))
      {
        std::vector<std::string> choices = others;
        if (std::ranges::find(choices, to) == choices.end())
        {
          choices.push_back(to);  // A name the world does not have (yet).
        }
        for (const std::string& choice : choices)
        {
          if (ImGui::Selectable(choice.c_str(), choice == to))
          {
            setRoute(i, "to", choice);
          }
        }
        ImGui::EndCombo();
      }
      const auto number = [&](const char* key, int maximum)
      {
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1);
        int value = static_cast<int>(numberOf(World::get(route, key), 0));
        if (ImGui::InputInt((std::string("##") + key).c_str(), &value, 0, 0))
        {
          setRoute(i, key, std::clamp(value, 0, maximum));
        }
      };
      number("km", 65'535);
      number("hours", 255);
      ImGui::TableNextColumn();
      if (UI::secondaryButton(LOC("WORLD_BTN_DELETE"), {},
                              UI::ButtonSize::COMPACT))
      {
        removeRoute(i);
      }
      ImGui::PopID();
      if (list = confederationList("routes");
          list == nullptr || i >= list->size())
      {
        break;  // The row was removed.
      }
    }
    ImGui::EndTable();
  }
  bool first = true;
  if (flowButton(LOC("WORLD_BTN_ADD"), first, !others.empty(), false,
                 UI::ButtonSize::COMPACT))
  {
    addRoute();
  }
}

void State::renderConfederation()
{
  if (!ImGui::BeginTabBar("##world_confederation"))
  {
    return;
  }
  if (ImGui::BeginTabItem(LOC("WORLD_SECTION_GENERAL")))
  {
    if (EditorForm::render(values, descriptors, true, "##world_sections"))
    {
      applyForm();
    }
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem(LOC("WORLD_TAB_CUPS")))
  {
    renderCups();
    ImGui::EndTabItem();
  }
  if (ImGui::BeginTabItem(LOC("WORLD_TAB_ROUTES")))
  {
    renderRoutes();
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
}

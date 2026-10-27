// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include "database/runtime_editor.h"
#include "database/world/world_edit.h"
#include "database/world/world_schema.h"
#include "global/language_manager.h"
#include "gui/scenes/world_editor_state.h"
#include "gui/widgets/theme.h"

using json = nlohmann::json;
using World::Kind;
using State = WorldEditorScene::State;
using Page = State::Page;
using namespace WorldEditorUi;

namespace
{
constexpr int SEARCH_CHARS = 128;
}  // namespace

void State::add()
{
  const auto k = kind();
  if (!k)
  {
    return;
  }
  std::optional<std::int64_t> parent;
  if (parent_filter > 0 && !World::parentKey(*k).empty())
  {
    parent = parent_filter;
  }
  snapshot();
  const std::int64_t key = World::create(doc, *k, parent);
  if (key < 0)
  {
    undo.pop_back();
    notify(LOC(*k == Kind::Travel ? "WORLD_ERR_NEED_CITIES"
                                  : "WORLD_ERR_NO_FREE_ID"),
           true);
    return;
  }
  search.clear();
  changed();
  select(*k, key);
}

void State::duplicateSelected()
{
  const auto k = kind();
  if (!k || selected < 0)
  {
    return;
  }
  snapshot();
  const std::int64_t key = World::duplicate(doc, *k, selected);
  if (key < 0)
  {
    undo.pop_back();
    notify(LOC("WORLD_ERR_NO_FREE_ID"), true);
    return;
  }
  changed();
  select(*k, key);
}

void State::removeSelected()
{
  const auto k = kind();
  if (!k || selected < 0)
  {
    return;
  }
  snapshot();
  World::removeEntity(doc, *k, selected);
  const std::int64_t removed = selected;
  std::erase_if(conflicts,
                [&](const World::Conflict& conflict)
                {
                  return conflict.kind == *k &&
                         (*k == Kind::Travel || conflict.base_id == removed);
                });
  std::erase_if(duplicates,
                [&](const World::PossibleDuplicate& duplicate)
                {
                  return duplicate.kind == *k &&
                         (duplicate.base_id == removed ||
                          duplicate.incoming_id == removed);
                });
  selected = -1;
  settleConflicts();
}

void State::rebuildRows()
{
  rows_dirty = false;
  rows.clear();
  parent_options.clear();
  counts.clear();
  const auto k = kind();
  if (!k)
  {
    return;
  }
  const std::string needle = World::normalise(search);
  const std::string_view parent_key = World::parentKey(*k);
  const json& section = doc.section(*k);
  for (std::size_t i = 0; i < section.size(); ++i)
  {
    const json& item = section[i];
    if (parent_filter >= 0 && !parent_key.empty() &&
        integer(item, std::string(parent_key).c_str()) != parent_filter)
    {
      continue;
    }
    if (!needle.empty() &&
        World::normalise(World::entityName(*k, item)).find(needle) ==
            std::string::npos)
    {
      continue;
    }
    rows.push_back(static_cast<std::uint32_t>(i));
  }
  if (const auto parent = World::parentKind(*k))
  {
    for (const json& item : doc.section(*parent))
    {
      parent_options.emplace_back(World::entityId(item),
                                  World::entityName(*parent, item));
    }
    std::ranges::sort(
        parent_options, [](const auto& a, const auto& b)
        { return World::normalise(a.second) < World::normalise(b.second); });
  }
  const bool leagues = *k == Kind::League;
  if (leagues || *k == Kind::Club)
  {
    for (const json& item : doc.section(leagues ? Kind::Club : Kind::Player))
    {
      ++counts[integer(item, leagues ? "league_id" : "team_id")];
    }
  }
  else if (*k == Kind::Language || *k == Kind::Region ||
           *k == Kind::Confederation)
  {
    const bool regions = *k == Kind::Confederation;
    for (const json& item : doc.section(regions ? Kind::Region : Kind::Country))
    {
      ++counts[integer(item, regions                ? "confederation_id"
                             : *k == Kind::Language ? "language"
                                                    : "region_id")];
    }
  }
}

namespace
{
bool inputText(const char* id, const char* hint, std::string& text)
{
  std::array<char, SEARCH_CHARS> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "%s", text.c_str());
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (!ImGui::InputTextWithHint(id, hint, buffer.data(), buffer.size()))
  {
    return false;
  }
  text = buffer.data();
  return true;
}
struct ColumnDef
{
  const char* key;
  float width;
  int priority;
};
std::vector<ColumnDef> columnDefs(Kind kind)
{
  switch (kind)
  {
    case Kind::Language:
      return {{"WORLD_FIELD_NAME", 0, 0}, {"WORLD_COL_COUNTRIES", 100, 1}};
    case Kind::Confederation:
      return {{"WORLD_FIELD_NAME", 0, 0}, {"WORLD_COL_REGIONS", 100, 1}};
    case Kind::Region:
      return {{"WORLD_FIELD_NAME", 0, 0},
              {"WORLD_FIELD_CONFEDERATION", 150, 2},
              {"WORLD_COL_COUNTRIES", 100, 1}};
    case Kind::Country:
      return {{"WORLD_FIELD_NAME", 0, 0},
              {"WORLD_FIELD_DEMONYM", 130, 1},
              {"WORLD_COL_LEVEL", 80, 2},
              {"WORLD_FIELD_LANGUAGE", 120, 3},
              {"WORLD_FIELD_REGION", 120, 4}};
    case Kind::City:
      return {{"WORLD_FIELD_NAME", 0, 0},
              {"WORLD_FIELD_COUNTRY", 150, 1},
              {"WORLD_FIELD_POPULATION", 110, 2}};
    case Kind::Travel:
      return {{"WORLD_FIELD_FROM", 0, 0},
              {"WORLD_FIELD_TO", 0, 0},
              {"WORLD_COL_MINUTES", 90, 0}};
    case Kind::League:
      return {{"WORLD_FIELD_NAME", 0, 0},
              {"WORLD_FIELD_COUNTRY", 150, 1},
              {"WORLD_FIELD_PARENT_LEAGUE", 150, 2},
              {"WORLD_COL_CLUBS", 70, 3}};
    case Kind::Club:
      return {{"WORLD_FIELD_NAME", 0, 0},
              {"WORLD_FIELD_LEAGUE", 160, 1},
              {"WORLD_FIELD_CITY", 130, 2},
              {"WORLD_COL_PLAYERS", 80, 3}};
    case Kind::Player:
      return {{"WORLD_FIELD_NAME", 0, 0},
              {"WORLD_COL_POSITION", 60, 1},
              {"WORLD_COL_OVERALL", 60, 2},
              {"WORLD_COL_AGE", 50, 3},
              {"WORLD_FIELD_CLUB", 160, 4}};
    case Kind::COUNT:
      break;
  }
  return {};
}
std::string referenceName(const World::Document& doc, Kind kind,
                          const json& item, const char* key)
{
  const std::int64_t id = integer(item, key);
  if (id == 0)
  {
    return {};
  }
  const json* found = doc.find(kind, id);
  return found != nullptr ? World::entityName(kind, *found)
                          : "#" + std::to_string(id);
}
std::string number(const json& item, const char* key)
{
  const auto found = item.find(key);
  return found != item.end() && found->is_number()
             ? RuntimeEditor::display(found->get<double>())
             : std::string();
}
std::string cellText(const State& state, Kind kind, const json& item,
                     std::size_t column)
{
  const World::Document& doc = state.doc;
  const auto count = [&](std::int64_t id)
  {
    const auto found = state.counts.find(id);
    return std::to_string(found == state.counts.end() ? 0U : found->second);
  };
  switch (kind)
  {
    case Kind::Language:
    case Kind::Confederation:
      return column == 1 ? count(World::entityId(item))
                         : World::entityName(kind, item);
    case Kind::Region:
      return column == 1   ? count(World::entityId(item))
             : column == 2 ? referenceName(doc, Kind::Confederation, item,
                                           "confederation_id")
                           : World::entityName(kind, item);
    case Kind::Country:
    {
      if (column == 1)
      {
        const std::string nation = World::demonym(item);
        const std::string key = "NATION_" + nation;
        return nation.empty() || key == LOC(key.c_str()) ? nation
                                                         : LOC(key.c_str());
      }
      if (column == 3)
      {
        const auto spoken = item.find("language");
        return spoken != item.end() && spoken->is_string()
                   ? spoken->get<std::string>()
                   : referenceName(doc, Kind::Language, item, "language");
      }
      if (column == 4)
      {
        const std::string region =
            referenceName(doc, Kind::Region, item, "region_id");
        return region.empty() ? item.value("region", std::string()) : region;
      }
      return column == 2 ? number(item, "reputation")
                         : World::entityName(kind, item);
    }
    case Kind::City:
      return column == 1 ? referenceName(doc, Kind::Country, item, "country_id")
             : column == 2 ? number(item, "population")
                           : World::entityName(kind, item);
    case Kind::Travel:
      return column == 0   ? referenceName(doc, Kind::City, item, "from")
             : column == 1 ? referenceName(doc, Kind::City, item, "to")
                           : number(item, "minutes");
    case Kind::League:
      return column == 1 ? referenceName(doc, Kind::Country, item, "country_id")
             : column == 2
                 ? referenceName(doc, Kind::League, item, "parent_league")
             : column == 3 ? count(World::entityId(item))
                           : World::entityName(kind, item);
    case Kind::Club:
      return column == 1   ? referenceName(doc, Kind::League, item, "league_id")
             : column == 2 ? referenceName(doc, Kind::City, item, "city_id")
             : column == 3 ? count(World::entityId(item))
                           : World::entityName(kind, item);
    case Kind::Player:
    {
      if (column == 1)
      {
        return item.value("role", std::string());
      }
      if (column == 3)
      {
        return number(item, "age");
      }
      if (column == 4)
      {
        const std::string club =
            referenceName(doc, Kind::Club, item, "team_id");
        return club.empty() ? LOC("WORLD_FREE_AGENT") : club;
      }
      if (column == 2)
      {
        double sum = 0;
        std::size_t n = 0;
        if (const auto stats = item.find("stats");
            stats != item.end() && stats->is_object())
        {
          for (const auto& [name, value] : stats->items())
          {
            if (value.is_number())
            {
              sum += value.get<double>();
              ++n;
            }
          }
        }
        return n == 0 ? std::string()
                      : std::to_string(static_cast<int>(
                            std::lround(sum / static_cast<double>(n))));
      }
      return World::entityName(kind, item);
    }
    case Kind::COUNT:
      break;
  }
  return {};
}
}  // namespace

void State::renderList(float width, float height)
{
  const Kind k = *kind();
  const std::string title = kindName(k);
  UI::beginCard("##world_list", title.c_str(), ImVec2(width, height));
  if (inputText("##world_search", LOC("WORLD_SEARCH_HINT"), search))
  {
    rows_dirty = true;
  }
  bool first = true;
  if (World::parentKind(k))
  {
    const char* none = k == Kind::Club     ? "WORLD_FILTER_UNASSIGNED"
                       : k == Kind::Player ? "WORLD_FILTER_FREE_AGENTS"
                                           : "WORLD_FILTER_NO_COUNTRY";
    std::string label = parent_filter < 0 ? LOC("WORLD_FILTER_ALL") : LOC(none);
    if (parent_filter > 0)
    {
      for (const auto& [id, name] : parent_options)
      {
        if (id == parent_filter)
        {
          label = name;
        }
      }
    }
    ImGui::SetNextItemWidth(
        std::min(ImGui::GetContentRegionAvail().x * 0.5F, scaled(240.0F)));
    if (ImGui::BeginCombo("##world_filter", label.c_str()))
    {
      const auto choose = [&](std::int64_t value, const char* text)
      {
        if (ImGui::Selectable(text, parent_filter == value) &&
            parent_filter != value)
        {
          parent_filter = value;
          rows_dirty = true;
        }
      };
      choose(-1, LOC("WORLD_FILTER_ALL"));
      choose(0, LOC(none));
      for (std::size_t i = 0; i < parent_options.size(); ++i)
      {
        ImGui::PushID(static_cast<int>(i));
        choose(parent_options[i].first, parent_options[i].second.c_str());
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
    first = false;
  }
  constexpr auto COMPACT = UI::ButtonSize::COMPACT;
  if (flowButton(LOC("WORLD_BTN_ADD"), first, true, false, COMPACT))
  {
    add();
  }
  if (flowButton(LOC("WORLD_BTN_DUPLICATE"), first, selected >= 0, false,
                 COMPACT))
  {
    duplicateSelected();
  }
  if (flowButton(LOC("WORLD_BTN_DELETE"), first, selected >= 0, false, COMPACT))
  {
    const World::RemoveImpact effect = World::impact(doc, k, selected);
    popup_text.clear();
    const auto line = [&](std::size_t n, const char* key)
    {
      if (n != 0)
      {
        popup_text += format(key, {std::to_string(n)}) + "\n";
      }
    };
    line(effect.players, "WORLD_IMPACT_PLAYERS");
    line(effect.clubs, "WORLD_IMPACT_CLUBS");
    line(effect.leagues, "WORLD_IMPACT_LEAGUES");
    line(effect.cities, "WORLD_IMPACT_CITIES");
    line(effect.countries, "WORLD_IMPACT_COUNTRIES");
    line(effect.travel, "WORLD_IMPACT_TRAVEL");
    popup_text += LOC("WORLD_DELETE_BODY");
    open_popup = Popup::Delete;
  }
  ImGui::Dummy(ImVec2(0.0F, scaled(Theme::Space::XS)));
  // A page switched earlier in this frame: rows must match the shown kind.
  if (rows_dirty)
  {
    rebuildRows();
  }
  if (rows.empty())
  {
    if (doc.count(k) == 0)
    {
      static const std::array<const char*, 9> HINTS = {
          "WORLD_EMPTY_LANGUAGE", "WORLD_EMPTY_CONFEDERATION",
          "WORLD_EMPTY_REGION",   "WORLD_EMPTY_COUNTRY",
          "WORLD_EMPTY_CITY",     "WORLD_EMPTY_TRAVEL",
          "WORLD_EMPTY_LEAGUE",   "WORLD_EMPTY_CLUB",
          "WORLD_EMPTY_PLAYER"};
      UI::emptyState(LOC("WORLD_EMPTY_TITLE"),
                     LOC(HINTS.at(static_cast<std::size_t>(k))));
    }
    else
    {
      UI::emptyState(LOC("WORLD_NO_MATCHES"), LOC("WORLD_NO_MATCHES_HINT"));
    }
    UI::endCard();
    return;
  }
  const auto defs = columnDefs(k);
  std::vector<UI::Column> columns;
  for (const ColumnDef& def : defs)
  {
    columns.push_back({LOC(def.key), def.width, def.priority});
  }
  const UI::ColumnMask mask =
      UI::fitColumns(columns, ImGui::GetContentRegionAvail().x, 120.0F);
  constexpr auto FLAGS = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                         ImGuiTableFlags_BordersInnerH;
  if (UI::beginResponsiveTable("##world_rows", columns, mask, FLAGS))
  {
    const json& section = doc.section(k);
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(rows.size()));
    int target = -1;
    if (scroll_to_selected && selected >= 0)
    {
      for (std::size_t i = 0; i < rows.size() && rows[i] < section.size(); ++i)
      {
        const json& item = section[rows[i]];
        if ((k == Kind::Travel ? static_cast<std::int64_t>(rows[i])
                               : World::entityId(item)) == selected)
        {
          target = static_cast<int>(i);
        }
      }
    }
    if (target >= 0)
    {
      clipper.IncludeItemByIndex(target);
    }
    while (clipper.Step())
    {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
      {
        const std::uint32_t position = rows[static_cast<std::size_t>(i)];
        if (position >= section.size())
        {
          continue;
        }
        const json& item = section[position];
        const std::int64_t key = k == Kind::Travel
                                     ? static_cast<std::int64_t>(position)
                                     : World::entityId(item);
        ImGui::PushID(i);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        std::string text = cellText(*this, k, item, 0);
        if (text.empty())
        {
          text = "-";
        }
        if (ImGui::Selectable(text.c_str(), key == selected,
                              ImGuiSelectableFlags_SpanAllColumns))
        {
          selected = key;
          form_stale = true;
        }
        if (i == target)
        {
          ImGui::SetScrollHereY(0.5F);
          scroll_to_selected = false;
        }
        for (std::size_t c = 1; c < defs.size(); ++c)
        {
          if (UI::cell(mask, static_cast<int>(c)))
          {
            ImGui::TextUnformatted(cellText(*this, k, item, c).c_str());
          }
        }
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
  scroll_to_selected = false;
  UI::endCard();
}

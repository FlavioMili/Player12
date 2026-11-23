// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <set>

#include "database/world/world_schema.h"
#include "global/language_manager.h"
#include "gui/scenes/world_editor_state.h"
#include "gui/widgets/theme.h"

using json = nlohmann::json;
using World::Kind;
using State = WorldEditorScene::State;
using Page = State::Page;
using namespace WorldEditorUi;

/** Keeps a valid selection and leaves the Conflicts page when nothing is left.
 */
void State::settleConflicts()
{
  take_incoming.clear();
  conflict_selected = std::clamp(
      conflict_selected, 0, std::max(0, static_cast<int>(openConflicts()) - 1));
  changed();
  form_stale = true;
  if (openConflicts() == 0 && page == Page::Conflicts)
  {
    setPage(Page::Details);
  }
}

bool State::combineFile(const std::filesystem::path& path)
{
  const World::LoadResult loaded = World::readFile(path);
  if (!loaded.document)
  {
    notify(LOC("WORLD_ERR_READ"), true);
    return false;
  }
  if (World::hasErrors(loaded.issues))
  {
    notify(LOC("WORLD_ERR_COMBINE_ERRORS"), true);
    return false;
  }
  return combineDocument(*loaded.document, World::pathToUtf8(path.stem()));
}

bool State::combineDocument(const World::Document& incoming,
                            const std::string& fallback_name)
{
  combined_name = incoming.manifest().name;
  if (combined_name.empty())
  {
    combined_name = fallback_name;
  }
  combined = World::combine(doc, incoming);
  open_popup = Popup::Summary;
  return true;
}

void State::applyCombined()
{
  if (!combined)
  {
    return;
  }
  snapshot();
  doc = std::move(combined->document);
  conflicts = std::move(combined->conflicts);
  duplicates = std::move(combined->possible_duplicates);
  auto_merges = std::move(combined->auto_merges);
  additions = std::move(combined->additions);
  review_selected = -1;
  combined.reset();
  conflict_selected = 0;
  take_incoming.clear();
  changed();
  form_stale = true;
  notify(format("WORLD_COMBINED", {combined_name}));
  if (openConflicts() != 0)
  {
    setPage(Page::Conflicts);
  }
  else if (!additions.empty() || !auto_merges.empty())
  {
    setPage(Page::Review);
  }
}

void State::resolveSelected(World::Choice choice)
{
  const auto index = static_cast<std::size_t>(conflict_selected);
  if (index >= conflicts.size())
  {
    return;
  }
  snapshot();
  World::resolve(doc, conflicts[index], choice);
  conflicts.erase(conflicts.begin() + static_cast<std::ptrdiff_t>(index));
  settleConflicts();
}

void State::resolveChosenFields()
{
  const auto index = static_cast<std::size_t>(conflict_selected);
  if (index >= conflicts.size())
  {
    return;
  }
  std::vector<std::string> paths;
  for (std::size_t i = 0;
       i < conflicts[index].fields.size() && i < take_incoming.size(); ++i)
  {
    if (take_incoming[i] != 0)
    {
      paths.push_back(conflicts[index].fields[i]);
    }
  }
  snapshot();
  World::resolveFields(doc, conflicts[index], paths);
  conflicts.erase(conflicts.begin() + static_cast<std::ptrdiff_t>(index));
  settleConflicts();
}

void State::resolveAllFrom(const std::string& source)
{
  snapshot();
  World::resolveAll(doc, conflicts, source);
  settleConflicts();
}

void State::mergeSelected(bool keep_base)
{
  const auto index =
      static_cast<std::size_t>(conflict_selected) - conflicts.size();
  if (conflict_selected < static_cast<int>(conflicts.size()) ||
      index >= duplicates.size())
  {
    return;
  }
  const World::PossibleDuplicate pair = duplicates[index];
  const std::int64_t keep = keep_base ? pair.base_id : pair.incoming_id;
  const std::int64_t drop = keep_base ? pair.incoming_id : pair.base_id;
  snapshot();
  World::mergeInto(doc, pair.kind, keep, drop);
  std::erase_if(duplicates,
                [&](const World::PossibleDuplicate& other)
                {
                  return other.kind == pair.kind &&
                         (other.base_id == drop || other.incoming_id == drop);
                });
  std::erase_if(conflicts, [&](const World::Conflict& other)
                { return other.kind == pair.kind && other.base_id == drop; });
  settleConflicts();
}

void State::keepSeparate()
{
  const auto index =
      static_cast<std::size_t>(conflict_selected) - conflicts.size();
  if (conflict_selected < static_cast<int>(conflicts.size()) ||
      index >= duplicates.size())
  {
    return;
  }
  snapshot();
  duplicates.erase(duplicates.begin() + static_cast<std::ptrdiff_t>(index));
  settleConflicts();
}

void State::keepAllAsIs()
{
  if (openConflicts() == 0)
  {
    return;
  }
  snapshot();
  conflicts.clear();
  duplicates.clear();
  settleConflicts();
}

void State::separateMerged(std::size_t index)
{
  if (index >= auto_merges.size())
  {
    return;
  }
  snapshot();
  const auto id = World::separate(doc, auto_merges[index]);
  if (!id)
  {
    undo.pop_back();
    notify(LOC("WORLD_ERR_NO_FREE_ID"), true);
    return;
  }
  auto_merges.erase(auto_merges.begin() + static_cast<std::ptrdiff_t>(index));
  review_selected = -1;
  changed();
  form_stale = true;
  notify(LOC("WORLD_KEPT_BOTH"));
}

bool State::decisionsPending()
{
  if (openConflicts() == 0)
  {
    return false;
  }
  setPage(Page::Conflicts);
  notify(format("WORLD_PENDING_DECISIONS", {std::to_string(openConflicts())}),
         true);
  return true;
}

std::vector<std::string> State::sources() const
{
  std::set<std::string> names;
  for (const World::Conflict& conflict : conflicts)
  {
    names.insert(conflict.base_source);
    names.insert(conflict.incoming_source);
  }
  return {names.begin(), names.end()};
}

namespace
{
std::string valueText(const World::Document& doc, Kind kind,
                      const std::string& path, const json* value)
{
  if (value == nullptr || value->is_null())
  {
    return "-";
  }
  if (const World::FieldSpec* spec = specOf(kind, path);
      spec != nullptr && spec->type == World::FieldType::Reference &&
      value->is_number_integer())
  {
    const json* target = doc.find(*spec->reference, value->get<std::int64_t>());
    return target != nullptr ? World::entityName(*spec->reference, *target)
                             : "#" + value->dump();
  }
  return value->is_string() ? value->get<std::string>() : value->dump();
}
/** Field | current | incoming rows; with @p choose a radio per side. */
void comparison(const State& state, Kind kind, const json* current,
                const json* incoming, const std::vector<std::string>& paths,
                std::vector<char>* choose, const std::string& current_title,
                const std::string& incoming_title)
{
  constexpr auto FLAGS = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                         ImGuiTableFlags_SizingStretchProp;
  if (!ImGui::BeginTable("##comparison", 3, FLAGS))
  {
    return;
  }
  ImGui::TableSetupColumn(LOC("WORLD_COL_FIELD"),
                          ImGuiTableColumnFlags_WidthStretch, 0.8F);
  ImGui::TableSetupColumn(current_title.c_str(),
                          ImGuiTableColumnFlags_WidthStretch, 1.0F);
  ImGui::TableSetupColumn(incoming_title.c_str(),
                          ImGuiTableColumnFlags_WidthStretch, 1.0F);
  UI::staticHeadersRow();
  for (std::size_t i = 0; i < paths.size(); ++i)
  {
    ImGui::PushID(static_cast<int>(i));
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    UI::textFitted(fieldLabel(kind, paths[i]), ImGui::GetContentRegionAvail().x,
                   Theme::palette().text);
    for (int side = 0; side < 2; ++side)
    {
      ImGui::TableNextColumn();
      const json* value = side == 0 ? current : incoming;
      const std::string text =
          valueText(state.doc, kind, paths[i],
                    value != nullptr ? World::get(*value, paths[i]) : nullptr);
      ImGui::PushID(side);
      if (choose != nullptr)
      {
        bool picked = (choose->at(i) != 0) == (side == 1);
        if (ImGui::RadioButton("##pick", picked))
        {
          choose->at(i) = side == 1;
        }
        ImGui::SameLine();
      }
      ImGui::AlignTextToFramePadding();
      UI::textFitted(text, ImGui::GetContentRegionAvail().x,
                     Theme::palette().text);
      ImGui::PopID();
    }
    ImGui::PopID();
  }
  ImGui::EndTable();
}
/** Localised description of a combine match basis ("name+country" ...). */
std::string basisText(const std::string& basis)
{
  std::string key = "WORLD_BASIS_";
  for (const char c : basis)
  {
    key += std::isalnum(static_cast<unsigned char>(c))
               ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
               : '_';
  }
  return format("WORLD_BASIS_LINE", {LOC(key.c_str())});
}
std::string conflictName(State& state, const World::Conflict& conflict)
{
  const json* base = state.entity(conflict.kind, conflict.base_id);
  return kindName(conflict.kind) + ": " +
         World::entityName(conflict.kind,
                           base != nullptr ? *base : conflict.incoming);
}
}  // namespace

void State::renderConflictDetail()
{
  const auto index = static_cast<std::size_t>(conflict_selected);
  const World::Conflict& conflict = conflicts[index];
  take_incoming.resize(conflict.fields.size(), 0);
  muted(format("WORLD_CONFLICT_HINT",
               {conflict.base_source, conflict.incoming_source}));
  if (!conflict.basis.empty())
  {
    muted(basisText(conflict.basis));
  }
  bool first = true;
  const bool any =
      std::ranges::any_of(take_incoming, [](char c) { return c != 0; });
  if (flowButton(LOC("WORLD_KEEP_CURRENT"), first))
  {
    resolveSelected(World::Choice::KeepBase);
  }
  else if (flowButton(LOC("WORLD_KEEP_INCOMING"), first))
  {
    resolveSelected(World::Choice::KeepIncoming);
  }
  else if (flowButton(LOC("WORLD_KEEP_BOTH"), first,
                      conflict.kind != Kind::Travel))
  {
    resolveSelected(World::Choice::KeepBoth);
  }
  else if (flowButton(LOC("WORLD_APPLY_FIELDS"), first, true, any))
  {
    resolveChosenFields();
  }
  else
  {
    comparison(*this, conflict.kind, entity(conflict.kind, conflict.base_id),
               &conflict.incoming, conflict.fields, &take_incoming,
               conflict.base_source, conflict.incoming_source);
  }
}

void State::renderDuplicateDetail()
{
  const auto index =
      static_cast<std::size_t>(conflict_selected) - conflicts.size();
  const World::PossibleDuplicate pair = duplicates[index];
  const json* base = entity(pair.kind, pair.base_id);
  const json* incoming = entity(pair.kind, pair.incoming_id);
  if (base == nullptr || incoming == nullptr)
  {
    return;
  }
  const std::string base_name = World::entityName(pair.kind, *base);
  const std::string incoming_name = World::entityName(pair.kind, *incoming);
  muted(LOC("WORLD_DUP_HINT"));
  if (!pair.basis.empty())
  {
    muted(basisText(pair.basis));
  }
  bool first = true;
  if (flowButton(
          (format("WORLD_MERGE_INTO", {base_name}) + "##merge_base").c_str(),
          first))
  {
    mergeSelected(true);
  }
  else if (flowButton((format("WORLD_MERGE_INTO", {incoming_name}) +
                       "##merge_incoming")
                          .c_str(),
                      first))
  {
    mergeSelected(false);
  }
  else if (flowButton(LOC("WORLD_KEEP_SEPARATE"), first, true, true))
  {
    keepSeparate();
  }
  else
  {
    comparison(*this, pair.kind, base, incoming,
               World::differingPaths(*base, *incoming), nullptr, base_name,
               incoming_name);
  }
}

void State::renderConflicts(float width, float height)
{
  const float left = std::max(scaled(300.0F), width * 0.36F);
  UI::beginCard("##world_conflict_list", LOC("WORLD_NAV_CONFLICTS"),
                ImVec2(left, height));
  if (!conflicts.empty())
  {
    const std::vector<std::string> names = sources();
    bulk_source =
        std::clamp(bulk_source, 0, static_cast<int>(names.size()) - 1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::BeginCombo(
            "##world_bulk_source",
            names.at(static_cast<std::size_t>(bulk_source)).c_str()))
    {
      for (std::size_t i = 0; i < names.size(); ++i)
      {
        if (ImGui::Selectable(names[i].c_str(),
                              static_cast<int>(i) == bulk_source))
        {
          bulk_source = static_cast<int>(i);
        }
      }
      ImGui::EndCombo();
    }
    if (UI::secondaryButton(LOC("WORLD_CONFLICTS_BULK"), ImVec2(-FLT_MIN, 0)))
    {
      resolveAllFrom(names.at(static_cast<std::size_t>(bulk_source)));
      UI::endCard();
      return;
    }
  }
  if (openConflicts() != 0 &&
      UI::secondaryButton(LOC("WORLD_KEEP_ALL"), ImVec2(-FLT_MIN, 0)))
  {
    keepAllAsIs();
    UI::endCard();
    return;
  }
  ImGui::Separator();
  const int total = static_cast<int>(openConflicts());
  ImGui::BeginChild("##world_conflict_rows", ImVec2(0, 0));
  ImGuiListClipper clipper;
  clipper.Begin(total);
  while (clipper.Step())
  {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
    {
      std::string label;
      if (i < static_cast<int>(conflicts.size()))
      {
        label = conflictName(*this, conflicts[static_cast<std::size_t>(i)]);
      }
      else
      {
        const auto& pair =
            duplicates[static_cast<std::size_t>(i) - conflicts.size()];
        const json* base = entity(pair.kind, pair.base_id);
        label = std::string(LOC("WORLD_DUP_TAG")) + ": " +
                (base != nullptr ? World::entityName(pair.kind, *base)
                                 : std::string());
      }
      ImGui::PushID(i);
      if (ImGui::Selectable(label.c_str(), i == conflict_selected))
      {
        conflict_selected = i;
        take_incoming.clear();
      }
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
  UI::endCard();
  ImGui::SameLine();
  UI::beginCard("##world_conflict_detail", nullptr,
                ImVec2(width - left - ImGui::GetStyle().ItemSpacing.x, height),
                true);
  if (conflict_selected < static_cast<int>(conflicts.size()))
  {
    renderConflictDetail();
  }
  else if (conflict_selected < total)
  {
    renderDuplicateDetail();
  }
  UI::endCard();
}

void State::renderSummary()
{
  if (!modal(format("WORLD_SUMMARY_TITLE", {combined_name})))
  {
    return;
  }
  if (!combined)
  {
    endModal(true);
    return;
  }
  for (const Kind kind : World::KINDS)
  {
    const auto slot = static_cast<std::size_t>(kind);
    if (combined->added[slot] + combined->merged[slot] != 0)
    {
      ImGui::TextUnformatted((kindName(kind) + ": " +
                              format("WORLD_SUMMARY_ADDED",
                                     {std::to_string(combined->added[slot])}) +
                              ", " +
                              format("WORLD_SUMMARY_MERGED",
                                     {std::to_string(combined->merged[slot])}))
                                 .c_str());
    }
  }
  const std::size_t found = combined->conflicts.size();
  const std::size_t similar = combined->possible_duplicates.size();
  ImGui::Spacing();
  // Every category is listed; each has its own page after Apply.
  ImGui::TextUnformatted(format("WORLD_SUMMARY_TOTAL_ADDED",
                                {std::to_string(combined->additions.size())})
                             .c_str());
  ImGui::TextUnformatted(format("WORLD_SUMMARY_TOTAL_MERGED",
                                {std::to_string(combined->auto_merges.size())})
                             .c_str());
  ImGui::TextUnformatted(
      format("WORLD_SUMMARY_CONFLICTS", {std::to_string(found)}).c_str());
  ImGui::TextUnformatted(
      format("WORLD_SUMMARY_DUPLICATES", {std::to_string(similar)}).c_str());
  if (found + similar == 0)
  {
    muted(LOC("WORLD_SUMMARY_CLEAN"));
  }
  else
  {
    muted(LOC("WORLD_SUMMARY_DECIDE"));
  }
  ImGui::Spacing();
  const bool apply = UI::primaryButton(LOC("WORLD_BTN_APPLY"));
  ImGui::SameLine();
  const bool cancel = cancelPressed();
  if (apply)
  {
    applyCombined();
  }
  else if (cancel)
  {
    combined.reset();
  }
  endModal(apply || cancel);
}

void State::renderReview(float width, float height)
{
  UI::beginCard("##world_review", LOC("WORLD_NAV_REVIEW"),
                ImVec2(width, height), true);
  const std::string added_tab =
      format("WORLD_REVIEW_ADDED_TAB", {std::to_string(additions.size())}) +
      "##review_added";
  const std::string merged_tab =
      format("WORLD_REVIEW_MERGED_TAB", {std::to_string(auto_merges.size())}) +
      "##review_merged";
  bool first = true;
  if (flowButton(added_tab.c_str(), first, true, review_tab == 0))
  {
    review_tab = 0;
    review_selected = -1;
  }
  if (flowButton(merged_tab.c_str(), first, true, review_tab == 1))
  {
    review_tab = 1;
    review_selected = -1;
  }
  const bool merged = review_tab == 1;
  muted(LOC(merged ? "WORLD_REVIEW_MERGED_HINT" : "WORLD_REVIEW_ADDED_HINT"));
  if (merged)
  {
    const bool ready = review_selected >= 0 &&
                       review_selected < static_cast<int>(auto_merges.size());
    if (flowButton(LOC("WORLD_KEEP_BOTH_DIFFERENT"), first, ready, ready))
    {
      separateMerged(static_cast<std::size_t>(review_selected));
    }
  }
  ImGui::Separator();
  const int total =
      static_cast<int>(merged ? auto_merges.size() : additions.size());
  ImGui::BeginChild("##world_review_rows", ImVec2(0, 0));
  ImGuiListClipper clipper;
  clipper.Begin(total);
  while (clipper.Step())
  {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
    {
      const auto row = static_cast<std::size_t>(i);
      std::string label;
      if (merged)
      {
        const World::AutoMerge& merge = auto_merges[row];
        const json* target = entity(merge.kind, merge.base_id);
        label = kindName(merge.kind) + ": " +
                World::entityName(merge.kind,
                                  target != nullptr ? *target : *merge.entity) +
                "  (" + basisText(merge.basis) + ")";
      }
      else
      {
        const World::Addition& added = additions[row];
        const json* target = entity(added.kind, added.new_id);
        label = kindName(added.kind) + ": " +
                (target != nullptr ? World::entityName(added.kind, *target)
                                   : std::string("-"));
        if (added.old_id >= 0)
        {
          label += "  (#" + std::to_string(added.old_id) + " -> #" +
                   std::to_string(added.new_id) + ")";
        }
      }
      ImGui::PushID(i);
      if (ImGui::Selectable(label.c_str(), i == review_selected))
      {
        review_selected = i;
      }
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
  UI::endCard();
}

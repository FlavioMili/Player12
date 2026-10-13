// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/runtime_editor_dialog.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <nlohmann/json.hpp>
#include <set>

#include "global/global.h"
#include "global/language_manager.h"
#include "gui/view_models/player_view.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"

namespace
{
int resizeText(ImGuiInputTextCallbackData* data)
{
  auto* string = static_cast<std::string*>(data->UserData);
  if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
  {
    string->resize(static_cast<std::size_t>(data->BufTextLen));
    data->Buf = string->data();
  }
  return 0;
}

void input(const char* label, std::string& string, bool multiline = false)
{
  constexpr auto FLAGS = ImGuiInputTextFlags_CallbackResize;
  if (multiline)
  {
    ImGui::InputTextMultiline(label, string.data(), string.capacity() + 1,
                              ImVec2(-1, 120 * Theme::scale()), FLAGS,
                              resizeText, &string);
  }
  else
  {
    ImGui::InputText(label, string.data(), string.capacity() + 1, FLAGS,
                     resizeText, &string);
  }
}

std::string_view contextTable(NavSection section)
{
  // These are optional navigation hints. Missing names fall back to catalog;
  // schema discovery and form generation have no dependency on this mapping.
  switch (section)
  {
    case NavSection::SQUAD:
    case NavSection::LINEUP:
    case NavSection::YOUTH:
    case NavSection::RESERVES:
    case NavSection::SQUAD_PLANNER:
    case NavSection::MEDICAL:
      return "Players";
    case NavSection::STAFF:
      return "Staff";
    case NavSection::TRAINING:
      return "TeamTraining";
    case NavSection::FIXTURES:
    case NavSection::CALENDAR:
      return "Fixtures";
    case NavSection::STANDINGS:
      return "LeaguePoints";
    case NavSection::FINANCES:
      return "Teams";
    case NavSection::TRANSFERS:
      return "TransferList";
    case NavSection::INBOX:
    case NavSection::NEWS:
      return "InboxMessages";
    case NavSection::SCOUTING:
      return "ScoutingKnowledge";
    case NavSection::MANAGER:
    case NavSection::TIMELINE:
      return "ManagerProfile";
    case NavSection::DELEGATION:
      return "DelegatedDuties";
    case NavSection::INTERNATIONAL:
    case NavSection::CALL_UPS:
      return "InternationalState";
    case NavSection::AWARDS:
    case NavSection::RECORDS:
      return "SeasonHistory";
    default:
      return "Teams";
  }
}
std::string tableLabel(const std::string& name)
{
  static const std::map<std::string, const char*, std::less<>> labels = {
      {"Players", "NAV_SQUAD"},
      {"Teams", "NAV_CLUB"},
      {"Staff", "NAV_STAFF"},
      {"PlayerTraining", "NAV_TRAINING"},
      {"TeamTraining", "NAV_TRAINING"},
      {"ManagerProfile", "NAV_MANAGER"},
      {"Fixtures", "NAV_FIXTURES"},
      {"MatchReports", "EDITOR_MATCH_REPORTS"},
      {"FinanceLedger", "EDITOR_LEDGER"},
      {"PlayerSeasonStats", "EDITOR_SEASON_STATS"},
      {"SquadStatuses", "EDITOR_SQUAD_ROLES"}};
  if (const auto label = labels.find(name); label != labels.end())
  {
    return LOC(label->second);
  }
  std::string result;
  for (std::size_t i = 0; i < name.size(); ++i)
  {
    if (i > 0 && std::isupper(static_cast<unsigned char>(name.at(i))) != 0 &&
        std::islower(static_cast<unsigned char>(name.at(i - 1))) != 0)
    {
      result += ' ';
    }
    result += name.at(i) == '_' ? ' ' : name.at(i);
  }
  return result;
}

std::vector<std::string> relatedTables(const std::optional<NavEntry>& context,
                                       const std::string& primary)
{
  if (context && (context->kind == NavEntry::Kind::PLAYER ||
                  context->kind == NavEntry::Kind::COMPARE))
  {
    return {"Players", "PlayerTraining", "PlayerSeasonStats", "SquadStatuses"};
  }
  if (context && context->kind == NavEntry::Kind::CLUB)
  {
    return {"Players", "Teams", "Staff", "TeamTraining"};
  }
  if (!context)
  {
    return {primary};
  }
  switch (context->section)
  {
    case NavSection::SQUAD:
    case NavSection::LINEUP:
    case NavSection::YOUTH:
    case NavSection::RESERVES:
    case NavSection::SQUAD_PLANNER:
    case NavSection::MEDICAL:
      return {primary, "Players", "PlayerTraining", "SquadStatuses"};
    case NavSection::HOME:
    case NavSection::CLUB:
    case NavSection::FINANCES:
      return {
          primary, "Teams", "FinanceLedger", "ClubFacilities", "SupporterMood",
      };
    case NavSection::TRAINING:
      return {primary, "PlayerTraining", "Staff"};
    case NavSection::FIXTURES:
    case NavSection::CALENDAR:
      return {primary, "MatchReports"};
    case NavSection::STANDINGS:
      return {primary, "Leagues"};
    case NavSection::MANAGER:
      return {primary, "ManagerStints", "ManagerSeasons", "ManagerAwards"};
    case NavSection::SCOUTING:
      return {primary, "ScoutReports", "RecruitmentFocus", "ScoutShortlist"};
    case NavSection::TRANSFERS:
      return {primary, "TransferOffers", "Loans", "PreContracts"};
    default:
      return {primary};
  }
}
}  // namespace

RuntimeEditorDialog::RuntimeEditorDialog(GameController& owner)
    : controller(owner)
{
}
RuntimeEditorDialog::~RuntimeEditorDialog()
{
  if (opened)
  {
    controller.cancelEditor();
  }
}

void RuntimeEditorDialog::open(const std::optional<NavEntry>& context)
{
  if (opened)
  {
    return;
  }
  opened = true;
  close_requested = false;
  popup_requested = true;
  error.clear();
  pending_changes.clear();
  fields.clear();
  original.reset();
  context_filter.clear();
  scoped_filters.clear();
  context_player = 0;
  context_team = controller.hasSelectedTeam()
                     ? controller.getGame()->getManagedTeamId()
                     : 0;
  offset = 0;
  browse_all = false;
  show_records = false;
  focus_label.clear();
  search.fill(0);
  try
  {
    editor_session = controller.beginEditor();
    tables = RuntimeEditor::catalog(editor_session->database->getRaw());
    selected_table = context ? contextTable(context->section) : "Teams";
    if (context && (context->kind == NavEntry::Kind::PLAYER ||
                    context->kind == NavEntry::Kind::COMPARE))
    {
      selected_table = "Players";
      context_player = context->player;
      if (context->player != 0)
      {
        context_filter["id"] = static_cast<std::int64_t>(context->player);
      }
    }
    else if (context && context->kind == NavEntry::Kind::CLUB)
    {
      selected_table = "Players";
      context_team = context->team;
      context_filter["team_id"] = static_cast<std::int64_t>(context->team);
    }
    else if (context && context->kind == NavEntry::Kind::MATCH_REPORT)
    {
      selected_table = "MatchReports";
      context_filter["game_date"] = context->date.toString();
      context_filter["home_team_id"] = static_cast<std::int64_t>(context->team);
      context_filter["away_team_id"] =
          static_cast<std::int64_t>(context->away_team);
    }
    else if (selected_table == "Teams" && controller.hasSelectedTeam())
    {
      context_filter["id"] =
          static_cast<std::int64_t>(controller.getGame()->getManagedTeamId());
    }
    if (context && context->kind == NavEntry::Kind::SECTION &&
        controller.hasSelectedTeam() &&
        (selected_table == "Players" || selected_table == "Staff" ||
         selected_table == "TeamTraining" || selected_table == "InboxMessages"))
    {
      context_filter["team_id"] =
          static_cast<std::int64_t>(controller.getGame()->getManagedTeamId());
    }
    if (std::ranges::find(tables, selected_table,
                          &RuntimeEditor::Table::name) == tables.end())
    {
      selected_table = tables.empty() ? "" : tables.front().name;
      context_filter.clear();
    }
    const auto related = relatedTables(context, selected_table);
    std::erase_if(
        tables, [&related](const RuntimeEditor::Table& table)
        { return std::ranges::find(related, table.name) == related.end(); });
    refresh();
    if (page.rows.size() == 1)
    {
      selectRow(0);
    }
    else
    {
      show_records = true;
    }
  }
  catch (const std::exception& failure)
  {
    error = failure.what();
  }
}

void RuntimeEditorDialog::close()
{
  controller.cancelEditor();
  editor_session.reset();
  opened = false;
  fields.clear();
  pending_changes.clear();
  request = Request::None;
}

void RuntimeEditorDialog::refresh()
{
  if (!editor_session || selected_table.empty())
  {
    return;
  }
  try
  {
    page =
        RuntimeEditor::read(editor_session->database->getRaw(), selected_table,
                            offset, search.data(), 50, context_filter);
    if (selected_table == "save_meta" || selected_table == "schema_migrations")
    {
      page.table.writable = false;
    }
  }
  catch (const std::exception& failure)
  {
    error = failure.what();
    page = {};
    fields.clear();
    original.reset();
  }
}

void RuntimeEditorDialog::selectRow(std::size_t index)
{
  if (formDirty())
  {
    stage();
    if (!error.empty())
    {
      return;
    }
  }
  original = page.rows.at(index);
  show_records = false;
  focus_label = tableLabel(selected_table);
  std::string record_name;
  for (const auto* name :
       {"first_name", "last_name", "name", "title", "subject"})
  {
    const auto column = std::ranges::find(page.table.columns, std::string(name),
                                          &RuntimeEditor::Column::name);
    if (column == page.table.columns.end())
    {
      continue;
    }
    if (!record_name.empty())
    {
      record_name += ' ';
    }
    record_name +=
        RuntimeEditor::display(original->values[static_cast<std::size_t>(
            column - page.table.columns.begin())]);
  }
  if (!record_name.empty())
  {
    focus_label = std::move(record_name);
  }
  fields.clear();
  for (std::size_t i = 0; i < page.table.columns.size(); ++i)
  {
    const auto& column = page.table.columns[i];
    const auto& value = original->values[i];
    fields.push_back({
        .name = column.name,
        .text = RuntimeEditor::display(value),
        .type = RuntimeEditor::storageType(value),
        .readonly = column.hidden != 0,
    });
  }
}

void RuntimeEditorDialog::stage()
{
  if (!original)
  {
    return;
  }
  try
  {
    RuntimeEditor::Change change;
    change.table = selected_table;
    change.schema = page.schema;
    change.operation = RuntimeEditor::Operation::Update;
    change.original = *original;
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
      const auto& field = fields[i];
      if (field.readonly)
      {
        continue;
      }
      const auto value = RuntimeEditor::parse(field.type, field.text);
      if (original->values[i] != value)
      {
        change.fields[field.name] = value;
      }
    }
    if (change.fields.empty())
    {
      return;
    }
    auto candidate = SaveManager::createWorkingCopy();
    RuntimeEditor::copy(editor_session->database->getRaw(),
                        candidate->getRaw());
    RuntimeEditor::apply(candidate->getRaw(), {change}, {}, true);
    const auto selected_identity = change.original.identity;
    auto pending = pending_changes;
    const auto previous = std::ranges::find_if(
        pending,
        [&change](const RuntimeEditor::Change& old)
        {
          return old.table == change.table &&
                 old.original.identity == change.original.identity;
        });
    if (previous == pending.end())
      pending.push_back(std::move(change));
    else
    {
      // Keep the initial original for optimistic locking, but only the final
      // values for validation. A corrected draft must not retain an earlier
      // invalid value in its command history.
      for (const auto& [name, value] : change.fields)
      {
        const auto column = std::ranges::find(page.table.columns, name,
                                              &RuntimeEditor::Column::name);
        const auto index =
            static_cast<std::size_t>(column - page.table.columns.begin());
        if (value == previous->original.values.at(index))
          previous->fields.erase(name);
        else
          previous->fields[name] = value;
      }
      if (previous->fields.empty()) pending.erase(previous);
    }
    pending_changes = std::move(pending);
    editor_session->database = std::move(candidate);
    fields.clear();
    original.reset();
    error.clear();
    refresh();
    const auto selected = std::ranges::find_if(
        page.rows, [&selected_identity](const RuntimeEditor::Row& row)
        { return row.identity == selected_identity; });
    if (selected != page.rows.end())
    {
      selectRow(static_cast<std::size_t>(selected - page.rows.begin()));
    }
    else
    {
      show_records = true;
    }
  }
  catch (const std::exception& failure)
  {
    error = failure.what();
  }
}

void RuntimeEditorDialog::renderBrowser()
{
  ImGui::SetNextItemWidth(-1);
  if (ImGui::InputTextWithHint("##editor_search", LOC("EDITOR_SEARCH"),
                               search.data(), search.size()))
  {
    offset = 0;
    refresh();
  }
  if (UI::secondaryButton(LOC("EDITOR_PREVIOUS")) && offset >= 50)
  {
    offset -= 50;
    refresh();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!page.more);
  if (UI::secondaryButton(LOC("EDITOR_NEXT")))
  {
    offset += 50;
    refresh();
  }
  ImGui::EndDisabled();
  ImGui::TextDisabled("%s: %d–%d", LOC("EDITOR_RECORDS"),
                      page.rows.empty() ? 0 : offset + 1,
                      offset + static_cast<int>(page.rows.size()));
  // Compact record cards avoid a horizontally scrolling 30-column table.
  if (ImGui::BeginChild("##editor_records", ImVec2(0, 0),
                        ImGuiChildFlags_Borders))
  {
    for (std::size_t i = 0; i < page.rows.size(); ++i)
    {
      ImGui::PushID(static_cast<int>(i));
      std::string label;
      const auto& row = page.rows[i];
      for (const auto* name :
           {"first_name", "last_name", "name", "title", "subject"})
      {
        const auto col =
            std::ranges::find(page.table.columns, std::string(name),
                              &RuntimeEditor::Column::name);
        if (col == page.table.columns.end())
        {
          continue;
        }
        if (!label.empty())
        {
          label += ' ';
        }
        label += RuntimeEditor::display(row.values[static_cast<std::size_t>(
            col - page.table.columns.begin())]);
      }
      if (label.empty())
      {
        for (std::size_t j = 0; j < std::min<std::size_t>(row.values.size(), 3);
             ++j)
        {
          if (!label.empty())
          {
            label += " · ";
          }
          auto value = RuntimeEditor::display(row.values[j]);
          if (value.size() > 40)
          {
            value = value.substr(0, 37) + "…";
          }
          label += value;
        }
      }
      if (label.empty())
      {
        label = std::to_string(offset + static_cast<int>(i) + 1);
      }
      if (ImGui::Selectable((label + "###record").c_str()))
      {
        selectRow(i);
      }
      ImGui::PopID();
    }
    if (page.rows.empty())
    {
      ImGui::TextDisabled("%s", LOC("EDITOR_EMPTY"));
    }
  }
  ImGui::EndChild();
}

void RuntimeEditorDialog::chooseTable(const std::string& name)
{
  if (formDirty())
  {
    stage();
    if (!error.empty())
    {
      return;
    }
  }
  selected_table = name;
  context_filter.clear();
  const auto table =
      std::ranges::find(tables, name, &RuntimeEditor::Table::name);
  if (table != tables.end())
  {
    const auto has = [&table](const char* column)
    {
      return std::ranges::find(table->columns, std::string(column),
                               &RuntimeEditor::Column::name) !=
             table->columns.end();
    };
    if ((context_player != 0u) && name == "Players" && has("id"))
    {
      context_filter["id"] = static_cast<std::int64_t>(context_player);
    }
    else if ((context_player != 0u) && has("player_id"))
    {
      context_filter["player_id"] = static_cast<std::int64_t>(context_player);
    }
    else if ((context_team != 0u) && name == "Teams" && has("id"))
    {
      context_filter["id"] = static_cast<std::int64_t>(context_team);
    }
    else if ((context_team != 0u) && has("team_id"))
    {
      context_filter["team_id"] = static_cast<std::int64_t>(context_team);
    }
  }
  if (const auto scope = scoped_filters.find(name);
      scope != scoped_filters.end())
    context_filter = scope->second;
  offset = 0;
  fields.clear();
  original.reset();
  show_records = true;
  focus_label.clear();
  search.fill(0);
  refresh();
  if (page.rows.size() == 1)
  {
    selectRow(0);
  }
}

void RuntimeEditorDialog::renderAttributes(Field& field)
{
  auto stats = nlohmann::json::parse(field.text, nullptr, false);
  if (!stats.is_object())
  {
    ImGui::TextWrapped("%s", LOC("EDITOR_ERROR"));
    return;
  }
  using Stat = std::pair<std::string, double>;
  struct Group
  {
    std::string title;
    std::vector<Stat> stats;
  };
  std::vector<Group> groups;
  std::set<std::string> grouped;
  for (const auto& descriptor : PlayerView::ATTRIBUTE_GROUPS)
  {
    Group group{.title = LOC(descriptor.title_key), .stats = {}};
    for (const auto name : descriptor.stats)
    {
      if (const auto it = stats.find(std::string(name));
          it != stats.end() && it->is_number())
      {
        group.stats.emplace_back(name, it->get<double>());
        grouped.emplace(name);
      }
    }
    if (!group.stats.empty())
    {
      groups.push_back(std::move(group));
    }
  }
  Group other{.title = LOC("PROFILE_GROUP_OTHER"), .stats = {}};
  for (const auto& [name, value] : stats.items())
  {
    if (!grouped.contains(name) && value.is_number())
    {
      other.stats.emplace_back(name, value.get<double>());
    }
  }
  if (!other.stats.empty())
  {
    groups.push_back(std::move(other));
  }
  ImGui::TextWrapped("%s", LOC("EDITOR_ATTRIBUTE_HINT"));
  ImGui::Spacing();
  const int columns =
      ImGui::GetContentRegionAvail().x >= 760 * Theme::scale()   ? 3
      : ImGui::GetContentRegionAvail().x >= 500 * Theme::scale() ? 2
                                                                 : 1;
  const bool table = ImGui::BeginTable("##attribute_groups", columns,
                                       ImGuiTableFlags_SizingStretchSame);
  bool changed = false;
  for (auto& group : groups)
  {
    if (table)
    {
      ImGui::TableNextColumn();
    }
    ImGui::PushID(group.title.c_str());
    UI::beginAutoHeightCard("##attribute_card", group.title.c_str());
    for (auto& [name, value] : group.stats)
    {
      ImGui::PushID(name.c_str());
      const auto label = PlayerView::statLabel(name);
      ImGui::TextUnformatted(label.c_str());
      ImGui::SetNextItemWidth(-1);
      auto rating = static_cast<float>(value);
      ImGui::PushStyleColor(ImGuiCol_SliderGrab, Theme::ratingColor(value));
      ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, Theme::palette().accent);
      if (ImGui::SliderFloat("##rating", &rating, MIN_STAT_VAL, MAX_STAT_VAL,
                             "%.0f / 100", ImGuiSliderFlags_AlwaysClamp))
      {
        stats[name] = rating;
        changed = true;
      }
      ImGui::PopStyleColor(2);
      ImGui::Dummy(ImVec2(0, 4 * Theme::scale()));
      ImGui::PopID();
    }
    UI::endCard();
    ImGui::PopID();
  }
  if (table)
  {
    ImGui::EndTable();
  }
  if (changed)
  {
    field.text = stats.dump();
  }
}

void RuntimeEditorDialog::renderForm()
{
  if (fields.empty())
  {
    ImGui::TextWrapped("%s", LOC("EDITOR_SELECT"));
    return;
  }
  bool attributes = false;
  const bool has_attributes =
      selected_table == "Players" &&
      std::ranges::any_of(
          fields, [](const Field& field) { return field.name == "stats"; });
  if (has_attributes && ImGui::BeginTabBar("##editor_tabs"))
  {
    if (ImGui::BeginTabItem(LOC("EDITOR_ATTRIBUTES")))
    {
      attributes = true;
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(LOC("EDITOR_DETAILS")))
    {
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  if (ImGui::BeginChild("##editor_fields", ImVec2(0, 0)))
  {
    for (auto& field : fields)
    {
      if (has_attributes && ((field.name == "stats") != attributes))
      {
        continue;
      }
      const auto column = std::ranges::find(page.table.columns, field.name,
                                            &RuntimeEditor::Column::name);
      if (column == page.table.columns.end())
      {
        continue;
      }
      const bool technical =
          column->primary_key != 0 || field.readonly || field.name == "id" ||
          field.name.ends_with("_id") || field.name.starts_with("next_") ||
          field.name == "traits" || field.name == "dynamics" ||
          field.name == "status" || field.name == "lineup" ||
          field.name == "strategy" || field.name == "recent_form";
      if (technical || field.type == SQLITE_BLOB)
      {
        continue;
      }
      if (field.name == "stats" && selected_table == "Players")
      {
        renderAttributes(field);
        continue;
      }
      ImGui::PushID(field.name.c_str());
      auto label = field.name;
      std::ranges::replace(label, '_', ' ');
      if (!label.empty())
      {
        label[0] = static_cast<char>(
            std::toupper(static_cast<unsigned char>(label[0])));
      }
      ImGui::TextUnformatted(label.c_str());
      ImGui::SetNextItemWidth(-1);
      ImGui::BeginDisabled(!page.table.writable);
      if (field.type == SQLITE_INTEGER)
      {
        auto number = std::get<std::int64_t>(
            RuntimeEditor::parse(field.type, field.text));
        if (ImGui::InputScalar("##value", ImGuiDataType_S64, &number))
        {
          field.text = std::to_string(number);
        }
      }
      else if (field.type == SQLITE_FLOAT)
      {
        double number =
            std::get<double>(RuntimeEditor::parse(field.type, field.text));
        if (ImGui::InputDouble("##value", &number) && std::isfinite(number))
        {
          field.text = RuntimeEditor::display(number);
        }
      }
      else if (field.type == SQLITE_TEXT)
      {
        const auto json = nlohmann::json::parse(field.text, nullptr, false);
        if (json.is_object() || json.is_array())
        {
          ImGui::TextDisabled("%s", LOC("EDITOR_STRUCTURED_READONLY"));
        }
        else
        {
          input("##value", field.text, field.text.size() > 100);
        }
      }
      else
      {
        ImGui::TextDisabled("%s", LOC("EDITOR_NO_VALUE"));
      }
      ImGui::EndDisabled();
      ImGui::Dummy(ImVec2(0, Theme::Space::M * Theme::scale()));
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
}

void RuntimeEditorDialog::render()
{
  if (!opened)
  {
    return;
  }
  if (popup_requested)
  {
    ImGui::OpenPopup("###runtime_editor");
    popup_requested = false;
  }
  const auto* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x * .5f),
             viewport->WorkPos.y + (viewport->WorkSize.y * .5f)),
      ImGuiCond_Appearing, ImVec2(.5f, .5f));
  ImGui::SetNextWindowSize(
      ImVec2(std::min(viewport->WorkSize.x - 24, 1160 * Theme::scale()),
             std::min(viewport->WorkSize.y - 24, 800 * Theme::scale())),
      ImGuiCond_Appearing);
  const std::string title =
      std::string(LOC("EDITOR_TITLE")) + "###runtime_editor";
  if (!ImGui::BeginPopupModal(
          title.c_str(), nullptr,
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar))
  {
    return;
  }
  if (close_requested)
  {
    ImGui::CloseCurrentPopup();
    close();
    ImGui::EndPopup();
    return;
  }
  UI::pageHeader(
      focus_label.empty() ? LOC("EDITOR_TITLE") : focus_label.c_str(),
      LOC("EDITOR_SUBTITLE"));
  if (!error.empty())
  {
    ImGui::TextColored(Theme::palette().negative, "%s", LOC("EDITOR_ERROR"));
    ImGui::TextWrapped("%s", error.c_str());
  }
  if (UI::secondaryButton(browse_all ? LOC("EDITOR_FOCUSED")
                                     : LOC("EDITOR_BROWSE")))
  {
    browse_all = !browse_all;
  }
  if (!browse_all && !show_records)
  {
    ImGui::SameLine();
    if (UI::secondaryButton(LOC("EDITOR_CHANGE_RECORD")))
    {
      show_records = true;
    }
  }
  const float footer = 80 * Theme::scale();
  if (editor_session)
  {
    if (ImGui::BeginChild("##editor_body", ImVec2(0, -footer)))
    {
      if (browse_all)
      {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##editor_table",
                              tableLabel(selected_table).c_str()))
        {
          for (const auto& table : tables)
          {
            if (ImGui::Selectable(tableLabel(table.name).c_str(),
                                  table.name == selected_table))
            {
              chooseTable(table.name);
            }
          }
          ImGui::EndCombo();
        }
      }
      if (show_records || fields.empty())
      {
        renderBrowser();
      }
      else
      {
        renderForm();
      }
    }
    ImGui::EndChild();
  }
  if (!pending_changes.empty())
    ImGui::Text("%s: %zu", LOC("EDITOR_PENDING"), pending_changes.size());
  else
    ImGui::TextDisabled("%s", LOC("EDITOR_DRAFT_HINT"));
  ImGui::BeginDisabled(pending_changes.empty() && !formDirty());
  if (UI::primaryButton(LOC("EDITOR_APPLY")))
  {
    if (formDirty())
    {
      stage();
    }
    apply_confirm = !pending_changes.empty() && error.empty();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (UI::secondaryButton(LOC("EDITOR_CANCEL")))
  {
    close();
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!controller.canUndoEditor());
  if (UI::secondaryButton(LOC("EDITOR_UNDO")))
  {
    undo_confirm = true;
  }
  ImGui::EndDisabled();
  if (apply_confirm || undo_confirm)
  {
    ImGui::OpenPopup("##editor_apply_confirm");
    apply_confirm = false;
  }
  if (ImGui::BeginPopupModal("##editor_apply_confirm", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::TextWrapped("%s", LOC(undo_confirm ? "EDITOR_UNDO_CONFIRM"
                                              : "EDITOR_APPLY_CONFIRM"));
    if (UI::primaryButton(LOC("EDITOR_APPLY")))
    {
      request = undo_confirm ? Request::Undo : Request::Apply;
      undo_confirm = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (UI::secondaryButton(LOC("EDITOR_CANCEL")))
    {
      undo_confirm = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::EndPopup();
}

RuntimeEditorDialog::Request RuntimeEditorDialog::takeRequest()
{
  const auto result = request;
  request = Request::None;
  return result;
}

void RuntimeEditorDialog::finish(bool success, std::string message)
{
  if (success)
  {
    controller.cancelEditor();
    editor_session.reset();
    close_requested = true;
  }
  else
  {
    error = std::move(message);
  }
}

bool RuntimeEditorDialog::formDirty() const
{
  if (!original)
  {
    return false;
  }
  for (std::size_t i = 0; i < fields.size(); ++i)
  {
    if (!fields[i].readonly &&
        (fields[i].type != RuntimeEditor::storageType(original->values[i]) ||
         fields[i].text != RuntimeEditor::display(original->values[i])))
    {
      return true;
    }
  }
  return false;
}

void RuntimeEditorDialog::open(const EditorContext& context)
{
  if (opened) return;
  open(std::nullopt);
  if (!editor_session) return;
  try
  {
    selected_table = context.table;
    context_filter = context.filter;
    scoped_filters = context.related_filters;
    scoped_filters[context.table] = context.filter;
    fields.clear();
    original.reset();
    tables = RuntimeEditor::catalog(editor_session->database->getRaw());
    std::erase_if(tables,
                  [&context](const RuntimeEditor::Table& table)
                  {
                    return table.name != context.table &&
                           std::ranges::find(context.related_tables,
                                             table.name) ==
                               context.related_tables.end();
                  });
    error.clear();
    refresh();
    if (page.rows.size() == 1)
      selectRow(0);
    else
      show_records = true;
  }
  catch (const std::exception& failure)
  {
    error = failure.what();
  }
}

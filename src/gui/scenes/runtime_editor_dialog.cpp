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
#include <format>
#include <nlohmann/json.hpp>
#include <set>

#include "database/database_exception.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "global/language_manager.h"
#include "gui/view_models/player_view.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"

namespace
{
constexpr int PAGE_SIZE = 50;
constexpr float WINDOW_WIDTH = 1060.0F;
constexpr float WINDOW_HEIGHT = 600.0F;
constexpr float WINDOW_MARGIN = 24.0F;
constexpr float WINDOW_CENTER = 0.5F;
constexpr float DETAILS_COLUMNS_WIDTH = 560.0F;

/** Dense form spacing preserves the user's font scale without giant gaps. */
class ScopedEditorStyle
{
 public:
  ScopedEditorStyle()
  {
    constexpr float FRAME_PADDING_X = 6.0F;
    const float scale = Theme::scale();
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(Theme::Space::M * scale, Theme::Space::S * scale));
    ImGui::PushStyleVar(
        ImGuiStyleVar_FramePadding,
        ImVec2(FRAME_PADDING_X * scale, Theme::Space::XS * scale));
    ImGui::PushStyleVar(
        ImGuiStyleVar_ItemSpacing,
        ImVec2(Theme::Space::S * scale, Theme::Space::XS * scale));
  }
  ~ScopedEditorStyle() { ImGui::PopStyleVar(3); }
  ScopedEditorStyle(const ScopedEditorStyle&) = delete;
  ScopedEditorStyle& operator=(const ScopedEditorStyle&) = delete;
  ScopedEditorStyle(ScopedEditorStyle&&) = delete;
  ScopedEditorStyle& operator=(ScopedEditorStyle&&) = delete;
};

void wrapped(const char* text)
{
  ImGui::PushTextWrapPos(0.0F);
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
}
void muted(const std::string& text)
{
  ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextUnformatted(text.c_str());
  ImGui::PopStyleColor();
}

std::string rowLabel(const RuntimeEditor::Table& table,
                     const RuntimeEditor::Row& row)
{
  std::string label;
  for (const auto* name :
       {"first_name", "last_name", "name", "title", "subject"})
  {
    const auto column = std::ranges::find(table.columns, std::string(name),
                                          &RuntimeEditor::Column::name);
    if (column == table.columns.end())
    {
      continue;
    }
    if (!label.empty())
    {
      label += ' ';
    }
    label += RuntimeEditor::display(row.values.at(
        static_cast<std::size_t>(column - table.columns.begin())));
  }
  return label;
}

struct AttributeGroup
{
  std::string title;
  std::vector<std::pair<std::string, double>> stats;
};
std::vector<AttributeGroup> attributeGroups(const nlohmann::json& stats)
{
  std::vector<AttributeGroup> groups;
  std::set<std::string, std::less<>> grouped;
  for (const auto& descriptor : PlayerView::ATTRIBUTE_GROUPS)
  {
    AttributeGroup group{.title = LOC(descriptor.title_key), .stats = {}};
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
  AttributeGroup other{.title = LOC("PROFILE_GROUP_OTHER"), .stats = {}};
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
  return groups;
}

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

void input(const char* label, std::string& string)
{
  constexpr auto FLAGS = ImGuiInputTextFlags_CallbackResize;
  ImGui::InputText(label, string.data(), string.capacity() + 1, FLAGS,
                   resizeText, &string);
}

std::string humanLabel(std::string_view name)
{
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
  if (!result.empty())
  {
    result.front() = static_cast<char>(
        std::toupper(static_cast<unsigned char>(result.front())));
  }
  return result;
}
int attributeColumns(const std::vector<AttributeGroup>& groups,
                     float rating_width)
{
  float label_width = 0.0F;
  for (const auto& group : groups)
  {
    for (const auto& [name, value] : group.stats)
    {
      const auto translated = PlayerView::statLabel(name);
      const auto label = translated == name ? humanLabel(name) : translated;
      label_width = std::max(label_width, ImGui::CalcTextSize(label.c_str()).x);
    }
  }
  // Include the card padding and both tables' cell padding. Never select a
  // column count that breaks ordinary attribute names into fragments.
  const float padding =
      (((2 * Theme::Space::M) + (4 * 2.0F)) * Theme::scale()) +
      (2 * ImGui::GetStyle().CellPadding.x);
  const float minimum_width = label_width + rating_width + padding;
  const int maximum =
      std::max(1, static_cast<int>(std::min<std::size_t>(3, groups.size())));
  return std::clamp(
      static_cast<int>(ImGui::GetContentRegionAvail().x / minimum_width), 1,
      maximum);
}

bool renderAttribute(const std::string& name, double value,
                     nlohmann::json& stats)
{
  bool changed = false;
  ImGui::PushID(name.c_str());
  ImGui::TableNextColumn();
  ImGui::AlignTextToFramePadding();
  const auto translated = PlayerView::statLabel(name);
  wrapped((translated == name ? humanLabel(name) : translated).c_str());
  ImGui::TableNextColumn();
  ImGui::SetNextItemWidth(-1);
  auto rating = static_cast<float>(value);
  ImGui::PushStyleColor(ImGuiCol_Text, Theme::ratingColor(value));
  if (ImGui::InputFloat("##rating", &rating, 1.0F, 0.0F, "%.0f") &&
      std::isfinite(rating))
  {
    stats.at(name) = std::clamp(rating, static_cast<float>(MIN_STAT_VAL),
                                static_cast<float>(MAX_STAT_VAL));
    changed = true;
  }
  ImGui::PopStyleColor();
  ImGui::PopID();
  return changed;
}

std::string tableLabel(const std::string& name)
{
  static const std::map<std::string, const char*, std::less<>> labels = {
      {"Players", "NAV_SQUAD"},
      {"Teams", "NAV_CLUB"},
  };
  if (const auto label = labels.find(name); label != labels.end())
  {
    return LOC(label->second);
  }
  return humanLabel(name);
}

std::vector<std::string> relatedTables(const std::optional<NavEntry>& context,
                                       const std::string& primary)
{
  if (primary.empty())
  {
    return {};
  }
  return context ? std::vector<std::string>{"Players", "Teams"}
                 : std::vector<std::string>{primary};
}

}  // namespace

RuntimeEditorDialog::RuntimeEditorDialog(GameController& owner)
    : controller(owner)
{
}
RuntimeEditorDialog::~RuntimeEditorDialog()
{
  if (popup.opened)
  {
    controller.cancelEditor();
  }
}

void RuntimeEditorDialog::open(const std::optional<NavEntry>& context)
{
  if (popup.opened)
  {
    return;
  }
  popup = {.opened = true, .popup_requested = true};
  error.clear();
  pending_changes.clear();
  fields.clear();
  original.reset();
  context_filter.clear();
  scoped_filters.clear();
  presentations.clear();
  context_player = 0;
  context_team = controller.hasSelectedTeam()
                     ? controller.getGame()->getManagedTeamId()
                     : 0;
  offset = 0;
  show_records = false;
  focus_label.clear();
  search.fill(0);
  try
  {
    editor_session = controller.beginEditor();
    tables = RuntimeEditor::catalog(editor_session->database->getRaw());
    selected_table = context ? editorTable(context->section) : "Teams";
    if (context && (context->kind == NavEntry::Kind::PLAYER ||
                    context->kind == NavEntry::Kind::COMPARE))
    {
      selected_table = "Players";
      context_player = context->player;
      if (const auto player =
              controller.getGameData()->getPlayer(context_player))
      {
        context_team = player->get().getTeamId();
      }
      if (context->player != 0)
      {
        context_filter["id"] = static_cast<std::int64_t>(context->player);
      }
    }
    else if (context && context->kind == NavEntry::Kind::CLUB)
    {
      selected_table = "Teams";
      context_team = context->team;
      context_filter["id"] = static_cast<std::int64_t>(context->team);
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
      selected_table.clear();
      context_filter.clear();
    }
    const auto related = relatedTables(context, selected_table);
    std::erase_if(
        tables, [&related](const RuntimeEditor::Table& table)
        { return std::ranges::find(related, table.name) == related.end(); });
    limitToGameplay();
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
  // Opening flushes the live game's feature serializers. Contain their
  // exception types here so a failed open cannot terminate the UI loop.
  catch (const std::exception& failure)
  {
    error = failure.what();
  }
}

void RuntimeEditorDialog::close()
{
  controller.cancelEditor();
  editor_session.reset();
  popup.opened = false;
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
                            offset, search.data(), PAGE_SIZE, context_filter);
    if (selected_table == "save_meta" || selected_table == "schema_migrations")
    {
      page.table.writable = false;
    }
  }
  catch (const RuntimeEditor::Error& failure)
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
  if (auto record_name = rowLabel(page.table, *original); !record_name.empty())
  {
    focus_label = std::move(record_name);
  }
  fields.clear();
  for (std::size_t i = 0; i < page.table.columns.size(); ++i)
  {
    const auto& column = page.table.columns.at(i);
    const auto& value = original->values.at(i);
    fields.push_back({
        .name = column.name,
        .text = RuntimeEditor::display(value),
        .type = RuntimeEditor::storageType(value),
        .readonly = column.hidden != 0 || column.primary_key != 0,
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
      const auto& field = fields.at(i);
      if (field.readonly || fieldDescriptor(field.name) == nullptr)
      {
        continue;
      }
      const auto value = RuntimeEditor::parse(field.type, field.text);
      if (original->values.at(i) != value)
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
    if (const auto previous = std::ranges::find_if(
            pending,
            [&change](const RuntimeEditor::Change& old)
            {
              return old.table == change.table &&
                     old.original.identity == change.original.identity;
            });
        previous == pending.end())
    {
      pending.push_back(std::move(change));
    }
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
        {
          previous->fields.erase(name);
        }
        else
        {
          previous->fields[name] = value;
        }
      }
      if (previous->fields.empty())
      {
        pending.erase(previous);
      }
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
  catch (const RuntimeEditor::Error& failure)
  {
    error = failure.what();
  }
  catch (const DatabaseException& failure)
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
  if (offset > 0 || page.more)
  {
    ImGui::BeginDisabled(offset == 0);
    if (UI::secondaryButton(LOC("EDITOR_PREVIOUS"), ImVec2(0, 0),
                            UI::ButtonSize::COMPACT))
    {
      offset -= PAGE_SIZE;
      refresh();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!page.more);
    if (UI::secondaryButton(LOC("EDITOR_NEXT"), ImVec2(0, 0),
                            UI::ButtonSize::COMPACT))
    {
      offset += PAGE_SIZE;
      refresh();
    }
    ImGui::EndDisabled();
  }
  muted(std::format("{}: {}–{}", LOC("EDITOR_RECORDS"),
                    page.rows.empty() ? 0 : offset + 1,
                    offset + static_cast<int>(page.rows.size())));
  // Compact record cards avoid a horizontally scrolling 30-column table.
  if (ImGui::BeginChild("##editor_records", ImVec2(0, 0),
                        ImGuiChildFlags_Borders))
  {
    for (std::size_t i = 0; i < page.rows.size(); ++i)
    {
      ImGui::PushID(static_cast<int>(i));
      auto label = rowLabel(page.table, page.rows.at(i));
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
      muted(LOC("EDITOR_EMPTY"));
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
  const auto table =
      std::ranges::find(tables, name, &RuntimeEditor::Table::name);
  if (table == tables.end())
  {
    return;
  }
  selected_table = name;
  context_filter.clear();
  const auto has = [&table](const char* column)
  {
    return std::ranges::find(table->columns, std::string(column),
                             &RuntimeEditor::Column::name) !=
           table->columns.end();
  };
  if ((context_player != 0U) && name == "Players" && has("id"))
  {
    context_filter["id"] = static_cast<std::int64_t>(context_player);
  }
  else if ((context_player != 0U) && has("player_id"))
  {
    context_filter["player_id"] = static_cast<std::int64_t>(context_player);
  }
  else if ((context_team != 0U) && name == "Teams" && has("id"))
  {
    context_filter["id"] = static_cast<std::int64_t>(context_team);
  }
  else if ((context_team != 0U) && has("team_id"))
  {
    context_filter["team_id"] = static_cast<std::int64_t>(context_team);
  }
  if (const auto scope = scoped_filters.find(name);
      scope != scoped_filters.end())
  {
    context_filter = scope->second;
  }
  offset = 0;
  fields.clear();
  original.reset();
  show_records = true;
  focus_label = tableLabel(name);
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
    wrapped(LOC("EDITOR_ERROR"));
    return;
  }
  const auto groups = attributeGroups(stats);
  wrapped(LOC("EDITOR_ATTRIBUTE_HINT"));
  ImGui::Spacing();
  const float scale = Theme::scale();
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2 * scale, 2 * scale));
  const float rating_width =
      ImGui::CalcTextSize(std::to_string(MAX_STAT_VAL).c_str()).x +
      (2 * ImGui::GetStyle().FramePadding.x) + (2 * ImGui::GetFrameHeight()) +
      (2 * ImGui::GetStyle().ItemInnerSpacing.x);
  const int columns = attributeColumns(groups, rating_width);
  const bool table = ImGui::BeginTable("##attribute_groups", columns,
                                       ImGuiTableFlags_SizingStretchSame);
  bool changed = false;
  for (const auto& group : groups)
  {
    if (table)
    {
      ImGui::TableNextColumn();
    }
    ImGui::PushID(group.title.c_str());
    UI::beginAutoHeightCard("##attribute_card", group.title.c_str());
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                        ImVec2(2 * scale, 2 * scale));
    if (ImGui::BeginTable("##ratings", 2, ImGuiTableFlags_SizingStretchProp))
    {
      ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthFixed,
                              rating_width);
      for (const auto& [name, value] : group.stats)
      {
        changed = renderAttribute(name, value, stats) || changed;
      }
      ImGui::EndTable();
    }
    ImGui::PopStyleVar();
    UI::endCard();
    ImGui::PopID();
  }
  if (table)
  {
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();
  if (changed)
  {
    field.text = stats.dump();
  }
}

const EditorField* RuntimeEditorDialog::fieldDescriptor(
    const std::string& name) const
{
  const auto presentation = presentations.find(selected_table);
  if (presentation == presentations.end())
  {
    return nullptr;
  }
  const auto descriptor =
      std::ranges::find(presentation->second, name, &EditorField::column);
  return descriptor == presentation->second.end() ? nullptr
                                                  : std::to_address(descriptor);
}

void RuntimeEditorDialog::limitToGameplay()
{
  for (const auto& table : tables)
  {
    if (!presentations.contains(table.name))
    {
      presentations[table.name] = editorFields(table.name);
    }
  }
  std::erase_if(tables, [this](const RuntimeEditor::Table& table)
                { return presentations.at(table.name).empty(); });
  if (std::ranges::find(tables, selected_table, &RuntimeEditor::Table::name) ==
      tables.end())
  {
    selected_table.clear();
    page = {};
    fields.clear();
    original.reset();
  }
}

void RuntimeEditorDialog::renderChoice(Field& field,
                                       const EditorField& descriptor)
{
  const auto selected =
      std::ranges::find(descriptor.choices, field.text,
                        &std::pair<std::string, std::string>::first);
  if (const char* preview = selected == descriptor.choices.end()
                                ? LOC("EDITOR_NO_VALUE")
                                : LOC(selected->second.c_str());
      !ImGui::BeginCombo("##value", preview))
  {
    return;
  }
  for (const auto& [value, label] : descriptor.choices)
  {
    if (ImGui::Selectable(LOC(label.c_str()), field.text == value))
    {
      field.text = value;
    }
  }
  ImGui::EndCombo();
}

void RuntimeEditorDialog::renderNumber(Field& field,
                                       const EditorField& descriptor)
{
  if (field.type == SQLITE_INTEGER)
  {
    auto number =
        std::get<std::int64_t>(RuntimeEditor::parse(field.type, field.text));
    if (const std::int64_t step = 1;
        !ImGui::InputScalar("##value", ImGuiDataType_S64, &number, &step))
    {
      return;
    }
    if (descriptor.minimum.has_value())
    {
      number = std::max(number, static_cast<std::int64_t>(*descriptor.minimum));
    }
    if (descriptor.maximum.has_value())
    {
      number = std::min(number, static_cast<std::int64_t>(*descriptor.maximum));
    }
    field.text = std::to_string(number);
    return;
  }
  if (field.type == SQLITE_FLOAT)
  {
    double number =
        std::get<double>(RuntimeEditor::parse(field.type, field.text));
    if (!ImGui::InputDouble("##value", &number) || !std::isfinite(number))
    {
      return;
    }
    if (descriptor.minimum.has_value())
    {
      number = std::max(number, *descriptor.minimum);
    }
    if (descriptor.maximum.has_value())
    {
      number = std::min(number, *descriptor.maximum);
    }
    field.text = RuntimeEditor::display(number);
    return;
  }
  ImGui::TextUnformatted(LOC("EDITOR_NO_VALUE"));
}

void RuntimeEditorDialog::renderField(Field& field,
                                      const EditorField& descriptor)
{
  using enum EditorField::Control;
  if (descriptor.control == Attributes)
  {
    renderAttributes(field);
    return;
  }
  ImGui::PushID(field.name.c_str());
  ImGui::TextUnformatted(LOC(descriptor.label.c_str()));
  ImGui::SetNextItemWidth(-1);
  switch (descriptor.control)
  {
    case Choice:
      renderChoice(field, descriptor);
      break;
    case Number:
      renderNumber(field, descriptor);
      break;
    case Text:
      input("##value", field.text);
      break;
    case Attributes:
      break;
  }
  ImGui::Dummy(ImVec2(0, Theme::Space::XS * Theme::scale()));
  ImGui::PopID();
}

void RuntimeEditorDialog::renderForm()
{
  const auto presentation = presentations.find(selected_table);
  if (fields.empty() || presentation == presentations.end())
  {
    wrapped(LOC("EDITOR_UNAVAILABLE"));
    return;
  }
  std::vector<std::string> sections;
  for (const auto& descriptor : presentation->second)
  {
    if (const auto field =
            std::ranges::find(fields, descriptor.column, &Field::name);
        field == fields.end() || field->readonly)
    {
      continue;
    }
    if (std::ranges::find(sections, descriptor.section) == sections.end())
    {
      sections.push_back(descriptor.section);
    }
  }
  if (sections.empty())
  {
    wrapped(LOC("EDITOR_UNAVAILABLE"));
    return;
  }
  std::string selected_section;
  if (ImGui::BeginTabBar("##editor_sections",
                         ImGuiTabBarFlags_FittingPolicyScroll))
  {
    for (const auto& section : sections)
    {
      if (ImGui::BeginTabItem(LOC(section.c_str())))
      {
        selected_section = section;
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
  renderSection(selected_section, presentation->second);
}

void RuntimeEditorDialog::renderSection(
    std::string_view selected_section,
    const std::vector<EditorField>& descriptors)
{
  if (ImGui::BeginChild("##editor_fields", ImVec2(0, 0)))
  {
    ImGui::Spacing();
    ImGui::BeginDisabled(!page.table.writable);
    const bool attributes = selected_section == "EDITOR_ATTRIBUTES";
    const int columns =
        !attributes && ImGui::GetContentRegionAvail().x >=
                           DETAILS_COLUMNS_WIDTH * Theme::scale()
            ? 2
            : 1;
    const bool layout = ImGui::BeginTable("##details_layout", columns,
                                          ImGuiTableFlags_SizingStretchSame);
    for (const auto& descriptor : descriptors)
    {
      if (descriptor.section != selected_section)
      {
        continue;
      }
      const auto field =
          std::ranges::find(fields, descriptor.column, &Field::name);
      if (field == fields.end() || field->readonly)
      {
        continue;
      }
      if (layout)
      {
        ImGui::TableNextColumn();
      }
      renderField(*field, descriptor);
    }
    if (layout)
    {
      ImGui::EndTable();
    }
    ImGui::EndDisabled();
  }
  ImGui::EndChild();
}

void RuntimeEditorDialog::render()
{
  if (!popup.opened)
  {
    return;
  }
  const ScopedEditorStyle editor_style;
  if (popup.popup_requested)
  {
    ImGui::OpenPopup("###runtime_editor");
    popup.popup_requested = false;
  }
  const auto* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + (viewport->WorkSize.x * WINDOW_CENTER),
             viewport->WorkPos.y + (viewport->WorkSize.y * WINDOW_CENTER)),
      ImGuiCond_Always, ImVec2(WINDOW_CENTER, WINDOW_CENTER));
  ImGui::SetNextWindowSize(ImVec2(std::min(viewport->WorkSize.x - WINDOW_MARGIN,
                                           WINDOW_WIDTH * Theme::scale()),
                                  std::min(viewport->WorkSize.y - WINDOW_MARGIN,
                                           WINDOW_HEIGHT * Theme::scale())),
                           ImGuiCond_Always);
  if (const std::string title =
          std::string(LOC("EDITOR_TITLE")) + "###runtime_editor";
      !ImGui::BeginPopupModal(
          title.c_str(), nullptr,
          static_cast<ImGuiWindowFlags>(
              static_cast<unsigned>(ImGuiWindowFlags_NoSavedSettings) |
              static_cast<unsigned>(ImGuiWindowFlags_NoTitleBar) |
              static_cast<unsigned>(ImGuiWindowFlags_MenuBar) |
              static_cast<unsigned>(ImGuiWindowFlags_NoResize) |
              static_cast<unsigned>(ImGuiWindowFlags_NoMove))))
  {
    return;
  }
  if (popup.close_requested)
  {
    ImGui::CloseCurrentPopup();
    close();
    ImGui::EndPopup();
    return;
  }
  const char* subtitle = LOC("EDITOR_SUBTITLE");
  if (selected_table == "Players")
  {
    subtitle = LOC("EDITOR_PLAYER_HINT");
  }
  else if (selected_table == "Teams")
  {
    subtitle = LOC("EDITOR_CLUB_HINT");
  }
  {
    const Theme::ScopedText title_style(Theme::Text::TITLE);
    ImGui::TextUnformatted(focus_label.empty() ? LOC("EDITOR_TITLE")
                                               : focus_label.c_str());
  }
  {
    const Theme::ScopedText subtitle_style(Theme::Text::SMALL);
    muted(subtitle);
  }
  if (!error.empty())
  {
    ImGui::PushStyleColor(ImGuiCol_Text, Theme::palette().negative);
    ImGui::TextUnformatted(LOC("EDITOR_ERROR"));
    ImGui::PopStyleColor();
    wrapped(error.c_str());
  }
  renderNavigation();
  const float footer = ImGui::GetTextLineHeight() +
                       UI::buttonHeight(UI::ButtonSize::COMPACT) +
                       (ImGui::GetStyle().ItemSpacing.y * 3);
  renderBody(footer);
  renderActions();
  renderConfirmation();
  ImGui::EndPopup();
}

void RuntimeEditorDialog::renderNavigation()
{
  if (tables.size() <= 1 || !ImGui::BeginMenuBar())
  {
    return;
  }
  if (ImGui::BeginMenu(LOC("EDITOR_EDIT_MENU")))
  {
    for (const auto& table : tables)
    {
      if (ImGui::MenuItem(tableLabel(table.name).c_str(), nullptr,
                          table.name == selected_table))
      {
        chooseTable(table.name);
      }
    }
    ImGui::EndMenu();
  }
  if (!show_records && selected_table == "Players" && context_player == 0 &&
      ImGui::MenuItem(LOC("EDITOR_CHOOSE_PLAYER")))
  {
    if (formDirty())
    {
      stage();
    }
    if (error.empty())
    {
      show_records = true;
    }
  }
  ImGui::EndMenuBar();
}

void RuntimeEditorDialog::renderBody(float footer)
{
  if (editor_session)
  {
    if (ImGui::BeginChild("##editor_body", ImVec2(0, -footer)))
    {
      if (selected_table.empty())
      {
        wrapped(LOC("EDITOR_UNAVAILABLE"));
      }
      else if (show_records || fields.empty())
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
}

void RuntimeEditorDialog::renderActions()
{
  muted(LOC((!pending_changes.empty() || formDirty()) ? "EDITOR_UNSAVED"
                                                      : "EDITOR_DRAFT_HINT"));
  ImGui::BeginDisabled(pending_changes.empty() && !formDirty());
  if (UI::primaryButton(LOC("EDITOR_APPLY"), ImVec2(0, 0),
                        UI::ButtonSize::COMPACT))
  {
    if (formDirty())
    {
      stage();
    }
    if (!pending_changes.empty() && error.empty())
    {
      request = Request::Apply;
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (UI::secondaryButton(LOC("EDITOR_CANCEL"), ImVec2(0, 0),
                          UI::ButtonSize::COMPACT))
  {
    close();
    ImGui::CloseCurrentPopup();
  }
  if (controller.canUndoEditor())
  {
    ImGui::SameLine();
    if (UI::secondaryButton(LOC("EDITOR_UNDO"), ImVec2(0, 0),
                            UI::ButtonSize::COMPACT))
    {
      popup.undo_confirm = true;
    }
  }
}

void RuntimeEditorDialog::renderConfirmation()
{
  if (popup.undo_confirm)
  {
    ImGui::OpenPopup("##editor_apply_confirm");
    popup.undo_confirm = false;
  }
  if (ImGui::BeginPopupModal("##editor_apply_confirm", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize))
  {
    wrapped(LOC("EDITOR_UNDO_CONFIRM"));
    if (UI::primaryButton(LOC("EDITOR_UNDO")))
    {
      request = Request::Undo;
      popup.undo_confirm = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (UI::secondaryButton(LOC("EDITOR_CANCEL")))
    {
      popup.undo_confirm = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
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
    popup.close_requested = true;
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
    if (!fields.at(i).readonly &&
        fieldDescriptor(fields.at(i).name) != nullptr &&
        (fields.at(i).type !=
             RuntimeEditor::storageType(original->values.at(i)) ||
         fields.at(i).text != RuntimeEditor::display(original->values.at(i))))
    {
      return true;
    }
  }
  return false;
}

void RuntimeEditorDialog::open(const EditorContext& context)
{
  if (popup.opened)
  {
    return;
  }
  open(std::nullopt);
  if (!editor_session)
  {
    return;
  }
  try
  {
    presentations[context.table] = context.editable_fields.empty()
                                       ? editorFields(context.table)
                                       : context.editable_fields;
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
    limitToGameplay();
    error.clear();
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
  catch (const RuntimeEditor::Error& failure)
  {
    error = failure.what();
  }
}

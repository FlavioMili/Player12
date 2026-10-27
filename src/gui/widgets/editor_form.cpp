// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/widgets/editor_form.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <nlohmann/json.hpp>
#include <set>

#include "database/runtime_editor.h"
#include "global/global.h"
#include "global/language_manager.h"
#include "gui/view_models/player_view.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"

namespace
{
constexpr float DETAILS_COLUMNS_WIDTH = 560.0F;
constexpr float COLOUR_CHANNEL_MAX = 255.0F;

void wrapped(const char* text)
{
  ImGui::PushTextWrapPos(0.0F);
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
}

void muted(const char* text)
{
  ImGui::PushStyleColor(ImGuiCol_Text,
                        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  // Wrap at the field's column, so hints never run under the next column.
  ImGui::PushTextWrapPos(0.0F);
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
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

int attributeColumns(const std::vector<AttributeGroup>& groups,
                     float rating_width)
{
  float label_width = 0.0F;
  for (const auto& group : groups)
  {
    for (const auto& [name, value] : group.stats)
    {
      const auto translated = PlayerView::statLabel(name);
      const auto label =
          translated == name ? EditorForm::humanLabel(name) : translated;
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
  wrapped(
      (translated == name ? EditorForm::humanLabel(name) : translated).c_str());
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

bool renderAttributes(EditorForm::Value& field)
{
  auto stats = nlohmann::json::parse(field.text, nullptr, false);
  if (!stats.is_object())
  {
    wrapped(LOC("EDITOR_ERROR"));
    return false;
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
  return changed;
}

void renderChoice(EditorForm::Value& field, const EditorField& descriptor)
{
  const auto text = [&descriptor](const std::string& label)
  { return descriptor.translate_choices ? LOC(label.c_str()) : label.c_str(); };
  const auto selected =
      std::ranges::find(descriptor.choices, field.text,
                        &std::pair<std::string, std::string>::first);
  if (const char* preview = selected == descriptor.choices.end()
                                ? LOC("EDITOR_NO_VALUE")
                                : text(selected->second);
      !ImGui::BeginCombo("##value", preview))
  {
    return;
  }
  for (std::size_t i = 0; i < descriptor.choices.size(); ++i)
  {
    const auto& [value, label] = descriptor.choices.at(i);
    ImGui::PushID(static_cast<int>(i));
    if (ImGui::Selectable(text(label), field.text == value))
    {
      field.text = value;
    }
    ImGui::PopID();
  }
  ImGui::EndCombo();
}

void renderNumber(EditorForm::Value& field, const EditorField& descriptor)
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

void renderOptionalNumber(EditorForm::Value& field,
                          const EditorField& descriptor)
{
  if (field.text.empty())
  {
    ImGui::AlignTextToFramePadding();
    muted(LOC("EDITOR_NO_VALUE"));
    ImGui::SameLine();
    if (ImGui::SmallButton(LOC("WORLD_BTN_SET")))
    {
      field.text = descriptor.initial;
    }
    return;
  }
  ImGui::SetNextItemWidth(
      -(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x));
  renderNumber(field, descriptor);
  ImGui::SameLine();
  if (ImGui::Button("\xc3\x97", ImVec2(ImGui::GetFrameHeight(), 0)))
  {
    field.text.clear();
  }
  if (ImGui::IsItemHovered())
  {
    ImGui::SetTooltip("%s", LOC("WORLD_BTN_CLEAR"));
  }
}

void renderColour(EditorForm::Value& field)
{
  unsigned int rgb = 0;
  if (field.text.size() == 7 && field.text.front() == '#')
  {
    std::sscanf(field.text.c_str() + 1, "%6x", &rgb);
  }
  std::array<float, 3> colour = {
      static_cast<float>((rgb >> 16U) & 0xFFU) / COLOUR_CHANNEL_MAX,
      static_cast<float>((rgb >> 8U) & 0xFFU) / COLOUR_CHANNEL_MAX,
      static_cast<float>(rgb & 0xFFU) / COLOUR_CHANNEL_MAX};
  constexpr auto FLAGS = static_cast<ImGuiColorEditFlags>(
      static_cast<unsigned>(ImGuiColorEditFlags_NoAlpha) |
      static_cast<unsigned>(ImGuiColorEditFlags_DisplayHex) |
      static_cast<unsigned>(ImGuiColorEditFlags_NoOptions));
  if (!ImGui::ColorEdit3("##value", colour.data(), FLAGS))
  {
    return;
  }
  const auto channel = [](float value)
  {
    return static_cast<unsigned>(
        std::lround(std::clamp(value, 0.0F, 1.0F) * COLOUR_CHANNEL_MAX));
  };
  std::array<char, 8> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "#%02X%02X%02X",
                channel(colour.at(0)), channel(colour.at(1)),
                channel(colour.at(2)));
  field.text = buffer.data();
}

bool renderToggle(EditorForm::Value& field, const EditorField& descriptor)
{
  bool enabled = field.text == "1";
  if (!ImGui::Checkbox(LOC(descriptor.label.c_str()), &enabled))
  {
    return false;
  }
  field.text = enabled ? "1" : "0";
  return true;
}

void renderSection(std::vector<EditorForm::Value>& values,
                   std::string_view selected_section,
                   const std::vector<EditorField>& descriptors, bool writable,
                   bool& changed)
{
  if (ImGui::BeginChild("##editor_fields", ImVec2(0, 0)))
  {
    ImGui::Spacing();
    ImGui::BeginDisabled(!writable);
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
      const auto field = std::ranges::find(values, descriptor.column,
                                           &EditorForm::Value::name);
      if (field == values.end() || field->readonly)
      {
        continue;
      }
      if (layout)
      {
        ImGui::TableNextColumn();
      }
      changed = EditorForm::renderField(*field, descriptor) || changed;
    }
    if (layout)
    {
      ImGui::EndTable();
    }
    ImGui::EndDisabled();
  }
  ImGui::EndChild();
}
}  // namespace

namespace EditorForm
{
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

bool renderField(Value& value, const EditorField& descriptor)
{
  using enum EditorField::Control;
  if (descriptor.control == Attributes)
  {
    return renderAttributes(value);
  }
  const std::string before = value.text;
  ImGui::PushID(value.name.c_str());
  if (descriptor.control != Toggle)
  {
    ImGui::TextUnformatted(LOC(descriptor.label.c_str()));
    ImGui::SetNextItemWidth(-1);
  }
  switch (descriptor.control)
  {
    case Choice:
      renderChoice(value, descriptor);
      break;
    case Number:
      if (descriptor.optional)
      {
        renderOptionalNumber(value, descriptor);
      }
      else
      {
        renderNumber(value, descriptor);
      }
      break;
    case Text:
      input("##value", value.text);
      break;
    case Colour:
      renderColour(value);
      break;
    case Toggle:
      renderToggle(value, descriptor);
      break;
    case Attributes:
      break;
  }
  if (!descriptor.hint.empty())
  {
    muted(LOC(descriptor.hint.c_str()));
  }
  ImGui::Dummy(ImVec2(0, Theme::Space::XS * Theme::scale()));
  ImGui::PopID();
  return value.text != before;
}

bool render(std::vector<Value>& values,
            const std::vector<EditorField>& descriptors, bool writable,
            const char* id)
{
  if (values.empty())
  {
    wrapped(LOC("EDITOR_UNAVAILABLE"));
    return false;
  }
  std::vector<std::string> sections;
  for (const auto& descriptor : descriptors)
  {
    if (const auto field =
            std::ranges::find(values, descriptor.column, &Value::name);
        field == values.end() || field->readonly)
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
    return false;
  }
  std::string selected_section;
  if (ImGui::BeginTabBar(id, ImGuiTabBarFlags_FittingPolicyScroll))
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
  bool changed = false;
  renderSection(values, selected_section, descriptors, writable, changed);
  return changed;
}
}  // namespace EditorForm

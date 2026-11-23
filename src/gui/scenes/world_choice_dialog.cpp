// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/world_choice_dialog.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>

#include "database/world/world_io.h"
#include "database/world/world_validate.h"
#include "global/language_manager.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"

namespace fs = std::filesystem;

namespace
{
constexpr const char* POPUP_ID = "###WorldChoiceDialog";
constexpr const char* REMOVE_ID = "##confirm_remove_world";
constexpr std::size_t MAX_PROBLEMS_SHOWN = 12;

/** Localisation keys of World::IssueCode, in declaration order. */
constexpr std::array<const char*, 26> ISSUE_KEYS = {
    "WORLD_ISSUE_NOT_JSON",
    "WORLD_ISSUE_TOO_LARGE",
    "WORLD_ISSUE_CANNOT_READ",
    "WORLD_ISSUE_NO_MANIFEST",
    "WORLD_ISSUE_NEWER_SCHEMA",
    "WORLD_ISSUE_NOT_A_LIST",
    "WORLD_ISSUE_NOT_AN_OBJECT",
    "WORLD_ISSUE_BAD_ID",
    "WORLD_ISSUE_DUPLICATE_ID",
    "WORLD_ISSUE_MISSING",
    "WORLD_ISSUE_WRONG_TYPE",
    "WORLD_ISSUE_OUT_OF_RANGE",
    "WORLD_ISSUE_BAD_CHOICE",
    "WORLD_ISSUE_BAD_COLOUR",
    "WORLD_ISSUE_BAD_REFERENCE",
    "WORLD_ISSUE_DUPLICATE_NAME",
    "WORLD_ISSUE_DUPLICATE_SHORT_NAME",
    "WORLD_ISSUE_PARENT_CYCLE",
    "WORLD_ISSUE_NO_PLAYABLE_LEAGUE",
    "WORLD_ISSUE_ODD_CLUB_COUNT",
    "WORLD_ISSUE_TOO_FEW_CLUBS",
    "WORLD_ISSUE_NO_TOP_DIVISION",
    "WORLD_ISSUE_SQUAD_TOO_LARGE",
    "WORLD_ISSUE_UNKNOWN_NATIONALITY",
    "WORLD_ISSUE_NO_COUNTRY",
    "WORLD_ISSUE_UNASSIGNED",
};

std::size_t count(const World::Library::Entry& entry, World::Kind kind)
{
  return entry.counts.at(static_cast<std::size_t>(kind));
}

/** Big clickable card with a title and a wrapped description. */
bool card(const char* id, const char* title, const char* body, ImVec2 size)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const bool clicked = ImGui::Button(id, size);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const float pad = 14.0f * scale;
  UI::drawTextFitted(drawList, ImVec2(start.x + pad, start.y + 10.0f * scale),
                     Theme::toU32(palette.text), title, size.x - 2.0f * pad);
  drawList->AddText(nullptr, 0.0f,
                    ImVec2(start.x + pad, start.y + 14.0f * scale +
                                              ImGui::GetTextLineHeight()),
                    Theme::toU32(palette.muted), body, nullptr,
                    size.x - 2.0f * pad);
  return clicked;
}
}  // namespace

std::string WorldChoiceDialog::issueText(const World::Issue& issue)
{
  std::string text;
  if (issue.kind)
  {
    text =
        std::string(LOC(std::string(World::kindLabel(*issue.kind)).c_str())) +
        ": ";
  }
  if (!issue.name.empty())
  {
    text += issue.name + " · ";
  }
  if (!issue.field.empty())
  {
    text += issue.field + " · ";
  }
  text += LOC(ISSUE_KEYS.at(static_cast<std::size_t>(issue.code)));
  if (!issue.detail.empty())
  {
    text += " (" + issue.detail + ")";
  }
  return text;
}

void WorldChoiceDialog::open(Mode new_mode, const fs::path& select_file)
{
  mode = new_mode;
  open_requested = true;
  close_requested = false;
  is_open = true;
  problems.clear();
  notice.clear();
  refresh();
  select(select_file);
}

void WorldChoiceDialog::refresh()
{
  entries = World::Library::list();
  const bool still_there = std::ranges::any_of(
      entries, [&](const auto& entry) { return entry.path == selected; });
  if (!still_there)
  {
    select({});
  }
}

void WorldChoiceDialog::select(const fs::path& path)
{
  selected = path;
  selection.reset();
  if (!path.empty())
  {
    selection = World::readFile(path);
  }
}

void WorldChoiceDialog::importFile(const fs::path& path)
{
  const World::Library::ImportResult imported =
      World::Library::importFile(path);
  notice.clear();
  problems.clear();
  if (imported.stored.empty())
  {
    problems = imported.load.issues;
    problems_title = fmt::sprintf(LOC("WORLD_CHOICE_IMPORT_FAILED"),
                                  World::pathToUtf8(path.filename()));
    return;
  }
  refresh();
  select(imported.stored);
  notice = fmt::sprintf(LOC("WORLD_CHOICE_IMPORTED"),
                        imported.load.document->manifest().name);
}

WorldChoiceDialog::Result WorldChoiceDialog::render(SDL_Window* window)
{
  Result result;
  const float scale = Theme::scale();
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  if (open_requested)
  {
    ImGui::OpenPopup(POPUP_ID);
    open_requested = false;
  }
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(
      ImVec2(std::min(760.0f * scale, viewport->WorkSize.x * 0.95f),
             std::min(660.0f * scale, viewport->WorkSize.y * 0.95f)),
      ImGuiCond_Always);
  const std::string title =
      std::string(LOC(mode == Mode::Choose ? "WORLD_CHOICE_TITLE"
                                           : "WORLD_CHOICE_MANAGE_TITLE")) +
      POPUP_ID;
  if (!ImGui::BeginPopupModal(title.c_str(), nullptr,
                              ImGuiWindowFlags_NoResize))
  {
    is_open = ImGui::IsPopupOpen(POPUP_ID);
    return result;
  }
  if (close_requested)
  {
    close_requested = false;
    is_open = false;
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return result;
  }
  const Theme::Palette& palette = Theme::palette();
  if (const auto picked = file_dialog.take())
  {
    importFile(*picked);
  }

  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.muted, "%s", LOC("WORLD_CHOICE_HELP"));
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  const float footer = UI::buttonHeight() + 16.0f * scale;
  if (ImGui::BeginChild("##world_choice_body", ImVec2(0.0f, -footer)))
  {
    if (mode == Mode::Choose)
    {
      renderCards(result);
    }
    renderLibrary(window, result);
    renderProblems();
    renderSummary();
  }
  ImGui::EndChild();

  ImGui::Separator();
  const bool can_start =
      mode == Mode::Choose && selection && selection->ok() &&
      World::Library::summarise(*selection->document, selection->issues)
              .blocking == 0;
  if (mode == Mode::Choose)
  {
    ImGui::BeginDisabled(!can_start);
    if (UI::primaryButton(LOC("WORLD_CHOICE_START")))
    {
      result = start();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
  }
  if (!removing.empty() && !ImGui::IsPopupOpen(REMOVE_ID))
  {
    ImGui::OpenPopup(REMOVE_ID);
  }
  const UI::DialogResult removal =
      UI::confirmDialog(REMOVE_ID, LOC("WORLD_CHOICE_REMOVE_TITLE"),
                        LOC("WORLD_CHOICE_REMOVE_BODY"),
                        LOC("WORLD_CHOICE_REMOVE"), LOC("SETTINGS_CANCEL"));
  if (removal == UI::DialogResult::CONFIRM)
  {
    World::Library::remove(removing);
    notice = LOC("WORLD_CHOICE_REMOVED");
    removing.clear();
    refresh();
  }
  else if (removal == UI::DialogResult::CANCEL)
  {
    removing.clear();
  }
  if (UI::secondaryButton(LOC(mode == Mode::Choose ? "SETTINGS_CANCEL"
                                                   : "WORLD_CHOICE_CLOSE")) ||
      (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && removing.empty() &&
       !file_dialog.busy()))
  {
    is_open = false;
    ImGui::CloseCurrentPopup();
  }
  if (result.action != Action::None)
  {
    is_open = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
  return result;
}

void WorldChoiceDialog::renderCards(Result& result)
{
  const float scale = Theme::scale();
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const ImVec2 size((ImGui::GetContentRegionAvail().x - gap) * 0.5f,
                    92.0f * scale);
  if (card("##default_world", LOC("WORLD_CHOICE_DEFAULT"),
           LOC("WORLD_CHOICE_DEFAULT_HELP"), size))
  {
    result.action = Action::DefaultWorld;
  }
  ImGui::SameLine();
  if (card("##edit_world", LOC("WORLD_CHOICE_EDIT"),
           LOC("WORLD_CHOICE_EDIT_HELP"), size))
  {
    result.action = Action::EditDefault;
  }
  ImGui::Spacing();
}

void WorldChoiceDialog::renderLibrary(SDL_Window* window, Result& result)
{
  const float scale = Theme::scale();
  const Theme::Palette& palette = Theme::palette();
  if (mode == Mode::Choose)
  {
    UI::sectionLabel(LOC("WORLD_CHOICE_DATABASE"));
  }
  if (file_dialog.busy())
  {
    ImGui::BeginDisabled();
    UI::secondaryButton(LOC("WORLD_CHOICE_IMPORT_WAIT"));
    ImGui::EndDisabled();
  }
  else if (UI::secondaryButton(LOC("WORLD_CHOICE_IMPORT")))
  {
    file_dialog.open(window);
  }
  ImGui::SameLine();
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(palette.muted, "%s", LOC("WORLD_CHOICE_DROP"));
  if (entries.empty())
  {
    ImGui::TextColored(palette.muted, "%s", LOC("WORLD_CHOICE_EMPTY"));
    return;
  }
  const float height =
      std::min(static_cast<float>(entries.size()), 3.0f) * 62.0f * scale;
  if (ImGui::BeginChild("##world_list", ImVec2(0.0f, height), true))
  {
    for (const auto& entry : entries)
    {
      renderEntry(entry, result);
    }
  }
  ImGui::EndChild();
}

void WorldChoiceDialog::renderEntry(const World::Library::Entry& entry,
                                    Result& result)
{
  const Theme::Palette& palette = Theme::palette();
  const bool usable = entry.errors == 0;
  const bool playable = usable && entry.blocking == 0;
  const std::string id = World::pathToUtf8(entry.path);
  ImGui::PushID(id.c_str());
  ImGui::BeginGroup();
  std::string label = entry.manifest.name;
  if (!entry.manifest.version.empty())
  {
    label += "  " + entry.manifest.version;
  }
  if (!entry.manifest.author.empty())
  {
    label += "  " + fmt::sprintf(LOC("WORLD_CHOICE_BY"), entry.manifest.author);
  }
  if (ImGui::Selectable(label.c_str(), entry.path == selected,
                        ImGuiSelectableFlags_None,
                        ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
  {
    select(entry.path);
  }
  if (usable)
  {
    ImGui::TextColored(
        palette.muted, "%s",
        fmt::sprintf(LOC("WORLD_CHOICE_COUNTS"),
                     static_cast<int>(count(entry, World::Kind::Country)),
                     static_cast<int>(count(entry, World::Kind::League)),
                     static_cast<int>(count(entry, World::Kind::Club)),
                     static_cast<int>(count(entry, World::Kind::Player)))
            .c_str());
  }
  if (!playable)
  {
    ImGui::TextColored(
        palette.negative, "%s",
        fmt::sprintf(
            LOC(usable ? "WORLD_CHOICE_INCOMPLETE" : "WORLD_CHOICE_INVALID"),
            static_cast<int>(usable ? entry.blocking : entry.errors))
            .c_str());
    if (mode == Mode::Choose)
    {
      ImGui::SameLine();
      if (UI::secondaryButton(LOC("WORLD_CHOICE_OPEN_EDITOR"),
                              ImVec2(0.0f, 0.0f), UI::ButtonSize::COMPACT))
      {
        const World::LoadResult loaded = World::readFile(entry.path);
        if (loaded.document)
        {
          result.action = Action::EditFile;
          result.file = entry.path;
          result.world =
              std::make_shared<const World::Document>(*loaded.document);
        }
      }
    }
  }
  ImGui::EndGroup();
  ImGui::PopID();
}

void WorldChoiceDialog::renderSummary()
{
  const Theme::Palette& palette = Theme::palette();
  if (!notice.empty())
  {
    ImGui::TextColored(palette.positive, "%s", notice.c_str());
  }
  if (!selection || !selection->document)
  {
    return;
  }
  const World::Library::Entry entry =
      World::Library::summarise(*selection->document, selection->issues);
  ImGui::Separator();
  {
    Theme::ScopedText heading(Theme::Text::HEADING);
    ImGui::TextUnformatted(entry.manifest.name.c_str());
  }
  if (!entry.manifest.author.empty() || !entry.manifest.version.empty())
  {
    ImGui::TextColored(palette.muted, "%s %s", entry.manifest.author.c_str(),
                       entry.manifest.version.c_str());
  }
  ImGui::TextUnformatted(
      fmt::sprintf(LOC("WORLD_CHOICE_COUNTS"),
                   static_cast<int>(count(entry, World::Kind::Country)),
                   static_cast<int>(count(entry, World::Kind::League)),
                   static_cast<int>(count(entry, World::Kind::Club)),
                   static_cast<int>(count(entry, World::Kind::Player)))
          .c_str());
  if (!entry.manifest.description.empty())
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.muted, "%s", entry.manifest.description.c_str());
    ImGui::PopTextWrapPos();
  }
  if (entry.warnings > 0)
  {
    ImGui::TextColored(palette.warning, "%s",
                       fmt::sprintf(LOC("WORLD_CHOICE_WARNINGS"),
                                    static_cast<int>(entry.warnings))
                           .c_str());
    std::size_t shown = 0;
    for (const World::Issue& issue : selection->issues)
    {
      if (issue.severity != World::Severity::Warning)
      {
        continue;
      }
      if (++shown > MAX_PROBLEMS_SHOWN)
      {
        break;
      }
      ImGui::Bullet();
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(palette.warning, "%s", issueText(issue).c_str());
      ImGui::PopTextWrapPos();
    }
  }
  if (mode == Mode::Manage && UI::dangerButton(LOC("WORLD_CHOICE_REMOVE")))
  {
    removing = selected;
  }
}

void WorldChoiceDialog::renderProblems() const
{
  if (problems.empty())
  {
    return;
  }
  const Theme::Palette& palette = Theme::palette();
  ImGui::Separator();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.negative, "%s", problems_title.c_str());
  std::size_t shown = 0;
  for (const World::Issue& issue : problems)
  {
    if (shown++ >= MAX_PROBLEMS_SHOWN)
    {
      break;
    }
    ImGui::Bullet();
    ImGui::TextColored(palette.negative, "%s", issueText(issue).c_str());
  }
  if (problems.size() > MAX_PROBLEMS_SHOWN)
  {
    ImGui::TextColored(
        palette.muted, "%s",
        fmt::sprintf(LOC("WORLD_CHOICE_MORE"),
                     static_cast<int>(problems.size() - MAX_PROBLEMS_SHOWN))
            .c_str());
  }
  ImGui::PopTextWrapPos();
}

WorldChoiceDialog::Result WorldChoiceDialog::start()
{
  Result result;
  // Read the file again: what starts is what is on disk now.
  World::LoadResult loaded = World::readFile(selected);
  problems = loaded.issues;
  if (loaded.document)
  {
    for (World::Issue& issue : World::playability(*loaded.document))
    {
      problems.push_back(std::move(issue));
    }
  }
  std::erase_if(problems, [](const World::Issue& issue)
                { return issue.severity == World::Severity::Warning; });
  if (!loaded.ok() || !problems.empty())
  {
    problems_title = LOC("WORLD_CHOICE_START_FAILED");
    return result;
  }
  result.action = Action::Start;
  result.file = selected;
  result.world =
      std::make_shared<const World::Document>(std::move(*loaded.document));
  return result;
}

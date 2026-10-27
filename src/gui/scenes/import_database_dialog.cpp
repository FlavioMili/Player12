// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/import_database_dialog.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <SDL3/SDL_dialog.h>

#include <algorithm>
#include <filesystem>

#include "global/language_manager.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"

namespace fs = std::filesystem;

namespace
{
constexpr const char* POPUP_ID = "###ImportDatabaseDialog";
constexpr int MAX_ISSUES_SHOWN = 20;

bool isDatabaseFile(const fs::directory_entry& entry)
{
  return entry.is_regular_file() &&
         entry.path().extension() == UserDatabase::FILE_EXTENSION;
}
}  // namespace

void ImportDatabaseDialog::render(SDL_Window* window)
{
  const float scale = Theme::scale();
  const Theme::Palette& palette = Theme::palette();
  if (open_requested)
  {
    refresh();
    ImGui::SetNextWindowSize(ImVec2(640.0f * scale, 540.0f * scale),
                             ImGuiCond_Always);
    ImGui::OpenPopup(POPUP_ID);
    open_requested = false;
  }
  if (close_requested)
  {
    ImGui::CloseCurrentPopup();
    close_requested = false;
    return;
  }
  if (!ImGui::BeginPopupModal(POPUP_ID, nullptr, ImGuiWindowFlags_None)) return;

  // A file the system picker returned is imported here, on the UI thread.
  if (has_pickup_)
  {
    has_pickup_ = false;
    importPicked(pending_pickup_);
    pending_pickup_.clear();
  }

  {
    Theme::ScopedText heading(Theme::Text::HEADING);
    ImGui::TextUnformatted(LOC("DB_IMPORT_TITLE"));
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.muted, "%s", LOC("DB_IMPORT_HELP"));
  ImGui::PopTextWrapPos();
  ImGui::Separator();

  const std::optional<std::string> active = UserDatabase::activeName();
  if (active)
  {
    ImGui::TextColored(palette.positive, "%s %s", LOC("DB_IMPORT_ACTIVE"),
                       active->c_str());
  }
  else
  {
    ImGui::TextColored(palette.muted, "%s: %s", LOC("DB_IMPORT_ACTIVE"),
                       LOC("DB_IMPORT_NONE"));
  }
  ImGui::SameLine();
  if (browser_open_)
  {
    ImGui::BeginDisabled();
    UI::secondaryButton(LOC("DB_IMPORT_WAIT"));
    ImGui::EndDisabled();
  }
  else if (UI::secondaryButton(LOC("DB_IMPORT_FILE")))
  {
    beginBrowse(window);
  }

  renderFileList(scale);
  renderDetail(scale);

  if (!status_message_.empty())
  {
    ImGui::TextColored(status_is_error_ ? palette.negative : palette.positive,
                       "%s", status_message_.c_str());
  }
  if (UI::secondaryButton(LOC("DB_IMPORT_CLOSE")) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    close();
  ImGui::EndPopup();
}

void ImportDatabaseDialog::refresh()
{
  entries_.clear();
  const fs::path dir = UserDatabase::databasesDir();
  std::error_code error;
  if (fs::exists(dir, error))
  {
    for (const auto& entry : fs::directory_iterator(dir, error))
    {
      if (!isDatabaseFile(entry)) continue;
      const std::string file = entry.path().filename().string();
      std::string title = file;
      const UserDatabase::LoadResult loaded =
          UserDatabase::loadFile(entry.path());
      if (loaded.ok && !loaded.content.manifest.name.empty())
      {
        title = loaded.content.manifest.name;
        if (!loaded.content.manifest.version.empty())
          title += " " + loaded.content.manifest.version;
      }
      else if (!loaded.ok)
      {
        title += " (!)";
      }
      entries_.push_back({file, title, false});
    }
  }
  std::ranges::sort(entries_, {}, &Entry::title);
  const std::optional<std::string> active = UserDatabase::activeName();
  for (auto& entry : entries_) entry.active = active && entry.file == *active;
  const bool still_there =
      std::ranges::any_of(entries_, [&](const Entry& entry)
                          { return entry.file == selected_; });
  if (!still_there)
  {
    selected_.clear();
    has_result_ = false;
    const auto active_entry =
        std::ranges::find_if(entries_, [](const Entry& entry)
                             { return entry.active; });
    if (active_entry != entries_.end())
      select(active_entry->file);
    else if (!entries_.empty())
      select(entries_.front().file);
  }
  else if (!selected_.empty())
  {
    select(selected_);
  }
}

void ImportDatabaseDialog::select(const std::string& file)
{
  selected_ = file;
  has_result_ = false;
  result_ = UserDatabase::LoadResult{};
  if (file.empty()) return;
  result_ = UserDatabase::loadFile(UserDatabase::databasesDir() / file);
  has_result_ = true;
}

void ImportDatabaseDialog::renderFileList(float scale)
{
  const Theme::Palette& palette = Theme::palette();
  if (entries_.empty())
  {
    ImGui::TextColored(palette.muted, "%s", LOC("DB_IMPORT_NO_FILES"));
    return;
  }
  if (ImGui::BeginChild("###DbFileList", ImVec2(0.0f, 150.0f * scale), true))
  {
    for (const auto& entry : entries_)
    {
      const bool is_selected = entry.file == selected_;
      if (entry.active) ImGui::PushStyleColor(ImGuiCol_Text, palette.accent);
      if (ImGui::Selectable(entry.title.c_str(), is_selected))
        select(entry.file);
      if (entry.active) ImGui::PopStyleColor();
    }
  }
  ImGui::EndChild();
}

void ImportDatabaseDialog::renderDetail(float scale)
{
  const Theme::Palette& palette = Theme::palette();
  if (ImGui::BeginChild("###DbDetail", ImVec2(0.0f, -40.0f * scale), true))
  {
    if (!has_result_)
    {
      ImGui::TextColored(palette.muted, "%s", LOC("DB_IMPORT_SELECT_HINT"));
    }
    else
    {
      const auto& manifest = result_.content.manifest;
      if (!manifest.name.empty())
      {
        Theme::ScopedText title(Theme::Text::TITLE);
        ImGui::TextUnformatted(manifest.name.c_str());
      }
      if (!manifest.author.empty() || !manifest.version.empty())
      {
        ImGui::TextColored(palette.muted, "%s %s", manifest.author.c_str(),
                           manifest.version.c_str());
      }
      if (result_.ok || !result_.content.clubs.is_null())
      {
        ImGui::TextUnformatted(
            fmt::sprintf(LOC("DB_IMPORT_SUMMARY"),
                         static_cast<int>(result_.content.leagues.size()),
                         static_cast<int>(result_.content.clubs.size()),
                         static_cast<int>(result_.content.players.size()))
                .c_str());
      }
      int errors = 0;
      int warnings = 0;
      for (const auto& issue : result_.issues)
        (issue.is_error ? errors : warnings)++;
      if (errors > 0 || warnings > 0)
      {
        ImGui::TextColored(
            errors > 0 ? palette.negative : palette.warning, "%s",
            fmt::sprintf(LOC("DB_IMPORT_FINDINGS"), errors, warnings).c_str());
        int shown = 0;
        for (const auto& issue : result_.issues)
        {
          if (shown >= MAX_ISSUES_SHOWN) break;
          ++shown;
          std::string label = "[" + issue.section;
          if (issue.index >= 0) label += " #" + std::to_string(issue.index);
          label += "] " + issue.message;
          ImGui::Bullet();
          ImGui::SameLine();
          ImGui::TextColored(
              issue.is_error ? palette.negative : palette.warning, "%s",
              label.c_str());
        }
        if (static_cast<int>(result_.issues.size()) > shown)
        {
          ImGui::TextColored(palette.muted, "... (%d)",
                             static_cast<int>(result_.issues.size()) - shown);
        }
      }
      ImGui::Spacing();
      const bool is_active = [&]
      {
        const auto active = UserDatabase::activeName();
        return active && *active == selected_;
      }();
      if (result_.ok && !is_active &&
          UI::primaryButton(LOC("DB_IMPORT_ACTIVATE")))
      {
        UserDatabase::setActive(selected_);
        status_message_ = LOC("DB_IMPORT_ACTIVATED");
        status_is_error_ = false;
        refresh();
      }
      if (!selected_.empty())
      {
        ImGui::SameLine();
        if (UI::dangerButton(LOC("DB_IMPORT_REMOVE")))
        {
          std::error_code error;
          fs::remove(UserDatabase::databasesDir() / selected_, error);
          if (is_active) UserDatabase::clearActive();
          status_message_ = LOC("DB_IMPORT_REMOVED");
          status_is_error_ = false;
          selected_.clear();
          has_result_ = false;
          refresh();
        }
      }
    }
  }
  ImGui::EndChild();
}

void ImportDatabaseDialog::beginBrowse(SDL_Window* window)
{
  static const SDL_DialogFileFilter filters[] = {{"Custom database", "fmdb"}};
  browser_open_ = true;
  SDL_ShowOpenFileDialog(&ImportDatabaseDialog::fileDialogCallback, this,
                         window, filters, 1, nullptr, false);
}

void ImportDatabaseDialog::fileDialogCallback(void* userdata,
                                              const char* const* filelist,
                                              int /*filter*/)
{
  auto* self = static_cast<ImportDatabaseDialog*>(userdata);
  self->browser_open_ = false;
  if (filelist != nullptr && filelist[0] != nullptr)
  {
    self->pending_pickup_ = filelist[0];
    self->has_pickup_ = true;
  }
}

void ImportDatabaseDialog::importPicked(const std::string& path)
{
  std::vector<UserDatabase::Issue> issues;
  std::string imported_name;
  if (UserDatabase::importFile(path, issues, imported_name))
  {
    status_message_ =
        std::string(LOC("DB_IMPORT_IMPORTED")) + " " + imported_name;
    status_is_error_ = false;
    refresh();
    select(imported_name);
  }
  else
  {
    std::string detail;
    for (const auto& issue : issues)
    {
      if (!issue.is_error) continue;
      detail = issue.section + ": " + issue.message;
      break;
    }
    status_message_ = std::string(LOC("DB_IMPORT_FAILED")) + " " + detail;
    status_is_error_ = true;
  }
}

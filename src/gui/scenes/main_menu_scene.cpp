// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/main_menu_scene.h"

#include <SDL3/SDL_misc.h>
#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <string>
#include <utility>

#include "global/build_info.h"
#include "global/language_manager.h"
#include "global/logger.h"
#include "gui/gui_view.h"
#include "gui/scenes/about_scene.h"
#include "gui/scenes/main_game_scene.h"
#include "gui/scenes/settings_scene.h"
#include "gui/scenes/team_selection_scene.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/inbox.h"

SceneID MainMenuScene::getID() const { return SceneID::MAIN_MENU; }

MainMenuScene::MainMenuScene(GUIView* guiView_ptr) : GUIScene(guiView_ptr) {}

namespace
{
std::string backupLabel(const SaveBackup& backup)
{
  const SaveInspection& info = backup.inspection;
  const std::string club = info.club_name.empty()
                               ? std::string(LOC("MENU_SAVE_SLOT_NOT_STARTED"))
                               : info.club_name;
  return backup.kind == SaveBackup::Kind::PreMigration
             ? formatLocalized("SAVE_BACKUP_PRE_UPGRADE",
                               {std::to_string(backup.index)})
             : formatLocalized(
                   "SAVE_BACKUP_ENTRY",
                   {std::to_string(backup.index), club, info.game_date});
}

/** file:// URL of a local folder, for the system file manager. */
std::string folderUrl(const std::filesystem::path& folder)
{
  std::string url = "file://";
  const std::string path = folder.generic_string();
  if (!path.starts_with('/')) url += '/';
  for (const char character : path)
  {
    if (character == ' ')
      url += "%20";
    else
      url += character;
  }
  return url;
}
}  // namespace

void MainMenuScene::onEnter()
{
  loadCachedMetadata();
  crash_notice = CrashReport::pending();
  if (crash_notice)
  {
    crash_notice_text =
        formatLocalized("CRASH_NOTICE_BODY", {crash_notice->report.string()});
    crash_notice_requested = true;
  }
  // A language file that could not be read (or lacks texts) falls back to
  // English; say so instead of leaving the player to guess.
  const LanguageManager::Status& language =
      LanguageManager::instance().status();
  language_notice.clear();
  if (language.file_failed)
  {
    const auto name = languageToString.find(language.requested);
    language_notice = fmt::sprintf(
        LOC("LANGUAGE_FALLBACK_NOTICE"),
        name == languageToString.end() ? std::string("?") : name->second);
  }
  else if (language.missing_keys > 0)
  {
    language_notice = fmt::sprintf(LOC("LANGUAGE_PARTIAL_NOTICE"),
                                   static_cast<int>(language.missing_keys));
  }
}

void MainMenuScene::loadCachedMetadata()
{
  cached_metadata.clear();
  latest_career_slot = 0;
  std::string latestKey;
  for (int i = 1; i <= 3; ++i)
  {
    cached_metadata.push_back(guiView->getController().getSaveSlotMetadata(i));
    const auto& metadata = cached_metadata.back();
    // real_date is "dd/mm/YYYY HH:MM"; reorder it into a sortable key.
    if (!metadata.exists || metadata.team_name.empty() ||
        metadata.real_date.size() < 16)
      continue;
    const std::string& date = metadata.real_date;
    const std::string key = date.substr(6, 4) + date.substr(3, 2) +
                            date.substr(0, 2) + date.substr(11, 5);
    if (key > latestKey)
    {
      latestKey = key;
      latest_career_slot = i;
    }
  }
}

void MainMenuScene::startSlot(int slot, bool newGame)
{
  is_new_game = newGame;
  loading_slot = slot;
  is_loading_rendered = false;
}

void MainMenuScene::update(float deltaTime)
{
  (void)deltaTime;

  if (loading_slot > 0 && is_loading_rendered)
  {
    if (!loading_operation_started)
    {
      const int slot = loading_slot;
      const bool createNewGame = is_new_game;
      const std::filesystem::path restore = pending_restore;
      pending_restore.clear();
      GameController* controller = &guiView->getController();
      loading_operation = launchBackgroundTask(
          loading_worker,
          [controller, slot, createNewGame, restore]()
          {
            if (createNewGame)
            {
              controller->newGame(slot);
              return true;
            }
            if (!restore.empty())
              return controller->restoreBackup(slot, restore);
            return controller->loadGame(slot);
          });
      loading_operation_started = true;
      return;
    }

    if (loading_operation.wait_for(std::chrono::seconds::zero()) !=
        std::future_status::ready)
    {
      return;
    }

    bool succeeded = false;
    try
    {
      succeeded = loading_operation.get();
    }
    catch (const std::exception& exception)
    {
      Logger::error(std::format("Failed to initialize slot {}: {}",
                                loading_slot, exception.what()));
    }
    loading_operation_started = false;

    if (succeeded)
    {
      auto gameScene = std::make_unique<MainGameScene>(guiView);
      changeScene(std::move(gameScene));
    }
    else
    {
      Logger::error(
          std::format("Failed to load game from slot {}", loading_slot));
      // Explain why and offer the slot's backups.
      const auto& error = guiView->getController().getLastLoadError();
      showLoadError(
          loading_slot,
          formatLocalized(
              error ? error->langKey() : "SAVE_ERROR_IO",
              {std::to_string(error ? error->found_version : 0),
               std::to_string(error ? error->supported_version : 0)}));
      loading_slot = 0;
      loadCachedMetadata();
    }
  }
}

void MainMenuScene::showLoadError(int slot, std::string text)
{
  load_error_text = std::move(text);
  load_error_slot = slot;
  load_error_requested = true;
  // Offer the newest backup that loads straight away.
  recovery_backup.clear();
  const std::vector<SaveBackup> slotBackups =
      guiView->getController().getSaveBackups(slot);
  if (const SaveBackup* usable = SaveManager::newestUsable(slotBackups))
  {
    recovery_backup = usable->path;
    recovery_text =
        formatLocalized("SAVE_RECOVERY_OFFER", {backupLabel(*usable)});
  }
}

void MainMenuScene::renderBackdrop() const
{
  // Procedural art: a soft vertical gradient and a large, faint pitch drawn
  // in perspective behind the menu.
  const Theme::Palette& palette = Theme::palette();
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImVec2 min = viewport->WorkPos;
  const ImVec2 size = viewport->WorkSize;
  const ImVec2 max(min.x + size.x, min.y + size.y);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImU32 top = Theme::toU32(palette.background);
  const ImVec4 tinted(palette.background.x * 0.55f + palette.accent.x * 0.12f,
                      palette.background.y * 0.55f + palette.accent.y * 0.12f,
                      palette.background.z * 0.55f + palette.accent.z * 0.12f,
                      1.0f);
  const ImU32 bottom = Theme::toU32(tinted);
  drawList->AddRectFilledMultiColor(min, max, top, top, bottom, bottom);

  const float scale = Theme::scale();
  const ImU32 line = Theme::toU32(palette.text, 0.09f);
  const float horizon = min.y + size.y * 0.42f;
  const float nearY = max.y + size.y * 0.05f;
  const float farHalf = size.x * 0.30f;
  const float nearHalf = size.x * 0.75f;
  const float centerX = min.x + size.x * 0.62f;
  const auto at = [&](float u, float v)
  {
    // u across the pitch (-1..1), v from far (0) to near (1).
    const float half = std::lerp(farHalf, nearHalf, v);
    return ImVec2(centerX + u * half, horizon + (nearY - horizon) * v);
  };
  const float thickness = 2.0f * scale;
  const std::array<ImVec2, 4> outline = {at(-1, 0), at(1, 0), at(1, 1),
                                         at(-1, 1)};
  drawList->AddPolyline(outline.data(), 4, line, ImDrawFlags_Closed, thickness);
  drawList->AddLine(at(-1, 0.45f), at(1, 0.45f), line, thickness);
  std::array<ImVec2, 48> circle{};
  for (size_t index = 0; index < circle.size(); ++index)
  {
    const float angle =
        static_cast<float>(index) / static_cast<float>(circle.size()) * 6.2832f;
    circle[index] = at(std::cos(angle) * 0.18f, 0.45f + std::sin(angle) * 0.1f);
  }
  drawList->AddPolyline(circle.data(), static_cast<int>(circle.size()), line,
                        ImDrawFlags_Closed, thickness);
  const std::array<ImVec2, 4> box = {at(-0.4f, 0), at(-0.4f, 0.12f),
                                     at(0.4f, 0.12f), at(0.4f, 0)};
  drawList->AddPolyline(box.data(), 4, line, ImDrawFlags_None, thickness);
  const std::array<ImVec2, 4> nearBox = {at(-0.4f, 1), at(-0.4f, 0.8f),
                                         at(0.4f, 0.8f), at(0.4f, 1)};
  drawList->AddPolyline(nearBox.data(), 4, line, ImDrawFlags_None, thickness);
  // Accent band along the left edge anchors the title block.
  drawList->AddRectFilled(min, ImVec2(min.x + 6.0f * scale, max.y),
                          Theme::toU32(palette.accent));
}

void MainMenuScene::render()
{
  const float scale = Theme::scale();
  const Theme::Palette& palette = Theme::palette();
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::Begin("##main_menu", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar(2);
  renderBackdrop();

  const ImVec2 origin(viewport->WorkPos.x +
                          std::max(48.0f * scale, viewport->WorkSize.x * 0.09f),
                      viewport->WorkPos.y + viewport->WorkSize.y * 0.2f);
  if (loading_slot > 0)
  {
    {
      const char* message =
          LOC(is_new_game ? "MENU_LOADING_INITIALIZING" : "MENU_LOADING_LOAD");
      Theme::ScopedText title(Theme::Text::TITLE);
      ImGui::SetCursorScreenPos(ImVec2(origin.x, viewport->GetCenter().y));
      ImGui::TextUnformatted(message);
    }
    if (!Theme::reducedMotion())
      ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()),
                         ImVec2(320.0f * scale, 6.0f * scale), "");
    ImGui::End();
    is_loading_rendered = true;
    return;
  }

  ImGui::SetCursorScreenPos(origin);
  ImGui::BeginGroup();
  {
    ImGui::PushFont(nullptr, Theme::textSize(Theme::Text::DISPLAY) * 1.7f);
    ImGui::TextUnformatted(LOC("MENU_TITLE"));
    ImGui::PopFont();
  }
  {
    Theme::ScopedText body(Theme::Text::TITLE);
    ImGui::TextColored(palette.muted, "%s", LOC("MENU_TAGLINE"));
  }
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XL * scale));

  const ImVec2 buttonSize(340.0f * scale, 50.0f * scale);
  ImGui::PushFont(nullptr, Theme::textSize(Theme::Text::TITLE));
  if (latest_career_slot > 0)
  {
    const auto& latest =
        cached_metadata[static_cast<size_t>(latest_career_slot - 1)];
    if (const std::string label =
            std::string(LOC("MENU_CONTINUE_CAREER")) + "###continue_career";
        UI::primaryButton(label.c_str(),
                          ImVec2(buttonSize.x, buttonSize.y * 1.35f)))
      startSlot(latest_career_slot, false);
    Theme::ScopedText small(Theme::Text::SMALL);
    ImGui::TextColored(
        palette.muted, "%s",
        fmt::sprintf(LOC("MENU_CONTINUE_DETAIL"), latest.team_name,
                     localizedDate(latest.game_date))
            .c_str());
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale));
  }
  if (const bool firstRun = latest_career_slot == 0;
      firstRun ? UI::primaryButton(LOC("MENU_NEW_GAME"), buttonSize)
               : UI::secondaryButton(LOC("MENU_NEW_GAME"), buttonSize))
  {
    slot_picker_requested = true;
    is_new_game = true;
  }
  const bool anySave = std::ranges::any_of(
      cached_metadata, [](const auto& metadata) { return metadata.exists; });
  ImGui::BeginDisabled(!anySave);
  if (UI::secondaryButton(LOC("MENU_LOAD_GAME"), buttonSize))
  {
    slot_picker_requested = true;
    is_new_game = false;
  }
  ImGui::EndDisabled();
  if (UI::secondaryButton(LOC("MENU_SETTINGS"), buttonSize))
    changeScene(std::make_unique<SettingsScene>(guiView));
  if (UI::secondaryButton(LOC("MENU_ABOUT"), buttonSize))
    changeScene(std::make_unique<AboutScene>(guiView, false));
  if (UI::secondaryButton(LOC("MENU_QUIT"), buttonSize)) quit();
  ImGui::PopFont();
  renderSlotPicker();
  renderLoadError();
  renderBackups();
  renderCrashNotice();
  ImGui::EndGroup();
  renderVersion();
  renderLanguageNotice();

  ImGui::End();
}

void MainMenuScene::renderLanguageNotice() const
{
  if (language_notice.empty()) return;
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float margin = Theme::Space::L * Theme::scale();
  Theme::ScopedText small(Theme::Text::SMALL);
  // Bottom left, clear of the version label in the other corner.
  const float width = std::max(viewport->WorkSize.x * 0.5f - margin, 1.0f);
  const float height =
      ImGui::CalcTextSize(language_notice.c_str(), nullptr, false, width).y;
  ImGui::SetCursorScreenPos(
      ImVec2(viewport->WorkPos.x + margin,
             viewport->WorkPos.y + viewport->WorkSize.y - height - margin));
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
  ImGui::TextColored(Theme::palette().warning, "%s", language_notice.c_str());
  ImGui::PopTextWrapPos();
}

void MainMenuScene::renderVersion() const
{
  static const std::string label = "v" + std::string(BuildInfo::version());
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const float margin = Theme::Space::L * Theme::scale();
  Theme::ScopedText small(Theme::Text::SMALL);
  const ImVec2 size = ImGui::CalcTextSize(label.c_str());
  ImGui::SetCursorScreenPos(
      ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - size.x - margin,
             viewport->WorkPos.y + viewport->WorkSize.y - size.y - margin));
  ImGui::TextColored(Theme::palette().faint, "%s", label.c_str());
}

void MainMenuScene::renderCrashNotice()
{
  if (!crash_notice) return;
  if (crash_notice_requested)
  {
    ImGui::OpenPopup("##crash_notice");
    crash_notice_requested = false;
  }
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal(
          "##crash_notice", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    return;
  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    ImGui::TextUnformatted(LOC("CRASH_NOTICE_TITLE"));
  }
  ImGui::PushTextWrapPos(460.0f * Theme::scale());
  ImGui::TextColored(Theme::palette().muted, "%s", crash_notice_text.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  const bool dismissed = UI::primaryButton(LOC("CRASH_NOTICE_OK"));
  ImGui::SameLine();
  if (UI::secondaryButton(LOC("CRASH_NOTICE_OPEN_FOLDER")) &&
      !SDL_OpenURL(folderUrl(crash_notice->report.parent_path()).c_str()))
    Logger::warn(std::string("Could not open the reports folder: ") +
                 SDL_GetError());
  if (dismissed || ImGui::IsKeyPressed(ImGuiKey_Escape))
  {
    CrashReport::acknowledge();
    crash_notice.reset();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void MainMenuScene::openBackups(int slot)
{
  backups_slot = slot;
  backups = guiView->getController().getSaveBackups(slot);
  backup_labels.clear();
  backup_labels.reserve(backups.size());
  for (const SaveBackup& backup : backups)
    backup_labels.push_back(backupLabel(backup));
  backups_requested = true;
}

void MainMenuScene::renderSlotActions(
    int slot, const GameController::SaveSlotMetadata& metadata)
{
  if (!metadata.exists) return;
  ImGui::PushID(slot);
  ImGui::SameLine();
  ImGui::BeginGroup();
  ImGui::BeginDisabled(metadata.backups == 0);
  if (UI::secondaryButton(LOC("SAVE_BACKUPS_TITLE"), ImVec2(0.0f, 0.0f),
                          UI::ButtonSize::COMPACT))
    openBackups(slot);
  ImGui::EndDisabled();
  // The career currently held in memory cannot be deleted under it.
  const bool loaded = guiView->getController().getCurrentSlot() == slot;
  ImGui::BeginDisabled(loaded);
  const bool deleteClicked = UI::dangerButton(
      LOC("SAVE_DELETE"), ImVec2(0.0f, 0.0f), UI::ButtonSize::COMPACT);
  ImGui::EndDisabled();
  if (deleteClicked)
  {
    delete_slot = slot;
    delete_text =
        formatLocalized("SAVE_DELETE_BODY",
                        {std::to_string(slot),
                         metadata.team_name.empty()
                             ? std::string(LOC("MENU_SAVE_SLOT_NOT_STARTED"))
                             : metadata.team_name});
  }
  ImGui::EndGroup();
  ImGui::PopID();
}

void MainMenuScene::renderLoadError()
{
  if (load_error_requested)
  {
    ImGui::OpenPopup("##load_error");
    load_error_requested = false;
  }
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal(
          "##load_error", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    return;
  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    ImGui::TextUnformatted(LOC("SAVE_LOAD_FAILED_TITLE"));
  }
  ImGui::PushTextWrapPos(420.0f * Theme::scale());
  ImGui::TextColored(Theme::palette().muted, "%s", load_error_text.c_str());
  const bool canRecover = !recovery_backup.empty();
  if (canRecover)
  {
    ImGui::Spacing();
    ImGui::TextWrapped("%s", recovery_text.c_str());
  }
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  if (canRecover && UI::primaryButton(LOC("SAVE_RECOVERY_RESTORE")))
  {
    // The damaged file is kept aside by the restore, never deleted.
    pending_restore = recovery_backup;
    recovery_backup.clear();
    startSlot(load_error_slot, false);
    ImGui::CloseCurrentPopup();
  }
  if (canRecover) ImGui::SameLine();
  const bool hasBackups =
      load_error_slot > 0 &&
      static_cast<size_t>(load_error_slot) <= cached_metadata.size() &&
      cached_metadata[static_cast<size_t>(load_error_slot - 1)].backups > 0;
  if (hasBackups && (canRecover ? UI::secondaryButton(LOC("SAVE_BACKUPS_TITLE"))
                                : UI::primaryButton(LOC("SAVE_BACKUPS_TITLE"))))
  {
    openBackups(load_error_slot);
    ImGui::CloseCurrentPopup();
  }
  if (hasBackups) ImGui::SameLine();
  if (UI::secondaryButton(LOC("TALK_CLOSE")) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape))
    ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

void MainMenuScene::renderBackups()
{
  if (backups_requested)
  {
    ImGui::OpenPopup("##save_backups");
    backups_requested = false;
  }
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal(
          "##save_backups", nullptr,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    return;
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    ImGui::TextUnformatted(
        fmt::sprintf("%s  ·  %s", LOC("SAVE_BACKUPS_TITLE"),
                     fmt::sprintf(LOC("MENU_SLOT_LABEL"), backups_slot))
            .c_str());
  }
  ImGui::Spacing();
  if (backups.empty())
    ImGui::TextColored(palette.muted, "%s", LOC("SAVE_BACKUPS_EMPTY"));
  const float rowWidth = 460.0f * scale;
  for (size_t index = 0; index < backups.size(); ++index)
  {
    const SaveBackup& backup = backups[index];
    const bool usable = backup.inspection.status == SaveStatus::Ok;
    ImGui::PushID(static_cast<int>(index));
    const float buttonWidth =
        UI::buttonWidth(LOC("SAVE_BACKUP_RESTORE"), UI::ButtonSize::COMPACT);
    ImGui::AlignTextToFramePadding();
    UI::textFitted(backup_labels[index],
                   rowWidth - buttonWidth - ImGui::GetStyle().ItemSpacing.x,
                   usable ? palette.text : palette.faint);
    ImGui::SameLine(rowWidth - buttonWidth);
    ImGui::BeginDisabled(!usable);
    if (UI::primaryButton(LOC("SAVE_BACKUP_RESTORE"), ImVec2(0.0f, 0.0f),
                          UI::ButtonSize::COMPACT))
      restore_candidate = backup.path;
    ImGui::EndDisabled();
    if (!usable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip(
          "%s", LOC(backup.inspection.status == SaveStatus::FutureVersion
                        ? "SAVE_STATUS_NEWER"
                        : "SAVE_STATUS_DAMAGED"));
    ImGui::PopID();
  }
  ImGui::Spacing();
  // Escape closes the list unless the restore confirmation is on top.
  if (UI::secondaryButton(LOC("TALK_CLOSE")) ||
      (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
       restore_candidate.empty()))
  {
    backups_slot = 0;
    ImGui::CloseCurrentPopup();
  }

  if (!restore_candidate.empty() && !ImGui::IsPopupOpen("##confirm_restore"))
    ImGui::OpenPopup("##confirm_restore");
  if (const UI::DialogResult result =
          UI::confirmDialog("##confirm_restore", LOC("SAVE_BACKUP_RESTORE"),
                            LOC("SAVE_BACKUP_RESTORE_CONFIRM"),
                            LOC("SAVE_BACKUP_RESTORE"), LOC("SETTINGS_CANCEL"));
      result == UI::DialogResult::CONFIRM)
  {
    // Restoring loads the career: run it on the loader like a normal load.
    pending_restore = restore_candidate;
    restore_candidate.clear();
    startSlot(backups_slot, false);
    backups_slot = 0;
    ImGui::CloseCurrentPopup();
  }
  else if (result == UI::DialogResult::CANCEL)
  {
    restore_candidate.clear();
  }
  ImGui::EndPopup();
}

void MainMenuScene::renderSlotPicker()
{
  const float scale = Theme::scale();
  const Theme::Palette& palette = Theme::palette();
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing,
                          ImVec2(0.5f, 0.5f));
  const std::string popupTitle =
      std::string(LOC(is_new_game ? "MENU_NEW_GAME" : "MENU_LOAD_GAME")) +
      "###select_save_slot";
  if (slot_picker_requested)
  {
    ImGui::OpenPopup("###select_save_slot");
    slot_picker_requested = false;
  }
  if (ImGui::BeginPopupModal(popupTitle.c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::TextColored(
        palette.muted, "%s",
        LOC(is_new_game ? "MENU_SLOT_PROMPT_NEW" : "MENU_SLOT_PROMPT_LOAD"));
    ImGui::Spacing();

    const ImVec2 slotSize(440.0f * scale, 58.0f * scale);
    for (int i = 1; i <= 3; ++i)
    {
      const auto& metadata = (cached_metadata.size() >= static_cast<size_t>(i))
                                 ? cached_metadata[static_cast<size_t>(i - 1)]
                                 : GameController::SaveSlotMetadata{};
      std::string detail;
      if (!metadata.exists)
        detail = LOC("MENU_SAVE_SLOT_EMPTY");
      else if (metadata.team_name.empty())
        detail = LOC("MENU_SAVE_SLOT_NOT_STARTED");
      else
      {
        detail = metadata.team_name;
        if (!metadata.game_date.empty())
          detail += "  ·  " + localizedDate(metadata.game_date);
      }

      const bool disableButton = !is_new_game && !metadata.exists;
      ImGui::BeginDisabled(disableButton);
      ImGui::PushID(i);
      const ImVec2 slotStart = ImGui::GetCursorScreenPos();
      if (ImGui::Button("##slot", slotSize))
      {
        if (is_new_game && metadata.exists)
        {
          overwrite_slot = i;
        }
        else if (!is_new_game && metadata.status != SaveStatus::Ok)
        {
          // A damaged or newer save cannot load: explain why and offer the
          // newest backup that does.
          showLoadError(
              i, formatLocalized(
                     metadata.status_key[0] != '\0' ? metadata.status_key
                                                    : "SAVE_ERROR_CORRUPT",
                     {std::to_string(metadata.schema_version),
                      std::to_string(metadata.supported_schema_version)}));
          ImGui::CloseCurrentPopup();
        }
        else
        {
          startSlot(i, is_new_game);
          ImGui::CloseCurrentPopup();
        }
      }
      const bool hovered = ImGui::IsItemHovered();
      ImGui::PopID();
      ImDrawList* drawList = ImGui::GetWindowDrawList();
      const std::string slotLabel = fmt::sprintf(LOC("MENU_SLOT_LABEL"), i);
      drawList->AddText(
          ImVec2(slotStart.x + 14.0f * scale, slotStart.y + 8.0f * scale),
          Theme::toU32(palette.text), slotLabel.c_str());
      UI::drawTextFitted(drawList,
                         ImVec2(slotStart.x + 14.0f * scale,
                                slotStart.y + slotSize.y -
                                    ImGui::GetTextLineHeight() - 8.0f * scale),
                         Theme::toU32(palette.muted), detail,
                         slotSize.x - 28.0f * scale);
      if (metadata.exists && metadata.status != SaveStatus::Ok)
      {
        // Status badge in the slot's top-right corner.
        Theme::ScopedText caption(Theme::Text::CAPTION);
        // A short label; the full explanation belongs to the load dialog.
        const char* status = LOC(metadata.status == SaveStatus::FutureVersion
                                     ? "SAVE_STATUS_NEWER"
                                 : metadata.status == SaveStatus::Incomplete
                                     ? "SAVE_STATUS_INCOMPLETE"
                                     : "SAVE_STATUS_DAMAGED");
        const ImVec2 textSize = ImGui::CalcTextSize(status);
        const ImVec2 pad(6.0f * scale, 2.0f * scale);
        const ImVec2 badgeMin(slotStart.x + slotSize.x - textSize.x -
                                  2.0f * pad.x - 10.0f * scale,
                              slotStart.y + 8.0f * scale);
        const ImVec4 tone = metadata.status == SaveStatus::FutureVersion
                                ? palette.warning
                                : palette.negative;
        drawList->AddRectFilled(badgeMin,
                                ImVec2(badgeMin.x + textSize.x + 2.0f * pad.x,
                                       badgeMin.y + textSize.y + 2.0f * pad.y),
                                Theme::toU32(tone, 0.2f), 3.0f * scale);
        drawList->AddText(ImVec2(badgeMin.x + pad.x, badgeMin.y + pad.y),
                          Theme::toU32(tone), status);
      }
      ImGui::EndDisabled();
      renderSlotActions(i, metadata);

      if (metadata.exists && !metadata.real_date.empty() && hovered)
      {
        const std::string tooltip =
            fmt::sprintf(LOC("MENU_SAVE_SLOT_LAST_SAVED"), metadata.real_date);
        ImGui::SetTooltip("%s", tooltip.c_str());
      }
    }

    ImGui::Spacing();
    // Escape belongs to whichever dialog is on top: a confirmation or the
    // backups list opened from here closes first.
    if (const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
                            overwrite_slot == 0 && delete_slot == 0 &&
                            !ImGui::IsPopupOpen("##save_backups");
        UI::secondaryButton(LOC("SETTINGS_CANCEL"), ImVec2(slotSize.x, 0.0f)) ||
        escape)
      ImGui::CloseCurrentPopup();

    // Overwriting a save is irreversible: ask first.
    if (overwrite_slot > 0 && !ImGui::IsPopupOpen("##confirm_overwrite"))
      ImGui::OpenPopup("##confirm_overwrite");
    const auto& target =
        cached_metadata[static_cast<size_t>(std::max(overwrite_slot, 1) - 1)];
    const std::string body =
        fmt::sprintf(LOC("MENU_OVERWRITE_BODY"), std::max(overwrite_slot, 1),
                     target.team_name.empty()
                         ? std::string(LOC("MENU_SAVE_SLOT_NOT_STARTED"))
                         : target.team_name);
    const UI::DialogResult result = UI::confirmDialog(
        "##confirm_overwrite", LOC("MENU_OVERWRITE_TITLE"), body.c_str(),
        LOC("MENU_OVERWRITE_CONFIRM"), LOC("SETTINGS_CANCEL"));
    // Deleting a save (and its backups) is permanent: ask first.
    if (delete_slot > 0 && !ImGui::IsPopupOpen("##confirm_delete"))
      ImGui::OpenPopup("##confirm_delete");
    if (const UI::DialogResult deletion = UI::confirmDialog(
            "##confirm_delete", LOC("SAVE_DELETE_TITLE"), delete_text.c_str(),
            LOC("SAVE_DELETE"), LOC("SETTINGS_CANCEL"));
        deletion == UI::DialogResult::CONFIRM)
    {
      if (!guiView->getController().deleteSave(delete_slot))
        Logger::error(std::format("Could not delete slot {}", delete_slot));
      delete_slot = 0;
      loadCachedMetadata();
    }
    else if (deletion == UI::DialogResult::CANCEL)
    {
      delete_slot = 0;
    }
    renderBackups();
    if (result == UI::DialogResult::CONFIRM)
    {
      startSlot(overwrite_slot, true);
      overwrite_slot = 0;
      ImGui::CloseCurrentPopup();
    }
    else if (result == UI::DialogResult::CANCEL)
    {
      overwrite_slot = 0;
    }
    ImGui::EndPopup();
  }
}

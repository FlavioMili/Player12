// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "settings_scene.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <utility>

#include "controller/game_controller.h"
#include "global/language_manager.h"
#include "global/paths.h"
#include "global/runtime_paths.h"
#include "gui/gui_constants.h"
#include "gui/scenes/main_menu_scene.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/inbox.h"
#include "model/onboarding.h"
#include "settings_manager.h"

SceneID SettingsScene::getID() const { return SceneID::SETTINGS; }

SettingsScene::SettingsScene(GUIView* guiView_ptr) : GUIScene(guiView_ptr) {}

SettingsScene::SettingsScene(GUIView* guiView_ptr, bool inCareer)
    : GUIScene(guiView_ptr), in_career(inCareer)
{
}

namespace
{
/** A language's own name ("Italiano"); other languages keep the file name. */
std::string nativeLanguageName(Language language, const std::string& fileName)
{
  switch (language)
  {
    case Language::EN:
      return "English";
    case Language::IT:
      return "Italiano";
    default:
      return fileName;
  }
}
}  // namespace

void SettingsScene::onEnter()
{
  languageOptions.clear();
  availableLanguageEnums.clear();
  fpsOptionsStrings.clear();
  for (const auto& [lang, str] : languageToString)
  {
    std::string filePath = AssetPaths::language(str);
    if (std::filesystem::exists(filePath))
    {
      languageOptions.push_back(nativeLanguageName(lang, str));
      availableLanguageEnums.push_back(lang);
    }
  }
  if (languageOptions.empty())
  {
    languageOptions.emplace_back("English");
    availableLanguageEnums.push_back(Language::EN);
  }
  for (const auto& fps : GUIConstants::FPS_OPTIONS)
  {
    fpsOptionsStrings.push_back(std::to_string(fps));
  }
  original_settings = SettingsManager::instance()->get();
  syncPendingFromSettings();
}

void SettingsScene::syncPendingFromSettings()
{
  const Settings& settings = SettingsManager::instance()->get();
  auto itLang = std::ranges::find(availableLanguageEnums, settings.language);
  selectedLanguage = (itLang != availableLanguageEnums.end())
                         ? static_cast<int>(std::distance(
                               availableLanguageEnums.begin(), itLang))
                         : 0;

  auto itFps = std::ranges::find(GUIConstants::FPS_OPTIONS, settings.fps_limit);
  selectedFPS = (itFps != GUIConstants::FPS_OPTIONS.end())
                    ? static_cast<int>(std::distance(
                          GUIConstants::FPS_OPTIONS.begin(), itFps))
                    : 1;

  // The presets, plus a size set in the file that is not one of them.
  resolutions.assign(GUIConstants::RESOLUTIONS.begin(),
                     GUIConstants::RESOLUTIONS.end());
  if (const auto current =
          std::ranges::find_if(resolutions,
                               [&settings](const GUIConstants::Resolution& r)
                               {
                                 return r.width == settings.resolution_width &&
                                        r.height == settings.resolution_height;
                               });
      current == resolutions.end())
    resolutions.push_back(
        {settings.resolution_width, settings.resolution_height});
  resolutionOptions.clear();
  for (const auto& res : resolutions)
    resolutionOptions.push_back(std::format("{}x{}", res.width, res.height));
  selectedResolution = static_cast<int>(std::distance(
      resolutions.begin(),
      std::ranges::find_if(resolutions,
                           [&settings](const GUIConstants::Resolution& r)
                           {
                             return r.width == settings.resolution_width &&
                                    r.height == settings.resolution_height;
                           })));

  fullscreen = settings.fullscreen;
  vsync = settings.vsync;
  pending_ui_scale =
      settings.ui_scale > 0.0f ? settings.ui_scale : Theme::scale();
  pending_text_scale = settings.text_scale;
  capture.reset();
  conflict.reset();
}

void SettingsScene::update(float deltaTime)
{
  if (showWipeDataOverlay && wipeDataTimer > 0.0f)
  {
    wipeDataTimer -= deltaTime;
  }
}

namespace
{
constexpr float CONTENT_MAX_WIDTH = 860.0f;
constexpr float LABEL_WIDTH = 210.0f;
constexpr float SWATCH_HEIGHT = 64.0f;
constexpr float MIN_SCALE_PERCENT = 75.0f;
constexpr float MAX_SCALE_PERCENT = 200.0f;
constexpr float MIN_TEXT_PERCENT = 90.0f;
constexpr float MAX_TEXT_PERCENT = 130.0f;

/** When a setting takes effect (said in its tooltip, marked with *). */
enum class Applies : uint8_t
{
  LIVE,    /**< At once (Cancel restores it). */
  ON_APPLY /**< When the player presses Apply. */
};

/**
 * Label of a settings row with its tooltip; settings applied by the Apply
 * button carry a " *" and say so in the tooltip. Hovering the row's control
 * shows the same tooltip (see rowTooltip()).
 */
void settingLabel(const char* label, const char* help,
                  Applies applies = Applies::LIVE)
{
  ImGui::AlignTextToFramePadding();
  if (applies == Applies::ON_APPLY)
    ImGui::Text("%s *", label);
  else
    ImGui::TextUnformatted(label);
  if (help != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
  {
    if (applies == Applies::ON_APPLY)
      ImGui::SetTooltip("%s\n%s", help, LOC("SETTINGS_APPLIES_ON_APPLY"));
    else
      ImGui::SetTooltip("%s", help);
  }
  ImGui::SameLine(LABEL_WIDTH * Theme::scale());
  ImGui::SetNextItemWidth(-FLT_MIN);
}

/** The row's help on its control too (the last item drawn). */
void rowTooltip(const char* help, Applies applies = Applies::LIVE)
{
  if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                            ImGuiHoveredFlags_AllowWhenDisabled))
    return;
  if (applies == Applies::ON_APPLY)
    ImGui::SetTooltip("%s\n%s", help, LOC("SETTINGS_APPLIES_ON_APPLY"));
  else
    ImGui::SetTooltip("%s", help);
}

template <typename Options>
void optionCombo(const char* id, const Options& options, int& selected)
{
  if (ImGui::BeginCombo(id, options[static_cast<size_t>(selected)].c_str()))
  {
    for (size_t index = 0; index < options.size(); ++index)
    {
      const bool isSelected = static_cast<size_t>(selected) == index;
      if (ImGui::Selectable(options[index].c_str(), isSelected))
        selected = static_cast<int>(index);
      if (isSelected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
}

bool presetCard(Theme::Preset preset, bool selected, float width)
{
  const Theme::Swatch swatch = Theme::presetSwatch(preset);
  const Theme::Palette& palette = Theme::palette();
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const ImVec2 size(width, SWATCH_HEIGHT * Theme::scale());
  ImGui::PushID(static_cast<int>(std::to_underlying(preset)));
  const bool pressed = ImGui::InvisibleButton("##preset", size);
  const bool hovered = ImGui::IsItemHovered();
  ImGui::PopID();
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const ImVec2 end(start.x + size.x, start.y + size.y);
  const float rounding = 6.0f * Theme::scale();
  drawList->AddRectFilled(start, end, Theme::toU32(swatch.background),
                          rounding);
  drawList->AddRectFilled(
      ImVec2(start.x + 8.0f * Theme::scale(), start.y + 8.0f * Theme::scale()),
      ImVec2(start.x + size.x * 0.45f, end.y - 8.0f * Theme::scale()),
      Theme::toU32(swatch.surface), 4.0f * Theme::scale());
  drawList->AddRectFilled(
      ImVec2(start.x + size.x * 0.52f, start.y + 14.0f * Theme::scale()),
      ImVec2(end.x - 10.0f * Theme::scale(), start.y + 20.0f * Theme::scale()),
      Theme::toU32(swatch.text), 2.0f * Theme::scale());
  drawList->AddRectFilled(
      ImVec2(start.x + size.x * 0.52f, start.y + 26.0f * Theme::scale()),
      ImVec2(end.x - 24.0f * Theme::scale(), start.y + 31.0f * Theme::scale()),
      Theme::toU32(swatch.muted), 2.0f * Theme::scale());
  drawList->AddRectFilled(
      ImVec2(start.x + size.x * 0.52f, end.y - 20.0f * Theme::scale()),
      ImVec2(start.x + size.x * 0.78f, end.y - 11.0f * Theme::scale()),
      Theme::toU32(palette.accent), 2.0f * Theme::scale());
  ImVec4 borderColor = palette.border;
  if (selected)
    borderColor = palette.accent;
  else if (hovered)
    borderColor = palette.muted;
  drawList->AddRect(start, end, Theme::toU32(borderColor), rounding, 0,
                    selected ? 2.5f * Theme::scale() : 1.0f);
  const char* name = LOC(Theme::presetKey(preset));
  ImGui::PushFont(nullptr, Theme::textSize(Theme::Text::SMALL));
  const float nameWidth = ImGui::CalcTextSize(name).x;
  ImGui::SetCursorScreenPos(
      ImVec2(start.x + std::max(0.0f, (width - nameWidth) * 0.5f),
             end.y + 3.0f * Theme::scale()));
  ImGui::TextColored(selected ? palette.text : palette.muted, "%s", name);
  ImGui::PopFont();
  return pressed;
}
}  // namespace

void SettingsScene::render()
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(Theme::Space::XL * Theme::scale(),
                             Theme::Space::XL * Theme::scale()));
  ImGui::Begin("##settings", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImGui::PopStyleVar(3);
  // A binding waiting for a key takes every key, Escape included.
  updateCapture();
  const bool keysFree = !capture && capture_end_frame != ImGui::GetFrameCount();
  // Escape backs out like Cancel unless a dialog or a combo takes it first.
  const bool leaveWithEscape =
      keysFree &&
      !ImGui::IsPopupOpen(
          "", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
      !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false);

  const float width = std::min(ImGui::GetContentRegionAvail().x,
                               CONTENT_MAX_WIDTH * Theme::scale());
  const float footerHeight =
      ImGui::GetFrameHeightWithSpacing() + Theme::Space::L * Theme::scale();
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                (ImGui::GetWindowWidth() - width) * 0.5f));
  ImGui::BeginGroup();
  UI::pageHeader(LOC("SETTINGS_TITLE"), LOC("SETTINGS_SUBTITLE"));
  ImGui::BeginChild(
      "##settings_body",
      ImVec2(width, ImGui::GetContentRegionAvail().y - footerHeight));
  renderGeneral();
  renderAppearance();
  renderGuidance();
  renderAudio();
  renderControls();
  renderSaving();
  // Wiping is offered only with no career in memory: a career left for the
  // main menu is still loaded and would be saved back on exit.
  if (!in_career && !guiView->getController().isGameLoaded()) renderData();
  {
    Theme::ScopedText small(Theme::Text::SMALL);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(Theme::palette().muted, "%s",
                       LOC("SETTINGS_APPLY_NOTE"));
    ImGui::PopTextWrapPos();
  }
  ImGui::EndChild();

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  const ImVec2 buttonSize(160.0f * Theme::scale(), 0.0f);
  const bool reset = UI::secondaryButton(LOC("SETTINGS_RESET"), buttonSize);
  rowTooltip(LOC("SETTINGS_RESET_HELP"));
  ImGui::SameLine();
  const bool cancelled =
      UI::secondaryButton(LOC("SETTINGS_CANCEL"), buttonSize) && keysFree;
  ImGui::SameLine();
  const bool applied =
      UI::primaryButton(LOC("SETTINGS_APPLY"), buttonSize) && keysFree;
  ImGui::EndGroup();
  if (reset && keysFree)
    resetToDefaults();
  else if (cancelled || leaveWithEscape)
    cancel();
  else if (applied)
    applyAndSaveSettings();

  ImGui::End();
}

void SettingsScene::renderGeneral()
{
  UI::beginAutoHeightCard("settings_general", LOC("SETTINGS_SECTION_GENERAL"));
  settingLabel(LOC("SETTINGS_LANGUAGE"), LOC("SETTINGS_LANGUAGE_HELP"),
               Applies::ON_APPLY);
  optionCombo("##language", languageOptions, selectedLanguage);
  rowTooltip(LOC("SETTINGS_LANGUAGE_HELP"), Applies::ON_APPLY);
  settingLabel(LOC("SETTINGS_RESOLUTION"), LOC("SETTINGS_RESOLUTION_HELP"),
               Applies::ON_APPLY);
  // A fullscreen window takes the display's size.
  ImGui::BeginDisabled(fullscreen);
  optionCombo("##resolution", resolutionOptions, selectedResolution);
  ImGui::EndDisabled();
  rowTooltip(fullscreen ? LOC("SETTINGS_RESOLUTION_FULLSCREEN")
                        : LOC("SETTINGS_RESOLUTION_HELP"),
             Applies::ON_APPLY);
  settingLabel(LOC("SETTINGS_REFRESH_RATE"), LOC("SETTINGS_REFRESH_RATE_HELP"),
               Applies::ON_APPLY);
  optionCombo("##fps", fpsOptionsStrings, selectedFPS);
  rowTooltip(LOC("SETTINGS_REFRESH_RATE_HELP"), Applies::ON_APPLY);
  settingLabel(LOC("SETTINGS_FULLSCREEN"), LOC("SETTINGS_FULLSCREEN_HELP"),
               Applies::ON_APPLY);
  ImGui::Checkbox("##fullscreen", &fullscreen);
  rowTooltip(LOC("SETTINGS_FULLSCREEN_HELP"), Applies::ON_APPLY);
  settingLabel(LOC("SETTINGS_VSYNC"), LOC("SETTINGS_VSYNC_HELP"),
               Applies::ON_APPLY);
  ImGui::Checkbox("##vsync", &vsync);
  rowTooltip(LOC("SETTINGS_VSYNC_HELP"), Applies::ON_APPLY);
  // Live match behaviour (applied at once, Cancel restores it).
  Settings& settings = SettingsManager::instance()->get();
  settingLabel(LOC("SETTINGS_MATCH_PAUSE_BREAKS"),
               LOC("SETTINGS_MATCH_PAUSE_BREAKS_HELP"));
  ImGui::Checkbox("##pause_at_breaks", &settings.pause_at_breaks);
  rowTooltip(LOC("SETTINGS_MATCH_PAUSE_BREAKS_HELP"));
  settingLabel(LOC("SETTINGS_MATCH_PAUSE_CHANGES"),
               LOC("MATCH_PAUSE_FOR_CHANGES_HINT"));
  ImGui::Checkbox("##pause_for_match_changes",
                  &settings.pause_for_match_changes);
  rowTooltip(LOC("MATCH_PAUSE_FOR_CHANGES_HINT"));
  settingLabel(LOC("SETTINGS_PLAY_HALF_MINUTES"),
               LOC("SETTINGS_PLAY_HALF_MINUTES_HELP"));
  ImGui::SliderInt("##play_half_minutes", &settings.play_half_minutes,
                   MatchTuning::Timing::MIN_PLAY_HALF_MINUTES,
                   MatchTuning::Timing::MAX_PLAY_HALF_MINUTES,
                   LOC("SETTINGS_PLAY_MINUTES_FORMAT"));
  rowTooltip(LOC("SETTINGS_PLAY_HALF_MINUTES_HELP"));
  // Play mode and its assistance (also in the play pause menu).
  settingLabel(LOC("SETTINGS_PLAY_MODE"), LOC("SETTINGS_PLAY_MODE_HELP"));
  ImGui::Checkbox("##play_mode", &settings.play_mode);
  rowTooltip(LOC("SETTINGS_PLAY_MODE_HELP"));
  const std::array<const char*, 3> switching{LOC("PLAY_SWITCH_MANUAL"),
                                             LOC("PLAY_SWITCH_ASSISTED"),
                                             LOC("PLAY_SWITCH_AUTO")};
  settingLabel(LOC("PLAY_AUTO_SWITCH"), LOC("PLAY_AUTO_SWITCH_HINT"));
  ImGui::Combo("##play_auto_switch", &settings.play_auto_switch,
               switching.data(), static_cast<int>(switching.size()));
  rowTooltip(LOC("PLAY_AUTO_SWITCH_HINT"));
  const std::array<const char*, 3> assistance{LOC("PLAY_ASSIST_NONE"),
                                              LOC("PLAY_ASSIST_NORMAL"),
                                              LOC("PLAY_ASSIST_STRONG")};
  settingLabel(LOC("PLAY_PASS_ASSIST"), LOC("SETTINGS_PLAY_HELP"));
  ImGui::Combo("##play_pass_assist", &settings.play_pass_assist,
               assistance.data(), static_cast<int>(assistance.size()));
  rowTooltip(LOC("SETTINGS_PLAY_HELP"));
  settingLabel(LOC("PLAY_DEAD_ZONE"), LOC("SETTINGS_PLAY_HELP"));
  ImGui::SliderFloat("##play_dead_zone", &settings.play_dead_zone, 0.05f, 0.5f,
                     "%.2f");
  rowTooltip(LOC("SETTINGS_PLAY_HELP"));
  UI::endCard();
}

void SettingsScene::renderAppearance()
{
  Settings& settings = SettingsManager::instance()->get();
  UI::beginAutoHeightCard("settings_appearance",
                          LOC("SETTINGS_SECTION_APPEARANCE"));

  // Theme presets as live swatches.
  ImGui::TextUnformatted(LOC("SETTINGS_THEME"));
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("%s", LOC("SETTINGS_THEME_HELP"));
  const auto presetCount =
      static_cast<int>(std::to_underlying(Theme::Preset::COUNT));
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const float cardWidth = (ImGui::GetContentRegionAvail().x -
                           gap * static_cast<float>(presetCount - 1)) /
                          static_cast<float>(presetCount);
  const float rowY = ImGui::GetCursorPosY();
  for (int index = 0; index < presetCount; ++index)
  {
    ImGui::SetCursorPos(
        ImVec2(ImGui::GetStyle().WindowPadding.x +
                   static_cast<float>(index) * (cardWidth + gap),
               rowY));
    if (presetCard(static_cast<Theme::Preset>(index),
                   settings.theme_preset == index, cardWidth))
    {
      settings.theme_preset = index;
      previewAppearance();
    }
  }
  ImGui::SetCursorPosY(rowY + SWATCH_HEIGHT * Theme::scale() +
                       ImGui::GetTextLineHeightWithSpacing() +
                       Theme::Space::S * Theme::scale());

  settingLabel(LOC("SETTINGS_ACCENT"), LOC("SETTINGS_ACCENT_HELP"));
  if (ImGui::RadioButton(LOC("SETTINGS_ACCENT_CLUB"), settings.club_accent))
  {
    settings.club_accent = true;
    previewAppearance();
  }
  ImGui::SameLine();
  if (ImGui::RadioButton(LOC("SETTINGS_ACCENT_CUSTOM"), !settings.club_accent))
  {
    settings.club_accent = false;
    previewAppearance();
  }
  if (!settings.club_accent)
  {
    ImGui::SameLine();
    ImVec4 accent = Theme::unpackRgb(settings.accent_rgb);
    if (ImGui::ColorEdit3(
            "##accent", &accent.x,
            ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
    {
      settings.accent_rgb = Theme::packRgb(accent);
      previewAppearance();
    }
  }

  settingLabel(LOC("SETTINGS_COLOR_VISION"), LOC("SETTINGS_COLOR_VISION_HELP"));
  {
    const auto count =
        static_cast<int>(std::to_underlying(Theme::ColorVision::COUNT));
    if (const int current = std::clamp(settings.color_vision, 0, count - 1);
        ImGui::BeginCombo("##color_vision",
                          LOC(Theme::colorVisionKey(
                              static_cast<Theme::ColorVision>(current)))))
    {
      for (int index = 0; index < count; ++index)
        if (ImGui::Selectable(LOC(Theme::colorVisionKey(
                                  static_cast<Theme::ColorVision>(index))),
                              index == current))
        {
          settings.color_vision = index;
          previewAppearance();
        }
      ImGui::EndCombo();
    }
    rowTooltip(LOC("SETTINGS_COLOR_VISION_HELP"));
  }

  settingLabel(LOC("SETTINGS_UI_SCALE"), LOC("SETTINGS_UI_SCALE_HELP"));
  bool automatic = settings.ui_scale <= 0.0f;
  if (ImGui::Checkbox(LOC("SETTINGS_UI_SCALE_AUTO"), &automatic))
  {
    settings.ui_scale = automatic ? 0.0f : pending_ui_scale;
    previewAppearance();
  }
  rowTooltip(LOC("SETTINGS_UI_SCALE_HELP"));
  if (!automatic)
  {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (float percent = pending_ui_scale * 100.0f;
        ImGui::SliderFloat("##ui_scale", &percent, MIN_SCALE_PERCENT,
                           MAX_SCALE_PERCENT, "%.0f%%"))
      pending_ui_scale = std::round(percent / 5.0f) * 0.05f;
    // Rescaling moves the slider itself, so apply once the drag ends.
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
      settings.ui_scale = pending_ui_scale;
      previewAppearance();
    }
  }

  settingLabel(LOC("SETTINGS_TEXT_SIZE"), LOC("SETTINGS_TEXT_SIZE_HELP"));
  {
    if (float percent = pending_text_scale * 100.0f;
        ImGui::SliderFloat("##text_scale", &percent, MIN_TEXT_PERCENT,
                           MAX_TEXT_PERCENT, "%.0f%%"))
      pending_text_scale = std::round(percent / 5.0f) * 0.05f;
    rowTooltip(LOC("SETTINGS_TEXT_SIZE_HELP"));
    // Like the UI scale: text grows under the cursor, so apply on release.
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
      settings.text_scale = pending_text_scale;
      previewAppearance();
    }
  }

  settingLabel(LOC("SETTINGS_DENSITY"), LOC("SETTINGS_DENSITY_HELP"));
  if (ImGui::RadioButton(LOC("SETTINGS_DENSITY_COMFORTABLE"),
                         !settings.compact_density))
  {
    settings.compact_density = false;
    previewAppearance();
  }
  ImGui::SameLine();
  if (ImGui::RadioButton(LOC("SETTINGS_DENSITY_COMPACT"),
                         settings.compact_density))
  {
    settings.compact_density = true;
    previewAppearance();
  }

  settingLabel(LOC("SETTINGS_REDUCED_MOTION"),
               LOC("SETTINGS_REDUCED_MOTION_HELP"));
  if (ImGui::Checkbox("##reduced_motion", &settings.reduced_motion))
    previewAppearance();
  rowTooltip(LOC("SETTINGS_REDUCED_MOTION_HELP"));
  UI::endCard();
}

void SettingsScene::renderGuidance()
{
  Settings& settings = SettingsManager::instance()->get();
  UI::beginAutoHeightCard("settings_guidance",
                          LOC("SETTINGS_SECTION_GUIDANCE"));
  settingLabel(LOC("SETTINGS_SCREEN_TIPS"), LOC("SETTINGS_SCREEN_TIPS_HELP"));
  // Turning tips back on explains every screen again.
  if (ImGui::Checkbox("##screen_tips", &settings.screen_tips) &&
      settings.screen_tips)
    settings.screen_tips_seen = 0;
  rowTooltip(LOC("SETTINGS_SCREEN_TIPS_HELP"));

  // A hidden first-week checklist can come back (it belongs to the career,
  // so this acts at once and Cancel does not undo it).
  if (GameController& controller = guiView->getController();
      in_career && controller.getManagedTeam())
  {
    const OnboardingState& checklist = controller.getOnboarding();
    settingLabel(LOC("SETTINGS_CHECKLIST"), LOC("SETTINGS_CHECKLIST_HELP"));
    if (checklist.allDone())
    {
      ImGui::TextColored(Theme::palette().muted, "%s",
                         LOC("SETTINGS_CHECKLIST_DONE"));
    }
    else if (checklist.isDismissed())
    {
      if (UI::secondaryButton(LOC("SETTINGS_CHECKLIST_SHOW"), ImVec2(0, 0),
                              UI::ButtonSize::COMPACT))
        controller.showOnboarding();
      rowTooltip(LOC("SETTINGS_CHECKLIST_HELP"));
    }
    else
    {
      ImGui::TextColored(Theme::palette().muted, "%s",
                         LOC("SETTINGS_CHECKLIST_SHOWN"));
    }
  }
  UI::endCard();
}

void SettingsScene::renderAudio()
{
  Settings& settings = SettingsManager::instance()->get();
  UI::beginAutoHeightCard("settings_audio", LOC("SETTINGS_SECTION_AUDIO"));
  // Volumes apply at once (a running match picks them up next frame);
  // Cancel restores the previous values.
  const auto volumeSlider = [](const char* id, float& volume, const char* help)
  {
    if (float percent = volume * 100.0f;
        ImGui::SliderFloat(id, &percent, 0.0f, 100.0f, "%.0f%%"))
      volume = std::clamp(std::round(percent) / 100.0f, 0.0f, 1.0f);
    rowTooltip(help);
  };
  settingLabel(LOC("SETTINGS_AUDIO_MASTER"), LOC("SETTINGS_AUDIO_MASTER_HELP"));
  volumeSlider("##master_volume", settings.master_volume,
               LOC("SETTINGS_AUDIO_MASTER_HELP"));
  settingLabel(LOC("SETTINGS_AUDIO_CROWD"), LOC("SETTINGS_AUDIO_CROWD_HELP"));
  volumeSlider("##crowd_volume", settings.crowd_volume,
               LOC("SETTINGS_AUDIO_CROWD_HELP"));
  settingLabel(LOC("SETTINGS_AUDIO_EFFECTS"),
               LOC("SETTINGS_AUDIO_EFFECTS_HELP"));
  volumeSlider("##effects_volume", settings.effects_volume,
               LOC("SETTINGS_AUDIO_EFFECTS_HELP"));
  settingLabel(LOC("SETTINGS_AUDIO_MUTE"), LOC("SETTINGS_AUDIO_MUTE_HELP"));
  ImGui::Checkbox("##audio_muted", &settings.audio_muted);
  rowTooltip(LOC("SETTINGS_AUDIO_MUTE_HELP"));
  UI::endCard();
}

void SettingsScene::updateCapture()
{
  if (!capture) return;
  Input::ActionRegistry& registry = Input::registry();
  // Escape cancels, Backspace clears the slot, anything else binds.
  if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
      ImGui::GetIO().KeyMods == 0)
  {
    capture.reset();
    capture_end_frame = ImGui::GetFrameCount();
    return;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false) &&
      ImGui::GetIO().KeyMods == 0)
  {
    registry.rebind(capture->action, capture->slot, ImGuiKey_None);
    capture.reset();
    capture_end_frame = ImGui::GetFrameCount();
    return;
  }
  const auto chord = Input::capturePressedChord();
  if (!chord.has_value()) return;
  const Capture target = *capture;
  capture.reset();
  capture_end_frame = ImGui::GetFrameCount();
  const Input::RebindResult result =
      registry.rebind(target.action, target.slot, *chord);
  if (!result.applied && result.conflict.has_value())
    conflict = Conflict{target.action, target.slot, *chord, *result.conflict};
}

void SettingsScene::renderControlRow(Input::ActionId id,
                                     const Input::Action& action,
                                     float labelWidth)
{
  Input::ActionRegistry& registry = Input::registry();
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  ImGui::PushID(static_cast<int>(id));
  ImGui::AlignTextToFramePadding();
  UI::textFitted(LOC(action.def.label_key.c_str()), labelWidth - spacing,
                 palette.text);
  ImGui::SameLine(labelWidth);
  const char* resetLabel = LOC("CONTROLS_RESET_ONE");
  const float resetWidth = UI::buttonWidth(resetLabel, UI::ButtonSize::COMPACT);
  const float slotWidth = std::max(
      60.0f * scale,
      (ImGui::GetContentRegionAvail().x - resetWidth - 2.0f * spacing) * 0.5f);
  for (std::size_t slot = 0; slot < Input::BINDING_SLOTS; ++slot)
  {
    ImGui::PushID(static_cast<int>(slot));
    const bool waiting =
        capture && capture->action == id && capture->slot == slot;
    const char* text =
        waiting ? LOC("CONTROLS_PRESS_KEY") : chord_labels[id][slot].c_str();
    ImGui::BeginDisabled(!action.def.rebindable);
    const bool pressed =
        waiting ? UI::primaryButton(text, ImVec2(slotWidth, 0.0f),
                                    UI::ButtonSize::COMPACT)
                : UI::secondaryButton(text, ImVec2(slotWidth, 0.0f),
                                      UI::ButtonSize::COMPACT);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                             ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", LOC(!action.def.rebindable ? "CONTROLS_FIXED"
                                  : waiting ? "CONTROLS_CAPTURE_HELP"
                                            : "CONTROLS_SLOT_HELP"));
    if (pressed && capture_end_frame != ImGui::GetFrameCount())
    {
      capture = Capture{id, slot};
      conflict.reset();
    }
    ImGui::PopID();
    ImGui::SameLine();
  }
  ImGui::BeginDisabled(registry.isDefault(id) || !action.def.rebindable);
  if (UI::secondaryButton(resetLabel, ImVec2(0.0f, 0.0f),
                          UI::ButtonSize::COMPACT))
  {
    registry.resetToDefault(id);
    conflict.reset();
  }
  ImGui::EndDisabled();

  if (conflict && conflict->action == id)
  {
    // The clash, and the choice: take the key over or keep things as they are.
    const std::string message = formatLocalized(
        "CONTROLS_CONFLICT",
        {Input::chordLabel(conflict->chord),
         LOC(registry.action(conflict->other).def.label_key.c_str())});
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImGui::Indent(labelWidth);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.warning, "%s %s", "!", message.c_str());
    ImGui::PopTextWrapPos();
    if (UI::secondaryButton(LOC("CONTROLS_REPLACE"), ImVec2(0, 0),
                            UI::ButtonSize::COMPACT))
    {
      registry.rebind(conflict->action, conflict->slot, conflict->chord, true);
      conflict.reset();
    }
    ImGui::SameLine();
    if (UI::secondaryButton(LOC("SETTINGS_CANCEL"), ImVec2(0, 0),
                            UI::ButtonSize::COMPACT))
      conflict.reset();
    ImGui::Unindent(labelWidth);
    // A bar beside the warning: shape as well as colour.
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(start.x + labelWidth - Theme::Space::S * scale, start.y),
        ImVec2(start.x + labelWidth - Theme::Space::S * scale + 3.0f * scale,
               ImGui::GetCursorScreenPos().y - ImGui::GetStyle().ItemSpacing.y),
        Theme::toU32(palette.warning));
  }
  ImGui::PopID();
}

void SettingsScene::renderControls()
{
  Input::ActionRegistry& registry = Input::registry();
  if (chord_labels.size() != registry.size() ||
      labels_revision != registry.revision())
  {
    labels_revision = registry.revision();
    chord_labels.assign(registry.size(), {});
    for (Input::ActionId id = 0; id < registry.size(); ++id)
      for (std::size_t slot = 0; slot < Input::BINDING_SLOTS; ++slot)
        chord_labels[id][slot] = Input::chordLabel(registry.chord(id, slot));
  }
  UI::beginAutoHeightCard("settings_controls",
                          LOC("SETTINGS_SECTION_CONTROLS"));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(Theme::palette().muted, "%s", LOC("CONTROLS_INTRO"));
  ImGui::PopTextWrapPos();
  const float labelWidth = std::min(ImGui::GetContentRegionAvail().x * 0.42f,
                                    300.0f * Theme::scale());
  for (std::size_t index = 0;
       index <
       static_cast<std::size_t>(std::to_underlying(Input::Category::COUNT));
       ++index)
  {
    const auto category = static_cast<Input::Category>(index);
    bool heading = false;
    registry.forEach(category,
                     [this, &heading, category, labelWidth](
                         Input::ActionId id, const Input::Action& action)
                     {
                       if (!heading)
                       {
                         UI::sectionLabel(LOC(Input::categoryKey(category)));
                         heading = true;
                       }
                       renderControlRow(id, action, labelWidth);
                     });
  }
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  if (UI::secondaryButton(LOC("CONTROLS_RESET_ALL")))
  {
    registry.resetAll();
    capture.reset();
    conflict.reset();
  }
  rowTooltip(LOC("CONTROLS_RESET_ALL_HELP"));
  UI::endCard();
}

void SettingsScene::renderSaving()
{
  Settings& settings = SettingsManager::instance()->get();
  UI::beginAutoHeightCard("settings_saving", LOC("SETTINGS_SECTION_SAVING"));
  static constexpr std::array<const char*, 6> FREQUENCY_KEYS = {
      "AUTOSAVE_OFF",     "AUTOSAVE_DAILY",    "AUTOSAVE_WEEKLY",
      "AUTOSAVE_MONTHLY", "AUTOSAVE_MATCHDAY", "AUTOSAVE_SEASON"};
  settingLabel(LOC("SETTINGS_AUTOSAVE"), LOC("SETTINGS_AUTOSAVE_HELP"),
               Applies::ON_APPLY);
  const int frequency = std::clamp(settings.autosave_frequency, 0,
                                   static_cast<int>(FREQUENCY_KEYS.size()) - 1);
  ImGui::SetNextItemWidth(260.0f * Theme::scale());
  if (ImGui::BeginCombo("##autosave_frequency",
                        LOC(FREQUENCY_KEYS[static_cast<size_t>(frequency)])))
  {
    for (int index = 0; index < static_cast<int>(FREQUENCY_KEYS.size());
         ++index)
    {
      if (ImGui::Selectable(LOC(FREQUENCY_KEYS[static_cast<size_t>(index)]),
                            index == frequency))
        settings.autosave_frequency = index;
    }
    ImGui::EndCombo();
  }
  rowTooltip(LOC("SETTINGS_AUTOSAVE_HELP"), Applies::ON_APPLY);
  settingLabel(LOC("SETTINGS_AUTOSAVE_BACKUPS"),
               LOC("SETTINGS_AUTOSAVE_BACKUPS_HELP"), Applies::ON_APPLY);
  ImGui::SetNextItemWidth(260.0f * Theme::scale());
  ImGui::SliderInt("##autosave_backups", &settings.autosave_backups, 0, 9);
  rowTooltip(LOC("SETTINGS_AUTOSAVE_BACKUPS_HELP"), Applies::ON_APPLY);
  UI::endCard();
}

void SettingsScene::renderData()
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("settings_data", LOC("SETTINGS_SECTION_DATA"));
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.muted, "%s", LOC("SETTINGS_WIPE_HELP"));
  ImGui::PopTextWrapPos();
  if (UI::dangerButton(LOC("SETTINGS_WIPE_BUTTON")))
  {
    showWipeDataOverlay = true;
    wipeDataTimer = 3.0f;
    ImGui::OpenPopup("###WipeDataPopup");
  }

  if (const std::string popupTitle =
          std::string(LOC("SETTINGS_WIPE_TITLE")) + "###WipeDataPopup";
      ImGui::BeginPopupModal(popupTitle.c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::PushTextWrapPos(420.0f * Theme::scale());
    ImGui::TextUnformatted(LOC("SETTINGS_WIPE_WARNING"));
    ImGui::PopTextWrapPos();
    ImGui::Separator();
    const ImVec2 buttonSize(150.0f * Theme::scale(), 0.0f);
    if (UI::secondaryButton(LOC("SETTINGS_CANCEL"), buttonSize) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
      showWipeDataOverlay = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (wipeDataTimer > 0.0f)
    {
      ImGui::BeginDisabled();
      const std::string waiting =
          fmt::sprintf(LOC("SETTINGS_WIPE_CONFIRM_WAIT"),
                       static_cast<double>(wipeDataTimer));
      UI::dangerButton(waiting.c_str(), buttonSize);
      ImGui::EndDisabled();
    }
    else if (UI::dangerButton(LOC("SETTINGS_WIPE_CONFIRM"), buttonSize))
    {
      RuntimePaths::removeAllSaves();
      showWipeDataOverlay = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  UI::endCard();
}

void SettingsScene::previewAppearance() { guiView->refreshTheme(); }

void SettingsScene::resetToDefaults()
{
  Settings& settings = SettingsManager::instance()->get();
  Settings defaults;
  // The language stays (a reset should not switch the screen to English),
  // and so does the record of which screen tips were already shown.
  defaults.language = settings.language;
  defaults.screen_tips_seen = settings.screen_tips_seen;
  settings = defaults;
  Input::registry().resetAll();
  syncPendingFromSettings();
  guiView->refreshTheme();
}

void SettingsScene::cancel()
{
  SettingsManager::instance()->get() = original_settings;
  Input::registry().reloadFromSettings();
  guiView->refreshTheme();
  leave();
}

void SettingsScene::leave()
{
  if (in_career)
    guiView->popScene();
  else
    changeScene(std::make_unique<MainMenuScene>(guiView));
}

void SettingsScene::applyAndSaveSettings()
{
  SettingsManager* settingsManager = SettingsManager::instance();
  auto& settings = settingsManager->getSettings();

  if (selectedLanguage >= 0 &&
      selectedLanguage < static_cast<int>(availableLanguageEnums.size()))
  {
    settings.language =
        availableLanguageEnums[static_cast<std::size_t>(selectedLanguage)];
  }

  if (selectedFPS >= 0 &&
      selectedFPS < static_cast<int>(GUIConstants::FPS_OPTIONS.size()))
  {
    settings.fps_limit =
        GUIConstants::FPS_OPTIONS[static_cast<std::size_t>(selectedFPS)];
  }

  if (selectedResolution >= 0 &&
      selectedResolution < static_cast<int>(resolutions.size()))
  {
    settings.resolution_width =
        resolutions[static_cast<std::size_t>(selectedResolution)].width;
    settings.resolution_height =
        resolutions[static_cast<std::size_t>(selectedResolution)].height;
  }
  settings.fullscreen = fullscreen;
  settings.vsync = vsync;

  // Resize only when the size changed or the window leaves fullscreen, so
  // Apply does not undo a maximised or hand-sized window.
  const bool resize =
      !settings.fullscreen &&
      (settings.resolution_width != original_settings.resolution_width ||
       settings.resolution_height != original_settings.resolution_height ||
       original_settings.fullscreen);
  guiView->applyWindowSettings(resize);
  settingsManager->save();
  guiView->refreshTheme();
  guiView->applySavePolicy();

  leave();
}

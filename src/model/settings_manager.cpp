// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "settings_manager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "global/language_manager.h"
#include "global/paths.h"
#include "global/runtime_paths.h"

using json = nlohmann::json;

SettingsManager::SettingsManager() {}

SettingsManager* SettingsManager::instance()
{
  static SettingsManager instance;
  return &instance;
}

void SettingsManager::load()
{
  std::ifstream in(RuntimePaths::settingsPath());
  if (!in)
  {
    std::cerr << "Settings file not found, using defaults.\n";
    return;
  }

  try
  {
    json j;
    in >> j;

    settings_.language = j.value("language", settings_.language);
    const auto resolution =
        j.value("resolution", std::vector<int>{settings_.resolution_width,
                                               settings_.resolution_height});
    if (constexpr int MAX_RESOLUTION = 16384;
        resolution.size() == 2 && resolution[0] >= 640 &&
        resolution[1] >= 480 && resolution[0] <= MAX_RESOLUTION &&
        resolution[1] <= MAX_RESOLUTION)
    {
      settings_.resolution_width = resolution[0];
      settings_.resolution_height = resolution[1];
    }
    settings_.fullscreen = j.value("fullscreen", settings_.fullscreen);
    settings_.vsync = j.value("vsync", settings_.vsync);
    settings_.fps_limit =
        std::clamp(j.value("fps_limit", settings_.fps_limit), 15, 360);
    settings_.theme_preset =
        std::clamp(j.value("theme_preset", settings_.theme_preset), 0, 5);
    settings_.club_accent = j.value("club_accent", settings_.club_accent);
    settings_.accent_rgb =
        j.value("accent_rgb", settings_.accent_rgb) & 0xFFFFFFU;
    const float scale = j.value("ui_scale", settings_.ui_scale);
    settings_.ui_scale = scale <= 0.0f ? 0.0f : std::clamp(scale, 0.75f, 2.0f);
    settings_.compact_density =
        j.value("compact_density", settings_.compact_density);
    settings_.reduced_motion =
        j.value("reduced_motion", settings_.reduced_motion);
    settings_.color_vision =
        std::clamp(j.value("color_vision", settings_.color_vision), 0, 2);
    settings_.text_scale =
        std::clamp(j.value("text_scale", settings_.text_scale), 0.85f, 1.5f);
    settings_.screen_tips = j.value("screen_tips", settings_.screen_tips);
    settings_.screen_tips_seen =
        j.value("screen_tips_seen", settings_.screen_tips_seen);
    settings_.autosave_frequency = std::clamp(
        j.value("autosave_frequency", settings_.autosave_frequency), 0, 5);
    settings_.autosave_backups = std::clamp(
        j.value("autosave_backups", settings_.autosave_backups), 0, 9);
    settings_.master_volume = std::clamp(
        j.value("master_volume", settings_.master_volume), 0.0f, 1.0f);
    settings_.crowd_volume =
        std::clamp(j.value("crowd_volume", settings_.crowd_volume), 0.0f, 1.0f);
    settings_.effects_volume = std::clamp(
        j.value("effects_volume", settings_.effects_volume), 0.0f, 1.0f);
    settings_.audio_muted = j.value("audio_muted", settings_.audio_muted);
    settings_.pause_for_match_changes =
        j.value("pause_for_match_changes", settings_.pause_for_match_changes);
    settings_.pause_at_breaks =
        j.value("pause_at_breaks", settings_.pause_at_breaks);
    settings_.play_half_minutes =
        std::clamp(j.value("play_half_minutes", settings_.play_half_minutes),
                   MatchTuning::Timing::MIN_PLAY_HALF_MINUTES,
                   MatchTuning::Timing::MAX_PLAY_HALF_MINUTES);
    settings_.play_mode = j.value("play_mode", settings_.play_mode);
    settings_.play_auto_switch = std::clamp(
        j.value("play_auto_switch", settings_.play_auto_switch), 0, 2);
    settings_.play_pass_assist = std::clamp(
        j.value("play_pass_assist", settings_.play_pass_assist), 0, 2);
    settings_.play_dead_zone = std::clamp(
        j.value("play_dead_zone", settings_.play_dead_zone), 0.05f, 0.5f);
    settings_.key_bindings.clear();
    if (const auto bindings = j.find("key_bindings");
        bindings != j.end() && bindings->is_object())
    {
      for (const auto& [action, chords] : bindings->items())
      {
        if (!chords.is_array()) continue;
        std::vector<std::string> names;
        for (const auto& chord : chords)
          names.push_back(chord.is_string() ? chord.get<std::string>() : "");
        settings_.key_bindings[action] = std::move(names);
      }
    }
    settings_.hidden_columns.clear();
    if (const auto views = j.find("hidden_columns");
        views != j.end() && views->is_object())
    {
      for (const auto& [table, columns] : views->items())
      {
        if (!columns.is_array()) continue;
        std::vector<std::string> keys;
        for (const auto& column : columns)
          if (column.is_string()) keys.push_back(column.get<std::string>());
        if (!keys.empty()) settings_.hidden_columns[table] = std::move(keys);
      }
    }
  }
  catch (const json::exception& exception)
  {
    std::cerr << "Invalid settings file, using defaults: " << exception.what()
              << '\n';
  }

  LanguageManager::instance().loadLanguage(settings_.language);
}

void SettingsManager::save() const
{
  json j;
  j["language"] = settings_.language;
  j["resolution"] = {settings_.resolution_width, settings_.resolution_height};
  j["fullscreen"] = settings_.fullscreen;
  j["fps_limit"] = settings_.fps_limit;
  j["vsync"] = settings_.vsync;
  j["theme_preset"] = settings_.theme_preset;
  j["club_accent"] = settings_.club_accent;
  j["accent_rgb"] = settings_.accent_rgb;
  j["ui_scale"] = settings_.ui_scale;
  j["compact_density"] = settings_.compact_density;
  j["reduced_motion"] = settings_.reduced_motion;
  j["color_vision"] = settings_.color_vision;
  j["text_scale"] = settings_.text_scale;
  j["screen_tips"] = settings_.screen_tips;
  j["screen_tips_seen"] = settings_.screen_tips_seen;
  j["autosave_frequency"] = settings_.autosave_frequency;
  j["autosave_backups"] = settings_.autosave_backups;
  j["master_volume"] = settings_.master_volume;
  j["crowd_volume"] = settings_.crowd_volume;
  j["effects_volume"] = settings_.effects_volume;
  j["audio_muted"] = settings_.audio_muted;
  j["pause_for_match_changes"] = settings_.pause_for_match_changes;
  j["pause_at_breaks"] = settings_.pause_at_breaks;
  j["play_half_minutes"] = settings_.play_half_minutes;
  j["play_mode"] = settings_.play_mode;
  j["play_auto_switch"] = settings_.play_auto_switch;
  j["play_pass_assist"] = settings_.play_pass_assist;
  j["play_dead_zone"] = settings_.play_dead_zone;
  j["key_bindings"] = settings_.key_bindings;
  j["hidden_columns"] = json::object();
  for (const auto& [table, columns] : settings_.hidden_columns)
    j["hidden_columns"][table] = columns;

  // Written next to the file and renamed over it, so a crash mid-write
  // never leaves a truncated settings file.
  const std::filesystem::path path = RuntimePaths::settingsPath();
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream out(temporary, std::ios::trunc);
    if (!out) return;
    out << j.dump(2);
    out.flush();
    if (!out) return;
  }
  std::error_code error;
  std::filesystem::rename(temporary, path, error);
  if (error)
  {
    std::cerr << "Could not save settings: " << error.message() << '\n';
    std::filesystem::remove(temporary, error);
  }
}

void SettingsManager::apply(SDL_Window* window)
{
  if (!window) return;

  SDL_SetWindowFullscreen(window, settings_.fullscreen);
  // Window size (GUIView::applyWindowSettings), fps limit (main loop) and
  // VSync (renderer) are applied by the GUI.

  LanguageManager::instance().loadLanguage(settings_.language);
}

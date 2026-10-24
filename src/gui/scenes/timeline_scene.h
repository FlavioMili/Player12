// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gui/scenes/management_scene.h"
#include "model/career_timeline.h"

/**
 * @brief The manager's career across every club: spells, trophies, honours,
 * promotions and relegations, season finishes, club records and key
 * signings (model/career_timeline.h), grouped by season or by month, with
 * an export of the career journal as Markdown.
 */
class TimelineScene : public ManagementScene
{
 public:
  enum class Zoom : std::uint8_t
  {
    SEASON,
    MONTH
  };

  explicit TimelineScene(GUIView* parent);

  void update(float /*deltaTime*/) override
  {
    // Rendering reads current state; there is no per-frame update work.
  }
  [[nodiscard]] SceneID getID() const override { return SceneID::TIMELINE; }

  [[nodiscard]] std::size_t entryCount() const { return entries.size(); }
  [[nodiscard]] std::size_t groupCount() const { return groups.size(); }
  void setZoom(Zoom value);
  /**
   * Writes the career journal to the user data folder.
   * @return The file written, or nullopt when it could not be written.
   */
  std::optional<std::filesystem::path> exportJournal();

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::TIMELINE;
  }
  void refresh() override;

 private:
  struct Row
  {
    std::string date;
    std::string kind;
    std::string text;
    std::string player;
    std::string club;
  };

  struct Group
  {
    std::string label;
    std::vector<std::size_t> entries;
  };

  void regroup();
  void renderSummary() const;
  void renderControls();
  void renderEntry(std::size_t index);

  std::vector<TimelineEntry> entries;
  std::vector<Row> rows;
  std::vector<Group> groups;
  /** Clubs of the career, in order of the first spell. */
  std::vector<std::pair<TeamID, std::string>> clubs;
  TeamID club_filter = 0;
  std::string club_label;
  Zoom zoom = Zoom::SEASON;
  int zoom_index = 0;
  std::string clubs_value;
  std::string matches_value;
  std::string matches_note;
  std::string win_rate_value;
  std::string trophies_value;
};

// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gui/scenes/management_scene.h"
#include "model/news_feed.h"

/**
 * @brief World news: big transfers, managerial changes, title races,
 * upsets, records and honours from this season and the last, built from
 * the saved world (model/news_feed.h). Filters by country, competition and
 * kind; every story links to its player, clubs or match report.
 */
class NewsScene : public ManagementScene
{
 public:
  explicit NewsScene(GUIView* parent);

  void update(float /*deltaTime*/) override
  {
    // Rendering reads current state; there is no per-frame update work.
  }
  [[nodiscard]] SceneID getID() const override { return SceneID::NEWS; }

  /** Stories in the feed, and those the filters keep. */
  [[nodiscard]] std::size_t storyCount() const { return items.size(); }
  [[nodiscard]] std::size_t shownCount() const { return kept.size(); }
  /** Keeps one country (0: continental) or every one (nullopt). */
  void filterCountry(std::optional<LeagueID> country);
  /** Keeps one kind of story or every kind (nullopt). */
  void filterKind(std::optional<NewsKind> kind);

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::NEWS;
  }
  void refresh() override;

 private:
  /** Display strings of a story, built once per refresh. */
  struct Row
  {
    std::string headline;
    std::string date;
    std::string meta; /*!< Kind and competition. */
    std::string player;
    std::string team;
    std::string other_team;
  };

  struct CompetitionOption
  {
    NewsCompetition competition;
    std::string name;
  };

  void rebuildCompetitions();
  void applyFilter();
  void renderFilters();
  void renderStory(std::size_t index);

  std::vector<NewsItem> items;
  std::vector<Row> rows;
  std::vector<std::size_t> kept;
  /** Countries with stories: top league ID and name (0: continental). */
  std::vector<std::pair<LeagueID, std::string>> countries;
  std::vector<CompetitionOption> competitions;
  NewsFilter filter;
  std::string country_label;
  std::string competition_label;
  std::size_t visible = 0;
};

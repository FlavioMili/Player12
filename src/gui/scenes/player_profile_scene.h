// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gui/scenes/contract_talks_dialog.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/player_talk_dialog.h"
#include "gui/view_models/player_view.h"
#include "model/player.h"
#include "model/season_history.h"

/**
 * @brief Full profile of one player: bio and contract, fitness and form,
 * attributes grouped by category, position suitability, season and career
 * statistics, transfer history and context actions.
 *
 * The managed club's players are shown exactly. Every other player is shown
 * through the club's scouting knowledge only (GameController::getScoutedView):
 * estimated attributes with their likely range, an estimated overall and
 * value, the latest scout report, and recruitment actions (scout, shortlist,
 * make an offer). Hidden true attributes are never displayed for them.
 */
class PlayerProfileScene : public ManagementScene
{
 public:
  PlayerProfileScene(GUIView* parent, PlayerID playerId);

  void update(float deltaTime) override;
  [[nodiscard]] SceneID getID() const override
  {
    return SceneID::PLAYER_PROFILE;
  }
  [[nodiscard]] std::optional<NavEntry> historyEntry() const override
  {
    return NavEntry::ofPlayer(player_id);
  }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override;
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;
  friend class GameFlowTest_ManagementScreensMidSeason_Test;

  /** @brief An attribute; low == high == value when known exactly. */
  struct AttributeLine
  {
    std::string name;
    float value = 0.0f;
    float low = 0.0f;
    float high = 0.0f;
  };

  struct AttributeSection
  {
    const char* title_key;
    std::vector<AttributeLine> lines;
  };

  /** @brief One statistics row (a competition this season, or a season). */
  struct StatsRow
  {
    std::string label;
    PlayerSeasonStats stats;
  };

  /** @brief One move in the player's transfer history. */
  struct TransferLine
  {
    std::string date;
    std::string from;
    std::string to;
    const char* kind_key = "";
    std::string fee;
  };

  /** @brief The latest scout report, formatted for display. */
  struct ReportSummary
  {
    std::string heading; /**< Date and scout. */
    char grade = 'C';
    const char* grade_key = "";
    std::string ability;
    std::string potential;
    std::string fee;
  };

  void renderHeader();
  void renderActions();
  void renderBio(const Player& player, float width, float height) const;
  void renderStatus(float width, float height) const;
  void renderAttributes(float width, float height) const;
  void renderSuitability(float width, float height) const;
  void renderScouting(float width, float height);
  void renderStatistics(float width, float height);
  void renderStatsTable(const std::vector<StatsRow>& rows, bool career) const;
  void renderTransfers() const;
  void renderDialogs();
  void sendScout();
  [[nodiscard]] const Player* player() const;
  [[nodiscard]] bool isOwnPlayer() const;

  PlayerID player_id;
  PlayerView::PlayerRow row;
  std::string club_name;
  std::string nationality;
  std::string clause_text; /**< His release clause, empty if none. */
  std::vector<PlayerView::RoleFit> fits;
  std::vector<AttributeSection> sections;
  std::vector<StatsRow> season_rows;
  std::vector<StatsRow> career_rows;
  std::vector<TransferLine> transfers;

  // Fitness, availability and outlook, cached on refresh.
  PlayerDynamics dynamics;
  float form = 0.0f;
  uint8_t league_ban = 0;
  uint8_t cup_ban = 0;
  const char* squad_role_key = nullptr;
  float potential_low = 0.0f;
  float potential_high = 0.0f;

  // Scouting view of another club's player.
  bool scouted = false;
  uint8_t knowledge = 0;
  std::string overall_range;
  std::optional<ReportSummary> latest_report;
  bool shortlisted = false;
  bool being_scouted = false;
  int64_t scout_cost = 0;
  bool window_open = false;

  bool list_confirm_requested = false;
  bool renew_requested = false;
  ContractTalksDialog contract_talks;
  PlayerTalkDialog talk_dialog;
};

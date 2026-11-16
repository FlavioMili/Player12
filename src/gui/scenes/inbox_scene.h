// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gui/scenes/draw_ceremony_dialog.h"
#include "gui/scenes/management_scene.h"
#include "gui/scenes/offer_negotiation_dialog.h"
#include "gui/scenes/player_talk_dialog.h"
#include "model/inbox.h"

/**
 * @brief Club inbox: pinned decisions with their actions in place, and the
 * information feed with category filters and digest grouping.
 *
 * Decisions (offers for the club's players, player requests, youth
 * trialists) stay on the Decisions tab until resolved, with negotiate /
 * accept / reject / reply / sign buttons right there; transfer bids open
 * the talks dialog (OfferNegotiationDialog). Read information older than
 * Inbox::ARCHIVE_DAYS moves to the archive.
 *
 * Routine messages of the same kind that arrive in the same week are folded
 * into one digest row, so a busy matchday reads as a single entry instead of
 * a wall of near-identical notifications.
 *
 * The filters (tab, category, unread, followed players, one player or club)
 * are kept with the career (InboxView), so they survive a scene change and
 * a reload. Decision moments are answered on the Decisions tab with their
 * effects shown (InboxDilemmaCard).
 */
class InboxScene : public ManagementScene
{
 public:
  explicit InboxScene(GUIView* parent);

  void update(float deltaTime) override;
  [[nodiscard]] SceneID getID() const override { return SceneID::INBOX; }

 protected:
  void renderContent() override;
  [[nodiscard]] NavSection navSection() const override
  {
    return NavSection::INBOX;
  }
  void refresh() override;

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  /** @brief One visible row: a single message or a folded digest. */
  struct Thread
  {
    std::vector<size_t> messages; /**< Indices into the inbox, newest first. */
    std::string title;
    std::string detail;
    std::string date_text;
    std::vector<std::string> message_titles; /**< Digest children only. */
    InboxCategory category = InboxCategory::General;
    size_t unread = 0;
  };

  /** @brief A pending decision with what can be done about it. */
  struct Decision
  {
    size_t message = 0; /**< Index into the inbox. */
    InboxAction action = InboxAction::None;
    PlayerID player = 0;
    std::string title;
    std::string body;
    std::string date_text;
    struct Option
    {
      uint32_t id = 0; /**< Offer id or trialist player id. */
      std::string text;
      bool negotiable = false; /**< A transfer bid (talks dialog). */
      bool awaiting = false;   /**< The buyer considers a counter. */
    };
    std::vector<Option> options;
  };

  void rebuildThreads();
  void rebuildDecisions();
  void renderDecisions();
  void renderDecision(const Decision& decision);
  void renderFilters(float width, float height);
  void renderThreads(float width, float height);
  void renderReader(float height);
  void openMessage(size_t messageIndex);
  void renderReaderActions(const InboxMessage& message);
  /** Watch the draw: continental draw messages open the ceremony. */
  void renderDrawAction(const InboxMessage& message);
  void renderEntityFilter();
  /** The filters as shown now. */
  InboxView currentView() const;
  void applyView(const InboxView& view);
  /** Keeps the filters with the career and rebuilds the feed. */
  void storeView();

  static constexpr size_t CATEGORY_COUNT =
      static_cast<size_t>(std::to_underlying(InboxCategory::COUNT));

  std::vector<Thread> threads;
  std::array<size_t, CATEGORY_COUNT> unread_by_category{};
  std::array<size_t, CATEGORY_COUNT> total_by_category{};
  int category_filter = -1; /**< -1 = all categories. */
  bool unread_only = false;
  int tab = -1; /**< 0 = decisions, 1 = information (-1: pick). */
  bool followed_only = false;
  std::optional<PlayerID> filter_player;
  std::optional<TeamID> filter_team;
  std::string filter_label; /**< Name of the filtered player or club. */
  InboxView stored_view;    /**< Last view read from or kept with the career. */
  std::vector<PlayerID> followed;
  size_t followed_unread = 0;
  size_t followed_total = 0;
  bool show_archived = false;
  size_t archived_count = 0;
  std::vector<Decision> decisions;
  /** Messages hidden from the feed (pending decisions, archive). */
  std::vector<bool> hidden;
  int selected_thread = -1;
  int expanded_thread = -1;
  size_t selected_message = SIZE_MAX;
  std::uint32_t selected_message_id = 0;
  std::string selected_title;
  std::string selected_body;
  PlayerTalkDialog talk_dialog;
  OfferNegotiationDialog offer_dialog;
  DrawCeremonyDialog draw_dialog;
};

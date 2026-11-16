// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <string>
#include <vector>

#include "global/types.h"
#include "model/interactions.h"

class GameController;
struct InboxMessage;

/**
 * @brief Modal one-to-one conversation with a managed player.
 *
 * Self-contained: a scene keeps one instance, calls open() from a button and
 * render() every frame (inside its ImGui window). The dialog lists the
 * conversation lines grouped by topic with the predicted reaction, cooldowns
 * and why a line is unavailable, the player's open request and his promises
 * with progress; after a line it shows his reply and what changed. One line
 * per conversation: talks are meant to be rare and to matter.
 *
 * All text is built when the dialog opens or a line is held, never per
 * frame.
 */
class PlayerTalkDialog
{
 public:
  /** Opens the conversation (shown from the next render()). */
  void open(GameController& controller, PlayerID player_id);

  /**
   * Draws the dialog while open.
   * @return True on the frame a conversation line was held (refresh views).
   */
  bool render(GameController& controller);

  /**
   * Inbox helper: a "Reply" button for messages from a player who waits for
   * an answer (request or story); opens the dialog when clicked.
   */
  void inboxAction(GameController& controller, const InboxMessage& message);

  [[nodiscard]] bool isOpen() const { return visible; }

 private:
  struct OptionRow
  {
    TalkOption option = TalkOption::PraiseForm;
    std::string label;
    std::string description;
    std::string status; /**< Hint, or why it is unavailable. */
    ImVec4 status_color;
    bool enabled = false;
    bool suggested = false;
  };

  struct Section
  {
    std::string title;
    std::vector<OptionRow> rows;
  };

  struct PromiseLine
  {
    std::string text;
    std::string when;
    float progress = 0.0f;
    ImVec4 color;
  };

  void rebuild(const GameController& controller);
  void renderHeader() const;
  void renderOptions(GameController& controller);
  void renderResult() const;
  void choose(GameController& controller, TalkOption option);

  PlayerID player_id = 0;
  bool open_requested = false;
  bool visible = false;
  bool has_result = false;

  std::string title;
  std::string subtitle;
  std::string morale_text;
  std::string trust_text;
  float morale = 0.0f;
  float trust = 0.0f;
  std::string request_text;
  std::string story_text;
  std::vector<PromiseLine> promises;
  std::vector<Section> sections;

  // Result of the line just held.
  std::string reaction_text;
  ImVec4 reaction_color;
  std::string reply_text;
  std::vector<std::string> consequences;
};

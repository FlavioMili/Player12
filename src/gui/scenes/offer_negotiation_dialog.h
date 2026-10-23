// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#pragma once

#include <imgui.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "controller/game_controller.h"
#include "gui/widgets/widgets.h"
#include "model/transfer_negotiation.h"

/**
 * @brief Modal talks over an AI club's bid for one of the managed players,
 * or its offer to borrow him.
 *
 * Self-contained like PlayerTalkDialog: the inbox and the transfer market
 * each keep one, call open() from a button and render() every frame. It
 * shows the buyer, the player (value, asking price, contract, how he feels
 * about the move), the bid on the table broken down (fee, upfront part,
 * instalments, add-ons, sell-on) and every round so far. The club accepts,
 * rejects, counters with a full structure, names its price or declares the
 * player not for sale; the buyer's answer arrives on a later day (the same
 * day near the deadline). A loan offer shows the loan terms instead and is
 * countered with wage share, length, purchase option or obligation, recall
 * clause and guaranteed appearances.
 *
 * Text is built when the dialog opens and after each action, never per
 * frame; only the terms editor runs every frame.
 */
class OfferNegotiationDialog
{
 public:
  /** Opens the talks over @p offer_id (shown from the next render()). */
  void open(GameController& controller, std::uint32_t offer_id);

  /**
   * Draws the dialog while open.
   * @return True on the frame an action changed the talks (refresh views).
   */
  bool render(GameController& controller);

  [[nodiscard]] bool isOpen() const { return visible; }

 private:
  friend class GameFlowTest_GUIFlowLifecycle_Test;

  enum class Mode : int
  {
    Counter = 0,
    NamePrice = 1
  };

  struct Line
  {
    std::string label;
    std::string value;
  };

  struct RoundLine
  {
    std::string who;
    std::string text;
    std::string detail;
    bool buyer = false;
  };

  void rebuild(const GameController& controller);
  /** Upfront and present-value texts of the counter being prepared. */
  void refreshCounterTexts();
  /** The offer on the table, line by line. */
  void rebuildTransferLines();
  void rebuildLoanLines();
  void renderHeader() const;
  void renderBid() const;
  void renderHistory() const;
  void renderAnswer();
  void renderActions(GameController& controller);
  void renderResult() const;

  void accept(GameController& controller);
  void reject(GameController& controller);
  void notForSale(GameController& controller);
  void submit(GameController& controller);
  /** Shows the end of the talks. */
  void finish(std::string_view headline, const ImVec4& color,
              std::string_view detail);

  std::uint32_t offer_id = 0;
  bool open_requested = false;
  bool visible = false;
  bool changed = false;
  std::optional<GameController::IncomingOfferView> view;

  // Built by rebuild().
  std::string title;
  std::string subtitle;
  std::string stance_text;
  ImVec4 stance_color{};
  std::vector<Line> facts;
  std::vector<Line> bid_lines;
  std::string bid_total;
  std::vector<RoundLine> rounds;
  std::string status_text; /**< Awaiting reply, final offer, window. */
  ImVec4 status_color{};
  std::string proposal_text; /**< The pending counter, while awaited. */
  std::vector<UI::MoneyChip> counter_chips;
  std::array<UI::MoneyChip, 4> price_chips{};

  // The club's answer being prepared.
  Mode mode = Mode::Counter;
  TransferNegotiation::OfferTerms counter;
  TransferNegotiation::LoanTerms loan_counter;
  std::int64_t price = 0;
  std::string counter_upfront_text;
  std::string counter_worth_text;
  std::string loan_wage_text;   /**< The borrower's weekly share. */
  std::string loan_saving_text; /**< Wages saved over the loan. */
  std::string notice; /**< Result of the last action while still open. */
  ImVec4 notice_color{};

  // End of the talks.
  bool finished = false;
  std::string result_headline;
  ImVec4 result_color{};
  std::string result_detail;
};

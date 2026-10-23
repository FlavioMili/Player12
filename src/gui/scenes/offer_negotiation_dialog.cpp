// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/offer_negotiation_dialog.h"

#include <fmt/printf.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include "global/language_manager.h"
#include "gui/scenes/transfer_terms_editor.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/buyer_negotiation.h"
#include "model/loan_negotiation.h"
#include "model/world_rng.h"
#include "model/world_simulation.h"

namespace
{
using BuyerNegotiation::PlayerStance;
using Outcome = GameController::OfferOutcome;
using TransferTermsEditor::beginPanel;
using TransferTermsEditor::endPanel;
using TransferTermsEditor::formLabel;

constexpr const char* POPUP_ID = "###offer_negotiation";
constexpr float DIALOG_WIDTH = 640.0f;
constexpr float WIDE_DIALOG_WIDTH = 1000.0f;
constexpr float TWO_COLUMN_MIN_VIEWPORT = 1100.0f;
constexpr float VIEWPORT_WIDTH_SHARE = 0.94f;
constexpr float VIEWPORT_HEIGHT_SHARE = 0.9f;
/** The opening counter and named price, relative to the bid. */
constexpr double OPENING_COUNTER = 1.15;
constexpr double OPENING_PRICE = 1.25;
/** The opening loan counter asks this much more of the wage. */
constexpr int OPENING_LOAN_SHARE_RISE = 20;
constexpr int SIX_MONTH_WEEKS = 26;
constexpr int DEADLINE_WARNING_DAYS = 2;

float scaled(float value) { return value * Theme::scale(); }

std::int64_t roundedFee(double amount)
{
  const auto step = static_cast<double>(TransferTermsEditor::FEE_STEP);
  return std::max(TransferTermsEditor::FEE_STEP,
                  static_cast<std::int64_t>(std::llround(amount / step)) *
                      TransferTermsEditor::FEE_STEP);
}

ImVec4 stanceColor(PlayerStance stance)
{
  using enum BuyerNegotiation::PlayerStance;
  const Theme::Palette& palette = Theme::palette();
  switch (stance)
  {
    case AskedToLeave:
    case WantsBiggerClub:
      return palette.warning;
    case HappyHere:
      return palette.positive;
    case Reluctant:
      return palette.info;
    case Open:
      break;
  }
  return palette.muted;
}

/** "€12.0M · 60% upfront, 3 yrs · add-ons €1.5M · sell-on 15%". */
std::string termsLine(const TransferNegotiation::OfferTerms& terms)
{
  std::string text = Format::money(terms.fee) + " · " +
                     TransferTermsEditor::structureText(terms);
  if (const std::int64_t add_ons =
          static_cast<std::int64_t>(terms.appearance_bonus) +
          static_cast<std::int64_t>(terms.goal_bonus);
      add_ons > 0)
    text += fmt::sprintf(LOC("OFFER_TERMS_ADD_ONS"), Format::money(add_ons));
  if (terms.sell_on_percent > 0)
    text += fmt::sprintf(LOC("OFFER_TERMS_SELL_ON"),
                         static_cast<int>(terms.sell_on_percent));
  return text;
}
}  // namespace

void OfferNegotiationDialog::open(GameController& controller, std::uint32_t id)
{
  offer_id = id;
  finished = false;
  notice.clear();
  mode = Mode::Counter;
  rebuild(controller);
  if (view && view->loan)
  {
    loan_counter = view->loan_terms;
    loan_counter.wage_share = static_cast<std::uint8_t>(
        std::min(100, loan_counter.wage_share + OPENING_LOAN_SHARE_RISE));
    TransferTermsEditor::snapLoanToOptions(loan_counter);
    refreshCounterTexts();
  }
  else if (view)
  {
    counter = view->terms;
    counter.fee = static_cast<std::uint32_t>(
        std::min<std::int64_t>(roundedFee(view->terms.fee * OPENING_COUNTER),
                               std::numeric_limits<std::uint32_t>::max()));
    price = std::max<std::int64_t>(view->asking_price,
                                   roundedFee(view->terms.fee * OPENING_PRICE));
    TransferTermsEditor::snapToOptions(counter);
    refreshCounterTexts();
  }
  open_requested = true;
}

void OfferNegotiationDialog::refreshCounterTexts()
{
  if (view && view->loan)
  {
    const int weeks =
        loan_counter.duration == TransferNegotiation::LoanDuration::SixMonths
            ? std::min(SIX_MONTH_WEEKS, view->season_weeks)
            : view->season_weeks;
    const std::int64_t weekly = static_cast<std::int64_t>(view->weekly_wage) *
                                loan_counter.wage_share / 100;
    loan_wage_text =
        fmt::sprintf(LOC("TRANSFER_PER_WEEK"), Format::moneyFull(weekly));
    loan_saving_text = Format::moneyFull(
        weekly * weeks + static_cast<std::int64_t>(loan_counter.loan_fee));
    return;
  }
  counter_upfront_text =
      Format::moneyFull(TransferNegotiation::upfrontAmount(counter));
  counter_worth_text = Format::moneyFull(static_cast<std::int64_t>(std::llround(
      TransferNegotiation::sellerValue(counter, view ? view->age : 25))));
}

void OfferNegotiationDialog::rebuild(const GameController& controller)
{
  view = controller.getIncomingOfferView(offer_id);
  facts.clear();
  bid_lines.clear();
  rounds.clear();
  counter_chips.clear();
  proposal_text.clear();
  if (!view) return;
  const Theme::Palette& palette = Theme::palette();
  const GameController::IncomingOfferView& offer = *view;

  title =
      fmt::sprintf(LOC(offer.loan ? "LOAN_TALKS_TITLE" : "OFFER_TALKS_TITLE"),
                   offer.buyer_name, offer.player_name);
  subtitle = fmt::sprintf(LOC("OFFER_TALKS_SUBTITLE"),
                          LOC(squadRoleKey(offer.role)), offer.age);
  // How he feels about a permanent move; a loan does not ask him to leave.
  stance_text =
      offer.loan ? std::string()
                 : fmt::sprintf(LOC("OFFER_STANCE_LINE"),
                                LOC(BuyerNegotiation::stanceKey(offer.stance)));
  stance_color = stanceColor(offer.stance);

  facts.emplace_back(LOC("OFFER_FACT_VALUE"),
                     Format::moneyFull(offer.market_value));
  facts.emplace_back(LOC("OFFER_FACT_ASKING"),
                     offer.asking_price > 0
                         ? Format::moneyFull(offer.asking_price)
                         : std::string(LOC("OFFER_FACT_NOT_LISTED")));
  facts.emplace_back(
      LOC("OFFER_FACT_CONTRACT"),
      fmt::sprintf(Format::plural("OFFER_FACT_YEARS", offer.contract_years),
                   static_cast<int>(offer.contract_years)));
  if (offer.release_clause > 0)
    facts.emplace_back(LOC("TRANSFER_FIELD_RELEASE_CLAUSE"),
                       Format::moneyFull(offer.release_clause));
  if (offer.loan)
    facts.emplace_back(LOC("TRANSFER_FIELD_WAGE"),
                       fmt::sprintf(LOC("TRANSFER_PER_WEEK"),
                                    Format::moneyFull(offer.weekly_wage)));
  if (offer.rivals > 0)
    facts.emplace_back(
        LOC("OFFER_FACT_RIVALS"),
        fmt::sprintf(Format::plural("OFFER_FACT_RIVAL_CLUBS", offer.rivals),
                     static_cast<int>(offer.rivals)));

  if (offer.loan)
    rebuildLoanLines();
  else
    rebuildTransferLines();

  for (const OfferRound& round : offer.history)
  {
    RoundLine line;
    line.buyer = BuyerNegotiation::byBuyer(round.move);
    line.who =
        line.buyer ? offer.buyer_name : std::string(LOC("OFFER_WHO_CLUB"));
    line.text = fmt::sprintf("%s · %s", Format::dayMonth(round.date),
                             LOC(BuyerNegotiation::moveKey(round.move)));
    if (round.move != BuyerNegotiation::Move::WalkedAway)
      line.detail = offer.loan
                        ? TransferTermsEditor::loanTermsLine(round.loan_terms)
                        : termsLine(round.terms);
    rounds.push_back(std::move(line));
  }

  // Money chips of the answer panel.
  const auto value = static_cast<std::int64_t>(offer.market_value);
  const auto bid = static_cast<std::int64_t>(offer.terms.fee);
  counter_chips = {{LOC("TRANSFER_CHIP_THEIR_OFFER"), bid},
                   {LOC("TRANSFER_CHIP_VALUE"), value}};
  if (offer.asking_price > 0)
    counter_chips.push_back({LOC("TRANSFER_CHIP_ASKING"), offer.asking_price});
  counter_chips.push_back({LOC("TRANSFER_CHIP_PLUS_10"),
                           roundedFee(static_cast<double>(bid) * 1.1)});
  counter_chips.push_back({LOC("TRANSFER_CHIP_PLUS_25"),
                           roundedFee(static_cast<double>(bid) * 1.25)});
  price_chips = {{{LOC("TRANSFER_CHIP_MARKET_VALUE"), value},
                  {LOC("TRANSFER_CHIP_PLUS_25"),
                   roundedFee(static_cast<double>(value) * 1.25)},
                  {LOC("TRANSFER_CHIP_PLUS_50"),
                   roundedFee(static_cast<double>(value) * 1.5)},
                  {LOC("TRANSFER_CHIP_THEIR_OFFER"), bid}}};
  if (offer.status == OfferStatus::AwaitingBuyer)
    proposal_text = fmt::sprintf(
        LOC("OFFER_YOUR_PROPOSAL"),
        offer.loan ? TransferTermsEditor::loanTermsLine(offer.asked_loan)
                   : termsLine(offer.asked));
  refreshCounterTexts();

  const int days_left = std::max(
      0, dayOrdinal(offer.expires) - dayOrdinal(controller.getCurrentDate()));
  if (offer.status == OfferStatus::AwaitingBuyer)
  {
    status_text = fmt::sprintf(LOC("OFFER_STATUS_AWAITING"), offer.buyer_name,
                               Format::date(offer.respond_on));
    status_color = palette.info;
  }
  else if (!offer.window_open)
  {
    status_text = LOC("OFFER_STATUS_WINDOW_SHUT");
    status_color = palette.negative;
  }
  else if (offer.final_offer)
  {
    status_text = LOC("OFFER_STATUS_FINAL");
    status_color = palette.warning;
  }
  else if (offer.days_to_deadline <= DEADLINE_WARNING_DAYS)
  {
    status_text = LOC("OFFER_STATUS_DEADLINE");
    status_color = palette.warning;
  }
  else
  {
    status_text =
        fmt::sprintf(Format::plural("OFFER_STATUS_ANSWER_BY", days_left),
                     Format::date(offer.expires), days_left);
    status_color = palette.muted;
  }
}

void OfferNegotiationDialog::rebuildTransferLines()
{
  const GameController::IncomingOfferView& offer = *view;
  const TransferNegotiation::OfferTerms& terms = offer.terms;
  const auto upfront = TransferNegotiation::upfrontAmount(terms);
  bid_lines.emplace_back(LOC("TRANSFER_SUMMARY_FEE"),
                         Format::moneyFull(terms.fee));
  bid_lines.emplace_back(
      LOC("TRANSFER_SUMMARY_UPFRONT"),
      fmt::sprintf(LOC("OFFER_UPFRONT_VALUE"), Format::moneyFull(upfront),
                   terms.instalment_years > 0 ? terms.upfront_percent : 100));
  if (const auto instalments = TransferNegotiation::instalmentAmounts(terms);
      !instalments.empty())
    bid_lines.emplace_back(
        LOC("TRANSFER_SUMMARY_INSTALMENTS"),
        fmt::sprintf(LOC("TRANSFER_SUMMARY_INSTALMENTS_VALUE"),
                     static_cast<int>(instalments.size()),
                     Format::money(instalments.front())));
  if (terms.appearance_bonus > 0)
    bid_lines.emplace_back(
        LOC("TRANSFER_FIELD_APPEARANCE_BONUS"),
        fmt::sprintf(LOC("OFFER_ADD_ON_APPS"),
                     Format::money(terms.appearance_bonus),
                     static_cast<int>(terms.appearance_target)));
  if (terms.goal_bonus > 0)
    bid_lines.emplace_back(
        LOC("TRANSFER_FIELD_GOAL_BONUS"),
        fmt::sprintf(LOC("OFFER_ADD_ON_GOALS"), Format::money(terms.goal_bonus),
                     static_cast<int>(terms.goal_target)));
  bid_lines.emplace_back(
      LOC("TRANSFER_FIELD_SELL_ON"),
      terms.sell_on_percent > 0
          ? fmt::sprintf("%d%%", static_cast<int>(terms.sell_on_percent))
          : std::string(LOC("OFFER_NONE")));
  bid_lines.emplace_back(
      LOC("OFFER_WORTH_TO_YOU"),
      Format::moneyFull(static_cast<std::int64_t>(
          std::llround(TransferNegotiation::sellerValue(terms, offer.age)))));
  bid_total = Format::moneyFull(static_cast<std::int64_t>(terms.fee) +
                                terms.appearance_bonus + terms.goal_bonus);
}

void OfferNegotiationDialog::rebuildLoanLines()
{
  const GameController::IncomingOfferView& offer = *view;
  const TransferNegotiation::LoanTerms& terms = offer.loan_terms;
  const int weeks =
      terms.duration == TransferNegotiation::LoanDuration::SixMonths
          ? std::min(SIX_MONTH_WEEKS, offer.season_weeks)
          : offer.season_weeks;
  const std::int64_t weekly =
      static_cast<std::int64_t>(offer.weekly_wage) * terms.wage_share / 100;
  bid_lines.emplace_back(
      LOC("TRANSFER_FIELD_DURATION"),
      LOC(terms.duration == TransferNegotiation::LoanDuration::SeasonEnd
              ? "TRANSFER_LOAN_SEASON"
              : "TRANSFER_LOAN_SIX_MONTHS"));
  bid_lines.emplace_back(
      LOC("TRANSFER_FIELD_WAGE_SHARE"),
      fmt::sprintf(LOC("LOAN_SHARE_VALUE"), static_cast<int>(terms.wage_share),
                   Format::money(weekly)));
  bid_lines.emplace_back(LOC("TRANSFER_FIELD_LOAN_FEE"),
                         terms.loan_fee > 0 ? Format::moneyFull(terms.loan_fee)
                                            : std::string(LOC("OFFER_NONE")));
  bid_lines.emplace_back(
      LOC("LOAN_FIELD_PURCHASE"),
      terms.option_fee == 0
          ? std::string(LOC("OFFER_NONE"))
          : fmt::sprintf(LOC(terms.obligation ? "LOAN_PURCHASE_OBLIGATION_VALUE"
                                              : "LOAN_PURCHASE_OPTION_VALUE"),
                         Format::moneyFull(terms.option_fee)));
  bid_lines.emplace_back(
      LOC("TRANSFER_FIELD_RECALL"),
      LOC(terms.recall_clause ? "LOAN_RECALL_YES" : "LOAN_RECALL_NO"));
  bid_lines.emplace_back(
      LOC("LOAN_FIELD_MIN_APPS"),
      terms.min_appearances == 0
          ? std::string(LOC("OFFER_NONE"))
          : fmt::sprintf(LOC("LOAN_APPS_VALUE"),
                         static_cast<int>(terms.min_appearances),
                         Format::money(terms.unplayed_fee)));
  bid_total = Format::moneyFull(weekly * weeks +
                                static_cast<std::int64_t>(terms.loan_fee));
}

bool OfferNegotiationDialog::render(GameController& controller)
{
  if (open_requested)
  {
    open_requested = false;
    ImGui::OpenPopup(POPUP_ID);
  }
  visible = ImGui::IsPopupOpen(POPUP_ID);
  if (!visible) return false;

  // The buyer may have answered since the dialog was built.
  if (!finished && view)
  {
    const auto& offers = controller.getIncomingOffers();
    const auto found = std::ranges::find(offers, offer_id, &IncomingOffer::id);
    if (found == offers.end() || found->status != view->status ||
        found->history.size() != view->history.size())
      rebuild(controller);
  }

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const bool wide = viewport->WorkSize.x >= scaled(TWO_COLUMN_MIN_VIEWPORT);
  const float width = std::min(scaled(wide ? WIDE_DIALOG_WIDTH : DIALOG_WIDTH),
                               viewport->WorkSize.x * VIEWPORT_WIDTH_SHARE);
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(width, 0.0f),
      ImVec2(width, viewport->WorkSize.y * VIEWPORT_HEIGHT_SHARE));
  if (!ImGui::BeginPopupModal(POPUP_ID, nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize |
                                  ImGuiWindowFlags_NoTitleBar |
                                  ImGuiWindowFlags_NoSavedSettings))
  {
    visible = false;
    return false;
  }
  changed = false;
  bool close = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
  if (finished)
  {
    renderResult();
    ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));
    close = UI::primaryButton(LOC("OFFER_ACTION_CLOSE")) || close;
  }
  else if (!view)
  {
    UI::emptyState(LOC("OFFER_TALKS_GONE_TITLE"), LOC("OFFER_TALKS_GONE_BODY"));
    close = UI::primaryButton(LOC("OFFER_ACTION_CLOSE")) || close;
  }
  else
  {
    renderHeader();
    if (wide && ImGui::BeginTable("##offer_columns", 2,
                                  ImGuiTableFlags_SizingStretchSame))
    {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      renderBid();
      renderHistory();
      ImGui::TableNextColumn();
      renderAnswer();
      ImGui::EndTable();
    }
    else
    {
      renderBid();
      renderHistory();
      renderAnswer();
    }
    ImGui::Separator();
    if (UI::secondaryButton(LOC("OFFER_ACTION_CLOSE"))) close = true;
    renderActions(controller);
  }
  if (close)
  {
    ImGui::CloseCurrentPopup();
    visible = false;
  }
  ImGui::EndPopup();
  return changed;
}

void OfferNegotiationDialog::renderHeader() const
{
  const Theme::Palette& palette = Theme::palette();
  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    UI::textFitted(title, ImGui::GetContentRegionAvail().x, palette.text);
  }
  UI::textFitted(subtitle, ImGui::GetContentRegionAvail().x, palette.muted);
  ImGui::PushTextWrapPos(0.0f);
  if (!stance_text.empty())
    ImGui::TextColored(stance_color, "%s", stance_text.c_str());
  ImGui::TextColored(status_color, "%s", status_text.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  for (const Line& fact : facts)
    UI::summaryRow(fact.label.c_str(), fact.value.c_str());
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
}

void OfferNegotiationDialog::renderBid() const
{
  if (!beginPanel("##offer_bid",
                  LOC(view->loan ? "LOAN_OFFER_TITLE" : "OFFER_BID_TITLE")))
    return;
  for (const Line& line : bid_lines)
    UI::summaryRow(line.label.c_str(), line.value.c_str());
  UI::summaryRow(LOC(view->loan ? "LOAN_SAVING_TOTAL" : "OFFER_HEADLINE_TOTAL"),
                 bid_total.c_str(), nullptr, true);
  endPanel();
}

void OfferNegotiationDialog::renderHistory() const
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
  if (!beginPanel("##offer_history", LOC("OFFER_HISTORY_TITLE"))) return;
  ImGui::PushTextWrapPos(0.0f);
  for (const RoundLine& line : rounds)
  {
    ImGui::TextColored(line.buyer ? palette.text : palette.info, "%s",
                       line.who.c_str());
    ImGui::SameLine();
    ImGui::TextColored(palette.muted, "%s", line.text.c_str());
    if (!line.detail.empty())
      ImGui::TextColored(palette.faint, "%s", line.detail.c_str());
  }
  ImGui::PopTextWrapPos();
  endPanel();
}

void OfferNegotiationDialog::renderAnswer()
{
  const Theme::Palette& palette = Theme::palette();
  if (!beginPanel("##offer_answer", LOC("OFFER_ANSWER_TITLE"))) return;
  if (view->status == OfferStatus::AwaitingBuyer)
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.info, "%s", status_text.c_str());
    ImGui::TextColored(palette.muted, "%s", proposal_text.c_str());
    ImGui::PopTextWrapPos();
    endPanel();
    return;
  }
  if (view->loan)
  {
    // A loan is countered with terms only.
    if (TransferTermsEditor::editLoan(loan_counter, view->market_value))
      refreshCounterTexts();
    ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
    UI::summaryRow(LOC("TRANSFER_SUMMARY_WEEKLY_SHARE"),
                   loan_wage_text.c_str());
    UI::summaryRow(LOC("LOAN_SAVING_TOTAL"), loan_saving_text.c_str());
  }
  else
  {
    auto selected = std::to_underlying(mode);
    if (const std::array<const char*, 2> modes = {LOC("OFFER_MODE_COUNTER"),
                                                  LOC("OFFER_MODE_PRICE")};
        UI::segmented("##answer_mode", selected, modes))
      mode = static_cast<Mode>(selected);
    if (mode == Mode::Counter)
    {
      if (TransferTermsEditor::edit(counter, counter_chips))
        refreshCounterTexts();
      ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
      UI::summaryRow(LOC("TRANSFER_SUMMARY_UPFRONT"),
                     counter_upfront_text.c_str());
      UI::summaryRow(LOC("OFFER_WORTH_TO_YOU"), counter_worth_text.c_str());
    }
    else
    {
      formLabel(LOC("OFFER_PRICE_FIELD"));
      UI::moneyInput("##asking_price", price,
                     {.minimum = TransferTermsEditor::FEE_STEP,
                      .maximum = std::numeric_limits<std::uint32_t>::max(),
                      .chips = price_chips});
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(palette.muted, "%s", LOC("OFFER_PRICE_HELP"));
      ImGui::PopTextWrapPos();
    }
  }
  if (!notice.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(notice_color, "%s", notice.c_str());
    ImGui::PopTextWrapPos();
  }
  endPanel();
}

void OfferNegotiationDialog::renderActions(GameController& controller)
{
  const bool talking = view->status == OfferStatus::AwaitingClub;
  const bool window = view->window_open;
  const auto fits = [](const char* label)
  { return UI::sameLineIfFits(UI::buttonWidth(label)); };

  const char* rejectLabel = LOC("OFFER_ACTION_REJECT");
  fits(rejectLabel);
  if (UI::secondaryButton(rejectLabel))
  {
    reject(controller);
    return;
  }
  if (!view->loan)
  {
    const char* notForSaleLabel = LOC("OFFER_ACTION_NOT_FOR_SALE");
    fits(notForSaleLabel);
    if (UI::secondaryButton(notForSaleLabel))
    {
      notForSale(controller);
      return;
    }
  }
  const char* submitLabel =
      LOC(view->loan || mode == Mode::Counter ? "OFFER_ACTION_COUNTER"
                                              : "OFFER_ACTION_NAME_PRICE");
  bool valid = price > 0;
  if (view->loan)
    valid = LoanNegotiation::isValid(loan_counter);
  else if (mode == Mode::Counter)
    valid = TransferNegotiation::isValid(counter) && counter.fee > 0;
  fits(submitLabel);
  ImGui::BeginDisabled(!talking || !window || !valid);
  if (UI::secondaryButton(submitLabel))
  {
    ImGui::EndDisabled();
    submit(controller);
    return;
  }
  ImGui::EndDisabled();
  const char* acceptLabel = LOC("OFFER_ACTION_ACCEPT");
  fits(acceptLabel);
  // Selling him would leave the squad short: the offer can wait.
  const bool blocked =
      view->sale_block != GameController::PlayerActionBlock::None;
  ImGui::BeginDisabled(!talking || !window || blocked);
  if (UI::primaryButton(acceptLabel))
  {
    ImGui::EndDisabled();
    accept(controller);
    return;
  }
  ImGui::EndDisabled();
  if (blocked && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip(
        "%s", LOC(GameController::playerActionBlockKey(view->sale_block)));
}

void OfferNegotiationDialog::renderResult() const
{
  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(result_color, "%s", result_headline.c_str());
    ImGui::PopTextWrapPos();
  }
  if (!result_detail.empty())
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(Theme::palette().muted, "%s", result_detail.c_str());
    ImGui::PopTextWrapPos();
  }
}

void OfferNegotiationDialog::finish(std::string_view headline,
                                    const ImVec4& color,
                                    std::string_view detail)
{
  finished = true;
  result_headline.assign(headline);
  result_color = color;
  result_detail.assign(detail);
}

void OfferNegotiationDialog::accept(GameController& controller)
{
  if (!view) return;
  const GameController::IncomingOfferView offer = *view;
  const Outcome outcome = controller.settleIncomingOffer(offer_id);
  changed = true;
  const Theme::Palette& palette = Theme::palette();
  if (outcome == Outcome::SquadTooSmall)
  {
    rebuild(controller);
    notice = LOC(GameController::playerActionBlockKey(
        view ? view->sale_block
             : GameController::PlayerActionBlock::SquadFloor));
    notice_color = palette.negative;
    return;
  }
  if (offer.loan && outcome == Outcome::Sold)
  {
    finish(fmt::sprintf(LOC("LOAN_RESULT_AGREED"), offer.player_name,
                        offer.buyer_name),
           palette.positive,
           TransferTermsEditor::loanTermsLine(offer.loan_terms));
    return;
  }
  switch (outcome)
  {
    case Outcome::Sold:
      finish(fmt::sprintf(LOC("OFFER_RESULT_SOLD"), offer.player_name,
                          offer.buyer_name),
             palette.positive,
             fmt::sprintf(LOC("OFFER_RESULT_SOLD_DETAIL"),
                          Format::moneyFull(
                              TransferNegotiation::upfrontAmount(offer.terms)),
                          termsLine(offer.terms)));
      break;
    case Outcome::TermsRefused:
      finish(fmt::sprintf(LOC("OFFER_RESULT_TERMS_REFUSED"), offer.player_name,
                          offer.buyer_name),
             palette.negative, LOC("OFFER_RESULT_TERMS_REFUSED_DETAIL"));
      break;
    default:
      finish(LOC("OFFER_RESULT_FAILED"), palette.negative,
             LOC("OFFER_RESULT_FAILED_DETAIL"));
      break;
  }
}

void OfferNegotiationDialog::reject(GameController& controller)
{
  if (!view) return;
  const GameController::IncomingOfferView offer = *view;
  controller.rejectIncomingOffer(offer_id);
  changed = true;
  const bool keen =
      !offer.loan && (offer.stance == PlayerStance::AskedToLeave ||
                      offer.stance == PlayerStance::WantsBiggerClub);
  finish(
      fmt::sprintf(LOC("OFFER_RESULT_REJECTED"), offer.buyer_name),
      Theme::palette().text,
      keen ? fmt::sprintf(LOC("OFFER_RESULT_REJECTED_UPSET"), offer.player_name)
           : std::string());
}

void OfferNegotiationDialog::notForSale(GameController& controller)
{
  if (!view) return;
  const GameController::IncomingOfferView offer = *view;
  const bool done = controller.declareNotForSale(offer_id);
  changed = true;
  if (!done)
  {
    rebuild(controller);
    return;
  }
  finish(fmt::sprintf(LOC("OFFER_RESULT_NOT_FOR_SALE"), offer.player_name),
         Theme::palette().info, LOC("OFFER_RESULT_NOT_FOR_SALE_DETAIL"));
}

void OfferNegotiationDialog::submit(GameController& controller)
{
  if (!view) return;
  const GameController::IncomingOfferView offer = *view;
  Outcome outcome;
  if (offer.loan)
    outcome = controller.counterLoanOffer(offer_id, loan_counter);
  else if (mode == Mode::Counter)
    outcome = controller.counterIncomingOffer(offer_id, counter);
  else
    outcome = controller.nameAskingPrice(
        offer_id, static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                      price, 0, std::numeric_limits<std::uint32_t>::max())));
  changed = true;
  const Theme::Palette& palette = Theme::palette();
  using enum GameController::OfferOutcome;
  switch (outcome)
  {
    case AwaitingReply:
      rebuild(controller);
      notice = view ? fmt::sprintf(LOC("OFFER_NOTICE_SENT"), offer.buyer_name,
                                   Format::date(view->respond_on))
                    : std::string();
      notice_color = palette.info;
      return;
    case Countered:
      rebuild(controller);
      notice = fmt::sprintf(LOC("OFFER_NOTICE_COUNTERED"), offer.buyer_name);
      notice_color = palette.warning;
      return;
    case Sold:
      if (offer.loan)
        finish(fmt::sprintf(LOC("LOAN_RESULT_AGREED"), offer.player_name,
                            offer.buyer_name),
               palette.positive,
               TransferTermsEditor::loanTermsLine(loan_counter));
      else
        finish(fmt::sprintf(LOC("OFFER_RESULT_AGREED"), offer.buyer_name,
                            offer.player_name),
               palette.positive, LOC("OFFER_RESULT_AGREED_DETAIL"));
      return;
    case WalkedAway:
      finish(fmt::sprintf(LOC("OFFER_RESULT_WALKED_AWAY"), offer.buyer_name),
             palette.negative, LOC("OFFER_RESULT_WALKED_AWAY_DETAIL"));
      return;
    case TermsRefused:
      finish(fmt::sprintf(LOC("OFFER_RESULT_TERMS_REFUSED"), offer.player_name,
                          offer.buyer_name),
             palette.negative, LOC("OFFER_RESULT_TERMS_REFUSED_DETAIL"));
      return;
    case SquadTooSmall:
      rebuild(controller);
      notice = LOC("BLOCK_SQUAD_FLOOR");
      notice_color = palette.negative;
      return;
    case Rejected:
    case Failed:
      break;
  }
  rebuild(controller);
  notice = LOC("OFFER_NOTICE_FAILED");
  notice_color = palette.negative;
}

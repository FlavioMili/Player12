// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/transfer_terms_editor.h"

#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <limits>

#include "global/language_manager.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "model/transfer_tuning.h"

namespace TransferTermsEditor
{
void formLabel(const char* text)
{
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  Theme::ScopedText caption(Theme::Text::SMALL);
  ImGui::TextColored(Theme::palette().muted, "%s", text);
}

void alignActions(std::initializer_list<const char*> labels)
{
  float total = 0.0f;
  int count = 0;
  for (const char* label : labels)
  {
    if (label == nullptr) continue;
    total += UI::buttonWidth(label);
    ++count;
  }
  total += ImGui::GetStyle().ItemSpacing.x * static_cast<float>(count - 1);
  ImGui::SetCursorPosX(
      ImGui::GetCursorPosX() +
      std::max(0.0f, ImGui::GetContentRegionAvail().x - total));
}

bool beginPanel(const char* id, const char* title)
{
  const float pad = Theme::Space::M * Theme::scale();
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(pad, pad));
  ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, Theme::palette().border);
  const bool open = ImGui::BeginTable(
      id, 1, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_PadOuterX,
      ImVec2(-FLT_MIN, 0.0f));
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
  if (!open) return false;
  ImGui::TableSetupColumn("##panel", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                         Theme::toU32(Theme::palette().surface));
  if (title != nullptr && *title != '\0') UI::sectionLabel(title);
  return true;
}

void endPanel() { ImGui::EndTable(); }

namespace
{
/** Target segment labels ("20 apps"), rebuilt only when the language's
 * pattern changes, never per frame. */
struct TargetLabels
{
  std::string pattern;
  std::array<std::string, 6> texts;
  std::array<const char*, 6> labels{};
};

std::span<const char* const> targetLabels(
    TargetLabels& cache, const char* key,
    std::span<const std::uint16_t> options)
{
  if (const char* pattern = LOC(key); cache.pattern != pattern)
  {
    cache.pattern = pattern;
    for (std::size_t index = 0; index < options.size(); ++index)
    {
      cache.texts[index] = fmt::sprintf(pattern, options[index]);
      cache.labels[index] = cache.texts[index].c_str();
    }
  }
  return std::span<const char* const>(cache.labels).first(options.size());
}

/** Guaranteed appearance targets after "None". */
constexpr std::array<std::uint16_t, 6> LOAN_APPEARANCE_TARGETS = {5,  10, 15,
                                                                  20, 25, 30};

template <typename T, std::size_t N, typename V>
bool snap(const std::array<T, N>& options, V& value)
{
  const T nearest =
      options[static_cast<std::size_t>(optionIndex(options, value))];
  if (static_cast<int>(nearest) == static_cast<int>(value)) return false;
  value = static_cast<V>(nearest);
  return true;
}
}  // namespace

bool snapToOptions(TransferNegotiation::OfferTerms& terms)
{
  bool changed = snap(UPFRONT_OPTIONS, terms.upfront_percent);
  changed = snap(SELL_ON_OPTIONS, terms.sell_on_percent) || changed;
  if (terms.upfront_percent < 100 && terms.instalment_years == 0)
  {
    terms.instalment_years = DEFAULT_INSTALMENT_YEARS;
    changed = true;
  }
  if (terms.instalment_years > TransferTuning::Offer::MAX_INSTALMENT_YEARS)
  {
    terms.instalment_years = TransferTuning::Offer::MAX_INSTALMENT_YEARS;
    changed = true;
  }
  if (terms.appearance_bonus > 0)
    changed = snap(APPEARANCE_TARGETS, terms.appearance_target) || changed;
  if (terms.goal_bonus > 0)
    changed = snap(GOAL_TARGETS, terms.goal_target) || changed;
  return changed;
}

bool edit(TransferNegotiation::OfferTerms& terms,
          std::span<const UI::MoneyChip> fee_chips)
{
  bool changed = snapToOptions(terms);
  formLabel(LOC("TRANSFER_FIELD_FEE"));
  if (std::int64_t fee = terms.fee;
      UI::moneyInput("##fee", fee,
                     {.minimum = FEE_STEP,
                      .maximum = std::numeric_limits<std::uint32_t>::max(),
                      .chips = fee_chips}))
  {
    terms.fee = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
        fee, FEE_STEP, std::numeric_limits<std::uint32_t>::max()));
    changed = true;
  }

  formLabel(LOC("TRANSFER_FIELD_UPFRONT"));
  if (int upfrontIndex = optionIndex(UPFRONT_OPTIONS, terms.upfront_percent);
      UI::segmented("##upfront", upfrontIndex, UPFRONT_LABELS))
  {
    terms.upfront_percent =
        UPFRONT_OPTIONS[static_cast<std::size_t>(upfrontIndex)];
    if (terms.upfront_percent < 100 && terms.instalment_years == 0)
      terms.instalment_years = DEFAULT_INSTALMENT_YEARS;
    changed = true;
  }
  if (terms.upfront_percent < 100)
  {
    formLabel(LOC("TRANSFER_FIELD_INSTALMENTS"));
    int yearIndex = std::max(0, terms.instalment_years - 1);
    if (UI::segmented("##years", yearIndex,
                      std::span(YEAR_LABELS)
                          .first(TransferTuning::Offer::MAX_INSTALMENT_YEARS)))
    {
      terms.instalment_years = static_cast<std::uint8_t>(yearIndex + 1);
      changed = true;
    }
  }
  else
  {
    terms.instalment_years = 0;
  }

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * Theme::scale()));
  const bool addOns = beginPanel("##offer_add_ons", LOC("TRANSFER_ADD_ONS"));
  static TargetLabels appearance_labels;
  static TargetLabels goal_labels;
  const auto bonusRow = [&](const char* label, const char* id,
                            std::uint32_t& bonus, std::uint16_t& target,
                            std::span<const std::uint16_t> options,
                            const char* target_key, TargetLabels& cache)
  {
    formLabel(label);
    if (std::int64_t amount = bonus; UI::moneyInput(
            id, amount, {.maximum = std::numeric_limits<std::uint32_t>::max()}))
    {
      bonus = static_cast<std::uint32_t>(amount);
      changed = true;
    }
    if (bonus == 0)
    {
      target = 0;
      return;
    }
    int targetIndex = optionIndex(options, target);
    if (target == 0) target = options[static_cast<std::size_t>(targetIndex)];
    ImGui::PushID(id);
    if (UI::segmented("##target", targetIndex,
                      targetLabels(cache, target_key, options)))
    {
      target = options[static_cast<std::size_t>(targetIndex)];
      changed = true;
    }
    ImGui::PopID();
  };
  bonusRow(LOC("TRANSFER_FIELD_APPEARANCE_BONUS"), "##app_bonus",
           terms.appearance_bonus, terms.appearance_target, APPEARANCE_TARGETS,
           "TRANSFER_APPS_SHORT", appearance_labels);
  bonusRow(LOC("TRANSFER_FIELD_GOAL_BONUS"), "##goal_bonus", terms.goal_bonus,
           terms.goal_target, GOAL_TARGETS, "TRANSFER_GOALS_SHORT",
           goal_labels);
  formLabel(LOC("TRANSFER_FIELD_SELL_ON"));
  if (int sellOnIndex = optionIndex(SELL_ON_OPTIONS, terms.sell_on_percent);
      UI::segmented("##sell_on", sellOnIndex, SELL_ON_LABELS))
  {
    terms.sell_on_percent =
        SELL_ON_OPTIONS[static_cast<std::size_t>(sellOnIndex)];
    changed = true;
  }
  if (addOns) endPanel();
  return changed;
}

bool snapLoanToOptions(TransferNegotiation::LoanTerms& terms)
{
  bool changed = false;
  if (const int share =
          std::clamp((terms.wage_share + LOAN_WAGE_SHARE_STEP / 2) /
                         LOAN_WAGE_SHARE_STEP * LOAN_WAGE_SHARE_STEP,
                     0, 100);
      share != terms.wage_share)
  {
    terms.wage_share = static_cast<std::uint8_t>(share);
    changed = true;
  }
  if (terms.min_appearances > 0)
    changed = snap(MIN_APPEARANCE_OPTIONS, terms.min_appearances) || changed;
  return changed;
}

bool editLoan(TransferNegotiation::LoanTerms& terms, std::uint32_t market_value)
{
  using TransferNegotiation::LoanDuration;
  bool changed = snapLoanToOptions(terms);
  formLabel(LOC("TRANSFER_FIELD_DURATION"));
  int duration = terms.duration == LoanDuration::SeasonEnd ? 0 : 1;
  if (const std::array<const char*, 2> durations =
          {LOC("TRANSFER_LOAN_SEASON"), LOC("TRANSFER_LOAN_SIX_MONTHS")};
      UI::segmented("##loan_duration", duration, durations))
  {
    terms.duration =
        duration == 0 ? LoanDuration::SeasonEnd : LoanDuration::SixMonths;
    changed = true;
  }

  formLabel(LOC("TRANSFER_FIELD_WAGE_SHARE"));
  int share = terms.wage_share;
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::SliderInt("##loan_share", &share, 0, 100, "%d%%",
                       ImGuiSliderFlags_AlwaysClamp))
  {
    terms.wage_share = static_cast<std::uint8_t>(
        std::clamp((share + LOAN_WAGE_SHARE_STEP / 2) / LOAN_WAGE_SHARE_STEP *
                       LOAN_WAGE_SHARE_STEP,
                   0, 100));
    changed = true;
  }

  formLabel(LOC("TRANSFER_FIELD_LOAN_FEE"));
  if (std::int64_t fee = terms.loan_fee;
      UI::moneyInput("##loan_fee", fee,
                     {.maximum = std::numeric_limits<std::uint32_t>::max()}))
  {
    terms.loan_fee = static_cast<std::uint32_t>(fee);
    changed = true;
  }

  formLabel(LOC("LOAN_FIELD_PURCHASE"));
  int purchase = 1;
  if (terms.option_fee == 0 && !terms.obligation)
    purchase = 0;
  else if (terms.obligation)
    purchase = 2;
  const std::array<const char*, 3> purchases = {
      LOC("LOAN_PURCHASE_NONE"), LOC("LOAN_PURCHASE_OPTION"),
      LOC("LOAN_PURCHASE_OBLIGATION")};
  const auto option_value =
      static_cast<std::int64_t>(std::llround(
          static_cast<double>(market_value) *
          static_cast<double>(TransferTuning::Loan::OPTION_VALUE_MULTIPLE) /
          static_cast<double>(FEE_STEP))) *
      FEE_STEP;
  if (UI::segmented("##loan_purchase", purchase, purchases))
  {
    terms.obligation = purchase == 2;
    if (purchase == 0)
      terms.option_fee = 0;
    else if (terms.option_fee == 0)
      terms.option_fee =
          static_cast<std::uint32_t>(std::max(option_value, FEE_STEP));
    changed = true;
  }
  if (purchase != 0)
  {
    formLabel(LOC("TRANSFER_FIELD_OPTION_FEE"));
    std::int64_t option = terms.option_fee;
    const std::array<UI::MoneyChip, 2> chips = {
        {{LOC("TRANSFER_CHIP_VALUE"), static_cast<std::int64_t>(market_value)},
         {LOC("TRANSFER_CHIP_OPTION_VALUE"), option_value}}};
    if (UI::moneyInput("##loan_option", option,
                       {.minimum = FEE_STEP,
                        .maximum = std::numeric_limits<std::uint32_t>::max(),
                        .chips = chips}))
    {
      terms.option_fee = static_cast<std::uint32_t>(option);
      changed = true;
    }
  }

  formLabel(LOC("TRANSFER_FIELD_RECALL"));
  int recall = terms.recall_clause ? 1 : 0;
  if (const std::array<const char*, 2> recalls = {LOC("LOAN_RECALL_NO"),
                                                  LOC("LOAN_RECALL_YES")};
      UI::segmented("##loan_recall", recall, recalls))
  {
    terms.recall_clause = recall == 1;
    changed = true;
  }

  formLabel(LOC("LOAN_FIELD_MIN_APPS"));
  static TargetLabels appearance_labels;
  // "None" first, then "5 apps", "10 apps"...
  const std::span<const char* const> targets =
      targetLabels(appearance_labels, "TRANSFER_APPS_SHORT",
                   std::span<const std::uint16_t>(LOAN_APPEARANCE_TARGETS));
  std::array<const char*, MIN_APPEARANCE_OPTIONS.size()> labels{};
  labels[0] = LOC("LOAN_APPS_NONE");
  for (std::size_t index = 0; index < targets.size(); ++index)
    labels[index + 1] = targets[index];
  if (int apps = optionIndex(MIN_APPEARANCE_OPTIONS, terms.min_appearances);
      UI::segmented("##loan_apps", apps, labels))
  {
    terms.min_appearances =
        MIN_APPEARANCE_OPTIONS[static_cast<std::size_t>(apps)];
    if (terms.min_appearances == 0) terms.unplayed_fee = 0;
    changed = true;
  }
  if (terms.min_appearances > 0)
  {
    formLabel(LOC("LOAN_FIELD_UNPLAYED_FEE"));
    if (std::int64_t unplayed = terms.unplayed_fee;
        UI::moneyInput("##loan_unplayed", unplayed,
                       {.minimum = FEE_STEP,
                        .maximum = std::numeric_limits<std::uint32_t>::max()}))
    {
      terms.unplayed_fee = static_cast<std::uint32_t>(unplayed);
      changed = true;
    }
    if (terms.unplayed_fee == 0)
    {
      terms.unplayed_fee = static_cast<std::uint32_t>(FEE_STEP);
      changed = true;
    }
  }
  return changed;
}

std::string loanTermsLine(const TransferNegotiation::LoanTerms& terms)
{
  std::string text = fmt::sprintf(
      LOC("LOAN_LINE_SHARE"), static_cast<int>(terms.wage_share),
      LOC(terms.duration == TransferNegotiation::LoanDuration::SeasonEnd
              ? "TRANSFER_LOAN_SEASON"
              : "TRANSFER_LOAN_SIX_MONTHS"));
  if (terms.loan_fee > 0)
    text += fmt::sprintf(LOC("LOAN_LINE_FEE"), Format::money(terms.loan_fee));
  if (terms.option_fee > 0)
    text += fmt::sprintf(
        LOC(terms.obligation ? "LOAN_LINE_OBLIGATION" : "LOAN_LINE_OPTION"),
        Format::money(terms.option_fee));
  if (terms.recall_clause) text += LOC("LOAN_LINE_RECALL");
  if (terms.min_appearances > 0)
    text += fmt::sprintf(LOC("LOAN_LINE_APPS"),
                         static_cast<int>(terms.min_appearances),
                         Format::money(terms.unplayed_fee));
  return text;
}

std::string structureText(const TransferNegotiation::OfferTerms& terms)
{
  if (terms.instalment_years == 0) return LOC("TRANSFER_TERMS_CASH");
  return fmt::sprintf(LOC("TRANSFER_TERMS_INSTALMENTS"),
                      static_cast<int>(terms.upfront_percent),
                      static_cast<int>(terms.instalment_years));
}

}  // namespace TransferTermsEditor

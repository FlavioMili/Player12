// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/contract_talks_dialog.h"

#include <fmt/format.h>
#include <fmt/printf.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>

#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/scenes/transfer_terms_editor.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/transfer_market.h"
#include "model/transfer_tuning.h"
#include "model/world_simulation.h"

namespace
{
using TransferNegotiation::ContractKind;
using TransferTermsEditor::beginPanel;
using TransferTermsEditor::endPanel;
using TransferTermsEditor::formLabel;
using Block = GameController::PlayerActionBlock;

constexpr const char* POPUP_ID = "###contract_talks";
constexpr float DIALOG_WIDTH = 640.0f;
constexpr float WIDE_DIALOG_WIDTH = 1000.0f;
constexpr float TWO_COLUMN_MIN_VIEWPORT = 1100.0f;
constexpr float VIEWPORT_WIDTH_SHARE = 0.94f;
constexpr float VIEWPORT_HEIGHT_SHARE = 0.9f;
constexpr std::int64_t WAGE_ROUNDING = 100;
constexpr auto WEEKS_PER_YEAR =
    static_cast<std::int64_t>(TransferTuning::Contract::WEEKS_PER_YEAR);

constexpr std::array<std::uint8_t, 5> RISE_OPTIONS = {0, 3, 5, 8, 10};
constexpr std::array<const char*, 5> RISE_LABELS = {"0%", "3%", "5%", "8%",
                                                    "10%"};
constexpr std::array<SquadRole, 5> PROMISE_ROLES = {
    SquadRole::KeyPlayer, SquadRole::FirstTeam, SquadRole::Rotation,
    SquadRole::Backup, SquadRole::Fringe};

float scaled(float value) { return value * Theme::scale(); }

std::int64_t scaledWage(std::int64_t wage, double factor)
{
  return std::llround(static_cast<double>(wage) * factor /
                      static_cast<double>(WAGE_ROUNDING)) *
         WAGE_ROUNDING;
}

std::uint32_t toMoney(std::int64_t value)
{
  return static_cast<std::uint32_t>(std::clamp<std::int64_t>(
      value, 0, std::numeric_limits<std::uint32_t>::max()));
}

const char* kindKey(ContractKind kind)
{
  using enum ContractKind;
  switch (kind)
  {
    case PreContract:
      return "TRANSFER_TALK_PRE_CONTRACT";
    case FreeAgent:
      return "TRANSFER_TALK_FREE";
    case Renewal:
      return "CONTRACT_TALK_RENEWAL";
    case Transfer:
      break;
  }
  return "TRANSFER_TALK_TRANSFER";
}
}  // namespace

bool ContractTalksDialog::open(GameController& controller, PlayerID id)
{
  const auto talk = controller.getContractTalkKind(id);
  const auto data = controller.getGameData();
  const auto who = data ? data->getPlayer(id) : std::nullopt;
  if (!talk || !who || !controller.getGame()) return false;
  const Player& p = who->get();
  kind = *talk;
  player_id = id;
  player = p.getName();
  const bool renewal = kind == ContractKind::Renewal;
  current_years = renewal ? p.getContractYears() : 0;
  last_year = TransferNegotiation::maxContractYears(p.getAge());
  first_year = static_cast<std::uint8_t>(renewal ? current_years + 1 : 1);
  if (first_year > last_year) return false;

  // A contract's seasons count the current one; a pre-contract starts with
  // the next.
  const GameDateValue today = controller.getCurrentDate();
  first_season_end = TransferNegotiation::seasonEndDate(today).year +
                     (kind == ContractKind::PreContract ? 1 : 0);
  const TeamID managed = controller.getGame()->getManagedTeamId();
  const auto club = controller.getManagedTeam();
  wage_room = club ? club->get().getFinances().getWageBudget() -
                         controller.getWeeklyWageBill(managed)
                   : 0;
  if (renewal) wage_room += p.getWage();
  if (kind == ContractKind::PreContract)
    wage_room =
        controller.getGame()->getTransfers().nextSeasonWageRoom(managed);
  budget = controller.transferBudgetForTeam(managed);

  response.reset();
  block = Block::None;
  over_budget = false;
  signed_up = false;
  signed_message.clear();
  promise_index = 0;
  agent_lines.clear();
  agent_lines.push_back(
      fmt::sprintf(LOC(controller.getAgentOpeningLine(id, kind)), player));
  title = fmt::sprintf(
      LOC(renewal ? "CONTRACT_RENEW_TITLE" : "TRANSFER_CONTRACT_TITLE"),
      player);
  kind_text = LOC(kindKey(kind));
  if (renewal)
    kind_text += fmt::sprintf(LOC("CONTRACT_CURRENT_END"),
                              first_season_end + current_years - 1);

  demand = controller.getPlayerDemand(id, kind);
  const int wanted = renewal
                         ? current_years + std::max<int>(demand.min_years, 1)
                         : demand.min_years;
  offer.years =
      static_cast<std::uint8_t>(std::clamp<int>(wanted, first_year, last_year));
  refreshDemands(controller);
  offer = PlayerAgent::askedOffer(agent, offer.years);
  offer.release_clause = 0;
  agent_fee = agent.agent_fee;
  offer.agent_fee = agent.agent_fee;
  rise_index =
      TransferTermsEditor::optionIndex(RISE_OPTIONS, offer.yearly_rise);
  offer.yearly_rise = RISE_OPTIONS[static_cast<std::size_t>(rise_index)];
  rounds_left = controller.getContractRoundsLeft(id);
  refreshYearLabels(controller);
  refreshSummary();
  open_requested = true;
  return true;
}

void ContractTalksDialog::refreshDemands(GameController& controller)
{
  demand = controller.getPlayerDemand(player_id, kind);
  agent = controller.getAgentDemands(player_id, kind,
                                     std::max<std::uint8_t>(offer.years, 1));
  projected = controller.getGame()->getTransfers().projectedRole(
      player_id, controller.getGame()->getManagedTeamId());
  demand_lines.clear();
  demand_lines.emplace_back(LOC("TRANSFER_FIELD_AGENT_ASK"),
                            fmt::sprintf(LOC("TRANSFER_PER_WEEK"),
                                         Format::moneyFull(agent.flat_wage)));
  demand_lines.emplace_back(
      LOC("AGENT_FIELD_PACKAGE"),
      fmt::sprintf(LOC("AGENT_PACKAGE_VALUE"), Format::money(agent.asking_wage),
                   static_cast<int>(agent.yearly_rise),
                   Format::money(agent.appearance_bonus)));
  if (agent.signing_bonus > 0)
    demand_lines.emplace_back(LOC("TRANSFER_FIELD_SIGNING_BONUS"),
                              Format::moneyFull(agent.signing_bonus));
  demand_lines.emplace_back(
      LOC("AGENT_FIELD_FEE"),
      fmt::sprintf(LOC("AGENT_FEE_VALUE"), Format::money(agent.agent_fee),
                   Format::money(agent.standard_agent_fee)));
  if (agent.wants_release_clause)
    demand_lines.emplace_back(
        LOC("TRANSFER_FIELD_RELEASE_CLAUSE"),
        fmt::sprintf(LOC("TRANSFER_CLAUSE_AT_MOST"),
                     Format::money(agent.max_release_clause)));
  std::string role = LOC(squadRoleKey(agent.desired_role));
  if (agent.wants_promise) role += LOC("AGENT_WANTS_PROMISE");
  demand_lines.emplace_back(LOC("TRANSFER_FIELD_DESIRED_ROLE"), role);
  demand_lines.emplace_back(LOC("TRANSFER_FIELD_PROJECTED_ROLE"),
                            LOC(squadRoleKey(projected)));
}

void ContractTalksDialog::refreshYearLabels(
    const GameController& /*controller*/)
{
  year_texts.clear();
  year_labels.clear();
  for (int years = first_year; years <= last_year; ++years)
    year_texts.push_back(std::to_string(first_season_end + years - 1));
  for (const std::string& text : year_texts)
    year_labels.push_back(text.c_str());
}

void ContractTalksDialog::refreshSummary()
{
  const std::int64_t yearly =
      static_cast<std::int64_t>(offer.weekly_wage) * WEEKS_PER_YEAR;
  yearly_text = Format::moneyFull(yearly);
  total_text = Format::moneyFull(
      yearly * offer.years + static_cast<std::int64_t>(offer.signing_bonus));
  rounds_text = fmt::sprintf(LOC("TRANSFER_ROUNDS_LEFT"), rounds_left);
}

ContractTalksDialog::Event ContractTalksDialog::render(
    GameController& controller)
{
  if (open_requested)
  {
    open_requested = false;
    ImGui::OpenPopup(POPUP_ID);
  }
  visible = ImGui::IsPopupOpen(POPUP_ID);
  if (!visible) return Event::None;
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const bool wide = viewport->WorkSize.x >= scaled(TWO_COLUMN_MIN_VIEWPORT);
  const float width = std::min(scaled(wide ? WIDE_DIALOG_WIDTH : DIALOG_WIDTH),
                               viewport->WorkSize.x * VIEWPORT_WIDTH_SHARE);
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(width, 0.0f),
      ImVec2(width, viewport->WorkSize.y * VIEWPORT_HEIGHT_SHARE));
  if (const std::string label = title + POPUP_ID; !ImGui::BeginPopupModal(
          label.c_str(), nullptr,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
  {
    visible = false;
    return Event::None;
  }
  Event event = Event::None;
  bool close = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(Theme::palette().muted, "%s", kind_text.c_str());
    ImGui::PopTextWrapPos();
  }
  if (wide && ImGui::BeginTable("##contract_columns", 2,
                                ImGuiTableFlags_SizingStretchSame))
  {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    renderTerms();
    ImGui::TableNextColumn();
    renderSummary();
    ImGui::EndTable();
  }
  else
  {
    renderTerms();
    ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));
    renderSummary();
  }

  ImGui::Separator();
  const char* cancel = LOC("TRANSFER_CANCEL");
  const char* package = LOC("AGENT_ACTION_PACKAGE");
  const char* proposeLabel = LOC("TRANSFER_PROPOSE");
  TransferTermsEditor::alignActions({cancel, package, proposeLabel});
  if (UI::secondaryButton(cancel)) close = true;
  ImGui::SameLine();
  if (UI::secondaryButton(package))
  {
    // The agent's package as he asks it now.
    const std::optional<SquadRole> promise = offer.promised_role;
    offer = PlayerAgent::askedOffer(agent, offer.years);
    offer.promised_role = promise;
    agent_fee = agent.agent_fee;
    rise_index =
        TransferTermsEditor::optionIndex(RISE_OPTIONS, offer.yearly_rise);
    offer.yearly_rise = RISE_OPTIONS[static_cast<std::size_t>(rise_index)];
    refreshSummary();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(rounds_left == 0 || offer.years == 0 || signed_up);
  if (UI::primaryButton(proposeLabel))
  {
    propose(controller);
    event = signed_up ? Event::Signed : Event::Proposed;
    if (signed_up) close = true;
  }
  ImGui::EndDisabled();
  if (close)
  {
    ImGui::CloseCurrentPopup();
    visible = false;
  }
  ImGui::EndPopup();
  return event;
}

void ContractTalksDialog::renderTerms()
{
  bool changed = false;
  formLabel(LOC("TRANSFER_FIELD_WAGE"));
  std::int64_t wage = offer.weekly_wage;
  if (const std::array<UI::MoneyChip, 3> wage_chips =
          {{{LOC("AGENT_CHIP_PACKAGE"), agent.asking_wage},
            {LOC("TRANSFER_CHIP_AGENT_ASK"), agent.flat_wage},
            {LOC("TRANSFER_CHIP_MINUS_5"), scaledWage(agent.flat_wage, 0.95)}}};
      UI::moneyInput("##contract_wage", wage,
                     {.maximum = std::numeric_limits<std::uint32_t>::max(),
                      .chips = wage_chips}))
  {
    offer.weekly_wage = toMoney(wage);
    changed = true;
  }

  formLabel(LOC("CONTRACT_FIELD_UNTIL"));
  if (int year_index = std::clamp(offer.years - first_year, 0,
                                  static_cast<int>(year_labels.size()) - 1);
      UI::segmented("##contract_until", year_index,
                    std::span<const char* const>(year_labels)))
  {
    offer.years = static_cast<std::uint8_t>(first_year + year_index);
    changed = true;
  }

  formLabel(LOC("TRANSFER_FIELD_SIGNING_BONUS"));
  std::int64_t bonus = offer.signing_bonus;
  std::vector<UI::MoneyChip> bonus_chips = {{LOC("TRANSFER_CHIP_NONE"), 0}};
  if (agent.signing_bonus > 0)
    bonus_chips.push_back(
        {LOC("TRANSFER_CHIP_ASKED_BONUS"), agent.signing_bonus});
  if (UI::moneyInput("##contract_bonus", bonus,
                     {.maximum = std::numeric_limits<std::uint32_t>::max(),
                      .chips = bonus_chips}))
  {
    offer.signing_bonus = toMoney(bonus);
    changed = true;
  }

  formLabel(LOC("CONTRACT_FIELD_RISE"));
  if (UI::segmented("##contract_rise", rise_index, RISE_LABELS))
  {
    offer.yearly_rise = RISE_OPTIONS[static_cast<std::size_t>(rise_index)];
    changed = true;
  }

  formLabel(LOC("CONTRACT_FIELD_APPEARANCE"));
  std::int64_t appearance = offer.appearance_bonus;
  if (const std::array<UI::MoneyChip, 2> appearance_chips =
          {{{LOC("TRANSFER_CHIP_NONE"), 0},
            {LOC("AGENT_CHIP_ASKED"), agent.appearance_bonus}}};
      UI::moneyInput("##contract_appearance", appearance,
                     {.maximum = std::numeric_limits<std::uint32_t>::max(),
                      .chips = appearance_chips}))
  {
    offer.appearance_bonus = toMoney(appearance);
    changed = true;
  }

  formLabel(LOC("TRANSFER_FIELD_RELEASE_CLAUSE"));
  std::int64_t clause = offer.release_clause;
  std::vector<UI::MoneyChip> clause_chips = {{LOC("TRANSFER_CHIP_NONE"), 0}};
  if (agent.wants_release_clause)
    clause_chips.push_back(
        {LOC("TRANSFER_CHIP_MAX_CLAUSE"), agent.max_release_clause});
  if (UI::moneyInput("##contract_clause", clause,
                     {.maximum = std::numeric_limits<std::uint32_t>::max(),
                      .chips = clause_chips}))
  {
    offer.release_clause = toMoney(clause);
    changed = true;
  }

  formLabel(LOC("AGENT_FIELD_FEE"));
  if (const std::array<UI::MoneyChip, 2> fee_chips =
          {{{LOC("AGENT_CHIP_ASKED"), agent.agent_fee},
            {LOC("AGENT_CHIP_STANDARD"), agent.standard_agent_fee}}};
      UI::moneyInput("##contract_agent_fee", agent_fee,
                     {.maximum = std::numeric_limits<std::uint32_t>::max(),
                      .chips = fee_chips}))
    changed = true;
  offer.agent_fee = toMoney(agent_fee);

  formLabel(LOC("TRANSFER_FIELD_PROMISE"));
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (const char* preview =
          promise_index == 0
              ? LOC("TRANSFER_PROMISE_NONE")
              : LOC(squadRoleKey(PROMISE_ROLES[static_cast<std::size_t>(
                    promise_index - 1)]));
      ImGui::BeginCombo("##contract_promise", preview))
  {
    if (ImGui::Selectable(LOC("TRANSFER_PROMISE_NONE"), promise_index == 0))
      promise_index = 0;
    for (std::size_t index = 0; index < PROMISE_ROLES.size(); ++index)
      if (ImGui::Selectable(LOC(squadRoleKey(PROMISE_ROLES[index])),
                            promise_index == static_cast<int>(index + 1)))
        promise_index = static_cast<int>(index + 1);
    ImGui::EndCombo();
  }
  offer.promised_role =
      promise_index == 0
          ? std::nullopt
          : std::optional<SquadRole>(
                PROMISE_ROLES[static_cast<std::size_t>(promise_index - 1)]);
  if (changed) refreshSummary();
}

void ContractTalksDialog::renderSummary() const
{
  const Theme::Palette& palette = Theme::palette();
  if (beginPanel("##agent_demands", LOC("TRANSFER_AGENT_DEMANDS")))
  {
    for (const Line& line : demand_lines)
      UI::summaryRow(line.label.c_str(), line.value.c_str());
    endPanel();
  }
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));
  if (beginPanel("##agent_says", LOC("AGENT_SAYS_TITLE")))
  {
    ImGui::PushTextWrapPos(0.0f);
    for (std::size_t index = 0; index < agent_lines.size(); ++index)
      ImGui::TextColored(
          index + 1 == agent_lines.size() ? palette.text : palette.faint, "%s",
          agent_lines[index].c_str());
    ImGui::PopTextWrapPos();
    endPanel();
  }
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));
  if (beginPanel("##contract_summary", LOC("TRANSFER_SUMMARY_TITLE")))
  {
    UI::summaryRow(LOC("TRANSFER_SUMMARY_YEARLY"), yearly_text.c_str());
    UI::summaryRow(LOC("TRANSFER_SUMMARY_CONTRACT_TOTAL"), total_text.c_str(),
                   nullptr, true);
    ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::XS)));
    UI::budgetImpact(LOC("TRANSFER_WAGE_IMPACT"), wage_room,
                     wage_room - static_cast<std::int64_t>(offer.weekly_wage),
                     LOC("TRANSFER_WAGE_OVER_ROOM"));
    ImGui::TextColored(palette.faint, "%s", rounds_text.c_str());
    endPanel();
  }
  if (block != Block::None)
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.negative, "%s",
                       LOC(GameController::playerActionBlockKey(block)));
    ImGui::PopTextWrapPos();
  }
  if (!response) return;
  const bool agreed = response->accepted;
  ImGui::Dummy(ImVec2(0.0f, scaled(Theme::Space::S)));
  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    ImGui::TextColored(
        agreed ? palette.positive : palette.negative, "%s",
        LOC(agreed ? "TRANSFER_PLAYER_AGREES" : "TRANSFER_PLAYER_REFUSES"));
  }
  ImGui::PushTextWrapPos(0.0f);
  for (const TransferNegotiation::Reason reason : response->reasons)
  {
    ImGui::Bullet();
    ImGui::TextColored(palette.muted, "%s",
                       LOC(TransferNegotiation::reasonKey(reason)));
  }
  if (over_budget)
    ImGui::TextColored(palette.negative, "%s",
                       LOC("TRANSFER_NOT_ENOUGH_BUDGET"));
  ImGui::PopTextWrapPos();
}

void ContractTalksDialog::propose(GameController& controller)
{
  const GameController::ContractTalkResult result =
      controller.proposeContract(player_id, offer);
  block = result.block;
  if (block != Block::None) return;
  response = result.response;
  over_budget = result.over_budget;
  rounds_left = result.rounds_left;
  if (result.agent_line != nullptr && *result.agent_line != '\0')
    agent_lines.push_back(fmt::sprintf(LOC(result.agent_line), player));
  if (result.completed)
  {
    signed_up = true;
    signed_message = fmt::sprintf(
        LOC(kind == ContractKind::PreContract ? "TRANSFER_PRE_CONTRACT_TOAST"
            : kind == ContractKind::Renewal   ? "CONTRACT_RENEWED_TOAST"
                                              : "TRANSFER_COMPLETED_TOAST"),
        player);
    return;
  }
  // The agent comes down a little after every refusal.
  refreshDemands(controller);
  refreshSummary();
}

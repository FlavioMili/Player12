// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/transfer_market_scene.h"

#include <fmt/format.h>
#include <fmt/printf.h>
#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include "controller/game_controller.h"
#include "database/gamedata.h"
#include "global/global.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/scenes/transfer_terms_editor.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "gui/widgets/widgets.h"
#include "model/competition.h"
#include "model/game.h"
#include "model/role_utils.h"
#include "model/scouting.h"
#include "model/transfer_market.h"
#include "model/transfer_tuning.h"

namespace
{
using TransferNegotiation::ClubResponse;
using TransferNegotiation::ContractKind;
using Tuning = TransferMarketSceneTuning;
using namespace TransferTermsEditor;

enum class TargetColumn : ImGuiID
{
  SHORTLIST = 1,
  NAME,
  CLUB,
  ROLE,
  AGE,
  OVERALL,
  POTENTIAL,
  FIT,
  VALUE,
  WAGE,
  CONTRACT,
  ACTION
};

constexpr ImGuiID columnId(TargetColumn column)
{
  return std::to_underlying(column);
}

/**
 * Target table columns in display order (labels are lang keys). Widths are
 * minimums in unscaled pixels (headers widen them); higher priorities hide
 * first on narrow windows and their values move into the row's Actions menu.
 */
const std::array<UI::Column, 12>& targetColumns()
{
  using enum TargetColumn;
  constexpr ImGuiTableColumnFlags DESCENDING =
      ImGuiTableColumnFlags_PreferSortDescending;
  static const std::array<UI::Column, 12> columns = {{
      {"TRANSFER_COL_SHORTLIST", 28.0f, 0, ImGuiTableColumnFlags_NoHide,
       columnId(SHORTLIST)},
      {"TRANSFER_COL_NAME", 0.0f, 0, ImGuiTableColumnFlags_NoHide,
       columnId(NAME)},
      {"TRANSFER_COL_TEAM", 130.0f, 4, ImGuiTableColumnFlags_None,
       columnId(CLUB)},
      {"TRANSFER_COL_ROLE", 44.0f, 3, ImGuiTableColumnFlags_None,
       columnId(ROLE)},
      {"TRANSFER_COL_AGE", 36.0f, 5, ImGuiTableColumnFlags_None, columnId(AGE)},
      {"TRANSFER_COL_EST_OVR", 64.0f, 0, DESCENDING, columnId(OVERALL)},
      {"TRANSFER_COL_POTENTIAL", 64.0f, 3, DESCENDING, columnId(POTENTIAL)},
      {"TRANSFER_COL_FIT", 100.0f, 0,
       DESCENDING | ImGuiTableColumnFlags_DefaultSort, columnId(FIT)},
      {"TRANSFER_COL_EST_VALUE", 80.0f, 1, DESCENDING, columnId(VALUE)},
      {"TRANSFER_COL_WAGE", 80.0f, 6, ImGuiTableColumnFlags_None,
       columnId(WAGE)},
      {"TRANSFER_COL_CONTRACT", 44.0f, 7, ImGuiTableColumnFlags_None,
       columnId(CONTRACT)},
      {"TRANSFER_COL_ACTION", 64.0f, 0,
       ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoHide,
       columnId(ACTION)},
  }};
  return columns;
}

/** Index of a column in targetColumns(). */
constexpr int targetIndex(TargetColumn column)
{
  return static_cast<int>(std::to_underlying(column)) -
         static_cast<int>(std::to_underlying(TargetColumn::SHORTLIST));
}

constexpr std::array<PlayerRole, 12> FILTER_ROLES = {
    PlayerRole::GK,  PlayerRole::CB, PlayerRole::LB,  PlayerRole::RB,
    PlayerRole::CDM, PlayerRole::CM, PlayerRole::CAM, PlayerRole::LM,
    PlayerRole::RM,  PlayerRole::LW, PlayerRole::RW,  PlayerRole::ST};

constexpr std::array<const char*, 5> AVAILABILITY_KEYS = {
    "TRANSFER_AVAIL_ANY", "TRANSFER_AVAIL_LISTED", "TRANSFER_AVAIL_LOAN",
    "TRANSFER_AVAIL_FREE", "TRANSFER_AVAIL_PRE_CONTRACT"};

constexpr std::array<const char*, 3> CONTRACT_KEYS = {
    "TRANSFER_CONTRACT_ANY", "TRANSFER_CONTRACT_EXPIRING",
    "TRANSFER_CONTRACT_TWO_YEARS"};

float scale() { return Theme::scale(); }

/** Estimated ability: a rating chip, or a range chip ("64-72") coloured by
 * its centre when the scouts know too little for a single number. */
void estimateChip(float overall, bool ranged, const std::string& text)
{
  if (!ranged)
  {
    UI::ratingChip(overall);
    return;
  }
  const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
  const float padX = 6.0f * scale();
  const ImVec2 size(textSize.x + 2.0f * padX, textSize.y + 2.0f * scale());
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const ImVec4 color = Theme::ratingColor(overall);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->AddRect(start, ImVec2(start.x + size.x, start.y + size.y),
                    Theme::toU32(color, 0.55f), 4.0f * scale());
  drawList->AddText(ImVec2(start.x + padX, start.y + scale()),
                    Theme::toU32(color), text.c_str());
  ImGui::Dummy(size);
}

/** Centres a deal dialog that fits its content up to the viewport height;
 * taller content (responses, reasons) scrolls instead of leaving the
 * screen. */
void placeDialog(float width)
{
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
  ImGui::SetNextWindowSizeConstraints(
      ImVec2(width, 0.0f),
      ImVec2(width, viewport->Size.y * Tuning::Layout::DIALOG_VIEWPORT_SHARE));
}

std::string fitText(const TransferNegotiation::SquadFit& fit)
{
  using enum TransferNegotiation::FitKind;
  switch (fit.kind)
  {
    case Starter:
      return LOC("TRANSFER_FIT_STARTER");
    case Upgrade:
      return fmt::sprintf(LOC("TRANSFER_FIT_UPGRADE"),
                          static_cast<int>(std::lround(fit.gain)));
    case Depth:
      return LOC("TRANSFER_FIT_DEPTH");
    case None:
      break;
  }
  return "-";
}

ImVec4 fitColor(TransferNegotiation::FitKind kind)
{
  using enum TransferNegotiation::FitKind;
  const Theme::Palette& palette = Theme::palette();
  switch (kind)
  {
    case Starter:
    case Upgrade:
      return palette.positive;
    case Depth:
      return palette.info;
    case None:
      break;
  }
  return palette.faint;
}

constexpr int64_t BID_ROUNDING = 10'000;

constexpr std::array<uint8_t, 5> WAGE_SHARE_OPTIONS = {0, 25, 50, 75, 100};
constexpr std::array<const char*, 5> WAGE_SHARE_LABELS = {"0%", "25%", "50%",
                                                          "75%", "100%"};

uint32_t clampFee(int64_t fee)
{
  return static_cast<uint32_t>(std::clamp<int64_t>(
      fee, BID_ROUNDING, std::numeric_limits<uint32_t>::max()));
}

/** @p amount times @p factor, rounded to a sensible fee. */
int64_t scaledAmount(int64_t amount, double factor)
{
  return std::llround(static_cast<double>(amount) * factor /
                      static_cast<double>(BID_ROUNDING)) *
         BID_ROUNDING;
}

/** An offer structure that fits the budget, if any. */
struct Suggestion
{
  std::optional<TransferNegotiation::OfferTerms> terms;
};

int64_t signingCash(const TransferNegotiation::OfferTerms& terms)
{
  TransferMarket::Deal deal;
  deal.terms = terms;
  return static_cast<int64_t>(TransferNegotiation::upfrontAmount(terms)) +
         static_cast<int64_t>(TransferMarket::agentFee(deal));
}

/** Spread the payments if that is enough, else lower the fee. */
Suggestion budgetFix(const TransferNegotiation::OfferTerms& terms,
                     int64_t budget)
{
  Suggestion fix;
  if (budget <= 0) return fix;
  for (const uint8_t percent : UPFRONT_OPTIONS)
  {
    if (percent >= terms.upfront_percent) continue;
    TransferNegotiation::OfferTerms candidate = terms;
    candidate.upfront_percent = percent;
    candidate.instalment_years =
        std::max(terms.instalment_years, DEFAULT_INSTALMENT_YEARS);
    if (signingCash(candidate) <= budget)
    {
      fix.terms = candidate;
      return fix;
    }
  }
  TransferNegotiation::OfferTerms candidate = terms;
  const double share = terms.upfront_percent / 100.0 +
                       TransferTuning::Offer::AGENT_FEE_PERCENT / 100.0;
  candidate.fee = static_cast<uint32_t>(std::clamp<int64_t>(
      static_cast<int64_t>(static_cast<double>(budget) / share) / BID_ROUNDING *
          BID_ROUNDING,
      0, std::numeric_limits<uint32_t>::max()));
  while (candidate.fee >= BID_ROUNDING && signingCash(candidate) > budget)
    candidate.fee -= static_cast<uint32_t>(BID_ROUNDING);
  if (candidate.fee >= BID_ROUNDING) fix.terms = candidate;
  return fix;
}

const char* decisionKey(ClubResponse::Decision decision)
{
  using enum ClubResponse::Decision;
  switch (decision)
  {
    case Accept:
      return "TRANSFER_RESPONSE_ACCEPTED";
    case Counter:
      return "TRANSFER_RESPONSE_COUNTER";
    case Reject:
      return "TRANSFER_RESPONSE_REJECTED";
  }
  return "";
}

ImVec4 decisionColor(ClubResponse::Decision decision)
{
  using enum ClubResponse::Decision;
  const Theme::Palette& palette = Theme::palette();
  switch (decision)
  {
    case Accept:
      return palette.positive;
    case Counter:
      return palette.warning;
    case Reject:
      return palette.negative;
  }
  return palette.text;
}

std::string teamName(const GameController& controller, TeamID team_id)
{
  if (team_id == FREE_AGENTS_TEAM_ID) return LOC("TRANSFER_FREE_AGENT_LABEL");
  const auto team = controller.getTeamById(team_id);
  return team ? team->get().getName() : std::string();
}

std::string playerName(const GameData& data, PlayerID player_id)
{
  const auto player = data.getPlayer(player_id);
  return player ? player->get().getName()
                : std::string(LOC("TRANSFER_RETIRED"));
}

}  // namespace

TransferMarketScene::TransferMarketScene(GUIView* parent)
    : ManagementScene(parent)
{
}

TransferMarketScene::TransferMarketScene(GUIView* parent, PlayerID player)
    : ManagementScene(parent), pending_deal_player(player)
{
}

void TransferMarketScene::openPendingDeal()
{
  if (pending_deal_player == 0) return;
  const PlayerID player_id = std::exchange(pending_deal_player, 0);
  const auto scouted = guiView->getController().getScoutedRow(player_id);
  if (!scouted) return;
  const TargetRow row = makeTargetRow(*scouted);
  if (row.free_agent)
    openContractDialog(row.id);
  else
    openOfferDialog(row);
}

void TransferMarketScene::update(float /*deltaTime*/)
{
  // This scene updates its view models on entry and after transfer actions.
}

SceneID TransferMarketScene::getID() const { return SceneID::TRANSFER_MARKET; }

// ---------------------------------------------------------------------------
// View models
// ---------------------------------------------------------------------------

void TransferMarketScene::refreshData()
{
  const auto& controller = guiView->getController();
  const Game* game = controller.getGame();
  if (!game || !controller.getManagedTeam()) return;
  const TeamID managed = game->getManagedTeamId();

  window = controller.getTransferWindow();
  embargo = game->getWorld().isTransferEmbargoed(managed);
  available_budget = embargo ? 0 : controller.transferBudgetForTeam(managed);
  wage_room = controller.getManagedTeam()->get().getFinances().getWageBudget() -
              controller.getWeeklyWageBill(managed);

  league_ids.clear();
  league_names.clear();
  for (const auto& league : controller.getLeagues())
  {
    league_ids.push_back(league.get().getId());
    league_names.push_back(Competitions::leagueName(league.get()));
  }

  refreshSquad();
  refreshNeeds();
  if (search_dirty)
  {
    refreshTargets();
    refreshRecommended();
  }
  else
  {
    refreshTargetFlags();
  }
  refreshShortlist();
  refreshOffers();
  refreshLoans();
  refreshHistory();
}

TransferMarketScene::TargetRow TransferMarketScene::makeTargetRow(
    const ScoutedPlayerRow& scouted) const
{
  const auto& controller = guiView->getController();
  const auto data = controller.getGameData();
  const TransferMarket& market = controller.getGame()->getTransfers();
  TargetRow row;
  row.id = scouted.player_id;
  row.team_id = scouted.team_id;
  row.name = playerName(*data, row.id);
  row.name_lower = PlayerView::toLower(row.name);
  row.club = teamName(controller, row.team_id);
  row.role_id = scouted.role;
  row.role = RoleUtils::shortName(scouted.role);
  row.age = scouted.age;
  row.overall = scouted.overall;
  row.overall_low = scouted.overall_low;
  row.overall_high = scouted.overall_high;
  row.knowledge = scouted.knowledge;
  row.ranged = scouted.knowledge < ScoutingTuning::RANGE_DISPLAY_KNOWLEDGE &&
               std::lround(row.overall_high) > std::lround(row.overall_low);
  row.overall_text = row.ranged ? fmt::format("{:.0f}-{:.0f}", row.overall_low,
                                              row.overall_high)
                                : fmt::format("{:.0f}", row.overall);
  row.potential_text = fmt::format("{:.0f}-{:.0f}", scouted.potential_low,
                                   scouted.potential_high);
  row.potential = (scouted.potential_low + scouted.potential_high) * 0.5f;
  row.fit = TransferNegotiation::squadFit(needs, row.role_id, row.overall);
  row.fit_text = fitText(row.fit);
  row.value = scouted.estimated_value;
  row.value_text = Format::money(row.value);
  row.wage = scouted.wage;
  row.wage_text = Format::money(row.wage);
  row.contract_years = scouted.contract_years;
  row.free_agent = row.team_id == FREE_AGENTS_TEAM_ID;
  const int64_t value_cap = affordableValueCap();
  row.affordable =
      (row.free_agent || (value_cap > 0 && row.value <= value_cap)) &&
      static_cast<double>(row.wage) *
              static_cast<double>(ScoutingTuning::EXPECTED_WAGE_RAISE) <=
          static_cast<double>(wage_room);
  if (const auto listing = controller.getAllListings().find(row.id);
      listing != controller.getAllListings().end() &&
      listing->second.seller_team_id == row.team_id)
    row.asking_price = listing->second.asking_price;
  row.release_clause = market.releaseClause(row.id);
  if (row.release_clause > 0)
    row.clause_text = fmt::sprintf(LOC("TRANSFER_CLAUSE_LINE"),
                                   Format::money(row.release_clause));
  row.loan_listed = market.isLoanListed(row.id);
  row.pre_contract = !row.free_agent && market.canBeTraded(row.id) &&
                     TransferNegotiation::canSignPreContract(
                         static_cast<uint8_t>(row.contract_years),
                         controller.getCurrentDate());
  row.shortlisted = scouted.shortlisted;
  return row;
}

void TransferMarketScene::refreshTargets()
{
  if (!search_dirty) return;
  search_dirty = false;
  applied_filters = filters;
  ScoutSearchFilter search;
  if (filters.role_index > 0)
    search.role = FILTER_ROLES[static_cast<size_t>(filters.role_index - 1)];
  search.min_age = static_cast<uint8_t>(filters.min_age);
  search.max_age = static_cast<uint8_t>(filters.max_age);
  search.min_overall = static_cast<float>(filters.min_overall);
  if (filters.max_value_index > 0)
    search.max_value = Tuning::Filters::MAX_VALUE_STEPS[static_cast<size_t>(
        filters.max_value_index - 1)];
  if (filters.league_index > 0 &&
      static_cast<size_t>(filters.league_index) <= league_ids.size())
    search.league_id =
        league_ids[static_cast<size_t>(filters.league_index - 1)];
  search.free_agents_only = filters.availability == Availability::FREE_AGENTS;
  search.limit = Tuning::Filters::RESULT_LIMIT;
  targets = searchRows(search, filters.affordable_only);
  applyTargetFilter();
}

int64_t TransferMarketScene::affordableValueCap() const
{
  return static_cast<int64_t>(static_cast<double>(available_budget) *
                              Tuning::Filters::AFFORDABLE_VALUE_MULTIPLE);
}

std::vector<TransferMarketScene::TargetRow> TransferMarketScene::searchRows(
    ScoutSearchFilter search, bool affordable) const
{
  const auto& controller = guiView->getController();
  std::vector<TargetRow> rows;
  if (affordable)
  {
    // Free agents cost no fee: they come from their own search below.
    if (const int64_t cap = affordableValueCap();
        cap > 0 && !search.free_agents_only)
    {
      ScoutSearchFilter capped = search;
      capped.max_value =
          search.max_value > 0 ? std::min(search.max_value, cap) : cap;
      for (const ScoutedPlayerRow& scouted :
           controller.searchScoutedPlayers(capped))
      {
        if (scouted.team_id != FREE_AGENTS_TEAM_ID)
          rows.push_back(makeTargetRow(scouted));
      }
    }
    // A league filter excludes the free-agent pool.
    if (search.league_id == 0)
    {
      search.free_agents_only = true;
      search.max_value = 0;
      for (const ScoutedPlayerRow& scouted :
           controller.searchScoutedPlayers(search))
        rows.push_back(makeTargetRow(scouted));
    }
    return rows;
  }
  for (const ScoutedPlayerRow& scouted :
       controller.searchScoutedPlayers(search))
    rows.push_back(makeTargetRow(scouted));
  return rows;
}

void TransferMarketScene::refreshNeeds()
{
  // The managed squad is known exactly; players leaving on a pre-contract
  // do not count.
  std::vector<std::pair<PlayerRole, float>> shape;
  shape.reserve(squad.size());
  for (const SquadRow& row : squad)
  {
    if (!row.committed)
      shape.emplace_back(row.player.role_id, row.player.overall);
  }
  needs = TransferNegotiation::squadNeeds(shape);
  needs_text.clear();
  for (const TransferNegotiation::PositionNeed& need : needs.positions)
  {
    if (need.count >= need.wanted) continue;
    if (!needs_text.empty()) needs_text += ", ";
    needs_text +=
        fmt::sprintf(LOC(need.count < need.starters ? "TRANSFER_NEED_STARTER"
                                                    : "TRANSFER_NEED_DEPTH"),
                     RoleUtils::shortName(need.group));
  }
}

void TransferMarketScene::refreshRecommended()
{
  // Need-based and affordable, whatever the Search tab filters say. With
  // the default filters the Search rows are exactly that pool: reuse them
  // instead of estimating the world again.
  const Filters defaults;
  const bool default_search =
      filters.affordable_only && filters.role_index == 0 &&
      filters.league_index == 0 && filters.min_age == defaults.min_age &&
      filters.max_age == defaults.max_age && filters.min_overall == 0 &&
      filters.max_value_index == 0 && filters.availability == Availability::ANY;
  std::vector<TargetRow> pool;
  if (default_search)
  {
    pool = targets;
  }
  else
  {
    ScoutSearchFilter search;
    search.limit = Tuning::Filters::RESULT_LIMIT;
    pool = searchRows(search, true);
  }
  std::erase_if(pool,
                [](const TargetRow& row)
                {
                  return !row.affordable || row.fit.score <= 0.0f ||
                         row.fit.kind == TransferNegotiation::FitKind::None;
                });
  const auto better = [](const TargetRow& a, const TargetRow& b)
  {
    if (a.fit.score != b.fit.score) return a.fit.score > b.fit.score;
    return a.value != b.value ? a.value < b.value : a.id < b.id;
  };
  const size_t keep = std::min(pool.size(), Tuning::Filters::RECOMMENDED_LIMIT);
  std::ranges::partial_sort(
      pool, pool.begin() + static_cast<std::ptrdiff_t>(keep), better);
  pool.resize(keep);
  recommended = std::move(pool);
  visible_recommended.resize(recommended.size());
  for (size_t index = 0; index < recommended.size(); ++index)
    visible_recommended[index] = index;
  sortTargets(recommended, visible_recommended, recommended_sort);
}

void TransferMarketScene::refreshTargetFlags()
{
  // Cheap after an action: re-derive the cached rows (club, listing, loan
  // list, shortlist) without estimating every player again.
  rederiveRows(targets);
  applyTargetFilter();
  rederiveRows(recommended);
  visible_recommended.resize(recommended.size());
  for (size_t index = 0; index < recommended.size(); ++index)
    visible_recommended[index] = index;
  sortTargets(recommended, visible_recommended, recommended_sort);
}

void TransferMarketScene::rederiveRows(std::vector<TargetRow>& rows) const
{
  const auto& controller = guiView->getController();
  const TeamID managed = controller.getGame()->getManagedTeamId();
  std::vector<TargetRow> updated;
  updated.reserve(rows.size());
  for (const TargetRow& row : rows)
  {
    const auto scouted = controller.getScoutedRow(row.id);
    if (scouted && scouted->team_id != managed)
      updated.push_back(makeTargetRow(*scouted));
  }
  rows = std::move(updated);
}

void TransferMarketScene::applyTargetFilter()
{
  using enum TransferMarketScene::Availability;
  const std::string query = PlayerView::toLower(filters.name.data());
  applied_filters.name = filters.name;
  visible_targets.clear();
  for (size_t index = 0; index < targets.size(); ++index)
  {
    const TargetRow& row = targets[index];
    if (!query.empty() && !row.name_lower.contains(query)) continue;
    if (filters.affordable_only && !row.affordable) continue;
    switch (filters.availability)
    {
      case LISTED:
        if (row.asking_price == 0) continue;
        break;
      case LOAN_LISTED:
        if (!row.loan_listed) continue;
        break;
      case PRE_CONTRACT:
        if (!row.pre_contract) continue;
        break;
      default:
        break;
    }
    if (filters.contract == ContractFilter::EXPIRING && row.contract_years > 1)
      continue;
    if (filters.contract == ContractFilter::TWO_YEARS && row.contract_years > 2)
      continue;
    visible_targets.push_back(index);
  }
  sortTargets(targets, visible_targets, search_sort);
}

void TransferMarketScene::sortTargets(const std::vector<TargetRow>& rows,
                                      std::vector<size_t>& visible,
                                      const SortState& sort) const
{
  const auto column = sort.column == 0 ? TargetColumn::FIT
                                       : static_cast<TargetColumn>(sort.column);
  using enum TargetColumn;
  std::ranges::sort(visible,
                    [&](size_t left, size_t right)
                    {
                      const TargetRow& a = rows[left];
                      const TargetRow& b = rows[right];
                      int cmp = 0;
                      switch (column)
                      {
                        case SHORTLIST:
                          cmp = UI::compare(a.shortlisted, b.shortlisted);
                          break;
                        case NAME:
                          cmp = a.name.compare(b.name);
                          break;
                        case CLUB:
                          cmp = a.club.compare(b.club);
                          break;
                        case ROLE:
                          cmp = UI::compare(a.role_id, b.role_id);
                          break;
                        case AGE:
                          cmp = UI::compare(a.age, b.age);
                          break;
                        case OVERALL:
                        case ACTION:
                          // Ranges sort by their midpoint.
                          cmp = UI::compare(a.overall_low + a.overall_high,
                                            b.overall_low + b.overall_high);
                          break;
                        case POTENTIAL:
                          cmp = UI::compare(a.potential, b.potential);
                          break;
                        case FIT:
                          // Equal fit: the cheaper player first.
                          cmp = UI::compare(a.fit.score, b.fit.score);
                          if (cmp == 0)
                            cmp = sort.ascending
                                      ? UI::compare(a.value, b.value)
                                      : UI::compare(b.value, a.value);
                          break;
                        case VALUE:
                          cmp = UI::compare(a.value, b.value);
                          break;
                        case WAGE:
                          cmp = UI::compare(a.wage, b.wage);
                          break;
                        case CONTRACT:
                          cmp = UI::compare(a.contract_years, b.contract_years);
                          break;
                      }
                      if (cmp == 0) cmp = UI::compare(a.id, b.id);
                      return sort.ascending ? cmp < 0 : cmp > 0;
                    });
}

void TransferMarketScene::refreshShortlist()
{
  const auto& controller = guiView->getController();
  shortlist.clear();
  for (const ShortlistEntry& entry : controller.getShortlist())
  {
    if (const auto scouted = controller.getScoutedRow(entry.player_id))
      shortlist.push_back(makeTargetRow(*scouted));
  }
  visible_shortlist.resize(shortlist.size());
  for (size_t index = 0; index < shortlist.size(); ++index)
    visible_shortlist[index] = index;
  sortTargets(shortlist, visible_shortlist, shortlist_sort);
}

void TransferMarketScene::refreshSquad()
{
  const auto& controller = guiView->getController();
  const TransferMarket& market = controller.getGame()->getTransfers();
  const TeamID managed = controller.getGame()->getManagedTeamId();
  squad.clear();
  for (const auto& reference : controller.getPlayersForTeam(managed))
  {
    SquadRow row;
    row.player = PlayerView::makeRow(controller, reference.get());
    row.loan_listed = market.isLoanListed(row.player.id);
    row.borrowed = market.findLoan(row.player.id) != nullptr;
    row.committed = market.findPreContract(row.player.id) != nullptr;
    if (const auto listing = controller.getAllListings().find(row.player.id);
        listing != controller.getAllListings().end())
    {
      row.asking_price = listing->second.asking_price;
      row.asking_text = Format::money(row.asking_price);
    }
    if (const uint32_t clause = market.releaseClause(row.player.id); clause > 0)
      row.clause_text =
          fmt::sprintf(LOC("TRANSFER_CLAUSE_LINE"), Format::money(clause));
    row.release_block = GameController::playerActionBlockKey(
        controller.getReleaseBlock(row.player.id));
    squad.push_back(std::move(row));
  }
  std::ranges::sort(squad, [](const SquadRow& a, const SquadRow& b)
                    { return a.player.overall > b.player.overall; });
}

void TransferMarketScene::refreshOffers()
{
  const auto& controller = guiView->getController();
  const auto data = controller.getGameData();
  const TransferMarket& market = controller.getGame()->getTransfers();
  const TeamID managed = controller.getGame()->getManagedTeamId();
  offers.clear();
  for (const IncomingOffer& incoming : controller.getIncomingOffers())
  {
    OfferRow row;
    row.offer_id = incoming.id;
    row.player_id = incoming.player_id;
    row.player = playerName(*data, incoming.player_id);
    row.club = teamName(controller, incoming.buyer);
    row.loan = incoming.loan;
    row.fee = incoming.terms.fee;
    row.amount_text =
        incoming.loan
            ? fmt::sprintf(LOC("LOAN_SHARE_SHORT"),
                           static_cast<int>(incoming.loan_terms.wage_share))
            : Format::money(incoming.terms.fee);
    row.terms_text =
        incoming.loan ? TransferTermsEditor::loanTermsLine(incoming.loan_terms)
                      : TransferTermsEditor::structureText(incoming.terms);
    const uint32_t value = controller.getPlayerMarketValue(incoming.player_id);
    row.value = value;
    row.value_ratio = !incoming.loan && value > 0
                          ? static_cast<float>(incoming.terms.fee) /
                                static_cast<float>(value) *
                                Tuning::PERCENT_SCALE
                          : 0.0f;
    row.ratio_text = fmt::sprintf(LOC("TRANSFER_VS_VALUE_SHORT"),
                                  static_cast<double>(row.value_ratio));
    row.awaiting = incoming.status == OfferStatus::AwaitingBuyer;
    row.expires_text = row.awaiting
                           ? fmt::sprintf(LOC("TRANSFER_OFFER_AWAITING"),
                                          Format::dayMonth(incoming.respond_on))
                           : Format::dayMonth(incoming.expires);
    offers.push_back(std::move(row));
  }

  talks.clear();
  for (const auto& [player_id, talk] : market.talks())
  {
    // Renewals of the club's own players are held from their profiles.
    if (talk.seller == managed) continue;
    TalkRow row;
    row.player_id = player_id;
    row.player = playerName(*data, player_id);
    row.club = teamName(controller, talk.seller);
    row.stage = LOC(talk.club_agreed ? "TRANSFER_STAGE_FEE_AGREED"
                                     : "TRANSFER_STAGE_TALKING");
    row.fee_text =
        talk.club_agreed ? Format::money(talk.agreed.fee) : std::string("-");
    row.date_text = Format::dayMonth(talk.expires);
    row.can_continue = controller.getContractTalkKind(player_id).has_value();
    talks.push_back(std::move(row));
  }
  for (const auto& [player_id, deal] : market.preContracts())
  {
    if (deal.to_team != managed && deal.from_team != managed) continue;
    TalkRow row;
    row.player_id = player_id;
    row.player = playerName(*data, player_id);
    row.club = teamName(
        controller, deal.to_team == managed ? deal.from_team : deal.to_team);
    row.stage = LOC(deal.to_team == managed ? "TRANSFER_STAGE_PRE_IN"
                                            : "TRANSFER_STAGE_PRE_OUT");
    row.fee_text = Format::money(deal.terms.weekly_wage);
    row.date_text = Format::date(deal.agreed);
    row.pre_contract = true;
    talks.push_back(std::move(row));
  }
  std::ranges::sort(talks, {}, &TalkRow::player);
}

void TransferMarketScene::refreshLoans()
{
  const auto& controller = guiView->getController();
  const auto data = controller.getGameData();
  const TransferMarket& market = controller.getGame()->getTransfers();
  const TeamID managed = controller.getGame()->getManagedTeamId();
  loans.clear();
  loans_in = 0;
  loans_out = 0;
  for (const auto& [player_id, loan] : market.loans())
  {
    if (loan.parent != managed && loan.borrower != managed) continue;
    LoanRow row;
    row.player_id = player_id;
    row.player = playerName(*data, player_id);
    row.incoming = loan.borrower == managed;
    row.club = teamName(controller, row.incoming ? loan.parent : loan.borrower);
    row.until_text = Format::date(loan.end);
    row.share_text = fmt::format("{}%", static_cast<int>(loan.wage_share));
    row.option_text =
        loan.option_fee == 0
            ? std::string("-")
            : fmt::sprintf(LOC(loan.obligation ? "TRANSFER_LOAN_OBLIGATION"
                                               : "TRANSFER_LOAN_OPTION"),
                           Format::money(loan.option_fee));
    // The recall button shows once the clause can be used (open window,
    // minimum spell served).
    row.can_recall = !row.incoming && controller.canRecallLoan(player_id);
    row.can_buy = row.incoming && loan.option_fee > 0;
    (row.incoming ? loans_in : loans_out) += 1;
    loans.push_back(std::move(row));
  }
  std::ranges::sort(loans, {}, &LoanRow::player);
}

void TransferMarketScene::refreshHistory()
{
  const auto& controller = guiView->getController();
  const auto data = controller.getGameData();
  const TransferMarket& market = controller.getGame()->getTransfers();
  const TeamID managed = controller.getGame()->getManagedTeamId();
  const auto& records = market.history();
  history.clear();
  const size_t count = std::min(records.size(), Tuning::Tables::HISTORY_LIMIT);
  history.reserve(count);
  for (size_t index = 0; index < count; ++index)
  {
    const TransferRecord& record = records[records.size() - 1 - index];
    HistoryRow row;
    row.date = Format::date(record.date);
    row.player_id = record.player_id;
    row.player = playerName(*data, record.player_id);
    row.from = teamName(controller, record.from_team);
    row.to = teamName(controller, record.to_team);
    row.kind_key = transferKindKey(record.kind);
    row.severance = record.kind == TransferKind::Release;
    if (record.fee == 0)
      row.fee_text = "-";
    else if (row.severance)
      row.fee_text = fmt::sprintf(LOC("TRANSFER_HISTORY_SEVERANCE"),
                                  Format::money(record.fee));
    else
      row.fee_text = Format::money(record.fee);
    row.managed = record.from_team == managed || record.to_team == managed;
    history.push_back(std::move(row));
  }
  visible_history.clear();
  for (size_t index = 0; index < history.size(); ++index)
    if (!history_managed_only || history[index].managed)
      visible_history.push_back(index);
}

// ---------------------------------------------------------------------------
// Page
// ---------------------------------------------------------------------------

void TransferMarketScene::renderContent()
{
  using enum ContractTalksDialog::Event;
  UI::pageHeader(LOC("TRANSFER_TITLE"), LOC("TRANSFER_SUBTITLE"));
  renderSummary();

  if (ImGui::BeginTabBar("TransferMarketTabs"))
  {
    if (ImGui::BeginTabItem(LOC("TRANSFER_TAB_RECOMMENDED")))
    {
      renderRecommendedTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(LOC("TRANSFER_TAB_SEARCH")))
    {
      renderSearchTab();
      ImGui::EndTabItem();
    }
    if (const std::string shortlistLabel =
            fmt::format("{} ({})###shortlist", LOC("TRANSFER_TAB_SHORTLIST"),
                        shortlist.size());
        ImGui::BeginTabItem(shortlistLabel.c_str()))
    {
      if (shortlist.empty())
        UI::emptyState(LOC("TRANSFER_SHORTLIST_EMPTY_TITLE"),
                       LOC("TRANSFER_SHORTLIST_EMPTY_BODY"));
      else
        renderTargetTable("ShortlistTable", shortlist, visible_shortlist,
                          shortlist_sort);
      ImGui::EndTabItem();
    }
    if (const std::string offersLabel =
            offers.empty()
                ? fmt::format("{}###offers", LOC("TRANSFER_TAB_OFFERS"))
                : fmt::format("{} ({})###offers", LOC("TRANSFER_TAB_OFFERS"),
                              offers.size());
        ImGui::BeginTabItem(offersLabel.c_str()))
    {
      renderOffersTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(LOC("TRANSFER_TAB_LOANS")))
    {
      renderLoansTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(LOC("TRANSFER_TAB_SQUAD")))
    {
      renderSquadTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(LOC("TRANSFER_TAB_HISTORY")))
    {
      renderHistoryTab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }

  renderOfferDialog();
  switch (contract_talks.render(guiView->getController()))
  {
    case Signed:
      showToast(contract_talks.signedMessage());
      refreshData();
      break;
    case Proposed:
      refreshOffers();
      break;
    case None:
      break;
  }
  renderLoanDialog();
  if (negotiation_dialog.render(guiView->getController())) refreshData();
  renderListingDialog();
  renderReleaseDialog();
}

void TransferMarketScene::renderSummary()
{
  const Theme::Palette& palette = Theme::palette();
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const float available = ImGui::GetContentRegionAvail().x;
  const int perRow =
      std::clamp(static_cast<int>(
                     (available + gap) /
                     (Tuning::Layout::SUMMARY_TILE_MIN_WIDTH * scale() + gap)),
                 1, Tuning::Layout::SUMMARY_TILES);
  const float width = (available - gap * static_cast<float>(perRow - 1)) /
                      static_cast<float>(perRow);
  int tile = 0;
  const auto next = [&]()
  {
    if (++tile % perRow != 0) ImGui::SameLine();
  };

  const std::string budget = Format::money(available_budget);
  ImVec4 budgetColor = palette.warning;
  if (embargo)
    budgetColor = palette.negative;
  else if (available_budget > 0)
    budgetColor = palette.text;
  UI::statTile(
      "budget", LOC("TRANSFER_TILE_BUDGET"), budget.c_str(),
      LOC(embargo ? "TRANSFER_TILE_EMBARGO_NOTE" : "TRANSFER_TILE_BUDGET_NOTE"),
      budgetColor, width);
  next();
  const std::string wages = Format::money(wage_room);
  UI::statTile("wages", LOC("TRANSFER_TILE_WAGE_ROOM"), wages.c_str(),
               LOC("TRANSFER_TILE_WAGE_ROOM_NOTE"),
               wage_room >= 0 ? palette.text : palette.negative, width);
  next();
  std::string windowText;
  if (!window.open)
    windowText = LOC("TRANSFER_WINDOW_CLOSED_SHORT");
  else if (window.days_to_deadline == 0)
    windowText = LOC("TRANSFER_DEADLINE_DAY");
  else
    windowText = fmt::sprintf(
        Format::plural("TRANSFER_DAYS_LEFT", window.days_to_deadline),
        window.days_to_deadline);
  const char* windowSeasonKey =
      window.winter ? "TRANSFER_WINDOW_WINTER" : "TRANSFER_WINDOW_SUMMER";
  const char* windowNoteKey =
      window.open ? windowSeasonKey : "TRANSFER_WINDOW_NOTE";
  ImVec4 windowColor = palette.negative;
  if (window.open)
    windowColor =
        window.days_to_deadline <= TransferTuning::Market::LATE_WINDOW_DAYS
            ? palette.warning
            : palette.positive;
  UI::statTile("window", LOC("TRANSFER_WINDOW"), windowText.c_str(),
               LOC(windowNoteKey), windowColor, width);
  next();
  const std::string offerCount = std::to_string(offers.size());
  UI::statTile("offers", LOC("TRANSFER_TILE_OFFERS"), offerCount.c_str(),
               LOC("TRANSFER_BIDS_NOTE"),
               offers.empty() ? palette.text : palette.warning, width);
  next();
  const std::string loanCount = fmt::format("{} / {}", loans_in, loans_out);
  UI::statTile("loans", LOC("TRANSFER_TILE_LOANS"), loanCount.c_str(),
               LOC("TRANSFER_TILE_LOANS_NOTE"), palette.text, width);
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale()));
}

// ---------------------------------------------------------------------------
// Search and shortlist
// ---------------------------------------------------------------------------

void TransferMarketScene::renderSearchTab()
{
  renderFilters();
  refreshTargets();
  if (visible_targets.empty())
  {
    UI::emptyState(
        LOC("TRANSFER_EMPTY_TITLE"),
        LOC(filters.affordable_only ? "TRANSFER_EMPTY_AFFORDABLE_BODY"
                                    : "TRANSFER_EMPTY_BODY"));
    return;
  }
  renderTargetTable("TargetTable", targets, visible_targets, search_sort);
}

void TransferMarketScene::renderRecommendedTab()
{
  const Theme::Palette& palette = Theme::palette();
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(
      palette.muted, "%s",
      (needs_text.empty()
           ? std::string(LOC("TRANSFER_RECOMMENDED_NO_GAPS"))
           : fmt::sprintf(LOC("TRANSFER_RECOMMENDED_NEEDS"), needs_text))
          .c_str());
  ImGui::PopTextWrapPos();
  if (recommended.empty())
  {
    UI::emptyState(LOC("TRANSFER_RECOMMENDED_EMPTY_TITLE"),
                   LOC("TRANSFER_RECOMMENDED_EMPTY_BODY"));
    return;
  }
  renderTargetTable("RecommendedTable", recommended, visible_recommended,
                    recommended_sort);
}

void TransferMarketScene::renderFilters()
{
  using L = Tuning::Layout;
  bool changed = false;
  ImGui::SetNextItemWidth(L::SEARCH_WIDTH * scale());
  if (ImGui::InputTextWithHint("##market_search", LOC("ROSTER_SEARCH_HINT"),
                               filters.name.data(), filters.name.size()))
    applyTargetFilter();

  UI::sameLineIfFits(L::COMBO_WIDTH * scale());
  ImGui::SetNextItemWidth(L::COMBO_WIDTH * scale());
  if (const std::string rolePreview =
          filters.role_index == 0
              ? std::string(LOC("TRANSFER_FILTER_ANY_POSITION"))
              : RoleUtils::shortName(
                    FILTER_ROLES[static_cast<size_t>(filters.role_index - 1)]);
      ImGui::BeginCombo("##market_role", rolePreview.c_str()))
  {
    if (ImGui::Selectable(LOC("TRANSFER_FILTER_ANY_POSITION"),
                          filters.role_index == 0))
    {
      filters.role_index = 0;
      changed = true;
    }
    for (size_t index = 0; index < FILTER_ROLES.size(); ++index)
    {
      const char* const role = RoleUtils::longName(FILTER_ROLES[index]);
      if (ImGui::Selectable(role,
                            filters.role_index == static_cast<int>(index + 1)))
      {
        filters.role_index = static_cast<int>(index + 1);
        changed = true;
      }
    }
    ImGui::EndCombo();
  }

  UI::sameLineIfFits(L::COMBO_WIDTH * scale());
  ImGui::SetNextItemWidth(L::COMBO_WIDTH * scale());
  if (const char* leaguePreview =
          filters.league_index == 0
              ? LOC("TRANSFER_FILTER_ANY_LEAGUE")
              : league_names[static_cast<size_t>(filters.league_index - 1)]
                    .c_str();
      ImGui::BeginCombo("##market_league", leaguePreview))
  {
    if (ImGui::Selectable(LOC("TRANSFER_FILTER_ANY_LEAGUE"),
                          filters.league_index == 0))
    {
      filters.league_index = 0;
      changed = true;
    }
    for (size_t index = 0; index < league_names.size(); ++index)
    {
      ImGui::PushID(static_cast<int>(index));
      if (ImGui::Selectable(
              league_names[index].c_str(),
              filters.league_index == static_cast<int>(index + 1)))
      {
        filters.league_index = static_cast<int>(index + 1);
        changed = true;
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }

  UI::sameLineIfFits(L::COMBO_WIDTH * scale());
  ImGui::SetNextItemWidth(L::COMBO_WIDTH * scale());
  if (ImGui::BeginCombo(
          "##market_availability",
          LOC(AVAILABILITY_KEYS[static_cast<size_t>(filters.availability)])))
  {
    for (size_t index = 0; index < AVAILABILITY_KEYS.size(); ++index)
    {
      const auto option = static_cast<Availability>(index);
      if (ImGui::Selectable(LOC(AVAILABILITY_KEYS[index]),
                            filters.availability == option))
      {
        // Free agents are a search scope; the others filter the results.
        changed |= (filters.availability == Availability::FREE_AGENTS) !=
                   (option == Availability::FREE_AGENTS);
        filters.availability = option;
        applyTargetFilter();
      }
    }
    ImGui::EndCombo();
  }

  UI::sameLineIfFits(L::COMBO_WIDTH * scale());
  ImGui::SetNextItemWidth(L::COMBO_WIDTH * scale());
  if (ImGui::BeginCombo(
          "##market_contract",
          LOC(CONTRACT_KEYS[static_cast<size_t>(filters.contract)])))
  {
    for (size_t index = 0; index < CONTRACT_KEYS.size(); ++index)
    {
      const auto option = static_cast<ContractFilter>(index);
      if (ImGui::Selectable(LOC(CONTRACT_KEYS[index]),
                            filters.contract == option))
      {
        filters.contract = option;
        applyTargetFilter();
      }
    }
    ImGui::EndCombo();
  }

  // Sliders search when released, not on every drag step.
  UI::sameLineIfFits(L::SLIDER_WIDTH * scale());
  ImGui::SetNextItemWidth(L::SLIDER_WIDTH * scale());
  const std::string ageFormat =
      fmt::format("{} %d", LOC("TRANSFER_FILTER_MIN_AGE"));
  ImGui::SliderInt("##market_min_age", &filters.min_age,
                   Tuning::Filters::MINIMUM_AGE, filters.max_age,
                   ageFormat.c_str());
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  UI::sameLineIfFits(L::SLIDER_WIDTH * scale());
  ImGui::SetNextItemWidth(L::SLIDER_WIDTH * scale());
  const std::string maxAgeFormat =
      fmt::format("{} %d", LOC("TRANSFER_FILTER_AGE"));
  ImGui::SliderInt("##market_max_age", &filters.max_age, filters.min_age,
                   Tuning::Filters::MAXIMUM_AGE, maxAgeFormat.c_str());
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  UI::sameLineIfFits(L::SLIDER_WIDTH * scale());
  ImGui::SetNextItemWidth(L::SLIDER_WIDTH * scale());
  const std::string overallFormat =
      fmt::format("{} %d", LOC("TRANSFER_FILTER_MIN_OVERALL"));
  ImGui::SliderInt("##market_min_overall", &filters.min_overall, 0,
                   Tuning::Filters::MAXIMUM_OVERALL, overallFormat.c_str());
  changed |= ImGui::IsItemDeactivatedAfterEdit();
  UI::sameLineIfFits(L::COMBO_WIDTH * scale());
  ImGui::SetNextItemWidth(L::COMBO_WIDTH * scale());
  if (const auto valueLabel =
          [](int index)
      {
        return index == 0
                   ? std::string(LOC("TRANSFER_FILTER_MAX_VALUE"))
                   : fmt::sprintf(
                         LOC("TRANSFER_FILTER_MAX_VALUE_AT"),
                         Format::money(Tuning::Filters::MAX_VALUE_STEPS
                                           [static_cast<size_t>(index - 1)]));
      };
      ImGui::BeginCombo("##market_max_value",
                        valueLabel(filters.max_value_index).c_str()))
  {
    for (int index = 0;
         index <= static_cast<int>(Tuning::Filters::MAX_VALUE_STEPS.size());
         ++index)
    {
      if (ImGui::Selectable(valueLabel(index).c_str(),
                            filters.max_value_index == index))
      {
        filters.max_value_index = index;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", LOC("TRANSFER_FILTER_MAX_VALUE_TIP"));

  const std::string affordableTip =
      fmt::sprintf(LOC("TRANSFER_FILTER_AFFORDABLE_TIP"),
                   Format::money(affordableValueCap()),
                   Format::money(std::max<int64_t>(wage_room, 0)));
  UI::sameLineIfFits(UI::buttonWidth(LOC("TRANSFER_FILTER_AFFORDABLE")) +
                     ImGui::GetFrameHeight());
  if (ImGui::Checkbox(LOC("TRANSFER_FILTER_AFFORDABLE"),
                      &filters.affordable_only))
    changed = true;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", affordableTip.c_str());

  UI::sameLineIfFits(UI::buttonWidth(LOC("TRANSFER_FILTER_RESET")));
  if (ImGui::Button(LOC("TRANSFER_FILTER_RESET")))
  {
    filters = Filters{};
    changed = true;
  }
  UI::sameLineIfFits(120.0f * scale());
  ImGui::AlignTextToFramePadding();
  ImGui::TextColored(
      Theme::palette().muted, "%s",
      fmt::sprintf(LOC("TRANSFER_RESULTS"), visible_targets.size()).c_str());
  if (changed) search_dirty = true;
}

void TransferMarketScene::renderTargetTable(const char* id,
                                            const std::vector<TargetRow>& rows,
                                            std::vector<size_t>& visible,
                                            SortState& sort)
{
  const Theme::Palette& palette = Theme::palette();
  auto& controller = guiView->getController();
  // Columns that do not fit hide (never sideways scrolling); their values
  // are listed in the row's Actions menu.
  std::array<UI::Column, 12> columns = targetColumns();
  const float scale = Theme::scale();
  const float sortArrow = ImGui::GetFontSize();
  for (UI::Column& column : columns)
  {
    column.label = LOC(column.label);
    if (column.width > 0.0f)
      column.width =
          std::max(column.width,
                   (ImGui::CalcTextSize(column.label).x + sortArrow) / scale);
  }
  const UI::ColumnMask mask =
      UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
  target_mask = mask;
  // The table fills the rest of the page and scrolls itself; on short
  // windows (big UI scales) it takes its natural height instead and the
  // page is the only scroll surface (the clipper keeps long lists cheap).
  const float room = ImGui::GetContentRegionAvail().y;
  const bool fill = room >= Tuning::Layout::TARGET_TABLE_MIN_HEIGHT * scale;
  if (const ImGuiTableFlags flags =
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
          ImGuiTableFlags_Sortable | ImGuiTableFlags_Hideable |
          (fill ? ImGuiTableFlags_ScrollY : ImGuiTableFlags_None);
      !UI::beginResponsiveTable(id, columns, mask, flags, UI::TableHeader::NONE,
                                ImVec2(0.0f, fill ? room : 0.0f)))
    return;
  // Sortable columns get clickable headers; the action column's is a label.
  ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
  for (int index = 0; index < ImGui::TableGetColumnCount(); ++index)
  {
    if (!ImGui::TableSetColumnIndex(index)) continue;
    const char* name = ImGui::TableGetColumnName(index);
    if ((ImGui::TableGetColumnFlags(index) & ImGuiTableColumnFlags_NoSort) != 0)
      ImGui::TextUnformatted(name);
    else
      ImGui::TableHeader(name);
  }

  // The table keeps its sort state across scene instances: follow it.
  if (ImGuiTableSortSpecs* specs = ImGui::TableGetSortSpecs();
      specs && specs->SpecsCount > 0)
  {
    const ImGuiTableColumnSortSpecs& spec = specs->Specs[0];
    if (const bool ascending =
            spec.SortDirection == ImGuiSortDirection_Ascending;
        specs->SpecsDirty || spec.ColumnUserID != sort.column ||
        ascending != sort.ascending)
    {
      sort.column = spec.ColumnUserID;
      sort.ascending = ascending;
      sortTargets(rows, visible, sort);
    }
    specs->SpecsDirty = false;
  }

  bool shortlist_changed = false;
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(visible.size()));
  while (clipper.Step())
  {
    for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line)
    {
      const TargetRow& row = rows[visible[static_cast<size_t>(line)]];
      ImGui::TableNextRow();
      ImGui::PushID(static_cast<int>(row.id));
      ImGui::TableNextColumn();
      bool shortlisted = row.shortlisted;
      // Compact box so rows keep the height of a text line.
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
      const bool toggled = ImGui::Checkbox("##shortlist", &shortlisted);
      ImGui::PopStyleVar();
      if (toggled)
      {
        if (shortlisted)
          controller.addToShortlist(row.id);
        else
          controller.removeFromShortlist(row.id);
        shortlist_changed = true;
      }
      ImGui::TableNextColumn();
      if (UI::link(row.name.c_str(), "name"))
        Navigation::openPlayer(guiView, row.id);
      if (ImGui::IsItemHovered() &&
          ImGui::CalcTextSize(row.name.c_str()).x > ImGui::GetColumnWidth())
        ImGui::SetTooltip("%s", row.name.c_str());
      if (row.asking_price > 0)
      {
        ImGui::SameLine();
        UI::badge(LOC("TRANSFER_BADGE_LISTED"), palette.info);
      }
      if (row.loan_listed)
      {
        ImGui::SameLine();
        UI::badge(LOC("TRANSFER_BADGE_LOAN"), palette.accent);
      }
      if (row.pre_contract)
      {
        ImGui::SameLine();
        UI::badge(LOC("TRANSFER_BADGE_EXPIRING"), palette.warning);
      }
      if (UI::cell(mask, targetIndex(TargetColumn::CLUB)))
        UI::textFitted(row.club, ImGui::GetContentRegionAvail().x,
                       row.free_agent ? palette.faint : palette.muted);
      if (UI::cell(mask, targetIndex(TargetColumn::ROLE)))
        ImGui::TextUnformatted(row.role.c_str());
      if (UI::cell(mask, targetIndex(TargetColumn::AGE)))
        ImGui::Text("%d", row.age);
      if (UI::cell(mask, targetIndex(TargetColumn::OVERALL)))
      {
        estimateChip(row.overall, row.ranged, row.overall_text);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip(
              "%s", fmt::sprintf(LOC("TRANSFER_KNOWLEDGE_TIP"), row.overall_low,
                                 row.overall_high, row.knowledge)
                        .c_str());
      }
      if (UI::cell(mask, targetIndex(TargetColumn::POTENTIAL)))
        ImGui::TextColored(palette.muted, "%s", row.potential_text.c_str());
      if (UI::cell(mask, targetIndex(TargetColumn::FIT)))
        UI::textFitted(row.fit_text, ImGui::GetContentRegionAvail().x,
                       fitColor(row.fit.kind));
      if (UI::cell(mask, targetIndex(TargetColumn::VALUE)))
        UI::textRight(row.value_text.c_str());
      if (UI::cell(mask, targetIndex(TargetColumn::WAGE)))
        UI::textRight(row.wage_text.c_str());
      if (UI::cell(mask, targetIndex(TargetColumn::CONTRACT)))
        ImGui::Text("%d", row.contract_years);
      ImGui::TableNextColumn();
      renderTargetActions(row);
      ImGui::PopID();
    }
  }
  ImGui::EndTable();
  if (shortlist_changed)
  {
    for (auto* list : {&targets, &recommended})
    {
      for (TargetRow& row : *list)
        row.shortlisted = controller.isShortlisted(row.id);
    }
    refreshShortlist();
  }
}

void TransferMarketScene::renderTargetActions(const TargetRow& row)
{
  const auto& controller = guiView->getController();
  if (ImGui::SmallButton(LOC("TRANSFER_ACTIONS"))) ImGui::OpenPopup("actions");
  if (!ImGui::BeginPopup("actions")) return;
  renderHiddenTargetValues(row);
  if (!row.clause_text.empty())
  {
    ImGui::TextColored(Theme::palette().info, "%s", row.clause_text.c_str());
    ImGui::Separator();
  }
  const auto talk = controller.getContractTalkKind(row.id);
  if (row.free_agent)
  {
    if (ImGui::Selectable(LOC("TRANSFER_ACTION_SIGN")))
      openContractDialog(row.id);
  }
  else
  {
    const ImGuiSelectableFlags dealFlags =
        window.open && !embargo ? 0 : ImGuiSelectableFlags_Disabled;
    if (ImGui::Selectable(LOC("TRANSFER_ACTION_OFFER"), false, dealFlags))
      openOfferDialog(row);
    if (ImGui::Selectable(LOC("TRANSFER_ACTION_LOAN"), false, dealFlags))
      openLoanDialog(row);
    if (row.release_clause > 0)
    {
      // Paying the clause in full: his club cannot say no.
      const std::string pay = fmt::sprintf(LOC("TRANSFER_ACTION_PAY_CLAUSE"),
                                           Format::money(row.release_clause));
      if (ImGui::Selectable(pay.c_str(), false, dealFlags))
      {
        auto& mutable_controller = guiView->getController();
        const ClubResponse response =
            mutable_controller.payReleaseClause(row.id);
        if (response.decision == ClubResponse::Decision::Accept)
        {
          refreshOffers();
          openContractDialog(row.id);
        }
        else
        {
          showToast(response.reasons.empty()
                        ? std::string(LOC("TRANSFER_DEAL_FAILED"))
                        : std::string(LOC(TransferNegotiation::reasonKey(
                              response.reasons.front()))),
                    true);
        }
      }
    }
    if (const bool agreed = talk == ContractKind::Transfer;
        agreed && ImGui::Selectable(LOC("TRANSFER_ACTION_TERMS")))
      openContractDialog(row.id);
    if (talk == ContractKind::PreContract &&
        ImGui::Selectable(LOC("TRANSFER_ACTION_PRE_CONTRACT")))
      openContractDialog(row.id);
    if (!window.open)
      ImGui::TextColored(Theme::palette().faint, "%s",
                         LOC("TRANSFER_WINDOW_CLOSED_HINT"));
    else if (embargo)
      ImGui::TextColored(Theme::palette().negative, "%s",
                         LOC("NEG_REASON_EMBARGO"));
  }
  ImGui::Separator();
  if (ImGui::Selectable(LOC("TRANSFER_ACTION_PROFILE")))
    Navigation::openPlayer(guiView, row.id);
  ImGui::EndPopup();
}

void TransferMarketScene::renderHiddenTargetValues(const TargetRow& row) const
{
  const Theme::Palette& palette = Theme::palette();
  const std::array<UI::Column, 12>& columns = targetColumns();
  const auto hidden = [this](TargetColumn column)
  { return (target_mask & (UI::ColumnMask{1} << targetIndex(column))) == 0; };
  float keyWidth = 0.0f;
  for (const TargetColumn column :
       {TargetColumn::CLUB, TargetColumn::ROLE, TargetColumn::AGE,
        TargetColumn::POTENTIAL, TargetColumn::VALUE, TargetColumn::WAGE,
        TargetColumn::CONTRACT})
    if (hidden(column))
      keyWidth = std::max(
          keyWidth,
          ImGui::CalcTextSize(
              LOC(columns[static_cast<size_t>(targetIndex(column))].label))
              .x);
  if (keyWidth <= 0.0f) return;  // Every column is on screen.
  keyWidth += ImGui::GetStyle().ItemSpacing.x * 2.0f;
  const auto line = [&](TargetColumn column, const std::string& value)
  {
    if (!hidden(column)) return;
    UI::keyValue(LOC(columns[static_cast<size_t>(targetIndex(column))].label),
                 value.c_str(), keyWidth);
  };
  ImGui::TextColored(palette.text, "%s", row.name.c_str());
  line(TargetColumn::CLUB, row.club);
  line(TargetColumn::ROLE, row.role);
  line(TargetColumn::AGE, std::to_string(row.age));
  line(TargetColumn::POTENTIAL, row.potential_text);
  line(TargetColumn::VALUE, row.value_text);
  line(TargetColumn::WAGE, row.wage_text);
  line(TargetColumn::CONTRACT, std::to_string(row.contract_years));
  ImGui::Separator();
}

// ---------------------------------------------------------------------------
// Offers, loans, squad and history
// ---------------------------------------------------------------------------

void TransferMarketScene::renderOffersTab()
{
  const Theme::Palette& palette = Theme::palette();
  auto& controller = guiView->getController();
  UI::sectionLabel(LOC("TRANSFER_INCOMING_OFFERS"));
  bool changed = false;
  if (offers.empty())
  {
    ImGui::TextColored(palette.muted, "%s", LOC("TRANSFER_NO_BIDS"));
  }
  else
  {
    // Player, bidder, fee and action always stay; the rest hides on narrow
    // windows and moves to a line under the player's name.
    const char* negotiate = LOC("TRANSFER_NEGOTIATE");
    const char* viewTalks = LOC("TRANSFER_VIEW_TALKS");
    constexpr UI::ButtonSize COMPACT = UI::ButtonSize::COMPACT;
    const float actionWidth = std::max(UI::buttonWidth(negotiate, COMPACT),
                                       UI::buttonWidth(viewTalks, COMPACT)) /
                              scale();
    std::array<UI::Column, 7> columns = {{
        {LOC("TRANSFER_COL_NAME"), 0.0f, 0},
        {LOC("TRANSFER_COL_BIDDER"), 120.0f, 0},
        {LOC("TRANSFER_COL_AMOUNT"), 80.0f, 0},
        {LOC("TRANSFER_COL_TERMS"), 150.0f, 2},
        {LOC("TRANSFER_COL_VS_VALUE"), 70.0f, 1},
        {LOC("TRANSFER_COL_EXPIRES"), 100.0f, 3},
        {LOC("TRANSFER_COL_ACTION"), actionWidth, 0},
    }};
    for (UI::Column& column : columns)
      if (column.width > 0.0f)
        column.width = std::max(column.width,
                                ImGui::CalcTextSize(column.label).x / scale());
    const UI::ColumnMask mask =
        UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
    const auto shown = [mask](int index)
    { return (mask & (UI::ColumnMask{1} << index)) != 0; };
    if (UI::beginResponsiveTable(
            "IncomingOffers", columns, mask,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH))
    {
      for (const OfferRow& row : offers)
      {
        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(row.offer_id));
        UI::cell(mask, 0);
        if (UI::link(row.player.c_str(), "name"))
          Navigation::openPlayer(guiView, row.player_id);
        if (!shown(3) || !shown(4) || !shown(5))
        {
          // Values of the hidden columns.
          Theme::ScopedText small(Theme::Text::SMALL);
          if (!shown(3))
            ImGui::TextColored(palette.muted, "%s", row.terms_text.c_str());
          if (!shown(4) && !row.loan)
            ImGui::TextColored(palette.muted, "%s", row.ratio_text.c_str());
          if (!shown(5))
            ImGui::TextColored(palette.muted, "%s", row.expires_text.c_str());
        }
        if (UI::cell(mask, 1)) ImGui::TextUnformatted(row.club.c_str());
        if (UI::cell(mask, 2)) UI::textRight(row.amount_text.c_str());
        if (UI::cell(mask, 3))
          ImGui::TextColored(palette.muted, "%s", row.terms_text.c_str());
        if (UI::cell(mask, 4) && !row.loan)
          ImGui::TextColored(row.value_ratio >= Tuning::PERCENT_SCALE
                                 ? palette.positive
                                 : palette.warning,
                             "%s", row.ratio_text.c_str());
        if (UI::cell(mask, 5)) ImGui::TextUnformatted(row.expires_text.c_str());
        UI::cell(mask, 6);
        // Bids and loan offers are negotiated in the talks dialog.
        if (UI::primaryButton(row.awaiting ? viewTalks : negotiate,
                              ImVec2(0.0f, 0.0f), COMPACT))
          negotiation_dialog.open(controller, row.offer_id);
        ImGui::PopID();
        if (changed) break;
      }
      ImGui::EndTable();
    }
  }
  if (changed)
  {
    refreshData();
    return;
  }

  ImGui::Dummy(ImVec2(0.0f, Theme::Space::M * scale()));
  UI::sectionLabel(LOC("TRANSFER_YOUR_TALKS"));
  if (talks.empty())
  {
    ImGui::TextColored(palette.muted, "%s", LOC("TRANSFER_NO_TALKS"));
    return;
  }
  if (!UI::beginDataTable(
          "Talks", Tuning::Tables::TALK_COLUMN_COUNT,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
          Tuning::Layout::WIDE_TABLE_MIN_WIDTH,
          ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
    return;
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_NAME"),
                          ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_TEAM"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_STAGE"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_AGREED"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_DATE"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_ACTION"));
  ImGui::TableHeadersRow();
  for (const TalkRow& row : talks)
  {
    ImGui::TableNextRow();
    ImGui::PushID(static_cast<int>(row.player_id));
    ImGui::TableNextColumn();
    if (UI::link(row.player.c_str(), "name"))
      Navigation::openPlayer(guiView, row.player_id);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(row.club.c_str());
    ImGui::TableNextColumn();
    ImGui::TextColored(row.pre_contract ? palette.info : palette.muted, "%s",
                       row.stage.c_str());
    ImGui::TableNextColumn();
    UI::textRight(row.fee_text.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(row.date_text.c_str());
    ImGui::TableNextColumn();
    if (row.can_continue && UI::primaryButton(LOC("TRANSFER_ACTION_TERMS")))
      openContractDialog(row.player_id);
    ImGui::PopID();
  }
  ImGui::EndTable();
}

void TransferMarketScene::renderLoansTab()
{
  const Theme::Palette& palette = Theme::palette();
  auto& controller = guiView->getController();
  if (loans.empty())
  {
    UI::emptyState(LOC("TRANSFER_NO_LOANS_TITLE"),
                   LOC("TRANSFER_NO_LOANS_BODY"));
    return;
  }
  if (!UI::beginDataTable(
          "LoansTable", Tuning::Tables::LOAN_COLUMN_COUNT,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
          Tuning::Layout::WIDE_TABLE_MIN_WIDTH,
          ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
    return;
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_NAME"),
                          ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_DIRECTION"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_TEAM"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_UNTIL"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_WAGE_SHARE"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_OPTION"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_ACTION"));
  ImGui::TableHeadersRow();
  bool changed = false;
  for (const LoanRow& row : loans)
  {
    ImGui::TableNextRow();
    ImGui::PushID(static_cast<int>(row.player_id));
    ImGui::TableNextColumn();
    if (UI::link(row.player.c_str(), "name"))
      Navigation::openPlayer(guiView, row.player_id);
    ImGui::TableNextColumn();
    UI::badge(LOC(row.incoming ? "TRANSFER_LOAN_IN" : "TRANSFER_LOAN_OUT"),
              row.incoming ? palette.positive : palette.info);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(row.club.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(row.until_text.c_str());
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(row.share_text.c_str());
    ImGui::TableNextColumn();
    ImGui::TextColored(palette.muted, "%s", row.option_text.c_str());
    ImGui::TableNextColumn();
    if (row.can_recall && ImGui::Button(LOC("TRANSFER_RECALL")))
    {
      const bool done = controller.recallLoan(row.player_id);
      showToast(LOC(done ? "TRANSFER_RECALLED_TOAST" : "TRANSFER_DEAL_FAILED"),
                !done);
      changed = true;
    }
    if (row.can_buy && UI::primaryButton(LOC("TRANSFER_EXERCISE_OPTION")))
    {
      const bool done = controller.exerciseLoanOption(row.player_id);
      showToast(done ? fmt::sprintf(LOC("TRANSFER_COMPLETED_TOAST"), row.player)
                     : std::string(LOC("TRANSFER_NOT_ENOUGH_BUDGET")),
                !done);
      changed = true;
    }
    ImGui::PopID();
    if (changed) break;
  }
  ImGui::EndTable();
  if (changed) refreshData();
}

void TransferMarketScene::renderSquadTab()
{
  const Theme::Palette& palette = Theme::palette();
  auto& controller = guiView->getController();
  ImGui::TextColored(palette.muted, "%s", LOC("TRANSFER_SQUAD_HINT"));
  if (!UI::beginDataTable(
          "SquadMarket", Tuning::Tables::SQUAD_COLUMN_COUNT,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
          Tuning::Layout::WIDE_TABLE_MIN_WIDTH,
          ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
    return;
  ImGui::TableSetupScrollFreeze(1, 1);
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_NAME"),
                          ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_ROLE"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_AGE"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_OVR"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_VALUE"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_WAGE"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_CONTRACT"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_STATUS"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_ACTION"));
  ImGui::TableHeadersRow();
  bool changed = false;
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(squad.size()));
  while (clipper.Step() && !changed)
  {
    for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line)
    {
      const SquadRow& row = squad[static_cast<size_t>(line)];
      const PlayerView::PlayerRow& player = row.player;
      ImGui::TableNextRow();
      ImGui::PushID(static_cast<int>(player.id));
      ImGui::TableNextColumn();
      if (UI::link(player.name.c_str(), "name"))
        Navigation::openPlayer(guiView, player.id);
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.muted, "%s", player.role.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%d", player.age);
      ImGui::TableNextColumn();
      UI::ratingChip(player.overall);
      ImGui::TableNextColumn();
      UI::textRight(player.value_text.c_str());
      ImGui::TableNextColumn();
      UI::textRight(player.wage_text.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%d", player.contract_years);
      ImGui::TableNextColumn();
      if (row.borrowed)
        UI::badge(LOC("TRANSFER_BADGE_BORROWED"), palette.positive);
      else if (row.committed)
        UI::badge(LOC("TRANSFER_BADGE_LEAVING"), palette.negative);
      if (row.asking_price > 0)
      {
        UI::badge(row.asking_text.c_str(), palette.info);
        ImGui::SameLine();
      }
      if (row.loan_listed)
        UI::badge(LOC("TRANSFER_BADGE_LOAN"), palette.accent);
      if (!row.clause_text.empty())
      {
        Theme::ScopedText small(Theme::Text::SMALL);
        ImGui::TextColored(palette.muted, "%s", row.clause_text.c_str());
      }
      ImGui::TableNextColumn();
      const bool tradable = !row.borrowed && !row.committed;
      ImGui::BeginDisabled(!tradable);
      if (row.asking_price > 0)
      {
        if (ImGui::SmallButton(LOC("TRANSFER_UNLIST")))
        {
          controller.removePlayerFromTransfer(player.id);
          showToast(LOC("PROFILE_UNLISTED_TOAST"));
          changed = true;
        }
      }
      else
      {
        ImGui::BeginDisabled(!window.open);
        if (ImGui::SmallButton(LOC("TRANSFER_LIST_SHORT")))
          listing_dialog = {true, player.id, player.name, player.market_value,
                            player.market_value};
        ImGui::EndDisabled();
      }
      ImGui::SameLine();
      if (ImGui::SmallButton(LOC(row.loan_listed ? "TRANSFER_UNLIST_LOAN"
                                                 : "TRANSFER_LIST_LOAN")))
      {
        controller.setLoanListed(player.id, !row.loan_listed);
        changed = true;
      }
      ImGui::SameLine();
      // Nobody may leave the squad short of eleven or of a goalkeeper.
      const bool releasable = *row.release_block == '\0';
      ImGui::BeginDisabled(!releasable);
      const bool release = ImGui::SmallButton(LOC("TRANSFER_RELEASE"));
      ImGui::EndDisabled();
      if (!releasable &&
          ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", LOC(row.release_block));
      if (release)
      {
        release_dialog.requested = true;
        release_dialog.player_id = player.id;
        release_dialog.body = fmt::sprintf(
            LOC("TRANSFER_RELEASE_BODY"), player.name,
            Format::moneyFull(controller.getReleaseCost(player.id)));
      }
      ImGui::EndDisabled();
      ImGui::PopID();
      if (changed) break;
    }
  }
  ImGui::EndTable();
  if (changed) refreshData();
}

void TransferMarketScene::renderHistoryTab()
{
  const Theme::Palette& palette = Theme::palette();
  if (ImGui::Checkbox(LOC("TRANSFER_HISTORY_MY_CLUB"), &history_managed_only))
  {
    visible_history.clear();
    for (size_t index = 0; index < history.size(); ++index)
      if (!history_managed_only || history[index].managed)
        visible_history.push_back(index);
  }
  if (visible_history.empty())
  {
    UI::emptyState(LOC("TRANSFER_HISTORY_EMPTY_TITLE"),
                   LOC("TRANSFER_HISTORY_EMPTY_BODY"));
    return;
  }
  if (!UI::beginDataTable(
          "HistoryTable", Tuning::Tables::HISTORY_COLUMN_COUNT,
          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
              ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
          Tuning::Layout::WIDE_TABLE_MIN_WIDTH,
          ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))
    return;
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_DATE"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_NAME"),
                          ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_FROM"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_TO"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_TYPE"));
  ImGui::TableSetupColumn(LOC("TRANSFER_COL_FEE"));
  ImGui::TableHeadersRow();
  ImGuiListClipper clipper;
  clipper.Begin(static_cast<int>(visible_history.size()));
  while (clipper.Step())
  {
    for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line)
    {
      const HistoryRow& row =
          history[visible_history[static_cast<size_t>(line)]];
      ImGui::TableNextRow();
      ImGui::PushID(line);
      ImGui::TableNextColumn();
      ImGui::TextColored(palette.muted, "%s", row.date.c_str());
      ImGui::TableNextColumn();
      if (UI::link(row.player.c_str(), "name"))
        Navigation::openPlayer(guiView, row.player_id);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(row.from.c_str());
      ImGui::TableNextColumn();
      ImGui::TextColored(row.managed ? palette.accent : palette.text, "%s",
                         row.to.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(LOC(row.kind_key));
      ImGui::TableNextColumn();
      if (row.severance)
      {
        UI::textRightColored(palette.muted, row.fee_text.c_str());
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("%s", LOC("TRANSFER_HISTORY_SEVERANCE_TIP"));
      }
      else
      {
        UI::textRight(row.fee_text.c_str());
      }
      ImGui::PopID();
    }
  }
  ImGui::EndTable();
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

bool TransferMarketScene::wideDialogs() const
{
  return ImGui::GetMainViewport()->Size.x >=
         Tuning::Layout::TWO_COLUMN_MIN_VIEWPORT * scale();
}

float TransferMarketScene::dialogWidth() const
{
  return std::min(
      (wideDialogs() ? Tuning::Layout::WIDE_DIALOG_WIDTH
                     : Tuning::Layout::DIALOG_WIDTH) *
          scale(),
      ImGui::GetMainViewport()->Size.x * Tuning::Layout::DIALOG_VIEWPORT_SHARE);
}

void TransferMarketScene::renderDialogColumns(
    const std::function<void()>& terms,
    const std::function<void()>& summary) const
{
  // Terms on the left, a summary card on the right; stacked when narrow.
  if (wideDialogs() && ImGui::BeginTable("##dialog_columns", 2,
                                         ImGuiTableFlags_SizingStretchProp))
  {
    ImGui::TableSetupColumn("##terms", ImGuiTableColumnFlags_WidthStretch,
                            Tuning::Layout::TERMS_COLUMN_WEIGHT);
    ImGui::TableSetupColumn("##summary", ImGuiTableColumnFlags_WidthStretch,
                            Tuning::Layout::SUMMARY_COLUMN_WEIGHT);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    terms();
    ImGui::TableNextColumn();
    summary();
    ImGui::EndTable();
    return;
  }
  terms();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale()));
  summary();
}

void TransferMarketScene::renderReasons(
    const std::vector<TransferNegotiation::Reason>& reasons,
    const ImVec4& color) const
{
  ImGui::PushTextWrapPos(0.0f);
  for (const TransferNegotiation::Reason reason : reasons)
  {
    ImGui::Bullet();
    ImGui::TextColored(color, "%s",
                       LOC(TransferNegotiation::reasonKey(reason)));
  }
  ImGui::PopTextWrapPos();
}

void TransferMarketScene::renderClubResponse(const ClubResponse& response,
                                             const std::string& detail) const
{
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale()));
  {
    Theme::ScopedText heading(Theme::Text::TITLE);
    ImGui::TextColored(decisionColor(response.decision), "%s",
                       LOC(decisionKey(response.decision)));
  }
  if (!detail.empty())
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(detail.c_str());
    ImGui::PopTextWrapPos();
  }
  renderReasons(response.reasons, Theme::palette().muted);
}

void TransferMarketScene::openOfferDialog(const TargetRow& row)
{
  offer_dialog = {};
  offer_dialog.requested = true;
  offer_dialog.player_id = row.id;
  offer_dialog.player = row.name;
  offer_dialog.club = row.club;
  offer_dialog.value = row.value;
  offer_dialog.asking_price = row.asking_price;
  offer_dialog.release_clause = row.release_clause;
  offer_dialog.wage = row.wage;
  offer_dialog.estimate_text = row.overall_text;
  offer_dialog.knowledge = row.knowledge;
  // Opening bid: the asking price, else the estimated value rounded to a
  // bid a club would make.
  offer_dialog.terms.fee = row.asking_price > 0
                               ? row.asking_price
                               : clampFee((row.value + BID_ROUNDING / 2) /
                                          BID_ROUNDING * BID_ROUNDING);
}

void TransferMarketScene::openContractDialog(PlayerID player_id)
{
  contract_talks.open(guiView->getController(), player_id);
}

void TransferMarketScene::openLoanDialog(const TargetRow& row)
{
  loan_dialog = {};
  loan_dialog.requested = true;
  loan_dialog.player_id = row.id;
  loan_dialog.player = row.name;
  loan_dialog.club = row.club;
  loan_dialog.wage = row.wage;
  loan_dialog.value = row.value;
  loan_dialog.terms.wage_share = TransferTuning::Loan::LISTED_WAGE_SHARE;
}

void TransferMarketScene::renderOfferDialog()
{
  if (offer_dialog.requested)
  {
    ImGui::OpenPopup("###transfer_offer");
    offer_dialog.requested = false;
  }
  placeDialog(dialogWidth());
  if (const std::string title =
          fmt::sprintf(LOC("TRANSFER_OFFER_TITLE"), offer_dialog.player) +
          "###transfer_offer";
      !ImGui::BeginPopupModal(
          title.c_str(), nullptr,
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    return;
  auto& controller = guiView->getController();
  const Theme::Palette& palette = Theme::palette();
  TransferNegotiation::OfferTerms& terms = offer_dialog.terms;
  const std::optional<ClubResponse>& response = offer_dialog.response;
  const bool accepted =
      response && response->decision == ClubResponse::Decision::Accept;

  // Figures shared by the terms and the summary.
  TransferMarket::Deal probe;
  probe.terms = terms;
  const int64_t upfront = TransferNegotiation::upfrontAmount(terms);
  const int64_t agent = TransferMarket::agentFee(probe);
  const int64_t signing = upfront + agent;
  const auto instalments = TransferNegotiation::instalmentAmounts(terms);
  const int64_t add_ons = static_cast<int64_t>(terms.appearance_bonus) +
                          static_cast<int64_t>(terms.goal_bonus);
  const auto expected_wage = static_cast<int64_t>(
      std::llround(static_cast<double>(offer_dialog.wage) *
                   static_cast<double>(ScoutingTuning::EXPECTED_WAGE_RAISE)));
  const bool over_budget = signing > available_budget;

  const auto termsColumn = [this, &palette, &terms, &response, accepted]
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(
        palette.muted, "%s",
        fmt::sprintf(LOC("TRANSFER_OFFER_HEADER"), offer_dialog.club,
                     Format::money(offer_dialog.value))
            .c_str());
    ImGui::TextColored(
        palette.faint, "%s",
        fmt::sprintf(LOC("TRANSFER_OFFER_ESTIMATE"), offer_dialog.estimate_text,
                     offer_dialog.knowledge)
            .c_str());
    ImGui::PopTextWrapPos();

    const int64_t fee = terms.fee;
    if (offer_dialog.release_clause > 0)
    {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(
          palette.info, "%s",
          fmt::sprintf(LOC("TRANSFER_CLAUSE_HINT"),
                       Format::money(offer_dialog.release_clause))
              .c_str());
      ImGui::PopTextWrapPos();
    }
    std::vector<UI::MoneyChip> chips;
    if (offer_dialog.release_clause > 0)
      chips.push_back(
          {LOC("TRANSFER_CHIP_CLAUSE"), offer_dialog.release_clause});
    if (offer_dialog.asking_price > 0)
      chips.push_back({LOC("TRANSFER_CHIP_ASKING"), offer_dialog.asking_price});
    chips.push_back({LOC("TRANSFER_CHIP_VALUE"), offer_dialog.value});
    if (response && response->counter_fee > 0 && !accepted)
      chips.push_back({LOC("TRANSFER_CHIP_COUNTER"), response->counter_fee});
    chips.push_back({LOC("TRANSFER_CHIP_MINUS_10"), scaledAmount(fee, 0.9)});
    chips.push_back({LOC("TRANSFER_CHIP_PLUS_10"), scaledAmount(fee, 1.1)});
    edit(terms, chips);
  };

  const auto summaryColumn = [this, &palette, &terms, upfront, instalments,
                              agent, add_ons, signing, expected_wage,
                              over_budget, &response, accepted]
  {
    const bool panel =
        beginPanel("##offer_summary", LOC("TRANSFER_SUMMARY_TITLE"));
    UI::summaryRow(LOC("TRANSFER_SUMMARY_FEE"),
                   Format::moneyFull(terms.fee).c_str());
    UI::summaryRow(LOC("TRANSFER_SUMMARY_UPFRONT"),
                   Format::moneyFull(upfront).c_str());
    if (!instalments.empty())
      UI::summaryRow(LOC("TRANSFER_SUMMARY_INSTALMENTS"),
                     fmt::sprintf(LOC("TRANSFER_SUMMARY_INSTALMENTS_VALUE"),
                                  static_cast<int>(instalments.size()),
                                  Format::money(instalments.front()))
                         .c_str());
    UI::summaryRow(LOC("TRANSFER_SUMMARY_AGENT"),
                   Format::moneyFull(agent).c_str());
    if (add_ons > 0)
      UI::summaryRow(LOC("TRANSFER_SUMMARY_ADD_ONS"),
                     Format::moneyFull(add_ons).c_str());
    UI::summaryRow(
        LOC("TRANSFER_SUMMARY_TOTAL"),
        Format::moneyFull(static_cast<int64_t>(terms.fee) + agent + add_ons)
            .c_str(),
        nullptr, true);
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale()));
    UI::budgetImpact(LOC("TRANSFER_BUDGET_IMPACT"), available_budget,
                     available_budget - signing);
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale()));
    UI::summaryRow(LOC("TRANSFER_WAGE_EXPECTED"),
                   fmt::sprintf(LOC("TRANSFER_WAGE_EXPECTED_VALUE"),
                                Format::money(expected_wage))
                       .c_str());
    UI::budgetImpact(LOC("TRANSFER_WAGE_IMPACT"), wage_room,
                     wage_room - expected_wage, LOC("TRANSFER_WAGE_WARNING"));
    if (panel) endPanel();

    if (over_budget && !accepted)
    {
      // Say what would fit: spread the payments, else lower the fee.
      const Suggestion fix = budgetFix(terms, available_budget);
      ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale()));
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextColored(palette.negative, "%s",
                         fmt::sprintf(LOC("TRANSFER_OVER_BUDGET_BY"),
                                      Format::money(signing - available_budget))
                             .c_str());
      if (fix.terms)
      {
        const std::string text =
            fix.terms->fee == terms.fee
                ? fmt::sprintf(LOC("TRANSFER_SUGGEST_SPREAD"),
                               static_cast<int>(fix.terms->upfront_percent),
                               static_cast<int>(fix.terms->instalment_years))
                : fmt::sprintf(LOC("TRANSFER_SUGGEST_LOWER"),
                               Format::money(fix.terms->fee));
        ImGui::TextColored(palette.muted, "%s", text.c_str());
        ImGui::PopTextWrapPos();
        if (UI::secondaryButton(LOC("TRANSFER_APPLY_SUGGESTION")))
          terms = *fix.terms;
      }
      else
      {
        ImGui::TextColored(palette.muted, "%s",
                           LOC("TRANSFER_NOT_ENOUGH_BUDGET"));
        ImGui::PopTextWrapPos();
      }
    }
    if (response)
      renderClubResponse(
          *response,
          response->counter_fee > 0 && !accepted
              ? fmt::sprintf(LOC("TRANSFER_COUNTER_FEE"),
                             Format::moneyFull(response->counter_fee))
              : std::string());
    if (!offer_dialog.agent_line.empty())
    {
      ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale()));
      if (beginPanel("##offer_agent", LOC("AGENT_SAYS_TITLE")))
      {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(offer_dialog.agent_line.c_str());
        ImGui::PopTextWrapPos();
        endPanel();
      }
    }
  };
  renderDialogColumns(termsColumn, summaryColumn);

  const bool valid = TransferNegotiation::isValid(terms) && terms.fee > 0;
  ImGui::Separator();
  const char* submit =
      LOC(accepted ? "TRANSFER_ACTION_TERMS" : "TRANSFER_SUBMIT_OFFER");
  const bool counter = response && response->counter_fee > 0 && !accepted;
  alignActions({LOC("TRANSFER_CANCEL"),
                counter ? LOC("TRANSFER_MATCH_COUNTER") : nullptr, submit});
  if (UI::secondaryButton(LOC("TRANSFER_CANCEL")) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    ImGui::CloseCurrentPopup();
  if (counter)
  {
    ImGui::SameLine();
    if (UI::secondaryButton(LOC("TRANSFER_MATCH_COUNTER")))
      terms.fee = response->counter_fee;
  }
  ImGui::SameLine();
  if (accepted)
  {
    if (UI::primaryButton(submit))
    {
      ImGui::CloseCurrentPopup();
      openContractDialog(offer_dialog.player_id);
    }
  }
  else
  {
    ImGui::BeginDisabled(!valid || over_budget);
    if (UI::primaryButton(submit))
    {
      offer_dialog.response =
          controller.makeTransferOffer(offer_dialog.player_id, terms);
      const char* line = controller.getAgentPurchaseLine(
          offer_dialog.player_id, *offer_dialog.response);
      offer_dialog.agent_line =
          *line == '\0' ? std::string()
                        : fmt::sprintf(LOC(line), offer_dialog.player);
      refreshOffers();
    }
    ImGui::EndDisabled();
  }
  ImGui::EndPopup();
}

void TransferMarketScene::renderLoanDialog()
{
  if (loan_dialog.requested)
  {
    ImGui::OpenPopup("###transfer_loan");
    loan_dialog.requested = false;
  }
  placeDialog(dialogWidth());
  if (const std::string title =
          fmt::sprintf(LOC("TRANSFER_LOAN_TITLE"), loan_dialog.player) +
          "###transfer_loan";
      !ImGui::BeginPopupModal(
          title.c_str(), nullptr,
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    return;
  auto& controller = guiView->getController();
  const Theme::Palette& palette = Theme::palette();
  TransferNegotiation::LoanTerms& terms = loan_dialog.terms;
  const GameDateValue today = controller.getCurrentDate();
  const int weeks = TransferNegotiation::weeksBetween(
      today, TransferNegotiation::loanEndDate(today, terms.duration));
  const int64_t weekly =
      static_cast<int64_t>(loan_dialog.wage) * terms.wage_share / 100;

  const auto termsColumn = [this, &palette, &terms]
  {
    ImGui::TextColored(
        palette.muted, "%s",
        fmt::sprintf(LOC("TRANSFER_LOAN_HEADER"), loan_dialog.club).c_str());
    formLabel(LOC("TRANSFER_FIELD_DURATION"));
    int duration =
        terms.duration == TransferNegotiation::LoanDuration::SeasonEnd ? 0 : 1;
    if (const std::array<const char*, 2> durations =
            {LOC("TRANSFER_LOAN_SEASON"), LOC("TRANSFER_LOAN_SIX_MONTHS")};
        UI::segmented("##duration", duration, durations))
      terms.duration = duration == 0
                           ? TransferNegotiation::LoanDuration::SeasonEnd
                           : TransferNegotiation::LoanDuration::SixMonths;
    formLabel(LOC("TRANSFER_FIELD_WAGE_SHARE"));
    if (int shareIndex = optionIndex(WAGE_SHARE_OPTIONS, terms.wage_share);
        UI::segmented("##share", shareIndex, WAGE_SHARE_LABELS))
      terms.wage_share = WAGE_SHARE_OPTIONS[static_cast<size_t>(shareIndex)];
    formLabel(LOC("TRANSFER_FIELD_LOAN_FEE"));
    if (int64_t fee = terms.loan_fee;
        UI::moneyInput("##loan_fee", fee,
                       {.maximum = std::numeric_limits<uint32_t>::max()}))
      terms.loan_fee = static_cast<uint32_t>(fee);
    formLabel(LOC("TRANSFER_FIELD_OPTION_FEE"));
    int64_t option = terms.option_fee;
    if (const std::array<UI::MoneyChip, 2> optionChips =
            {{{LOC("TRANSFER_CHIP_NONE"), 0},
              {LOC("TRANSFER_CHIP_OPTION_VALUE"),
               scaledAmount(loan_dialog.value,
                            TransferTuning::Loan::OPTION_VALUE_MULTIPLE)}}};
        UI::moneyInput("##option_fee", option,
                       {.maximum = std::numeric_limits<uint32_t>::max(),
                        .chips = optionChips}))
      terms.option_fee = static_cast<uint32_t>(option);
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale()));
    ImGui::Checkbox(LOC("TRANSFER_FIELD_OBLIGATION"), &terms.obligation);
    ImGui::SameLine();
    ImGui::Checkbox(LOC("TRANSFER_FIELD_RECALL"), &terms.recall_clause);
  };

  const auto summaryColumn = [this, &palette, weekly, weeks, &terms]
  {
    const bool panel =
        beginPanel("##loan_summary", LOC("TRANSFER_SUMMARY_TITLE"));
    UI::summaryRow(
        LOC("TRANSFER_SUMMARY_WEEKLY_SHARE"),
        fmt::sprintf(LOC("TRANSFER_PER_WEEK"), Format::moneyFull(weekly))
            .c_str());
    UI::summaryRow(
        LOC("TRANSFER_SUMMARY_LOAN_TOTAL"),
        Format::moneyFull(weekly * weeks + static_cast<int64_t>(terms.loan_fee))
            .c_str(),
        nullptr, true);
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::S * scale()));
    UI::budgetImpact(LOC("TRANSFER_BUDGET_IMPACT"), available_budget,
                     available_budget - static_cast<int64_t>(terms.loan_fee),
                     LOC("TRANSFER_NOT_ENOUGH_BUDGET"));
    UI::budgetImpact(LOC("TRANSFER_WAGE_IMPACT"), wage_room, wage_room - weekly,
                     LOC("TRANSFER_WAGE_OVER_ROOM"));
    if (panel) endPanel();
    if (loan_dialog.response)
    {
      const ClubResponse& response = *loan_dialog.response;
      std::string detail;
      if (response.decision == ClubResponse::Decision::Counter)
      {
        const std::string optionFee =
            response.counter_option_fee > 0
                ? Format::moneyFull(response.counter_option_fee)
                : std::string("-");
        detail = fmt::sprintf(LOC("TRANSFER_LOAN_COUNTER"),
                              static_cast<int>(response.counter_wage_share),
                              optionFee);
      }
      renderClubResponse(response, detail);
    }
  };
  renderDialogColumns(termsColumn, summaryColumn);

  ImGui::Separator();
  const bool counter =
      loan_dialog.response &&
      loan_dialog.response->decision == ClubResponse::Decision::Counter;
  alignActions({LOC("TRANSFER_CANCEL"),
                counter ? LOC("TRANSFER_MATCH_COUNTER") : nullptr,
                LOC("TRANSFER_SUBMIT_OFFER")});
  if (UI::secondaryButton(LOC("TRANSFER_CANCEL")) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    ImGui::CloseCurrentPopup();
  if (counter)
  {
    ImGui::SameLine();
    if (UI::secondaryButton(LOC("TRANSFER_MATCH_COUNTER")))
    {
      terms.wage_share = loan_dialog.response->counter_wage_share;
      terms.option_fee = loan_dialog.response->counter_option_fee;
    }
  }
  ImGui::SameLine();
  if (UI::primaryButton(LOC("TRANSFER_SUBMIT_OFFER")))
  {
    loan_dialog.response =
        controller.makeLoanOffer(loan_dialog.player_id, terms);
    if (loan_dialog.response->decision == ClubResponse::Decision::Accept)
    {
      showToast(fmt::sprintf(LOC("TRANSFER_LOAN_TOAST"), loan_dialog.player));
      refreshData();
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::EndPopup();
}

void TransferMarketScene::renderListingDialog()
{
  if (listing_dialog.requested)
  {
    ImGui::OpenPopup("###transfer_listing");
    listing_dialog.requested = false;
  }
  placeDialog(std::min(Tuning::Layout::DIALOG_WIDTH * scale(),
                       ImGui::GetMainViewport()->Size.x *
                           Tuning::Layout::DIALOG_VIEWPORT_SHARE));
  if (const std::string title =
          fmt::sprintf(LOC("TRANSFER_LIST_TITLE"), listing_dialog.player) +
          "###transfer_listing";
      !ImGui::BeginPopupModal(
          title.c_str(), nullptr,
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    return;
  formLabel(LOC("TRANSFER_ASKING_PRICE"));
  int64_t price = listing_dialog.price;
  if (const std::array<UI::MoneyChip, 3> chips =
          {{{LOC("TRANSFER_CHIP_MARKET_VALUE"), listing_dialog.value},
            {LOC("TRANSFER_CHIP_PLUS_25"),
             scaledAmount(listing_dialog.value, 1.25)},
            {LOC("TRANSFER_CHIP_PLUS_50"),
             scaledAmount(listing_dialog.value, 1.5)}}};
      UI::moneyInput(
          "##asking", price,
          {.maximum = std::numeric_limits<uint32_t>::max(), .chips = chips}))
    listing_dialog.price = static_cast<uint32_t>(price);
  ImGui::Separator();
  alignActions({LOC("TRANSFER_CANCEL"), LOC("TRANSFER_LIST_PLAYER")});
  if (UI::secondaryButton(LOC("TRANSFER_CANCEL")) ||
      ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    ImGui::CloseCurrentPopup();
  ImGui::SameLine();
  ImGui::BeginDisabled(listing_dialog.price == 0);
  if (UI::primaryButton(LOC("TRANSFER_LIST_PLAYER")))
  {
    auto& controller = guiView->getController();
    controller.listPlayerForTransfer(listing_dialog.player_id,
                                     listing_dialog.price);
    const bool listed = controller.isPlayerListed(listing_dialog.player_id);
    showToast(LOC(listed ? "PROFILE_LISTED_TOAST" : "TRANSFER_DEAL_FAILED"),
              !listed);
    refreshData();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  ImGui::EndPopup();
}

void TransferMarketScene::renderReleaseDialog()
{
  if (release_dialog.requested)
  {
    ImGui::OpenPopup("###transfer_release");
    release_dialog.requested = false;
  }
  if (const UI::DialogResult result = UI::confirmDialog(
          "###transfer_release", LOC("TRANSFER_RELEASE_TITLE"),
          release_dialog.body.c_str(), LOC("TRANSFER_RELEASE"),
          LOC("TRANSFER_CANCEL"));
      result != UI::DialogResult::CONFIRM)
    return;
  GameController& controller = guiView->getController();
  const GameController::PlayerActionBlock block =
      controller.getReleaseBlock(release_dialog.player_id);
  const bool released = controller.releasePlayer(release_dialog.player_id);
  showToast(LOC(released ? "TRANSFER_RELEASED_TOAST"
                : block != GameController::PlayerActionBlock::None
                    ? GameController::playerActionBlockKey(block)
                    : "TRANSFER_DEAL_FAILED"),
            !released);
  refreshData();
}

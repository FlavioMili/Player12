// -----------------------------------------------------------------------------
//  Football Management Project
//  Copyright (c) 2025 - 2026 Flavio Milinanni. All Rights Reserved.
//
//  This file is part of the Football Management Project.
//  See the LICENSE file in the project root.
// -----------------------------------------------------------------------------

#include "gui/scenes/youth_scene.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <format>
#include <utility>

#include "database/gamedata.h"
#include "global/language_manager.h"
#include "gui/gui_view.h"
#include "gui/widgets/format.h"
#include "gui/widgets/theme.h"
#include "model/calendar.h"
#include "model/inbox.h"
#include "model/role_utils.h"
#include "model/world_rng.h"

namespace
{
constexpr float TWO_COLUMN_MIN_WIDTH = 1000.0f;
constexpr float KEY_WIDTH = 170.0f;
constexpr float CHART_HEIGHT = 220.0f;
constexpr std::size_t RECENT_RESULTS = 5;
constexpr std::size_t IMPROVERS = 6;
constexpr const char* CONFIRM_POPUP_ID = "##youth_confirm";

/** @brief What a table column shows. */
enum class Cell : uint8_t
{
  NAME,
  POS,
  AGE,
  ABILITY,
  POTENTIAL,
  CONTRACT,
  APPS,
  RATING,
  PERSONALITY,
  HOMEGROWN,
  NATION,
  HEIGHT,
  GROWTH
};

struct ColumnSpec
{
  Cell cell;
  UI::Column column;
};

// Columns hide from the highest priority number down as the card narrows;
// actions live in the selected row's detail strip, never in a column.
constexpr std::array<ColumnSpec, 10> SQUAD_COLUMNS = {{
    {Cell::NAME, {"YOUTH_COL_NAME", 0.0f, 0}},
    {Cell::POS, {"YOUTH_COL_POS", 52.0f, 0}},
    {Cell::AGE, {"YOUTH_COL_AGE", 44.0f, 1}},
    {Cell::ABILITY, {"YOUTH_COL_ABILITY", 76.0f, 0}},
    {Cell::POTENTIAL, {"YOUTH_COL_POTENTIAL", 84.0f, 0}},
    {Cell::CONTRACT, {"YOUTH_COL_CONTRACT", 150.0f, 2}},
    {Cell::HOMEGROWN, {"YOUTH_COL_HOMEGROWN", 44.0f, 3}},
    {Cell::APPS, {"YOUTH_COL_APPS", 52.0f, 4}},
    {Cell::RATING, {"YOUTH_COL_RATING", 60.0f, 4}},
    {Cell::PERSONALITY, {"YOUTH_COL_PERSONALITY", 150.0f, 5}},
}};

constexpr std::array<ColumnSpec, 7> ELIGIBLE_COLUMNS = {{
    {Cell::NAME, {"YOUTH_COL_NAME", 0.0f, 0}},
    {Cell::POS, {"YOUTH_COL_POS", 52.0f, 0}},
    {Cell::AGE, {"YOUTH_COL_AGE", 44.0f, 1}},
    {Cell::ABILITY, {"YOUTH_COL_ABILITY", 76.0f, 0}},
    {Cell::POTENTIAL, {"YOUTH_COL_POTENTIAL", 84.0f, 0}},
    {Cell::CONTRACT, {"YOUTH_COL_CONTRACT", 150.0f, 2}},
    {Cell::PERSONALITY, {"YOUTH_COL_PERSONALITY", 150.0f, 3}},
}};

constexpr std::array<ColumnSpec, 8> INTAKE_COLUMNS = {{
    {Cell::NAME, {"YOUTH_COL_NAME", 0.0f, 0}},
    {Cell::POS, {"YOUTH_COL_POS", 52.0f, 0}},
    {Cell::AGE, {"YOUTH_COL_AGE", 44.0f, 1}},
    {Cell::ABILITY, {"YOUTH_COL_ABILITY", 76.0f, 0}},
    {Cell::POTENTIAL, {"YOUTH_COL_POTENTIAL", 84.0f, 0}},
    {Cell::PERSONALITY, {"YOUTH_COL_PERSONALITY", 150.0f, 2}},
    {Cell::NATION, {"YOUTH_COL_NATION", 120.0f, 3}},
    {Cell::HEIGHT, {"YOUTH_COL_HEIGHT", 64.0f, 4}},
}};

constexpr std::array<ColumnSpec, 6> DEVELOPMENT_COLUMNS = {{
    {Cell::NAME, {"YOUTH_COL_NAME", 0.0f, 0}},
    {Cell::POS, {"YOUTH_COL_POS", 52.0f, 1}},
    {Cell::AGE, {"YOUTH_COL_AGE", 44.0f, 2}},
    {Cell::ABILITY, {"YOUTH_COL_ABILITY", 76.0f, 1}},
    {Cell::POTENTIAL, {"YOUTH_COL_POTENTIAL", 84.0f, 1}},
    {Cell::GROWTH, {"YOUTH_COL_GROWTH", 72.0f, 0}},
}};

std::span<const ColumnSpec> columnsFor(int kind)
{
  switch (kind)
  {
    case 1:
      return ELIGIBLE_COLUMNS;
    case 2:
      return INTAKE_COLUMNS;
    case 3:
      return DEVELOPMENT_COLUMNS;
    default:
      return SQUAD_COLUMNS;
  }
}

std::string nationalityName(Language nationality)
{
  const auto name = languageToString.find(nationality);
  if (name == languageToString.end()) return {};
  const std::string key = "NATION_" + name->second;
  const char* localized = LOC(key.c_str());
  // LOC returns the key itself when a translation is missing.
  return key == localized ? name->second : std::string(localized);
}

std::string range(float low, float high)
{
  const auto lo = static_cast<int>(std::lround(low));
  const auto hi = static_cast<int>(std::lround(high));
  return lo == hi ? std::to_string(lo) : std::format("{}-{}", lo, hi);
}

ImVec4 qualityColor(IntakeQuality quality)
{
  const Theme::Palette& palette = Theme::palette();
  using enum IntakeQuality;
  switch (quality)
  {
    case Golden:
      return palette.positive;
    case Promising:
      return palette.info;
    case Weak:
      return palette.warning;
    case Average:
      break;
  }
  return palette.muted;
}

GameDateValue dateOfDay(const GameDateValue& today, std::int32_t ordinal)
{
  return SeasonCalendar::addDays(today, ordinal - dayOrdinal(today));
}
}  // namespace

YouthScene::YouthScene(GUIView* parent) : ManagementScene(parent) {}

void YouthScene::update(float /*deltaTime*/)
{
  /* Youth data refreshes after explicit controller actions and date changes;
   * the screen has no frame-driven state. */
}

YouthScene::Row YouthScene::makeRow(
    const GameController::YouthPlayerView& view) const
{
  Row row;
  row.view = view;
  row.role = RoleUtils::shortName(view.role);
  row.ability = range(view.estimate.current_low, view.estimate.current_high);
  row.potential =
      range(view.estimate.potential_low, view.estimate.potential_high);
  row.nationality = nationalityName(view.nationality);
  row.height = std::format("{} cm", view.height);
  row.wage = formatLocalized("YOUTH_PER_WEEK", {Format::money(view.wage)});
  if (view.status == YouthStatus::Candidate)
    row.contract = LOC("YOUTH_CONTRACT_TRIAL");
  else
    row.contract = formatLocalized("YOUTH_CONTRACT_CELL",
                                   {LOC(YouthModel::contractKey(view.contract)),
                                    std::to_string(view.contract_years)});
  if (view.offer != YouthContract::None)
    row.offer_label = formatLocalized(view.offer == YouthContract::Scholarship
                                          ? "YOUTH_OFFER_SCHOLARSHIP"
                                          : "YOUTH_OFFER_PROFESSIONAL",
                                      {Format::money(view.offer_wage)});
  row.rating = view.appearances > 0 ? std::format("{:.1f}", view.average_rating)
                                    : std::string("–");
  if (view.progress.size() >= 2)
    row.growth = view.progress.back().overall - view.progress.front().overall;
  row.growth_text = std::format("{:+.1f}", row.growth);
  row.summary = std::format("{}  ·  {}  ·  {}", LOC(view.personality_key),
                            row.nationality, row.height);
  row.terms = std::format("{}  ·  {}", row.contract, row.wage);
  row.chart_caption =
      formatLocalized("YOUTH_CHART_CAPTION", {row.ability, row.potential});
  if (!view.progress.empty())
  {
    row.chart_from =
        Format::dayMonth(dateOfDay(cached_date, view.progress.front().day));
    row.chart_to =
        Format::dayMonth(dateOfDay(cached_date, view.progress.back().day));
  }
  return row;
}

void YouthScene::refresh()
{
  const GameController& controller = guiView->getController();
  cached_date = controller.getCurrentDate();
  squad_rows.clear();
  eligible_rows.clear();
  intake_rows.clear();
  development_rows.clear();
  results.clear();
  form.clear();
  improvers.clear();
  improver_values.clear();
  if (!controller.hasSelectedTeam()) return;
  overview = controller.getAcademyOverview();

  for (const auto& view : controller.getYouthPlayers(YouthStatus::Squad))
    squad_rows.push_back(makeRow(view));
  for (const auto& view : controller.getYouthEligibleFirstTeam())
    eligible_rows.push_back(makeRow(view));
  for (const auto& view : controller.getYouthPlayers(YouthStatus::Candidate))
    intake_rows.push_back(makeRow(view));
  development_rows = squad_rows;
  for (const auto& view : controller.getYouthPlayers(YouthStatus::Graduated))
    development_rows.push_back(makeRow(view));
  if (const bool known =
          std::ranges::any_of(development_rows, [this](const Row& row)
                              { return row.view.id == chart_player; });
      !known)
    chart_player =
        development_rows.empty() ? 0 : development_rows.front().view.id;

  // Biggest improvers since their first monthly snapshot.
  std::vector<const Row*> ranked;
  for (const Row& row : development_rows)
  {
    if (row.view.progress.size() >= 2 && row.growth > 0.05f)
      ranked.push_back(&row);
  }
  std::ranges::sort(
      ranked, [](const Row* a, const Row* b) { return a->growth > b->growth; });
  if (ranked.size() > IMPROVERS) ranked.resize(IMPROVERS);
  improver_values.reserve(ranked.size());
  for (const Row* row : ranked) improver_values.push_back(row->growth_text);
  const Theme::Palette& palette = Theme::palette();
  for (std::size_t i = 0; i < ranked.size(); ++i)
    improvers.emplace_back(ranked[i]->view.name, ranked[i]->growth,
                           palette.positive, improver_values[i]);

  // Overview texts.
  head_rating = overview.head.id != 0 ? std::to_string(overview.ratings.head)
                                      : std::string("–");
  const int days_to_intake =
      dayOrdinal(overview.intake_date) - dayOrdinal(cached_date);
  intake_line = days_to_intake > 0
                    ? formatLocalized("YOUTH_NEXT_INTAKE",
                                      {Format::date(overview.intake_date),
                                       std::to_string(days_to_intake)})
                    : formatLocalized("YOUTH_INTAKE_ARRIVED",
                                      {Format::date(overview.intake_date)});
  const int days_left =
      dayOrdinal(overview.decision_deadline) - dayOrdinal(cached_date);
  deadline_line = formatLocalized("YOUTH_DEADLINE",
                                  {std::to_string(overview.candidates),
                                   Format::date(overview.decision_deadline),
                                   std::to_string(std::max(0, days_left))});
  const std::string head_name = overview.head.id != 0
                                    ? overview.head.name
                                    : std::string(LOC("YOUTH_ACADEMY_STAFF"));
  if (overview.preview_ready)
  {
    std::string positions = RoleUtils::shortName(overview.preview.standout[0]);
    if (overview.preview.standout[1] != PlayerRole::UNKNOWN)
      positions.append(", ").append(
          RoleUtils::shortName(overview.preview.standout[1]));
    preview_line = formatLocalized(
        "YOUTH_PREVIEW_LINE",
        {head_name, positions, LOC(overview.preview.personality_key),
         std::to_string(overview.preview.size)});
  }
  else
  {
    preview_line =
        formatLocalized("YOUTH_PREVIEW_PENDING",
                        {head_name, Format::date(overview.preview_date)});
  }
  squad_title =
      formatLocalized("YOUTH_CARD_SQUAD", {std::to_string(squad_rows.size())});
  // The first visit during the decision window opens on the trialists.
  if (first_refresh && overview.candidates > 0) openTab(Tab::INTAKE);
  first_refresh = false;
  intake_tab_label =
      overview.candidates > 0
          ? std::format("{} ({})###intake_tab", LOC("YOUTH_TAB_INTAKE"),
                        overview.candidates)
          : std::format("{}###intake_tab", LOC("YOUTH_TAB_INTAKE"));

  // U18 league.
  const YouthTableRow& table = overview.table;
  league_line = overview.league_position > 0 && table.played > 0
                    ? formatLocalized("YOUTH_LEAGUE_POSITION",
                                      {std::to_string(overview.league_position),
                                       std::to_string(overview.league_size),
                                       std::to_string(table.points())})
                    : std::string(LOC("YOUTH_LEAGUE_NOT_STARTED"));
  league_record = std::format("{}  ·  {}-{}-{}", table.played, table.won,
                              table.drawn, table.lost);
  league_goals = std::format("{}:{}", table.goals_for, table.goals_against);
  const auto& played = controller.getYouthResults();
  const std::size_t first =
      played.size() > RECENT_RESULTS ? played.size() - RECENT_RESULTS : 0;
  for (std::size_t i = first; i < played.size(); ++i)
  {
    const YouthResult& result = played[i];
    ResultLine line;
    line.date = Format::dayMonth(result.date);
    const auto opponent = controller.getTeamById(result.opponent_id);
    line.opponent = std::format(
        "{} ({})", opponent ? opponent->get().getName() : std::string("?"),
        LOC(result.home ? "YOUTH_HOME" : "YOUTH_AWAY"));
    line.score = std::format("{}-{}", result.goals_for, result.goals_against);
    if (result.goals_for > result.goals_against)
      line.outcome = UI::Outcome::WIN;
    else if (result.goals_for == result.goals_against)
      line.outcome = UI::Outcome::DRAW;
    else
      line.outcome = UI::Outcome::LOSS;
    form.push_back(line.outcome);
    results.push_back(std::move(line));
  }

  // Board requests.
  constexpr std::array<AcademyUpgrade, 2> KINDS = {AcademyUpgrade::Facilities,
                                                   AcademyUpgrade::Recruitment};
  for (std::size_t i = 0; i < KINDS.size(); ++i)
  {
    UpgradeLine& line = upgrades[i];
    line.quote = controller.getAcademyUpgradeQuote(KINDS[i]);
    line.level = std::to_string(line.quote.current);
    line.cost = Format::money(line.quote.cost);
    line.time =
        formatLocalized("YOUTH_DAYS", {std::to_string(line.quote.days)});
    line.target = std::to_string(line.quote.target);
    line.ready.clear();
    line.progress = 0.0f;
    if (line.quote.running_done_day > 0)
    {
      const std::int32_t today = dayOrdinal(cached_date);
      const auto total = static_cast<float>(std::max(
          1, line.quote.running_done_day - line.quote.running_start_day));
      line.progress = std::clamp(
          static_cast<float>(today - line.quote.running_start_day) / total,
          0.0f, 1.0f);
      line.ready = formatLocalized(
          "YOUTH_PROJECT_READY",
          {Format::date(dateOfDay(cached_date, line.quote.running_done_day))});
    }
  }
}

void YouthScene::openTab(Tab tab)
{
  requested_tab = tab;
  tab_pending = true;
  selected = 0;
}

void YouthScene::renderContent()
{
  const GameController& controller = guiView->getController();
  if (!controller.hasSelectedTeam()) return;
  if (!(cached_date == controller.getCurrentDate())) refresh();
  UI::pageHeader(LOC("YOUTH_TITLE"), LOC("YOUTH_SUBTITLE"));

  if (ImGui::BeginTabBar("YouthTabs"))
  {
    const auto tab = [this](Tab which, const char* label, auto&& body)
    {
      const ImGuiTabItemFlags flags = tab_pending && requested_tab == which
                                          ? ImGuiTabItemFlags_SetSelected
                                          : ImGuiTabItemFlags_None;
      if (ImGui::BeginTabItem(label, nullptr, flags))
      {
        body();
        ImGui::EndTabItem();
      }
    };
    tab(Tab::OVERVIEW, LOC("YOUTH_TAB_OVERVIEW"), [this] { renderOverview(); });
    tab(Tab::SQUAD, LOC("YOUTH_TAB_SQUAD"), [this] { renderSquad(); });
    tab(Tab::INTAKE, intake_tab_label.c_str(), [this] { renderIntake(); });
    tab(Tab::DEVELOPMENT, LOC("YOUTH_TAB_DEVELOPMENT"),
        [this] { renderDevelopment(); });
    tab_pending = false;
    ImGui::EndTabBar();
  }
  renderConfirm();
  runPending();
}

// ---------------------------------------------------------------- Overview

void YouthScene::renderOverview()
{
  const Theme::Palette& palette = Theme::palette();
  UI::TileRow tiles(4);
  const float tile = tiles.width();
  const std::string facilities = std::to_string(overview.ratings.facilities);
  tiles.next();
  UI::statTile("facilities", LOC("YOUTH_TILE_FACILITIES"), facilities.c_str(),
               LOC("YOUTH_TILE_FACILITIES_NOTE"),
               Theme::ratingColor(overview.ratings.facilities), tile);
  const std::string recruitment = std::to_string(overview.ratings.recruitment);
  tiles.next();
  UI::statTile("recruitment", LOC("YOUTH_TILE_RECRUITMENT"),
               recruitment.c_str(), LOC("YOUTH_TILE_RECRUITMENT_NOTE"),
               Theme::ratingColor(overview.ratings.recruitment), tile);
  const std::string coaching = std::to_string(overview.ratings.junior_coaching);
  tiles.next();
  UI::statTile("coaching", LOC("YOUTH_TILE_COACHING"), coaching.c_str(),
               LOC("YOUTH_TILE_COACHING_NOTE"),
               Theme::ratingColor(overview.ratings.junior_coaching), tile);
  tiles.next();
  UI::statTile("head", LOC("YOUTH_TILE_HEAD"), head_rating.c_str(),
               overview.head.id != 0 ? overview.head.name.c_str()
                                     : LOC("YOUTH_HEAD_VACANT"),
               overview.head.id != 0 ? Theme::ratingColor(overview.ratings.head)
                                     : palette.faint,
               tile);

  // One scroll surface: cards grow with their content and the page scrolls.
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool twoColumns = available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
  const float left =
      twoColumns ? std::floor((available - gap) * 0.5f) : available;
  const float right = twoColumns ? available - gap - left : available;
  ImGui::BeginGroup();
  renderIntakeCard(left);
  renderLeagueCard(left);
  ImGui::EndGroup();
  if (twoColumns) ImGui::SameLine();
  ImGui::BeginGroup();
  renderHeadCard(right);
  renderBoardCard(right);
  ImGui::EndGroup();
}

void YouthScene::renderIntakeCard(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("youth_intake_card", LOC("YOUTH_CARD_INTAKE"), width);
  ImGui::PushTextWrapPos(0.0f);
  if (overview.candidates > 0)
  {
    ImGui::TextUnformatted(deadline_line.c_str());
    ImGui::PopTextWrapPos();
    if (UI::primaryButton(LOC("YOUTH_REVIEW_INTAKE"))) openTab(Tab::INTAKE);
    UI::endCard();
    return;
  }
  ImGui::TextUnformatted(intake_line.c_str());
  if (overview.preview_ready)
  {
    UI::badge(LOC(YouthModel::qualityKey(overview.preview.quality)),
              qualityColor(overview.preview.quality));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(palette.muted, "%s", LOC("YOUTH_PREVIEW_VERDICT"));
  }
  ImGui::TextColored(palette.muted, "%s", preview_line.c_str());
  ImGui::PopTextWrapPos();
  UI::endCard();
}

void YouthScene::renderHeadCard(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("youth_head_card", LOC("YOUTH_CARD_HEAD"), width);
  const float keyWidth = KEY_WIDTH * Theme::scale();
  if (overview.head.id == 0)
  {
    UI::emptyState(LOC("YOUTH_HEAD_VACANT"), LOC("YOUTH_HEAD_VACANT_BODY"));
    if (UI::secondaryButton(LOC("YOUTH_OPEN_STAFF")))
      Navigation::open(guiView, NavSection::STAFF);
    UI::endCard();
    return;
  }
  {
    Theme::ScopedText title(Theme::Text::TITLE);
    ImGui::TextUnformatted(overview.head.name.c_str());
  }
  ImGui::TextColored(palette.muted, "%s", LOC(overview.head.style_key));
  UI::attributeBar(LOC("YOUTH_HEAD_ABILITY"),
                   static_cast<float>(overview.head.ability), keyWidth);
  UI::attributeBar(LOC("YOUTH_HEAD_JUDGING"),
                   static_cast<float>(overview.head.judging), keyWidth);
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextColored(palette.faint, "%s", LOC("YOUTH_HEAD_NOTE"));
  ImGui::PopTextWrapPos();
  UI::endCard();
}

void YouthScene::renderBoardCard(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("youth_board_card", LOC("YOUTH_CARD_BOARD"), width);
  const float keyWidth = KEY_WIDTH * Theme::scale();
  constexpr std::array<const char*, 2> LABELS = {"YOUTH_UPGRADE_FACILITIES",
                                                 "YOUTH_UPGRADE_RECRUITMENT"};
  for (std::size_t i = 0; i < upgrades.size(); ++i)
  {
    const UpgradeLine& line = upgrades[i];
    ImGui::PushID(static_cast<int>(i));
    UI::sectionLabel(LOC(LABELS[i]));
    UI::meter(LOC("YOUTH_LEVEL"),
              static_cast<float>(line.quote.current) / 100.0f, keyWidth,
              Theme::ratingColor(line.quote.current), line.level.c_str());
    if (!line.ready.empty())
    {
      UI::meter(LOC("YOUTH_PROJECT_PROGRESS"), line.progress, keyWidth,
                palette.info, line.target.c_str());
      ImGui::TextColored(palette.muted, "%s", line.ready.c_str());
    }
    else if (line.quote.verdict != UpgradeRequestResult::AtMaximum)
    {
      UI::keyValue(LOC("YOUTH_UPGRADE_TARGET"), line.target.c_str(), keyWidth);
      UI::keyValue(LOC("YOUTH_UPGRADE_COST"), line.cost.c_str(), keyWidth);
      UI::keyValue(LOC("YOUTH_UPGRADE_TIME"), line.time.c_str(), keyWidth);
    }
    const UpgradeRequestResult verdict = line.quote.verdict;
    const bool blocked = verdict == UpgradeRequestResult::InProgress ||
                         verdict == UpgradeRequestResult::AtMaximum ||
                         verdict == UpgradeRequestResult::TooSoon ||
                         verdict == UpgradeRequestResult::NoClub;
    ImGui::BeginDisabled(blocked);
    if (UI::secondaryButton(LOC("YOUTH_ASK_BOARD")))
      pending = {PendingAction::Kind::UPGRADE, 0, line.quote.kind};
    ImGui::EndDisabled();
    if (blocked && line.ready.empty())
    {
      ImGui::SameLine();
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(palette.faint, "%s",
                         LOC(YouthModel::requestResultKey(verdict)));
    }
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
    ImGui::PopID();
  }
  UI::endCard();
}

void YouthScene::renderLeagueCard(float width)
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("youth_league_card", LOC("YOUTH_CARD_LEAGUE"), width);
  const float keyWidth = KEY_WIDTH * Theme::scale();
  ImGui::TextUnformatted(league_line.c_str());
  if (overview.table.played > 0)
  {
    UI::keyValue(LOC("YOUTH_LEAGUE_RECORD"), league_record.c_str(), keyWidth);
    UI::keyValue(LOC("YOUTH_LEAGUE_GOALS"), league_goals.c_str(), keyWidth);
  }
  if (!form.empty())
  {
    ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * Theme::scale()));
    UI::formStrip(form);
    for (auto it = results.rbegin(); it != results.rend(); ++it)
    {
      ImGui::TextColored(palette.faint, "%s", it->date.c_str());
      ImGui::SameLine(keyWidth * 0.45f);
      ImGui::TextUnformatted(it->score.c_str());
      ImGui::SameLine();
      UI::textFitted(it->opponent, ImGui::GetContentRegionAvail().x,
                     palette.muted);
    }
  }
  UI::endCard();
}

// ---------------------------------------------------------------- Lists

void YouthScene::renderSquad()
{
  UI::beginAutoHeightCard("youth_squad_card", squad_title.c_str());
  if (squad_rows.empty())
    UI::emptyState(LOC("YOUTH_SQUAD_EMPTY_TITLE"),
                   LOC("YOUTH_SQUAD_EMPTY_BODY"));
  else
    renderTable("squad", squad_rows, ListKind::SQUAD);
  UI::endCard();

  UI::beginAutoHeightCard("youth_eligible_card", LOC("YOUTH_CARD_ELIGIBLE"));
  if (eligible_rows.empty())
    UI::emptyState(LOC("YOUTH_ELIGIBLE_EMPTY_TITLE"),
                   LOC("YOUTH_ELIGIBLE_EMPTY_BODY"));
  else
    renderTable("eligible", eligible_rows, ListKind::ELIGIBLE);
  UI::endCard();
}

void YouthScene::renderIntake()
{
  const Theme::Palette& palette = Theme::palette();
  UI::beginAutoHeightCard("youth_candidates_card",
                          LOC("YOUTH_CARD_CANDIDATES"));
  if (intake_rows.empty())
  {
    UI::emptyState(LOC("YOUTH_INTAKE_EMPTY_TITLE"), intake_line.c_str());
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(palette.muted, "%s", preview_line.c_str());
    ImGui::PopTextWrapPos();
  }
  else
  {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(deadline_line.c_str());
    ImGui::TextColored(palette.faint, "%s", LOC("YOUTH_INTAKE_HINT"));
    ImGui::PopTextWrapPos();
    renderTable("candidates", intake_rows, ListKind::INTAKE);
  }
  UI::endCard();
}

void YouthScene::renderTable(const char* id, const std::vector<Row>& rows,
                             ListKind kind)
{
  const Theme::Palette& palette = Theme::palette();
  const std::span<const ColumnSpec> specs =
      columnsFor(static_cast<int>(std::to_underlying(kind)));
  std::array<UI::Column, SQUAD_COLUMNS.size()> localized{};
  for (std::size_t i = 0; i < specs.size(); ++i)
  {
    localized[i] = specs[i].column;
    localized[i].label = LOC(specs[i].column.label);
  }
  const std::span<const UI::Column> columns(localized.data(), specs.size());
  const UI::ColumnMask mask =
      UI::fitColumns(columns, ImGui::GetContentRegionAvail().x);
  const ImGuiTableFlags flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH;
  const bool development = kind == ListKind::DEVELOPMENT;

  ImGui::PushID(id);
  // The selected row opens a detail strip right under it: the table is split
  // there and continues with the same id (shared column widths).
  UI::TableHeader header = UI::TableHeader::STATIC;
  std::size_t first = 0;
  using enum Cell;
  while (first < rows.size())
  {
    if (!UI::beginResponsiveTable("rows", columns, mask, flags, header)) break;
    header = UI::TableHeader::NONE;
    std::size_t index = first;
    const Row* opened = nullptr;
    while (index < rows.size() && opened == nullptr)
    {
      const Row& row = rows[index++];
      const GameController::YouthPlayerView& view = row.view;
      ImGui::PushID(static_cast<int>(view.id));
      ImGui::TableNextRow(ImGuiTableRowFlags_None,
                          UI::buttonHeight(UI::ButtonSize::COMPACT));
      const bool isSelected =
          development ? chart_player == view.id : selected == view.id;
      for (std::size_t column = 0; column < specs.size(); ++column)
      {
        if (!UI::cell(mask, static_cast<int>(column))) continue;
        switch (specs[column].cell)
        {
          case NAME:
          {
            // The row selects (details or chart); the name opens the profile.
            const float x = ImGui::GetCursorPosX();
            if (ImGui::Selectable("##row", isSelected,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowOverlap))
            {
              if (development)
                chart_player = view.id;
              else
                selected = isSelected ? 0 : view.id;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
              ImGui::SetTooltip("%s", LOC(development ? "YOUTH_ROW_CHART_HINT"
                                                      : "YOUTH_ROW_HINT"));
            ImGui::SameLine();
            ImGui::SetCursorPosX(x);
            if (UI::link(view.name.c_str(), "name"))
              Navigation::openPlayer(guiView, view.id);
            break;
          }
          case POS:
            ImGui::TextColored(palette.muted, "%s", row.role.c_str());
            break;
          case AGE:
            ImGui::Text("%d", view.age);
            break;
          case ABILITY:
            ImGui::TextUnformatted(row.ability.c_str());
            break;
          case POTENTIAL:
            ImGui::TextColored(
                Theme::ratingColor(0.5f * (view.estimate.potential_low +
                                           view.estimate.potential_high)),
                "%s", row.potential.c_str());
            break;
          case CONTRACT:
            ImGui::TextColored(
                view.contract_years <= 1 ? palette.warning : palette.text, "%s",
                row.contract.c_str());
            break;
          case APPS:
            ImGui::Text("%d", view.appearances);
            break;
          case RATING:
            ImGui::TextUnformatted(row.rating.c_str());
            break;
          case PERSONALITY:
            UI::textFitted(LOC(view.personality_key),
                           ImGui::GetContentRegionAvail().x, palette.muted);
            break;
          case HOMEGROWN:
            if (view.homegrown)
            {
              UI::badge(LOC("YOUTH_HOMEGROWN_BADGE"), palette.info);
              if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", LOC("YOUTH_HOMEGROWN_HINT"));
            }
            break;
          case NATION:
            UI::textFitted(row.nationality, ImGui::GetContentRegionAvail().x,
                           palette.muted);
            break;
          case HEIGHT:
            ImGui::TextUnformatted(row.height.c_str());
            break;
          case GROWTH:
          {
            ImVec4 growthColor = palette.muted;
            if (row.growth > 0.05f)
              growthColor = palette.positive;
            else if (row.growth < -0.05f)
              growthColor = palette.negative;
            ImGui::TextColored(growthColor, "%s", row.growth_text.c_str());
            break;
          }
        }
      }
      ImGui::PopID();
      if (!development && selected == view.id) opened = &row;
    }
    ImGui::EndTable();
    if (opened != nullptr) renderDetail(*opened, kind);
    first = index;
  }
  ImGui::PopID();
}

void YouthScene::renderDetail(const Row& row, ListKind kind)
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const GameController::YouthPlayerView& view = row.view;
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  drawList->ChannelsSplit(2);
  drawList->ChannelsSetCurrent(1);
  const ImVec2 start = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;
  const float pad = Theme::Space::M * scale;
  ImGui::SetCursorScreenPos(ImVec2(start.x + pad, start.y + pad));
  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 2.0f * pad);
  ImGui::TextColored(palette.muted, "%s", row.summary.c_str());
  if (kind != ListKind::INTAKE) ImGui::TextUnformatted(row.terms.c_str());
  ImGui::PopTextWrapPos();
  ImGui::Dummy(ImVec2(0.0f, Theme::Space::XS * scale));
  ImGui::PushID(static_cast<int>(view.id));

  const auto button = [](const char* label, bool primary)
  {
    UI::sameLineIfFits(UI::buttonWidth(label));
    return primary ? UI::primaryButton(label) : UI::secondaryButton(label);
  };
  if (UI::secondaryButton(LOC("YOUTH_VIEW_PROFILE")))
    Navigation::openPlayer(guiView, view.id);
  switch (kind)
  {
    case ListKind::INTAKE:
      if (button(row.offer_label.c_str(), true))
        pending = {PendingAction::Kind::SIGN, view.id};
      UI::sameLineIfFits(UI::buttonWidth(LOC("YOUTH_LET_GO")));
      if (UI::dangerButton(LOC("YOUTH_LET_GO")))
      {
        confirm_player = view.id;
        confirm_release = false;
        confirm_text = formatLocalized("YOUTH_LET_GO_BODY", {view.name});
        confirm_requested = true;
      }
      break;
    case ListKind::SQUAD:
    {
      if (view.offer == YouthContract::Professional &&
          button(row.offer_label.c_str(), true))
        pending = {PendingAction::Kind::PROFESSIONAL, view.id};
      if (button(LOC("YOUTH_PROMOTE"), false))
        pending = {PendingAction::Kind::PROMOTE, view.id};
      if (button(LOC("YOUTH_TO_U21"), false))
        pending = {PendingAction::Kind::TO_U21, view.id};
      if (button(LOC(view.loan_listed ? "YOUTH_LOAN_WITHDRAW"
                                      : "YOUTH_LOAN_OFFER"),
                 false))
        pending = {PendingAction::Kind::LOAN, view.id};
      UI::sameLineIfFits(UI::buttonWidth(LOC("YOUTH_RELEASE")));
      if (UI::dangerButton(LOC("YOUTH_RELEASE")))
      {
        confirm_player = view.id;
        confirm_release = true;
        confirm_text = formatLocalized(
            "YOUTH_RELEASE_BODY",
            {view.name,
             Format::money(guiView->getController().getReleaseCost(view.id))});
        confirm_requested = true;
      }
      if (view.offer == YouthContract::None &&
          view.contract == YouthContract::Scholarship)
      {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width - 2.0f * pad);
        ImGui::TextColored(palette.faint, "%s", LOC("YOUTH_PRO_AGE_HINT"));
        ImGui::PopTextWrapPos();
      }
      break;
    }
    case ListKind::ELIGIBLE:
      if (button(LOC("YOUTH_DEMOTE"), true))
        pending = {PendingAction::Kind::DEMOTE, view.id};
      break;
    case ListKind::DEVELOPMENT:
      break;
  }
  ImGui::PopID();
  ImGui::EndGroup();
  const float bottom = ImGui::GetItemRectMax().y + pad;
  drawList->ChannelsSetCurrent(0);
  drawList->AddRectFilled(start, ImVec2(start.x + width, bottom),
                          Theme::toU32(palette.raised), 4.0f * scale);
  drawList->AddRectFilled(start, ImVec2(start.x + 3.0f * scale, bottom),
                          Theme::toU32(palette.accent), 2.0f * scale);
  drawList->ChannelsMerge();
  ImGui::SetCursorScreenPos(ImVec2(start.x, bottom));
  ImGui::Dummy(ImVec2(width, Theme::Space::XS * scale));
}

// ---------------------------------------------------------------- Development

void YouthScene::renderDevelopment()
{
  const float available = ImGui::GetContentRegionAvail().x;
  const float gap = ImGui::GetStyle().ItemSpacing.x;
  const bool twoColumns = available >= TWO_COLUMN_MIN_WIDTH * Theme::scale();
  const float left =
      twoColumns ? std::floor((available - gap) * 0.5f) : available;
  const float right = twoColumns ? available - gap - left : available;

  UI::beginAutoHeightCard("youth_development_list", LOC("YOUTH_CARD_PROGRESS"),
                          left);
  if (development_rows.empty())
    UI::emptyState(LOC("YOUTH_SQUAD_EMPTY_TITLE"),
                   LOC("YOUTH_SQUAD_EMPTY_BODY"));
  else
    renderTable("development", development_rows, ListKind::DEVELOPMENT);
  UI::endCard();
  if (twoColumns) ImGui::SameLine();

  ImGui::BeginGroup();
  const auto chosen =
      std::ranges::find(development_rows, chart_player,
                        [](const Row& row) { return row.view.id; });
  UI::beginAutoHeightCard("youth_development_chart",
                          chosen != development_rows.end()
                              ? chosen->view.name.c_str()
                              : LOC("YOUTH_CARD_CHART"),
                          right);
  if (chosen == development_rows.end())
    UI::emptyState(LOC("YOUTH_CHART_EMPTY_TITLE"),
                   LOC("YOUTH_CHART_EMPTY_BODY"));
  else
    renderChart(*chosen, ImGui::GetContentRegionAvail().x);
  UI::endCard();

  UI::beginAutoHeightCard("youth_improvers", LOC("YOUTH_CARD_IMPROVERS"),
                          right);
  if (improvers.empty())
    UI::emptyState(LOC("YOUTH_CHART_EMPTY_TITLE"),
                   LOC("YOUTH_CHART_EMPTY_BODY"));
  else
    UI::barChart("improvers", improvers, ImGui::GetContentRegionAvail().x,
                 KEY_WIDTH * Theme::scale());
  UI::endCard();
  ImGui::EndGroup();
}

void YouthScene::renderChart(const Row& row, float width) const
{
  const Theme::Palette& palette = Theme::palette();
  const float scale = Theme::scale();
  const auto& points = row.view.progress;
  ImGui::TextColored(palette.muted, "%s", row.chart_caption.c_str());
  if (points.size() < 2)
  {
    UI::emptyState(LOC("YOUTH_CHART_EMPTY_TITLE"),
                   LOC("YOUTH_CHART_EMPTY_BODY"));
    return;
  }
  const float height = CHART_HEIGHT * scale;
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImGui::Dummy(ImVec2(width, height));
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const float axis = ImGui::CalcTextSize("99").x + Theme::Space::S * scale;
  const float line_height = ImGui::GetTextLineHeight();
  const ImVec2 plot_min(origin.x + axis, origin.y + Theme::Space::XS * scale);
  const ImVec2 plot_max(
      origin.x + width - Theme::Space::XS * scale,
      origin.y + height - line_height - Theme::Space::XS * scale);

  // Scale: the ability line and the scouted potential range.
  float low = row.view.estimate.potential_low;
  float high = row.view.estimate.potential_high;
  for (const YouthProgressPoint& point : points)
  {
    low = std::min(low, point.overall);
    high = std::max(high, point.overall);
  }
  low = std::floor(low / 5.0f) * 5.0f - 5.0f;
  high = std::ceil(high / 5.0f) * 5.0f + 5.0f;
  const auto y_of = [&](float value)
  {
    return plot_max.y -
           (value - low) / (high - low) * (plot_max.y - plot_min.y);
  };
  const std::int32_t first_day = points.front().day;
  const auto span_days =
      static_cast<float>(std::max(1, points.back().day - first_day));
  const auto x_of = [&](std::int32_t day)
  {
    return plot_min.x + static_cast<float>(day - first_day) / span_days *
                            (plot_max.x - plot_min.x);
  };

  drawList->AddRectFilled(
      ImVec2(plot_min.x, y_of(row.view.estimate.potential_high)),
      ImVec2(plot_max.x, y_of(row.view.estimate.potential_low)),
      Theme::toU32(palette.info, 0.12f));
  for (int step = 0; step <= 2; ++step)
  {
    const float value = low + (high - low) * static_cast<float>(step) / 2.0f;
    const float y = y_of(value);
    drawList->AddLine(ImVec2(plot_min.x, y), ImVec2(plot_max.x, y),
                      Theme::toU32(palette.border));
    std::array<char, 8> label{};
    const auto formatted = std::format_to_n(
        label.data(), static_cast<std::ptrdiff_t>(label.size() - 1), "{:.0f}",
        static_cast<double>(value));
    label[std::min(static_cast<std::size_t>(formatted.size),
                   label.size() - 1)] = '\0';
    drawList->AddText(ImVec2(origin.x, y - line_height * 0.5f),
                      Theme::toU32(palette.faint), label.data());
  }
  for (std::size_t i = 1; i < points.size(); ++i)
    drawList->AddLine(
        ImVec2(x_of(points[i - 1].day), y_of(points[i - 1].overall)),
        ImVec2(x_of(points[i].day), y_of(points[i].overall)),
        Theme::toU32(palette.text), 2.0f * scale);
  for (const YouthProgressPoint& point : points)
    drawList->AddCircleFilled(ImVec2(x_of(point.day), y_of(point.overall)),
                              3.0f * scale, Theme::toU32(palette.text));
  drawList->AddText(ImVec2(plot_min.x, plot_max.y + Theme::Space::XS * scale),
                    Theme::toU32(palette.faint), row.chart_from.c_str());
  drawList->AddText(
      ImVec2(plot_max.x - ImGui::CalcTextSize(row.chart_to.c_str()).x,
             plot_max.y + Theme::Space::XS * scale),
      Theme::toU32(palette.faint), row.chart_to.c_str());
  ImGui::TextColored(palette.faint, "%s", LOC("YOUTH_CHART_LEGEND"));
}

// ---------------------------------------------------------------- Actions

void YouthScene::renderConfirm()
{
  if (confirm_requested)
  {
    confirm_requested = false;
    ImGui::OpenPopup(CONFIRM_POPUP_ID);
  }
  const char* action = LOC(confirm_release ? "YOUTH_RELEASE" : "YOUTH_LET_GO");
  const UI::DialogResult result =
      UI::confirmDialog(CONFIRM_POPUP_ID, action, confirm_text.c_str(), action,
                        LOC("SETTINGS_CANCEL"));
  if (result == UI::DialogResult::CONFIRM && confirm_player != 0)
  {
    pending = {confirm_release ? PendingAction::Kind::RELEASE
                               : PendingAction::Kind::LET_GO,
               confirm_player};
    confirm_player = 0;
  }
  else if (result == UI::DialogResult::CANCEL)
  {
    confirm_player = 0;
  }
}

void YouthScene::runPending()
{
  if (pending.kind == PendingAction::Kind::NONE) return;
  GameController& controller = guiView->getController();
  const PendingAction action = pending;
  pending = {};
  std::string name;
  if (const auto data = controller.getGameData())
    if (const auto player = data->getPlayer(action.id))
      name = player->get().getName();
  const auto report = [&](YouthActionResult result, const char* success_key)
  {
    if (result == YouthActionResult::Ok)
      showToast(formatLocalized(success_key, {name}));
    else
      showToast(LOC(YouthModel::actionResultKey(result)), true);
  };
  switch (action.kind)
  {
    case PendingAction::Kind::SIGN:
      report(controller.signYouthCandidate(action.id), "YOUTH_TOAST_SIGNED");
      selected = 0;
      break;
    case PendingAction::Kind::LET_GO:
      report(controller.releaseYouthCandidate(action.id), "YOUTH_TOAST_LET_GO");
      selected = 0;
      break;
    case PendingAction::Kind::PROFESSIONAL:
      report(controller.offerYouthProfessionalContract(action.id),
             "YOUTH_TOAST_PROFESSIONAL");
      break;
    case PendingAction::Kind::PROMOTE:
      report(controller.promoteYouthPlayer(action.id), "YOUTH_TOAST_PROMOTED");
      selected = 0;
      break;
    case PendingAction::Kind::DEMOTE:
      report(controller.moveToYouthSquad(action.id), "YOUTH_TOAST_DEMOTED");
      selected = 0;
      break;
    case PendingAction::Kind::TO_U21:
      report(controller.moveToReserves(action.id), "U21_TOAST_SENT_DOWN");
      selected = 0;
      break;
    case PendingAction::Kind::LOAN:
    {
      const auto row = std::ranges::find(
          squad_rows, action.id, [](const Row& line) { return line.view.id; });
      if (const bool listed = row != squad_rows.end() && row->view.loan_listed;
          controller.setLoanListed(action.id, !listed))
        showToast(formatLocalized(
            listed ? "YOUTH_TOAST_LOAN_WITHDRAWN" : "YOUTH_TOAST_LOAN_OFFERED",
            {name}));
      else
        showToast(LOC("YOUTH_ACTION_NOT_ALLOWED"), true);
      break;
    }
    case PendingAction::Kind::RELEASE:
      if (controller.releasePlayer(action.id))
        showToast(formatLocalized("YOUTH_TOAST_RELEASED", {name}));
      else
        showToast(LOC("YOUTH_ACTION_NOT_ALLOWED"), true);
      selected = 0;
      break;
    case PendingAction::Kind::UPGRADE:
    {
      const UpgradeRequestResult result =
          controller.requestAcademyUpgrade(action.upgrade);
      showToast(LOC(YouthModel::requestResultKey(result)),
                result != UpgradeRequestResult::Approved);
      break;
    }
    case PendingAction::Kind::NONE:
      break;
  }
  refresh();
}
